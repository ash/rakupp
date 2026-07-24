#pragma once
#include "Ast.h"
#include "BigInt.h"
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace rakupp {

struct Value;

// codepoint -> UTF-8 (shared: Str-Range endpoints derive their text from rFrom/rTo)
inline std::string cpToU8(uint32_t cp) {
    std::string o;
    if (cp < 0x80) o += (char)cp;
    else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
    else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
    return o;
}
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
    std::shared_ptr<Env> closure;
    std::shared_ptr<Env> stateEnv;                 // persistent storage for `state` vars (across calls)
    std::once_flag stateInit;                      // stateEnv is created exactly once (thread-safe under parallel calls)
    BuiltinFn builtin;                             // set => builtin
    std::vector<std::string> placeholders;         // $^a auto-params (sorted)
    std::vector<Value> candidates;                 // multi-dispatch candidates
    bool isMultiDispatcher = false;
    bool isProto = false;                           // `proto` — a dispatch group header, not a candidate
    bool isWhateverCode = false;                    // produced by * currying (composes further)
    long long whateverArity = 0;                    // # of `*` a WhateverCode consumes (`* + *` => 2)
    bool isMethod = false;                          // when invoked via .() the 1st arg is the invocant
    bool isBlock = false;                            // a bare { } block (no `return`), not a Sub/Routine
    std::string retType;                             // declared return type (`of`/`returns`/`-->`), "" = none
    std::vector<Value> wrappers;                      // &routine.wrap({…}) stack (outermost last); .unwrap pops
    bool isNative = false;                            // `is native` — a C FFI call
    std::string nativeLib, nativeSym;                // library ("" = default namespace) and C symbol
    std::string nativeLibSub;                        // `is native(&sub)` — sub called at runtime for the lib path
    bool isStub = false;                              // body is a bare `...`/`!!!` stub (role requirement)
    bool usesArgs = false;                            // body references @_ / %_ (implicit slurpy signature)
    bool hadSig = false;                              // declared with explicit (…) — arity is enforceable
    std::string pod;                                  // `#|` leading declarator pod (.WHY)
    bool hasPrimed = false;                           // .assuming wrapper: primedParams is the residual signature
    std::vector<const Param*> primedParams;           // params left unbound by the priming (point into the AST)
};

enum class VT { Nil, Any, Bool, Int, Num, Str, Array, Hash, Code, Range, Pair, Type, Whatever, Object, Rat, Regex, Match, Complex };

struct ClassInfo;
struct ObjectData;
struct Env; // defined in Interpreter.h; ClassInfo keeps its declaration scope for defaults

struct Value {
    VT t = VT::Any;
    bool b = false;
    long long i = 0;
    double n = 0;
    double im = 0; // imaginary part for VT::Complex (real part is n)
    std::string s; // also holds type name for VT::Type, key for VT::Pair
    std::string hashKind; // "" normal Hash; else "Set"/"Bag"/"Mix"/"SetHash"/...
    bool isList = false;  // VT::Array that is a List/Seq (gists with parens)
    bool itemized = false; // $[...] / $(...): a single scalar item that does NOT flatten in list context
    bool readonly = false; // a readonly-bound parameter ($x with no `is rw`/`is copy`) — s/// dies on it
    bool namedArg = false; // a VT::Pair passed as a NAMED arg (written syntactically as k=>v / :k(v) at the callsite). A value pair defaults positional.
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
    std::string enumName; // non-empty for enum values: the KEY (e.g. Order: Less/Same/More)
    std::string enumType; // the enum's TYPE name (e.g. "Order", "Color") — set on values and the type-list
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

bool valueEq(const Value& a, const Value& b);   // numeric/str smart equality
int valueCmp(const Value& a, const Value& b);   // for <=> / cmp
std::string strSucc(const std::string& s);             // Raku magic string increment
std::string strPred(const std::string& s, bool& ok);  // magic decrement (ok=false on underflow)

struct ClassAttr {
    std::string name;
    char sigil = '$';
    bool pub = true;
    bool rw = false;  // `is rw` — the public accessor is a writable lvalue
    std::string type; // declared type name (`has Int $.x`), "" = Mu
    std::string containerIs; // `has %.a is Set` — container type trait
    const Expr* def = nullptr; // borrowed from AST
    Value defVal;              // native codegen: precomputed default value
    bool hasDefVal = false;    // use defVal instead of `def`
    std::vector<std::string> handles; // `has $.b handles <m1 m2>` — methods delegated to this attr
    const void* declId = nullptr;     // identity of the declaring AttrDecl (diamond-composition dedup)
};

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
    std::string repr; // `is repr("CStruct")` — NativeCall native memory layout
    std::string ver, auth, api; // :ver<>/:auth<>/:api<> — answered by .^ver/.^auth/.^api
    std::string pod; // `#|` declarator pod (.WHY)
    std::set<std::string> requiredMethods; // methods a composing class must implement (role stubs)
    std::map<std::string, std::vector<std::string>> requiredMultiSigs; // stubbed MULTI candidates: name -> positional-type sig keys that must each be implemented
    std::set<std::string> doneRoles; // names of roles this class/role composes (for ~~ / .does)
    std::shared_ptr<Env> declEnv; // scope the type was declared in (for evaluating attr defaults)

    // Does this class do a role named `rn` — directly, transitively via a
    // composed role, or through a parent? (A role also "does" itself.)
    bool doesRole(const std::string& rn) const {
        if (isRole && name == rn) return true;
        if (doneRoles.count(rn)) return true;
        if (parent && parent->doesRole(rn)) return true;
        for (auto& p : extraParents) if (p && p->doesRole(rn)) return true;
        return false;
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
};

} // namespace rakupp
