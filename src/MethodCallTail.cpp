#include "AsciiCtype.h"
#include "MethodCallSegment.h"

namespace rakupp {

// One element of a .flat: append x (or its spread) to out. Shared by the eager
// arm and the lazy view over an endless source (issue #30 follow-up) — the
// rules are .flat's own, and they are about the SLOT x sits in, not about x:
// a bare list slot spreads its Iterable, an ARRAY's slot never does because
// array assignment itemises each element into a Scalar container. So
// `my @a = (1,2),(3,4); @a.flat` is TWO elements even though each element is
// a List, while `((1,2),(3,4)).flat` is four. `.item` opts out either way,
// and `:hammer` (6.e) flattens containers regardless.
static void flatOneInto(const Value& x, bool ofArray, bool hammer, ValueList& out) {
    if (hammer) {
        if (x.t == VT::Array && x.arr()) { for (auto& e : *x.arr()) flatOneInto(e, false, true, out); return; }
        if (x.t == VT::Hash && x.hash() && x.hashKind.empty()) {
            for (auto& kv : *x.hash()) out.push_back(Value::pair(kv.first, kv.second));
            return;
        }
        if (x.t == VT::Range) { for (auto& e : x.flatten()) out.push_back(e); return; }
        out.push_back(x);
        return;
    }
    if (x.t == VT::Array && x.arr() && !x.itemized && !ofArray)
        for (auto& e : *x.arr()) flatOneInto(e, !x.isList, false, out);
    else if (!ofArray && x.t == VT::Hash && x.hash() && x.hashKind.empty())
        for (auto& kv : *x.hash()) out.push_back(Value::pair(kv.first, kv.second));
    else if (!ofArray && x.t == VT::Range)
        for (auto& e : x.flatten()) out.push_back(e);
    else {
        // What stays whole stays whole BECAUSE it is in a container, and
        // `.raku` says so: `[1, (2, 3)].flat` is `(1, $(2, 3))` (sheet LA-34).
        Value keep = x;
        if (ofArray && (keep.t == VT::Array || keep.t == VT::Hash)) keep.itemized = true;
        out.push_back(std::move(keep));
    }
}

// .rotor/.batch argument parsing, shared by the eager arm and the lazy view
// over an endless source. Sizes CYCLE (rotor(2, 3) is windows of 2, 3, 2, …);
// `size => gap` starts the next window size+gap later (negative overlaps);
// batch implies :partial, rotor drops a short final window unless :partial.
struct RotorSpec { long long n, step; };
// `.map` needs a Callable. Anything else is X::Cannot::Map, which carries what
// was being mapped, how it was spelled, and the mistake it usually is — a
// Whatever swallowed by a list, a missing block, a `.classify` written as a
// `.map` (Nil-Any sheet NA-20).
[[noreturn]] void throwCannotMap(Interpreter& I, const std::string& what, const Value& using_);

// A START POSITION that is negative, or past what an Int can hold, is out of
// range for every string search that takes one — index, rindex, indices,
// contains, substr-eq — and the answer is a RETURNED Failure naming the method,
// the value and the range (Str sheet ST-27; roast asserts it with fails-like).
// A position merely past the END of the string is not an error.
Value outOfRangePos(Interpreter& I, const std::string& what, const Value& got,
                    const std::string& subject) {
    const std::string range = "0.." + std::to_string(graphemeCount(subject));
    const std::string msg = "start argument to " + what + " out of range. Is: " +
                            got.gist() + "; should be in " + range;
    Value f = rakuppNewFailure();
    (*f.hash())["exception"] = I.makeTypedEx("X::OutOfRange",
        {{"got", got}, {"what", Value::str("start argument to " + what)},
         {"range", Value::str(range)}}, msg);
    (*f.hash())["message"] = Value::str(msg);
    return f;
}

static void parseRotorSpecs(const ValueList& args, bool isBatch,
                            std::vector<RotorSpec>& specs, bool& partial) {
    for (auto& a : args)
        if (a.isNumeric() && a.toInt() <= 0) {
            // a real INSTANCE, so `.got` / `.range` / `.what` answer — the suite
            // asks `throws-like …, X::OutOfRange, got => 0`
            const std::string msg = "batch size is out of range. Is: " +
                std::to_string(a.toInt()) + ", should be in 1..^Inf";
            throw RakuError{g_makeTypedEx
                ? g_makeTypedEx("X::OutOfRange",
                    {{"got", a}, {"range", Value::str("1..^Inf")},
                     {"what", Value::str(isBatch ? "Batching sublist length is"
                                                 : "Rotorizing sublist length is")}}, msg)
                : Value::typeObj("X::OutOfRange"), msg};
        }
    partial = isBatch;
    // `.rotor(*@cycle)` is SLURPY, so a Positional argument spreads:
    // `.rotor(flat (3 xx $a), (2 xx $b))` hands over one list and means
    // the sizes inside it.
    ValueList flatArgs;
    for (auto& a : args) {
        if ((a.t == VT::Array || a.t == VT::Range) && !a.itemized)
            for (auto& x : a.flatten()) flatArgs.push_back(x);
        else flatArgs.push_back(a);
    }
    for (auto& a : flatArgs) {
        if (a.t == VT::Pair && a.s == "partial") { if (!a.pairVal() || a.pairVal()->truthy()) partial = true; }
        // `.batch(:2elems)` names the batch SIZE. It fell into the `size => gap`
        // arm below, where the key "elems" numified to 0 and the size became 1
        // with a gap of 1 + 2 — so `(1..5).batch(:2elems)` was `((1,), (4,))`
        // (Nil-Any sheet NA-30).
        else if (a.t == VT::Pair && !a.pairKey() && a.s == "elems" && a.pairVal()) {
            long long n = a.pairVal()->toInt(); if (n < 1) n = 1;
            specs.push_back({n, n});
        }
        else if (a.t == VT::Pair && a.pairVal()) {
            long long n = a.pairKey() ? a.pairKey()->toInt() : std::atoll(a.s.c_str());
            if (n < 1) n = 1;
            specs.push_back({n, n + a.pairVal()->toInt()});
        }
        else if (a.isNumeric()) { long long n = a.toInt(); if (n < 1) n = 1; specs.push_back({n, n}); }
    }
    if (specs.empty()) specs.push_back({1, 1});
}

[[noreturn]] void throwCannotMap(Interpreter& I, const std::string& what, const Value& u) {
    const bool listy = u.t == VT::Array || u.t == VT::Range;
    const bool hashy = u.t == VT::Hash;
    std::string using_ = listy ? "a List" : hashy ? "a Hash"
                                                  : "'" + I.methodCall(u, "raku", ValueList{}).toStr() + "'";
    std::string suggestion =
        hashy ? "Did you mean to add a stub ({ ... }) or did you mean to .classify?"
      : listy ? "Did a * (Whatever) get absorbed by a comma, range, series, or list repetition?\n"
                "Consider using a block if any of these are necessary for your mapping code."
              : "Did a * (Whatever) get absorbed by a list?";
    I.throwTypedV("X::Cannot::Map",
                  {{"what", Value::str(what)}, {"using", Value::str(using_)},
                   {"suggestion", Value::str(suggestion)}},
                  "Cannot map a " + what + " using " + using_);
}

} // namespace rakupp

// The TAIL of the method-dispatch chain.
//
// methodCallInner was a single 9,138-line function — 61% of Builtins.cpp, and
// enough on its own to make a GCC -O3 build of that file take 88s against
// clang's 27s, because the optimiser is superlinear in function size. This is
// the last ~2,200 lines of that chain, moved out verbatim.
//
// It is a SEGMENT, not a category. The chain is ORDER-SENSITIVE — an earlier arm
// shadows a later one — so these arms must keep running after everything left in
// Builtins.cpp and before the unknown-method fallthrough. Do not reorder them
// against the rest, and add a new arm where its priority belongs, not where it
// reads nicely.
//
// Returning std::optional lets every arm keep its original `return X;`: nothing
// inside was rewritten, so a `return` in a nested lambda still means what it did.
// nullopt = "not handled here", and the caller falls through to the next segment.
namespace rakupp {

std::optional<Value> Interpreter::methodCallTail(const Value& inv, const MName& m,
                                                 ValueList& args,
                                                 const std::vector<ExprPtr>* rwArgs) {
    auto a0 = [&]() -> Value { return args.empty() ? Value::any() : args[0]; };
    if (m == "fmt" && inv.t == VT::Pair)
        return Value::str(doSprintf(args.empty() ? "%s\t%s" : a0().toStr(),
                                    {inv.pairKey() ? *inv.pairKey() : Value::str(inv.s),
                                     inv.pairVal() ? *inv.pairVal() : Value::any()}));
    if (m == "fmt" && inv.t != VT::Array && inv.t != VT::Range && inv.t != VT::Hash)
        return Value::str(doSprintf(args.empty() ? "%s" : a0().toStr(), {inv}));
    // Cool.printf / Cool.sprintf: the invocant IS the format ("%s\n".printf($x))
    // An ARRAY argument spreads into the directive list, exactly like the
    // sprintf() builtin: `"%08x-%04x…".sprintf(@unpacked)` formats the
    // elements, not the array-as-one-value (UUID::V4 builds its UUIDs so).
    auto spreadFmtArgs = [&]() {
        ValueList out;
        for (auto& x : args) {
            if (x.t == VT::Array && x.arr() && !x.itemized)
                for (auto& e : *x.arr()) out.push_back(e);
            else out.push_back(x);
        }
        return out;
    };
    if (m == "printf" && (inv.t == VT::Str || inv.t == VT::Match)) {
        // ioEmit, not std::cout: it takes the output lock, and it honours a
        // rebound `$*OUT`. Writing the stream directly meant
        // `my $*OUT = open(…); "%s\n".printf(…)` went to the terminal while
        // `say` on the next line went to the file.
        ValueList fa = spreadFmtArgs();
        return ioEmit(doSprintf(inv.toStr(), fa, langRev_), "$*OUT", false);
    }
    if (m == "sprintf" && (inv.t == VT::Str || inv.t == VT::Match)) {
        ValueList fa = spreadFmtArgs();
        return Value::str(doSprintf(inv.toStr(), fa, langRev_));
    }
    // Str.parse-base($radix) — "ff".parse-base(16) == 255; fractions give a Rat
    if (m == "parse-base" && (inv.t == VT::Str || inv.t == VT::Match) && !args.empty()) {
        std::string s = inv.toStr(); long long base = a0().toInt();
        // The digits and the sign may be written in any script Unicode gives a
        // value to: U+2212 MINUS is a minus, and an Nd digit is its value
        // (roast parse-base.t parses "๕๖๗۶۷៤៥１２３"). The parse below is
        // byte-oriented, so both are folded to ASCII first — the same step
        // numifyStr takes.
        {
            for (size_t k = 0; (k = s.find("\xE2\x88\x92", k)) != std::string::npos; )
                s.replace(k, 3, "-");
            bool anyHigh = false;
            for (unsigned char c : s) if (c >= 0x80) { anyHigh = true; break; }
            if (anyHigh) {
                std::string t;
                for (uint32_t cp : utf8cp(s)) {
                    int dv = cp >= 0x80 ? uniDigitValue(cp) : -1;
                    if (dv >= 0) t += (char)('0' + dv); else t += cpToU8(cp);
                }
                s = std::move(t);
            }
        }
        if (base < 2 || base > 36)
            return armedFailure("X::Syntax::Number::RadixOutOfRange",
                                "Radix " + std::to_string(base) + " out of range (allowed: 2..36)");
        size_t i2 = 0; bool neg = false;
        if (i2 < s.size() && (s[i2] == '-' || s[i2] == '+')) { neg = s[i2] == '-'; i2++; }
        auto digval = [&](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'z') return c - 'a' + 10;
            if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
            return -1;
        };
        auto badDigits = [&]() -> Value {
            return armedFailure("X::Str::Numeric",
                                "Cannot convert string to number: '" + s +
                                "' is not a valid base-" + std::to_string(base) + " number");
        };
        BigInt whole(0); bool any = false;
        for (; i2 < s.size() && s[i2] != '.'; i2++) {
            if (s[i2] == '_') continue;
            int d = digval(s[i2]);
            if (d < 0 || d >= base) return badDigits();
            whole = whole * BigInt(base) + BigInt(d); any = true;
        }
        BigInt fnum(0), fden(1);
        if (i2 < s.size() && s[i2] == '.') {
            for (i2++; i2 < s.size(); i2++) {
                if (s[i2] == '_') continue;
                int d = digval(s[i2]);
                if (d < 0 || d >= base) return badDigits();
                fnum = fnum * BigInt(base) + BigInt(d); fden = fden * BigInt(base); any = true;
            }
        }
        if (!any) return badDigits();
        if (fden.fitsLL() && fden.toLL() == 1) {
            if (neg) whole = -whole;
            return Value::bigint(whole);
        }
        BigInt num = whole * fden + fnum;
        if (neg) num = -num;
        return Value::rat(std::move(num), std::move(fden));
    }
    // Str.indices($needle, :overlap) — every start position of the substring
    if (m == "indices" && (inv.t == VT::Str || inv.t == VT::Match) && !args.empty()) {
        std::string s = inv.toStr(), needle = a0().toStr();
        bool overlap = false, icase = false, imark = false;
        for (auto& a : args) if (a.t == VT::Pair) {
            if (a.s == "overlap") overlap = !a.pairVal() || a.pairVal()->truthy();
            else if (a.s == "i" || a.s == "ignorecase") icase = !a.pairVal() || a.pairVal()->truthy();
            else if (a.s == "smartcase" && sixE() && !args.empty())
                icase = (!a.pairVal() || a.pairVal()->truthy()) && strHasNoUpper(args[0].toStr()); // 6.e
            else if (a.s == "m" || a.s == "ignoremark") imark = !a.pairVal() || a.pairVal()->truthy();
        }
        // a second positional is the CHARACTER position to start looking from.
        // A NEGATIVE one, or one past what an Int can hold, is out of range and
        // answers a Failure rather than searching from the start
        // (Str sheet ST-27).
        size_t from = 0;
        for (size_t i = 1; i < args.size(); i++)
            if (args[i].t != VT::Pair) {
                if (args[i].isNumeric()) {
                    double fd = args[i].toNum();
                    if (fd < 0 || fd > 9.2e18) return outOfRangePos(*this, "indices", args[i], s);
                }
                from = charToByte(s, args[i].toInt());
                break;
            }
        if (imark) { s = markFold(s); needle = markFold(needle); }
        if (icase) {
            auto fold = [](const std::string& in) {
                std::string o; for (auto c : utf8cp(in)) o += cpToU8(toLowerCp(c)); return o;
            };
            s = fold(s); needle = fold(needle);
        }
        Value out = Value::array(); out.isList = true;
        // the answers are CHARACTER positions, not byte offsets
        auto charPos = [&](size_t byte) { // GRAPHEME positions, as .index answers (this counted codepoints)
            return graphemeCount(s.substr(0, std::min(byte, s.size())));
        };
        // An EMPTY needle is found at EVERY position, the one past the end
        // included: `"foo".indices("")` is (0, 1, 2, 3) (roast indices.t).
        if (needle.empty()) {
            if (from <= s.size())
                for (size_t g = charPos(from); g <= graphemeCount(s); g++)
                    out.arr()->push_back(Value::integer((long long)g));
            return out;
        }
        if (from <= s.size())
            for (size_t p = s.find(needle, from); p != std::string::npos;
                 p = s.find(needle, p + (overlap ? 1 : needle.size())))
                out.arr()->push_back(Value::integer(charPos(p)));
        return out;
    }
    // Str.chop($n = 1)
    // Real numbers are their own conjugate; a Cool number chops its string form.
    if (m == "conj" && (inv.t == VT::Int || inv.t == VT::Num || inv.t == VT::Rat || inv.t == VT::Bool)) return inv;
    // .lsb / .msb — least / most significant set bit of an Int (Nil for 0).
    if ((m == "lsb" || m == "msb") && (inv.t == VT::Int || inv.t == VT::Bool)) {
        // A NEGATIVE number is measured in two's complement, where the top bit
        // is the sign: `msb` is the length of |n| - 1, so -1 is 0 and both -255
        // and -256 are 8. Measuring |n| instead answered 7 for -126 and -255
        // (S32-num/int.t). The SUB form already had this rule; the method did
        // not, and the two disagreed on the same number.
        const bool neg = inv.big() ? inv.big()->sign < 0 : inv.toInt() < 0;
        // a BIG integer counts its bits by halving — 64 bits is not the limit
        if (inv.big() && !inv.big()->fitsLL()) {
            BigInt n = inv.big()->abs(), two(2LL), q, r;
            if (n.isZero()) return Value::nil();
            if (neg && m == "msb") n = n - BigInt(1);
            long long lsb = -1, bit = 0;
            while (!n.isZero()) {
                BigInt::divmod(n, two, q, r);
                if (!r.isZero() && lsb < 0) lsb = bit;
                n = q; bit++;
            }
            // for a negative msb the loop already measured |n| - 1, whose
            // LENGTH is the answer — not its top index
            return Value::integer(m == "lsb" ? lsb : neg ? bit : bit - 1);
        }
        long long v = inv.toInt();
        if (v == 0) return Value::nil();
        unsigned long long u = v < 0 ? (unsigned long long)(-(v + 1)) + 1ull : (unsigned long long)v;
        if (m == "lsb") return Value::integer(rakupp::ctzll(u));
        if (!neg) return Value::integer(63 - rakupp::clzll(u));
        unsigned long long below = u - 1ull;                 // |v| - 1
        return Value::integer(below == 0 ? 0 : 64 - rakupp::clzll(below));
    }
    if (m == "chop" && (inv.t == VT::Int || inv.t == VT::Num || inv.t == VT::Rat || inv.t == VT::Complex))
        return methodCall(Value::str(inv.toStr()), "chop", std::move(args), rwArgs);
    if (m == "chop" && (inv.t == VT::Str || inv.t == VT::Match)) {
        auto cps = utf8cp(inv.toStr());
        long long n = args.empty() ? 1 : a0().toInt();
        if (n < 0) n = 0;
        // by GRAPHEME, as .flip/.substr/.comb count: "x\x[301]".chop is "" (one cluster), not "x"
        auto starts = uniGraphemeStarts(cps);
        size_t keep = n == 0 ? cps.size() : (size_t)n >= starts.size() ? 0 : starts[starts.size() - (size_t)n];
        std::string r;
        for (size_t k = 0; k < keep; k++) r += cpToUtf8(cps[k]);
        return Value::str(r);
    }
    // numeric .narrow — the narrowest type that holds the value.
    //
    // For a Num, Rakudo's test is APPROXIMATE: the value narrows when its `.Int`
    // is `=~=` to it, a RELATIVE comparison against $*TOLERANCE (1e-15). That is
    // what S32-num/narrow.t asserts — `((.1e0 + .2e0) * 10).narrow` is the Int
    // 3, not 3.0000000000000004 — and an exact test cannot answer it.
    //
    // One half of that rule is NOT copied. `=~=` falls back to an ABSOLUTE
    // comparison when either side is zero, which makes every number under 1e-15
    // narrow to 0: Rakudo answers 0 for `1e-300.narrow`, destroying the value
    // outright. Nothing in Roast asks for that, so a Num narrows to zero only
    // when it IS zero. (Int-Num-Rat sheet N-19 records the whole rule as a
    // Rakudo bug; this follows Roast where Roast speaks and declines the rest.)
    if (m == "narrow" && inv.isNumeric()) {
        if (inv.t == VT::Rat && inv.ratN() && inv.ratD() && inv.ratD()->fitsLL() && inv.ratD()->toLL() == 1)
            return Value::bigint(*inv.ratN());
        if (inv.t == VT::Num && std::isfinite(inv.n)) {
            // past the int64 range a double is still an exact integer — 1e20 and
            // (2**70).Num both are — and numToIntExact converts without loss.
            if (inv.n == std::trunc(inv.n)) return numToIntExact(inv.n);
            double whole = std::trunc(inv.n);
            if (whole != 0.0 &&
                std::fabs(inv.n - whole) <= 1e-15 * std::fabs(inv.n))
                return numToIntExact(whole);
        }
        return inv;
    }
    // .UInt — Int coercion that fails on negatives
    if (m == "UInt") {
        // a non-numeric string is a FAILURE, not a silent 0 — same ladder as .Int
        if (inv.t == VT::Str || inv.t == VT::Match) {
            Value nv = numifyStrFailure(inv.toStr());
            if (nv.t == VT::Hash && nv.hashKind == "Failure") return nv;
        }
        // exact past int64: a big Int stays big, a large Num converts exactly
        if ((inv.t == VT::Int && inv.big()) || (inv.t == VT::Num && std::fabs(inv.n) >= 9223372036854775807.0)) {
            Value iv = inv.t == VT::Int ? inv : numToIntExact(std::trunc(inv.n));
            if (iv.t == VT::Int && iv.big() ? iv.big()->sign < 0 : iv.toInt() < 0)
                return armedFailure("X::OutOfRange", "Cannot coerce " + iv.toStr() + " to UInt: it is negative");
            return iv;
        }
        long long v = inv.toInt();
        if (v < 0) return armedFailure("X::OutOfRange",
            "Cannot coerce " + std::to_string(v) + " to UInt: it is negative");
        return Value::integer(v);
    }
    // Baggy.kxxv — every key repeated by its weight
    if (m == "kxxv" && inv.t == VT::Hash && inv.hash() &&
        (inv.hashKind == "Bag" || inv.hashKind == "BagHash" || inv.hashKind == "Set" || inv.hashKind == "SetHash")) {
        Value out = Value::array(); out.isList = true;
        for (auto& kv : *inv.hash()) {
            long long n = inv.hashKind[0] == 'S' ? 1 : kv.second.toInt();
            for (long long k = 0; k < n; k++) out.arr()->push_back(Value::str(kv.first));
        }
        return out;
    }
    // Rat.base-repeating($radix) — (non-repeating part, repeating cycle)
    if (m == "base-repeating" && inv.t == VT::Rat && inv.ratN() && inv.ratD() && !args.empty()) {
        long long base = a0().toInt();
        if (base < 2 || base > 36)
            return armedFailure("X::Syntax::Number::RadixOutOfRange",
                                "Radix " + std::to_string(base) + " out of range (allowed: 2..36)");
        auto digchr = [](int d) -> char { return d < 10 ? char('0' + d) : char('A' + d - 10); };
        BigInt n = inv.ratN()->abs(), d = inv.ratD()->abs();
        std::string sign = inv.ratN()->sign < 0 ? "-" : "";
        BigInt q, r; BigInt::divmod(n, d, q, r);
        std::string whole = sign + q.toString(); // NB: decimal digits of the WHOLE part are base-10 for base 10 only
        if (base != 10) { // re-render the whole part in the target base
            BigInt w = q; std::string ws;
            if (w.isZero()) ws = "0";
            while (!w.isZero()) { BigInt q2, r2; BigInt::divmod(w, BigInt(base), q2, r2); ws.insert(ws.begin(), digchr((int)r2.toLL())); w = q2; }
            whole = sign + ws;
        }
        std::string fracDigits, cycle;
        std::map<std::string, size_t> seen; // remainder -> position in fracDigits
        BigInt rem = r;
        while (!rem.isZero() && fracDigits.size() < 10000) {
            std::string key = rem.toString();
            auto it = seen.find(key);
            if (it != seen.end()) { cycle = fracDigits.substr(it->second); fracDigits = fracDigits.substr(0, it->second); break; }
            seen[key] = fracDigits.size();
            rem = rem * BigInt(base);
            BigInt q2, r2; BigInt::divmod(rem, d, q2, r2);
            fracDigits += digchr((int)q2.toLL());
            rem = r2;
        }
        Value out = Value::array(); out.isList = true;
        out.arr()->push_back(Value::str(whole + "." + fracDigits));
        out.arr()->push_back(Value::str(cycle));
        return out;
    }

    // the KEY/POS protocol on an undefined scalar: vacuously empty (xxKEY.t)
    if ((inv.t == VT::Any || inv.t == VT::Nil) && inv.enumName.empty()) {
        if (m == "EXISTS-KEY" || m == "EXISTS-POS") return Value::boolean(false);
        if ((m == "AT-KEY" || m == "AT-POS") && !args.empty()) return Value::any();
        if ((m == "DELETE-KEY" || m == "DELETE-POS") && !args.empty()) return Value::nil();
    }
    // Pair
    // low-level access protocol as ordinary methods (xxKEY.t etc.)
    if (inv.t == VT::Hash && inv.hash()) {
        // on an OBJECT-KEYED hash (declared `{Mu:U}`) a TYPE-OBJECT key keys by
        // its (parenthesised) name, not its empty stringification —
        // `%!Conversions{Mu:U} handles <AT-KEY EXISTS-KEY>` stores per-type
        // converters and `{Str}` must not collide with `{Int}` (DBIish). A
        // plain hash keeps Rakudo's ""-key for type objects.
        bool objKeyed = inv.objKeyed;
        // The payload index for a key. An OBJECT-KEYED hash indexes by IDENTITY,
        // so `1` and `"1"` stay two entries (sheet HM-04); a type object keeps
        // its parenthesised name, which the declaration paths also write.
        auto kkey = [objKeyed](const Value& k) -> std::string {
            return objKeyed ? objHashIndex(k) : k.toStr();
        };
        if (m == "AT-KEY" && !args.empty()) {
            auto it = inv.hash()->find(kkey(args[0]));
            return it != inv.hash()->end() ? it->second : Value::any();
        }
        if (m == "EXISTS-KEY" && !args.empty())
            return Value::boolean(inv.hash()->count(kkey(args[0])) > 0);
        if (m == "DELETE-KEY" && !args.empty()) {
            auto it = inv.hash()->find(kkey(args[0]));
            if (it == inv.hash()->end()) return Value::any();
            Value v = it->second; inv.hash()->erase(it); return v;
        }
        if ((m == "ASSIGN-KEY" || m == "BIND-KEY") && args.size() >= 2) {
            const std::string k = kkey(args[0]);
            if (!objHashKeyType(inv).empty()) {
                Value stored = args[0]; stored.itemized = false;   // as the subscript path does
                inv.hash()->setObjKey(k, stored);
            }
            Value& slot = (*inv.hash())[k];
            slot = args[1];
            // BIND-KEY puts the value in the slot with NO Scalar container around
            // it, so the element is immutable afterwards — exactly as `%h<k> := v`
            // is. Binding something that NAMES a container (the argument arrives
            // as a Proxy, taken raw by the eval arm) aliases it instead, and
            // writing through that alias is the whole point of it.
            //
            // Only the METHOD spelling was missing this; the subscript spelling
            // has marked it all along. Hash::Agnostic is written entirely in the
            // method spelling — `method BIND-KEY($key,\value) is raw { %!hash.BIND-KEY($key,value) }`
            // — so a bound key stayed writable there, and seven dists sit behind it.
            if (m == "BIND-KEY" && !(args[1].t == VT::Hash && args[1].hashKind == "Proxy"))
                slot.readonly = true;
            return args[1];
        }
    }
    // `@a.BIND-POS($i, $container)` — the positional twin of BIND-KEY. The value
    // arrives as the caller's CONTAINER (the eval arm takes that argument raw),
    // so storing it as-is makes the slot an alias for it: reading `@a[$i]` in a
    // value context fetches through it, and binding to `@a[$i]` picks the same
    // container up. BinaryHeap's `!sift-down` is built entirely on this.
    if (inv.t == VT::Array && inv.arr() && m == "BIND-POS" && args.size() >= 2) {
        if (inv.isList && inv.s.empty() && inv.enumName.empty())
            throwTyped("X::Immutable", {{"method", m}, {"typename", "List"}},
                "Cannot call 'BIND-POS' on an immutable 'List'");
        long long i = args[0].toInt();
        if (i < 0) i += (long long)inv.arr()->size();
        if (i < 0) i = 0;
        while ((long long)inv.arr()->size() <= i) inv.arr()->push_back(Value::any());
        (*inv.arr())[(size_t)i] = args[1];
        return args[1];
    }
    // (Array AT-POS/EXISTS-POS/ASSIGN-POS/DELETE-POS are fully handled in
    // methodCallPart3, which runs before this file — no Array arm here.)
    if (inv.t == VT::Pair) {
        if (m == "Pair") return inv;   // .Pair on a Pair is itself
        if (m == "key") return inv.pairKey() ? *inv.pairKey() : Value::str(inv.s); // object/array keys preserved
        if (m == "value") return inv.pairVal() ? *inv.pairVal() : Value::any();
        if (m == "kv") { Value o = Value::array({inv.pairKey() ? *inv.pairKey() : Value::str(inv.s), inv.pairVal() ? *inv.pairVal() : Value::any()}); o.isList = true; return o; }
        if (m == "antipair") {
            // The VALUE becomes the key, as itself — `(a => 1).antipair` is
            // `1 => "a"`, an Int key, not the string "1" (sheet HM-17). Same
            // rule the list forms below already follow.
            Value val = inv.pairVal() ? *inv.pairVal() : Value::any();
            Value p = Value::pair(val.toStr(), Value::str(inv.s));
            if (val.t != VT::Str) p.pairKeyM() = std::make_shared<Value>(val);
            return p;
        }
        // `.freeze` snapshots the value out of its container. rakupp's Pair already
        // copies rather than binding, so the pair is frozen the moment it is built —
        // if Pair ever holds a real container this has to copy pairVal explicitly.
        if (m == "freeze") return inv;
        // `.hash` / `.Hash` / `.Map` on a Pair is the one-entry hash it describes
        if (m == "hash" || m == "Hash" || m == "Map") {
            Value h = Value::makeHash();
            h.hashRef()[inv.s] = inv.pairVal() ? *inv.pairVal() : Value::any();
            if (m == "Map") h.hashKind = "Map";
            return h;
        }
        // A Pair is ONE element, so its list views are one long: `.keys` is the key,
        // not an index (that is what a Positional would answer).
        if (m == "keys" || m == "values" || m == "pairs") {
            Value o = Value::array(); o.isList = true;
            o.arr()->push_back(m == "keys"   ? (inv.pairKey() ? *inv.pairKey() : Value::str(inv.s))
                           : m == "values" ? (inv.pairVal() ? *inv.pairVal() : Value::any())
                                           : inv);
            return o;
        }
        // `.antipairs`/`.invert` are the LIST forms of .antipair — and a list
        // VALUE inverts to one pair per element: `(a => (1,2)).invert` is
        // (1 => "a", 2 => "a")
        if (m == "antipairs" || m == "invert") {
            Value o = Value::array(); o.isList = true;
            Value val = inv.pairVal() ? *inv.pairVal() : Value::any();
            // a HASH value inverts to one pair per ENTRY, and the entry itself is the
            // new key: `:foo{ :42a, :72b }.invert` is ((:a(42)) => "foo", …)
            if (m == "invert" && val.t == VT::Hash && val.hash()) {
                Value o2 = Value::array(); o2.isList = true;
                for (auto& kv : *val.hash()) {
                    Value p = Value::pair("", Value::str(inv.s));
                    p.pairKeyM() = std::make_shared<Value>(Value::pair(kv.first, kv.second));
                    o2.arr()->push_back(std::move(p));
                }
                return o2;
            }
            ValueList vs = (m == "invert" && val.t == VT::Array && val.arr()) ? *val.arr() : ValueList{val};
            for (auto& v : vs) {
                Value p = Value::pair(v.toStr(), Value::str(inv.s));
                // a Str key needs no separate key VALUE — carrying one makes the
                // pair render as `"bar" => "foo"` instead of `:bar("foo")`
                if (v.t != VT::Str) p.pairKeyM() = std::make_shared<Value>(v);
                o.arr()->push_back(std::move(p));
            }
            return o;
        }
    }

    // A TYPE OBJECT (or an undefined value) is ONE element too, as `.List`
    // already answered: `Any.Array` is [(Any)], not an error. Coercing an
    // absent hash lookup with `.Array` is how one Weekly Challenge solution
    // reads a graph's missing edges.
    // …and Nil is one element too — but an ARRAY ELEMENT cannot hold Nil: a
    // stored Nil becomes the element default, so `Nil.Array` is `[Any]`
    // (Nil-Any sheet NA-04, and the same rule NA-11 gives `[Nil]`).
    if (m == "Array" && (inv.t == VT::Type || inv.t == VT::Any || inv.t == VT::Nil)) {
        Value one = Value::array();
        one.arr()->push_back(inv.t == VT::Nil ? Value::any() : inv);
        return one;
    }
    if ((m == "Hash" || m == "hash") && (inv.t == VT::Type || inv.t == VT::Any || inv.t == VT::Nil))
        return Value::makeHash();
    // scalar .Array / .List — a 1-element container: "LLL".Array is ["LLL"]
    if ((m == "Array" || m == "List") &&
        (inv.t == VT::Int || inv.t == VT::Num || inv.t == VT::Rat || inv.t == VT::Bool ||
         inv.t == VT::Str || inv.t == VT::Complex || inv.t == VT::Pair)) {
        Value one = Value::array(); one.arr()->push_back(inv);
        one.isList = (m == "List");
        return one;
    }
    // A TYPE OBJECT is an EMPTY list, not a one-element one: `Num.pairs` is (),
    // `Range.reduce(&[+])` is Nil. (An INSTANCE of the same type is one element —
    // that is the branch just below.)
    if (inv.t == VT::Type || inv.t == VT::Any || inv.t == VT::Nil) {
        // …but only for the KEY/VALUE family. `.map`/`.sort`/`.grep` still see a
        // ONE-element list (`Int.map({$_})` is `((Int))`) — a type object has no
        // ELEMENTS to pair up, yet it is still a single thing to iterate.
        // `.pairup` joins them: it PAIRS the elements, and there are none.
        static const std::set<std::string> emptyList = {
            "pairs", "antipairs", "kv", "keys", "values", "invert", "pairup"};
        if (emptyList.count(m)) { Value o = Value::array(); o.isList = true; return o; }
        // Neither reducer has a seed to start from, and neither dies: both
        // answer Nil (NA-25). `.tree` and `.are` answer the invocant itself
        // (NA-44, NA-31) — there is no structure to descend into.
        if (m == "reduce" || m == "produce") return Value::nil();
        // Methods Rakudo declares only for a DEFINED invocant have no candidate
        // here, and a missing candidate is X::Multi::NoMatch, not a quiet
        // answer (NA-05, NA-28, NA-30, NA-45).
        static const std::set<std::string> needsDefined = {
            "batch", "rotor", "toggle", "slice", "splice",
            "minmax", "min", "max", "sum"};
        if (needsDefined.count(m))
            throw RakuError{Value::typeObj("X::Multi::NoMatch"),
                            "Cannot resolve caller " + (const std::string&)m +
                                "(" + inv.typeName() + ":U: ...); none of these signatures matches"};
    }
    // list methods on a lone scalar treat it as a 1-element list: 42.grep(*>3), 'x'.map(...)
    // (a Code is one too — `(&say).kv` is `(0, &say)`)
    if (inv.t == VT::Code &&
        (m == "kv" || m == "pairs" || m == "antipairs" || m == "keys" || m == "values")) {
        Value one = Value::array(); one.arr()->push_back(inv); one.isList = true;
        return methodCall(one, m, args, rwArgs);
    }
    if ((inv.t == VT::Int || inv.t == VT::Num || inv.t == VT::Rat || inv.t == VT::Bool ||
         inv.t == VT::Str || inv.t == VT::Complex || inv.t == VT::Pair ||
         inv.t == VT::Type ||  // a type object is one item to ITERATE (see above)
         inv.t == VT::Any || inv.t == VT::Nil) && // …and so is an undefined value
        (m == "grep" || m == "map" || m == "flatmap" || m == "first" || m == "sort" || m == "reverse" ||
         m == "flat" || m == "reduce" || m == "grep-index" || m == "first-index" || m == "Supply" ||
         m == "head" || m == "tail" || m == "skip" || m == "elems" || m == "end" ||
         m == "keys" || m == "values" || m == "kv" || m == "pairs" || m == "batch" ||
         m == "rotor" || m == "unique" || m == "squish" || m == "antipairs" ||
         m == "collate" ||     // `Supply.collate` is `(Supply,).collate` (Roast collate.t)
         m == "repeated" || m == "produce" || m == "pairup" ||
         // …but an ENUM type object picks from its VALUES, which a later arm
         // knows how to enumerate: `Order.pick` is one of Less/Same/More, not
         // the type object itself.
         ((m == "pick" || m == "roll") && !(inv.t == VT::Type && inv.s == "Order")) ||
         m == "slice" || m == "Slip" || m == "chrs" ||
         // `.hash`/`.Hash`/`.Map` read the invocant as a hash INITIALIZER, so a
         // lone non-Pair is an odd-element store: `42.hash` and `Any.Map` are
         // X::Hash::Store::OddNumber, which the list arm already raises.
         // (`.hash`/`.Hash` on an undefined invocant answered `{}` above.)
         m == "hash" || m == "Hash" || m == "Map" ||
         // …but Supply declares classify/categorize for an INSTANCE only, and
         // roast (S17-supply/classify.t) asks for the death: a class-method call
         // must not quietly classify the one-element list `(Supply,)`.
         ((m == "classify" || m == "categorize") && !(inv.t == VT::Type && inv.s == "Supply")) ||
         m == "combinations" || m == "permutations")) {
        // …but a bad `.map` argument is reported against the SCALAR, not the
        // one-element list it is about to become: `42.map(1)` says "Cannot map
        // a Int" (Nil-Any sheet NA-20).
        if (m == "map" && !args.empty() && args[0].t != VT::Code &&
            !(args[0].t == VT::Pair && args[0].namedArg))
            throwCannotMap(*this, inv.typeName(), args[0]);
        // toList keeps the scalar as one item, but a Blob/Buf expands to its
        // BYTES (`$blob.rotor(3, :partial)` in Base64 chunks byte-wise)
        Value one = Value::array(); *one.arr() = toList(inv); one.isList = true;
        return methodCall(one, m, args, rwArgs);
    }

    // lazy list (infinite `… … *` or a lazy `.map` over one): keep `.map`/`.head`
    // lazy so consumers materialise only what they index.
    if (inv.t == VT::Array && inv.ext()) {
        auto lst = std::static_pointer_cast<LazySeqState>(inv.ext());
        bool infinite = lst->infinite;
        if (m == "is-lazy") {
            // A gather has not been run yet, so whether it is lazy is not known
            // until it has been. This is the only question that asks without
            // consuming, so it does the first pull itself; a gather that turns
            // out to be finite answers False, as it did when the probe was eager.
            if (lst->gatherSeq) { materializeLazy(inv, 1); return Value::boolean(!lst->exhausted); }
            return Value::boolean(true);
        }
        if (infinite) {
            // operations that need the end of the list can't complete on an infinite
            // source (.List/.Array/.gist stay ANSWERABLE — lazy views and "(...)"
            // — in their own arms below, as in Rakudo)
            // Rakudo splits the refusals two ways, and which way it goes is
            // observable: some hand back an ARMED FAILURE, so `my $t =
            // @lazy.sum` only detonates when $t is used, and the rest THROW at
            // once (sheets NA-23, LA-27, LA-23). Measured against the 2026.08
            // binary for both a lazy List and a lazy Array; the two agree
            // except for `pop`, which a List refuses as X::Immutable instead.
            static const std::set<std::string> kLazyFailure = {
                "elems", "reverse", "sum", "pick", "roll", "Capture", "rotate",
                "Numeric", "Int", "pop"};
            static const std::set<std::string> kLazyThrow = {
                "end", "tail", "sort", "min", "max", "eager", "reduce",
                "push", "append", "grab"};
            // …but `.roll($n)` and `.List` of a lazy ARRAY throw where the
            // no-argument forms fail, and a lazy LIST answers `.List` with
            // itself.
            if (m == "roll" && !args.empty())
                throwTyped("X::Cannot::Lazy", {{"action", "roll"}}, "Cannot roll a lazy list");
            if (m == "List" && !inv.isList)
                throwTyped("X::Cannot::Lazy", {{"action", "List"}}, "Cannot List a lazy list");
            if (kLazyFailure.count(m))
                return armedFailure("X::Cannot::Lazy", "Cannot " + m + " a lazy list");
            if (kLazyThrow.count(m))
                throwTyped("X::Cannot::Lazy", {{"action", m.s}}, "Cannot " + m + " a lazy list");
            // `join` and `Str` answer the REIFIED PREFIX with `...` for the
            // rest — `my @a = 1..*; @a[2]; @a.join(",")` is `1,2,3,...`, and a
            // list nothing has pulled from yet is just `...` (sheet LA-14).
            if (m == "join" || m == "Str") {
                std::string sep = m == "join" && !args.empty() ? args[0].toStr()
                                : m == "join" ? "" : " ";
                std::string out;
                if (inv.arr()) for (auto& e : *inv.arr()) { out += e.toStr(); out += sep; }
                return Value::str(out + "...");
            }
            if (m == "shift") { materializeLazy(inv, 1); if (inv.arr()->empty()) return Value::nil(); Value v = inv.arr()->front(); inv.arr()->erase(inv.arr()->begin()); return v; }
        } else {
            // FINITE lazy (a gather that outgrew its probe, a lazy map over a finite
            // source, …): whole-list operations force full materialisation first,
            // so .elems/.sort/.join see every element, not just the cached prefix.
            // `list`/`Seq`/`cache`/`flat` belong here for the same reason as
            // `List`/`Array`: each answers the WHOLE sequence. Without them the
            // generic arm downstream snapshots `toList(inv)` — the 64-element
            // probe prefix — so `gather for 1..200 { take $_ }.list.elems` said
            // 64, and `for … .list { }` iterated 64 times, silently dropping the
            // tail. (An ENDLESS source never reaches here: it keeps its growing
            // view in the `infinite` arm above, which is what issue #30 built.)
            static const std::set<std::string> forceAll = {
                "list", "Seq", "cache", "flat",
                "elems", "end", "pop", "tail", "reverse", "sort", "eager", "List", "Array",
                "sum", "min", "max", "minmax", "join", "Str", "gist", "raku", "perl", "reduce",
                "Numeric", "Int", "all", "any", "one", "none", "unique", "squish",
                "classify", "categorize", "Set", "Bag", "Mix", "SetHash", "BagHash",
                "MixHash", "Hash", "hash", "antipairs", "pairs", "kv", "keys", "values",
                "rotate", "pick", "roll", "combinations", "permutations", "splice"};
            if (forceAll.count(m)) materializeLazy(inv, 1000000);
        }
        if (m == "map" && !args.empty() && args[0].t == VT::Code && codeArity(args[0]) == 1) {
            Value fn = args[0], src = inv;                 // src shares arr+ext with inv
            Value out = Value::array(); out.isList = true; // 1:1 map → cache index == source index
            auto st = std::make_shared<LazySeqState>();
            st->infinite = infinite; // a view over an endless source is endless too
            Interpreter* self = this;
            st->appendNext = [self, src, fn](ValueList& cache) -> bool {
                size_t si = cache.size();
                self->materializeLazy(src, si + 1);
                if (si >= src.arr()->size()) return false;
                ValueList one{ (*src.arr())[si] };
                cache.push_back(self->callCallable(fn, one));
                return true;
            };
            out.extM() = st;
            return out;
        }
        if (m == "grep" && !args.empty()) {
            // lazy filter: each appendNext pulls source elements (bounded per call)
            // until the predicate matches, so `(^Inf).grep(…).head(3)` terminates.
            Value pred = args[0], src = inv;
            Value out = Value::array(); out.isList = true;
            auto st = std::make_shared<LazySeqState>();
            // NOT marked infinite even over an endless source: a grep can still
            // end — `(^Inf).grep({last if $_ > 5; True}).eager` is Roast's own
            // (S32-list/grep.t) — so whether it drains is only known by trying.
            auto spos = std::make_shared<size_t>(0); // next unexamined source index
            Interpreter* self = this;
            std::weak_ptr<LazySeqState> stw = st; // weak: st owns the closure
            st->appendNext = [self, src, pred, spos, stw](ValueList& cache) -> bool {
                for (long long tries = 0; tries < 1000000; tries++) { // bail on a never-matching predicate
                    self->materializeLazy(src, *spos + 1);
                    if (*spos >= src.arr()->size()) {
                        // the source stopped growing: either it truly ran out, or it
                        // is endless and hit materializeLazy's ceiling. In the second
                        // case this view has no end either, and now knows it — a
                        // reduce over it must refuse rather than fold what got pulled
                        if (isEndlessLazy(src)) if (auto s = stw.lock()) s->infinite = true;
                        return false;
                    }
                    Value v = (*src.arr())[(*spos)++];
                    bool match;
                    if (pred.t == VT::Code) {
                        self->topicWriteback_ = &(*src.arr())[*spos - 1]; // $_ mutations alias the element
                        try { match = predAnswerTruthy(*self, self->callCallable(pred, {v}), v); }
                        catch (LastEx&) { self->topicWriteback_ = nullptr; return false; } // `last` ends the grep
                        catch (NextEx&) { self->topicWriteback_ = nullptr; continue; }     // `next` skips
                        catch (RedoEx&) { self->topicWriteback_ = nullptr; (*spos)--; continue; } // `redo` retries
                        v = (*src.arr())[*spos - 1]; // keep the (possibly mutated) value
                    } else match = applyArith("~~", v, pred).truthy();
                    if (match) { cache.push_back(v); return true; }
                }
                return false;
            };
            out.extM() = st;
            return out;
        }
        if (m == "first" && !args.empty()) { // first match, scanning lazily (bounded)
            bool wantK = false, wantKv = false, wantP = false; // :k index / :kv / :p forms
            for (auto& a : args) if (a.t == VT::Pair && a.pairVal() && a.pairVal()->truthy()) {
                if (a.s == "k") wantK = true;
                else if (a.s == "kv") wantKv = true;
                else if (a.s == "p") wantP = true;
            }
            Value pred; bool havePred = false;
            for (auto& a : args) if (a.t != VT::Pair) { pred = a; havePred = true; break; }
            // `:end` scans BACKWARDS, which a list with no end cannot be asked
            // to do: there is no last element to start from. Answering the
            // first match instead — which is what an unguarded forward scan
            // did — is a different question with a plausible-looking answer.
            for (auto& a : args)
                if (a.t == VT::Pair && a.s == "end" && (!a.pairVal() || a.pairVal()->truthy()) && infinite)
                    throwTyped("X::Cannot::Lazy", {{"action", "first"}},
                               "Cannot first a lazy list");
            for (size_t si = 0; si < 1000000; si++) {
                materializeLazy(inv, si + 1);
                if (si >= inv.arr()->size()) break;
                Value v = (*inv.arr())[si];
                bool match = !havePred ? true
                           : pred.t == VT::Regex ? regexMatch(v.toStr(), pred.s).truthy()
                           : pred.t == VT::Code ? predAnswerTruthy(*this, callCallable(pred, {v}), v)
                                                : applyArith("~~", v, pred).truthy();
                if (match) {
                    if (wantK) return Value::integer((long long)si);
                    if (wantP) return Value::pair(std::to_string(si), v);
                    if (wantKv) { Value o = Value::array(); o.isList = true;
                                  o.arr()->push_back(Value::integer((long long)si));
                                  o.arr()->push_back(v); return o; }
                    return v;
                }
            }
            return Value::nil();
        }
        if (m == "skip") { // lazy skip: shared view starting n further along the source
            long long n = args.empty() ? 1 : std::max(0LL, args[0].toInt());
            Value src = inv;
            Value out = Value::array(); out.isList = true;
            auto st = std::make_shared<LazySeqState>();
            st->infinite = infinite; // a view over an endless source is endless too
            Interpreter* self = this;
            st->appendNext = [self, src, n](ValueList& cache) -> bool {
                size_t si = cache.size() + (size_t)n;
                self->materializeLazy(src, si + 1);
                if (si >= src.arr()->size()) return false;
                cache.push_back((*src.arr())[si]);
                return true;
            };
            out.extM() = st;
            return out;
        }
        if (m == "head" && (args.empty() || args[0].t != VT::Whatever)) {
            size_t n = args.empty() ? 1 : (size_t)std::max(0LL, args[0].toInt());
            materializeLazy(inv, n);
            Value out = Value::array(); out.isList = true;
            if (args.empty()) return inv.arr()->empty() ? Value::nil() : (*inv.arr())[0]; // scalar .head
            for (size_t i = 0; i < n && i < inv.arr()->size(); i++) out.arr()->push_back((*inv.arr())[i]);
            return out;
        }
        // ---- ENDLESS-source views (issue #30 follow-up). Each stays LAZY, so
        // `.kv`/`.pairs`/… over `1 xx *` stream instead of freezing at whatever
        // prefix happened to be materialised. Finite lazies keep the
        // force-then-eager path (the forceAll block above).
        if (infinite && (m == "values" || m == "Seq" || m == "list" || m == "List" ||
                         m == "lazy" || m == "cache")) {
            Value out = inv; out.isList = true; // the same shared cache + state, list-shaped
            // …and the VIEW's own type: `.List`/`.list`/`.cache` of an endless
            // Seq is a List, which is why `(1…∞) eqv (1…∞).List` is False.
            // (`.Seq` and `.lazy` stay a Seq; `.values` keeps what it had.)
            if (m == "List" || m == "list" || m == "cache") out.s.clear();
            else if (m == "Seq") out.s = "Seq";
            return out;
        }
        if (infinite && m == "Array") { Value out = inv; out.isList = false; return out; } // a lazy Array (Rakudo)
        // (.gist and .raku of an endless source are answered in segment 2, which
        // runs before this one — see MethodCallPart2's gist/raku arms)
        if (infinite && m == "keys") { // 0, 1, 2, … — as endless as the source
            Value out = Value::array(); out.isList = true;
            auto st = std::make_shared<LazySeqState>(); st->infinite = true;
            st->appendNext = [](ValueList& cache) -> bool {
                cache.push_back(Value::integer((long long)cache.size()));
                return true;
            };
            out.extM() = st;
            return out;
        }
        if (infinite && m == "kv") { // index, value, index, value, …
            Value src = inv; Interpreter* self = this;
            Value out = Value::array(); out.isList = true;
            auto st = std::make_shared<LazySeqState>(); st->infinite = true;
            st->appendNext = [self, src](ValueList& cache) -> bool {
                size_t k = cache.size() / 2; // two cache entries per source element
                self->materializeLazy(src, k + 1);
                if (k >= src.arr()->size()) return false;
                cache.push_back(Value::integer((long long)k));
                cache.push_back((*src.arr())[k]);
                return true;
            };
            out.extM() = st;
            return out;
        }
        if (infinite && (m == "pairs" || m == "antipairs")) {
            bool anti = m == "antipairs";
            Value src = inv; Interpreter* self = this;
            Value out = Value::array(); out.isList = true;
            auto st = std::make_shared<LazySeqState>(); st->infinite = true;
            st->appendNext = [self, src, anti](ValueList& cache) -> bool {
                size_t k = cache.size();
                self->materializeLazy(src, k + 1);
                if (k >= src.arr()->size()) return false;
                const Value& v = (*src.arr())[k];
                Value p;
                if (anti) { // value => index
                    p = Value::pair(v.toStr(), Value::integer((long long)k));
                    p.pairKeyM() = std::make_shared<Value>(v);
                }
                else { // index => value, with an Int key (mirrors the eager arm)
                    p = Value::pair(std::to_string(k), v);
                    p.pairKeyM() = std::make_shared<Value>(Value::integer((long long)k));
                }
                cache.push_back(std::move(p));
                return true;
            };
            out.extM() = st;
            return out;
        }
        if (infinite && m == "flat") {
            bool hammer = false;
            for (auto& a : args)
                if (a.t == VT::Pair && a.s == "hammer")
                    hammer = !a.pairVal() || a.pairVal()->truthy();
            Value src = inv; Interpreter* self = this;
            Value out = Value::array(); out.isList = true; out.s = "Seq";
            auto st = std::make_shared<LazySeqState>(); st->infinite = true;
            auto spos = std::make_shared<size_t>(0); // next source element to spread
            st->appendNext = [self, src, hammer, spos](ValueList& cache) -> bool {
                self->materializeLazy(src, *spos + 1);
                if (*spos >= src.arr()->size()) return false;
                // a Seq's element spreads by the LIST rule (ofArray = false); may
                // add ZERO elements (an empty sublist) — callers simply re-pull
                flatOneInto((*src.arr())[(*spos)++], false, hammer, cache);
                return true;
            };
            out.extM() = st;
            return out;
        }
        if (infinite && (m == "rotor" || m == "batch")) {
            std::vector<RotorSpec> specs;
            bool partial;
            parseRotorSpecs(args, m == "batch", specs, partial);
            Value src = inv; Interpreter* self = this;
            Value out = Value::array(); out.isList = true;
            auto st = std::make_shared<LazySeqState>(); st->infinite = true;
            auto pos = std::make_shared<size_t>(0); // next source index
            auto cyc = std::make_shared<size_t>(0); // spec cycle counter
            st->appendNext = [self, src, specs, partial, pos, cyc](ValueList& cache) -> bool {
                const RotorSpec& sp = specs[*cyc % specs.size()];
                self->materializeLazy(src, *pos + (size_t)sp.n);
                size_t have = src.arr()->size();
                if (*pos >= have) return false;
                if (*pos + (size_t)sp.n > have && !partial) return false;
                Value chunk = Value::array(); chunk.isList = true;
                for (size_t j = *pos; j < *pos + (size_t)sp.n && j < have; j++)
                    chunk.arr()->push_back((*src.arr())[j]);
                cache.push_back(std::move(chunk));
                (*cyc)++;
                *pos += (size_t)(sp.step < 1 ? 1 : sp.step);
                return true;
            };
            out.extM() = st;
            return out;
        }
    }

    // a Regex is a Callable with no phasers of its own (rak asks before running one)
    if (inv.t == VT::Regex && m == "has-loop-phasers") return Value::boolean(false);
    if (inv.t == VT::Regex && m == "ACCEPTS") // returns the Match (or Nil), sets $/
        return regexMatch(args.empty() ? std::string() : args[0].toStr(), inv.s);

    // quanthash smartmatch: same support with equal weights (topic coerced to
    // `.set` / `.unset` exist on SetHash ONLY — Set.^can('set') and
    // BagHash.^can('set') are both False in Rakudo, and roast checks .^can.
    // Both take a single (possibly listy) positional and return Nil.
    if (inv.t == VT::Hash && inv.hash() && inv.hashKind == "SetHash" &&
        (m == "set" || m == "unset")) {
        for (auto& a : args)
            for (auto& x : a.flatten()) {
                std::string k = baggyKeyStr(x);
                if (m == "set") { Value b = Value::boolean(true); b.pairKeyM() = baggyKey(x); (*inv.hash())[k] = std::move(b); }
                else inv.hash()->erase(k);
            }
        return Value::nil();
    }
    // the invocant's family — Set weights count as 1)
    if (inv.t == VT::Hash && m == "ACCEPTS" && !args.empty() &&
        (inv.hashKind == "Set" || inv.hashKind == "SetHash" ||
         inv.hashKind == "Bag" || inv.hashKind == "BagHash" ||
         inv.hashKind == "Mix" || inv.hashKind == "MixHash")) {
        static const std::set<std::string> qk = {"Set","SetHash","Bag","BagHash","Mix","MixHash"};
        Value other = args[0];
        if (!(other.t == VT::Hash && other.hash() && qk.count(other.hashKind))) {
            ValueList items = other.flatten();
            bool mixK = inv.hashKind == "Mix" || inv.hashKind == "MixHash";
            bool bagK = inv.hashKind == "Bag" || inv.hashKind == "BagHash";
            other = makeBaggy(items, mixK ? "Mix" : bagK ? "Bag" : "Set", false);
        }
        auto wt = [](const Value& h, const std::string& k) -> double {
            auto it = h.hash()->find(k);
            if (it == h.hash()->end()) return 0.0;
            return it->second.t == VT::Bool ? (it->second.b ? 1.0 : 0.0) : it->second.toNum();
        };
        bool eq = true;
        if (!inv.hash() || !other.hash()) eq = (!inv.hash() || inv.hash()->empty()) && (!other.hash() || other.hash()->empty());
        else {
            for (auto& kv : *inv.hash())   if (wt(inv, kv.first) != wt(other, kv.first)) { eq = false; break; }
            if (eq) for (auto& kv : *other.hash()) if (wt(inv, kv.first) != wt(other, kv.first)) { eq = false; break; }
        }
        return Value::boolean(eq);
    }
    // BagHash.remove(keys): takes ONE off each named key's count (removing the
    // key when it reaches zero) and answers Nil. Note this is NOT `.delete`,
    // which drops the key outright — Rakudo defines `remove` on BagHash alone,
    // not on SetHash or MixHash.
    if (inv.t == VT::Hash && inv.hash() && m == "remove" && inv.hashKind == "BagHash") {
        for (auto& a : args)
            for (auto& k : (a.t == VT::Array || a.t == VT::Range) ? a.flatten() : ValueList{a}) {
                auto it = inv.hash()->find(baggyKeyStr(k));
                if (it == inv.hash()->end()) continue;
                double w = it->second.toNum() - 1;
                if (w > 0) it->second = Value::integer((long long)w);
                else inv.hash()->erase(it);
            }
        return Value::nil();
    }
    // quanthash STORE: replace contents — (items) or the (keys, values) candidate
    if (inv.t == VT::Hash && m == "STORE" && !args.empty() &&
        (inv.hashKind == "Set" || inv.hashKind == "SetHash" ||
         inv.hashKind == "Bag" || inv.hashKind == "BagHash" ||
         inv.hashKind == "Mix" || inv.hashKind == "MixHash")) {
        Value nv;
        if (args.size() == 2 && (args[0].t == VT::Array || args[0].t == VT::Range) &&
            (args[1].t == VT::Array || args[1].t == VT::Range)) {
            ValueList ks = args[0].flatten(), vs = args[1].flatten(), pairs;
            for (size_t i = 0; i < ks.size(); i++)
                pairs.push_back(Value::pair(ks[i].toStr(), i < vs.size() ? vs[i] : Value::any()));
            nv = makeBaggy(pairs, inv.hashKind, false);
        } else {
            ValueList items;
            for (auto& a : args) {
                if (a.t == VT::Array || a.t == VT::Range) for (auto& x : a.flatten()) items.push_back(x);
                else items.push_back(a);
            }
            nv = makeBaggy(items, inv.hashKind, false);
        }
        if (inv.hash() && nv.hash()) { *inv.hash() = *nv.hash(); return inv; }
        return nv;
    }
    // %h.Capture — a Capture whose named part is the hash's pairs
    if (inv.t == VT::Hash && m == "Capture" &&
        (inv.hashKind.empty() || inv.hashKind == "Map" ||
         inv.hashKind == "Set" || inv.hashKind == "SetHash" ||
         inv.hashKind == "Bag" || inv.hashKind == "BagHash" ||
         inv.hashKind == "Mix" || inv.hashKind == "MixHash")) {
        Value c = Value::array(); c.hashKind = "Capture"; c.itemized = true;
        if (inv.hash()) for (auto& kv : *inv.hash()) c.arr()->push_back(Value::pair(kv.first, kv.second));
        return c;
    }
    // $obj.Capture — the object's public attributes as NAMED arguments, each
    // read through its accessor (a method may override the attribute's value)
    if (inv.t == VT::Object && inv.obj() && inv.obj()->cls && m == "Capture") {
        Value c = Value::array(); c.hashKind = "Capture"; c.itemized = true;
        std::set<std::string> seen;
        for (ClassInfo* ci = inv.obj()->cls.get(); ci; ci = ci->parent.get())
            for (auto& at : ci->attrs) {
                if (!at.pub || !seen.insert(at.name).second) continue;
                Value v;
                try { v = methodCall(inv, at.name, {}); }
                catch (RakuError&) {
                    auto it = inv.obj()->attrs.find(at.name);
                    if (it == inv.obj()->attrs.end()) continue;
                    v = it->second;
                }
                c.arr()->push_back(Value::pair(at.name, v));
            }
        std::sort(c.arr()->begin(), c.arr()->end(),
                  [](const Value& a, const Value& b) { return a.s < b.s; });
        return c;
    }
    // @a.Capture — elements become positional arguments, Pairs become named ones
    // (so the nameds sort to the back of the rendering, as in `\(2, :a(1))`)
    if (inv.t == VT::Array && inv.hashKind.empty() && m == "Capture") {
        ValueList items = toList(inv);
        Value c = Value::array(); c.hashKind = "Capture"; c.itemized = true;
        for (auto& e : items) if (e.t != VT::Pair) c.arr()->push_back(e);
        for (auto& e : items) if (e.t == VT::Pair) c.arr()->push_back(e);
        return c;
    }

    // list / array / range
    if (inv.t == VT::Range && m == "ACCEPTS")
        return Value::boolean(applyArith("~~", args.empty() ? Value::any() : args[0], inv).truthy());
    // `@a.ACCEPTS($x)` — a list matches iff $x is a same-length list, element-wise
    if ((inv.t == VT::Array) && m == "ACCEPTS") {
        Value x = args.empty() ? Value::any() : args[0];
        if (x.t != VT::Array && x.t != VT::Range) return Value::boolean(false);
        ValueList self = toList(inv), other = toList(x);
        if (self.size() != other.size()) return Value::boolean(false);
        for (size_t i = 0; i < self.size(); i++)
            if (!applyArith("~~", other[i], self[i]).truthy()) return Value::boolean(false);
        return Value::boolean(true);
    }
    // `.int-bounds($lo, $hi)` — the TWO-ARGUMENT form stores the bounds into
    // its arguments and answers a Bool, so `(1..Inf).int-bounds(my $lo, my $hi)`
    // is False with both left undefined where the no-argument form fails. It
    // sat inside the finite arm, so an infinite range never reached it and the
    // Failure blew the caller up instead of telling it there are none (RG-26).
    if (inv.t == VT::Range && m == "int-bounds" && args.size() >= 2 &&
        rwArgs && rwArgs->size() >= 2) {
        Value b = methodCall(inv, "int-bounds", ValueList{});
        const bool have = b.t == VT::Array && b.arr() && b.arr()->size() == 2;
        if (have)
            for (int k = 0; k < 2; k++) {
                Value* lv = nullptr;
                try { lv = lvalue((*rwArgs)[(size_t)k].get()); } catch (RakuError&) {}
                if (lv) *lv = (*b.arr())[(size_t)k];
            }
        return Value::boolean(have);
    }
    // A Range that starts at -Inf, Inf or NaN — see degenRange in Value.h.
    // Ahead of every other Range arm, because the integer fields it would
    // otherwise read hold the saturated int64 limits and answer nonsense:
    // `(Inf..Inf)[^5]` counted up from the int64 maximum and wrapped, and
    // `(-Inf..0).map` walked up from the minimum instead of standing still.
    if (inv.t == VT::Range) {
        DegenRange d = degenRange(inv);
        if (d == DegenRange::Empty) {
            if (m == "elems" || m == "end") return Value::integer(m == "end" ? -1 : 0);
            if (m == "is-lazy" || m == "infinite") return Value::boolean(false);
            if (m == "AT-POS" || m == "head" || m == "tail") {
                if (m == "AT-POS" || args.empty()) return Value::nil();
                Value o = Value::array(); o.isList = true; return o;
            }
            if (m == "list" || m == "List" || m == "Seq" || m == "cache" || m == "flat" ||
                m == "eager" || m == "Array" || m == "reverse" || m == "sort")
                { Value o = Value::array(); o.isList = true; return o; }
        }
        else if (d == DegenRange::Repeat) {
            const Value& rep = rangeEnds(inv)->from;
            auto take = [&](long long k) {
                Value o = Value::array(); o.isList = true;
                o.arr()->assign((size_t)std::max(0LL, k), rep);
                return o;
            };
            if (m == "is-lazy" || m == "infinite") return Value::boolean(true);
            if (m == "min") return rep;
            if (m == "head") return args.empty() ? rep : take(args[0].toInt());
            if (m == "AT-POS") return rep;
            if (m == "elems")
                return ioFailure("X::Cannot::Lazy", {{"action", Value::str(".elems")}},
                                 "Cannot .elems a lazy list");
            if (m == "list" || m == "List" || m == "Seq" || m == "cache" || m == "flat")
                return take(10000);      // the bounded prefix every endless range hands out
            if (m == "map" || m == "grep" || m == "first")
                return methodCall(take(10000), m, args, rwArgs);
        }
    }
    // A Range is immutable: the six resizing methods refuse it with
    // X::Immutable naming the method and the typename, the way a List does.
    // They used to reach the generic method lookup and come back as
    // X::Method::NotFound, which says the wrong thing and carries neither
    // attribute (S02-types/range.t asserts both).
    if (inv.t == VT::Range &&
        (m == "push" || m == "pop" || m == "shift" || m == "unshift" ||
         m == "append" || m == "prepend"))
        throwTyped("X::Immutable", {{"method", m}, {"typename", "Range"}},
                   "Cannot call '" + m + "' on an immutable 'Range'");
    // `.minmax` is `int-bounds` for an Int range — the first and last integer
    // it iterates, so `(^10).minmax` is (0, 9) — and the RAW endpoints for any
    // other, so `(3.5..4.5).minmax` is (3.5, 4.5) and `("a".."z")` is ("a",
    // "z"). It used to route to the List arm, which read the integer fields and
    // answered codepoints for a string range and the int64 limits for
    // `-Inf..Inf`. A non-Int range with an excluded end has no answer at all:
    // Rakudo fails it, because neither endpoint is an element.
    if (inv.t == VT::Range && m == "minmax" && args.empty()) {
        const bool isInt = methodCall(inv, "is-int", ValueList{}).truthy();
        if (isInt) return methodCall(inv, "int-bounds", ValueList{});
        if (inv.rExFrom() || inv.rExTo())
            return ioFailure("X::AdHoc", {},
                             "Cannot return minmax on Range with excluded ends");
        Value o = Value::array({methodCall(inv, "min", ValueList{}),
                                methodCall(inv, "max", ValueList{})});
        o.isList = true; return o;
    }
    // `.min`/`.max` with `:k`, `:kv`, `:p`, `:v` — a Range answers these as
    // SCALARS, not as the list of winning positions a List gives, because its
    // extremes are its endpoints and there is exactly one of each. The key of
    // `min` is always 0; the key of `max` is `.end`, the index of the last
    // ELEMENT — so `(2^..^6).max(:kv)` is (2, 6), pairing the last index with
    // the raw endpoint, a pair whose halves never meet in the list. An endless
    // range keys on Inf and an empty one on -1. The VALUE is always the raw
    // endpoint, exclusions and all: `(1.5..3).max(:kv)` is (1, 3) though the
    // element at index 1 is 2.5. `:by` is a comparator, not an adverb, and
    // still goes to the list. (S02-types/range.t, 26 assertions.)
    if (inv.t == VT::Range && (m == "min" || m == "max")) {
        char want = 0;
        bool by = false;
        for (auto& a : args) {
            if (a.t == VT::Code) by = true;
            if (a.t != VT::Pair || !a.pairVal()) continue;
            if (a.s == "by") { by = true; continue; }
            if (!a.pairVal()->truthy()) continue;        // `:!k` asks for the plain answer
            if (a.s == "k" || a.s == "v" || a.s == "kv" || a.s == "p")
                want = a.s == "kv" ? 'm' : a.s[0];
        }
        // `:by` compares by something other than the natural order, so the
        // endpoints stop being the extremes and only the elements can answer.
        if (by && !isEndlessRange(inv) && inv.rTo() < 9000000000000000000LL &&
            inv.rFrom() > -9000000000000000000LL) {
            Value lst = Value::array(); lst.isList = true; *lst.arr() = toList(inv);
            return methodCall(lst, m, args, rwArgs);
        }
        if (want && !by) {
            Value raw = methodCall(inv, m, ValueList{});        // the endpoint itself
            if (want == 'v') return raw;
            Value key;
            if (m == "min") key = Value::integer(0);
            else if (isEndlessRange(inv) || inv.rTo() >= 9000000000000000000LL)
                key = Value::number(INFINITY);
            else {
                Value n = methodCall(inv, "elems", ValueList{});
                key = applyArith("-", n, Value::integer(1));
            }
            if (want == 'k') return key;
            if (want == 'p') {
                Value p = Value::pair(key.toStr(), raw);
                p.pairKeyM() = std::make_shared<Value>(key);   // the key is an Int, not its text
                return p;
            }
            Value o = Value::array({key, raw}); o.isList = true; return o;
        }
    }
    if (inv.t == VT::Range && m == "is-lazy")
        return Value::boolean(inv.b || inv.rTo() >= 9000000000000000000LL); // `lazy 1..3` marks .b
    // finite-Range scalar accessors: endpoints (min/max ignore exclusivity), the
    // exclusion flags, and the integer-inclusive int-bounds.
    if (inv.t == VT::Range && inv.rTo() < 9000000000000000000LL &&
        inv.rFrom() > -9000000000000000000LL) {
        if (m == "excludes-min") return Value::boolean(inv.rExFrom());
        if (m == "excludes-max") return Value::boolean(inv.rExTo());
        if (m == "infinite")     return Value::boolean(false);
        // fractional ranges aren't integer-bounded, and neither is a Str range
        if (m == "is-int")       return Value::boolean(!inv.rNum() && inv.ofType() != "Str");
        // .min/.max/.bounds answer the endpoint OBJECTS when the range kept them
        // (`(1/2 .. 1/3).min` is a Rat, not the Int it iterates from)
        const RangeEnds* re = rangeEnds(inv);
        if (m == "min" && re) return re->from;
        if (m == "max" && re) return re->to;
        if (m == "min")
            return inv.ofType() == "Str" ? Value::str(cpToU8((uint32_t)inv.rFrom()))
                 : inv.rNum() ? Value::number(inv.n)
                 : inv.rFrom() <= -9000000000000000000LL ? Value::number(-INFINITY)
                 : Value::integer(inv.rFrom());
        if (m == "max")
            return inv.ofType() == "Str" ? Value::str(cpToU8((uint32_t)inv.rTo()))
                 : inv.rNum() ? Value::number(inv.im())
                 : inv.rTo() >= 9000000000000000000LL ? Value::number(INFINITY)
                 : Value::integer(inv.rTo());
        if (m == "bounds") {
            Value o = re ? Value::array({re->from, re->to})
                   : inv.ofType() == "Str" ? Value::array({Value::str(cpToU8((uint32_t)inv.rFrom())),
                                                         Value::str(cpToU8((uint32_t)inv.rTo()))})
                   : inv.rNum() ? Value::array({Value::number(inv.n), Value::number(inv.im())})
                              : Value::array({Value::integer(inv.rFrom()), Value::integer(inv.rTo())});
            o.isList = true; return o;
        }
        if (m == "int-bounds") {
            // There are no integer bounds when an endpoint is not a whole
            // number to begin with: a fractional START (the END may be
            // fractional — `(0..5.5)` is (0, 5)), a string range, or an
            // infinite or NaN endpoint. Rakudo fails all of those, and
            // S02-types/range.t walks fifteen Inf/NaN combinations expecting it.
            const RangeEnds* re = rangeEnds(inv);
            auto nonFinite = [](const Value& v) {
                return v.t == VT::Num && !std::isfinite(v.n);
            };
            bool fracStart = inv.rNum() && inv.n != std::floor(inv.n);
            if (inv.ofType() == "Str" || fracStart ||
                (re && (nonFinite(re->from) || nonFinite(re->to))))
                return ioFailure("X::AdHoc", {}, "Cannot determine integer bounds");
            // An excluded end only bites when a step LANDS on it, so the
            // adjustment is for a whole-number endpoint alone: `(1..^5.0)` is
            // (1, 4) but `(0..^5.5)` is (0, 5) — 5 is inside 5.5 whether or not
            // the end is excluded. Subtracting unconditionally lost that last
            // integer for every fractional end.
            const bool wholeTop = !inv.rNum() || inv.im() == std::floor(inv.im());
            const bool wholeBot = !inv.rNum() || inv.n == std::floor(inv.n);
            Value o = Value::array({Value::integer(inv.rFrom() + (inv.rExFrom() && wholeBot ? 1 : 0)),
                                    Value::integer(inv.rTo() - (inv.rExTo() && wholeTop ? 1 : 0))});
            o.isList = true; return o;
        }
    }
    // an infinite range (…..Inf) must not materialise: only lazy views are defined
    // `$range.in-range($v)` — True when the value is inside, and otherwise
    // THROWS X::OutOfRange (Rakudo throws here; it does not hand back a soft
    // Failure, so even `.defined` on the result explodes)
    if (inv.t == VT::Range && m == "in-range" && !args.empty()) {
        if (applyArith("~~", args[0], inv).truthy()) return Value::boolean(true);
        std::string what = "Value";
        for (auto& a : args) if (a.t == VT::Pair && a.pairVal()) what = a.pairVal()->toStr();
        throwTypedV("X::OutOfRange",
            {{"got", args[0]}, {"what", Value::str(what)}, {"range", inv}},
            what + " out of range. Is: " + args[0].gist() + ", should be in " + inv.gist());
    }
    // An endless Range is summed by the limit of its arithmetic series, not by
    // adding up elements there is no end of: `(1..Inf).sum` is Inf and
    // `(-Inf..0).sum` is -Inf (both Rakudo's answers). `.reduce` asks the same
    // question of an arbitrary operator, and endlessReduce answers only the
    // ones that can be answered without the elements.
    // A Range of integers sums by Gauss — count × (lo + hi) / 2 — instead of
    // walking it. `(1..10**100).sum` (S03-operators/range-int.t) has no other
    // answer: its endpoint does not fit a long long, so the range carries the
    // same sentinel an endless one does and flatten() would hand back a prefix.
    if (inv.t == VT::Range && m == "sum" && !inv.rNum() && inv.ofType() != "Str" &&
        !isEndlessRange(inv)) {
        const RangeEnds* re = rangeEnds(inv);
        Value lo = re ? re->from : Value::integer(inv.rFrom());
        Value hi = inv.big() ? Value::bigint(*inv.big())          // an endpoint past long long
                 : re     ? re->to
                          : Value::integer(inv.rTo());
        if (lo.t == VT::Int && hi.t == VT::Int) {
            if (inv.rExFrom()) lo = applyArith("+", lo, Value::integer(1));
            if (inv.rExTo())   hi = applyArith("-", hi, Value::integer(1));
            Value count = applyArith("+", applyArith("-", hi, lo), Value::integer(1));
            if (applyArith("<", count, Value::integer(1)).truthy()) return Value::integer(0);
            return applyArith("div", applyArith("*", count, applyArith("+", lo, hi)),
                              Value::integer(2));
        }
    }
    if (inv.t == VT::Range && isEndlessRange(inv)) {
        if (m == "sum") return endlessRangeSum(inv);
        if (m == "reduce" && !args.empty() && args[0].t == VT::Code) {
            std::string n = args[0].code() ? args[0].code()->name : std::string(), op;
            if (n.rfind("infix:<", 0) == 0 && n.back() == '>') op = n.substr(7, n.size() - 8);
            Value r; endlessReduce(op, inv, r); return r; // throws when there is no answer
        }
    }
    // …but a range whose top is a BIGINT only LOOKS endless: `i` saturated at the
    // same sentinel. Answer the three questions that are about the endpoint from
    // the carried objects before falling into the endless arm. (Iterating such a
    // range is still refused there, which is the right answer for 10**42 elements.)
    // `*..1` — endless BELOW, bounded above. Neither the finite arm (which wants
    // both ends in range) nor the endless arm (which keys on the TOP) claimed it,
    // so `.min` answered the raw LLONG_MIN and `.max` a number off by the
    // exclusive adjustment.
    if (inv.t == VT::Range && inv.rFrom() <= -9000000000000000000LL &&
        inv.rTo() < 9000000000000000000LL) {
        if (m == "min")          return Value::number(-INFINITY);
        if (m == "max")          return Value::integer(inv.rTo());
        if (m == "excludes-min") return Value::boolean(inv.rExFrom());
        if (m == "excludes-max") return Value::boolean(inv.rExTo());
        if (m == "infinite" || m == "is-lazy") return Value::boolean(true);
        if (m == "bounds") { Value o = Value::array({Value::number(-INFINITY), Value::integer(inv.rTo())});
                             o.isList = true; return o; }
        if (m == "elems")
            return ioFailure("X::Cannot::Lazy", {{"action", Value::str(".elems")}},
                             "Cannot .elems a lazy list");
        if (m == "list" || m == "List" || m == "Seq" || m == "eager" ||
            m == "Array" || m == "reverse" || m == "sort" || m == "join" || m == "iterator")
            throw RakuError{Value::typeObj("X::Cannot::Lazy"), "Cannot ." + std::string(m) + " a lazy list"};
    }
    if (inv.t == VT::Range && inv.rTo() >= 9000000000000000000LL) {
        if (const RangeEnds* bre = rangeEnds(inv)) {
            if (bre->to.t == VT::Int) {
                if (m == "max") return bre->to;
                if (m == "is-int") return Value::boolean(bre->from.t == VT::Int);
                if (m == "infinite") return Value::boolean(false);   // 1..2**70 only LOOKS endless
                if (m == "bounds") { Value o = Value::array({bre->from, bre->to}); o.isList = true; return o; }
                if (m == "elems" || m == "Numeric" || m == "Int")
                    return applyArith("+", applyArith("-", bre->to, bre->from),
                                      Value::integer(1 - (inv.rExFrom() ? 1 : 0) - (inv.rExTo() ? 1 : 0)));
            }
        }
    }
    // `is-int` on a range with no finite end. Its top (or bottom) is `*`, `Inf`
    // or NaN, none of which is an Int object, so the answer is always False —
    // but only the finite arm above defined the method at all, so every such
    // range answered X::Method::NotFound and took the whole expression with it.
    // (The bigint arm just above claims the ranges that only look endless.)
    if (inv.t == VT::Range && m == "is-int" &&
        (inv.rTo() >= 9000000000000000000LL || inv.rFrom() <= -9000000000000000000LL))
        return Value::boolean(false);
    // …and `int-bounds` on one has no answer: there is no last integer. Only
    // the finite arm defined it, so an infinite range answered
    // X::Method::NotFound where Rakudo hands back a Failure.
    if (inv.t == VT::Range && m == "int-bounds" &&
        (inv.rTo() >= 9000000000000000000LL || inv.rFrom() <= -9000000000000000000LL))
        return ioFailure("X::AdHoc", {}, "Cannot determine integer bounds");
    // An endless range whose LOW end is FRACTIONAL steps by one FROM that
    // fraction — `(1.5..*)` is 1.5, 2.5, 3.5 — where the integer arm below
    // walks the whole numbers around it. Same shape as the string arm that
    // follows, and for the same reason: the integer fields cannot say what the
    // first element is. (S07-iterators/range-iterator.t pulls fourteen of
    // these.)
    if (inv.t == VT::Range && inv.rTo() >= 9000000000000000000LL && inv.rNum() &&
        rangeEnds(inv)) {
        Value first = rangeEnds(inv)->from;
        if (inv.rExFrom()) first = applyArith("+", first, Value::integer(1));
        auto take = [&](long long k) {
            Value o = Value::array(); o.isList = true;
            Value cur = first;
            for (long long i = 0; i < k; i++) { o.arr()->push_back(cur); cur = applyArith("+", cur, Value::integer(1)); }
            return o;
        };
        if (m == "is-lazy" || m == "infinite") return Value::boolean(true);
        if (m == "min")  return first;
        if (m == "max")  return Value::number(INFINITY);
        if (m == "head" && args.empty()) return first;
        if (m == "head") return take(std::max(0LL, args[0].toInt()));
        if (m == "AT-POS" && !args.empty()) {
            long long i = args[0].toInt();
            if (i < 0) return Value::any();
            return applyArith("+", first, Value::integer(i));
        }
        if (m == "list" || m == "List" || m == "Seq" || m == "cache" || m == "lazy" || m == "flat")
            return take(10000);          // the same bounded prefix the Int arm hands out
    }
    // An endless range whose LOW end is a STRING climbs by `succ`, not by
    // codepoint: `('a'..*)[^5]` is a, b, c, d, e. Reading the integer field gave
    // the codepoints 97..101 instead.
    if (inv.t == VT::Range && inv.rTo() >= 9000000000000000000LL && rangeEnds(inv) &&
        rangeEnds(inv)->from.t == VT::Str) {
        std::string first = rangeEnds(inv)->from.s.str();
        if (inv.rExFrom()) first = strSucc(first);
        auto take = [&](long long n) {
            Value o = Value::array(); o.isList = true;
            std::string cur = first;
            for (long long i = 0; i < n; i++) { o.arr()->push_back(Value::str(cur)); cur = strSucc(cur); }
            return o;
        };
        if (m == "is-lazy" || m == "infinite") return Value::boolean(true);
        if (m == "min")  return Value::str(rangeEnds(inv)->from.s.str());
        if (m == "max")  return Value::number(INFINITY);
        if (m == "head" && args.empty()) return Value::str(first);
        if (m == "head") return take(std::max(0LL, args[0].toInt()));
        if (m == "AT-POS" && !args.empty()) {
            long long i = args[0].toInt();
            if (i < 0) return Value::any();
            std::string cur = first;
            for (long long k = 0; k < i; k++) cur = strSucc(cur);
            return Value::str(cur);
        }
        if (m == "list" || m == "List" || m == "Seq" || m == "cache" || m == "lazy" || m == "flat")
            return take(10000);          // the same bounded prefix the Int arm hands out
    }
    if (inv.t == VT::Range && inv.rTo() >= 9000000000000000000LL) {
        long long lo = inv.rFrom() + (inv.rExFrom() ? 1 : 0);
        if (m == "is-lazy" || m == "infinite") return Value::boolean(true);
        if (m == "head" && args.empty()) return Value::integer(lo); // scalar first element
        if (m == "head") { long long n = std::max(0LL, args[0].toInt());
            Value o = Value::array(); o.isList = true; for (long long i = 0; i < n; i++) o.arr()->push_back(Value::integer(lo + i)); return o; }
        if (m == "skip") { long long n = args.empty() ? 1 : std::max(0LL, args[0].toInt()); return Value::range(lo + n, inv.rTo(), false, inv.rExTo()); }
        // `.elems` on an ENDLESS range is X::Cannot::Lazy, not Inf — and it is a
        // SOFT failure, so `throws-like $range.elems, …` still gets to see it
        // rather than being blown up while its arguments are built.
        if (m == "elems")
            return ioFailure("X::Cannot::Lazy", {{"action", Value::str(".elems")}},
                             "Cannot .elems a lazy list");
        if (m == "min") return inv.rFrom() <= -9000000000000000000LL
            ? Value::number(-INFINITY) : Value::integer(inv.rFrom());
        if (m == "max") return Value::number(INFINITY);                 // `1..*` .max is Inf, not an error
        if (m == "excludes-min") return Value::boolean(inv.rExFrom());
        if (m == "excludes-max") return Value::boolean(inv.rExTo());
        if (m == "bounds") { Value o = Value::array({Value::integer(inv.rFrom()), Value::number(INFINITY)}); o.isList = true; return o; }
        if (m == "list" || m == "List" || m == "Seq" || m == "cache" || m == "lazy" || m == "flat" ||
            m == "map" || m == "grep" || m == "first" || m == "rotor" || m == "batch")
            return (m == "map" || m == "grep" || m == "first") ? methodCall(makeInfArray(lo), m, args, rwArgs) : makeInfArray(lo);
        if (m == "AT-POS" && !args.empty()) return Value::integer(lo + args[0].toInt()); // infRange[i]
        // (`Str` and `gist` are NOT here: an endless range renders as its endpoint
        // form — 1..* and 1..Inf — instead of dying, the same as Rakudo.)
        if (m == "tail" || m == "pop" || m == "reverse" || m == "sort" ||
            m == "Array" || m == "eager" || m == "join")
            throw RakuError{Value::typeObj("X::Cannot::Lazy"), "Cannot " + m + " an infinite range"};
    }
    // `.hyper` / `.race` — the parallel iteration wrappers, run SERIALLY: the
    // stand-in is the same flat list a Seq would give (ordered, which race
    // permits and hyper requires), so downstream .map/.grep/.sum proceed as
    // usual; the :degree/:batch tuning nameds arrive as ignored extras.
    // `.serial` unwraps a HyperSeq — on the plain-list stand-in it is the
    // identity. (Ecosystem and JSON::Fast::Hyper want exactly this surface.)
    // `Iterable.hyper.configuration` — the TYPE OBJECT answers the parallel
    // defaults, which is how a module reads them before hyperizing anything
    // (hyperize's INIT, and Ecosystem/JSON::Fast::Hyper behind it). Serial here,
    // so the numbers are Rakudo's documented defaults rather than a live pool's.
    if ((m == "hyper" || m == "race") && inv.t == VT::Type &&
        (inv.s == "Iterable" || inv.s == "Any" || inv.s == "List" ||
         inv.s == "Array" || inv.s == "Seq" || inv.s == "HyperSeq")) {
        unsigned hc = std::thread::hardware_concurrency();
        Value cfg = Value::makeHash();
        (*cfg.hash())["batch"]  = Value::integer(64);
        (*cfg.hash())["degree"] = Value::integer(hc > 1 ? (long long)hc - 1 : 1);
        cfg.hashKind = "HyperConfiguration";
        Value o = Value::makeHash();
        (*o.hash())["configuration"] = cfg;
        o.hashKind = "HyperSeq";
        return o;
    }
    if (inv.t == VT::Hash && inv.hash() &&
        (inv.hashKind == "HyperSeq" || inv.hashKind == "HyperConfiguration") &&
        (m == "configuration" || m == "batch" || m == "degree")) {
        std::string key = m == "configuration" ? std::string("configuration") : std::string(m);
        auto it = inv.hash()->find(key);
        if (it != inv.hash()->end()) return it->second;
        auto c = inv.hash()->find("configuration");
        if (c != inv.hash()->end() && c->second.hash()) {
            auto k = c->second.hash()->find(key);
            if (k != c->second.hash()->end()) return k->second;
        }
    }
    // `.hyper` here is the identity on a plain list, so `.configuration` has to
    // answer on one too — that is what a hyperized sequence is asked for.
    if (m == "configuration" && (inv.t == VT::Array || inv.t == VT::Range)) {
        unsigned hc = std::thread::hardware_concurrency();
        long long batch = 64, degree = hc > 1 ? (long long)hc - 1 : 1;
        if (inv.t == VT::Array && inv.arr()) { // what `.hyper(:batch, :degree)` asked for
            auto it = hyperCfg_.find((const void*)inv.arr());
            if (it != hyperCfg_.end()) {
                if (it->second.first >= 0)  batch  = it->second.first;
                if (it->second.second >= 0) degree = it->second.second;
            }
        }
        Value cfg = Value::makeHash();
        (*cfg.hash())["batch"]  = Value::integer(batch);
        (*cfg.hash())["degree"] = Value::integer(degree);
        cfg.hashKind = "HyperConfiguration";
        return cfg;
    }
    if ((m == "hyper" || m == "race" || m == "serial") &&
        (inv.t == VT::Array || inv.t == VT::Range || (inv.t == VT::Hash && inv.hash()))) {
        if (inv.t == VT::Range) { Value r = inv; return methodCall(r, "list", {}, nullptr); }
        if (inv.t == VT::Hash) {
            Value o = Value::array(); o.isList = true;
            for (auto& kv : *inv.hash()) o.arr()->push_back(Value::pair(kv.first, kv.second));
            return o;
        }
        Value o = inv; o.isList = true; o.itemized = false;
        // `.hyper(:batch(42), :degree(16))` — the parallel stand-in is serial,
        // but what it was ASKED is what `.configuration` has to answer (hyperize
        // reads it straight back: `@a.&hyperize(42).configuration.batch`). The
        // list gets storage of its own so the answer keys on it alone.
        if (m != "serial" && inv.t == VT::Array && inv.arr()) {
            long long batch = -1, degree = -1;
            for (auto& a : args)
                if (a.t == VT::Pair && a.pairVal()) {
                    if (a.s == "batch") batch = a.pairVal()->toInt();
                    else if (a.s == "degree") degree = a.pairVal()->toInt();
                }
            if (batch >= 0 || degree >= 0) {
                o = Value::array(); o.isList = true;
                *o.arr() = *inv.arr();
                hyperCfg_[(const void*)o.arr()] = {batch, degree};
            }
        }
        return o;
    }
    // `.all`/`.any`/`.one`/`.none` on a single (non-container) value → a one-element
    // junction (Rakudo: `5.all` === `all(5)`); containers are handled just below.
    if ((m == "all" || m == "any" || m == "none" || m == "one") &&
        inv.t != VT::Array && inv.t != VT::Range && inv.t != VT::Hash) {
        Value j = Value::array(); j.enumName = m;
        j.setArr(makePayload<ValueList>(ValueList{inv}));
        return j;
    }
    // Answers that need only the SIZE (or one element) of an Array, taken BEFORE the
    // copy below. `toList` returns by value and for an Array that is the whole
    // vector, so the generic segment copied every element — allocating and
    // destroying 40,000 Values to answer `.elems` — before it had even looked at
    // which method was called. That made `@a.elems` and `@a[$i]` inside a loop
    // quadratic in the array's length. Deliberately narrow: the copy also serves as
    // a snapshot for the arms that mutate the invocant (`reverse` sorts `items` in
    // place, `push`/`splice` write through `inv.arr`), so only arms that just READ
    // may be hoisted past it.
    if (inv.t == VT::Array && inv.arr()) {
        const ValueList& live = *inv.arr();
        if (m == "elems") return Value::integer((long long)live.size());
        if (m == "end")   return Value::integer((long long)live.size() - 1);
        // (AT-POS/EXISTS-POS answered in methodCallPart3 before this runs)
    }
    // A MATCH is Positional over its captures, so the list methods work on it:
    // `$0.flatmap({…})` / `$m.map(…)` (URI::Escape unescapes that way). Route it
    // through the list arms as its capture list rather than duplicating them.
    if (inv.t == VT::Match) {
        static const std::set<std::string> listy = {
            "map", "flatmap", "grep", "first", "reduce", "sort", "reverse",
            "flat", // Email::Valid asks a Match for `.flat` before it walks the captures
            "join", "kv", "pairs", "antipairs", "head", "tail", "skip", "rotor",
            "classify", "categorize", "unique", "squish", "sum", "min", "max",
            "combinations", "permutations", "batch", "produce", "tree"};
        if (listy.count(m.s)) {
            Value l = Value::array(); l.isList = true;
            if (inv.arr()) *l.arr() = *inv.arr();
            return methodCall(l, m, std::move(args), rwArgs);
        }
    }
    if (inv.t == VT::Array || inv.t == VT::Range || inv.t == VT::Hash) {
        // The snapshot below serves the arms that READ the whole list. The
        // through-the-handle mutators never touch it — their arms operate on
        // inv.arr alone — yet they paid the O(n) copy per call, which turned
        // every accumulate loop quadratic: `@a.push($x)` 48k times in cognates'
        // build-db was ~1.15 billion Value copies, the bulk of its 197 s.
        // (The Hash `.push` arm has its own gate; lazy arrays are excluded —
        // their arms materialize through the snapshot machinery.)
        auto throughHandle = [&]() {
            return m == "push" || m == "append" || m == "unshift" ||
                   m == "prepend" || m == "pop" || m == "shift" || m == "splice";
        };
        ValueList items;
        // Hash push/append also skip the snapshot: their arm reads only args and
        // inv.hash, and the per-call pair materialization made `%h.push` in an
        // accumulate loop quadratic (same disease the Array gate cured).
        bool hashPush = inv.t == VT::Hash && inv.hash() && (m == "push" || m == "append");
        // The key/value walkers likewise read *inv.hash directly and never look
        // at `items` — for them the snapshot materialized a Pair per entry only
        // to be discarded, the dominant cost of `%h.values` on a large hash.
        bool hashDirect = inv.t == VT::Hash && inv.hash() &&
            (m == "values" || m == "keys" || m == "kv" || m == "pairs" || m == "antipairs");
        if (!(inv.t == VT::Array && inv.arr() && !inv.ext() && throughHandle()) && !hashPush && !hashDirect)
            items = toList(inv);
        // .collate — UCA-ordered sort (the coll infix already implements DUCET;
        // this just wires the method Rakudo exposes on lists)
        if (m == "collate") {
            ValueList sorted = items;
            std::stable_sort(sorted.begin(), sorted.end(), [&](const Value& a, const Value& b) {
                return applyArith("coll", a, b).i < 0;
            });
            // a Seq, as `.sort` answers — Roast collate.t reads `.raku` of it
            Value outv = Value::array(); outv.isList = true; outv.s = "Seq";
            *outv.arr() = std::move(sorted);
            return outv;
        }
        // junction methods: @a.any / .all / .none / .one — a tagged-Array junction
        if (m == "any" || m == "all" || m == "none" || m == "one") {
            Value j = Value::array(); j.enumName = m;
            j.setArr(makePayload<ValueList>(items));
            return j;
        }
        if (m == "Supply") { Value s = Value::makeHash(); s.hashKind = "Supply"; Value v = Value::array(); *v.arr() = items; (*s.hash())["values"] = v; return s; }
        if (m == "chrs") { std::string r; for (auto& x : items) r += cpToUtf8((uint32_t)x.toInt()); return Value::str(r); } // list of codepoints -> Str
        if (m == "of") return Value::typeObj("Mu"); // element type of an untyped Array/List
        // the positional protocol, spelled out — a Range answers these as the
        // list it stands for, and an Array/List does too
        if (m == "AT-POS" && !args.empty()) {
            long long i = args[0].toInt(), n = (long long)items.size();
            if (i < 0) i += n;
            return (i >= 0 && i < n) ? items[(size_t)i] : Value::any();
        }
        if (m == "EXISTS-POS" && !args.empty()) {
            long long i = args[0].toInt(), n = (long long)items.size();
            if (i < 0) i += n;
            return Value::boolean(i >= 0 && i < n);
        }
        // `.slice(@indices)` — the elements at those positions, as a Seq
        if (m == "slice") {
            Value o = Value::array(); o.isList = true; o.s = "Seq";
            long long n = (long long)items.size();
            for (auto& a : args) {
                if (a.t == VT::Pair) continue;
                for (auto& iv : (a.t == VT::Array || a.t == VT::Range) ? toList(a) : ValueList{a}) {
                    long long i = iv.toInt();
                    if (i < 0) i += n;
                    if (i >= 0 && i < n) o.arr()->push_back(items[(size_t)i]);
                }
            }
            return o;
        }
        if (m == "elems") return Value::integer((long long)items.size());
        if (m == "end") return Value::integer((long long)items.size() - 1);
        // `.Array` DECONTAINERIZES. A hash/scalar value sits in a container, so
        // an Array read out of one is itemized; returning it unchanged meant
        // `my @a = $v.Array` bound it as ONE element while `.Array.elems` said 3.
        // `@($v)` was already right, which is what made the two disagree.
        if (m == "Array") {
            if (inv.t != VT::Array) return Value::array(items);
            // `.Array` on a shaped array is its LEAVES as a plain Array — the
            // shape is what it drops (Rakudo: `[1, 2, 3, 4, 5, 6]`).
            if (isMultiDimShaped(inv)) return Value::array(shapedLeaves(inv));
            Value r = inv; r.itemized = false; r.isList = false; return r;
        }
        if (m == "values") {
            Value out = Value::array();
            if (inv.t == VT::Hash) { for (auto& kv : *inv.hash()) out.arr()->push_back(kv.second); }
            else out.setArr(makePayload<ValueList>(items));
            out.isList = true; return out;
        }
        // `.pairup` reads the list PAIRWISE — but a Pair element already is one,
        // so it stands alone and does not consume its neighbour.
        if (m == "pairup") {
            Value out = Value::array(); out.isList = true; out.s = "Seq";
            for (size_t i = 0; i < items.size(); i++) {
                if (items[i].t == VT::Pair) { out.arr()->push_back(items[i]); continue; }
                // A non-itemized Map contributes its OWN pairs rather than
                // standing in as one key: `({a => 1}, 2, 3).pairup` is
                // `(:a(1), 2 => 3)`. An itemized `$(…)` stays a key
                // (Nil-Any sheet NA-27).
                if (items[i].t == VT::Hash && items[i].hash() && !items[i].itemized &&
                    items[i].hashKind.empty()) {
                    for (auto& kv : *items[i].hash())
                        out.arr()->push_back(Value::pair(kv.first, kv.second));
                    continue;
                }
                if (i + 1 >= items.size())
                    throw RakuError{Value::typeObj("X::Pairup::OddNumber"),
                                    "Odd number of elements found for .pairup()"};
                Value p = Value::pair(items[i].toStr(), items[i + 1]);
                // a non-Str key keeps its own value (`1 => 2`, not `"1" => 2`)
                if (items[i].t != VT::Str) p.pairKeyM() = std::make_shared<Value>(items[i]);
                out.arr()->push_back(std::move(p));
                i++;
            }
            return out;
        }
        if (m == "flat") {
            // deep-flatten NON-itemized sublists ((((0,1),2),3).flat is 0,1,2,3);
            // itemized Arrays ([..] / .item) stay whole elements
            // An element of a LIST flattens if it is a non-itemized Iterable —
            // `(6, @a).flat` and `(6, [7,8]).flat` both spread. An element of an
            // ARRAY does not: array assignment itemises each element, so
            // `[[1,2],[3]].flat` stays two elements. `$@a` / `.item` opt out
            // either way.
            // `:hammer` (6.e) hammers the containers flat: itemisation stops
            // mattering, so `[[1,2],[3]].flat(:hammer)` is (1,2,3) where plain
            // .flat keeps the two itemised Arrays whole.
            bool hammer = false;
            for (auto& a : args)
                if (a.t == VT::Pair && a.s == "hammer")
                    hammer = !a.pairVal() || a.pairVal()->truthy();
            Value out = Value::array(); out.isList = true; out.s = "Seq";
            bool topOfArray = inv.t == VT::Array && !inv.isList;
            for (auto& x : items) flatOneInto(x, topOfArray, hammer, *out.arr());
            return out;
        }
        // `.eager` on a concrete Array is the identity — it keeps the same
        // container (and its element type: `my int @a` stays array[int]); only a
        // lazy Seq needs forcing (its elements are already materialised in `items`).
        // A SEQ is the exception: eager answers the List it reified to, not
        // another Seq (sheet LA-10).
        if (m == "eager" && inv.t == VT::Array && !inv.ext() && inv.s != "Seq")
            return inv;
        if (m == "list" || m == "cache" || m == "eager" || m == "Seq" || m == "List" || m == "lazy") {
            Value out = Value::list(items);
            if (m == "Seq") out.s = "Seq"; // `.Seq` really is one — `(1,2).Seq.raku` says so
            // `.eager` answers a LIST — `(1..*).list.head(3).eager` is `(1, 2, 3)`,
            // not a Seq (sheet LA-10). `.cache` keeps the invocant's own type.
            if (m == "lazy") out.b = true; // `.lazy` MARKS it: `.is-lazy` says True after
            return out;
        }
        if (m == "reverse") { std::reverse(items.begin(), items.end()); return Value::list(items); }
        if (m == "rotate") {
            // the rotation count binds an Int; an undefined one is a binding
            // failure, not a rotation of zero
            if (!args.empty() && (args[0].t == VT::Type || args[0].t == VT::Any || args[0].t == VT::Nil))
                throw RakuError{Value::typeObj("X::TypeCheck::Binding::Parameter"),
                    "Type check failed in binding to parameter '$n'; expected Int but got " +
                    args[0].typeName() + " (" + args[0].gist() + ")"};
            long n = args.empty() ? 1 : args[0].toInt(); long sz = (long)items.size();
            if (sz) { n = ((n % sz) + sz) % sz; std::rotate(items.begin(), items.begin() + n, items.end()); }
            return Value::list(items); }
        if (m == "permutations") {
            Value out = Value::array(); out.isList = true; out.s = "Seq";
            std::vector<size_t> idx(items.size());
            for (size_t i = 0; i < idx.size(); i++) idx[i] = i;
            // generate in lexicographic order of indices (matches Rakudo's ordering)
            do {
                Value perm = Value::array(); perm.isList = true; // a sublist gists with (…)
                for (size_t i : idx) perm.arr()->push_back(items[i]);
                out.arr()->push_back(perm);
            } while (std::next_permutation(idx.begin(), idx.end()));
            return out;
        }
        if (m == "combinations") {
            Value out = Value::array(); out.isList = true; out.s = "Seq";
            long long lo = 0, hi = (long long)items.size();
            if (!args.empty()) {
                Value k = a0();
                // a Range of SIZES, with both endpoints' exclusivity honoured:
                // `1^..3` is sizes 2 through 3, `0^..0` none at all. The low
                // `^` was ignored, so every such call also produced the sizes
                // below it (roast S32-list/combinations.t).
                if (k.t == VT::Range) { lo = k.rFrom() + (k.rExFrom() ? 1 : 0);
                                        hi = k.rExTo() ? k.rTo() - 1 : k.rTo(); }
                else { lo = hi = k.toInt(); }
            }
            if (hi > (long long)items.size()) hi = (long long)items.size();
            for (long long k = lo; k <= hi; k++) {
                if (k < 0) continue;
                std::vector<bool> mask(items.size(), false);
                for (long long i = 0; i < k; i++) mask[items.size() - 1 - i] = true; // start: choose last k, then permute mask ascending
                std::vector<size_t> sel;
                // enumerate all k-subsets in index-ascending order
                std::vector<long long> c(k);
                for (long long i = 0; i < k; i++) c[i] = i;
                while (k == 0 ? (sel.empty()) : true) {
                    Value combo = Value::array(); combo.isList = true; // each combo is a List
                    for (long long i = 0; i < k; i++) combo.arr()->push_back(items[c[i]]);
                    out.arr()->push_back(combo);
                    if (k == 0) break;
                    long long i = k - 1;
                    while (i >= 0 && c[i] == (long long)items.size() - k + i) i--;
                    if (i < 0) break;
                    c[i]++;
                    for (long long j = i + 1; j < k; j++) c[j] = c[j-1] + 1;
                }
            }
            return out;
        }
        if (m == "join") {
            // the separator is `Str(Cool)`: an undefined one cannot coerce, and
            // stringifying it to "" hid the mistake
            if (!args.empty() && (a0().t == VT::Type || a0().t == VT::Any || a0().t == VT::Nil))
                throw RakuError{Value::typeObj("X::TypeCheck::Binding::Parameter"),
                    "Type check failed in binding to parameter '$separator'; expected Str but got " +
                    a0().typeName() + " (" + a0().gist() + ")"};
            // each element through ITS OWN .Str, so a user `method Str` is honoured —
            // except a Str-ish one, which contributes its VALUE (Str:D candidate)
            const std::string sep = args.empty() ? "" : a0().toStr();
            // A HOLE stringifies as what READING it would give: "" for a plain
            // array, the `is default(v)` value where there is one, and the
            // element type's object for a typed array (sheet LA-14).
            const Value* dflt = inv.t == VT::Array && inv.elemDefault()
                                    ? inv.elemDefault().get() : nullptr;
            std::string out;
            for (size_t k = 0; k < items.size(); k++) {
                if (k) out += sep;
                out += (dflt && items[k].t == VT::Any) ? strInStrContext(*dflt)
                                                       : strInStrContext(items[k]);
            }
            return Value::str(nfcNormalize(std::move(out))); // NFG: compose across the joins
        }
        if (m == "fmt") {
            // An ASSOCIATIVE invocant formats its (key, value) PAIRS: the
            // default format is `%s\t%s`, the default separator a NEWLINE, and
            // a format with a single directive consumes only the KEY (sheet
            // HM-15). A Set/Bag/Mix is one of these, and its value is the
            // weight. The list defaults below — `%s` joined by a space — are a
            // different method on a different type, and a hash borrowing them
            // printed `%h.fmt` as "a b".
            const bool assoc = inv.t == VT::Hash && inv.hash() &&
                (inv.hashKind.empty() || inv.hashKind == "Map" || inv.hashKind == "Stash" ||
                 inv.hashKind.rfind("Set", 0) == 0 || inv.hashKind.rfind("Bag", 0) == 0 ||
                 inv.hashKind.rfind("Mix", 0) == 0);
            std::string fmt = args.empty() ? (assoc ? "%s\t%s" : "%s") : a0().toStr();
            std::string sep = args.size() > 1 ? args[1].toStr() : (assoc ? "\n" : " ");
            std::string out;
            auto countDirectives = [](const std::string& f) {
                size_t n = 0;
                for (size_t i = 0; i + 1 < f.size(); i++)
                    if (f[i] == '%') { if (f[i + 1] == '%') i++; else n++; }
                return n;
            };
            if (assoc) {
                const bool keyOnly = countDirectives(fmt) < 2;
                bool first = true;
                for (auto& kv : *inv.hash()) {
                    if (!first) out += sep;
                    first = false;
                    Value key = kv.second.pairKey() ? *kv.second.pairKey() : Value::str(kv.first);
                    out += keyOnly ? doSprintf(fmt, {key}) : doSprintf(fmt, {key, kv.second});
                }
                return Value::str(out);
            }
            // Each element is formatted ON ITS OWN, so a format wanting two
            // arguments can never be satisfied by a plain element — Rakudo
            // reports the sprintf arity failure as X::AdHoc (sheet LA-12). A
            // Pair (and a Setty/Baggy entry, handled above) supplies two, so
            // the count is only checked where one is supplied.
            {
                size_t directives = 0;
                for (size_t i = 0; i + 1 < fmt.size(); i++)
                    if (fmt[i] == '%') { if (fmt[i + 1] == '%') i++; else directives++; }
                bool allPlain = true;
                for (auto& it : items) if (it.t == VT::Pair) { allPlain = false; break; }
                if (directives > 1 && allPlain && !items.empty())
                    throw RakuError{Value::typeObj("X::AdHoc"),
                        "Your printf-style directives specify " + std::to_string(directives) +
                        " arguments, but 1 argument was supplied to format '" + fmt + "'"};
            }
            for (size_t k = 0; k < items.size(); k++) {
                if (k) out += sep;
                // a Pair element formats as its (key, value) — `@pairs.fmt('%s: %s', ', ')`
                // is how CBOR::Simple prints a map in diagnostic notation
                if (items[k].t == VT::Pair)
                    out += doSprintf(fmt, {Value::str(items[k].s),
                                           items[k].pairVal() ? *items[k].pairVal() : Value::any()});
                // an ITERABLE element is formatted RECURSIVELY with the same
                // format and separator, so a nested list spreads:
                // `(1, (2, 3)).fmt("<%s>", ",")` is "<1>,<2>,<3>" (LA-12)
                else if ((items[k].t == VT::Array && items[k].arr() && !items[k].itemized) ||
                         items[k].t == VT::Range) {
                    ValueList fa{Value::str(fmt), Value::str(sep)};
                    out += methodCall(items[k], "fmt", fa).toStr();
                }
                else out += doSprintf(fmt, {items[k]});
            }
            return Value::str(out);
        }
        if (m == "sum") {
            // Fold through the EXACT tower rather than a double: summing into a
            // double and casting back saturated at int64 (`(2**70, 1).sum` came
            // out as 9223372036854775807) and lost Rat exactness. applyArith also
            // autothreads a junction element, which is what Rakudo does.
            if (items.empty()) return Value::integer(0);
            Value acc = Value::integer(0);
            for (auto& v : items) acc = applyArith("+", acc, v);
            return acc;
        }
        if (m == "enums") { // enum type (a pair-list) -> Map of name => value
            Value h = Value::makeHash();
            h.hashKind = "Map";
            for (auto& v : items) if (v.t == VT::Pair) (*h.hash())[v.s] = v.pairVal() ? *v.pairVal() : Value::any();
            return h;
        }
        if (m == "shape") { // a declared shape (my @a[2;3]) reports its dims; else (*,)
            Value o = Value::array(); o.isList = true;
            if (inv.shape() && !inv.shape()->empty())
                for (long long d : *inv.shape()) o.arr()->push_back(Value::integer(d));
            else o.arr()->push_back(Value::whatever());
            return o;
        }
        if (m == "hyper" || m == "race") { Value o = Value::array(items); o.isList = true; return o; } // parallel -> sequential
        if (m == "is-lazy") return Value::boolean(inv.t == VT::Array && inv.b); // materialised list is not lazy (unless `lazy`-marked)
        // A RIGHT-associative operator folds from the right: `.reduce(&[**])` is
        // 2**(3**4), not (2**3)**4. `&[OP]` callables carry their name, which is
        // the only place the associativity is recorded.
        auto rightAssoc = [](const Value& f) {
            if (f.t != VT::Code || !f.code()) return false;
            const std::string& n = f.code()->name;
            if (n.rfind("infix:<", 0) != 0 || n.size() < 9) return false;
            std::string op = n.substr(7, n.size() - 8);
            return op == "**" || op == "=>";
        };
        if (m == "reduce" && !args.empty() && args[0].t == VT::Code) { // fold with a 2-arg op: (1,2,3).reduce(* + *)
            // over NOTHING an operator answers its identity — `().reduce(&[+])`
            // is 0 — which the [op] metaop already knows how to look up
            if (items.empty()) {
                const std::string& cn = args[0].code() ? args[0].code()->name : std::string();
                if (cn.rfind("infix:<", 0) == 0 && cn.back() == '>') {
                    ValueList none;
                    return applyReduce(cn.substr(7, cn.size() - 8), none);
                }
                // …but a plain BLOCK has no identity to fall back on, so folding
                // nothing with one is an error, not a quiet Any (Nil-Any sheet
                // NA-25).
                throw RakuError{Value::typeObj("X::AdHoc"),
                    "Too few positionals passed; expected 2 arguments but got 0"};
            }
            // A ONE-element list still CALLS the reducer with that single
            // element — an operator answers it, a two-parameter block without a
            // default dies "Too few positionals" (NA-25). Handing the element
            // straight back skipped the call, so the error never happened and a
            // reducer with a side effect never ran.
            if (items.size() == 1) {
                size_t req = 0;
                if (args[0].code()) {
                    if (args[0].code()->params && !args[0].code()->params->empty()) {
                        for (auto& pp : *args[0].code()->params)
                            if (!pp.named && !pp.slurpy && !pp.optional && !pp.defaultVal) req++;
                    } else req = args[0].code()->placeholders.size();
                }
                if (req > 1)
                    throw RakuError{Value::typeObj("X::AdHoc"),
                        "Too few positionals passed; expected " + std::to_string(req) +
                            " arguments but got 1"};
                return callCallable(args[0], ValueList{items[0]});
            }
            // `last` in the folding block ENDS THE FOLD and answers the
            // accumulator built so far — it is a loop from the block's point of
            // view, so the control exception must not escape as "last without
            // loop construct".
            if (rightAssoc(args[0])) {
                Value acc = items.back();
                try {
                    for (size_t k = items.size() - 1; k-- > 0; ) acc = callCallable(args[0], {items[k], acc});
                } catch (LastEx&) {}
                return acc;
            }
            Value acc = items[0];
            try {
                for (size_t k = 1; k < items.size(); k++) acc = callCallable(args[0], {acc, items[k]});
            } catch (LastEx&) {}
            return acc;
        }
        if (m == "produce" && !args.empty() && args[0].t == VT::Code) { // scan: running reductions
            Value out = Value::array(); out.isList = true; out.s = "Seq"; // .produce is a Seq
            if (items.empty()) return out;
            if (rightAssoc(args[0])) {
                // the running folds of the SUFFIXES, reported left to right
                ValueList acc(items.size());
                acc[items.size() - 1] = items.back();
                for (size_t k = items.size() - 1; k-- > 0; )
                    acc[k] = callCallable(args[0], {items[k], acc[k + 1]});
                for (size_t k = items.size(); k-- > 0; ) out.arr()->push_back(acc[k]);
                return out;
            }
            Value acc = items[0]; out.arr()->push_back(acc);
            // `last` ends the scan — and DROPS the most recent running value, because
            // Rakudo produces lazily and so lags one behind what has been computed.
            // Checked against `(2,3,4,5).produce: {last if $^a > 7; $^a+$^b}` -> (2 5),
            // `(1,2,3,4)` with `$^a > 2` -> (1), and `$^a > 0` -> ().
            try {
                for (size_t k = 1; k < items.size(); k++) { acc = callCallable(args[0], {acc, items[k]}); out.arr()->push_back(acc); }
            } catch (LastEx&) { if (!out.arr()->empty()) out.arr()->pop_back(); }
            return out;
        }
        // `%h.classify-list($mapper, *@values)` classifies INTO the invocant and
        // answers it. A list-valued key NESTS — `("1a","1b")` files the value
        // under %h<1a><1b> — which is what separates it from plain `.classify`.
        // `.categorize-list` files under EVERY key the mapper yields instead.
        // …but only for an invocant that is NOT a Hash. A Hash (and a Baggy) has
        // a fuller implementation further down — `:as`, `:into`, the mixed-level
        // check, object-hash keys — and this arm, reached first, silently filed
        // the `as => &code` adverb away as one of the VALUES to classify
        // (roast S32-list/classify-list.t and categorize-list.t, every `&as` case).
        if ((m == "classify-list" || m == "categorize-list") && !args.empty() &&
            inv.t != VT::Hash) {
            bool cat = (m == "categorize-list");
            Value self = inv.t == VT::Hash && inv.hash() ? inv : Value::makeHash();
            Value mapper = args[0];
            ValueList vals;
            for (size_t i = 1; i < args.size(); i++)
                for (auto& x : toList(args[i])) vals.push_back(x);
            auto keyFor = [&](const Value& v) -> Value {
                if (mapper.t == VT::Code) return callCallable(mapper, {v});
                if (mapper.t == VT::Hash && mapper.hash()) {
                    auto it = mapper.hash()->find(v.toStr());
                    return it != mapper.hash()->end() ? it->second : Value::any();
                }
                if (mapper.t == VT::Array && mapper.arr()) {
                    long long i = v.toInt();
                    return (i >= 0 && i < (long long)mapper.arr()->size()) ? (*mapper.arr())[i] : Value::any();
                }
                return v;
            };
            // walk/‌create the nested hashes, then append at the leaf
            auto fileUnder = [&](Value& root, const ValueList& path, const Value& v) {
                Value* cur = &root;
                for (size_t d = 0; d + 1 < path.size(); d++) {
                    Value& slot = (*cur->hash())[path[d].toStr()];
                    if (slot.t != VT::Hash || !slot.hash()) slot = Value::makeHash();
                    cur = &slot;
                }
                Value& leaf = (*cur->hash())[path.back().toStr()];
                if (leaf.t != VT::Array || !leaf.arr()) leaf = Value::array();
                leaf.arr()->push_back(v);
            };
            for (auto& v : vals) {
                Value k = keyFor(v);
                if (k.t == VT::Array && k.arr() && !k.arr()->empty()) {
                    if (cat) for (auto& kk : *k.arr()) fileUnder(self, ValueList{kk}, v);
                    else     fileUnder(self, *k.arr(), v);
                }
                else fileUnder(self, ValueList{k}, v);
            }
            return self;
        }
        if (m == "classify" || m == "categorize") { // group elements by a mapper into a Hash of lists
            Value* into = nullptr; Value* asF = nullptr;
            for (auto& x : args) if (x.t == VT::Pair && x.pairVal()) {
                if (x.s == "into")   into = x.pairVal();
                else if (x.s == "as") asF = x.pairVal();  // what gets STORED, vs what is classified BY
            }
            // The classifier is the first POSITIONAL argument — `:as` and
            // `:into` may be written before it (`classify(:as(*  * 2), * % 2)`),
            // and reading args[0] blindly took the adverb for the classifier and
            // keyed by the element itself (Nil-Any sheet NA-33).
            Value mapper; bool haveMapper = false;
            for (auto& x : args)
                if (!(x.t == VT::Pair && (x.s == "into" || x.s == "as"))) {
                    mapper = x; haveMapper = true; break;
                }
            if (!haveMapper)
                throw RakuError{Value::typeObj("X::AdHoc"),
                    "Must specify something to " + (const std::string&)m +
                        " with, a Callable, Hash or List"};
            Value h = Value::makeHash();
            // …and the result is an OBJECT hash (Rakudo's `Hash[Mu,Mu]`), so a
            // key keeps the classifier's own type instead of stringifying.
            h.ofTypeM() = "Mu,Mu,Any";
            h.objKeyed = true;
            // A key that is itself a LIST is a multi-LEVEL path: the element lands
            // in a hash of hashes, one level per key (`classify { [.&odd, .&big] }`
            // is %h<odd><big>), not under the keys joined into one string.
            // Rakudo's classify/categorize answer an OBJECT hash (`Mu %{Mu}`), so a
            // key keeps the classifier's own type: `(61,).classify(*)` has the Int
            // 61 for a key, not "61". Our payload is string-keyed, so the original
            // travels on the stored value's pairKey — the same channel Set/Bag/Mix
            // already use, which .key/.keys/.pairs/hashToPairs/.raku all honour.
            // (Math::NumberTheory reads `$_.key` straight out of a classify and
            // hands it to `power-mod(Int:D …)`, which a Str never bound.)
            // An UNDEFINED key keeps its gist — Nil, (Any), (Int) — instead of
            // collapsing to the empty string. Rakudo shows `Bag(Nil(6))` where
            // rakupp showed `Bag((6))` for lines a classifier could not key
            // (issue #14's file: lines with fewer words than the index).
            auto keyOf = [](const Value& kv) {
                // the OBJECT-HASH index, so a classify's keys line up with a
                // `:{ }` literal's (roast's classify/categorize files compare
                // the two with `is-deeply`) — identity for everything but a
                // plain Str, which indexes by itself
                if (!rtIsDefined(kv)) return kv.gist();
                return objHashIndex(kv);
            };
            auto keyObj = [](const Value& kv) -> std::shared_ptr<Value> {
                return kv.t == VT::Str ? nullptr : std::make_shared<Value>(kv);
            };
            auto add = [&](const ValueList& path, const Value& vIn) {
                // `:as` maps the STORED value; the key still comes from the classifier
                Value v = asF ? callCallable(*asF, {vIn}) : vIn;
                // …and the bucket is an ARRAY, whose elements cannot hold Nil:
                // `Nil.classify({$_})` files `[Any]` under the key Nil (NA-14).
                if (v.t == VT::Nil) v = Value::any();
                Value* level = &h;
                for (size_t d = 0; d + 1 < path.size(); d++) {
                    std::string ks = keyOf(path[d]);
                    auto it = level->hash()->find(ks);
                    if (it == level->hash()->end() || it->second.t != VT::Hash || !it->second.hash()) {
                        Value nested = Value::makeHash();
                        nested.ofTypeM() = "Mu,Mu,Any"; // a nested level is an object hash too
                        nested.objKeyed = true;
                        nested.pairKeyM() = keyObj(path[d]);
                        (*level->hash())[ks] = std::move(nested);
                    }
                    level = &(*level->hash())[ks];
                }
                std::string key = keyOf(path.back());
                auto it = level->hash()->find(key);
                if (it == level->hash()->end()) {
                    Value a = Value::array(); a.arr()->push_back(v);
                    a.pairKeyM() = keyObj(path.back());
                    (*level->hash())[key] = std::move(a);
                }
                else {
                    if (it->second.t != VT::Array) {
                        Value a = Value::array(); a.arr()->push_back(it->second);
                        a.pairKeyM() = it->second.pairKey();
                        it->second = a;
                    }
                    it->second.arr()->push_back(v);
                }
            };
            for (auto& v : items) {
                // the classifier may be a Callable (called), a Hash (indexed by the
                // element), or an Array (indexed by the element as position)
                Value k;
                if (mapper.t == VT::Code) k = callCallable(mapper, {v});
                else if (mapper.t == VT::Hash && mapper.hash()) { auto it = mapper.hash()->find(v.toStr()); k = it != mapper.hash()->end() ? it->second : Value::any(); }
                else if (mapper.t == VT::Array && mapper.arr()) { long long i = v.toInt(); k = (i >= 0 && i < (long long)mapper.arr()->size()) ? (*mapper.arr())[i] : Value::any(); }
                else k = v;
                // An UNDEFINED key keeps its gist — Nil, (Any), (Int) — instead of
                // collapsing to the empty string. Rakudo shows `Bag(Nil(6))` where
                // rakupp showed `Bag((6))` for lines a classifier could not key
                // (issue #14's file: lines with fewer words than the index).
                auto pathOf = [&](const Value& kv) {
                    ValueList p;
                    if (kv.t == VT::Array && kv.arr() && !kv.itemized && !kv.arr()->empty())
                        for (auto& e : *kv.arr()) p.push_back(e);
                    else p.push_back(kv);
                    return p;
                };
                // categorize's classifier returns a LIST OF categories (each of
                // which may be a multi-level path); classify's returns just one.
                if (m == "categorize" && k.t == VT::Array && k.arr()) { for (auto& kk : *k.arr()) add(pathOf(kk), v); }
                else add(pathOf(k), v);
            }
            if (into) { // :into(%h) — append into an existing hash and return it
                if (into->t != VT::Hash || !into->hash()) *into = Value::makeHash();
                for (auto& kv : *h.hash()) {
                    auto it = into->hash()->find(kv.first);
                    if (it == into->hash()->end()) (*into->hash())[kv.first] = kv.second;
                    else if (it->second.t == VT::Array && kv.second.t == VT::Array)
                        for (auto& e : *kv.second.arr()) it->second.arr()->push_back(e);
                }
                return *into;
            }
            return h;
        }
        if (m == "rotor" || m == "batch") { // chunk into sublists of a fixed size
            // sizes cycle, `size => gap`, :partial — parsing shared with the
            // lazy endless-source arm (parseRotorSpecs above)
            std::vector<RotorSpec> specs;
            bool partial;
            parseRotorSpecs(args, m == "batch", specs, partial);
            Value out = Value::array(); out.isList = true;
            for (size_t i = 0, k = 0; i < items.size(); k++) {
                const RotorSpec& sp = specs[k % specs.size()];
                const bool short_ = i + (size_t)sp.n > items.size();
                if (short_ && !partial) break;
                Value chunk = Value::array(); chunk.isList = true;
                for (size_t j = i; j < i + (size_t)sp.n && j < items.size(); j++) chunk.arr()->push_back(items[j]);
                out.arr()->push_back(chunk);
                // `:partial` emits THE final partial batch — one, and then the
                // walk is over. With a negative gap the windows overlap, so
                // carrying on produced a tail of ever-shorter leftovers
                // ((1..5).rotor(3 => -2, :partial) ended …(4,5),(5,)).
                if (short_) break;
                i += (size_t)(sp.step < 1 ? 1 : sp.step); // step is clamped, so this terminates
            }
            return out;
        }
        if (m == "snip" && sixE()) { // 6.e: split into sublists — each predicate consumes the
            // leading run it matches; leftovers form the final sublist. The predicate
            // arg is one Callable/type-object, or a list of them.
            ValueList preds;
            for (auto& p : args) {
                if (p.t == VT::Array && p.arr()) for (auto& q : *p.arr()) preds.push_back(q); // a (p1,p2) list of preds
                else preds.push_back(p);
            }
            auto matches = [&](const Value& pred, const Value& el) -> bool {
                if (pred.t == VT::Code) return boolify(callCallable(pred, {el}));
                if (pred.t == VT::Type) return rtTypeMatch(el, pred.s);
                return deepEq(pred, el);
            };
            Value out = Value::array(); out.isList = true;
            size_t idx = 0;
            for (auto& pred : preds) {
                Value sub = Value::array(); sub.isList = true;
                while (idx < items.size() && matches(pred, items[idx])) sub.arr()->push_back(items[idx++]);
                out.arr()->push_back(sub);
            }
            if (idx < items.size()) {
                Value sub = Value::array(); sub.isList = true;
                while (idx < items.size()) sub.arr()->push_back(items[idx++]);
                out.arr()->push_back(sub);
            }
            return out;
        }
        if (m == "are") { // 6.e: narrowest common type, or `.are(T)` = all-conform check
            if (!args.empty()) {
                // `.are(T)` asks whether every element CONFORMS to T, which is the
                // smartmatch question — the nominal matcher used here knows
                // nothing about a Pair (or a Match, or a junction), so a list of
                // Pairs was reported as not being Pairs at all.
                std::string t = typeOfVal(args[0]);
                // A mismatch is a FAILURE, not a throw: `.are(T)` answers True or
                // hands back a Failure naming the first element that does not
                // conform, so `if @a.are(Int) { }` reads it without a `try`
                // (Nil-Any sheet NA-31). The exception is a plain X::AdHoc —
                // roast S29-any/are.t asserts both the type and the wording, and
                // even carries an `# XXX proper exception?` next to it.
                for (size_t k = 0; k < items.size(); k++)
                    if (!applyArith("~~", items[k], args[0]).truthy()) {
                        Value f = rakuppNewFailure();
                        const std::string msg = "Expected '" + t + "' but got '" +
                                                typeOfVal(items[k]) + "' in element " +
                                                std::to_string(k);
                        (*f.hash())["exception"] = Value::typeObj("X::AdHoc");
                        (*f.hash())["message"]   = Value::str(msg);
                        return f;
                    }
                return Value::boolean(true);
            }
            if (items.empty()) return Value::nil();
            // The narrowest type or ROLE every element matches, found by walking
            // the FIRST element's own linearisation (roles included) and taking
            // the first entry the rest conform to: `(1, 2.5)` is Real, `(1, "a")`
            // is Cool, `("foo", MyStr.new)` is Str (Nil-Any sheet NA-31). Folding
            // pairwise over a table of built-in ancestries could not see a user
            // class's chain at all, so anything home-made collapsed to Any.
            // Membership is decided on the other elements' linearisations too,
            // not by smartmatch: `~~` is deliberately loose in places (a tagged
            // value answers Cool), and that looseness would report Cool for a
            // list of Ints and a Date, where Rakudo says Any.
            std::map<std::string, std::set<std::string>> mroCache;
            auto mroOf = [&](const Value& v) -> const std::set<std::string>& {
                std::string key = typeOfVal(v);
                auto it = mroCache.find(key);
                if (it != mroCache.end()) return it->second;
                ValueList mroArgs{Value::pair("roles", Value::boolean(true))};
                Value mro = methodCall(v, "^mro", mroArgs);
                std::set<std::string> names;
                if (mro.t == VT::Array && mro.arr())
                    for (auto& t : *mro.arr()) names.insert(typeOfVal(t));
                return mroCache.emplace(std::move(key), std::move(names)).first->second;
            };
            ValueList mroArgs0{Value::pair("roles", Value::boolean(true))};
            Value mro0 = methodCall(items[0], "^mro", mroArgs0);
            if (mro0.t == VT::Array && mro0.arr())
                for (auto& cand : *mro0.arr()) {
                    const std::string cn = typeOfVal(cand);
                    bool all = true;
                    for (size_t k = 1; k < items.size() && all; k++)
                        all = mroOf(items[k]).count(cn) > 0;
                    if (all) return cand;
                }
            std::string lub = typeOfVal(items[0]);
            for (size_t k = 1; k < items.size(); k++) lub = lubType(lub, typeOfVal(items[k]));
            return Value::typeObj(lub);
        }
        if (m == "minmax") {
            // Range.minmax → the (min max) List; List.minmax → a min..max Range
            if (inv.t == VT::Range) {
                Value out = Value::array(); out.isList = true;
                out.arr()->push_back(Value::integer(inv.rFrom() + (inv.rExFrom() ? 1 : 0)));
                out.arr()->push_back(Value::integer(inv.rTo() - (inv.rExTo() ? 1 : 0)));
                return out;
            }
            // an optional &mapper (or `:by(&code)`) decides the ORDER; the
            // endpoints are still the original elements. As for min/max, the block
            // is the first CODE argument — an adverb may precede it.
            Value mapper = Value::nil();
            for (auto& a : args)
                if (a.t == VT::Code) { mapper = a; break; }
            for (auto& a : args)
                if (a.t == VT::Pair && a.s == "by" && a.pairVal()) mapper = *a.pairVal();
            Value lo, hi, loK, hiK; bool started = false;
            for (auto& v : items) {
                Value k = v;
                if (mapper.t == VT::Code) { ValueList one{v}; k = callCallable(mapper, one); }
                if (!started) { lo = hi = v; loK = hiK = k; started = true; continue; }
                if (valueCmp(k, loK) < 0) { lo = v; loK = k; }
                if (valueCmp(k, hiK) > 0) { hi = v; hiK = k; }
            }
            if (!started) { // no defined elements: Rakudo's empty minmax is Inf..-Inf
                Value rr = Value::range(0, -1, false, false);
                setRangeEnds(rr, Value::number(INFINITY), Value::number(-INFINITY));
                return rr;
            }
            if (lo.t == VT::Int && hi.t == VT::Int)
                return Value::range(lo.toInt(), hi.toInt(), false, false);
            Value out = Value::array(); out.isList = true; // non-Int endpoints (our Range is Int-only)
            out.arr()->push_back(lo); out.arr()->push_back(hi);
            return out;
        }
        if (m == "min" || m == "max") {
            bool wantMax = (m == "max");
            // Rakudo: the extremum of an empty list is ±Inf (min → Inf, max → -Inf)
            // — but with `:k`/`:v`/`:kv`/`:p` the answer is a LIST of the winning
            // positions, and an empty list has none, so it stays empty. (An
            // author summing `.Bag.max(:v)` over possibly-empty bags got -Inf.)
            if (items.empty()) {
                for (auto& a : args)
                    if (a.t == VT::Pair && a.pairVal() && a.pairVal()->truthy() &&
                        (a.s == "k" || a.s == "v" || a.s == "kv" || a.s == "p")) {
                        Value o = Value::array(); o.isList = true; return o;
                    }
                return Value::number(m == "min" ? INFINITY : -INFINITY);
            }
            // an optional &mapper: compare by mapper($_), returning the original
            // element. `:by(&code)` is the named spelling, for the sub form.
            // The block is the first CODE argument, not the first argument — an
            // adverb may come before it (`.min(:k, { … })`), and looking only at
            // args[0] silently dropped the mapper and compared the raw elements.
            Value mapper = Value::nil();
            for (auto& a : args)
                if (a.t == VT::Code) { mapper = a; break; }
            // `:k`/`:v`/`:kv`/`:p` answer EVERY position attaining the extremum,
            // as indices / values / both interleaved / index => value pairs
            char want = 0;
            for (auto& a : args)
                if (a.t == VT::Pair) {
                    if (a.s == "by" && a.pairVal()) mapper = *a.pairVal();
                    else if (a.pairVal() && a.pairVal()->truthy() &&
                             (a.s == "k" || a.s == "v" || a.s == "kv" || a.s == "p"))
                        want = a.s == "kv" ? 'm' : a.s[0];
                }
            // An ASSOCIATIVE invocant answers those adverbs as a MAPPING, not as a
            // numbered list: `%h.max(:v)` is the largest VALUE and `:k` its key,
            // where a Positional's `:k` is the index. It maxes by value too, so
            // `bag(1,1,2).max(:k)` is (1) — the element that occurs most — even
            // though the bare `.max` still compares whole pairs.
            const bool assocAdv = want && inv.t == VT::Hash;
            Value best, bestKey; bool started = false;
            std::vector<size_t> at;
            for (size_t i = 0; i < items.size(); i++) {
                const Value& v = items[i];
                // undefined elements (holes in a sparse array, type objects) don't compete
                if (v.t == VT::Nil || v.t == VT::Any || v.t == VT::Type) continue;
                Value key = v;
                if (assocAdv && v.t == VT::Pair && v.pairVal()) key = *v.pairVal();
                if (mapper.t == VT::Code) { ValueList one{v}; key = callCallable(mapper, one); }
                if (!started) { best = v; bestKey = key; started = true; at = {i}; continue; }
                int c = valueCmp(key, bestKey); // strict compare keeps the FIRST on ties
                if ((!wantMax && c < 0) || (wantMax && c > 0)) { best = v; bestKey = key; at = {i}; }
                else if (c == 0) at.push_back(i);
            }
            if (!started) { // all undefined — same rule as the empty list above
                if (want) { Value o = Value::array(); o.isList = true; return o; }
                return Value::number(m == "min" ? INFINITY : -INFINITY);
            }
            if (!want) return best;
            Value o = Value::array(); o.isList = true; // Rakudo answers a List here
            for (size_t i : at) {
                if (assocAdv && items[i].t == VT::Pair) {
                    const Value& pv = items[i];
                    Value k = pv.pairKey() ? *pv.pairKey() : Value::str(pv.s);
                    if (want == 'k' || want == 'm') o.arr()->push_back(k);
                    if (want == 'v' || want == 'm') o.arr()->push_back(pv.pairVal() ? *pv.pairVal() : Value::any());
                    if (want == 'p') o.arr()->push_back(pv);
                    continue;
                }
                if (want == 'k' || want == 'm') o.arr()->push_back(Value::integer((long long)i));
                if (want == 'v' || want == 'm') o.arr()->push_back(items[i]);
                if (want == 'p') {
                    Value pr = Value::pair(std::to_string(i), items[i]);
                    pr.pairKeyM() = std::make_shared<Value>(Value::integer((long long)i));
                    o.arr()->push_back(std::move(pr));
                }
            }
            return o;
        }
        // resolve a head/tail count arg: Int, `*` (all), or `*-N` (WhateverCode of the length)
        auto resolveCount = [&](Value a, long long sz) -> long long {
            if (a.t == VT::Whatever) return sz;
            if (a.isNumeric() && std::isinf(a.toNum())) return sz; // head(Inf) / tail(Inf) = all
            // ANY Callable count is called with the element count — `*-2` and the
            // spelled-out `{ $_ - 2 }` mean the same thing to .head/.tail/.skip
            if (a.t == VT::Code) { ValueList one{Value::integer(sz)}; return callCallable(a, one).toInt(); }
            if (a.t == VT::Str) { // a non-numeric string count is an error (.skip("foo"))
                const std::string& s = a.s; bool num = !s.empty();
                for (char c : s) if (!ascii::isdigit((unsigned char)c) && c != '-' && c != '+' && c != '.' && c != ' ') { num = false; break; }
                if (!num) throw RakuError{Value::typeObj("X::Str::Numeric"), "Cannot convert string to number: '" + s + "'"};
            }
            return a.toInt();
        };
        if (m == "head") {
            // NIL when there is nothing to take, for the same reason `.tail`
            // below answers Nil: an empty list has no first element, and Nil
            // (not Any) is Rakudo's word for that (Nil-Any sheet NA-29).
            if (args.empty()) return items.empty() ? Value::nil() : items.front();
            long long n = resolveCount(a0(), (long long)items.size());
            if (n < 0) n = 0;
            Value o = Value::array(); o.isList = true;
            for (long long k = 0; k < n && k < (long long)items.size(); k++) o.arr()->push_back(items[k]);
            return o;
        }
        if (m == "tail") {
            // NIL, not Any, when there is nothing to take — `.first` already
            // answers Nil and `.tail` did not. The difference is not cosmetic:
            // Nil assigned into a TYPED container resets it to the type object,
            // where Any type-fails, so `has CounterTracker @!ct; @!ct.push:
            // @!ct.tail.clone` works upstream and threw here (RakuDoc::Render's
            // ScopedData opens every scope that way).
            if (args.empty()) return items.empty() ? Value::nil() : items.back();
            long long n = resolveCount(a0(), (long long)items.size());
            if (n < 0) n = 0;
            Value o = Value::array(); o.isList = true;
            long long start = std::max(0LL, (long long)items.size() - n);
            for (long long k = start; k < (long long)items.size(); k++) o.arr()->push_back(items[k]);
            return o;
        }
        if (m == "skip") { // drop the first n elements (default 1)
            // 6.e added a LIST form: the counts alternate produce, skip,
            // produce, skip… so .skip(2,3) keeps the first two, drops the next
            // three and then produces everything left. A single Int keeps the
            // 6.c meaning — skip that many — because that candidate is the more
            // specific one and did not move.
            bool manyCounts  = args.size() > 1;
            bool iterableArg = args.size() == 1 &&
                               (args[0].t == VT::Array || args[0].t == VT::Range);
            // Before 6.e there is no candidate taking two counts at all. A single
            // ITERABLE argument is a different matter: it numifies to its length
            // and skips that many, which is what 6.d does with `.skip((2,3))`, so
            // that spelling keeps falling through to the plain path.
            if (manyCounts && !sixE())
                throw RakuError{Value::typeObj("X::Multi::NoMatch"),
                    "Cannot resolve caller skip(" + inv.typeName() + ": Int, Int); "
                    "the list form of skip arrived with 6.e"};
            if (sixE() && (manyCounts || iterableArg)) {
                ValueList counts;
                if (args.size() == 1) counts = toList(args[0]);
                else for (auto& a2 : args) counts.push_back(a2);
                Value o = Value::array(); o.isList = true;
                size_t at = 0;
                bool producing = true;                 // the first count is a PRODUCE
                for (auto& cv : counts) {
                    long long n = std::max(0LL, cv.toInt());
                    for (long long k = 0; k < n && at < items.size(); k++, at++)
                        if (producing) o.arr()->push_back(items[at]);
                    producing = !producing;
                }
                for (; at < items.size(); at++) o.arr()->push_back(items[at]); // the tail is produced
                return o;
            }
            long long n = args.empty() ? 1 : resolveCount(a0(), (long long)items.size());
            if (n < 0) n = 0;
            Value o = Value::array(); o.isList = true;
            for (long long k = n; k < (long long)items.size(); k++) o.arr()->push_back(items[k]);
            return o;
        }
        if (m == "first") {
            // :k → index; :v → value (the default); :kv → both; :p → index => value;
            // :end → search backwards for the LAST match
            char want = 0; bool wantEnd = false;
            ValueList firstSel;
            for (auto& a : args) if (a.t == VT::Pair && a.pairVal()) {
                // :!v is an error — "not the value" has nothing to return
                if (a.s == "v" && !a.pairVal()->truthy())
                    throw RakuError{Value::typeObj("X::Adverb"), "Specified a negated :v adverb"};
                if (!a.pairVal()->truthy()) continue;
                if (a.s == "end") wantEnd = true;
                else if (a.s == "k" || a.s == "v" || a.s == "p") { want = a.s[0]; firstSel.push_back(Value::str(a.s.str())); }
                else if (a.s == "kv") { want = 'm'; firstSel.push_back(Value::str("kv")); }
            }
            // Two shape adverbs at once is X::Adverb — but `.first` hands it back
            // as a FAILURE rather than throwing, so `my $r = @a.first(…, :k, :v)`
            // only detonates when $r is used (Nil-Any sheet NA-22).
            if (firstSel.size() > 1) {
                Value o = Value::array(firstSel); o.isList = true; o.s = "Seq";
                Value f = rakuppNewFailure();
                const std::string msg = "Cannot use both adverbs at the same time";
                Value none = Value::array(); none.isList = true; none.s = "Seq";
                (*f.hash())["exception"] = makeTypedEx(
                    "X::Adverb", {{"what", Value::str("first")}, {"source", Value::str("a List")},
                                  {"nogo", o}, {"unexpected", none}}, msg);
                (*f.hash())["message"] = Value::str(msg);
                return f;
            }
            auto answer = [&](size_t i) -> Value {
                if (want == 'k') return Value::integer((long long)i);
                if (want == 'p') {
                    Value pr = Value::pair(std::to_string(i), items[i]);
                    pr.pairKeyM() = std::make_shared<Value>(Value::integer((long long)i));
                    return pr;
                }
                if (want == 'm') { // `.first(:kv)` answers a LIST of two, not a Seq
                    Value o = Value::array(); o.isList = true;
                    o.arr()->push_back(Value::integer((long long)i));
                    o.arr()->push_back(items[i]);
                    return o;
                }
                return items[i];
            };
            Value pred; bool havePred = false;
            for (auto& a : args) if (a.t != VT::Pair) { pred = a; havePred = true; break; }
            // A Bool matcher is a mistake, and `.first` reports it as a FAILURE:
            // `.grep` throws, `.first` hands one back (Nil-Any sheet NA-22).
            if (havePred && pred.t == VT::Bool) {
                const std::string msg =
                    "Cannot use Bool as Matcher with '.first'.  Did you mean to use $_ inside a block?";
                Value f = rakuppNewFailure();
                (*f.hash())["exception"] = Value::typeObj("X::Match::Bool");
                (*f.hash())["message"]   = Value::str(msg);
                return f;
            }
            auto match = [&](const Value& v) {
                if (!havePred) return true;
                return matcherAccepts(*this, v, pred);
            };
            if (wantEnd) {
                for (size_t i = items.size(); i-- > 0; ) if (match(items[i])) return answer(i);
            } else {
                for (size_t i = 0; i < items.size(); i++) if (match(items[i])) return answer(i);
            }
            return Value::nil(); // no match: Nil (like Rakudo)
        }
        if ((m == "pickpairs" || m == "grabpairs") && inv.t == VT::Hash && inv.hash() &&
            (inv.hashKind == "Set" || inv.hashKind == "SetHash" ||
             inv.hashKind == "Bag" || inv.hashKind == "BagHash" ||
             inv.hashKind == "Mix" || inv.hashKind == "MixHash")) {
            // random DISTINCT keys as key => weight Pairs (unweighted among
            // keys); grabpairs also REMOVES them from the (mutable) hash
            if (m == "grabpairs" &&
                (inv.hashKind == "Set" || inv.hashKind == "Bag" || inv.hashKind == "Mix"))
                throw RakuError{Value::typeObj("X::Immutable"),
                    "Cannot call 'grabpairs' on an immutable '" + inv.hashKind + "'"};
            std::vector<std::string> keys;
            for (auto& kv : *inv.hash()) keys.push_back(kv.first);
            long long n = 1;
            if (!args.empty())
                n = (args[0].t == VT::Whatever || (args[0].t == VT::Num && std::isinf(args[0].n)))
                  ? (long long)keys.size()
                  : args[0].t == VT::Code ? std::max(0LL, callCallable(args[0], ValueList{Value::integer((long long)keys.size())}).toInt())
                  : args[0].toInt();
            if (n > (long long)keys.size()) n = (long long)keys.size();
            Value out = Value::array(); out.isList = true; out.s = "Seq";
            for (long long k = 0; k < n && !keys.empty(); k++) {
                size_t i = (size_t)(randDouble() * keys.size());
                if (i >= keys.size()) i = keys.size() - 1;
                std::string key = keys[i]; keys.erase(keys.begin() + i);
                out.arr()->push_back(Value::pair(key, (*inv.hash())[key]));
                if (m == "grabpairs") inv.hash()->erase(key);
            }
            if (args.empty()) return out.arr()->empty() ? Value::nil() : (*out.arr())[0];
            return out;
        }
        if (m == "grab" && inv.t == VT::Hash && inv.hash() &&
            (inv.hashKind == "Set" || inv.hashKind == "SetHash" ||
             inv.hashKind == "Bag" || inv.hashKind == "BagHash" ||
             inv.hashKind == "Mix" || inv.hashKind == "MixHash")) {
            // .grab = .pick that CONSUMES: each draw removes one unit of weight
            if (inv.hashKind == "Set" || inv.hashKind == "Bag" || inv.hashKind == "Mix")
                throw RakuError{Value::typeObj("X::Immutable"),
                    "Cannot call 'grab' on an immutable '" + inv.hashKind + "'"};
            if (inv.hashKind == "MixHash")
                throw RakuError{Value::typeObj("X::AdHoc"),
                    "Cannot .grab from a MixHash; weights aren't multiplicities"};
            bool one = args.empty();
            bool all = !args.empty() && (args[0].t == VT::Whatever ||
                                         (args[0].t == VT::Num && std::isinf(args[0].n)));
            long long want = one ? 1 : all ? -1 : args[0].toInt();
            Value out = Value::array(); out.isList = true; out.s = "Seq";
            for (long long k = 0; want < 0 || k < want; k++) {
                double total = 0;
                for (auto& kv : *inv.hash())
                    total += inv.hashKind == "SetHash" ? 1.0 : kv.second.toNum();
                if (total <= 0) break;
                double r = randDouble() * total;
                std::string key;
                for (auto& kv : *inv.hash()) {
                    double w = inv.hashKind == "SetHash" ? 1.0 : kv.second.toNum();
                    if (w <= 0) continue;
                    if (r < w) { key = kv.first; break; }
                    r -= w;
                }
                if (key.empty() && !inv.hash()->empty()) key = inv.hash()->begin()->first;
                if (key.empty()) break;
                out.arr()->push_back(Value::str(key));
                if (inv.hashKind == "SetHash") inv.hash()->erase(key);
                else {
                    // exact decrement — a count past long long must not saturate
                    Value c = rtSub((*inv.hash())[key], Value::integer(1));
                    bool pos = c.big() ? c.big()->sign > 0 : c.i > 0;
                    if (!pos) inv.hash()->erase(key); else (*inv.hash())[key] = std::move(c);
                }
            }
            if (one) return out.arr()->empty() ? Value::nil() : (*out.arr())[0];
            return out;
        }
        if (m == "pick" || m == "roll") { // random element(s); pick = without replacement
            // an enum type picks from its VALUES (red/green/blue), not its (key=>val) pairs
            ValueList enumVals;
            for (auto& pr : items) if (!inv.enumType.empty() && pr.t == VT::Pair) {
                Value ev = Value::enumVal(pr.s, pr.pairVal() ? pr.pairVal()->toInt() : 0);
                ev.enumType = inv.enumType; enumVals.push_back(ev);
            }
            // quanthashes pick from their KEYS (Bag/Mix: weighted by count — sampled,
            // never materialized: a bag with a count of 10^9 must not build a pool)
            static const std::set<std::string> setty = {"Set", "SetHash"};
            static const std::set<std::string> baggy = {"Bag", "BagHash", "Mix", "MixHash"};
            if (inv.t == VT::Hash && inv.hash() && (setty.count(inv.hashKind) || baggy.count(inv.hashKind))) {
                if (m == "pick" && inv.hashKind.rfind("Mix", 0) == 0) // Mix has no .pick — weights aren't multiplicities
                    throw RakuError{Value::typeObj("X::AdHoc"),
                        "Cannot .pick from a " + inv.hashKind + "; use .roll instead"};
                if (!args.empty() && args[0].t == VT::Num && std::isnan(args[0].n))
                    throw RakuError{Value::typeObj("X::AdHoc"), "Cannot coerce NaN to an Int"};
                // element, weight — the ELEMENT, not the map key: a quanthash keys
                // by identity (`Str|a`), so drawing the key would hand back that
                // identity string instead of the value that was put in.
                std::vector<std::pair<Value, double>> pool;
                double total = 0;
                for (auto& kv : *inv.hash()) {
                    double w = setty.count(inv.hashKind) ? 1.0 : kv.second.toNum();
                    if (w > 0) {
                        pool.push_back({kv.second.pairKey() ? *kv.second.pairKey()
                                                            : Value::str(kv.first), w});
                        total += w;
                    }
                }
                auto draw = [&]() -> long long { // weighted index, -1 when exhausted
                    if (total <= 0) return -1;
                    double r = randDouble() * total;
                    for (size_t k = 0; k < pool.size(); k++) {
                        if (r < pool[k].second) return (long long)k;
                        r -= pool[k].second;
                    }
                    for (size_t k = pool.size(); k-- > 0;) if (pool[k].second > 0) return (long long)k;
                    return -1;
                };
                // An empty quanthash still answers with a SEQ, like the
                // non-empty path below — `set().roll(1)` is `().Seq`, which is
                // what S02-types/set.t asserts. It returned a plain Array here,
                // and nothing noticed until `eqv` learned to tell a Seq from an
                // Array: the tests compared equal on elements and so passed for
                // the wrong reason.
                if (pool.empty()) {
                    if (args.empty()) return Value::nil();
                    Value empty = Value::array(); empty.isList = true; empty.s = "Seq";
                    return empty;
                }
                if (args.empty()) { long long k = draw(); return k < 0 ? Value::nil() : pool[k].first; }
                bool all = args[0].t == VT::Whatever ||
                           // the NAME `Whatever` is the TYPE OBJECT, and Rakudo
                           // treats .roll(Whatever) exactly as .roll(*)
                           (args[0].t == VT::Type && args[0].s == "Whatever") ||
                           (args[0].t == VT::Str && (args[0].s == "*" || args[0].s == "Inf")) ||
                           (args[0].isNumeric() && std::isinf(args[0].toNum()));
                if (all && m == "roll") { // roll(*): an INFINITE lazy stream of weighted draws
                    Value out = Value::array(); out.isList = true; out.s = "Seq";
                    auto st = std::make_shared<LazySeqState>();
                    st->infinite = true;
                    auto poolC = pool; double totalC = total;
                    st->appendNext = [poolC, totalC](ValueList& cache) -> bool {
                        double r = randDouble() * totalC;
                        for (auto& pw : poolC) {
                            if (r < pw.second) { cache.push_back(pw.first); return true; }
                            r -= pw.second;
                        }
                        if (!poolC.empty()) { cache.push_back(poolC.back().first); return true; }
                        return false;
                    };
                    out.extM() = st;
                    return out;
                }
                double totalUnits = 0; for (auto& pw : pool) totalUnits += setty.count(inv.hashKind) ? 1 : std::ceil(pw.second);
                // .pick(&calc) applies the Callable to the total weight (`$b.total`)
                long long n = all ? (long long)totalUnits
                    : args[0].t == VT::Code ? std::max(0LL, callCallable(args[0], ValueList{Value::number(total)}).toInt())
                    : args[0].toInt();
                Value out = Value::array(); out.isList = true; out.s = "Seq";
                if (m == "pick") { // without replacement: consume one unit of weight per draw
                    for (long long i = 0; i < n; i++) {
                        long long k = draw();
                        if (k < 0) break;
                        out.arr()->push_back(pool[k].first);
                        double dec = std::min(1.0, pool[k].second);
                        pool[k].second -= dec; total -= dec;
                    }
                }
                else for (long long i = 0; i < n; i++) {
                    long long k = draw();
                    if (k < 0) break;
                    out.arr()->push_back(pool[k].first);
                }
                return out;
            }
            const ValueList& pool0 = inv.enumType.empty() ? items : enumVals;
            // Nothing to draw from: a bare `.pick`/`.roll` is Nil, and the
            // counted form is the empty LIST (`.roll(n)` a Seq) — not an empty
            // ARRAY, which compared unequal to `()` (Nil-Any sheet NA-43).
            if (pool0.empty()) {
                if (args.empty()) return Value::nil();
                Value o = Value::array(); o.isList = true;
                if (m == "roll") o.s = "Seq";
                return o;
            }
            bool all = !args.empty() && (args[0].t == VT::Whatever ||
                       (args[0].t == VT::Type && args[0].s == "Whatever") || // .pick(Whatever) == .pick(*)
                       (args[0].t == VT::Str && (args[0].s == "*" || args[0].s == "Inf")) ||
                       (args[0].isNumeric() && std::isinf(args[0].toNum())));
            if (args.empty()) return pool0[(size_t)(randDouble() * pool0.size())]; // single element
            if (m == "roll" && all) { // roll(*): an INFINITE lazy stream of random draws
                Value out = Value::array(); out.isList = true; out.s = "Seq";
                auto st = std::make_shared<LazySeqState>();
                st->infinite = true;
                ValueList poolC = pool0;
                st->appendNext = [poolC](ValueList& cache) -> bool {
                    cache.push_back(poolC[(size_t)(randDouble() * poolC.size())]);
                    return true;
                };
                out.extM() = st;
                return out;
            }
            long long n = all ? (long long)pool0.size()
                : args[0].t == VT::Code ? std::max(0LL, callCallable(args[0], ValueList{Value::integer((long long)pool0.size())}).toInt())
                : args[0].toInt();
            Value out = Value::array(); out.isList = true; out.s = "Seq"; // .pick(n)/.roll(n) return a Seq
            if (m == "pick") { // without replacement
                ValueList pool = pool0;
                for (long long i = 0; i < n && !pool.empty(); i++) {
                    size_t j = (size_t)(randDouble() * pool.size());
                    out.arr()->push_back(pool[j]); pool.erase(pool.begin() + j);
                }
            } else { // roll: with replacement
                for (long long i = 0; i < n; i++) out.arr()->push_back(pool0[(size_t)(randDouble() * pool0.size())]);
            }
            return out;
        }
        if (m == "unique") {
            // :as(&mapper) compares mapped keys; :with(&eq) uses a custom equality (O(n²)).
            Value asF, withF;
            for (auto& a : args) if (a.t == VT::Pair && a.pairVal() && a.pairVal()->t == VT::Code) { if (a.s == "as") asF = *a.pairVal(); else if (a.s == "with") withF = *a.pairVal(); }
            auto keyOf = [&](const Value& v) { return asF.t == VT::Code ? callCallable(asF, ValueList{v}) : v; };
            Value out = Value::array(); out.isList = true;
            if (withF.t == VT::Code) {
                ValueList kept;
                for (auto& v : items) { Value k = keyOf(v); bool dup = false;
                    for (auto& kk : kept) if (callCallable(withF, ValueList{k, kk}).truthy()) { dup = true; break; }
                    if (!dup) { kept.push_back(k); out.arr()->push_back(v); } }
            } else {
                std::set<std::string> seen;
                for (auto& v : items) if (seen.insert(whichOf(keyOf(v))).second) out.arr()->push_back(v); // === identity: 1, "1", 1.0 are three
            }
            return out;
        }
        if (m == "repeated") { // elements seen more than once (2nd+ occurrences)
            // `:as(&code)` compares the MAPPED value; `:with(&op)` supplies the
            // comparison itself, which needs a linear scan rather than a set
            Value asF, withF;
            for (auto& a : args)
                if (a.t == VT::Pair && a.pairVal()) {
                    if (a.s == "as") asF = *a.pairVal();
                    else if (a.s == "with") withF = *a.pairVal();
                }
            Value out = Value::array(); out.isList = true; out.s = "Seq";
            auto keyOf = [&](const Value& v) {
                if (asF.t != VT::Code) return v;
                ValueList one{v}; return callCallable(asF, one);
            };
            if (withF.t == VT::Code) {
                ValueList kept;
                for (auto& v : items) {
                    Value k = keyOf(v);
                    bool dup = false;
                    for (auto& p : kept) { ValueList two{p, k}; if (callCallable(withF, two).truthy()) { dup = true; break; } }
                    if (dup) out.arr()->push_back(v); else kept.push_back(k);
                }
                return out;
            }
            std::set<std::string> seen;
            for (auto& v : items) if (!seen.insert(whichOf(keyOf(v))).second) out.arr()->push_back(v); // === identity, as unique
            return out;
        }
        if (m == "toggle") { // gate values on/off, flipping at each condition boundary
            // ON: emit while cond(v) is true; the first false value flips OFF (not
            // emitted) and consumes the condition. OFF: skip while false; the first
            // true value flips ON (emitted) and consumes the condition. Out of
            // conditions → the state freezes. :off starts in the OFF state.
            bool on = true;
            ValueList conds;
            for (auto& a : args) {
                if (a.t == VT::Pair && a.s == "off") on = !(a.pairVal() && a.pairVal()->truthy());
                else if (a.t == VT::Code) conds.push_back(a);
            }
            Value out = Value::array(); out.isList = true;
            size_t ci = 0;
            for (auto& v : items) {
                if (ci < conds.size()) {
                    bool c = predAnswerTruthy(*this, callCallable(conds[ci], ValueList{v}), v);
                    if (on) { if (c) out.arr()->push_back(v); else { on = false; ci++; } }
                    else if (c) { on = true; ci++; out.arr()->push_back(v); }
                } else if (on) out.arr()->push_back(v);
            }
            return out;
        }
        if (m == "squish") { // collapse adjacent duplicates (:as maps keys, :with compares them)
            Value asF, withF;
            for (auto& a : args) if (a.t == VT::Pair && a.pairVal() && a.pairVal()->t == VT::Code) { if (a.s == "as") asF = *a.pairVal(); else if (a.s == "with") withF = *a.pairVal(); }
            auto keyOf = [&](const Value& v) { return asF.t == VT::Code ? callCallable(asF, ValueList{v}) : v; };
            Value out = Value::array(); out.isList = true;
            bool first = true; Value prevKey;
            for (auto& v : items) {
                Value k = keyOf(v); bool same = false;
                // `:with` is called (PREVIOUS, CURRENT) — the operands were the
                // other way round, so an asymmetric test like
                // `-> $prev, $cur { $cur == $prev + 1 }` read backwards and
                // squished nothing (Nil-Any sheet NA-26; roast squish.t
                // asserts the call sequence as well as the result).
                if (!first) same = withF.t == VT::Code ? callCallable(withF, ValueList{prevKey, k}).truthy()
                                                      : applyArith("===", k, prevKey).truthy();
                if (first || !same) out.arr()->push_back(v);
                prevKey = k; first = false;
            }
            return out;
        }
        if (m == "sort") {
            // Deciding the order from a flat array of int64 keys instead of from
            // the Values themselves. Every comparison in the generic path is an
            // out-of-line valueCmp plus two RANDOM probes into an array of
            // Values — sizeof(Value) is ~100 bytes, so 50k elements is a 5 MB
            // working set the sort walks in shuffled order, and the comparator
            // measured 35% of `(1..50_000).map(…).sort` compiled. When every
            // element is a native Int the whole order is decided by one int64
            // apiece: pull those out, sort sixteen-byte (key, index) pairs with
            // an inlined compare, and the probes go away with the call. The
            // index tiebreak is what makes plain sort stable here.
            //
            // Returns false — leaving `order` untouched — for any list this does
            // not describe, including one long enough that an index needs more
            // than 32 bits.
            auto sortByNativeInt = [](const ValueList& xs, std::vector<size_t>& order) {
                if (xs.size() > 0xFFFFFFFFull) return false;
                for (const Value& v : xs)
                    if (!((v.t == VT::Int && !v.big()) || v.t == VT::Bool)) return false;
                std::vector<std::pair<long long, uint32_t>> kv(xs.size());
                for (size_t i = 0; i < xs.size(); i++)
                    kv[i] = { xs[i].t == VT::Bool ? (xs[i].b ? 1LL : 0LL) : xs[i].i, (uint32_t)i };
                std::sort(kv.begin(), kv.end(), [](const std::pair<long long, uint32_t>& a,
                                                   const std::pair<long long, uint32_t>& b) {
                    return a.first != b.first ? a.first < b.first : a.second < b.second;
                });
                for (size_t i = 0; i < kv.size(); i++) order[i] = kv[i].second;
                return true;
            };
            // :k sorts the INDICES of the elements instead of the elements
            bool wantK = false;
            for (auto& av : args)
                if (av.t == VT::Pair && av.namedArg && av.s == "k")
                    wantK = !av.pairVal() || av.pairVal()->truthy();
            std::vector<size_t> order(items.size());
            for (size_t i = 0; i < order.size(); i++) order[i] = i;
            if (!args.empty() && args[0].t == VT::Code) {
                Value blk = args[0];
                size_t arity = blk.code()->params && !blk.code()->params->empty()
                    ? blk.code()->params->size()
                    : (blk.code()->placeholders.empty() ? (size_t)blk.code()->whateverArity : blk.code()->placeholders.size());
                // a 0-arity comparator ((1..10).sort(&rand)) has no candidate
                if (arity == 0 && blk.code()->params && blk.code()->hadSig)
                    throw RakuError{Value::typeObj("X::TypeCheck::Argument"),
                        "Uncallable 0-arity comparator for sort"};
                if (arity >= 2) {
                    // Rakudo's merge takes the RIGHT element only when
                    // `by(left, right) > 0`, so a comparator that answers a Bool
                    // rather than an Order still orders the list: `.sort(-> $a,
                    // $b { $b.value > $a.value })` is descending there, and
                    // ML::TriesWithFrequencies sorts its Pareto children that
                    // way. Asked as `cmp(x, y) < 0` no pair ever compared less
                    // and the list came back untouched. The two spellings agree
                    // for every antisymmetric comparator.
                    std::stable_sort(order.begin(), order.end(), [&](size_t x, size_t y) {
                        return callCallable(blk, {items[y], items[x]}).toInt() > 0;
                    });
                } else {
                    // A 1-ary block is a KEY EXTRACTOR, so it runs ONCE PER ELEMENT and
                    // the sort compares the extracted keys — a Schwartzian transform,
                    // which is what Rakudo does. Calling it inside the comparator ran it
                    // O(n log n) times instead of O(n): the documented
                    // `(0..0x1FFFF).sort(*.uniname.chars)` took 49s against Rakudo's 1.2s.
                    ValueList keys(items.size());
                    for (size_t i = 0; i < items.size(); i++) keys[i] = callCallable(blk, {items[i]});
                    if (!sortByNativeInt(keys, order))
                        std::stable_sort(order.begin(), order.end(), [&](size_t x, size_t y) {
                            return valueCmp(keys[x], keys[y]) < 0;
                        });
                }
            } else if (!sortByNativeInt(items, order)) {
                std::stable_sort(order.begin(), order.end(), [&](size_t x, size_t y) {
                    const Value& xa = items[x]; const Value& yb = items[y];
                    bool nx = xa.t == VT::Num && std::isnan(xa.n), ny = yb.t == VT::Num && std::isnan(yb.n);
                    if (nx || ny) return !nx && ny; // NaN sorts after everything
                    return valueCmp(xa, yb) < 0;
                });
            }
            ValueList out;
            out.reserve(order.size());
            for (size_t i : order) out.push_back(wantK ? Value::integer((long long)i) : items[i]);
            return Value::list(out);
        }
        if (m == "tree") {
            // .tree — a nested view of the list. No arg: identity (already nested).
            // .tree(N): N levels deep, flattening everything below level N into leaves.
            // .tree(&c0, &c1, …): descend, then apply closure cD to each level-D node.
            // `*` is no depth LIMIT, which is what no argument already means
            if (args.empty() || args[0].t == VT::Whatever) {
                Value o = Value::array(); *o.arr() = items; o.isList = true; return o;
            }
            // closures may be passed as bare args (`.tree(&a, &b)`) or one array (`.tree([&a, &b])`)
            ValueList closures;
            if (args[0].t == VT::Array && args[0].arr()) { for (auto& e : *args[0].arr()) if (e.t == VT::Code) closures.push_back(e); }
            else for (auto& a : args) if (a.t == VT::Code) closures.push_back(a);
            bool byClosure = !closures.empty();
            long long depth = byClosure ? (long long)closures.size() : args[0].toInt();
            // `.tree(0)` is the IDENTITY, not "flatten everything": roast asks for
            // it by `===`, so it must be the same object and not an equal copy.
            if (!byClosure && depth <= 0) return inv;
            std::function<Value(const Value&, long long)> build = [&](const Value& node, long long d) -> Value {
                bool isList = node.t == VT::Array || node.t == VT::Range;
                if (!isList) return node;
                if (byClosure) {
                    if (d >= (long long)closures.size()) return node; // past the last closure: leaf
                    // the LAST closure is applied to the node itself (by identity, so a
                    // single-closure `.tree(&c)` calls c(self)); deeper closures rebuild
                    if (d + 1 >= (long long)closures.size()) return callCallable(closures[d], ValueList{node});
                    Value kids = Value::array(); kids.isList = true;
                    for (auto& e : (node.t == VT::Range ? node.flatten() : *node.arr()))
                        kids.arr()->push_back(build(e, d + 1));
                    return callCallable(closures[d], ValueList{kids});
                }
                if (d >= depth) { // depth cap: flatten the rest into one level
                    Value o = Value::array(); o.isList = true;
                    for (auto& e : node.flatten()) o.arr()->push_back(e);
                    return o;
                }
                Value kids = Value::array(); kids.isList = true;
                for (auto& e : (node.t == VT::Range ? node.flatten() : *node.arr()))
                    kids.arr()->push_back(build(e, d + 1));
                // every node BELOW the root is an item — that is what stops a later
                // `.flat` from descending, so `.tree(2).flat.elems` is 2 and not 6
                if (d > 0) kids.itemized = true;
                return kids;
            };
            Value self = inv; self.isList = true; // shares the invocant's storage (=== identity)
            return build(self, 0);
        }
        if ((m == "deepmap" || m == "nodemap" || m == "duckmap") && !args.empty() &&
            args[0].t == VT::Code) {
            // deepmap descends nested arrays/hashes and applies the fn at the
            // leaves — which it receives as ALIASES (`.deepmap(++*)` mutates the
            // source); nodemap applies per top-level node without descending;
            // duckmap applies where the fn "quacks", descending on failure.
            const Value& fn = args[0];
            // All three walk ONE element at a time, so a mapper that wants more
            // than one parameter can never be called (Nil-Any sheet NA-32).
            if (codeArity(fn) > 1)
                throw RakuError{Value::typeObj("X::AdHoc"),
                    "." + (const std::string&)m + " only supports Callables with a single parameter, got " +
                        std::to_string(codeArity(fn))};
            // A SLIP result splices into the level it was produced at, and Empty
            // — the empty Slip — vanishes (NA-32). Anything else is one element.
            auto pushResult = [](Value& o, Value r) {
                if (r.t == VT::Array && r.isList && r.s == "Slip") {
                    if (r.arr()) for (auto& y : *r.arr()) o.arr()->push_back(y);
                    return;
                }
                o.arr()->push_back(std::move(r));
            };
            auto leaf = [&](Value& slot) -> Value {
                topicWriteback_ = &slot; // $_/placeholder mutations alias the node
                Value r;
                try { r = callCallable(fn, ValueList{slot}); }
                catch (...) { topicWriteback_ = nullptr; throw; }
                topicWriteback_ = nullptr;
                return r;
            };
            // `next` in the block drops the element and `last` ends the walk, as
            // in `map` — S32-list/duckmap.t's `<a b c>.duckmap({ next if $_ eq
            // "b"; $_ })` is `a c`. duckmap's catch-all below took the control
            // exception for "does not quack" and kept the element.
            auto pushEl = [&](Value& o, Value& x, const std::function<Value(Value&)>& f) -> bool {
                try { pushResult(o, f(x)); }
                catch (NextEx&) { }
                catch (LastEx&) { return false; }
                return true;
            };
            std::function<Value(Value&)> deepEl = [&](Value& e) -> Value {
                if (e.t == VT::Array && e.arr()) {
                    Value o = Value::array(); o.isList = e.isList;
                    for (auto& x : *e.arr()) pushResult(o, deepEl(x));
                    // An INNER result is itemized, so it stays one element of the
                    // level above: `(1, (2, 3)).deepmap(* * 10)` is
                    // `(10, $(20, 30))`, not a flattened `(10, 20, 30)` (NA-32).
                    o.itemized = true;
                    return o;
                }
                if (e.t == VT::Hash && e.hash() && e.hashKind.empty()) {
                    Value o = Value::makeHash();
                    for (auto& kv : *e.hash()) (*o.hash())[kv.first] = deepEl(kv.second);
                    return o;
                }
                return leaf(e);
            };
            std::function<Value(Value&)> duckEl = [&](Value& e) -> Value {
                try { return leaf(e); }
                catch (NextEx&) { throw; }
                catch (LastEx&) { throw; }
                catch (...) {
                    if (e.t == VT::Array && e.arr()) {
                        Value o = Value::array(); o.isList = e.isList;
                        for (auto& x : *e.arr()) if (!pushEl(o, x, duckEl)) break;
                        return o;
                    }
                    if (e.t == VT::Hash && e.hash() && e.hashKind.empty()) {
                        Value o = Value::makeHash();
                        for (auto& kv : *e.hash()) (*o.hash())[kv.first] = duckEl(kv.second);
                        return o;
                    }
                    return e;
                }
            };
            auto applyEl = [&](Value& e) -> Value {
                return m == "deepmap" ? deepEl(e) : m == "duckmap" ? duckEl(e) : leaf(e);
            };
            if (inv.t == VT::Hash && inv.hash() && inv.hashKind.empty()) {
                Value o = Value::makeHash();
                for (auto& kv : *inv.hash()) (*o.hash())[kv.first] = applyEl(kv.second);
                return o;
            }
            // deepmap/duckmap answer in the invocant's own container (Array in,
            // Array out); nodemap always answers a List
            Value out = Value::array();
            out.isList = (m == "nodemap") || inv.t != VT::Array || inv.isList;
            if (inv.t == VT::Array && inv.arr()) {
                for (auto& e : *inv.arr()) if (!pushEl(out, e, applyEl)) break;
            }
            else { Value tmp = inv; return applyEl(tmp); }
            return out;
        }
        if (m == "map" || m == "flatmap") { // flatmap == map that flattens list results one level
            // the mapper must be a Callable — `%h.map(Hash)` (a type object),
            // `.map(1)`, `.map((3,4))` and `.map({a => 1})` all die (NA-20).
            // The named-only forms delegate instead: `map(flat => &f)` is
            // flatmap, and node/deep/duck are the three walkers.
            {
                Value namedFn; std::string namedTo;
                bool positional = false, wantItem = false;
                for (auto& a : args) {
                    if (a.t == VT::Pair && a.namedArg) {
                        const std::string& k = a.s.str();
                        if (k == "item") { if (!a.pairVal() || a.pairVal()->truthy()) wantItem = true; }
                        else if ((k == "flat" || k == "node" || k == "deep" || k == "duck") &&
                                 a.pairVal() && a.pairVal()->t == VT::Code) {
                            namedFn = *a.pairVal();
                            namedTo = k == "flat" ? "flatmap" : k + "map";
                        }
                    } else positional = true;
                }
                if (!positional && !namedTo.empty())
                    return methodCall(inv, namedTo, ValueList{namedFn});
                // `:item` maps the invocant as ONE item: the block sees the whole
                // list as `$_`, so `(1, 2).map(:item, *.elems)` is `(2,)`.
                if (wantItem && positional) {
                    Value fn;
                    for (auto& a : args) if (!(a.t == VT::Pair && a.namedArg)) { fn = a; break; }
                    Value o = Value::array(); o.isList = true; o.s = "Seq";
                    if (fn.t == VT::Code) {
                        Value self = inv; if (self.t == VT::Array) self.itemized = true;
                        o.arr()->push_back(callCallable(fn, ValueList{self}));
                    }
                    return o;
                }
                if (!args.empty() && args[0].t != VT::Code &&
                    !(args[0].t == VT::Pair && args[0].namedArg))
                    throwCannotMap(*this, inv.typeName(), args[0]);
            }
            Value out = Value::array();
            if (!args.empty() && args[0].t == VT::Code) {
                // A block of arity N consumes N elements per iteration
                // (e.g. `%h.kv.map(-> $k, $v {…})` or `{ $^a … $^b }`).
                size_t ar = codeArity(args[0]);
                bool aliasable = ar == 1 && inv.t == VT::Array && inv.arr() && items.size() == inv.arr()->size();
                // Does the block carry loop phasers (FIRST/NEXT/LAST)? One scan;
                // if so, hand callCallableRaw per-iteration control so they fire
                // with loop semantics in the block's own env (Base64's encoder
                // computes its padding in a LAST that reads the block param).
                bool loopPh = false;
                if (args[0].code() && args[0].code()->body)
                    for (auto& s : *args[0].code()->body)
                        if (s->kind == NK::Block) {
                            const std::string& ph = static_cast<Block*>(s.get())->phaser;
                            if (ph == "FIRST" || ph == "NEXT" || ph == "LAST") { loopPh = true; break; }
                        }
                // …and a chunk SHORTER than the arity is an error, not a call
                // with the tail repeated: `(1,2,3).map(-> $a, $b {…})` dies
                // "Too few positionals passed" on its last chunk, while
                // `-> $a, $b?` takes the short one happily (Nil-Any sheet
                // NA-20; roast S32-list/map.t asserts the death).
                size_t required = ar;
                if (args[0].code() && args[0].code()->params) {
                    size_t req = 0;
                    for (auto& pp : *args[0].code()->params)
                        if (!pp.named && !pp.slurpy && !pp.optional && !pp.defaultVal) req++;
                    if (req <= ar) required = req;
                }
                for (size_t i = 0; i < items.size(); i += ar) {
                    ValueList ca;
                    for (size_t k = 0; k < ar && i + k < items.size(); k++) ca.push_back(items[i + k]);
                    if (ca.size() < required)
                        throw RakuError{Value::typeObj("X::AdHoc"),
                            "Too few positionals passed; expected " + std::to_string(required) +
                                " arguments but got " + std::to_string(ca.size())};
                    if (aliasable) topicWriteback_ = &(*inv.arr())[i]; // $_ mutations alias the element
                    if (loopPh)
                        loopPhaserCtl_ = (i == 0 ? 1 : 0) | (i + ar >= items.size() ? 2 : 0) | 4;
                    Value r;
                    try { r = callCallable(args[0], ca); }
                    // 6.e: `next $v` / `last $v` supply the value for the
                    // iteration they end; a bare next/last still skips or stops.
                    catch (LastEx& le) { topicWriteback_ = nullptr;
                                         if (le.hasVal) out.arr()->push_back(le.val);
                                         break; }
                    catch (NextEx& ne) { topicWriteback_ = nullptr;
                                         if (ne.hasVal) out.arr()->push_back(ne.val);
                                         continue; }
                    // post-GLR: map keeps each block result as ONE element; only a
                    // Slip (or flatmap, which flattens one level by design) spreads.
                    if (m == "flatmap") {
                        if (r.t == VT::Array) for (auto& x : *r.arr()) out.arr()->push_back(x);
                        else if (r.t == VT::Range) for (auto& x : r.flatten()) out.arr()->push_back(x);
                        else out.arr()->push_back(r);
                    }
                    else if (r.t == VT::Array && r.isList && r.s == "Slip")
                        for (auto& x : *r.arr()) out.arr()->push_back(x);
                    else out.arr()->push_back(r);
                }
            }
            out.isList = true; out.s = "Seq";
            return out;
        }
        if (m == "grep") {
            Value out = Value::array(); out.isList = true; out.s = "Seq"; // Rakudo: .grep is lazy
            if (args.empty()) return out;
            // adverbs: :v values (default), :k indices, :kv, :p pairs. Exactly
            // ONE may select the shape — two at once is X::Adverb with `.nogo`
            // naming both — and any other named argument is X::Adverb with
            // `.unexpected` naming it. A NEGATED selector (`:!k`) simply means
            // plain values. Unknown adverbs used to be swallowed silently, and a
            // second selector quietly replaced the first (Nil-Any sheet NA-21;
            // roast S32-list/grep.t asserts the unexpected-adverb throw).
            std::string adv = "v";
            Value mt; bool haveMt = false;
            ValueList selected, unexpected;
            for (auto& a : args) {
                if (a.t == VT::Pair && a.namedArg &&
                    (a.s == "k" || a.s == "v" || a.s == "kv" || a.s == "p")) {
                    if (!a.pairVal() || a.pairVal()->truthy()) {
                        adv = a.s; selected.push_back(Value::str(a.s));
                    }
                    // `:!v` asks for "not the values", which names no shape at
                    // all — Rakudo reports it as an unexpected adverb.
                    else if (a.s == "v") unexpected.push_back(Value::str(a.s));
                }
                else if (a.t == VT::Pair && a.namedArg) unexpected.push_back(Value::str(a.s.str()));
                else if (!haveMt) { mt = a; haveMt = true; }
            }
            auto adverbList = [](const ValueList& names) {
                Value o = Value::array(names); o.isList = true; o.s = "Seq"; return o;
            };
            if (!unexpected.empty())
                throwTypedV("X::Adverb",
                            {{"what", Value::str("grep")}, {"source", Value::str("a List")},
                             {"unexpected", adverbList(unexpected)}, {"nogo", adverbList({})}},
                            "Unexpected adverb" + std::string(unexpected.size() > 1 ? "s" : "") +
                                " '" + unexpected[0].toStr() + "'");
            if (selected.size() > 1)
                throwTypedV("X::Adverb",
                            {{"what", Value::str("grep")}, {"source", Value::str("a List")},
                             {"nogo", adverbList(selected)}, {"unexpected", adverbList({})}},
                            "Cannot use both adverbs at the same time");
            if (!haveMt) return out;
            if (mt.t == VT::Bool)
                throw RakuError{Value::typeObj("X::Match::Bool"),
                    "Cannot use Bool as Matcher with '.grep'.  Did you mean to use $_ inside a block?"};
            bool aliasable = inv.t == VT::Array && inv.arr() && items.size() == inv.arr()->size();
            auto emit = [&](size_t idx, const Value& v) {
                if (adv == "k") out.arr()->push_back(Value::integer((long long)idx));
                else if (adv == "kv") { out.arr()->push_back(Value::integer((long long)idx)); out.arr()->push_back(v); }
                else if (adv == "p") { Value pr = Value::pair(std::to_string(idx), v); pr.pairKeyM() = std::make_shared<Value>(Value::integer((long long)idx)); out.arr()->push_back(pr); }
                else out.arr()->push_back(v);
            };
            size_t ar = mt.t == VT::Code ? codeArity(mt) : 1; // arity-N blocks test N at a time
            if (ar < 1) ar = 1;
            for (size_t gi = 0; gi < items.size(); gi += ar) {
                Value v = items[gi];
                bool match;
                if (mt.t == VT::Code) {
                    ValueList ca;
                    for (size_t k = 0; k < ar && gi + k < items.size(); k++) ca.push_back(items[gi + k]);
                    if (aliasable && ar == 1) topicWriteback_ = &(*inv.arr())[gi]; // $_ mutations alias the element
                    try { match = predAnswerTruthy(*this, callCallable(mt, ca), v); }
                    catch (LastEx&) { topicWriteback_ = nullptr; break; }   // `last` in the block ends the grep
                    catch (NextEx&) { topicWriteback_ = nullptr; continue; } // `next` skips the element
                    catch (RedoEx&) { topicWriteback_ = nullptr; gi -= ar; continue; } // `redo` retries it
                    if (aliasable && ar == 1) v = (*inv.arr())[gi];
                    if (match) { for (size_t k = 0; k < ar && gi + k < items.size(); k++) emit(gi + k, gi + k == gi ? v : items[gi + k]); continue; }
                    continue;
                }
                else match = matcherAccepts(*this, v, mt); // .grep(/re/) / .grep(Int) / junction / value
                if (match) emit(gi, v);
            }
            return out;
        }
        // a Setty/Baggy .hash is a PLAIN Hash copy (values: Bool for Set, counts for Bag/Mix)
        if ((m == "hash" || m == "Hash") && inv.t == VT::Hash &&
            (inv.hashKind.rfind("Set", 0) == 0 || inv.hashKind.rfind("Bag", 0) == 0 ||
             inv.hashKind.rfind("Mix", 0) == 0)) {
            Value h = Value::makeHash();
            if (inv.hash()) *h.hash() = *inv.hash();
            return h;
        }
        if (m == "hash" && inv.t == VT::Hash) return inv;   // %h.hash() is the hash itself
        // %h.Hash — a Hash is already one, so it answers itself; a Map (immutable)
        // answers a mutable Hash copy. Only `.hash` was implemented, so the
        // idiomatic `(%meta<provides> // {}).Hash` died with "No such method".
        if (m == "Hash" && inv.t == VT::Hash) {
            if (inv.hashKind.empty() || inv.hashKind == "Hash") return inv;
            Value h = Value::makeHash();
            if (inv.hash()) *h.hash() = *inv.hash();
            return h;
        }
        if (m == "Map" && inv.t == VT::Hash) { // %h.Map — an immutable view (detached copy)
            if (inv.hashKind == "Map") return inv; // a Map's .Map is the Map ITSELF, not a copy
            Value h = Value::makeHash();
            if (inv.hash()) *h.hash() = *inv.hash();
            h.hashKind = "Map";
            return h;
        }
        if ((m == "hash" || m == "Hash" || m == "Map") && inv.t == VT::Array) { // list -> Hash/Map
            // Pairs map directly; non-Pair elements pair up CONSECUTIVELY as
            // key, value — `(0,"a",1,"b").hash` is {0 => "a", 1 => "b"}, so
            // `@a.kv.reverse.hash` inverts an index map (value => index).
            Value h = Value::makeHash();
            for (size_t k = 0; k < items.size(); k++) {
                // …and a HASH element contributes its own pairs, rather than
                // its stringification standing in as one key: `(%a, %b).Hash`
                // is how hashes are merged (later keys win), and pairing the
                // first one up with the second as key => value made nonsense
                // of it.
                if (items[k].t == VT::Hash && items[k].hash()) {
                    for (auto& kv : *items[k].hash()) (*h.hash())[kv.first] = kv.second;
                }
                else if (items[k].t == VT::Pair)
                    (*h.hash())[items[k].s] = items[k].pairVal() ? *items[k].pairVal() : Value::any();
                else if (k + 1 < items.size()) {
                    std::string key = items[k].toStr(); // sequenced explicitly: in `m[f(k)] = g(++k)`
                    (*h.hash())[key] = items[++k];        // the RHS would evaluate before the key!
                }
                else throwHashOddNumber((long long)items.size(), items[k]); // as Hash.new and Rakudo
            }
            // `.Map` asks for a MAP: `(a => 1, b => 2).Map` is immutable and
            // reports Map, where `.Hash`/`.hash` answer a mutable Hash. All
            // three shared this one builder and every one of them came back a
            // Hash — which is also what `--> Map()` on a routine coerces
            // through, so Red's `method exports(--> Map())` handed `use` the
            // wrong kind of thing.
            if (m == "Map") h.hashKind = "Map";
            return h;
        }
        if ((m == "push" || m == "append") && inv.t == VT::Hash) { // %h.push(:a(1)) accumulates into a list
            // Flatten the arguments into the pairs they contribute FIRST: a list, a
            // Seq or a Hash argument contributes its own pairs (`%inv.push: %wc.invert`
            // was silently dropping every one of them). A NAMED argument contributes
            // nothing at all — `%h.push(e => 6)` is a bareword fat-arrow, which binds
            // as a named and is a no-op, not an element.
            ValueList flat;
            for (auto& a : args) {
                if (a.t == VT::Pair && a.namedArg) continue;
                if (a.t == VT::Pair) { flat.push_back(a); continue; }
                if (a.t == VT::Hash && a.hash()) {
                    for (auto& kv : *a.hash()) flat.push_back(Value::pair(kv.first, kv.second));
                    continue;
                }
                if (a.t == VT::Array || a.t == VT::Range) { for (auto& x : a.flatten()) flat.push_back(x); continue; }
                flat.push_back(a);
            }
            // Rakudo pairs consecutive NON-Pair items up as key, value:
            // `%h.push: ('a', 42)` is `a => 42`, and a lone trailing item warns
            // and contributes nothing. ML::TriesWithFrequencies rebuilds every
            // traversed node with `%resChildren.push: ($k, $chNode)`, so dropping
            // the non-Pair items left each rebuilt trie childless.
            for (size_t fi = 0; fi < flat.size(); fi++) {
                const Value& a = flat[fi];
                std::string key; Value val;
                if (a.t == VT::Pair) {
                    key = a.s; val = a.pairVal() ? *a.pairVal() : Value::any();
                } else {
                    if (fi + 1 >= flat.size()) {
                        std::string msg = "Trailing item in Hash." + m.s;
                        if (quietDepth_ == 0 && !runControlWarn(msg)) std::cerr << msg << "\n";
                        break;
                    }
                    key = a.toStr(); val = flat[++fi];
                }
                auto it = inv.hash()->find(key);
                if (it == inv.hash()->end()) {
                    // a NEW key stores the value as it is; only a LIST value spreads
                    // (append and push agree here — it is the existing-key branch that
                    // tells them apart)
                    if (val.t == VT::Array && val.isList) { Value ar = Value::array(); for (auto& x : val.flatten()) ar.arr()->push_back(x); (*inv.hash())[key] = ar; }
                    else (*inv.hash())[key] = val;
                } else {
                    if (it->second.t != VT::Array) { Value ar = Value::array(); ar.arr()->push_back(it->second); it->second = ar; }
                    // …and a LIST value becomes an Array too — `:b(2, 3)` then
                    // `.append(:b<Y>)` is `[2, 3, "Y"]`, an Array (S32-hash/push.t);
                    // pushing onto the List left it a List.
                    else if (it->second.isList) { Value ar = Value::array(); for (auto& x : *it->second.arr()) ar.arr()->push_back(x); it->second = ar; }
                    if (m == "append") for (auto& x : val.flatten()) it->second.arr()->push_back(x);
                    else it->second.arr()->push_back(val);
                }
            }
            return inv;
        }
        if (m == "keys") {
            Value out = Value::array();
            // Set/Bag/Mix recover the element's original type from the count's pairKey.
            if (inv.t == VT::Hash) { for (auto& kv : *inv.hash()) out.arr()->push_back(hashEntryKey(inv, kv.first, kv.second)); }
            else for (size_t i = 0; i < items.size(); i++) out.arr()->push_back(Value::integer((long long)i));
            out.isList = true;
            return out;
        }
        if (m == "invert" && inv.t == VT::Array) { // (a=>1, b=>2).invert -> Seq of value=>key
            Value out = Value::array(); out.isList = true; out.s = "Seq";
            auto push1 = [&](const Value& v, const Value& k) {
                Value p = Value::pair(v.toStr(), k);
                if (v.t != VT::Str) p.pairKeyM() = std::make_shared<Value>(v); // keep a numeric key numeric
                out.arr()->push_back(std::move(p));
            };
            for (auto& e : items) if (e.t == VT::Pair) {
                Value val = e.pairVal() ? *e.pairVal() : Value::any();
                Value key = e.pairKey() ? *e.pairKey() : Value::str(e.s);
                if (val.t == VT::Array && val.arr()) for (auto& vv : *val.arr()) push1(vv, key);
                else push1(val, key);
            }
            return out;
        }
        if (m == "invert" && inv.t == VT::Hash) { // %h.invert -> list of (value => key)
            Value out = Value::array(); out.isList = true; out.s = "Seq";
            auto push1 = [&](const Value& v, const Value& k) {
                Value p = Value::pair(v.toStr(), k);
                if (v.t != VT::Str) p.pairKeyM() = std::make_shared<Value>(v); // a numeric value stays a numeric key
                out.arr()->push_back(std::move(p));
            };
            for (auto& kv : *inv.hash()) {
                Value key = hashEntryKey(inv, kv.first, kv.second);
                if (kv.second.t == VT::Array && kv.second.arr())
                    for (auto& v : *kv.second.arr()) push1(v, key);
                else push1(kv.second, key);
            }
            return out;
        }
        if ((m == "categorize-list" || m == "classify-list") && inv.t == VT::Hash) {
            // %h.categorize-list(mapper, values, :&as) — mutate %h in place (shared
            // container) and return it. mapper: Callable → mapper(v); Hash → lookup;
            // Array → index. categorize: the result is a LIST of categories (each a
            // key, or a key-PATH array for nested classification); classify: one.
            if (inv.hashKind == "Bag" || inv.hashKind == "Mix" || inv.hashKind == "Set")
                throw RakuError{Value::typeObj("X::Immutable"),
                                "Cannot call " + m + " on an immutable " + inv.hashKind};
            bool baggy = inv.hashKind == "BagHash" || inv.hashKind == "MixHash";
            Value asF, mapper; bool haveMapper = false; ValueList vals;
            for (auto& a2 : args) {
                if (a2.t == VT::Pair && a2.s == "as" && a2.pairVal()) { asF = *a2.pairVal(); continue; }
                if (!haveMapper) { mapper = a2; haveMapper = true; continue; }
                if (a2.t == VT::Array && a2.ext())
                    throw RakuError{Value::typeObj("X::Cannot::Lazy"), "Cannot " + m + " a lazy list"};
                if (a2.t == VT::Range || a2.t == VT::Array) { for (auto& x : a2.flatten()) vals.push_back(x); }
                else vals.push_back(a2);
            }
            int runMode = 0; // 0 unset, 1 flat keys, 2 nested key-paths (mixing throws)
            for (auto& v : vals) {
                Value cat;
                if (mapper.t == VT::Code) cat = callCallable(mapper, ValueList{v});
                else if (mapper.t == VT::Hash) {
                    if (!mapper.hash()) continue;
                    auto f = mapper.hash()->find(v.toStr());
                    if (f == mapper.hash()->end()) continue;
                    cat = f->second;
                }
                else if (mapper.t == VT::Array) {
                    long long i = v.toInt();
                    if (!mapper.arr() || i < 0 || (size_t)i >= mapper.arr()->size()) continue;
                    cat = (*mapper.arr())[i];
                }
                else continue;
                if (cat.t == VT::Nil || cat.t == VT::Any) continue; // Nil category: skip the value
                ValueList cats;
                if (m == "categorize-list" && cat.t == VT::Array) {
                    if (!cat.arr() || cat.arr()->empty()) continue;
                    cats = *cat.arr();
                } else cats.push_back(cat);
                Value sv = asF.t == VT::Code ? callCallable(asF, ValueList{v}) : v;
                for (auto& c : cats) {
                    int mode = c.t == VT::Array ? 2 : 1;
                    if (runMode == 0) runMode = mode;
                    else if (runMode != mode)
                        throw RakuError{Value::typeObj("X::Invalid::ComputedValue"),
                            m + " mapper on " + inv.typeName() + " cannot produce mixed-level keys"};
                    if (mode == 2 && baggy)
                        throw RakuError{Value::typeObj("X::Invalid::ComputedValue"),
                            m + " mapper on " + inv.typeName() + " cannot produce multi-level keys"};
                    if (mode == 1) {
                        Value& slot = (*inv.hash())[c.toStr()];
                        if (baggy) {
                            // A MixHash's weights are Real, not necessarily Num:
                            // adding one to an Int weight (or to nothing) keeps
                            // the Int, so the mix reads `"cat2" => 2`, not
                            // `2e0` (roast S32-list/classify-list.t).
                            if (inv.hashKind == "BagHash" ||
                                slot.t == VT::Int || !slot.isNumeric())
                                slot = Value::integer((slot.t == VT::Int ? slot.i : 0) + 1);
                            else
                                slot = applyArith("+", slot, Value::integer(1));
                        } else {
                            if (slot.t != VT::Array || !slot.arr()) { slot = Value::array(); slot.itemized = true; }
                            slot.arr()->push_back(sv);
                        }
                    } else { // key path: descend/autovivify nested hashes, push at the leaf
                        if (!c.arr() || c.arr()->empty()) continue;
                        // mutable cursor for the descent; the copy shares inv's
                        // hash shared_ptr, so the autovivified writes still land in
                        // the caller's container (see the same pattern in Part3)
                        Value invLocal = inv;
                        Value* curH = &invLocal;
                        for (size_t k = 0; k + 1 < c.arr()->size(); k++) {
                            Value& slot = (*curH->hash())[(*c.arr())[k].toStr()];
                            if (slot.t != VT::Hash || !slot.hash()) { slot = Value::makeHash(); slot.itemized = true; }
                            curH = &slot;
                        }
                        Value& slot = (*curH->hash())[c.arr()->back().toStr()];
                        if (slot.t != VT::Array || !slot.arr()) { slot = Value::array(); slot.itemized = true; }
                        slot.arr()->push_back(sv);
                    }
                }
            }
            return inv;
        }
        if (m == "toggle" && inv.t == VT::Hash) { // Any.toggle works over .list
            Value lst = methodCall(inv, "list", ValueList{});
            return methodCall(lst, "toggle", args, rwArgs);
        }
        if (m == "antipairs" && inv.t == VT::Hash) { // (value => key) pairs, like invert
            Value out = Value::array(); out.isList = true; out.s = "Seq";
            for (auto& kv : *inv.hash()) {
                Value p = Value::pair(kv.second.toStr(), hashEntryKey(inv, kv.first, kv.second));
                if (kv.second.t != VT::Str) p.pairKeyM() = std::make_shared<Value>(kv.second); // numeric value -> numeric key
                out.arr()->push_back(std::move(p));
            }
            return out;
        }
        if (m == "pairs" || m == "kv" || m == "antipairs") {
            Value out = Value::array(); out.isList = true; out.s = "Seq";
            if (inv.t == VT::Hash) {
                for (auto& kv : *inv.hash()) {
                    Value key = hashEntryKey(inv, kv.first, kv.second);
                    if (m == "kv") { out.arr()->push_back(key); out.arr()->push_back(kv.second); }
                    // (Hash antipairs answered by its own arm above)
                    else { Value p = Value::pair(kv.first, kv.second);
                           // …and a STR key of an object hash needs recovering
                           // too: the payload indexes by identity (`Str|a`)
                           if (key.t != VT::Str) p.pairKeyM() = std::make_shared<Value>(std::move(key));
                           else p.s = key.s;
                           out.arr()->push_back(std::move(p)); }
                }
            } else {
                for (size_t i = 0; i < items.size(); i++) {
                    if (m == "kv") { out.arr()->push_back(Value::integer((long long)i)); out.arr()->push_back(items[i]); }
                    else if (m == "antipairs") { // value => index
                        Value p = Value::pair(items[i].toStr(), Value::integer((long long)i));
                        p.pairKeyM() = std::make_shared<Value>(items[i]);
                        out.arr()->push_back(p);
                    }
                    else {
                        Value p = Value::pair(std::to_string(i), items[i]);
                        p.pairKeyM() = std::make_shared<Value>(Value::integer((long long)i)); // Int keys
                        out.arr()->push_back(p);
                    }
                }
            }
            return out;
        }
        // mutators on real arrays
        if (inv.t == VT::Array && inv.arr()) {
            // a List (or Slip) has a fixed size — the resizing mutators die.
            // Rakudo's message names 'List' even for a Slip invocant; splice is
            // a DISPATCH failure there (List has no splice candidates, only the
            // proto), so its message differs. Seq/Uni/Junction/Capture-tagged
            // arrays are excluded — they have their own (unfixed) stories.
            if (inv.isList && inv.enumName.empty() &&
                (inv.s.empty() || inv.s == "Slip")) {
                if (m == "push" || m == "append" || m == "pop" || m == "unshift" ||
                    m == "prepend" || m == "shift")
                    throwTyped("X::Immutable", {{"method", m}, {"typename", "List"}},
                        "Cannot call '" + m + "' on an immutable 'List'");
                if (m == "splice") {
                    std::string sig = "splice(List:D";
                    for (auto& x : args)
                        sig += ", " + x.typeName() +
                               (x.t == VT::Type || x.t == VT::Any || x.t == VT::Nil ? ":U" : ":D");
                    throw RakuError{Value::typeObj("X::Multi::NoMatch"),
                        "Cannot resolve caller " + sig + "); Routine does not have any candidates.  Is only the proto defined?"};
                }
            }
            // a native-typed array (`my str @a`, `my int @a`) rejects a value of the
            // wrong native kind — str takes Str, int/uint/byte take Int, num takes Real
            auto natCheck = [&](const Value& v) {
                if (inv.ofType().empty() || v.t == VT::Nil) return; // a Nil RESETS, it is not a store
                std::string bt = inv.ofType().substr(0, inv.ofType().find(','));
                bool isNat = bt == "str" || bt == "byte" || bt == "atomicint" ||
                             bt.compare(0, 3, "int") == 0 ||
                             bt.compare(0, 4, "uint") == 0 || bt.compare(0, 3, "num") == 0;
                if (!isNat) return; // boxed-type arrays keep their existing behaviour
                // …and a mixin over a Str (`"bar" but Type<words>`, highlighter's
                // tagged needle) is a Str with a role on it: its box is the string
                bool ok = bt == "str" ? (v.t == VT::Str || v.isAllomorph() || // an allomorph's Str side
                                         (v.t == VT::Object && v.obj() && v.obj()->hasBoxed &&
                                          v.obj()->boxed.t == VT::Str))
                        : bt.compare(0, 3, "num") == 0 ? v.isNumeric()
                        : (v.t == VT::Int || v.t == VT::Bool);
                if (!ok) throw RakuError{Value::typeObj("X::TypeCheck::Binding"),
                    "Type check failed in binding; expected " + bt + " but got " + v.typeName() + " (" + typeCheckRepr(v) + ")"};
            };
            // a shaped array (`my @a[2;2]`) has fixed dimensions — size-changing
            // operations are illegal
            if (inv.shape() && !inv.shape()->empty()) {
                static const std::set<std::string> fixedIllegal = {
                    "push", "append", "pop", "unshift", "prepend", "shift",
                    "splice", "reverse", "rotate"};
                if (fixedIllegal.count(m))
                    throw RakuError{Value::typeObj("X::IllegalOnFixedDimensionArray"),
                        "Cannot " + m + " a fixed-dimension array"};
            }
            // a BOXED typed array (`my Int @a`, `has Str @.d`) checks the same
            // way — that is the half natCheck left out, and it is why
            // `has Str @.data` silently accepted Ints (issue #63).
            const std::string boxedElem = elemTypeOf(inv);
            // Both checks see the value that ACTUALLY LANDS, so they run per
            // arm, after append/prepend's one-level flattening: `my int @a;
            // @a.append([1,2])` appends two ints and is legal (natCheck read
            // the raw argument and rejected the Array), while `my Int @a;
            // @a.push([1,2])` stores the Array itself and is not.
            auto elemCheck = [&](const Value& v) {
                natCheck(v);
                if (!boxedElem.empty()) checkElemType(boxedElem, v, "");
            };
            // P3 (the no-crash contract): in parallel mode the structural
            // mutators below run under the array's stripe — unguarded
            // concurrent pushes are still a race for the USER's data (loss is
            // allowed), but vector growth can no longer corrupt the runtime.
            Interpreter::ParStripe mutStripe(*this, inv.arr());
            // a native-int element array (`uint32 @W`) wraps each stored value to
            // its bit width (SHA1's `@W.push: S(...)` relies on uint32 overflow)
            auto natMask = [&](Value v) -> Value {
                bool sign; int bits = Value::natWidthOfType(inv.ofType(), sign);
                if (bits > 0 && bits < 64 && (v.t == VT::Int || v.t == VT::Bool)) {
                    unsigned long long u = (unsigned long long)v.toInt() & ((1ULL << bits) - 1);
                    long long x = (sign && (u & (1ULL << (bits - 1)))) ? (long long)u - (long long)(1ULL << bits) : (long long)u;
                    return Value::integer(x);
                }
                return v;
            };
            // a stored Nil resets to the element default, as assignment does
            auto elemDef = [&](const Value& v) -> Value {
                if (v.t != VT::Nil) return v;
                if (inv.pairVal()) return *inv.pairVal();
                if (!inv.ofType().empty()) { bool sg; int b = Value::natWidthOfType(inv.ofType(), sg);
                    if (b > 0) return Value::integer(0);
                    std::string f = inv.ofType().substr(0, inv.ofType().find(','));
                    if (!f.empty() && ascii::isupper((unsigned char)f[0])) return Value::typeObj(f); }
                return Value::any();
            };
            // push/unshift add each argument as one element; append/prepend flatten
            // A SLIP argument SLIPS: `@a.push(Empty)` adds nothing and
            // `@a.push(slip(7, 8))` adds two elements, where an ordinary list
            // argument (`()`, `[]`) is one. append/prepend already flatten a
            // lone Positional, so only push/unshift had to say so — they
            // stored the Slip itself, which left `@a.push: Empty` one element
            // longer than Rakudo (roast S02-types/undefined-types.t) and, once
            // a typed array started checking its elements, made an Int array
            // reject the Empty it should have ignored.
            auto slipped = [](const ValueList& in) -> ValueList {
                bool any = false;
                for (auto& x : in) if (x.t == VT::Array && x.s == "Slip" && !x.itemized) { any = true; break; }
                if (!any) return in;
                ValueList out;
                for (auto& x : in)
                    if (x.t == VT::Array && x.s == "Slip" && !x.itemized) {
                        if (x.arr()) for (auto& y : *x.arr()) out.push_back(y);
                    }
                    else out.push_back(x);
                return out;
            };
            if (m == "push") { for (auto& a : slipped(args)) { Value v = natMask(a); elemCheck(v); inv.arr()->push_back(elemDef(v)); } return inv; } // returns the array (shared storage)
            // append/prepend follow the single-argument rule: a lone Positional arg is
            // treated as the list of values (flattened one level); multiple args are each
            // added as-is (nested lists preserved, exactly like push).
            auto appendValues = [](ValueList& args) -> ValueList {
                // …but an ITEMIZED array is one item, not a list to flatten:
                // `@paths.append($[|@prefix, $node])` adds ONE path, which is how
                // ML::TriesWithFrequencies gathers its root-to-leaf paths.
                // (`push` never had to say so — it flattens nothing.)
                if (args.size() == 1 && args[0].itemized) return args;
                if (args.size() == 1 && isMultiDimShaped(args[0]))
                    return shapedLeaves(args[0]);   // a shaped array appends its leaves
                if (args.size() == 1 && args[0].t == VT::Array && args[0].arr())
                    return *args[0].arr();   // one-level: the sole list's own elements
                // …and a sole RANGE contributes its VALUES, by the same
                // single-argument rule. `my Int @x; @x.append: $from .. $to` is
                // how Text::CSV builds a column range, and appending the Range
                // itself failed the element type check.
                if (args.size() == 1 && args[0].t == VT::Range) return args[0].flatten();
                // …and a sole HASH contributes its PAIRS — a Hash is Iterable,
                // so the single-argument rule spreads it exactly as it spreads
                // a list: `@a.append(%h)` adds one element per key (LA-22).
                if (args.size() == 1 && args[0].t == VT::Hash && args[0].hash() &&
                    (args[0].hashKind.empty() || args[0].hashKind == "Map")) {
                    ValueList out;
                    for (auto& kv : *args[0].hash()) {
                        Value p = Value::pair(kv.first, kv.second);
                        p.pairKeyM() = kv.second.pairKey();
                        out.push_back(p);
                    }
                    return out;
                }
                return args;               // 2+ args: each as-is
            };
            // An ENDLESS argument can never finish being appended: Rakudo
            // refuses `append` with X::Cannot::Lazy (sheet LA-22), and this
            // extends the same refusal to `prepend`, where Rakudo 2026.08
            // simply hangs — a hang is not a behaviour worth imitating
            // (REVIEW-GRAND's precedent for choosing the sane answer).
            // `push` is exempt: it adds the Range itself, unflattened.
            if (m == "append" || m == "push" || m == "prepend")
                for (auto& a : args)
                    if (isEndlessLazy(a) ||
                        (a.t == VT::Range && !a.rNum() && a.rTo() >= 9000000000000000000LL &&
                         (m != "push")))   // push adds the Range itself, unflattened
                        throwTyped("X::Cannot::Lazy", {{"action", m.s}},
                                   "Cannot " + m + " a lazy list onto an Array");
            if (m == "append") { for (auto& a : appendValues(args)) { elemCheck(a); inv.arr()->push_back(elemDef(a)); } return inv; }
            if (m == "unshift") { ValueList u; for (auto& a : slipped(args)) { elemCheck(a); u.push_back(elemDef(a)); }
                                  inv.arr()->insert(inv.arr()->begin(), u.begin(), u.end()); return inv; }
            if (m == "prepend") { auto f = appendValues(args); for (auto& a : f) { elemCheck(a); a = elemDef(a); }
                                  inv.arr()->insert(inv.arr()->begin(), f.begin(), f.end()); return inv; }
            // popping/shifting an EMPTY Array yields a FAILURE, not a bare
            // undefined value: it boolifies False — so `while @a.shift -> $x`
            // terminates, which is how Cro's router drains its handler queue —
            // but detonates with X::Cannot::Empty the moment the value is USED.
            if (m == "pop" || m == "shift") {
                for (auto& av : args)
                    if (av.t != VT::Pair)
                        throw RakuError{Value::typeObj("X::Multi::NoMatch"),
                            "Cannot resolve caller " + m.s + "(Array:D, " + av.typeName() + ") — " + m.s + " takes no argument"};
                if (inv.arr()->empty()) {
                    Value f = rakuppNewFailure();
                    (*f.hash())["exception"] = makeTypedEx("X::Cannot::Empty",
                        {{"action", Value::str(m)}, {"what", Value::str("Array")}},
                        "Cannot " + m + " from an empty Array");
                    (*f.hash())["message"] = Value::str("Cannot " + m + " from an empty Array");
                    return f;
                }
                Value v = m == "pop" ? inv.arr()->back() : inv.arr()->front();
                if (m == "pop") inv.arr()->pop_back(); else inv.arr()->erase(inv.arr()->begin());
                if (v.t == VT::Array) v.itemized = true;
                // a HOLE pops or shifts as the container's default, the same
                // value reading that slot would have given (sheet LA-23)
                if (v.t == VT::Any && inv.elemDefault()) return *inv.elemDefault();
                return v;
            }
            // `.grab` is `.pick` that CONSUMES: the drawn elements leave the
            // array. A bare grab answers one element (Nil on an empty array);
            // `.grab($n)`, `.grab(*)` and `.grab({…})` answer a Seq of up to
            // that many (sheet LA-25).
            if (m == "grab") {
                if (inv.ext() && isEndlessLazy(inv))
                    throwTyped("X::Cannot::Lazy", {{"action", "grab"}},
                               "Cannot grab from a lazy list");
                bool one = args.empty();
                bool all = !one && (args[0].t == VT::Whatever ||
                                    (args[0].t == VT::Type && args[0].s == "Whatever") ||
                                    (args[0].isNumeric() && std::isinf(args[0].toNum())));
                long long have = (long long)inv.arr()->size();
                long long n = one ? 1 : all ? have
                            : args[0].t == VT::Code
                                ? callCallable(args[0], ValueList{Value::integer(have)}).toInt()
                                : args[0].toInt();
                Value out = Value::seq();
                for (long long k = 0; k < n && !inv.arr()->empty(); k++) {
                    size_t j = (size_t)(randDouble() * inv.arr()->size());
                    out.arr()->push_back((*inv.arr())[j]);
                    inv.arr()->erase(inv.arr()->begin() + j);
                }
                if (one) return out.arr()->empty() ? Value::nil() : (*out.arr())[0];
                return out;
            }
            if (m == "splice") { // .splice($start?, $count?, *@replacement) → the removed elements
                // a lazy array only holds a prefix — materialize enough to cover the window
                if (inv.ext()) {
                    long s0 = args.size() > 0 ? args[0].toInt() : 0;
                    materializeLazy(inv, args.size() > 1 ? (size_t)(std::max(0L, s0) + args[1].toInt()) : 1000000);
                }
                long n = (long)inv.arr()->size();
                // `*-2` / `{ $_ - 2 }` resolve against the length, like .head/.tail
                // do — .toInt() on a WhateverCode is 0, so `splice(*-2, *-1)` was
                // splicing nothing at the front.
                auto resolve = [&](const Value& a, long long sz) -> long {
                    if (a.t == VT::Whatever) return (long)sz;
                    if (a.t == VT::Code) { ValueList one{Value::integer(sz)}; return (long)callCallable(const_cast<Value&>(a), one).toInt(); }
                    return (long)a.toInt();
                };
                long start = args.size() > 0 ? resolve(args[0], n) : 0;
                // Both arguments are VALIDATED, and a bad one is THROWN, not
                // clamped: an offset outside 0..elems and a negative size are
                // each X::OutOfRange, named for the argument (sheet LA-24).
                if (start < 0 || start > n)
                    throwTypedV("X::OutOfRange",
                        {{"what", Value::str("Offset argument to splice")},
                         {"got", Value::integer(start)},
                         {"range", Value::str("0.." + std::to_string(n))}},   // Rakudo's is a Str
                        "Offset argument to splice out of range. Is: " + std::to_string(start) +
                            ", should be in 0.." + std::to_string(n));
                // the COUNT resolves against what is left after the start
                long count = args.size() > 1 ? resolve(args[1], n - start) : (n - start);
                if (count < 0)
                    throwTypedV("X::OutOfRange",
                        {{"what", Value::str("Size argument to splice")},
                         {"got", Value::integer(count)},
                         {"range", Value::str("0..^" + std::to_string(n - start))}},
                        "Size argument to splice out of range. Is: " + std::to_string(count) +
                            ", should be in 0..^" + std::to_string(n - start));
                count = std::min(count, n - start);   // a size past the end clamps
                Value removed = Value::array(); // the removed elements are an Array
                removed.ofTypeM() = inv.ofType(); // …of the SAME type as the source
                for (long k = 0; k < count; k++) removed.arr()->push_back((*inv.arr())[start + k]);
                ValueList repl;
                for (size_t k = 2; k < args.size(); k++) {
                    // From 6.e an ITEMIZED array argument goes in whole: the
                    // point of writing `$[8, 9]` is to insert one element that
                    // happens to be an array, and before 6.e there was no way to
                    // say it — every replacement flattened.
                    if (sixE() && args[k].t == VT::Array && args[k].itemized) { repl.push_back(args[k]); continue; }
                    // a Hash is ONE replacement element in every language version,
                    // itemized or not: `@a.splice(1, 0, %h)` inserts the whole hash
                    // (only `|%h` flattens it — and as NAMED args, which splice
                    // ignores). Flattening a bare `%h` to its pairs scattered the
                    // record across the array — Crane's positional `add` splices a
                    // `$value` holding a Hash exactly this way.
                    if (args[k].t == VT::Hash) { repl.push_back(args[k]); continue; }
                    // an ENDLESS replacement can never be spliced in
                    if (isEndlessLazy(args[k]) ||
                        (args[k].t == VT::Range && !args[k].rNum() &&
                         args[k].rTo() >= 9000000000000000000LL))
                        throwTyped("X::Cannot::Lazy", {{"action", "splice in"}},
                                   "Cannot splice in a lazy list");
                    for (auto& x : toList(args[k])) repl.push_back(x);
                }
                // a typed array checks its REPLACEMENTS before any of them lands
                // — a splice that dies leaves the array untouched — and says
                // so with its own exception (Rakudo: X::TypeCheck::Splice).
                // A NATIVE element type is a separate story that stays as it
                // is: Rakudo rejects it as an unboxing failure (X::AdHoc,
                // "This type cannot unbox to a native integer"), not as a
                // splice type check, and rakupp does not reject it at all.
                if (std::string want = elemTypeOf(inv); !want.empty())
                    for (auto& x : repl) {
                        if (x.t == VT::Nil || typeOrSubsetMatches(x, want)) continue;
                        throwTypedV("X::TypeCheck::Splice",
                            {{"got", x}, {"expected", Value::typeObj(want)}},
                            "Type check failed in splice; expected " + want +
                                " but got " + x.typeName() + " (" + typeCheckRepr(x) + ")");
                    }
                inv.arr()->erase(inv.arr()->begin() + start, inv.arr()->begin() + start + count);
                inv.arr()->insert(inv.arr()->begin() + start, repl.begin(), repl.end());
                return removed;
            }
        }
        if (inv.t == VT::Hash && inv.hash()) {
            if (m == "exists") return Value::boolean(inv.hash()->count(a0().toStr()) > 0);
        }
    }

    // an undefined scalar still reports as a 1-item list for .elems (Any.elems == 1)
    if ((inv.t == VT::Any || inv.t == VT::Nil) && m == "elems") return Value::integer(1);
    // Any.* single-item list semantics: a scalar answers the list-y methods
    // as a one-element list (Rakudo's Any fallbacks): 5.sum == 5, "x".join eq "x"
    if (inv.t == VT::Int || inv.t == VT::Num || inv.t == VT::Rat || inv.t == VT::Str ||
        inv.t == VT::Bool || inv.t == VT::Complex || inv.t == VT::Match) {
        if (m == "join") return Value::str(inv.toStr());
        if (m == "sum") return inv.isNumeric() ? inv : Value::number(inv.toNum());
        if (m == "min" || m == "max") return inv;
        if (m == "minmax") {
            long long v = inv.toInt();
            return Value::range(v, v, false, false);
        }
        if (m == "expmod" && args.size() >= 2) { // modular exponentiation (bigint-safe)
            BigInt base = inv.big() ? *inv.big() : BigInt(inv.toInt());
            BigInt e = args[0].big() ? *args[0].big() : BigInt(args[0].toInt());
            BigInt mod = args[1].big() ? *args[1].big() : BigInt(args[1].toInt());
            if (mod.isZero()) return Value::integer(0);
            auto modOf = [&](const BigInt& x) { BigInt q, r; BigInt::divmod(x, mod, q, r); if (r.sign < 0) r = r + mod; return r; };
            // A NEGATIVE exponent asks for the modular inverse raised to |e|
            // (Math::NumberTheory's modular-inverse is `power-mod($k, -1, $n)`).
            // Halving a negative `e` in the square-and-multiply below just ran the
            // |e| = 1 case and answered the base itself.
            if (e.sign < 0) {
                if (mod.abs().fitsLL() && mod.abs().toLL() == 1) return Value::integer(0);
                // extended Euclid over the residue: x with (base·x) ≡ 1 (mod m)
                BigInt m = mod.abs(), a = modOf(base);
                BigInt old_r = a, r0 = m, old_s(1), s0(0);
                while (!r0.isZero()) {
                    BigInt q, rem; BigInt::divmod(old_r, r0, q, rem);
                    old_r = r0; r0 = rem;
                    BigInt ns = old_s - q * s0; old_s = s0; s0 = ns;
                }
                if (!(old_r.fitsLL() && old_r.toLL() == 1))
                    throw RakuError{Value::typeObj("X::AdHoc"),
                                    "expmod: " + base.toString() + " has no inverse modulo " +
                                    mod.toString()};
                BigInt q, inv; BigInt::divmod(old_s, m, q, inv);
                if (inv.sign < 0) inv = inv + m;
                base = inv;
                e = e.abs();
            }
            BigInt result(1), b = modOf(base);
            // square-and-multiply over e's bits (via halving)
            BigInt two(2), cur = e;
            while (!cur.isZero()) {
                BigInt q, r; BigInt::divmod(cur, two, q, r);
                if (!r.isZero()) result = modOf(result * b);
                b = modOf(b * b);
                cur = q;
            }
            return result.fitsLL() ? Value::integer(result.toLL()) : Value::bigint(result);
        }
    }
    // $x.take — the method form of take
    if (m == "take") {
        if (tctx_.gatherStack.empty())
            throw RakuError{Value::typeObj("X::ControlFlow"), "take without gather"};
        auto& coll = *tctx_.gatherStack.back();
        coll.push_back(inv);
        size_t lim = tctx_.gatherLimits.empty() ? 0 : tctx_.gatherLimits.back();
        if (lim && coll.size() >= lim) throw StopGatherEx{};
        return inv;
    }
    if (m == "pick" || m == "roll") {
        if (inv.t == VT::Type && inv.s == "Order") { // built-in enum: its three values
            ValueList vs;
            for (auto& nv : {std::pair<const char*, int>{"Less", -1}, {"Same", 0}, {"More", 1}}) {
                Value e = Value::enumVal(nv.first, nv.second); e.enumType = "Order"; vs.push_back(e);
            }
            Value l = Value::array(vs); l.isList = true;
            return methodCall(l, m, args);
        }
        if (inv.t != VT::Type) { // any scalar picks from a one-element pool: 42.pick == 42
            Value l = Value::array({inv}); l.isList = true;
            return methodCall(l, m, args);
        }
    }
    // Cool list methods on a scalar treat it as a one-element list:
    // 5.unique is (5,), 5.permutations is ((5,),), 5.classify{…} groups the one
    // element. Whitelisted so a genuine typo still errors.
    if (inv.t == VT::Int || inv.t == VT::Num || inv.t == VT::Rat || inv.t == VT::Str ||
        inv.t == VT::Bool || inv.t == VT::Complex) {
        static const std::set<std::string> listCool = {
            "unique", "squish", "repeated", "permutations", "combinations",
            "classify", "categorize", "rotor", "batch",
            "chrs", // `(65).chrs` is "A" — one codepoint is a one-element list
        };
        if (listCool.count(m)) {
            // toList keeps a plain scalar as one item but expands a Blob/Buf to
            // its bytes (`$blob.rotor(3, :partial)` in Base64 chunks byte-wise)
            Value l = Value::array(); *l.arr() = toList(inv); l.isList = true;
            return methodCall(l, m, std::move(args), rwArgs);
        }
    }
    // Real numification protocol: built-in numerics answer .Bridge with a Num
    if (m == "Bridge" && (inv.t == VT::Int || inv.t == VT::Num || inv.t == VT::Rat || inv.t == VT::Bool))
        return Value::number(inv.toNum());
    // `has $.b handles *` — the CATCH-ALL delegation, and only that: it is a
    // fallback for names nothing else answers, so it belongs at the end of the
    // ladder. Named delegations (`handles <m1 m2>`) are real methods and are
    // resolved up front, in methodCallPart2's user-object block.
    if (inv.t == VT::Object && inv.obj() && inv.obj()->cls) {
        for (ClassInfo* c = inv.obj()->cls.get(); c; c = c->parent.get()) {
            for (auto& a : c->attrs)
                for (auto& h : a.handles)
                    if (h == "*") {
                        auto ait = inv.obj()->attrs.find(a.name);
                        Value target = ait != inv.obj()->attrs.end() ? ait->second : Value::any();
                        // an unset typed attr delegates to its type object
                        if ((target.t == VT::Any || target.t == VT::Nil) && !a.type.empty())
                            target = Value::typeObj(a.type);
                        return methodCall(target, m, std::move(args), rwArgs);
                    }
        }
    }
    // Real-role bridge: an object whose class defines .Bridge (`class F does Real
    // { method Bridge() {…} }`) answers unknown methods through the bridged
    // value — .succ/.Int/.Bool/.sqrt/… all come from Real via the bridge.
    if (inv.t == VT::Object && inv.obj() && inv.obj()->cls && m != "Bridge") {
        if (Value* br = inv.obj()->cls->findMethod("Bridge")) {
            Value bv = invokeMethod(*br, inv, {});
            return methodCall(bv, m, std::move(args), rwArgs);
        }
    }
    // `.emit` / `.take` inside a supply/react block (or gather) act on the topic:
    // `.emit` == `emit $_`, `.take` == `take $_`. Cro emits its response with
    // `.emit; done;`. Only a fallback — `.done` is NOT routed (it is a real method
    // on Supplier/Channel/Promise, and Cro uses the `done` statement, not `.done`).
    if ((m == "emit" || m == "take") &&
        (!tctx_.tapStack.empty() || !reactStack_.empty() || !tctx_.gatherStack.empty())) {
        auto it = builtins_.find(m);
        if (it != builtins_.end()) { ValueList a2{inv}; return it->second(*this, a2); }
    }
    // Universal fallbacks from Mu/Any, reached only once nothing above claimed
    // the name — so a real .join/.Capture/.bless still wins.
    //
    // `.tree` of a non-Iterable — a scalar, a type object, Nil — is the value
    // itself: there is nothing to descend into (Nil-Any sheet NA-44).
    if (m == "tree" && inv.t != VT::Array && inv.t != VT::Range && inv.t != VT::Hash)
        return inv;
    // `.ACCEPTS` is the smartmatch's right-hand side asking about its left, so
    // `$x.ACCEPTS($y)` IS `$y ~~ $x`. Every built-in matcher (Bool, the
    // numerics, a type object, a Regex) has its own arm earlier; this is Mu's,
    // reached only when nothing claimed the name, and it is how an instance of
    // a user class answers at all — it used to be a missing method
    // (Nil-Any sheet NA-46). Nil keeps its own answer: roast's nil.t asserts
    // `Nil.ACCEPTS(Any) === Nil`, and the trailing Nil fallback gives it.
    if (m == "ACCEPTS" && !args.empty() && inv.t != VT::Nil)
        return smartmatchValue("~~", args[0], inv);
    // `Any.join` treats the invocant as the ONE-element list it is: `3.join("-")`
    // is "3", and the separator never gets a chance to appear.
    // …and a TYPE OBJECT joins to the empty string (with the uninitialized
    // warning strOf raises), exactly as the one-element list of it would:
    // `Any.join` is "" (Nil-Any sheet NA-41). It used to be a missing method.
    if (m == "join") {
        if (!args.empty() && (args[0].t == VT::Type || args[0].t == VT::Any || args[0].t == VT::Nil))
            throw RakuError{Value::typeObj("X::TypeCheck::Binding::Parameter"),
                "Type check failed in binding to parameter '$separator'; expected Str but got " +
                args[0].typeName() + " (" + args[0].gist() + ")"};
        return Value::str(strInStrContext(inv));
    }
    // `Any.Capture` unpacks a value into its parts: an undefined one has none,
    // a Complex is its :re/:im, a Blob its bytes, an object its public
    // attributes. Only a plain scalar has nothing to unpack, and Rakudo says so
    // rather than inventing an empty Capture.
    if (m == "Capture") {
        Value c = Value::array(); c.hashKind = "Capture"; c.itemized = true;
        if (inv.t == VT::Any || inv.t == VT::Nil || inv.t == VT::Type) return c;
        if (inv.t == VT::Complex) {
            c.arr()->push_back(Value::pair("im", Value::number(inv.im())));
            c.arr()->push_back(Value::pair("re", Value::number(inv.n)));
            return c;
        }
        if (inv.t == VT::Str && (inv.hashKind == "Buf" || inv.hashKind == "Blob"))
            { *c.arr() = inv.blobList(); return c; }
        // a Pair unpacks like any object, into its public attributes:
        // `('OH' => 'HAI').Capture` is `\(:key<OH>, :value<HAI>)` (Rakudo;
        // S02-types/capture.t reads `$c<key>`). It used to be the "cannot
        // unpack" error below.
        if (inv.t == VT::Pair) {
            Value k = Value::pair("key", Value::str(inv.s)); k.namedArg = true;
            Value v = Value::pair("value", inv.pairVal() ? *inv.pairVal() : Value::any()); v.namedArg = true;
            c.arr()->push_back(k); c.arr()->push_back(v);
            return c;
        }
        if (inv.t == VT::Object && inv.obj()) {
            std::vector<std::string> names;
            for (auto& kv : inv.obj()->attrs) names.push_back(kv.first);
            std::sort(names.begin(), names.end());
            for (auto& n : names) c.arr()->push_back(Value::pair(n, inv.obj()->attrs[n]));
            return c;
        }
        throw RakuError{Value::typeObj("X::Cannot::Capture"),
                        "Cannot unpack or Capture `" + inv.gist() + "`."};
    }
    // `.Failure` wraps the value in an unthrown Failure carrying it as the
    // payload of an X::AdHoc — `fail $x` spelled as a coercion. It lives on
    // Cool, and only on Cool: Roast's currying helpers ask `$code.can('Failure')`
    // to decide whether a Failure was mixed in, so answering for a Code made
    // every priming test take the failure branch.
    if (m == "Failure" &&
        (inv.isNumeric() || inv.t == VT::Str || inv.t == VT::Match ||
         inv.t == VT::Array || inv.t == VT::Range || inv.t == VT::Complex ||
         (inv.t == VT::Hash && (inv.hashKind.empty() || inv.hashKind == "Hash" || inv.hashKind == "Map")))) {
        Value f = rakuppNewFailure();
        (*f.hash())["exception"] = Value::typeObj("X::AdHoc");
        (*f.hash())["message"] = Value::str(strOf(inv));
        (*f.hash())["payload"] = inv;
        return f;
    }
    // `Mu.bless` on a built-in value makes a fresh, default instance of its type
    // — `3.bless` is 0, not "no such method".
    if (m == "bless") return methodCall(Value::typeObj(inv.typeName()), "new", std::move(args), rwArgs);
    return std::nullopt;   // not handled here — fall through to the caller's tail
}

} // namespace rakupp
