// Interpreter.cpp — shared helpers, construction, the mainline run, the precompiled-module cache, --exe bundling
//
// One of the parts InterpreterParts.h lists; what they share is declared there.
#include "InterpreterParts.h"

namespace rakupp {


// Thread-local RNG state: drand48's process-global state is not thread-safe, so
// under parallel execution each thread keeps its own erand48 seed.
// Does a CATCH block contain `when`/`default` clauses? If so, an exception that
// matches none of them is NOT handled and must rethrow; a CATCH with only plain
// statements is an unconditional handler.
static thread_local bool g_rand_seeded = false;
static thread_local unsigned short g_rand_xs[3];
// `srand($seed)` reseeds the generator (Raku returns the seed used). NB rakupp's PRNG
// is erand48, not MoarVM's — the same seed does NOT reproduce Rakudo's exact sequence.
void srandSeed(long long s) {
    g_rand_seeded = true;
    g_rand_xs[0] = (unsigned short)s; g_rand_xs[1] = (unsigned short)(s >> 16); g_rand_xs[2] = (unsigned short)(s >> 32);
}
// --seed=N (the CLI): a thread's FIRST use of the generator seeds from N
// instead of time+pid+thread, so a run reproduces. The program's thread comes
// first and gets N itself (so `--seed=7` is `srand(7)` on line 0); each later
// thread gets the next number, so workers draw distinct sequences rather than
// one shared one. An explicit srand() still wins after it, as it always did.
static std::atomic<long long> g_seedOverride{0};
static std::atomic<long long> g_seedUsers{0};
static std::atomic<bool> g_seedOverrideSet{false};
void rakuppSetSeed(long long s) { g_seedOverride.store(s); g_seedOverrideSet.store(true); }
// --trace (bash -x): a plain global read on every statement — the same cost
// the profiler's hooks were measured at, nothing.
bool g_traceStmts = false;
void rakuppSetTrace(bool on) { g_traceStmts = on; }

// --stagestats: the module-load collector. Loads can nest (a module's own
// `use`) and can happen on a worker thread, so a mutex and a per-thread depth.
bool g_stageStats = false;
std::mutex g_stageMu;
std::vector<StageModuleLoad> g_stageLoads;
void stageStatsEnable(bool on) { g_stageStats = on; }
bool stageStatsOn() { return g_stageStats; }
std::vector<StageModuleLoad> stageModuleLoads() {
    std::lock_guard<std::mutex> lk(g_stageMu);
    return g_stageLoads;
}

// Uniform random double in [0, 1). Seeded once from time+pid if srand wasn't called.
double randDouble() {
    if (!g_rand_seeded) {
        if (g_seedOverrideSet.load(std::memory_order_relaxed)) {
            srandSeed(g_seedOverride.load(std::memory_order_relaxed) + g_seedUsers.fetch_add(1));
            return erand48(g_rand_xs);
        }
        g_rand_seeded = true;
        // 64-bit on purpose: unsigned long is 32 bits on LLP64 (Windows) and
        // wasm32, where a `>> 32` would be undefined.
        unsigned long long s = (unsigned long long)::time(nullptr) ^ ((unsigned long long)::getpid() << 16)
                             ^ (unsigned long long)std::hash<std::thread::id>{}(std::this_thread::get_id());
        g_rand_xs[0] = (unsigned short)s; g_rand_xs[1] = (unsigned short)(s >> 16); g_rand_xs[2] = (unsigned short)(s >> 32);
    }
    return erand48(g_rand_xs);
}

// sha1hex — one line now; the implementation is src/Digest.cpp, shared with the
// `digest` tag's primitives and the Jupyter kernel's message signatures. It was
// a second copy of SHA-1 here until DATA-PLAN P3 lifted them all into one file.

Value applyArith(const std::string& op, const Value& l, const Value& r);

void collectPubAttrs(ClassInfo* c, std::vector<const ClassAttr*>& out) {
    if (!c) return;
    for (auto& a : c->attrs) if (a.pub) out.push_back(&a);
    collectPubAttrs(c->parent.get(), out);
    for (auto& p : c->extraParents) collectPubAttrs(p.get(), out);
}

// Division by zero, and the two shapes it takes. Rakudo is not uniform here and
// Roast pins both: `10 div 0` and `10 % 0` SOFT-FAIL — they return a Failure,
// which throws only when something sinks or uses it (S03-operators/div.t's
// `fails-like`) — while `mod` and `%%` throw on the spot. Making them all throw
// looks tidier and breaks `fails-like`; making them all soft-fail loses the two
// that really do throw.
//
// Under 6.e, `mod` crosses to the soft-fail side: core.e redoes the div/mod
// candidates, and Rakudo 2026.08 answers a Failure for `1 mod 0` under the
// pragma while still throwing under 6.d. (That Rakudo also soft-fails `%%`
// under plain 6.d — roast's throws-like pins accept either shape, since a
// sunk Failure throws too — but `%%` is left eager here until that 6.d-side
// question is taken up on its own.) applyArith is a free function shared with
// compiled code, so the live revision is read through the same
// ctor-set-pointer pattern the NativeCall trampoline uses.
Interpreter* g_revInterp = nullptr;
thread_local const Value* g_rxRoutine = nullptr;   // the regex whose code blocks are running

// X::Numeric::DivideByZero carries WHICH operator hit it and WHAT the numerator
// was — `throws-like … using => 'infix:<%%>', numerator => 9` asks for both, and
// a bare type object has neither to give.
// Rakudo names the operator bare (`%`, `/`, `div`) — only `%%` comes as
// `infix:<%%>`
static std::string divByZeroUsing(const char* opName) {
    return std::string(opName) == "%%" ? std::string("infix:<%%>") : std::string(opName);
}
static Value divByZeroEx(const Value& lhs, const char* opName, const std::string& msg) {
    if (g_revInterp)
        return g_revInterp->makeTypedEx("X::Numeric::DivideByZero",
            {{"using", Value::str(divByZeroUsing(opName))},
             {"numerator", lhs}}, msg);
    return Value::typeObj("X::Numeric::DivideByZero");
}
Value divideByZero(const Value& lhs, const char* opName) {
    std::string msg = "Attempt to divide " + lhs.toStr() +
                      " by zero using " + divByZeroUsing(opName);
    Value f = rakuppNewFailure();
    (*f.hash())["exception"] = divByZeroEx(lhs, opName, msg);
    (*f.hash())["message"] = Value::str(msg);
    return f;
}
[[noreturn]] void throwDivideByZero(const Value& lhs, const char* opName) {
    std::string msg = "Attempt to divide " + lhs.toStr() +
                      " by zero using " + divByZeroUsing(opName);
    throw RakuError{divByZeroEx(lhs, opName, msg), msg};
}
// Which of the two a given operator wants.
static bool divZeroThrows(const std::string& op) {
    return op == "mod" && !(g_revInterp && g_revInterp->sixE());
}
Value divZeroResult(const Value& lhs, const std::string& op) {
    if (divZeroThrows(op)) throwDivideByZero(lhs, op.c_str());
    return divideByZero(lhs, op.c_str());
}

[[noreturn]] void throwImmutable(const Value& v) {
    // a TYPE OBJECT refused is named as one: `my \v = Int; v = 1` is "Cannot
    // modify an immutable 'Int' type object" in Rakudo, not "Int ((Int))"
    if (v.t == VT::Type || v.t == VT::Any) {
        const std::string tn = v.t == VT::Type ? v.s.str() : v.typeName();
        const std::string msg = "Cannot modify an immutable '" + tn + "' type object";
        if (g_revInterp)
            g_revInterp->throwTypedV("X::Assignment::RO", {{"value", v}, {"typename", Value::str(tn)}}, msg);
        throw RakuError{Value::typeObj("X::Assignment::RO"), msg};
    }
    std::string kind = v.t == VT::Hash && !v.hashKind.empty() ? v.hashKind : v.typeName();
    // `.value` names what refused the write (`throws-like …, value => $list`)
    if (g_revInterp)
        g_revInterp->throwTypedV("X::Assignment::RO", {{"value", v}, {"typename", Value::str(kind)}},
                                 "Cannot modify an immutable " + kind + " (" + v.gist() + ")");
    throw RakuError{Value::typeObj("X::Assignment::RO"),
                    "Cannot modify an immutable " + kind + " (" + v.gist() + ")"};
}

// ---- user-defined operators, for compiled code -----------------------------
//
// A user `infix:<+>` SHADOWS the built-in of the same spelling, but only for the
// operand shapes it has a candidate for: `multi infix:<+>(Money, Money)` must
// not stop `1 + 2` working. evalBinary makes that two decisions — gate on an
// object/enum operand, and treat "no candidate took these" as a fall-through to
// the built-in — and compiled code has to make the same two or it answers
// differently from the interpreter, which is exactly what `--exe`, `--jit` and
// `--cnp` all did until these existed. The emitted code calls the user's own
// dispatcher, so the candidate choice itself is the one Codegen already emits.
//
// The gate is why this costs nothing in general: two tag tests before any
// lookup, on an operator the program actually overloaded, and nothing at all on
// one it did not — the emitter only reaches for this when the program declares
// the routine.
Value rtUserInfix(Value (*fn)(ValueList), const char* op, const Value& l, const Value& r) {
    if (l.t == VT::Object || r.t == VT::Object || !l.enumType.empty() || !r.enumType.empty()) {
        try { return fn(ValueList{l, r}); }
        // Only "no candidate took these operands" falls back. An error the
        // user's operator RAISED is its answer and must keep being raised.
        catch (RakuError& e) {
            std::string en = e.payload.t == VT::Type ? e.payload.s : e.payload.typeName();
            if (en != "X::Multi::NoMatch" && en != "X::Multi::Ambiguous") throw;
        }
    }
    return applyArith(std::string(op), l, r);
}

// The COMPOUND-ASSIGNMENT form, `$obj OP= x`. Deliberately not rtUserInfix:
// evalAssign's arm gates on an object operand alone — no enum — and swallows
// whatever the candidate raises rather than only a no-match. Mirroring what is
// actually there matters more than making the three consistent with each other;
// if they should agree, that is a change to the interpreter first.
bool rtUserInfixInto(Value (*fn)(ValueList), Value& lhs, const Value& r) {
    if (!(lhs.t == VT::Object || r.t == VT::Object)) return false;
    try { lhs = fn(ValueList{lhs, r}); return true; }
    catch (RakuError&) {}
    return false;
}

// The PREFIX half, which differs in both of its decisions and so shares neither
// of the two above. evalUnary gates on an OBJECT WITH A CLASS (no enum arm: a
// built-in prefix on an enum stays built-in), and falls back on X::Multi::NoMatch
// alone. It answers into `out` rather than returning, because the built-in
// fallback for a prefix is a different expression per operator and the emitter
// is what holds it.
bool rtUserPrefix(Value (*fn)(ValueList), const Value& v, Value& out) {
    if (!(v.t == VT::Object && v.obj() && v.obj()->cls)) return false;
    try { out = fn(ValueList{v}); return true; }
    catch (RakuError& e) {
        if (!(e.payload.t == VT::Type && e.payload.s == "X::Multi::NoMatch")) throw;
    }
    return false;
}

// A Pair BINDS its value: `a => $x` carries $x's container, so `$p.value = 5`
// writes $x, while `a => 1` carries a value and refuses the write with
// X::Assignment::RO (sheet HM-18). rakupp has no scalar containers yet, so the
// aliasing half is out of reach — but which side of the line a pair is on is
// decided by the value EXPRESSION, and that much is knowable here: a variable,
// an element or an attribute names a container; a literal, an operator result
// or a call does not. The flag rides on the pair's own value Value, so it
// survives `.clone`, storage in an array and every copy of the pair.
bool exprNamesContainer(const Expr* e) {
    if (!e) return false;
    switch (e->kind) {
        case NK::VarExpr:  return true;
        case NK::Index:    return true;
        // `Pair.new('a', my $v = 1)` — the declaration YIELDS the container it
        // just made, so the pair binds it (t/regression/clone-semantics.raku)
        case NK::Assign:   return static_cast<const Assign*>(e)->op == "=" &&
                                  exprNamesContainer(static_cast<const Assign*>(e)->target.get());
        case NK::Unary:    return static_cast<const Unary*>(e)->op == "ctx$" &&
                                  exprNamesContainer(static_cast<const Unary*>(e)->operand.get());
        default:           return false;
    }
}
// An OBJECT-KEYED hash CONSTRAINS its keys: `my %h{Int}` takes Int keys and
// nothing else, and a wrong one is a binding failure at the subscript — the
// same exception a parameter type check raises, because that is literally what
// Rakudo's ASSIGN-KEY signature does (sheet HM-04). Reads are NOT checked: a
// Str key simply does not find an Int one, so `%h<1>:exists` is False.
// The payload index for one key of an object hash. See hashSubKey below.
std::string objHashIndex(const Value& k) {
    if (k.t == VT::Type) return "(" + k.s.str() + ")";
    if (k.t == VT::Array && !k.enumType.empty() && k.enumName.empty())
        return "(" + std::string(k.enumType.str()) + ")";
    if (k.t == VT::Str && k.hashKind.empty() && k.enumName.empty()) return k.s;
    return whichOf(k);
}

// Howard Hinnant's days<->civil algorithms (proleptic Gregorian, day 0 = 1970-01-01).
long long civilToDays(long long y, long long m, long long d) {
    y -= m <= 2;
    long long era = (y >= 0 ? y : y - 399) / 400;
    long long yoe = y - era * 400;
    long long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}
void daysToCivil(long long z, long long& y, long long& m, long long& d) {
    z += 719468;
    long long era = (z >= 0 ? z : z - 146096) / 146097;
    long long doe = z - era * 146097;
    long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = yoe + era * 400;
    long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    long long mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp + (mp < 10 ? 3 : -9);
    y += (m <= 2);
}
Value makeDate(long long days, const Value* from) {
    long long y, m, d; daysToCivil(days, y, m, d);
    Value v = Value::makeHash(); v.hashKind = "Date";
    (*v.hash())["year"] = Value::integer(y);
    (*v.hash())["month"] = Value::integer(m);
    (*v.hash())["day"] = Value::integer(d);
    if (from && from->t == VT::Hash && from->hash() && from->hash()->count("formatter"))
        (*v.hash())["formatter"] = from->hash()->at("formatter");
    return v;
}

// Variables that may be used without an explicit `my` declaration (never "undeclared").
// Not static: DeclCheck's before-the-run pass has to exempt exactly the names
// this exempts, or the two would drift and the static check would reject a
// program the interpreter runs happily.
bool isSpecialVar(const std::string& n) {
    if (n.size() < 2) return true;          // bare sigil
    char sig = n[0];
    if (sig == '&') return n[1] != '?';     // &foo code refs (builtins not in env);
                                             // &?ROUTINE/&?BLOCK exist only inside callables
    char c = n[1];
    if (c == '*' || c == '?' || c == '.' || c == '!' || c == '<' ||
        c == '=' || c == '~' || c == ':' || c == '^' || c == '/' || c == '_')
        return true;                        // twigils, $_, $/, $!, attribute/placeholder
    if (ascii::isdigit((unsigned char)c)) return true; // $0, $1, ... match vars
    if (n.find("::") != std::string::npos) return true; // package-qualified $Foo::bar (may be undefined)
    // (`$a`/`$b` are NOT special in Raku — `sort { $a <=> $b }` is an
    // undeclared-variable error there; the placeholders are `$^a`/`$^b`)
    if (n == "@_" || n == "%_") return true;
    // `$¢`, the match cursor: the regex engine installs it in the enclosing
    // scope alongside `$/`, so like `$/` it is never the program's to declare —
    // and asking for it where no match has run answers undefined, as Rakudo does.
    if (n == "$\u00a2") return true;             // U+00A2 CENT SIGN
    return false;
}

extern std::function<Value(const Value&)> g_deproxy;
extern std::function<Value*(const std::string&)> g_lexInfixLookup;
thread_local unsigned long long g_subscriptRefusals = 0;

// eqv for a `repr('CStruct')`/CUnion instance. Such an object holds NO Raku
// attributes — only `__native_ptr`, the address of its native body — so the
// generic attribute walk compared two malloc addresses and called every pair of
// distinct structs unequal, however identical their contents.
//
// Rakudo compares them field by field, and renders a POINTER field without its
// contents (`.raku` gives a bare `CArray[num64].new`), so only the scalar
// fields discriminate. That is not an oversight to improve on: a raw pointer
// carries no length, and nothing at this boundary can say how much of it to
// compare. Math::SparseMatrix::Native is exactly this shape — `$m[2] eqv
// $m.row-at(2)` is two structs with equal numbers and two different buffers,
// and Rakudo says True.
static bool ncStructEqv(const Value& a, const Value& b) {
    ClassInfo* ci = a.obj()->cls.get();
    long long pa = a.obj()->attrs.at("__native_ptr").toInt();
    long long pb = b.obj()->attrs.at("__native_ptr").toInt();
    if (pa == pb) return true;
    if (!pa || !pb) return false;
    for (auto& at : ci->attrs) {
        std::string an = at.name;
        if (!an.empty() && (an[0] == '$' || an[0] == '@' || an[0] == '%')) an = an.substr(1);
        if (!an.empty() && (an[0] == '!' || an[0] == '.')) an = an.substr(1);
        std::string type;
        long long off = Interpreter::ncFieldOffset(ci, an, type);
        if (off < 0) continue;
        std::string bt = type.substr(0, type.find('['));
        // Str is a char* the struct points AT, and its length is its own — so it
        // is the one pointer field that can be, and in Rakudo is, compared.
        if (bt == "Str") {
            long long x = 0, y = 0;
            std::memcpy(&x, (void*)(intptr_t)(pa + off), 8);
            std::memcpy(&y, (void*)(intptr_t)(pb + off), 8);
            if (!x || !y) { if (x != y) return false; continue; }
            if (std::strcmp((const char*)(intptr_t)x, (const char*)(intptr_t)y) != 0) return false;
            continue;
        }
        // Pointer, CArray and a nested class field are all bare addresses: not
        // comparable, and Rakudo does not compare them either.
        bool sgn, isF;
        if (!ncScalarWidth(type, sgn, isF)) continue;
        if (!valueEqv(Interpreter::ncReadElem(pa + off, type, 0),
                      Interpreter::ncReadElem(pb + off, type, 0))) return false;
    }
    return true;
}
bool valueEqv(const Value& a, const Value& b) {
    // A CYCLIC structure compares by shape, not by walking forever. A PDF page
    // tree holds its parent (`:Parent($pages)`), so `is-deeply` over one
    // recursed until the stack ran out and the process died with no diagnostic.
    // A pair of containers already being compared higher up the walk is taken as
    // equal: that is what makes two structures cyclic in the SAME way compare
    // equal, and the only answer a finite walk can give.
    struct EqvGuard {
        static std::vector<std::pair<const void*, const void*>>& seen() {
            static thread_local std::vector<std::pair<const void*, const void*>> v;
            return v;
        }
        bool pushed = false;
        ~EqvGuard() { if (pushed) seen().pop_back(); }
    } eqvG;
    {
        const void* pa = a.arr() ? (const void*)a.arr()
                       : a.hash() ? (const void*)a.hash()
                       : (a.t == VT::Object && a.obj()) ? (const void*)a.obj() : nullptr;
        const void* pb = b.arr() ? (const void*)b.arr()
                       : b.hash() ? (const void*)b.hash()
                       : (b.t == VT::Object && b.obj()) ? (const void*)b.obj() : nullptr;
        if (pa && pb) {
            auto& sv = EqvGuard::seen();
            for (auto& e : sv) if (e.first == pa && e.second == pb) return true;
            sv.push_back({pa, pb});
            eqvG.pushed = true;
        }
    }
    // A JUNCTION against a plain value AUTOTHREADS and collapses, element or
    // not: `(1, 2) eqv (1, any(2, 3))` is True (Rakudo). Two junctions still
    // compare as the structures they are.
    {
        auto junc = [](const Value& v) {
            return v.t == VT::Array && v.arr() && (v.enumName == "any" || v.enumName == "all" ||
                                                   v.enumName == "one" || v.enumName == "none");
        };
        const bool ja = junc(a), jb = junc(b);
        if (ja != jb) {
            const Value& j = ja ? a : b;
            const Value& o = ja ? b : a;
            size_t hits = 0, n = j.arr()->size();
            for (auto& e : *j.arr()) if (ja ? valueEqv(e, o) : valueEqv(o, e)) hits++;
            if (j.enumName == "any") return hits > 0;
            if (j.enumName == "all") return hits == n;
            if (j.enumName == "one") return hits == 1;
            return hits == 0;   // none
        }
    }
    // A Proxy is a container: compare what it HOLDS. `is-deeply $q<foo>, $('1','3')`
    // over URI::Query's list of Proxy containers compared containers against
    // strings and failed on type alone.
    if (a.t == VT::Hash && a.hashKind == "Proxy" && a.hash() && g_deproxy)
        return valueEqv(g_deproxy(a), b);
    if (b.t == VT::Hash && b.hashKind == "Proxy" && b.hash() && g_deproxy)
        return valueEqv(a, g_deproxy(b));
    // Two Parameters are eqv when they are SPELLED alike: a default is a
    // fresh closure per signature, so comparing it would say `:($a = 2)` is
    // not eqv to itself (Rakudo: True, and a default's value is not compared)
    if (a.t == VT::Hash && b.t == VT::Hash && a.hashKind == "Parameter" &&
        b.hashKind == "Parameter" && a.hash() && b.hash()) {
        auto str = [](const Value& v) {
            auto it = v.hash()->find("str");
            return it == v.hash()->end() ? std::string() : it->second.toStr();
        };
        return str(a) == str(b);
    }
    // …and two Signatures when their parameters are, one for one (the cached
    // renderings differ between `:(Int)` and its reparsed `:(Int $)`)
    if (a.t == VT::Hash && b.t == VT::Hash && a.hashKind == "Signature" &&
        b.hashKind == "Signature" && a.hash() && b.hash()) {
        auto get = [](const Value& v, const char* k) {
            auto it = v.hash()->find(k);
            return it == v.hash()->end() ? Value::any() : it->second;
        };
        for (const char* k : {"arity", "count", "returns"})
            if (!valueEqv(get(a, k), get(b, k))) return false;
        Value pa = get(a, "params"), pb = get(b, "params");
        size_t na = pa.arr() ? pa.arr()->size() : 0, nb = pb.arr() ? pb.arr()->size() : 0;
        if (na != nb) return false;
        for (size_t i = 0; i < na; i++)
            if (!valueEqv((*pa.arr())[i], (*pb.arr())[i])) return false;
        return true;
    }
    // the two spellings of Any are one object — see isAnyTypeObject
    if (a.t != b.t && isAnyTypeObject(a) && isAnyTypeObject(b)) return true;
    // eqv is type-aware: 42 eqv 42.0 is False (Int vs Num/Rat), unlike ==
    if (a.t != b.t) return false;
    // …and an ALLOMORPH is its own type with a string face: IntStr is not
    // ComplexStr, and `<42+0i>` spelled "x" is not the one spelled "a"
    if (a.isAllomorph() || b.isAllomorph()) {
        if (a.isAllomorph() != b.isAllomorph() || a.hashKind != b.hashKind) return false;
        if (a.s != b.s) return false;
    }
    // Two mentions of the same built-in operator are one routine. `&[~~]` is
    // synthesised fresh each time it is named, so a pointer comparison said
    // False where Rakudo says True — Test::Run decides whether to flip its
    // comparison operator with `$op_std eqv &[~~]`, and never matched. User
    // routines keep pointer identity.
    if (a.t == VT::Code && a.code() && b.code() && a.code() != b.code() &&
        a.code()->builtin && b.code()->builtin && !a.code()->name.empty())
        return a.code()->name == b.code()->name;
    // …and an allomorph is its own type: `42 eqv <42>` is False although both are
    // VT::Int. They differ only in their WHICH, which carries both halves.
    if (a.isAllomorph() || b.isAllomorph()) return whichOf(a) == whichOf(b);
    // An IO::Path is its path text AND the directory that text is read
    // against: the same "a" against two different :CWDs names two files, so
    // the two paths are not equivalent. (Smartmatch asks the weaker question —
    // do they resolve to one absolute path — and lives in applyBinOp.)
    if (a.t == VT::Str && a.hashKind == "IO" && b.hashKind == "IO")
        return a.s == b.s && a.ofType() == b.ofType() && a.enumName == b.enumName;
    switch (a.t) {
        case VT::Array:
            // An Array, a List, a Seq and a Slip are DIFFERENT TYPES that share
            // VT::Array here, and eqv is type-aware — that is the whole of what
            // separates it from `==` and `eq`. Comparing only the elements made
            // `(1,2) eqv [1,2]` True where Rakudo says False, which is the
            // dangerous shape: a test agreeing on the values and disagreeing on
            // the verdict. (Found writing t/regression/ast-cache-publication.raku,
            // which passed here and failed under Rakudo with identical numbers.)
            //
            // typeName() is exactly the rule — it derives Array/List/Seq/Slip
            // from isList and s, and a Junction/Capture/Uni from their own tags.
            // It also, correctly, ignores `itemized`: `[1,2] eqv $[1,2]` is True
            // on both engines, because itemisation is not a type difference.
            if (a.typeName() != b.typeName()) return false;
            forceLazy(a); forceLazy(b);   // an unpulled gather has no elements yet
            if (!a.arr() || !b.arr() || a.arr()->size() != b.arr()->size()) return false;
            // A Capture's POSITIONAL part is ordered, its NAMED part is a map:
            // \(1, :a, :b) eqv \(1, :b, :a) is True in Rakudo. Getopt::Long's
            // suite builds nameds in parse order and is-deeply's them against
            // source order.
            if (a.typeName() == "Capture") {
                std::vector<const Value*> ap, bp;
                std::map<std::string, const Value*> an, bn;
                for (auto& e : *a.arr()) { if (e.t == VT::Pair) an[e.s] = &e; else ap.push_back(&e); }
                for (auto& e : *b.arr()) { if (e.t == VT::Pair) bn[e.s] = &e; else bp.push_back(&e); }
                if (ap.size() != bp.size() || an.size() != bn.size()) return false;
                for (size_t i = 0; i < ap.size(); i++) if (!valueEqv(*ap[i], *bp[i])) return false;
                for (auto& kv : an) {
                    auto it = bn.find(kv.first);
                    if (it == bn.end() || !valueEqv(*kv.second, *it->second)) return false;
                }
                return true;
            }
            for (size_t i = 0; i < a.arr()->size(); i++) if (!valueEqv((*a.arr())[i], (*b.arr())[i])) return false;
            return true;
        case VT::Hash:
            // eqv is type-aware here for exactly the reason it is for Array:
            // comparing only the ENTRIES made a Hash eqv a Map, a Set eqv a
            // SetHash, a Bag eqv a Mix and `my Int %h` eqv `my %h` — every
            // hash flavour equal to every other as long as the pairs lined up.
            // typeName() carries the flavour (hashKind), ofType the value
            // constraint and the `{Any}` key shape.
            // …but only the value and key parameters count: `:{ }` carries a
            // third one (see the `ctx%{}` composer) and a `my Mu %{Mu}` does
            // not, and Rakudo calls those two eqv all the same.
            {
                auto ofPair = [](const Value& v) {
                    const std::string& t = v.ofType();
                    size_t c = t.find(',');
                    if (c == std::string::npos) return t;
                    size_t c2 = t.find(',', c + 1);
                    return c2 == std::string::npos ? t : t.substr(0, c2);
                };
                // (the KEY SHAPE is in ofType's second component, so the
                // objKeyed bit adds nothing here — and the classify/categorize
                // hashes carry the shape without the bit)
                if (a.typeName() != b.typeName() || ofPair(a) != ofPair(b)) return false;
            }
            if (!a.hash() || !b.hash()) return false;
            // A Dateish `:formatter` is a RENDERING hook, not state. Rakudo's
            // `Date.new(…, :formatter($f)) eqv Date.new(…)` is True, and roast
            // leans on it: S32-temporal/Date.t is-deeply's `.first-date-in-month`
            // (which inherits the formatter) against a plain Date. Every other
            // key still counts, in both directions.
            if (a.hashKind == "Date" || a.hashKind == "DateTime") {
                for (auto& kv : *a.hash()) {
                    if (kv.first == "formatter") continue;
                    auto it = b.hash()->find(kv.first);
                    if (it == b.hash()->end() || !valueEqv(kv.second, it->second)) return false;
                }
                for (auto& kv : *b.hash())
                    if (kv.first != "formatter" && !a.hash()->count(kv.first)) return false;
                return true;
            }
            if (a.hash()->size() != b.hash()->size()) return false;
            for (auto& kv : *a.hash()) { auto it = b.hash()->find(kv.first); if (it == b.hash()->end() || !valueEqv(kv.second, it->second)) return false; }
            return true;
        case VT::Pair:
            // typed keys compare structurally (an object key's rendering now
            // carries its identity, so `$o => 1 eqv Foo.new => 1` needs the key)
            return (a.pairKey() && b.pairKey() ? valueEqv(*a.pairKey(), *b.pairKey())
                                           : a.s == b.s) &&
                   valueEqv(a.pairVal() ? *a.pairVal() : Value::any(), b.pairVal() ? *b.pairVal() : Value::any());
        // structural, like Rakudo's default eqv: same class + eqv attributes
        // (a clone eqv its source; identity alone was too narrow)
        case VT::Object:
            // …except a native-backed struct, whose state is in C memory rather
            // than in `attrs` — see ncStructEqv.
            if (a.obj() && b.obj() && a.obj()->cls && a.obj()->cls == b.obj()->cls &&
                (a.obj()->cls->repr == "CStruct" || a.obj()->cls->repr == "CPPStruct" ||
                 a.obj()->cls->repr == "CUnion") &&
                a.obj()->attrs.count("__native_ptr") && b.obj()->attrs.count("__native_ptr"))
                return ncStructEqv(a, b);
            // A class with a `.raku` of its own is compared by it, which is
            // Rakudo's rule for objects (same type, same `.raku`): the default
            // `.raku` spells out exactly the attributes compared below.
            if (a.obj() && b.obj() && a.obj()->cls && a.obj()->cls == b.obj()->cls && g_revInterp &&
                a.obj()->cls->findMethod("raku")) {
                ValueList none;
                try {
                    return g_revInterp->methodCall(a, "raku", none).toStr() ==
                           g_revInterp->methodCall(b, "raku", none).toStr();
                } catch (...) {}
            }
            return objectStructEqv(a, b, valueEqv);
        // A RANGE compares by its endpoint FORM, exclusion markers included —
        // expanding it made `1..^5 eqv 1..4` True, and built the whole list to
        // answer. A CODE object is identical only to itself. A TYPE object carries
        // its parameterisation, so Array[Int] and Array[Str] are different types.
        case VT::Range:  return whichOf(a) == whichOf(b);
        case VT::Code:   return a.code() == b.code();
        case VT::Type:   return a.s == b.s && a.ofType() == b.ofType();
        case VT::Rat: // structural nude compare — .Str on a 0-denominator Rat throws
            return a.fatRat() == b.fatRat() &&
                   a.ratN() && b.ratN() && a.ratD() && b.ratD() &&
                   BigInt::cmp(*a.ratN(), *b.ratN()) == 0 && BigInt::cmp(*a.ratD(), *b.ratD()) == 0;
        default: return a.toStr() == b.toStr();
    }
}

// Numify a string with Raku-correct result type (Int vs Num); undefined if non-numeric.
// External linkage (declared in Interpreter.h) so Builtins.cpp's .Int/.Numeric can
// reuse the BigInt-aware parse instead of the lossy long-long toInt().
Value numifyStr(const std::string& in) {
    size_t a = in.find_first_not_of(" \t\n\r\f\v");
    if (a == std::string::npos) return Value::integer(0); // empty/whitespace -> 0
    size_t b = in.find_last_not_of(" \t\n\r\f\v");
    std::string s = in.substr(a, b - a + 1);
    // Unicode MINUS SIGN (U+2212) is accepted as an ASCII '-' in numeric strings
    for (size_t k = 0; (k = s.find("\xE2\x88\x92", k)) != std::string::npos; )
        s.replace(k, 3, "-");
    // Non-ASCII decimal digits (category Nd: Arabic-Indic, Devanagari, …)
    // transliterate to ASCII so the ordinary positional parse handles them —
    // "٤٢".Int is 42, exactly as each digit's Numeric_Value says.
    {
        bool anyHigh = false; // any byte >= 0x80 anywhere ("1٢" — not only the first)
        for (unsigned char c : s) if (c >= 0x80) { anyHigh = true; break; }
        if (anyHigh) {
            std::string t2;
            for (uint32_t cp : utf8cp(s)) {
                if (cp >= 0x80) {
                    int dv = uniDigitValue(cp);   // never-cut: plain digits are not the names feature
                    if (dv >= 0) {
                        t2 += (char)('0' + dv);
                        continue;
                    }
                }
                t2 += cpToU8(cp);
            }
            s = std::move(t2);
        }
    }
    // strip underscores that sit between two digits (numeric separators)
    if (s.find('_') != std::string::npos) {
        std::string t;
        for (size_t k = 0; k < s.size(); k++) {
            if (s[k] == '_' && k > 0 && k + 1 < s.size() &&
                ascii::isalnum((unsigned char)s[k-1]) && ascii::isalnum((unsigned char)s[k+1])) continue;
            t += s[k];
        }
        s = t;
    }
    if (s == "Inf" || s == "+Inf") return Value::number(INFINITY);
    if (s == "-Inf") return Value::number(-INFINITY);
    if (s == "NaN") return Value::number(NAN);
    static const std::regex reInt(R"(^[+-]?\d+$)");
    // …and a RADIX POINT is allowed after the prefix: `0x1.8` is 1.5 and
    // `0b1.1` is 1.5 (Str sheet ST-06). parseRadix already reads a dot; the
    // pattern that reaches it did not admit one.
    static const std::regex reRadix(R"(^[+-]?0[xobd][0-9a-fA-F_.]+$)");
    static const std::regex reFloat(R"(^[+-]?(\d+(\.\d+)?|\.\d+)([eE][+-]?\d+)?$)");
    static const std::regex reRat(R"(^[+-]?\d+/\d+$)");
    // convert a single radix digit (0-9, a-z / A-Z) → value, or -1 if not a digit
    auto digitVal = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'z') return c - 'a' + 10;
        if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
        return -1;
    };
    // parse a radix number "d.d" (underscores already allowed) in the given base,
    // validating each digit < base. Returns a numeric Value or Value::any() on failure.
    auto parseRadix = [&](const std::string& raw, int base, bool neg) -> Value {
        // underscores are separators only BETWEEN two digits — reject edges/doubles
        for (size_t k = 0; k < raw.size(); k++)
            if (raw[k] == '_' && (k == 0 || k + 1 == raw.size() ||
                digitVal(raw[k-1]) < 0 || digitVal(raw[k+1]) < 0)) return Value::any();
        std::string digs; for (char c : raw) if (c != '_') digs += c;
        size_t dot = digs.find('.');
        std::string ip = dot == std::string::npos ? digs : digs.substr(0, dot);
        std::string fp = dot == std::string::npos ? "" : digs.substr(dot + 1);
        if (ip.empty() && fp.empty()) return Value::any();
        BigInt iv(0), b(base);
        for (char c : ip) { int d = digitVal(c); if (d < 0 || d >= base) return Value::any(); iv = iv * b + BigInt(d); }
        if (fp.empty()) { if (neg) iv = BigInt(0) - iv; return Value::bigint(iv); }
        BigInt num = iv, den(1);
        for (char c : fp) { int d = digitVal(c); if (d < 0 || d >= base) return Value::any(); num = num * b + BigInt(d); den = den * b; }
        if (neg) num = BigInt(0) - num;
        return Value::rat(num, den);
    };
    try {
        if (std::regex_match(s, reInt)) {
            try { return Value::integer(std::stoll(s)); }
            catch (...) { return Value::bigint(BigInt::fromString(s)); }
        }
        // :N<digits> radix notation, e.g. :10<42>, :2<11>, :36<aZ>, :16<c.8>
        {
            size_t off = (s[0] == '+' || s[0] == '-') ? 1 : 0;
            bool neg = s[0] == '-';
            if (off < s.size() && s[off] == ':') {
                // Three bracketings, all of them the same radix form: `:16<FF>`,
                // `:2«101»` (French quotes) and `:10[1, 2, 3]`, where the
                // BRACKETED form lists digit VALUES rather than digit characters,
                // so a base above 36 can still be written (Str sheet ST-06).
                static const std::string kOpenFr = "\xC2\xAB", kCloseFr = "\xC2\xBB";
                size_t lt = s.find('<', off), openLen = 1;
                char closeCh = '>';
                if (lt == std::string::npos) {
                    size_t fr = s.find(kOpenFr, off);
                    if (fr != std::string::npos && s.size() >= kCloseFr.size() &&
                        s.compare(s.size() - kCloseFr.size(), kCloseFr.size(), kCloseFr) == 0) {
                        lt = fr; openLen = kOpenFr.size(); closeCh = 0;
                    }
                }
                size_t br = (lt == std::string::npos) ? s.find('[', off) : std::string::npos;
                static const std::regex reDec(R"(^\d+$)");
                if (lt != std::string::npos &&
                    (closeCh == 0 || s.back() == closeCh)) {
                    std::string baseStr = s.substr(off + 1, lt - off - 1);
                    if (std::regex_match(baseStr, reDec)) {
                        int base = std::atoi(baseStr.c_str());
                        size_t closeLen = closeCh ? 1 : kCloseFr.size();
                        if (base >= 2 && base <= 36)
                            return parseRadix(s.substr(lt + openLen,
                                                       s.size() - lt - openLen - closeLen),
                                              base, neg);
                    }
                }
                else if (br != std::string::npos && s.back() == ']') {
                    std::string baseStr = s.substr(off + 1, br - off - 1);
                    if (std::regex_match(baseStr, reDec)) {
                        long long base = std::atoll(baseStr.c_str());
                        if (base >= 2) {
                            std::string body = s.substr(br + 1, s.size() - br - 2);
                            BigInt acc(0), bb(base);
                            bool ok = !body.empty();
                            size_t k = 0;
                            while (ok && k <= body.size()) {
                                size_t c = body.find(',', k);
                                std::string d = body.substr(k, c == std::string::npos ? std::string::npos : c - k);
                                size_t d0 = d.find_first_not_of(" \t");
                                size_t d1 = d.find_last_not_of(" \t");
                                if (d0 == std::string::npos) { ok = false; break; }
                                d = d.substr(d0, d1 - d0 + 1);
                                if (!std::regex_match(d, reDec)) { ok = false; break; }
                                long long dv = std::atoll(d.c_str());
                                if (dv >= base) { ok = false; break; }
                                acc = acc * bb + BigInt(dv);
                                if (c == std::string::npos) break;
                                k = c + 1;
                            }
                            if (ok) { if (neg) acc = BigInt(0) - acc; return Value::bigint(acc); }
                        }
                    }
                }
                return Value::any();
            }
        }
        if (std::regex_match(s, reRadix)) {
            bool neg = s[0] == '-'; size_t off = (s[0] == '+' || s[0] == '-') ? 1 : 0;
            char pc = s[off + 1];
            int base = pc == 'x' ? 16 : pc == 'o' ? 8 : pc == 'd' ? 10 : 2;
            return parseRadix(s.substr(off + 2), base, neg);
        }
        if (std::regex_match(s, reRat)) {
            size_t sl = s.find('/');
            return Value::rat(BigInt::fromString(s.substr(0, sl)), BigInt::fromString(s.substr(sl + 1)));
        }
        // …and a `/` between anything else the grammar accepts is a DIVISION:
        // `1/2e0` is `0.5e0`, `0x10/2` is `8.0`, `-1.5/3` is `-0.5`. Two plain
        // integers make an unnormalised Rat (the arm above, which is what keeps
        // `1/0` as `<1/0>`); everything else divides (Str sheet ST-06). Only the
        // two-integer spelling used to parse at all.
        {
            size_t sl = s.find('/');
            if (sl != std::string::npos && sl > 0 && sl + 1 < s.size() &&
                s.find('/', sl + 1) == std::string::npos) {
                Value nu = numifyStr(s.substr(0, sl)), de = numifyStr(s.substr(sl + 1));
                const bool okN = nu.t != VT::Any && nu.t != VT::Nil;
                const bool okD = de.t != VT::Any && de.t != VT::Nil;
                if (okN && okD) {
                    if (nu.t == VT::Int && de.t == VT::Int && !nu.big() && !de.big())
                        return Value::rat(BigInt(nu.toInt()), BigInt(de.toInt()));
                    return applyArith("/", nu, de);
                }
                return Value::any();
            }
        }
        // The MULTIPLIER form: `2*10**3` is 2000. Both halves are required —
        // a bare `2*3` and a bare `2**3` are refused (ST-06, ST-07) — so this
        // reads exactly `<coefficient> * <base> ** <exponent>`.
        {
            size_t dbl = s.find("**");
            if (dbl != std::string::npos && dbl > 0) {
                size_t mul = s.rfind('*', dbl - 1);
                if (mul != std::string::npos && mul > 0 && mul + 1 < dbl) {
                    Value co = numifyStr(s.substr(0, mul));
                    Value ba = numifyStr(s.substr(mul + 1, dbl - mul - 1));
                    Value ex = numifyStr(s.substr(dbl + 2));
                    auto ok = [](const Value& v) { return v.t != VT::Any && v.t != VT::Nil; };
                    if (ok(co) && ok(ba) && ok(ex))
                        return applyArith("*", co, applyArith("**", ba, ex));
                }
                return Value::any();
            }
        }
        if (std::regex_match(s, reFloat)) {
            // a plain decimal ("3.14") numifies to a Rat, like the literal would;
            // an exponent ("1.23E4") makes it a Num
            if (s.find('e') == std::string::npos && s.find('E') == std::string::npos) {
                size_t dot = s.find('.');
                std::string frac = dot == std::string::npos ? "" : s.substr(dot + 1);
                if (!frac.empty()) {
                    std::string digits = (dot == std::string::npos ? s : s.substr(0, dot) + frac);
                    if (!digits.empty() && digits[0] == '+') digits.erase(0, 1);
                    BigInt den(1);
                    for (size_t k = 0; k < frac.size(); k++) den = den * BigInt(10);
                    return Value::rat(BigInt::fromString(digits), std::move(den));
                }
            }
            return Value::number(cnum::stod(s));
        }
        // Complex: "a+bi" / "a-bi" / "bi" (i may be written "\i"). Split at the
        // sign that separates real and imaginary parts (not a leading sign or an
        // exponent sign), numify each half recursively.
        {
            std::string t = s;
            size_t ip = t.rfind("\\i");
            const bool backslashI = (ip != std::string::npos && ip == t.size() - 2);
            if (backslashI) t.erase(ip, 1); // "\i" -> "i"
            if (!t.empty() && (t.back() == 'i')) {
                std::string body = t.substr(0, t.size() - 1); // drop trailing i
                // find split sign: scan from the right for +/- not preceded by e/E
                size_t split = std::string::npos;
                for (size_t k = body.size(); k-- > 1; ) {
                    if ((body[k] == '+' || body[k] == '-') &&
                        body[k-1] != 'e' && body[k-1] != 'E') { split = k; break; }
                }
                std::string reStr, imStr;
                // a bare `i` (or `+i`/`-i`) is a WORD, not the imaginary unit:
                // `"i".Numeric` and `"is".Numeric` fail under Rakudo, and
                // Text::SubParsers' number scanner runs every token through it
                // …but `Inf\i` and `NaN\i` are numbers, and they carry no digit.
                // The BACKSLASH is what tells them apart from a word ending in
                // `i`: `"Infi"` is refused where `"Inf\i"` is `<0+Inf\i>`
                // (Str sheet ST-06, ST-07).
                if (body.find_first_of("0123456789") == std::string::npos && !backslashI)
                    throw std::runtime_error("not numeric");
                if (split == std::string::npos) { reStr = "0"; imStr = body.empty() ? "1" : body; }
                else { reStr = body.substr(0, split); imStr = body.substr(split);
                       if (imStr == "+" || imStr == "-") imStr += "1"; }
                // …and the same rule applies to the IMAGINARY half on its own:
                // `"3+Infi"` is refused where `"3+Inf\\i"` is `<3+Inf\\i>`. The
                // digit in the real part does not vouch for it.
                if (imStr.find_first_of("0123456789") == std::string::npos && !backslashI)
                    throw std::runtime_error("not numeric");
                Value rv = numifyStr(reStr), iv = numifyStr(imStr);
                if (rv.isNumeric() && iv.isNumeric()) {
                    // a signed zero keeps its sign: `<-0-0i>` is -0e0 both ways
                    double re = rv.toNum(), im = iv.toNum();
                    auto negZero = [](const std::string& s) {
                        size_t b = s.find_first_not_of(" \t");
                        return b != std::string::npos && (s[b] == '-' || s.compare(b, 3, "\u2212") == 0);
                    };
                    if (re == 0 && negZero(reStr)) re = -0.0;
                    if (im == 0 && negZero(imStr)) im = -0.0;
                    return Value::complex(re, im);
                }
            }
        }
        // A single VULGAR FRACTION character is its own Rat: `"½"` is 0.5 and
        // `"⅓"` is `<1/3>`. Only a fraction, and only one character — a Roman
        // numeral (`"Ⅻ"`, an No) and a doubled fraction (`"½½"`) are both
        // refused (Str sheet ST-06, ST-07). The Nd transliteration above has
        // already run, so a plain digit never reaches here.
        {
            auto cps = utf8cp(s);
            if (cps.size() == 1 && cps[0] >= 0x80) {
                long long num, den;
                if (uniNumValueQuiet(cps[0], num, den)) {
                    // A fraction by its VALUE covers every one of them but
                    // U+2189 VULGAR FRACTION ZERO THIRDS, whose value Unicode
                    // records as plain 0 — so the NAME settles that one. It is
                    // also what keeps a Roman numeral, an ideographic number and
                    // a Tamil number out: those carry a numeric value too.
                    // …and the answer is the ALLOMORPH: `"½".Numeric` is
                    // `RatStr.new(0.5, "½")`, keeping the character it was
                    // written as, which is what `val` then hands on.
                    if (den != 1 || uniNameOf(cps[0]).rfind("VULGAR FRACTION", 0) == 0) {
                        Value r = Value::rat(BigInt(num), BigInt(den == 0 ? 1 : den));
                        r.hashKind = "RatStr"; r.s = s;
                        return r;
                    }
                }
            }
        }
    } catch (...) {}
    return Value::any(); // not numeric -> undefined (so .defined is False)
}

// Numeric coercion of a string: a non-numeric one yields a FAILURE carrying
// X::Str::Numeric — an unthrown exception, exactly like Rakudo. `+"a"` is quiet
// (`.defined` is False), but USING the value (say/arithmetic) throws.
// Rakudo's X::Str::Numeric for a string that is no number: the reason, the
// position the parse stopped at, and the source with a <HERE> marker in it
// (control characters shown escaped — `"\b"` reads as `\b`).
static void strNumericParts(const std::string& in, std::string& msg, std::string& reason,
                            std::string& indicator, long long& pos) {
    bool anyDigit = false;
    for (unsigned char c : in) if (ascii::isdigit(c)) { anyDigit = true; break; }
    reason = anyDigit ? "trailing characters after number" : "base-10 number must begin with valid digits or '.'";
    size_t lead = 0;
    while (lead < in.size() && (in[lead] == ' ' || in[lead] == '\t' || in[lead] == '\n' || in[lead] == '\r')) lead++;
    pos = (long long)lead;
    if (anyDigit) {
        const char* b = in.c_str(); char* e = nullptr;
        std::strtod(b, &e);
        if (e && e > b) pos = (long long)(e - b);
    }
    auto esc = [](const std::string& t) {
        std::string o;
        for (unsigned char c : t) {
            if (c == 8) o += "\\b";
            else if (c == 9) o += "\\t";
            else if (c == 10) o += "\\n";
            else if (c == 13) o += "\\r";
            else if (c < 0x20 || c == 0x7F) { char buf[16]; snprintf(buf, sizeof buf, "\\x[%X]", c); o += buf; }
            else o += (char)c;
        }
        return o;
    };
    std::string head = in.substr(0, std::min<size_t>((size_t)pos, in.size()));
    std::string tail = (size_t)pos < in.size() ? in.substr((size_t)pos) : std::string();
    if (tail.size() > 40) tail = tail.substr(0, 40) + "...";
    indicator = "in '" + esc(head) + "<HERE>" + esc(tail) + "' (indicated by <HERE>)";
    msg = "Cannot convert string to number: " + reason + " " + indicator;
}

// Numeric coercion of a string: a non-numeric one yields a FAILURE carrying
// X::Str::Numeric — an unthrown exception, exactly like Rakudo. `+"a"` is quiet
// (`.defined` is False), but USING the value (say/arithmetic) throws.
Value numifyStrFailure(const std::string& in) {
    Value v = numifyStr(in);
    if (v.t != VT::Any && v.t != VT::Nil) return v; // Complex counts: numifyStr's failure signal is undefined
    std::string msg, reason, ind; long long pos = 0;
    strNumericParts(in, msg, reason, ind, pos);
    Value f = rakuppNewFailure();
    if (g_revInterp)
        (*f.hash())["exception"] = g_revInterp->makeTypedEx("X::Str::Numeric",
            {{"source", Value::str(in)}, {"pos", Value::integer(pos)}, {"reason", Value::str(reason)},
             {"source-indicator", Value::str(ind)}}, msg);
    else (*f.hash())["exception"] = Value::typeObj("X::Str::Numeric");
    (*f.hash())["message"] = Value::str(msg);
    return f;
}
// …and the throwing form, for contexts that must produce a number NOW
// (an arithmetic operand: `"a" + 1` dies).
Value numifyStrOrThrow(const std::string& in) {
    Value v = numifyStr(in);
    if (v.t != VT::Any && v.t != VT::Nil) return v;
    std::string msg, reason, ind; long long pos = 0;
    strNumericParts(in, msg, reason, ind, pos);
    if (g_revInterp)
        g_revInterp->throwTypedV("X::Str::Numeric",
            {{"source", Value::str(in)}, {"pos", Value::integer(pos)}, {"reason", Value::str(reason)},
             {"source-indicator", Value::str(ind)}},
            msg);
    throw RakuError{Value::typeObj("X::Str::Numeric"), msg};
}

std::atomic<bool> g_anyShaped{false};
Value makeShapedContainer(const std::vector<long long>& dims, const std::string& declType,
                          const ValueList* fill) {
    g_anyShaped.store(true, std::memory_order_relaxed);
    // native (lowercase) element types default to a concrete zero/empty; named
    // types default to the type object; untyped to Any.
    Value elemDef;
    if (declType.empty()) elemDef = Value::any();
    else if (declType == "str") elemDef = Value::str("");
    else if (declType.rfind("num", 0) == 0) elemDef = Value::number(0);
    else if (declType.rfind("int", 0) == 0 || declType.rfind("uint", 0) == 0 ||
             declType == "byte" || declType == "atomicint")
        elemDef = Value::integer(0);
    else elemDef = Value::typeObj(declType);
    size_t idx = 0;
    std::function<Value(size_t)> mk = [&](size_t lvl) -> Value {
        Value a = Value::array(); a.ofTypeM() = declType;
        long long n = lvl < dims.size() ? dims[lvl] : 0;
        for (long long i = 0; i < n; i++) {
            if (lvl + 1 < dims.size()) a.arr()->push_back(mk(lvl + 1));
            else a.arr()->push_back(fill && idx < fill->size() ? (*fill)[idx++] : elemDef);
        }
        return a;
    };
    Value v = mk(0);
    v.shapeM() = std::make_shared<std::vector<long long>>(dims);
    return v;
}
// Evaluate the dimension list of a shaped-array declaration (`my @a[2;3]` → {2,3}).
std::vector<long long> Interpreter::evalShapeDims(Expr* shape) {
    std::vector<long long> dims;
    // every dimension must be a positive integer: `my @a[0]` is refused
    auto dim = [&](const Value& v) -> long long {
        // an ENUMERATION is a dimension as long as its value list: `my @a[Bool]` is (2,)
        if (isEnumTypeObject(v) && v.arr()) return (long long)v.arr()->size();
        if (v.t == VT::Type && v.s == "Bool") return 2;
        if (v.t == VT::Type && v.s == "Order") return 3;
        if ((v.t == VT::Int || v.t == VT::Num || v.t == VT::Rat) &&
            (v.big() ? v.big()->sign <= 0 : v.toNum() <= 0)) {
            std::string ds = v.toStr();
            throwTypedV("X::IllegalDimensionInShape", {{"dim", v}},
                        "Illegal dimension in shape: " + ds + ". All dimensions must be integers bigger than 0");
        }
        return v.toInt();
    };
    if (shape && shape->kind == NK::ListExpr)
        for (auto& d : static_cast<ListExpr*>(shape)->items) dims.push_back(dim(eval(d.get())));
    else if (shape) {
        // one expression that yields a LIST is the whole shape:
        // `my @md[(1..$n).reverse]`, `my @y[$dims]` (S02-types/multi_dimensional_array.t)
        Value sv = eval(shape);
        if (sv.t == VT::Range)
            throw RakuError{Value::typeObj("X::AdHoc"),
                            "Setting a shape with a Range not allowed: " + sv.toStr()};
        if (sv.t == VT::Array && sv.arr() && !isJunction(sv))
            for (auto& d : toList(sv)) dims.push_back(dim(d));
        else dims.push_back(dim(sv));
    }
    return dims;
}
// default value for a typed declaration: native lowercase types (num/int/str) have
// concrete defaults; a named type (`my Int $x`) defaults to that type object.
Value typedDefault(const std::string& type, char sigil) {
    if (sigil == '$' && !type.empty()) {
        // sized native integers wrap to their bit width on assignment
        static const struct { const char* n; int bits; bool sgn; } nts[] = {
            {"uint8",8,false},{"int8",8,true},{"byte",8,false},{"uint16",16,false},{"int16",16,true},
            {"uint32",32,false},{"int32",32,true},{"uint64",64,false},{"int64",64,true},
            {"uint",64,false},{"int",64,true}};   // the unsized spellings are 64-bit
        for (auto& t : nts) if (type == t.n) { Value v = Value::integer(0); v.natBits = t.bits; v.natSigned = t.sgn; return v; }
        // num32 rounds to float32 precision on assignment (num/num64 are already
        // double, so they need no truncation marker).
        if (type == "num32") { Value v = Value::number(0); v.natBits = 32; v.natFloat = true; return v; }
        // …while num/num64 carry the 64-bit float tag: it marks the value as
        // NATIVE (multi dispatch prefers a `num` parameter for it) and makes an
        // Int assigned in a Num
        if (type == "num" || type == "num64") { Value v = Value::number(0); v.natBits = 64; v.natFloat = true; return v; }
        if (type.rfind("num", 0) == 0) return Value::number(0);
        if (type == "int" || type.rfind("int", 0) == 0 || type.rfind("uint", 0) == 0 ||
            type == "atomicint") return Value::integer(0);
        if (type == "str") return Value::str("");
        // The lowercase Buf/Blob aliases are TYPE names, not native scalars:
        // `my buf8 $b .= new` needs the (buf8) type object to dispatch .new on
        // (it was falling through to Any, so `.= new` died "No such method
        // 'new' for invocant of type 'Any'" — Digest::MD5's first line).
        if (type.rfind("buf", 0) == 0 || type.rfind("blob", 0) == 0 ||
            type == "utf8" || type == "utf16" || type == "utf32")
            return Value::typeObj(type);
        // `my Nil $x` holds Nil itself (`$x === Nil`), not a (Nil) type object
        if (type == "Nil") return Value::nil();
        if (ascii::isupper((unsigned char)type[0])) return Value::typeObj(type); // my Int $x -> (Int)
    }
    // typed containers: `my Int @a` -> Array[Int], `my int @a` (native) too
    if ((sigil == '@' || sigil == '%') && !type.empty() &&
        (ascii::isupper((unsigned char)type[0]) ||
         type.rfind("int", 0) == 0 || type.rfind("uint", 0) == 0 ||
         type.rfind("num", 0) == 0 || type == "str" || type == "byte" ||
         type == "atomicint")) {
        Value v = defaultFor(sigil);
        v.ofTypeM() = type;
        // "valueType,keyType" is the declarator's encoding of an OBJECT hash
        // (`my %h{Any:U}`): type-object keys must stay distinct, exactly as
        // the attribute form already does
        if (sigil == '%' && type.find(',') != std::string::npos) v.objKeyed = true;
        return v;
    }
    // `my &f` holds the Callable type object until something is assigned
    if (sigil == '&') return Value::typeObj(type.empty() ? "Callable" : type);
    return defaultFor(sigil);
}
// The COMPILER needs the same answer: `my Int @a` is an Array[Int] whether the
// program is interpreted or generated as C++. Codegen used to emit a bare
// Value::array()/makeHash()/any() and drop the declared type, so `.of` was (Mu)
// and an object hash lost its key type. One rule, reached from both sides.
Value rtTypedDefault(const char* type, char sigil) {
    return typedDefault(type ? type : "", sigil);
}

bool isKnownTypeName(const std::string& n);   // below
bool capturedType(Interpreter& I, Env* scope, const std::string& name, Value& out) {
    if (!scope || name.empty() || !ascii::isupper((unsigned char)name[0])) return false;
    if (I.classes_.count(name) || I.subsets_.count(name) || isKnownTypeName(name)) return false;
    Value* tv = scope->find(name);
    if (!tv || tv->t != VT::Type || tv->s.empty() || tv->s == name) return false;
    // a `constant` naming a NATIVE type (`constant MyTime = int64`) keeps the
    // native-container path it always had
    if (isNativeTypeName(tv->s)) return false;
    // the TYPE, not the binding: a constant's value carries its readonly flag
    out = Value::typeObj(tv->s);
    out.i = tv->i;
    if (!tv->ofType().empty()) out.ofTypeM() = tv->ofType();
    return true;
}

thread_local const std::set<std::string>* Interpreter::roleInstNames_ = nullptr;

// Which of a parametric role's body statements are GENERIC: the ones that use
// a role parameter, or a name a generic statement declares (transitively —
// `my T $x; my $y = $x.^name` makes both generic). Repeated to a fixed point
// because a sub is declared ahead of its textual position, so an earlier sub
// can call a later generic one. Never generic: a `use` (a load, which the
// declaration needs), a `my method` (it is the role's method), and a multi or
// proto sub (a group grows by joining, and a second scope would split it).
const Interpreter::GenericRoleBody* Interpreter::genericRoleBody(const ClassDecl* cd) {
    if (!cd || !cd->isRole || cd->roleParams.empty()) return nullptr;
    auto hit = genericRoleBodies_.find(cd);
    if (hit != genericRoleBodies_.end()) return &hit->second;
    GenericRoleBody g;
    const size_t n = cd->body.size();
    g.generic.assign(n, 0);
    for (auto& p : cd->roleParams) {
        if (!p.name.empty()) {
            g.names.insert(p.name);
            if (p.name[0] == '\\' && p.name.size() > 1) g.names.insert(p.name.substr(1));
        }
        if (p.typeCapture && !p.type.empty()) g.names.insert(p.type);
        if (!p.captureName.empty()) g.names.insert(p.captureName);
    }
    std::vector<NameScan> scans(n);
    std::vector<char> eligible(n, 0);
    for (size_t i = 0; i < n; i++) {
        const Stmt* st = cd->body[i].get();
        if (!st || st->kind == NK::UseStmt || st->kind == NK::EmptyStmt) continue;
        if (st->kind == NK::SubDecl) {
            auto* sd = static_cast<const SubDecl*>(st);
            if (sd->isMethod || sd->isMulti || sd->isProto) continue;
        }
        // (compile-time constructs run once, at the declaration, as in Rakudo:
        // a BEGIN, a constant, an enum)
        if (st->kind == NK::EnumDecl) continue;
        if (st->kind == NK::Block && !static_cast<const Block*>(st)->phaser.empty()) continue;
        if (st->kind == NK::ExprStmt) {
            const Expr* ex = static_cast<const ExprStmt*>(st)->e.get();
            if (ex && ex->kind == NK::Assign) {
                const Expr* t = static_cast<const Assign*>(ex)->target.get();
                const Expr* v = static_cast<const Assign*>(ex)->value.get();
                if (t && t->kind == NK::VarExpr && static_cast<const VarExpr*>(t)->declScope == "constant") continue;
                if (v && v->kind == NK::BlockExpr && !static_cast<const BlockExpr*>(v)->phaser.empty()) continue;   // `my $x = BEGIN { … }`
                if (v && v->kind == NK::Unary && static_cast<const Unary*>(v)->op == "BEGIN") continue;              // `my $x = BEGIN …`
            }
        }
        eligible[i] = 1;
        scans[i].stmt(st);
    }
    for (bool changed = true; changed;) {
        changed = false;
        for (size_t i = 0; i < n; i++) {
            if (!eligible[i] || g.generic[i]) continue;
            // a statement whose names are decided at run time (EVAL, `::('…')`)
            // may name a generic lexical: it runs where those exist
            bool uses = scans[i].dynamic && !scans[i].decls.empty();
            for (auto& r : scans[i].refs)
                if (!uses && taintedName(g.names, r)) { uses = true; break; }
            // …and a declaration a generic statement USES is per
            // parameterization too (`my @seen; … @seen.push(T)`)
            if (!uses)
                for (auto& d : scans[i].decls) {
                    for (size_t j = 0; j < n && !uses; j++)
                        if (g.generic[j] && j != i && scans[j].refs.count(d)) uses = true;
                    if (uses) break;
                }
            if (!uses) continue;
            g.generic[i] = 1;
            g.any = changed = true;
            for (auto& d : scans[i].decls) {
                g.names.insert(d);
                if (!d.empty() && (d[0] == '$' || d[0] == '@' || d[0] == '%') && d.size() > 1) g.lexicals.insert(d);
            }
        }
    }
    // A multi, proto or `my method` is never split out, but one that
    // mentions a generic name is declared AGAIN in each parameterization's
    // scope (whole groups: every candidate of that name), so it reads that
    // parameterization's types and lexicals
    for (auto& st : cd->body) {
        if (!st || st->kind != NK::SubDecl) continue;
        auto* sd = static_cast<const SubDecl*>(st.get());
        if (!(sd->isMulti || sd->isProto || sd->isMethod) || sd->name.empty()) continue;
        NameScan ns; ns.stmt(st.get());
        for (auto& r : ns.refs) if (taintedName(g.names, r)) { g.groups.insert(sd->name); g.any = true; break; }
    }
    return &(genericRoleBodies_[cd] = std::move(g));
}

// One parameterization's run of its role's generic statements: in a scope of
// its own (under the role body's, so everything the body declared once is
// still in view), with the parameters bound and the role's package current,
// as Rakudo runs a role body per composition. That scope becomes the
// parameterization's declaration scope — its attribute defaults evaluate there
// and its methods close over it (recloseRoleMethods) — and the attributes are
// re-read against it: `has @.a is G::A` names this parameterization's G::A.
bool Interpreter::runGenericRoleBody(const std::shared_ptr<ClassInfo>& conc, ClassInfo* role) {
    if (!conc || !role || !role->decl) return false;
    const GenericRoleBody* g = genericRoleBody(role->decl);
    if (!g || !g->any) return false;
    auto env = std::make_shared<Env>();
    env->parent = role->declEnv ? role->declEnv : global_;
    env->packageFrame = true;
    env->x().pkgType = conc;
    for (auto& b : conc->roleParamBindings)
        if (!b.first.empty() && !env->local(b.first)) env->define(b.first, b.second);
    struct Restore {
        Interpreter& I;
        std::shared_ptr<Env> cur;
        std::string pfx;
        const std::set<std::string>* inst;
        int line;
        ~Restore() { I.tctx_.cur = cur; I.tctx_.pkgPrefix = pfx; I.roleInstNames_ = inst; I.curLine_ = line; }
    } restore{*this, tctx_.cur, tctx_.pkgPrefix, roleInstNames_, curLine_};
    tctx_.cur = env;
    tctx_.pkgPrefix = role->name + "::";
    roleInstNames_ = &g->names;
    const auto& body = role->decl->body;
    auto isSub = [](const Stmt* st) { return st && st->kind == NK::SubDecl; };
    // the subs first, as a block hoists them: a generic statement may call
    // one declared further down, and must find THIS parameterization's
    for (size_t i = 0; i < body.size() && i < g->generic.size(); i++)
        if (g->generic[i] && isSub(body[i].get())) exec(body[i].get());
    // …and a multi, proto or `my method` that mentions a generic name is
    // declared here again (whole groups: every candidate of that name), so
    // it reads THIS parameterization's lexicals
    {
        const std::set<std::string>& groups = g->groups;
        // (a group of this scope's own, so the body's candidates — closed over
        // the role body — are not what the calls here find)
        for (auto& nm : groups) {
            Value fresh; fresh.t = VT::Code; fresh.setCode(makePayload<Callable>());
            fresh.code()->name = nm;
            fresh.code()->isMultiDispatcher = true;
            env->define("&" + nm, fresh);
        }
        for (auto& st : body)
            if (isSub(st.get()) && groups.count(static_cast<const SubDecl*>(st.get())->name)) exec(st.get());
    }
    // (the subs declared here read their parameters' types — `T $x` — from
    // this scope, as a parameterization's methods do: before anything calls them)
    auto markConcrete = [&]() {
        for (auto& kv : env->vars) {
            if (kv.first.empty() || kv.first[0] != '&' || kv.second.t != VT::Code || !kv.second.code()) continue;
            Callable* c = kv.second.code();
            if (c->isMultiDispatcher) { for (auto& cand : c->candidates) if (cand.code()) cand.code()->roleConcrete = true; }
            else if (c->closure == env) c->roleConcrete = true;
        }
    };
    markConcrete();
    for (size_t i = 0; i < body.size() && i < g->generic.size(); i++)
        if (g->generic[i] && body[i] && !isSub(body[i].get())) exec(body[i].get());
    markConcrete();
    for (auto& a : conc->attrs) applyContainerElemType(a);
    conc->declEnv = env;
    return true;
}

// See Interpreter.h. Looked up the way a composition names its parent: the
// lexical binding first, then the enclosing package's own, then the alias.
void Interpreter::applyContainerElemType(ClassAttr& a) {
    if (a.containerIs.empty() || (a.sigil != '@' && a.sigil != '%')) return;
    auto it = classes_.find(lexicalTypeName(*this, a.containerIs));
    if (it == classes_.end() && !tctx_.pkgPrefix.empty()) it = classes_.find(tctx_.pkgPrefix + a.containerIs);
    if (it == classes_.end()) it = classes_.find(resolveClassAlias(a.containerIs));
    if (it == classes_.end() || !it->second || it->second->isRole) return;
    a.containerIs = it->first;
    if (!a.type.empty()) return;
    for (ClassInfo* k = it->second.get(); k; k = k->parent.get()) {
        if (k->nativeParent.empty()) continue;
        if (!k->nativeOf.empty() &&
            ((a.sigil == '@' && k->nativeParent == "Array") || (a.sigil == '%' && k->nativeParent == "Hash")))
            a.type = k->nativeOf;
        break;
    }
}

Value Interpreter::declInitial(const VarExpr* ve, char sigil) {
    // `my ::foo $x` with no `foo` anywhere: $x holds a BARE type of that name
    if (ve && !ve->declStubType.empty() && sigil == '$' && ve->declType.empty()) {
        const std::string& sn = ve->declStubType;
        if (!classes_.count(sn) && !isKnownTypeName(sn) && !(tctx_.cur && tctx_.cur->find(sn))) {
            bareStubTypes_.insert(sn);
            return Value::typeObj(sn);
        }
    }
    // A declaration whose TYPE names nothing is the error Rakudo makes it —
    // `my Foo $x` is "Type 'Foo' is not declared" there and declared an untyped
    // variable here. See declTypeIsKnown for how carefully the question is
    // asked: a refusal has to be a certainty, so every shape this cannot model
    // is answered "known".
    // Rakudo reports it as a group: the undeclared type (with its "Did you
    // mean" suggestions) as the sorrow, and the declaration it broke as the panic.
    if (ve && !ve->declType.empty() && !ve->declTypeExpr &&
        (!declTypeIsKnown(ve->declType) || requireHidden(ve->declType))) {
        auto sug = typeSuggestions(ve->declType);
        Value sl = Value::array(); sl.isList = true;
        for (auto& n : sug) sl.arr()->push_back(Value::str(n));
        std::string msg = "Type '" + ve->declType + "' is not declared" + didYouMean(sug);
        Value und = makeTypedEx("X::Undeclared",
            {{"symbol", Value::str(ve->declType)}, {"what", Value::str("Type")}, {"suggestions", sl}}, msg);
        Value sorrows = Value::array(); sorrows.isList = true;
        sorrows.arr()->push_back(und);
        Value panic = makeTypedEx("X::Syntax::Malformed", {{"what", Value::str("my")}}, "Malformed my");
        throwTypedV("X::Comp::Group", {{"sorrows", sorrows}, {"panic", panic}}, msg + "\nMalformed my");
    }
    if (ve && ve->declTypeExpr && sigil == '$') {
        try {
            Value t = eval(ve->declTypeExpr.get());
            if (t.t == VT::Type) return t;
        } catch (RakuError&) {}   // unresolvable: fall back to the textual type
    }
    if (ve && sigil == '$' && !ve->declType.empty()) {
        Value cap;
        if (capturedType(*this, tctx_.cur.get(), ve->declType, cap)) { cap.i = 0; return cap; }
    }
    return typedDefault(ve ? ve->declType : std::string(), sigil);
}

void nativeAssignCheck(const Value& v, int bits, bool isFloat, const std::string& what, bool sign) {
    if (bits <= 0) return;
    if (nativeRefusesKind(v, isFloat)) throwNativeKind(v, bits, isFloat, sign, what);
    // a native NUM refuses a Str too (`my num $n; $n = "x"`)
    if (isFloat) {
        if (v.t == VT::Str && !v.isAllomorph() && v.hashKind.empty())
            throw RakuError{Value::typeObj("X::TypeCheck::Assignment"),
                "Type check failed in assignment to " + what + "; expected num but got Str (\"" +
                v.s.str() + "\")"};
        return;
    }
    if (v.t == VT::Str && !v.isAllomorph() && v.hashKind.empty())
        throw RakuError{Value::typeObj("X::TypeCheck::Assignment"),
            "Type check failed in assignment to " + what +
            "; expected " + (bits == 64 ? "int" : "int" + std::to_string(bits)) +
            " but got Str (\"" + v.s.str() + "\")"};
    // Too wide for a machine integer at ALL is refused whatever the declared
    // width (`my int8 $x = 2**64` too); an unsigned one takes the full 64 bits.
    if (v.t == VT::Int && v.big() &&
        (sign ? !v.big()->fitsLL() : v.big()->bitLength() > 64))
        throw RakuError{Value::typeObj("X::AdHoc"),
            "Cannot unbox " + std::to_string(v.big()->bitLength()) +
            " bit wide bigint into native integer. Did you mix int and Int or literals?"};
}

// A native container cannot hold a type object, Nil included: `my int $x = Nil`
// dies "Cannot unbox a type object (Nil) to int." rather than storing (Any).
// The declared type is on a DECLARING target; a sized native assigned later is
// known by its slot's natBits.
void nativeUndefCheck(const Value& rv, const Expr* target, const Value* slot) {
    if (rv.t != VT::Nil && rv.t != VT::Type) return;
    const char* kind = nullptr;
    if (target && target->kind == NK::VarExpr) {
        auto* tv = static_cast<const VarExpr*>(target);
        if (tv->declare && tv->name.size() > 1 && tv->name[0] == '$') {
            const std::string& t = tv->declType;
            if (t == "str") kind = "str";
            else if (t.rfind("num", 0) == 0 && (t.size() == 3 || t == "num32" || t == "num64")) kind = "num";
            else if (t == "int" || t == "uint" || t == "byte" ||
                     ((t.rfind("int", 0) == 0 || t.rfind("uint", 0) == 0) &&
                      ascii::isdigit((unsigned char)t.back())))
                kind = "int";
        }
    }
    if (!kind && slot && slot->natBits) kind = slot->natFloat ? "num" : "int";
    if (!kind) return;
    const std::string what = rv.t == VT::Nil ? std::string("Nil") : rv.s.str();
    throw RakuError{Value::typeObj("X::AdHoc"),
        "Cannot unbox a type object (" + what + ") to " +
        (std::string(kind) == "int" ? "int." : std::string("a ") + kind + ".")};
}

// ---- natives for the compiling backends (--exe) ----------------------------
// The code generator decides statically which stores are into natives and which
// operations are native (Ast.h nativeNumericStatic), and calls these, so that a
// compiled program converts, checks, wraps and refuses exactly as the
// interpreter does. A native's container is a Value carrying the width tags:
// the value returned here carries them too, so a plain C++ assignment of it
// keeps the variable native.
static Value nativeStoreValue(const Value& v, int bits, bool sign, bool isFloat,
                              const std::string& name, bool srcNative) {
    Value slotLike = isFloat ? Value::number(0) : Value::integer(0);
    slotLike.natBits = bits; slotLike.natSigned = sign; slotLike.natFloat = isFloat;
    if (v.t == VT::Nil || v.t == VT::Type) nativeUndefCheck(v, nullptr, &slotLike);
    Value out = v;
    out.readonly = out.immutableBind = false;
    if (!(srcNative && (out.t == VT::Int || out.t == VT::Num)))
        nativeAssignCheck(out, bits, isFloat, name, sign);
    wrapNative(out, bits, sign, isFloat);
    return out;
}
Value rtNativeValue(const Value& v, const std::string& type, const std::string& name, bool srcNative) {
    bool sign = true;
    bool isFloat = type.rfind("num", 0) == 0;
    int bits = isFloat ? (type == "num32" ? 32 : 64) : Value::natWidthOfType(type, sign);
    if (!bits && !isFloat) { bits = 64; sign = true; }   // plain `int`
    return nativeStoreValue(v, bits, sign, isFloat, name, srcNative);
}
Value rtNativeValueLike(const Value& slot, const Value& v, const std::string& name, bool srcNative) {
    if (!slot.natBits) return v;   // not (or no longer) a native container
    return nativeStoreValue(v, slot.natBits, slot.natSigned, slot.natFloat, name, srcNative);
}
Value rtNativeArith(const char* op, const Value& l, const Value& r) {
    auto plainInt = [](const Value& v) {
        return v.t == VT::Int && !v.big() && v.hashKind.empty() && v.enumName.empty();
    };
    if (plainInt(l) && plainInt(r)) {
        long long x;
        if (rtNativeIntOp(op, l.i, r.i, x)) {
            Value out = Value::integer(x); out.natBits = 64; out.natSigned = true;
            return out;
        }
        if (std::string(op) == "**" && r.i < 0) return Value::integer(0);   // see nativeIntPowNegative
    }
    Value res = applyArith(op, l, r);
    if (res.t == VT::Num) { res.natBits = 64; res.natFloat = true; }
    return res;
}
Value rtNativeNeg(const Value& v) {
    if (v.t == VT::Int && !v.big()) {
        Value out = Value::integer((long long)(0ULL - (unsigned long long)v.i));
        out.natBits = 64; out.natSigned = true;
        return out;
    }
    if (v.t == VT::Num) { Value out = Value::number(-v.n); out.natBits = 64; out.natFloat = true; return out; }
    return applyArith("-", Value::integer(0), v);
}
void collectPHExprPublic(const Expr* e, std::set<std::string>& out) { collectPHExpr(e, out); }

// the root variable a write lands on: `@a`, `@a[$k]`, `%h<x><y>`, `$obj.attr`
static const VarExpr* writeRootVar(const Expr* e) {
    while (e) {
        switch (e->kind) {
            case NK::VarExpr: return static_cast<const VarExpr*>(e);
            case NK::Index: e = static_cast<const Index*>(e)->base.get(); break;
            case NK::MethodCall: e = static_cast<const MethodCall*>(e)->inv.get(); break;
            default: return nullptr;
        }
    }
    return nullptr;
}
static void noteWrite(const Expr* target, std::set<std::string>& writes) {
    if (const VarExpr* v = writeRootVar(target))
        if (!v->declare && v->name.size() > 1) writes.insert(v->name);
}
void gatherWritesExpr(const Expr* e, std::set<std::string>& writes,
                             std::set<std::string>& declared) {
    if (!e) return;
    switch (e->kind) {
        case NK::VarExpr: {
            auto* v = static_cast<const VarExpr*>(e);
            if (v->declare) declared.insert(v->name);
            gatherWritesExpr(v->declDefault.get(), writes, declared);
            break;
        }
        case NK::Assign: { auto* a = static_cast<const Assign*>(e);
            noteWrite(a->target.get(), writes);
            gatherWritesExpr(a->target.get(), writes, declared);
            gatherWritesExpr(a->value.get(), writes, declared); break; }
        case NK::Unary: { auto* u = static_cast<const Unary*>(e);
            if (u->op == "++" || u->op == "--") noteWrite(u->operand.get(), writes);
            gatherWritesExpr(u->operand.get(), writes, declared); break; }
        case NK::MethodCall: { auto* m = static_cast<const MethodCall*>(e);
            static const std::set<std::string> kMutators = {
                "push", "append", "unshift", "prepend", "pop", "shift", "splice",
                "delete"};
            if (kMutators.count(m->method) || m->mutate) noteWrite(m->inv.get(), writes);
            gatherWritesExpr(m->inv.get(), writes, declared);
            for (auto& a : m->args) gatherWritesExpr(a.get(), writes, declared); break; }
        case NK::Binary: { auto* b = static_cast<const Binary*>(e);
            gatherWritesExpr(b->lhs.get(), writes, declared);
            gatherWritesExpr(b->rhs.get(), writes, declared); break; }
        case NK::ChainExpr: for (auto& o : static_cast<const ChainExpr*>(e)->operands) gatherWritesExpr(o.get(), writes, declared); break;
        case NK::Call: { auto* c = static_cast<const Call*>(e); gatherWritesExpr(c->callee.get(), writes, declared);
            for (auto& a : c->args) gatherWritesExpr(a.get(), writes, declared); break; }
        case NK::Index: { auto* i = static_cast<const Index*>(e);
            gatherWritesExpr(i->base.get(), writes, declared);
            gatherWritesExpr(i->index.get(), writes, declared); break; }
        case NK::Ternary: { auto* t = static_cast<const Ternary*>(e); gatherWritesExpr(t->cond.get(), writes, declared);
            gatherWritesExpr(t->then.get(), writes, declared); gatherWritesExpr(t->els.get(), writes, declared); break; }
        case NK::Range: { auto* r = static_cast<const RangeExpr*>(e);
            gatherWritesExpr(r->from.get(), writes, declared); gatherWritesExpr(r->to.get(), writes, declared); break; }
        case NK::Pair: { auto* p = static_cast<const PairExpr*>(e);
            gatherWritesExpr(p->keyExpr.get(), writes, declared); gatherWritesExpr(p->value.get(), writes, declared); break; }
        case NK::ListExpr: for (auto& it : static_cast<const ListExpr*>(e)->items) gatherWritesExpr(it.get(), writes, declared); break;
        case NK::ArrayLit: for (auto& it : static_cast<const ArrayLit*>(e)->items) gatherWritesExpr(it.get(), writes, declared); break;
        case NK::HashLit: for (auto& it : static_cast<const HashLit*>(e)->items) gatherWritesExpr(it.get(), writes, declared); break;
        case NK::InterpStr: for (auto& it : static_cast<const InterpStr*>(e)->parts) gatherWritesExpr(it.get(), writes, declared); break;
        case NK::BlockExpr: for (auto& st : static_cast<const BlockExpr*>(e)->body) gatherWritesStmt(st.get(), writes, declared); break;
        default: break;
    }
}
void gatherWritesStmt(const Stmt* s, std::set<std::string>& writes,
                             std::set<std::string>& declared) {
    if (!s) return;
    switch (s->kind) {
        case NK::ExprStmt: gatherWritesExpr(static_cast<const ExprStmt*>(s)->e.get(), writes, declared); break;
        case NK::ReturnStmt: gatherWritesExpr(static_cast<const ReturnStmt*>(s)->value.get(), writes, declared); break;
        case NK::Block: for (auto& st : static_cast<const Block*>(s)->stmts) gatherWritesStmt(st.get(), writes, declared); break;
        case NK::IfStmt: { auto* i = static_cast<const IfStmt*>(s);
            for (auto& br : i->branches) { gatherWritesExpr(br.first.get(), writes, declared); gatherWritesStmt(br.second.get(), writes, declared); }
            gatherWritesStmt(i->elseBlock.get(), writes, declared); break; }
        case NK::WhileStmt: { auto* w = static_cast<const WhileStmt*>(s);
            gatherWritesExpr(w->cond.get(), writes, declared); gatherWritesStmt(w->body.get(), writes, declared); break; }
        case NK::ForStmt: { auto* f = static_cast<const ForStmt*>(s);
            gatherWritesExpr(f->list.get(), writes, declared); gatherWritesStmt(f->body.get(), writes, declared); break; }
        case NK::GivenStmt: { auto* g = static_cast<const GivenStmt*>(s);
            gatherWritesExpr(g->topic.get(), writes, declared);
            if (g->body) for (auto& st : g->body->stmts) gatherWritesStmt(st.get(), writes, declared); break; }
        default: break;
    }
}
// ---- INIT-phaser hoisting -------------------------------------------------
// Every INIT in the program runs ONCE, before the mainline, in source order —
// wherever it is written. Rakudo runs the one in a never-taken branch and the
// one in a sub that is never called, both before the first mainline statement;
// so `gather for 1..3 { INIT take "OH HAI"; … }` executes that take with no
// gather on the stack at all, and it throws (roast S04-statements/gather.t).
// Running it in place instead made the INIT part of the loop body.
//
// The walk therefore has to reach through EXPRESSIONS too — the roast case
// buries its INIT under `say(gather(BlockExpr(for … )))`. A node kind this
// misses simply leaves that INIT unmarked, and an unmarked INIT still runs at
// its textual position exactly as before: the fallback is the old behaviour,
// never a phaser that silently disappears.
// …with one limit, and it is a real one. Rakudo hoists an INIT in TIME while
// keeping its LEXICAL scope: `{ my $var; for … { my $s = { INIT { $var++ } } } }`
// increments the container the mainline later reads, because in Rakudo that
// container exists from compile time. rakupp builds scopes as blocks execute,
// so at program-init time the enclosing block has not run and there is no
// container to reach — hoisting that INIT makes `$var` undeclared.
//
// So a NESTED init is hoisted only when it is self-contained: every lexical it
// names, it declares itself. One that reaches outward keeps running in place,
// exactly as before (`INIT has run exactly once` in S04-phasers/init.t stays
// as wrong as it was, rather than becoming a hard error). Top-level INITs are
// unconditional — run() pre-declares the mainline's `my`s before phasers, so
// their containers do exist, which is why that case has always worked.
static bool initFreeStmt(Stmt* s, std::set<std::string>& decl);
static bool initFreeExpr(Expr* e, std::set<std::string>& decl) {
    if (!e) return false;
    switch (e->kind) {
        case NK::VarExpr: { auto* v = static_cast<VarExpr*>(e);
            if (v->declare) { decl.insert(v->name); return initFreeExpr(v->declDefault.get(), decl); }
            // dynamics ($*x) and compile-time vars ($?FILE) do not resolve
            // lexically, so an early run finds them the same way a late one does
            if (v->name.size() > 1 && (v->name[1] == '*' || v->name[1] == '?')) return false;
            return !decl.count(v->name); }
        case NK::SelfTerm: return true; // no invocant exists at program-init time
        case NK::BlockExpr: { for (auto& s : static_cast<BlockExpr*>(e)->body) if (initFreeStmt(s.get(), decl)) return true; return false; }
        case NK::Assign: { auto* a = static_cast<Assign*>(e);
            // the VALUE first: `my $x = $x` reads the OUTER $x
            return initFreeExpr(a->value.get(), decl) || initFreeExpr(a->target.get(), decl); }
        case NK::Binary: { auto* b = static_cast<Binary*>(e);
            return initFreeExpr(b->lhs.get(), decl) || initFreeExpr(b->rhs.get(), decl); }
        case NK::Unary: return initFreeExpr(static_cast<Unary*>(e)->operand.get(), decl);
        case NK::Call: { auto* c = static_cast<Call*>(e);
            if (initFreeExpr(c->callee.get(), decl)) return true;
            for (auto& a : c->args) if (initFreeExpr(a.get(), decl)) return true; return false; }
        case NK::MethodCall: { auto* m = static_cast<MethodCall*>(e);
            if (initFreeExpr(m->inv.get(), decl) || initFreeExpr(m->methodExpr.get(), decl)) return true;
            for (auto& a : m->args) if (initFreeExpr(a.get(), decl)) return true; return false; }
        case NK::Index: { auto* i = static_cast<Index*>(e);
            return initFreeExpr(i->base.get(), decl) || initFreeExpr(i->index.get(), decl); }
        case NK::Ternary: { auto* t = static_cast<Ternary*>(e);
            return initFreeExpr(t->cond.get(), decl) || initFreeExpr(t->then.get(), decl) ||
                   initFreeExpr(t->els.get(), decl); }
        case NK::Range: { auto* r = static_cast<RangeExpr*>(e);
            return initFreeExpr(r->from.get(), decl) || initFreeExpr(r->to.get(), decl); }
        case NK::Pair: { auto* p = static_cast<PairExpr*>(e);
            return initFreeExpr(p->keyExpr.get(), decl) || initFreeExpr(p->value.get(), decl); }
        case NK::ChainExpr: for (auto& o : static_cast<ChainExpr*>(e)->operands) if (initFreeExpr(o.get(), decl)) return true; return false;
        case NK::ListExpr:  for (auto& i : static_cast<ListExpr*>(e)->items)  if (initFreeExpr(i.get(), decl)) return true; return false;
        case NK::ArrayLit:  for (auto& i : static_cast<ArrayLit*>(e)->items)  if (initFreeExpr(i.get(), decl)) return true; return false;
        case NK::HashLit:   for (auto& i : static_cast<HashLit*>(e)->items)   if (initFreeExpr(i.get(), decl)) return true; return false;
        case NK::InterpStr: for (auto& p : static_cast<InterpStr*>(e)->parts) if (initFreeExpr(p.get(), decl)) return true; return false;
        case NK::IntLit: case NK::NumLit: case NK::StrLit: case NK::BoolLit:
        case NK::NameTerm: case NK::Whatever: case NK::RegexLit: case NK::AllomorphLit:
            return false;
        default: return true; // an unmodelled node may reach anywhere — don't hoist
    }
}
static bool initFreeStmt(Stmt* s, std::set<std::string>& decl) {
    if (!s) return false;
    switch (s->kind) {
        case NK::EmptyStmt: case NK::LastStmt: case NK::NextStmt: case NK::RedoStmt:
            return false;
        case NK::ExprStmt: return initFreeExpr(static_cast<ExprStmt*>(s)->e.get(), decl);
        case NK::ReturnStmt: return initFreeExpr(static_cast<ReturnStmt*>(s)->value.get(), decl);
        case NK::VarDecl: { auto* d = static_cast<VarDecl*>(s);
            if (initFreeExpr(d->init.get(), decl)) return true;
            for (auto& n : d->names) decl.insert(n);
            return false; }
        case NK::Block: { for (auto& x : static_cast<Block*>(s)->stmts) if (initFreeStmt(x.get(), decl)) return true; return false; }
        case NK::IfStmt: { auto* f = static_cast<IfStmt*>(s);
            for (auto& br : f->branches) { if (initFreeExpr(br.first.get(), decl)) return true;
                if (br.second) for (auto& x : br.second->stmts) if (initFreeStmt(x.get(), decl)) return true; }
            if (f->elseBlock) for (auto& x : f->elseBlock->stmts) if (initFreeStmt(x.get(), decl)) return true;
            return false; }
        case NK::WhileStmt: { auto* w = static_cast<WhileStmt*>(s);
            if (initFreeExpr(w->cond.get(), decl)) return true;
            if (w->body) for (auto& x : w->body->stmts) if (initFreeStmt(x.get(), decl)) return true; return false; }
        case NK::ForStmt: { auto* f = static_cast<ForStmt*>(s);
            if (initFreeExpr(f->list.get(), decl)) return true;
            for (auto& v : f->vars) decl.insert(v);
            decl.insert("$_");
            if (f->body) for (auto& x : f->body->stmts) if (initFreeStmt(x.get(), decl)) return true; return false; }
        default: return true; // conservative: anything unmodelled blocks the hoist
    }
}
// True when this INIT names no lexical it does not itself declare.
static bool initIsSelfContained(Block* b) {
    std::set<std::string> decl;
    for (auto& s : b->stmts) if (initFreeStmt(s.get(), decl)) return false;
    return true;
}
static void collectPhasersExpr(Expr* e, const char* want, std::vector<Block*>& out);
// The statement lists an END walk is currently inside, and the (list, END) edges
// it has found. An END is re-captured by the entry of EVERY block that lexically
// holds it, not just its own — Rakudo's compiler flattens the blocks between a
// phaser and its routine into one frame, so `sub f($n) { if $n == 1 { END say $n } }`
// called f(1), f(2) says 2 there even though the `if` body never ran a second
// time. Kept beside the walk rather than threaded through its forty call sites;
// thread-local because an EVAL on a worker registers its own unit. Only an END
// walk fills them, and registerEnds empties them.
thread_local std::vector<const std::vector<StmtPtr>*> g_endChain;
thread_local std::vector<Block*> g_endBlockChain;
thread_local std::vector<std::pair<const std::vector<StmtPtr>*, Block*>> g_endEdges;
static void collectPhasersBody(const std::vector<StmtPtr>& b, const char* want, std::vector<Block*>& out) {
    const bool ends = std::strcmp(want, "END") == 0;
    if (ends) g_endChain.push_back(&b);
    for (auto& s : b) collectPhasersStmt(s.get(), want, out);
    if (ends) g_endChain.pop_back();
}
static void collectPhasersExpr(Expr* e, const char* want, std::vector<Block*>& out) {
    if (!e) return;
    switch (e->kind) {
        case NK::BlockExpr: collectPhasersBody(static_cast<BlockExpr*>(e)->body, want, out); return;
        case NK::Assign: { auto* a = static_cast<Assign*>(e);
            collectPhasersExpr(a->target.get(), want, out); collectPhasersExpr(a->value.get(), want, out); return; }
        case NK::Binary: { auto* b = static_cast<Binary*>(e);
            collectPhasersExpr(b->lhs.get(), want, out); collectPhasersExpr(b->rhs.get(), want, out); return; }
        case NK::Unary: collectPhasersExpr(static_cast<Unary*>(e)->operand.get(), want, out); return;
        case NK::Call: { auto* c = static_cast<Call*>(e);
            collectPhasersExpr(c->callee.get(), want, out);
            for (auto& a : c->args) collectPhasersExpr(a.get(), want, out); return; }
        case NK::MethodCall: { auto* m = static_cast<MethodCall*>(e);
            collectPhasersExpr(m->inv.get(), want, out); collectPhasersExpr(m->methodExpr.get(), want, out);
            for (auto& a : m->args) collectPhasersExpr(a.get(), want, out); return; }
        case NK::Index: { auto* i = static_cast<Index*>(e);
            collectPhasersExpr(i->base.get(), want, out); collectPhasersExpr(i->index.get(), want, out); return; }
        case NK::Ternary: { auto* t = static_cast<Ternary*>(e);
            collectPhasersExpr(t->cond.get(), want, out); collectPhasersExpr(t->then.get(), want, out);
            collectPhasersExpr(t->els.get(), want, out); return; }
        case NK::Range: { auto* r = static_cast<RangeExpr*>(e);
            collectPhasersExpr(r->from.get(), want, out); collectPhasersExpr(r->to.get(), want, out); return; }
        case NK::Pair: { auto* p = static_cast<PairExpr*>(e);
            collectPhasersExpr(p->keyExpr.get(), want, out); collectPhasersExpr(p->value.get(), want, out); return; }
        case NK::ChainExpr: for (auto& o : static_cast<ChainExpr*>(e)->operands) collectPhasersExpr(o.get(), want, out); return;
        case NK::ListExpr:  for (auto& i : static_cast<ListExpr*>(e)->items)  collectPhasersExpr(i.get(), want, out); return;
        case NK::ArrayLit:  for (auto& i : static_cast<ArrayLit*>(e)->items)  collectPhasersExpr(i.get(), want, out); return;
        case NK::HashLit:   for (auto& i : static_cast<HashLit*>(e)->items)   collectPhasersExpr(i.get(), want, out); return;
        case NK::InterpStr: for (auto& p : static_cast<InterpStr*>(e)->parts) collectPhasersExpr(p.get(), want, out); return;
        default: return;
    }
}
void collectPhasersStmt(Stmt* s, const char* want, std::vector<Block*>& out, bool topLevel) {
    if (!s) return;
    switch (s->kind) {
        case NK::Block: { auto* b = static_cast<Block*>(s);
            // a collected phaser's own body is NOT re-walked: a phaser nested
            // inside it belongs to that one execution, not to a second hoist
            // that would run it twice
            if (b->phaser == want) {
                if (b->phaser == "END") { // always, wherever it is
                    out.push_back(b);
                    // …and every scope that holds it: a Block takes the list on
                    // its own node, a routine body through the interpreter's map.
                    for (auto* blk : g_endBlockChain) blk->endsWithin.push_back(b);
                    for (auto* body : g_endChain) g_endEdges.push_back({body, b});
                    return;
                }
                // top level: always (its containers are pre-declared).
                // nested: only if it reaches no scope that has yet to exist.
                if (topLevel || initIsSelfContained(b)) { b->initHoisted = true; out.push_back(b); }
                return;
            }
            if (std::strcmp(want, "END") == 0) {
                g_endBlockChain.push_back(b);
                collectPhasersBody(b->stmts, want, out);
                g_endBlockChain.pop_back();
                return;
            }
            collectPhasersBody(b->stmts, want, out); return; }
        case NK::ExprStmt: collectPhasersExpr(static_cast<ExprStmt*>(s)->e.get(), want, out); return;
        case NK::VarDecl:  collectPhasersExpr(static_cast<VarDecl*>(s)->init.get(), want, out); return;
        case NK::ReturnStmt: collectPhasersExpr(static_cast<ReturnStmt*>(s)->value.get(), want, out); return;
        case NK::SubDecl:  collectPhasersBody(static_cast<SubDecl*>(s)->body, want, out); return;
        case NK::IfStmt: { auto* f = static_cast<IfStmt*>(s);
            for (auto& br : f->branches) { collectPhasersExpr(br.first.get(), want, out);
                if (br.second) collectPhasersBody(br.second->stmts, want, out); }
            if (f->elseBlock) collectPhasersBody(f->elseBlock->stmts, want, out); return; }
        case NK::WhileStmt: { auto* w = static_cast<WhileStmt*>(s);
            collectPhasersExpr(w->cond.get(), want, out);
            if (w->body) collectPhasersBody(w->body->stmts, want, out); return; }
        case NK::RepeatStmt: { auto* r = static_cast<RepeatStmt*>(s);
            if (r->body) collectPhasersBody(r->body->stmts, want, out);
            collectPhasersExpr(r->cond.get(), want, out); return; }
        case NK::ForStmt: { auto* f = static_cast<ForStmt*>(s);
            collectPhasersExpr(f->list.get(), want, out);
            if (f->body) collectPhasersBody(f->body->stmts, want, out); return; }
        case NK::LoopStmt: { auto* l = static_cast<LoopStmt*>(s);
            collectPhasersExpr(l->init.get(), want, out); collectPhasersExpr(l->cond.get(), want, out);
            collectPhasersExpr(l->incr.get(), want, out);
            if (l->body) collectPhasersBody(l->body->stmts, want, out); return; }
        case NK::GivenStmt: { auto* g = static_cast<GivenStmt*>(s);
            collectPhasersExpr(g->topic.get(), want, out);
            if (g->body) collectPhasersBody(g->body->stmts, want, out); return; }
        case NK::WhenStmt: { auto* w = static_cast<WhenStmt*>(s);
            collectPhasersExpr(w->cond.get(), want, out);
            if (w->body) collectPhasersBody(w->body->stmts, want, out); return; }
        case NK::ClassDecl: { auto* c = static_cast<ClassDecl*>(s);
            for (auto& m : c->methods) collectPhasersStmt(m.get(), want, out);
            collectPhasersBody(c->body, want, out); return; }
        case NK::EnumDecl: collectPhasersExpr(static_cast<EnumDecl*>(s)->values.get(), want, out); return;
        default: return;
    }
}

// Sink a ROUTINE's RETURNED value — MAIN's, which nobody looks at (Rakudo runs
// `sub MAIN` in sink context). A return value is already decontainerized, so
// unlike the statement path this needs no "fresh object" test before running a
// user `sink` method; the rest is the one rule above.
void Interpreter::sinkReturnedValue(const Value& r) {
    if (r.t == VT::Object && r.obj() && r.obj()->cls && r.obj()->cls->findMethod("sink"))
        methodCall(r, "sink", ValueList{});
    sinkValue(r);
}

bool Interpreter::bodyUsesAtUnderscore(const Callable* c) {
    if (!c || !c->body) return false;
    std::set<std::string> ph;
    for (auto& s : *c->body) collectPHStmt(s.get(), ph);
    return ph.count("@_") > 0;
}

// the first placeholder-ish name (incl. @_) in a body that TAKES NO signature —
// for the X::Placeholder::Block diagnostic
std::string firstBlockPlaceholder(const std::vector<StmtPtr>& body) {
    std::set<std::string> ph;
    for (auto& s : body) collectPHStmt(s.get(), ph);
    for (auto& n : ph) if (n[1] == '^' || n[1] == ':') return n;
    if (ph.count("@_")) return "@_";
    return "";
}

// every `$!x`/`@!x`/`%!x` attribute reference in a body (for undeclared-attribute checks)
std::vector<std::string> collectAttrRefs(const std::vector<StmtPtr>& body) {
    std::set<std::string> ph;
    for (auto& s : body) collectPHStmt(s.get(), ph);
    std::vector<std::string> out;
    for (auto& n : ph) if (n[1] == '!') out.push_back(n);
    return out;
}

Value listToArray(const ValueList& items) {
    // Post-GLR: a comma list keeps every member as-is — nested Lists, Ranges,
    // and word lists stay single elements. Only Slips splice (`|x`, slip(),
    // .Slip — all tagged s=="Slip" by their producers).
    Value v = Value::array();
    for (auto& it : items) {
        if (it.t == VT::Array && it.arr() && it.s == "Slip")
            v.arr()->insert(v.arr()->end(), it.arr()->begin(), it.arr()->end());
        else v.arr()->push_back(it);
    }
    return v;
}

// Turn a raw argv into MAIN's argument list: --opt / --opt=val / --/opt become
// named args, everything else stays a positional string. Shared by the
// interpreter's auto-invoke and the native-codegen main().
// Rakudo-format usage text generated from &MAIN's signature(s): the `Usage:`
// header, one line per candidate, then an option list built from `#=` trailing
// declarator pod, aligned, with [default: X] for defaulted params.
std::string Interpreter::mainUsage() {
    Value* mainSub = tctx_.cur ? tctx_.cur->find("&MAIN") : nullptr;
    if (!mainSub && global_) mainSub = global_->find("&MAIN");
    if (!mainSub || mainSub->t != VT::Code || !mainSub->code()) return "";
    ValueList cands;
    if (mainSub->code()->isMultiDispatcher)
        for (auto& c : mainSub->code()->candidates) cands.push_back(c);
    else cands.push_back(*mainSub);
    // a candidate marked `is hidden-from-USAGE` is not advertised
    cands.erase(std::remove_if(cands.begin(), cands.end(),
                               [](const Value& c) { return c.code() && c.code()->hiddenFromUsage; }),
                cands.end());
    std::string out = "Usage:\n";
    struct Opt { std::string label, desc, def; };
    std::vector<Opt> opts;
    auto bare = [](const Param& p) {
        const std::string& n = p.namedKey.empty() ? p.name : p.namedKey;
        return (!n.empty() && (n[0] == '$' || n[0] == '@' || n[0] == '%' || n[0] == '&'))
             ? n.substr(1) : n;
    };
    for (auto& cand : cands) {
        if (!cand.code() || !cand.code()->params) continue;
        std::string line = "  " + (progName().empty() ? std::string("<program>") : progName());
        // Rakudo lists the OPTIONS first and the positionals after, whatever
        // order they were declared in — so the two are collected separately and
        // joined below. (The option list underneath keeps declaration order.)
        std::string named, positional;
        for (auto& p : *cand.code()->params) {
            std::string label;
            if (p.litVal) { positional += " " + eval(p.litVal.get()).toStr(); continue; }
            if (p.named) {
                // A one-character name is a SHORT option: `-x`, not `--x`. An
                // untyped one takes `[=Any]`, a typed one `=<Type>`, and a Bool
                // takes nothing at all because its presence is the value.
                std::string nm = bare(p);
                label = (nm.size() == 1 ? "-" : "--") + nm;
                // A type that would take the bare flag (`--h` is True, and a
                // Bool is an Int, a Cool, an Any) leaves the value optional:
                // `[=Int]`, `[=S]` for a subset of Any; Str, Num and their
                // subsets need one: `=<Str>` (Rakudo)
                auto takesBareFlag = [&](std::string t) {
                    for (int d = 0; d < 16; d++) {
                        auto sit = subsets_.find(t);
                        if (sit == subsets_.end()) break;
                        t = sit->second.base;
                    }
                    return t.empty() || typeOrSubsetMatches(Value::boolean(true), t);
                };
                if (p.type == "Bool") { }
                else if (p.type.empty()) label += "[=Any]";
                else if (p.sigil == '$' && takesBareFlag(p.type)) label += "[=" + p.type + "]";
                else label += "=<" + p.type + ">";
                // a REQUIRED named param (`:$foo!` or `is required`) prints
                // without the optionality brackets, as in Rakudo (issue #17)
                if (p.required) named += " " + label;
                else named += " [" + label + "]";
            }
            else if (p.slurpy) { label = "[<" + bare(p) + "> ...]"; positional += " " + label; }
            else if (p.optional || p.defaultVal) { label = "[<" + bare(p) + ">]"; positional += " " + label; }
            else { label = "<" + bare(p) + ">"; positional += " " + label; }
            if (!p.pod.empty()) {
                std::string def;
                if (p.defaultVal) {
                    try {
                        Value d = eval(p.defaultVal.get());
                        // A string default is shown quoted, so `[default: '.']`
                        // cannot be read as punctuation of the sentence around it.
                        def = d.t == VT::Str ? "'" + d.toStr() + "'" : d.gist();
                    } catch (...) {}
                }
                opts.push_back({label, p.pod, def});
            }
        }
        line += named + positional;
        // …and the routine's own `#|` becomes the candidate's description.
        if (!cand.code()->pod.empty()) line += " -- " + cand.code()->pod;
        out += line + "\n";
    }
    if (!opts.empty()) {
        out += "  \n";
        size_t w = 0;
        for (auto& o : opts) w = std::max(w, o.label.size());
        for (auto& o : opts) {
            out += "    " + o.label + std::string(w - o.label.size() + 4, ' ') + o.desc;
            if (!o.def.empty()) out += " [default: " + o.def + "]";
            out += "\n";
        }
    }
    return out;
}

Value valAllomorph(const Value& v) {
    if (v.t != VT::Str) return v;
    // whitespace-only input stays Str — but the EMPTY string is IntStr(0, "")
    // under Rakudo (val(""), an empty --opt= value, an empty %*ENV entry)
    if (!v.s.empty() && v.s.find_first_not_of(" \t\n\r\f\v") == std::string::npos) return v;
    Value n = numifyStr(v.s);
    switch (n.t) {
        case VT::Int:     n.hashKind = "IntStr";     break;
        case VT::Rat:     n.hashKind = "RatStr";     break;
        case VT::Num:     n.hashKind = "NumStr";     break;
        // a bare `i` (or `+i`/`-i`) is a WORD, not the imaginary unit: val("i")
        // is Str under Rakudo, and Text::SubParsers runs every token of "The
        // average mass is 55 lbs." through val — "is" came back as a Str, but
        // its numeric sub-parser then tried the prefix "i" and got 0+1i.
        case VT::Complex: if (v.s.str().find_first_of("0123456789") == std::string::npos) return v;
                          n.hashKind = "ComplexStr"; break;
        default: return v;
    }
    n.s = v.s; // the allomorph's Str face is the original spelling
    return n;
}

// One command-line word against the PROGRAM's own scope. Rakudo reads every
// argument this way — it looks the name up and takes what it finds when that is
// an ENUM VALUE — which is why `enum Color <Red …>` makes `prog Red` arrive as
// `Color::Red`, and why `True` arrives as the Bool (rakudo#2794).
//
// The program's scope is consulted FIRST and is final: a name it declared as
// something else keeps the word a string, so `my constant True = "shadow"`
// leaves `True` the Str it was spelled as, exactly as under Rakudo. Only a name
// nobody declared reaches CORE's own members.
//
// The lookup is deliberately narrow — `find(n)` and nothing else. It must not
// go through rtNameTerm, which CALLS a routine of that name and invokes no-arg
// builtins: an argument that happened to read `now`, or to match the program's
// own `sub baz`, would have RUN it. Routines live under `&baz` anyway, so a bare
// find cannot see them, and a type object is VT::Type and no enum value — which
// is why `Int`, `Klass` and an enum's own name `Color` stay strings.
bool Interpreter::mainArgEnum(const std::string& n, Value& out) {
    if (n.empty()) return false;
    Value* p = tctx_.cur ? tctx_.cur->find(n) : nullptr;
    if (!p && global_) p = global_->find(n);
    if (p) {
        // An enum VALUE is an Int carrying its member key, and its enum's type
        // name when the enum has one — an ANONYMOUS `enum <Aa Bb>` has members
        // all the same, and Rakudo converts those too. On an Int those two
        // fields are set by nothing but an enum member. Bool's two are stored
        // natively here and are every bit the members Raku says they are.
        //
        // The enum's own name is bound as well, to the type-LIST, which wears
        // the same enumType and is no member — it is an Array, so `prog Color`
        // stays the word it was spelled as, as under Rakudo.
        if (p->t == VT::Bool ||
            (p->t == VT::Int && (!p->enumType.empty() || !p->enumName.empty()))) { out = *p; return true; }
        return false;   // the name is taken, and not by an enum value
    }
    // A natively compiled program's own enums live in no Env — they are C++
    // statics the generated startup hands to registerEnumMember — so a compiled
    // binary reads the same argument line as the interpreter does.
    if (!compiledEnums_.empty()) {
        auto it = compiledEnums_.find(n);
        if (it != compiledEnums_.end()) { out = it->second; return true; }
    }
    return coreEnumValue(n, out);
}

ValueList rtMainArgs(const std::vector<std::string>& argv, bool namedAnywhere, Interpreter* scope) {
    ValueList pos;
    // A repeated option collects EVERY value into one named arg (insertion
    // order), exactly as RUN-MAIN-args-to-capture does: `--x=a --x=b` is
    // :x(["a","b"]) — which binds :@x whole, and (oracle-verified) FAILS to
    // bind a scalar Str :$x instead of silently keeping the last value.
    std::vector<std::pair<std::string, ValueList>> named;
    auto addNamed = [&](std::string key, Value v) { // --opt args bind to :$named params
        auto slot = std::find_if(named.begin(), named.end(),
                                 [&](auto& kv) { return kv.first == key; });
        if (slot == named.end()) named.push_back({std::move(key), {std::move(v)}});
        else slot->second.push_back(std::move(v));
    };
    // Rakudo runs every command-line argument through val(), so a numeric-looking
    // one arrives as a real IntStr/RatStr and binds Int/Rat/Num params by its
    // VALUE — which is what makes `UInt` reject `-2` instead of merely inspecting
    // the spelling. See issue #11.
    // Ahead of val() comes the command line's OWN rule, which val() has no part
    // in (`val("True")` is still the Str): a word that NAMES an enum value IS
    // that value. mainArgEnum resolves it against the program's scope, so
    // `--tls=True` binds the `Bool :$tls` a program wrote for `--tls` (issue
    // #95) and `prog Red` reaches a `Color` parameter (rakudo#2794).
    //
    // Being a rule about the spelling and not about the parameter, it is exact
    // and type-blind in both directions: `--tls=1`, `--tls=yes` and `--tls=true`
    // name nothing, stay Str and fail to bind that Bool, while `Str :$a` refuses
    // `--a=True` — it is handed a Bool.
    //
    // With no interpreter in reach only Bool's four spellings convert; they are
    // the ones that need no scope, and the ones every program uses.
    auto argValue = [scope](const std::string& str) -> Value {
        Value ev;
        if (scope) { if (scope->mainArgEnum(str, ev)) return ev; }
        else if (str == "True"  || str == "Bool::True")  return Value::boolean(true);
        else if (str == "False" || str == "Bool::False") return Value::boolean(false);
        return valAllomorph(Value::str(str));
    };
    // Rakudo's conventions, oracle-verified case by case. The loop mirrors
    // default-args-to-capture, whose CHECK ORDER is observable:
    //   1. a bare `--`, met while the loop is still live, is consumed and the
    //      whole rest is positional (`-- pos --foo=x` keeps "--foo=x" literal
    //      — how a positional -5 is passed);
    //   2. otherwise, once a positional has been taken (and named-anywhere is
    //      off), the current token AND the whole rest are positional, verbatim
    //      — `pos --foo=x` makes "--foo=x" a literal string, and a `--` deeper
    //      in the tail stays a literal "--". Check 1 running FIRST is what
    //      consumes a `--` directly after the first positional (`pos -- x`
    //      -> pos, x) while `pos y -- x` keeps its "--";
    //   3. option spellings: `--foo`, and single-dash/colon short forms
    //      (`-v`, `-n=3`, `-foo=bar`, `:v`, `:n=3` — the whole rest of the
    //      token is the name, `=` splits off the value; `--/k`, `-/k`, `:/k`
    //      negate). A lone `-` or `:` is positional (for `:`, Rakudo 2026.07
    //      dies with an internal shift error — kept a positional here).
    // With %*SUB-MAIN-OPTS<named-anywhere> check 2 goes away — options bind
    // wherever they appear — but the first `--` still ends them.
    for (size_t i = 0; i < argv.size(); i++) {
        const std::string& a = argv[i];
        if (a == "--") { // check 1: consumed, everything after is positional
            for (++i; i < argv.size(); i++) pos.push_back(argValue(argv[i]));
            break;
        }
        if (!namedAnywhere && !pos.empty()) { // check 2: this + rest, verbatim
            for (; i < argv.size(); i++) pos.push_back(argValue(argv[i]));
            break;
        }
        if (a.size() > 1 && (a[0] == '-' || a[0] == ':')) {
            std::string rest = a[0] == ':' ? a.substr(1)
                             : a[1] == '-' ? a.substr(2) : a.substr(1);
            // %*SUB-MAIN-OPTS extras (Rakudo's RUN-MAIN options)
            if (scope && !rest.empty()) {
                const bool single = a[0] == '-' && a[1] != '-';
                // :allow-no — `--no-foo` is :foo(False)
                if (a[0] == '-' && a[1] == '-' && rest.size() > 3 && rest.compare(0, 3, "no-") == 0 &&
                    rest.find('=') == std::string::npos && scope->mainOption("allow-no").truthy()) {
                    addNamed(rest.substr(3), Value::boolean(false));
                    continue;
                }
                // :numeric-suffix-as-value — `-j2` is :j(2)
                if (single && rest.size() > 1 && ascii::isalpha((unsigned char)rest[0]) &&
                    std::all_of(rest.begin() + 1, rest.end(), [](char ch) { return ascii::isdigit((unsigned char)ch); }) &&
                    scope->mainOption("numeric-suffix-as-value").truthy()) {
                    addNamed(rest.substr(0, 1), argValue(rest.substr(1)));
                    continue;
                }
                // :bundling — `-ab` is :a, :b
                if (single && rest.size() > 1 && rest.find('=') == std::string::npos && rest[0] != '/' &&
                    std::all_of(rest.begin(), rest.end(), [](char ch) { return ascii::isalpha((unsigned char)ch); }) &&
                    scope->mainOption("bundling").truthy()) {
                    for (char ch : rest) addNamed(std::string(1, ch), Value::boolean(true));
                    continue;
                }
            }
            if (!rest.empty()) {
                if (rest[0] == '/') { addNamed(rest.substr(1), Value::boolean(false)); continue; }
                auto eq = rest.find('=');
                if (eq != std::string::npos) { addNamed(rest.substr(0, eq), argValue(rest.substr(eq + 1))); continue; }
                addNamed(rest, Value::boolean(true));
                continue;
            }
        }
        pos.push_back(argValue(a));
    }
    // :coerce-allomorphs-to(Type) — the positional allomorphs become that type
    if (scope) {
        Value to = scope->mainOption("coerce-allomorphs-to");
        if (to.t == VT::Type && !to.s.empty())
            for (auto& v : pos)
                if (v.isAllomorph()) v = scope->methodCall(v, to.s.str(), ValueList{});
    }
    // positionals first, then the named args — the same capture shape the
    // RUN-MAIN builtin produces; named binding is by key, not position
    ValueList margs = std::move(pos);
    for (auto& kv : named) {
        Value v;
        if (kv.second.size() == 1) v = std::move(kv.second[0]);
        else { v = Value::array(); *v.arr() = std::move(kv.second); }
        Value p = Value::pair(kv.first, std::move(v));
        p.namedArg = true;
        margs.push_back(std::move(p));
    }
    return margs;
}
Value Interpreter::rtGather(Value blockClosure) {
#if RAKUPP_HAVE_CORO
    // compiled code's gather is the interpreter's: a coroutine (see GatherCoro)
    return gatherSeqForNative(*this, std::move(blockClosure));
#endif
    auto runGather = [this, blockClosure](size_t limit, long long budgetUs, ValueList& out) -> bool {
        auto collector = makePayload<ValueList>();
        pushGatherFrame(collector, limit, budgetUs ? nowMicros() + budgetUs : 0);
        bool hit = false;
        try { ValueList noargs; callCallable(blockClosure, noargs); }
        catch (StopGatherEx&) { hit = true; }
        catch (...) { popGatherFrame(); throw; }
        popGatherFrame();
        out = std::move(*collector);
        return hit;
    };
    const size_t INITIAL = 64;
    const long long PROBE_US = 20000;   // as in the interpreter's gather — see there for why
    ValueList prefix;
    if (!runGather(INITIAL, PROBE_US, prefix)) { // finite: eager, but still a Seq
        Value a = Value::array(std::move(prefix)); a.isList = true; a.s = "Seq"; return a;
    }
    Value arr = Value::array(prefix); arr.isList = true; arr.s = "Seq";
    auto st = std::make_shared<LazySeqState>();
    st->appendNext = [this, runGather](ValueList& out) -> bool {   // growing runs unbudgeted
        ValueList grown;
        bool more = runGather(out.size() + std::max<size_t>(64, out.size()), 0, grown);
        for (size_t i = out.size(); i < grown.size(); i++) out.push_back(grown[i]);
        return more;
    };
    arr.extM() = st;
    return arr;
}

Value Interpreter::seqOp(Value l, Value r, bool exclusive) {
    // The sequence operator, callable from both evalBinary and native codegen:
    // seed list [, generator closure] ... endpoint|*|Code.
    // A LIST on the right: its first element is the endpoint and the rest
    // follow the sequence (`-3 ... ^3` is -3 -2 -1 0 1 2)
    // `1 ... ()` — an empty list has no endpoint to aim at
    if (r.t == VT::Array && r.arr() && r.arr()->empty() && !r.ext() && !isJunction(r))
        throwTypedV("X::Cannot::Empty", {{"action", Value::str("get sequence endpoint")},
                                         {"what", Value::str("list (use * or :!elems instead?)")}},
                    "Cannot get sequence endpoint from an empty list (use * or :!elems instead?)");
    // a LAZY list on the left (`@fib ... 8`): its own elements, up to the endpoint
    // (…or up to the first element a CODE endpoint accepts: `@fib ...^ * > 10000`)
    const bool codeEnd = r.t == VT::Code && r.code() && l.t == VT::Array && l.ext() &&
                         std::static_pointer_cast<LazySeqState>(l.ext())->infinite;
    if (l.t == VT::Array && l.ext() && r.t != VT::Whatever && !isJunction(r) &&
        (codeEnd || r.t == VT::Int || r.t == VT::Num || r.t == VT::Rat || r.t == VT::Str)) {
        auto st = std::static_pointer_cast<LazySeqState>(l.ext());
        ValueList buf = l.arr() ? *l.arr() : ValueList{};
        Value out = Value::array(); out.isList = true; out.s = "Seq";
        for (size_t i = 0; i < 1000000; i++) {
            while (buf.size() <= i && st && st->appendNext && st->appendNext(buf)) {}
            if (buf.size() <= i) break;
            const Value& e = buf[i];
            bool hit = codeEnd ? boolify(callCallable(r, ValueList{e}))
                     : (e.isNumeric() && r.isNumeric()) ? applyArith("==", e, r).truthy() : e.toStr() == r.toStr();
            if (hit) { if (!exclusive) out.arr()->push_back(e); break; }
            out.arr()->push_back(e);
        }
        return out;
    }
    if ((r.t == VT::Range && r.rTo() < 1000000000LL && r.rFrom() > -1000000000LL) ||
        (r.t == VT::Array && r.arr() && !r.ext() && r.isList && r.arr()->size() > 1)) {
        ValueList rl = r.flatten();
        if (rl.size() >= 2 && rl.size() < 1000000) {
            Value res = seqOp(l, rl[0], exclusive);
            if (res.t == VT::Array && res.arr() && !res.ext()) {
                for (size_t i = 1; i < rl.size(); i++) res.arr()->push_back(rl[i]);
                return res;
            }
        }
    }
    {
        // A comma-list `a, b, c` is a list of seeds; take its direct elements only
        // (a SHALLOW split — a deep flatten would collapse an array-valued seed like
        // `[1]` to `1` and break an array sequence `[1], -> @b {…} … *`).
        ValueList seed;
        // a FINITE lazy list on the left (`my @abc = lazy <a b c>; @abc ... *`)
        // seeds with its elements, which its buffer does not hold until read
        if (l.t == VT::Array && declLazyLive(l)) forceLazy(l);
        if (l.t == VT::Array) seed = *l.arr();
        else if (l.t == VT::Range) seed = l.flatten();
        else seed = ValueList{l};
        Value gen; bool hasGen = false; // a trailing closure is the generator
        if (!seed.empty() && seed.back().t == VT::Code) { gen = seed.back(); seed.pop_back(); hasGen = true; }
        // a Code endpoint (`… * > 100`) terminates the sequence at the first
        // element it accepts (included for `...`, dropped for `...^`)
        // a TYPE endpoint (`… … Rat`, `… … Str`) stops at the first element of
        // that type: it is a smartmatcher, like a Code one
        // `… … 2|3` — a JUNCTION endpoint smartmatches too
        if (isJunction(r)) {
            Value jr = r;
            r = Value::closure([this, jr](ValueList& a) -> Value {
                return a.empty() ? Value::boolean(false) : Value::boolean(boolify(smartmatchValue("~~", a[0], jr)));
            });
        }
        if (r.t == VT::Type && r.s != "Whatever" && r.s != "HyperWhatever") {
            const std::string tn = r.s.str();
            Value tc = Value::closure([this, tn](ValueList& a) -> Value {
                return Value::boolean(!a.empty() && typeOrSubsetMatches(a[0], tn));
            });
            r = tc;
        }
        // a REGEX endpoint is smartmatched, not a generator: "fom" ... /foo/
        if (r.t == VT::Regex) {
            Value rx = r;
            r = Value::closure([this, rx](ValueList& a) -> Value {
                return Value::boolean(!a.empty() && boolify(smartmatchValue("~~", a[0], rx)));
            });
        }
        bool endCode = (r.t == VT::Code);
        bool infinite = (r.t == VT::Whatever) || (r.t == VT::Num && std::isinf(r.n));
        // `-Inf` is unbounded too, but DOWNWARDS: it picks .pred for a lone
        // seed (`3 ... -Inf` is 3 2 1 0 …), where `*` and `Inf` pick .succ
        const bool negInf = r.t == VT::Num && std::isinf(r.n) && r.n < 0;
        double endVal = (infinite || endCode) ? 0 : r.toNum();
        Value out = Value::array(); out.isList = true; out.s = "Seq"; // (1...5).WHAT is (Seq)
        for (auto& s : seed) out.arr()->push_back(s);
        // an EMPTY seed has no start value (`() ... *`); `{ } ... *` seeds from the closure itself
        if (out.arr()->empty() && !hasGen)
            throwTypedV("X::Cannot::Empty", {{"action", Value::str("get sequence start value")},
                                             {"what", Value::str("list")}},
                        "Cannot get sequence start value from an empty list");
        // String sequence: "a"..."e" climbs via strSucc, "E"..."A" descends via strPred.
        if (!hasGen && !infinite && r.t == VT::Str && seed.back().t == VT::Str) {
            std::string end = r.s, cur = seed.back().s;
            bool desc = cur > end;
            if (cur == end) { if (exclusive) out.arr()->pop_back(); return out; }
            // Single-CODEPOINT endpoints step by codepoint, not by succ — the same
            // rule `..` already follows. succ only knows the magic alphabets, so
            // ('☀' ... '☕') left the seed unchanged and re-pushed it a million
            // times (11 MB of ☀); by codepoint it is the 22 characters Rakudo gives.
            if (u8CpLen(cur) == 1 && u8CpLen(end) == 1) {
                long long a = (long long)u8FirstCp(cur), b = (long long)u8FirstCp(end);
                for (long long cp = a + (desc ? -1 : 1); desc ? cp >= b : cp <= b; cp += desc ? -1 : 1) {
                    if (cp == b && exclusive) break;
                    out.arr()->push_back(Value::str(cpToU8((uint32_t)cp)));
                }
                return out;
            }
            // Same-length endpoints walk EACH POSITION over its own range, the
            // rightmost fastest: '000' ... '077' is the octal digits, 'az' ... 'bc'
            // runs z→c downwards under a→b (Rakudo's RangeIter over the chars)
            if (u8CpLen(cur) == u8CpLen(end) && u8CpLen(cur) > 1) {
                auto cps = [](const std::string& x) {
                    std::vector<uint32_t> v;
                    for (size_t i = 0; i < x.size();) {
                        unsigned char c = x[i]; int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
                        v.push_back(u8FirstCp(x.substr(i, n))); i += n;
                    }
                    return v;
                };
                std::vector<uint32_t> a = cps(cur), b = cps(end), at = a;
                const size_t SCAPR = 1000000;
                for (;;) {
                    // odometer step, rightmost position first
                    size_t k = at.size();
                    while (k-- > 0) {
                        if (at[k] != b[k]) { at[k] += a[k] <= b[k] ? 1 : (uint32_t)-1; break; }
                        at[k] = a[k];
                        if (k == 0) { k = (size_t)-1; break; }
                    }
                    if (k == (size_t)-1) break;
                    std::string sv; for (uint32_t cp : at) sv += cpToU8(cp);
                    if (sv == end && exclusive) break;
                    out.arr()->push_back(Value::str(sv));
                    if (sv == end || out.arr()->size() >= SCAPR) break;
                }
                return out;
            }
            const size_t SCAP = 1000000;
            const long long endLen = u8CpLen(end);
            while (out.arr()->size() < SCAP) {
                bool ok = true;
                std::string nxt = desc ? strPred(cur, ok) : strSucc(cur);
                // going DOWN, a pred that has nowhere to go is an error, not an
                // end: `'ZZ' ... 'A'` reaches 'AA', whose leftmost letter is never
                // removed (Rakudo: "Decrement out of range")
                if (!ok && desc)
                    throw RakuError{Value::typeObj("X::AdHoc"), "Decrement out of range"};
                if (!ok || nxt == cur) break;   // a value succ/pred cannot advance
                long long nxtLen = u8CpLen(nxt);
                if (!desc && (nxtLen > endLen || (nxtLen == endLen && nxt > end))) break;
                // …and it stops at the first value that sorts BEFORE the end,
                // whatever its length: 'Z' ... 'AA' is Z Y … B, as 'A' lt 'AA'
                if (desc && nxt < end) break;
                out.arr()->push_back(Value::str(nxt));
                cur = nxt;
                if (cur == end) { if (exclusive) out.arr()->pop_back(); break; }
            }
            return out;
        }
        // A step is DEDUCED from the last three seeds (or two) — anything before
        // them is junk Rakudo ignores, so `1,1,1,2,3 ...` continues by +1. And a
        // window that is not all numeric is no window at all: deduction reads its
        // seeds AS NUMBERS, while a chained sequence hands each segment the whole
        // previous group, so `'a' ... 'c', 0 ... 2` arrives here seeded ('c', 0)
        // and `'c'.toNum` is 0 — which deduced a step of 0 and repeated the seed
        // forever. Rakudo steps by .succ from the last seed in that case, so fall
        // back to the single-seed rules and let that seed alone type the walk.
        const size_t dedWin = seed.size() >= 3 ? 3 : seed.size();
        bool numericWindow = dedWin >= 2;
        for (size_t k = seed.size() - dedWin; numericWindow && k < seed.size(); k++)
            if (!seed[k].isNumeric()) numericWindow = false;
        bool allInt = true;
        for (size_t k = (numericWindow || seed.empty()) ? 0 : seed.size() - 1; k < seed.size(); k++)
            if (seed[k].t != VT::Int && seed[k].t != VT::Bool) allInt = false;
        bool deduceFailed = false; std::string dedFrom; // see the deduction arm below
        auto seqDeduceThrow = [this](const std::string& from) {
            std::string msg = "Unable to deduce arithmetic or geometric sequence from: " + from;
            auto& ci = classes_["X::Sequence::Deduction"];
            auto od = makePayload<ObjectData>();
            od->cls = ci;
            od->attrs["from"] = Value::str(from);
            od->attrs["message"] = Value::str(msg);
            throw RakuError{Value::object(od), msg};
        };
        double step = 1; bool geometric = false; double ratio = 1;
        if (!hasGen) {
            if (seed.size() >= 3 && numericWindow) {
                // Rakudo deduces from the LAST THREE seeds only: `1,1,1,2,3 ...`
                // continues +1 from its 1,2,3 tail (S03-sequence/basic.t), junk
                // before the tail notwithstanding. Constant difference =>
                // arithmetic; else constant ratio => geometric (1,2,4,8 → ×2);
                // else the sequence is underivable and Rakudo refuses to guess.
                size_t n3 = seed.size();
                double a1 = seed[n3 - 3].toNum(), a2 = seed[n3 - 2].toNum(), a3 = seed[n3 - 1].toNum();
                bool arith = std::abs((a3 - a2) - (a2 - a1)) <= 1e-9;
                bool geom  = !arith && a1 != 0 && a2 != 0 &&
                             std::abs(a3 / a2 - a2 / a1) <= 1e-9;
                if (arith) step = a3 - a2;
                else if (geom) { geometric = true; ratio = a3 / a2; }
                else {
                    // Neither arithmetic nor geometric. WHEN this throws is
                    // part of the semantics: Rakudo streams the SEEDS first
                    // and only deduces on generating BEYOND them — roast's
                    // advent2012-day14 runs `@primes ...^ * > sqrt $n` whose
                    // endpoint fires inside the seeds, so no deduction ever
                    // happens; while a BOUNDED numeric endpoint needs the
                    // step up front and throws here (misc.t: 1,2,5...10).
                    deduceFailed = true;
                    for (size_t k = 0; k < seed.size(); k++) { if (k) dedFrom += ","; dedFrom += seed[k].toStr(); }
                }
            } else if (seed.size() == 2 && numericWindow) step = seed[1].toNum() - seed[0].toNum();
            else if (!endCode && (infinite ? negInf : out.arr()->back().toNum() > endVal)) step = -1;
            if (deduceFailed && !infinite && !endCode)
                seqDeduceThrow(dedFrom); // a bounded endpoint needs the step NOW
        }
        // Non-numeric seeds (Str / object with .succ) step via succ/pred when the
        // endpoint is Whatever or Code: 'a' ... * ; H.new ... *.y > 10
        bool succSeed = !hasGen && !seed.empty() && (infinite || endCode) &&
                        (seed.back().t == VT::Str || seed.back().t == VT::Object);
        bool succDesc = succSeed && seed.size() >= 2 &&
                        seed[seed.size() - 2].toStr() > seed.back().toStr();
        // …and two EQUAL last seeds make a constant sequence: 'c', 'c' ... *
        const bool succConst = succSeed && seed.size() >= 2 && seed.back().t == VT::Str &&
                               seed[seed.size() - 2].t == VT::Str &&
                               seed[seed.size() - 2].s == seed.back().s;
        // exact geometric ratio: Rat/Int seeds continue in Rat space (Rakudo keeps
        // 1, 1/2, 1/4 ... 0 as Rats; doubles would leak an e-suffix via .raku)
        // exact arithmetic step: Rat/Int seeds continue in Rat space, so
        // `⅓, ⅔ … 30` reaches an exact 15 instead of drifting into doubles
        Value stepV; bool exactStep = false;
        if (!hasGen && !geometric && seed.size() >= 2) {
            bool seedsExact = true;
            for (auto& sv : seed) if (sv.t != VT::Int && sv.t != VT::Bool && sv.t != VT::Rat) seedsExact = false;
            if (seedsExact) {
                // the LAST pair — the deduction window — not the first: with a
                // constant-prefix seed like 1,1,1,2,3 the first pair's 0 is junk
                stepV = applyArith("-", seed[seed.size() - 1], seed[seed.size() - 2]);
                // exact whenever the walk cannot live in a double: a Rat step, or
                // Int seeds/step at or past 2**53 (between 2**53 and the 4.6e18
                // switch below the double add lost the step — `10**17, 10**17+1
                // ... *` repeated its seed forever; the geometric arm was exact)
                auto bigI = [](const Value& v) { return v.t == VT::Int && (v.big() || v.i >= (1LL << 53) || v.i <= -(1LL << 53)); };
                bool anyBig = bigI(stepV);
                for (auto& sv : seed) if (bigI(sv)) anyBig = true;
                exactStep = ((stepV.t == VT::Rat) && !allInt) || (allInt && anyBig);
            }
        }
        // A lone RAT seed steps by .succ/.pred, which keep it a Rat: `1.0 ... 3`
        // is (1.0, 2.0, 3.0), where the double walk made it (1.0, 2e0, 3e0)
        if (!hasGen && !geometric && seed.size() == 1 && seed[0].t == VT::Rat && !exactStep) {
            stepV = Value::integer(step < 0 ? -1 : 1);
            exactStep = true;
        }
        // …and a Rat STEP makes the seed it was deduced up to a Rat too: an
        // element is the one before it plus the step, so `0.1, 2 ... 3` is
        // (0.1, 2.0) (both 6.c/MISC/bug-coverage.t)
        if (exactStep && stepV.t == VT::Rat && seed.size() >= 2 && seed.back().t == VT::Int &&
            out.arr() && out.arr()->size() == seed.size()) {
            seed.back() = applyArith("+", seed[seed.size() - 2], stepV);
            out.arr()->back() = seed.back();
        }
        Value ratioV; bool exactRatio = false;
        if (geometric) {
            bool seedsExact = true;
            for (auto& sv : seed) if (sv.t != VT::Int && sv.t != VT::Bool && sv.t != VT::Rat) seedsExact = false;
            if (seedsExact && seed.size() >= 2) {
                const Value& gp = seed[seed.size() - 2]; // the deduction window's pair,
                const Value& gl = seed[seed.size() - 1]; // matching the last-three rule
                ratioV = applyArith("/", gl, gp);
                // an integral ratio must STAY Int: Int/Int yields a Rat here,
                // and Int × Rat(2/1) walks the whole sequence into integral
                // Rats (8.0 in .raku, Rat in .WHAT — Rakudo's 1,2,4 ... * are
                // plain Ints). div is exact and bigint-safe.
                if (ratioV.t == VT::Rat && gl.t != VT::Rat && gp.t != VT::Rat &&
                    !applyArith("%", gl, gp).truthy())
                    ratioV = applyArith("div", gl, gp);
                exactRatio = (ratioV.t == VT::Rat || ratioV.t == VT::Int);
                // a FRACTIONAL ratio re-derives the window's later seeds from its
                // first: `81, 27, 9 ... 1` is (81, 27.0, 9.0, 3.0, 1.0) in Rakudo
                if (ratioV.t == VT::Rat && out.arr() && out.arr()->size() >= dedWin) {
                    auto& oa = *out.arr();
                    const size_t n = oa.size();
                    for (size_t k = n - dedWin + 1; k < n; k++)
                        if (oa[k].t == VT::Int && oa[k - 1].isNumeric()) {
                            Value nv = applyArith("*", oa[k - 1], ratioV);
                            if (nv.toNum() == oa[k].toNum()) oa[k] = nv;
                        }
                }
            }
        }
        bool ascending = hasGen ? true : (geometric ? ratio >= 1 : step >= 0);
        // A NEGATIVE geometric ratio alternates sign, so the endpoint is compared by
        // MAGNITUDE — Rakudo's `1, -2, 4 ... 10` is (1 -2 4 -8) and `... 3` is (1 -2).
        bool seqMagnitude = geometric && ratio < 0;
        // seeds already climbing can never come down to `-Inf`: Rakudo's
        // `1, 3 ... -Inf` is empty, as `5, 3 ... 10` is
        if (negInf && !hasGen && !deduceFailed && seed.size() >= 2 && numericWindow &&
            !seqMagnitude && (geometric ? ratio > 1 : step > 0))
            { out.arr()->clear(); return out; }
        // A DEDUCED sequence has a travel direction, so it can tell when a value has
        // gone past the endpoint. A generator closure has none (Rakudo runs `5, 4,
        // { $_ - 1 } ... 10` forever), and neither does a constant step/ratio.
        bool seqHaveDir = !hasGen && !infinite && !endCode &&
                          (geometric ? (ratio != 1 && ratio != 0) : step != 0);
        // …but only a CROSSING counts: the previous element inside the
        // endpoint's magnitude and this one beyond it. A wrong-signed element
        // AT the magnitude (`1, -2, 4 ... 2`) means it can never match, and the
        // sequence runs forever, as Rakudo's does.
        auto seqPassedEnd = [&](double v, bool hasPrev = false, double prev = 0) {
            if (seqMagnitude) return hasPrev && std::fabs(v) > std::fabs(endVal) && std::fabs(prev) < std::fabs(endVal);
            return ascending ? v > endVal : v < endVal;
        };
        // How many trailing elements the generator consumes: a `* + *` WhateverCode
        // by its whateverArity, `{ $^a + $^b }` by its placeholder count (the
        // canonical `1, 1, { $^a + $^b } … *` fibonacci), `-> $a, $b {…}` by its
        // signature — and a topic block `{ $_ … }` or a bare block by 1 (an EMPTY
        // params vector must not read as arity 0, which fed the block nothing).
        // arity 0 = SLURPY: the generator takes EVERY element produced so far
        // (`sub { [*] @_[*-1], @_ + 1 }` — a body using @_, or a declared *@slurpy).
        long long arity = 1;
        if (hasGen && gen.code()) {
            bool slurpy = gen.code()->usesArgs;
            if (gen.code()->params)
                for (auto& p : *gen.code()->params) if (p.slurpy) { slurpy = true; break; }
            // an ANONYMOUS `sub { … @_ … }` is built without usesArgs being computed,
            // so look for @_/%_ in the body here too
            if (!slurpy && gen.code()->body && (!gen.code()->params || gen.code()->params->empty())) {
                std::set<std::string> ph2;
                for (auto& s2 : *gen.code()->body) collectPHStmt(s2.get(), ph2);
                slurpy = ph2.count("@_") || ph2.count("%_");
            }
            if (slurpy)                                 arity = 0;
            else if (gen.code()->whateverArity > 0)       arity = gen.code()->whateverArity;
            else if (!gen.code()->placeholders.empty())   arity = (long long)gen.code()->placeholderPos();
            else if (gen.code()->params && !gen.code()->params->empty()) arity = (long long)gen.code()->params->size();
        }
        // …and how many of those it cannot do without: a generator handed fewer
        // elements than that dies as its binder would (Rakudo: `^1, *+* … *` is
        // "Too few positionals passed; expected 2 arguments but got 1") rather
        // than being fed made-up zeros
        long long needArity = arity;
        if (hasGen && gen.code() && arity > 0 && gen.code()->whateverArity <= 0 &&
            gen.code()->placeholders.empty() && gen.code()->params) {
            needArity = 0;
            for (auto& p : *gen.code()->params)
                if (!p.named && !p.slurpy && !p.optional && !p.defaultVal) needArity++;
        }
        auto tooFew = [needArity](size_t n) {
            if (needArity > 0 && (long long)n < needArity)
                throw RakuError{Value::typeObj("X::TypeCheck::Argument"),
                    "Too few positionals passed; expected " + std::to_string(needArity) +
                    " arguments but got " + std::to_string(n)};
        };
        // An infinite sequence (`… … *`) is LAZY — and so is a GENERATOR
        // sequence with a literal endpoint (`1, {-$_} ... 3`): it stops only on
        // an EXACT endpoint match, which may never come (Rakudo semantics),
        // so it must not materialise eagerly.
        if (infinite || (hasGen && !endCode)) {
            // Does an element reach the endpoint? Two numbers compare by value;
            // anything else by its string form. The test used to be toNum()-only,
            // so a STRING endpoint compared 0 against 0 while `.isNumeric()`
            // stayed false — it never matched, and `"a", { $_ ~ "x" } ... "axxx"`
            // ran to the million-element runaway cap instead of stopping at four.
            Value endValue = r;
            auto atEnd = [](const Value& v, const Value& e) {
                // exact for exact types: two big Ints five apart compare EQUAL as doubles
                if ((v.t == VT::Int || v.t == VT::Rat) && (e.t == VT::Int || e.t == VT::Rat)) return applyArith("==", v, e).truthy();
                if (v.isNumeric() && e.isNumeric()) return v.toNum() == e.toNum();
                return v.toStr() == e.toStr();
            };
            // seed already at the endpoint: the sequence is just the seed
            if (!infinite && !seed.empty() && atEnd(seed.back(), endValue)) {
                if (exclusive) out.arr()->pop_back();
                return out;
            }
            auto st = std::make_shared<LazySeqState>();
            Interpreter* self = this;
            bool boundedGen = !infinite;
            st->infinite = !boundedGen; // a literal-endpoint gen seq CAN drain (stops on match)
            st->seqUserGen = infinite && hasGen;
            st->appendNext = [self, gen, hasGen, geometric, ratio, step, allInt, arity, tooFew,
                              deduceFailed, dedFrom, seqDeduceThrow,
                              succSeed, succDesc, succConst, ratioV, exactRatio, stepV, exactStep,
                              boundedGen, endVal, endValue, atEnd, exclusive](ValueList& cache) -> bool {
                if (cache.empty() && !hasGen) return false;
                if (boundedGen && !cache.empty() && atEnd(cache.back(), endValue))
                    return false; // endpoint reached
                if (boundedGen && cache.size() >= 1000000) return false; // runaway cap (endpoint never matched)
                double lastV = cache.empty() ? 0 : cache.back().toNum();
                Value next;
                if (hasGen) {
                    ValueList args; size_t n = cache.size();
                    if (arity == 0) { for (size_t q = 0; q < n; q++) args.push_back(cache[q]); } // slurpy: all so far
                    // (fewer elements than the generator takes are passed as they are,
                    // so its binder says "Too few positionals" — arity-2-or-more.t)
                    else { tooFew(n); for (long long k = arity; k >= 1; k--) { long long idx = (long long)n - k; if (idx >= 0) args.push_back(cache[idx]); } }
                    // `last` inside the generator terminates the sequence
                    try { next = self->callCallable(gen, args); }
                    catch (const LastEx&) { return false; }
                    if (boundedGen && exclusive && atEnd(next, endValue))
                        return false; // ...^ drops the endpoint element
                } else if (succSeed) { // 'a' ... * : step by succ/pred
                    const Value& lastE = cache.back();
                    if (succConst) next = lastE;
                    else if (lastE.t == VT::Str) {
                        bool ok = true;
                        std::string s = succDesc ? strPred(lastE.s, ok) : strSucc(lastE.s);
                        if (!ok) return false;
                        next = Value::str(s);
                    } else {
                        ValueList none;
                        next = self->methodCall(lastE, succDesc ? "pred" : "succ", none);
                    }
                } else if (deduceFailed) {
                    seqDeduceThrow(dedFrom); // generating BEYOND the seeds needs the step we never had
                } else if (geometric) {
                    const Value& lastE = cache.back();
                    // exact multiply FIRST: the llround-through-a-double fast
                    // path saturated at int64 max, so 1, 2, 4 ... * flatlined
                    // at 9223372036854775807 from 2^63 on. applyArith promotes
                    // Ints to bigints and keeps Rat ratios exact.
                    if (exactRatio && (lastE.t == VT::Int || lastE.t == VT::Rat || lastE.t == VT::Bool))
                        next = applyArith("*", lastE, ratioV);
                    else if (allInt && ratio == std::floor(ratio)) next = Value::integer((long long)std::llround(lastV * ratio));
                    else next = Value::number(lastV * ratio);
                } else if (exactStep && (cache.back().t == VT::Int || cache.back().t == VT::Rat ||
                                         cache.back().t == VT::Bool)) {
                    next = applyArith("+", cache.back(), stepV); // stays Rat
                } else {
                    double nv = lastV + step;
                    // an int walk that reaches |2^62| leaves the double-safe
                    // zone: continue with exact addition (bigint promotion)
                    if (allInt && stepV.t == VT::Int && std::abs(nv) > 4.6e18)
                        next = applyArith("+", cache.back(), stepV);
                    else next = allInt ? Value::integer((long long)nv) : Value::number(nv);
                }
                // a SLIPPING generator contributes every element of the slip —
                // see the eager twin below for why that is what interleaves
                // two sequences under one `...`
                if (hasGen && next.t == VT::Array && next.arr() && next.s == "Slip") {
                    if (next.arr()->empty()) return false; // nothing to advance with
                    for (auto& nx : *next.arr()) cache.push_back(nx);
                    return true;
                }
                cache.push_back(next);
                return true;
            };
            out.extM() = st;
            return out;
        }
        // Code endpoint: the first accepted element ends the sequence — check the
        // seeds themselves first (`1, 2, 4 ... * > 3` is (1 2 4))
        // a SLURPY endpoint block (`{ @_ eq "1 2 3" }`) sees every element so far
        bool endSlurpy = false;
        if (endCode && r.code() && r.code()->body && (!r.code()->params || r.code()->params->empty()) &&
            r.code()->placeholders.empty()) {
            std::set<std::string> ph2;
            for (auto& s2 : *r.code()->body) collectPHStmt(s2.get(), ph2);
            endSlurpy = ph2.count("@_") > 0;
        }
        long long endArity = 1;
        if (endCode && !endSlurpy && r.code()) {
            if (r.code()->whateverArity > 1) endArity = r.code()->whateverArity;
            else if (r.code()->placeholderPos() > 1) endArity = (long long)r.code()->placeholderPos();
            else if (r.code()->params && r.code()->params->size() > 1) endArity = (long long)r.code()->params->size();
        }
        auto endAccepts = [&](const Value& v, size_t nBefore) -> bool {
            if (endArity > 1) {   // the last N elements, this one included
                size_t have = std::min(nBefore, out.arr()->size());
                if ((long long)have + 1 < endArity) return false;
                ValueList a(out.arr()->begin() + (have + 1 - (size_t)endArity), out.arr()->begin() + have);
                a.push_back(v);
                return predAnswerTruthy(*this, callCallable(r, a), v);
            }
            if (endSlurpy) {
                ValueList a(out.arr()->begin(), out.arr()->begin() + std::min(nBefore, out.arr()->size()));
                a.push_back(v);
                return predAnswerTruthy(*this, callCallable(r, a), v);
            }
            ValueList a{v}; return predAnswerTruthy(*this, callCallable(r, a), v);
        };
        if (endCode)
            for (size_t k = 0; k < out.arr()->size(); k++)
                if (endAccepts((*out.arr())[k], k)) {
                    out.arr()->resize(exclusive ? k : k + 1);
                    return out;
                }
        // The endpoint tests every element the sequence EMITS, seeds included:
        // `3, 5 ... 2` is empty and `0, 4 ... 2` is (0,), not (0, 4). Pushing the
        // seeds unconditionally made Math::NumberTheory's frobenius-solve —
        // `for $first, $first + $step ... $limit` — search branches past its limit
        // and report solutions with negative coefficients.
        if (seqHaveDir) {
            auto& es = *out.arr();
            for (size_t k = 0; k < es.size(); k++) {
                double v = es[k].toNum();
                if (seqPassedEnd(v, k > 0, k > 0 ? es[k - 1].toNum() : 0)) { es.resize(k); return out; }
                if (v == endVal) { es.resize(exclusive ? k : k + 1); return out; }
            }
        }
        // no direction (a constant `1, 1 ... 1`): a seed AT the endpoint ends it
        if (!seqHaveDir && !hasGen && !infinite && !endCode && !succSeed) {
            auto& es = *out.arr();
            for (size_t k = 0; k < es.size(); k++)
                if (es[k].isNumeric() && r.isNumeric() && applyArith("==", es[k], r).truthy()) {
                    es.resize(exclusive ? k : k + 1); return out;
                }
        }
        const size_t CAP = 1000000;
        bool dirKnown = !hasGen; // for a closure gen the travel direction is learned
        while (out.arr()->size() < CAP) {
            double lastV = out.arr()->empty() ? 0 : out.arr()->back().toNum(); // empty seed: gen makes elem 1
            // Non-gen numeric sequences can pre-check the endpoint; a closure gen
            // must generate first (its direction — and thus overshoot test — is
            // only known once we see the next value).
            if (!hasGen && !infinite && !endCode) {
                // exact types compare exactly (two Ints five apart at 10**17 are EQUAL as doubles)
                const Value* lastE = out.arr()->empty() ? nullptr : &out.arr()->back();
                const bool exactHead = !seqMagnitude && lastE && (lastE->t == VT::Int || lastE->t == VT::Rat) && (r.t == VT::Int || r.t == VT::Rat);
                // an alternating geometric sequence already at or beyond the
                // endpoint's magnitude without having matched it never will:
                // it runs forever (lazily)
                if (seqMagnitude && std::fabs(lastV) >= std::fabs(endVal)) {
                    Value star; star.t = VT::Whatever;
                    return seqOp(l, star, exclusive);
                }
                if (!seqMagnitude &&
                    (exactHead ? applyArith(ascending ? ">=" : "<=", *lastE, r).truthy()
                               : (ascending ? lastV >= endVal : lastV <= endVal))) break; // reached endpoint
            }
            Value next;
            if (hasGen) {
                ValueList args; size_t n = out.arr()->size();
                if (arity == 0) { for (size_t q = 0; q < n; q++) args.push_back((*out.arr())[q]); } // slurpy: all so far
                else { tooFew(n); for (long long k = arity; k >= 1; k--) { long long idx = (long long)n - k; if (idx >= 0) args.push_back((*out.arr())[idx]); } }
                // `last` inside the generator terminates the sequence
                try { next = callCallable(gen, args); }
                catch (const LastEx&) { break; }
                if (!dirKnown && !infinite && !endCode && next.isNumeric()) {
                    ascending = next.toNum() >= lastV; dirKnown = true;
                }
            } else if (succSeed) { // H.new ... *.y > 10 : step by succ/pred
                const Value& lastE = out.arr()->back();
                if (succConst) next = lastE;
                else if (lastE.t == VT::Str) {
                    bool ok = true;
                    std::string s = succDesc ? strPred(lastE.s, ok) : strSucc(lastE.s);
                    if (!ok) break;
                    next = Value::str(s);
                } else {
                    ValueList none;
                    next = methodCall(lastE, succDesc ? "pred" : "succ", none);
                }
            } else if (deduceFailed) {
                seqDeduceThrow(dedFrom); // beyond the seeds without a deducible step
            } else if (geometric) {
                const Value& lastE = out.arr()->back();
                if (exactRatio && (lastE.t == VT::Int || lastE.t == VT::Rat || lastE.t == VT::Bool))
                    next = applyArith("*", lastE, ratioV); // exact first — see the lazy twin
                else if (allInt && ratio == std::floor(ratio)) next = Value::integer((long long)std::llround(lastV * ratio));
                else next = Value::number(lastV * ratio);
            } else if (exactStep && (out.arr()->back().t == VT::Int || out.arr()->back().t == VT::Rat ||
                                     out.arr()->back().t == VT::Bool)) {
                next = applyArith("+", out.arr()->back(), stepV); // stays Rat
            } else {
                double nv = lastV + step;
                if (allInt && stepV.t == VT::Int && std::abs(nv) > 4.6e18)
                    next = applyArith("+", out.arr()->back(), stepV); // exact past the double-safe zone
                else next = allInt ? Value::integer((long long)nv) : Value::number(nv);
            }
            // A generator that SLIPS produces SEVERAL elements at once, each a
            // separate element of the sequence — which is how one `...` interleaves
            // two: `1, 1, { slip $^a + 1, $^b * 2 } ... *` is 1 1 2 2 3 4 4 8, and
            // the next call reads the last `arity` of whatever came out. Pushed
            // whole, the Slip became one element and the two strands collapsed.
            ValueList batch;
            if (hasGen && next.t == VT::Array && next.arr() && next.s == "Slip") batch = *next.arr();
            else batch.push_back(std::move(next));
            bool stop = false;
            for (auto& nx : batch) {
                if (endCode) {
                    if (endAccepts(nx, out.arr()->size())) { if (!exclusive) out.arr()->push_back(nx); stop = true; break; }
                    out.arr()->push_back(nx);
                    continue;
                }
                // exact types compare exactly: two Ints five apart at 10**17 are EQUAL as
                // doubles, so the double tests ended the walk after its first exact step
                const bool exactEnd = !seqMagnitude && (nx.t == VT::Int || nx.t == VT::Rat) && (r.t == VT::Int || r.t == VT::Rat);
                if (!infinite) {
                    if (exactEnd) { if (applyArith(ascending ? ">" : "<", nx, r).truthy()) { stop = true; break; } } // would overshoot
                    else { double nv = nx.toNum();
                           if (seqPassedEnd(nv, !out.arr()->empty(), out.arr()->empty() ? 0 : out.arr()->back().toNum())) { stop = true; break; } }
                }
                out.arr()->push_back(nx);
                if (!infinite && (exactEnd ? applyArith("==", nx, r).truthy() : nx.toNum() == endVal)) { stop = true; break; } // hit endpoint exactly
            }
            if (stop) break;
        }
        if (exclusive && !infinite && !endCode && !out.arr()->empty()) {
            const Value& lastE = out.arr()->back();
            const bool exactLast = (lastE.t == VT::Int || lastE.t == VT::Rat) && (r.t == VT::Int || r.t == VT::Rat);
            if (exactLast ? applyArith("==", lastE, r).truthy() : lastE.toNum() == endVal) out.arr()->pop_back();
        }
        return out;
    }
    return Value::array();
}
// `^...` drops the seed. An eager sequence simply loses its first element, but
// a LAZY one's array is the generator's own cache — the value it steps from —
// so erasing from it left `3 ^... *` nothing to step from (an empty Seq) and
// `1, *+1 ^... *` a generator called with no arguments. Read it through a view
// that skips the seed instead.
Value Interpreter::seqDropSeed(Value seq) {
    if (seq.t != VT::Array || !seq.arr()) return seq;
    if (!seq.ext()) {
        if (!seq.arr()->empty()) seq.arr()->erase(seq.arr()->begin());
        return seq;
    }
    auto inner = std::make_shared<Value>(seq);
    auto innerSt = std::static_pointer_cast<LazySeqState>(seq.ext());
    auto pos = std::make_shared<size_t>(1);
    auto st = std::make_shared<LazySeqState>();
    st->infinite = innerSt->infinite;
    st->seqUserGen = innerSt->seqUserGen;
    st->appendNext = [inner, innerSt, pos](ValueList& cache) -> bool {
        while (*pos >= inner->arr()->size())
            if (!innerSt->appendNext(*inner->arr())) return false;
        cache.push_back((*inner->arr())[(*pos)++]);
        return true;
    };
    Value out = Value::array(); out.isList = true; out.s = seq.s;
    out.extM() = st;
    return out;
}
// `'aa' .. 'bb'` — a MULTI-character string range, as an eager list.
//
// Rakudo does not climb by succ here at all: it delegates to the `...` sequence
// machinery, which cross-products the character positions, so ('ab'..'ba') is
// ("ab","aa","bb","ba") and not the 26-element succ chain we produce. That is a
// separate (large) piece of work; what this function owns is the part that is
// not about ORDER — when the range is EMPTY, and never running away.
//
//   * the emptiness guard is a plain whole-string compare of the RAW endpoints,
//     `min gt max`. It used to be length-first, so ("Y".."AB") yielded Y Z AA AB
//     (Rakudo: nothing) and, far worse, ('fig'..'banana') — 3 chars climbing
//     toward a 6-char endpoint it can never reach — ran to the 1,000,000 cap and
//     peaked at 952 MB of resident memory before answering.
//   * the length tests count CODEPOINTS, not bytes; on the byte count every
//     multi-byte character looked "longer" than its ASCII endpoint.
//   * strSucc leaves a non-magical string unchanged ('a!'.succ is 'a!'), which
//     made the loop re-push the same value until the cap. Rakudo genuinely hangs
//     on this input; we stop instead — a compiler that allocates a gigabyte, or
//     spins, is worse than one that gives up on a value succ cannot advance.
Value strRangeList(const std::string& min, const std::string& to, bool exFrom, bool exTo) {
    Value arr = Value::array(); arr.isList = true; // a string range is list-like (flattens)
    if (min > to) return arr;                      // the guard, on the RAW endpoints
    // `^..` seeds from min.succ; the guard above has already run on the raw pair,
    // so ('az'^..'ba') is ("ba") and not empty.
    const std::string from = exFrom ? strSucc(min) : min;
    if (from == to) { if (!exTo) arr.arr()->push_back(Value::str(from)); return arr; }
    if (from > to) return arr;
    const long long endLen = u8CpLen(to);
    // The SEED is emitted whatever its length; it is each SUCCESSOR that is tested,
    // against BOTH the endpoint and the endpoint's length. ('aa'..'b') is therefore
    // ("aa") — the seed, then 'ab' is too long — and ('a'..'aa') is ("a"), because
    // 'b' already sorts after 'aa'. Testing `cur` instead of `cur.succ` made the
    // first empty and the second the whole alphabet.
    arr.arr()->push_back(Value::str(from));
    std::string cur = from;
    for (int g = 0; g < 1000000; g++) {
        std::string nxt = strSucc(cur);
        if (nxt == cur) break;                     // succ cannot advance this value
        if (nxt > to || u8CpLen(nxt) > endLen) break;
        if (nxt == to) { if (!exTo) arr.arr()->push_back(Value::str(nxt)); break; }
        arr.arr()->push_back(Value::str(nxt));
        cur = std::move(nxt);
    }
    return arr;
}
static ValueList strRangeElemsImpl(const Value& r) {
    const RangeEnds* re = rangeEnds(r);
    if (!re) return ValueList{};
    Value l = strRangeList(re->from.s.str(), re->to.s.str(), r.rExFrom(), r.rExTo());
    return l.arr() ? *l.arr() : ValueList{};
}
static const bool g_strRangeElemsInstalled = ((g_strRangeElems = &strRangeElemsImpl), true);

// was a Range interpreted and a List under --exe.
// Value.cpp steps a bignum-ended Range through this (see g_applyArith).
static Value applyArithHook(const std::string& op, const Value& l, const Value& r) {
    return applyArith(op, l, r);
}
static const bool g_applyArithInstalled = ((g_applyArith = &applyArithHook), true);

Value rtRangeVal(const Value& from, const Value& to, bool exFrom, bool exTo) {
    if (from.t == VT::Str && to.t == VT::Str) {
        if (u8CpLen(from.s) == 1 && u8CpLen(to.s) == 1) {
            Value rr = Value::range(u8FirstCp(from.s), u8FirstCp(to.s), exFrom, exTo);
            rr.ofTypeM() = "Str";
            return rr;
        }
        return strRangeVal(from, to, exFrom, exTo);
    }
    // `1..*` / `*..5`: a Whatever endpoint is unbounded, the LLONG extreme
    // marking an infinite range exactly as the NK::RangeLit arm does. Without
    // this, `*` fell through to `.toInt()` — which is 0 — so under --exe every
    // Whatever range was built INSIDE OUT: `1..*` was the empty `1..0`, and so
    // `for 1..* {…}` ran zero times, `(1..*)[3]` was Nil, `5 ~~ 1..*` was False,
    // `.head(4)` was (), and `*..5` was `0..5`. All silent wrong answers.
    checkRangeEndpoint(from); checkRangeEndpoint(to);
    // (the `^` markers survive: `1..^*` still excludes its endpoint)
    if (from.t == VT::Whatever && to.t == VT::Whatever)
        return Value::range(-9223372036854775807LL - 1, 9223372036854775807LL, exFrom, exTo);
    // …and a STRING endpoint opposite the Whatever stays a string: `'c'..*` is
    // bounded below by "c", so "b" is not in it. Numified, every such range
    // started at 0 and held every string there is.
    if (to.t == VT::Whatever) {
        Value r = Value::range(from.t == VT::Str ? (long long)u8FirstCp(from.s) : from.toInt(),
                               9223372036854775807LL, exFrom, exTo);
        if (from.t == VT::Str) attachRangeEnds(r, from, Value::number(INFINITY));
        else if (endlessFracStart(from)) {                   // `1.5..*` steps 1.5, 2.5, …
            r.rNumM() = true; r.n = from.toNum(); r.imM() = INFINITY;
            attachRangeEnds(r, from, Value::number(INFINITY));
        }
        return r;
    }
    if (from.t == VT::Whatever) {
        Value r = Value::range(-9223372036854775807LL - 1,
                               to.t == VT::Str ? (long long)u8FirstCp(to.s) : to.toInt(), exFrom, exTo);
        if (to.t == VT::Str) attachRangeEnds(r, Value::number(-INFINITY), to);
        return r;
    }
    // A FRACTIONAL range keeps its real endpoints, exactly as the interpreter's
    // own `..` does: `to.toInt()` alone made `0 ..^ 2.5` the integer range
    // `0..^2`, so under --exe every fractional range was silently truncated at
    // construction — `.min`, `.max`, `.rand`, `.raku` and `2.4 ~~ 0..^2.5` all
    // answered for the wrong range. Endpoints are carried too, so a Rat stays a
    // Rat. (Kept in step with the NK::RangeLit arm in eval.)
    {
        // A Num or Rat endpoint makes the ELEMENTS Nums or Rats, whole or not:
        // `(1e0..3e0).list` is (1e0, 2e0, 3e0) and `(1.0..3.0).list` is Rats.
        // Only whole-number endpoints used to take this path, so an inexact
        // range whose ends happened to be round came back as plain Ints.
        bool fFrac = (from.t == VT::Num || from.t == VT::Rat);
        bool tFrac = (to.t == VT::Num || to.t == VT::Rat);
        if ((fFrac || tFrac) && from.isNumeric() && to.isNumeric() &&
            std::isfinite(from.toNum()) && std::isfinite(to.toNum())) {
            Value rr = Value::range((long long)std::floor(from.toNum()),
                                    (long long)std::floor(to.toNum()), exFrom, exTo);
            rr.rNumM() = true; rr.n = from.toNum(); rr.imM() = to.toNum();
            setRangeEnds(rr, from, to);
            return rr;
        }
    }
    // A MIXED range. Which side is the string decides everything (RG-03): a
    // Real on the LEFT coerces the right, so `1 .. "5"` is the integer range
    // 1..5 — but a Str on the left coerces NOTHING, so `"1"..9` is a range of
    // the STRINGS "1" through "9" and `"a"..5` is empty, because "a" is after
    // "5" in string order. We used to numify the string in both directions, so
    // `my @a = "1"..9` came back as integers (S02-types/range.t, "did we get
    // strings"). The endpoints are carried either way, so `.min` is still the
    // Str and `.raku` still prints `"5"..9`.
    if ((from.t == VT::Str) != (to.t == VT::Str) &&
        (from.t == VT::Str || from.isNumeric()) && (to.t == VT::Str || to.isNumeric())) {
        if (from.t == VT::Str) return strLeftRange(from, to, exFrom, exTo);
        Value num = numifyStrOrThrow(to.s.str());   // `1.."b"` is X::Str::Numeric
        Value r = Value::range(from.toInt(), num.toInt(), exFrom, exTo);
        setRangeEnds(r, from, num);
        return r;
    }
    {
        Value r = Value::range(from.toInt(), rangeTopInt(to), exFrom, exTo);
        if (to.t == VT::Int && to.big()) r.bigM() = to.big(); // keep the big bound (pick/roll sample it)
        setRangeEnds(r, from, to);
        return r;
    }
}

// `@a[$i .. *]` for native codegen: the tail slice from index `from` to the end.
Value rtSliceFrom(const Value& base, long long from, bool exFrom) {
    Value out = Value::array(); out.isList = true;
    if (from < 0) return out;
    if (base.t == VT::Array && base.arr()) {
        long long n = (long long)base.arr()->size();
        for (long long i = from + (exFrom ? 1 : 0); i < n; i++) out.arr()->push_back((*base.arr())[i]);
    }
    else if (base.t == VT::Str) { // "abc"[1..*] is rare but harmless to support: chars
        std::string ss = base.s;
        if (from + (exFrom ? 1 : 0) < (long long)ss.size()) return Value::str(ss.substr(from + (exFrom ? 1 : 0)));
    }
    return out;
}

// >>.method for native codegen: apply to each top-level element (structure-
// preserving, no deep flatten) — mirrors the interpreter's hyper method call.
Value rtHyperMethod(Interpreter& I, const Value& inv, const std::string& m, ValueList args) {
    // `».(args)` keeps the interpreter's shape and descent rules
    if (opEq(m, kHyperInvoke)) return I.hyperMethodEach(inv, m, args, false);
    Value out = Value::array();
    if (inv.t == VT::Array && inv.arr())
        for (auto& el : *inv.arr()) out.arr()->push_back(I.methodCall(el, m, args));
    else
        for (auto& el : inv.flatten()) out.arr()->push_back(I.methodCall(el, m, args));
    out.isList = true;
    return out;
}

// An INDIRECT method call for native codegen — `$obj."$name"()`, `$obj.$code`,
// `$obj.&sub`, `self!"$name"()`, `@a».&f`. `mv` is the already-evaluated name
// expression, `prefix` the dispatch-key prefix of the call form ("!" private,
// "^" metamodel, "" ordinary). Mirrors the methodExpr path of the interpreter's
// MethodCall eval, minus its Whatever currying: codegen curries statically
// (exArg wraps a `*`-bearing expression in a closure before this is reached).
Value rtIndirectMethod(Interpreter& I, const Value& inv, const Value& mv, ValueList args,
                       const char* prefix, bool hyper) {
    // A Code — or a TYPE OBJECT, which Rakudo invokes as a coercion, so
    // `$value.$ct` with `my $ct = Rat` is Rat($value) — is CALLED with the
    // invocant as its first argument, rather than named as a method.
    if (mv.t == VT::Code || mv.t == VT::Type) {
        auto one = [&](const Value& self) {
            ValueList ca;
            ca.reserve(args.size() + 1);
            ca.push_back(self);
            for (auto& a : args) ca.push_back(a);
            return I.callCallable(mv, std::move(ca));
        };
        if (!hyper) return one(inv);
        if (inv.t == VT::Hash && inv.hash() && inv.hashKind.empty()) { // %h».&f keeps the keys
            Value hout = Value::makeHash();
            for (auto& kv : *inv.hash()) (*hout.hash())[kv.first] = one(kv.second);
            return hout;
        }
        Value out = Value::array(); out.isList = true;
        if (inv.t == VT::Array && inv.arr())
            for (auto& el : *inv.arr()) out.arr()->push_back(one(el));
        else
            for (auto& el : inv.flatten()) out.arr()->push_back(one(el));
        return out;
    }
    std::string name = prefix + mv.toStr();
    if (hyper) return rtHyperMethod(I, inv, name, std::move(args));
    return I.methodCall(inv, name, std::move(args));
}

// |x for native codegen. In argument position (argPos): arrays/ranges spread
// positionally and a hash spreads as named args — mirroring evalArgs. In a list
// literal a hash stays one item — mirroring the ListExpr eval.
void rtSpreadSlipArg(ValueList& args, const Value& v);   // InterpreterCore.cpp — evalArgs' own `|`
void rtSpreadArg(ValueList& as, const Value& v, bool argPos) {
    // an argument list spreads ONE level, by the interpreter's own rules: `f(|@aoa)`
    // passes each inner array whole (flatten() took them apart as well)
    if (argPos) { rtSpreadSlipArg(as, v); return; }
    if (v.t == VT::Array || v.t == VT::Range) { for (auto& x : v.flatten()) as.push_back(x); return; }
    if (argPos && v.t == VT::Hash && v.hash()) {
        for (auto& kv : *v.hash()) { Value p = Value::pair(kv.first, kv.second); p.namedArg = true; as.push_back(std::move(p)); }
        return;
    }
    as.push_back(v);
}
// |x as a list-literal element: a pre-spread List that listToArray will splice.
Value rtSlipVal(const Value& v) {
    Value out = Value::array(); out.isList = true; out.s = "Slip"; // splices via listToArray
    rtSpreadArg(*out.arr(), v, false);
    return out;
}

// One replication for `xx`: a Slip contributes its ELEMENTS, everything else is
// one element — `|(1,2) xx 2` is (1 2 1 2), not ((1 2) (1 2)) (issue #30).
// Deliberately uniform: Rakudo's literal `Empty xx 3` keeps three Empty elements
// only because its constant-folded LHS skips the thunk — the SAME Slip through a
// variable, a call, or `Slip.new` splices there too. Roast asserts neither.
void rtXxAppend(ValueList& out, Value one) {
    if (one.t == VT::Array && one.arr() && one.isList && one.s == "Slip")
        for (auto& e : *one.arr()) out.push_back(e);
    else {
        // a Seq replication is CACHED: the copies are plain lists, not Seqs
        // that would each be consumable on their own (sheet LA-32)
        if (one.t == VT::Array && one.isList && one.s == "Seq") one.s.clear();
        out.push_back(std::move(one));
    }
}

bool nameTermConstant(const std::string& n, Value& out, bool sixE) {
    if (n == "pi" || n == "\xcf\x80") { out = Value::number(M_PI); return true; }
    if (n == "e")   { out = Value::number(M_E); return true; }
    if (n == "i")   { out = Value::complex(0, 1); return true; } // imaginary unit
    if (n == "tau" || n == "\xcf\x84") { out = Value::number(2 * M_PI); return true; }
    if (n == "\xE2\x88\x85") { // ∅ — the empty Set
        out = Value::makeHash(); out.hashKind = "Set"; return true;
    }
    if (n == "now") { // Instant: high-resolution seconds since the epoch
        // …on the `now` clock, which carries the Instant epoch offset that
        // `.to-posix` takes back off (see epochNowSecs). Handing out raw POSIX
        // put `now.to-posix` ten seconds in the past.
        out = Value::number(epochNowSecs());
        out.hashKind = "Instant";
        identify(out);
        return true;
    }
    if (n == "time") { out = Value::integer((long long)::time(nullptr)); return true; } // POSIX seconds (Int)
    // 6.e `nano`: the same clock as `time`, in nanoseconds, as an Int. Gated
    // like every other 6.e addition — a 6.d program has no such term.
    if (n == "nano" && sixE) {
        auto d = std::chrono::system_clock::now().time_since_epoch();
        out = Value::integer(std::chrono::duration_cast<std::chrono::nanoseconds>(d).count());
        return true;
    }
    if (n == "rand") { out = Value::number(randDouble()); return true; }                // random Num in [0, 1)
    return false;
}

Value Interpreter::rtNameTerm(const std::string& n) {
    if (tctx_.cur) {
        if (Value* p = tctx_.cur->find(n)) // bound slot: fetch, as the eval path does
            return (p->t == VT::Hash && p->hashKind == "Proxy" && p->hash()) ? deproxy(*p) : *p;
        if (Value* f = tctx_.cur->find("&" + n)) return callCallable(*f, {});
    }
    // after the env/&routine lookups, so a user's own `sub now {…}` still wins
    { Value c; if (nameTermConstant(n, c, sixE())) return c; }
    auto it = builtins_.find(n);
    if (it != builtins_.end() && builtinVisible(n)) { ValueList none; return it->second(*this, none); }
    // builtin enum members (the SAME list the NameTerm eval reads): without the
    // numeric payload a native `sort { $a < $b ?? Less !! More }` compares 0 vs 0
    { Value c; if (coreEnumValue(n, c)) return c; }
    return Value::typeObj(n);
}

// k => v at a call site (native codegen): a syntactic identifier-keyed pair is
// a NAMED argument — mirrors evalArgs.
Value rtNamedPair(const std::string& k, Value v) {
    Value p = Value::pair(k, std::move(v));
    p.namedArg = true;
    return p;
}
// The count of POSITIONAL args (named pairs excluded) — for multi-dispatch arity.
size_t rtPosCount(const ValueList& a, size_t from) {
    size_t n = 0;
    for (size_t i = from; i < a.size(); i++) if (!(a[i].t == VT::Pair && a[i].namedArg)) n++;
    return n;
}

// `use MODULE` for native codegen: mirror exec(UseStmt) — Test flag, language
// pragma, lib paths, real module loading into the runtime env.
void Interpreter::rtUse(const std::string& module, const std::string& arg, bool isNo,
                        const std::vector<std::string>& importArgs) {
    if (module == "Test") { usedTest_ = true; return; }
    // `use experimental :rakuast`, as the interpreter's UseStmt arm does it.
    // The parse-time flag on the Program governs hoisted routines; this one is
    // the running unit's own state, which an EVAL inside compiled code reads.
    if (module == "experimental" && !isNo) {
        for (auto& tag : importArgs)
            if (tag == "rakuast") {
                rakuAstPragma_ = true; anyRevSwitch_ = true; rakuAstMaterialize();
            }
        // …and then takes its ordinary road, as the interpreter arm does:
        // `experimental` has no file behind it and the load below knows that.
    }
    // `no strict` / `use strict`, as the interpreter's own UseStmt arm does it.
    // Compiled code resolves an auto-vivified NAME statically (codegen emits
    // laxVarRef for it), so this flag is here for what compiled code hands back
    // to the interpreter — and to keep `strict` from being looked for on disk.
    if (module == "strict") {
        if (tctx_.cur) tctx_.cur->strictPragma = isNo ? 1 : -1;
        return;
    }
    if (module == "fatal") {
        if (tctx_.cur) tctx_.cur->fatalPragma = isNo ? -1 : 1;
        return;
    }
    if (module.size() >= 2 && module[0] == 'v' && ascii::isdigit((unsigned char)module[1])) {
        langRev_ = langRevOfPragma(module);   // (bare `v6` is the default revision, 6.d)
        return;
    }
    if (module == "lib") {
        if (!arg.empty()) useLibPath(arg);
        return;
    }
    if (!module.empty()) loadModule(module);
}

// |@a in VALUE position (a return value, an assignment RHS) for native codegen:
// a one-level splice marker — the consumer (map, list assignment) splices the
// top-level elements; itemized inner arrays stay whole. Mirrors the interp,
// where the value-position slip returns the array and the consumer flattens.
Value& rtIndexRefW(Value& base, const Value& key) {
    Value idx = key;
    Value* b = base.deref();
    const long long n = b->t == VT::Array && b->arr() ? (long long)b->arr()->size() : 0;
    if (key.t == VT::Whatever) idx = Value::integer(n);
    else if (key.t == VT::Code && key.code() && key.code()->isWhateverCode)
        if (Interpreter* I = Interpreter::liveTarget()) idx = I->callCallable(key, ValueList{Value::integer(n)});
    return rtIndexRef(base, idx, false);
}

std::string rtInterpStr(const Value& v) {
    if (v.t == VT::Str && v.hashKind.empty() && v.enumName.empty()) return v.s;
    if ((v.t == VT::Int && !v.big()) && v.hashKind.empty() && v.enumName.empty()) return v.toStr();
    if (Interpreter* I = Interpreter::liveTarget()) return I->strOf(v);
    return v.toStr();
}

// `EXPR => value` for native codegen: the key keeps its own type, as the
// interpreter's Pair arm keeps it — `1 => 2`, an object, a type, a regex — and
// only a Str (or anything that is one) is stored as the plain string key.
Value rtPairKeyed(const Value& kv, Value value) {
    Value pr = Value::pair(kv.toStr(), std::move(value));
    if (kv.t == VT::Int || kv.t == VT::Num || kv.t == VT::Rat || kv.t == VT::Bool ||
        kv.t == VT::Array || kv.t == VT::Hash || kv.t == VT::Object || kv.t == VT::Pair ||
        kv.t == VT::Match || kv.t == VT::Code || kv.t == VT::Regex ||
        kv.t == VT::Type || kv.t == VT::Complex || kv.t == VT::Nil || kv.t == VT::Range)
        pr.pairKeyM() = std::make_shared<Value>(kv);
    return pr;
}

// `K => V` where a `*` was written on either side is a WhateverCode, not a
// Pair (`.map(* => True)`, `.map($ns ~ * => *)`): the curry the interpreter's
// Pair arm and native codegen both build. `kVar`/`vVar`: that side is a plain
// variable, whose Whatever is a VALUE and curries nothing. Answers Any when
// neither side curries; the caller then builds the Pair.
Value rtPairCurry(const Value& kv, const Value& vv, bool kVar, bool vVar) {
    auto curries = [](const Value& v) {
        return v.t == VT::Whatever || (v.t == VT::Code && v.code() && v.code()->isWhateverCode);
    };
    if (!((curries(kv) && !kVar) || (curries(vv) && !vVar))) return Value::any();
    Value kc = kv, vc = vv;
    Value code; code.t = VT::Code; code.setCode(makePayload<Callable>());
    code.code()->isWhateverCode = true;
    auto arityOf = [&](const Value& x) -> long long {
        if (x.t == VT::Whatever) return 1;
        if (x.t == VT::Code && x.code() && x.code()->isWhateverCode)
            return x.code()->whateverArity > 0 ? x.code()->whateverArity : 1;
        return 0;
    };
    code.code()->whateverArity = arityOf(kc) + arityOf(vc);
    code.code()->builtin = [kc, vc](Interpreter& I, ValueList& a) -> Value {
        size_t idx = 0;
        auto resolve = [&](const Value& x) -> Value {
            if (x.t == VT::Whatever) return idx < a.size() ? a[idx++] : Value::any();
            if (x.t == VT::Code && x.code() && x.code()->isWhateverCode) {
                long long ar = x.code()->whateverArity > 0 ? x.code()->whateverArity : 1;
                ValueList sub;
                for (long long k = 0; k < ar && idx < a.size(); k++) sub.push_back(a[idx++]);
                return I.callCallable(x, sub);
            }
            return x;
        };
        Value k = resolve(kc);   // key first — argument order matters
        Value v = resolve(vc);
        Value pr = Value::pair(k.toStr(), v);
        if (k.t != VT::Str) pr.pairKeyM() = std::make_shared<Value>(k);
        return pr;
    };
    return code;
}
// …the whole of `EXPR => v` for native codegen: the curry, else the Pair.
Value rtPairOf(const Value& kv, Value value, bool kVar, bool vVar) {
    if (Value code = rtPairCurry(kv, value, kVar, vVar); code.t == VT::Code) return code;
    return rtPairKeyed(kv, std::move(value));
}

Value rtSlipOf(const Value& v);   // InterpreterCore.cpp — the interpreter's own `|`
Value rtSlipShallow(const Value& v) {
    if (v.t == VT::Array && v.arr()) { Value r = v; r.isList = true; r.s = "Slip"; return r; }
    // A Hash/Map slips its pairs, out of its item container too: `(|$h, 5)` and
    // `%(|$h, k => 1)` spread the pairs of the Hash in $h, as the interpreter's
    // list and hash composers do.
    if (v.t == VT::Hash) { Value d = v; d.itemized = false; return rtSlipOf(d); }
    if (v.t == VT::Range) { Value r = Value::array(v.flatten()); r.isList = true; r.s = "Slip"; return r; }
    Value out = Value::array(); out.isList = true; out.s = "Slip";
    if (v.t != VT::Nil) out.arr()->push_back(v);
    return out;
}
// [ … ] composer items for native codegen — mirror the interpreter's ArrayLit
// splice rules (listToArray then splices anything tagged Slip one level):
// a List-valued member of a non-comma [..] splices; a comma-list member stays.
Value rtSpliceIfList(const Value& v) {
    if (v.t == VT::Array && v.arr() && v.isList) { Value r = v; r.s = "Slip"; return r; }
    return v;
}
// the one-arg rule: `[<a b>».Str]` — a SINGLE list-valued, non-itemized item spreads
// …and so does a lone Range, under the interpreter's own bounds: `[1..3]` is
// three elements, `[($k*8) ..^ ($k*8+8)]` eight (a `$`-held Range never gets
// here — codegen passes a `$` variable through as the one item it is)
Value rtOneArgItem(const Value& v) {
    if (v.t == VT::Array && v.arr() && v.isList && !v.itemized) { Value r = v; r.s = "Slip"; return r; }
    if (v.t == VT::Range && !v.itemized && !v.rExFrom() && v.rTo() - v.rFrom() < 1000000) return rtSlipShallow(v);
    return v;
}
// `[ ITEM ]` with one plain item, for native codegen: the one-arg rule above,
// and an ENDLESS integer Range is the lazy Array of its elements (`[1..*]`),
// as the interpreter's ArrayLit arm makes it
// An endless integer Range as the lazy Array of its elements, or false.
static bool endlessRangeArray(const Value& v, Value& out) {
    if (v.t != VT::Range || v.rTo() < 9000000000000000000LL || v.rNum() || v.ofType() == "Str" || v.itemized)
        return false;
    out = makeInfArray(v.rFrom() + (v.rExFrom() ? 1 : 0));
    out.isList = false;
    return true;
}
// a single lazy list (endless, or lazy by declaration and unread) is the lazy
// Array over it, as the interpreter's ArrayLit arm makes it
static bool lazyListArray(const Value& v, Value& out) {
    if (v.t != VT::Array || !v.ext() || v.itemized || !(endlessLazy(v) || declLazyLive(v))) return false;
    out = v; out.isList = false; out.s.clear();
    return true;
}
extern void (*g_pullLazy)(const Value&, size_t);   // Value.cpp
bool rtForPull(const Value& lst, size_t i) {
    if (!lst.arr() || !g_pullLazy) return false;
    g_pullLazy(lst, i + 1);
    return i < lst.arr()->size();
}
Value rtOneArgArray(const Value& v) {
    if (Value a; endlessRangeArray(v, a) || lazyListArray(v, a)) return a;
    return listToArray({rtOneArgItem(v)});
}
// `[@a]` for native codegen: the elements of @a, or the lazy Array over it
Value rtOneArgAtVar(const Value& v) {
    if (Value a; lazyListArray(v, a)) return a;
    return listToArray({rtSlipShallow(v)});
}
// a hyper result kept as one element is itemized — clear isList so later list
// contexts don't re-spread it (matches the interpreter's ArrayLit else-branch)
Value rtHyperItem(const Value& v) {
    if (v.t == VT::Array) { Value r = v; r.isList = false; return r; }
    return v;
}

// next/last/redo in EXPRESSION position for native codegen (`$x > 3 && last`):
// throw the interpreter's control-flow signals; native loop bodies catch them.
Value rtThrowNext(const std::string& label) { throw NextEx{label}; }
Value rtThrowLast(const std::string& label) { throw LastEx{label}; }
Value rtThrowRedo(const std::string& label) { throw RedoEx{label}; }

// { k => v, … } for native codegen — mirrors the interpreter's HashLit eval
// (arrays splice one level, then hash coercion).
Value rtHashLit(const ValueList& items) {
    Value arr = Value::array();
    for (auto& v : items) {
        if (v.t == VT::Array && v.arr()) { for (auto& x : *v.arr()) arr.arr()->push_back(x); }
        else arr.arr()->push_back(v);
    }
    return rtCoerceHash(arr);
}

// `@a = expr` for native codegen: list-assignment semantics. A List splices its
// elements (one level, via listToArray); an Array (itemized rows included) keeps
// its elements as they are; a Range expands; a lone scalar becomes a 1-elem array.
// A Hash (or quanthash) in list context spreads into its Pairs: a plain Hash
// gives `key => value`, a Set gives `elem => True`, a Bag/Mix `elem => weight`.
// `my @a = %h` / `my @s = $set` all go through this. (Iteration order follows
// the container; callers that need a stable order sort.)
// IO::Path::Parts SUBSCRIPTS in declaration order — volume, dirname, basename —
// not the map's sorted order, so `$parts[0]` is the volume pair.
//
// Only the subscript. Rakudo does NOT spread the parts anywhere else: `.list` is
// `(IO::Path::Parts.new(…),)`, `.pairs` is `(0 => the object,)` and `for $parts`
// yields the object once. Spreading them in toList/hashToPairs was tried and
// reverted — it made `.list` diverge in the other direction.
ValueList pathPartsPairs(const Value& v) {
    ValueList out;
    if (!v.hash()) return out;
    for (const char* k : {"volume", "dirname", "basename"}) {
        auto it = v.hash()->find(k);
        out.push_back(Value::pair(k, it != v.hash()->end() ? it->second : Value::str("")));
    }
    return out;
}

// `my @a[3;2]` for native codegen: a container with the given dimensions, the
// same one the interpreter's declaration arm builds.
Value rtShapedArray(const ValueList& dims, const std::string& declType) {
    std::vector<long long> d;
    d.reserve(dims.size());
    for (auto& v : dims) d.push_back(v.toInt());
    return makeShapedContainer(d, declType);
}

// `@a[2;2] = …` — fill a SHAPED container from the right-hand side. Extracted
// from the interpreter's assignment arm so the native backend can call the very
// same code: a shaped store is one of the places where two implementations
// would be two behaviours.
void rtShapedStore(Value& lv, const Value& rhs, const std::string& keepType) {
    auto shp = lv.shape();
    if (!shp || shp->empty()) return;
    if (shp->size() == 1) { // 1-dim: flat row fill, reject overflow
        long long cap = (*shp)[0];
        ValueList flat; for (auto& x : rhs.flatten()) flat.push_back(x);
        // a NATIVE integer array takes numbers only — a string has no int to unbox
        {
            const std::string& ot = lv.ofType();
            const bool nInt = ot.compare(0, 3, "int") == 0 || ot.compare(0, 4, "uint") == 0 || ot.compare(0, 4, "byte") == 0;
            const bool nNum = ot.compare(0, 3, "num") == 0;
            const bool nStr = ot == "str";
            for (auto& x : flat) {
                if ((nInt || nNum) && x.t == VT::Str && !x.isAllomorph())
                    throw RakuError{Value::typeObj("X::AdHoc"),
                        std::string("This type cannot unbox to a native ") + (nInt ? "integer" : "number") + ": P6opaque, Str"};
                if (nStr && x.t != VT::Str && x.t != VT::Any && x.t != VT::Nil)
                    throw RakuError{Value::typeObj("X::AdHoc"),
                        "This type cannot unbox to a native string: P6opaque, " + x.typeName()};
            }
        }
        if ((long long)flat.size() > cap)
            throw RakuError{Value::typeObj("X::OutOfRange"),
                "Cannot assign " + std::to_string(flat.size()) +
                " elements to a shaped array of " + std::to_string(cap)};
        lv = makeShapedContainer(*shp, keepType, &flat);
        return;
    }
    // multi-dim: the RHS must MATCH the shape. A shaped source must have an
    // identical shape; a nested list may not have MORE than dims[d] elements at
    // any level (a flat list is rejected), but a shortfall is fine — missing
    // slots keep the element default.
    if (rhs.shape() && *rhs.shape() != *shp)
        throw RakuError{Value::typeObj("X::Assignment::ArrayShapeMismatch"),
            "Cannot assign an array of a different shape"};
    Value built = makeShapedContainer(*shp, keepType); // all defaults
    std::function<void(Value&, const Value&, size_t)> overlay =
      [&](Value& dst, const Value& src, size_t d) {
        if (d == shp->size()) { dst = src; return; } // leaf
        if (!(src.t == VT::Array && src.arr()))
            throw RakuError{Value::typeObj("X::Assignment::ToShaped"),
                "Assignment to a shaped array needs a matching nested structure"};
        // …and a FLAT list is unstructured however long it is: every element at
        // a level above the leaves has to be a list of its own, so
        // `my @a[2;2] = <a b c d>` is ToShaped and not a size complaint.
        if (d + 1 < shp->size())
            for (auto& el : *src.arr())
                if (!(el.t == VT::Array || el.t == VT::Range)) {
                    std::string sh;
                    for (auto dim : *shp) sh += (sh.empty() ? "" : " ") + std::to_string(dim);
                    throw RakuError{Value::typeObj("X::Assignment::ToShaped"),
                        "Assignment to array with shape " + sh + " must provide structured data"};
                }
        if ((long long)src.arr()->size() > (*shp)[d])
            throw RakuError{Value::typeObj("X::Assignment::ArrayShapeMismatch"),
                "Too many elements for dimension " + std::to_string(d)};
        for (size_t i = 0; i < src.arr()->size(); i++)
            overlay((*dst.arr())[i], (*src.arr())[i], d + 1);
    };
    overlay(built, rhs, 0);
    lv = built;
}

// `my @a = gather { … }` must end up an ARRAY, not a Seq: `eqv [1, 2, 3]` is
// type-aware and says so. A gather has not been run at this point, so whether it
// is lazy is not known until it has been — pull once, and reify it here if that
// showed the block to be finite. An unbounded source stays lazy, as it does
// under Rakudo, where an Array may be lazy too.
static Value reifyIfFinite(const Value& v) {
    auto st = std::static_pointer_cast<LazySeqState>(v.ext());
    if (st->finiteSource) {   // a handle's lines: read to the end, keep the values
        forceLazy(v);
        Value r = Value::array(*v.arr()); r.isList = false; return r;
    }
    if (!st->gatherSeq) return v;
    // `lazy gather {…}` (or a list ending in one) stays lazy: assignment runs
    // none of it (S02-types/array.t)
    if (st->declaredLazy) { Value r = v; r.isList = false; r.s.clear(); return r; }
#if RAKUPP_HAVE_CORO
    // A coroutine gather is read in growing batches, each one switch into its
    // block, for as long as that stays cheap — list assignment is eager, so a
    // finite block arrives whole. One still producing after that is taken to
    // be unbounded (Rakudo would hang) and the Array stays lazy over it.
    if (!st->exhausted && g_revInterp && st->appendNext) {
        // (the clock is read only once the first batch has not finished it —
        // the common small gather never pays for it)
        g_revInterp->materializeLazy(v, 1024);
        const long long until = st->exhausted ? 0 : nowMicros() + 100000;
        size_t batch = 2048;
        while (!st->exhausted && v.arr()->size() < 50000000 && nowMicros() < until) {
            const size_t before = v.arr()->size();
            g_revInterp->materializeLazy(v, before + batch);
            if (v.arr()->size() == before) break;
            if (batch < (size_t(1) << 20)) batch *= 2;
        }
    }
#else
    forceLazy(v);
    // …and past the first growth step, too: list assignment is EAGER in Rakudo
    // (a plain gather is not lazy), so a finite gather of a few hundred takes
    // must arrive as all of them — `my @t = gather …; plan @t + @f` counted the
    // 128 a lazy prefix held. Growth continues while it stays cheap; a block
    // still producing after that is taken to be unbounded, and stays lazy.
    if (!st->exhausted && g_revInterp && st->appendNext) {
        const long long until = nowMicros() + 100000;
        while (!st->exhausted && v.arr()->size() < 50000000 && nowMicros() < until) {
            const size_t before = v.arr()->size();
            g_revInterp->materializeLazy(v, before * 2 + 64);
            if (v.arr()->size() == before) break;
        }
    }
#endif
    if (!st->exhausted) return v;
    Value r = Value::array(*v.arr()); r.isList = false; return r;
}

// A `take-rw`n element is a Proxy over the storage it was taken from. LIST
// ASSIGNMENT decontainerizes it: the array receives the VALUE, in a fresh slot
// of its own, so `my @s = gather { take-rw $x }; @s[0]++` steps the copy and
// leaves $x alone — as Rakudo. Iterating the Seq directly (`for f(@a) { $_++ }`)
// does NOT come through here, which is what keeps write-through working there.
//
// Callers gate this on the source being a Seq: only a gather can put a Proxy
// into a list, so an ordinary `@b = @a` copy never walks the elements at all.
// (Gating matters — an unconditional pass cost ~8% on array-assignment.)
static void deproxyElems(Value& a) {
    if (!g_deproxy || !a.arr()) return;
    for (auto& e : *a.arr())
        if (e.t == VT::Hash && e.hashKind == "Proxy" && e.hash()) e = g_deproxy(e);
}

Value rtArrayVal(const Value& v) {
    // an ITEMIZED hash (`$%h`, `$(%h)`) is one element, not a spread of pairs
    if (v.t == VT::Hash && v.hash() && v.itemized) { Value a = Value::array(); a.arr()->push_back(v); return a; }
    // `my @a = %h` / `my @a = set(1)` is an ARRAY of the pairs — hashToPairs
    // builds the List form its other callers want, so untag it here (LA-04).
    if (v.t == VT::Hash && v.hash()) { Value a = hashToPairs(v); a.isList = false; return a; }
    if (v.t == VT::Array && v.arr()) {
        if (v.ext()) { // a lazy seq stays lazy; a finite gather does not
            Value r = reifyIfFinite(v);
            if (v.s == "Seq" && r.arrS() != v.arrS()) deproxyElems(r);
            return r;
        }
        // a shaped source contributes its LEAVES, as it does in coerceArray —
        // these two must agree, or a program means one thing interpreted and
        // another compiled
        if (isMultiDimShaped(v)) return Value::array(shapedLeaves(v));
        if (v.isList) { Value r = listToArray(*v.arr()); r.isList = false;
                        if (v.s == "Seq") deproxyElems(r); return r; }
        Value r = Value::array(*v.arr()); // fresh buffer: `@a = @b` must not alias @b
        if (v.s == "Seq") deproxyElems(r); // take-rw's Proxies decontainerize on assign
        return r;
    }
    // `my @a = 1..*` is lazy, as interpreted: flattening reached the cap
    // and `@a.is-lazy` said False
    if (Value lz; endlessRangeArray(v, lz)) return lz;
    if (v.t == VT::Range) return Value::array(v.flatten());
    Value a = Value::array();
    if (v.t != VT::Nil) a.arr()->push_back(v);
    return a;
}

// ---- DESTROY protocol ------------------------------------------------------
// Registration end: the construction protocol calls this on every finished
// instance. Only a class chain that actually declares DESTROY pays anything —
// everything else returns before touching the registry.
void Interpreter::maybeRegisterDestroy(const Value& self) {
    if (self.t != VT::Object || !self.obj() || !self.obj()->cls) return;
    bool has = false;
    for (ClassInfo* c = self.obj()->cls.get(); c && !has; c = c->parent.get()) {
        if (c->methods.count("DESTROY")) has = true;
        else for (auto& p : c->extraParents)
            if (p && p->methods.count("DESTROY")) { has = true; break; }
    }
    if (!has) return;
    bool pressure = false;
    {
        std::lock_guard<std::mutex> lk(destroyMu_);
        destroyReg_.push_back(self.objS());
        pressure = !inDestroySweep_ && destroyReg_.size() >= destroySweepAt_;
    }
    if (pressure) {
        runPendingDestroys();
        std::lock_guard<std::mutex> lk(destroyMu_);
        destroySweepAt_ = std::max<size_t>(1024, destroyReg_.size() * 2);
    }
}

// Sweep end: an entry the registry alone still owns (use_count == 1) is an
// object the program can no longer reach, so its DESTROY chain runs — each
// class's OWN declaration, child first, parents after: the reverse of BUILD,
// per S12. A submethod found through the walk is exactly as inherited-proof
// as BUILD's walk makes it. An exception out of a destructor is swallowed;
// the object is already dead to the program. DESTROY runs at most once per
// object: the entry leaves the registry before its destructor is called, and
// resurrection does not re-register.
void Interpreter::runPendingDestroys() {
    std::vector<PRef<ObjectData>> dead;
    {
        std::lock_guard<std::mutex> lk(destroyMu_);
        if (inDestroySweep_) return; // a DESTROY that requests GC must not recurse
        inDestroySweep_ = true;
        for (auto it = destroyReg_.begin(); it != destroyReg_.end();) {
            if (it->use_count() == 1) { dead.push_back(std::move(*it)); it = destroyReg_.erase(it); }
            else ++it;
        }
    }
    for (auto& od : dead) {
        Value self = Value::object(od);
        // the class chain child-first: the primary parent line, then any
        // multiple-inheritance parents' lines, each class visited once
        std::vector<ClassInfo*> chain, pending{od->cls.get()};
        while (!pending.empty()) {
            ClassInfo* c = pending.front(); pending.erase(pending.begin());
            while (c) {
                if (std::find(chain.begin(), chain.end(), c) == chain.end()) chain.push_back(c);
                for (auto& p : c->extraParents) if (p) pending.push_back(p.get());
                c = c->parent.get();
            }
        }
        auto runOne = [&](Value& m) {
            try { invokeMethod(m, self, ValueList{}); }
            catch (...) {}
        };
        // 6.e: a role's DESTROY submethod is not composed; it runs after its
        // class's own, the last-composed role first and each role before the
        // roles it does — BUILD's order reversed
        auto roleParents = [](ClassInfo* r) {
            std::vector<ClassInfo*> out;
            if (r->parent && r->parent->isRole) out.push_back(r->parent.get());
            for (auto& p : r->extraParents) if (p && p->isRole) out.push_back(p.get());
            return out;
        };
        std::function<void(ClassInfo*, int)> roleDestroy = [&](ClassInfo* r, int depth) {
            if (!r || depth > 32) return;
            auto parents = roleParents(r);
            auto it = r->methods.find("DESTROY");
            bool own = it != r->methods.end() && it->second.t == VT::Code && it->second.code() &&
                       it->second.code()->isSubmethod;
            for (ClassInfo* p : parents) {
                if (!own) break;
                auto pt = p->methods.find("DESTROY");
                if (pt != p->methods.end() && pt->second.code() == it->second.code()) own = false;
            }
            if (own) runOne(it->second);
            for (size_t i = parents.size(); i-- > 0;) roleDestroy(parents[i], depth + 1);
        };
        for (ClassInfo* c : chain) {
            if (c->langRev >= 2 && od->cls && od->cls->langRev >= 2) {
                if (c->isRole) continue;
                std::vector<ClassInfo*> roles = roleParents(c);
                for (auto& r : c->composedRoles)
                    if (std::find(roles.begin(), roles.end(), r.get()) == roles.end()) roles.push_back(r.get());
                if (!roles.empty()) {
                    auto mi = c->methods.find("DESTROY");
                    if (mi != c->methods.end() &&
                        !(c->roleSubmethods.count("DESTROY") && c->roleSubmethodsHidden.count("DESTROY")))
                        runOne(mi->second);
                    for (size_t i = roles.size(); i-- > 0;) roleDestroy(roles[i], 0);
                    continue;
                }
            }
            auto mi = c->methods.find("DESTROY");
            if (mi == c->methods.end()) continue;
            runOne(mi->second);
        }
    }
    std::lock_guard<std::mutex> lk(destroyMu_);
    inDestroySweep_ = false;
}

// Rvalue overload: a freshly built plain List (a method-call result being
// assigned into an array) whose buffer nothing else shares can hand that
// buffer over instead of copying it element-wise — `@a = %h.values` on a
// 2M-entry hash paid a second full materialization here. Anything with a
// Slip to splice, a lazy tail, shape, or a shared buffer takes the copying
// path, whose semantics stay authoritative.
Value rtArrayVal(Value&& v) {
    if (v.t == VT::Array && v.arr() && v.isList && !v.ext() && !v.itemized &&
        !(v.shape() && !v.shape()->empty()) && v.payloadUnique()) {
        for (auto& it : *v.arr())
            if (it.t == VT::Array && it.arr() && it.s == "Slip")
                return rtArrayVal(static_cast<const Value&>(v));
        Value r = Value::array();
        r.setArr(v.arrS());
        return r;
    }
    return rtArrayVal(static_cast<const Value&>(v));
}
bool isNativeScalarName(const std::string& t) {
    static const std::set<std::string> k = {
        "int", "int8", "int16", "int32", "int64", "uint", "uint8", "uint16", "uint32", "uint64",
        "num", "num32", "num64", "str", "byte", "long", "longlong", "ulong", "ulonglong",
        "size_t", "ssize_t", "bool", "atomicint"};
    return k.count(t) > 0;
}
Value coerceArray(const Value& v, bool nativeTarget) {
    v.seqTouch();   // an Array made of a Seq has its values: the Seq is cached (SeqToken)
    // `@a = Nil` is ONE element reset to the container default ([Any] — and a
    // typed array's store turns it into the element type's type object), NOT
    // an empty list: zef's Build assigns a promise's Nil result into
    // `my Bool @results` and then counts on `?@results` seeing one element
    // (Rakudo: Array[Bool].new(Bool)). `()` still empties. (issue #37)
    if (v.t == VT::Nil) { Value a = Value::array(); a.arr()->push_back(Value::typeObj("Any")); return a; }
    if (v.t == VT::Hash && v.hash() && v.itemized) { Value a = Value::array(); a.arr()->push_back(v); return a; }
    // `my @a = %h` / `my @a = set(1)` is an ARRAY of the pairs — hashToPairs
    // builds the List form its other callers want, so untag it here (LA-04).
    if (v.t == VT::Hash && v.hash()) { Value a = hashToPairs(v); a.isList = false; return a; }
    // a Blob/Buf assigns to an @-array as its elements (`my uint32 @W = $M`
    // in Digest::SHA1 — 32-bit words for blob32)
    // …but only into a NATIVE array: `my @a = "hi".encode` is ONE element under
    // Rakudo (a Blob is not Iterable), and PSGI's `my @body = $body` relied on
    // that — its utf8 body arrived as a list of byte Ints.
    if (nativeTarget && v.t == VT::Str && !v.itemized && (v.hashKind == "Blob" || v.hashKind == "Buf")) {
        Value r = Value::array(v.blobList()); r.isList = false; return r;
    }
    if (v.t == VT::Array) {
        if (v.itemized) { // an itemized Array is ONE element: `my @row = @m[0]` is [[...],]
            Value r = Value::array(); r.arr()->push_back(v); return r;
        }
        if (v.ext()) { // a lazy seq stays lazy; a finite gather does not
            Value r = reifyIfFinite(v);
            // …and only a FRESH buffer may be decontainerized: a still-lazy seq
            // is shared with v, where the Proxies are the write-through
            if (v.s == "Seq" && r.arrS() != v.arrS()) deproxyElems(r);
            return r;
        }
        // A shaped array STORED into an unshaped one contributes its leaves:
        // `my @flat = @a[3;2]` is six elements, and so is a `@a is copy`
        // parameter. (Plain binding — `sub f(@a)` — does not come through here,
        // and keeps the shaped array itself, as it does under Rakudo.)
        if (isMultiDimShaped(v)) { Value r = Value::array(shapedLeaves(v)); r.isList = false; return r; }
        // `@b = @a` / `@x is copy` copy the top-level buffer — a fresh Array that does
        // NOT alias the source (nested itemized arrays are containers, shared by value,
        // matching Rakudo). Mirrors rtArrayVal so the interpreter and native backends agree.
        Value r = Value::array(*v.arr()); r.isList = false;
        decontCopiedElems(*r.arr());   // …as does an eager gather's take-rw
        // a boxed array's elements are boxed: a native's tags stay behind, or
        // `@a[1] = 0.5` would truncate and `@a[0] + 1` wrap (see dropNativeTags)
        if (!nativeTarget)
            for (auto& e : *r.arr()) if (e.natBits) dropNativeTags(e);
        return r;
    }
    if (v.t == VT::Range) {
        if (v.rTo() >= 9000000000000000000LL) { // …..Inf : a lazy @-array, materialised on demand
            long long start = v.rFrom() + (v.rExFrom() ? 1 : 0);
            Value a = Value::array(); a.isList = false;
            auto st = std::make_shared<LazySeqState>(); st->infinite = true;
            auto next = std::make_shared<long long>(start);
            st->appendNext = [next](ValueList& cache) -> bool { cache.push_back(Value::integer((*next)++)); return true; };
            a.extM() = st;
            return a;
        }
        return Value::array(v.flatten());
    }
    // …and an OBJECT assigned to an `@` container is asked what it holds, the same
    // way a `%` assignment already did: Rakudo's list store walks the right-hand
    // side's `.list`, whose default is built on `.iterator`. `my @files =
    // glob('*.md')` is the files, not one IO::Glob.
    if (v.t == VT::Object && g_objListItems) {
        ValueList items;
        if (g_objListItems(v, items)) { Value r = Value::array(std::move(items)); r.isList = false; return r; }
    }
    Value a = Value::array();
    if (v.t != VT::Nil && v.t != VT::Any) {
        a.arr()->push_back(v);
        if (!nativeTarget && v.natBits) dropNativeTags(a.arr()->back());   // `my @a = $an-int`
    }
    return a;
}

// Assigning an OBJECT to a `%` container asks the object what it holds: Rakudo's
// hash store walks the right-hand side's `.list`, and the default `Any.list` is
// built on `.iterator` — so a class that supplies either one (declared, or
// delegated with `handles`) fills the hash with its own pairs instead of landing
// in it as a single value. zef's config object is exactly that shape
// (`class :: { has %.hash handles <AT-KEY … iterator list kv keys values> }`
// assigned into Zef::Client's `has %.config`). Set by the Interpreter, because
// answering the question means calling a user method. Declared in BuiltinsShared.h
// — `has %.h = $obj` takes the same route through coerceToSigil.
std::function<bool(const Value&, ValueList&)> g_objListItems;

// `store` = this is an ASSIGNMENT into a %-container, not a binding. The two differ
// for QuantHashes: `my %h = set <a b>` copies the set's pairs into a plain Hash
// ({a=>True, b=>True}), while `sub f(%h)` binds the Set itself and %h.^name stays Set.
// A hash store that runs out of values half-way through is an ERROR, not a
// silent truncation (sheet HM-01): `my %h = 1, 2, 3` throws, and the exception
// carries how many elements were seen (`.found`) and the one left over
// (`.last`), which is what a handler reports. The two message shapes are
// Rakudo's: a lone element "Only saw", several "Found N (implicit) elements".
[[noreturn]] void throwHashOddNumber(long long found, const Value& last) {
    Value shown = last;
    if (shown.t == VT::Array || shown.t == VT::Hash) shown.itemized = true; // a hash value is itemized
    std::string repr = g_rakuRepr ? g_rakuRepr(shown) : shown.gist();
    std::string msg = "Odd number of elements found where hash initializer expected:\n";
    msg += found == 1 ? "Only saw: " + repr
                      : "Found " + std::to_string(found) + " (implicit) elements:\nLast element seen: " + repr;
    if (g_revInterp)
        g_revInterp->throwTypedV("X::Hash::Store::OddNumber",
                                 {{"found", Value::integer(found)}, {"last", last}}, msg);
    throw RakuError{Value::typeObj("X::Hash::Store::OddNumber"), msg};
}
// `my %h = { … }` where the block is a real Callable — a `{ $_ }` or a `{ ; }`
// — is its own mistake, and Rakudo spells out the two ways to make it.
[[noreturn]] void throwHashCallableStore() {
    throw RakuError{Value::typeObj("X::AdHoc"),
        "Cannot use a Callable as the only argument to store in a Hash.  If the\n"
        "intent was to store the contents of a Hash, one should probably use the\n"
        "%( ) hash constructor instead of { }.  Causes of { } misinterpretation:\n"
        "- using ';' instead of ',' to separate values, as these imply statements\n"
        "- using '$_' or any placeholder variable, as they imply a block scope"};
}
// A hash STORE puts each value in a Scalar container: a List, Array or Hash
// held there is ONE item — `for %h<k>` iterates once and `my @x = %h<k>` takes
// one element, as `%h<k> = (1, 2)` already did (S12-attributes/instance.t).
// A Junction and the tagged hashes (Proxy, Failure, handles …) are left alone.
static void itemizeHashValues(Value& h) {
    if (h.t != VT::Hash || !h.hash()) return;
    for (auto& kv : *h.hash()) {
        Value& e = kv.second;
        if ((e.t == VT::Array && e.hashKind != "Capture" && e.enumName.empty()) ||
            (e.t == VT::Hash && e.hashKind.empty()))
            e.itemized = true;
    }
}
Value coerceHash(const Value& v, bool store, bool objKeyed) {
    forceLazy(v);   // `my %h = gather { take … }`: the gather's pairs
    // a CAPTURE's hash is its NAMED part only: `%(\( (:a(2)) ))` is empty
    if (v.t == VT::Array && v.hashKind == "Capture" && v.arr()) {
        Value h = Value::makeHash();
        for (auto& e : *v.arr())
            if (e.t == VT::Pair && e.namedArg) (*h.hash())[e.s] = e.pairVal() ? *e.pairVal() : Value::any();
        return h;
    }
    if (v.t == VT::Hash) { // already a hash: copy entries (value semantics for my %h = %other)
        bool quant = v.hashKind.rfind("Set", 0) == 0 || v.hashKind.rfind("Bag", 0) == 0 ||
                     v.hashKind.rfind("Mix", 0) == 0;
        // A STORE fills a Hash, so the SOURCE's kind does not travel: `my %h =
        // $map` leaves a mutable Hash, not a Map that then refuses every write
        // (sheet HM-14 made that refusal real, which is how it surfaced).
        bool sourceKind = quant || v.hashKind == "Map" || v.hashKind == "Stash";
        Value h = Value::makeHash();
        if (!(store && sourceKind)) h.hashKind = v.hashKind;
        if (v.hash()) {
            if (store && quant) {
                bool setty = v.hashKind.rfind("Set", 0) == 0; // a Set's weights are all True
                for (auto& kv : *v.hash())
                    (*h.hash())[kv.first] = setty ? Value::boolean(true) : kv.second;
            }
            // an OBJECT hash stored into a plain one: its keys become Strs
            // (`my %n = %obj-hash` — Rakudo stringifies them)
            else if (store && v.objKeyed && !objKeyed) {
                for (auto& kv : *v.hash()) {
                    Value val = kv.second;
                    std::string k = val.pairKey() ? val.pairKey()->toStr() : kv.first;
                    val.pairKeyM().reset();
                    (*h.hash())[k] = std::move(val);
                }
            }
            else {
                *h.hash() = *v.hash();
                if (store) for (auto& kv : *h.hash()) decontCopied(kv.second);
            }
        }
        if (store) itemizeHashValues(h);
        return h;
    }
    // a lone Callable is the `{ … }` that was meant to be a hash composer
    if (store && v.t == VT::Code) throwHashCallableStore();
    Value h = Value::makeHash();
    ValueList items;
    if (v.t == VT::Array) items = *v.arr();
    else if (v.t == VT::Pair) items.push_back(v);
    else if (v.t == VT::Range && !v.itemized) items = v.flatten();   // `my %h = 1..4` is 1 => 2, 3 => 4
    else if (v.t == VT::Object && g_objListItems && g_objListItems(v, items)) { /* filled above */ }
    else if (v.t != VT::Nil && v.t != VT::Any) items.push_back(v);
    // The key a value stands for in a plain / object hash: an OBJECT hash keys
    // a type object (and an enum's bare type) by "(Name)", the same convention
    // the subscript paths use, instead of the empty stringification that
    // collapsed Getopt::Long's whole converter table onto one key.
    auto keyStr = [&](const Value& k, const std::string& fallback) -> std::string {
        if (objKeyed) return objHashIndex(k);   // by identity (sheet HM-04)
        return fallback;   // the pair's own key string (empty stays empty)
    };
    for (size_t i = 0; i < items.size(); i++) {
        if (items[i].t == VT::Pair) {
            Value pv = items[i].pairVal() ? *items[i].pairVal() : Value::any();
            const std::shared_ptr<Value>& pk = items[i].pairKey();
            // a JUNCTION key autothreads: `'W'|'W*' => X` stores X under BOTH
            // 'W' and 'W*', as Rakudo does when a Junction is assigned as a key
            // (PDF::Content::Ops' %Transition table keys ops by `'W'|'W*'` and
            // `any(PaintingOps.keys)`, and rakupp kept only the "any" string).
            if (pk && isJunction(*pk)) {
                for (auto& eig : *pk->arr()) (*h.hash())[keyStr(eig, eig.toStr())] = pv;
                continue;
            }
            // …and an object hash keeps the key AS STORED, so `:{ 1 => "a" }.keys`
            // answers the Int 1. Only the flat k,v branch below did this.
            // (a Str key too: the index is an identity string, so nothing else
            // remembers the key's own spelling)
            Value realK = pk ? *pk : Value::str(items[i].s);
            if (objKeyed) pv.pairKeyM() = std::make_shared<Value>(realK);
            (*h.hash())[keyStr(realK, items[i].s)] = pv;
        } else if (items[i].t == VT::Hash && items[i].hash() && items[i].hashKind.empty() &&
                   !items[i].itemized) {
            // a plain (non-itemized) Hash in the list MERGES its pairs
            // (`%( $<authority>.ast, path => … )` in Cro::Uri) — it is not a key.
            // An ITEMIZED $hashitem stays whole (S02 assigning-refs).
            for (auto& kv : *items[i].hash()) {
                // (an OBJECT hash's entries go in under their keys' Str form)
                if (items[i].objKeyed && !objKeyed) {
                    Value val = kv.second;
                    std::string k = val.pairKey() ? val.pairKey()->toStr() : kv.first;
                    val.pairKeyM().reset();
                    (*h.hash())[k] = std::move(val);
                }
                else (*h.hash())[kv.first] = kv.second;
            }
        } else if (i + 1 < items.size()) {
            // flat k,v pairing — an OBJECT hash keys type objects by "(Name)"
            // here too (`my %type2allo{Any} = Int, IntStr, …` in roast's val.t)
            std::string k2 = objKeyed ? keyStr(items[i], items[i].toStr()) : items[i].toStr();
            // …and keeps the key AS STORED for an object hash, so `.keys`
            // answers the Int (`my %h{Mu} = 1, 2, 3, 4` — CBOR::Simple encodes
            // such keys as integers)
            Value stored = items[i + 1];
            if (objKeyed) stored.pairKeyM() = std::make_shared<Value>(items[i]);
            (*h.hash())[k2] = std::move(stored);
            i++;
        }
        else if (store) throwHashOddNumber((long long)items.size(), items[i]);
    }
    if (store)
        for (auto& kv : *h.hash()) {
            decontCopied(kv.second);
            if (kv.second.natBits) dropNativeTags(kv.second);   // `my %h = a => $an-int`
            // Nil assigned into an element resets it, as into any container:
            // `my %h = point => Nil; %h<point>` is Any (Rakudo), not Nil
            if (kv.second.t == VT::Nil) kv.second = Value::any();
        }
    if (store) itemizeHashValues(h);
    return h;
}

// `A ,= B` — the assignment metaoperator over `infix:<,>`, and it is ALWAYS
// `A = A, B`: the comma applied to the two operands, then assigned, with
// whatever that assignment already means for A's container. So a hash flattens
// `(%h, 5 => 4)` back into one hash (issue #85, where `,=` had no token at all
// and `%h ,= 5 => 4` lexed as a trailing-comma list assignment that REPLACED
// the hash), an array takes the list as `my @b = @a, 3` does, and a scalar
// holds it as one item. The store goes INTO the container A already holds, so
// A keeps its identity — which is also what makes `@a ,= 3` answer the same
// self-referential list `@a = (@a, 3)` does here and on Rakudo: the list's
// first element IS that container.
// One definition, called by the interpreter and by both compiling backends,
// so a `,=` cannot mean three things.
void rtCommaAssign(Value& l, const Value& r) {
    Value lst = listToArray({l, r});   // the engine's own comma: only a Slip splices
    lst.isList = true;
    if (l.t == VT::Hash && l.hashKind.empty() && l.hash()) {
        Value h = coerceHash(lst, /*store=*/true);
        *l.hash() = *h.hash();
    }
    else if (l.t == VT::Array && !l.itemized && l.arr()) {
        Value arr = coerceArray(lst);
        *l.arr() = *arr.arr();
    }
    else { lst.itemized = true; l = std::move(lst); }  // a scalar holds the List as one item
}
Interpreter* g_cbInterp = nullptr; // NativeCall callback trampoline target
// The two live-target pointers above are FILE statics the constructor points at
// the newest Interpreter — a SCRATCH one (a slang's host, an L10N table read)
// would leave them dangling once it is gone. Whoever builds a short-lived
// Interpreter saves the target first and puts it back.
Interpreter* Interpreter::liveTarget() { return g_revInterp; }
thread_local Value* Interpreter::builtinTopicWB_ = nullptr;
thread_local const Interpreter::ArgWriter* Interpreter::builtinArgWriter_ = nullptr;
thread_local bool Interpreter::deferGather_ = false;
thread_local bool Interpreter::valueSmartmatch_ = false;
thread_local bool Interpreter::matchVarSuppressed_ = false;
thread_local std::string Interpreter::declaringType_;
thread_local bool Interpreter::hoistingSubs_ = false;
thread_local bool Interpreter::suppressLoopFirst_ = false;
// Per-thread call-stack state (step 3a — see header).
thread_local std::vector<Interpreter::RedispatchCtx> Interpreter::redispatchStack_;
thread_local int Interpreter::loopNest_ = 0;
thread_local std::vector<Interpreter::ProtoCtx> Interpreter::protoStack_;

// A `proto` whose body is more than a bare `{*}` runs AROUND the dispatch: it does its
// own work, and the `{*}` statement inside it hands the ORIGINAL arguments to the best
// candidate (S06). It is never a candidate itself — with no candidates to redispatch
// to it is simply the routine, and a `{*}` in it then reports the same no-match Rakudo
// would.
const Value* Interpreter::protoBodyOf(const Callable& c) {
    for (auto& cand : c.candidates)
        if (cand.code() && cand.code()->isProtoBody) return &cand;
    return nullptr;
}
// The `{*}` inside a proto body: hand the proto's own arguments to the best candidate.
// The frame is popped for the duration so the candidate cannot re-enter it.
Value Interpreter::protoRedispatch() {
    ProtoCtx ctx = protoStack_.back();
    protoStack_.pop_back(); tctx_.protoDepth--;
    struct Push { std::vector<ProtoCtx>& s; ProtoCtx c;
                  ~Push() { s.push_back(std::move(c)); tctx_.protoDepth++; } } push{protoStack_, ctx};
    return ctx.dispatch(ctx.args);
}
Value Interpreter::callProtoBody(const Value& proto, const std::function<Value(ValueList)>& dispatch,
                                 ValueList args, const std::vector<ExprPtr>* rwArgs) {
    protoStack_.push_back(ProtoCtx{dispatch, args}); tctx_.protoDepth++;
    struct Pop { std::vector<ProtoCtx>& s; ~Pop() { s.pop_back(); tctx_.protoDepth--; } } pop{protoStack_};
    return callCallable(proto, std::move(args), rwArgs, /*ownFrame=*/true);
}
Value Interpreter::callProtoBodyMethod(const Value& proto, const Value& self,
                                       const std::function<Value(ValueList)>& dispatch,
                                       ValueList args, const std::vector<ExprPtr>* rwArgs) {
    protoStack_.push_back(ProtoCtx{dispatch, args}); tctx_.protoDepth++;
    struct Pop { std::vector<ProtoCtx>& s; ~Pop() { s.pop_back(); tctx_.protoDepth--; } } pop{protoStack_};
    return invokeMethod(proto, self, std::move(args), rwArgs, /*ownFrame=*/true);
}

thread_local std::vector<std::shared_ptr<ReactCtx>> Interpreter::reactStack_;
thread_local int Interpreter::threadDepth_ = 0;

// the live interpreter's class registry, for free-function smartmatch on user
// type objects (applyArith has no Interpreter&); the newest instance wins
std::unordered_map<std::string, std::shared_ptr<ClassInfo>>* g_matchClasses = nullptr;
// …and the alias table beside it, for the identity operators (free functions)
const std::unordered_map<std::string, std::string>* g_classAliases = nullptr;
// subset check for free-function ~~ (`5 ~~ Five`): returns true and sets `out`
// when the RHS names a live subset; the newest interpreter instance wins
std::function<bool(const std::string&, const Value&, bool&)> g_subsetCheck;
// A Proxy is a CONTAINER: anything that renders a value has to read it first.
// rakuRepr is a free function with no interpreter to call FETCH with, so the
// interpreter publishes one here — same shape as g_subsetCheck above.
std::function<Value(const Value&)> g_deproxy;
std::function<bool(const Value&, std::string&)> g_userStr;
std::function<bool(const Value&, std::string&, bool&)> g_userWhich;
std::atomic<uint64_t> g_lexShadowMask{0};
std::atomic<bool> g_userPrefixShadow{false};
// applyArith is a free function, but the Whatever-curry it builds has to resolve
// a shadowing `&infix:<op>` in the scope it is being written in — same shape as
// g_deproxy, and set from the same place.
std::function<Value*(const std::string&)> g_lexInfixLookup;

// See the declaration in Interpreter.h. Both ',' and ':' separate; a ':' right
// after a lone leading drive letter belongs to the path.
std::vector<std::string> splitSearchPath(const std::string& spec) {
    std::vector<std::string> out;
    std::string cur;
    for (size_t i = 0; i < spec.size(); i++) {
        char c = spec[i];
        bool driveLetter = c == ':' && cur.size() == 1 &&
                           ascii::isalpha((unsigned char)cur[0]) &&
                           i + 1 < spec.size() && (spec[i + 1] == '\\' || spec[i + 1] == '/');
        if ((c == ':' && !driveLetter) || c == ',') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        }
        else cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// The process-wide statics the type matchers, the NativeCall trampolines and
// the revision probe dispatch through all point at ONE Interpreter — the one
// that adopted them last. The constructor adopts; a short-lived scratch
// Interpreter (a slang's host, see rakuppActivateSlang) hands them back to
// the one it was built beside, or they dangle once it is gone.
void Interpreter::adoptProcessStatics() {
    g_cbInterp = this; // NativeCall callback trampolines dispatch through here
    g_revInterp = this; // divide-by-zero shape consults the live language revision
    g_matchClasses = &classes_;
    g_classAliases = &classAliases_;
    rtSetAliasView(&classAliases_, &classes_); // package-relative short names for the type matchers
    g_objListItems = [this](const Value& v, ValueList& out) { return objListItems(v, out); };
    g_deproxy = [this](const Value& v) -> Value { return deproxy(v); };
    // Only an OBJECT with a `Str` of its own: everything else keeps the
    // rendering it already had, so this cannot change what a plain value
    // sprintf'd or a plain key hashed to.
    g_userStr = [this](const Value& v, std::string& out) {
        if (v.t != VT::Object || !v.obj() || !v.obj()->cls) return false;
        if (!v.obj()->cls->findMethod("Str")) return false;
        out = strOf(v);
        return true;
    };
    // An OBJECT whose class writes its own `method WHICH` identifies by what
    // that answers — `===`, `.unique`, a Set's elements. Red::Column is
    // `ValueObjAt.new: self.gist`, so two columns built from one attribute are
    // the same column, and a model's `UNIQUE (name)` is declared once, not once
    // per `.column` call. `isValue` reports a ValueObjAt answer.
    g_userWhich = [this](const Value& v, std::string& out, bool& isValue) {
        if (v.t != VT::Object || !v.obj() || !v.obj()->cls) return false;
        if (!v.obj()->cls->findMethod("WHICH")) return false;
        thread_local int depth = 0;
        if (depth > 8) return false;           // a WHICH asking its own identity
        struct D { int& d; D(int& x) : d(x) { d++; } ~D() { d--; } } g{depth};
        Value w = methodCall(v, "WHICH", ValueList{});
        out = w.toStr();
        isValue = w.t == VT::Str && w.hashKind == "ValueObjAt";
        return true;
    };
    g_lexInfixLookup = [this](const std::string& op) -> Value* { return lexInfixLookup(op); };
    g_subsetCheck = [this](const std::string& name, const Value& v, bool& out) {
        if (!subsets_.count(name)) return false;
        out = subsetMatches(name, v);
        return true;
    };
}

Interpreter::Interpreter() {
    adoptProcessStatics();
    // On the `now` clock, NOT raw POSIX. epochNowSecs() carries the Instant
    // epoch offset that `now` and every timer compare against, so reading the
    // system clock directly here put $*INIT-INSTANT exactly that offset behind
    // `INIT now` — ten seconds, which is twice S28-named-variables/init-instant.t's
    // five-second tolerance, and the same skew made sleep-until(Instant) decide
    // it still had ten seconds to wait.
    initInstant_ = epochNowSecs();
    defaultScheduler_ = Value::makeHash(); defaultScheduler_.hashKind = "Scheduler";
    (*defaultScheduler_.hash())["name"] = Value::str("ThreadPoolScheduler");
    mainThread_ = std::this_thread::get_id();
    // PARALLEL BY DEFAULT (v3, PARALLEL-PLAN P5): start/worker threads run
    // on all cores. RAKUPP_GIL=1 selects the cooperative GIL — the escape
    // hatch, the bisection tool, and a CI leg. RAKUPP_PARALLEL=0 is honored
    // as a synonym for symmetry with the old opt-in spelling.
    {
        const char* g = std::getenv("RAKUPP_GIL");
        const char* p = std::getenv("RAKUPP_PARALLEL");
        bool gilWanted = (g && *g && std::string(g) != "0") ||
                         (p && std::string(p) == "0");
        parallelMode_ = !gilWanted;
    }
    global_ = std::make_shared<Env>();
    curPkgEnv_ = global_;
    tctx_.cur = global_;
    rtInstallStdoutCounter();   // `$*OUT.tell` on a pipe counts bytes from here on
    // Module search paths. "lib"/"."/"rakulib" are relative to the CWD; the rest
    // come from the environment so a checkout works anywhere:
    //   RAKULIB  extra module dirs, separated by ',' (Rakudo's spelling) or ':'
    //   ROAST    a Roast checkout, adds its Test-Helpers lib (for the test suite)
    if (const char* rl = std::getenv("RAKULIB"))
        for (auto& d : splitSearchPath(rl)) libPaths_.push_back(d);
    if (const char* ro = std::getenv("ROAST"))
        libPaths_.push_back(std::string(ro) + "/packages/Test-Helpers/lib");
    // The BINARY-relative rakulib/ — the engine's shadow modules
    // (NativeHelpers::Blob first among them). The cwd-relative "rakulib" entry
    // only exists when a program runs from the checkout root; a dist suite
    // runs from its own extract dir and must still find the shadows, so the
    // search is anchored to the executable rather than to the cwd. (The
    // installer itself no longer looks anything up beside the binary — it is
    // baked into the CLI; these shadow modules are the remaining case.)
    {
        char buf[4096];
        std::string self;
#if defined(_WIN32)
        DWORD n = ::GetModuleFileNameA(nullptr, buf, sizeof buf);
        if (n > 0 && n < sizeof buf) self = buf;
#elif defined(__APPLE__)
        uint32_t sz = sizeof buf;
        if (_NSGetExecutablePath(buf, &sz) == 0) self = buf;
#else
        ssize_t n = ::readlink("/proc/self/exe", buf, sizeof buf - 1);
        if (n > 0) { buf[n] = '\0'; self = buf; }
#endif
        auto slash = self.rfind('/');
        if (slash != std::string::npos) {
            std::string dir = self.substr(0, slash);
            for (const char* rel : {"/../rakulib", "/../libexec/rakupp/rakulib"}) {
                std::string cand = dir + rel;
                struct ::stat st;
                if (::stat(cand.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
                    libPaths_.push_back(cand);
                    if (shadowLibDir_.empty()) shadowLibDir_ = cand;
                }
            }
        }
    }
    // %*ENV — the process environment, as a Hash (found via the normal env chain).
    // The values are ALLOMORPHS, as `val` makes them: a variable set to "0" is an
    // IntStr and therefore FALSE, where a plain Str "0" is true in Raku (it is not
    // Perl). Test::When's harness sets every flag it is not testing to 0 and the
    // module asks `unless %*ENV<ALL_TESTING>` — read as Str, that turned the skip
    // off and ran every test it was supposed to skip.
    {
        Value envh = Value::makeHash();
        for (char** ep = rakupp_environ(); ep && *ep; ++ep) {
            std::string kv = *ep; auto eq = kv.find('=');
            if (eq == std::string::npos) continue;
            (*envh.hash())[kv.substr(0, eq)] = valAllomorph(Value::str(kv.substr(eq + 1)));
        }
        global_->define("%*ENV", envh);
    }
    // `Cursor` is the setting's old name for Match: `has Cursor $.cursor` holds
    // a Match (Cro::WebApp::Template's SyntaxError does), and `Cursor.^name`
    // answers Match
    global_->define("Cursor", Value::typeObj("Match"));
    // X::AdHoc — the exception `die "message"` produces (so $_/$! in CATCH answer .message/.^name)
    // — plus the handful of typed exceptions roast constructs with .new.
    {
        // Registering an exception makes it CONSTRUCTIBLE — but it also takes it
        // out of the X:: ancestry, because a registered name answers `.^mro` and
        // `.^parents` from its ClassInfo and not from typeAncestry. Without the
        // parent below, `X::AdHoc.^mro` was `X::AdHoc, Any, Mu` — no Exception in
        // it at all — while every UNregistered X:: name got the full chain. Every
        // one of these is an Exception; the roles and any deeper ancestry still
        // come from the generated table, which `~~` consults by name.
        auto reg = [&](const char* name, std::initializer_list<const char*> attrs) {
            auto ci = std::make_shared<ClassInfo>();
            ci->name = name;
            ci->nativeParent = "Exception";
            for (const char* an : attrs) { ClassAttr a; a.name = an; a.sigil = '$'; a.pub = true; ci->attrs.push_back(a); }
            classes_[name] = ci;
        };
        reg("X::AdHoc", {"payload", "message"}); // `die "msg"` — .payload IS the message
        reg("X::Syntax::Reserved", {"reserved", "instead", "pos", "message"});
        // `...` with seeds that are neither arithmetic nor geometric throws this
        // (the sequence builder constructs it with `from` = the seed list)
        reg("X::Sequence::Deduction", {"from", "message"});
        // X::NYI — the engine already THROWS it as a bare type object; registering
        // it lets a program CONSTRUCT one, which is the whole of what the NYI
        // distribution does (`Failure.new: X::NYI.new: :$feature`). Its message is
        // composed from `feature` at construction, like the X::IO family below.
        reg("X::NYI", {"feature", "message"});
        // X::Dynamic::NotFound — reading, assigning to or `temp`-ing a `*`-twigil
        // name nothing declares. Rakudo's carries just `$.name` (the full
        // spelling, sigil and twigil included) and composes its message from it;
        // its .^mro is (NotFound, Exception, Any, Mu), so the generated ancestry
        // table needs no row — Exception is implied for every X:: name.
        reg("X::Dynamic::NotFound", {"name", "message"});
        reg("Exception", {"message"}); // base class: Exception.new is instantiable
        // The X::IO family. rakupp's own IO builtins already THROW these (as bare
        // type objects with a hand-written message); registering them as classes
        // lets a program CONSTRUCT one too — File::Find's test suite mocks `dir`
        // with `X::IO::Dir.new(path => …, os-error => …).throw` — and lets
        // `when X::IO::Dir` match it. `.message` is composed from the attributes
        // at construction (see the X::IO table in MethodCallPart2.cpp).
        for (const char* n : {"X::IO::Dir", "X::IO::Rmdir", "X::IO::Unlink",
                              "X::IO::Chdir", "X::IO::Cwd"})
            reg(n, {"path", "os-error", "message"});
        for (const char* n : {"X::IO::Symlink", "X::IO::Link"})
            reg(n, {"name", "target", "os-error", "message"});
        for (const char* n : {"X::IO::Rename", "X::IO::Copy", "X::IO::Move"})
            reg(n, {"from", "to", "os-error", "message"});
        for (const char* n : {"X::IO::Mkdir", "X::IO::Chmod"})
            reg(n, {"path", "mode", "os-error", "message"});
        reg("X::IO::DoesNotExist", {"path", "trying", "message"});
        // `.open` (and every other file operation) on a directory answers this,
        // not the errno the syscall would have given: EISDIR is a description of
        // the mistake, "is a directory, cannot do '.open'" is the mistake itself.
        reg("X::IO::Directory", {"path", "trying", "message"});
    }
    // CompUnit::Repository — a role a repository class must fully implement, and the
    // $*REPO instance that does it.
    {
        // X::Wrapper — Rakudo's core role for exceptions that wrap another
        // exception. JSON::Class marks its Serialize/Deserialize::Fatal wrappers
        // with it at trait time (`type.^add_role(::('X::Wrapper'))`). Only the
        // attribute surface is seeded; the private helper methods matter only on
        // error-formatting paths.
        auto xwrap = std::make_shared<ClassInfo>();
        xwrap->name = "X::Wrapper"; xwrap->isRole = true;
        { ClassAttr a; a.name = "exception"; a.sigil = '$'; a.pub = true; xwrap->attrs.push_back(a); }
        classes_["X::Wrapper"] = xwrap;
        auto repoRole = std::make_shared<ClassInfo>();
        repoRole->name = "CompUnit::Repository"; repoRole->isRole = true;
        repoRole->requiredMethods = {"id", "need", "load", "loaded"};
        classes_["CompUnit::Repository"] = repoRole;
        auto fs = std::make_shared<ClassInfo>();
        fs->name = "CompUnit::Repository::FileSystem"; fs->parent = repoRole;
        // …with the two attributes the constructor is always given, declared so
        // that `.prefix` answers (Rakudo hands back an IO::Path) and so the
        // repository can look a module up in the tree it was pointed at.
        for (const char* a : {"prefix", "next-repo"}) {
            ClassAttr ca; ca.name = a; ca.sigil = '$'; ca.pub = true; fs->attrs.push_back(ca);
        }
        classes_["CompUnit::Repository::FileSystem"] = fs;
        // PseudoStash — what `MY::`, `CALLER::`, `OUTER::OUTER::` … answer as
        // terms. Its key protocol is live (see makePseudoStash); every other
        // method reaches the snapshot Hash it boxes.
        {
            auto ps = std::make_shared<ClassInfo>();
            ps->name = "PseudoStash";
            for (const char* mn : {"AT-KEY", "EXISTS-KEY", "BIND-KEY", "ASSIGN-KEY", "DELETE-KEY", "WHO"}) {
                Value code; code.t = VT::Code; code.setCode(makePayload<Callable>());
                code.code()->name = mn; code.code()->isMethod = true; code.code()->subAsMethod = true;
                code.code()->pkg = "PseudoStash";
                std::string m = mn;
                code.code()->builtin = [m](Interpreter& I, ValueList& a) -> Value {
                    if (a.empty()) return Value::nil();
                    Value self = a[0];
                    ValueList rest(a.begin() + 1, a.end());
                    // the invocant may arrive twice (as `self` and prepended)
                    if (!rest.empty() && rest[0].t == VT::Object && rest[0].obj() == self.obj())
                        rest.erase(rest.begin());
                    return I.pseudoStashCall(m, self, rest);
                };
                ps->methods[mn] = code;
            }
            classes_["PseudoStash"] = ps;
        }
        // CompUnit::Repository::Installation — a writable CURI (short/sources/dist/
        // resources index). rakupp already READS this layout to resolve `use`; the
        // Installation object lets zef query and INSTALL into it. Backed by a real
        // on-disk prefix held in the `prefix` attr.
        auto inst = std::make_shared<ClassInfo>();
        inst->name = "CompUnit::Repository::Installation"; inst->parent = repoRole;
        for (const char* a : {"prefix", "name", "next-repo"}) {
            ClassAttr ca; ca.name = a; ca.sigil = '$'; ca.pub = true; inst->attrs.push_back(ca);
        }
        classes_["CompUnit::Repository::Installation"] = inst;
        // the roles a repository answers to: every one is Locally (a directory
        // on this machine), an Installation is also Installable
        for (const char* rn : {"CompUnit::Repository::Installable", "CompUnit::Repository::Locally"}) {
            auto r = std::make_shared<ClassInfo>();
            r->name = rn; r->isRole = true;
            classes_[rn] = r;
        }
        for (const char* rn : {"CompUnit::Repository", "CompUnit::Repository::Installable", "CompUnit::Repository::Locally"})
            inst->doneRoles.insert(rn);
        fs->doneRoles.insert("CompUnit::Repository");
        fs->doneRoles.insert("CompUnit::Repository::Locally");
        // the registry is a bare type object; its methods are handled in methodCall.
        auto reg = std::make_shared<ClassInfo>();
        reg->name = "CompUnit::RepositoryRegistry";
        classes_["CompUnit::RepositoryRegistry"] = reg;
        // The precompilation surface Pod::Load loads a FILE's pod through:
        // a store over a prefix, a repository over the store, a dependency
        // naming the source, and `try-load` answering a handle whose `.unit`
        // carries `$=pod`. rakupp compiles the file when asked (there is no
        // bytecode to cache), so these are the shapes, with the loading done in
        // methodCall — exactly the string path Pod::Load itself takes, minus
        // the cache directory it never needs.
        auto mkShim = [&](const char* nm, std::initializer_list<const char*> attrs) {
            auto c = std::make_shared<ClassInfo>();
            c->name = nm;
            for (const char* a : attrs) { ClassAttr ca; ca.name = a; ca.sigil = '$'; ca.pub = true; c->attrs.push_back(ca); }
            classes_[nm] = c;
        };
        mkShim("CompUnit::PrecompilationStore::FileSystem", {"prefix"});
        mkShim("CompUnit::PrecompilationRepository::Default", {"store"});
        mkShim("CompUnit::PrecompilationDependency::File", {"src", "id", "spec"});
        mkShim("CompUnit::PrecompilationId", {"id"});
        mkShim("CompUnit::Handle", {"unit"});
        // one UDP datagram, from `$udp.Supply(:datagram)`: its payload and sender
        mkShim("IO::Socket::Async::Datagram", {"data", "hostname", "port"});
        // $*REPO is the head of the repo chain — an Installation over ~/.raku, which is
        // exactly the prefix rakupp resolves `use` from. Methods handled in methodCall.
        auto od = makePayload<ObjectData>(); od->cls = inst;
        od->attrs["name"] = Value::str("home");
        Value pfx = Value::str(platHomeDir() + "/.raku"); pfx.hashKind = "IO";
        od->attrs["prefix"] = pfx;
        od->attrs["\x01chain"] = Value::boolean(true);   // (one of the default chain: see the CURI arm)
        global_->define("$*REPO", Value::object(od));
    }
    // The slang language-objects ($~MAIN and friends) exist as defined Grammar
    // objects. rakupp can't mutate its own grammar through them (that's the
    // compiler-internals frontier), but they are present and introspectable.
    {
        auto slangCls = std::make_shared<ClassInfo>();
        slangCls->name = "Grammar"; slangCls->isGrammar = true;
        for (const char* nm : {"$~MAIN", "$~Quote", "$~Q", "$~Regex", "$~P5Regex"}) {
            auto od = makePayload<ObjectData>(); od->cls = slangCls;
            global_->define(nm, Value::object(od));
        }
    }
    registerBuiltins();
}

void Interpreter::keepMatchOrig(Value& m, const Value& topic) {
    if (m.t != VT::Match || !m.truthy()) return;
    if (!(topic.t == VT::Int || topic.t == VT::Num || topic.t == VT::Rat || topic.t == VT::Complex ||
          topic.t == VT::Object))
        return;
    auto orig = std::make_shared<Value>(topic);
    Value* slash = tctx_.cur ? tctx_.cur->find("$/") : nullptr;
    if (slash && slash->t == VT::Match && slash->rFrom() == m.rFrom() && slash->rTo() == m.rTo() &&
        slash->s == m.s)
        slash->pairKeyM() = orig;
    m.pairKeyM() = orig;
}

// Pads (PADS-PLAN.md). Build the slot layout for one owner body — params plus
// top-level plain `my` statements — and one for each inline statement block in
// it that declares plain `my`s of its own (Block::padLayout), and annotate the
// owner's DOMINATED variable references with (slot, layout address). Everything here is
// conservative by construction: a reference this pass cannot prove safe is
// simply not annotated and keeps today's map lookup; a construct it does not
// recognise is not descended into. The one rule that is load-bearing for
// correctness rather than speed: never descend into anything whose execution
// can be DEFERRED (closure bodies, gather/start/lazy operands) — a deferred
// body can run inside a DIFFERENT activation of this same owner (a recursion
// sibling), where the frame-identity compare would pass and the slot indexes
// the wrong call's variable. Inline statement blocks cannot cross an
// activation boundary, so they are safe to enter.
std::shared_ptr<const PadLayout> Interpreter::resolvePads(const std::vector<StmtPtr>& stmts,
                                                          const std::vector<Param>* params,
                                                          bool withSelf) {
    std::lock_guard<std::mutex> lk(padMu_);
    auto hit = padLayouts_.find(&stmts);
    if (hit != padLayouts_.end()) return hit->second;
    auto& cacheSlot = padLayouts_[&stmts]; // null until proven useful

    // (`$_`, `@_` and `%_` themselves are NOT: the topic is rebound implicitly
    // by `for`, `given`, `with`, `andthen` and the statement modifiers, which
    // this pass does not see, so a slotted `$_` parameter answered for the
    // loop's topic inside `for 16, 8 ... 0 { … $_ … }`. `$_x` is an ordinary name.)
    auto slottable = [](const std::string& n) {
        return n.size() > 1 && (n[0] == '$' || n[0] == '@' || n[0] == '%') &&
               (ascii::isalpha((unsigned char)n[1]) || (n[1] == '_' && n.size() > 2));
    };
    // A declaration this pass may own: a plain lexical `my` with none of the
    // machinery that keys per-scope side tables by name in OTHER structures
    // (shapes, dynamics, coercion types, `is default`, Set/Bag containers).
    auto slottableDecl = [&](const VarExpr* v) {
        return v->declare && v->declScope == "my" && slottable(v->name) &&
               !v->declDynamic && !v->declShape && !v->processScoped &&
               !v->pkgSymbol && !v->viaPseudoPkg && !v->declDefault &&
               v->containerIs.empty() && v->declCoerce.empty() && !v->namedBind;
    };
    // The two top-level statement shapes that declare owner-level lexicals:
    // `my $x;` / `my $x = E;` / `my ($a, $b) = E;` (plain `=` only — `:=`
    // binding aliases containers and stays on the map path).
    auto declaredVars = [&](const Stmt* s, std::vector<const VarExpr*>& out) {
        if (s->kind != NK::ExprStmt) return;
        const Expr* e = static_cast<const ExprStmt*>(s)->e.get();
        if (e && e->kind == NK::Assign) {
            auto* a = static_cast<const Assign*>(e);
            if (a->op != "=") return;
            e = a->target.get();
        }
        if (!e) return;
        if (e->kind == NK::VarExpr) {
            auto* v = static_cast<const VarExpr*>(e);
            if (slottableDecl(v)) out.push_back(v);
        }
        else if (e->kind == NK::ListExpr) {
            for (auto& it : static_cast<const ListExpr*>(e)->items)
                if (it && it->kind == NK::VarExpr) {
                    auto* v = static_cast<const VarExpr*>(it.get());
                    if (slottableDecl(v)) out.push_back(v);
                }
        }
    };

    auto layout = std::make_shared<PadLayout>();
    if (params)
        for (auto& p : *params)
            if (slottable(p.name)) {
                // (a TYPED `is copy` parameter is a typed container — Nil resets
                // it to the type and assignment is checked against it — so the
                // simple-assign lane, which knows neither, must not take it)
                const bool typedCopy = p.isCopy && p.sigil == '$' && !p.type.empty() &&
                                       !p.typeCapture && !p.coerce;
                int ps = layout->add(p.name, /*simple=*/!typedCopy);
                p.padSlot = ps;                          // TARG C2: the binder
                p.padOwner = (const void*)layout.get();  // writes slots directly
            }
    // A method's `self`: a slot no site is annotated with (reads go through
    // Env::selfSlot), so the per-call define lands in the pad, not the map.
    if (withSelf) layout->add("self");
    // The plain `my`s a statement list declares at its own level: the owner's
    // body, or an inline block's.
    auto collectDecls = [&](PadLayout& L, const std::vector<StmtPtr>& ss) {
        for (auto& s : ss) {
            std::vector<const VarExpr*> ds;
            declaredVars(s.get(), ds);
            for (auto* v : ds)
                L.add(v->name, /*simple=*/v->declType.empty() ||
                    (!ascii::isupper((unsigned char)v->declType[0]) && v->declType != "atomicint"));
            if (s->kind == NK::VarDecl) {
                auto* vd = static_cast<const VarDecl*>(s.get());
                if (vd->scope == "my")
                    for (auto& n : vd->names) if (slottable(n)) L.add(n, /*simple=*/true);
            }
        }
    };
    collectDecls(*layout, stmts);
    if (layout->names.size() > 64) return nullptr; // cacheSlot stays null
    // An owner with no slots of its own still has its inline blocks', so the
    // pass runs; only the owner's frame goes without a layout.
    const bool ownerSlots = !layout->names.empty();

    // An inline statement block that declares plain `my`s gets a pad of its
    // own (Block::padLayout), which execBlock puts on the block's scope: per
    // ENTRY, so a closure made in one loop iteration keeps that iteration's
    // variable, as with the map. A block whose `my`s overflow the mask keeps
    // the map.
    auto blockLayout = [&](Block* b) -> const PadLayout* {
        if (!b) return nullptr;
        auto L = std::make_shared<PadLayout>();
        L->inlineBlock = true;
        collectDecls(*L, b->stmts);
        if (L->names.empty() || L->names.size() > 64) return nullptr;
        b->padLayout = L;
        return L.get();
    };

    // ---- annotation: references dominated by their declaration ----
    // `active` maps a name to its slot, and the layout that slot is in, from
    // the declaration point onward; entering an inline block snapshots the
    // modification log so a shadowing inner declaration deactivates the name
    // for that subtree only.
    struct Act { int slot = -1; const PadLayout* L = nullptr; };
    std::unordered_map<std::string, Act> active;
    std::vector<std::pair<std::string, Act>> undo; // (name, previous binding; slot -1 = none)
    auto deactivate = [&](const std::string& n) {
        auto it = active.find(n);
        if (it == active.end()) return;
        undo.emplace_back(n, it->second);
        active.erase(it);
    };
    auto activate = [&](const std::string& n, int slot, const PadLayout* L) {
        auto it = active.find(n);
        undo.emplace_back(n, it == active.end() ? Act{} : it->second);
        active[n] = Act{slot, L};
    };
    auto popTo = [&](size_t mark) {
        while (undo.size() > mark) {
            auto& [n, old] = undo.back();
            if (old.slot < 0) active.erase(n);
            else active[n] = old;
            undo.pop_back();
        }
    };
    if (params)
        for (auto& p : *params) {
            auto it = layout->byName.find(p.name);
            if (it != layout->byName.end()) active[p.name] = Act{it->second, layout.get()};
        }

    // Unary operators whose operand runs immediately, in this frame. Anything
    // not listed (gather, start, lazy, supply, react, …) is not entered.
    static const std::set<std::string> kNowUnary = {
        "-", "+", "!", "?", "~", "not", "so", "++", "--", "^", "|",
        "ctx$", "ctx@", "ctx%", "ctx%{}", "item", "decont", "?^", "+^", "~^"};

    auto annE = [&](auto&& self, const Expr* e) -> void {
        if (!e) return;
        switch (e->kind) {
            case NK::VarExpr: {
                auto* v = const_cast<VarExpr*>(static_cast<const VarExpr*>(e));
                if (!v->declare && !v->viaPseudoPkg && !v->pkgSymbol && !v->processScoped) {
                    auto it = active.find(v->name);
                    if (it != active.end()) {
                        v->padSlot = it->second.slot;
                        v->padOwner = (const void*)it->second.L;
                    }
                }
                else if (v->declare) {
                    // any inner declaration shadows the owner slot for the rest
                    // of the current scope, whatever its declarator
                    deactivate(v->name);
                }
                if (v->declDefault) self(self, v->declDefault.get());
                break;
            }
            case NK::Assign: { auto* a = static_cast<const Assign*>(e); self(self, a->value.get()); self(self, a->target.get()); break; }
            case NK::Binary: { auto* b = static_cast<const Binary*>(e); self(self, b->lhs.get()); self(self, b->rhs.get()); break; }
            case NK::ChainExpr: for (auto& o : static_cast<const ChainExpr*>(e)->operands) self(self, o.get()); break;
            case NK::Unary: { auto* u = static_cast<const Unary*>(e);
                if (kNowUnary.count(u->op)) self(self, u->operand.get());
                break; }
            case NK::Call: { auto* c = static_cast<const Call*>(e); self(self, c->callee.get()); for (auto& x : c->args) self(self, x.get()); break; }
            case NK::MethodCall: { auto* m = static_cast<const MethodCall*>(e); self(self, m->inv.get()); for (auto& x : m->args) self(self, x.get()); break; }
            case NK::Index: { auto* i = static_cast<const Index*>(e); self(self, i->base.get()); self(self, i->index.get()); break; }
            case NK::Ternary: { auto* t = static_cast<const Ternary*>(e); self(self, t->cond.get()); self(self, t->then.get()); self(self, t->els.get()); break; }
            case NK::ListExpr: for (auto& x : static_cast<const ListExpr*>(e)->items) self(self, x.get()); break;
            case NK::ArrayLit: for (auto& x : static_cast<const ArrayLit*>(e)->items) self(self, x.get()); break;
            case NK::HashLit: for (auto& x : static_cast<const HashLit*>(e)->items) self(self, x.get()); break;
            case NK::InterpStr: for (auto& x : static_cast<const InterpStr*>(e)->parts) self(self, x.get()); break;
            case NK::NqpOp: for (auto& x : static_cast<const NqpOp*>(e)->args) self(self, x.get()); break;
            case NK::Range: { auto* r = static_cast<const RangeExpr*>(e); self(self, r->from.get()); self(self, r->to.get()); break; }
            case NK::Pair: { auto* p = static_cast<const PairExpr*>(e); self(self, p->keyExpr.get()); self(self, p->value.get()); break; }
            default: break; // BlockExpr, RegexLit, SymbolicRef, … — never entered
        }
    };

    // `lvl` is the layout this statement list's own `my`s go into: the owner's
    // at the top, an inline block's own further in, or none (a block with no
    // pad, or one that runs in the scope around it — a statement modifier's
    // body, a CATCH — whose `my`s stay on the map, unannotated).
    auto annStmts = [&](auto&& self, const std::vector<StmtPtr>& ss, const PadLayout* lvl) -> void {
        // an inline body that enters a scope of its own, with its own pad
        auto body = [&](Block* b) { self(self, b->stmts, blockLayout(b)); };
        for (auto& sp : ss) {
            Stmt* s = sp.get();
            if (!s) continue;
            switch (s->kind) {
                case NK::ExprStmt: {
                    std::vector<const VarExpr*> ds;
                    if (lvl) declaredVars(s, ds);
                    // RHS first — `my $x = $x + 1` reads the OUTER $x
                    annE(annE, static_cast<const ExprStmt*>(s)->e.get());
                    for (auto* dv : ds) {
                        auto it = lvl->byName.find(dv->name);
                        if (it == lvl->byName.end()) continue;
                        activate(dv->name, it->second, lvl);
                        // the declaration site itself runs slot-direct
                        auto* v = const_cast<VarExpr*>(dv);
                        v->padSlot = it->second;
                        v->padOwner = (const void*)lvl;
                    }
                    break;
                }
                case NK::VarDecl: {
                    auto* vd = static_cast<const VarDecl*>(s);
                    annE(annE, vd->init.get());
                    if (lvl && vd->scope == "my")
                        for (auto& n : vd->names) {
                            auto it = lvl->byName.find(n);
                            if (it != lvl->byName.end()) activate(n, it->second, lvl);
                        }
                    else
                        for (auto& n : vd->names) deactivate(n);
                    break;
                }
                case NK::Block: {
                    auto* b = static_cast<Block*>(s);
                    size_t mark = undo.size();
                    // a CATCH/CONTROL body runs in the scope that threw
                    if (b->isCatch) self(self, b->stmts, nullptr);
                    else body(b);
                    popTo(mark);
                    break;
                }
                case NK::IfStmt: {
                    auto* is = static_cast<IfStmt*>(s);
                    for (size_t i = 0; i < is->branches.size(); i++) {
                        annE(annE, is->branches[i].first.get());
                        if (!is->branches[i].second) continue;
                        size_t mark = undo.size();
                        deactivate(i == 0 ? is->thenVar
                                          : (i < is->branchVars.size() ? is->branchVars[i] : std::string()));
                        if (i < is->branchParams.size())
                            for (auto& p : is->branchParams[i]) deactivate(p.name);
                        if (is->modifier) self(self, is->branches[i].second->stmts, nullptr);
                        else body(is->branches[i].second.get());
                        popTo(mark);
                    }
                    if (is->elseBlock) {
                        size_t mark = undo.size();
                        deactivate(is->elseVar);
                        for (auto& p : is->elseParams) deactivate(p.name);
                        if (is->modifier) self(self, is->elseBlock->stmts, nullptr);
                        else body(is->elseBlock.get());
                        popTo(mark);
                    }
                    break;
                }
                case NK::WhileStmt: {
                    auto* w = static_cast<WhileStmt*>(s);
                    annE(annE, w->cond.get());
                    if (w->body) {
                        size_t mark = undo.size();
                        deactivate(w->var);
                        for (auto& p : w->params) deactivate(p.name);
                        if (w->modifier) self(self, w->body->stmts, nullptr);
                        else body(w->body.get());
                        popTo(mark);
                    }
                    break;
                }
                case NK::ForStmt: {
                    auto* f = static_cast<ForStmt*>(s);
                    annE(annE, f->list.get());
                    if (f->body) {
                        size_t mark = undo.size();
                        for (auto& n : f->vars) deactivate(n);
                        for (auto& p : f->params) deactivate(p.name);
                        if (f->modifier) self(self, f->body->stmts, nullptr);
                        else body(f->body.get());
                        popTo(mark);
                    }
                    break;
                }
                case NK::LoopStmt: {
                    auto* l = static_cast<LoopStmt*>(s);
                    size_t mark = undo.size();
                    // `loop (my $i = 0; …)` declares into the loop scope; the
                    // declare VarExpr shadows via annE's deactivate
                    annE(annE, l->init.get());
                    annE(annE, l->cond.get());
                    annE(annE, l->incr.get());
                    if (l->body) body(l->body.get());
                    popTo(mark);
                    break;
                }
                case NK::GivenStmt: {
                    auto* g = static_cast<GivenStmt*>(s);
                    annE(annE, g->topic.get());
                    if (g->body) {
                        size_t mark = undo.size();
                        deactivate(g->var);
                        for (auto& p : g->params) deactivate(p.name);
                        if (g->modifier) self(self, g->body->stmts, nullptr);
                        else body(g->body.get());
                        popTo(mark);
                    }
                    if (g->elseBody) {
                        size_t mark = undo.size();
                        deactivate(g->elseVar);
                        for (auto& p : g->elseParams) deactivate(p.name);
                        if (g->modifier) self(self, g->elseBody->stmts, nullptr);
                        else body(g->elseBody.get());
                        popTo(mark);
                    }
                    break;
                }
                case NK::WhenStmt: {
                    auto* w = static_cast<WhenStmt*>(s);
                    annE(annE, w->cond.get());
                    if (w->body) {
                        size_t mark = undo.size();
                        body(w->body.get());
                        popTo(mark);
                    }
                    break;
                }
                case NK::RepeatStmt: {
                    auto* r = static_cast<RepeatStmt*>(s);
                    if (r->body) {
                        size_t mark = undo.size();
                        body(r->body.get());
                        popTo(mark);
                    }
                    annE(annE, r->cond.get());
                    break;
                }
                case NK::ReturnStmt:
                    annE(annE, static_cast<const ReturnStmt*>(s)->value.get());
                    break;
                default: break; // SubDecl, ClassDecl, Use, … — other owners or no refs
            }
        }
    };
    annStmts(annStmts, stmts, ownerSlots ? layout.get() : nullptr);

    if (!ownerSlots) return nullptr; // cacheSlot stays null: the frame carries no layout
    cacheSlot = layout;
    return layout;
}

// Create a type whose declaration is further down the file, the moment its name
// is actually used. Answers false when there is no such pending declaration.
// ---- per-unit package stashes (see Interpreter.h) ----
std::string Interpreter::stashUnitHere() {
    if (stashUnitOverride_) return *stashUnitOverride_;
    for (Env* e = tctx_.cur.get(); e; e = e->parent.get())
        if (e->unitFrame) {
            auto it = unitOfEnv_.find(e);
            if (it != unitOfEnv_.end()) return it->second;
        }
    return "";
}

static std::vector<std::string> stashParts(const std::string& fq) {
    std::vector<std::string> parts;
    size_t b = 0;
    for (;;) {
        size_t c = fq.find("::", b);
        parts.push_back(fq.substr(b, c == std::string::npos ? std::string::npos : c - b));
        if (c == std::string::npos) break;
        b = c + 2;
    }
    return parts;
}

// `class A::B::C` in unit U: A resolves in U's own view (a package U imported
// is the SAME node the exporting unit holds, so the declaration shows there
// too), B and C are made under it as needed, and A becomes one of U's
// package-scoped names.
void Interpreter::stashDeclare(const std::string& fq, bool stubPkg) {
    if (fq.empty() || fq.find('\x01') != std::string::npos) return;
    std::vector<std::string> parts = stashParts(fq);
    if (parts.empty() || parts[0].empty()) return;
    const std::string& p1 = parts[0];
    std::unique_lock<std::mutex> kl(sharedMut_, std::defer_lock);
    if (parallelMode_) kl.lock();
    auto newNode = [&](const std::string& name, bool stub) {
        stashNodes_.emplace_back();
        StashNode* n = &stashNodes_.back();
        n->fq = name; n->stub = stub;
        return n;
    };
    UnitStash& U = unitStash_[stashUnitHere()];
    StashNode*& slot = U.lexical[p1];
    const bool single = parts.size() == 1;
    if (!slot) slot = newNode(p1, !single || stubPkg);
    else if (single && !stubPkg) slot->stub = false;
    U.globalish[p1] = slot;
    stashTracked_.insert(p1);
    StashNode* n = slot;
    std::string acc = p1;
    for (size_t i = 1; i < parts.size(); i++) {
        acc += "::" + parts[i];
        const bool last = i + 1 == parts.size();
        StashNode*& k = n->kids[parts[i]];
        if (!k) k = newNode(acc, !last || stubPkg);
        else if (last && !stubPkg) k->stub = false;
        stashTracked_.insert(acc);
        n = k;
    }
}

// Package nodes merge by CONTENT: a stub (an implied package, or `package
// P {}`) takes the other's entries, a real one is kept, and entries that are
// already the same node are left alone.
void Interpreter::stashMergeNodes(StashNode* t, StashNode* s, std::set<StashNode*>& seen) {
    if (!t || !s || t == s || !seen.insert(t).second) return;
    for (auto& kv : s->kids) {
        auto it = t->kids.find(kv.first);
        if (it == t->kids.end()) t->kids[kv.first] = kv.second;
        else if (it->second != kv.second && (kv.second->stub || it->second->stub))
            stashMergeNodes(it->second, kv.second, seen);
    }
}

// `use M` in unit I: M's package-scoped names join I's view.
void Interpreter::stashImport(const std::string& importer, const std::string& mod) {
    if (importer == mod) return;
    std::unique_lock<std::mutex> kl(sharedMut_, std::defer_lock);
    if (parallelMode_) kl.lock();
    auto mit = unitStash_.find(mod);
    if (mit == unitStash_.end()) return;
    std::vector<std::pair<std::string, StashNode*>> names(mit->second.globalish.begin(), mit->second.globalish.end());
    UnitStash& I = unitStash_[importer];
    for (auto& [name, src] : names) {
        StashNode*& tgt = I.lexical[name];
        if (!tgt) { tgt = src; continue; }
        if (tgt == src) continue;
        std::set<StashNode*> seen;
        if (src->stub || !tgt->stub) stashMergeNodes(tgt, src, seen);
        else { stashMergeNodes(src, tgt, seen); tgt = src; }
    }
}

Interpreter::StashNode* Interpreter::stashResolve(const std::string& unit, const std::string& pkg) {
    auto uit = unitStash_.find(unit);
    if (uit == unitStash_.end()) return nullptr;
    std::vector<std::string> parts = stashParts(pkg);
    auto lit = uit->second.lexical.find(parts[0]);
    if (lit == uit->second.lexical.end() || !lit->second) return nullptr;
    StashNode* n = lit->second;
    for (size_t i = 1; i < parts.size() && n; i++) {
        auto k = n->kids.find(parts[i]);
        n = k == n->kids.end() ? nullptr : k->second;
    }
    return n;
}

// A declaration counts as built when the type it made is the one registered
// under its name (or one of that role group's candidates), or when a hoist
// pass made it and normal flow has not reached it yet. A same-named type from
// a SIBLING scope is another declaration, and does not count.
bool Interpreter::typeDeclBuilt(const ClassDecl* cd) {
    if (hoistedTypes_.count(cd)) return true;
    auto it = classes_.find(cd->name);
    if (it == classes_.end() || !it->second) return false;
    if (it->second->decl == cd) return true;
    for (auto& v : it->second->roleVariants)
        if (v && v->decl == cd) return true;
    return false;
}

void Interpreter::notePendingType(ClassDecl* cd) {
    if (typeDeclBuilt(cd)) return;
    auto& v = pendingTypes_[cd->name];
    if (std::find(v.begin(), v.end(), cd) == v.end()) v.push_back(cd);
    pendingTypeUnit_[cd] = stashUnitHere();   // built early, it is still ITS unit's declaration
}

bool Interpreter::materializePendingType(const std::string& name) {
    auto it = pendingTypes_.find(name);
    if (it == pendingTypes_.end()) return false;
    std::vector<ClassDecl*> decls = std::move(it->second);
    pendingTypes_.erase(it);
    // (a record normal flow has built since is stale: executing that
    // declaration AGAIN would re-run the body's statements)
    decls.erase(std::remove_if(decls.begin(), decls.end(),
                               [&](ClassDecl* d) { return typeDeclBuilt(d); }), decls.end());
    std::vector<ClassDecl*> real;
    for (ClassDecl* d : decls) if (!d->isStubDecl) real.push_back(d);
    if (real.empty()) return false;
    // their own parents and roles may be pending too (`class D does R` with
    // both declared below the code that uses D) — build those first
    for (ClassDecl* d : real) {
        if (!d->parent.empty()) materializePendingType(d->parent);
        for (auto& p : d->extraParents) materializePendingType(p);
        for (auto& r : d->roles) materializePendingType(r);
    }
    // A ROLE group is every declaration of the name, built in textual order
    // so each joins the one before it (`role R[::T] {…}; role R {…}`);
    // anything else is its last declaration, the completion of any stub.
    auto build = [&](ClassDecl* d) {
        struct UnitOverride {
            Interpreter& I; std::optional<std::string> saved;
            ~UnitOverride() { I.stashUnitOverride_ = saved; }
        } unitOverride{*this, stashUnitOverride_};
        auto pu = pendingTypeUnit_.find(d);
        if (pu != pendingTypeUnit_.end()) stashUnitOverride_ = pu->second;
        exec(d);
        hoistedTypes_[d]++;   // …so reaching it in normal flow does not rebuild it
    };
    if (real.back()->isRole) { for (ClassDecl* d : real) build(d); }
    else build(real.back());
    return classes_.count(name) > 0;
}
void collectMentionedE(const Expr* e, std::set<std::string>& out) {
    if (!e) return;
    auto each = [&](const std::vector<ExprPtr>& v) { for (auto& x : v) collectMentionedE(x.get(), out); };
    switch (e->kind) {
        case NK::VarExpr: {
            auto* v = static_cast<const VarExpr*>(e);
            if (!v->declare && !v->name.empty()) out.insert(v->name);
            collectMentionedE(v->declDefault.get(), out);
            collectMentionedE(v->declShape.get(), out);
            break;
        }
        case NK::ListExpr:  each(static_cast<const ListExpr*>(e)->items); break;
        case NK::ArrayLit:  each(static_cast<const ArrayLit*>(e)->items); break;
        case NK::HashLit:   each(static_cast<const HashLit*>(e)->items); break;
        case NK::InterpStr: each(static_cast<const InterpStr*>(e)->parts); break;
        case NK::NqpOp:     each(static_cast<const NqpOp*>(e)->args); break;
        case NK::ChainExpr: each(static_cast<const ChainExpr*>(e)->operands); break;
        case NK::Assign: { auto* a = static_cast<const Assign*>(e);
            collectMentionedE(a->target.get(), out); collectMentionedE(a->value.get(), out); break; }
        case NK::Binary: { auto* b = static_cast<const Binary*>(e);
            collectMentionedE(b->lhs.get(), out); collectMentionedE(b->rhs.get(), out); break; }
        case NK::Unary: collectMentionedE(static_cast<const Unary*>(e)->operand.get(), out); break;
        case NK::Call: { auto* c = static_cast<const Call*>(e);
            collectMentionedE(c->callee.get(), out); each(c->args); break; }
        case NK::MethodCall: { auto* m = static_cast<const MethodCall*>(e);
            // a QUALIFIED private call is noted too, under a name no variable
            // can have (`\x02Pkg::meth`) — the trust check at class declaration
            if (m->bang && !m->methodExpr && m->method.find("::") != std::string::npos)
                out.insert("\x02" + m->method);
            collectMentionedE(m->inv.get(), out); collectMentionedE(m->methodExpr.get(), out);
            each(m->args); break; }
        case NK::Index: { auto* i = static_cast<const Index*>(e);
            collectMentionedE(i->base.get(), out); collectMentionedE(i->index.get(), out); break; }
        case NK::Ternary: { auto* t = static_cast<const Ternary*>(e);
            collectMentionedE(t->cond.get(), out); collectMentionedE(t->then.get(), out);
            collectMentionedE(t->els.get(), out); break; }
        case NK::Range: { auto* r = static_cast<const RangeExpr*>(e);
            collectMentionedE(r->from.get(), out); collectMentionedE(r->to.get(), out); break; }
        case NK::Pair: { auto* p = static_cast<const PairExpr*>(e);
            collectMentionedE(p->keyExpr.get(), out); collectMentionedE(p->value.get(), out); break; }
        case NK::BlockExpr: for (auto& st : static_cast<const BlockExpr*>(e)->body)
                                collectMentionedS(st.get(), out);
                            break;
        default: break;   // literals, regexes (their interpolations are text here), symbolic refs
    }
}
void collectMentionedS(const Stmt* s, std::set<std::string>& out) {
    if (!s) return;
    switch (s->kind) {
        case NK::ExprStmt: collectMentionedE(static_cast<const ExprStmt*>(s)->e.get(), out); break;
        case NK::ReturnStmt: collectMentionedE(static_cast<const ReturnStmt*>(s)->value.get(), out); break;
        case NK::Block: collectMentionedB(static_cast<const Block*>(s), out); break;
        case NK::SubDecl: for (auto& st : static_cast<const SubDecl*>(s)->body)
                              collectMentionedS(st.get(), out);
                          break;
        case NK::IfStmt: { auto* i = static_cast<const IfStmt*>(s);
            for (auto& br : i->branches) { collectMentionedE(br.first.get(), out);
                                           collectMentionedB(br.second.get(), out); }
            collectMentionedB(i->elseBlock.get(), out); break; }
        case NK::WhileStmt: { auto* w = static_cast<const WhileStmt*>(s);
            collectMentionedE(w->cond.get(), out); collectMentionedB(w->body.get(), out); break; }
        // its own struct, with `isUntil` BEFORE `body`: read as a WhileStmt the
        // flag was taken for the body pointer (a crash for `repeat … until`, a
        // skipped body for `repeat … while`)
        case NK::RepeatStmt: { auto* r = static_cast<const RepeatStmt*>(s);
            collectMentionedE(r->cond.get(), out); collectMentionedB(r->body.get(), out); break; }
        case NK::ForStmt: { auto* f = static_cast<const ForStmt*>(s);
            collectMentionedE(f->list.get(), out); collectMentionedB(f->body.get(), out); break; }
        case NK::LoopStmt: { auto* l = static_cast<const LoopStmt*>(s);
            collectMentionedE(l->init.get(), out); collectMentionedE(l->cond.get(), out);
            collectMentionedE(l->incr.get(), out); collectMentionedB(l->body.get(), out); break; }
        case NK::GivenStmt: { auto* g = static_cast<const GivenStmt*>(s);
            collectMentionedE(g->topic.get(), out); collectMentionedB(g->body.get(), out);
            collectMentionedB(g->elseBody.get(), out); break; }
        case NK::WhenStmt: { auto* w = static_cast<const WhenStmt*>(s);
            collectMentionedE(w->cond.get(), out); collectMentionedB(w->body.get(), out); break; }
        default: break;
    }
}

void Interpreter::predeclareSubClosures(Block* b, Env* env) {
    if (b->subClosureDecls == 0) return;             // decided already: nothing here
    // The `my` declarations at THIS block's statement level, read exactly as the
    // mainline's pre-declare reads its own: a bare declarator, the target of an
    // initialising assignment, or a list of either (`my ($a, $b) = …`).
    std::vector<const VarExpr*> decls;
    auto one = [&](const Expr* x) {
        if (!x || x->kind != NK::VarExpr) return;
        auto* ve = static_cast<const VarExpr*>(x);
        if (ve->declare && ve->declScope == "my" && !ve->name.empty()) decls.push_back(ve);
    };
    for (auto& s : b->stmts) {
        if (s->kind != NK::ExprStmt) continue;
        const Expr* e = static_cast<const ExprStmt*>(s.get())->e.get();
        if (e && e->kind == NK::Assign) e = static_cast<const Assign*>(e)->target.get();
        if (e && e->kind == NK::ListExpr)
            for (auto& it : static_cast<const ListExpr*>(e)->items) one(it.get());
        else one(e);
    }
    if (decls.empty()) { b->subClosureDecls = 0; return; }
    std::set<std::string> mentioned;
    for (auto& s : b->stmts) {
        if (s->kind != NK::SubDecl) continue;
        auto* sd = static_cast<SubDecl*>(s.get());
        if (sd->isMethod || sd->name.empty()) continue;
        for (auto& st : sd->body) collectMentionedS(st.get(), mentioned);
    }
    bool any = false;
    for (const VarExpr* ve : decls) {
        if (!mentioned.count(ve->name)) continue;
        // The three shapes the mainline's pre-declare also leaves to the
        // declaration itself, and for its reasons: a container trait decides
        // what the variable IS (`my %h is Set`) and a plain Hash put here first
        // would leave the trait nothing to replace; a parameterized declared
        // type cannot be evaluated this early; a shaped array needs its dims.
        if (!ve->containerIs.empty() && (ve->name[0] == '%' || ve->name[0] == '@')) continue;
        if (ve->declTypeExpr || ve->declShape) continue;
        any = true;
        if (env->local(ve->name)) continue;
        // A declared type that names nothing is the DECLARATION's error to
        // raise, at its own line — not this pass's, at the top of the block.
        try { env->define(ve->name, declInitial(ve, ve->name[0])); }
        catch (RakuError&) {}
    }
    b->subClosureDecls = any ? 1 : 0;
}

// An `our sub` inside a nested bare block is installed in its package at
// COMPILE time, so `&OUR::f()` answers before the block has ever run —
// closing over the block's lexicals as they are before it runs (undefined).
// When the block does run, the declaration installs the real closure over the
// top of this one. Only bare blocks are looked into: a routine or a loop body
// is its own scope with its own entry.
void Interpreter::preinstallNestedOurSubs(const std::vector<StmtPtr>& stmts) {
    for (auto& s : stmts) {
        if (s->kind != NK::Block) continue;
        auto* b = static_cast<Block*>(s.get());
        bool has = false;
        for (auto& st : b->stmts)
            if (st->kind == NK::SubDecl) {
                auto* sd = static_cast<SubDecl*>(st.get());
                if (sd->isOur && !sd->isMethod && !sd->isMulti && !sd->isProto && !sd->name.empty()) has = true;
            }
        if (!has) { preinstallNestedOurSubs(b->stmts); continue; }
        if (!curPkgEnv_) continue;
        auto sc = std::make_shared<Env>(); sc->parent = tctx_.cur;
        predeclareSubClosures(b, sc.get());
        auto saved = tctx_.cur;
        tctx_.cur = sc;
        try {
            for (auto& st : b->stmts)
                if (st->kind == NK::SubDecl) {
                    auto* sd = static_cast<SubDecl*>(st.get());
                    if (sd->isOur && !sd->isMethod && !sd->isMulti && !sd->isProto && !sd->name.empty() &&
                        !curPkgEnv_->local("&" + sd->name))
                        exec(st.get());
                }
        } catch (...) { tctx_.cur = saved; throw; }
        tctx_.cur = saved;
        preinstallNestedOurSubs(b->stmts);
    }
}

// Run a hoisted sub's user `is` traits at its textual position (the sub itself
// was registered at scope entry; the trait handler may use `my`s declared above).
// A parameter's own user traits: `sub f(:$foo is option("=s%"))` calls
// trait_mod:<is>(Parameter, :option("=s%")). The Parameter meta-object comes out
// of the signature itself and is cached on the Param, so whatever the trait
// mixes into it is what every later `.signature.params` answers with — which is
// how Getopt::Long reads an option's spec back off a sub it was handed.
void Interpreter::applyParamTraits(const std::vector<Param>& params, const Value& fn) {
    bool any = false;
    for (auto& pp : params) if (!pp.userTraits.empty()) { any = true; break; }
    if (!any || fn.t != VT::Code || !fn.code()) return;
    Value* tm = tctx_.cur->find("&trait_mod:<is>");
    if (!tm || tm->t != VT::Code) {
        // with no trait_mod:<is> in scope at all, a trait no built-in knows is
        // a compile error: `sub f($x is nonesuch)`
        static const std::set<std::string> kKnown = {
            "readonly", "item", "dynamic", "encoded", "pure", "default", "DEPRECATED",
            "leading_docs", "trailing_docs", "export", "hidden-from-backtrace", "optional"};
        for (auto& pp : params)
            for (auto& ut : pp.userTraits)
                if (!kKnown.count(ut.first))
                    throwTypedV("X::Comp::Trait::Unknown",
                        {{"type", Value::str("is")}, {"subtype", Value::str(ut.first)},
                         {"declaring", Value::str(" parameter")}},
                        "Can't use unknown trait 'is' -> '" + ut.first + "' in a parameter declaration.");
        return;
    }
    // …from the candidate whose signature IS this declaration's: a multi's name
    // answers with the dispatcher, and the dispatcher's own params are the
    // proto's, not the candidate's.
    const Callable* c = fn.code();
    if (c->params != &params)
        for (auto& cand : c->candidates)
            if (cand.t == VT::Code && cand.code() && cand.code()->params == &params) {
                c = cand.code(); break;
            }
    if (c->params != &params) return;
    Value sig = makeSignature(c);
    if (!sig.hash()) return;
    Value& pl = (*sig.hash())["params"];
    if (pl.t != VT::Array || !pl.arr()) return;
    size_t i = 0;
    for (auto& pp : params) {
        if (i >= pl.arr()->size()) break;
        Value pm = (*pl.arr())[i++];
        for (auto& ut : pp.userTraits) {
            Value tv = ut.second ? eval(ut.second.get()) : Value::boolean(true);
            Value pr = Value::pair(ut.first, tv); pr.namedArg = true;
            ValueList ta; ta.push_back(pm); ta.push_back(pr);
            try { callCallable(*tm, ta); }
            catch (RakuError& te) {
                // no candidate = not a user trait at all; anything else is the
                // trait body's own error and belongs to the program
                if (te.message.rfind("Cannot resolve caller", 0) != 0) throw;
            }
        }
    }
}

// One `is NAME` / `is NAME(arg)` on a routine, handed to the user's
// trait_mod:<is>. A NAME that is a TYPE (`is description(…)` with a `role
// description`) arrives as that type object, positionally, with the argument
// after it; any other name arrives as the named argument `:NAME(arg)`.
void Interpreter::callRoutineTrait(const Value& tm, const Value& code, const SubTraitSpec& st) {
    if (st.name == "default" && !st.arg) return;   // `multi … is default`: a dispatch tie-break
    Value arg = st.arg ? eval(st.arg.get()) : Value::boolean(true);
    auto cit = classes_.find(st.name);
    if (cit != classes_.end() && cit->second) {
        ValueList ta{code, Value::typeObj(st.name)};
        if (st.arg) ta.push_back(arg);
        try { callCallable(tm, ta); return; }
        catch (RakuError&) {} // no positional candidate: try the named form
    }
    Value p = Value::pair(st.name, arg); p.namedArg = true;
    ValueList ta; ta.push_back(code); ta.push_back(p);
    try { callCallable(tm, ta); }
    catch (RakuError& te) {
        // no candidate takes it: not a trait anyone declared — an error where
        // that is a certainty (the attribute arm's rule); the trait body's own
        // error propagates as it always should have
        if (te.message.rfind("Cannot resolve caller", 0) != 0) throw;
        if (routineTraitsCertain() && !routineTraitKnown(st.name)) unknownRoutineTrait(st.name, code);
    }
}

// A USER trait on a variable, `my $a is noted` / `my $dog is doc('barks')`:
// `trait_mod:<is>(Variable, :noted)`, run once the variable exists (the parser
// emits the call after the declaration). The Variable answers `.name` and, in
// `.var`, a placeholder for the container; what the trait mixes into that
// (`$v.var.VAR does Doc[$doc]`) is then put on the variable. A mixed-in Scalar
// is no container any more, so a `$` variable holds that object, as in Rakudo;
// an `@`/`%` one keeps its elements in a container of the mixed type. A trait
// no candidate takes is left alone: the same word may be a type the parser
// could not tell apart (`my @a is buf` in an EVAL). The call answers the
// variable's value, as the declaration it follows did, so a block ending in
// the declaration still ends in its value (`EVAL 'my @a is buf = ^10'`).
Value Interpreter::applyVarTrait(const std::string& var, const std::string& trait, const Value* arg) {
    auto valueNow = [&]() -> Value {
        Value* v = tctx_.cur && !var.empty() ? tctx_.cur->find(var) : nullptr;
        return v ? *v : Value::nil();
    };
    Value* tm = tctx_.cur ? tctx_.cur->find("&trait_mod:<is>") : nullptr;
    if (!tm || tm->t != VT::Code || var.empty()) return valueNow();
    Value vm = Value::makeHash();
    vm.hashKind = "Variable";
    (*vm.hash())["name"] = Value::str(var);
    Value p = Value::pair(trait, arg ? *arg : Value::boolean(true));
    p.namedArg = true;
    try { callCallable(*tm, ValueList{vm, p}); }
    catch (RakuError& te) {
        if (te.message.rfind("Cannot resolve caller", 0) != 0) throw;
        return valueNow();
    }
    auto ci = vm.hash()->find(ATTR_CONTAINER_KEY);
    if (ci == vm.hash()->end() || ci->second.t != VT::Object || !ci->second.obj() || !ci->second.obj()->cls ||
        ci->second.obj()->cls->name.find("+{") == std::string::npos)
        return valueNow();
    Value* slot = tctx_.cur->find(var);
    if (!slot) return Value::nil();
    if (var[0] == '$') { *slot = ci->second; return *slot; }
    if (slot->t != VT::Array && slot->t != VT::Hash) return *slot;
    auto mixed = makePayload<ObjectData>();
    mixed->cls = ci->second.obj()->cls;
    mixed->attrs = ci->second.obj()->attrs;
    mixed->boxed() = *slot;
    mixed->hasBoxed = true;
    Value nv; nv.t = VT::Object; nv.setObj(mixed);
    *slot = nv;
    return nv;
}

// A unit that imports nothing and whose declarations the parser could list: a
// trait no `trait_mod:<is>` answers cannot come from anywhere else
bool Interpreter::routineTraitsCertain() {
    const Program* u = unitCurrent();
    return u && !u->importsModules && !u->typeNamesOpaque;
}
// A trait NAME the language itself defines (or a type, for the positional form)
// is never "unknown", whether or not this engine models what it does
bool Interpreter::routineTraitKnown(const std::string& n) {
    static const std::set<std::string> kBuiltin = {
        "default", "hidden-from-USAGE", "cached", "pure", "nodal", "export", "rw", "raw", "copy",
        "readonly", "dynamic", "required", "built", "DEPRECATED", "test-assertion",
        "hidden-from-backtrace", "implementation-detail", "specialized", "context", "parsed",
        "reparsed", "controlled", "cloned", "item", "revision-gated", "tighter", "looser", "equiv",
        "assoc", "native", "symbol", "nativeconv", "encoded", "rakudo", "repr", "box_target",
        "leading_docs", "trailing_docs"};
    return kBuiltin.count(n) || classes_.count(n) || isKnownTypeName(n);
}
void Interpreter::unknownRoutineTrait(const std::string& name, const Value& code) {
    const std::string decl = code.t == VT::Code && code.code() && code.code()->isMethod
                           ? (code.code()->isSubmethod ? "submethod" : "method") : "sub";
    throwTypedV("X::Comp::Trait::Unknown",
        {{"type", Value::str("is")}, {"subtype", Value::str(name)}, {"declaring", Value::str(decl)}},
        "Can't use unknown trait 'is' -> '" + name + "' in " + decl + " declaration.");
}

void Interpreter::applySubTraits(SubDecl* sd) {
    Value* fn = tctx_.cur->find("&" + sd->name);
    if (!fn) return;
    applyParamTraits(sd->params, *fn);
    if (sd->traits.empty()) return;
    Value* tm = tctx_.cur->find("&trait_mod:<is>");
    if (!tm || tm->t != VT::Code) {
        // no `trait_mod:<is>` at all: a trait that names no type is unknown
        if (routineTraitsCertain())
            for (auto& st : sd->traits)
                if (!routineTraitKnown(st.name))
                    unknownRoutineTrait(st.name, *fn);
        return;
    }
    for (auto& st : sd->traits) callRoutineTrait(*tm, *fn, st);
}

// A named sub hoisted into a scope closes over that scope's Env and is stored back
// into it, forming a shared_ptr cycle (Env→Value→Callable→closure→Env, plus a
// second edge via stateEnv->parent) that neither side can free — the frame leaks.
// When the frame exits, break the back-edges of any Code that closes over THIS env
// and is referenced nowhere else (use_count 1 ⇒ it did not escape via return or
// assignment). A torn-down non-escaped frame has no surviving `state` to preserve.
void Interpreter::breakSelfClosures(const std::shared_ptr<Env>& envp) {
    // Wiring an on-demand supply block: its env OUTLIVES the frame (whenever
    // taps fire later, from I/O workers) — a nested `my sub`'s closure must
    // survive, so the frame-death heuristic is suspended.
    if (noCycleBreak_ > 0) return;
    Env* env = envp.get();
    if (!env) return;
    // Count what the frame's own nested subs hold before cutting anything. Each
    // self-closured sub is one reference back to this frame, and so is a `state`
    // env parked on it; anything LEFT once those are subtracted — beyond the one
    // reference this call was handed — means the frame ESCAPED. A closure
    // returned from here still reaches these subs BY NAME, and cutting their
    // closure strands their lexicals: Terminal::ANSIParser hands back a table of
    // blocks that call nested `my sub`s sharing a `$sequence` buffer, and every
    // one of them died with "Variable '$sequence' is not declared" the moment
    // the parser was used outside the sub that built it.
    //
    // forEachVar: a nested `my &f = sub {…}` at owner level lives in the pad.
    size_t held = 0;
    env->forEachVar([&](const std::string&, Value& v) {
        if (v.t != VT::Code || !v.code() || !v.payloadUnique()) return;
        if (v.code()->closure.get() == env) held++;
        if (v.code()->state.env.get() == env) held++;
    });
    if (!held) return;
    if ((size_t)envp.use_count() > held + 1) return;   // the frame outlives this scope
    env->forEachVar([&](const std::string&, Value& v) {
        if (v.t == VT::Code && v.code() && v.payloadUnique() &&
            v.code()->closure.get() == env) {
            v.code()->closure.reset();
            v.code()->state.env.reset();
        }
    });
}

// Move the live execution registers into `c` (used when a thread parks) …
void Interpreter::saveCtx(ExecContext& c) {
    c.cur         = std::move(tctx_.cur);
    c.dynStack    = std::move(tctx_.dynStack);
    c.callDepth   = tctx_.callDepth;
    c.curStateEnv = tctx_.curStateEnv;
    c.gatherStack = std::move(tctx_.gatherStack);
    c.gatherLimits = std::move(tctx_.gatherLimits);
    c.gatherDeadlines = std::move(tctx_.gatherDeadlines);
    t_poll.gatherDeadline = 0;   // nothing of this thread's is probing while it is parked
    c.topicAliases = std::move(tctx_.topicAliases);
    c.supplyStack = std::move(tctx_.supplyStack);
    c.tapStack    = std::move(tctx_.tapStack);
    c.makeTargets = std::move(tctx_.makeTargets);
    c.pkgPrefix   = std::move(tctx_.pkgPrefix);
    // NOT nqpArgs/nqpDepth, deliberately. They are scratch buffers belonging to
    // the OS THREAD, not to the Raku context being parked: a suspended nqp op
    // further up this thread's C++ stack still holds a live reference into the
    // deque, and moving it out from under that reference would dangle. The
    // thread blocks while parked, so nothing else can be using them meanwhile.
}
// … and back out when it resumes (or when another thread is scheduled in).
void Interpreter::loadCtx(ExecContext& c) {
    tctx_.cur          = std::move(c.cur);
    tctx_.dynStack     = std::move(c.dynStack);
    tctx_.callDepth    = c.callDepth;
    tctx_.curStateEnv  = c.curStateEnv;
    tctx_.gatherStack  = std::move(c.gatherStack);
    tctx_.gatherLimits = std::move(c.gatherLimits);
    tctx_.gatherDeadlines = std::move(c.gatherDeadlines);
    t_poll.gatherDeadline = tctx_.gatherDeadlines.empty() ? 0 : tctx_.gatherDeadlines.back();
    tctx_.topicAliases = std::move(c.topicAliases);
    tctx_.supplyStack  = std::move(c.supplyStack);
    tctx_.tapStack     = std::move(c.tapStack);
    tctx_.makeTargets  = std::move(c.makeTargets);
    tctx_.pkgPrefix    = std::move(c.pkgPrefix);
}

// Engage the GIL the first time any asynchronous work appears. Before this the
// program is purely single-threaded and takes no locks at all. This is also the
// symbol-table freeze point: once concurrency can begin, the shared tables are
// meant to be immutable (see noteSymbolMutation / symbolsFrozen_).
void Interpreter::engageGil() {
    if (gilHeld_) return;
    gilHeld_ = true;
    symbolsFrozen_.store(true, std::memory_order_relaxed);
    if (!parallelMode_) gil_.lock();  // parallel mode never holds the GIL for compute
}

// The embed surface's GIL handoff (declared beside gil_ in the header): the
// outermost rk_* entry takes the engaged GIL on the thread that will actually
// run, and releases it on the way out. Enter cannot remember whether it
// locked — the body may be the one that first engages (engageGil locks on
// this same thread mid-body) — so leave re-derives ownership from gilHeld_:
// in both histories the current thread is the owner, and the unlock is the
// same-thread unlock the mutex contract requires.
bool Interpreter::gilMainlineEnter() {
    if (embedGilDepth_++ > 0) return false;
    if (gilHeld_ && !parallelMode_) gil_.lock();
    return true;
}
void Interpreter::gilMainlineLeave(bool outermost) {
    --embedGilDepth_;
    if (!outermost) return;
    if (gilHeld_ && !parallelMode_) gilYieldNotify(); // the documented handoff: unlock + wake yielders
}

// Tripwire wired into every structural writer of a shared symbol table. Once the
// tables are frozen (concurrency engaged), a mutation means a worker thread could
// be reading a table another thread is restructuring — the race lock-free reads
// must avoid. Off by default (no behaviour change); set RAKUPP_FREEZE_TRACE to
// have each post-freeze mutation reported to stderr with the offending thread.
std::atomic<uint64_t> g_symbolGen{0};
void Interpreter::noteSymbolMutation(const char* what) {
    // every structural change moves the generation the multi-dispatch cache
    // was filled under (dispatchCacheLookup), whatever the freeze says
    g_symbolGen.fetch_add(1, std::memory_order_relaxed);
    if (!symbolsFrozen_.load(std::memory_order_relaxed)) return;
    static const bool trace = std::getenv("RAKUPP_FREEZE_TRACE") != nullptr;
    if (!trace) return;
    bool onMain = std::this_thread::get_id() == mainThread_;
    fprintf(stderr, "[freeze] post-freeze symbol mutation: %s  (thread=%s, file=%s)\n",
            what, onMain ? "main" : "worker", srcFile_.c_str());
}

// Release the GIL and wake any thread parked in yieldToWorker. Every place a
// thread hands the GIL off (worker finish, sleepYield, awaitPromise, react wait)
// goes through here so the spawner's cooperative yield sees the progress.
void Interpreter::gilYieldNotify() {
    t_holdsGil = false;
    gil_.unlock();
    { std::lock_guard<std::mutex> lk(gilRelMutex_); ++gilReleaseCount_; }
    gilReleased_.notify_all();
}

// Called by the spawner right after starting a worker: drop the GIL and wait
// until *some* thread has made progress (acquired then released the GIL), then
// reacquire. A pure-compute worker runs to completion in that window (so its
// effects are visible immediately, like the old eager model); a worker that
// blocks (sleep/await) releases the GIL at its first block, and we resume then —
// leaving it running concurrently.
void Interpreter::yieldToWorker() {
    if (!gilHeld_ || parallelMode_) return;  // parallel workers already run concurrently
    static thread_local ExecContext parked;
    saveCtx(parked);
    long before;
    { std::lock_guard<std::mutex> lk(gilRelMutex_); before = gilReleaseCount_; }
    gil_.unlock();
    { std::unique_lock<std::mutex> lk(gilRelMutex_); gilReleased_.wait(lk, [&] { return gilReleaseCount_ > before; }); }
    gil_.lock();
    loadCtx(parked);
}

// Same handoff, but bounded — for callers that block on a condition a worker may
// never satisfy (Channel.receive on a queue nobody sends to) and must not wedge.
bool Interpreter::yieldToWorkerFor(double secs) {
    if (!gilHeld_ || parallelMode_) return false;
    static thread_local ExecContext parked;
    saveCtx(parked);
    // Release the GIL *and* notify: a counterpart parked in yieldToWorker is
    // waiting for the release counter to move, and a plain unlock would leave it
    // asleep — so the thread that could satisfy us never runs.
    gil_.unlock();
    long before;
    { std::lock_guard<std::mutex> lk(gilRelMutex_); before = ++gilReleaseCount_; }
    gilReleased_.notify_all();
    bool progressed;
    {
        std::unique_lock<std::mutex> lk(gilRelMutex_);
        progressed = gilReleased_.wait_for(lk, std::chrono::duration<double>(secs),
                                           [&] { return gilReleaseCount_ > before; });
    }
    gil_.lock();
    loadCtx(parked);
    return progressed;
}

// sleep with the GIL released, so sibling worker threads run (and sleep) at the
// same time — this is what makes concurrent-timing programs (e.g. sleep-sort)
// actually interleave. The full duration is honored: `sleep 333` sleeps 333 s
// (issue #41 — an early cap here made every mainline sleep return after 1 s).
// A runaway sleep in a TEST is the harness's problem (run-roast kills a file at
// its per-file timeout); the language must not round wall-clock time down.
void Interpreter::sleepYield(double secs) {
    if (!(secs > 0)) return;                             // zero, negative, NaN: no wait
    // A WORKER sleeps in slices against a fixed deadline, so it notices
    // shutdown within ~50 ms — one long sleep_for made it hold drainWorkers'
    // 2 s grace for the sleep's whole remainder (a GUI window's close button
    // felt seconds slow). The mainline sleeps in hour-long chunks: effectively
    // uninterrupted, but `sleep 1e19` / `sleep Inf` never feed sleep_for a
    // duration past the int64 nanosecond range (libstdc++ overflows there and
    // returns at once). The deadline arithmetic stays in double-rep time for
    // the same reason; it also keeps a sliced sleep's total duration exact.
    auto sliceUntilAbort = [&](double s) -> bool {       // true: abort observed
        const double slice = t_poll.isWorker ? 0.05 : 3600.0;
        auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(s);
        for (;;) {
            if (t_poll.isWorker && workerAbort_.load(std::memory_order_relaxed)) return true;
            auto now = std::chrono::steady_clock::now();
            if (now >= end) return false;
            double left = std::chrono::duration<double>(end - now).count();
            std::this_thread::sleep_for(std::chrono::duration<double>(left < slice ? left : slice));
        }
    };
    // In parallel mode the timer/cue/supply workers still serialize their
    // callbacks on gil_: one that sleeps must let the others tick meanwhile
    // (`$*SCHEDULER.cue: { sleep 120 }` held every Supply.interval still).
    if (parallelMode_ && t_holdsGil) {
        gilYieldNotify();
        bool aborted = sliceUntilAbort(secs);
        gilLock();
        if (aborted) throw WorkerAbortEx{};
        return;
    }
    // No GIL held (single-threaded) or parallel mode (no GIL at all): just sleep.
    if (!gilHeld_ || parallelMode_) {
        if (sliceUntilAbort(secs)) throw WorkerAbortEx{};
        return;
    }
    static thread_local ExecContext parked;
    saveCtx(parked);
    gilYieldNotify();
    bool aborted = sliceUntilAbort(secs);
    gil_.lock();                 // restore invariants before unwinding
    loadCtx(parked);
    if (aborted) throw WorkerAbortEx{};
}

// Release the GIL for a blocking syscall so other worker threads run concurrently.
// Modeled on sleepYield, but the caller does the blocking (child-process wait). The
// parked window must touch NO interpreter state — only this thread's own buffers.
static thread_local ExecContext g_gilParkCtx;
bool Interpreter::gilPark() {
    if (!gilHeld_ || parallelMode_) return false;  // parallel mode: no GIL to release, waits already overlap
    saveCtx(g_gilParkCtx);
    gilYieldNotify();
    return true;
}
void Interpreter::gilUnpark(bool wasParked) {
    if (!wasParked) return;
    gil_.lock();
    loadCtx(g_gilParkCtx);
}

thread_local bool t_holdsGil = false;
std::atomic<int> g_stmtLine{0};
std::atomic<bool> g_stmtLineThreaded{false};

thread_local int t_stmtLine = 0;

// --hints / RAKUPP_HINTS: performance advice on stderr, off by default. Read
// once; `RAKUPP_HINTS=0` is off. Each hint is said once per source line.
bool rtHintsOn() {
    static const bool on = [] {
        const char* h = std::getenv("RAKUPP_HINTS");
        return h && *h && std::string(h) != "0";
    }();
    return on;
}
// What was said already: once per line and kind. The REPL empties it for each
// input, since every input it evaluates is line 1 again.
static std::mutex g_hintMu;
static std::set<std::pair<int, std::string>> g_hintSaid;
void rtHintsReset() {
    std::lock_guard<std::mutex> lk(g_hintMu);
    g_hintSaid.clear();
}
void rtHint(const char* kind, const std::string& msg) {
    const int line = g_stmtLineThreaded.load(std::memory_order_relaxed)
                         ? t_stmtLine : g_stmtLine.load(std::memory_order_relaxed);
    auto& mu = g_hintMu;
    auto& said = g_hintSaid;
    std::lock_guard<std::mutex> lk(mu);
    if (!said.insert({line, kind}).second) return;   // once per line and kind
    std::cerr << "hint: line " << line << ": " << msg << "\n";
}
thread_local Value t_threadSelf;

long long Interpreter::newThreadId() {
    static std::atomic<long long> next{2};
    return next++;
}

// `$*THREAD`. A Thread.start thread answers the object it was started as. Any
// other worker — a `start`, a `hyper for` batch, a timer's continuation — gets
// one of its own the first time it asks: its own id, and not the initial
// thread. They all used to answer the initial thread's id 1, so no code could
// tell it had left the main thread at all.
Value Interpreter::currentThread() {
    if (t_threadSelf.t == VT::Hash) return t_threadSelf;
    Value h = Value::makeHash(); h.hashKind = "Thread";
    if (t_poll.isWorker) {
        (*h.hash())["initial"] = Value::boolean(false);
        (*h.hash())["id"] = Value::integer(newThreadId());
        t_threadSelf = h;
        return h;
    }
    (*h.hash())["initial"] = Value::boolean(threadDepth_ == 0);
    (*h.hash())["id"] = Value::integer(1);
    return h;
}
// Its isWorker is true only on `start`/async worker threads — it gates the
// safe-point abort (inline in the header) so the main thread is never unwound.
thread_local LoopPoll t_poll;

// Called from a worker's safe point every few thousand loop iterations: release the
// GIL (waking a main thread parked in yieldToWorker), give the scheduler a chance to
// hand the mutex over, then reacquire — mirroring sleepYield's save/park/restore.
void Interpreter::workerYield() {
    if (!gilHeld_ || parallelMode_) return;
    static thread_local ExecContext parked;
    saveCtx(parked);
    gilYieldNotify();            // unlock GIL + bump release counter + notify
    std::this_thread::yield();   // let the woken thread actually take the mutex
    gil_.lock();
    loadCtx(parked);
}

// Spawn a real worker thread that runs `code` and keeps/breaks a Promise backed
// by a PromiseState. The worker blocks on the GIL until the main thread yields
// (inside awaitPromise) or the program drains, so nothing runs truly in parallel.
Value Interpreter::spawnPromise(Value code, Value threadVal) {
    engageGil();
    everSpawned_.store(true, std::memory_order_relaxed);
    auto ps = std::make_shared<PromiseState>();
    Value p = Value::makeHash(); p.hashKind = "Promise";
    (*p.hash())["status"] = Value::str("Planned");
    p.extM() = ps;
    Interpreter* self = this;
    if (parallelMode_) {
        // True-parallel worker: runs interpreter compute concurrently with the main
        // thread and its siblings — no GIL. Its registers/stacks are thread_local
        // (fresh & empty on this new thread, steps 1/3a); the symbol tables are frozen
        // (step 2); the promise settle is guarded by ps->m and the workers_ push
        // by addWorker's own sharedMut_ scope — holding sharedMut_ HERE
        // self-deadlocked against addWorker/throttleSpawn (std::mutex is not
        // recursive).
        liveWorkers_++;
        auto fin = std::make_shared<std::atomic<bool>>(false);
        auto spawnScope = tctx_.cur ? tctx_.cur : global_;
        throttleSpawn();
        addWorker(BigStackThread([self, code, ps, fin, spawnScope, threadVal]() mutable {
            t_poll.isWorker = true;
            if (threadVal.t == VT::Hash) t_threadSelf = threadVal;
            tctx_.cur = spawnScope;        // anchor: dynamics visible at the spawn point
            tctx_.dynStack.push_back(spawnScope.get());
            Value r; bool broke = false; Value cause;
            try {
                ValueList noargs;
                // The worker's top-level block is a ROUTINE frame, so `$/`
                // scopes to this thread.
                //
                // Without it, `$/` scoped to the nearest routine frame OUTWARDS
                // — and a `start` block's frame is parented to its lexical
                // closure, which every worker shares. So N threads assigned `$/`
                // into one Env's std::map at once: a data race on a container
                // with no locking, which corrupted the heap and surfaced as an
                // intermittent SIGSEGV in an unrelated frame. Reachable from
                // `await ^30 .map: { start { "abc" ~~ /.+/ } }`, and since
                // v3.0.0 made parallel the default, without asking for threads.
                //
                // It is also what the semantics want: a thread's match is its
                // own. Sharing one `$/` would let one worker read another's
                // match, which no program could rely on — and Rakudo does not.
                tctx_.forceRoutineFrame = true;
                r = code.t == VT::Code ? self->callCallable(code, noargs) : code;
            }
            catch (const RakuError& e) {
                broke = true; cause = e.payload; ps->causeMsg = e.message;
                // carry the WORKER's chain out with the exception: the awaiting
                // thread's own stack says nothing about where this died
                ps->causeBt = e.bt;
            }
            catch (...) { broke = true; }
            std::vector<std::function<void()>> fire;
            {
                std::lock_guard<std::mutex> lk(ps->m);
                ps->result = r; ps->cause = cause; ps->broken = broke; ps->done = true;
                fire.swap(ps->thens);      // `.then`s registered before the worker settled
            }
            ps->cv.notify_all();
            for (auto& f : fire) f();
            self->liveWorkers_--;
            fin->store(true, std::memory_order_release);
        }), fin);
        return p;
    }
    liveWorkers_++;
    auto fin = std::make_shared<std::atomic<bool>>(false);
    auto spawnScope = tctx_.cur ? tctx_.cur : global_; // dynamics visible at the spawn point
    throttleSpawn();
    addWorker(BigStackThread([self, code, ps, fin, spawnScope, threadVal]() mutable {
        t_poll.isWorker = true;
        if (threadVal.t == VT::Hash) t_threadSelf = threadVal;
        self->gil_.lock();                 // acquire the GIL (main must have yielded)
        ExecContext wctx;                  // fresh, empty registers for this worker
        self->loadCtx(wctx);
        tctx_.cur = spawnScope;            // anchor: `start {…}` sees the spawner's dynamics
        tctx_.dynStack.push_back(spawnScope.get());
        Value r; bool broke = false; Value cause;
        try {
            ValueList noargs;
            r = code.t == VT::Code ? self->callCallable(code, noargs) : code;
        }
        catch (const RakuError& e) { broke = true; cause = e.payload; ps->causeMsg = e.message; }
        catch (...) { broke = true; }
        self->saveCtx(wctx);               // pull worker registers back out
        std::vector<std::function<void()>> fire;
        {
            std::lock_guard<std::mutex> lk(ps->m);
            ps->result = r; ps->cause = cause; ps->broken = broke; ps->done = true;
            fire.swap(ps->thens);          // `.then`s registered before the worker settled
        }
        for (auto& f : fire) f();          // run them now, still on the GIL — like keep/break
        self->liveWorkers_--;
        self->gilYieldNotify();            // release the GIL (waking any cooperative yielder)
        ps->cv.notify_all();
        fin->store(true, std::memory_order_release);
    }), fin);
    return p;
}

// $*SCHEDULER.cue(&code, :in/:at → delaySecs, :every, :times, :stop, :catch).
// A worker sleeps GIL-free, takes the GIL per run, and re-arms for :every.
Value Interpreter::cueJob(Value code, double delaySecs, double everySecs, long long times,
                          Value stopF, Value catchF) {
    engageGil();
    auto cs = std::make_shared<CueState>();
    Value cancellation = Value::makeHash(); cancellation.hashKind = "Cancellation";
    cancellation.extM() = cs;
    cuedLoads_++;
    liveWorkers_++;
    auto fin = std::make_shared<std::atomic<bool>>(false);
    auto spawnScope = tctx_.cur ? tctx_.cur : global_;
    Interpreter* self = this;
    throttleSpawn();
    addWorker(BigStackThread([self, code, cs, fin, spawnScope, delaySecs, everySecs, times, stopF, catchF]() mutable {
        t_poll.isWorker = true;
        auto clock0 = std::chrono::steady_clock::now();
        // Drift-free deadline from the cue's start, slept in slices so shutdown
        // (workerAbort_) and .cancel wake it within ~50 ms instead of holding the
        // worker for the delay's whole remainder. Double-rep arithmetic: a huge
        // `:in`/`:every` must not overflow the int64 nanosecond range.
        auto napUntil = [&](double secsFromStart) -> bool { // true: abort/cancel observed
            for (;;) {
                if (self->workerAbort_.load(std::memory_order_relaxed)) return true;
                if (cs->cancelled.load(std::memory_order_relaxed)) return true;
                double left = secsFromStart -
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - clock0).count();
                if (left <= 0) return false;
                std::this_thread::sleep_for(std::chrono::duration<double>(left < 0.05 ? left : 0.05));
            }
        };
        double firedAt = delaySecs;
        bool interrupted = napUntil(delaySecs);
        self->gilLock();
        ExecContext wctx;
        self->loadCtx(wctx);
        tctx_.cur = spawnScope;
        tctx_.dynStack.push_back(spawnScope.get());
        long long ran = 0;
        for (;;) {
            if (interrupted) break; // shutdown or .cancel during a nap: never run
            if (cs->cancelled.load(std::memory_order_relaxed)) break;
            if (stopF.t == VT::Code) {
                bool stop = false;
                try { ValueList na; stop = self->callCallable(stopF, na).truthy(); } catch (...) {}
                if (stop) break;
            }
            try { ValueList na; self->callCallable(code, na); }
            catch (const RakuError& e) {
                if (catchF.t == VT::Code) {
                    try { ValueList ca{self->exceptionFor(e)}; self->callCallable(catchF, ca); } catch (...) {}
                }
            }
            catch (const WorkerAbortEx&) { break; } // shutdown: unwind the cue loop
            catch (...) {}
            if (self->workerAbort_.load(std::memory_order_relaxed)) break;
            ran++;
            // :times(N) without :every repeats back-to-back; :every repeats on the
            // interval; neither -> one-shot
            long long target = times > 0 ? times : (everySecs > 0 ? -1 : 1);
            if (target > 0 && ran >= target) break;
            if (everySecs > 0) {
                // park like workerYield: the live registers belong to whoever holds
                // the GIL — save ours, sleep unlocked, re-acquire, restore
                static thread_local ExecContext parked; // reused buffers: no per-tick allocs
                self->saveCtx(parked);
                self->gilYieldNotify();
                firedAt += everySecs;      // fixed cadence from the START, not from run end
                interrupted = napUntil(firedAt);
                self->gilLock();
                self->loadCtx(parked);
            }
        }
        self->saveCtx(wctx);
        self->cuedLoads_--;
        self->liveWorkers_--;
        self->gilYieldNotify();
        fin->store(true, std::memory_order_release);
    }), fin);
    return cancellation;
}

// Block the caller until `ps` completes, dropping the GIL while parked so a
// worker can run, and handing over the live execution registers to it.
void Interpreter::awaitPromise(const std::shared_ptr<PromiseState>& ps) {
    if (!ps) return;
    std::unique_lock<std::mutex> plk(ps->m);
    if (ps->done) return;                  // already settled — no need to yield
    if (!gilHeld_) return;                 // no async workers exist; nothing could
                                           // ever settle it — don't deadlock/UB, just return
    if (parallelMode_) {                   // no GIL: just wait for the worker to settle it
        // …unless this is an event worker (timer, socket reader, a `.then`
        // continuation), which still serialises on gil_. It must let the
        // others run while it waits, as sleepYield does: Cro's client, with
        // two `.then({ await .result.body })` in flight, parked one holding
        // gil_ while the socket readers that would settle it queued for it.
        if (t_holdsGil) {
            gilYieldNotify();
            ps->cv.wait(plk, [&] { return ps->done; });
            plk.unlock();                  // drop ps->m BEFORE reacquiring the GIL (as below)
            gilLock();
            return;
        }
        ps->cv.wait(plk, [&] { return ps->done; });
        return;
    }
    static thread_local ExecContext parked; // per-thread stash (supports nested await)
    saveCtx(parked);
    gilYieldNotify();
    ps->cv.wait(plk, [&] { return ps->done; });
    plk.unlock();          // drop ps->m BEFORE reacquiring the GIL (avoids ABBA with keep/break/second-awaiter)
    gil_.lock();
    loadCtx(parked);
}

bool Interpreter::promiseSettled(const Value& p) {
    if (p.ext()) {
        auto ps = std::static_pointer_cast<PromiseState>(p.ext());
        std::lock_guard<std::mutex> lk(ps->m);
        return ps->done;
    }
    if (!p.hash()) return true;
    const ValueMap& h = *p.hash();
    auto st = h.find("status");
    if (st != h.end()) {
        const std::string s = st->second.toStr();
        if (s == "Kept" || s == "Broken") return true;
    }
    auto k = h.find("kind");
    const std::string kind = k != h.end() ? k->second.toStr() : std::string();
    if (kind == "timer") return timerRemainingSecs(p) <= 0;
    // `$proc.ready` keeps no state of its own either: it is ready once its
    // process has been started (the process itself runs lazily, see "proc")
    if (kind == "proc-ready") {
        auto pr = h.find("proc");
        return pr != h.end() && pr->second.hash() &&
               (pr->second.hash()->count("started") || pr->second.hash()->count("pid"));
    }
    if (kind == "anyof" || kind == "allof") {
        // anyof: kept once one member has settled; allof: once every one has
        // (a broken member does not fail it); no members at all: kept
        auto ms = h.find("promises");
        if (ms == h.end() || !ms->second.arr()) return true;
        const bool any = kind == "anyof";
        for (auto& m : *ms->second.arr())
            if (promiseSettled(m) == any) return any;
        return !any || ms->second.arr()->empty();
    }
    return false;
}

// `.then` on an anyof/allof. The combinator has no PromiseState of its own —
// its state is a fold over its members — so it cannot queue a continuation the
// way a start promise does, and running `fn` on the spot (as it used to) handed
// `Promise.allof(@workers).then({…})` its callback before any worker had run.
// Instead every start/vow member, a nested combinator's included, gets a
// continuation that re-folds the combinator, and the first fold to come out
// settled runs `fn`: on the thread that settled the member, as `.then` on that
// member would. Nothing settles a timer, so when the combinator holds any, one
// worker sleeps from moment to moment re-folding as each passes — woken early,
// and gone, as soon as `fn` has run.
void Interpreter::thenCombinator(const Value& combo, std::function<void()> fn) {
    if (promiseSettled(combo)) { fn(); return; }
    struct Watch { std::mutex m; std::condition_variable cv; bool fired = false; };
    auto w = std::make_shared<Watch>();
    std::function<void()> refold = [combo, w, fn]() {
        if (!promiseSettled(combo)) return;
        { std::lock_guard<std::mutex> lk(w->m); if (w->fired) return; w->fired = true; }
        w->cv.notify_all();
        fn();
    };
    std::vector<double> moments;   // the unfired timer members' fire times (the `now` clock)
    std::function<void(const Value&)> hook = [&](const Value& m) {
        if (m.ext()) {
            auto ps = std::static_pointer_cast<PromiseState>(m.ext());
            bool now = false;
            { std::lock_guard<std::mutex> lk(ps->m); if (ps->done) now = true; else ps->thens.push_back(refold); }
            if (now) refold();
            return;
        }
        if (!m.hash() || promiseSettled(m)) return;
        auto k = m.hash()->find("kind");
        const std::string kind = k != m.hash()->end() ? k->second.toStr() : std::string();
        if (kind == "anyof" || kind == "allof") {
            auto ms = m.hash()->find("promises");
            if (ms != m.hash()->end() && ms->second.arr())
                for (auto& x : *ms->second.arr()) hook(x);
        }
        else if (kind == "timer") moments.push_back(epochNowSecs() + timerRemainingSecs(m));
        else if (kind == "proc") {
            // a lazily-realized process promise is realized by whoever waits on
            // it (await, `.then`): run it now, as `.then` on it alone does
            Value pv = m;
            runProcPromise(pv, 0);
            refold();
        }
    };
    hook(combo);
    // a member that settled while the others were being hooked, without a
    // continuation to say so (a timer whose moment passed meanwhile)
    refold();
    if (moments.empty()) return;
    { std::lock_guard<std::mutex> lk(w->m); if (w->fired) return; }
    std::sort(moments.begin(), moments.end());
    engageGil();
    liveWorkers_++;
    auto fin = std::make_shared<std::atomic<bool>>(false);
    auto spawnScope = tctx_.cur ? tctx_.cur : global_;
    Interpreter* self = this;
    throttleSpawn();
    addWorker(BigStackThread([self, w, refold, moments, fin, spawnScope]() mutable {
        t_poll.isWorker = true;
        for (double at : moments) {
            bool stop = false;                                        // GIL not held
            {
                std::unique_lock<std::mutex> lk(w->m);
                for (;;) {
                    if (w->fired || self->workerAbort_.load(std::memory_order_relaxed)) { stop = true; break; }
                    double left = at - epochNowSecs();
                    if (!(left > 0)) break;                           // (a NaN moment: now)
                    w->cv.wait_for(lk, std::chrono::duration<double>(left < 0.05 ? left : 0.05));
                }
            }
            if (stop) break;
            self->gilLock();
            ExecContext wctx; self->loadCtx(wctx);
            tctx_.cur = spawnScope;
            tctx_.dynStack.push_back(spawnScope.get());
            try { refold(); } catch (...) {}
            self->gilYieldNotify();
        }
        self->liveWorkers_--;
        fin->store(true, std::memory_order_release);
    }), fin);
}

// The `react` event loop: block until every live source has signalled done (or
// `done`/`last` closed the block). Same-thread emits already ran synchronously,
// so when no async emitter engaged the GIL there is nothing to wait for and we
// return at once. Otherwise drop the GIL and wait for an emitter thread to
// decrement liveSources / close the context.
void Interpreter::runReactLoop(const std::shared_ptr<ReactCtx>& ctx) {
    // Register for the drain-time wake-up: a react parked on sources that have
    // no worker of their own (a Supplier-backed `whenever $button.clicks`) has
    // nobody to hand its liveSources back at shutdown, so an unwoken wait held
    // drainWorkers' whole 2 s grace — the pause between a GUI window's close
    // button and the process exiting. drainWorkers sets ctx->aborted and
    // notifies; a WORKER parked here then unwinds via WorkerAbortEx. Steady-
    // state waiting stays the plain indefinite predicated wait (a 50 ms
    // polling variant destabilized timing-marginal S17 files under the
    // parallel roast harness).
    {
        std::lock_guard<std::mutex> g(parkedReactsMut_);
        parkedReacts_.push_back(ctx);
    }
    auto pred = [&] { return ctx->liveSources <= 0 || ctx->closed || !ctx->deferred.empty() ||
                             (t_poll.isWorker && ctx->aborted); };
    auto sourcesDone = [&] { return ctx->liveSources <= 0 || ctx->closed; };
    std::unique_lock<std::mutex> lk(ctx->m);
    // A whenever registered from INSIDE a whenever block defers its first
    // deliveries like one in the react body does (issue #18); here they run
    // once that block has finished. (They were queued and never run: a
    // nested `whenever Supply.from-list(…)` delivered nothing.)
    auto drainDeferred = [&]() -> bool {   // lk held on entry and on exit
        if (ctx->closed || ctx->deferred.empty()) return false;
        std::vector<std::function<void()>> ds;
        ds.swap(ctx->deferred);
        lk.unlock();
        try { for (auto& d : ds) d(); }
        catch (DoneEx&) {}
        catch (...) { lk.lock(); throw; }
        lk.lock();
        return true;
    };
    while (drainDeferred()) {}
    while (ctx->liveSources > 0 && !ctx->closed) {
        if (!gilHeld_) break; // no async emitter can exist → don't hang the loop
        if (parallelMode_) {  // no GIL: wait for an emitter thread to close/drain
            ctx->cv.wait(lk, pred);
            if (t_poll.isWorker && ctx->aborted && !sourcesDone()) throw WorkerAbortEx{};
            break;
        }
        static thread_local ExecContext parked;
        saveCtx(parked);
        gilYieldNotify();
        ctx->cv.wait(lk, pred);
        bool aborted = t_poll.isWorker && ctx->aborted && !sourcesDone();
        lk.unlock();       // drop ctx->m before reacquiring the GIL (avoids ABBA with emitters)
        gil_.lock();       // restore invariants before unwinding
        loadCtx(parked);
        if (aborted) throw WorkerAbortEx{};
        lk.lock();         // re-check the loop condition under ctx->m
        while (drainDeferred()) {}
    }
    while (drainDeferred()) {}
}

// Wake every parked react so its worker can unwind promptly at shutdown:
// sources without a worker of their own have nobody to release them.
void Interpreter::wakeParkedReacts() {
    std::vector<std::shared_ptr<ReactCtx>> live;
    {
        std::lock_guard<std::mutex> g(parkedReactsMut_);
        auto& v = parkedReacts_;
        v.erase(std::remove_if(v.begin(), v.end(), [&](std::weak_ptr<ReactCtx>& w) {
            if (auto s = w.lock()) { live.push_back(std::move(s)); return false; }
            return true;   // react long gone — prune the entry
        }), v.end());
    }
    for (auto& c : live) {
        std::lock_guard<std::mutex> lk(c->m);
        c->aborted = true;
        c->cv.notify_all();
    }
}

// Let every outstanding worker finish before the interpreter goes away. Workers
// may spawn further workers, so loop until the queue is empty.
void Interpreter::drainWorkers() {
    if (!gilHeld_) return;
    if (parallelMode_) {
        // Workers already run freely — but daemon-ish ones (a server's accept
        // loop, a tapSupply pump, an :app_lifetime sleeper) never finish on
        // their own, and joining them UNCONDITIONALLY kept the process alive
        // until the harness's kill: socket-recv-vs-read.t passed every test,
        // then sat in this join for the full 30 s timeout. Same daemon
        // semantics as the GIL branch below: ask workers to unwind, give the
        // stragglers a short grace, then abandon them so the program can exit.
        workerAbort_.store(true, std::memory_order_relaxed);
        wakeSignalWorker();
        wakeParkedReacts();
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (liveWorkers_.load() > 0 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        std::vector<WorkerSlot> batch;
        { std::lock_guard<std::mutex> lk(sharedMut_); batch.swap(workers_); }
        if (liveWorkers_.load() > 0) {
            for (auto& t : batch) t.th.detach(); // daemon: don't wait on it
            abandonedWorkers_ = true;
        } else {
            for (auto& t : batch) if (t.th.joinable()) t.th.join();
        }
        gilHeld_ = false;
        return;
    }
    // Ask workers to unwind: a compute-bound worker (no I/O to yield at) hits the
    // safe point in its next loop iteration and throws WorkerAbortEx, releasing the
    // GIL and finishing. Workers blocked in a syscall (a server's accept/recv) can't
    // see this — they're handled by the grace-period abandon below.
    workerAbort_.store(true, std::memory_order_relaxed);
    wakeSignalWorker();
    wakeParkedReacts();
    gil_.unlock();                         // let workers run while we wait
    // Wait for outstanding workers to finish, but not forever: a fire-and-forget
    // `start {…}` that loops (a server's accept loop) is a daemon thread. Once the
    // mainline is done we give such workers a short grace period, then abandon them
    // so the program can exit — matching Rakudo's thread-pool (daemon) semantics.
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (liveWorkers_.load() > 0 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    std::vector<WorkerSlot> batch;
    { std::lock_guard<std::mutex> lk(sharedMut_); batch.swap(workers_); } // take ownership under the lock
    if (liveWorkers_.load() > 0) {
        for (auto& t : batch) t.th.detach(); // daemon: don't wait on it
        abandonedWorkers_ = true;
    } else {
        for (auto& t : batch) if (t.th.joinable()) t.th.join(); // reap the finished threads
    }
    gilHeld_ = false;
}

void Interpreter::flushOpenWriteHandles() {
    for (auto& h : openWriteHandles_) {
        if (!h) continue;
        auto cl = h->find("closed"); if (cl != h->end() && cl->second.truthy()) continue;
        auto fl = h->find("flushed"); if (fl != h->end() && fl->second.truthy()) continue;
        std::string mode = (*h)["mode"].toStr();
        // A COPY: the lookups below can insert into the map, and an insert can
        // rehash it out from under a reference to a value in it.
        std::string buf = (*h)["buffer"].s;
        if ((mode == "rw" || mode == "update") && buf.empty()) continue; // nothing written — leave the file alone
        // A handle that was .flush-ed already has part of its output on disk:
        // append the rest, and write nothing at all when there is no rest.
        if ((*h)["wrote"].truthy() && buf.empty()) continue;
        // Through the same writer every other flush uses — this used to be a
        // second copy of it that opened the file in TEXT mode, so on Windows a
        // handle whose output went out in two pieces got two different newline
        // translations.
        fhAppendToFile(h, buf);
    }
    openWriteHandles_.clear();
}

// ---- mainline sink-context "Useless use" warnings ------------------------
// A conservative compile-time pass over the mainline (and blocks/loop bodies
// reached from it): only provably value-only statements warn. Anything with a
// call, assignment, or unknown node stays silent — over-warning would pollute
// stderr that tests compare exactly.
static bool sinkPure(Expr* e, std::string& spell, std::string& kindw) {
    switch (e->kind) {
        case NK::IntLit: {
            auto* n = static_cast<IntLit*>(e);
            spell = !n->raw.empty() ? n->raw : (!n->big.empty() ? n->big : std::to_string(n->v));
            kindw = "constant integer ";
            return true;
        }
        case NK::NumLit: {
            auto* n = static_cast<NumLit*>(e);
            spell = n->raw;
            if (spell.empty()) { std::ostringstream o; o << n->v; spell = o.str(); }
            kindw = "constant number ";
            return true;
        }
        case NK::StrLit:
            spell = "\"" + static_cast<StrLit*>(e)->v + "\"";
            kindw = "constant string ";
            return true;
        case NK::AllomorphLit: // a `<42>`/`<1.5>` word warns as its numeric literal
            return sinkPure(static_cast<AllomorphLit*>(e)->num.get(), spell, kindw);
        case NK::InterpStr: { // "foo" with nothing to interpolate is still constant
            std::string flat;
            for (auto& p : static_cast<InterpStr*>(e)->parts) {
                if (p->kind != NK::StrLit) return false;
                flat += static_cast<StrLit*>(p.get())->v;
            }
            spell = "\"" + flat + "\"";
            kindw = "constant string ";
            return true;
        }
        case NK::VarExpr: {
            auto* ve = static_cast<VarExpr*>(e);
            const std::string& n = ve->name;
            // a bare `$` term (parsed as an anonymous state var) is useless in
            // sink position even though it technically declares
            if (n.rfind("$anon--state--", 0) == 0) {
                spell = "unnamed $ variable"; kindw = "";
                return true;
            }
            if (ve->declare) return false; // `my $b;` declares, no useless use
            if (n.empty() || (n[0] != '$' && n[0] != '@' && n[0] != '%')) return false;
            if (n.size() == 1) { spell = "unnamed " + n + " variable"; kindw = ""; return true; }
            char tw = n[1]; // no twigilled/special vars: $*DYN may throw, $_ / $/ / $! are set by context
            if (!(ascii::isalpha((unsigned char)tw) && (unsigned char)tw < 0x80) && tw != '-') return false;
            spell = n; kindw = "";
            return true;
        }
        case NK::NameTerm: {
            const std::string& n = static_cast<NameTerm*>(e)->name;
            static const std::set<std::string> known = {
                "Inf", "NaN", "pi", "tau", "e", "\xCF\x80", "\xCF\x84",
                "Any", "Mu", "Cool", "Int", "Str", "Num", "Rat", "Complex"};
            if (!known.count(n) || !static_cast<NameTerm*>(e)->ofType.empty()) return false;
            spell = n; kindw = "";
            return true;
        }
        case NK::BoolLit:
            spell = static_cast<BoolLit*>(e)->v ? "True" : "False";
            kindw = "constant Bool ";
            return true;
        case NK::Unary: {
            auto* u = static_cast<Unary*>(e);
            if (u->postfix || !u->operand) return false;
            if (u->op != "-" && u->op != "+" && u->op != "?" && u->op != "~" && u->op != "!")
                return false;
            std::string in;
            if (!sinkPure(u->operand.get(), in, kindw)) return false;
            spell = u->op + in;
            return true;
        }
        case NK::Binary: {
            auto* b = static_cast<Binary*>(e);
            static const std::set<std::string> pureOps = {
                "+", "-", "*", "/", "%", "**", "~", "x", "div", "mod", "gcd", "lcm",
                "==", "!=", "<", "<=", ">", ">=", "eq", "ne", "lt", "le", "gt", "ge"};
            if (!pureOps.count(b->op)) return false;
            std::string l, r, kw2;
            if (!b->lhs || !b->rhs) return false;
            if (!sinkPure(b->lhs.get(), l, kindw) || !sinkPure(b->rhs.get(), r, kw2)) return false;
            spell = l + b->op + r; kindw = "";
            return true;
        }
        case NK::Pair: {
            auto* p = static_cast<PairExpr*>(e);
            if (!p->value) return false;
            if (p->keyExpr) { // "foo" => 42 — a constant quoted key is still pure
                std::string k, v, kw2, kw3;
                if (!sinkPure(p->keyExpr.get(), k, kw2) || kw2 != "constant string " ||
                    !sinkPure(p->value.get(), v, kw3)) return false;
                spell = k + " => " + v; kindw = "";
                return true;
            }
            if (p->key.empty()) return false;
            std::string v, kw2;
            if (!sinkPure(p->value.get(), v, kw2)) return false;
            spell = p->colonForm ? ":" + p->key + "(" + v + ")"
                                 : (p->quotedKey ? "\"" + p->key + "\"" : p->key) + " => " + v;
            kindw = "";
            return true;
        }
        case NK::ListExpr: { // only the EMPTY list as a unit; others warn per element
            auto* le = static_cast<ListExpr*>(e);
            if (!le->items.empty()) return false;
            spell = "()"; kindw = "";
            return true;
        }
        default: return false;
    }
}

static void sinkWarnOne(Expr* e, int line, bool nilHint, std::vector<std::string>& out) {
    std::string sp, kw;
    if (!sinkPure(e, sp, kw)) return;
    out.push_back("Useless use of " + kw + sp + " in sink context" +
                  (nilHint ? " (use Nil instead to suppress this warning)" : "") +
                  " (line " + std::to_string(line) + ")");
}

static void sinkWarnExprTop(Expr* e, int line, bool nilHint, std::vector<std::string>& out) {
    if (e->kind == NK::BlockExpr) { // a bare block runs sunk: its statements sink too
        auto* be = static_cast<BlockExpr*>(e);
        if (!be->isSub && be->params.empty())
            for (auto& st : be->body) sinkWarnStmt(st.get(), nilHint, out);
        return;
    }
    if (e->kind == NK::ListExpr && !static_cast<ListExpr*>(e)->items.empty()) {
        for (auto& it : static_cast<ListExpr*>(e)->items) // sink distributes over commas
            sinkWarnExprTop(it.get(), line, nilHint, out);
        return;
    }
    if (e->kind == NK::ArrayLit && static_cast<ArrayLit*>(e)->isList) { // word list <a b c>
        for (auto& it : static_cast<ArrayLit*>(e)->items)
            sinkWarnOne(it.get(), line, nilHint, out);
        return;
    }
    // `xor` / `^^` in sink context discards EVERY operand, so each one sinks on
    // its own: `uc 'foo' xor 'bar'` warns about the constant and not the call.
    // (The chain nests left, so the recursion walks it.)
    if (e->kind == NK::Binary) {
        auto* b = static_cast<Binary*>(e);
        if ((b->op == "xor" || b->op == "^^") && b->lhs && b->rhs) {
            sinkWarnExprTop(b->lhs.get(), line, nilHint, out);
            sinkWarnExprTop(b->rhs.get(), line, nilHint, out);
            return;
        }
    }
    sinkWarnOne(e, line, nilHint, out);
}

// find `gather` inside a mainline statement's expression (its body is sunk:
// `my @x = gather 43` must warn about the 43)
static void sinkScanGather(Expr* e, std::vector<std::string>& out) {
    if (!e) return;
    switch (e->kind) {
        case NK::Unary: {
            auto* u = static_cast<Unary*>(e);
            if (u->op == "gather" && u->operand) {
                if (u->operand->kind == NK::BlockExpr) {
                    for (auto& st : static_cast<BlockExpr*>(u->operand.get())->body)
                        sinkWarnStmt(st.get(), false, out);
                }
                else sinkWarnOne(u->operand.get(), u->line, false, out);
                return;
            }
            // `try { 42; 1 }` — every statement but the last is sunk (that one
            // is the try's value)
            if (u->op == "try" && u->operand && u->operand->kind == NK::BlockExpr) {
                auto& body = static_cast<BlockExpr*>(u->operand.get())->body;
                for (size_t i = 0; i + 1 < body.size(); i++) sinkWarnStmt(body[i].get(), false, out);
                return;
            }
            sinkScanGather(u->operand.get(), out);
            return;
        }
        case NK::Binary: {
            auto* b = static_cast<Binary*>(e);
            sinkScanGather(b->lhs.get(), out); sinkScanGather(b->rhs.get(), out);
            return;
        }
        case NK::Assign:
            sinkScanGather(static_cast<Assign*>(e)->value.get(), out);
            return;
            for (auto& it : static_cast<ListExpr*>(e)->items) sinkScanGather(it.get(), out);
            return;
        default: return;
    }
}
void sinkWarnStmt(Stmt* s, bool nilHint, std::vector<std::string>& out) {
    if (!s) return;
    switch (s->kind) {
        case NK::ExprStmt: {
            auto* es = static_cast<ExprStmt*>(s);
            if (!es->e) return;
            int ln = s->line ? s->line : es->e->line;
            sinkWarnExprTop(es->e.get(), ln, nilHint, out);
            sinkScanGather(es->e.get(), out);
            return;
        }
        case NK::Block: {
            auto* b = static_cast<Block*>(s);
            if (b->isCatch || !b->phaser.empty()) return;
            for (auto& st : b->stmts) sinkWarnStmt(st.get(), nilHint, out);
            return;
        }
        case NK::WhileStmt: {
            auto* w = static_cast<WhileStmt*>(s);
            if (!w->asExpr && w->body)
                for (auto& st : w->body->stmts) sinkWarnStmt(st.get(), true, out);
            return;
        }
        case NK::ForStmt: {
            auto* f = static_cast<ForStmt*>(s);
            if (!f->asExpr && f->body)
                for (auto& st : f->body->stmts) sinkWarnStmt(st.get(), true, out);
            return;
        }
        case NK::RepeatStmt: {
            auto* r = static_cast<RepeatStmt*>(s);
            if (r->body) for (auto& st : r->body->stmts) sinkWarnStmt(st.get(), true, out);
            return;
        }
        case NK::LoopStmt: {
            auto* l = static_cast<LoopStmt*>(s);
            if (!l->asExpr && l->body)
                for (auto& st : l->body->stmts) sinkWarnStmt(st.get(), true, out);
            return;
        }
        case NK::GivenStmt: { // only the modifier form (`1.0 given 1,2`) is surely sunk
            auto* g = static_cast<GivenStmt*>(s);
            if (g->modifier && g->body)
                for (auto& st : g->body->stmts) sinkWarnStmt(st.get(), true, out);
            return;
        }
        case NK::VarDecl: {
            auto* d = static_cast<VarDecl*>(s);
            if (d->init) sinkScanGather(d->init.get(), out);
            return;
        }
        default: return;
    }
}

// Discover installed Rakudo CompUnit::Repository::Installation prefixes (site/vendor + ~/.raku).
// The installation repositories, in resolution order (home, then each site
// and vendor prefix). Exposed so `$*REPO.repo-chain` can report the same
// chain the loader actually searches.
const std::vector<std::string>& rakuRepoPrefixes() {
    static std::vector<std::string> cached;
    static bool init = false;
    if (init) return cached;
    init = true;
    std::vector<std::string>& repos = cached;
    if (std::string home = platHomeDir(); !home.empty()) repos.push_back(home + "/.raku");
    // Every per-version Rakudo install we know how to find, each contributing its
    // site and vendor stores:
    //   /usr/local/Cellar/rakudo/<ver>/share/perl6/…      Homebrew (Intel)
    //   /opt/homebrew/Cellar/rakudo/<ver>/share/perl6/…   Homebrew (arm64)
    //   ~/.rakubrew/versions/<ver>/install/share/perl6/…  rakubrew (issue #21)
    // rakubrew nests an extra `install/` level, which is why its stores are named
    // separately rather than folded into the Cellar loop.
    std::vector<std::pair<std::string, std::string>> roots = {
        {"/usr/local/Cellar/rakudo", "/share/perl6/"},
        {"/opt/homebrew/Cellar/rakudo", "/share/perl6/"},
    };
    if (std::string home = platHomeDir(); !home.empty())
        roots.push_back({home + "/.rakubrew/versions", "/install/share/perl6/"});
    for (auto& [root, tail] : roots) {
        if (DIR* d = opendir(root.c_str())) {
            while (struct dirent* e = readdir(d)) {
                std::string v = e->d_name;
                if (v == "." || v == "..") continue;
                repos.push_back(root + "/" + v + tail + "site");
                repos.push_back(root + "/" + v + tail + "vendor");
            }
            closedir(d);
        }
    }
    return repos;
}
std::vector<std::string> repoPrefixesForPath(const std::vector<std::string>& searchPath) {
    return repoPrefixesFor(searchPath);
}

// Build the %?RESOURCES hash for an installed distribution: read its dist meta
// (JSON) and map each `resources/<name>` file entry to an IO::Path pointing at
// the on-disk `<repo>/resources/<content-id>` copy. The dist `files` map is a
// flat string→string object, so a targeted scan for `"resources/…":"…"` pairs
// is enough (the top-level `resources` array holds bare strings, no colon).
Value Interpreter::buildResourceMap(const std::string& repo, const std::string& distId) {
    Value h = Value::makeHash(); h.hashKind = "";
    std::ifstream meta(repo + "/dist/" + distId);
    if (!meta) return h;
    std::ostringstream ss; ss << meta.rdbuf();
    const std::string j = ss.str();
    // scan for "resources/<key>" : "<value>"
    size_t p = 0;
    const std::string tag = "\"resources/";
    while ((p = j.find(tag, p)) != std::string::npos) {
        size_t ks = p + tag.size();
        size_t ke = j.find('"', ks);
        if (ke == std::string::npos) break;
        std::string key = j.substr(ks, ke - ks);
        size_t c = j.find(':', ke);
        if (c == std::string::npos) { p = ke + 1; continue; }
        size_t vs = j.find('"', c);
        if (vs == std::string::npos) break;
        size_t ve = j.find('"', vs + 1);
        if (ve == std::string::npos) break;
        std::string val = j.substr(vs + 1, ve - vs - 1);
        Value path = Value::str(repo + "/resources/" + val);
        path.hashKind = "IO"; // IO::Path — supports .slurp/.IO/.Str/.e
        (*h.hash())[key] = path;
        p = ve + 1;
    }
    return h;
}

// Build %?RESOURCES for a module loaded from a SOURCE checkout (`-I <distRoot>`):
// read <distRoot>/META6.json's `resources` array (bare "name" strings) and map
// each to an IO::Path at <distRoot>/resources/<name>. Rakudo resolves resources
// from META6 even when running uninstalled, so `%?RESOURCES<x>.IO` must work from
// source too (zef's Zef::Config reads its resources/config.json this way).
// $?DISTRIBUTION — the distribution the module being compiled belongs to. zef 1.x
// writes `use Zef:ver($?DISTRIBUTION.meta<version> // …)` at the top of every one
// of its modules, so an undefined value there stops the load dead. Built from the
// source checkout's META6.json; the `meta` hash is what callers actually read.
Value Interpreter::makeCuri(const std::string& name, const std::string& prefix, const Value& nextRepo) {
    auto od = makePayload<ObjectData>();
    od->cls = classes_["CompUnit::Repository::Installation"];
    od->attrs["name"] = Value::str(name);
    Value p = Value::str(prefix); p.hashKind = "IO";
    od->attrs["prefix"] = p;
    od->attrs["next-repo"] = nextRepo;
    return Value::object(od);
}

// `use lib "inst#/path"` puts an Installation over /path at the HEAD of the
// repository chain — `$*REPO` is it from here on, and what was the head is
// its next-repo — besides adding the store to the module search.
void Interpreter::useLibPath(const std::string& path) {
    libPaths_.insert(libPaths_.begin(), path);
    std::string pre;
    if (path.rfind("inst#", 0) != 0 || !global_) return;
    pre = path.substr(5);
    if (pre.empty()) return;
    Value* cur = global_->find("$*REPO");
    Value head = makeCuri("", pre, cur ? *cur : Value::any());
    if (cur) *cur = head;
    else global_->define("$*REPO", head);
}

static std::string platformLibraryName(const std::string& base) {
#if defined(_WIN32)
    return base + ".dll";
#elif defined(__APPLE__)
    return "lib" + base + ".dylib";
#else
    return "lib" + base + ".so";
#endif
}
// …and back: resources/libraries/libfoo.dylib is the library `foo`
static std::string universalLibraryName(const std::string& file) {
    std::string b = file;
    for (const char* ext : {".dylib", ".so", ".dll"}) {
        size_t n = std::strlen(ext);
        if (b.size() > n && b.compare(b.size() - n, n, ext) == 0) { b.resize(b.size() - n); break; }
    }
#if !defined(_WIN32)
    if (b.rfind("lib", 0) == 0 && b.size() > 3) b = b.substr(3);
#endif
    return b;
}
static void walkFiles(const std::string& root, const std::string& rel, std::vector<std::string>& out, int depth = 0) {
    if (depth > 32) return;
    DIR* d = opendir((rel.empty() ? root : root + "/" + rel).c_str());
    if (!d) return;
    std::vector<std::string> names;
    while (struct dirent* e = readdir(d)) {
        std::string n = e->d_name;
        if (n == "." || n == ".." || n == ".precomp") continue;
        names.push_back(n);
    }
    closedir(d);
    std::sort(names.begin(), names.end());
    for (auto& n : names) {
        std::string r = rel.empty() ? n : rel + "/" + n;
        struct stat st;
        if (::stat((root + "/" + r).c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) walkFiles(root, r, out, depth + 1);
        else out.push_back(r);
    }
}

Value Interpreter::makeDistribution(Value meta, const std::string& prefix, const std::string& type, bool fabricateFiles) {
    Value m = Value::makeHash();
    if (meta.t == VT::Hash && meta.hash()) *m.hash() = *meta.hash();
    if (fabricateFiles) {
        // meta<files>: what the dist holds besides its modules — every file
        // under bin/, and each META6 resource (a `libraries/x` one under its
        // platform file name), each keyed by the name it is asked for by
        // (a META6 `files` entry is not taken as given: what the dist holds
        // is what is on disk — a META6 cannot name paths outside it)
        Value files = Value::makeHash();
        std::vector<std::string> bins;
        walkFiles(prefix + "/bin", "", bins);
        for (auto& b : bins) (*files.hash())["bin/" + b] = Value::str("bin/" + b);
        auto rit = m.hash()->find("resources");
        if (rit != m.hash()->end() && rit->second.t == VT::Array && rit->second.arr())
            for (auto& r : *rit->second.arr()) {
                std::string rn = r.toStr();
                if (rn.rfind("libraries/", 0) == 0) {
                    std::string dir = "libraries/", base = rn.substr(10);
                    size_t sl = base.rfind('/');
                    if (sl != std::string::npos) { dir += base.substr(0, sl + 1); base = base.substr(sl + 1); }
                    (*files.hash())["resources/" + rn] = Value::str("resources/" + dir + platformLibraryName(base));
                }
                else (*files.hash())["resources/" + rn] = Value::str("resources/" + rn);
            }
        (*m.hash())["files"] = files;
    }
    Value d = Value::makeHash(); d.hashKind = "Distribution";
    (*d.hash())["meta"] = m;
    Value pfx = Value::str(prefix); pfx.hashKind = "IO";
    (*d.hash())["prefix"] = pfx;
    (*d.hash())["\x01type"] = Value::str(type);
    return d;
}

bool Interpreter::distFieldMatches(const std::string& field, const Value& matcher, bool isAuth, bool starIsWildcard) {
    if (matcher.t == VT::Bool) return matcher.truthy();
    // a Junction matcher threads: each of its values is a matcher in turn
    if (matcher.t == VT::Array && matcher.arr() &&
        (matcher.enumName == "any" || matcher.enumName == "all" || matcher.enumName == "one" || matcher.enumName == "none")) {
        size_t hits = 0, n = matcher.arr()->size();
        for (auto& m : *matcher.arr()) if (distFieldMatches(field, m, isAuth, starIsWildcard)) hits++;
        const std::string& k = matcher.enumName;
        return k == "any" ? hits > 0 : k == "all" ? hits == n : k == "one" ? hits == 1 : hits == 0;
    }
    if (matcher.t == VT::Any || matcher.t == VT::Nil) return true;
    if (starIsWildcard && field == "*") return true;
    try {
        if (isAuth) {
            if (starIsWildcard && field.empty()) return true;
            return boolify(smartmatchValue("~~", Value::str(field), matcher));
        }
        Value have = methodCall(Value::typeObj("Version"), "new", ValueList{Value::str(field)});
        const bool matcherIsVersion = matcher.t == VT::Str && matcher.hashKind == "Version";
        Value want = matcherIsVersion || matcher.t == VT::Code || matcher.t == VT::Range || matcher.t == VT::Regex ||
                     (matcher.t == VT::Array && !matcher.enumName.empty())
                   ? matcher : methodCall(Value::typeObj("Version"), "new", ValueList{Value::str(matcher.toStr())});
        return boolify(smartmatchValue("~~", have, want));
    } catch (RakuError&) { return false; }
}

// The FileSystem repository's distribution: its META6.json when the prefix
// (or the directory above a `lib` prefix) has one, else the tree itself —
// named after the prefix, with a wildcard version and API — providing
// every module file under the prefix.
Value Interpreter::curfsDistribution(ObjectData& repo) {
    auto cached = repo.attrs.find("\x01dist");
    if (cached != repo.attrs.end()) return cached->second;
    std::string prefix = repo.attrs.count("prefix") ? repo.attrs["prefix"].toStr() : std::string(".");
    while (prefix.size() > 1 && prefix.back() == '/') prefix.pop_back();
    auto isFile = [](const std::string& p) { struct stat st; return ::stat(p.c_str(), &st) == 0 && !S_ISDIR(st.st_mode); };
    Value dist;
    if (isFile(prefix + "/META6.json")) {
        std::ifstream in(prefix + "/META6.json");
        std::ostringstream ss; ss << in.rdbuf();
        Value meta = jsonParseDoc(ss.str());
        if (meta.t != VT::Hash || !meta.hash()) meta = Value::makeHash();
        for (const char* k : {"ver", "api"})
            if (!meta.hash()->count(k)) (*meta.hash())[k] = Value::str(std::string(k) == "ver" && meta.hash()->count("version")
                                                                        ? (*meta.hash())["version"].toStr() : "");
        dist = makeDistribution(meta, prefix, "CompUnit::Repository::Distribution", true);
    }
    else {
        std::string filesPrefix = prefix;
        size_t sl = prefix.rfind('/');
        if (sl != std::string::npos && sl > 0) filesPrefix = prefix.substr(0, sl);
        std::string relBase = prefix.size() > filesPrefix.size() ? prefix.substr(filesPrefix.size() + 1) : std::string();
        Value meta = Value::makeHash();
        (*meta.hash())["name"] = Value::str(prefix);
        (*meta.hash())["ver"] = Value::str("*");
        (*meta.hash())["api"] = Value::str("*");
        (*meta.hash())["auth"] = Value::str("");
        Value provides = Value::makeHash();
        std::map<std::string, int> rank;   // .rakumod over .pm6 over .pm
        std::vector<std::string> srcs;
        walkFiles(prefix, "", srcs);
        for (auto& f : srcs) {
            int r = -1; size_t cut = 0;
            if (f.size() > 8 && f.compare(f.size() - 8, 8, ".rakumod") == 0) { r = 3; cut = 8; }
            else if (f.size() > 4 && f.compare(f.size() - 4, 4, ".pm6") == 0) { r = 2; cut = 4; }
            else if (f.size() > 3 && f.compare(f.size() - 3, 3, ".pm") == 0) { r = 1; cut = 3; }
            if (r < 0) continue;
            std::string mod = f.substr(0, f.size() - cut);
            for (size_t p = mod.find('/'); p != std::string::npos; p = mod.find('/')) mod.replace(p, 1, "::");
            if (rank.count(mod) && rank[mod] >= r) continue;
            rank[mod] = r;
            (*provides.hash())[mod] = Value::str(relBase.empty() ? f : relBase + "/" + f);
        }
        (*meta.hash())["provides"] = provides;
        Value files = Value::makeHash();
        Value resources = Value::array();
        std::vector<std::string> bins, res;
        walkFiles(filesPrefix + "/bin", "", bins);
        for (auto& b : bins) (*files.hash())["bin/" + b] = Value::str("bin/" + b);
        walkFiles(filesPrefix + "/resources", "", res);
        for (auto& r : res) {
            std::string key = r;
            if (r.rfind("libraries/", 0) == 0) {
                size_t s2 = r.rfind('/');
                key = r.substr(0, s2 + 1) + universalLibraryName(r.substr(s2 + 1));
            }
            (*files.hash())["resources/" + key] = Value::str("resources/" + r);
            resources.arr()->push_back(Value::str(key));
        }
        (*meta.hash())["files"] = files;
        (*meta.hash())["resources"] = resources;
        dist = makeDistribution(meta, filesPrefix, "CompUnit::Repository::Distribution", false);
    }
    repo.attrs["\x01dist"] = dist;
    return dist;
}

ValueList Interpreter::curiCandidates(const std::string& prefix, const Value& spec) {
    std::string want;
    Value verM = Value::boolean(true), authM = Value::boolean(true), apiM = Value::boolean(true);
    if (spec.t == VT::Hash && spec.hash()) {
        auto g = [&](const char* k, Value& into) { auto it = spec.hash()->find(k); if (it != spec.hash()->end()) into = it->second; };
        Value sn; g("short-name", sn); want = sn.toStr();
        g("version-matcher", verM); g("auth-matcher", authM); g("api-matcher", apiM);
    }
    else want = spec.toStr();
    struct Cand { std::string ver, auth, api, src, id; };
    std::vector<Cand> found;
    const std::string shortDir = prefix + "/short/" + sha1hex(want);
    if (DIR* d = opendir(shortDir.c_str())) {
        while (struct dirent* e = readdir(d)) {
            std::string n = e->d_name;
            if (n == "." || n == "..") continue;
            std::ifstream in(shortDir + "/" + n);
            std::vector<std::string> lines; std::string ln;
            while (std::getline(in, ln)) lines.push_back(ln);
            if (lines.size() < 4) continue;
            // (the dist id is the ENTRY's name: Rakudo's own fifth line is a checksum)
            Cand c{lines[0], lines[1], lines[2], lines[3], n};
            if (!distFieldMatches(c.auth, authM, true, false)) continue;
            if (!distFieldMatches(c.ver.empty() ? "0" : c.ver, verM, false, false)) continue;
            if (!distFieldMatches(c.api.empty() ? "0" : c.api, apiM, false, false)) continue;
            found.push_back(c);
        }
        closedir(d);
    }
    auto cmpVer = [&](const std::string& a, const std::string& b) -> long long {
        try {
            Value va = methodCall(Value::typeObj("Version"), "new", ValueList{Value::str(a.empty() ? "0" : a)});
            Value vb = methodCall(Value::typeObj("Version"), "new", ValueList{Value::str(b.empty() ? "0" : b)});
            return applyArith("cmp", va, vb).toInt();
        } catch (RakuError&) { return a < b ? -1 : a > b ? 1 : 0; }
    };
    std::sort(found.begin(), found.end(), [&](const Cand& x, const Cand& y) {
        long long v = cmpVer(x.ver, y.ver);
        if (v != 0) return v > 0;
        return cmpVer(x.api, y.api) > 0;
    });
    ValueList out;
    for (auto& c : found) {
        std::ifstream in(prefix + "/dist/" + c.id);
        std::ostringstream ss; ss << in.rdbuf();
        Value meta = jsonParseDoc(ss.str());
        if (meta.t != VT::Hash) meta = Value::makeHash();
        // (a stored dist's meta answers its version and API as Versions)
        for (const char* k : {"ver", "version", "api"}) {
            auto it = meta.hash()->find(k);
            if (it != meta.hash()->end() && ((it->second.t == VT::Str && it->second.hashKind.empty()) || it->second.t == VT::Int))
                try { it->second = methodCall(Value::typeObj("Version"), "new", ValueList{Value::str(it->second.toStr())}); } catch (RakuError&) {}
        }
        Value d = makeDistribution(meta, prefix, "CompUnit::Repository::Installation::LazyDistribution", false);
        (*d.hash())["dist-id"] = Value::str(c.id);
        (*d.hash())["\x01src"] = Value::str(c.src);
        (*d.hash())["\x01ver"] = Value::str(c.ver);
        out.push_back(d);
    }
    return out;
}

Value Interpreter::buildDistribution(const std::string& distRoot) {
    // ALWAYS an object: a source tree with no META6.json still has a
    // $?DISTRIBUTION in Rakudo (whose .meta<anything> is a quiet Nil), and
    // answering the bare undefined here made `use Zef:ver($?DISTRIBUTION.
    // meta<version> // '*')` die "No such method 'meta'" mid-load, which the
    // loader then reported as the module not being found at all (issue #37).
    auto emptyDist = [&]() {
        Value d = Value::makeHash(); d.hashKind = "Distribution";
        (*d.hash())["meta"] = Value::makeHash();
        Value pfx = Value::str(distRoot); pfx.hashKind = "IO";
        (*d.hash())["prefix"] = pfx;
        return d;
    };
    std::ifstream meta(distRoot + "/META6.json");
    if (!meta) return emptyDist();
    std::ostringstream ss; ss << meta.rdbuf();
    Value m = jsonParseDoc(ss.str());
    if (m.t != VT::Hash) return emptyDist();
    // Rakudo's Distribution meta carries `ver` ALONGSIDE `version` (its meta
    // normalization adds the alias) — DBDish stamps every driver with
    // `:ver($?DISTRIBUTION.meta<ver>)` and reads .^ver back in its suite
    if (m.hash() && !m.hash()->count("ver") && m.hash()->count("version"))
        (*m.hash())["ver"] = (*m.hash())["version"];
    Value d = Value::makeHash(); d.hashKind = "Distribution";
    (*d.hash())["meta"] = m;
    Value pfx = Value::str(distRoot); pfx.hashKind = "IO";
    (*d.hash())["prefix"] = pfx;
    return d;
}

// $?DISTRIBUTION for an INSTALLED dist: its meta is the CURI's dist/<id> JSON
// (zef's own modules read `$?DISTRIBUTION.meta<ver>` in their `unit` lines).
Value Interpreter::buildInstalledDistribution(const std::string& repo, const std::string& distId) {
    std::ifstream meta(repo + "/dist/" + distId);
    if (!meta) return Value::any();
    std::ostringstream ss; ss << meta.rdbuf();
    Value m = jsonParseDoc(ss.str());
    if (m.t != VT::Hash) return Value::any();
    Value d = Value::makeHash(); d.hashKind = "Distribution";
    if (m.t == VT::Hash && m.hash() && !m.hash()->count("ver") && m.hash()->count("version"))
        (*m.hash())["ver"] = (*m.hash())["version"];   // Rakudo's meta alias, as above
    (*d.hash())["meta"] = m;
    Value pfx = Value::str(repo); pfx.hashKind = "IO";
    (*d.hash())["prefix"] = pfx;
    (*d.hash())["dist-id"] = Value::str(distId);   // (its files are in the store: see .content)
    return d;
}

Value Interpreter::buildSourceResourceMap(const std::string& distRoot) {
    Value h = Value::makeHash(); h.hashKind = "";
    std::ifstream meta(distRoot + "/META6.json");
    if (!meta) return h;
    std::ostringstream ss; ss << meta.rdbuf();
    const std::string j = ss.str();
    size_t rp = j.find("\"resources\"");
    if (rp == std::string::npos) return h;
    size_t lb = j.find('[', rp);
    if (lb == std::string::npos) return h;
    size_t rb = j.find(']', lb);
    if (rb == std::string::npos) return h;
    // each bare string between [ and ] is a resource name (the `{file,libname}`
    // native-lib form isn't used by the dists we target, so a string scan suffices)
    size_t p = lb + 1;
    while (p < rb) {
        size_t vs = j.find('"', p);
        if (vs == std::string::npos || vs >= rb) break;
        size_t vend = j.find('"', vs + 1);
        if (vend == std::string::npos || vend > rb) break;
        std::string nm = j.substr(vs + 1, vend - vs - 1);
        // A `libraries/<name>` resource resolves to the PLATFORM library file
        // (Rakudo: $*VM.platform-library-name) — the file on disk is
        // resources/libraries/libsha1.dylib, never the literal "sha1"
        // (Digest::SHA1::Native, issue #13). Probe the decorated candidates and
        // take the first that exists; with nothing built yet, use the platform-
        // canonical name so an error message names the file that SHOULD exist.
        std::string rel = nm;
        if (nm.rfind("libraries/", 0) == 0) {
            std::string dir = "libraries/", base = nm.substr(10);
            size_t sl = base.rfind('/');
            if (sl != std::string::npos) { dir += base.substr(0, sl + 1); base = base.substr(sl + 1); }
            std::vector<std::string> cands;
#if defined(_WIN32)
            cands = {base + ".dll", "lib" + base + ".dll"};
#elif defined(__APPLE__)
            cands = {"lib" + base + ".dylib", "lib" + base + ".so"};
#else
            cands = {"lib" + base + ".so", "lib" + base + ".dylib"};
#endif
            cands.push_back(base); // a literal file, if the dist ships one
            rel.clear();
            for (auto& cnd : cands) {
                std::ifstream f(distRoot + "/resources/" + dir + cnd);
                if (f) { rel = dir + cnd; break; }
            }
            if (rel.empty()) rel = dir + cands[0];
        }
        Value path = Value::str(distRoot + "/resources/" + rel);
        path.hashKind = "IO"; // IO::Path — supports .IO/.slurp/.Str/.e
        (*h.hash())[nm] = path;
        p = vend + 1;
    }
    return h;
}

// The distributions compiled into this binary (MODULES-PLAN B3). The generated
// prologue fills these before the program runs, exactly as it does the module
// table — but unlike that one these are NOT read-only afterwards: a dist's
// `materializedAt` is written the first time its resources are needed, and
// loadModule is not serialized (a `require` inside a parallel worker can reach
// it), so the lazy half takes a lock. `moduleDist` maps a module to the dist it
// came from; `distMeta` holds its META6 text for $?DISTRIBUTION.
struct EmbeddedDist {
    std::string meta;
    std::vector<BundledResource> resources;   // key, rel, bytes
    std::string materializedAt;               // temp dir, once the files are written
};
std::map<std::string, std::string>& embeddedModuleDist() {
    static std::map<std::string, std::string> m;
    return m;
}
static std::map<std::string, EmbeddedDist>& embeddedDists() {
    static std::map<std::string, EmbeddedDist> m;
    return m;
}

void rakuppRegisterModuleDist(const std::string& module, const std::string& distKey) {
    embeddedModuleDist()[module] = distKey;
}
void rakuppRegisterDistResource(const std::string& distKey, const std::string& key,
                                const std::string& rel, const char* bytes, size_t len) {
    embeddedDists()[distKey].resources.push_back({key, rel, std::string(bytes, len)});
}
void rakuppRegisterDistMeta(const std::string& distKey, const char* meta, size_t len) {
    embeddedDists()[distKey].meta.assign(meta, len);
}

// The temp directories embedded resources were written into, removed at exit.
// A binary that never touches a resource never creates one.
static std::vector<std::string>& embeddedResourceDirs() {
    static std::vector<std::string> v;
    return v;
}
static void removeEmbeddedResourceDirs() {
    std::error_code ec;
    for (auto& d : embeddedResourceDirs()) std::filesystem::remove_all(d, ec);
}

// %?RESOURCES for a distribution compiled INTO this binary (MODULES-PLAN B3).
//
// The bytes travel inside the executable, but the hash has to hand back
// something that behaves like the file it stands in for, so on first use a dist
// writes its resources into one temp directory and the hash points there.
// Serving the bytes straight from memory would have been less work and would
// have broken the case that matters most: `is native(%?RESOURCES<libraries/x>)`
// passes the value to dlopen, which needs a path on a real filesystem. `.open`,
// `.lines` and handing the path to a C library all want the same thing.
//
// Written once per process and reused (materializedAt). A resource that cannot
// be written is left out rather than made fatal: the program then reports the
// same missing-resource error it would have got from a disk install.
Value Interpreter::buildEmbeddedResourceMap(const std::string& distKey) {
    Value h = Value::makeHash(); h.hashKind = "";
    auto it = embeddedDists().find(distKey);
    if (it == embeddedDists().end() || it->second.resources.empty()) return h;
    EmbeddedDist& d = it->second;
    // Writes `materializedAt` and the files themselves; two threads reaching the
    // same dist must not both create the directory or half-write a resource.
    static std::mutex resMut;
    std::lock_guard<std::mutex> resLock(resMut);
    if (d.materializedAt.empty()) {
        std::error_code ec;
        std::string base = tmpDirPath();
        if (!base.empty() && base.back() != '/') base += '/';
        // One directory per (process, dist): two runs of the same binary must not
        // race over one set of files, and two dists must not collide by name.
        base += "rakupp-res-" + std::to_string((unsigned long long)::getpid()) + "-" +
                sha1hex(distKey).substr(0, 12);
        std::filesystem::create_directories(base, ec);
        if (ec) return h;
        if (embeddedResourceDirs().empty()) std::atexit(removeEmbeddedResourceDirs);
        embeddedResourceDirs().push_back(base);
        d.materializedAt = base;
    }
    for (auto& r : d.resources) {
        const std::string full = d.materializedAt + "/" + r.rel;
        std::error_code ec;
        if (!std::filesystem::exists(full, ec)) {
            size_t slash = full.rfind('/');
            if (slash != std::string::npos)
                std::filesystem::create_directories(full.substr(0, slash), ec);
            std::ofstream out(full, std::ios::binary);
            if (!out) continue;
            out.write(r.bytes.data(), (std::streamsize)r.bytes.size());
            out.close();
            // A `libraries/` resource is about to be dlopen'd; give it the mode a
            // shared library is installed with rather than the default 0644.
            if (r.key.rfind("libraries/", 0) == 0)
                std::filesystem::permissions(full,
                    std::filesystem::perms::owner_all | std::filesystem::perms::group_read |
                    std::filesystem::perms::group_exec | std::filesystem::perms::others_read |
                    std::filesystem::perms::others_exec, ec);
        }
        Value path = Value::str(full);
        path.hashKind = "IO";
        (*h.hash())[r.key] = path;
    }
    return h;
}

// $?DISTRIBUTION for a distribution compiled into this binary: the same shape
// buildDistribution gives a source checkout, from the META6 text that travelled
// with it. `prefix` is the materialized resource directory when there is one —
// that is the only path under which this dist's files exist at run time — and
// otherwise empty rather than a lie about a directory that is not there.
Value Interpreter::buildEmbeddedDistribution(const std::string& distKey) {
    auto it = embeddedDists().find(distKey);
    Value d = Value::makeHash(); d.hashKind = "Distribution";
    Value meta = Value::makeHash();
    if (it != embeddedDists().end() && !it->second.meta.empty()) {
        Value m = jsonParseDoc(it->second.meta);
        if (m.t == VT::Hash) {
            // As buildDistribution does: Rakudo's meta carries `ver` beside
            // `version`, and DBDish reads it back through .^ver.
            if (m.hash() && !m.hash()->count("ver") && m.hash()->count("version"))
                (*m.hash())["ver"] = (*m.hash())["version"];
            meta = m;
        }
    }
    (*d.hash())["meta"] = meta;
    Value pfx = Value::str(it == embeddedDists().end() ? "" : it->second.materializedAt);
    pfx.hashKind = "IO";
    (*d.hash())["prefix"] = pfx;
    return d;
}


// ---- precompiled-module cache -------------------------------------------
//
// A module's parse is cached on disk as a serialized AST (see AstSerial.h).
// Roughly 70% of what `use Foo` costs is lexing and parsing the same unchanged
// text on every run; this removes that in EVERY mode — the interpreter, and an
// `--exe` binary too, since a compiled `use` still comes through loadModule.
//
// One entry PER SOURCE FILE, keyed by its resolved path. Editing a module
// overwrites its entry; it never leaves a new one behind. That is the whole
// reason the key is not the source hash: content-keying looked elegant, but a
// module under active development would strand a fresh orphan on every save,
// and no eviction policy makes that feel right.
//
// Content is still what VALIDATES the entry — the stored SHA-1 of the source
// must match the file on disk — so a touched-but-unchanged file is a hit and a
// backdated edit is a miss. Timestamps are never consulted. An entry also
// records every module source scanModuleOps read, because an imported module's
// OPERATORS change how this file parses without this file changing at all.
//
// Anything unexpected — missing, truncated, wrong version, source changed, a
// dependency that moved — is a cache MISS, never an error. The source is there.
static std::string precompDir() {
    if (const char* d = std::getenv("RAKUPP_PRECOMP_DIR")) return d;
    std::string base;
    if (const char* x = std::getenv("XDG_CACHE_HOME")) base = x;
    else if (std::string h = platHomeDir(); !h.empty()) base = h + "/.cache";
    else return "";
    return base + "/rakupp/precomp";
}

// WHAT GETS CACHED — off unless asked for, and the two halves are independent
// because they pay off very differently:
//
//   modules   a dependency tree is a lot of source, and caching it is a clear
//             win: `use XML` (10 files) goes 16.0 -> 5.7 ms.
//   files     a script's own parse is already sub-millisecond. Caching the main
//             program costs a one-off write (+0.6 ms at 50 lines) to save about
//             as much later; it only starts to matter for big single files
//             (20k lines: 159 -> 67 ms).
//
// Nothing is cached by default: rakupp does not write to a user's disk unasked.
// Turn a half on with `rakupp --precomp-modules=on` / `--precomp-files=on`,
// which persists to the config file; RAKUPP_PRECOMP_MODULES / _FILES override it
// for one invocation and RAKUPP_NO_PRECOMP=1 forces both off.
static std::string configPath() {
    if (const char* c = std::getenv("RAKUPP_CONFIG")) return c;
    std::string base;
    if (const char* x = std::getenv("XDG_CONFIG_HOME")) base = x;
    else if (std::string h = platHomeDir(); !h.empty()) base = h + "/.config";
    else return "";
    return base + "/rakupp/rakupp.config";
}

// `key = value` lines; `#` comments. Tiny by design — this is a handful of
// switches, not a settings system.
static std::string configGet(const std::string& key) {
    std::string p = configPath();
    if (p.empty()) return "";
    std::ifstream in(p);
    if (!in) return "";
    std::string line;
    while (std::getline(in, line)) {
        auto hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        auto trim = [](std::string v) {
            auto a = v.find_first_not_of(" \t\r\n");
            auto b = v.find_last_not_of(" \t\r\n");
            return a == std::string::npos ? std::string() : v.substr(a, b - a + 1);
        };
        if (trim(line.substr(0, eq)) == key) return trim(line.substr(eq + 1));
    }
    return "";
}

static bool truthySetting(const std::string& v) {
    return v == "on" || v == "1" || v == "true" || v == "yes";
}

// source of the answer, for --precomp-info
static const char* precompHalfSource(const char* envName, const char* key) {
    if (std::getenv("RAKUPP_NO_PRECOMP")) return "RAKUPP_NO_PRECOMP";
    if (std::getenv(envName)) return envName;
    return configGet(key).empty() ? "default" : "config";
}

static bool precompHalf(const char* envName, const char* key, bool dflt) {
    if (precompDir().empty()) return false;                 // nowhere to put it
    if (std::getenv("RAKUPP_NO_PRECOMP")) return false;
    if (const char* e = std::getenv(envName)) return truthySetting(e);
    std::string v = configGet(key);
    return v.empty() ? dflt : truthySetting(v);
}
// BOTH OFF for now. On the measurements, `modules` is a clear win and turning it
// on by default would be defensible — but nothing writes to a user's disk until
// they ask, and this cache is young: every bug found in it so far has been in
// deciding whether to REUSE an entry, which is precisely the kind of thing a
// default makes everyone's problem. Flipping this one argument to `true` is the
// whole change when that confidence is there.
bool precompModulesEnabled() {
    static bool v = precompHalf("RAKUPP_PRECOMP_MODULES", "precomp-modules", false);
    return v;
}
static bool precompProgramEnabled() {
    static bool v = precompHalf("RAKUPP_PRECOMP_FILES", "precomp-files", false);
    return v;
}

#ifndef RAKUPP_VERSION
#define RAKUPP_VERSION "0.0.0"
#endif
// What an entry was built BY. It has to change whenever anything that can affect
// the parsed tree changes — not just this file. A version string plus this
// translation unit's compile time does not do that: edit only Parser.cpp, so it
// produces a different tree for the same source, and neither would move while
// the cached trees went stale. So the running BINARY's own path, size and mtime
// go in; any rebuild of any object file changes them.
//
// This is VALIDATED INSIDE the entry rather than mixed into the key, which is
// the same reasoning that made entries path-keyed: in the key, every rebuild of
// rakupp would orphan the entire cache and leave it there. In the entry, a
// rebuild simply makes each entry stale, and the next run overwrites it in
// place. One entry per source file, still.
static const std::string& precompBuildId() {
    static const std::string id = [] {
        std::string s = std::string(RAKUPP_VERSION) + "/" + std::to_string(kAstSerialVersion);
        char buf[4096];
        std::string self;
#if defined(_WIN32)
        DWORD n = ::GetModuleFileNameA(nullptr, buf, sizeof buf);
        if (n > 0 && n < sizeof buf) self = buf;
#elif defined(__APPLE__)
        uint32_t sz = sizeof buf;
        if (_NSGetExecutablePath(buf, &sz) == 0) self = buf;
#else
        ssize_t n = ::readlink("/proc/self/exe", buf, sizeof buf - 1);
        if (n > 0) { buf[n] = '\0'; self = buf; }
#endif
        struct ::stat st;
        if (!self.empty() && ::stat(self.c_str(), &st) == 0)
            s += "/" + self + "/" + std::to_string((long long)st.st_size) +
                 "/" + std::to_string((long long)st.st_mtime);
        else
            s += "/" __DATE__ " " __TIME__;   // no self-path: weaker, but not nothing
        return s;
    }();
    return id;
}

// Paths in a cache entry and in its key must be ABSOLUTE. Relative ones mean
// something different from every directory, and a cache is read from all of
// them: a stored "1.raku" cannot be told apart in a listing, and re-resolves
// against whatever the current directory happens to be when validated.
static std::string precompAbs(const std::string& p) {
    if (p.empty()) return p;
    std::error_code ec;
    std::string a = std::filesystem::absolute(p, ec).lexically_normal().string();
    return ec ? p : a;
}
std::string precompPath(const std::string& srcPath,
                               const std::vector<std::string>& searchPath) {
    std::string dir = precompDir();
    if (dir.empty() || srcPath.empty()) return "";
    std::string abs = precompAbs(srcPath);
    // The search path is part of the key: it decides which file a `use` resolves
    // to when the parser scans for operators, so the same source under a
    // different -I may legitimately parse differently. Recording the dependency
    // CONTENTS is not enough — a different -I selects a different FILE.
    //
    // Each entry is ABSOLUTIZED first, which is what makes the key depend on the
    // working directory. It has to: `.` and `lib` sit in the search path by
    // default, so the very same script run from two directories can legitimately
    // `use` two different modules. Keying on the literal strings gave both runs
    // one entry and made them fight over it.
    std::string sp;
    for (auto& d : searchPath) { sp += precompAbs(d); sp += '\x01'; }
    std::string h = sha1hex(abs + std::string(1, '\0') + sp);
    // one level of fan-out, so a large cache is not one enormous directory
    return dir + "/" + h.substr(0, 2) + "/" + h.substr(2) + ".ast";
}

// entry = [u32] buildId [u32] srcPath [40] srcSha
//         [u32 nDeps] { [u32] depPath [40] depSha }…  [u32] finish  [blob]
//
// srcPath is stored for `--precomp-info`, which would otherwise have nothing to
// show but hashed filenames.
struct PrecompHeader {
    std::string buildId, srcPath, srcSha;
    std::vector<std::pair<std::string, std::string>> deps;   // path, sha
    size_t bodyPos = 0;                                      // where finish+blob start
};

static bool precompParseHeader(const std::string& all, PrecompHeader& h) {
    size_t pos = 0;
    auto u32 = [&](uint32_t& v) {
        if (all.size() - pos < 4) return false;
        std::memcpy(&v, all.data() + pos, 4); pos += 4; return true;
    };
    auto str = [&](std::string& out) {
        uint32_t len = 0;
        if (!u32(len) || all.size() - pos < len) return false;
        out.assign(all, pos, len); pos += len; return true;
    };
    if (!str(h.buildId) || !str(h.srcPath)) return false;
    if (all.size() - pos < 40) return false;
    h.srcSha.assign(all, pos, 40); pos += 40;
    uint32_t n = 0;
    if (!u32(n) || n > 4096) return false;
    for (uint32_t i = 0; i < n; i++) {
        std::string dp;
        if (!str(dp) || all.size() - pos < 40) return false;
        h.deps.push_back({dp, all.substr(pos, 40)}); pos += 40;
    }
    h.bodyPos = pos;
    return true;
}
bool precompRead(const std::string& path, const std::string& src,
                        std::string& blobOut, std::string& finishOut) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss; ss << in.rdbuf();
    const std::string all = ss.str();
    PrecompHeader h;
    if (!precompParseHeader(all, h)) return false;
    if (h.buildId != precompBuildId()) return false;   // a different rakupp wrote it
    if (h.srcSha != sha1hex(src)) return false;        // the source changed
    for (auto& d : h.deps) {
        std::ifstream df(d.first, std::ios::binary);
        if (!df) return false;                         // dependency moved: miss
        std::ostringstream ds; ds << df.rdbuf();
        if (sha1hex(ds.str()) != d.second) return false; // its operators may differ: miss
    }
    size_t pos = h.bodyPos;
    uint32_t flen = 0;
    if (all.size() - pos < 4) return false;
    std::memcpy(&flen, all.data() + pos, 4); pos += 4;
    if (all.size() - pos < flen) return false;
    finishOut.assign(all, pos, flen); pos += flen;
    blobOut.assign(all, pos, std::string::npos);
    return true;
}

// Drop the entries in ONE fan-out bucket whose source file no longer exists.
//
// An edited source keeps its entry (it is rewritten in place) and a source from
// another rakupp build keeps its entry (that build still wants it); only a
// source that is GONE leaves an entry nothing will ever ask for again — a
// deleted checkout, a module version zef replaced, a per-run temp directory.
// Those accumulated without limit before.
//
// Called after a write that ADDS an entry — the only kind that grows the cache,
// and a moment we are already touching this directory. Every bucket still gets
// swept eventually, without a timer, a policy, or a flag to explain.
static void precompSweepBucket(const std::string& entryPath) {
    auto slash = entryPath.rfind('/');
    if (slash == std::string::npos) return;
    std::string bucket = entryPath.substr(0, slash);
    std::error_code ec;
    for (std::filesystem::directory_iterator it(bucket, ec), end; !ec && it != end; ++it) {
        if (it->path() == entryPath) continue;             // the one just written
        if (it->path().extension() != ".ast") continue;
        // only the source path is wanted, and it is the second field: read the
        // two length-prefixed strings at the front, never the tree behind them.
        // Reading whole entries cost a bucket's worth of ASTs per write — with
        // Roast's packages cached once per search path, ~550 entries and ~1.7 MB
        // a bucket, two seconds before a Cro server started after a rebuild.
        std::ifstream in(it->path(), std::ios::binary);
        if (!in) continue;
        auto field = [&](std::string& out) {
            uint32_t n = 0;
            if (!in.read(reinterpret_cast<char*>(&n), 4) || n > 65536) return false;
            out.resize(n);
            return (bool)in.read(out.data(), (std::streamsize)n);
        };
        std::string buildId, srcPath;
        if (!field(buildId) || !field(srcPath)) continue;  // leave what we cannot read
        in.close();
        if (srcPath.empty()) continue;
        std::error_code e2;
        if (std::filesystem::exists(srcPath, e2) || e2) continue;
        std::filesystem::remove(it->path(), e2);           // a loser in a race is fine
    }
}
void precompWrite(const std::string& path, const std::string& srcPath,
                         const std::string& src,
                         const std::string& blob, const std::string& finish,
                         const std::vector<std::pair<std::string, std::string>>& deps) {
    auto slash = path.rfind('/');
    if (slash != std::string::npos) {
        std::error_code ec;
        std::filesystem::create_directories(path.substr(0, slash), ec);
        if (ec) return;
    }
    // write to a unique temp then rename: a concurrent reader sees either the old
    // entry or the complete new one, never a half-written file
    std::string tmp = path + "." + std::to_string((long)::getpid()) + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary);
        if (!out) return;
        auto u32 = [&](uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
        auto str = [&](const std::string& v) { u32((uint32_t)v.size()); out.write(v.data(), (std::streamsize)v.size()); };
        str(precompBuildId());
        str(precompAbs(srcPath));
        { std::string h = sha1hex(src); out.write(h.data(), 40); }
        u32((uint32_t)deps.size());
        for (auto& d : deps) {
            str(precompAbs(d.first));
            std::string h = sha1hex(d.second);
            out.write(h.data(), 40);
        }
        u32((uint32_t)finish.size());
        out.write(finish.data(), (std::streamsize)finish.size());
        out.write(blob.data(), (std::streamsize)blob.size());
        if (!out) { out.close(); std::remove(tmp.c_str()); return; }
    }
    std::error_code ec;
    // A write that REPLACES an entry (an edited source, or every entry the first
    // time a rebuilt rakupp runs) cannot grow the cache, so it leaves the sweep
    // alone; only a write that ADDS an entry looks for one to drop. Sweeping on
    // every write opened the whole bucket each time — after a rebuild, ~100
    // rewrites for Cro and some 55,000 files read before its server started.
    const bool adds = !std::filesystem::exists(path, ec);
    std::filesystem::rename(tmp, path, ec); // atomic: replaces this file's previous entry
    if (ec) { std::remove(tmp.c_str()); return; }
    if (adds) precompSweepBucket(path);
}

// The two switches, for --precomp-info and --precomp-modules=/--precomp-files=.
bool precompModulesOn() { return precompModulesEnabled(); }
bool precompFilesOn()   { return precompProgramEnabled(); }
std::string precompModulesSource() { return precompHalfSource("RAKUPP_PRECOMP_MODULES", "precomp-modules"); }
std::string precompFilesSource()   { return precompHalfSource("RAKUPP_PRECOMP_FILES", "precomp-files"); }
std::string precompConfigPath()    { return configPath(); }

// Rewrite one key in the config, leaving every other line as the user wrote it —
// including comments and anything a later version adds.
bool precompSetSetting(const std::string& key, bool on) {
    if (!(key == "precomp-modules" || key == "precomp-files")) return false;
    std::string path = configPath();
    if (path.empty()) return false;
    auto slash = path.rfind('/');
    if (slash != std::string::npos) {
        std::error_code ec;
        std::filesystem::create_directories(path.substr(0, slash), ec);
        if (ec) return false;
    }
    std::vector<std::string> lines;
    bool replaced = false;
    { std::ifstream in(path);
      std::string line;
      while (std::getline(in, line)) {
          std::string probe = line;
          auto hash = probe.find('#');
          if (hash != std::string::npos) probe.erase(hash);
          auto eq = probe.find('=');
          bool isKey = false;
          if (eq != std::string::npos) {
              std::string k = probe.substr(0, eq);
              auto a = k.find_first_not_of(" \t");
              auto b = k.find_last_not_of(" \t");
              isKey = a != std::string::npos && k.substr(a, b - a + 1) == key;
          }
          if (isKey) { lines.push_back(key + " = " + (on ? "on" : "off")); replaced = true; }
          else lines.push_back(line);
      } }
    if (!replaced) {
        if (lines.empty())
            lines.push_back("# rakupp settings. See `rakupp --precomp-info` and docs/guide/CACHING.md.");
        lines.push_back(key + " = " + (on ? "on" : "off"));
    }
    std::string tmp = path + ".tmp";
    { std::ofstream out(tmp);
      if (!out) return false;
      for (auto& l : lines) out << l << "\n";
      if (!out) return false; }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) { std::remove(tmp.c_str()); return false; }
    return true;
}

// Where entries WOULD go, regardless of whether either switch is on — reporting
// needs to name the location even when nothing is being cached.
std::string precompCacheDir() { return precompDir(); }

bool precompLoadProgram(const std::string& srcPath, const std::string& src,
                        const std::vector<std::string>& searchPath,
                        Program& out, std::string& finishOut) {
    if (!precompProgramEnabled()) return false;
    std::string cpath = precompPath(srcPath, searchPath);
    if (cpath.empty()) return false;
    std::string blob;
    if (!precompRead(cpath, src, blob, finishOut)) return false;
    try { deserializeAst(blob, out); }
    catch (AstSerialError&) { out.stmts.clear(); finishOut.clear(); return false; }
    return true;
}

void precompStoreProgram(const std::string& srcPath, const std::string& src,
                         const std::vector<std::string>& searchPath,
                         const Program& prog, const std::string& finish,
                         const std::vector<std::pair<std::string, std::string>>& deps) {
    if (!precompProgramEnabled()) return;
    std::string cpath = precompPath(srcPath, searchPath);
    if (cpath.empty()) return;
    try { precompWrite(cpath, srcPath, src, serializeAst(prog), finish, deps); }
    catch (AstSerialError&) {} // a construct the format can't hold: just don't cache
}

std::vector<PrecompEntry> precompCacheList() {
    std::vector<PrecompEntry> out;
    std::string dir = precompDir();
    if (dir.empty()) return out;
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(dir, ec), end; !ec && it != end; ++it) {
        if (!it->is_regular_file(ec) || it->path().extension() != ".ast") continue;
        std::ifstream in(it->path(), std::ios::binary);
        if (!in) continue;
        std::ostringstream ss; ss << in.rdbuf();
        PrecompHeader h;
        auto sz = (unsigned long long)std::filesystem::file_size(it->path(), ec);
        if (ec) { ec.clear(); sz = 0; }
        if (!precompParseHeader(ss.str(), h)) { out.push_back({"(unreadable)", sz, false, true}); continue; }
        std::ifstream sf(h.srcPath, std::ios::binary);
        bool orphan = !sf;                                 // the source itself is gone
        bool usable = !orphan && h.buildId == precompBuildId();
        if (usable) { // still current only if the source is unchanged
            std::ostringstream s2; s2 << sf.rdbuf();
            usable = sha1hex(s2.str()) == h.srcSha;
        }
        out.push_back({h.srcPath, sz, usable, orphan});
    }
    std::sort(out.begin(), out.end(),
              [](const PrecompEntry& a, const PrecompEntry& b) { return a.source < b.source; });
    return out;
}

std::pair<size_t, unsigned long long> precompCacheClear() {
    size_t n = 0; unsigned long long bytes = 0;
    std::string dir = precompDir();
    if (dir.empty()) return {0, 0};
    std::error_code ec;
    std::vector<std::filesystem::path> dirs;
    for (std::filesystem::recursive_directory_iterator it(dir, ec), end; !ec && it != end; ++it) {
        if (it->is_directory(ec)) { dirs.push_back(it->path()); continue; }
        if (!it->is_regular_file(ec)) continue;
        auto sz = std::filesystem::file_size(it->path(), ec);
        if (ec) { ec.clear(); continue; }
        if (std::filesystem::remove(it->path(), ec)) { n++; bytes += sz; }
    }
    // …and the fan-out directories the entries lived in. Deepest first, so a
    // nested one is gone before its parent is tried; plain remove() declines a
    // directory that still holds something, which is the check we want. The
    // cache root itself stays — it is where the next entry goes.
    std::sort(dirs.begin(), dirs.end(), [](const std::filesystem::path& a, const std::filesystem::path& b) {
        return a.string().size() > b.string().size();
    });
    for (auto& d : dirs) { ec.clear(); std::filesystem::remove(d, ec); }
    return {n, bytes};
}
std::map<std::string, EmbeddedModule>& embeddedModules() {
    static std::map<std::string, EmbeddedModule> m;
    return m;
}

void rakuppRegisterModule(const std::string& name, const char* blob, size_t blobLen,
                          const std::string& finish) {
    embeddedModules()[name] = EmbeddedModule{std::string(blob, blobLen), finish};
}

// A `use` name with no file behind it: a pragma, or a version literal (`use
// v6.d`). Kept at file scope because the module BUNDLER needs the same answer —
// it must not try to embed `strict`. Sits next to the comment explaining why the
// list is explicit rather than "whatever failed to resolve".
//
// PRAGMAS have no file to find, so they must not be mistaken for a missing
// module now that missing is fatal. This list was short enough to reject real
// Raku pragmas: `use newline :lf` and `no precompilation` are both ordinary
// language features with no module behind them, and rejecting them cost three
// Roast files that Rakudo passes. rakupp ignores the ones it does not implement,
// which is right — they change compilation details, not semantics it can observe
// — but ignoring them must be a deliberate entry here rather than a side effect
// of the lookup failing.
bool isPragmaName(const std::string& name) {  // shared with SlimScan.cpp (module-vs-pragma)
    static const std::set<std::string> pragmas = {
        "strict", "fatal", "lib", "isms", "nqp", "soft", "worries", "experimental",
        // `use js` — the JavaScript target. Lowercase because it is compiler
        // territory, like `strict` and `nqp`: there is no distribution named js
        // and nothing to claim in the ecosystem's module namespace. `JS` stays
        // capitalised as the TERM for the host's global object.
        "js", "JS",
        "variables", "attributes", "cur", "Slang", "MONKEY-SEE-NO-EVAL", "MONKEY-TYPING",
        "MONKEY", "MONKEY-GUTS", "Test", "v6", "v6.c", "v6.d", "v6.e",
        "NativeCall",  // its `is native` FFI is handled natively by the compiler
        // Rakudo ships Pod::To::Text in CORE, so a dist `use`s it and expects
        // `pod2text` to be there; there is no distribution to install. The
        // renderer is native (see Pod.cpp), so there is no file either.
        "Pod::To::Text",
        // …and the types WITHOUT the machinery, which upstream ships as its own
        // compunit and dists `use` directly (Font::FreeType's Raw/Defs, and the
        // thirteen dists behind it). Built in here too, so there is no file.
        "NativeCall::Types",
        // The ecosystem's `if` dist: `use if;` then `use Foo:if(EXPR)`. The
        // colonpair is read natively by the `use` parser (UseStmt::ifCond)
        // and the load skipped when it is false, so the dist has nothing
        // left to do — and loading it anyway ran its actions-only slang
        // into the refusal. Crypt::Random opens with the pair, and UUID::V4
        // and six more dists stand behind it.
        "if",
        // pragmas Rakudo accepts that rakupp does not act on
        "newline", "precompilation", "trace", "dynamic-scope", "snapper",
        "invocant", "internals", "parameters", "routines", "subroutines",
        "absolute", "dispatch", "DEPRECATED",
    };
    if (pragmas.count(name)) return true;
    return name.size() >= 2 && name[0] == 'v' && ascii::isdigit((unsigned char)name[1]);
}
// ---- module bundling for --exe / --aot ---------------------------------
//
// Find a module's SOURCE the way loadModule does: the search path first (trying
// both <base>/ and <base>/lib/), then the installed zef/Rakudo repositories via
// their short-name index. Returns false when nothing matches.
// Module names are case-sensitive; a case-insensitive filesystem (APFS,
// NTFS) opens Config.raku as config.raku, so a stray lowercase file silently
// SHADOWS the real dist (a grammar demo named config.raku ate the ecosystem
// Config for an afternoon). realpath does NOT case-correct on APFS — compare
// the actual directory entry byte-exact. Both resolver loops use this.
bool dirEntryCaseExact(const std::string& cand) {
#ifndef _WIN32
    size_t slash = cand.find_last_of('/');
    if (slash == std::string::npos) return true;
    std::string parent = slash == 0 ? "/" : cand.substr(0, slash);
    std::string want = cand.substr(slash + 1);
    bool exact = false;
    if (DIR* dp = opendir(parent.c_str())) {
        while (struct dirent* de = readdir(dp))
            if (want == de->d_name) { exact = true; break; }
        closedir(dp);
    }
    return exact;
#else
    (void)cand;
    return true; // Windows: GetFinalPathNameByHandle territory, out of scope
#endif
}
// Version ORDER, for choosing among installed candidates: dot-separated
// segments compare numerically ("0.1.10" is newer than "0.1.9"), a missing
// segment is 0 — the same reading verSatisfies gives a segment. <0 / 0 / >0.
int verCmp(const std::string& a, const std::string& b) {
    auto segs = [](const std::string& s) {
        std::vector<long> out; long cur = 0; bool any = false;
        for (char ch : s) {
            if (ch >= '0' && ch <= '9') { cur = cur * 10 + (ch - '0'); any = true; }
            else if (ch == '.') { out.push_back(any ? cur : 0); cur = 0; any = false; }
        }
        out.push_back(any ? cur : 0);
        return out;
    };
    auto av = segs(a), bv = segs(b);
    size_t n = std::max(av.size(), bv.size());
    for (size_t i = 0; i < n; i++) {
        long x = i < av.size() ? av[i] : 0, y = i < bv.size() ? bv[i] : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}
// Pick ONE dist out of a short/<sha1(name)>/ index directory, which holds one
// 5-line entry file per installed dist providing the name (ver / auth / api /
// source-sha / dist-id). Several versions routinely COEXIST — installing a
// dependency pinned `:ver<0.1.7>` does not remove an already-installed 0.1.8 —
// and readdir order is filesystem-arbitrary (APFS hashes names), so taking the
// first entry loaded Statistics::Distributions as 0.1.7 or 0.1.8 by directory
// hash. The winner is chosen instead: the NEWEST version that satisfies
// verReq, which is how Rakudo resolves the same store. False when none does.
bool pickInstalledDist(const std::string& shortDir, const std::string& verReq,
                              std::string& entryOut, std::vector<std::string>& linesOut) {
    DIR* dd = opendir(shortDir.c_str());
    if (!dd) return false;
    std::vector<std::string> entries;
    while (struct dirent* e = readdir(dd)) { std::string n = e->d_name; if (n != "." && n != "..") entries.push_back(n); }
    closedir(dd);
    entryOut.clear();
    for (auto& cand : entries) {
        std::ifstream meta(shortDir + "/" + cand);
        std::vector<std::string> ls; std::string ln;
        while (std::getline(meta, ln)) ls.push_back(ln);
        if (ls.size() < 4 || ls[3].empty()) continue;
        if (!verReq.empty() && !verSatisfies(ls[0], verReq)) continue;
        if (!entryOut.empty() && verCmp(ls[0], linesOut[0]) <= 0) continue;
        entryOut = cand; linesOut = std::move(ls);
    }
    return !entryOut.empty();
}

static bool findModuleSourceFor(const std::string& name,
                                const std::vector<std::string>& searchPath,
                                std::string& pathOut, std::string& srcOut,
                                bool sixE) {
    std::string rel = name;
    for (size_t p = rel.find("::"); p != std::string::npos; p = rel.find("::")) rel.replace(p, 2, "/");
    // The extensions a NAME resolves to, and `.raku` is deliberately not among
    // them: Rakudo's CompUnit::Repository::FileSystem does not take it even
    // with an explicit -I, `.raku` is the extension a PROGRAM wears, and
    // treating it as a module made every script beside the one being run a
    // candidate module — a scratch file named `paths.raku` shadowed the
    // installed `paths` distribution and reported "Undefined routine 'paths'".
    // A dist that really does keep a module in a `.raku` file still loads: a
    // META6 `provides` path is explicit and is tried before any of these.
    // 6.e then drops `.pm`, as Rakudo does, leaving .rakumod and .pm6.
    static const char* extsAll[] = {".rakumod", ".pm6", ".pm"};
    static const char* exts6e[]  = {".rakumod", ".pm6"};
    const char* const* exts = sixE ? exts6e : extsAll;
    const size_t nExts = sixE ? 2 : 3;
    if (rakuppShadowModule(name, pathOut, srcOut)) return true;   // ahead of everything, as the loader
    for (auto& entry : searchPath) {
        std::string storePre;
        if (repoSpecStore(entry, storePre)) continue; // an inst# store is not a directory
        const std::string base = repoSpecDir(entry);
        for (const std::string& dir : {base, base + "/lib"})
            for (size_t xi = 0; xi < nExts; xi++) {
                const char* ext = exts[xi];
                std::string cand = dir + "/" + rel + ext;
                std::ifstream in(cand);
                if (!in) continue;
                if (!dirEntryCaseExact(cand)) continue; // config.raku is not Config.raku
                std::ostringstream ss; ss << in.rdbuf();
                pathOut = cand; srcOut = ss.str();
                return true;
            }
    }
    std::string nameSha = sha1hex(name);
    for (auto& repo : repoPrefixesFor(searchPath)) {
        // no version constraint in scope on this path (parser export scan,
        // bundler): the newest installed candidate is the one the
        // unconstrained loader will run, so scan that one
        std::string entry; std::vector<std::string> lines;
        if (!pickInstalledDist(repo + "/short/" + nameSha, "", entry, lines)) continue;
        std::ifstream src(repo + "/sources/" + lines[3]);
        if (!src) continue;
        std::ostringstream ss; ss << src.rdbuf();
        pathOut = repo + "/sources/" + lines[3]; srcOut = ss.str();
        return true;
    }
    return false;
}

// Directory-only existence check: does a plain `-I`/`use lib` entry hold this
// module? A few stat() calls, and deliberately NOT a store lookup — reading the
// store is the cost the caller is avoiding.
//
// It sits HERE rather than in Parser.cpp beside its one parser-side caller
// because `--slim` cuts Parser.cpp, and dataNativeUse() below needs it in every
// build: a slim binary that has given up EVAL still has to decide whether an
// explicit -I beats the compiler's answer to `use Data::Native`. Its two
// siblings, rakuppFindModuleSource and rakuppCompilerAnswersModule, are in this
// file for the same reason and are declared from the same header.
bool moduleFileOnPath(const std::string& module,
                      const std::vector<std::string>& paths, bool sixE) {
    std::string rel = module;
    for (size_t p = rel.find("::"); p != std::string::npos; p = rel.find("::")) rel.replace(p, 2, "/");
    static const char* extsAll[] = {".rakumod", ".pm6", ".pm"};  // not `.raku` — see findModuleSourceFor
    for (auto& base : paths) {
        std::string storePre;
        if (base.empty() || repoSpecStore(base, storePre)) continue; // an inst# store is not a directory (`base[0] == '#'` never matched one)
        for (size_t e = 0; e < (sixE ? 2u : 3u); e++) {
            struct ::stat st;
            std::string cand = repoSpecDir(base) + "/" + rel + extsAll[e]; // `file#/dir` names a directory
            if (::stat(cand.c_str(), &st) == 0) return true;
            cand = base + "/lib/" + rel + extsAll[e];
            if (::stat(cand.c_str(), &st) == 0) return true;
        }
    }
    return false;
}

// Same resolution the loader uses, exposed for the PARSER: a module that is
// installed (zef) rather than sitting on a lib path must still be scanned at
// parse time, or its exported operators and sigilless constants are invisible
// to the file that `use`s it.
bool rakuppFindModuleSource(const std::string& name,
                            const std::vector<std::string>& searchPath,
                            std::string& pathOut, std::string& srcOut, bool sixE) {
    return findModuleSourceFor(name, searchPath, pathOut, srcOut, sixE);
}

static std::map<std::string, std::string>& shadowModuleSources() {
    static std::map<std::string, std::string> m;
    return m;
}
void rakuppRegisterShadowModule(const std::string& name, const char* src, size_t len) {
    shadowModuleSources()[name] = std::string(src, len);
}
bool rakuppShadowModule(const std::string& name, std::string& pathOut, std::string& srcOut) {
    auto& m = shadowModuleSources();
    auto it = m.find(name);
    if (it == m.end()) return false;
    std::string rel = name;
    for (size_t p = rel.find("::"); p != std::string::npos; p = rel.find("::")) rel.replace(p, 2, "/");
    pathOut = "rakupp:rakulib/" + rel + ".rakumod";
    srcOut = it->second;
    return true;
}

// Every module name a tree `use`s, at any depth — a `use` can sit inside a
// class body, a package, or any block, so this walks statements rather than
// scanning only the top level.
static void collectUseNames(const std::vector<StmtPtr>& stmts, std::vector<std::string>& out);

static void collectUseNamesStmt(const Stmt* st, std::vector<std::string>& out) {
    if (!st) return;
    switch (st->kind) {
        case NK::UseStmt: {
            auto* u = static_cast<const UseStmt*>(st);
            if (!u->isNo && !u->module.empty() && u->fromLang.empty()) out.push_back(u->module); // `:from<NQP>`: nothing to bundle
            break;
        }
        case NK::Block: collectUseNames(static_cast<const Block*>(st)->stmts, out); break;
        case NK::ClassDecl: {
            auto* c = static_cast<const ClassDecl*>(st);
            collectUseNames(c->body, out);
            for (auto& m : c->methods) if (m) collectUseNames(m->body, out);
            break;
        }
        case NK::SubDecl: collectUseNames(static_cast<const SubDecl*>(st)->body, out); break;
        case NK::IfStmt: {
            auto* f = static_cast<const IfStmt*>(st);
            for (auto& br : f->branches) if (br.second) collectUseNames(br.second->stmts, out);
            if (f->elseBlock) collectUseNames(f->elseBlock->stmts, out);
            break;
        }
        case NK::WhileStmt: if (auto* b = static_cast<const WhileStmt*>(st)->body.get()) collectUseNames(b->stmts, out); break;
        case NK::ForStmt:   if (auto* b = static_cast<const ForStmt*>(st)->body.get())   collectUseNames(b->stmts, out); break;
        case NK::LoopStmt:  if (auto* b = static_cast<const LoopStmt*>(st)->body.get())  collectUseNames(b->stmts, out); break;
        case NK::RepeatStmt:if (auto* b = static_cast<const RepeatStmt*>(st)->body.get())collectUseNames(b->stmts, out); break;
        case NK::GivenStmt: {
            auto* g = static_cast<const GivenStmt*>(st);
            if (g->body) collectUseNames(g->body->stmts, out);
            if (g->elseBody) collectUseNames(g->elseBody->stmts, out);
            break;
        }
        case NK::WhenStmt: if (auto* b = static_cast<const WhenStmt*>(st)->body.get()) collectUseNames(b->stmts, out); break;
        default: break;
    }
}

static void collectUseNames(const std::vector<StmtPtr>& stmts, std::vector<std::string>& out) {
    for (auto& st : stmts) collectUseNamesStmt(st.get(), out);
}

// A DYNAMIC require (`require ::($name)`) escapes the static module graph —
// its target does not exist until run time. The statement spelling
// (`require Foo`) parses as a UseStmt and is embedded like any `use`; only
// the expression form (Unary op "require") is undetectable, and B1 wants it
// REPORTED, not guessed at. Walks the same statement shapes collectUseNames
// does, drilling into the two expression positions the parser produces.
static bool exprHasDynRequire(const Expr* e) {
    if (!e) return false;
    if (e->kind == NK::Unary && static_cast<const Unary*>(e)->op == "require") return true;
    if (e->kind == NK::Assign) {
        auto* a = static_cast<const Assign*>(e);
        return exprHasDynRequire(a->target.get()) || exprHasDynRequire(a->value.get());
    }
    return false;
}
static void collectDynRequires(const std::vector<StmtPtr>& stmts, bool& found) {
    for (auto& st : stmts) {
        if (!st || found) return;
        switch (st->kind) {
            case NK::ExprStmt:
                if (exprHasDynRequire(static_cast<const ExprStmt*>(st.get())->e.get())) found = true;
                break;
            case NK::Block: collectDynRequires(static_cast<const Block*>(st.get())->stmts, found); break;
            case NK::SubDecl: collectDynRequires(static_cast<const SubDecl*>(st.get())->body, found); break;
            case NK::ClassDecl: {
                auto* c = static_cast<const ClassDecl*>(st.get());
                collectDynRequires(c->body, found);
                for (auto& m : c->methods) if (m) collectDynRequires(m->body, found);
                break;
            }
            case NK::IfStmt: {
                auto* f = static_cast<const IfStmt*>(st.get());
                for (auto& br : f->branches) if (br.second) collectDynRequires(br.second->stmts, found);
                if (f->elseBlock) collectDynRequires(f->elseBlock->stmts, found);
                break;
            }
            case NK::WhileStmt: if (auto* b = static_cast<const WhileStmt*>(st.get())->body.get()) collectDynRequires(b->stmts, found); break;
            case NK::ForStmt:   if (auto* b = static_cast<const ForStmt*>(st.get())->body.get())   collectDynRequires(b->stmts, found); break;
            default: break;
        }
    }
}

// The shared libraries `is native(...)` subs in `stmts` will dlopen at run
// time (MODULES-PLAN B5): a standalone binary cannot embed them, so it must
// name them. "" (the default namespace: libc and friends) is reported as
// such; a runtime-computed library path is reported as unknowable.
static void collectNativeLibNames(const std::vector<StmtPtr>& stmts, std::set<std::string>& out) {
    for (auto& st : stmts) {
        if (!st) continue;
        if (st->kind == NK::SubDecl) {
            auto* sd = static_cast<const SubDecl*>(st.get());
            if (sd->isNative) {
                if (!sd->nativeLib.empty()) out.insert(sd->nativeLib);
                else if (!sd->nativeLibSub.empty() || sd->nativeLibExpr)
                    out.insert("<computed at run time>");
                else out.insert("<default namespace (libc)>");
            }
            collectNativeLibNames(sd->body, out);
        }
        else if (st->kind == NK::ClassDecl) {
            auto* c = static_cast<const ClassDecl*>(st.get());
            collectNativeLibNames(c->body, out);
            for (auto& m : c->methods)
                if (m && m->isNative) {
                    if (!m->nativeLib.empty()) out.insert(m->nativeLib);
                    else if (!m->nativeLibSub.empty() || m->nativeLibExpr)
                        out.insert("<computed at run time>");
                    else out.insert("<default namespace (libc)>");
                }
        }
        else if (st->kind == NK::Block)
            collectNativeLibNames(static_cast<const Block*>(st.get())->stmts, out);
    }
}

void collectExportedSubNames(const std::vector<StmtPtr>& stmts, std::set<std::string>& out) {
    for (auto& st : stmts) {
        if (!st) continue;
        if (st->kind == NK::SubDecl) {
            auto* sd = static_cast<SubDecl*>(st.get());
            if (sd->isExport && !sd->name.empty()) out.insert(sd->name);
        } else if (st->kind == NK::ClassDecl)
            // `is export` subs may sit inside a BRACED module/package body
            // (Cro::HTTP::Router exports `get`/`post`/… from `module … { }`)
            collectExportedSubNames(static_cast<ClassDecl*>(st.get())->body, out);
    }
}
// Name → the export TAGS of an `is export(:foo :bar)` sub. Only tag-bearing subs
// are recorded; a plain `is export` (default) has no entry. A tag that is not
// DEFAULT/MANDATORY is a SELECTIVE export — published only when the importer
// asks for it (`use Mod :foo`), as in Rakudo. `sub prompt is export(:prompt)`.
void collectExportTagsByName(const std::vector<StmtPtr>& stmts,
                             std::map<std::string, std::vector<std::string>>& out) {
    for (auto& st : stmts) {
        if (!st) continue;
        if (st->kind == NK::SubDecl) {
            auto* sd = static_cast<SubDecl*>(st.get());
            if (sd->isExport && !sd->name.empty() && !sd->exportTags.empty())
                out[sd->name] = sd->exportTags;
        } else if (st->kind == NK::ClassDecl)
            collectExportTagsByName(static_cast<ClassDecl*>(st.get())->body, out);
    }
}

// The platform file name a `libraries/<base>` resource actually lives under —
// META6 says `libraries/sha1` and the file is `libraries/libsha1.dylib`. The
// same rule buildSourceResourceMap applies when resolving from disk, kept here
// so an embedded copy is written under the name dlopen will look for.
static std::string libraryResourceRel(const std::string& key, const std::string& distRoot) {
    if (key.rfind("libraries/", 0) != 0) return key;
    std::string dir = "libraries/", base = key.substr(10);
    size_t sl = base.rfind('/');
    if (sl != std::string::npos) { dir += base.substr(0, sl + 1); base = base.substr(sl + 1); }
    std::vector<std::string> cands;
#if defined(_WIN32)
    cands = {base + ".dll", "lib" + base + ".dll"};
#elif defined(__APPLE__)
    cands = {"lib" + base + ".dylib", "lib" + base + ".so"};
#else
    cands = {"lib" + base + ".so", "lib" + base + ".dylib"};
#endif
    cands.push_back(base);
    if (!distRoot.empty())
        for (auto& c : cands) {
            std::ifstream f(distRoot + "/resources/" + dir + c);
            if (f) return dir + c;
        }
    return dir + cands[0];
}

static std::string slurpFileBytes(const std::string& path, bool& ok) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { ok = false; return {}; }
    std::ostringstream ss; ss << f.rdbuf();
    ok = true;
    return ss.str();
}

// The distribution a resolved module file belongs to, and its resources, so the
// compile modes can carry both into the binary (MODULES-PLAN B3). Two shapes,
// matching the two the loader resolves from:
//
//   - a SOURCE checkout — walk up from the module file for the META6.json that
//     describes it, then read its `resources` array. Walking beats stripping
//     `/lib/` because a META6 `provides` may point anywhere in the tree.
//   - an INSTALLED store blob (`<repo>/sources/<id>`) — the dist record names
//     each `resources/<key>` and the content-addressed file holding it.
//
// False when there is no distribution at all: a bare `-I` directory of loose
// modules has no META6, so there is nothing to carry and nothing is claimed.
static bool embeddedDistContext(const std::string& modPath, const std::string& modName,
                                std::string& keyOut, std::string& metaOut,
                                std::vector<BundledResource>& resOut) {
    // --- the installed store ------------------------------------------------
    size_t sp = modPath.rfind("/sources/");
    if (sp != std::string::npos) {
        const std::string repo = modPath.substr(0, sp);
        std::string entry; std::vector<std::string> lines;
        if (pickInstalledDist(repo + "/short/" + sha1hex(modName), "", entry, lines)) {
            bool ok = false;
            const std::string j = slurpFileBytes(repo + "/dist/" + entry, ok);
            if (ok) {
                keyOut = "store:" + entry;
                metaOut = j;                       // the dist record IS its meta
                // the same targeted scan buildResourceMap uses: "resources/<key>":"<id>"
                const std::string tag = "\"resources/";
                for (size_t p = 0; (p = j.find(tag, p)) != std::string::npos; ) {
                    size_t ks = p + tag.size(), ke = j.find('"', ks);
                    if (ke == std::string::npos) break;
                    const std::string key = j.substr(ks, ke - ks);
                    size_t c = j.find(':', ke);
                    if (c == std::string::npos) { p = ke + 1; continue; }
                    size_t vs = j.find('"', c), ve = vs == std::string::npos ? vs : j.find('"', vs + 1);
                    if (vs == std::string::npos || ve == std::string::npos) break;
                    bool rok = false;
                    std::string bytes = slurpFileBytes(repo + "/resources/" +
                                                       j.substr(vs + 1, ve - vs - 1), rok);
                    if (rok) resOut.push_back({key, libraryResourceRel(key, ""), std::move(bytes)});
                    p = ve + 1;
                }
                return true;
            }
        }
        return false;
    }
    // --- a source checkout --------------------------------------------------
    std::error_code ec;
    std::filesystem::path dir = std::filesystem::path(modPath).parent_path();
    std::string distRoot;
    for (int up = 0; up < 8 && !dir.empty(); up++) {
        if (std::filesystem::exists(dir / "META6.json", ec)) { distRoot = dir.string(); break; }
        auto parent = dir.parent_path();
        if (parent == dir) break;
        dir = parent;
    }
    if (distRoot.empty()) return false;
    bool ok = false;
    const std::string j = slurpFileBytes(distRoot + "/META6.json", ok);
    if (!ok) return false;
    keyOut = "src:" + distRoot;
    metaOut = j;
    // the `resources` array holds bare strings, as buildSourceResourceMap reads it
    size_t rp = j.find("\"resources\"");
    if (rp == std::string::npos) return true;          // a dist, simply without resources
    size_t lb = j.find('[', rp), rb = lb == std::string::npos ? lb : j.find(']', lb);
    if (lb == std::string::npos || rb == std::string::npos) return true;
    for (size_t p = lb + 1; p < rb; ) {
        size_t vs = j.find('"', p);
        if (vs == std::string::npos || vs >= rb) break;
        size_t ve = j.find('"', vs + 1);
        if (ve == std::string::npos || ve > rb) break;
        const std::string key = j.substr(vs + 1, ve - vs - 1);
        const std::string rel = libraryResourceRel(key, distRoot);
        bool rok = false;
        std::string bytes = slurpFileBytes(distRoot + "/resources/" + rel, rok);
        if (rok) resOut.push_back({key, rel, std::move(bytes)});
        p = ve + 1;
    }
    return true;
}

std::vector<BundledModule> collectModuleGraph(const Program& prog,
                                              const std::vector<std::string>& searchPath,
                                              std::set<std::string>* exportsOut,
                                              std::vector<ModuleSkip>* skipsOut,
                                              std::set<std::string>* nativeLibsOut) {
    std::vector<BundledModule> out;
    std::set<std::string> seen;
    std::set<std::string> distsSeen;   // a dist's resources travel once, not once per module
    std::vector<std::string> queue;
    collectUseNames(prog.stmts, queue);
    if (nativeLibsOut) collectNativeLibNames(prog.stmts, *nativeLibsOut);
    if (skipsOut) {
        bool dynReq = false;
        collectDynRequires(prog.stmts, dynReq);
        if (dynReq) skipsOut->push_back({"<dynamic require>",
            "a runtime-computed `require ::($name)` — its target cannot be known at compile time"});
    }

    for (size_t qi = 0; qi < queue.size(); qi++) {
        const std::string name = queue[qi];
        if (name.empty() || !seen.insert(name).second) continue;
        if (isPragmaName(name)) continue;                 // no file behind it
        // A module the COMPILER answers has no file to embed and needs none at
        // run time — the binary carries the primitives already. Reporting it as
        // a skip is not merely noisy: --standalone refuses to build when the
        // skip list is non-empty, so `rakupp --exe --standalone` on a program
        // using Data::Native was refused for needing a disk it does not touch.
        // An explicit -I still wins here exactly as it does in the loader, so a
        // checkout under -I is collected and embedded normally.
        if (rakuppCompilerAnswersModule(name) &&
            !moduleFileOnPath(name, searchPath, prog.langRev >= 2)) continue;
        std::string path, src;
        if (!findModuleSourceFor(name, searchPath, path, src, prog.langRev >= 2)) {
            // load from disk at run time — silently before MODULES-PLAN B1
            if (skipsOut) skipsOut->push_back({name, "not found on the module search path"});
            continue;
        }
        Program mp;
        std::string finish;
        try {
            Lexer lx(src);
            Parser parser(lx.tokenize());
            parser.libPaths_ = searchPath;                // its own `use`s resolve like the real load
            parser.srcFile_ = path;
            mp = parser.parseProgram();
            finish = lx.finishData();
        } catch (ParseError&) {                           // let the run-time loader report it
            if (skipsOut) skipsOut->push_back({name, "its source does not parse (the run-time loader will report the error)"});
            continue;
        }
        collectUseNames(mp.stmts, queue);                 // depth-first over its dependencies
        if (nativeLibsOut) collectNativeLibNames(mp.stmts, *nativeLibsOut);
        if (skipsOut) {
            bool dynReq = false;
            collectDynRequires(mp.stmts, dynReq);
            if (dynReq) skipsOut->push_back({name,
                "contains a runtime-computed `require ::($name)` — that load escapes the embedded graph"});
        }
        // Before the embeddability checks below: a module that is loaded from
        // DISK at run time still exports these names, and a compiled call site
        // has to resolve them the same way either way.
        if (exportsOut) collectExportedSubNames(mp.stmts, *exportsOut);
        std::string blob;
        try { blob = serializeAst(mp); }
        catch (AstSerialError& e) {                       // not embeddable: disk fallback
            if (skipsOut) skipsOut->push_back({name,
                "its AST does not serialize (" + e.msg + ")"});
            continue;
        }
        // Its distribution comes too (MODULES-PLAN B3): %?RESOURCES and
        // $?DISTRIBUTION are bound from the dist, and an embedded module is
        // loaded from none. The resources and META6 ride on the FIRST module of
        // each dist, so a dist providing a dozen modules carries one copy.
        BundledModule bm{name, std::move(blob), std::move(finish), std::move(src)};
        std::string distKey, distMeta;
        std::vector<BundledResource> res;
        if (embeddedDistContext(path, name, distKey, distMeta, res)) {
            bm.distKey = distKey;
            if (distsSeen.insert(distKey).second) {
                bm.distMeta = std::move(distMeta);
                bm.resources = std::move(res);
            }
        }
        out.push_back(std::move(bm));
    }
    // Dependencies first, so a registration order matching load order costs
    // nothing to reason about (the table is a map, but the emitted code reads
    // top to bottom and this makes it legible).
    std::reverse(out.begin(), out.end());
    return out;
}
// Does a candidate module version satisfy a `use Foo:ver<...>` constraint?
// Forms: "0.0.14+" (>= — the common ecosystem spelling), an exact version, or
// a prefix with ".*". Segments compare numerically; a missing segment is 0.
bool verSatisfies(const std::string& have, const std::string& want) {
    if (want.empty()) return true;
    // `:ver("*")` / `:ver<*>` is the ANYTHING requirement — zef spells every
    // sibling use `:ver($?DISTRIBUTION.meta<version> // '*')`, and the bare
    // star used to fall through to the numeric compare (segments to [0]) and
    // reject every candidate, versioned or not (issue #37's zef leg)
    if (want == "*") return true;
    if (have.empty()) return false;   // constrained use, versionless candidate: skip it
    std::string w = want;
    bool plus = !w.empty() && w.back() == '+';
    if (plus) w.pop_back();
    bool star = w.size() >= 2 && w.compare(w.size() - 2, 2, ".*") == 0;
    if (star) w = w.substr(0, w.size() - 2);
    auto segs = [](const std::string& s) {
        std::vector<long> out; long cur = 0; bool any = false;
        for (char ch : s) {
            if (ch >= '0' && ch <= '9') { cur = cur * 10 + (ch - '0'); any = true; }
            else if (ch == '.') { out.push_back(any ? cur : 0); cur = 0; any = false; }
        }
        out.push_back(any ? cur : 0);
        return out;
    };
    auto a = segs(have), b = segs(w);
    if (star) { // prefix match on the written segments
        for (size_t i = 0; i < b.size(); i++)
            if ((i < a.size() ? a[i] : 0) != b[i]) return false;
        return true;
    }
    size_t n = std::max(a.size(), b.size());
    for (size_t i = 0; i < n; i++) {
        long x = i < a.size() ? a[i] : 0, y = i < b.size() ? b[i] : 0;
        if (x != y) return plus ? x > y : false;
    }
    return true; // equal
}

} // namespace rakupp
