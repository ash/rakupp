#include "MethodCallSegment.h"
#include <unistd.h>
#include <cstring>
#include <cstdlib>

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
    (void)rwArgs;
    if (inv.t == VT::Hash && !inv.hashKind.empty()) {
        bool isSet = inv.hashKind.find("Set") == 0;
        if (m == "default") return isSet ? Value::boolean(false) : Value::integer(0);
        if (m == "total") { // Mix weights may be fractional — keep the numeric type
            bool allInt = true; double t = 0;
            for (auto& kv : *inv.hash) {
                if (isSet) { t += 1; continue; }
                t += kv.second.toNum();
                if (kv.second.t != VT::Int && kv.second.t != VT::Bool) allInt = false;
            }
            return allInt ? Value::integer((long long)t) : Value::number(t);
        }
        if (m == "elems") return Value::integer((long long)inv.hash->size());
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
        std::complex<double> z(inv.n, inv.im);
        if (m == "re" || m == "Real") return Value::number(inv.n);
        if (m == "im") return Value::number(inv.im);
        if (m == "reals") { Value o = Value::array({Value::number(inv.n), Value::number(inv.im)});
                            o.isList = true; return o; } // a List, not an Array
        if (m == "abs" || m == "magnitude") return Value::number(std::abs(z));
        if (m == "conj") return Value::complex(inv.n, -inv.im);
        if (m == "sqrt") return complexSqrt(inv.n, inv.im);
        if (m == "exp") { auto r = std::exp(z); return Value::complex(r.real(), r.imag()); }
        if (m == "log") { // optional base argument: log(z) / log(base)
            auto r = std::log(z);
            if (!args.empty()) {
                const Value& b = args[0];
                r /= std::log(b.t == VT::Complex ? std::complex<double>(b.n, b.im)
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
        if (m == "polar") return Value::array({Value::number(std::abs(z)), Value::number(std::arg(z))});
        if (m == "arg") return Value::number(std::arg(z));
        if (m == "Complex") return inv;
        if (m == "isNaN") return Value::boolean(std::isnan(inv.n) || std::isnan(inv.im));
        if (m == "Str" || m == "gist" || m == "Stringy") return Value::str(inv.toStr());
        if (m == "raku") return Value::str("<" + inv.toStr() + ">");
        if (m == "Num" || m == "Real" || m == "Int") { if (inv.im != 0) throw RakuError{Value::typeObj("X::Numeric::Real"), "Can not convert Complex with nonzero imaginary part"}; return m == "Int" ? Value::integer((long long)inv.n) : Value::number(inv.n); }
        // Complex.narrow is `self.im == 0 ?? self.re.narrow !! self` — it must RECURSE,
        // or (4.0+0i).narrow stops at the Num and never demotes to Int.
        if (m == "narrow") return inv.im == 0 ? methodCall(Value::number(inv.n), "narrow", ValueList{}) : inv;
    }

    // Cool-style numeric coercion: an object that defines .Numeric/.Bridge (but
    // not the numeric method itself) acts as its numeric value here.
    if (inv.t == VT::Object && inv.obj) {
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
    // numeric
    if (m == "abs") {
        if (inv.t == VT::Int && inv.big) return Value::bigint(inv.big->abs());
        if (inv.t == VT::Int) return Value::integer(std::llabs(inv.toInt()));
        if (inv.t == VT::Rat) { Value r = Value::rat(inv.ratN->abs(), *inv.ratD); r.fatRat = inv.fatRat; return r; }
        return Value::number(std::fabs(inv.toNum()));
    }
    if (m == "sqrt") { double x = inv.toNum(); return (x < 0 && langRev_ >= 2) ? Value::complex(0, std::sqrt(-x)) : Value::number(std::sqrt(x)); }
    if (m == "rand") return Value::number(inv.toNum() * randDouble()); // $n.rand — Num in [0, $n)
    if (m == "base" && !args.empty() && (inv.t == VT::Int || inv.t == VT::Bool)) { // Int -> string in base 2..36
        long long b = args[0].toInt(); if (b < 2) b = 2; if (b > 36) b = 36;
        static const char* BD = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        // a BIG integer digits out by repeated division — toInt() would truncate
        if (inv.big && !inv.big->fitsLL()) {
            BigInt n = inv.big->abs(), base((long long)b), q, r;
            std::string d;
            while (!n.isZero()) { BigInt::divmod(n, base, q, r); d = std::string(1, BD[r.fitsLL() ? r.toLL() : 0]) + d; n = q; }
            if (d.empty()) d = "0";
            return Value::str(inv.big->sign < 0 ? "-" + d : d);
        }
        long long n = inv.toInt();
        if (n == 0) return Value::str("0");
        bool neg = n < 0; unsigned long long u = neg ? -(unsigned long long)n : (unsigned long long)n;
        static const char* D = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        std::string s;
        while (u) { s = std::string(1, D[u % b]) + s; u /= b; }
        return Value::str(neg ? "-" + s : s);
    }
    if (m == "polymod" && (inv.t == VT::Num || inv.t == VT::Rat)) {
        // non-integer polymod stays in Value arithmetic (Rat exactness, Num):
        // v % d is pushed, v becomes (v - mod) / d; a lazy list stops at v == 0
        Value out = Value::array(); out.isList = true;
        bool lazy = false; ValueList fin;
        for (auto& a : args) {
            if (a.t == VT::Array && a.ext &&
                std::static_pointer_cast<LazySeqState>(a.ext)->infinite) { lazy = true; break; }
            if (a.t == VT::Array && a.b) lazy = true; // `lazy 2, 3`
            for (auto& d : a.flatten()) fin.push_back(d);
        }
        Value v = inv;
        for (size_t i = 0; ; i++) {
            bool have = i < fin.size();
            if (lazy) {
                if (!v.truthy()) break;
                if (!have || fin[i].toNum() == 1.0) { out.arr->push_back(v); break; }
            }
            else if (!have) { out.arr->push_back(v); break; }
            Value mod = applyBinOp("%", v, fin[i]);
            out.arr->push_back(mod);
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
            if (a.t == VT::Array && a.ext &&
                std::static_pointer_cast<LazySeqState>(a.ext)->infinite) { lazy = true; tail = a; break; }
            if (a.t == VT::Range && a.rTo >= 9000000000000000000LL) { lazy = true; tail = a; break; }
            if (a.t == VT::Array && a.b) lazy = true; // `lazy 2, 3` — finite but lazy
            for (auto& d : a.flatten()) fin.push_back(d);
        }
        if (!lazy) {
            for (auto& d : fin) {
                long long dv = d.toInt(); if (dv == 0) break;
                out.arr->push_back(Value::integer(n % dv));
                n /= dv;
            }
            out.arr->push_back(Value::integer(n)); // trailing remainder
            return out;
        }
        size_t fi = 0, ti = 0;
        ValueList tcache;
        std::shared_ptr<LazySeqState> st;
        if (tail.t == VT::Array && tail.arr) { tcache = *tail.arr; st = std::static_pointer_cast<LazySeqState>(tail.ext); }
        long long rnext = tail.t == VT::Range ? tail.rFrom + (tail.rExFrom ? 1 : 0) : 0;
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
        while (n != 0) {
            long long d;
            if (!next(d) || d == 1) { out.arr->push_back(Value::integer(n)); break; }
            if (d == 0) break;
            out.arr->push_back(Value::integer(n % d));
            n /= d;
        }
        return out;
    }
    // trigonometry as methods (radians): $x.sin, $x.asin, ... (Str is Cool -> numeric)
    if (inv.isNumeric() || inv.t == VT::Str) {
        double x = inv.toNum();
        if (m == "cis") return Value::complex(std::cos(x), std::sin(x)); // e^(ix)
        if (m == "roots") { // $x.roots($n) — same as roots($x, $n)
            auto it = builtins_.find("roots");
            if (it != builtins_.end()) { ValueList ra{inv, a0()}; return it->second(*this, ra); }
        }
        if (m == "roots") { // the n n-th roots, as Complexes around the circle
            long long n = args.empty() ? 1 : a0().toInt();
            Value out = Value::array(); out.isList = true;
            if (n <= 0) { out.arr->push_back(Value::number(std::nan(""))); return out; }
            double mag = std::pow(std::abs(x), 1.0 / (double)n);
            double th0 = x < 0 ? 3.14159265358979323846 : 0.0;
            for (long long k = 0; k < n; k++) {
                double th = (th0 + 2 * 3.14159265358979323846 * k) / (double)n;
                out.arr->push_back(Value::complex(mag * std::cos(th), mag * std::sin(th)));
            }
            return out;
        }
        if (m == "unpolar") { // $mag.unpolar($angle) — Complex from polar coordinates
            double ang = args.empty() ? 0.0 : a0().toNum();
            return Value::complex(x * std::cos(ang), x * std::sin(ang));
        }
        if (m == "sin") return Value::number(std::sin(x));
        if (m == "cos") return Value::number(std::cos(x));
        if (m == "tan") return Value::number(std::tan(x));
        if (m == "asin") return Value::number(std::asin(x));
        if (m == "acos") return Value::number(std::acos(x));
        if (m == "atan") return Value::number(std::atan(x));
        if (m == "atan2") return Value::number(std::atan2(x, args.empty() ? 1.0 : a0().toNum()));
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
        if (inv.t == VT::Rat && inv.ratD && inv.ratD->isZero()) {
            Value f = Value::makeHash(); f.hashKind = "Failure";
            (*f.hash)["exception"] = Value::typeObj("X::Numeric::DivideByZero");
            return f;
        }
        // exact rounding for Rats/Ints (big-safe): floor = div, others derive from it
        if (m != "round" && (inv.t == VT::Rat || inv.t == VT::Int || inv.t == VT::Bool)) {
            BigInt n = inv.t == VT::Rat ? *inv.ratN : inv.toBig();
            BigInt d = inv.t == VT::Rat ? *inv.ratD : BigInt(1);
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
        double r = std::floor(x / scale + 0.5) * scale;
        if (args.empty()) return Value::integer((long long)r); // .round with no arg is an Int
        return Value::number(r);
    }
    if (m == "truncate") return Value::integer((long long)inv.toNum());
    if (m == "sign") {
        if (inv.t == VT::Type)
            throw RakuError{Value::typeObj("X::Multi::NoMatch"), "Cannot call sign on a type object"};
        if (inv.t == VT::Complex) { // 6.e: v / |v|; 6.c/6.d keep the historical throw
            if (langRev_ < 2)
                throw RakuError{Value::typeObj("X::Numeric::Real"), "Complex is not in the Real domain, so it has no sign"};
            double mag = std::hypot(inv.n, inv.im);
            if (mag == 0) return Value::complex(0, 0);
            return Value::complex(inv.n / mag, inv.im / mag);
        }
        double n = inv.toNum();
        if (std::isnan(n)) return Value::number(NAN); // sign(NaN) is NaN
        return Value::integer(n < 0 ? -1 : n > 0 ? 1 : 0);
    }
    if (m == "exp") return Value::number(std::exp(inv.toNum()));
    if (m == "log") {
        if (!args.empty() && args[0].t == VT::Complex) { // real.log(complex base)
            std::complex<double> r = std::log(std::complex<double>(inv.toNum(), 0.0)) /
                                     std::log(std::complex<double>(args[0].n, args[0].im));
            return Value::complex(r.real(), r.imag());
        }
        if (!args.empty()) return Value::number(std::log(inv.toNum()) / std::log(args[0].toNum()));
        return Value::number(std::log(inv.toNum()));
    }
    if (m == "log10") return Value::number(std::log10(inv.toNum()));
    if (m == "log2")  return Value::number(std::log2(inv.toNum()));
    if (m == "sin") return Value::number(std::sin(inv.toNum()));
    if (m == "cos") return Value::number(std::cos(inv.toNum()));
    if (m == "numerator") return inv.t == VT::Rat ? Value::bigint(*inv.ratN) : Value::integer(inv.toInt());
    if (m == "denominator") return inv.t == VT::Rat ? Value::bigint(*inv.ratD) : Value::integer(1);
    if (m == "nude") { // a List (prints "(3 10)"), not an Array, like Rakudo
        Value o = inv.t == VT::Rat
            ? Value::array({Value::bigint(*inv.ratN), Value::bigint(*inv.ratD)})
            : Value::array({Value::integer(inv.toInt()), Value::integer(1)});
        o.isList = true;
        return o;
    }
    if (m == "norm" && inv.t == VT::Rat) return inv; // Rats are always stored reduced
    if (inv.t == VT::Array && inv.arr &&
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
                if (!cur->arr || ix < 0 || ix >= (long long)cur->arr->size()) { oob = true; break; }
                cur = &(*cur->arr)[ix];
            }
            long long last = args[nidx - 1].toInt();
            bool in = !oob && cur->t == VT::Array && cur->arr && last >= 0 && last < (long long)cur->arr->size();
            if (m == "EXISTS-POS") return Value::boolean(in && defined((*cur->arr)[last]));
            if (m == "AT-POS") {
                if (!in) throw RakuError{Value::typeObj("X::OutOfRange"), "Index out of range"};
                return (*cur->arr)[last];
            }
            if (m == "ASSIGN-POS") {
                Value v = args.back();
                if (in) (*cur->arr)[last] = v;
                return v;
            }
            Value old = in ? (*cur->arr)[last] : Value::any();
            if (in) (*cur->arr)[last] = Value::any();
            return old;
        }
        long long i = args.empty() ? 0 : args[0].toInt();
        if (i < 0) i += (long long)inv.arr->size();
        bool in = i >= 0 && i < (long long)inv.arr->size();
        if (m == "EXISTS-POS") return Value::boolean(in && defined((*inv.arr)[i]));
        if (m == "AT-POS") return in ? (*inv.arr)[i] : Value::any();
        if (m == "ASSIGN-POS") {
            Value v = args.size() > 1 ? args[1] : Value::any();
            if (i >= 0) { while ((long long)inv.arr->size() <= i) inv.arr->push_back(Value::any());
                          (*inv.arr)[i] = v; }
            return v;
        }
        // DELETE-POS
        Value old = in ? (*inv.arr)[i] : Value::any();
        if (in) (*inv.arr)[i] = Value::any();
        return old;
    }
    if (m == "minpairs" || m == "maxpairs") {
        // pairs whose value is the min/max (per cmp); a scalar is its 0 => self pair
        Value out = Value::array(); out.isList = true;
        std::vector<std::pair<Value, Value>> kvs; // key, value
        if (inv.t == VT::Array && inv.arr) {
            for (size_t k = 0; k < inv.arr->size(); k++) kvs.push_back({Value::integer((long long)k), (*inv.arr)[k]});
        } else if (inv.t == VT::Hash && inv.hash &&
                   (inv.hashKind.empty() || inv.hashKind.rfind("Set", 0) == 0 ||
                    inv.hashKind.rfind("Bag", 0) == 0 || inv.hashKind.rfind("Mix", 0) == 0)) {
            // a Setty/Baggy competes on its counts (elem => count pairs)
            for (auto& kv : *inv.hash) kvs.push_back({Value::str(kv.first), kv.second});
        } else {
            out.arr->push_back(Value::pair("0", inv));
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
                out.arr->push_back(p);
            }
        return out;
    }
    if (m == "isa" && !args.empty()) {
        // Foo.isa(Foo) / $obj.isa("Any") / 5.isa(Int) — walk the class chain, then
        // built-in ancestry. Works on any value via its type name.
        std::string want = args[0].t == VT::Type ? args[0].s : args[0].toStr();
        std::string tn = inv.t == VT::Type ? inv.s : (inv.obj && inv.obj->cls ? inv.obj->cls->name : inv.typeName());
        if (tn == want || want == "Any" || want == "Mu") return Value::boolean(true);
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
        ClassInfo* c0 = inv.t == VT::Object && inv.obj ? inv.obj->cls.get() : nullptr;
        if (!c0) { auto cit = classes_.find(tn); if (cit != classes_.end()) c0 = cit->second.get(); }
        for (ClassInfo* c = c0; c; c = c->parent.get()) {
            if (c->name == want || c->nativeParent == want) return Value::boolean(true);
            if (!c->nativeParent.empty())
                for (auto& anc : typeAncestry(c->nativeParent)) if (anc == want) return Value::boolean(true);
        }
        for (auto& anc : typeAncestry(tn)) if (anc == want) return Value::boolean(true);
        return Value::boolean(false);
    }
    if (m == "package" && inv.t == VT::Code && inv.code)
        return Value::typeObj(inv.code->pkg.empty() ? "GLOBAL" : inv.code->pkg);
    if (m == "of" && inv.t == VT::Type) { // array[int].of / Array[Str].of
        if (const char* vt = quantValueType(inv.s)) return Value::typeObj(vt); // Bag.of is UInt
        return Value::typeObj(inv.ofType.empty() ? "Mu" : inv.ofType);
    }
    if (m == "new" && inv.t == VT::Array) { // @a.new: fresh empty array of the same type
        Value out = Value::array();
        out.ofType = inv.ofType;
        return out;
    }
    if (m == "keyof") { // key type of an Associative (unparameterized: Mu / Str(Any))
        if (inv.t == VT::Hash && !inv.hashKind.empty()) // quanthash: its key parameter (unparameterized: Mu)
            return Value::typeObj(inv.ofType.empty() ? "Mu" : inv.ofType);
        if (inv.t == VT::Type) {
            static const std::set<std::string> qh = {"Set", "SetHash", "Bag", "BagHash", "Mix", "MixHash"};
            if (qh.count(inv.s)) // Mix[Str].keyof is Str; unparameterized quanthashes key on Mu
                return Value::typeObj(inv.ofType.empty() ? "Mu" : inv.ofType);
            return Value::typeObj("Str(Any)"); // `Hash.keyof`
        }
        // An OBJECT hash keys on its declared key type. Parser.cpp records that as
        // the second half of declType ("Any,Int"), which typedDefault copies into
        // ofType — `.of` already reads the first half and `.keyof` never read the
        // second. A plain hash keys on the COERCION type Str(Any), not bare Str.
        if (inv.t == VT::Hash) {
            size_t c = inv.ofType.find(',');
            if (c != std::string::npos) return Value::typeObj(inv.ofType.substr(c + 1));
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
        if (inv.t == VT::Rat) r = inv;
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
        r.fatRat = fat; // FatRat is the arbitrary-precision Rat, tagged for type identity
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
        if (inv.big) {
            const BigInt& n = *inv.big;
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
    if (m == "slurp" && !(inv.t == VT::Hash && inv.hashKind == "FileHandle")) { // FileHandle has its own slurp
        std::ifstream in(inv.toStr(), std::ios::binary);
        if (!in) throwFailedOpen(inv.toStr());
        std::ostringstream ss; ss << in.rdbuf();
        Value v = Value::str(ss.str());
        // slurp(:bin) yields a Blob (the raw bytes), not a decoded Str
        for (auto& a : args) if (a.t == VT::Pair && a.s == "bin" && a.pairVal && a.pairVal->truthy()) v.hashKind = "Blob";
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
                if (a.s == "append") append = a.pairVal && a.pairVal->truthy();
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
            (*f.hash)["exception"] = Value::typeObj("X::IO::DoesNotExist");
            (*f.hash)["message"] = Value::str("Failed to stat '" + inv.toStr() + "': no such file or directory");
            return f;
        }
        if (m == "z") return Value::boolean(st.st_size == 0);
        return Value::integer((long long)st.st_size);
    }
    if (m == "mode" && inv.hashKind == "IO") { // permission bits as a 4-digit octal string
        struct stat st;
        if (stat(inv.toStr().c_str(), &st) != 0) {
            Value f = Value::makeHash(); f.hashKind = "Failure";
            (*f.hash)["exception"] = Value::typeObj("X::IO::DoesNotExist");
            (*f.hash)["message"] = Value::str("Failed to stat '" + inv.toStr() + "': no such file or directory");
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
    if (m == "path") {
        if (inv.t == VT::Hash && inv.hashKind == "FileHandle") {
            auto st = inv.hash->find("std"); // standard streams: an IO::Special
            if (st != inv.hash->end()) {
                std::string nm = st->second.toStr() == "err" ? "<STDERR>" : st->second.toStr() == "in" ? "<STDIN>" : "<STDOUT>";
                Value sp = Value::str(nm); sp.hashKind = "IO::Special"; return sp;
            }
            auto pt = inv.hash->find("path");
            if (pt != inv.hash->end()) return pt->second;
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
                if (ioSpecMethod(*this, spec, "split", sa, r) && r.t == VT::Hash && r.hash) {
                    if (m == "parts") { r.hashKind = "IO::Path::Parts"; return r; }
                    auto it = r.hash->find(m);
                    if (it != r.hash->end()) return it->second;
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
            if (m == "path") return Value::str(inv.s);
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
            for (long long k = 0; k < up; k++) s = dirOf(s);
            return asIO(s);
        }
        if (m == "dirname") return Value::str(dirOf(inv.toStr()));
        // `.parts` — the (volume, dirname, basename) triple as an
        // IO::Path::Parts, which is Associative on those three keys
        if (m == "parts") {
            std::string full = inv.toStr();
            Value pp = Value::makeHash(); pp.hashKind = "IO::Path::Parts";
            (*pp.hash)["volume"]   = Value::str("");
            (*pp.hash)["dirname"]  = Value::str(dirOf(full));
            std::string b = full; while (b.size() > 1 && b.back() == '/') b.pop_back();
            auto bp = b.find_last_of('/');
            (*pp.hash)["basename"] = Value::str(bp == std::string::npos ? b : b.substr(bp + 1));
            return pp;
        }
        if (m == "sibling") return asIO(dirOf(inv.toStr()) + "/" + (args.empty() ? "" : a0().toStr()));
        if (m == "child" || m == "add") {
            if (!args.empty()) rejectNulPath(args[0].toStr());
            std::string s = inv.toStr(); if (!s.empty() && s.back() == '/') s.pop_back();
            // several parts (or one list argument) append as successive segments:
            // `"foo".IO.add(<bar baz>)` is foo/bar/baz
            for (auto& a : args) {
                if (a.t == VT::Pair) continue;
                for (auto& part : toList(a)) { s += "/"; s += part.toStr(); }
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
                if (a.t == VT::Pair && a.s == "parts" && a.pairVal) {
                    const Value& p = *a.pairVal;
                    if (p.t == VT::Range) {
                        lo = p.rFrom + (p.rExFrom ? 1 : 0);
                        hi = p.rTo >= 9000000000000000000LL ? avail : p.rTo - (p.rExTo ? 1 : 0);
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
                    if (a.t == VT::Pair && a.s == "joiner" && a.pairVal) joiner = a.pairVal->toStr();
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
            ssize_t n = ::readlink(p.c_str(), lbuf, sizeof lbuf - 1);
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
                char buf[4096]; if (getcwd(buf, sizeof buf)) s = std::string(buf) + "/" + s;
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
            return asIO(s);
        }
        if (m == "is-absolute") return Value::boolean(!inv.toStr().empty() && inv.toStr()[0] == '/');
        // the path's OS grammar and the directory it is resolved against
        if (m == "SPEC") return Value::typeObj("IO::Spec::" + (inv.enumName.empty() ? "Unix" : inv.enumName));
        if (m == "CWD") {
            if (!inv.ofType.empty()) return Value::str(inv.ofType); // an explicit :CWD
            char buf[4096]; return Value::str(getcwd(buf, sizeof buf) ? buf : ".");
        }
        if (m == "is-relative") return Value::boolean(inv.toStr().empty() || inv.toStr()[0] != '/');
        if (m == "contents" || m == "dir") {
            Value out = Value::array(); out.isList = true;
            std::string base = inv.toStr();
            if (DIR* d = opendir(base.c_str())) {
                while (struct dirent* e = readdir(d)) {
                    std::string nm = e->d_name;
                    if (nm == "." || nm == "..") continue;
                    out.arr->push_back(asIO(base + (base.empty() || base.back() == '/' ? "" : "/") + nm));
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
        return Value::number(secs);
    }
    if (m == "chmod" && inv.hashKind == "IO") { // $path.IO.chmod(0o644)
        ::chmod(inv.toStr().c_str(), (mode_t)(args.empty() ? 0 : args[0].toInt()));
        Value p = Value::str(inv.toStr()); p.hashKind = "IO"; return p;
    }
    if (m == "open") { // returns a buffered file handle
        Value h = Value::makeHash(); h.hashKind = "FileHandle";
        (*h.hash)["path"] = Value::str(inv.toStr());
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
        (*h.hash)["mode"] = Value::str(mode);
        (*h.hash)["buffer"] = Value::str("");
        if (mode == "w") { std::ofstream create(inv.toStr(), std::ios::trunc); } // the file exists immediately
        if (mode == "rw") { std::ofstream create(inv.toStr(), std::ios::app); }  // exists immediately, kept intact
        if (mode != "r") registerWriteHandle(h.hash); // flush at exit if not closed
        return h;
    }
    if (inv.t == VT::Hash && inv.hashKind == "FileHandle") {
        // IO::Handle accessors (with defaults); writable via lvalue()
        if (m == "chomp")  { auto it = inv.hash->find("chomp");  return it != inv.hash->end() ? it->second : Value::boolean(true); }
        // .lock/.unlock (flock): rakupp handles are buffered (no live OS fd), so
        // there is nothing to flock; report success. Cross-PROCESS exclusion (zef's
        // lock-file-protect guards concurrent zef runs) is thus not provided — fine
        // for a single interpreter process, revisit if real fd-backed IO lands.
        if (m == "lock" || m == "unlock") return Value::boolean(true);
        if (m == "encoding") { auto it = inv.hash->find("encoding"); return it != inv.hash->end() ? it->second : Value::str("utf8"); }
        if (m == "nl-in")  { auto it = inv.hash->find("nl-in");  return it != inv.hash->end() ? it->second : Value::str("\n"); }
        if (m == "nl-out") { auto it = inv.hash->find("nl-out"); return it != inv.hash->end() ? it->second : Value::str("\n"); }
        if (m == "path" || m == "IO") {
            auto st = inv.hash->find("std"); // standard streams: an IO::Special
            if (st != inv.hash->end()) {
                std::string nm = st->second.toStr() == "err" ? "<STDERR>" : st->second.toStr() == "in" ? "<STDIN>" : "<STDOUT>";
                Value sp = Value::str(nm); sp.hashKind = "IO::Special"; return sp;
            }
            return (*inv.hash)["path"];
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
            auto stdit = inv.hash->find("std");
            if (stdit != inv.hash->end()) { // $*OUT / $*ERR — write straight to the stream
                (stdit->second.toStr() == "err" ? std::cerr : std::cout) << s;
                return Value::boolean(true);
            }
            (*inv.hash)["buffer"] = Value::str((*inv.hash)["buffer"].toStr() + s);
            return Value::boolean(true);
        }
        if (m == "t") { // is the handle a terminal? files never; std handles ask isatty
            auto stdit = inv.hash->find("std");
            if (stdit == inv.hash->end()) return Value::boolean(false);
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
                else if ((a.t == VT::Array || a.t == VT::Range) && !(a.t == VT::Array && !a.arr))
                    for (auto& e : a.flatten()) bytes += (char)(unsigned char)(e.toInt() & 0xFF);
            }
            (*inv.hash)["buffer"] = Value::str((*inv.hash)["buffer"].toStr() + bytes);
            return Value::boolean(true);
        }
        if (m == "read") { // binary read: up to N bytes from a byte cursor, as a Buf
            long long want = args.empty() ? 65536 : args[0].toInt();
            if (inv.hash->find("bytes") == inv.hash->end()) {
                std::ifstream in((*inv.hash)["path"].toStr(), std::ios::binary);
                std::ostringstream ss; ss << in.rdbuf();
                (*inv.hash)["bytes"] = Value::str(ss.str());
                (*inv.hash)["bpos"] = Value::integer(0);
            }
            const std::string& all = (*inv.hash)["bytes"].s;
            long long pos = (*inv.hash)["bpos"].toInt();
            if (pos < 0) pos = 0;
            if (want < 0) want = 0;
            if (pos > (long long)all.size()) pos = all.size();
            long long take = std::min(want, (long long)all.size() - pos);
            Value b = Value::str(all.substr((size_t)pos, (size_t)take));
            b.hashKind = "Buf";
            (*inv.hash)["bpos"] = Value::integer(pos + take);
            return b;
        }
        if (m == "close") {
            std::string mode = (*inv.hash)["mode"].toStr();
            const std::string& buf = (*inv.hash)["buffer"].s;
            // rw/update flush only when something was WRITTEN — an untouched
            // rw handle on an existing file must not wipe it with a trunc
            if (mode == "w" || mode == "a" || ((mode == "rw" || mode == "update") && !buf.empty())) {
                std::ofstream out((*inv.hash)["path"].toStr(),
                                  std::ios::binary | (mode == "a" ? std::ios::app : std::ios::trunc));
                if (out) out << buf;
                (*inv.hash)["flushed"] = Value::boolean(true); // exit-flush skips it now
            }
            return Value::boolean(true);
        }
        if (m == "spurt") { // IO::Handle.spurt($content) — write through the open handle
            Value c = args.empty() ? Value::str("") : args[0];
            (*inv.hash)["buffer"] = Value::str((*inv.hash)["buffer"].toStr() + c.toStr());
            return Value::boolean(true); // flushed to "path" on .close (zef's spurt-package-list)
        }
        if (m == "slurp") {
            auto cap = inv.hash->find("captured"); // in-memory handle (e.g. Proc.out)
            if (cap != inv.hash->end() && cap->second.truthy()) return (*inv.hash)["buffer"];
            if (inv.hash->find("std") != inv.hash->end() && (*inv.hash)["std"].toStr() == "in") {
                std::ostringstream ss; ss << std::cin.rdbuf(); return Value::str(ss.str()); // $*IN.slurp
            }
            std::ifstream in((*inv.hash)["path"].toStr()); std::ostringstream ss; ss << in.rdbuf(); return Value::str(ss.str());
        }
        // .getc / .readchars: load the file's codepoints once, track a cursor in "cpos".
        if (m == "getc" || m == "readchars") {
            if (inv.hash->find("cps") == inv.hash->end()) {
                std::string path = (*inv.hash)["path"].toStr();
                struct stat st;
                if (::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
                    throw RakuError{Value::typeObj("X::IO"), "Cannot read characters from a directory: " + path};
                std::ifstream in(path); std::ostringstream ss; ss << in.rdbuf();
                Value cps = Value::array();
                for (auto cp : utf8cp(ss.str())) cps.arr->push_back(Value::str(cpToUtf8(cp)));
                (*inv.hash)["cps"] = cps;
                (*inv.hash)["cpos"] = Value::integer(0);
            }
            long pos = (*inv.hash)["cpos"].toInt();
            auto& cps = *(*inv.hash)["cps"].arr;
            if (m == "readchars") { // read up to N chars (default 65536), "" at EOF
                long want = args.empty() ? 65536 : args[0].toInt();
                std::string out; long got = 0;
                for (; got < want && pos < (long)cps.size(); got++, pos++) out += cps[pos].toStr();
                (*inv.hash)["cpos"] = Value::integer(pos);
                return Value::str(out);
            }
            if (pos >= (long)cps.size()) return Value::nil(); // getc at EOF → Nil
            (*inv.hash)["cpos"] = Value::integer(pos + 1);
            return cps[pos];
        }
        // reading: lazily load the file into lines, track a cursor in "pos"
        bool isStdin = inv.hash->find("std") != inv.hash->end() && (*inv.hash)["std"].toStr() == "in";
        if (m == "get" || m == "getline" || m == "lines" || m == "eof" || m == "words" || m == "slurp-rest") {
            if (inv.hash->find("lines") == inv.hash->end()) {
                // a custom line separator (`.nl-in = "+"`) splits on that instead of \n
                std::string sep;
                auto nit = inv.hash->find("nl-in");
                if (nit != inv.hash->end()) {
                    if (nit->second.t == VT::Str) sep = nit->second.s;
                    else if (nit->second.t == VT::Array && nit->second.arr && !nit->second.arr->empty())
                        sep = (*nit->second.arr)[0].toStr(); // Array nl-in: first separator
                }
                Value lines = Value::array();
                if (!sep.empty() && sep != "\n") {
                    std::string content;
                    if (isStdin) { std::ostringstream ss; ss << std::cin.rdbuf(); content = ss.str(); }
                    else { std::ifstream in((*inv.hash)["path"].toStr()); std::ostringstream ss; ss << in.rdbuf(); content = ss.str(); }
                    size_t start = 0, p;
                    while ((p = content.find(sep, start)) != std::string::npos) {
                        lines.arr->push_back(Value::str(content.substr(start, p - start)));
                        start = p + sep.size();
                    }
                    if (start < content.size()) lines.arr->push_back(Value::str(content.substr(start)));
                } else {
                    std::string line;
                    // an IN-MEMORY handle ($*ARGFILES, Proc.out/.err) has its
                    // whole content in "buffer" — there is no file to reopen
                    auto capIt = inv.hash->find("captured");
                    if (capIt != inv.hash->end() && capIt->second.truthy()) {
                        const std::string& content = (*inv.hash)["buffer"].s;
                        size_t start = 0;
                        while (start <= content.size()) {
                            size_t nl = content.find('\n', start);
                            if (nl == std::string::npos) {
                                if (start < content.size()) lines.arr->push_back(Value::str(content.substr(start)));
                                break;
                            }
                            std::string l = content.substr(start, nl - start);
                            if (!l.empty() && l.back() == '\r') l.pop_back();
                            lines.arr->push_back(Value::str(l));
                            start = nl + 1;
                        }
                    }
                    else if (isStdin) { // $*IN — read standard input
                        while (std::getline(std::cin, line)) {
                            if (!line.empty() && line.back() == '\r') line.pop_back();
                            lines.arr->push_back(Value::str(line));
                        }
                    } else {
                        std::ifstream in((*inv.hash)["path"].toStr());
                        while (std::getline(in, line)) {
                            if (!line.empty() && line.back() == '\r') line.pop_back();
                            lines.arr->push_back(Value::str(line));
                        }
                    }
                }
                (*inv.hash)["lines"] = lines;
                (*inv.hash)["pos"] = Value::integer(0);
            }
            if (m == "words") { // remaining input split on whitespace
                auto& ln = *(*inv.hash)["lines"].arr;
                long long p = (*inv.hash)["pos"].toInt();
                std::string all;
                for (long long i = p; i < (long long)ln.size(); i++) { if (!all.empty()) all += "\n"; all += ln[i].toStr(); }
                (*inv.hash)["pos"] = Value::integer((long long)ln.size());
                Value out = Value::array(); out.isList = true;
                std::istringstream ws(all); std::string w;
                while (ws >> w) out.arr->push_back(Value::str(w));
                return out;
            }
            if (m == "slurp-rest") {
                auto& ln = *(*inv.hash)["lines"].arr;
                long long p = (*inv.hash)["pos"].toInt();
                std::string all;
                for (long long i = p; i < (long long)ln.size(); i++) { all += ln[i].toStr(); all += "\n"; }
                (*inv.hash)["pos"] = Value::integer((long long)ln.size());
                return Value::str(all);
            }
            auto& lines = *(*inv.hash)["lines"].arr;
            long long pos = (*inv.hash)["pos"].toInt();
            if (m == "eof") return Value::boolean(pos >= (long long)lines.size());
            if (m == "lines") {
                Value out = Value::array(); out.isList = true;
                for (long long i = pos; i < (long long)lines.size(); i++) out.arr->push_back(lines[i]);
                (*inv.hash)["pos"] = Value::integer((long long)lines.size());
                return out;
            }
            // get / getline: next line or Nil at EOF
            if (pos >= (long long)lines.size()) return Value::nil();
            (*inv.hash)["pos"] = Value::integer(pos + 1);
            return lines[pos];
        }
    }
    if (m == "lines" && inv.hashKind == "IO") {
        std::ifstream in(inv.toStr()); Value out = Value::array(); out.isList = true; out.s = "Seq";
        if (!in) throwFailedOpen(inv.toStr());
        std::string line;
        while (std::getline(in, line)) { // strip \r\n too (Windows/HTTP text)
            if (!line.empty() && line.back() == '\r') line.pop_back();
            out.arr->push_back(Value::str(line));
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
        if (a0v.t == VT::Code && a0v.code) a0v = callCallable(a0v, ValueList{Value::integer(n)});
        if (a0v.t == VT::Range) { from = a0v.rFrom + (a0v.rExFrom ? 1 : 0);
                                  len = (a0v.rTo - (a0v.rExTo ? 1 : 0)) - from + 1; }
        else {
            from = a0v.toInt();
            if (args.size() > 1 && args[1].t == VT::Code && args[1].code) {
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
        Value b = Value::str(inv.s.substr((size_t)from, (size_t)len)); b.hashKind = inv.hashKind; return b;
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
            if (std::isspace((unsigned char)dir)) continue;
            bool all = false; long long cnt = 1;
            if (k + 1 < tmpl.size() && tmpl[k + 1] == '*') { all = true; k++; }
            else if (k + 1 < tmpl.size() && std::isdigit((unsigned char)tmpl[k + 1])) {
                size_t j = k + 1; std::string num;
                while (j < tmpl.size() && std::isdigit((unsigned char)tmpl[j])) num += tmpl[j++];
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
                    out.arr->push_back(Value::integer((unsigned char)d[pos++]));
            } else if (dir == 'A' || dir == 'a' || dir == 'Z') {
                long long r = all ? left() : cnt;
                std::string t; for (long long i2 = 0; i2 < r && pos < d.size(); i2++) t += d[pos++];
                out.arr->push_back(Value::str(t));
            } else if (dir == 'H') {
                long long r = all ? left() * 2 : cnt;
                std::string t; static const char* hx = "0123456789abcdef";
                for (long long i2 = 0; i2 < r && pos < d.size(); i2 += 2) {
                    unsigned char c2 = (unsigned char)d[pos++];
                    t += hx[c2 >> 4]; if (i2 + 1 < r) t += hx[c2 & 0xF];
                }
                out.arr->push_back(Value::str(t));
            } else if (dir == 'x') {
                pos += (size_t)(all ? left() : cnt);
            } else {
                int w = (dir == 'n' || dir == 'S' || dir == 'v') ? 2 : 4;
                bool bigEnd = (dir == 'n' || dir == 'N');
                long long r = all ? left() / w : cnt;
                for (long long i2 = 0; i2 < r && pos < d.size(); i2++)
                    out.arr->push_back(Value::integer(bigEnd ? be(w) : le(w)));
            }
        }
        // Rakudo hands back the VALUE when the template produced exactly one —
        // `.unpack("A3")` is a Str and `.unpack("C1")` an Int, not a 1-element list.
        if (out.arr->size() == 1) return (*out.arr)[0];
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
        for (char ch : enc) if (std::isalnum((unsigned char)ch)) norm += (char)std::tolower((unsigned char)ch);
        bool latin1 = norm == "iso88591" || norm == "latin1" || norm == "windows1252";
        if (m == "encode") {
            // `:replacement` substitutes for every character the encoding cannot
            // represent; a bare `:replacement` means "?". Without it an
            // unencodable character is an error in Rakudo — we keep the byte.
            bool haveRepl = false; std::string repl;
            for (auto& a : args)
                if (a.t == VT::Pair && a.s == "replacement" && (!a.pairVal || a.pairVal->truthy())) {
                    haveRepl = true;
                    repl = (a.pairVal && a.pairVal->t == VT::Str) ? a.pairVal->s : "?";
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
                b.ofType = u16 ? "uint16" : "uint32";
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
            if (inv.enumName == "utf16" || inv.ofType == "uint16" || inv.ofType == "int16") norm = "utf16";
            else if (inv.enumName == "utf32" || inv.ofType == "uint32" || inv.ofType == "int32") norm = "utf32";
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
        if (m == "chars") return Value::integer(graphemeCount(inv.toStr())); // graphemes
        if (m == "codes") return Value::integer(cpCount(inv.toStr()));       // codepoints
        int mode = m == "NFD" ? 0 : m == "NFC" ? 1 : m == "NFKD" ? 2 : 3;
        auto norm = uniNormalize(utf8cp(inv.toStr()), mode);
        // tag it with the NORMALISATION FORM, not the generic "Uni": `"x".NFD` is
        // an NFD, and both `.gist` (NFD:0x<…>) and `.raku` (Uni.new(…).NFD) read
        // this. The Uni type-object constructor a few hundred lines up already
        // tags correctly; only this Str-method path flattened them all to "Uni".
        Value out = Value::array(); out.s = m;
        for (auto c : norm) out.arr->push_back(Value::integer((long long)c));
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
            out.arr->push_back(Value::str(nm.empty() ? "<unassigned>" : nm));
        }
        return out;
    }
    // `.uniparse` as a METHOD — the invocant NAMES the character(s) to build
    // ('TWO HEARTS, BUTTERFLY'.uniparse), the mirror of `.uniname`
    if (m == "uniparse" && (inv.t == VT::Str || inv.t == VT::Match)) {
        auto it = builtins_.find("uniparse");
        if (it != builtins_.end()) { ValueList ua{Value::str(inv.toStr())}; return it->second(*this, ua); }
    }
    if (m == "unival" || m == "univals" || m == "uniname") {
        if (inv.t == VT::Type)
            throw RakuError{Value::typeObj("X::Multi::NoMatch"), "Cannot call " + m + " with a type object"};
        // a character with no numeric value has unival NaN, not Nil — `'a'.unival`
        // is a Num you can compare, and `.univals` interleaves them with the reals
        auto univ = [](uint32_t cp) -> Value { long long num, den; if (!uniNumValue(cp, num, den)) return Value::number(std::nan("")); return den == 1 ? Value::integer(num) : Value::rat(BigInt(num), BigInt(den)); };
        if (m == "univals") { Value out = Value::array(); out.isList = true; for (uint32_t cp : utf8cp(inv.toStr())) out.arr->push_back(univ(cp)); return out; }
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
            if (!have || cp > 0x10FFFF) return Value::str("<unassigned>");
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
                return Value::integer(uniCombiningClass(cp));
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
        for (uint32_t cp : cps) out.arr->push_back(one(cp));
        return out;
    }
    if (m == "unival" || m == "univals") {
        auto uv = [&](uint32_t cp) -> Value {
            long long nu, de;
            if (!uniNumValue(cp, nu, de)) return Value::number(std::nan(""));
            return de == 1 ? Value::integer(nu) : Value::ratZ(BigInt(nu), BigInt(de));
        };
        std::vector<uint32_t> cps;
        if (inv.t == VT::Int || inv.t == VT::Bool) cps.push_back((uint32_t)inv.toInt());
        else cps = utf8cp(inv.toStr());
        if (m == "unival") return cps.empty() ? Value::number(std::nan("")) : uv(cps[0]);
        Value out = Value::array(); out.isList = true; out.s = "Seq";
        for (uint32_t cp : cps) out.arr->push_back(uv(cp));
        return out;
    }
    if (m == "uc") return Value::str(mapCase(inv.toStr(), 1, 0));
    if (m == "lc") return Value::str(mapCase(inv.toStr(), 0, 0));
    if (m == "tc") return Value::str(mapCase(inv.toStr(), 0, 1));
    if (m == "tclc") return Value::str(mapCase(inv.toStr(), 0, 2));
    if (m == "indent" && !args.empty()) { // add (negative: remove) indentation, AFTER existing leading whitespace
        long long amt = args[0].toInt();
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
    if (m == "flip") { // reverses GRAPHEMES: a combining mark stays behind its base
        auto cps = utf8cp(inv.toStr());
        GraphemeMap gm(cps);
        std::string r;
        for (size_t g = gm.count(); g-- > 0; )
            for (size_t k = gm.cpAt(g), e = gm.cpAt(g + 1); k < e; k++) r += cpToUtf8(cps[k]);
        return Value::str(r);
    }
    if (m == "ords") { Value out = Value::array(); for (auto cp : uniNormalize(utf8cp(inv.toStr()), 1 /*NFC: .ords returns grapheme ordinals*/)) out.arr->push_back(Value::integer(cp)); return out; }
    if (m == "chomp") { // a logical newline: "\n", "\r\n" or a lone "\r"
        std::string s = inv.toStr();
        if (!s.empty() && s.back() == '\n') s.pop_back();
        if (!s.empty() && s.back() == '\r') s.pop_back();
        return Value::str(s);
    }
    if (m == "trim") { std::string s = inv.toStr(); size_t a = s.find_first_not_of(" \t\n\r"); size_t b = s.find_last_not_of(" \t\n\r"); return Value::str(a == std::string::npos ? "" : s.substr(a, b - a + 1)); }
    if (m == "trim-leading") { std::string s = inv.toStr(); size_t a = s.find_first_not_of(" \t\n\r"); return Value::str(a == std::string::npos ? "" : s.substr(a)); }
    if (m == "trim-trailing") { std::string s = inv.toStr(); size_t b = s.find_last_not_of(" \t\n\r"); return Value::str(b == std::string::npos ? "" : s.substr(0, b + 1)); }
    if (m == "substr" || m == "substr-rw") {
        auto cps = utf8cp(inv.toStr());
        // Raku indexes by GRAPHEME, so `n` counts clusters and every cut lands on a
        // cluster boundary. Indexing `cps` directly splits "ŕ̥" into its base and its
        // combining ring, which is how `substr` and `chars` came to disagree.
        GraphemeMap gm(cps);
        long long n = (long long)gm.count();
        auto slice = [&](long long lo, long long hi) { // [lo, hi) in graphemes
            std::string r;
            if (hi <= lo) return r;
            size_t a = gm.cpAt((size_t)lo), b = gm.cpAt((size_t)hi);
            for (size_t k = a; k < b; k++) r += cpToUtf8(cps[k]);
            return r;
        };
        // a RANGE gives both ends at once: `substr("Long string", 3..6)`
        if (!args.empty() && args[0].t == VT::Range) {
            const Value& rg = args[0];
            long long lo = rg.rFrom + (rg.rExFrom ? 1 : 0);
            long long hi = rg.rTo - (rg.rExTo ? 1 : 0);
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
                    std::strtod(av.s.c_str(), &end);
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
        bool icase = false, imark = false;
        for (auto& av : args)
            if (av.t == VT::Pair) {
                if (av.s == "i" || av.s == "ignorecase") icase = !av.pairVal || av.pairVal->truthy();
                // `:ignoremark` compares base characters; the fold is
                // grapheme-for-grapheme, so the answered position still indexes
                // the ORIGINAL string
                else if (av.s == "m" || av.s == "ignoremark") imark = !av.pairVal || av.pairVal->truthy();
            }
        std::string hay = inv.toStr(), ndl = a0().toStr();
        if (imark) { hay = markFold(hay); ndl = markFold(ndl); }
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
                (*f.hash)["exception"] = makeTypedEx("X::OutOfRange",
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
         (args[0].t == VT::Array && args[0].arr))) {
        const std::string& subj = inv.s;
        std::string needle;
        if (args[0].t == VT::Array) {
            for (auto& e : *args[0].arr) { if (!needle.empty()) needle += " "; needle += e.toStr(); }
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
        std::string subj = inv.toStr();
        // `/@alpha/` — array elements as a longest-first literal alternation
        // (Base64 decodes via `$str.comb(/@alpha/)`); match/subst interpolate later
        std::string pat = rxInterpArrays(args[rxIdx].s);
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
            Regex re(pat); Value out = Value::array(); out.isList = true; out.s = "Seq"; long pos = 0; RxMatch mm;
            while (re.ok() && pos <= (long)subj.size() && re.search(subj, pos, mm)) {
                out.arr->push_back(Value::str(subj.substr(mm.from, mm.to - mm.from)));
                pos = mm.to > mm.from ? mm.to : mm.to + 1;
            }
            return out;
        }
        if (m == "split") {
            Regex re(pat); Value out = Value::array(); out.isList = true; out.s = "Seq"; long pos = 0; RxMatch mm;
            bool skipEmpty = false;
            // `:v`/`:k`/`:kv`/`:p` — the separator comes back between the pieces as
            // a Match, as its delimiter index (always 0 here: one delimiter), or both
            char want = 0;
            for (auto& la : args)
                if (la.t == VT::Pair && (!la.pairVal || la.pairVal->truthy())) {
                    if (la.s == "skip-empty") skipEmpty = true;
                    else if (la.s == "v" || la.s == "k" || la.s == "p") want = la.s[0];
                    else if (la.s == "kv") want = 'm';
                }
            auto emit = [&](const std::string& piece) { if (!(skipEmpty && piece.empty())) out.arr->push_back(Value::str(piece)); };
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
                if (haveLimit && (long long)out.arr->size() >= limit - 1) break;
                if (mm.to == mm.from && mm.from == pos) { if (pos >= (long)subj.size()) break; }
                emit(subj.substr(pos, mm.from - pos));
                if (want) {
                    Value sepv = Value::matchVal(subj.substr(mm.from, mm.to - mm.from),
                                                 (long)mm.from, (long)mm.to);
                    Value idx = Value::integer(0);
                    if (want == 'k' || want == 'm') out.arr->push_back(idx);
                    if (want == 'v' || want == 'm') out.arr->push_back(sepv);
                    if (want == 'p') {
                        Value pr = Value::pair("0", sepv);
                        pr.pairKey = std::make_shared<Value>(idx);
                        out.arr->push_back(std::move(pr));
                    }
                }
                pos = mm.to > mm.from ? mm.to : mm.to + 1;
            }
            emit(subj.substr(std::min((size_t)pos, subj.size())));
            return out;
        }
    }
    if (m == "subst" && args.size() >= 1) { // literal (string) substitution
        std::string s = inv.toStr(), from = a0().toStr();
        if (from.empty()) return Value::str(s);
        Value* replArg = nullptr;
        for (size_t i = 1; i < args.size(); i++) if (args[i].t != VT::Pair) { replArg = &args[i]; break; }
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
            if (a.t == VT::Pair && (!a.pairVal || a.pairVal->t == VT::Bool)) {
                bool on = !a.pairVal || a.pairVal->truthy();
                if (a.s == "s" || a.s == "squash")          { squash = on; continue; }
                if (a.s == "c" || a.s == "complement")      { complement = on; continue; }
                if (a.s == "d" || a.s == "delete")          { del = on; continue; }
            }
            if (a.t != VT::Pair) continue;
            std::vector<std::string> froms, tos;
            // an Array side may hold Range ELEMENTS (['a'..'c']); flatten()
            // descends into them, and a bare Range side flattens to its chars
            if (a.pairKey && (a.pairKey->t == VT::Array || a.pairKey->t == VT::Range))
                for (auto& x : a.pairKey->flatten()) froms.push_back(x.toStr());
            else froms = expandTrans(a.s); // string key: char-by-char, with `..` ranges
            if (a.pairVal && (a.pairVal->t == VT::Array || a.pairVal->t == VT::Range))
                for (auto& x : a.pairVal->flatten()) tos.push_back(x.toStr());
            else if (a.pairVal) tos = expandTrans(a.pairVal->toStr());
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
        bool icase = false, imark = false;
        for (auto& a2 : args)
            if (a2.t == VT::Pair) {
                if (a2.s == "i" || a2.s == "ignorecase")
                    icase = !a2.pairVal || a2.pairVal->truthy();   // bare `:i` is true
                else if (a2.s == "m" || a2.s == "ignoremark")
                    imark = !a2.pairVal || a2.pairVal->truthy();
            }
        auto fold = [](const std::string& in) {
            auto cps = utf8cp(in); std::string o;
            for (auto c : cps) o += cpToU8(toLowerCp(c));
            return o;
        };
        std::string s = inv.toStr(), n = a0().toStr();
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
        bool icase = false, imark = false;
        ValueList pargs;
        for (auto& a2 : args) {
            if (a2.t == VT::Pair && (a2.s == "i" || a2.s == "ignorecase"))
                icase = !a2.pairVal || a2.pairVal->truthy();
            else if (a2.t == VT::Pair && (a2.s == "m" || a2.s == "ignoremark"))
                imark = !a2.pairVal || a2.pairVal->truthy();
            else if (a2.t != VT::Pair) pargs.push_back(a2);
        }
        if (pargs.empty()) // only adverbs given, no needle — a clean error, not an OOB read
            throw RakuError{Value::typeObj("X::AdHoc"), "Cannot call substr-eq without a needle string"};
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
            (*f.hash)["exception"] = Value::typeObj("X::OutOfRange");
            return f;
        }
        // taken from `s`, which is the mark-folded text when :ignoremark is on
        Value sub = methodCall(Value::str(s), "substr", ValueList{Value::integer(pos),
                                                        methodCall(Value::str(n), "chars", ValueList{})});
        if (!icase) return Value::boolean(sub.toStr() == n);
        Value a1 = methodCall(sub, "lc", ValueList{}), b1 = methodCall(Value::str(n), "lc", ValueList{});
        return Value::boolean(a1.toStr() == b1.toStr());
    }

    if (m == "ord") { auto c = utf8cp(inv.toStr()); return c.empty() ? Value::nil() : Value::integer(c[0]); }
    if (m == "chr") {
        long long cp = inv.big ? LLONG_MAX : inv.toInt(); // BigInt is certainly out of bounds
        if (cp < 0 || cp > 0x10FFFF)
            throw RakuError{Value::typeObj("X::AdHoc"),
                "chr codepoint " + (inv.big ? inv.big->toString() : std::to_string(cp)) + " is out of bounds"};
        return Value::str(cpToUtf8((uint32_t)cp));
    }
    if (m == "split") {
        std::string s = inv.toStr();
        Value d0 = a0();
        struct Delim { bool isRx; std::string str; };
        std::vector<Delim> delims;
        auto add = [&](const Value& d) { if (d.t == VT::Regex) delims.push_back({true, d.s}); else delims.push_back({false, d.toStr()}); };
        if (d0.t == VT::Array) { for (auto& e : *d0.arr) add(e); } else add(d0);
        // `:v`/`:k`/`:kv`/`:p` interleave the SEPARATORS with the pieces — as the
        // matched text, the matching delimiter's INDEX in the delimiter list, both,
        // or index => text. A regex delimiter yields a Match, a literal one a Str.
        char want = 0;
        for (auto& a : args)
            if (a.t == VT::Pair && (!a.pairVal || a.pairVal->truthy())) {
                if (a.s == "v" || a.s == "k" || a.s == "p") want = a.s[0];
                else if (a.s == "kv") want = 'm';
            }
        bool keepSep = false, skipEmpty = false, fromEnd = false;
        long long limit = -1; bool haveLimit = false; // second positional (a `*` means unlimited)
        { bool first = true;
          for (auto& a : args) {
              if (a.t == VT::Pair) {
                  if (a.pairVal && a.pairVal->truthy()) {
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
        auto emit = [&](const std::string& piece) { if (!(skipEmpty && piece.empty())) out.arr->push_back(Value::str(piece)); };
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
                if (haveLimit && (long long)out.arr->size() == limit - 1) {
                    std::string rest; for (size_t cj = ci; cj < cps.size(); cj++) rest += cpToUtf8(cps[cj]);
                    emit(rest); return out;
                }
                out.arr->push_back(Value::str(cpToUtf8(cps[ci])));
                taken = ci;
            }
            (void)taken;
            if (!haveLimit || (long long)out.arr->size() < limit) emit("");
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
                if (want == 'k' || want == 'm') out.arr->push_back(idx);
                if (want == 'v' || want == 'm' || !want) out.arr->push_back(sepv);
                if (want == 'p') {
                    Value pr = Value::pair(std::to_string(seps[k].which), sepv);
                    pr.pairKey = std::make_shared<Value>(idx);
                    out.arr->push_back(std::move(pr));
                }
            }
            at = seps[k].at + seps[k].len;
        }
        emit(s.substr(at));
        return out;
    }
    if (m == "words") {
        std::istringstream is(inv.toStr()); std::string w; Value out = Value::array();
        out.isList = true; out.s = "Seq";
        // an optional limit: at most N words (`*`/Inf means all of them)
        long long limit = -1;
        for (auto& a : args)
            if (a.t != VT::Pair && a.t != VT::Whatever &&
                !(a.isNumeric() && std::isinf(a.toNum()))) { limit = a.toInt(); break; }
        while (is >> w) {
            if (limit >= 0 && (long long)out.arr->size() >= limit) break;
            out.arr->push_back(Value::str(w));
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
                bool on = !a.pairVal || a.pairVal->truthy();
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
            if (limit >= 0 && (long long)out.arr->size() >= limit) break;
            std::string term;
            if (!w.empty() && w.back() == '\r') { w.pop_back(); term = "\r"; }
            if (!is.eof()) term += "\n"; // getline ate the newline unless this is the last line
            out.arr->push_back(Value::str(chomp ? w : w + term));
        }
        if (wantCount) return Value::integer((long long)out.arr->size());
        return out;
    }
    if (m == "comb") {
        Value out = Value::array();
        out.isList = true; out.s = "Seq";
        // .comb($needle): every non-overlapping occurrence of the literal substring
        // (a regex needle is handled earlier); .comb() with no arg: one entry per codepoint.
        if (!args.empty() && args[0].t != VT::Int && !args[0].toStr().empty()) {
            // an EMPTY needle falls through to the no-arg form (Rakudo:
            // "abc".comb("") is ("a","b","c"))
            std::string subj = inv.toStr(), needle = args[0].toStr();
            for (size_t p = subj.find(needle); p != std::string::npos; p = subj.find(needle, p + needle.size()))
                out.arr->push_back(Value::str(needle));
            return out;
        }
        if (!args.empty() && args[0].t == VT::Int) {
            // .comb($n [, $limit]): consecutive chunks of $n graphemes
            long long chunk = args[0].toInt(); if (chunk < 1) chunk = 1;
            long long limit = (args.size() > 1 && args[1].isNumeric() && args[1].t != VT::Whatever) ? args[1].toInt() : -1;
            auto cps = utf8cp(inv.toStr());
            auto starts = uniGraphemeStarts(cps);
            for (size_t gi = 0; gi < starts.size(); gi += (size_t)chunk) {
                if (limit >= 0 && (long long)out.arr->size() >= limit) break;
                size_t endGi = std::min(gi + (size_t)chunk, starts.size());
                size_t from = starts[gi], to = endGi < starts.size() ? starts[endGi] : cps.size();
                std::string g; for (size_t k = from; k < to; k++) g += cpToUtf8(cps[k]);
                out.arr->push_back(Value::str(g));
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
                out.arr->push_back(Value::str(g));
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
