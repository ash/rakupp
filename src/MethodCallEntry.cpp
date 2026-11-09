// MethodCallEntry.cpp — what a plain value answers before the method chain
//
// methodCall's first arms (Builtins.cpp) hand a call here before the chain,
// whose arms for the commonest questions about a plain core value sit
// thousands of lines down it, and the two type questions that `~~` and `.isa`
// ask of one, kept per symbol generation. Each answer is the one the chain's
// own arm gives; anything tagged, user-declared or extended goes the long way.
#include "BuiltinsParts.h"
#include <array>
#include <atomic>
#include <string_view>

namespace rakupp {

extern std::atomic<uint64_t> g_symbolGen;   // Interpreter.cpp: moves on every structural symbol change

// `$x ~~ Int`, `when Pair`, `where Str` — a CORE type object against a plain
// built-in value. applyArithGeneral answers it at the bottom of two thousand
// lines of operator tests, so each `when` of a type-dispatching `given` cost a
// few hundred nanoseconds: a third of Mathematica::Serializer's encoder (#217).
// For the values below the answer depends on the value's kind and the type's
// name alone, so it is asked once per kind, type and symbol generation (every
// class, role, subset or augment declared moves it) and kept.
//
// The kinds: untagged (no allomorph, Instant, Buf, IO, Failure, Map …), no enum,
// no native width, and for a Hash or a list no cold block (element or key type,
// laziness, shape) — doubled for an itemized one. An Int is two kinds, by its
// sign, which is all UInt asks. 0: none of them.
static unsigned typeMatchShape(const Value& v) {
    if (!v.hashKind.empty() || !v.enumName.empty() || !v.enumType.empty() || v.natBits || v.natFloat) return 0;
    unsigned k;
    switch (v.t) {
        case VT::Str:   k = 1; break;
        case VT::Int:   k = (v.big() ? v.big()->sign < 0 : v.i < 0) ? 10 : 2; break;
        case VT::Num:   k = 3; break;
        case VT::Rat:   k = v.fatRat() ? 5 : 4; break;
        case VT::Pair:  k = 6; break;
        case VT::Hash:  if (v.x_.get() || v.objKeyed) return 0; k = 7; break;
        case VT::Array: if (v.x_.get() || !v.s.empty()) return 0; k = v.isList ? 9 : 8; break;
        default: return 0;
    }
    return k * 2 + v.itemized;   // (below typeMatchMemo_'s 22)
}
// The core types it keeps answers for — never one a user subset or class has
// taken the name of (its `where` or ACCEPTS asks the value), and nothing
// parameterized or coercing (the caller refuses a type object with a cold
// block or a smiley).
static constexpr std::string_view kTypeMatchNames[] = {
    "Pair", "Str", "Int", "Rat", "FatRat", "Num", "Complex", "Numeric", "Real", "Rational", "Cool",
    "Bool", "Seq", "Positional", "Associative", "Map", "Hash", "Array", "List", "Iterable", "Stringy",
    "Blob", "Buf", "DateTime", "Date", "Dateish", "Callable", "Code", "Block", "Routine", "Sub",
    "Method", "Range", "Whatever", "WhateverCode", "HyperWhatever", "Junction", "Any", "Mu", "Nil",
    "Failure", "Exception", "Match", "Regex", "Set", "Bag", "Mix", "QuantHash", "Setty", "Baggy",
    "Mixy", "Capture", "Version", "Instant", "Duration", "Slip", "Enumeration", "IO::Path", "Uni",
    "UInt"};
static_assert(std::size(kTypeMatchNames) < 64, "typeMatchMemo_ has a row per name, and row 0 is none");
static unsigned typeMatchHash(const char* p, size_t n) {
    return (unsigned(n) * 31u + (unsigned char)p[0] * 7u + (unsigned char)p[n / 2] * 3u +
            (unsigned char)p[n - 1]) & 255u;
}
static unsigned typeMatchCode(std::string_view n) {
    static const std::array<uint8_t, 256> table = [] {
        std::array<uint8_t, 256> t{};
        for (unsigned i = 0; i < std::size(kTypeMatchNames); i++) {
            unsigned h = typeMatchHash(kTypeMatchNames[i].data(), kTypeMatchNames[i].size());
            while (t[h]) h = (h + 1) & 255u;
            t[h] = uint8_t(i + 1);
        }
        return t;
    }();
    if (n.empty() || n.size() > 13) return 0;
    for (unsigned h = typeMatchHash(n.data(), n.size()); table[h]; h = (h + 1) & 255u)
        if (kTypeMatchNames[table[h] - 1] == n) return table[h];
    return 0;
}
// One memo slot: gen << 2 | 1 False, 2 True, 3 a name the program declared
// (not kept). `fill` asks the general path, whose Bool is kept.
template <class Fill>
static bool typeMemoRead(Interpreter& I, std::atomic<uint64_t>& slot, const Value& type, Value& out, Fill fill) {
    const uint64_t gen = g_symbolGen.load(std::memory_order_relaxed) + 1;
    const uint64_t seen = slot.load(std::memory_order_relaxed);
    if ((seen >> 2) == gen) {
        if ((seen & 3) == 3) return false;
        out = Value::boolean((seen & 3) == 2);
        return true;
    }
    if (I.subsets_.count(type.s) || I.classes_.count(type.s)) {
        slot.store(gen << 2 | 3, std::memory_order_relaxed);
        return false;
    }
    out = fill();
    if (out.t == VT::Bool) slot.store(gen << 2 | (out.b ? 2 : 1), std::memory_order_relaxed);
    return true;
}
// …and the type object itself: a plain core one, no smiley, parameter or coercion
static bool typeMemoType(const Value& r) {
    return r.t == VT::Type && !r.i && !r.x_.get() && r.hashKind.empty() && r.enumType.empty();
}
bool Interpreter::typeMatchMemo(const std::string& op, const Value& l, const Value& r, Value& out) {
    if (r.t != VT::Type || op.size() != 2 || op[0] != '~' || op[1] != '~') return false;
    const unsigned k = typeMatchShape(l);
    // (a user metaobject, a one-shot value match pending and a user `infix:<~~>`
    // all change who answers; the memo stays out of their way)
    if (!k || !typeMemoType(r) || valueSmartmatch_ || haveCustomHows_.load(std::memory_order_relaxed) ||
        boundInfixPossible(op))
        return false;
    const unsigned t = typeMatchCode(r.s);
    return t && typeMemoRead(*this, typeMatchMemo_[t][k], r, out, [&] {
        return applyArith(op, l, r);   // (applyBinOp's own arms are for other shapes)
    });
}
// `$x.isa(Whatever)` — Mathematica::Serializer asks three of these of every
// value it encodes, and each walked the whole method dispatch to Part3's arm.
// The arm answers from the value's type name, so a plain value's kind decides
// it as it decides `~~`. Not a list (methodCall has container arms for those),
// and not while a built-in type has a method added or wrapped, a user
// metaobject is about, or --race watches the reads.
bool Interpreter::isaMemo(const Value& inv, const std::string& m, const ValueList& args,
                          const std::vector<ExprPtr>* rwArgs, Value& out) {
    if (!opEq(m, "isa") || args.size() != 1 || inv.t == VT::Array) return false;
    const unsigned k = typeMatchShape(inv);
    if (!k || !typeMemoType(args[0]) || !builtinExt_.empty() ||
        haveCustomHows_.load(std::memory_order_relaxed) || RAKUPP_RACE_ON())
        return false;
    const unsigned t = typeMatchCode(args[0].s);
    return t && typeMemoRead(*this, isaMemo_[t][k], args[0], out, [&] {
        Value r = methodCallInner(inv, m, ValueList(args), rwArgs, false);
        if (RAKUPP_UNLIKELY(isNoMethod(r))) throwNoMethod(std::move(r));
        if (RAKUPP_UNLIKELY(r.t == VT::Pair && r.hashKind == kThrowTag)) throwMarked(std::move(r));
        return r;
    });
}

std::optional<Value> Interpreter::methodCallEntry(const Value& inv, const std::string& m, ValueList& args,
                                                  const std::vector<ExprPtr>* rwArgs) {
    if (Value r; m.size() == 3 && isaMemo(inv, m, args, rwArgs, r)) return r;
    // `Buf.new(…)` of machine Ints and `.allocate(n[, fill])`: Part1c's arm
    // (bufNewOrAllocate) without the walk to it, and without the re-entry into
    // methodCall every `.new` makes to catch a BUILD's Failure — 310 ns a buffer
    // against Rakudo's 56 (#217). A user class, package or alias of the name, a
    // sized or parameterized buffer, a method added to a built-in type and a
    // user metaobject all take the long way.
    if (inv.t == VT::Type && m.size() >= 3 && (opEq(m, "new") || opEq(m, "allocate")) &&
        (inv.s == "Buf" || inv.s == "Blob") && !inv.i && !inv.x_.get() && inv.hashKind.empty() &&
        inv.enumType.empty() && builtinExt_.empty() && !haveCustomHows_.load(std::memory_order_relaxed) &&
        !RAKUPP_RACE_ON() && !classes_.count(inv.s) && !classAliases_.count(inv.s) && !pkgKind_.count(inv.s)) {
        const bool allocate = m[0] == 'a';
        bool words = !allocate || args.size() == 1 || args.size() == 2;
        for (auto& a : args) if (a.t != VT::Int || a.big()) { words = false; break; }
        if (words && (!allocate || args[0].i >= 0)) return bufNewOrAllocate(inv, allocate, args);
    }
    // `$s.subst($str, $repl, :g)` where the pattern's bytes do not occur, so no
    // character of it can: the string as it is, which is what methodCallPart3's
    // literal arm answers after walking the chain and substSelect's adverb setup
    // (neither touches `$/`). Mathematica::Serializer escapes the quotes of
    // every string it encodes this way, and most strings have none (#217). Any
    // adverb but :g, an empty or tagged pattern, and a tagged invocant take
    // the long way.
    if (inv.t == VT::Str && m.size() == 5 && opEq(m, "subst") && inv.hashKind.empty() && inv.enumName.empty() &&
        builtinExt_.empty()) {
        const Value* pat = nullptr;
        size_t npos = 0;
        bool plain = true;
        for (auto& a : args) {
            if (a.t == VT::Pair && a.namedArg) { plain = plain && (a.s == "g" || a.s == "global"); continue; }
            if (npos++ == 0) pat = &a;
        }
        if (plain && npos == 2 && pat->t == VT::Str && pat->hashKind.empty() && pat->enumName.empty() &&
            !pat->s.empty() && inv.s.str().find(pat->s.str()) == std::string::npos)
            return Value::str(inv.s);
    }
    // The commonest zero-argument coercions and queries on a PLAIN core value,
    // answered before the general chain, which spent ~150-250 ns reaching them
    // in an interpreted loop. Each arm answers what the chain's own arm does.
    // Anything tagged goes the long way: an enum value or type, an allomorph, a
    // native, a big Int, and a lazy, packed, shaped or Seq array.
    // (The plain-value test and the `.elems` one are Interpreter.h's, which -O's
    // rtMElems and rtMDefined make too.)
    if (args.empty() && m.size() >= 2 && m.size() <= 7 && rtQueryPlain(inv) && builtinExt_.empty()) {
        // `.WHICH` of a plain value — the general arm's own answer (whichOf)
        auto which = [&]() {
            Value w = Value::str(whichOf(inv));
            w.hashKind = whichIsObjAt(inv) ? "ObjAt" : "ValueObjAt";
            return w;
        };
        switch (inv.t) {
            case VT::Num:
                if (opEq(m, "Num")) return Value::number(inv.n);
                if (opEq(m, "defined")) return Value::boolean(true);
                if (opEq(m, "WHICH")) return which();
                break;
            case VT::Int:
                if (inv.big()) break;
                if (opEq(m, "Int")) return Value::integer(inv.i);
                if (opEq(m, "Str")) return Value::str(std::to_string(inv.i));
                if (opEq(m, "Num")) return Value::number((double)inv.i);
                if (opEq(m, "defined")) return Value::boolean(true);
                if (opEq(m, "WHICH")) return which();
                break;
            case VT::Str:
                if (opEq(m, "Str")) return inv;
                if (opEq(m, "defined")) return Value::boolean(true);
                if (opEq(m, "WHICH")) return which();
                break;
            case VT::Bool:
                if (opEq(m, "WHICH")) return which();
                break;
            // `.key` / `.value`: MethodCallTail.cpp's Pair arm, as -O's rtMKey/rtMValue make it
            case VT::Pair:
                if (opEq(m, "key")) return rtMKey(*this, inv);
                if (opEq(m, "value")) return rtMValue(*this, inv);
                break;
            case VT::Any:
            case VT::Nil:
                if (opEq(m, "defined")) return Value::boolean(false);
                break;
            // a CORE type object (a user class may declare its own WHICH)
            case VT::Type:
                if (opEq(m, "WHICH") && !classes_.count(inv.s)) return which();
                break;
            case VT::Array:
                if (opEq(m, "elems") && rtElemsPlain(inv)) { raceLen(inv.arr(), RaceAcc::Read); return Value::integer((long long)inv.arr()->size()); }
                // `.kv` of a plain Array: (0, a, 1, b, …), the Seq the list arm builds
                if (opEq(m, "kv") && inv.arr() && inv.s.empty() && !inv.ext() &&
                    inv.pk_ != PK::Packed && !(inv.shape() && !inv.shape()->empty()) &&
                    !inv.holdsContainers() && !inv.elemDefault() && inv.ofType().empty()) {
                    Value out = Value::array(); out.isList = true; out.s = "Seq"; raceLen(inv.arr(), RaceAcc::Read);
                    const ValueList& src = *inv.arr();
                    out.arr()->reserve(src.size() * 2);
                    for (size_t i = 0; i < src.size(); i++) {
                        out.arr()->push_back(Value::integer((long long)i));
                        out.arr()->push_back(src[i]);
                        // (a nested Array or Hash of an Array is in a container)
                        Value& e = out.arr()->back();
                        if (!inv.isList && (e.t == VT::Array || e.t == VT::Hash)) e.itemized = true;
                    }
                    return out;
                }
                break;
            // `.values` of a plain Hash: a Seq of its values, as the hash arm and
            // methodCall's kv-family step make it
            case VT::Hash:
                // `.elems` (and `.end`) of a plain Hash: its size. The general
                // arm counted what toList made of it, a Pair built for every entry.
                if ((opEq(m, "elems") || opEq(m, "end")) && inv.hash() && !inv.ext()) {
                    raceKeys(inv.hash(), RaceAcc::Read);
                    return Value::integer((long long)inv.hash()->size() - (m[0] == 'e' && m[1] == 'n'));
                }
                if (opEq(m, "values") && inv.hash()) {
                    Value out = Value::array(); out.isList = true; out.s = "Seq"; raceKeys(inv.hash(), RaceAcc::Read);
                    out.arr()->reserve(inv.hash()->size());
                    for (auto& kv : *inv.hash()) out.arr()->push_back(kv.second);
                    return out;
                }
                break;
            default: break;
        }
    }
    return std::nullopt;
}

// A Pod block's `.raku`, as Rakudo writes it: the constructor that builds it,
// `Pod::Heading.new(level => 1, config => {}, contents => [Pod::Block::Para.new(…)])`,
// each block inside it the same way. It was the block's text.
std::string Interpreter::podRaku(const Value& v) {
    if (v.t == VT::Array && v.arr()) {
        std::string o = "[";
        for (size_t i = 0; i < v.arr()->size(); i++) o += (i ? ", " : "") + podRaku((*v.arr())[i]);
        return o + "]";
    }
    if (!(v.t == VT::Hash && v.hashKind == "Pod" && v.hash())) return methodCall(v, "raku", ValueList{}).toStr();
    const auto& h = *v.hash();
    auto pc = h.find("podclass");
    // a declarator block's names its declarand and its two parts, the leading
    // one a Str in an Array and the trailing one an Array in one; its text is
    // in neither its `contents`
    if (pc != h.end() && pc->second.s.str() == "Pod::Block::Declarator") {
        auto part = [&](const char* k, bool nested) {
            auto it = h.find(k);
            if (it == h.end() || it->second.t != VT::Str) return std::string("[]");
            const std::string s = methodCall(it->second, "raku", ValueList{}).toStr();
            return nested ? "[[" + s + "],]" : "[" + s + "]";
        };
        auto w = h.find("WHEREFORE");
        return "Pod::Block::Declarator.new(WHEREFORE => " +
               methodCall(w == h.end() ? Value::any() : w->second, "raku", ValueList{}).toStr() +
               ", leading => " + part("leading", false) + ", trailing => " + part("trailing", true) +
               ", config => {}, contents => [])";
    }
    std::string o = (pc != h.end() ? pc->second.s.copyText() : std::string("Pod::Block")) + ".new(";
    for (const char* k : {"name", "type", "meta", "level", "caption", "headers", "term", "config", "contents"}) {
        auto it = h.find(k);
        if (it == h.end() && std::strcmp(k, "config") != 0) continue;   // (a block's config is {} when it has none)
        if (o.back() != '(') o += ", ";
        o += std::string(k) + " => " + (it == h.end() ? std::string("{}") : podRaku(it->second));
    }
    return o + ")";
}

} // namespace rakupp
