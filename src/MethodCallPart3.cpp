#include "CNumeric.h"
#include "AsciiCtype.h"
#include "MethodCallSegment.h"

// Segment 3 of the method-dispatch chain, split out of methodCallInner.
//
// It is a SEGMENT, not a category: the chain is ORDER-SENSITIVE — an earlier arm
// shadows a later one — so these arms run after segment 2 and before the tail segment. Add a new arm
// where its priority belongs, not where it reads nicely.
//
// std::optional lets every arm keep its original `return X;` verbatim, so a
// `return` inside a nested lambda still means what it always did. nullopt =
// "not handled here".
namespace rakupp {

std::optional<Value> Interpreter::methodCallPart3(const Value& inv, const MName& m, ValueList& args,
                                     const std::vector<ExprPtr>* rwArgs) {
    auto a0 = [&]() -> Value { return args.empty() ? Value::any() : args[0]; };
    if (inv.t == VT::Hash && !inv.hashKind.empty()) {
        bool isSet = inv.hashKind.find("Set") == 0;
        if (m == "default") return isSet ? Value::boolean(false) : Value::integer(0);
        if (m == "total") { // Mix weights may be fractional — sum EXACTLY through
            // the numeric tower (Rat stays Rat). A double accumulator's rounding
            // depended on iteration order: 0.3 + 0.5 + … printed 13.6 in one
            // order and 13.599999999999998 in another. Rakudo is exact here.
            if (isSet) return Value::integer((long long)inv.hash()->size());
            Value t = Value::integer(0);
            for (auto& kv : *inv.hash()) t = applyArith("+", t, kv.second);
            return t;
        }
        if (m == "elems") return Value::integer((long long)inv.hash()->size());
    }

    // numeric -> Complex coercion
    if (m == "Complex" && (inv.t == VT::Int || inv.t == VT::Num || inv.t == VT::Rat || inv.t == VT::Bool))
        return Value::complex(inv.toNum(), 0);
    // A STRING that spells a complex number answers the Complex methods through
    // it — `"6+8i".abs` is 10, not the real part. (A non-numeric string gives the
    // usual Failure.)
    if (inv.t == VT::Str && inv.hashKind.empty() && !inv.isAllomorph() &&
        (m == "Complex" || m == "conj" || m == "re" || m == "im" ||
         m == "abs" || m == "polar" || m == "sqrt")) {
        Value nv = numifyStrFailure(inv.s);
        if (nv.t == VT::Hash && nv.hashKind == "Failure") return nv;
        if (nv.t == VT::Complex || m == "Complex")
            return methodCall(nv.t == VT::Complex ? nv : Value::complex(nv.toNum(), 0), m, args, rwArgs);
    }
    // Complex
    if (inv.t == VT::Complex) {
        std::complex<double> z(inv.n, inv.im());
        if (m == "re" || m == "Real") return Value::number(inv.n);
        if (m == "im") return Value::number(inv.im());
        if (m == "reals") { Value o = Value::array({Value::number(inv.n), Value::number(inv.im())});
                            o.isList = true; return o; } // a List, not an Array
        if (m == "abs" || m == "magnitude") return Value::number(std::abs(z));
        if (m == "conj") return Value::complex(inv.n, -inv.im());
        if (m == "sqrt") return complexSqrt(inv.n, inv.im());
        if (m == "exp") { auto r = std::exp(z); return Value::complex(r.real(), r.imag()); }
        if (m == "log") { // optional base argument: log(z) / log(base)
            auto r = std::log(z);
            if (!args.empty()) {
                const Value& b = args[0];
                r /= std::log(b.t == VT::Complex ? std::complex<double>(b.n, b.im())
                                                 : std::complex<double>(b.toNum(), 0.0));
            }
            return Value::complex(r.real(), r.imag());
        }
        if (m == "log10" || m == "log2") { // log to a fixed base stays Complex
            auto r = std::log(z) / std::log(std::complex<double>(m == "log10" ? 10.0 : 2.0, 0.0));
            return Value::complex(r.real(), r.imag());
        }
        for (const char* tm : {"sin","cos","tan","asin","acos","atan",
                               "sinh","cosh","tanh","asinh","acosh","atanh"})
            if (m == tm) { // complex trigonometry
                std::complex<double> r =
                    m == "sin" ? std::sin(z) : m == "cos" ? std::cos(z) : m == "tan" ? std::tan(z)
                  : m == "asin" ? std::asin(z) : m == "acos" ? std::acos(z) : m == "atan" ? std::atan(z)
                  : m == "sinh" ? std::sinh(z) : m == "cosh" ? std::cosh(z) : m == "tanh" ? std::tanh(z)
                  : m == "asinh" ? std::asinh(z) : m == "acosh" ? std::acosh(z) : std::atanh(z);
                return Value::complex(r.real(), r.imag());
            }
        // reciprocal trig (sec/cosec/cotan + hyperbolic + inverses) via 1/z forms
        {
            auto C = [&](std::complex<double> r) { return Value::complex(r.real(), r.imag()); };
            std::complex<double> one(1.0, 0.0);
            if (m == "sec")   return C(one / std::cos(z));
            if (m == "cosec" || m == "csc") return C(one / std::sin(z));
            if (m == "cotan" || m == "cot") return C(one / std::tan(z));
            if (m == "sech")  return C(one / std::cosh(z));
            if (m == "cosech" || m == "csch") return C(one / std::sinh(z));
            if (m == "cotanh" || m == "coth") return C(one / std::tanh(z));
            if (m == "asec")  return C(std::acos(one / z));
            if (m == "acosec" || m == "acsc") return C(std::asin(one / z));
            if (m == "acotan" || m == "acot") return C(std::atan(one / z));
            if (m == "asech") return C(std::acosh(one / z));
            if (m == "acosech" || m == "acsch") return C(std::asinh(one / z));
            if (m == "acotanh" || m == "acoth") return C(std::atanh(one / z));
        }
        if (m == "roots") { // the n-th roots of a Complex, same as roots($z, $n)
            auto it = builtins_.find("roots");
            if (it != builtins_.end()) { ValueList ra{inv, args.empty() ? Value::integer(1) : args[0]}; return it->second(*this, ra); }
        }
        if (m == "polar") return Value::array({Value::number(std::abs(z)), Value::number(std::arg(z))});
        if (m == "arg") return Value::number(std::arg(z));
        if (m == "Complex") return inv;
        if (m == "isNaN") return Value::boolean(std::isnan(inv.n) || std::isnan(inv.im()));
        if (m == "Str" || m == "gist" || m == "Stringy") return Value::str(inv.toStr());
        if (m == "raku") return Value::str("<" + inv.toStr() + ">");
        if (m == "Num" || m == "Real" || m == "Int") { if (inv.im() != 0) throw RakuError{Value::typeObj("X::Numeric::Real"), "Can not convert Complex with nonzero imaginary part"}; return m == "Int" ? Value::integer((long long)inv.n) : Value::number(inv.n); }
        // Complex.narrow is `self.im == 0 ?? self.re.narrow !! self` — it must RECURSE,
        // or (4.0+0i).narrow stops at the Num and never demotes to Int.
        if (m == "narrow") return inv.im() == 0 ? methodCall(Value::number(inv.n), "narrow", ValueList{}) : inv;
    }

    // Cool-style numeric coercion: an object that defines .Numeric/.Bridge (but
    // not the numeric method itself) acts as its numeric value here.
    if (inv.t == VT::Object && inv.obj()) {
        static const std::set<std::string> numMeths = {
            "abs","sqrt","sin","cos","tan","asin","acos","atan","atan2","sinh","cosh","tanh",
            "asinh","acosh","atanh","sec","cosec","csc","cotan","cot","asec","acosec","acsc",
            "sech","cosech","csch","cotanh","coth","asech","acosech","acsch","acotanh","acoth",
            "acotan","acot","floor","ceiling","round","truncate","sign","exp","log","log10","log2"};
        if (numMeths.count(m)) {
            // this arm coerces the invocant (Bridge/Numeric) and then re-dispatches
            // on the result — a rewrite, so it needs its own copy now that the
            // dispatch path passes the invocant by const reference
            Value invLocal = inv;
            Value& inv = invLocal;
            for (const char* acc : {"Bridge", "Numeric"}) {
                try { ValueList none; Value nv = methodCall(inv, acc, none);
                      if (nv.isNumeric() || nv.t == VT::Complex) { inv = nv; break; } } catch (...) {}
            }
            if (inv.t == VT::Complex) return methodCall(inv, m, args); // re-enter the Complex path
        }
    }
    // Every method below numifies its invocant, and a Str that is not a number
    // cannot be numified: `"a".floor` answered 0, `"a".chr` answered "\0" and
    // `"a".is-prime` answered False, all off the same silent zero. (`.succ` and
    // `.pred` are deliberately absent — on a Str they increment the STRING.)
    if ((inv.t == VT::Str || inv.t == VT::Match) && !inv.isAllomorph() && inv.hashKind.empty()) {
        // Only the ones that answered a WRONG VALUE. `.abs`, `.sqrt`, `.Rat`,
        // `.FatRat` and the other coercions already hand back a Failure carrying
        // the same X::Str::Numeric, and that soft form is the contract
        // t/regression/cool-round-and-numeric-failures.raku pins down.
        static const MNameSet8 kNumifiesInv = {
            "floor", "ceiling", "round", "truncate", "sign",
            "exp", "log", "log10", "log2", "chr", "is-prime"};
        if (kNumifiesInv.has(m)) numifyStrOrThrow(inv.toStr());
    }
    // numeric
    if (m == "abs") {
        if (inv.t == VT::Int && inv.big()) return Value::bigint(inv.big()->abs());
        if (inv.t == VT::Int) return Value::integer(std::llabs(inv.toInt()));
        if (inv.t == VT::Rat) { Value r = Value::rat(inv.ratN()->abs(), *inv.ratD()); r.fatRatM() = inv.fatRat(); return r; }
        return Value::number(std::fabs(inv.toNum()));
    }
    if (m == "sqrt") { double x = inv.toNum(); return (x < 0 && langRev_ >= 2) ? Value::complex(0, std::sqrt(-x)) : Value::number(std::sqrt(x)); }
    // 6.e: `42.pick(3)` is short for `(^42).pick(3)`, and likewise .roll. Before
    // it, Int has no pick/roll of its own and Any's one-item-list version answers
    // — `6.pick(3)` is `(6)` there, not three numbers below six.
    if (sixE() && inv.t == VT::Int && !inv.big() && (m == "pick" || m == "roll")) {
        Value upto = Value::range(0, inv.i, false, true); // ^$n
        if (args.empty()) { ValueList one{Value::integer(1)}; 
            Value got = methodCall(upto, m, one);
            return (got.t == VT::Array && got.arr() && got.arr()->size() == 1) ? (*got.arr())[0] : got;
        }
        return methodCall(upto, m, args);
    }
    // `(5..6).rand` — a Num drawn uniformly from a numeric range. The generic
    // arm below multiplies the invocant's toNum() by a random fraction, and a
    // Range's toNum() is 0, so EVERY numeric range answered a constant 0.
    // Statistics::Distributions generates its Uniform variates as
    // `($min .. $max).rand`, so that module quietly produced a column of zeros
    // here while its own suite — which checks counts and types — stayed green.
    //
    // Rakudo's four refusals come with it, and they are soft: `fail`, not
    // `throw`, so roast can write `throws-like ("a".."z").rand, …` and have the
    // argument survive being built. The sentinel endpoints are tested before
    // .min/.max because an open-BOTTOM range answers LLONG_MIN.
    if (m == "rand" && inv.t == VT::Range) {
        auto softly = [](const char* type, const std::string& msg) {
            Value f = Value::makeHash(); f.hashKind = "Failure";
            (*f.hash())["exception"] = Value::typeObj(type);
            (*f.hash())["message"]   = Value::str(msg);
            return f;
        };
        static const char* kBadEnds = "X::Range::Rand::InvalidEndpoints";
        if (inv.rTo() >= 9000000000000000000LL || inv.rFrom() <= -9000000000000000000LL)
            return softly(kBadEnds, "Impossible to get a random number from an infinite range");
        ValueList none;
        Value loV = methodCall(inv, "min", none), hiV = methodCall(inv, "max", none);
        if (!loV.isNumeric() || !hiV.isNumeric())
            return softly("X::AdHoc",
                          "Can only get a random value on Real values, did you mean .pick?");
        double lo = loV.toNum(), hi = hiV.toNum();
        if (std::isinf(lo) || std::isinf(hi))
            return softly(kBadEnds, "Impossible to get a random number from an infinite range");
        if (lo == hi)
            return softly(kBadEnds,
                "Impossible to generate random numbers for a range where endpoints are equal");
        if (lo > hi) {
            // Rakudo's message for a descending range carries its own hint, and
            // the hint names the endpoints both ways round.
            std::string a = loV.gist(), b = hiV.gist();
            return softly(kBadEnds,
                "Impossible to get a random number from range containing no values.\n"
                "The sequence (...) operator supports descension between " + a + " and " + b + ",\n"
                "but for a random number between " + a + " and " + b + ", (" + b + ".." + a + ").rand is\n"
                "likely to be functionally equivalent to what was meant by (" + a + ".." + b + ").rand");
        }
        // randDouble() is [0, 1), so the top endpoint never comes up however it
        // is written; only an excluded BOTTOM one can, and it is redrawn.
        double v = lo + (hi - lo) * randDouble();
        while (inv.rExFrom() && v == lo) v = lo + (hi - lo) * randDouble();
        return Value::number(v);
    }
    if (m == "rand") return Value::number(inv.toNum() * randDouble()); // $n.rand — Num in [0, $n)
    if (m == "base" && !args.empty() && (inv.t == VT::Int || inv.t == VT::Bool)) { // Int -> string in base 2..36
        long long b = args[0].toInt(); if (b < 2) b = 2; if (b > 36) b = 36;
        static const char* BD = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        // a BIG integer digits out by repeated division — toInt() would truncate
        if (inv.big() && !inv.big()->fitsLL()) {
            BigInt n = inv.big()->abs(), base((long long)b), q, r;
            std::string d;
            while (!n.isZero()) { BigInt::divmod(n, base, q, r); d = std::string(1, BD[r.fitsLL() ? r.toLL() : 0]) + d; n = q; }
            if (d.empty()) d = "0";
            return Value::str(inv.big()->sign < 0 ? "-" + d : d);
        }
        long long n = inv.toInt();
        if (n == 0) return Value::str("0");
        bool neg = n < 0; unsigned long long u = neg ? -(unsigned long long)n : (unsigned long long)n;
        std::string s;
        while (u) { s = std::string(1, BD[u % b]) + s; u /= b; }
        return Value::str(neg ? "-" + s : s);
    }
    if (m == "polymod" && (inv.t == VT::Num || inv.t == VT::Rat)) {
        // non-integer polymod stays in Value arithmetic (Rat exactness, Num):
        // v % d is pushed, v becomes (v - mod) / d; a lazy list stops at v == 0
        Value out = Value::array(); out.isList = true;
        bool lazy = false; ValueList fin;
        for (auto& a : args) {
            if (a.t == VT::Array && a.ext() &&
                std::static_pointer_cast<LazySeqState>(a.ext())->infinite) { lazy = true; break; }
            if (a.t == VT::Array && a.b) lazy = true; // `lazy 2, 3`
            for (auto& d : a.flatten()) fin.push_back(d);
        }
        Value v = inv;
        for (size_t i = 0; ; i++) {
            bool have = i < fin.size();
            if (lazy) {
                if (!v.truthy()) break;
                if (!have || fin[i].toNum() == 1.0) { out.arr()->push_back(v); break; }
            }
            else if (!have) { out.arr()->push_back(v); break; }
            Value mod = applyBinOp("%", v, fin[i]);
            out.arr()->push_back(mod);
            v = applyBinOp("/", applyBinOp("-", v, mod), fin[i]);
        }
        return out;
    }
    if (m == "polymod" && (inv.t == VT::Int || inv.t == VT::Bool)) { // successive divmod by each divisor
        Value out = Value::array(); out.isList = true;
        long long n = inv.toInt();
        // a lazy divisor list (10 xx *, lazy 2,3) switches to pull-driven mode:
        // stop as soon as n hits 0 (no trailing remainder); an exhausted list or
        // a divisor of 1 pushes the remaining n and stops (Rakudo's rules)
        bool lazy = false;
        ValueList fin;          // finite divisor prefix
        Value tail;             // an INFINITE tail (lazy array / endless Range)
        for (auto& a : args) {
            if (a.t == VT::Array && a.ext() &&
                std::static_pointer_cast<LazySeqState>(a.ext())->infinite) { lazy = true; tail = a; break; }
            if (a.t == VT::Range && a.rTo() >= 9000000000000000000LL) { lazy = true; tail = a; break; }
            if (a.t == VT::Array && a.b) lazy = true; // `lazy 2, 3` — finite but lazy
            for (auto& d : a.flatten()) fin.push_back(d);
        }
        if (!lazy) {
            // A bigint invocant divides in BigInt: `inv.toInt()` saturates, so
            // `0xFFFF_FFFF_FFFF_FFFF.polymod(256 xx 7)` answered a leading 0x7F
            // instead of 0xFF — which is how SHA-512 lost its top byte on the way
            // out of the digest. (The lazy-divisor branch below still works in
            // long long; a bigint there wants the same treatment when something
            // needs it.)
            if (inv.big()) {
                BigInt bn = *inv.big();
                auto emit = [&](const BigInt& v) {
                    out.arr()->push_back(v.fitsLL() ? Value::integer(v.toLL()) : Value::bigint(v));
                };
                for (auto& d : fin) {
                    long long dv = d.toInt(); if (dv == 0) break;
                    BigInt q, r;
                    BigInt::divmod(bn, BigInt(dv), q, r);
                    emit(r);
                    bn = q;
                }
                emit(bn); // trailing remainder
                return out;
            }
            for (auto& d : fin) {
                long long dv = d.toInt(); if (dv == 0) break;
                out.arr()->push_back(Value::integer(n % dv));
                n /= dv;
            }
            out.arr()->push_back(Value::integer(n)); // trailing remainder
            return out;
        }
        size_t fi = 0, ti = 0;
        ValueList tcache;
        std::shared_ptr<LazySeqState> st;
        if (tail.t == VT::Array && tail.arr()) { tcache = *tail.arr(); st = std::static_pointer_cast<LazySeqState>(tail.ext()); }
        long long rnext = tail.t == VT::Range ? tail.rFrom() + (tail.rExFrom() ? 1 : 0) : 0;
        auto next = [&](long long& d) -> bool {
            if (fi < fin.size()) { d = fin[fi++].toInt(); return true; }
            if (tail.t == VT::Range) { d = rnext++; return true; }
            if (st) {
                while (ti >= tcache.size()) if (!st->appendNext(tcache)) return false;
                d = tcache[ti++].toInt();
                return true;
            }
            return false;
        };
        // a bigint invocant divides in BigInt here too — `parse-base($hex,
        // 16).polymod(256 xx *)` is how Digest's tests spell an expected blob,
        // and the long-long path saturated it to 0x7FFF… bytes
        if (inv.big()) {
            BigInt bn = *inv.big();
            while (!bn.isZero()) {
                long long d;
                if (!next(d) || d == 1) {
                    out.arr()->push_back(bn.fitsLL() ? Value::integer(bn.toLL()) : Value::bigint(bn));
                    break;
                }
                if (d == 0) break;
                BigInt q, r;
                BigInt::divmod(bn, BigInt(d), q, r);
                out.arr()->push_back(r.fitsLL() ? Value::integer(r.toLL()) : Value::bigint(r));
                bn = q;
            }
            return out;
        }
        while (n != 0) {
            long long d;
            if (!next(d) || d == 1) { out.arr()->push_back(Value::integer(n)); break; }
            if (d == 0) break;
            out.arr()->push_back(Value::integer(n % d));
            n /= d;
        }
        return out;
    }
    // trigonometry as methods (radians): $x.sin, $x.asin, ... (Str is Cool -> numeric)
    //
    // EVERY Cool gets these, not just the numeric ones: a List/Array/Hash/Range
    // numifies to its .elems first, exactly as Rakudo's Cool does. The guard
    // used to be numeric-or-Str, which split the family in half — `.sin` and
    // `.cos` fall through to unguarded handlers further down and so worked on a
    // list, while `.tan`, `.atan2`, `.unpolar` and the whole reciprocal set were
    // "no such method" on the very same value.
    bool coolNumeric = inv.isNumeric() ||
        (inv.t == VT::Str && inv.hashKind != "Buf" && inv.hashKind != "Blob") ||
        inv.t == VT::Array || inv.t == VT::Range || inv.t == VT::Match ||
        (inv.t == VT::Hash && (inv.hashKind.empty() || inv.hashKind == "Hash" || inv.hashKind == "Map"));
    if (coolNumeric) {
        // The rest of Cool's coercion surface. Each one goes through the value's
        // own .Int/.Str first, so it inherits their failure modes: `"a".int` is
        // the same X::Str::Numeric `"a".Int` is, not a silent 0.
        if (m == "Order") { // Cool.Order — the sign of .Int as the Order enum
            ValueList none; Value iv = methodCall(inv, "Int", none);
            if (iv.t == VT::Hash && iv.hashKind == "Failure") return iv;
            return Value::orderVal(iv.big() ? iv.big()->sign : (iv.toInt() < 0 ? -1 : iv.toInt() > 0 ? 1 : 0));
        }
        if (m == "Version") { ValueList one{Value::str(strOf(inv))}; return methodCall(Value::typeObj("Version"), "new", one); }
        if (m == "EVAL") {
            auto it = builtins_.find("EVAL");
            if (it != builtins_.end()) { ValueList ea{Value::str(strOf(inv))}; return it->second(*this, ea); }
        }
        if (m == "conj" && !inv.isNumeric()) { // Cool.conj — the conjugate of .Numeric
            ValueList none; Value nv = methodCall(inv, "Numeric", none);
            if (nv.t == VT::Hash && nv.hashKind == "Failure") return nv;
            return methodCall(nv, "conj", none);
        }
        // Native-width coercions: `.int8` is .Int wrapped into 8 bits two's
        // complement (200.int8 is -56), `.uintN` the unsigned form, `.byte` uint8.
        {
            int bits = 0; bool sign = true;
            if (m == "int" || m == "uint") { bits = 64; sign = (m == "int"); }
            else if (m == "byte") { bits = 8; sign = false; }
            else if (m.s.rfind("int", 0) == 0 || m.s.rfind("uint", 0) == 0) {
                std::string w = m.s.substr(m.s[0] == 'u' ? 4 : 3);
                if (w == "8" || w == "16" || w == "32" || w == "64")
                    { bits = std::atoi(w.c_str()); sign = (m.s[0] != 'u'); }
            }
            if (bits) {
                ValueList none; Value iv = methodCall(inv, "Int", none);
                if (iv.t == VT::Hash && iv.hashKind == "Failure") return iv;
                BigInt mod = BigInt(2).pow(bits), n = iv.big() ? *iv.big() : BigInt(iv.toInt()), q, r;
                BigInt::divmod(n, mod, q, r);
                if (r.sign < 0) r = r + mod;                       // divmod truncates toward zero
                if (sign && (r - BigInt(2).pow(bits - 1)).sign >= 0) r = r - mod; // top bit set: negative
                return r.fitsLL() ? Value::integer(r.toLL()) : Value::bigint(r);
            }
        }
    }
    // The trigonometric family proper, split from the coercions above because it
    // numifies the invocant UP FRONT. That numification is strict — `"a".sin` is
    // X::Str::Numeric in Rakudo, not sin(0) — so it must not run for a method
    // that merely PASSES THROUGH on its way to a later arm, which is why the
    // name is checked before the value is touched.
    static const MNameSet8 kNumMeth = {
        "Complex", "cis", "roots", "unpolar",
        "sin", "cos", "tan", "asin", "acos", "atan", "atan2",
        "sinh", "cosh", "tanh", "asinh", "acosh", "atanh",
        "sec", "cosec", "csc", "cotan", "cot", "asec", "acosec", "acsc",
        "acotan", "acot", "sech", "cosech", "csch", "cotanh", "coth",
        "asech", "acosech", "acsch", "acotanh", "acoth"};
    if (coolNumeric && kNumMeth.has(m)) {
        auto strict = [](const Value& v) -> double {
            if ((v.t == VT::Str || v.t == VT::Match) && !v.isAllomorph() && v.hashKind.empty())
                return numifyStrOrThrow(v.toStr()).toNum();
            return v.toNum();
        };
        // `.roots($n)` binds a Cool, so a numeric-looking string is fine and
        // `"a"` fails as X::Str::Numeric further down. `.unpolar($angle)` binds a
        // REAL: no string binds to it at all, not even "2".
        if (m == "roots" && !args.empty() &&
            (args[0].t == VT::Type || args[0].t == VT::Any || args[0].t == VT::Nil))
            throw RakuError{Value::typeObj("X::TypeCheck::Binding::Parameter"),
                "Type check failed in binding to parameter '$n'; expected Cool but got " +
                args[0].typeName() + " (" + args[0].gist() + ")"};
        // …the check names what CANNOT be a Real rather than what can: a user
        // class doing Real through .Bridge (Roast's Fixed2) is one, and demanding
        // a built-in numeric rejected it.
        if (m == "unpolar" && !args.empty() &&
            (args[0].t == VT::Str || args[0].t == VT::Match || args[0].t == VT::Type ||
             args[0].t == VT::Any || args[0].t == VT::Nil || args[0].t == VT::Code ||
             args[0].t == VT::Complex || args[0].t == VT::Pair))
            throw RakuError{Value::typeObj("X::TypeCheck::Binding::Parameter"),
                "Type check failed in binding to parameter '$angle'; expected Real but got " +
                args[0].typeName() + " (" + args[0].gist() + ")"};
        // A Range numifies to its element count. Counted here rather than by
        // asking .elems: this runs before the Range arms further down, so a
        // methodCall("elems") would come straight back in and never return.
        double x;
        if (inv.t == VT::Range) {
            long long lo = inv.rFrom() + (inv.rExFrom() ? 1 : 0), hi = inv.rTo() - (inv.rExTo() ? 1 : 0);
            x = inv.rTo() >= 9000000000000000000LL ? INFINITY : (double)std::max(0LL, hi - lo + 1);
        }
        else x = strict(inv);
        // a Cool CONTAINER coerces to Complex through its element count too
        if (m == "Complex" && !inv.isNumeric() && inv.t != VT::Str && inv.t != VT::Match)
            return Value::complex(x, 0.0);
        if (m == "cis") return Value::complex(std::cos(x), std::sin(x)); // e^(ix)
        if (m == "roots") { // $x.roots($n) — same as roots($x, $n)
            auto it = builtins_.find("roots");
            if (it != builtins_.end()) { ValueList ra{inv, Value::number(strict(a0()))}; return it->second(*this, ra); }
        }
        if (m == "unpolar") { // $mag.unpolar($angle) — Complex from polar coordinates
            double ang = args.empty() ? 0.0 : strict(a0());
            return Value::complex(x * std::cos(ang), x * std::sin(ang));
        }
        if (m == "sin") return Value::number(std::sin(x));
        if (m == "cos") return Value::number(std::cos(x));
        if (m == "tan") return Value::number(std::tan(x));
        if (m == "asin") return Value::number(std::asin(x));
        if (m == "acos") return Value::number(std::acos(x));
        if (m == "atan") return Value::number(std::atan(x));
        if (m == "atan2") {
            // atan2's second argument is a Cool; an undefined one matches no
            // candidate rather than standing in for zero
            if (!args.empty() && (a0().t == VT::Type || a0().t == VT::Any || a0().t == VT::Nil))
                throw RakuError{Value::typeObj("X::Multi::NoMatch"),
                                "Cannot resolve caller atan2(" + inv.typeName() + ": " +
                                a0().typeName() + ":U); the second argument must be defined"};
            return Value::number(std::atan2(x, args.empty() ? 1.0 : strict(a0())));
        }
        if (m == "sinh") return Value::number(std::sinh(x));
        if (m == "cosh") return Value::number(std::cosh(x));
        if (m == "tanh") return Value::number(std::tanh(x));
        if (m == "asinh") return Value::number(std::asinh(x));
        if (m == "acosh") return Value::number(std::acosh(x));
        if (m == "atanh") return Value::number(std::atanh(x));
        if (m == "sec") return Value::number(1.0 / std::cos(x));
        if (m == "cosec" || m == "csc") return Value::number(1.0 / std::sin(x));
        if (m == "cotan" || m == "cot") return Value::number(1.0 / std::tan(x));
        if (m == "asec") return Value::number(std::acos(1.0 / x));
        if (m == "acosec" || m == "acsc") return Value::number(std::asin(1.0 / x));
        if (m == "acotan" || m == "acot") return Value::number(std::atan(1.0 / x));
        if (m == "sech") return Value::number(1.0 / std::cosh(x));
        if (m == "cosech" || m == "csch") return Value::number(1.0 / std::sinh(x));
        if (m == "cotanh" || m == "coth") return Value::number(1.0 / std::tanh(x));
        if (m == "asech") return Value::number(std::acosh(1.0 / x));
        if (m == "acosech" || m == "acsch") return Value::number(std::asinh(1.0 / x));
        if (m == "acotanh" || m == "acoth") return Value::number(std::atanh(1.0 / x));
    }
    if (m == "floor" || m == "ceiling" || m == "round" || m == "truncate") {
        // Inf/NaN round to themselves (they stay Num) — only .Int coercion throws.
        if (inv.t == VT::Num && !std::isfinite(inv.n)) return inv;
        // zero-denominator Rats cannot round — they FAIL (X::Numeric::DivideByZero)
        if (inv.t == VT::Rat && inv.ratD() && inv.ratD()->isZero()) {
            Value f = Value::makeHash(); f.hashKind = "Failure";
            (*f.hash())["exception"] = Value::typeObj("X::Numeric::DivideByZero");
            return f;
        }
        // exact rounding for Rats/Ints (big-safe): floor = div, others derive from it
        if (m != "round" && (inv.t == VT::Rat || inv.t == VT::Int || inv.t == VT::Bool)) {
            BigInt n = inv.t == VT::Rat ? *inv.ratN() : inv.toBig();
            BigInt d = inv.t == VT::Rat ? *inv.ratD() : BigInt(1);
            BigInt q, r; BigInt::divmod(n, d, q, r);
            if (m == "floor"   && !r.isZero() && n.sign < 0) q = q - BigInt(1);
            if (m == "ceiling" && !r.isZero() && n.sign > 0) q = q + BigInt(1);
            return Value::bigint(q); // truncate: q as-is
        }
    }
    if (m == "floor") return Value::integer((long long)std::floor(inv.toNum()));
    if (m == "ceiling") return Value::integer((long long)std::ceil(inv.toNum()));
    if (m == "round") {
        double x = inv.toNum();
        if (!std::isfinite(x)) return Value::number(x); // NaN/±Inf round to themselves
        // Rakudo rounds a half toward +∞ — `(self / $scale + 1/2).floor * $scale`.
        // Done in VALUE arithmetic whenever neither side is a Num, so
        // `round(1000, 23.01)` is exactly 989.43 rather than 989.4300000000001,
        // and a big Int rounded by an Int stays that Int.
        Value scaleV = args.empty() ? Value::integer(1) : a0();
        bool exact = inv.t != VT::Num && scaleV.t != VT::Num &&
                     inv.t != VT::Complex && scaleV.toNum() != 0;
        if (exact) {
            Value q = applyArith("+", applyArith("/", inv, scaleV),
                                 Value::ratZ(BigInt(1LL), BigInt(2LL)));
            Value fl = methodCall(q, "floor", {});
            return args.empty() ? fl : applyArith("*", fl, scaleV);
        }
        double scale = args.empty() ? 1.0 : scaleV.toNum();
        if (scale == 0) scale = 1.0;
        double q = std::floor(x / scale + 0.5);
        if (args.empty()) return Value::integer((long long)q); // .round with no arg is an Int
        // Rakudo's last step is `.floor * $scale`, and the type of THAT multiply
        // is the type of the answer: an Int or Rat scale makes it exact, and only
        // a Num scale leaves it a Num. Doing the whole thing in doubles gave
        // `178.14159e0.round(0.1)` as 178.10000000000002 where Rakudo says 178.1.
        // The division and the floor stay in doubles, as they are under Rakudo
        // too — a Num divided by a Rat is a Num there.
        // (A zero scale is excluded because the double path substitutes 1 for it
        // above, and multiplying by the real scale would answer 0 instead.)
        if (scaleV.t != VT::Num && inv.t != VT::Complex && scaleV.toNum() != 0 &&
            std::isfinite(q) && std::fabs(q) < 9.2e18)
            return applyArith("*", Value::integer((long long)q), scaleV);
        return Value::number(q * scale);
    }
    if (m == "truncate") return Value::integer((long long)inv.toNum());
    if (m == "sign") {
        if (inv.t == VT::Type)
            throw RakuError{Value::typeObj("X::Multi::NoMatch"), "Cannot call sign on a type object"};
        if (inv.t == VT::Complex) { // 6.e: v / |v|; 6.c/6.d keep the historical throw
            if (langRev_ < 2)
                throw RakuError{Value::typeObj("X::Numeric::Real"), "Complex is not in the Real domain, so it has no sign"};
            double mag = std::hypot(inv.n, inv.im());
            if (mag == 0) return Value::complex(0, 0);
            return Value::complex(inv.n / mag, inv.im() / mag);
        }
        double n = inv.toNum();
        if (std::isnan(n)) return Value::number(NAN); // sign(NaN) is NaN
        return Value::integer(n < 0 ? -1 : n > 0 ? 1 : 0);
    }
    if (m == "exp") return Value::number(std::exp(inv.toNum()));
    if (m == "log") {
        if (!args.empty() && args[0].t == VT::Complex) { // real.log(complex base)
            std::complex<double> r = std::log(std::complex<double>(inv.toNum(), 0.0)) /
                                     std::log(std::complex<double>(args[0].n, args[0].im()));
            return Value::complex(r.real(), r.imag());
        }
        if (!args.empty()) return rtLogReal(*this, inv.toNum(), args[0].toNum());
        return rtLogReal(*this, inv.toNum(), 0.0);
    }
    if (m == "log10") return rtLogReal(*this, inv.toNum(), 10.0);
    if (m == "log2")  return rtLogReal(*this, inv.toNum(), 2.0);
    if (m == "sin") return Value::number(std::sin(inv.toNum()));
    if (m == "cos") return Value::number(std::cos(inv.toNum()));
    if (m == "numerator") return inv.t == VT::Rat ? Value::bigint(*inv.ratN()) : Value::integer(inv.toInt());
    if (m == "denominator") return inv.t == VT::Rat ? Value::bigint(*inv.ratD()) : Value::integer(1);
    if (m == "nude") { // a List (prints "(3 10)"), not an Array, like Rakudo
        Value o = inv.t == VT::Rat
            ? Value::array({Value::bigint(*inv.ratN()), Value::bigint(*inv.ratD())})
            : Value::array({Value::integer(inv.toInt()), Value::integer(1)});
        o.isList = true;
        return o;
    }
    if (m == "norm" && inv.t == VT::Rat) return inv; // Rats are always stored reduced
    if (inv.t == VT::Array && inv.arr() &&
        (m == "AT-POS" || m == "EXISTS-POS" || m == "ASSIGN-POS" || m == "DELETE-POS")) {
        // multi-dim access on a shaped array (`@a.AT-POS(i, j)`): walk each index
        // level. ASSIGN-POS takes a trailing value, so its last arg is the value.
        size_t nidx = (m == "ASSIGN-POS") ? (args.size() > 1 ? args.size() - 1 : args.size()) : args.size();
        if (nidx > 1) {
            // The descent needs a mutable cursor. Copying the invocant is safe and
            // does not lose the write: the loop always steps at least once (nidx > 1),
            // so `cur` ends up inside inv.arr — which is a shared_ptr, the same array
            // object the caller holds. The copy shares it rather than duplicating it.
            Value invLocal = inv;
            Value* cur = &invLocal;
            bool oob = false;
            for (size_t d = 0; d + 1 < nidx; d++) { // descend to the innermost array
                long long ix = args[d].toInt();
                if (!cur->arr() || ix < 0 || ix >= (long long)cur->arr()->size()) { oob = true; break; }
                cur = &(*cur->arr())[ix];
            }
            long long last = args[nidx - 1].toInt();
            bool in = !oob && cur->t == VT::Array && cur->arr() && last >= 0 && last < (long long)cur->arr()->size();
            if (m == "EXISTS-POS") return Value::boolean(in && defined((*cur->arr())[last]));
            if (m == "AT-POS") {
                if (!in) throw RakuError{Value::typeObj("X::OutOfRange"), "Index out of range"};
                return (*cur->arr())[last];
            }
            if (m == "ASSIGN-POS") {
                Value v = args.back();
                if (in) (*cur->arr())[last] = v;
                return v;
            }
            Value old = in ? (*cur->arr())[last] : Value::any();
            if (in) (*cur->arr())[last] = Value::any();
            return old;
        }
        long long i = args.empty() ? 0 : args[0].toInt();
        if (i < 0) i += (long long)inv.arr()->size();
        bool in = i >= 0 && i < (long long)inv.arr()->size();
        if (m == "EXISTS-POS") return Value::boolean(in && defined((*inv.arr())[i]));
        if (m == "AT-POS") return in ? (*inv.arr())[i] : Value::any();
        if (m == "ASSIGN-POS") {
            Value v = args.size() > 1 ? args[1] : Value::any();
            if (i >= 0) { while ((long long)inv.arr()->size() <= i) inv.arr()->push_back(Value::any());
                          (*inv.arr())[i] = v; }
            return v;
        }
        // DELETE-POS
        Value old = in ? (*inv.arr())[i] : Value::any();
        if (in) (*inv.arr())[i] = Value::any();
        return old;
    }
    if (m == "minpairs" || m == "maxpairs") {
        // pairs whose value is the min/max (per cmp); a scalar is its 0 => self pair
        Value out = Value::array(); out.isList = true;
        std::vector<std::pair<Value, Value>> kvs; // key, value
        if (inv.t == VT::Array && inv.arr()) {
            for (size_t k = 0; k < inv.arr()->size(); k++) kvs.push_back({Value::integer((long long)k), (*inv.arr())[k]});
        } else if (inv.t == VT::Hash && inv.hash() &&
                   (inv.hashKind.empty() || inv.hashKind.rfind("Set", 0) == 0 ||
                    inv.hashKind.rfind("Bag", 0) == 0 || inv.hashKind.rfind("Mix", 0) == 0)) {
            // a Setty/Baggy competes on its counts (elem => count pairs)
            for (auto& kv : *inv.hash()) kvs.push_back({Value::str(kv.first), kv.second});
        } else {
            out.arr()->push_back(Value::pair("0", inv));
            return out;
        }
        // holes in a sparse array (and undefined values generally) do not compete
        kvs.erase(std::remove_if(kvs.begin(), kvs.end(),
            [&](const std::pair<Value, Value>& kv) {
                const Value& v = kv.second;
                return v.t == VT::Nil || v.t == VT::Any || v.t == VT::Type;
            }), kvs.end());
        if (kvs.empty()) return out;
        Value best = kvs[0].second;
        bool wantMax = (m == "maxpairs");
        for (auto& kv : kvs) {
            Value c = applyArith("cmp", kv.second, best);
            if (wantMax ? c.toInt() > 0 : c.toInt() < 0) best = kv.second;
        }
        for (auto& kv : kvs)
            if (applyArith("cmp", kv.second, best).toInt() == 0) {
                Value p = Value::pair(kv.first.toStr(), kv.second);
                out.arr()->push_back(p);
            }
        return out;
    }
    if (m == "isa" && !args.empty()) {
        // Foo.isa(Foo) / $obj.isa("Any") / 5.isa(Int) — walk the class chain, then
        // built-in ancestry. Works on any value via its type name.
        std::string want = args[0].t == VT::Type ? args[0].s : args[0].toStr();
        // a parameterized type object carries its parameter separately
        if (args[0].t == VT::Type && !args[0].ofType().empty() &&
            want.find('[') == std::string::npos)
            want += "[" + args[0].ofType() + "]";
        std::string tn = inv.t == VT::Type ? inv.s : (inv.obj() && inv.obj()->cls ? inv.obj()->cls->name : inv.typeName());
        if (inv.t == VT::Type && !inv.ofType().empty() && tn.find('[') == std::string::npos)
            tn += "[" + inv.ofType() + "]";
        if (tn == want || want == "Any" || want == "Mu") return Value::boolean(true);
        // `CArray[int32]` IS a `CArray`: an unparameterized want matches the base
        // of a parameterized type. (The reverse does not hold — a bare CArray is
        // not a CArray[int32].)
        if (want.find('[') == std::string::npos) {
            size_t br = tn.find('[');
            if (br != std::string::npos && tn.compare(0, br, want) == 0) return Value::boolean(true);
        }
        // an allomorph (IntStr/NumStr/RatStr/ComplexStr) IS both its numeric
        // type and Str by inheritance
        if (inv.isAllomorph() &&
            (want == "Str" || want == "Int" || want == "Num" || want == "Rat" || want == "Complex")) {
            if (want == "Str") return Value::boolean(true);
            return Value::boolean(tn == want + std::string("Str"));
        }
        // `.isa` is strict CLASS inheritance — a role (Numeric, Real, …) is never
        // an `isa` ancestor (use `~~`/`.does` for role membership)
        if (isBuiltinRole(want)) return Value::boolean(false);
        ClassInfo* c0 = inv.t == VT::Object && inv.obj() ? inv.obj()->cls.get() : nullptr;
        if (!c0) { auto cit = classes_.find(tn); if (cit != classes_.end()) c0 = cit->second.get(); }
        for (ClassInfo* c = c0; c; c = c->parent.get()) {
            if (c->name == want || c->nativeParent == want) return Value::boolean(true);
            if (!c->nativeParent.empty())
                for (auto& anc : typeAncestry(c->nativeParent)) if (anc == want) return Value::boolean(true);
        }
        for (auto& anc : typeAncestry(tn)) if (anc == want) return Value::boolean(true);
        return Value::boolean(false);
    }
    // `&f.cando(\(1, 2))` — the candidates that would accept that capture, as a
    // list (empty when none would). Multi dispatch already answers exactly this
    // question through scoreCandidate; there was simply no method exposing it,
    // and HTTP::Tiny gates its cookie-jar check on `.cando`.
    if (m == "cando" && inv.t == VT::Code && inv.code() && !args.empty()) {
        ValueList call;                      // the capture's parts, as a call would see them
        const Value& cap = args[0];
        if (cap.t == VT::Array && cap.arr())
            for (auto& x : *cap.arr()) {
                Value e = x;
                if (e.t == VT::Pair) e.namedArg = true;   // its NAMED parts
                call.push_back(std::move(e));
            }
        else call.push_back(cap);
        // On a METHOD the capture's first positional is the INVOCANT, which is not
        // part of the parameter list — `$m.cando: \(Jar, 'GET', 'x')` asks whether
        // `method add($, $)` accepts the two that follow.
        auto forCand = [&](const Value& c) {
            if (!(c.t == VT::Code && c.code() && c.code()->isMethod)) return call;
            ValueList rest;
            for (size_t i = 1; i < call.size(); i++) rest.push_back(call[i]);
            return rest;
        };
        Value out = Value::array(); out.isList = true;
        // Candidates, whenever there are any — an explicit `proto g(|) {*}` is not
        // flagged as a dispatcher, and scoring ITS signature accepts anything.
        if (!inv.code()->candidates.empty()) {
            for (auto& c : inv.code()->candidates) {
                if (c.t == VT::Code && c.code() && c.code()->isProto) continue; // the proto
                                        // defines the group; its `|` accepts anything
                if (scoreCandidate(c, forCand(c)) >= 0) out.arr()->push_back(c);
            }
        }
        else if (scoreCandidate(inv, forCand(inv)) >= 0) out.arr()->push_back(inv);
        return out;
    }
    if (m == "package" && inv.t == VT::Code && inv.code())
        return Value::typeObj(inv.code()->pkg.empty() ? "GLOBAL" : inv.code()->pkg);
    if (m == "of" && inv.t == VT::Type) { // array[int].of / Array[Str].of
        if (const char* vt = quantValueType(inv.s)) return Value::typeObj(vt); // Bag.of is UInt
        return Value::typeObj(inv.ofType().empty() ? "Mu" : inv.ofType());
    }
    if (m == "new" && inv.t == VT::Array) { // @a.new: fresh empty array of the same type
        Value out = Value::array();
        out.ofTypeM() = inv.ofType();
        return out;
    }
    if (m == "keyof") { // key type of an Associative (unparameterized: Mu / Str(Any))
        if (inv.t == VT::Hash && !inv.hashKind.empty()) // quanthash: its key parameter (unparameterized: Mu)
            return Value::typeObj(inv.ofType().empty() ? "Mu" : inv.ofType());
        if (inv.t == VT::Type) {
            static const std::set<std::string> qh = {"Set", "SetHash", "Bag", "BagHash", "Mix", "MixHash"};
            if (qh.count(inv.s)) // Mix[Str].keyof is Str; unparameterized quanthashes key on Mu
                return Value::typeObj(inv.ofType().empty() ? "Mu" : inv.ofType());
            return Value::typeObj("Str(Any)"); // `Hash.keyof`
        }
        // An OBJECT hash keys on its declared key type. Parser.cpp records that as
        // the second half of declType ("Any,Int"), which typedDefault copies into
        // ofType — `.of` already reads the first half and `.keyof` never read the
        // second. A plain hash keys on the COERCION type Str(Any), not bare Str.
        if (inv.t == VT::Hash) {
            size_t c = inv.ofType().find(',');
            if (c != std::string::npos) return Value::typeObj(inv.ofType().substr(c + 1));
        }
        return Value::typeObj("Str(Any)");
    }
    if (m == "Rat" || m == "FatRat") {
        bool fat = (m == "FatRat");
        Value r;
        // a non-numeric string is the usual Failure, not a Rat of zero
        if (inv.t == VT::Str && inv.hashKind.empty() && !inv.isAllomorph()) {
            Value nv = numifyStrFailure(inv.s);
            if (nv.t == VT::Hash && nv.hashKind == "Failure") return nv;
            if (nv.t != VT::Str) return methodCall(nv, m, args, rwArgs);
        }
        if (inv.t == VT::Rat) {
            r = inv;
            // .Rat on a RatStr allomorph sheds the Str half — keeping the tag made
            // `jsonify(.Rat)` see another RatStr and recurse forever (JSON::Fast)
            r.hashKind.clear(); r.s.clear();
        }
        else if (inv.t == VT::Int || inv.t == VT::Bool) r = Value::rat(inv.toBig(), BigInt(1));
        else {
            // Num→Rat by continued fractions (Rakudo's default epsilon 1e-6):
            // pi.Rat == 355/113; dyadic values come out exact (4.5e0 → 9/2).
            double x = inv.toNum();
            double eps = args.size() > 0 ? args[0].toNum() : 1e-6;
            if (std::isnan(x)) r = Value::ratZ(BigInt(0), BigInt(0));
            else if (std::isinf(x)) r = Value::ratZ(BigInt(x > 0 ? 1 : -1), BigInt(0));
            else {
                bool neg = x < 0; double ax = neg ? -x : x, v = ax;
                long long p0 = 0, q0 = 1, p1 = 1, q1 = 0; // CF convergents h/k
                for (int it = 0; it < 64; it++) {
                    double fa = std::floor(v);
                    if (fa > 9e17) break; // convergent would overflow
                    long long a = (long long)fa;
                    if (p1 && a > (LLONG_MAX - p0) / p1) break;
                    if (q1 && a > (LLONG_MAX - q0) / q1) break;
                    long long p2 = a * p1 + p0, q2 = a * q1 + q0;
                    p0 = p1; q0 = q1; p1 = p2; q1 = q2;
                    if (std::abs((double)p1 / (double)q1 - ax) <= eps) break;
                    double frac = v - fa;
                    if (frac <= 1e-18) break;
                    v = 1.0 / frac;
                }
                r = Value::rat(BigInt(neg ? -p1 : p1), BigInt(q1 ? q1 : 1));
            }
        }
        r.fatRatM() = fat; // FatRat is the arbitrary-precision Rat, tagged for type identity
        return r;
    }
    if (m == "succ") {
        if (inv.t == VT::Bool) return Value::boolean(true);   // Bool saturates
        if (inv.t != VT::Str) return Value::integer(inv.toInt() + 1);
        Value r = Value::str(strSucc(inv.s));
        // an IO::Path stays an IO::Path (`"foo/file_02.txt".IO.succ` is an IO::Path)
        r.hashKind = inv.hashKind; r.enumName = inv.enumName;
        return r;
    }
    if (m == "pred") {
        if (inv.t == VT::Bool) return Value::boolean(false);
        if (inv.t == VT::Str) {
            bool ok; std::string r = strPred(inv.s, ok);
            if (!ok) throw RakuError{Value::typeObj("X::AdHoc"), "Decrement out of range"};
            Value o = Value::str(r);
            o.hashKind = inv.hashKind; o.enumName = inv.enumName;
            return o;
        }
        return Value::integer(inv.toInt() - 1);
    }
    if (m == "is-prime") {
        // Miller-Rabin with the standard small-prime witness set: deterministic
        // for anything below 3.3e24, and matches Rakudo's probabilistic answer
        // beyond (found via (2**127-1).is-prime — trial division on a truncated
        // int64 said False for M127)
        static const long long kWit[] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37};
        if (inv.big()) {
            const BigInt& n = *inv.big();
            BigInt one(1), two(2);
            if (n.sign <= 0 || BigInt::cmp(n, two) < 0) return Value::boolean(false);
            auto mod = [](const BigInt& a, const BigInt& b) { BigInt q, r; BigInt::divmod(a, b, q, r); return r; };
            auto modpow = [&](BigInt b, BigInt e, const BigInt& mo) {
                BigInt r(1);
                b = mod(b, mo);
                while (!e.isZero()) {
                    BigInt q, rm; BigInt::divmod(e, BigInt(2), q, rm);
                    if (!rm.isZero()) r = mod(r * b, mo);
                    b = mod(b * b, mo);
                    e = q;
                }
                return r;
            };
            for (long long w : kWit) { // small-prime divisibility screen
                if (BigInt::cmp(n, BigInt(w)) == 0) return Value::boolean(true);
                if (mod(n, BigInt(w)).isZero()) return Value::boolean(false);
            }
            BigInt d = n - one; long long s = 0;
            for (;;) { BigInt q, r; BigInt::divmod(d, BigInt(2), q, r); if (!r.isZero()) break; d = q; s++; }
            BigInt nm1 = n - one;
            for (long long w : kWit) {
                BigInt x = modpow(BigInt(w), d, n);
                if (BigInt::cmp(x, one) == 0 || BigInt::cmp(x, nm1) == 0) continue;
                bool composite = true;
                for (long long r = 1; r < s; r++) {
                    x = mod(x * x, n);
                    if (BigInt::cmp(x, nm1) == 0) { composite = false; break; }
                }
                if (composite) return Value::boolean(false);
            }
            return Value::boolean(true);
        }
        long long n = inv.toInt();
        if (n < 2) return Value::boolean(false);
        for (long long w : kWit) { if (n == w) return Value::boolean(true); if (n % w == 0) return Value::boolean(false); }
        auto mulmod = [](long long a, long long b, long long mo) -> long long {
#if RAKUPP_HAS_INT128
            return (long long)((__int128)a * b % mo);
#else
            BigInt q, r; BigInt::divmod(BigInt(a) * BigInt(b), BigInt(mo), q, r);
            return r.fitsLL() ? r.toLL() : 0; // MSVC: no __int128 — go through BigInt
#endif
        };
        auto modpow = [&](long long b, long long e, long long mo) {
            long long r = 1; b %= mo;
            for (; e; e >>= 1) { if (e & 1) r = mulmod(r, b, mo); b = mulmod(b, b, mo); }
            return r;
        };
        long long d = n - 1; int s = 0;
        while ((d & 1) == 0) { d >>= 1; s++; }
        for (long long w : kWit) {
            long long x = modpow(w, d, n);
            if (x == 1 || x == n - 1) continue;
            bool composite = true;
            for (int r = 1; r < s; r++) { x = mulmod(x, x, n); if (x == n - 1) { composite = false; break; } }
            if (composite) return Value::boolean(false);
        }
        return Value::boolean(true);
    }

    // Str -> Date / DateTime / Version: the string coercions the types answer
    // through their own .new (`"2024-06-01".Date`)
    if (inv.t == VT::Str && inv.hashKind.empty() &&
        (m == "Date" || m == "DateTime" || m == "Version")) {
        ValueList one{inv};
        return methodCall(Value::typeObj(m), "new", one, nullptr);
    }

    // ---- IO::Path (string-as-path) ----
    if (m == "IO") {
        // Any has no .IO (Cool does): an undefined invocant dies rather than
        // silently becoming the "" path; Nil keeps its absorb-everything rule.
        // A bare type object (Date:U, Int:U, …) has no instance to make a path from.
        if (inv.t == VT::Any || inv.t == VT::Type)
            throw RakuError{Value::typeObj("X::Method::NotFound"),
                            "No such method 'IO' for invocant of type '" +
                            (inv.t == VT::Type ? inv.s : std::string("Any")) + "'"};
        if (inv.t == VT::Nil) return Value::nil();
        rejectNulPath(inv.toStr()); Value p = Value::str(inv.toStr()); p.hashKind = "IO"; return p;
    }
    // `.slurp` belongs to IO::Path (IO::Handle has its own, below) — a Str is NOT
    // a path in Rakudo, `"file".slurp` is "no such method". rakupp accepted any
    // invocant, which made `$value.^lookup('slurp')` true for a plain string and
    // sent HTTP::Tiny off to slurp a form field. `slurp $path` (the SUB) is
    // unaffected; so is every `$io.slurp`.
    if (m == "slurp" && inv.hashKind == "IO") {
        std::ifstream in(inv.toStr(), std::ios::binary);
        if (!in) throwFailedOpen(inv.toStr());
        std::ostringstream ss; ss << in.rdbuf();
        std::string text = ss.str();
        bool bin = false;
        for (auto& a : args) if (a.t == VT::Pair && a.s == "bin" && a.pairVal() && a.pairVal()->truthy()) bin = true;
        // TEXT mode translates the line separator: an IO::Handle's default
        // :nl-in is ["\n", "\r\n"], so a CRLF file reads back with plain LF
        // (`"a\r\nb".IO.slurp.encode.bytes` is 4 in Rakudo, not 5). :bin is the
        // raw bytes and keeps every CR. HTTP::Tiny compares a generated request
        // against a CRLF fixture read this way.
        if (!bin && text.find('\r') != std::string::npos) {
            std::string outT; outT.reserve(text.size());
            for (size_t i = 0; i < text.size(); i++) {
                if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') continue;
                outT += text[i];
            }
            text.swap(outT);
        }
        Value v = Value::str(text);
        if (bin) v.hashKind = "Blob";   // slurp(:bin) yields a Blob, not a decoded Str
        return v;
    }
    if (m == "spurt" && !(inv.t == VT::Hash && inv.hashKind == "FileHandle")) {
        // path-spurt; an open IO::Handle's .spurt is handled in the FileHandle
        // block below (buffer + flush-on-close), not by stringifying the handle
        bool append = false;
        std::string content;
        bool haveContent = false;
        for (auto& a : args) {
            if (a.t == VT::Pair && a.namedArg) {
                if (a.s == "append") append = a.pairVal() && a.pairVal()->truthy();
            }
            else if (!haveContent) { content = a.toStr(); haveContent = true; }
        }
        std::ofstream out(inv.toStr(), append ? (std::ios::out | std::ios::app) : std::ios::out);
        if (!out) return Value::boolean(false);
        out << content;
        return Value::boolean(true);
    }
    if ((m == "e" || m == "f" || m == "d" || m == "r" || m == "w" || m == "x" ||
         m == "rw" || m == "rx" || m == "wx" || m == "rwx") && inv.hashKind == "IO") {
        struct stat st;
        if (stat(inv.toStr().c_str(), &st) != 0) return Value::boolean(false);
        if (m == "d") return Value::boolean(S_ISDIR(st.st_mode));
        if (m == "f") return Value::boolean(S_ISREG(st.st_mode));
        if (m == "e") return Value::boolean(true);
        // r/w/x and their combinations: every named permission must hold
        int mode = 0;
        if (m.find('r') != std::string::npos) mode |= R_OK;
        if (m.find('w') != std::string::npos) mode |= W_OK;
        if (m.find('x') != std::string::npos) mode |= X_OK;
        return Value::boolean(::access(inv.toStr().c_str(), mode) == 0);
    }
    if (m == "l" && inv.hashKind == "IO") { // symlink? (lstat, so broken links still count)
#if defined(_WIN32)
        return Value::boolean(false); // Windows: no POSIX symlink test here
#else
        struct stat st;
        return Value::boolean(::lstat(inv.toStr().c_str(), &st) == 0 && S_ISLNK(st.st_mode));
#endif
    }
    if ((m == "s" || m == "z") && inv.hashKind == "IO") { // size / zero-length; both FAIL (softly) if absent
        struct stat st;
        if (stat(inv.toStr().c_str(), &st) != 0) {
            Value f = Value::makeHash(); f.hashKind = "Failure";
            (*f.hash())["exception"] = Value::typeObj("X::IO::DoesNotExist");
            (*f.hash())["message"] = Value::str("Failed to stat '" + inv.toStr() + "': no such file or directory");
            return f;
        }
        if (m == "z") return Value::boolean(st.st_size == 0);
        return Value::integer((long long)st.st_size);
    }
    if (m == "mode" && inv.hashKind == "IO") { // permission bits as a 4-digit octal string
        struct stat st;
        if (stat(inv.toStr().c_str(), &st) != 0) {
            Value f = Value::makeHash(); f.hashKind = "Failure";
            (*f.hash())["exception"] = Value::typeObj("X::IO::DoesNotExist");
            (*f.hash())["message"] = Value::str("Failed to stat '" + inv.toStr() + "': no such file or directory");
            return f;
        }
        char buf[8]; snprintf(buf, sizeof buf, "0%03o", st.st_mode & 07777);
        return Value::str(buf);
    }
    if (m == "mkdir") { // $path.IO.mkdir(:parent) — create the directory (and parents)
        std::string path = inv.toStr();
        std::string acc;
        for (size_t i = 0; i <= path.size(); i++) {
            if (i == path.size() || path[i] == '/') {
                if (!acc.empty()) ::mkdir(acc.c_str(), 0777);
                if (i < path.size()) acc += '/';
            } else acc += path[i];
        }
        Value p = Value::str(path); p.hashKind = "IO"; return p;
    }
    if (m == "unlink") { // $path.IO.unlink — remove the file; True on success
        return Value::boolean(::unlink(inv.toStr().c_str()) == 0);
    }
    if (m == "rmdir") { // $path.IO.rmdir — remove the (empty) directory
        return Value::boolean(::rmdir(inv.toStr().c_str()) == 0);
    }
    // `$path.IO.copy($to, :$createonly)` / `.rename($to)` / `.move($to)` — file
    // moves and copies (Shell::Command's cp/mv are thin wrappers over these).
    // rename() is one syscall but only within a filesystem; fall back to a copy +
    // unlink so a cross-device move still works, which is what Rakudo does.
    if ((m == "copy" || m == "rename" || m == "move") && !args.empty()) {
        std::string from = inv.toStr(), to = args[0].toStr();
        bool createonly = false;
        for (auto& a : args)
            if (a.t == VT::Pair && a.s == "createonly") createonly = !a.pairVal() || a.pairVal()->truthy();
        // Copying or MOVING a file onto itself is an error, not a no-op: `rename`
        // succeeds on the same path and `copy` would truncate the source before
        // reading it. `rename` is the exception — Rakudo lets that one through.
        if (m != "rename") {
            struct stat sf{}, st{};
            if (::stat(from.c_str(), &sf) == 0 && ::stat(to.c_str(), &st) == 0 &&
                sf.st_dev == st.st_dev && sf.st_ino == st.st_ino) {
                Value f = Value::makeHash(); f.hashKind = "Failure";
                (*f.hash())["exception"] = Value::typeObj(m == "move" ? "X::IO::Move" : "X::IO::Copy");
                (*f.hash())["message"] = Value::str(
                    "Failed to " + m + " '" + from + "' to '" + to +
                    "': source and target are the same file");
                return f;
            }
        }
        if (createonly && std::ifstream(to).good())
            throw RakuError{Value::typeObj("X::IO::Copy"),
                "Failed to copy '" + from + "' to '" + to + "': target already exists"};
        auto copyFile = [&]() -> bool {
            std::ifstream in(from, std::ios::binary);
            if (!in) return false;
            std::ostringstream buf; buf << in.rdbuf();
            std::ofstream out(to, std::ios::binary | std::ios::trunc);
            if (!out) return false;
            // NB: read the source into memory rather than `out << in.rdbuf()` —
            // inserting an EMPTY streambuf sets failbit, so copying a zero-length
            // file reported failure while having done exactly the right thing.
            const std::string& data = buf.str();
            if (!data.empty()) out.write(data.data(), (std::streamsize)data.size());
            out.flush();
            return out.good();
        };
        if (m == "copy") {
            if (!copyFile())
                throw RakuError{Value::typeObj("X::IO::Copy"),
                    "Failed to copy '" + from + "' to '" + to + "': " + std::strerror(errno)};
            return Value::boolean(true);
        }
        if (::rename(from.c_str(), to.c_str()) == 0) return Value::boolean(true);
        if (!copyFile())
            throw RakuError{Value::typeObj(m == "move" ? "X::IO::Move" : "X::IO::Rename"),
                "Failed to " + std::string(m == "move" ? "move" : "rename") + " '" + from +
                "' to '" + to + "': " + std::strerror(errno)};
        ::unlink(from.c_str());
        return Value::boolean(true);
    }
    if (m == "path") {
        if (inv.t == VT::Hash && inv.hashKind == "FileHandle") {
            auto st = inv.hash()->find("std"); // standard streams: an IO::Special
            if (st != inv.hash()->end()) {
                std::string nm = st->second.toStr() == "err" ? "<STDERR>" : st->second.toStr() == "in" ? "<STDIN>" : "<STDOUT>";
                Value sp = Value::str(nm); sp.hashKind = "IO::Special"; return sp;
            }
            auto pt = inv.hash()->find("path");
            if (pt != inv.hash()->end()) return pt->second;
        }
        return Value::str(inv.toStr());
    }
    if (m == "relative") {
        // IO::Path.relative($base = $*CWD) — the path expressed relative to $base.
        // Prefix case only (zef's extractor lists files under the extraction dir);
        // unrelated paths come back unchanged rather than computed via `..`.
        std::string p = inv.toStr();
        std::string base;
        for (auto& a : args) if (a.t != VT::Pair) { base = a.toStr(); break; }
        if (base.empty()) { char buf[4096]; base = getcwd(buf, sizeof buf) ? buf : "."; }
        while (base.size() > 1 && base.back() == '/') base.pop_back();
        if (p == base) return Value::str(".");
        if (p.rfind(base + "/", 0) == 0) return Value::str(p.substr(base.size() + 1));
        return Value::str(p);
    }
    // 6.e `.stem`: the basename with its extensions removed — all of them by
    // default, or the last N. "foo.tar.gz".IO.stem is "foo" and .stem(1) is
    // "foo.tar"; a basename with no dot is its own stem.
    if (m == "stem" && sixE() && inv.hashKind == "IO") {
        Value base = methodCall(inv, "basename", {});
        std::string b = base.toStr();
        std::vector<size_t> dots;
        for (size_t i = 0; i < b.size(); i++) if (b[i] == '.') dots.push_back(i);
        if (dots.empty()) return Value::str(b);
        size_t cut;
        if (args.empty() || args[0].t == VT::Whatever) cut = dots.front();
        else {
            long long want = args[0].toInt();
            if (want <= 0) return Value::str(b);
            cut = (size_t)want >= dots.size() ? dots.front() : dots[dots.size() - (size_t)want];
        }
        return Value::str(b.substr(0, cut));
    }
    if (m == "basename" && !(inv.hashKind == "IO" && !inv.enumName.empty())) {
        // (a FLAVORED path answers through its own IO::Spec, further down)
        // trailing slashes don't count: "/a/b/".basename is "b" (Rakudo; zef's
        // fez mirror "http://360.zef.pm/" must yield "360.zef.pm", not "")
        std::string s = inv.toStr();
        size_t end = s.find_last_not_of('/');
        if (end == std::string::npos) return Value::str("/"); // all slashes: root
        s = s.substr(0, end + 1);
        auto p = s.find_last_of('/');
        return Value::str(p == std::string::npos ? s : s.substr(p + 1));
    }
    // ---- more IO::Path methods (operate on the path string) ----
    {
        auto asIO = [](std::string s) { Value v = Value::str(s); v.hashKind = "IO"; return v; };
        auto dirOf = [](const std::string& s) -> std::string {
            std::string t = s; while (t.size() > 1 && t.back() == '/') t.pop_back();
            auto p = t.find_last_of('/');
            if (p == std::string::npos) return ".";
            return p == 0 ? "/" : t.substr(0, p);
        };
        // a flavored path (IO::Path::Win32 etc., flavor in enumName) answers
        // through ITS IO::Spec instead of the platform default
        if (!inv.enumName.empty()) {
            std::string spec = "IO::Spec::" + inv.enumName;
            if (m == "volume" || m == "dirname" || m == "basename" || m == "parts") {
                ValueList sa{Value::str(inv.s)};
                Value r;
                if (ioSpecMethod(*this, spec, "split", sa, r) && r.t == VT::Hash && r.hash()) {
                    if (m == "parts") { r.hashKind = "IO::Path::Parts"; return r; }
                    auto it = r.hash()->find(m);
                    if (it != r.hash()->end()) return it->second;
                }
            }
            if (m == "cleanup") {
                ValueList sa{Value::str(inv.s)};
                Value r;
                if (ioSpecMethod(*this, spec, "canonpath", sa, r)) {
                    Value p = Value::str(r.toStr()); p.hashKind = "IO"; p.enumName = inv.enumName;
                    return p;
                }
            }
            if (m == "is-absolute" || m == "is-relative") {
                ValueList sa{Value::str(inv.s)};
                Value r;
                if (ioSpecMethod(*this, spec, "is-absolute", sa, r))
                    return m == "is-absolute" ? r : Value::boolean(!r.truthy());
            }
            if (m == "raku") {
                std::string q = inv.s; // escape for a double-quoted literal
                std::string esc; for (char ch : q) { if (ch == '"' || ch == '\\') esc += '\\'; esc += ch; }
                return Value::str("IO::Path::" + inv.enumName + ".new(\"" + esc + "\")");
            }
            if (m == "SPEC") return Value::typeObj(spec);
        }
        if (m == "parent") {
            long long up = args.empty() ? 1 : a0().toInt();
            std::string s = inv.toStr();
            for (long long k = 0; k < up; k++) {
                // relative tops climb: ".".parent is "..", "..".parent "../.."
                // (dirOf answers "." for both, which is the DIRNAME rule, not
                // the parent rule — Test::META resolves its dist dir this way).
                // ONLY a pure ..-chain climbs by appending; any real path
                // ending in "/.." chops like everything else ("a/..".parent
                // is "a" under Rakudo). Appending there made File::Directory::
                // Tree's mktree walk-up diverge forever on "x/bar/../baz":
                // every appended step still contained the nonexistent "x",
                // so .e never came true — the battery's one reliable hang.
                bool pureDots = false;
                if (s == "." ) { s = ".."; continue; }
                if (!s.empty() && s[0] == '.') {
                    pureDots = true;
                    for (size_t i = 0; i < s.size(); i += 3)
                        if (s.compare(i, 2, "..") != 0 ||
                            (i + 2 < s.size() && s[i + 2] != '/')) { pureDots = false; break; }
                    if (s.size() % 3 != 2) pureDots = false;   // "..", "../..", …
                }
                if (pureDots) s += "/..";
                else s = dirOf(s);
            }
            return asIO(s);
        }
        if (m == "dirname") return Value::str(dirOf(inv.toStr()));
        // `.parts` — the (volume, dirname, basename) triple as an
        // IO::Path::Parts, which is Associative on those three keys
        if (m == "parts") {
            std::string full = inv.toStr();
            Value pp = Value::makeHash(); pp.hashKind = "IO::Path::Parts";
            (*pp.hash())["volume"]   = Value::str("");
            (*pp.hash())["dirname"]  = Value::str(dirOf(full));
            std::string b = full; while (b.size() > 1 && b.back() == '/') b.pop_back();
            auto bp = b.find_last_of('/');
            (*pp.hash())["basename"] = Value::str(bp == std::string::npos ? b : b.substr(bp + 1));
            return pp;
        }
        if (m == "sibling") return asIO(dirOf(inv.toStr()) + "/" + (args.empty() ? "" : a0().toStr()));
        if (m == "child" || m == "add") {
            if (!args.empty()) rejectNulPath(args[0].toStr());
            std::string s = inv.toStr(); if (!s.empty() && s.back() == '/') s.pop_back();
            // a bare `.` parent contributes NOTHING: `'.'.IO.child('t')` is `t`,
            // not `./t` (Rakudo's IO::Spec::Unix.join drops a '.' dirname). Only
            // the exact `.` — `'./x'.IO.child('y')` stays `./x/y`. IO::Glob walks
            // a tree from `'.'.IO` and compares the result against bare names.
            bool dotRoot = (s == ".");
            // several parts (or one list argument) append as successive segments:
            // `"foo".IO.add(<bar baz>)` is foo/bar/baz
            for (auto& a : args) {
                if (a.t == VT::Pair) continue;
                for (auto& part : toList(a)) {
                    if (dotRoot) { s = part.toStr(); dotRoot = false; }
                    else { s += "/"; s += part.toStr(); }
                }
            }
            if (args.empty()) s += "/";
            return asIO(s);
        }
        if (m == "extension") {
            std::string b = inv.toStr();
            auto sl = b.find_last_of('/'); if (sl != std::string::npos) b = b.substr(sl + 1);
            // the dot-separated segments — a LEADING dot counts, so ".bashrc"
            // has extension "bashrc" and "...tar" has "tar"
            std::vector<std::string> seg;
            { std::string cur;
              for (char c : b) { if (c == '.') { seg.push_back(cur); cur.clear(); } else cur += c; }
              seg.push_back(cur); }
            long long avail = (long long)seg.size() - 1; if (avail < 0) avail = 0;
            // `:parts(N)` — exactly N tail segments; `:parts(a..b)` — the LARGEST
            // count in the range the name actually has. Neither invents parts.
            long long lo = 1, hi = 1;
            for (auto& a : args)
                if (a.t == VT::Pair && a.s == "parts" && a.pairVal()) {
                    const Value& p = *a.pairVal();
                    if (p.t == VT::Range) {
                        lo = p.rFrom() + (p.rExFrom() ? 1 : 0);
                        hi = p.rTo() >= 9000000000000000000LL ? avail : p.rTo() - (p.rExTo() ? 1 : 0);
                    }
                    else lo = hi = p.toInt();
                }
            long long take = hi < avail ? hi : avail;
            // A positional argument REPLACES the extension instead of reading it:
            // the matched parts come off and `$joiner ~ $new` goes on. The joiner
            // defaults to "" for an empty replacement and "." otherwise, which is
            // why `.extension('')` trims the dot too but `:joiner<_>` keeps one.
            const Value* repl = nullptr;
            for (auto& a : args) if (a.t != VT::Pair) { repl = &a; break; }
            if (repl) {
                std::string nw = repl->toStr(), joiner = nw.empty() ? "" : ".";
                for (auto& a : args)
                    if (a.t == VT::Pair && a.s == "joiner" && a.pairVal()) joiner = a.pairVal()->toStr();
                // no extension of the requested size exists → nothing is replaced
                if (take < lo && lo > 0) return asIO(inv.toStr());
                if (take < 0) take = 0;
                std::string stem;
                for (long long k = 0; k < (long long)seg.size() - take; k++) {
                    if (k) stem += ".";
                    stem += seg[(size_t)k];
                }
                std::string name = stem + joiner + nw;
                if (name.empty()) name = "."; // an empty basename is the current directory
                std::string full = inv.toStr();
                auto sl2 = full.find_last_of('/');
                return asIO(sl2 == std::string::npos ? name : full.substr(0, sl2 + 1) + name);
            }
            if (take < lo || take <= 0) return Value::str("");
            std::string out;
            for (long long k = (long long)seg.size() - take; k < (long long)seg.size(); k++) {
                if (!out.empty()) out += ".";
                out += seg[(size_t)k];
            }
            return Value::str(out);
        }
        if (m == "readlink") { // the link's own target, unresolved (cf. .resolve)
            std::string p = inv.toStr(); char lbuf[4096];
            long long n = platform_readlink(p.c_str(), lbuf, sizeof lbuf - 1);
            if (n < 0) throw RakuError{Value::typeObj("X::IO::Symlink"),
                "Failed to readlink '" + p + "': " + std::strerror(errno)};
            return asIO(std::string(lbuf, (size_t)n)); // an IO::Path, as in Rakudo
        }
        if (m == "resolve") {
            // Rakudo's .resolve FOLLOWS symlinks: it realpaths the longest prefix
            // that exists and appends whatever is left verbatim (so `..` past a
            // missing directory stays literal, since it cannot be crossed).
            // Merely absolutizing, as this used to, made `.resolve` a no-op on
            // macOS, where $*TMPDIR is /var/… and the kernel reports /private/var/….
            std::string s = inv.toStr();
            if (!s.empty() && s[0] != '/') {
                char buf[4096]; if (getcwd(buf, sizeof buf)) s = std::string(buf) + "/" + s;
            }
            std::vector<std::string> segs;
            { std::string cur;
              for (char c : s) { if (c == '/') { if (!cur.empty()) segs.push_back(cur); cur.clear(); } else cur += c; }
              if (!cur.empty()) segs.push_back(cur); }
            std::string tail;
            for (size_t take = segs.size() + 1; take-- > 0; ) {
                std::string pre = "/";
                for (size_t k = 0; k < take; k++) { if (k) pre += "/"; pre += segs[k]; }
                char rbuf[4096];
                if (realpath(pre.c_str(), rbuf)) {
                    std::string out = rbuf;
                    if (!tail.empty()) { if (out != "/") out += "/"; out += tail; }
                    return asIO(out);
                }
                if (take == 0) break;
                tail = tail.empty() ? segs[take - 1] : segs[take - 1] + "/" + tail;
            }
            return asIO(s);
        }
        if (m == "absolute" || m == "canonpath" || m == "cleanup") {
            std::string s = inv.toStr();
            if (m == "absolute" && !s.empty() && s[0] != '/') {
                // `.absolute($base)` resolves against $base rather than $*CWD
                std::string base;
                if (!args.empty()) base = args[0].toStr();
                if (base.empty()) { char buf[4096]; if (getcwd(buf, sizeof buf)) base = buf; }
                if (!base.empty() && base.back() == '/') base.pop_back();
                s = base + "/" + s;
            }
            if (m == "canonpath" || m == "cleanup") {
                // squeeze repeated separators and drop `.` segments — but NOT
                // `..`, which may cross a symlink and so cannot be resolved
                // textually (that is `.resolve`'s job)
                bool abs = !s.empty() && s[0] == '/';
                std::vector<std::string> segs;
                { std::string cur;
                  for (char c : s) { if (c == '/') { if (!cur.empty()) segs.push_back(cur); cur.clear(); } else cur += c; }
                  if (!cur.empty()) segs.push_back(cur); }
                std::vector<std::string> keep;
                for (auto& g : segs) if (g != ".") keep.push_back(g);
                std::string out = abs ? "/" : "";
                for (size_t k = 0; k < keep.size(); k++) { if (k) out += "/"; out += keep[k]; }
                if (out.empty()) out = ".";
                return m == "canonpath" ? Value::str(out) : asIO(out);
            }
            return Value::str(s);   // .absolute is a Str, like .relative
        }
        if (m == "is-absolute") return Value::boolean(!inv.toStr().empty() && inv.toStr()[0] == '/');
        // the path's OS grammar and the directory it is resolved against
        if (m == "SPEC") return Value::typeObj("IO::Spec::" + (inv.enumName.empty() ? std::string("Unix") : inv.enumName.str()));
        if (m == "CWD") {
            if (!inv.ofType().empty()) return Value::str(inv.ofType()); // an explicit :CWD
            char buf[4096]; return Value::str(getcwd(buf, sizeof buf) ? buf : ".");
        }
        if (m == "is-relative") return Value::boolean(inv.toStr().empty() || inv.toStr()[0] != '/');
        if (m == "contents" || m == "dir") {
            // `:test` filters on the BASENAME, smart-matched: a Regex, a Callable,
            // or any object with an ACCEPTS (IO::Glob passes a glob object). The
            // sub form `dir($path, :test)` already honoured it; the method form
            // silently returned everything.
            Value test; bool haveTest = false;
            for (auto& x : args)
                if (x.t == VT::Pair && x.s == "test" && x.pairVal()) { test = *x.pairVal(); haveTest = true; }

            Value out = Value::array(); out.isList = true;
            std::string base = inv.toStr();
            if (DIR* d = opendir(base.c_str())) {
                while (struct dirent* e = readdir(d)) {
                    std::string nm = e->d_name;
                    // `.` and `..` are excluded by the DEFAULT :test only. An
                    // explicit :test replaces that filter, so `dir(:test(*))`
                    // yields them too — IO::Glob's `glob(*).dir` counts on it.
                    if (!haveTest && (nm == "." || nm == "..")) continue;
                    if (haveTest && !matcherAccepts(*this, Value::str(nm), test)) continue;
                    // a `.` directory contributes nothing to the entry's path —
                    // `'.'.IO.dir` yields `META6.json`, not `./META6.json`, the
                    // same rule `.child` follows
                    out.arr()->push_back(asIO(base == "." ? nm
                        : base + (base.empty() || base.back() == '/' ? "" : "/") + nm));
                }
                closedir(d);
            }
            return out;
        }
    }
    if (m == "modified" || m == "created" || m == "accessed" || m == "changed") {
        struct stat st;
        if (stat(inv.toStr().c_str(), &st) != 0)
            throw RakuError{Value::typeObj("X::IO::DoesNotExist"),
                "Failed to get the timestamp of '" + inv.toStr() + "': no such file or directory"};
        // an Instant. Sub-second field names differ by platform; Windows stat only
        // carries second precision.
        double secs;
#if defined(_WIN32)
        time_t t = (m == "accessed") ? st.st_atime : (m == "changed") ? st.st_ctime : st.st_mtime;
        secs = (double)t;
#else
  #if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
        const struct timespec& ats = st.st_atimespec, &cts = st.st_ctimespec, &mts = st.st_mtimespec;
  #else
        const struct timespec& ats = st.st_atim, &cts = st.st_ctim, &mts = st.st_mtim;
  #endif
        const struct timespec& ts = (m == "accessed") ? ats : (m == "changed") ? cts : mts;
        secs = (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
#endif
        // an INSTANT, not a bare Num: `.modified.DateTime` must dispatch
        // (HTTP::Tiny's mirror builds if-modified-since from it)
        Value v = Value::number(secs); v.hashKind = "Instant"; return identify(v);
    }
    if (m == "chmod" && inv.hashKind == "IO") { // $path.IO.chmod(0o644)
        ::chmod(inv.toStr().c_str(), (mode_t)(args.empty() ? 0 : args[0].toInt()));
        Value p = Value::str(inv.toStr()); p.hashKind = "IO"; return p;
    }
    if (m == "open") { // returns a buffered file handle
        Value h = Value::makeHash(); h.hashKind = "FileHandle";
        (*h.hash())["path"] = Value::str(inv.toStr());
        std::string mode = "r"; bool excl = false;
        for (auto& a : args) if (a.t == VT::Pair) {
            if (a.s == "w") mode = "w"; else if (a.s == "a") mode = "a"; else if (a.s == "r") mode = "r";
            else if (a.s == "rw") mode = "rw";
            else if (a.s == "update") mode = "update";
            else if (a.s == "exclusive" || a.s == "x") excl = true;
        }
        if (excl) { // create-new-or-fail — see the open() builtin (File::Temp's claim)
            std::ifstream probe(inv.toStr());
            if (probe) throw RakuError{Value::typeObj("X::IO::Exclusive"),
                "Failed to open file " + inv.toStr() + ": file already exists"};
            if (mode == "r") mode = "w";
        }
        if (mode == "update") {
            std::ifstream probe(inv.toStr());
            if (!probe) throw RakuError{Value::typeObj("X::IO::DoesNotExist"),
                "Failed to open file " + inv.toStr() + ": no such file or directory"};
        }
        (*h.hash())["mode"] = Value::str(mode);
        (*h.hash())["buffer"] = Value::str("");
        if (mode == "w") { std::ofstream create(inv.toStr(), std::ios::trunc); } // the file exists immediately
        if (mode == "rw") { std::ofstream create(inv.toStr(), std::ios::app); }  // exists immediately, kept intact
        if (mode != "r") registerWriteHandle(h.hashS()); // flush at exit if not closed
        return h;
    }
    if (inv.t == VT::Hash && inv.hashKind == "FileHandle") {
        // IO::Handle accessors (with defaults); writable via lvalue()
        if (m == "chomp")  { auto it = inv.hash()->find("chomp");  return it != inv.hash()->end() ? it->second : Value::boolean(true); }
        // .lock/.unlock (flock): rakupp handles are buffered (no live OS fd), so
        // there is nothing to flock; report success. Cross-PROCESS exclusion (zef's
        // lock-file-protect guards concurrent zef runs) is thus not provided — fine
        // for a single interpreter process, revisit if real fd-backed IO lands.
        if (m == "lock" || m == "unlock") return Value::boolean(true);
        if (m == "encoding") { auto it = inv.hash()->find("encoding"); return it != inv.hash()->end() ? it->second : Value::str("utf8"); }
        if (m == "nl-in")  { auto it = inv.hash()->find("nl-in");  return it != inv.hash()->end() ? it->second : Value::str("\n"); }
        if (m == "nl-out") { auto it = inv.hash()->find("nl-out"); return it != inv.hash()->end() ? it->second : Value::str("\n"); }
        if (m == "path" || m == "IO") {
            auto st = inv.hash()->find("std"); // standard streams: an IO::Special
            if (st != inv.hash()->end()) {
                std::string nm = st->second.toStr() == "err" ? "<STDERR>" : st->second.toStr() == "in" ? "<STDIN>" : "<STDOUT>";
                Value sp = Value::str(nm); sp.hashKind = "IO::Special"; return sp;
            }
            return (*inv.hash())["path"];
        }
        if (m == "say" || m == "print" || m == "put" || m == "printf") {
            std::string s;
            if (m == "printf") { // $fh.printf(FMT, args…) — FMT stringifies via .Str (junctions too)
                std::string fmt = args.empty() ? "" : methodCall(args[0], "Str", ValueList{}).toStr();
                ValueList rest(args.begin() + (args.empty() ? 0 : 1), args.end());
                s = doSprintf(fmt, rest, langRev_);
            } else {
                for (auto& a : args) s += (m == "say" ? a.gist() : a.toStr());
                if (m != "print") s += "\n";
            }
            auto stdit = inv.hash()->find("std");
            if (stdit != inv.hash()->end()) { // $*OUT / $*ERR — write straight to the stream
                std::lock_guard<std::mutex> lk(rtOutMutex());
                (stdit->second.toStr() == "err" ? std::cerr : std::cout) << s;
                return Value::boolean(true);
            }
            // Appending to an open handle is a READ-MODIFY-WRITE on state the
            // handle shares, so two threads writing to one file both read the
            // buffer, both append, and one write is simply lost — twelve
            // threads writing a line each produced eleven lines. Under the
            // output lock it is one update at a time.
            {
                std::lock_guard<std::mutex> lk(rtOutMutex());
                Value& buf = (*inv.hash())["buffer"];
                buf = Value::str(buf.toStr() + s);
            }
            return Value::boolean(true);
        }
        if (m == "t") { // is the handle a terminal? files never; std handles ask isatty
            auto stdit = inv.hash()->find("std");
            if (stdit == inv.hash()->end()) return Value::boolean(false);
            std::string which = stdit->second.toStr();
#ifdef _WIN32
            int fd = which == "err" ? 2 : which == "in" ? 0 : 1;
            return Value::boolean(::_isatty(fd) != 0);
#else
            int fd = which == "err" ? 2 : which == "in" ? 0 : 1;
            return Value::boolean(::isatty(fd) != 0);
#endif
        }
        if (m == "write") { // binary write: append the Blob/Buf's raw bytes
            std::string bytes;
            for (auto& a : args) {
                if (a.t == VT::Str) bytes += a.s; // Buf/Blob byte string (or a plain Str's bytes)
                else if ((a.t == VT::Array || a.t == VT::Range) && !(a.t == VT::Array && !a.arr()))
                    for (auto& e : a.flatten()) bytes += (char)(unsigned char)(e.toInt() & 0xFF);
            }
            auto wstd = inv.hash()->find("std");
            if (wstd != inv.hash()->end()) { // $*OUT / $*ERR: straight to the stream,
                std::lock_guard<std::mutex> lk(rtOutMutex()); // as .print does — a std
                std::ostream& os = wstd->second.toStr() == "err" ? std::cerr : std::cout;
                os.write(bytes.data(), (std::streamsize)bytes.size()); // handle has no path
                return Value::boolean(true);                  // to flush a buffer to
            }
            {   // same read-modify-write as .print above, same lock
                std::lock_guard<std::mutex> lk(rtOutMutex());
                Value& buf = (*inv.hash())["buffer"];
                buf = Value::str(buf.toStr() + bytes);
            }
            return Value::boolean(true);
        }
        if (m == "read") { // binary read: up to N bytes from a byte cursor, as a Buf
            long long want = args.empty() ? 65536 : args[0].toInt();
            // $*IN has no path to slurp: read the bytes as they arrive, so a
            // terminal in raw mode delivers each keystroke instead of nothing.
            if (inv.hash()->find("std") != inv.hash()->end() && (*inv.hash())["std"].toStr() == "in") {
                std::string got;
                if (want < 0) want = 0;
                for (long long i = 0; i < want; i++) {
                    int c = std::cin.get();
                    if (c == EOF) break;
                    got += (char)(unsigned char)c;
                }
                Value b = Value::str(got);
                b.hashKind = "Buf"; identify(b);
                return b;
            }
            if (inv.hash()->find("bytes") == inv.hash()->end()) {
                std::ifstream in((*inv.hash())["path"].toStr(), std::ios::binary);
                std::ostringstream ss; ss << in.rdbuf();
                (*inv.hash())["bytes"] = Value::str(ss.str());
                (*inv.hash())["bpos"] = Value::integer(0);
            }
            const std::string& all = (*inv.hash())["bytes"].s;
            long long pos = (*inv.hash())["bpos"].toInt();
            if (pos < 0) pos = 0;
            if (want < 0) want = 0;
            if (pos > (long long)all.size()) pos = all.size();
            long long take = std::min(want, (long long)all.size() - pos);
            Value b = Value::str(all.substr((size_t)pos, (size_t)take));
            b.hashKind = "Buf"; identify(b);
            (*inv.hash())["bpos"] = Value::integer(pos + take);
            return b;
        }
        // .flush — put what has been written on disk NOW, without closing. These
        // handles buffer in memory until .close, so without this a program that
        // flushes deliberately (a log, a trace file read by something else while
        // it runs) saw nothing until it exited — and `.flush` itself did not
        // exist, so it died instead.
        if (m == "flush") {
            auto st = inv.hash()->find("std");
            if (st != inv.hash()->end()) {
                if (st->second.toStr() == "err") std::cerr.flush(); else std::cout.flush();
                return Value::boolean(true);
            }
            std::string mode = (*inv.hash())["mode"].toStr();
            const std::string& buf = (*inv.hash())["buffer"].s;
            if (!buf.empty() && (mode == "w" || mode == "a" || mode == "rw" || mode == "update")) {
                bool wrote = (*inv.hash())["wrote"].truthy();
                std::ofstream out((*inv.hash())["path"].toStr(),
                                  std::ios::binary | ((mode == "a" || wrote) ? std::ios::app : std::ios::trunc));
                if (out) out << buf;
                // The buffer is now on disk: keep only what comes AFTER it, and
                // remember to append from here on. Truncating again at close
                // would delete exactly what the flush was for.
                (*inv.hash())["buffer"] = Value::str("");
                (*inv.hash())["wrote"]  = Value::boolean(true);
            }
            return Value::boolean(true);
        }
        if (m == "close") {
            std::string mode = (*inv.hash())["mode"].toStr();
            const std::string& buf = (*inv.hash())["buffer"].s;
            bool wrote = (*inv.hash())["wrote"].truthy();   // a .flush already put some on disk
            // rw/update flush only when something was WRITTEN — an untouched
            // rw handle on an existing file must not wipe it with a trunc
            bool write = (mode == "w" || mode == "a" || ((mode == "rw" || mode == "update") && !buf.empty()));
            if (wrote && buf.empty()) write = false;      // everything is already there
            if (write) {
                std::ofstream out((*inv.hash())["path"].toStr(),
                                  std::ios::binary | ((mode == "a" || wrote) ? std::ios::app : std::ios::trunc));
                if (out) out << buf;
            }
            (*inv.hash())["flushed"] = Value::boolean(true); // exit-flush skips it now
            return Value::boolean(true);
        }
        if (m == "spurt") { // IO::Handle.spurt($content, :close) — write through the open handle
            Value c = Value::str("");
            bool wantClose = false;              // `:close` writes NOW and shuts the handle
            bool haveContent = false;
            for (auto& a : args) {
                if (a.t == VT::Pair && a.namedArg) {
                    if (a.s == "close") wantClose = !a.pairVal() || a.pairVal()->truthy();
                    continue;                    // …and it may be written AFTER the content
                }
                if (!haveContent) { c = a; haveContent = true; }
            }
            (*inv.hash())["buffer"] = Value::str((*inv.hash())["buffer"].toStr() + c.toStr());
            // Without :close the content sits in "buffer" and reaches the file on
            // .close (zef's spurt-package-list). WITH it, the caller is done with
            // the handle and expects the bytes on disk — File::Temp's own suite
            // writes `$fh.spurt($text, :close)` and then slurps the path back.
            if (wantClose) {
                std::string mode = (*inv.hash())["mode"].toStr();
                const std::string& buf = (*inv.hash())["buffer"].s;
                bool wrote = (*inv.hash())["wrote"].truthy();
                bool write = (mode == "w" || mode == "a" ||
                              ((mode == "rw" || mode == "update") && !buf.empty()));
                if (wrote && buf.empty()) write = false;
                if (write) {
                    std::ofstream out((*inv.hash())["path"].toStr(),
                                      std::ios::binary | ((mode == "a" || wrote) ? std::ios::app : std::ios::trunc));
                    if (out) out << buf;
                }
                (*inv.hash())["buffer"]  = Value::str("");
                (*inv.hash())["wrote"]   = Value::boolean(true);
                (*inv.hash())["flushed"] = Value::boolean(true);
            }
            return Value::boolean(true);
        }
        if (m == "slurp") {
            auto cap = inv.hash()->find("captured"); // in-memory handle (e.g. Proc.out)
            if (cap != inv.hash()->end() && cap->second.truthy()) return (*inv.hash())["buffer"];
            if (inv.hash()->find("std") != inv.hash()->end() && (*inv.hash())["std"].toStr() == "in") {
                std::ostringstream ss; ss << std::cin.rdbuf(); return Value::str(ss.str()); // $*IN.slurp
            }
            std::ifstream in((*inv.hash())["path"].toStr()); std::ostringstream ss; ss << in.rdbuf(); return Value::str(ss.str());
        }
        // .getc / .readchars: load the file's codepoints once, track a cursor in "cpos".
        if (m == "getc" || m == "readchars") {
            if (inv.hash()->find("std") != inv.hash()->end() && (*inv.hash())["std"].toStr() == "in") {
                // Straight off the stream, one UTF-8 character at a time: the
                // whole point on a terminal is that the next character has not
                // been typed yet, so there is nothing to load up front.
                long want = m == "getc" ? 1 : (args.empty() ? 65536 : args[0].toInt());
                std::string out; long got = 0;
                for (; got < want; got++) {
                    int c = std::cin.get();
                    if (c == EOF) break;
                    std::string ch(1, (char)(unsigned char)c);
                    unsigned char lead = (unsigned char)c;   // gather the continuations
                    int extra = lead >= 0xf0 ? 3 : lead >= 0xe0 ? 2 : lead >= 0xc0 ? 1 : 0;
                    for (int k = 0; k < extra; k++) {
                        int cc = std::cin.get();
                        if (cc == EOF) break;
                        ch += (char)(unsigned char)cc;
                    }
                    out += ch;
                }
                if (m == "getc") return out.empty() ? Value::nil() : Value::str(out);
                return Value::str(out);
            }
            if (inv.hash()->find("cps") == inv.hash()->end()) {
                std::string path = (*inv.hash())["path"].toStr();
                struct stat st;
                if (::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
                    throw RakuError{Value::typeObj("X::IO"), "Cannot read characters from a directory: " + path};
                std::ifstream in(path); std::ostringstream ss; ss << in.rdbuf();
                Value cps = Value::array();
                for (auto cp : utf8cp(ss.str())) cps.arr()->push_back(Value::str(cpToUtf8(cp)));
                (*inv.hash())["cps"] = cps;
                (*inv.hash())["cpos"] = Value::integer(0);
            }
            long pos = (*inv.hash())["cpos"].toInt();
            auto& cps = *(*inv.hash())["cps"].arr();
            if (m == "readchars") { // read up to N chars (default 65536), "" at EOF
                long want = args.empty() ? 65536 : args[0].toInt();
                std::string out; long got = 0;
                for (; got < want && pos < (long)cps.size(); got++, pos++) out += cps[pos].toStr();
                (*inv.hash())["cpos"] = Value::integer(pos);
                return Value::str(out);
            }
            if (pos >= (long)cps.size()) return Value::nil(); // getc at EOF → Nil
            (*inv.hash())["cpos"] = Value::integer(pos + 1);
            return cps[pos];
        }
        // reading: lazily load the file into lines, track a cursor in "pos"
        bool isStdin = inv.hash()->find("std") != inv.hash()->end() && (*inv.hash())["std"].toStr() == "in";
        if (m == "get" || m == "getline" || m == "lines" || m == "eof" || m == "words" || m == "slurp-rest") {
            if (inv.hash()->find("lines") == inv.hash()->end()) {
                // a custom line separator (`.nl-in = "+"`) splits on that instead of \n
                std::string sep;
                auto nit = inv.hash()->find("nl-in");
                if (nit != inv.hash()->end()) {
                    if (nit->second.t == VT::Str) sep = nit->second.s;
                    else if (nit->second.t == VT::Array && nit->second.arr() && !nit->second.arr()->empty())
                        sep = (*nit->second.arr())[0].toStr(); // Array nl-in: first separator
                }
                Value lines = Value::array();
                if (!sep.empty() && sep != "\n") {
                    std::string content;
                    if (isStdin) { std::ostringstream ss; ss << std::cin.rdbuf(); content = ss.str(); }
                    else { std::ifstream in((*inv.hash())["path"].toStr()); std::ostringstream ss; ss << in.rdbuf(); content = ss.str(); }
                    size_t start = 0, p;
                    while ((p = content.find(sep, start)) != std::string::npos) {
                        lines.arr()->push_back(Value::str(content.substr(start, p - start)));
                        start = p + sep.size();
                    }
                    if (start < content.size()) lines.arr()->push_back(Value::str(content.substr(start)));
                } else {
                    std::string line;
                    // an IN-MEMORY handle ($*ARGFILES, Proc.out/.err) has its
                    // whole content in "buffer" — there is no file to reopen
                    auto capIt = inv.hash()->find("captured");
                    if (capIt != inv.hash()->end() && capIt->second.truthy()) {
                        const std::string& content = (*inv.hash())["buffer"].s;
                        size_t start = 0;
                        while (start <= content.size()) {
                            size_t nl = content.find('\n', start);
                            if (nl == std::string::npos) {
                                if (start < content.size()) lines.arr()->push_back(Value::str(content.substr(start)));
                                break;
                            }
                            std::string l = content.substr(start, nl - start);
                            if (!l.empty() && l.back() == '\r') l.pop_back();
                            lines.arr()->push_back(Value::str(l));
                            start = nl + 1;
                        }
                    }
                    else if (isStdin) { // $*IN — read standard input
                        while (std::getline(std::cin, line)) {
                            if (!line.empty() && line.back() == '\r') line.pop_back();
                            lines.arr()->push_back(Value::str(line));
                        }
                    } else {
                        std::ifstream in((*inv.hash())["path"].toStr());
                        while (std::getline(in, line)) {
                            if (!line.empty() && line.back() == '\r') line.pop_back();
                            lines.arr()->push_back(Value::str(line));
                        }
                    }
                }
                (*inv.hash())["lines"] = lines;
                (*inv.hash())["pos"] = Value::integer(0);
            }
            if (m == "words") { // remaining input split on whitespace
                auto& ln = *(*inv.hash())["lines"].arr();
                long long p = (*inv.hash())["pos"].toInt();
                std::string all;
                for (long long i = p; i < (long long)ln.size(); i++) { if (!all.empty()) all += "\n"; all += ln[i].toStr(); }
                (*inv.hash())["pos"] = Value::integer((long long)ln.size());
                Value out = Value::array(); out.isList = true;
                std::istringstream ws(all); std::string w;
                while (ws >> w) out.arr()->push_back(Value::str(w));
                return out;
            }
            if (m == "slurp-rest") {
                auto& ln = *(*inv.hash())["lines"].arr();
                long long p = (*inv.hash())["pos"].toInt();
                std::string all;
                for (long long i = p; i < (long long)ln.size(); i++) { all += ln[i].toStr(); all += "\n"; }
                (*inv.hash())["pos"] = Value::integer((long long)ln.size());
                return Value::str(all);
            }
            auto& lines = *(*inv.hash())["lines"].arr();
            long long pos = (*inv.hash())["pos"].toInt();
            if (m == "eof") return Value::boolean(pos >= (long long)lines.size());
            if (m == "lines") {
                Value out = Value::array(); out.isList = true;
                for (long long i = pos; i < (long long)lines.size(); i++) out.arr()->push_back(lines[i]);
                (*inv.hash())["pos"] = Value::integer((long long)lines.size());
                return out;
            }
            // get / getline: next line or Nil at EOF
            if (pos >= (long long)lines.size()) return Value::nil();
            (*inv.hash())["pos"] = Value::integer(pos + 1);
            return lines[pos];
        }
    }
    if (m == "lines" && inv.hashKind == "IO") {
        std::ifstream in(inv.toStr()); Value out = Value::array(); out.isList = true; out.s = "Seq";
        if (!in) throwFailedOpen(inv.toStr());
        std::string line;
        while (std::getline(in, line)) { // strip \r\n too (Windows/HTTP text)
            if (!line.empty() && line.back() == '\r') line.pop_back();
            out.arr()->push_back(Value::str(line));
        }
        return out;
    }

    // string
    // Str/Blob byte views. rakupp stores a Blob/Buf as a Str tagged hashKind="Blob";
    // its raw UTF-8 bytes are the buffer, so encode/decode are (tagged) identity.
    if (m == "new" && inv.t == VT::Str) return Value::str(""); // "literal".new — a fresh empty Str
    if (inv.t == VT::Str && (inv.hashKind == "Blob" || inv.hashKind == "Buf")) {
        if (m.rfind("read-", 0) == 0) { // read-(u)bits / read-num* / read-(u)int*
            Value tmp = inv;
            return bufBitOp(tmp, m, args);
        }
    }
    if ((m == "subbuf" || m == "subbuf-rw") && inv.t == VT::Str && (inv.hashKind == "Buf" || inv.hashKind == "Blob")) {
        // rvalue subbuf-rw reads like subbuf (the writable form is an assignment target)
        long long n = (long long)inv.s.size(), from, len;
        Value a0v = args.empty() ? Value::integer(0) : args[0];
        if (a0v.t == VT::Code && a0v.code()) a0v = callCallable(a0v, ValueList{Value::integer(n)});
        if (a0v.t == VT::Range) { from = a0v.rFrom() + (a0v.rExFrom() ? 1 : 0);
                                  len = (a0v.rTo() - (a0v.rExTo() ? 1 : 0)) - from + 1; }
        else {
            from = a0v.toInt();
            if (args.size() > 1 && args[1].t == VT::Code && args[1].code()) {
                // a Callable count is called with .elems and names the INCLUSIVE
                // end index: subbuf(5, *-3) of 10 elems is bytes 5..7
                Value ev = callCallable(args[1], ValueList{Value::integer(n)});
                len = ev.toInt() - from + 1;
            }
            else if (args.size() > 1 &&
                     (args[1].t == VT::Whatever ||
                      (args[1].t == VT::Num && std::isinf(args[1].n)))) len = n - from; // (5,*) / (5,Inf): to the end
            else len = args.size() > 1 ? args[1].toInt() : n - from;
        }
        if (from < 0) from += n;
        if (from < 0) from = 0; if (from > n) from = n;
        if (len < 0) len = 0; if (from + len > n) len = n - from;
        Value b = Value::str(inv.s.substr((size_t)from, (size_t)len)); b.hashKind = inv.hashKind;
        if (b.hashKind == "Buf") identify(b); // a subbuf is a NEW Buf, not a view
        return b;
    }
    // `.unpack(TEMPLATE)` — the subset the documentation exercises. "C*" is every
    // byte as an integer; a count is a repeat, `*` is "all that remain".
    if (m == "unpack" && inv.t == VT::Str && (inv.hashKind == "Buf" || inv.hashKind == "Blob")) {
        const std::string tmpl = args.empty() ? "" : args[0].toStr();
        const std::string& d = inv.s;
        Value out = Value::array(); out.isList = true;
        size_t pos = 0;
        for (size_t k = 0; k < tmpl.size(); k++) {
            char dir = tmpl[k];
            if (ascii::isspace((unsigned char)dir)) continue;
            bool all = false; long long cnt = 1;
            if (k + 1 < tmpl.size() && tmpl[k + 1] == '*') { all = true; k++; }
            else if (k + 1 < tmpl.size() && ascii::isdigit((unsigned char)tmpl[k + 1])) {
                size_t j = k + 1; std::string num;
                while (j < tmpl.size() && ascii::isdigit((unsigned char)tmpl[j])) num += tmpl[j++];
                cnt = std::stoll(num); k = j - 1;
            }
            auto left = [&] { return d.size() > pos ? (long long)(d.size() - pos) : 0LL; };
            auto be = [&](int w) { long long v = 0;
                for (int b2 = 0; b2 < w && pos < d.size(); b2++) v = (v << 8) | (unsigned char)d[pos++];
                return v; };
            auto le = [&](int w) { long long v = 0; int sh = 0;
                for (int b2 = 0; b2 < w && pos < d.size(); b2++, sh += 8)
                    v |= (long long)(unsigned char)d[pos++] << sh;
                return v; };
            if (dir == 'C' || dir == 'c') {
                long long r = all ? left() : cnt;
                for (long long i2 = 0; i2 < r && pos < d.size(); i2++)
                    out.arr()->push_back(Value::integer((unsigned char)d[pos++]));
            } else if (dir == 'A' || dir == 'a' || dir == 'Z') {
                long long r = all ? left() : cnt;
                std::string t; for (long long i2 = 0; i2 < r && pos < d.size(); i2++) t += d[pos++];
                out.arr()->push_back(Value::str(t));
            } else if (dir == 'H') {
                long long r = all ? left() * 2 : cnt;
                std::string t; static const char* hx = "0123456789abcdef";
                for (long long i2 = 0; i2 < r && pos < d.size(); i2 += 2) {
                    unsigned char c2 = (unsigned char)d[pos++];
                    t += hx[c2 >> 4]; if (i2 + 1 < r) t += hx[c2 & 0xF];
                }
                out.arr()->push_back(Value::str(t));
            } else if (dir == 'x') {
                pos += (size_t)(all ? left() : cnt);
            } else {
                int w = (dir == 'n' || dir == 'S' || dir == 'v') ? 2 : 4;
                bool bigEnd = (dir == 'n' || dir == 'N');
                long long r = all ? left() / w : cnt;
                for (long long i2 = 0; i2 < r && pos < d.size(); i2++)
                    out.arr()->push_back(Value::integer(bigEnd ? be(w) : le(w)));
            }
        }
        // Rakudo hands back the VALUE when the template produced exactly one —
        // `.unpack("A3")` is a Str and `.unpack("C1")` an Int, not a 1-element list.
        if (out.arr()->size() == 1) return (*out.arr())[0];
        return out;
    }
    if (m == "bytes" && inv.t == VT::Str) return Value::integer((long long)inv.s.size());
    // A List/Array is a Cool, so `.encode` STRINGIFIES first — `(1,2).encode` is
    // "1 2".encode. `.uc`, `.chars` and friends already worked that way because
    // they carry no type guard; `.encode` was gated to Str and so died with
    // "No such method 'encode' for invocant of type 'List'" (rakupp#12), which is
    // what `[~]($l>>.&bencode.encode)` hits.
    //
    // `.decode` is deliberately NOT included: Rakudo rejects it on a List
    // ("Did you mean 'encode'?"), because decoding a stringified list is nonsense.
    if (m == "encode" && (inv.t == VT::Array || inv.t == VT::Range) && inv.hashKind.empty()) {
        ValueList a2 = args;
        return methodCall(Value::str(inv.toStr()), "encode", a2, nullptr);
    }
    if ((m == "encode" || m == "decode") && inv.t == VT::Str) {
        // normalize the encoding name: utf8 (default) or latin-1/iso-8859-1
        std::string enc;
        for (auto& a : args) if (a.t != VT::Pair) { enc = a.toStr(); break; }
        std::string norm;
        for (char ch : enc) if (ascii::isalnum((unsigned char)ch)) norm += (char)ascii::tolower((unsigned char)ch);
        bool latin1 = norm == "iso88591" || norm == "latin1" || norm == "windows1252";
        if (m == "encode") {
            // `:replacement` substitutes for every character the encoding cannot
            // represent; a bare `:replacement` means "?". Without it an
            // unencodable character is an error in Rakudo — we keep the byte.
            bool haveRepl = false; std::string repl;
            for (auto& a : args)
                if (a.t == VT::Pair && a.s == "replacement" && (!a.pairVal() || a.pairVal()->truthy())) {
                    haveRepl = true;
                    repl = (a.pairVal() && a.pairVal()->t == VT::Str) ? a.pairVal()->s.str() : std::string("?");
                }
            bool ascii = norm == "ascii" || norm == "usascii";
            Value b;
            if (latin1) { // one byte per codepoint (<= 0xFF; others become '?')
                std::string bytes;
                for (uint32_t cp : utf8cp(inv.s))
                    if (cp <= 0xFF) bytes += (char)(unsigned char)cp;
                    else bytes += haveRepl ? repl : "?";
                b = Value::str(bytes);
            } else if (ascii && haveRepl) {
                std::string bytes;
                for (uint32_t cp : utf8cp(inv.s)) { if (cp < 0x80) bytes += (char)cp; else bytes += repl; }
                b = Value::str(bytes);
            } else if (norm.rfind("utf16", 0) == 0 || norm.rfind("utf32", 0) == 0) {
                // utf-16: 16-bit code units with surrogate pairs; utf-32: raw codepoints.
                // Little-endian words in the byte string, ofType carries the width so
                // .values/.elems/[] see CODE UNITS, not bytes (JSON::Tiny \u-escaping).
                bool u16 = norm.rfind("utf16", 0) == 0;
                std::string bytes;
                auto word = [&](uint32_t u, int w) { for (int i = 0; i < w; i++) bytes += (char)((u >> (8 * i)) & 0xFF); };
                for (uint32_t cp : utf8cp(inv.s)) {
                    if (!u16) word(cp, 4);
                    else if (cp < 0x10000) word(cp, 2);
                    else { uint32_t v = cp - 0x10000; word(0xD800 | (v >> 10), 2); word(0xDC00 | (v & 0x3FF), 2); }
                }
                b = Value::str(bytes);
                b.ofTypeM() = u16 ? "uint16" : "uint32";
                b.hashKind = "Blob";
                b.enumName = u16 ? "utf16" : "utf32";
                return b;
            } else b = Value::str(inv.s); // utf8/ascii: the bytes as stored
            b.hashKind = "Blob";
            // the ENCODING names the result type (`"abc".encode` is a utf8, a
            // Blob subtype). Kept in enumName so every `hashKind == "Blob"`
            // check still sees a Blob.
            // The ENCODING names the result type: utf8 for the default, and
            // Blob[uint8] for latin-1 — Rakudo reports the latter from `.^name`
            // and renders it that way in `.raku`, which is what the documented
            // `(try $blob.decode) // $blob` fallback prints when it keeps the blob.
            b.enumName = latin1 ? "Blob[uint8]" : "utf8";
            return b;
        }
        // decode: the invocant is a byte string (Buf/Blob).
        // A bare `.decode` on a utf16/utf32 blob decodes with the blob's OWN
        // encoding (Rakudo ties the default to the blob type) — treating the
        // 16-bit words as UTF-8 bytes dies on any surrogate (encode.t #32).
        if (norm.empty()) {
            if (inv.enumName == "utf16" || inv.ofType() == "uint16" || inv.ofType() == "int16") norm = "utf16";
            else if (inv.enumName == "utf32" || inv.ofType() == "uint32" || inv.ofType() == "int32") norm = "utf32";
        }
        if (latin1) { // each byte is a codepoint
            std::string out;
            for (unsigned char byte : inv.s) {
                if (byte < 0x80) out += (char)byte;
                else { out += (char)(0xC0 | (byte >> 6)); out += (char)(0x80 | (byte & 0x3F)); }
            }
            return Value::str(out);
        }
        // utf-16 / utf-32: read fixed-width code units, form codepoints (with
        // surrogate pairing for utf-16), then re-encode as our UTF-8 NFG string.
        if (norm.rfind("utf16", 0) == 0 || norm.rfind("utf32", 0) == 0) {
            const unsigned char* p = (const unsigned char*)inv.s.data();
            size_t n = inv.s.size();
            bool utf32 = norm.rfind("utf32", 0) == 0;
            bool be = norm.size() > 5 && norm.substr(5) == "be";
            size_t w = utf32 ? 4 : 2;
            size_t i = 0;
            // a leading BOM in the plain (endianness-unspecified) form picks endianness
            if (norm == "utf16" || norm == "utf32") {
                if (n >= w) {
                    unsigned long u0 = 0, uN = 0;
                    for (size_t k = 0; k < w; k++) { u0 = (u0 << 8) | p[k]; uN |= (unsigned long)p[k] << (8 * k); }
                    if (u0 == 0xFEFF) { be = true;  i = w; }        // BE BOM
                    else if (uN == 0xFEFF) { be = false; i = w; }   // LE BOM
                    // no BOM: default to little-endian (matches buf16/buf32 packing)
                }
            }
            auto unit = [&](size_t off) -> unsigned long {
                unsigned long u = 0;
                if (be) for (size_t k = 0; k < w; k++) u = (u << 8) | p[off + k];
                else    for (size_t k = 0; k < w; k++) u |= (unsigned long)p[off + k] << (8 * k);
                return u;
            };
            std::string out;
            while (i + w <= n) {
                unsigned long u = unit(i); i += w;
                uint32_t cp;
                if (!utf32 && u >= 0xD800 && u <= 0xDBFF && i + w <= n) { // high surrogate
                    unsigned long lo = unit(i);
                    if (lo >= 0xDC00 && lo <= 0xDFFF) { cp = 0x10000 + ((uint32_t)(u - 0xD800) << 10) + (uint32_t)(lo - 0xDC00); i += w; }
                    else cp = (uint32_t)u;
                } else cp = (uint32_t)u;
                out += cpToUtf8(cp);
            }
            return Value::str(nfcNormalize(out));
        }
        // utf8-c8 ("UTF-8 Clean-8") exists precisely to round-trip malformed bytes
        // without throwing, so it must NOT be validated — validating it killed all
        // 56 assertions of S32-str/utf8-c8.t.
        if (norm == "utf8c8") return Value::str(inv.s);
        // utf8: the bytes ARE the string — but only if they are valid UTF-8.
        // Returning them unchecked let malformed input through, where it later
        // rendered as U+FFFD; Rakudo throws, which is what makes the documented
        // `(try $blob.decode) // $blob` fallback work at all (rakupp#12).
        {
            const unsigned char* p = (const unsigned char*)inv.s.data();
            size_t n = inv.s.size();
            size_t line = 1, col = 1;
            for (size_t i = 0; i < n; ) {
                unsigned char c = p[i];
                size_t len = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2
                           : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
                bool ok = len > 0 && i + len <= n;
                for (size_t k = 1; ok && k < len; k++) if ((p[i + k] & 0xC0) != 0x80) ok = false;
                if (ok && len == 2 && c < 0xC2) ok = false;                       // overlong
                if (ok && len == 3 && c == 0xE0 && p[i + 1] < 0xA0) ok = false;   // overlong
                if (ok && len == 4 && c == 0xF0 && p[i + 1] < 0x90) ok = false;   // overlong
                if (ok && len == 4 && c > 0xF4) ok = false;                       // > U+10FFFF
                if (ok && len == 3 && c == 0xED && p[i + 1] >= 0xA0) ok = false;  // surrogate
                if (!ok) {
                    char buf[8]; std::snprintf(buf, sizeof buf, "%02x", c);
                    throw RakuError{Value::typeObj("X::AdHoc"),
                        "Malformed UTF-8 near byte " + std::string(buf) +
                        " at line " + std::to_string(line) + " col " + std::to_string(col)};
                }
                if (c == '\n') { line++; col = 1; } else col++;
                i += len;
            }
        }
        return Value::str(inv.s);
    }
    if (m == "chars" || m == "codes" || m == "NFC" || m == "NFD" || m == "NFKC" || m == "NFKD") {
        if (m == "chars") return Value::integer(inv.t == VT::Str ? cowGraphemeCount(inv.s)
                                                             : graphemeCount(inv.toStr())); // graphemes
        if (m == "codes") return Value::integer(cpCount(inv.toStr()));       // codepoints
        int mode = m == "NFD" ? 0 : m == "NFC" ? 1 : m == "NFKD" ? 2 : 3;
        auto norm = uniNormalize(utf8cp(inv.toStr()), mode);
        // tag it with the NORMALISATION FORM, not the generic "Uni": `"x".NFD` is
        // an NFD, and both `.gist` (NFD:0x<…>) and `.raku` (Uni.new(…).NFD) read
        // this. The Uni type-object constructor a few hundred lines up already
        // tags correctly; only this Str-method path flattened them all to "Uni".
        Value out = Value::array(); out.s = m;
        for (auto c : norm) out.arr()->push_back(Value::integer((long long)c));
        return out;
    }
    if (m == "unimatch") { // method form delegates to the sub
        ValueList a2; a2.push_back(inv);
        for (auto& a : args) a2.push_back(a);
        auto it = builtins_.find("unimatch");
        if (it != builtins_.end()) return it->second(*this, a2);
    }
    if (m == "uninames") { // one name per grapheme, as a Seq
        Value out = Value::array(); out.isList = true; out.s = "Seq";
        for (uint32_t cp : utf8cp(inv.toStr())) {
            std::string nm = uniNameOf(cp);
            out.arr()->push_back(Value::str(nm.empty() ? "<unassigned>" : nm));
        }
        return out;
    }
    // `.uniparse` as a METHOD — the invocant NAMES the character(s) to build
    // ('TWO HEARTS, BUTTERFLY'.uniparse), the mirror of `.uniname`
    // `.parse-names` is the same method under its older name, and both live on
    // Cool — every Cool stringifies first, so `(3).uniparse` names no character
    // and fails with X::Str::InvalidCharName rather than "no such method".
    if ((m == "uniparse" || m == "parse-names") &&
        (inv.t == VT::Str || inv.t == VT::Match || inv.isNumeric() ||
         inv.t == VT::Array || inv.t == VT::Range ||
         (inv.t == VT::Hash && (inv.hashKind.empty() || inv.hashKind == "Hash" || inv.hashKind == "Map")))) {
        auto it = builtins_.find("uniparse");
        if (it != builtins_.end()) { ValueList ua{Value::str(strOf(inv))}; return it->second(*this, ua); }
    }
    if (m == "unival" || m == "univals" || m == "uniname") {
        if (inv.t == VT::Type)
            throw RakuError{Value::typeObj("X::Multi::NoMatch"), "Cannot call " + m + " with a type object"};
        // a character with no numeric value has unival NaN, not Nil — `'a'.unival`
        // is a Num you can compare, and `.univals` interleaves them with the reals
        auto univ = [](uint32_t cp) -> Value { long long num, den; if (!uniNumValue(cp, num, den)) return Value::number(std::nan("")); return den == 1 ? Value::integer(num) : Value::rat(BigInt(num), BigInt(den)); };
        if (m == "univals") { Value out = Value::array(); out.isList = true; for (uint32_t cp : utf8cp(inv.toStr())) out.arr()->push_back(univ(cp)); return out; }
        uint32_t cp; bool have = true;
        if (inv.t == VT::Int || inv.t == VT::Bool) cp = (uint32_t)inv.toInt();
        else { auto cps = utf8cp(inv.toStr()); if (cps.empty()) have = false; else cp = cps[0]; }
        if (m == "uniname") {
            if (inv.t == VT::Str && inv.s.empty()) return Value::nil(); // uniname("") is Nil
            if ((inv.t == VT::Int || inv.t == VT::Bool) && inv.toInt() < 0)
                return Value::str("<illegal>"); // negative codepoints
            char lb[32];
            std::string gc = have ? uniGeneralCategory(cp) : "";
            if (have && gc == "Cc") { // controls have no Name property, only a label
                snprintf(lb, sizeof lb, "<control-%04X>", cp); return Value::str(lb);
            }
            std::string nm = have ? uniNameOf(cp) : "";
            if (!nm.empty()) return Value::str(nm);
            if (!have || cp > 0x10FFFF) {
                // 6.e answers a Failure for a codepoint that has no name at all
                // (out of Unicode's range, or nothing to read); before it the
                // string "<unassigned>" stands in, which reads like a name.
                if (sixE()) {
                    Value f = Value::makeHash(); f.hashKind = "Failure";
                    (*f.hash())["exception"] = Value::typeObj("X::AdHoc");
                    (*f.hash())["message"]   = Value::str("Unassigned codepoint: 0x" +
                                                        [&]{ char b[16]; snprintf(b, sizeof b, "%X", cp); return std::string(b); }());
                    return f;
                }
                return Value::str("<unassigned>");
            }
            const char* kind = ((cp & 0xFFFE) == 0xFFFE || (cp >= 0xFDD0 && cp <= 0xFDEF)) ? "noncharacter"
                             : gc == "Cs" ? "surrogate"
                             : gc == "Co" ? "private-use"
                             : "reserved";
            snprintf(lb, sizeof lb, "<%s-%04X>", kind, cp);
            return Value::str(lb);
        }
        return have ? univ(cp) : Value::nil();
    }
    if (m == "uniprop" || m == "uniprops") {
        // one property of one codepoint; .uniprops maps every codepoint.
        // String-valued properties answer strings, numeric ones numbers, and
        // any other name is treated as a BINARY property (strict — an unknown
        // name is False, never a lenient match).
        if (inv.t == VT::Type) // uniprop needs a Cool (Str/Int), not a type object
            throw RakuError{Value::typeObj("X::Multi::NoMatch"), "Cannot call " + m + " with a type object"};
        // …and the PROPERTY is named by a string. Every other candidate in
        // Rakudo's signature set takes a Stringy, so `"a".uniprop(0)` resolves
        // to nothing rather than quietly asking for the property named "0".
        if (!args.empty() && args[0].t != VT::Str && args[0].t != VT::Match)
            throw RakuError{Value::typeObj("X::Multi::NoMatch"),
                            "Cannot resolve caller " + m.s + "(" + inv.typeName() + ":D: " +
                            args[0].typeName() + "); the property must be named by a string"};
        std::string prop = args.empty() ? "General_Category" : args[0].toStr();
        auto caseMapStr = [](uint32_t cp, int kind) -> Value { // full mapping as a Str
            std::string s; for (uint32_t c : uniCaseMap(cp, kind)) s += cpToUtf8(c); return Value::str(s);
        };
        auto simpleMapStr = [](uint32_t cp, uint32_t mapped) -> Value { return Value::str(cpToUtf8(mapped)); };
        auto one = [&](uint32_t cp) -> Value {
            if (prop == "General_Category" || prop == "gc") return Value::str(uniGeneralCategory(cp));
            if (prop == "Script" || prop == "sc") return Value::str(uniScript(cp));
            if (prop == "Name" || prop == "na") return Value::str(uniNameOf(cp));
            if (prop == "Block" || prop == "blk") return Value::str(uniBlockOf(cp));
            if (prop == "Bidi_Class" || prop == "bc") return Value::str(uniBidiClassOf(cp));
            if (prop == "Canonical_Combining_Class" || prop == "ccc")
                return Value::str(uniCombiningClassName(cp)); // by ALIAS: 0 is "Not_Reordered"
            if (prop == "Unicode_1_Name" || prop == "na1") return Value::str(uniUnicode1Name(cp));
            if (prop == "Jamo_Short_Name" || prop == "JSN") return Value::str(uniJamoShortName(cp));
            if (prop == "Bidi_Paired_Bracket" || prop == "bpb")
                return Value::str(cpToUtf8(uniBidiPairedBracket(cp)));
            if (prop == "Bidi_Paired_Bracket_Type" || prop == "bpt")
                return Value::str(uniBidiPairedBracketType(cp));
            if (prop == "Numeric_Value" || prop == "nv") {
                long long nu, de;
                if (!uniNumValue(cp, nu, de)) return Value::number(std::nan(""));
                return de == 1 ? Value::integer(nu) : Value::ratZ(BigInt(nu), BigInt(de));
            }
            // full case mappings answer a Str, simple ones a single-codepoint Str
            if (prop == "Uppercase_Mapping" || prop == "uc") return caseMapStr(cp, 1);
            if (prop == "Lowercase_Mapping" || prop == "lc") return caseMapStr(cp, 0);
            if (prop == "Titlecase_Mapping" || prop == "tc") return caseMapStr(cp, 2);
            if (prop == "Case_Folding" || prop == "cf")       return caseMapStr(cp, 3);
            if (prop == "Simple_Uppercase_Mapping" || prop == "suc") return simpleMapStr(cp, uniSimpleUpper(cp));
            if (prop == "Simple_Lowercase_Mapping" || prop == "slc") return simpleMapStr(cp, uniSimpleLower(cp));
            if (prop == "Simple_Titlecase_Mapping" || prop == "stc") return simpleMapStr(cp, uniSimpleTitle(cp));
            if (prop == "Bidi_Mirroring_Glyph" || prop == "bmg") {
                int32_t m2 = uniBidiMirror(cp); return Value::str(m2 < 0 ? "" : cpToUtf8((uint32_t)m2));
            }
            if (prop == "ISO_Comment" || prop == "isc") return Value::str(""); // empty since Unicode 5.2
            // enumerated properties (Age, Line_Break, East_Asian_Width, Numeric_Type, …)
            std::string ev = uniEnumProp(prop, cp);
            if (!ev.empty()) return Value::str(ev);
            // otherwise a binary property — strict (unknown names are False, not a lenient match)
            int b = uniBinaryProp(cp, prop);
            return Value::boolean(b == 1);
        };
        std::vector<uint32_t> cps;
        if (inv.t == VT::Int || inv.t == VT::Bool) cps.push_back((uint32_t)inv.toInt());
        else cps = utf8cp(inv.toStr());
        if (m == "uniprop") return cps.empty() ? Value::str("") : one(cps[0]);
        Value out = Value::array(); out.isList = true; out.s = "Seq";
        for (uint32_t cp : cps) out.arr()->push_back(one(cp));
        return out;
    }
    if (m == "uc") return Value::str(mapCase(inv.toStr(), 1, 0));
    if (m == "lc") return Value::str(mapCase(inv.toStr(), 0, 0));
    if (m == "tc") return Value::str(mapCase(inv.toStr(), 0, 1));
    if (m == "tclc") return Value::str(mapCase(inv.toStr(), 0, 2));
    if (m == "indent" && !args.empty()) { // add (negative: remove) indentation, AFTER existing leading whitespace
        // the amount is a real number, not whatever toInt() makes of it:
        // `.indent("a")` is X::Str::Numeric, not an indent of zero
        long long amt = args[0].t == VT::Str && !args[0].isAllomorph() && args[0].hashKind.empty()
                      ? numifyStrOrThrow(args[0].s).toInt() : args[0].toInt();
        auto isWs = [](uint32_t c) {
            return c == 0x09 || c == 0x0B || c == 0x0C || c == 0x0D || c == 0x20 || c == 0x85 || c == 0xA0 ||
                   c == 0x1680 || (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 ||
                   c == 0x202F || c == 0x205F || c == 0x3000;
        };
        std::string s = inv.toStr(), out; size_t i = 0;
        while (i <= s.size()) {
            size_t nl = s.find('\n', i);
            std::string line = s.substr(i, nl == std::string::npos ? std::string::npos : nl - i);
            auto cps = utf8cp(line);
            size_t lead = 0; while (lead < cps.size() && isWs(cps[lead])) lead++;
            std::string leadStr, rest;
            for (size_t k = 0; k < lead; k++) leadStr += cpToUtf8(cps[k]);
            for (size_t k = lead; k < cps.size(); k++) rest += cpToUtf8(cps[k]);
            if (amt >= 0) { if (!line.empty()) out += leadStr + std::string((size_t)amt, ' ') + rest; }
            else { size_t drop = std::min((size_t)(-amt), lead);
                   std::string kept; for (size_t k = 0; k < lead - drop; k++) kept += cpToUtf8(cps[k]);
                   out += kept + rest; }
            if (nl == std::string::npos) break;
            out += '\n'; i = nl + 1;
        }
        return Value::str(out);
    }
    if (m == "wordcase") { // titlecase each word (first letter up, rest down)
        auto cps = utf8cp(inv.toStr());
        std::string r; bool wordStart = true;
        for (uint32_t c : cps) {
            bool ws = c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == 0x0B;
            if (ws) { r += cpToUtf8(c); wordStart = true; }
            else { r += cpToUtf8(wordStart ? toUpperCp(c) : toLowerCp(c)); wordStart = false; }
        }
        return Value::str(r);
    }
    if (m == "fc") return Value::str(mapCase(inv.toStr(), 3, 0));
    // The same-* family all take a DEFINED Str pattern: `.samecase(Any)` has no
    // candidate in Rakudo, and treating the type object as "" silently returned
    // the invocant unchanged.
    if (m == "samecase" || m == "samespace" || m == "samemark") {
        if (args.empty() || args[0].t == VT::Type || args[0].t == VT::Any || args[0].t == VT::Nil)
            throw RakuError{Value::typeObj("X::Multi::NoMatch"),
                            "Cannot resolve caller " + m.s + "(" + inv.typeName() + ":D: " +
                            (args.empty() ? std::string("") : args[0].typeName() + ":U") +
                            "); the pattern must be a defined Str"};
    }
    if (m == "samecase") { // copy the case pattern of the arg, position by position (last char repeats)
        auto src = utf8cp(inv.toStr());
        auto pat = utf8cp(args.empty() ? "" : args[0].toStr());
        std::string r;
        for (size_t i = 0; i < src.size(); i++) {
            uint32_t c = src[i];
            uint32_t mask = pat.empty() ? 0 : pat[std::min(i, pat.size() - 1)];
            if (mask && toLowerCp(mask) != mask) r += cpToUtf8(toUpperCp(c));      // mask is upper
            else if (mask && toUpperCp(mask) != mask) r += cpToUtf8(toLowerCp(c)); // mask is lower
            else r += cpToUtf8(c);                                                 // uncased: unchanged
        }
        return Value::str(r);
    }
    // `.samespace($pat)` — copy the pattern's WHITESPACE RUNS onto the
    // invocant's, run by run in order. A run the pattern does not reach keeps
    // whatever the invocant had, so "a b c".samespace("x  y") is "a  b c".
    if (m == "samespace") {
        std::string s = inv.toStr(), p = args.empty() ? std::string() : args[0].toStr();
        auto isws = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; };
        std::vector<std::string> pruns; // the pattern's whitespace runs, in order
        for (size_t i = 0; i < p.size(); ) {
            if (!isws((unsigned char)p[i])) { i++; continue; }
            size_t j = i; while (j < p.size() && isws((unsigned char)p[j])) j++;
            pruns.push_back(p.substr(i, j - i));
            i = j;
        }
        std::string r; size_t k = 0;
        for (size_t i = 0; i < s.size(); ) {
            if (!isws((unsigned char)s[i])) { r += s[i]; i++; continue; }
            size_t j = i; while (j < s.size() && isws((unsigned char)s[j])) j++;
            r += k < pruns.size() ? pruns[k] : s.substr(i, j - i);
            k++;
            i = j;
        }
        return Value::str(r);
    }
    if (m == "flip") { // reverses GRAPHEMES: a combining mark stays behind its base
        auto cps = utf8cp(inv.toStr());
        GraphemeMap gm(cps);
        std::string r;
        for (size_t g = gm.count(); g-- > 0; )
            for (size_t k = gm.cpAt(g), e = gm.cpAt(g + 1); k < e; k++) r += cpToUtf8(cps[k]);
        return Value::str(r);
    }
    // A Seq, not an Array — `Nil.ords` is `().Seq` (S02-types/nil.t). It said
    // Array until `eqv` learned to tell the two apart, at which point the test
    // stopped passing for the wrong reason.
    if (m == "ords") { Value out = Value::array(); out.isList = true; out.s = "Seq"; for (auto cp : uniNormalize(utf8cp(inv.toStr()), 1 /*NFC: .ords returns grapheme ordinals*/)) out.arr()->push_back(Value::integer(cp)); return out; }
    // `Any.nl-out` — documented on Any as returning the string "\n" (the output
    // line ending an IO::Handle would use). It is a plain constant there; the
    // per-handle value lives on IO::Handle.
    if (m == "nl-out") return Value::str("\n");
    if (m == "chomp") { // a logical newline: "\n", "\r\n" or a lone "\r"
        // …or, given a needle, that exact suffix: `"a".chomp("a")` is "". The
        // needle must be DEFINED — an undefined one binds to no candidate, and
        // ignoring it chomped a newline that was never asked about.
        if (!args.empty()) {
            if (args[0].t == VT::Type || args[0].t == VT::Any || args[0].t == VT::Nil)
                throw RakuError{Value::typeObj("X::Multi::NoMatch"),
                                "Cannot resolve caller chomp(" + inv.typeName() + ":D: " +
                                args[0].typeName() + ":U); the needle must be defined"};
            std::string s = inv.toStr(), needle = strOf(args[0]);
            if (!needle.empty() && s.size() >= needle.size() &&
                s.compare(s.size() - needle.size(), needle.size(), needle) == 0)
                s.resize(s.size() - needle.size());
            return Value::str(s);
        }
        std::string s = inv.toStr();
        if (!s.empty() && s.back() == '\n') s.pop_back();
        if (!s.empty() && s.back() == '\r') s.pop_back();
        return Value::str(s);
    }
    if (m == "trim") { std::string s = inv.toStr(); size_t a = s.find_first_not_of(" \t\n\r"); size_t b = s.find_last_not_of(" \t\n\r"); return Value::str(a == std::string::npos ? "" : s.substr(a, b - a + 1)); }
    if (m == "trim-leading") { std::string s = inv.toStr(); size_t a = s.find_first_not_of(" \t\n\r"); return Value::str(a == std::string::npos ? "" : s.substr(a)); }
    if (m == "trim-trailing") { std::string s = inv.toStr(); size_t b = s.find_last_not_of(" \t\n\r"); return Value::str(b == std::string::npos ? "" : s.substr(0, b + 1)); }
    if (m == "substr" || m == "substr-rw") {
        // Raku indexes by GRAPHEME, so `n` counts clusters and every cut lands on a
        // cluster boundary. Indexing codepoints directly splits "ŕ̥" into its base
        // and its combining ring, which is how `substr` and `chars` came to
        // disagree. But when a byte index IS a grapheme index — ASCII, no CR —
        // none of that machinery is needed, and skipping it is what stops a
        // `.substr($i, 1)` loop being quadratic in the string. Only these two
        // primitives differ; every argument shape below is decided once.
        // Both of these used to be O(length) on EVERY call: toStr() copies the
        // whole invocant, and byteIsGraphemeIndex rescans it. A `.substr($i, 1)`
        // loop calls this per character, so the pair put the quadratic straight
        // back that the caching in cowByteIsGraphemeIndex exists to remove
        // (STRING-SCAN-QUADRATICS.md §5). When the invocant is already a Str,
        // read its CowStr in place and take the verdict off the shared body.
        // `substr-rw` keeps the snapshot: its write-back path may reach the same
        // storage, and a reference into it would be reading a moving target.
        // `enumName` is the catch: Value::toStr on a Str-valued ENUM answers the
        // enum key, not the string, so `inv.s` is not interchangeable with
        // `inv.toStr()` for those. Everything else with t == VT::Str is.
        const bool cowStr = inv.t == VT::Str && inv.enumName.empty();
        const bool cowOk = cowStr && m == "substr";
        std::string rawCopy;
        if (!cowOk) rawCopy = inv.toStr();
        const std::string& raw = cowOk ? inv.s.str() : rawCopy;
        const bool plain = cowStr ? cowByteIsGraphemeIndex(inv.s)
                                  : byteIsGraphemeIndex(raw);
        // Non-ASCII text: take the grapheme→byte table cached on the body
        // (built once) rather than decoding the string and building a
        // GraphemeMap PER CALL — which made a `.substr($i, 1)` loop over a
        // string with one `é` O(n²) all over again, this time in the
        // non-ASCII lane (STRING-SCAN-QUADRATICS, the 52.8 s JSON::Fast
        // parse). The per-call path remains for short/unpromoted strings.
        const std::vector<uint32_t>* gt = (!plain && cowOk) ? cowGraphemeIndex(inv.s) : nullptr;
        std::vector<uint32_t> cps;
        std::unique_ptr<GraphemeMap> gm;
        if (!plain && !gt) { cps = utf8cp(raw); gm.reset(new GraphemeMap(cps)); }
        long long n = plain ? (long long)raw.size()
                    : gt    ? (long long)gt->size() - 1
                            : (long long)gm->count();
        auto slice = [&](long long lo, long long hi) { // [lo, hi) in graphemes
            std::string r;
            if (hi <= lo) return r;
            if (plain) return raw.substr((size_t)lo, (size_t)(hi - lo));
            if (gt) { size_t a = (*gt)[(size_t)lo]; return raw.substr(a, (*gt)[(size_t)hi] - a); }
            size_t a = gm->cpAt((size_t)lo), b = gm->cpAt((size_t)hi);
            for (size_t k = a; k < b; k++) r += cpToUtf8(cps[k]);
            return r;
        };
        // a RANGE gives both ends at once: `substr("Long string", 3..6)`
        if (!args.empty() && args[0].t == VT::Range) {
            const Value& rg = args[0];
            long long lo = rg.rFrom() + (rg.rExFrom() ? 1 : 0);
            long long hi = rg.rTo() - (rg.rExTo() ? 1 : 0);
            if (lo < 0) lo += n;
            if (hi < 0) hi += n;
            if (lo < 0) lo = 0;
            if (hi >= n) hi = n - 1;
            return Value::str(slice(lo, hi + 1)); // the Range end is INCLUSIVE
        }
        // the START may be a Whatever/WhateverCode too — `*-3` counts from the end
        long long start;
        if (!args.empty() && args[0].t == VT::Whatever) start = n;
        else if (!args.empty() && args[0].t == VT::Code) {
            ValueList wa{Value::integer(n)}; start = callCallable(args[0], wa).toInt();
        }
        else start = a0().toInt();
        if (start < 0) start += n;
        if (start < 0) start = 0;
        if (start > n) start = n;
        // The length may be a Whatever/WhateverCode: `*` means "to the end" and
        // `*-1` etc. is called with the max available length (n - start).
        long long len;
        if (args.size() <= 1) len = n - start;
        else if (args[1].t == VT::Whatever) len = n - start;
        else if (args[1].t == VT::Code) {
            // `*-1` as a LENGTH counts back from the END of the string, not from
            // the remaining tail — `substr($s, *-3, *-1)` keeps all but the last
            ValueList wa{Value::integer(n)}; len = callCallable(args[1], wa).toInt() - start;
        }
        else len = args[1].toInt();
        if (len < 0) len = n - start + len;
        if (len < 0) len = 0;
        if (start + len > n) len = n - start;
        return Value::str(slice(start, start + len));
    }
    if (m == "index" || m == "rindex") {
        // splatted multi-needle: index($s, "a", "o", :i) — several positional
        // STRING args are all needles (a numeric-looking string is a start pos)
        {
            size_t nPos = 0; bool allStrNeedles = true;
            for (auto& av : args) {
                if (av.t == VT::Pair) continue;
                nPos++;
                if (nPos == 1) continue;
                bool strNum = false;
                if (av.t == VT::Str && !av.s.empty()) {
                    char* end = nullptr;
                    cnum::strtod(av.s.c_str(), &end);
                    strNum = end && *end == 0;
                }
                if (av.t != VT::Str || strNum) { allStrNeedles = false; break; }
            }
            if (nPos >= 2 && allStrNeedles) {
                Value best; bool have = false;
                for (auto& nd : args) {
                    if (nd.t == VT::Pair) continue;
                    ValueList sub{nd};
                    for (auto& av : args) if (av.t == VT::Pair) sub.push_back(av);
                    Value r = methodCall(inv, m, sub);
                    if (r.t == VT::Int &&
                        (!have || (m == "index" ? r.i < best.i : r.i > best.i))) {
                        best = r; have = true;
                    }
                }
                return have ? best : Value::nil();
            }
        }
        // a LIST of needles: the best (leftmost for index, rightmost for
        // rindex) position across all of them
        if (!args.empty() && (args[0].t == VT::Array || args[0].t == VT::Range)) {
            Value best; bool have = false;
            for (auto& nd : args[0].flatten()) {
                ValueList sub{nd};
                for (size_t i = 1; i < args.size(); i++) sub.push_back(args[i]);
                Value r = methodCall(inv, m, sub);
                if (r.t == VT::Int &&
                    (!have || (m == "index" ? r.i < best.i : r.i > best.i))) {
                    best = r; have = true;
                }
            }
            return have ? best : Value::nil();
        }
        // positions are in characters, not bytes; the optional 2nd arg is the
        // start (index) / rightmost-allowed start (rindex) position.
        // `:i`/`:ignorecase` case-folds both sides before comparing.
        bool icase = false, imark = false, smart = false;
        for (auto& av : args)
            if (av.t == VT::Pair) {
                if (av.s == "i" || av.s == "ignorecase") icase = !av.pairVal() || av.pairVal()->truthy();
                else if (av.s == "smartcase") smart = !av.pairVal() || av.pairVal()->truthy(); // 6.e

                // `:ignoremark` compares base characters; the fold is
                // grapheme-for-grapheme, so the answered position still indexes
                // the ORIGINAL string
                else if (av.s == "m" || av.s == "ignoremark") imark = !av.pairVal() || av.pairVal()->truthy();
            }
        // Same pair of O(length)-per-call costs as `substr` above, on a method
        // a scanning loop calls per character: two whole-string copies and two
        // uncached rescans. With no `:ignoremark` fold nothing is rewritten, so
        // the two operands can be read in place and their verdicts taken off the
        // shared bodies (STRING-SCAN-QUADRATICS.md §5).
        // …with the same enum caveat as `substr` above: a Str-valued enum
        // stringifies to its KEY, so only a plain Str may be read in place.
        const bool cowHay = !imark && inv.t == VT::Str && inv.enumName.empty();
        const bool cowNdl = !imark && !args.empty() && args[0].t == VT::Str &&
                            args[0].enumName.empty();
        std::string hayCopy, ndlCopy;
        if (!cowHay) { hayCopy = inv.toStr(); if (imark) hayCopy = markFold(hayCopy); }
        if (!cowNdl) { ndlCopy = a0().toStr(); if (imark) ndlCopy = markFold(ndlCopy); }
        const std::string& hay = cowHay ? inv.s.str() : hayCopy;
        const std::string& ndl = cowNdl ? args[0].s.str() : ndlCopy;
        if (smart && sixE()) icase = strHasNoUpper(ndl);
        // Plain needle in a plain haystack, no folding: positions are byte
        // positions and std::string::find is the whole algorithm. Worth taking
        // before the decode, since a scan over a long string calls this per
        // character. `from` is still parsed below in the general path.
        if (!icase && !imark &&
            (cowHay ? cowByteIsGraphemeIndex(inv.s) : byteIsGraphemeIndex(hay)) &&
            (cowNdl ? cowByteIsGraphemeIndex(args[0].s) : byteIsGraphemeIndex(ndl)) &&
            (args.size() <= 1 || !args[1].isNumeric())) {
            size_t at = m == "index" ? hay.find(ndl) : hay.rfind(ndl);
            return at == std::string::npos ? Value::nil() : Value::integer((long long)at);
        }
        auto cps = utf8cp(hay); auto ncps = utf8cp(ndl);
        if (icase) { for (auto& c : cps) c = toLowerCp(c); for (auto& c : ncps) c = toLowerCp(c); }
        // Positions are GRAPHEME indices, on the way in (the start argument) and on
        // the way out (the answer) — and a match must begin on a cluster boundary,
        // so a lone combining mark does not "find" the inside of a cluster.
        GraphemeMap hg(cps), ng(ncps);
        long long n = (long long)hg.count(), k = (long long)ng.count();
        long long from = m == "index" ? 0 : n;
        if (args.size() > 1 && args[1].isNumeric()) {
            double fd = args[1].toNum();
            // only a NEGATIVE (or int64-overflowing) start is out of range; a
            // moderate position past the end just yields no match (Nil), matching
            // Rakudo (`index("Hello","l",10)` is Nil, but `…,-1` / `…,1e35` throw)
            if (fd < 0 || fd > 9.2e18) {
                // fails-like wants a RETURNED Failure whose typed exception
                // carries the offending value in .got
                Value f = Value::makeHash(); f.hashKind = "Failure";
                (*f.hash())["exception"] = makeTypedEx("X::OutOfRange",
                    {{"got", args[1]}, {"what", Value::str("start argument to " + m)},
                     {"range", Value::str("0.." + std::to_string(n))}},
                    "start argument to " + m + " out of range. Is: " + args[1].gist() +
                    "; should be in 0.." + std::to_string(n));
                return f;
            }
            from = args[1].toInt();
            if (m == "rindex" && from > n) from = n; // rindex clamps the rightmost start
        }
        auto eq = [&](long long at) { // `at` is a grapheme index into the haystack
            if (at < 0 || at + k > n) return false;
            size_t ha = hg.cpAt((size_t)at), hb = hg.cpAt((size_t)(at + k));
            if (hb - ha != ncps.size()) return false; // same cluster count, different codepoints
            for (size_t j = 0; j < ncps.size(); j++) if (cps[ha + j] != ncps[j]) return false;
            return true;
        };
        if (m == "index") {
            for (long long at = from; at + k <= n; at++)
                if (eq(at)) return Value::integer(at);
        }
        else {
            for (long long at = std::min(from, n - k); at >= 0; at--)
                if (eq(at)) return Value::integer(at);
        }
        return Value::nil();
    }
    // ---- regex-argument string methods ----
    // Find the Regex argument and the replacement — a named adverb (`:g`) may come
    // before the regex (`.subst(:g, /re/, repl)`), so we can't assume positions.
    int rxIdx = -1;
    for (size_t i = 0; i < args.size(); i++) if (args[i].t == VT::Regex) { rxIdx = (int)i; break; }
    // `"abc".match("b")` — a Str needle is a LITERAL pattern, not a regex.
    // An ARRAY needle is its elements joined by a space (`.match([1,2,3])`).
    if (m == "match" && inv.t == VT::Str && inv.hashKind.empty() && !args.empty() &&
        ((args[0].t == VT::Str && args[0].hashKind.empty()) ||
         (args[0].t == VT::Array && args[0].arr()))) {
        const std::string& subj = inv.s;
        std::string needle;
        if (args[0].t == VT::Array) {
            for (auto& e : *args[0].arr()) { if (!needle.empty()) needle += " "; needle += e.toStr(); }
        } else needle = args[0].s;
        bool anyAdverb = false, allKnown = true;
        for (auto& a : args) if (a.t == VT::Pair && a.namedArg) {
            anyAdverb = true;
            if (!substSelectKnowsAdverb(a.s)) allKnown = false;
        }
        if (anyAdverb && allKnown) { // the adverbs mean the same thing for a literal needle
            long nsub = 0; Value mres;
            std::string keep = subj;
            ValueList sargs = args;
            substSelect(subj, needle, nullptr, sargs, nsub, true, &keep, &mres);
            return mres;
        }
        size_t p = subj.find(needle);
        if (p == std::string::npos) return Value::nil();
        return Value::matchVal(needle, (long)p, (long)(p + needle.size()));
    }
    if ((m == "match" || m == "subst" || m == "comb" || m == "split" || m == "contains" || m == "subst-mutate")
        && rxIdx >= 0) {
        std::string subj = rxSubject(inv);   // an object matches on its Str form
        // `/@alpha/` — array elements as a longest-first literal alternation
        // (Base64 decodes via `$str.comb(/@alpha/)`); match/subst interpolate later
        // `$var` atoms in the pattern resolve here too — `.split(/$d+/)`,
        // `.comb(/$sep/)`, `.subst(/$old/, …)` all compile a raw regex source, and
        // without this pass they matched the literal text "$d".
        std::string pat = rxInterpArrays(interpRegexPattern(args[rxIdx].s));
        // the replacement is the first positional (non-Pair) arg that isn't the regex
        Value* replArg = nullptr;
        for (size_t i = 0; i < args.size(); i++)
            if ((int)i != rxIdx && args[i].t != VT::Pair) { replArg = &args[i]; break; }
        if (m == "match") {
            // Any adverb at all (`:g`, `:x(2)`, `:2nd`, `:continue(2)`, `:pos(2)`,
            // `:exhaustive`, …) goes through the SAME occurrence-selection code
            // s/// uses — it already implements the whole family. The replacement
            // is a no-op there; only the selected matches are wanted.
            bool anyAdverb = false, allKnown = true;
            for (auto& a : args) if (a.t == VT::Pair && a.namedArg) {
                anyAdverb = true;
                if (!substSelectKnowsAdverb(a.s)) allKnown = false;
            }
            if (anyAdverb && allKnown) {
                long nsub = 0; Value mres;
                std::string keep = subj;                 // replace each match with itself
                ValueList sargs = args;
                substSelect(subj, pat, nullptr, sargs, nsub, false, &keep, &mres);
                return mres;
            }
            return regexMatch(subj, pat);
        }
        if (m == "contains") { // an optional second positional is where to start
            long from = 0;
            for (size_t i = 0; i < args.size(); i++)
                if ((int)i != rxIdx && args[i].t != VT::Pair)
                    { from = (long)charToByte(subj, args[i].toInt()); break; }
            Regex re(pat); RxMatch mm;
            return Value::boolean(re.ok() && from <= (long)subj.size() && re.search(subj, from, mm));
        }
        if (m == "subst") {
            long nsub = 0;
            std::string out = substSelect(subj, pat, replArg, args, nsub);
            return Value::str(out);
        }
        if (m == "comb") {
            // `:match` — the elements are the MATCH OBJECTS, captures included
            // (Sparrow6's check engine: `$data.comb(/<m=$p>/,:match)>>.<m>`).
            // The plain path below answers strings, so a capture lookup on an
            // element quietly gave Any. Same machinery as `.match(:g)`; the
            // optional positional limit truncates the list.
            bool wantMatch = false;
            for (auto& la : args)
                if (la.t == VT::Pair && la.namedArg && la.s == "match" &&
                    (!la.pairVal() || la.pairVal()->truthy())) wantMatch = true;
            if (wantMatch) {
                long nsub = 0; Value mres;
                std::string keep = subj;                 // replace each match with itself
                ValueList sargs; sargs.push_back(args[rxIdx]);
                Value g = Value::pair("g", Value::boolean(true)); g.namedArg = true;
                sargs.push_back(g);
                substSelect(subj, pat, nullptr, sargs, nsub, false, &keep, &mres);
                long long limit = -1;
                for (size_t i = 0; i < args.size(); i++)
                    if ((int)i != rxIdx && args[i].t != VT::Pair && args[i].t != VT::Whatever)
                        { limit = args[i].toInt(); break; }
                if (limit >= 0 && mres.t == VT::Array && mres.arr() &&
                    (long long)mres.arr()->size() > limit)
                    mres.arr()->resize(limit);
                return mres;
            }
            Regex re(pat);
            // a `<?{…}>` in the pattern must run, here as much as in `~~`
            GrammarHooks ch = codeAssertHooks();
            if (patHasCodeAssert(pat)) re.runHooks = &ch; Value out = Value::array(); out.isList = true; out.s = "Seq"; long pos = 0; RxMatch mm;
            while (re.ok() && pos <= (long)subj.size() && re.search(subj, pos, mm)) {
                out.arr()->push_back(Value::str(subj.substr(mm.from, mm.to - mm.from)));
                pos = mm.to > mm.from ? mm.to : mm.to + 1;
            }
            return out;
        }
        if (m == "split") {
            Regex re(pat);
            // a `<?{…}>` in the pattern must run, here as much as in `~~`
            GrammarHooks ch = codeAssertHooks();
            if (patHasCodeAssert(pat)) re.runHooks = &ch; Value out = Value::array(); out.isList = true; out.s = "Seq"; long pos = 0; RxMatch mm;
            bool skipEmpty = false;
            // `:v`/`:k`/`:kv`/`:p` — the separator comes back between the pieces as
            // a Match, as its delimiter index (always 0 here: one delimiter), or both
            char want = 0;
            for (auto& la : args)
                if (la.t == VT::Pair && (!la.pairVal() || la.pairVal()->truthy())) {
                    if (la.s == "skip-empty") skipEmpty = true;
                    else if (la.s == "v" || la.s == "k" || la.s == "p") want = la.s[0];
                    else if (la.s == "kv") want = 'm';
                }
            auto emit = [&](const std::string& piece) { if (!(skipEmpty && piece.empty())) out.arr()->push_back(Value::str(piece)); };
            // optional limit (second positional): <=0 → empty, 1 → the whole string,
            // n → at most n pieces
            long long limit = -12345;
            for (auto& la : args) if (la.t != VT::Pair && la.t != VT::Regex) {
                if (la.t != VT::Whatever) limit = la.toInt(); // a `*` limit means unlimited
                break;
            }
            bool haveLimit = limit != -12345;
            if (haveLimit && limit <= 0) return out;
            if (haveLimit && limit == 1) { emit(subj); return out; }
            while (re.ok() && pos <= (long)subj.size() && re.search(subj, pos, mm)) {
                if (haveLimit && (long long)out.arr()->size() >= limit - 1) break;
                if (mm.to == mm.from && mm.from == pos) { if (pos >= (long)subj.size()) break; }
                emit(subj.substr(pos, mm.from - pos));
                if (want) {
                    Value sepv = Value::matchVal(subj.substr(mm.from, mm.to - mm.from),
                                                 (long)mm.from, (long)mm.to);
                    Value idx = Value::integer(0);
                    if (want == 'k' || want == 'm') out.arr()->push_back(idx);
                    if (want == 'v' || want == 'm') out.arr()->push_back(sepv);
                    if (want == 'p') {
                        Value pr = Value::pair("0", sepv);
                        pr.pairKeyM() = std::make_shared<Value>(idx);
                        out.arr()->push_back(std::move(pr));
                    }
                }
                pos = mm.to > mm.from ? mm.to : mm.to + 1;
            }
            emit(subj.substr(std::min((size_t)pos, subj.size())));
            return out;
        }
    }
    if (m == "subst" && args.size() >= 1) { // literal (string) substitution
        // named adverbs are position-independent: `.subst(:g, '%2A', '*')`
        // (HTTP::Request's form-escape) puts :g FIRST — the pattern is the
        // first POSITIONAL, the replacement the second
        std::string s = inv.toStr(), from;
        Value* replArg = nullptr;
        bool haveFrom = false;
        for (size_t i = 0; i < args.size(); i++) {
            if (args[i].t == VT::Pair && args[i].namedArg) continue;
            if (!haveFrom) { from = args[i].toStr(); haveFrom = true; }
            else { replArg = &args[i]; break; }
        }
        if (from.empty()) return Value::str(s);
        long nsub = 0;
        return Value::str(substSelect(s, from, replArg, args, nsub, /*literal=*/true));
    }
    if (m == "samemark" && !args.empty()) {
        // copy the combining marks of the pattern's clusters onto the invocant's
        // base characters, cluster by cluster (last pattern cluster repeats)
        auto clusters = [](const std::string& s) {
            std::vector<std::pair<uint32_t, std::vector<uint32_t>>> out;
            for (uint32_t cp : uniNormalize(utf8cp(s), 0)) { // NFD
                std::string gc = uniGeneralCategory(cp);
                if (!out.empty() && (gc == "Mn" || gc == "Mc" || gc == "Me"))
                    out.back().second.push_back(cp);
                else out.push_back({cp, {}});
            }
            return out;
        };
        auto sc = clusters(inv.toStr()), pc = clusters(args[0].toStr());
        if (pc.empty()) return Value::str(inv.toStr());
        std::vector<uint32_t> res;
        for (size_t i = 0; i < sc.size(); i++) {
            res.push_back(sc[i].first);
            auto& marks = pc[std::min(i, pc.size() - 1)].second;
            res.insert(res.end(), marks.begin(), marks.end());
        }
        std::string outs;
        for (uint32_t cp : uniNormalize(res, 1)) outs += cpToUtf8(cp); // NFC
        return Value::str(outs);
    }
    // 6.e `.nomark`: the string with every mark stripped from its graphemes —
    // "élan vitál" is "elan vital". markFold is the same fold `:ignoremark`
    // compares with, which is exactly this operation.
    if (m == "nomark" && sixE() &&
        (inv.t == VT::Str || inv.t == VT::Int || inv.t == VT::Num || inv.t == VT::Rat))
        return Value::str(markFold(inv.toStr()));
    if (m == "trans") { // $s.trans(@from => @to) / .trans('abc' => 'xyz') / .trans('a..c' => 'A..C')
        std::string s = inv.toStr();
        // a string arg is taken char-by-char, but `X..Y` denotes an inclusive codepoint range
        auto expandTrans = [](const std::string& str) -> std::vector<std::string> {
            std::vector<std::string> out;
            auto cps = utf8cp(str);
            for (size_t i = 0; i < cps.size(); ) {
                if (i + 3 < cps.size() && cps[i + 1] == (uint32_t)'.' && cps[i + 2] == (uint32_t)'.') {
                    uint32_t lo = cps[i], hi = cps[i + 3];
                    if (lo <= hi) for (uint32_t c = lo; c <= hi; c++) out.push_back(cpToUtf8(c));
                    else for (uint32_t c = lo; ; c--) { out.push_back(cpToUtf8(c)); if (c == hi) break; }
                    i += 4;
                } else { out.push_back(cpToUtf8(cps[i])); i++; }
            }
            return out;
        };
        // `:c`/`:complement` translates everything NOT named on the left, `:d`/`:delete`
        // drops what has no replacement, `:s`/`:squash` collapses a RUN of the same
        // replacement to one. An adverb is a Pair with a Bool value — a mapping pair
        // always has a Str/Range/Array one, so `'squash' => 'x'` still translates.
        bool squash = false, complement = false, del = false;
        std::string compTo; // what :complement replaces an unnamed character with
        std::vector<std::pair<std::string, std::string>> maps;
        for (auto& a : args) {
            if (a.t == VT::Pair && (!a.pairVal() || a.pairVal()->t == VT::Bool)) {
                bool on = !a.pairVal() || a.pairVal()->truthy();
                if (a.s == "s" || a.s == "squash")          { squash = on; continue; }
                if (a.s == "c" || a.s == "complement")      { complement = on; continue; }
                if (a.s == "d" || a.s == "delete")          { del = on; continue; }
            }
            if (a.t != VT::Pair) continue;
            std::vector<std::string> froms, tos;
            // an Array side may hold Range ELEMENTS (['a'..'c']); flatten()
            // descends into them, and a bare Range side flattens to its chars
            if (a.pairKey() && (a.pairKey()->t == VT::Array || a.pairKey()->t == VT::Range))
                for (auto& x : a.pairKey()->flatten()) froms.push_back(x.toStr());
            else froms = expandTrans(a.s); // string key: char-by-char, with `..` ranges
            if (a.pairVal() && (a.pairVal()->t == VT::Array || a.pairVal()->t == VT::Range))
                for (auto& x : a.pairVal()->flatten()) tos.push_back(x.toStr());
            else if (a.pairVal()) tos = expandTrans(a.pairVal()->toStr());
            // a SHORTER replacement side CYCLES: `.trans("abcd" => "xy")` is xyxy
            for (size_t i = 0; i < froms.size(); i++)
                maps.push_back({froms[i], tos.empty() ? std::string() : tos[i % tos.size()]});
            // remembered unconditionally: `:complement` may be given AFTER the mapping
            if (!tos.empty()) compTo = tos.back(); // :c replaces with the LAST replacement
        }
        std::string out;
        const std::string* lastTo = nullptr; // for :squash — what the previous position emitted
        for (size_t pos = 0; pos < s.size(); ) {
            size_t bestLen = 0; const std::string* bestTo = nullptr;
            for (auto& kv : maps) {
                if (!kv.first.empty() && kv.first.size() > bestLen &&
                    s.compare(pos, kv.first.size(), kv.first) == 0) { bestLen = kv.first.size(); bestTo = &kv.second; }
            }
            if (complement) {
                // the left side names what to KEEP; everything else is replaced
                size_t clen = 1; // one CHARACTER, not one byte
                while (pos + clen < s.size() && ((unsigned char)s[pos + clen] & 0xC0) == 0x80) clen++;
                if (bestLen) { out.append(s, pos, bestLen); pos += bestLen; lastTo = nullptr; continue; }
                if (!del || !compTo.empty()) {
                    if (!(squash && lastTo == &compTo)) out += compTo;
                    lastTo = &compTo;
                }
                pos += clen;
                continue;
            }
            if (bestTo) {
                if (!(squash && lastTo == bestTo)) out += *bestTo;
                lastTo = bestTo;
                pos += bestLen;
            } else {
                out += s[pos]; pos++; lastTo = nullptr;
            }
        }
        return Value::str(out);
    }
    // `:i`/`:ignorecase` on the string predicates — fold both sides and compare
    if (m == "contains" || m == "starts-with" || m == "ends-with") {
        bool icase = false, imark = false, smart = false;
        for (auto& a2 : args)
            if (a2.t == VT::Pair) {
                if (a2.s == "i" || a2.s == "ignorecase")
                    icase = !a2.pairVal() || a2.pairVal()->truthy();   // bare `:i` is true
                else if (a2.s == "smartcase")
                    smart = !a2.pairVal() || a2.pairVal()->truthy();   // 6.e: decided by the needle
                else if (a2.s == "m" || a2.s == "ignoremark")
                    imark = !a2.pairVal() || a2.pairVal()->truthy();
            }
        auto fold = [](const std::string& in) {
            auto cps = utf8cp(in); std::string o;
            for (auto c : cps) o += cpToU8(toLowerCp(c));
            return o;
        };
        std::string s = inv.toStr(), n = a0().toStr();
        if (smart && sixE()) icase = strHasNoUpper(n);
        if (imark) { s = markFold(s); n = markFold(n); }
        if (icase) { s = fold(s); n = fold(n); }
        // `.contains($needle, $pos)` starts the search at CHARACTER $pos
        size_t from = 0;
        if (m == "contains") {
            for (size_t i = 1; i < args.size(); i++)
                if (args[i].t != VT::Pair) { from = charToByte(s, args[i].toInt()); break; }
            return Value::boolean(from <= s.size() && s.find(n, from) != std::string::npos);
        }
        if (m == "starts-with") return Value::boolean(s.size() >= n.size() && s.compare(0, n.size(), n) == 0);
        return Value::boolean(s.size() >= n.size() && s.compare(s.size() - n.size(), n.size(), n) == 0);
    }
    if (m == "substr-eq") { // does the substring starting at pos equal the needle?
        if (args.empty() || (args[0].t == VT::Type && args.size() < 2))
            throw RakuError{Value::typeObj("X::AdHoc"), "Cannot call substr-eq without a needle string"};
        bool icase = false, imark = false, smart = false;
        ValueList pargs;
        for (auto& a2 : args) {
            if (a2.t == VT::Pair && a2.s == "smartcase")
                smart = !a2.pairVal() || a2.pairVal()->truthy();          // 6.e
            else if (a2.t == VT::Pair && (a2.s == "i" || a2.s == "ignorecase"))
                icase = !a2.pairVal() || a2.pairVal()->truthy();
            else if (a2.t == VT::Pair && (a2.s == "m" || a2.s == "ignoremark"))
                imark = !a2.pairVal() || a2.pairVal()->truthy();
            else if (a2.t != VT::Pair) pargs.push_back(a2);
        }
        if (pargs.empty()) // only adverbs given, no needle — a clean error, not an OOB read
            throw RakuError{Value::typeObj("X::AdHoc"), "Cannot call substr-eq without a needle string"};
        if (smart && sixE()) icase = strHasNoUpper(pargs[0].toStr());
        std::string s = inv.toStr(), n = pargs[0].toStr();
        // `:ignoremark` compares base characters; the fold keeps one character
        // per grapheme, so the POSITION still indexes the original
        if (imark) { s = markFold(s); n = markFold(n); }
        long long len = (long long)methodCall(inv, "chars", ValueList{}).toInt();
        long long pos = 0;
        if (pargs.size() > 1) // a Code position (*-4) resolves against .chars
            pos = pargs[1].t == VT::Code ? callCallable(pargs[1], ValueList{Value::integer(len)}).toInt()
                                         : pargs[1].toInt();
        if (pos < 0 || pos > len) { // out of range FAILS (fails-like X::OutOfRange)
            Value f = Value::makeHash(); f.hashKind = "Failure";
            (*f.hash())["exception"] = Value::typeObj("X::OutOfRange");
            return f;
        }
        // taken from `s`, which is the mark-folded text when :ignoremark is on
        Value sub = methodCall(Value::str(s), "substr", ValueList{Value::integer(pos),
                                                        methodCall(Value::str(n), "chars", ValueList{})});
        if (!icase) return Value::boolean(sub.toStr() == n);
        Value a1 = methodCall(sub, "lc", ValueList{}), b1 = methodCall(Value::str(n), "lc", ValueList{});
        return Value::boolean(a1.toStr() == b1.toStr());
    }

    if (m == "ord") {
        // Only the FIRST character is wanted; decoding the rest was pure waste,
        // and it made `$s.substr($i,1).ord` in a loop quadratic in $s.
        const std::string s = inv.toStr();
        if (s.empty()) return Value::nil();
        auto c = utf8cp(s.substr(0, 4));   // a codepoint is at most 4 bytes
        return Value::integer(c[0]);
    }
    if (m == "chr") {
        long long cp = inv.big() ? LLONG_MAX : inv.toInt(); // BigInt is certainly out of bounds
        if (cp < 0 || cp > 0x10FFFF)
            throw RakuError{Value::typeObj("X::AdHoc"),
                "chr codepoint " + (inv.big() ? inv.big()->toString() : std::to_string(cp)) + " is out of bounds"};
        return Value::str(cpToUtf8((uint32_t)cp));
    }
    if (m == "split") {
        std::string s = inv.toStr();
        Value d0 = a0();
        struct Delim { bool isRx; std::string str; };
        std::vector<Delim> delims;
        // A regex delimiter's source still carries its `$var` atoms: `.split(/$d+/)`
        // must resolve $d the same way `~~ /$d+/` does. Compiling the raw source
        // instead matched a literal "$d" and split nothing.
        auto add = [&](const Value& d) {
            if (d.t == VT::Regex) delims.push_back({true, interpRegexPattern(d.s)});
            else delims.push_back({false, d.toStr()});
        };
        if (d0.t == VT::Array) { for (auto& e : *d0.arr()) add(e); } else add(d0);
        // `:v`/`:k`/`:kv`/`:p` interleave the SEPARATORS with the pieces — as the
        // matched text, the matching delimiter's INDEX in the delimiter list, both,
        // or index => text. A regex delimiter yields a Match, a literal one a Str.
        char want = 0;
        for (auto& a : args)
            if (a.t == VT::Pair && (!a.pairVal() || a.pairVal()->truthy())) {
                if (a.s == "v" || a.s == "k" || a.s == "p") want = a.s[0];
                else if (a.s == "kv") want = 'm';
            }
        bool keepSep = false, skipEmpty = false, fromEnd = false;
        long long limit = -1; bool haveLimit = false; // second positional (a `*` means unlimited)
        { bool first = true;
          for (auto& a : args) {
              if (a.t == VT::Pair) {
                  if (a.pairVal() && a.pairVal()->truthy()) {
                      if (a.s == "v" || a.s == "kv" || a.s == "k" || a.s == "p") keepSep = true;
                      else if (a.s == "skip-empty") skipEmpty = true;
                      else if (a.s == "end") fromEnd = true; // limit applies from the END (2026.06)
                  }
                  continue;
              }
              if (first) { first = false; continue; } // the delimiter itself
              if (!haveLimit && a.t != VT::Whatever) { limit = a.toInt(); haveLimit = true; }
              // keep scanning: adverbs may follow the limit (.split(",", 2, :v))
          }
        }
        Value out = Value::array();
        out.isList = true; out.s = "Seq";
        if (haveLimit && limit <= 0) return out;
        auto emit = [&](const std::string& piece) { if (!(skipEmpty && piece.empty())) out.arr()->push_back(Value::str(piece)); };
        // empty single delimiter => split into characters, with the empty-string
        // edges Rakudo yields ('abc'.split('') is ("", "a", "b", "c", ""));
        // a limit keeps the first limit-1 pieces and the rest as the final piece
        if (delims.size() == 1 && !delims[0].isRx && delims[0].str.empty()) {
            if (s.empty()) return out; // ''.split('') is ()
            auto cps = utf8cp(s);
            if (haveLimit && limit == 1) { emit(s); return out; }
            emit("");
            size_t taken = 0;
            for (size_t ci = 0; ci < cps.size(); ci++) {
                if (haveLimit && (long long)out.arr()->size() == limit - 1) {
                    std::string rest; for (size_t cj = ci; cj < cps.size(); cj++) rest += cpToUtf8(cps[cj]);
                    emit(rest); return out;
                }
                out.arr()->push_back(Value::str(cpToUtf8(cps[ci])));
                taken = ci;
            }
            (void)taken;
            if (!haveLimit || (long long)out.arr()->size() < limit) emit("");
            return out;
        }
        // collect every separator match, then apply the limit by VALUE count
        // (separators from :v never count) from the front — or the end (:end)
        struct Sep { size_t at, len, which; };
        std::vector<Sep> seps;
        size_t pos = 0;
        while (pos <= s.size()) {
            size_t bestStart = std::string::npos, bestLen = 0, bestWhich = 0; // earliest, then longest match
            for (size_t di = 0; di < delims.size(); di++) {
                const Delim& d = delims[di];
                if (d.isRx) {
                    Regex re(d.str); RxMatch mm;
                    if (re.ok() && re.search(s, (long)pos, mm) && mm.to > mm.from) {
                        size_t st = mm.from, ln = mm.to - mm.from;
                        if (st < bestStart || (st == bestStart && ln > bestLen)) { bestStart = st; bestLen = ln; bestWhich = di; }
                    }
                } else if (!d.str.empty()) {
                    size_t f = s.find(d.str, pos);
                    if (f != std::string::npos && (f < bestStart || (f == bestStart && d.str.size() > bestLen))) { bestStart = f; bestLen = d.str.size(); bestWhich = di; }
                }
            }
            if (bestStart == std::string::npos) break;
            seps.push_back({bestStart, bestLen, bestWhich});
            pos = bestStart + (bestLen ? bestLen : 1);
        }
        size_t keep = haveLimit ? (size_t)std::max(0LL, limit - 1) : seps.size();
        if (keep > seps.size()) keep = seps.size();
        size_t k0 = (fromEnd && haveLimit) ? seps.size() - keep : 0;
        size_t k1 = (fromEnd && haveLimit) ? seps.size() : keep;
        size_t at = 0;
        for (size_t k = k0; k < k1; k++) {
            emit(s.substr(at, seps[k].at - at));
            if (keepSep) {
                std::string txt = s.substr(seps[k].at, seps[k].len);
                Value sepv = delims[seps[k].which].isRx
                           ? Value::matchVal(txt, (long)seps[k].at, (long)(seps[k].at + seps[k].len))
                           : Value::str(txt);
                Value idx = Value::integer((long long)seps[k].which);
                if (want == 'k' || want == 'm') out.arr()->push_back(idx);
                if (want == 'v' || want == 'm' || !want) out.arr()->push_back(sepv);
                if (want == 'p') {
                    Value pr = Value::pair(std::to_string(seps[k].which), sepv);
                    pr.pairKeyM() = std::make_shared<Value>(idx);
                    out.arr()->push_back(std::move(pr));
                }
            }
            at = seps[k].at + seps[k].len;
        }
        emit(s.substr(at));
        return out;
    }
    if (m == "words") {
        Value out = Value::array();
        out.isList = true; out.s = "Seq";
        // an optional limit: at most N words (`*`/Inf means all of them)
        long long limit = -1;
        for (auto& a : args)
            if (a.t != VT::Pair && a.t != VT::Whatever &&
                !(a.isNumeric() && std::isinf(a.toNum()))) { limit = a.toInt(); break; }
        // A direct scan, not `std::istringstream >>`. The stream form allocated a
        // stream and a buffer per call and — the reason this is a fix, not just a
        // speed-up — split on the C locale's idea of whitespace, which is ASCII
        // only: `"a\c[NO-BREAK SPACE]b".words` was one word here and two in
        // Rakudo. Raku splits on \s, which is Unicode's White_Space.
        const std::string& str = inv.toStr();
        size_t i = 0, n = str.size();
        auto spaceAt = [&](size_t p, size_t& adv) {
            unsigned char c = (unsigned char)str[p];
            if (c < 0x80) { adv = 1; return c == ' ' || c == '\t' || c == '\n' ||
                                            c == '\r' || c == 0x0B || c == 0x0C; }
            adv = (size_t)((c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : (c >= 0xC0) ? 2 : 1);
            return uniIsSpaceCp(cpAtByte(str, p));
        };
        while (i < n) {
            size_t adv;
            while (i < n && spaceAt(i, adv)) i += adv;      // skip separators
            if (i >= n) break;
            size_t start = i;
            while (i < n && !spaceAt(i, adv)) i += adv;     // take the word
            if (limit >= 0 && (long long)out.arr()->size() >= limit) break;
            out.arr()->push_back(Value::str(str.substr(start, i - start)));
        }
        return out;
    }
    if (m == "lines") {
        std::istringstream is(inv.toStr()); std::string w; Value out = Value::array();
        out.isList = true; out.s = "Seq";
        // `:!chomp` keeps each terminator; `:count` answers HOW MANY lines there
        // are; a positional limit stops after that many
        bool chomp = true, wantCount = false;
        for (auto& a : args)
            if (a.t == VT::Pair) {
                bool on = !a.pairVal() || a.pairVal()->truthy();
                if (a.s == "chomp") chomp = on;
                else if (a.s == "count") wantCount = on;
            }
        long long limit = -1;
        for (auto& a : args)
            if (a.t != VT::Pair && a.t != VT::Whatever &&
                !(a.isNumeric() && std::isinf(a.toNum()))) { limit = a.toInt(); break; }
        // `\r\n` (and a lone trailing `\r`) is a line terminator too — Raku's
        // .lines strips it, so an HTTP response's `$resp.lines[0]` has no \r
        while (std::getline(is, w)) {
            if (limit >= 0 && (long long)out.arr()->size() >= limit) break;
            std::string term;
            if (!w.empty() && w.back() == '\r') { w.pop_back(); term = "\r"; }
            if (!is.eof()) term += "\n"; // getline ate the newline unless this is the last line
            out.arr()->push_back(Value::str(chomp ? w : w + term));
        }
        if (wantCount) return Value::integer((long long)out.arr()->size());
        return out;
    }
    if (m == "comb") {
        Value out = Value::array();
        out.isList = true; out.s = "Seq";
        // .comb(SIZE => GAP) — 6.e gave comb rotor's shape: chunks of SIZE
        // graphemes, GAP of them skipped between chunks (so the stride is
        // SIZE + GAP), with an optional limit and :partial. Before 6.e a Pair
        // is not a needle at all and Rakudo refuses the call.
        if (!args.empty() && args[0].t == VT::Pair) {
            if (!sixE())
                throw RakuError{Value::typeObj("X::Multi::NoMatch"),
                    "Cannot resolve caller comb(" + inv.typeName() + ": Pair); "
                    "the Pair form of comb arrived with 6.e"};
            long long size = args[0].pairKey() ? args[0].pairKey()->toInt() : Value::str(args[0].s).toInt();
            long long gap  = args[0].pairVal() ? args[0].pairVal()->toInt() : 0;
            if (size < 1) size = 1;
            long long stride = size + gap; if (stride < 1) stride = 1;
            bool partial = false;
            long long limit = -1;
            for (size_t i = 1; i < args.size(); i++) {
                if (args[i].t == VT::Pair && args[i].s == "partial")
                    partial = !args[i].pairVal() || args[i].pairVal()->truthy();
                else if (args[i].isNumeric() && args[i].t != VT::Whatever) limit = args[i].toInt();
            }
            auto cps = utf8cp(inv.toStr());
            auto starts = uniGraphemeStarts(cps);
            for (size_t gi = 0; gi < starts.size(); gi += (size_t)stride) {
                if (limit >= 0 && (long long)out.arr()->size() >= limit) break;
                if (!partial && gi + (size_t)size > starts.size()) break; // a short tail is dropped
                size_t endGi = std::min(gi + (size_t)size, starts.size());
                size_t from = starts[gi], to = endGi < starts.size() ? starts[endGi] : cps.size();
                std::string chunk;
                for (size_t k = from; k < to; k++) chunk += cpToU8(cps[k]);
                out.arr()->push_back(Value::str(chunk));
            }
            return out;
        }
        // .comb($needle): every non-overlapping occurrence of the literal substring
        // (a regex needle is handled earlier); .comb() with no arg: one entry per codepoint.
        if (!args.empty() && args[0].t != VT::Int && !args[0].toStr().empty()) {
            // an EMPTY needle falls through to the no-arg form (Rakudo:
            // "abc".comb("") is ("a","b","c"))
            std::string subj = inv.toStr(), needle = args[0].toStr();
            for (size_t p = subj.find(needle); p != std::string::npos; p = subj.find(needle, p + needle.size()))
                out.arr()->push_back(Value::str(needle));
            return out;
        }
        if (!args.empty() && args[0].t == VT::Int) {
            // .comb($n [, $limit]): consecutive chunks of $n graphemes
            long long chunk = args[0].toInt(); if (chunk < 1) chunk = 1;
            long long limit = (args.size() > 1 && args[1].isNumeric() && args[1].t != VT::Whatever) ? args[1].toInt() : -1;
            auto cps = utf8cp(inv.toStr());
            auto starts = uniGraphemeStarts(cps);
            for (size_t gi = 0; gi < starts.size(); gi += (size_t)chunk) {
                if (limit >= 0 && (long long)out.arr()->size() >= limit) break;
                size_t endGi = std::min(gi + (size_t)chunk, starts.size());
                size_t from = starts[gi], to = endGi < starts.size() ? starts[endGi] : cps.size();
                std::string g; for (size_t k = from; k < to; k++) g += cpToUtf8(cps[k]);
                out.arr()->push_back(Value::str(g));
            }
            return out;
        }
        { // one entry per GRAPHEME (UAX #29 cluster), not per codepoint —
          // "e\x[301]" combs to one "é", emoji ZWJ sequences stay whole.
            auto cps = utf8cp(inv.toStr());
            auto starts = uniGraphemeStarts(cps);
            for (size_t gi = 0; gi < starts.size(); gi++) {
                size_t from = starts[gi], to = gi + 1 < starts.size() ? starts[gi + 1] : cps.size();
                std::string g;
                for (size_t k = from; k < to; k++) g += cpToUtf8(cps[k]);
                out.arr()->push_back(Value::str(g));
            }
        }
        return out;
    }
    // a Pair formats its KEY and VALUE as the two arguments
    // The tail of the dispatch chain lives in MethodCallTail.cpp — a SEGMENT of
    // this same ordered chain, split out to get this function under control. It
    // must run here, after everything above and before the fallthrough below.
    return std::nullopt;   // not handled here — fall through to the next segment
}

} // namespace rakupp
