#pragma once
#include "Ast.h"
#include "BigInt.h"
#include <atomic>
#include <cstring>
#include <ostream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "IStr.h"

namespace rakupp {

struct Value;

// A Raku Str inside a Value.
//
// Value is copied by value everywhere — every argument pass, every operand
// evaluation, every list element — so holding a bare std::string meant a long
// string was memcpy'd on each of those. The cost is O(length) per OPERATION,
// which makes any pure-Raku tokenizer O(n^2): JSON::Fast spent 13.9 s on a
// 421 KB document that Rakudo parses in 50 ms, and the profile was all copying,
// not parsing.
//
// So: short strings stay inline, where std::string's own small-buffer
// optimisation already makes a copy free, and anything longer is promoted ONCE,
// at construction, into a shared immutable body. Copying then costs a refcount
// bump. Promotion is eager rather than lazy-on-first-copy precisely because
// rakupp runs work in parallel — a lazy promotion would have to mutate the
// source from a const copy constructor, and two threads copying the same Value
// would race. Eager promotion means a const CowStr is never written to at all.
//
// Because the promoted body is immutable, it is also the right place to cache
// the two string properties the scanning ops recompute per character (see
// asciiState/nGraphemes below).
struct StrBody {
    std::string text;
    // All -1 until computed, then 0/1. Racing threads may each compute one of
    // these, but the text is immutable so they compute the same answer — the
    // store is idempotent and needs no lock.
    mutable std::atomic<signed char> allAscii{-1};   // every byte < 0x80: a byte index is a codepoint index
    mutable std::atomic<signed char> crFree{-1};     // no CR: with allAscii, a byte index is a GRAPHEME index
                                                     // (CR LF is the one ASCII sequence that clusters, GB3)
    mutable std::atomic<long long>   nGraphemes{-1}; // .chars
    // Byte-offset tables for POSITIONAL ops on non-ASCII text, built lazily by
    // cowCpIndex/cowGraphemeIndex (Builtins.cpp). Without them every positional
    // op re-decoded the WHOLE text per call, so one `é` anywhere turned a
    // scanner into O(n²) — the non-ASCII half of STRING-SCAN-QUADRATICS
    // (JSON::Fast on a 203 KB doc: 52.8 s → ASCII-band once cached). Install is
    // a CAS; a racing loser deletes its copy — both compute identical tables
    // from the immutable text. acquire/release: the pointer gates a filled
    // vector, the byteset lesson.
    mutable std::atomic<const std::vector<uint32_t>*> cpIndex{nullptr}; // byte offset of codepoint i, +end sentinel
    mutable std::atomic<const std::vector<uint32_t>*> gIndex{nullptr};  // byte offset of grapheme g, +end sentinel
    explicit StrBody(std::string t) : text(std::move(t)) {}
    ~StrBody() {
        delete cpIndex.load(std::memory_order_relaxed);
        delete gIndex.load(std::memory_order_relaxed);
    }
};

class CowStr {
    // Exactly one of these carries the value: `p_` when set, otherwise `s_`.
    std::string s_;
    std::shared_ptr<const StrBody> p_;
    // Below this, a copy is a couple of words and sharing would cost more than
    // it saves (an allocation per string). Above it, copying is the thing we are
    // here to avoid.
    static constexpr size_t kPromote = 64;

    void take(std::string x) {
        if (x.size() >= kPromote) { p_ = std::make_shared<const StrBody>(std::move(x)); s_.clear(); }
        else { s_ = std::move(x); p_.reset(); }
    }

public:
    CowStr() = default;
    // Both converting constructors are explicit ON PURPOSE. With an implicit
    // std::string -> CowStr alongside the CowStr -> const std::string& below,
    // every `cond ? value.s : someString` became an ambiguous conversion in
    // both directions. Assignment (operator= just below) covers the cases that
    // actually want to convert.
    explicit CowStr(const char* x) { take(std::string(x)); }
    explicit CowStr(std::string x) { take(std::move(x)); }
    CowStr(const CowStr&) = default;
    CowStr(CowStr&&) noexcept = default;
    CowStr& operator=(const CowStr&) = default;
    CowStr& operator=(CowStr&&) noexcept = default;
    CowStr& operator=(std::string x) { take(std::move(x)); return *this; }
    CowStr& operator=(const char* x) { take(std::string(x)); return *this; }

    const std::string& str() const { return p_ ? p_->text : s_; }
    operator const std::string&() const { return str(); }         // NOLINT(google-explicit-constructor)

    // Write access. Detaches from the shared body first, so a mutation never
    // reaches another Value holding the same text. The result stays inline
    // until it is next assigned — which is where promotion happens again.
    std::string& mut() {
        if (p_) { s_ = p_->text; p_.reset(); }
        return s_;
    }
    // The cache lives on the shared body, so it survives only for promoted
    // strings — which is exactly the case that needs it. Short strings are
    // cheap to rescan.
    const StrBody* body() const { return p_.get(); }

    // const std::string forwarding — keeps the ~1,100 read sites unchanged.
    size_t size()  const { return str().size(); }
    bool   empty() const { return str().empty(); }
    const char* c_str() const { return str().c_str(); }
    const char* data()  const { return str().data(); }
    char   back()  const { return str().back(); }
    char operator[](size_t i) const { return str()[i]; }
    std::string substr(size_t p = 0, size_t n = std::string::npos) const { return str().substr(p, n); }
    size_t find(const std::string& x, size_t p = 0) const { return str().find(x, p); }
    size_t find(char x, size_t p = 0) const { return str().find(x, p); }
    size_t find(const char* x, size_t p = 0) const { return str().find(x, p); }
    size_t rfind(const std::string& x, size_t p = std::string::npos) const { return str().rfind(x, p); }
    size_t rfind(char x, size_t p = std::string::npos) const { return str().rfind(x, p); }
    size_t rfind(const char* x, size_t p = std::string::npos) const { return str().rfind(x, p); }
    size_t find_first_not_of(const char* x, size_t p = 0) const { return str().find_first_not_of(x, p); }
    int compare(const std::string& x) const { return str().compare(x); }
    int compare(size_t p, size_t n, const std::string& x) const { return str().compare(p, n, x); }
    std::string::const_iterator begin() const { return str().begin(); }
    std::string::const_iterator end()   const { return str().end(); }

    // Mutating forwards — each detaches.
    void clear() { p_.reset(); s_.clear(); }
    void pop_back() { mut().pop_back(); }
    void resize(size_t n) { mut().resize(n); }
    void resize(size_t n, char c) { mut().resize(n, c); }
    void erase(size_t p, size_t n = std::string::npos) { mut().erase(p, n); }
    void replace(size_t p, size_t n, const std::string& x) { mut().replace(p, n, x); }
    CowStr& operator+=(const std::string& x) { mut() += x; return *this; }
    CowStr& operator+=(const char* x) { mut() += x; return *this; }
    CowStr& operator+=(char x) { mut() += x; return *this; }
};

// The implicit CowStr -> const std::string& conversion does not apply when BOTH
// operands are CowStr (no conversion is considered for a built-in operator with
// no candidate), so the comparisons and concatenation are spelled out.
inline bool operator==(const CowStr& a, const CowStr& b) { return a.str() == b.str(); }
inline bool operator!=(const CowStr& a, const CowStr& b) { return a.str() != b.str(); }
inline bool operator<(const CowStr& a, const CowStr& b)  { return a.str() <  b.str(); }
inline bool operator>(const CowStr& a, const CowStr& b)  { return a.str() >  b.str(); }
inline bool operator<=(const CowStr& a, const CowStr& b) { return a.str() <= b.str(); }
inline bool operator>=(const CowStr& a, const CowStr& b) { return a.str() >= b.str(); }
inline std::string operator+(const CowStr& a, const CowStr& b) { return a.str() + b.str(); }
inline std::string operator+(const CowStr& a, const std::string& b) { return a.str() + b; }
inline std::string operator+(const std::string& a, const CowStr& b) { return a + b.str(); }
inline std::string operator+(const CowStr& a, const char* b) { return a.str() + b; }
inline std::string operator+(const char* a, const CowStr& b) { return a + b.str(); }
inline std::string operator+(const CowStr& a, char b) { return a.str() + b; }
// std::string's own comparisons are templates, and template deduction never
// considers a user-defined conversion — so `value.s == "Int"` needs real
// candidates rather than the conversion operator above.
#define RAKUPP_COWSTR_CMP(OP)                                                                  \
    inline bool operator OP(const CowStr& a, const char* b)        { return a.str() OP b; }    \
    inline bool operator OP(const char* a, const CowStr& b)        { return a OP b.str(); }    \
    inline bool operator OP(const CowStr& a, const std::string& b) { return a.str() OP b; }    \
    inline bool operator OP(const std::string& a, const CowStr& b) { return a OP b.str(); }
RAKUPP_COWSTR_CMP(==)
RAKUPP_COWSTR_CMP(!=)
RAKUPP_COWSTR_CMP(<)
RAKUPP_COWSTR_CMP(>)
RAKUPP_COWSTR_CMP(<=)
RAKUPP_COWSTR_CMP(>=)
#undef RAKUPP_COWSTR_CMP
inline std::ostream& operator<<(std::ostream& o, const CowStr& x) { return o << x.str(); }

// codepoint -> UTF-8 (shared: Str-Range endpoints derive their text from rFrom/rTo)
inline std::string cpToU8(uint32_t cp) {
    std::string o;
    if (cp < 0x80) o += (char)cp;
    else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
    else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
    return o;
}
// …and back. Str ranges and the `...` string sequence both dispatch on the
// CODEPOINT count (Rakudo tests `.codes` there, not bytes), so both live here
// rather than as a lambda re-declared at each site.
inline long long u8CpLen(const std::string& s) {
    long long n = 0;
    for (unsigned char ch : s) if ((ch & 0xC0) != 0x80) n++;
    return n;
}
inline uint32_t u8FirstCp(const std::string& s) {
    if (s.empty()) return 0;
    unsigned char c0 = s[0];
    if (c0 < 0x80) return c0;
    int len = (c0 >> 5) == 0x6 ? 2 : (c0 >> 4) == 0xE ? 3 : 4;
    uint32_t cp = c0 & (0xFF >> (len + 1));
    for (int k = 1; k < len && k < (int)s.size(); k++) cp = (cp << 6) | (s[k] & 0x3F);
    return cp;
}
// ISO 8601 for a Date/DateTime attribute map. Declared here because the value
// model renders one (say, interpolation, a container holding one) and the
// .Str/.gist/.yyyy-mm-dd method arm renders the same thing — it was written out
// twice, verbatim, including the year>9999 `+` prefix, the ±HH:MM suffix and the
// fractional-second branch.
std::string dateGist(const std::map<std::string, Value>& h, bool isDate);

struct Env;
class Interpreter;

using ValueList = std::vector<Value>;
using BuiltinFn = std::function<Value(Interpreter&, ValueList&)>;

// A callable: either a user sub (params+body+closure) or a builtin.
struct Callable {
    std::string pkg; // enclosing package name ("" = GLOBAL) — &?ROUTINE.package
    std::string name;
    const std::vector<Param>* params = nullptr;   // borrowed from AST
    const std::vector<StmtPtr>* body = nullptr;    // borrowed from AST
    DecidedOnce<signed char> hoistNeed{-1};        // see Interpreter::hoistExprDecls (-1 = undecided)
    // Three more static properties of the AST that callCallableRaw used to
    // recompute on EVERY call. Each is cheap once and worthless repeated; a
    // 73,603-call parse pays for them 73,603 times. -1/undecided as above.
    // These two flags PUBLISH the fields under them, so they are PublishedOnce
    // (release/acquire) rather than DecidedOnce (relaxed), and the payloads are
    // atomic rather than plain. As plain ints under a relaxed flag, two threads
    // calling one `sub` at the same time raced writing the bounds — which is
    // undefined behaviour even though both compute the same numbers, and worse,
    // a reader could see arityShape == 1 while the bounds were still 0 and
    // reject a perfectly good call. ThreadSanitizer had been reporting it from
    // t/stress/parallel-map.raku, the case that shares nothing at all.
    PublishedOnce<signed char> arityShape{-1};     // 1 = the arity pre-check applies to this callable
    DecidedOnce<int> arityMaxPos{0}, arityReqPos{0}; // …and its precomputed bounds (valid when arityShape == 1)
    DecidedOnce<bool> arityUnbounded{false};
    PublishedOnce<signed char> catchScan{-1};      // 1 = body holds an inline CATCH block
    PublishedOnce<signed char> phaserScan{-1};     // 1 = body holds an ENTER/LEAVE/… phaser block
    DecidedOnce<Stmt*> catchBlkCache{nullptr};     // …which one (valid when catchScan == 1)
    std::string declFile;                          // source file the routine was declared in (backtrace .file)
    // Language revision this routine was DECLARED under (0=6.c, 1=6.d, 2=6.e),
    // or -1 for callables the runtime makes up (WhateverCode, wrappers,
    // builtins), which simply run under whatever their caller is. Declared
    // routines carry it because the revision belongs to the code, not to the
    // process: a 6.e module's sub must keep 6.e semantics when a 6.d program
    // calls it, and a 6.d module must not acquire 6.e ones by being `use`d
    // from a 6.e program.
    int langRev = -1;
    std::shared_ptr<Env> closure;
    std::shared_ptr<Env> stateEnv;                 // persistent storage for `state` vars (across calls)
    std::once_flag stateInit;                      // stateEnv is created exactly once (thread-safe under parallel calls)
    BuiltinFn builtin;                             // set => builtin
    std::vector<std::string> placeholders;         // $^a auto-params (sorted)
    std::vector<Value> candidates;                 // multi-dispatch candidates
    bool isMultiDispatcher = false;
    bool isMultiCandidate = false;                  // declared `multi` — dispatch may pass it over
    bool isProto = false;                           // `proto` — a dispatch group header, not a candidate
    bool isWhateverCode = false;                    // produced by * currying (composes further)
    long long whateverArity = 0;                    // # of `*` a WhateverCode consumes (`* + *` => 2)
    bool isMethod = false;                          // when invoked via .() the 1st arg is the invocant
    bool isPrivateMethod = false;                   // `method !name` — only reachable via self!name
    bool isSubmethod = false;                       // `submethod` — NOT inherited by subclasses
    bool isBlock = false;                            // a bare { } block (no `return`), not a Sub/Routine
    bool isRegexRoutine = false;                     // `my regex R {…}` / token / rule — .^name is Regex, not Sub
    std::string retType;                             // declared return type (`of`/`returns`/`-->`), "" = none
    std::vector<Value> wrappers;                      // &routine.wrap({…}) stack (outermost last); .unwrap pops
    bool isNative = false;                            // `is native` — a C FFI call
    std::string nativeLib, nativeSym;                // library ("" = default namespace) and C symbol
    std::string nativeLibSub;                        // `is native(&sub)` — sub called at runtime for the lib path
    const Expr* nativeLibExpr = nullptr;             // `is native(EXPR)` whose declaration-time eval failed —
                                                     // retried once at first call (AST outlives the interpreter)
    const Expr* nativeSymExpr = nullptr;             // ditto for `is symbol(EXPR)` — a computed C symbol name
    void* nativeSymCache = nullptr;                   // resolved fn pointer — dlopen/dlsym once, not per call
                                                      // (5 dlopen candidates per call cost a flat ~67 µs)
    void* nativeCifCache = nullptr;                   // prepared libffi call interface, built once per signature.
                                                      // ffi_prep_cif is ~80 ns — 20% of a whole crossing — so it
                                                      // must not run per call. Opaque here; see ncFreeCif.
    ~Callable();
    bool isStub = false;                              // body is a bare `...`/`!!!` stub (role requirement)
    bool usesArgs = false;                            // body references @_ / %_ (implicit slurpy signature)
    bool hadSig = false;                              // declared with explicit (…) — arity is enforceable
    std::string pod;                                  // `#|` leading declarator pod (.WHY)
    bool isSigLiteral = false;                        // built by `:( … )` — a bare Signature, not a routine's
                                                      // (an unconstrained param is Mu there, Any on a routine)
    bool hasPrimed = false;                           // .assuming wrapper: primedParams is the residual signature
    std::vector<std::shared_ptr<Param>> primedParams; // the residual signature (copies: a primed named param
                                                      // survives with the primed value as its default)
};

enum class VT { Nil, Any, Bool, Int, Num, Str, Array, Hash, Code, Range, Pair, Type, Whatever, Object, Rat, Regex, Match, Complex };

struct ClassInfo;
struct ObjectData;
struct Env; // defined in Interpreter.h; ClassInfo keeps its declaration scope for defaults
struct ClassDecl; // defined in Ast.h; ClassInfo keeps a program-lifetime view for roleParams

struct Value {
    VT t = VT::Any;
    bool b = false;
    long long i = 0;
    double n = 0;
    double im = 0; // imaginary part for VT::Complex (real part is n)
    CowStr s; // also holds type name for VT::Type, key for VT::Pair
    // "" normal Hash; else "Set"/"Bag"/"Mix"/"SetHash"/... — a secondary type
    // tag drawn from a closed vocabulary, so it is INTERNED (IStr.h): 8 bytes
    // and a trivial copy, where a std::string was 24 bytes with a constructor
    // and a destructor run on every Value copy.
    IStr hashKind;
    bool isList = false;  // VT::Array that is a List/Seq (gists with parens)
    bool itemized = false; // $[...] / $(...): a single scalar item that does NOT flatten in list context
    bool objKeyed = false; // hash declared with a key shape (`has %!h{Mu:U}`): type-object
                           // subscript keys stay distinct ("(Name)") instead of "" like a plain hash
    bool readonly = false; // a readonly-bound parameter ($x with no `is rw`/`is copy`) — s/// dies on it
    bool namedArg = false; // a VT::Pair passed as a NAMED arg (written syntactically as k=>v / :k(v) at the callsite). A value pair defaults positional.
#ifdef RAKUPP_PTR_CENSUS
    // Phase 1 batch 2 wants to collapse the eleven pointers below into a small
    // number of tag-dispatched slots, which is only sound if the sets that can
    // be live AT THE SAME TIME are what the type tags suggest. This build counts
    // the combinations that actually occur instead of reasoning about them.
    // Compiled out of every normal build; see tools/ptr-census.md.
    ~Value();
    unsigned ptrMask() const {
        return (arr ? 1u : 0) | (hash ? 2u : 0) | (code ? 4u : 0) | (pairVal ? 8u : 0) |
               (pairKey ? 16u : 0) | (obj ? 32u : 0) | (ext ? 64u : 0) | (big ? 128u : 0) |
               (ratN ? 256u : 0) | (ratD ? 512u : 0) | (shape ? 1024u : 0);
    }
#endif
    std::shared_ptr<ValueList> arr;
    std::shared_ptr<std::map<std::string, Value>> hash;
    std::shared_ptr<Callable> code;
    std::shared_ptr<Value> pairVal; // for Pair value
    std::shared_ptr<Value> pairKey; // for Pair key when it's non-scalar (e.g. an array key: [..] => [..])
    std::shared_ptr<ObjectData> obj; // for VT::Object
    std::shared_ptr<void> ext;       // opaque runtime handle: Promise/Channel/Lock/Supplier state (concurrency)
    std::shared_ptr<BigInt> big;     // for VT::Int when value exceeds long long
    std::shared_ptr<BigInt> ratN, ratD; // for VT::Rat (normalized, ratD > 0)
    bool fatRat = false; // VT::Rat tagged as FatRat (type identity; arithmetic stays FatRat)
    // shaped array `my @a[2;3]`: fixed dimensions (row-major). Empty/null = unshaped.
    std::shared_ptr<std::vector<long long>> shape;
    // range
    long long rFrom = 0, rTo = 0;
    bool rExFrom = false, rExTo = false;
    // A fractional numeric range (`1.1 .. 3.1`, `-1.5 ..^ 3`) keeps its real
    // endpoints in the otherwise-unused `n`/`im` doubles; elements step by 1 from
    // `n` while <= `im`. Integer ranges leave this false and use rFrom/rTo.
    bool rNum = false;
    IStr enumName; // non-empty for enum values: the KEY (e.g. Order: Less/Same/More)
    IStr enumType; // the enum's TYPE name (e.g. "Order", "Color") — set on values and the type-list
    // NOT interned, unlike the three tags above, and it must stay that way
    // until one site moves: `IO::Path`'s `:CWD` rides in `ofType` because a path
    // value has no other use for it (Builtins.cpp:4242), so this field can hold
    // a runtime DIRECTORY NAME rather than a type name. The intern table is
    // append-only by design, so a program walking many directories would add an
    // entry per directory and never release it. Everything else here is drawn
    // from the program's own vocabulary of type names and is safe to intern.
    std::string ofType;   // parameter/element type: `Array[Int]` type object, or a typed `my Int @a`/`%h`
                          // (comma-joined for multiple params, e.g. Hash[Int,Str] -> "Int,Str")
    int natBits = 0;      // native int width (uint8/int16/…): 0 = not native; wraps on assignment
    bool natSigned = false;
    bool natFloat = false; // native float container (num32): truncates to float32 on assignment

    Value() : t(VT::Any) {}

    static Value nil() { Value v; v.t = VT::Nil; return v; }
    static Value any() { Value v; v.t = VT::Any; return v; }
    static Value boolean(bool x) { Value v; v.t = VT::Bool; v.b = x; return v; }
    static Value integer(long long x) { Value v; v.t = VT::Int; v.i = x; return v; }
    static Value bigint(const BigInt& b) {
        Value v; v.t = VT::Int;
        if (b.fitsLL()) v.i = b.toLL();
        else v.big = std::make_shared<BigInt>(b);
        return v;
    }
    static Value rat(BigInt n, BigInt d) {
        if (d.sign == 0) return ratZ(std::move(n), std::move(d)); // zero denominator: ±1/0 or 0/0, not 1/1
        Value v; v.t = VT::Rat;
        if (d.sign < 0) { n = -n; d = -d; }
        BigInt g = BigInt::gcd(n, d);
        if (!g.isZero()) { BigInt q, r; BigInt::divmod(n, g, q, r); n = q; BigInt::divmod(d, g, q, r); d = q; }
        v.ratN = std::make_shared<BigInt>(n);
        v.ratD = std::make_shared<BigInt>(d);
        return v;
    }
    // Rat.new semantics: like rat() but a zero denominator is preserved
    // (normalized to ±1/0; 0/0 stays 0/0). Str/Num of such a Rat throws.
    static Value ratZ(BigInt n, BigInt d) {
        if (!d.isZero()) return rat(std::move(n), std::move(d));
        Value v; v.t = VT::Rat;
        if (n.sign > 0) n = BigInt(1);
        else if (n.sign < 0) n = BigInt(-1);
        v.ratN = std::make_shared<BigInt>(n);
        v.ratD = std::make_shared<BigInt>(BigInt(0));
        return v;
    }
    BigInt toBig() const {
        if (t == VT::Int) return big ? *big : BigInt(i);
        if (t == VT::Bool) return BigInt(b ? 1 : 0);
        return BigInt((long long)toInt());
    }
    static Value number(double x) { Value v; v.t = VT::Num; v.n = x; return v; }
    static Value complex(double re, double imag) { Value v; v.t = VT::Complex; v.n = re; v.im = imag; return v; }
    static Value str(std::string x) { Value v; v.t = VT::Str; v.s = std::move(x); return v; }
    static Value array() { Value v; v.t = VT::Array; v.arr = std::make_shared<ValueList>(); return v; }
    static Value array(ValueList items) { Value v; v.t = VT::Array; v.arr = std::make_shared<ValueList>(std::move(items)); return v; }
    // a List/Seq: same storage as Array but gists with (..) instead of [..]
    static Value list(ValueList items) { Value v = array(std::move(items)); v.isList = true; return v; }
    // Wrap a C++ callable as a Raku Code value (used by native codegen for closures / WhateverCode).
    static Value closure(std::function<Value(ValueList&)> fn) {
        Value v; v.t = VT::Code; v.code = std::make_shared<Callable>();
        v.code->builtin = [fn](Interpreter&, ValueList& a) -> Value { return fn(a); };
        return v;
    }
    static Value makeHash() { Value v; v.t = VT::Hash; v.hash = std::make_shared<std::map<std::string, Value>>(); return v; }
    static Value typeObj(std::string name) { Value v; v.t = VT::Type; v.s = std::move(name); return v; }
    static Value whatever() { Value v; v.t = VT::Whatever; return v; }
    static Value object(std::shared_ptr<ObjectData> o) { Value v; v.t = VT::Object; v.obj = std::move(o); return v; }
    static Value enumVal(const std::string& name, long long val) { Value v; v.t = VT::Int; v.i = val; v.enumName = name; return v; }
    // Order::Less/Same/More — the result of cmp/<=>/leg/unicmp/coll. Tagged with
    // its enum TYPE so `.WHAT.^name` is `Order`, not `Int` (Rakudo parity).
    static Value orderVal(long long c) {
        Value v = enumVal(c < 0 ? "Less" : c > 0 ? "More" : "Same", c < 0 ? -1 : c > 0 ? 1 : 0);
        v.enumType = "Order";
        return v;
    }
    static Value regex(std::string pat, std::string flags = "") {
        Value v; v.t = VT::Regex; v.s = std::move(pat); v.hashKind = std::move(flags); return v;
    }
    static Value matchVal(std::string text, long from = 0, long to = 0) {
        Value v; v.t = VT::Match; v.s = std::move(text); v.rFrom = from; v.rTo = to;
        v.arr = std::make_shared<ValueList>();
        v.hash = std::make_shared<std::map<std::string, Value>>();
        return v;
    }
    // Writers use these so the containers stay valid even if matchVal is ever made lazy;
    // with eager allocation above they simply return the existing container.
    ValueList& arrRef() { if (!arr) arr = std::make_shared<ValueList>(); return *arr; }
    std::map<std::string, Value>& hashRef() { if (!hash) hash = std::make_shared<std::map<std::string, Value>>(); return *hash; }
    static Value pair(std::string key, Value val) {
        Value v; v.t = VT::Pair; v.s = std::move(key);
        v.pairVal = std::make_shared<Value>(std::move(val)); return v;
    }
    static Value range(long long from, long long to, bool exFrom, bool exTo) {
        Value v; v.t = VT::Range; v.rFrom = from; v.rTo = to;
        v.rExFrom = exFrom; v.rExTo = exTo; return v;
    }

    bool isNumeric() const { return t == VT::Int || t == VT::Num || t == VT::Bool || t == VT::Rat; }

    bool truthy() const;
    long long toInt() const;
    double toNum() const;
    // an allomorph (IntStr/RatStr/NumStr/ComplexStr): a numeric value tagged so it
    // is ALSO its source string. `s` holds the string; hashKind names the type.
    bool isAllomorph() const {
        return (t == VT::Int || t == VT::Rat || t == VT::Num || t == VT::Complex) &&
               (hashKind == "IntStr" || hashKind == "RatStr" || hashKind == "NumStr" || hashKind == "ComplexStr");
    }
    std::string toStr() const;        // Str coercion (~)
    std::string gist() const;         // .gist / say output
    std::string typeName() const;

    // expand a Range/Array into a flat list of values
    ValueList flatten() const;

    // Typed Blob/Buf support: blob16/32/64 (and utf16/32) store little-endian
    // words in the byte string; ofType ("uint16"/"uint32"/…) carries the width.
    int blobElemSize() const {                 // bytes per element (1 for plain Blob)
        if (ofType == "uint16" || ofType == "int16") return 2;
        if (ofType == "uint32" || ofType == "int32") return 4;
        if (ofType == "uint64" || ofType == "int64") return 8;
        return 1;
    }
    long long blobElems() const { int w = blobElemSize(); return (long long)(s.size() / w); }
    long long blobWordAt(long long idx) const; // one element, LE-decoded (idx pre-checked)
    // The same element as a VALUE. An unsigned 64-bit word with its top bit set
    // does not fit a long long, so blobWordAt() hands it back negative — a
    // blob64 read as -1 instead of 18446744073709551615. Use this everywhere an
    // element is handed to Raku.
    Value blobElemAt(long long idx) const;
    ValueList blobList() const;                // every element as an Int

    // native-int element width for a typed array/hash `ofType` (uint32/int8/…):
    // returns bits (>0) and sets `sign`, or 0 for a non-native element type.
    static int natWidthOfType(const std::string& ofType, bool& sign) {
        std::string bt = ofType.substr(0, ofType.find(','));
        sign = bt.compare(0, 4, "uint") != 0 && bt != "byte";
        if (bt == "int8" || bt == "uint8" || bt == "byte") return 8;
        if (bt == "int16" || bt == "uint16") return 16;
        if (bt == "int32" || bt == "uint32") return 32;
        if (bt == "int64" || bt == "uint64") return 64;
        return 0;
    }
};

// Buf, Instant and Duration are REFERENCE types in Rakudo — two of them are the
// same one only when they are the same object — but here they are plain tagged
// scalars: a Buf is a Str with hashKind="Buf", an Instant/Duration a Num. With
// no address to compare, `===` fell through to comparing the RENDERING and
// called two independently built buffers identical. That is the same trap that
// made `@!outstanding-writes .= grep({ $_ !=== $p })` unemptiable for Promises,
// and worse here, because a Buf is mutable: dropping one from a list by
// `!=== $buf` threw away every buffer that happened to hold the same bytes.
//
// So each freshly built one stamps a token into `ext` — unused by Str and Num,
// and carried along by a plain Value copy, which is exactly what "the same
// object" means for these. `whichOf` reads it; `===` compares that.
// Blob stays out: it is immutable and compares by value in Rakudo too.
inline bool identityScalar(const Value& v) {
    return (v.t == VT::Str && v.hashKind == "Buf") ||
           (v.t == VT::Num && (v.hashKind == "Instant" || v.hashKind == "Duration"));
}
inline Value& identify(Value& v) { v.ext = std::make_shared<char>(); return v; }

bool valueEq(const Value& a, const Value& b);   // numeric/str smart equality
// structural eqv over two objects: same class, attrs pairwise-equal via `eq`
bool objectStructEqv(const Value& a, const Value& b,
                     bool (*eq)(const Value&, const Value&));
int valueCmp(const Value& a, const Value& b);   // for <=> / cmp
std::string strSucc(const std::string& s);             // Raku magic string increment
std::string strPred(const std::string& s, bool& ok);  // magic decrement (ok=false on underflow)

// A Range remembers the endpoint OBJECTS it was written with, so `1/2 .. 1/3`
// keeps its Rats and `True .. False` its Bools instead of collapsing to the
// integers it iterates over. Iteration still walks rFrom/rTo (or n/im when
// fractional); these drive only .min/.max/.bounds and rendering. Parked in the
// otherwise-unused `ext`, so a plain `1..5` costs nothing extra.
struct RangeEnds { Value from, to; };
inline const RangeEnds* rangeEnds(const Value& v) {
    return v.t == VT::Range && v.ext ? static_cast<const RangeEnds*>(v.ext.get()) : nullptr;
}
// Range endpoints render with `.raku` (`Bool::True..Bool::False`, `0.5..<1/3>`),
// which lives in Builtins.cpp. A raw pointer is zero-initialized before any
// dynamic init, so installing it from another TU is order-safe.
using RakuReprFn = std::string (*)(const Value&);
extern RakuReprFn g_rakuRepr;
inline void attachRangeEnds(Value& r, Value from, Value to) {
    r.ext = std::make_shared<RangeEnds>(RangeEnds{std::move(from), std::move(to)});
}
// `..` keeps a Real or undefined endpoint as the object it is, but NUMIFIES a
// Str ("2" becomes 2) or a list (its element count) — so those must not be
// carried. An Int renders identically either way; carrying it would just cost an
// allocation on the hot `1..n` path.
inline void setRangeEnds(Value& r, const Value& from, const Value& to) {
    auto keep = [](const Value& v) {
        return v.t == VT::Rat || v.t == VT::Num || v.t == VT::Bool ||
               v.t == VT::Nil || v.t == VT::Any || v.t == VT::Type; // `1 .. Any`
    };
    auto renders = [&](const Value& v) { return keep(v) || v.t == VT::Int; };
    if (!keep(from) && !keep(to)) return;       // nothing the numeric path gets wrong
    if (!renders(from) || !renders(to)) return; // the other side must numify anyway
    attachRangeEnds(r, from, to);
}

struct ClassAttr {
    std::string name;
    char sigil = '$';
    bool pub = true;
    bool rw = false;  // `is rw` — the public accessor is a writable lvalue
    bool required = false; // `is required` — construction without a value throws
    bool built = false;    // `is built` — settable at construction even when private
    std::string requiredWhy;  // `is required("it is a good idea")` — the reason, for the message
    std::string type; // declared type name (`has Int $.x`), "" = Mu
    std::string containerIs; // `has %.a is Set` — container type trait
    const Expr* def = nullptr; // borrowed from AST
    Value defVal;              // native codegen: precomputed default value
    bool hasDefVal = false;    // use defVal instead of `def`
    std::vector<std::string> handles; // `has $.b handles <m1 m2>` — methods delegated to this attr
    int defConstraint = 0; // type smiley on the attr type: 0=none, 1=:D (defined), 2=:U (undefined)
    bool objKeyed = false; // `has %!h{Mu:U}` — object-keyed hash (type-object keys stay distinct)
    const void* declId = nullptr;     // identity of the declaring AttrDecl (diamond-composition dedup)
    // user traits, evaluated at class-declaration time (`is json-name('x')` →
    // {"json-name", Str}; `is unmarshalled-by({…})` → {"unmarshalled-by", Code};
    // a bare `is json-skip` → {"json-skip", True}). Surfaced on the Attribute
    // meta-object so JSON::Unmarshal's role checks and accessors see them.
    std::vector<std::pair<std::string, Value>> userTraits;
    // The Attribute meta-object, built ONCE and shared. A user `trait_mod:<is>`
    // runs against it at class-declaration time and its `$a does SomeRole` state
    // lives in this object's map, so `.^attributes` must hand back the same
    // object every time or the trait's work is invisible (META6's `is customary`).
    // Empty until the class body declares an attribute carrying a user trait.
    Value metaObj;
};

// The Attribute meta-object for one declared attribute, built once and cached on
// the ClassAttr (MethodCallPart2.cpp) — user `is` traits mix roles into it.
Value attributeMetaObject(ClassAttr& a, const std::string& ownerName);
// Where `$attr does SomeRole` records the roles a trait mixed in. A \x01 prefix
// keeps it out of the way of any real key: role attributes live in the same map
// under their plain names, which is what makes `$a.where` work afterwards.
inline constexpr const char* ATTR_ROLES_KEY = "\x01roles";

struct ClassInfo {
    std::string name;
    std::shared_ptr<ClassInfo> parent;
    std::string nativeParent; // a built-in parent (`is Str`/`is Cool`/…) that has no user ClassInfo
    std::vector<std::shared_ptr<ClassInfo>> extraParents; // additional `is` parents (multiple inheritance)
    std::vector<ClassAttr> attrs;
    std::map<std::string, Value> methods; // Code values (closures)
    std::map<std::string, std::string> rules; // grammar token/rule/regex -> pattern
    std::vector<std::string> ruleOrder; // rule names in DECLARATION order (proto LTM tie-break)
    std::map<std::string, std::string> ruleKind; // name -> "token"/"rule"/"regex"
    std::map<std::string, std::vector<std::string>> ruleParams; // name -> positional param var names ($indent…)
    bool isGrammar = false;
    bool isRole = false;
    bool isMonitor = false; // `monitor Foo {…}` — per-instance lock around every method call
    std::string repr; // `is repr("CStruct")` — NativeCall native memory layout
    std::string ver, auth, api; // :ver<>/:auth<>/:api<> — answered by .^ver/.^auth/.^api
    std::string pod; // `#|` declarator pod (.WHY)
    std::set<std::string> requiredMethods; // methods a composing class must implement (role stubs)
    std::map<std::string, std::vector<std::string>> requiredMultiSigs; // stubbed MULTI candidates: name -> positional-type sig keys that must each be implemented
    std::set<std::string> doneRoles;
    // Names composed in from a ROLE that are SUBMETHODS. They stay in `methods`
    // so the construction protocol's explicit BUILD/TWEAK walks still find them
    // (Rakudo runs a role's BUILD under 6.e too), but ordinary dispatch hides
    // them from 6.e on. A name the class declares ITSELF is erased again.
    std::set<std::string> roleSubmethods; // names of roles this class/role composes (for ~~ / .does)
    Value howObj; // persistent .HOW metaobject — `T.HOW does SomeRole` mixins must stick (Method::Also)
    std::shared_ptr<Env> declEnv; // scope the type was declared in (for evaluating attr defaults)
    ClassDecl* decl = nullptr; // the AST declaration (program-lifetime) — carries roleParams for parameterized roles
    // `role R[$x, %h, Bool :$opt]` composed as `does R[42, %(...), :opt]` — the
    // role's value/type parameters bound to the composition's arguments (name incl.
    // sigil → value). Injected into the scope of the class's methods/submethods so
    // the role body sees them (e.g. Cro::Policy::Timeout[%phase-defaults]).
    std::vector<std::pair<std::string, Value>> roleParamBindings;
    // `state` inside a COMPOSED ROLE method belongs to the composition, not to the
    // role: two classes doing the same role each get their own slot. The Callable
    // is shared (it carries a once_flag and the native-call caches, so it must not
    // be copied), so the per-class env lives here, keyed by it. Empty for all but
    // the few role methods that declare `state`.
    std::map<const Callable*, std::shared_ptr<Env>> roleStateEnvs;

    // Does this class do a role named `rn` — directly, transitively via a
    // composed role, or through a parent? (A role also "does" itself.)
    bool doesRole(const std::string& rn) const {
        if (isRole && name == rn) return true;
        if (doneRoles.count(rn)) return true;
        if (parent && parent->doesRole(rn)) return true;
        for (auto& p : extraParents) if (p && p->doesRole(rn)) return true;
        return false;
    }

    // Ordinary method dispatch: like findMethod, but a SUBMETHOD is visible only
    // on the class that declares it — `class C is P {}` does not get P's
    // submethods. findMethod itself still inherits them, because the object
    // construction path deliberately walks the whole hierarchy calling each
    // class's BUILD and TWEAK, which are submethods and must keep being found.
    // `roleSubs`: does a submethod composed from a ROLE stay visible on the
    // consuming class? Yes up to 6.d — composition flattens the role into the
    // class. Since 6.e submethods are NOT composed, and only a class-qualified
    // `$obj.Role::name` call reaches them.
    Value* findMethodForCall(const std::string& m, bool roleSubs = true) {
        auto it = methods.find(m);
        if (it != methods.end()) {
            if (!roleSubs && roleSubmethods.count(m)) return nullptr; // 6.e: not composed
            return &it->second;
        }
        // A submethod reached through a real ancestor CLASS is never inherited.
        auto inherited = [&](ClassInfo* c) -> Value* {
            if (!c) return nullptr;
            Value* r = c->findMethodForCall(m, roleSubs);
            if (r && r->code && r->code->isSubmethod && (!c->isRole || !roleSubs)) return nullptr;
            return r;
        };
        if (Value* r = inherited(parent.get())) return r;
        for (auto& p : extraParents) if (Value* r = inherited(p.get())) return r;
        return nullptr;
    }
    Value* findMethod(const std::string& m) { return findMethod(m, nullptr); }
    // `owner` (if given) receives the ClassInfo the method was found in — used to seed
    // the next method (that class's parent) for callsame/nextsame.
    Value* findMethod(const std::string& m, ClassInfo** owner) {
        auto it = methods.find(m);
        if (it != methods.end()) { if (owner) *owner = this; return &it->second; }
        if (parent) { if (Value* r = parent->findMethod(m, owner)) return r; }
        for (auto& p : extraParents) if (p) { if (Value* r = p->findMethod(m, owner)) return r; }
        if (owner) *owner = nullptr;
        return nullptr;
    }
    const std::string* findRule(const std::string& n) const {
        auto it = rules.find(n);
        if (it != rules.end()) return &it->second;
        if (parent) return parent->findRule(n);
        return nullptr;
    }
    const std::vector<std::string>* findRuleParams(const std::string& n) const {
        auto it = ruleParams.find(n);
        if (it != ruleParams.end()) return &it->second;
        if (parent) return parent->findRuleParams(n);
        return nullptr;
    }
    const ClassAttr* findAttr(const std::string& n) const {
        for (auto& a : attrs) if (a.name == n) return &a;
        if (parent) { if (const ClassAttr* r = parent->findAttr(n)) return r; }
        for (auto& p : extraParents) if (p) { if (const ClassAttr* r = p->findAttr(n)) return r; }
        return nullptr;
    }
};

struct ObjectData {
    std::shared_ptr<ClassInfo> cls;
    std::map<std::string, Value> attrs;
    // For a `but`/`does` mixin over a non-object base (`5 but Role`, `{} does R`):
    // the original value is kept here and the object delegates coercions,
    // operators, and unfound methods to it.
    Value boxed;
    bool hasBoxed = false;
    // `monitor` instances: one reentrant lock per object, held around every
    // method call (created lazily by invokeMethod's guard). Reentrant, so a
    // monitor method calling another method on self does not deadlock —
    // OO::Monitors' semantics, native.
    std::shared_ptr<std::recursive_mutex> monitorLock;
};

} // namespace rakupp
