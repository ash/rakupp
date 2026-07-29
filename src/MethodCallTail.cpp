#include "MethodCallSegment.h"

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
    (void)rwArgs;
    if (m == "fmt" && inv.t == VT::Pair)
        return Value::str(doSprintf(args.empty() ? "%s\t%s" : a0().toStr(),
                                    {inv.pairKey ? *inv.pairKey : Value::str(inv.s),
                                     inv.pairVal ? *inv.pairVal : Value::any()}));
    if (m == "fmt" && inv.t != VT::Array && inv.t != VT::Range && inv.t != VT::Hash)
        return Value::str(doSprintf(args.empty() ? "%s" : a0().toStr(), {inv}));
    // Cool.printf / Cool.sprintf: the invocant IS the format ("%s\n".printf($x))
    if (m == "printf" && (inv.t == VT::Str || inv.t == VT::Match)) {
        std::cout << doSprintf(inv.toStr(), args, langRev_);
        return Value::boolean(true);
    }
    if (m == "sprintf" && (inv.t == VT::Str || inv.t == VT::Match))
        return Value::str(doSprintf(inv.toStr(), args, langRev_));
    // Str.parse-base($radix) — "ff".parse-base(16) == 255; fractions give a Rat
    if (m == "parse-base" && (inv.t == VT::Str || inv.t == VT::Match) && !args.empty()) {
        std::string s = inv.toStr(); long long base = a0().toInt();
        if (base < 2 || base > 36) return Value::typeObj("Failure");
        size_t i2 = 0; bool neg = false;
        if (i2 < s.size() && (s[i2] == '-' || s[i2] == '+')) { neg = s[i2] == '-'; i2++; }
        auto digval = [&](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'z') return c - 'a' + 10;
            if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
            return -1;
        };
        BigInt whole(0); bool any = false;
        for (; i2 < s.size() && s[i2] != '.'; i2++) {
            if (s[i2] == '_') continue;
            int d = digval(s[i2]);
            if (d < 0 || d >= base) return Value::typeObj("Failure");
            whole = whole * BigInt(base) + BigInt(d); any = true;
        }
        BigInt fnum(0), fden(1);
        if (i2 < s.size() && s[i2] == '.') {
            for (i2++; i2 < s.size(); i2++) {
                if (s[i2] == '_') continue;
                int d = digval(s[i2]);
                if (d < 0 || d >= base) return Value::typeObj("Failure");
                fnum = fnum * BigInt(base) + BigInt(d); fden = fden * BigInt(base); any = true;
            }
        }
        if (!any) return Value::typeObj("Failure");
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
            if (a.s == "overlap") overlap = !a.pairVal || a.pairVal->truthy();
            else if (a.s == "i" || a.s == "ignorecase") icase = !a.pairVal || a.pairVal->truthy();
            else if (a.s == "m" || a.s == "ignoremark") imark = !a.pairVal || a.pairVal->truthy();
        }
        // a second positional is the CHARACTER position to start looking from
        size_t from = 0;
        for (size_t i = 1; i < args.size(); i++)
            if (args[i].t != VT::Pair) { from = charToByte(s, args[i].toInt()); break; }
        if (imark) { s = markFold(s); needle = markFold(needle); }
        if (icase) {
            auto fold = [](const std::string& in) {
                std::string o; for (auto c : utf8cp(in)) o += cpToU8(toLowerCp(c)); return o;
            };
            s = fold(s); needle = fold(needle);
        }
        Value out = Value::array(); out.isList = true;
        // the answers are CHARACTER positions, not byte offsets
        auto charPos = [&](size_t byte) {
            long long n = 0;
            for (size_t k = 0; k < byte && k < s.size(); k++)
                if ((static_cast<unsigned char>(s[k]) & 0xC0) != 0x80) n++;
            return n;
        };
        if (!needle.empty() && from <= s.size())
            for (size_t p = s.find(needle, from); p != std::string::npos;
                 p = s.find(needle, p + (overlap ? 1 : needle.size())))
                out.arr->push_back(Value::integer(charPos(p)));
        return out;
    }
    // Str.chop($n = 1)
    // Real numbers are their own conjugate; a Cool number chops its string form.
    if (m == "conj" && (inv.t == VT::Int || inv.t == VT::Num || inv.t == VT::Rat || inv.t == VT::Bool)) return inv;
    // .lsb / .msb — least / most significant set bit of an Int (Nil for 0).
    if ((m == "lsb" || m == "msb") && (inv.t == VT::Int || inv.t == VT::Bool)) {
        // a BIG integer counts its bits by halving — 64 bits is not the limit
        if (inv.big && !inv.big->fitsLL()) {
            BigInt n = inv.big->abs(), two(2LL), q, r;
            if (n.isZero()) return Value::nil();
            long long lsb = -1, bit = 0;
            while (!n.isZero()) {
                BigInt::divmod(n, two, q, r);
                if (!r.isZero() && lsb < 0) lsb = bit;
                n = q; bit++;
            }
            return Value::integer(m == "lsb" ? lsb : bit - 1);
        }
        long long v = inv.toInt();
        if (v == 0) return Value::nil();
        unsigned long long u = v < 0 ? (unsigned long long)(-v) : (unsigned long long)v;
        return Value::integer(m == "lsb" ? rakupp::ctzll(u) : 63 - rakupp::clzll(u));
    }
    if (m == "chop" && (inv.t == VT::Int || inv.t == VT::Num || inv.t == VT::Rat || inv.t == VT::Complex))
        return methodCall(Value::str(inv.toStr()), "chop", std::move(args), rwArgs);
    if (m == "chop" && (inv.t == VT::Str || inv.t == VT::Match)) {
        auto cps = utf8cp(inv.toStr());
        long long n = args.empty() ? 1 : a0().toInt();
        if (n < 0) n = 0;
        std::string r;
        for (size_t k = 0; k + (size_t)n < cps.size(); k++) r += cpToUtf8(cps[k]);
        return Value::str(r);
    }
    // numeric .narrow — smallest type that holds the value exactly
    if (m == "narrow" && inv.isNumeric()) {
        if (inv.t == VT::Rat && inv.ratN && inv.ratD && inv.ratD->fitsLL() && inv.ratD->toLL() == 1)
            return Value::bigint(*inv.ratN);
        if (inv.t == VT::Num && !std::isinf(inv.n) && !std::isnan(inv.n) && inv.n == (long long)inv.n)
            return Value::integer((long long)inv.n);
        return inv;
    }
    // .UInt — Int coercion that fails on negatives
    if (m == "UInt") {
        // a non-numeric string is a FAILURE, not a silent 0 — same ladder as .Int
        if (inv.t == VT::Str || inv.t == VT::Match) {
            Value nv = numifyStrFailure(inv.toStr());
            if (nv.t == VT::Hash && nv.hashKind == "Failure") return nv;
        }
        long long v = inv.toInt();
        if (v < 0) return Value::typeObj("Failure");
        return Value::integer(v);
    }
    // Baggy.kxxv — every key repeated by its weight
    if (m == "kxxv" && inv.t == VT::Hash && inv.hash &&
        (inv.hashKind == "Bag" || inv.hashKind == "BagHash" || inv.hashKind == "Set" || inv.hashKind == "SetHash")) {
        Value out = Value::array(); out.isList = true;
        for (auto& kv : *inv.hash) {
            long long n = inv.hashKind[0] == 'S' ? 1 : kv.second.toInt();
            for (long long k = 0; k < n; k++) out.arr->push_back(Value::str(kv.first));
        }
        return out;
    }
    // Rat.base-repeating($radix) — (non-repeating part, repeating cycle)
    if (m == "base-repeating" && inv.t == VT::Rat && inv.ratN && inv.ratD && !args.empty()) {
        long long base = a0().toInt();
        if (base < 2 || base > 36) return Value::typeObj("Failure");
        auto digchr = [](int d) -> char { return d < 10 ? char('0' + d) : char('A' + d - 10); };
        BigInt n = inv.ratN->abs(), d = inv.ratD->abs();
        std::string sign = inv.ratN->sign < 0 ? "-" : "";
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
        out.arr->push_back(Value::str(whole + "." + fracDigits));
        out.arr->push_back(Value::str(cycle));
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
    if (inv.t == VT::Hash && inv.hash) {
        if (m == "AT-KEY" && !args.empty()) {
            auto it = inv.hash->find(args[0].toStr());
            return it != inv.hash->end() ? it->second : Value::any();
        }
        if (m == "EXISTS-KEY" && !args.empty())
            return Value::boolean(inv.hash->count(args[0].toStr()) > 0);
        if (m == "DELETE-KEY" && !args.empty()) {
            auto it = inv.hash->find(args[0].toStr());
            if (it == inv.hash->end()) return Value::any();
            Value v = it->second; inv.hash->erase(it); return v;
        }
        if ((m == "ASSIGN-KEY" || m == "BIND-KEY") && args.size() >= 2) {
            (*inv.hash)[args[0].toStr()] = args[1]; return args[1];
        }
    }
    if (inv.t == VT::Array && inv.arr) {
        if (m == "AT-POS" && !args.empty()) {
            long long i = args[0].toInt(), n = (long long)inv.arr->size();
            if (i < 0) i += n;
            return (i >= 0 && i < n) ? (*inv.arr)[i] : Value::any();
        }
        if (m == "EXISTS-POS" && !args.empty()) {
            long long i = args[0].toInt(), n = (long long)inv.arr->size();
            if (i < 0) i += n;
            return Value::boolean(i >= 0 && i < n);
        }
        if (m == "ASSIGN-POS" && args.size() >= 2) {
            long long i = args[0].toInt();
            if (i >= 0) {
                while ((long long)inv.arr->size() <= i) inv.arr->push_back(Value::any());
                (*inv.arr)[i] = args[1];
            }
            return args[1];
        }
    }
    if (inv.t == VT::Pair) {
        if (m == "Pair") return inv;   // .Pair on a Pair is itself
        if (m == "key") return inv.pairKey ? *inv.pairKey : Value::str(inv.s); // object/array keys preserved
        if (m == "value") return inv.pairVal ? *inv.pairVal : Value::any();
        if (m == "kv") { Value o = Value::array({inv.pairKey ? *inv.pairKey : Value::str(inv.s), inv.pairVal ? *inv.pairVal : Value::any()}); o.isList = true; return o; }
        if (m == "antipair") return Value::pair((inv.pairVal ? inv.pairVal->toStr() : ""), Value::str(inv.s));
        // `.freeze` snapshots the value out of its container. rakupp's Pair already
        // copies rather than binding, so the pair is frozen the moment it is built —
        // if Pair ever holds a real container this has to copy pairVal explicitly.
        if (m == "freeze") return inv;
        // `.Hash` / `.Map` on a Pair is the one-entry hash it describes
        if (m == "Hash" || m == "Map") {
            Value h = Value::makeHash();
            h.hashRef()[inv.s] = inv.pairVal ? *inv.pairVal : Value::any();
            if (m == "Map") h.hashKind = "Map";
            return h;
        }
        // A Pair is ONE element, so its list views are one long: `.keys` is the key,
        // not an index (that is what a Positional would answer).
        if (m == "keys" || m == "values" || m == "pairs") {
            Value o = Value::array(); o.isList = true;
            o.arr->push_back(m == "keys"   ? (inv.pairKey ? *inv.pairKey : Value::str(inv.s))
                           : m == "values" ? (inv.pairVal ? *inv.pairVal : Value::any())
                                           : inv);
            return o;
        }
        // `.antipairs`/`.invert` are the LIST forms of .antipair — and a list
        // VALUE inverts to one pair per element: `(a => (1,2)).invert` is
        // (1 => "a", 2 => "a")
        if (m == "antipairs" || m == "invert") {
            Value o = Value::array(); o.isList = true;
            Value val = inv.pairVal ? *inv.pairVal : Value::any();
            // a HASH value inverts to one pair per ENTRY, and the entry itself is the
            // new key: `:foo{ :42a, :72b }.invert` is ((:a(42)) => "foo", …)
            if (m == "invert" && val.t == VT::Hash && val.hash) {
                Value o2 = Value::array(); o2.isList = true;
                for (auto& kv : *val.hash) {
                    Value p = Value::pair("", Value::str(inv.s));
                    p.pairKey = std::make_shared<Value>(Value::pair(kv.first, kv.second));
                    o2.arr->push_back(std::move(p));
                }
                return o2;
            }
            ValueList vs = (m == "invert" && val.t == VT::Array && val.arr) ? *val.arr : ValueList{val};
            for (auto& v : vs) {
                Value p = Value::pair(v.toStr(), Value::str(inv.s));
                // a Str key needs no separate key VALUE — carrying one makes the
                // pair render as `"bar" => "foo"` instead of `:bar("foo")`
                if (v.t != VT::Str) p.pairKey = std::make_shared<Value>(v);
                o.arr->push_back(std::move(p));
            }
            return o;
        }
    }

    // scalar .Array / .List — a 1-element container: "LLL".Array is ["LLL"]
    if ((m == "Array" || m == "List") &&
        (inv.t == VT::Int || inv.t == VT::Num || inv.t == VT::Rat || inv.t == VT::Bool ||
         inv.t == VT::Str || inv.t == VT::Complex || inv.t == VT::Pair)) {
        Value one = Value::array(); one.arr->push_back(inv);
        one.isList = (m == "List");
        return one;
    }
    // A TYPE OBJECT is an EMPTY list, not a one-element one: `Num.pairs` is (),
    // `Range.reduce(&[+])` is Nil. (An INSTANCE of the same type is one element —
    // that is the branch just below.)
    if (inv.t == VT::Type) {
        // …but only for the KEY/VALUE family. `.map`/`.sort`/`.grep` still see a
        // ONE-element list (`Int.map({$_})` is `((Int))`) — a type object has no
        // ELEMENTS to pair up, yet it is still a single thing to iterate.
        static const std::set<std::string> emptyList = {
            "pairs", "antipairs", "kv", "keys", "values", "invert"};
        if (emptyList.count(m)) { Value o = Value::array(); o.isList = true; o.s = "Seq"; return o; }
        if (m == "reduce" || m == "produce") return Value::nil();
    }
    // list methods on a lone scalar treat it as a 1-element list: 42.grep(*>3), 'x'.map(...)
    // (a Code is one too — `(&say).kv` is `(0, &say)`)
    if (inv.t == VT::Code &&
        (m == "kv" || m == "pairs" || m == "antipairs" || m == "keys" || m == "values")) {
        Value one = Value::array(); one.arr->push_back(inv); one.isList = true;
        return methodCall(one, m, args, rwArgs);
    }
    if ((inv.t == VT::Int || inv.t == VT::Num || inv.t == VT::Rat || inv.t == VT::Bool ||
         inv.t == VT::Str || inv.t == VT::Complex || inv.t == VT::Pair ||
         inv.t == VT::Type) && // a type object is one item to ITERATE (see above)
        (m == "grep" || m == "map" || m == "first" || m == "sort" || m == "reverse" ||
         m == "flat" || m == "reduce" || m == "grep-index" || m == "first-index" || m == "Supply" ||
         m == "head" || m == "tail" || m == "skip" || m == "elems" || m == "end" ||
         m == "keys" || m == "values" || m == "kv" || m == "pairs" || m == "batch" ||
         m == "rotor" || m == "unique" || m == "squish" || m == "antipairs" ||
         m == "combinations" || m == "permutations")) {
        // toList keeps the scalar as one item, but a Blob/Buf expands to its
        // BYTES (`$blob.rotor(3, :partial)` in Base64 chunks byte-wise)
        Value one = Value::array(); *one.arr = toList(inv); one.isList = true;
        return methodCall(one, m, args, rwArgs);
    }

    // lazy list (infinite `… … *` or a lazy `.map` over one): keep `.map`/`.head`
    // lazy so consumers materialise only what they index.
    if (inv.t == VT::Array && inv.ext) {
        bool infinite = std::static_pointer_cast<LazySeqState>(inv.ext)->infinite;
        if (m == "is-lazy") return Value::boolean(true);
        if (infinite) {
            // operations that need the end of the list can't complete on an infinite source
            if (m == "elems" || m == "end" || m == "pop" || m == "tail" || m == "reverse" ||
                m == "sort" || m == "eager" || m == "List" || m == "Array" || m == "sum" ||
                m == "min" || m == "max" || m == "join" || m == "Str" || m == "gist")
                throw RakuError{Value::typeObj("X::Cannot::Lazy"), "Cannot " + m + " a lazy list onto an Array"};
            if (m == "shift") { materializeLazy(inv, 1); if (inv.arr->empty()) return Value::nil(); Value v = inv.arr->front(); inv.arr->erase(inv.arr->begin()); return v; }
        } else {
            // FINITE lazy (a gather that outgrew its probe, a lazy map over a finite
            // source, …): whole-list operations force full materialisation first,
            // so .elems/.sort/.join see every element, not just the cached prefix.
            static const std::set<std::string> forceAll = {
                "elems", "end", "pop", "tail", "reverse", "sort", "eager", "List", "Array",
                "sum", "min", "max", "minmax", "join", "Str", "gist", "raku", "perl",
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
            Interpreter* self = this;
            st->appendNext = [self, src, fn](ValueList& cache) -> bool {
                size_t si = cache.size();
                self->materializeLazy(src, si + 1);
                if (si >= src.arr->size()) return false;
                ValueList one{ (*src.arr)[si] };
                cache.push_back(self->callCallable(fn, one));
                return true;
            };
            out.ext = st;
            return out;
        }
        if (m == "grep" && !args.empty()) {
            // lazy filter: each appendNext pulls source elements (bounded per call)
            // until the predicate matches, so `(^Inf).grep(…).head(3)` terminates.
            Value pred = args[0], src = inv;
            Value out = Value::array(); out.isList = true;
            auto st = std::make_shared<LazySeqState>();
            auto spos = std::make_shared<size_t>(0); // next unexamined source index
            Interpreter* self = this;
            st->appendNext = [self, src, pred, spos](ValueList& cache) -> bool {
                for (long long tries = 0; tries < 1000000; tries++) { // bail on a never-matching predicate
                    self->materializeLazy(src, *spos + 1);
                    if (*spos >= src.arr->size()) return false;
                    Value v = (*src.arr)[(*spos)++];
                    bool match;
                    if (pred.t == VT::Code) {
                        self->topicWriteback_ = &(*src.arr)[*spos - 1]; // $_ mutations alias the element
                        try { match = self->callCallable(pred, {v}).truthy(); }
                        catch (LastEx&) { self->topicWriteback_ = nullptr; return false; } // `last` ends the grep
                        catch (NextEx&) { self->topicWriteback_ = nullptr; continue; }     // `next` skips
                        catch (RedoEx&) { self->topicWriteback_ = nullptr; (*spos)--; continue; } // `redo` retries
                        v = (*src.arr)[*spos - 1]; // keep the (possibly mutated) value
                    } else match = applyArith("~~", v, pred).truthy();
                    if (match) { cache.push_back(v); return true; }
                }
                return false;
            };
            out.ext = st;
            return out;
        }
        if (m == "first" && !args.empty()) { // first match, scanning lazily (bounded)
            bool wantK = false, wantKv = false, wantP = false; // :k index / :kv / :p forms
            for (auto& a : args) if (a.t == VT::Pair && a.pairVal && a.pairVal->truthy()) {
                if (a.s == "k") wantK = true;
                else if (a.s == "kv") wantKv = true;
                else if (a.s == "p") wantP = true;
            }
            Value pred; bool havePred = false;
            for (auto& a : args) if (a.t != VT::Pair) { pred = a; havePred = true; break; }
            for (size_t si = 0; si < 1000000; si++) {
                materializeLazy(inv, si + 1);
                if (si >= inv.arr->size()) break;
                Value v = (*inv.arr)[si];
                bool match = !havePred ? true
                           : pred.t == VT::Regex ? regexMatch(v.toStr(), pred.s).truthy()
                           : pred.t == VT::Code ? callCallable(pred, {v}).truthy()
                                                : applyArith("~~", v, pred).truthy();
                if (match) {
                    if (wantK) return Value::integer((long long)si);
                    if (wantP) return Value::pair(std::to_string(si), v);
                    if (wantKv) { Value o = Value::array(); o.isList = true;
                                  o.arr->push_back(Value::integer((long long)si));
                                  o.arr->push_back(v); return o; }
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
            Interpreter* self = this;
            st->appendNext = [self, src, n](ValueList& cache) -> bool {
                size_t si = cache.size() + (size_t)n;
                self->materializeLazy(src, si + 1);
                if (si >= src.arr->size()) return false;
                cache.push_back((*src.arr)[si]);
                return true;
            };
            out.ext = st;
            return out;
        }
        if (m == "head" && (args.empty() || args[0].t != VT::Whatever)) {
            size_t n = args.empty() ? 1 : (size_t)std::max(0LL, args[0].toInt());
            materializeLazy(inv, n);
            Value out = Value::array(); out.isList = true;
            if (args.empty()) return inv.arr->empty() ? Value::nil() : (*inv.arr)[0]; // scalar .head
            for (size_t i = 0; i < n && i < inv.arr->size(); i++) out.arr->push_back((*inv.arr)[i]);
            return out;
        }
    }

    if (inv.t == VT::Regex && m == "ACCEPTS") // returns the Match (or Nil), sets $/
        return regexMatch(args.empty() ? std::string() : args[0].toStr(), inv.s);

    // quanthash smartmatch: same support with equal weights (topic coerced to
    // `.set` / `.unset` exist on SetHash ONLY — Set.^can('set') and
    // BagHash.^can('set') are both False in Rakudo, and roast checks .^can.
    // Both take a single (possibly listy) positional and return Nil.
    if (inv.t == VT::Hash && inv.hash && inv.hashKind == "SetHash" &&
        (m == "set" || m == "unset")) {
        for (auto& a : args)
            for (auto& x : a.flatten()) {
                std::string k = baggyKeyStr(x);
                if (m == "set") { Value b = Value::boolean(true); b.pairKey = baggyKey(x); (*inv.hash)[k] = std::move(b); }
                else inv.hash->erase(k);
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
        if (!(other.t == VT::Hash && other.hash && qk.count(other.hashKind))) {
            ValueList items = other.flatten();
            bool mixK = inv.hashKind == "Mix" || inv.hashKind == "MixHash";
            bool bagK = inv.hashKind == "Bag" || inv.hashKind == "BagHash";
            other = makeBaggy(items, mixK ? "Mix" : bagK ? "Bag" : "Set", false);
        }
        auto wt = [](const Value& h, const std::string& k) -> double {
            auto it = h.hash->find(k);
            if (it == h.hash->end()) return 0.0;
            return it->second.t == VT::Bool ? (it->second.b ? 1.0 : 0.0) : it->second.toNum();
        };
        bool eq = true;
        if (!inv.hash || !other.hash) eq = (!inv.hash || inv.hash->empty()) && (!other.hash || other.hash->empty());
        else {
            for (auto& kv : *inv.hash)   if (wt(inv, kv.first) != wt(other, kv.first)) { eq = false; break; }
            if (eq) for (auto& kv : *other.hash) if (wt(inv, kv.first) != wt(other, kv.first)) { eq = false; break; }
        }
        return Value::boolean(eq);
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
        if (inv.hash && nv.hash) { *inv.hash = *nv.hash; return inv; }
        return nv;
    }
    // %h.Capture — a Capture whose named part is the hash's pairs
    if (inv.t == VT::Hash && m == "Capture" &&
        (inv.hashKind.empty() || inv.hashKind == "Map" ||
         inv.hashKind == "Set" || inv.hashKind == "SetHash" ||
         inv.hashKind == "Bag" || inv.hashKind == "BagHash" ||
         inv.hashKind == "Mix" || inv.hashKind == "MixHash")) {
        Value c = Value::array(); c.hashKind = "Capture"; c.itemized = true;
        if (inv.hash) for (auto& kv : *inv.hash) c.arr->push_back(Value::pair(kv.first, kv.second));
        return c;
    }
    // $obj.Capture — the object's public attributes as NAMED arguments, each
    // read through its accessor (a method may override the attribute's value)
    if (inv.t == VT::Object && inv.obj && inv.obj->cls && m == "Capture") {
        Value c = Value::array(); c.hashKind = "Capture"; c.itemized = true;
        std::set<std::string> seen;
        for (ClassInfo* ci = inv.obj->cls.get(); ci; ci = ci->parent.get())
            for (auto& at : ci->attrs) {
                if (!at.pub || !seen.insert(at.name).second) continue;
                Value v;
                try { v = methodCall(inv, at.name, {}); }
                catch (RakuError&) {
                    auto it = inv.obj->attrs.find(at.name);
                    if (it == inv.obj->attrs.end()) continue;
                    v = it->second;
                }
                c.arr->push_back(Value::pair(at.name, v));
            }
        std::sort(c.arr->begin(), c.arr->end(),
                  [](const Value& a, const Value& b) { return a.s < b.s; });
        return c;
    }
    // @a.Capture — elements become positional arguments, Pairs become named ones
    // (so the nameds sort to the back of the rendering, as in `\(2, :a(1))`)
    if (inv.t == VT::Array && inv.hashKind.empty() && m == "Capture") {
        ValueList items = toList(inv);
        Value c = Value::array(); c.hashKind = "Capture"; c.itemized = true;
        for (auto& e : items) if (e.t != VT::Pair) c.arr->push_back(e);
        for (auto& e : items) if (e.t == VT::Pair) c.arr->push_back(e);
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
    if (inv.t == VT::Range && m == "is-lazy")
        return Value::boolean(inv.b || inv.rTo >= 9000000000000000000LL); // `lazy 1..3` marks .b
    // finite-Range scalar accessors: endpoints (min/max ignore exclusivity), the
    // exclusion flags, and the integer-inclusive int-bounds.
    if (inv.t == VT::Range && inv.rTo < 9000000000000000000LL &&
        inv.rFrom > -9000000000000000000LL) {
        if (m == "excludes-min") return Value::boolean(inv.rExFrom);
        if (m == "excludes-max") return Value::boolean(inv.rExTo);
        if (m == "infinite")     return Value::boolean(false);
        // fractional ranges aren't integer-bounded, and neither is a Str range
        if (m == "is-int")       return Value::boolean(!inv.rNum && inv.ofType != "Str");
        // .min/.max/.bounds answer the endpoint OBJECTS when the range kept them
        // (`(1/2 .. 1/3).min` is a Rat, not the Int it iterates from)
        const RangeEnds* re = rangeEnds(inv);
        if (m == "min" && re) return re->from;
        if (m == "max" && re) return re->to;
        if (m == "min")
            return inv.ofType == "Str" ? Value::str(cpToU8((uint32_t)inv.rFrom))
                 : inv.rNum ? Value::number(inv.n)
                 : inv.rFrom <= -9000000000000000000LL ? Value::number(-INFINITY)
                 : Value::integer(inv.rFrom);
        if (m == "max")
            return inv.ofType == "Str" ? Value::str(cpToU8((uint32_t)inv.rTo))
                 : inv.rNum ? Value::number(inv.im)
                 : inv.rTo >= 9000000000000000000LL ? Value::number(INFINITY)
                 : Value::integer(inv.rTo);
        if (m == "bounds") {
            Value o = re ? Value::array({re->from, re->to})
                   : inv.ofType == "Str" ? Value::array({Value::str(cpToU8((uint32_t)inv.rFrom)),
                                                         Value::str(cpToU8((uint32_t)inv.rTo))})
                   : inv.rNum ? Value::array({Value::number(inv.n), Value::number(inv.im)})
                              : Value::array({Value::integer(inv.rFrom), Value::integer(inv.rTo)});
            o.isList = true; return o;
        }
        if (m == "int-bounds") {
            Value o = Value::array({Value::integer(inv.rFrom + (inv.rExFrom ? 1 : 0)),
                                    Value::integer(inv.rTo - (inv.rExTo ? 1 : 0))}); o.isList = true; return o;
        }
    }
    // an infinite range (…..Inf) must not materialise: only lazy views are defined
    // `$range.in-range($v)` — True when the value is inside, and otherwise
    // THROWS X::OutOfRange (Rakudo throws here; it does not hand back a soft
    // Failure, so even `.defined` on the result explodes)
    if (inv.t == VT::Range && m == "in-range" && !args.empty()) {
        if (applyArith("~~", args[0], inv).truthy()) return Value::boolean(true);
        std::string what = "Value";
        for (auto& a : args) if (a.t == VT::Pair && a.pairVal) what = a.pairVal->toStr();
        throwTypedV("X::OutOfRange",
            {{"got", args[0]}, {"what", Value::str(what)}, {"range", inv}},
            what + " out of range. Is: " + args[0].gist() + ", should be in " + inv.gist());
    }
    if (inv.t == VT::Range && inv.rTo >= 9000000000000000000LL) {
        long long lo = inv.rFrom + (inv.rExFrom ? 1 : 0);
        if (m == "is-lazy" || m == "infinite") return Value::boolean(true);
        if (m == "head" && args.empty()) return Value::integer(lo); // scalar first element
        if (m == "head") { long long n = std::max(0LL, args[0].toInt());
            Value o = Value::array(); o.isList = true; for (long long i = 0; i < n; i++) o.arr->push_back(Value::integer(lo + i)); return o; }
        if (m == "skip") { long long n = args.empty() ? 1 : std::max(0LL, args[0].toInt()); return Value::range(lo + n, inv.rTo, false, inv.rExTo); }
        if (m == "elems" || m == "Numeric" || m == "Int") return Value::number(INFINITY);
        if (m == "min") return inv.rFrom <= -9000000000000000000LL
            ? Value::number(-INFINITY) : Value::integer(inv.rFrom);
        if (m == "max") return Value::number(INFINITY);                 // `1..*` .max is Inf, not an error
        if (m == "excludes-min") return Value::boolean(inv.rExFrom);
        if (m == "excludes-max") return Value::boolean(inv.rExTo);
        if (m == "bounds") { Value o = Value::array({Value::integer(inv.rFrom), Value::number(INFINITY)}); o.isList = true; return o; }
        if (m == "list" || m == "List" || m == "Seq" || m == "cache" || m == "lazy" || m == "flat" ||
            m == "map" || m == "grep" || m == "first" || m == "iterator" || m == "rotor" || m == "batch")
            return (m == "map" || m == "grep" || m == "first") ? methodCall(makeInfArray(lo), m, args, rwArgs) : makeInfArray(lo);
        if (m == "AT-POS" && !args.empty()) return Value::integer(lo + args[0].toInt()); // infRange[i]
        if (m == "tail" || m == "pop" || m == "reverse" || m == "sort" || m == "sum" ||
            m == "Array" || m == "eager" || m == "join" || m == "Str" || m == "gist")
            throw RakuError{Value::typeObj("X::Cannot::Lazy"), "Cannot " + m + " an infinite range"};
    }
    // `.all`/`.any`/`.one`/`.none` on a single (non-container) value → a one-element
    // junction (Rakudo: `5.all` === `all(5)`); containers are handled just below.
    if ((m == "all" || m == "any" || m == "none" || m == "one") &&
        inv.t != VT::Array && inv.t != VT::Range && inv.t != VT::Hash) {
        Value j = Value::array(); j.enumName = m;
        j.arr = std::make_shared<ValueList>(ValueList{inv});
        return j;
    }
    if (inv.t == VT::Array || inv.t == VT::Range || inv.t == VT::Hash) {
        ValueList items = toList(inv);
        // junction methods: @a.any / .all / .none / .one — a tagged-Array junction
        if (m == "any" || m == "all" || m == "none" || m == "one") {
            Value j = Value::array(); j.enumName = m;
            j.arr = std::make_shared<ValueList>(items);
            return j;
        }
        if (m == "Supply") { Value s = Value::makeHash(); s.hashKind = "Supply"; Value v = Value::array(); *v.arr = items; (*s.hash)["values"] = v; return s; }
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
                    if (i >= 0 && i < n) o.arr->push_back(items[(size_t)i]);
                }
            }
            return o;
        }
        if (m == "elems") return Value::integer((long long)items.size());
        if (m == "end") return Value::integer((long long)items.size() - 1);
        if (m == "Bool") return Value::boolean(!items.empty());
        // `.Array` DECONTAINERIZES. A hash/scalar value sits in a container, so
        // an Array read out of one is itemized; returning it unchanged meant
        // `my @a = $v.Array` bound it as ONE element while `.Array.elems` said 3.
        // `@($v)` was already right, which is what made the two disagree.
        if (m == "Array") {
            if (inv.t != VT::Array) return Value::array(items);
            Value r = inv; r.itemized = false; r.isList = false; return r;
        }
        if (m == "values") {
            Value out = Value::array();
            if (inv.t == VT::Hash) { for (auto& kv : *inv.hash) out.arr->push_back(kv.second); }
            else out.arr = std::make_shared<ValueList>(items);
            out.isList = true; return out;
        }
        // `.pairup` reads the list PAIRWISE — but a Pair element already is one,
        // so it stands alone and does not consume its neighbour.
        if (m == "pairup") {
            Value out = Value::array(); out.isList = true; out.s = "Seq";
            for (size_t i = 0; i < items.size(); i++) {
                if (items[i].t == VT::Pair) { out.arr->push_back(items[i]); continue; }
                if (i + 1 >= items.size())
                    throw RakuError{Value::typeObj("X::AdHoc"),
                                    "Odd number of elements found for .pairup()"};
                Value p = Value::pair(items[i].toStr(), items[i + 1]);
                // a non-Str key keeps its own value (`1 => 2`, not `"1" => 2`)
                if (items[i].t != VT::Str) p.pairKey = std::make_shared<Value>(items[i]);
                out.arr->push_back(std::move(p));
                i++;
            }
            return out;
        }
        if (m == "flat") {
            // deep-flatten NON-itemized sublists ((((0,1),2),3).flat is 0,1,2,3);
            // itemized Arrays ([..] / .item) stay whole elements
            Value out = Value::array(); out.isList = true; out.s = "Seq";
            std::function<void(const Value&)> go = [&](const Value& x) {
                if (x.t == VT::Array && x.arr && x.isList && !x.itemized)
                    for (auto& e : *x.arr) go(e);
                else if (x.t == VT::Hash && x.hash && x.hashKind.empty())
                    for (auto& kv : *x.hash) out.arr->push_back(Value::pair(kv.first, kv.second));
                else if (x.t == VT::Range)
                    for (auto& e : x.flatten()) out.arr->push_back(e);
                else out.arr->push_back(x);
            };
            for (auto& x : items) go(x);
            return out;
        }
        // `.eager` on a concrete Array is the identity — it keeps the same
        // container (and its element type: `my int @a` stays array[int]); only a
        // lazy Seq needs forcing (its elements are already materialised in `items`)
        if (m == "eager" && inv.t == VT::Array && !inv.ext)
            return inv;
        if (m == "list" || m == "cache" || m == "eager" || m == "Seq" || m == "List" || m == "lazy") {
            Value out = Value::list(items);
            if (m == "Seq") out.s = "Seq"; // `.Seq` really is one — `(1,2).Seq.raku` says so
            if (m == "lazy") out.b = true; // `.lazy` MARKS it: `.is-lazy` says True after
            return out;
        }
        if (m == "reverse") { std::reverse(items.begin(), items.end()); return Value::list(items); }
        if (m == "rotate") { long n = args.empty() ? 1 : args[0].toInt(); long sz = (long)items.size();
            if (sz) { n = ((n % sz) + sz) % sz; std::rotate(items.begin(), items.begin() + n, items.end()); }
            return Value::list(items); }
        if (m == "permutations") {
            Value out = Value::array(); out.isList = true; out.s = "Seq";
            std::vector<size_t> idx(items.size());
            for (size_t i = 0; i < idx.size(); i++) idx[i] = i;
            // generate in lexicographic order of indices (matches Rakudo's ordering)
            do {
                Value perm = Value::array(); perm.isList = true; // a sublist gists with (…)
                for (size_t i : idx) perm.arr->push_back(items[i]);
                out.arr->push_back(perm);
            } while (std::next_permutation(idx.begin(), idx.end()));
            return out;
        }
        if (m == "combinations") {
            Value out = Value::array(); out.isList = true; out.s = "Seq";
            long long lo = 0, hi = (long long)items.size();
            if (!args.empty()) {
                Value k = a0();
                if (k.t == VT::Range) { lo = k.rFrom; hi = k.rExTo ? k.rTo - 1 : k.rTo; }
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
                    for (long long i = 0; i < k; i++) combo.arr->push_back(items[c[i]]);
                    out.arr->push_back(combo);
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
        if (m == "join") return Value::str(joinValues(items, args.empty() ? "" : a0().toStr()));
        if (m == "fmt") {
            std::string fmt = args.empty() ? "%s" : a0().toStr();
            std::string sep = args.size() > 1 ? args[1].toStr() : " ";
            std::string out;
            // a Setty/Baggy formats each (key, count) pair — `%s` consumes just the key
            if (inv.t == VT::Hash && inv.hash &&
                (inv.hashKind.rfind("Set", 0) == 0 || inv.hashKind.rfind("Bag", 0) == 0 ||
                 inv.hashKind.rfind("Mix", 0) == 0)) {
                bool first = true;
                for (auto& kv : *inv.hash) {
                    if (!first) out += sep;
                    first = false;
                    Value key = kv.second.pairKey ? *kv.second.pairKey : Value::str(kv.first);
                    out += doSprintf(fmt, {key, kv.second});
                }
                return Value::str(out);
            }
            for (size_t k = 0; k < items.size(); k++) { if (k) out += sep; out += doSprintf(fmt, {items[k]}); }
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
            for (auto& v : items) if (v.t == VT::Pair) (*h.hash)[v.s] = v.pairVal ? *v.pairVal : Value::any();
            return h;
        }
        if (m == "shape") { // a declared shape (my @a[2;3]) reports its dims; else (*,)
            Value o = Value::array(); o.isList = true;
            if (inv.shape && !inv.shape->empty())
                for (long long d : *inv.shape) o.arr->push_back(Value::integer(d));
            else o.arr->push_back(Value::whatever());
            return o;
        }
        if (m == "hyper" || m == "race") { Value o = Value::array(items); o.isList = true; return o; } // parallel -> sequential
        if (m == "is-lazy") return Value::boolean(inv.t == VT::Array && inv.b); // materialised list is not lazy (unless `lazy`-marked)
        // A RIGHT-associative operator folds from the right: `.reduce(&[**])` is
        // 2**(3**4), not (2**3)**4. `&[OP]` callables carry their name, which is
        // the only place the associativity is recorded.
        auto rightAssoc = [](const Value& f) {
            if (f.t != VT::Code || !f.code) return false;
            const std::string& n = f.code->name;
            if (n.rfind("infix:<", 0) != 0 || n.size() < 9) return false;
            std::string op = n.substr(7, n.size() - 8);
            return op == "**" || op == "=>";
        };
        if (m == "reduce" && !args.empty() && args[0].t == VT::Code) { // fold with a 2-arg op: (1,2,3).reduce(* + *)
            // over NOTHING an operator answers its identity — `().reduce(&[+])`
            // is 0 — which the [op] metaop already knows how to look up
            if (items.empty()) {
                const std::string& cn = args[0].code ? args[0].code->name : std::string();
                if (cn.rfind("infix:<", 0) == 0 && cn.back() == '>') {
                    ValueList none;
                    return applyReduce(cn.substr(7, cn.size() - 8), none);
                }
                return Value::any();
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
                for (size_t k = items.size(); k-- > 0; ) out.arr->push_back(acc[k]);
                return out;
            }
            Value acc = items[0]; out.arr->push_back(acc);
            // `last` ends the scan — and DROPS the most recent running value, because
            // Rakudo produces lazily and so lags one behind what has been computed.
            // Checked against `(2,3,4,5).produce: {last if $^a > 7; $^a+$^b}` -> (2 5),
            // `(1,2,3,4)` with `$^a > 2` -> (1), and `$^a > 0` -> ().
            try {
                for (size_t k = 1; k < items.size(); k++) { acc = callCallable(args[0], {acc, items[k]}); out.arr->push_back(acc); }
            } catch (LastEx&) { if (!out.arr->empty()) out.arr->pop_back(); }
            return out;
        }
        // `%h.classify-list($mapper, *@values)` classifies INTO the invocant and
        // answers it. A list-valued key NESTS — `("1a","1b")` files the value
        // under %h<1a><1b> — which is what separates it from plain `.classify`.
        // `.categorize-list` files under EVERY key the mapper yields instead.
        if ((m == "classify-list" || m == "categorize-list") && !args.empty()) {
            bool cat = (m == "categorize-list");
            Value self = inv.t == VT::Hash && inv.hash ? inv : Value::makeHash();
            Value mapper = args[0];
            ValueList vals;
            for (size_t i = 1; i < args.size(); i++)
                for (auto& x : toList(args[i])) vals.push_back(x);
            auto keyFor = [&](const Value& v) -> Value {
                if (mapper.t == VT::Code) return callCallable(mapper, {v});
                if (mapper.t == VT::Hash && mapper.hash) {
                    auto it = mapper.hash->find(v.toStr());
                    return it != mapper.hash->end() ? it->second : Value::any();
                }
                if (mapper.t == VT::Array && mapper.arr) {
                    long long i = v.toInt();
                    return (i >= 0 && i < (long long)mapper.arr->size()) ? (*mapper.arr)[i] : Value::any();
                }
                return v;
            };
            // walk/‌create the nested hashes, then append at the leaf
            auto fileUnder = [&](Value& root, const ValueList& path, const Value& v) {
                Value* cur = &root;
                for (size_t d = 0; d + 1 < path.size(); d++) {
                    Value& slot = (*cur->hash)[path[d].toStr()];
                    if (slot.t != VT::Hash || !slot.hash) slot = Value::makeHash();
                    cur = &slot;
                }
                Value& leaf = (*cur->hash)[path.back().toStr()];
                if (leaf.t != VT::Array || !leaf.arr) leaf = Value::array();
                leaf.arr->push_back(v);
            };
            for (auto& v : vals) {
                Value k = keyFor(v);
                if (k.t == VT::Array && k.arr && !k.arr->empty()) {
                    if (cat) for (auto& kk : *k.arr) fileUnder(self, ValueList{kk}, v);
                    else     fileUnder(self, *k.arr, v);
                }
                else fileUnder(self, ValueList{k}, v);
            }
            return self;
        }
        if (m == "classify" || m == "categorize") { // group elements by a mapper into a Hash of lists
            Value* into = nullptr; Value* asF = nullptr;
            for (auto& x : args) if (x.t == VT::Pair && x.pairVal) {
                if (x.s == "into")   into = x.pairVal.get();
                else if (x.s == "as") asF = x.pairVal.get();  // what gets STORED, vs what is classified BY
            }
            Value mapper = args.empty() ? Value::nil() : args[0];
            Value h = Value::makeHash();
            auto add = [&](const std::string& key, const Value& vIn) {
                // `:as` maps the STORED value; the key still comes from the classifier
                Value v = asF ? callCallable(*asF, {vIn}) : vIn;
                auto it = h.hash->find(key);
                if (it == h.hash->end()) { Value a = Value::array(); a.arr->push_back(v); (*h.hash)[key] = a; }
                else { if (it->second.t != VT::Array) { Value a = Value::array(); a.arr->push_back(it->second); it->second = a; } it->second.arr->push_back(v); }
            };
            for (auto& v : items) {
                // the classifier may be a Callable (called), a Hash (indexed by the
                // element), or an Array (indexed by the element as position)
                Value k;
                if (mapper.t == VT::Code) k = callCallable(mapper, {v});
                else if (mapper.t == VT::Hash && mapper.hash) { auto it = mapper.hash->find(v.toStr()); k = it != mapper.hash->end() ? it->second : Value::any(); }
                else if (mapper.t == VT::Array && mapper.arr) { long long i = v.toInt(); k = (i >= 0 && i < (long long)mapper.arr->size()) ? (*mapper.arr)[i] : Value::any(); }
                else k = v;
                if (m == "categorize" && k.t == VT::Array && k.arr) { for (auto& kk : *k.arr) add(kk.toStr(), v); }
                else add(k.toStr(), v);
            }
            if (into) { // :into(%h) — append into an existing hash and return it
                if (into->t != VT::Hash || !into->hash) *into = Value::makeHash();
                for (auto& kv : *h.hash) {
                    auto it = into->hash->find(kv.first);
                    if (it == into->hash->end()) (*into->hash)[kv.first] = kv.second;
                    else if (it->second.t == VT::Array && kv.second.t == VT::Array)
                        for (auto& e : *kv.second.arr) it->second.arr->push_back(e);
                }
                return *into;
            }
            return h;
        }
        if (m == "pairup") { // consecutive elements become Pairs (odd tail: X::Pairup::OddNumber)
            ValueList items = inv.t == VT::Array && inv.arr ? *inv.arr : ValueList{};
            Value out = Value::array(); out.isList = true; out.s = "Seq";
            for (size_t i = 0; i + 1 < items.size(); i += 2)
                out.arr->push_back(Value::pair(items[i].toStr(), items[i + 1]));
            if (items.size() % 2)
                throw RakuError{Value::typeObj("X::Pairup::OddNumber"),
                    "Odd number of elements found where hash initializer expected"};
            return out;
        }
        if (m == "rotor" || m == "batch") { // chunk into sublists of a fixed size
            for (auto& a : args)
                if (a.isNumeric() && a.toInt() <= 0)
                    throw RakuError{Value::typeObj("X::OutOfRange"),
                        "batch size is out of range. Is: " + std::to_string(a.toInt()) + ", should be in 1..^Inf"};
            // Several positionals CYCLE: rotor(2, 3) is windows of 2, 3, 2, 3, …
            // Each spec is a size, or `size => gap` where the next window starts
            // size+gap later (a negative gap overlaps).
            struct Spec { long long n, step; };
            std::vector<Spec> specs;
            bool partial = (m == "batch"); // batch always keeps a short final chunk; rotor drops it unless :partial
            for (auto& a : args) {
                if (a.t == VT::Pair && a.s == "partial") { if (!a.pairVal || a.pairVal->truthy()) partial = true; }
                else if (a.t == VT::Pair && a.pairVal) {
                    long long n = a.pairKey ? a.pairKey->toInt() : std::atoll(a.s.c_str());
                    if (n < 1) n = 1;
                    specs.push_back({n, n + a.pairVal->toInt()});
                }
                else if (a.isNumeric()) { long long n = a.toInt(); if (n < 1) n = 1; specs.push_back({n, n}); }
            }
            if (specs.empty()) specs.push_back({1, 1});
            Value out = Value::array(); out.isList = true;
            for (size_t i = 0, k = 0; i < items.size(); k++) {
                const Spec& sp = specs[k % specs.size()];
                if (i + (size_t)sp.n > items.size() && !partial) break;
                Value chunk = Value::array(); chunk.isList = true;
                for (size_t j = i; j < i + (size_t)sp.n && j < items.size(); j++) chunk.arr->push_back(items[j]);
                out.arr->push_back(chunk);
                i += (size_t)(sp.step < 1 ? 1 : sp.step); // step is clamped, so this terminates
            }
            return out;
        }
        if (m == "snip") { // 6.e: split into sublists — each predicate consumes the
            // leading run it matches; leftovers form the final sublist. The predicate
            // arg is one Callable/type-object, or a list of them.
            std::vector<Value> preds;
            for (auto& p : args) {
                if (p.t == VT::Array && p.arr) for (auto& q : *p.arr) preds.push_back(q); // a (p1,p2) list of preds
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
                while (idx < items.size() && matches(pred, items[idx])) sub.arr->push_back(items[idx++]);
                out.arr->push_back(sub);
            }
            if (idx < items.size()) {
                Value sub = Value::array(); sub.isList = true;
                while (idx < items.size()) sub.arr->push_back(items[idx++]);
                out.arr->push_back(sub);
            }
            return out;
        }
        if (m == "are") { // 6.e: narrowest common type, or `.are(T)` = all-conform check
            if (!args.empty()) {
                std::string t = typeOfVal(args[0]);
                for (auto& el : items) if (!rtTypeMatch(el, t))
                    throw RakuError{Value::typeObj("X::AdHoc"), "Not all list elements are of type " + t};
                return Value::boolean(true);
            }
            if (items.empty()) return Value::nil();
            std::string lub = typeOfVal(items[0]);
            for (size_t k = 1; k < items.size(); k++) lub = lubType(lub, typeOfVal(items[k]));
            return Value::typeObj(lub);
        }
        if (m == "minmax") {
            // Range.minmax → the (min max) List; List.minmax → a min..max Range
            if (inv.t == VT::Range) {
                Value out = Value::array(); out.isList = true;
                out.arr->push_back(Value::integer(inv.rFrom + (inv.rExFrom ? 1 : 0)));
                out.arr->push_back(Value::integer(inv.rTo - (inv.rExTo ? 1 : 0)));
                return out;
            }
            // an optional &mapper (or `:by(&code)`) decides the ORDER; the
            // endpoints are still the original elements. As for min/max, the block
            // is the first CODE argument — an adverb may precede it.
            Value mapper = Value::nil();
            for (auto& a : args)
                if (a.t == VT::Code) { mapper = a; break; }
            for (auto& a : args)
                if (a.t == VT::Pair && a.s == "by" && a.pairVal) mapper = *a.pairVal;
            Value lo, hi, loK, hiK; bool started = false;
            for (auto& v : items) {
                Value k = v;
                if (mapper.t == VT::Code) { ValueList one{v}; k = callCallable(mapper, one); }
                if (!started) { lo = hi = v; loK = hiK = k; started = true; continue; }
                if (valueCmp(k, loK) < 0) { lo = v; loK = k; }
                if (valueCmp(k, hiK) > 0) { hi = v; hiK = k; }
            }
            if (started && lo.t == VT::Int && hi.t == VT::Int)
                return Value::range(lo.toInt(), hi.toInt(), false, false);
            Value out = Value::array(); out.isList = true; // non-Int endpoints (our Range is Int-only)
            if (started) { out.arr->push_back(lo); out.arr->push_back(hi); }
            return out;
        }
        if (m == "min" || m == "max") {
            // Rakudo: the extremum of an empty list is ±Inf (min → Inf, max → -Inf)
            if (items.empty()) return Value::number(m == "min" ? INFINITY : -INFINITY);
            bool wantMax = (m == "max");
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
                    if (a.s == "by" && a.pairVal) mapper = *a.pairVal;
                    else if (a.pairVal && a.pairVal->truthy() &&
                             (a.s == "k" || a.s == "v" || a.s == "kv" || a.s == "p"))
                        want = a.s == "kv" ? 'm' : a.s[0];
                }
            Value best, bestKey; bool started = false;
            std::vector<size_t> at;
            for (size_t i = 0; i < items.size(); i++) {
                const Value& v = items[i];
                // undefined elements (holes in a sparse array, type objects) don't compete
                if (v.t == VT::Nil || v.t == VT::Any || v.t == VT::Type) continue;
                Value key = v;
                if (mapper.t == VT::Code) { ValueList one{v}; key = callCallable(mapper, one); }
                if (!started) { best = v; bestKey = key; started = true; at = {i}; continue; }
                int c = valueCmp(key, bestKey); // strict compare keeps the FIRST on ties
                if ((!wantMax && c < 0) || (wantMax && c > 0)) { best = v; bestKey = key; at = {i}; }
                else if (c == 0) at.push_back(i);
            }
            if (!started) return Value::number(m == "min" ? INFINITY : -INFINITY); // all undefined
            if (!want) return best;
            Value o = Value::array(); o.isList = true; o.s = "Seq";
            for (size_t i : at) {
                if (want == 'k' || want == 'm') o.arr->push_back(Value::integer((long long)i));
                if (want == 'v' || want == 'm') o.arr->push_back(items[i]);
                if (want == 'p') {
                    Value pr = Value::pair(std::to_string(i), items[i]);
                    pr.pairKey = std::make_shared<Value>(Value::integer((long long)i));
                    o.arr->push_back(std::move(pr));
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
                for (char c : s) if (!std::isdigit((unsigned char)c) && c != '-' && c != '+' && c != '.' && c != ' ') { num = false; break; }
                if (!num) throw RakuError{Value::typeObj("X::Str::Numeric"), "Cannot convert string to number: '" + s + "'"};
            }
            return a.toInt();
        };
        if (m == "head") {
            if (args.empty()) return items.empty() ? Value::any() : items.front();
            long long n = resolveCount(a0(), (long long)items.size());
            if (n < 0) n = 0;
            Value o = Value::array(); o.isList = true;
            for (long long k = 0; k < n && k < (long long)items.size(); k++) o.arr->push_back(items[k]);
            return o;
        }
        if (m == "tail") {
            if (args.empty()) return items.empty() ? Value::any() : items.back();
            long long n = resolveCount(a0(), (long long)items.size());
            if (n < 0) n = 0;
            Value o = Value::array(); o.isList = true;
            long long start = std::max(0LL, (long long)items.size() - n);
            for (long long k = start; k < (long long)items.size(); k++) o.arr->push_back(items[k]);
            return o;
        }
        if (m == "skip") { // drop the first n elements (default 1)
            long long n = args.empty() ? 1 : resolveCount(a0(), (long long)items.size());
            if (n < 0) n = 0;
            Value o = Value::array(); o.isList = true;
            for (long long k = n; k < (long long)items.size(); k++) o.arr->push_back(items[k]);
            return o;
        }
        if (m == "first") {
            // :k → index; :v → value (the default); :kv → both; :p → index => value;
            // :end → search backwards for the LAST match
            char want = 0; bool wantEnd = false;
            for (auto& a : args) if (a.t == VT::Pair && a.pairVal && a.pairVal->truthy()) {
                if (a.s == "end") wantEnd = true;
                else if (a.s == "k" || a.s == "v" || a.s == "p") want = a.s[0];
                else if (a.s == "kv") want = 'm';
            }
            auto answer = [&](size_t i) -> Value {
                if (want == 'k') return Value::integer((long long)i);
                if (want == 'p') {
                    Value pr = Value::pair(std::to_string(i), items[i]);
                    pr.pairKey = std::make_shared<Value>(Value::integer((long long)i));
                    return pr;
                }
                if (want == 'm') {
                    Value o = Value::array(); o.isList = true; o.s = "Seq";
                    o.arr->push_back(Value::integer((long long)i));
                    o.arr->push_back(items[i]);
                    return o;
                }
                return items[i];
            };
            Value pred; bool havePred = false;
            for (auto& a : args) if (a.t != VT::Pair) { pred = a; havePred = true; break; }
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
        if ((m == "pickpairs" || m == "grabpairs") && inv.t == VT::Hash && inv.hash &&
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
            for (auto& kv : *inv.hash) keys.push_back(kv.first);
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
                out.arr->push_back(Value::pair(key, (*inv.hash)[key]));
                if (m == "grabpairs") inv.hash->erase(key);
            }
            if (args.empty()) return out.arr->empty() ? Value::nil() : (*out.arr)[0];
            return out;
        }
        if (m == "grab" && inv.t == VT::Hash && inv.hash &&
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
                for (auto& kv : *inv.hash)
                    total += inv.hashKind == "SetHash" ? 1.0 : kv.second.toNum();
                if (total <= 0) break;
                double r = randDouble() * total;
                std::string key;
                for (auto& kv : *inv.hash) {
                    double w = inv.hashKind == "SetHash" ? 1.0 : kv.second.toNum();
                    if (w <= 0) continue;
                    if (r < w) { key = kv.first; break; }
                    r -= w;
                }
                if (key.empty() && !inv.hash->empty()) key = inv.hash->begin()->first;
                if (key.empty()) break;
                out.arr->push_back(Value::str(key));
                if (inv.hashKind == "SetHash") inv.hash->erase(key);
                else {
                    long long c = (*inv.hash)[key].toInt() - 1;
                    if (c <= 0) inv.hash->erase(key); else (*inv.hash)[key] = Value::integer(c);
                }
            }
            if (one) return out.arr->empty() ? Value::nil() : (*out.arr)[0];
            return out;
        }
        if (m == "pick" || m == "roll") { // random element(s); pick = without replacement
            // an enum type picks from its VALUES (red/green/blue), not its (key=>val) pairs
            ValueList enumVals;
            for (auto& pr : items) if (!inv.enumType.empty() && pr.t == VT::Pair) {
                Value ev = Value::enumVal(pr.s, pr.pairVal ? pr.pairVal->toInt() : 0);
                ev.enumType = inv.enumType; enumVals.push_back(ev);
            }
            // quanthashes pick from their KEYS (Bag/Mix: weighted by count — sampled,
            // never materialized: a bag with a count of 10^9 must not build a pool)
            static const std::set<std::string> setty = {"Set", "SetHash"};
            static const std::set<std::string> baggy = {"Bag", "BagHash", "Mix", "MixHash"};
            if (inv.t == VT::Hash && inv.hash && (setty.count(inv.hashKind) || baggy.count(inv.hashKind))) {
                if (m == "pick" && inv.hashKind.rfind("Mix", 0) == 0) // Mix has no .pick — weights aren't multiplicities
                    throw RakuError{Value::typeObj("X::AdHoc"),
                        "Cannot .pick from a " + inv.hashKind + "; use .roll instead"};
                if (!args.empty() && args[0].t == VT::Num && std::isnan(args[0].n))
                    throw RakuError{Value::typeObj("X::AdHoc"), "Cannot coerce NaN to an Int"};
                std::vector<std::pair<std::string, double>> pool; // key, weight
                double total = 0;
                for (auto& kv : *inv.hash) {
                    double w = setty.count(inv.hashKind) ? 1.0 : kv.second.toNum();
                    if (w > 0) { pool.push_back({kv.first, w}); total += w; }
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
                if (pool.empty()) return args.empty() ? Value::nil() : Value::array();
                if (args.empty()) { long long k = draw(); return k < 0 ? Value::nil() : Value::str(pool[k].first); }
                bool all = args[0].t == VT::Whatever ||
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
                            if (r < pw.second) { cache.push_back(Value::str(pw.first)); return true; }
                            r -= pw.second;
                        }
                        if (!poolC.empty()) { cache.push_back(Value::str(poolC.back().first)); return true; }
                        return false;
                    };
                    out.ext = st;
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
                        out.arr->push_back(Value::str(pool[k].first));
                        double dec = std::min(1.0, pool[k].second);
                        pool[k].second -= dec; total -= dec;
                    }
                }
                else for (long long i = 0; i < n; i++) {
                    long long k = draw();
                    if (k < 0) break;
                    out.arr->push_back(Value::str(pool[k].first));
                }
                return out;
            }
            const ValueList& pool0 = inv.enumType.empty() ? items : enumVals;
            if (pool0.empty()) return args.empty() ? Value::nil() : Value::array();
            bool all = !args.empty() && (args[0].t == VT::Whatever ||
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
                out.ext = st;
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
                    out.arr->push_back(pool[j]); pool.erase(pool.begin() + j);
                }
            } else { // roll: with replacement
                for (long long i = 0; i < n; i++) out.arr->push_back(pool0[(size_t)(randDouble() * pool0.size())]);
            }
            return out;
        }
        if (m == "unique") {
            // :as(&mapper) compares mapped keys; :with(&eq) uses a custom equality (O(n²)).
            Value asF, withF;
            for (auto& a : args) if (a.t == VT::Pair && a.pairVal && a.pairVal->t == VT::Code) { if (a.s == "as") asF = *a.pairVal; else if (a.s == "with") withF = *a.pairVal; }
            auto keyOf = [&](const Value& v) { return asF.t == VT::Code ? callCallable(asF, ValueList{v}) : v; };
            Value out = Value::array(); out.isList = true;
            if (withF.t == VT::Code) {
                ValueList kept;
                for (auto& v : items) { Value k = keyOf(v); bool dup = false;
                    for (auto& kk : kept) if (callCallable(withF, ValueList{k, kk}).truthy()) { dup = true; break; }
                    if (!dup) { kept.push_back(k); out.arr->push_back(v); } }
            } else {
                std::set<std::string> seen;
                for (auto& v : items) if (seen.insert(keyOf(v).toStr()).second) out.arr->push_back(v);
            }
            return out;
        }
        if (m == "repeated") { // elements seen more than once (2nd+ occurrences)
            // `:as(&code)` compares the MAPPED value; `:with(&op)` supplies the
            // comparison itself, which needs a linear scan rather than a set
            Value asF, withF;
            for (auto& a : args)
                if (a.t == VT::Pair && a.pairVal) {
                    if (a.s == "as") asF = *a.pairVal;
                    else if (a.s == "with") withF = *a.pairVal;
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
                    if (dup) out.arr->push_back(v); else kept.push_back(k);
                }
                return out;
            }
            std::set<std::string> seen;
            for (auto& v : items) if (!seen.insert(keyOf(v).toStr()).second) out.arr->push_back(v);
            return out;
        }
        if (m == "toggle") { // gate values on/off, flipping at each condition boundary
            // ON: emit while cond(v) is true; the first false value flips OFF (not
            // emitted) and consumes the condition. OFF: skip while false; the first
            // true value flips ON (emitted) and consumes the condition. Out of
            // conditions → the state freezes. :off starts in the OFF state.
            bool on = true;
            std::vector<Value> conds;
            for (auto& a : args) {
                if (a.t == VT::Pair && a.s == "off") on = !(a.pairVal && a.pairVal->truthy());
                else if (a.t == VT::Code) conds.push_back(a);
            }
            Value out = Value::array(); out.isList = true;
            size_t ci = 0;
            for (auto& v : items) {
                if (ci < conds.size()) {
                    bool c = callCallable(conds[ci], ValueList{v}).truthy();
                    if (on) { if (c) out.arr->push_back(v); else { on = false; ci++; } }
                    else if (c) { on = true; ci++; out.arr->push_back(v); }
                } else if (on) out.arr->push_back(v);
            }
            return out;
        }
        if (m == "squish") { // collapse adjacent duplicates (:as maps keys, :with compares them)
            Value asF, withF;
            for (auto& a : args) if (a.t == VT::Pair && a.pairVal && a.pairVal->t == VT::Code) { if (a.s == "as") asF = *a.pairVal; else if (a.s == "with") withF = *a.pairVal; }
            auto keyOf = [&](const Value& v) { return asF.t == VT::Code ? callCallable(asF, ValueList{v}) : v; };
            Value out = Value::array(); out.isList = true;
            bool first = true; Value prevKey;
            for (auto& v : items) {
                Value k = keyOf(v); bool same = false;
                if (!first) same = withF.t == VT::Code ? callCallable(withF, ValueList{k, prevKey}).truthy()
                                                      : applyArith("===", k, prevKey).truthy();
                if (first || !same) out.arr->push_back(v);
                prevKey = k; first = false;
            }
            return out;
        }
        if (m == "sort") {
            // :k sorts the INDICES of the elements instead of the elements
            bool wantK = false;
            for (auto& av : args)
                if (av.t == VT::Pair && av.namedArg && av.s == "k")
                    wantK = !av.pairVal || av.pairVal->truthy();
            std::vector<size_t> order(items.size());
            for (size_t i = 0; i < order.size(); i++) order[i] = i;
            if (!args.empty() && args[0].t == VT::Code) {
                Value blk = args[0];
                size_t arity = blk.code->params && !blk.code->params->empty()
                    ? blk.code->params->size()
                    : (blk.code->placeholders.empty() ? (size_t)blk.code->whateverArity : blk.code->placeholders.size());
                // a 0-arity comparator ((1..10).sort(&rand)) has no candidate
                if (arity == 0 && blk.code->params && blk.code->hadSig)
                    throw RakuError{Value::typeObj("X::TypeCheck::Argument"),
                        "Uncallable 0-arity comparator for sort"};
                if (arity >= 2) {
                    std::stable_sort(order.begin(), order.end(), [&](size_t x, size_t y) {
                        return callCallable(blk, {items[x], items[y]}).toInt() < 0;
                    });
                } else {
                    // A 1-ary block is a KEY EXTRACTOR, so it runs ONCE PER ELEMENT and
                    // the sort compares the extracted keys — a Schwartzian transform,
                    // which is what Rakudo does. Calling it inside the comparator ran it
                    // O(n log n) times instead of O(n): the documented
                    // `(0..0x1FFFF).sort(*.uniname.chars)` took 49s against Rakudo's 1.2s.
                    std::vector<Value> keys(items.size());
                    for (size_t i = 0; i < items.size(); i++) keys[i] = callCallable(blk, {items[i]});
                    std::stable_sort(order.begin(), order.end(), [&](size_t x, size_t y) {
                        return valueCmp(keys[x], keys[y]) < 0;
                    });
                }
            } else {
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
            if (args.empty()) { Value o = Value::array(); *o.arr = items; o.isList = true; return o; }
            // closures may be passed as bare args (`.tree(&a, &b)`) or one array (`.tree([&a, &b])`)
            ValueList closures;
            if (args[0].t == VT::Array && args[0].arr) { for (auto& e : *args[0].arr) if (e.t == VT::Code) closures.push_back(e); }
            else for (auto& a : args) if (a.t == VT::Code) closures.push_back(a);
            bool byClosure = !closures.empty();
            long long depth = byClosure ? (long long)closures.size() : args[0].toInt();
            std::function<Value(const Value&, long long)> build = [&](const Value& node, long long d) -> Value {
                bool isList = node.t == VT::Array || node.t == VT::Range;
                if (!isList) return node;
                if (byClosure) {
                    if (d >= (long long)closures.size()) return node; // past the last closure: leaf
                    // the LAST closure is applied to the node itself (by identity, so a
                    // single-closure `.tree(&c)` calls c(self)); deeper closures rebuild
                    if (d + 1 >= (long long)closures.size()) return callCallable(closures[d], ValueList{node});
                    Value kids = Value::array(); kids.isList = true;
                    for (auto& e : (node.t == VT::Range ? node.flatten() : *node.arr))
                        kids.arr->push_back(build(e, d + 1));
                    return callCallable(closures[d], ValueList{kids});
                }
                if (d >= depth) { // depth cap: flatten the rest into one level
                    Value o = Value::array(); o.isList = true;
                    for (auto& e : node.flatten()) o.arr->push_back(e);
                    return o;
                }
                Value kids = Value::array(); kids.isList = true;
                for (auto& e : (node.t == VT::Range ? node.flatten() : *node.arr))
                    kids.arr->push_back(build(e, d + 1));
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
            auto leaf = [&](Value& slot) -> Value {
                topicWriteback_ = &slot; // $_/placeholder mutations alias the node
                Value r = callCallable(fn, ValueList{slot});
                topicWriteback_ = nullptr;
                return r;
            };
            std::function<Value(Value&)> deepEl = [&](Value& e) -> Value {
                if (e.t == VT::Array && e.arr) {
                    Value o = Value::array(); o.isList = e.isList;
                    for (auto& x : *e.arr) o.arr->push_back(deepEl(x));
                    return o;
                }
                if (e.t == VT::Hash && e.hash && e.hashKind.empty()) {
                    Value o = Value::makeHash();
                    for (auto& kv : *e.hash) (*o.hash)[kv.first] = deepEl(kv.second);
                    return o;
                }
                return leaf(e);
            };
            std::function<Value(Value&)> duckEl = [&](Value& e) -> Value {
                try { return leaf(e); }
                catch (...) {
                    if (e.t == VT::Array && e.arr) {
                        Value o = Value::array(); o.isList = e.isList;
                        for (auto& x : *e.arr) o.arr->push_back(duckEl(x));
                        return o;
                    }
                    if (e.t == VT::Hash && e.hash && e.hashKind.empty()) {
                        Value o = Value::makeHash();
                        for (auto& kv : *e.hash) (*o.hash)[kv.first] = duckEl(kv.second);
                        return o;
                    }
                    return e;
                }
            };
            auto applyEl = [&](Value& e) -> Value {
                return m == "deepmap" ? deepEl(e) : m == "duckmap" ? duckEl(e) : leaf(e);
            };
            if (inv.t == VT::Hash && inv.hash && inv.hashKind.empty()) {
                Value o = Value::makeHash();
                for (auto& kv : *inv.hash) (*o.hash)[kv.first] = applyEl(kv.second);
                return o;
            }
            // deepmap/duckmap answer in the invocant's own container (Array in,
            // Array out); nodemap always answers a List
            Value out = Value::array();
            out.isList = (m == "nodemap") || inv.t != VT::Array || inv.isList;
            if (inv.t == VT::Array && inv.arr)
                for (auto& e : *inv.arr) out.arr->push_back(applyEl(e));
            else { Value tmp = inv; return applyEl(tmp); }
            return out;
        }
        if (m == "tree") {
            // .tree(&f, *@rest): f applied to the node's children, each child
            // first transformed by .tree(|@rest); non-iterables return themselves
            std::function<Value(const Value&, size_t)> tr = [&](const Value& v, size_t k) -> Value {
                if (v.t != VT::Array || !v.arr) return v;
                Value kids = Value::array(); kids.isList = true;
                for (auto& e : *v.arr) kids.arr->push_back(tr(e, k + 1));
                if (k < args.size() && args[k].t == VT::Code) return callCallable(args[k], ValueList{kids});
                return kids;
            };
            return tr(inv, 0);
        }
        if (m == "map" || m == "flatmap") { // flatmap == map that flattens list results one level
            // the mapper must be a Callable — `%h.map(Hash)` (a type object) dies
            if (!args.empty() && args[0].t == VT::Type)
                throw RakuError{Value::typeObj("X::Cannot::Map"),
                    "Cannot map a " + inv.typeName() + " with a " + args[0].s};
            Value out = Value::array();
            if (!args.empty() && args[0].t == VT::Code) {
                // A block of arity N consumes N elements per iteration
                // (e.g. `%h.kv.map(-> $k, $v {…})` or `{ $^a … $^b }`).
                size_t ar = codeArity(args[0]);
                bool aliasable = ar == 1 && inv.t == VT::Array && inv.arr && items.size() == inv.arr->size();
                // Does the block carry loop phasers (FIRST/NEXT/LAST)? One scan;
                // if so, hand callCallableRaw per-iteration control so they fire
                // with loop semantics in the block's own env (Base64's encoder
                // computes its padding in a LAST that reads the block param).
                bool loopPh = false;
                if (args[0].code && args[0].code->body)
                    for (auto& s : *args[0].code->body)
                        if (s->kind == NK::Block) {
                            const std::string& ph = static_cast<Block*>(s.get())->phaser;
                            if (ph == "FIRST" || ph == "NEXT" || ph == "LAST") { loopPh = true; break; }
                        }
                for (size_t i = 0; i < items.size(); i += ar) {
                    ValueList ca;
                    for (size_t k = 0; k < ar && i + k < items.size(); k++) ca.push_back(items[i + k]);
                    if (aliasable) topicWriteback_ = &(*inv.arr)[i]; // $_ mutations alias the element
                    if (loopPh)
                        loopPhaserCtl_ = (i == 0 ? 1 : 0) | (i + ar >= items.size() ? 2 : 0) | 4;
                    Value r;
                    try { r = callCallable(args[0], ca); }
                    catch (LastEx&) { topicWriteback_ = nullptr; break; }   // `last` in the block ends the map
                    catch (NextEx&) { topicWriteback_ = nullptr; continue; } // `next` skips the element
                    // post-GLR: map keeps each block result as ONE element; only a
                    // Slip (or flatmap, which flattens one level by design) spreads.
                    if (m == "flatmap") {
                        if (r.t == VT::Array) for (auto& x : *r.arr) out.arr->push_back(x);
                        else if (r.t == VT::Range) for (auto& x : r.flatten()) out.arr->push_back(x);
                        else out.arr->push_back(r);
                    }
                    else if (r.t == VT::Array && r.isList && r.s == "Slip")
                        for (auto& x : *r.arr) out.arr->push_back(x);
                    else out.arr->push_back(r);
                }
            }
            out.isList = true; out.s = "Seq";
            return out;
        }
        if (m == "grep") {
            Value out = Value::array(); out.isList = true; out.s = "Seq"; // Rakudo: .grep is lazy
            if (args.empty()) return out;
            // adverbs: :v values (default), :k indices, :kv, :p pairs
            std::string adv = "v";
            Value mt; bool haveMt = false;
            for (auto& a : args) {
                if (a.t == VT::Pair && (a.s == "k" || a.s == "v" || a.s == "kv" || a.s == "p")) {
                    if (!a.pairVal || a.pairVal->truthy()) adv = a.s;
                    else if (a.s == "v") // :!v is an error (specifying "not values" is meaningless)
                        throw RakuError{Value::typeObj("X::Adverb"), "Cannot use :!v adverb with grep"};
                }
                else if (!haveMt) { mt = a; haveMt = true; }
            }
            if (!haveMt) return out;
            if (mt.t == VT::Bool)
                throw RakuError{Value::typeObj("X::Match::Bool"),
                    "Cannot use Bool as Matcher with '.grep'.  Did you mean to use $_ inside a block?"};
            bool aliasable = inv.t == VT::Array && inv.arr && items.size() == inv.arr->size();
            auto emit = [&](size_t idx, const Value& v) {
                if (adv == "k") out.arr->push_back(Value::integer((long long)idx));
                else if (adv == "kv") { out.arr->push_back(Value::integer((long long)idx)); out.arr->push_back(v); }
                else if (adv == "p") { Value pr = Value::pair(std::to_string(idx), v); pr.pairKey = std::make_shared<Value>(Value::integer((long long)idx)); out.arr->push_back(pr); }
                else out.arr->push_back(v);
            };
            size_t ar = mt.t == VT::Code ? codeArity(mt) : 1; // arity-N blocks test N at a time
            if (ar < 1) ar = 1;
            for (size_t gi = 0; gi < items.size(); gi += ar) {
                Value v = items[gi];
                bool match;
                if (mt.t == VT::Code) {
                    ValueList ca;
                    for (size_t k = 0; k < ar && gi + k < items.size(); k++) ca.push_back(items[gi + k]);
                    if (aliasable && ar == 1) topicWriteback_ = &(*inv.arr)[gi]; // $_ mutations alias the element
                    try { match = callCallable(mt, ca).truthy(); }
                    catch (LastEx&) { topicWriteback_ = nullptr; break; }   // `last` in the block ends the grep
                    catch (NextEx&) { topicWriteback_ = nullptr; continue; } // `next` skips the element
                    catch (RedoEx&) { topicWriteback_ = nullptr; gi -= ar; continue; } // `redo` retries it
                    if (aliasable && ar == 1) v = (*inv.arr)[gi];
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
            if (inv.hash) *h.hash = *inv.hash;
            return h;
        }
        if (m == "hash" && inv.t == VT::Hash) return inv;   // %h.hash is the hash itself
        if (m == "Map" && inv.t == VT::Hash) { // %h.Map — an immutable view (detached copy)
            Value h = Value::makeHash();
            if (inv.hash) *h.hash = *inv.hash;
            h.hashKind = "Map";
            return h;
        }
        if ((m == "hash" || m == "Hash" || m == "Map") && inv.t == VT::Array) { // list -> Hash/Map
            // Pairs map directly; non-Pair elements pair up CONSECUTIVELY as
            // key, value — `(0,"a",1,"b").hash` is {0 => "a", 1 => "b"}, so
            // `@a.kv.reverse.hash` inverts an index map (value => index).
            Value h = Value::makeHash();
            for (size_t k = 0; k < items.size(); k++) {
                if (items[k].t == VT::Pair)
                    (*h.hash)[items[k].s] = items[k].pairVal ? *items[k].pairVal : Value::any();
                else if (k + 1 < items.size()) {
                    std::string key = items[k].toStr(); // sequenced explicitly: in `m[f(k)] = g(++k)`
                    (*h.hash)[key] = items[++k];        // the RHS would evaluate before the key!
                }
                else // odd trailing key (Rakudo dies; we stay lenient)
                    (*h.hash)[items[k].toStr()] = Value::any();
            }
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
                if (a.t == VT::Hash && a.hash) {
                    for (auto& kv : *a.hash) flat.push_back(Value::pair(kv.first, kv.second));
                    continue;
                }
                if (a.t == VT::Array || a.t == VT::Range) { for (auto& x : a.flatten()) flat.push_back(x); continue; }
                flat.push_back(a);
            }
            for (auto& a : flat) {
                if (a.t != VT::Pair) continue;
                std::string key = a.s; Value val = a.pairVal ? *a.pairVal : Value::any();
                auto it = inv.hash->find(key);
                if (it == inv.hash->end()) {
                    // a NEW key stores the value as it is; only a LIST value spreads
                    // (append and push agree here — it is the existing-key branch that
                    // tells them apart)
                    if (val.t == VT::Array && val.isList) { Value ar = Value::array(); for (auto& x : val.flatten()) ar.arr->push_back(x); (*inv.hash)[key] = ar; }
                    else (*inv.hash)[key] = val;
                } else {
                    if (it->second.t != VT::Array) { Value ar = Value::array(); ar.arr->push_back(it->second); it->second = ar; }
                    if (m == "append") for (auto& x : val.flatten()) it->second.arr->push_back(x);
                    else it->second.arr->push_back(val);
                }
            }
            return inv;
        }
        if (m == "keys") {
            Value out = Value::array();
            // Set/Bag/Mix recover the element's original type from the count's pairKey.
            if (inv.t == VT::Hash) { for (auto& kv : *inv.hash) out.arr->push_back(hashEntryKey(inv, kv.first, kv.second)); }
            else for (size_t i = 0; i < items.size(); i++) out.arr->push_back(Value::integer((long long)i));
            out.isList = true;
            return out;
        }
        if (m == "invert" && inv.t == VT::Array) { // (a=>1, b=>2).invert -> Seq of value=>key
            Value out = Value::array(); out.isList = true; out.s = "Seq";
            auto push1 = [&](const Value& v, const Value& k) {
                Value p = Value::pair(v.toStr(), k);
                if (v.t != VT::Str) p.pairKey = std::make_shared<Value>(v); // keep a numeric key numeric
                out.arr->push_back(std::move(p));
            };
            for (auto& e : items) if (e.t == VT::Pair) {
                Value val = e.pairVal ? *e.pairVal : Value::any();
                Value key = e.pairKey ? *e.pairKey : Value::str(e.s);
                if (val.t == VT::Array && val.arr) for (auto& vv : *val.arr) push1(vv, key);
                else push1(val, key);
            }
            return out;
        }
        if (m == "invert" && inv.t == VT::Hash) { // %h.invert -> list of (value => key)
            Value out = Value::array(); out.isList = true; out.s = "Seq";
            auto push1 = [&](const Value& v, const Value& k) {
                Value p = Value::pair(v.toStr(), k);
                if (v.t != VT::Str) p.pairKey = std::make_shared<Value>(v); // a numeric value stays a numeric key
                out.arr->push_back(std::move(p));
            };
            for (auto& kv : *inv.hash) {
                Value key = hashEntryKey(inv, kv.first, kv.second);
                if (kv.second.t == VT::Array && kv.second.arr)
                    for (auto& v : *kv.second.arr) push1(v, key);
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
                if (a2.t == VT::Pair && a2.s == "as" && a2.pairVal) { asF = *a2.pairVal; continue; }
                if (!haveMapper) { mapper = a2; haveMapper = true; continue; }
                if (a2.t == VT::Array && a2.ext)
                    throw RakuError{Value::typeObj("X::Cannot::Lazy"), "Cannot " + m + " a lazy list"};
                if (a2.t == VT::Range || a2.t == VT::Array) { for (auto& x : a2.flatten()) vals.push_back(x); }
                else vals.push_back(a2);
            }
            int runMode = 0; // 0 unset, 1 flat keys, 2 nested key-paths (mixing throws)
            for (auto& v : vals) {
                Value cat;
                if (mapper.t == VT::Code) cat = callCallable(mapper, ValueList{v});
                else if (mapper.t == VT::Hash) {
                    if (!mapper.hash) continue;
                    auto f = mapper.hash->find(v.toStr());
                    if (f == mapper.hash->end()) continue;
                    cat = f->second;
                }
                else if (mapper.t == VT::Array) {
                    long long i = v.toInt();
                    if (!mapper.arr || i < 0 || (size_t)i >= mapper.arr->size()) continue;
                    cat = (*mapper.arr)[i];
                }
                else continue;
                if (cat.t == VT::Nil || cat.t == VT::Any) continue; // Nil category: skip the value
                ValueList cats;
                if (m == "categorize-list" && cat.t == VT::Array) {
                    if (!cat.arr || cat.arr->empty()) continue;
                    cats = *cat.arr;
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
                        Value& slot = (*inv.hash)[c.toStr()];
                        if (baggy) {
                            if (inv.hashKind == "BagHash")
                                slot = Value::integer((slot.t == VT::Int ? slot.i : 0) + 1);
                            else
                                slot = Value::number((slot.isNumeric() ? slot.toNum() : 0.0) + 1.0);
                        } else {
                            if (slot.t != VT::Array || !slot.arr) { slot = Value::array(); slot.itemized = true; }
                            slot.arr->push_back(sv);
                        }
                    } else { // key path: descend/autovivify nested hashes, push at the leaf
                        if (!c.arr || c.arr->empty()) continue;
                        // mutable cursor for the descent; the copy shares inv's
                        // hash shared_ptr, so the autovivified writes still land in
                        // the caller's container (see the same pattern in Part3)
                        Value invLocal = inv;
                        Value* curH = &invLocal;
                        for (size_t k = 0; k + 1 < c.arr->size(); k++) {
                            Value& slot = (*curH->hash)[(*c.arr)[k].toStr()];
                            if (slot.t != VT::Hash || !slot.hash) { slot = Value::makeHash(); slot.itemized = true; }
                            curH = &slot;
                        }
                        Value& slot = (*curH->hash)[c.arr->back().toStr()];
                        if (slot.t != VT::Array || !slot.arr) { slot = Value::array(); slot.itemized = true; }
                        slot.arr->push_back(sv);
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
            for (auto& kv : *inv.hash) {
                Value p = Value::pair(kv.second.toStr(), hashEntryKey(inv, kv.first, kv.second));
                if (kv.second.t != VT::Str) p.pairKey = std::make_shared<Value>(kv.second); // numeric value -> numeric key
                out.arr->push_back(std::move(p));
            }
            return out;
        }
        if (m == "pairup") { // (1,2,3,4).pairup → (1=>2, 3=>4); odd tail pairs with Any
            Value out = Value::array(); out.isList = true; out.s = "Seq";
            for (size_t i = 0; i < items.size(); i += 2) {
                Value key = items[i];
                Value val = (i + 1 < items.size()) ? items[i + 1] : Value::any();
                if (key.t == VT::Pair) out.arr->push_back(key); // an already-Pair element passes through
                else out.arr->push_back(Value::pair(key.toStr(), val));
            }
            return out;
        }
        if (m == "pairs" || m == "kv" || m == "antipairs") {
            Value out = Value::array(); out.isList = true; out.s = "Seq";
            if (inv.t == VT::Hash) {
                for (auto& kv : *inv.hash) {
                    Value key = hashEntryKey(inv, kv.first, kv.second);
                    if (m == "kv") { out.arr->push_back(key); out.arr->push_back(kv.second); }
                    else if (m == "antipairs") { Value p = Value::pair(kv.second.toStr(), key); out.arr->push_back(std::move(p)); }
                    else { Value p = Value::pair(kv.first, kv.second);
                           if (key.t != VT::Str) p.pairKey = std::make_shared<Value>(std::move(key));
                           out.arr->push_back(std::move(p)); }
                }
            } else {
                for (size_t i = 0; i < items.size(); i++) {
                    if (m == "kv") { out.arr->push_back(Value::integer((long long)i)); out.arr->push_back(items[i]); }
                    else if (m == "antipairs") { // value => index
                        Value p = Value::pair(items[i].toStr(), Value::integer((long long)i));
                        p.pairKey = std::make_shared<Value>(items[i]);
                        out.arr->push_back(p);
                    }
                    else {
                        Value p = Value::pair(std::to_string(i), items[i]);
                        p.pairKey = std::make_shared<Value>(Value::integer((long long)i)); // Int keys
                        out.arr->push_back(p);
                    }
                }
            }
            return out;
        }
        // mutators on real arrays
        if (inv.t == VT::Array && inv.arr) {
            // a native-typed array (`my str @a`, `my int @a`) rejects a value of the
            // wrong native kind — str takes Str, int/uint/byte take Int, num takes Real
            auto natCheck = [&](const Value& v) {
                if (inv.ofType.empty()) return;
                std::string bt = inv.ofType.substr(0, inv.ofType.find(','));
                bool isNat = bt == "str" || bt == "byte" || bt.compare(0, 3, "int") == 0 ||
                             bt.compare(0, 4, "uint") == 0 || bt.compare(0, 3, "num") == 0;
                if (!isNat) return; // boxed-type arrays keep their existing behaviour
                bool ok = bt == "str" ? (v.t == VT::Str || v.isAllomorph()) // an allomorph's Str side
                        : bt.compare(0, 3, "num") == 0 ? v.isNumeric()
                        : (v.t == VT::Int || v.t == VT::Bool);
                if (!ok) throw RakuError{Value::typeObj("X::TypeCheck::Binding"),
                    "Type check failed in binding; expected " + bt + " but got " + v.typeName() + " (" + typeCheckRepr(v) + ")"};
            };
            // a shaped array (`my @a[2;2]`) has fixed dimensions — size-changing
            // operations are illegal
            if (inv.shape && !inv.shape->empty()) {
                static const std::set<std::string> fixedIllegal = {
                    "push", "append", "pop", "unshift", "prepend", "shift",
                    "splice", "reverse", "rotate"};
                if (fixedIllegal.count(m))
                    throw RakuError{Value::typeObj("X::IllegalOnFixedDimensionArray"),
                        "Cannot " + m + " a fixed-dimension array"};
            }
            if (m == "push" || m == "unshift" || m == "append" || m == "prepend") for (auto& a : args) natCheck(a);
            // a native-int element array (`uint32 @W`) wraps each stored value to
            // its bit width (SHA1's `@W.push: S(...)` relies on uint32 overflow)
            auto natMask = [&](Value v) -> Value {
                bool sign; int bits = Value::natWidthOfType(inv.ofType, sign);
                if (bits > 0 && bits < 64 && (v.t == VT::Int || v.t == VT::Bool)) {
                    unsigned long long u = (unsigned long long)v.toInt() & ((1ULL << bits) - 1);
                    long long x = (sign && (u & (1ULL << (bits - 1)))) ? (long long)u - (long long)(1ULL << bits) : (long long)u;
                    return Value::integer(x);
                }
                return v;
            };
            // push/unshift add each argument as one element; append/prepend flatten
            if (m == "push") { for (auto& a : args) inv.arr->push_back(natMask(a)); return inv; } // returns the array (shared storage)
            // append/prepend follow the single-argument rule: a lone Positional arg is
            // treated as the list of values (flattened one level); multiple args are each
            // added as-is (nested lists preserved, exactly like push).
            auto appendValues = [](ValueList& args) -> ValueList {
                if (args.size() == 1 && args[0].t == VT::Array && args[0].arr)
                    return *args[0].arr;   // one-level: the sole list's own elements
                return args;               // 2+ args: each as-is
            };
            if (m == "append") { for (auto& a : appendValues(args)) inv.arr->push_back(a); return inv; }
            if (m == "unshift") { inv.arr->insert(inv.arr->begin(), args.begin(), args.end()); return inv; }
            if (m == "prepend") { auto f = appendValues(args); inv.arr->insert(inv.arr->begin(), f.begin(), f.end()); return inv; }
            // popping/shifting an EMPTY Array yields a FAILURE, not a bare
            // undefined value: it boolifies False — so `while @a.shift -> $x`
            // terminates, which is how Cro's router drains its handler queue —
            // but detonates with X::Cannot::Empty the moment the value is USED.
            if (m == "pop" || m == "shift") {
                if (inv.arr->empty()) {
                    Value f = Value::makeHash(); f.hashKind = "Failure";
                    (*f.hash)["exception"] = makeTypedEx("X::Cannot::Empty",
                        {{"action", Value::str(m)}, {"what", Value::str("Array")}},
                        "Cannot " + m + " from an empty Array");
                    (*f.hash)["message"] = Value::str("Cannot " + m + " from an empty Array");
                    return f;
                }
                Value v = m == "pop" ? inv.arr->back() : inv.arr->front();
                if (m == "pop") inv.arr->pop_back(); else inv.arr->erase(inv.arr->begin());
                if (v.t == VT::Array) v.itemized = true;
                return v;
            }
            if (m == "splice") { // .splice($start?, $count?, *@replacement) → the removed elements
                // a lazy array only holds a prefix — materialize enough to cover the window
                if (inv.ext) {
                    long s0 = args.size() > 0 ? args[0].toInt() : 0;
                    materializeLazy(inv, args.size() > 1 ? (size_t)(std::max(0L, s0) + args[1].toInt()) : 1000000);
                }
                long n = (long)inv.arr->size();
                // `*-2` / `{ $_ - 2 }` resolve against the length, like .head/.tail
                // do — .toInt() on a WhateverCode is 0, so `splice(*-2, *-1)` was
                // splicing nothing at the front.
                auto resolve = [&](const Value& a, long long sz) -> long {
                    if (a.t == VT::Whatever) return (long)sz;
                    if (a.t == VT::Code) { ValueList one{Value::integer(sz)}; return (long)callCallable(const_cast<Value&>(a), one).toInt(); }
                    return (long)a.toInt();
                };
                long start = args.size() > 0 ? resolve(args[0], n) : 0;
                if (start < 0) start += n;
                start = std::max(0L, std::min(start, n));
                // the COUNT resolves against what is left after the start
                long count = args.size() > 1 ? resolve(args[1], n - start) : (n - start);
                count = std::max(0L, std::min(count, n - start));
                Value removed = Value::array(); // the removed elements are an Array
                for (long k = 0; k < count; k++) removed.arr->push_back((*inv.arr)[start + k]);
                ValueList repl;
                for (size_t k = 2; k < args.size(); k++) for (auto& x : toList(args[k])) repl.push_back(x);
                inv.arr->erase(inv.arr->begin() + start, inv.arr->begin() + start + count);
                inv.arr->insert(inv.arr->begin() + start, repl.begin(), repl.end());
                return removed;
            }
        }
        if (inv.t == VT::Hash && inv.hash) {
            if (m == "exists") return Value::boolean(inv.hash->count(a0().toStr()) > 0);
        }
    }

    // an undefined scalar still reports as a 1-item list for .elems (Any.elems == 1)
    if ((inv.t == VT::Any || inv.t == VT::Nil) && m == "elems") return Value::integer(1);
    // method form of EVAL: '1+2'.EVAL — dispatch to the builtin sub
    if (m == "EVAL" && inv.t == VT::Str) {
        auto it = builtins_.find("EVAL");
        if (it != builtins_.end()) {
            ValueList a; a.push_back(inv);
            for (auto& x : args) a.push_back(x);
            return it->second(*this, a);
        }
    }
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
            BigInt base = inv.big ? *inv.big : BigInt(inv.toInt());
            BigInt e = args[0].big ? *args[0].big : BigInt(args[0].toInt());
            BigInt mod = args[1].big ? *args[1].big : BigInt(args[1].toInt());
            if (mod.isZero()) return Value::integer(0);
            auto modOf = [&](const BigInt& x) { BigInt q, r; BigInt::divmod(x, mod, q, r); if (r.sign < 0) r = r + mod; return r; };
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
        if (!tctx_.gatherStack.empty()) {
            auto& coll = *tctx_.gatherStack.back();
            coll.push_back(inv);
            size_t lim = tctx_.gatherLimits.empty() ? 0 : tctx_.gatherLimits.back();
            if (lim && coll.size() >= lim) throw StopGatherEx{};
        }
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
        };
        if (listCool.count(m)) {
            // toList keeps a plain scalar as one item but expands a Blob/Buf to
            // its bytes (`$blob.rotor(3, :partial)` in Base64 chunks byte-wise)
            Value l = Value::array(); *l.arr = toList(inv); l.isList = true;
            return methodCall(l, m, std::move(args), rwArgs);
        }
    }
    // Real numification protocol: built-in numerics answer .Bridge with a Num
    if (m == "Bridge" && (inv.t == VT::Int || inv.t == VT::Num || inv.t == VT::Rat || inv.t == VT::Bool))
        return Value::number(inv.toNum());
    // `has $.b handles <m1 m2>` — an unknown method listed in an attribute's
    // handles trait is delegated to that attribute's value.
    if (inv.t == VT::Object && inv.obj && inv.obj->cls) {
        for (ClassInfo* c = inv.obj->cls.get(); c; c = c->parent.get()) {
            for (auto& a : c->attrs)
                for (auto& h : a.handles)
                    if (h == m || h == "*") { // `handles *` delegates any unknown method
                        auto ait = inv.obj->attrs.find(a.name);
                        Value target = ait != inv.obj->attrs.end() ? ait->second : Value::any();
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
    if (inv.t == VT::Object && inv.obj && inv.obj->cls && m != "Bridge") {
        if (Value* br = inv.obj->cls->findMethod("Bridge")) {
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
    return std::nullopt;   // not handled here — fall through to the caller's tail
}

} // namespace rakupp
