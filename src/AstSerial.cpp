// Binary (de)serialization of the parsed AST — see AstSerial.h for what it is
// for and what it deliberately omits.
//
// THE ONE STRUCTURAL IDEA: every node type has exactly ONE `visit(io, node)`
// listing its fields, and that function is instantiated twice — once with a
// Writer, once with a Reader. `io(x)` writes x or fills x depending on which.
// So a field cannot be saved but not restored (or the reverse): the two
// directions are the same source line. That matters more here than anywhere
// else in the codebase, because the failure mode of a hand-written pair of
// walkers is a field that silently drops and a program that then behaves
// almost right. `--aot`'s emitter has exactly that bug today: it rebuilds only
// the fields it happens to mention, so `my Int(Str) $x` loses its coercion.
#include "AstSerial.h"
#include <cstring>
#include <type_traits>

namespace rakupp {
namespace {

constexpr char kMagic[4] = {'R', 'K', 'A', 'S'};

// ---- primitive streams -------------------------------------------------

struct Writer {
    std::string buf;
    static constexpr bool reading = false;

    void raw(const void* p, size_t n) { buf.append(static_cast<const char*>(p), n); }
    void u8(uint8_t v) { buf.push_back((char)v); }
    // LEB128: most values here are small (kinds, counts, line numbers), so a
    // varint costs one byte where a fixed u32 costs four.
    void uvar(uint64_t v) { do { uint8_t b = v & 0x7f; v >>= 7; if (v) b |= 0x80; u8(b); } while (v); }
    void ivar(int64_t v) { uvar((uint64_t)((v << 1) ^ (v >> 63))); } // zigzag
};

struct Reader {
    const char* p;
    const char* end;
    static constexpr bool reading = true;

    void need(size_t n) const { if ((size_t)(end - p) < n) throw AstSerialError{"truncated AST cache"}; }
    void raw(void* d, size_t n) { need(n); std::memcpy(d, p, n); p += n; }
    uint8_t u8() { need(1); return (uint8_t)*p++; }
    uint64_t uvar() {
        uint64_t v = 0; int sh = 0;
        for (;;) {
            uint8_t b = u8();
            v |= (uint64_t)(b & 0x7f) << sh;
            if (!(b & 0x80)) break;
            sh += 7;
            if (sh > 63) throw AstSerialError{"corrupt varint in AST cache"};
        }
        return v;
    }
    int64_t ivar() { uint64_t u = uvar(); return (int64_t)(u >> 1) ^ -(int64_t)(u & 1); }
    // element count for a container about to be resize()d: every element costs
    // at least one byte, so a count past the remaining bytes is corruption —
    // throw the typed error the callers catch (cache miss + reparse) instead of
    // letting resize() die with length_error/bad_alloc.
    size_t count() {
        uint64_t n = uvar();
        if (n > (uint64_t)(end - p)) throw AstSerialError{"corrupt count in AST cache"};
        return (size_t)n;
    }
};

// ---- scalar field operations, one overload set per direction ------------

inline void io_(Writer& w, bool v)              { w.u8(v ? 1 : 0); }
inline void io_(Reader& r, bool& v)             { v = r.u8() != 0; }
inline void io_(Writer& w, char v)              { w.u8((uint8_t)v); }
inline void io_(Reader& r, char& v)             { v = (char)r.u8(); }
inline void io_(Writer& w, signed char v)       { w.u8((uint8_t)v); }
inline void io_(Reader& r, signed char& v)      { v = (signed char)r.u8(); }
inline void io_(Writer& w, int v)               { w.ivar(v); }
inline void io_(Reader& r, int& v)              { v = (int)r.ivar(); }
inline void io_(Writer& w, long long v)         { w.ivar(v); }
inline void io_(Reader& r, long long& v)        { v = r.ivar(); }
inline void io_(Writer& w, double v)            { w.raw(&v, sizeof v); }       // exact bits: a literal must not drift
inline void io_(Reader& r, double& v)           { r.raw(&v, sizeof v); }
inline void io_(Writer& w, NqpOpc v)            { w.uvar((uint64_t)v); }
inline void io_(Reader& r, NqpOpc& v)           { v = (NqpOpc)r.uvar(); }

inline void io_(Writer& w, const std::string& s) { w.uvar(s.size()); w.raw(s.data(), s.size()); }
inline void io_(Reader& r, std::string& s) {
    size_t n = (size_t)r.uvar();
    r.need(n);
    s.assign(r.p, n);
    r.p += n;
}

} // namespace

// ---- the node visitors -------------------------------------------------
//
// Declared before use so the mutually recursive ones (an Expr holding a Stmt
// holding an Expr) resolve.
namespace {

template <class IO> void ioExpr(IO& io, ExprPtr& e);
template <class IO> void ioStmt(IO& io, StmtPtr& s);
template <class IO> void ioBlock(IO& io, std::unique_ptr<Block>& b);
template <class IO> void ioSubDecl(IO& io, std::unique_ptr<SubDecl>& d);
template <class IO> void ioParams(IO& io, std::vector<Param>& ps);

// vector<T> of a scalar type
template <class IO, class T>
void ioVec(IO& io, std::vector<T>& v) {
    if constexpr (IO::reading) {
        size_t n = io.count();
        v.clear(); v.resize(n);
        for (auto& x : v) io_(io, x);
    } else {
        io.uvar(v.size());
        for (auto& x : v) io_(io, x);
    }
}

// vector<ExprPtr> / vector<StmtPtr>
template <class IO> void ioExprVec(IO& io, std::vector<ExprPtr>& v) {
    if constexpr (IO::reading) {
        size_t n = io.count(); v.clear(); v.resize(n);
        for (auto& x : v) ioExpr(io, x);
    } else {
        io.uvar(v.size());
        for (auto& x : v) ioExpr(io, x);
    }
}
template <class IO> void ioStmtVec(IO& io, std::vector<StmtPtr>& v) {
    if constexpr (IO::reading) {
        size_t n = io.count(); v.clear(); v.resize(n);
        for (auto& x : v) ioStmt(io, x);
    } else {
        io.uvar(v.size());
        for (auto& x : v) ioStmt(io, x);
    }
}

// A scalar field, written or read through the same call.
template <class IO, class T> void F(IO& io, T& x) {
    if constexpr (IO::reading) io_(io, x);
    else io_(io, const_cast<const T&>(x));
}
// bool/char/int/double need the by-value writer overloads
template <class IO> void F(IO& io, bool& x)        { if constexpr (IO::reading) io_(io, x); else io_(io, (bool)x); }
template <class IO> void F(IO& io, char& x)        { if constexpr (IO::reading) io_(io, x); else io_(io, (char)x); }
template <class IO> void F(IO& io, signed char& x) { if constexpr (IO::reading) io_(io, x); else io_(io, (signed char)x); }
template <class IO> void F(IO& io, int& x)         { if constexpr (IO::reading) io_(io, x); else io_(io, (int)x); }
template <class IO> void F(IO& io, long long& x)   { if constexpr (IO::reading) io_(io, x); else io_(io, (long long)x); }
template <class IO> void F(IO& io, double& x)      { if constexpr (IO::reading) io_(io, x); else io_(io, (double)x); }
template <class IO> void F(IO& io, NqpOpc& x)      { if constexpr (IO::reading) io_(io, x); else io_(io, (NqpOpc)x); }

// ---- Param and the other plain records ----

template <class IO> void ioParam(IO& io, Param& p) {
    F(io, p.name); F(io, p.sigil); F(io, p.type);
    ioExpr(io, p.whereExpr); ioExpr(io, p.litVal); ioExpr(io, p.defaultVal);
    F(io, p.defaultRaku); F(io, p.hadWhere); F(io, p.typeCapture);
    F(io, p.namedKey); F(io, p.aliasBoth); ioVec(io, p.aliasKeys);
    F(io, p.pod); F(io, p.slurpyKind); F(io, p.named); F(io, p.slurpy);
    F(io, p.optional); F(io, p.required); F(io, p.invocant);
    F(io, p.defConstraint); F(io, p.coerce); F(io, p.coerceFrom);
    F(io, p.isRw); F(io, p.isCopy); F(io, p.isRaw);
    // subSig: optional nested signature
    if constexpr (IO::reading) {
        if (io.u8()) { p.subSig = std::make_shared<std::vector<Param>>(); ioParams(io, *p.subSig); }
        else p.subSig.reset();
    } else {
        io.u8(p.subSig ? 1 : 0);
        if (p.subSig) ioParams(io, *p.subSig);
    }
}

template <class IO> void ioParams(IO& io, std::vector<Param>& ps) {
    if constexpr (IO::reading) {
        size_t n = io.count(); ps.clear(); ps.resize(n);
        for (auto& p : ps) ioParam(io, p);
    } else {
        io.uvar(ps.size());
        for (auto& p : ps) ioParam(io, p);
    }
}

template <class IO> void ioAttr(IO& io, AttrDecl& a) {
    F(io, a.name); F(io, a.sigil); F(io, a.containerIs); F(io, a.pub); F(io, a.rw);
    F(io, a.required); F(io, a.built); F(io, a.requiredWhy); F(io, a.type);
    F(io, a.coerce); ioVec(io, a.handles); ioVec(io, a.handlesTo); F(io, a.defConstraint);
    F(io, a.objKeyed);
    if constexpr (IO::reading) {
        size_t n = io.count();
        a.userTraits.clear(); a.userTraits.resize(n);
        for (auto& ut : a.userTraits) { F(io, ut.first); ioExpr(io, ut.second); }
    } else {
        io.uvar(a.userTraits.size());
        for (auto& ut : a.userTraits) { F(io, ut.first); ioExpr(io, ut.second); }
    }
    ioExpr(io, a.def);
    ioExpr(io, a.whereExpr);
}

template <class IO> void ioRule(IO& io, GrammarRuleDecl& g) {
    F(io, g.name); F(io, g.pattern); F(io, g.kind); ioVec(io, g.params);
}

template <class IO> void ioTrait(IO& io, SubTraitSpec& t) { F(io, t.name); ioExpr(io, t.arg); }

// ---- the per-kind field lists ----
//
// One function per node type. Adding a field to Ast.h means adding it here,
// once, and both directions follow.

template <class IO> void visit(IO& io, IntLit& n)   { F(io, n.v); F(io, n.big); F(io, n.raw); }
template <class IO> void visit(IO& io, NumLit& n)   { F(io, n.v); F(io, n.imaginary); F(io, n.raw);
                                                      F(io, n.isRat); F(io, n.ratNum); F(io, n.ratDen);
                                                      F(io, n.bigNum); F(io, n.bigDen); }
// `v` is written after construction here, so the constructor's normalization
// missed it — normalize on the way in. (Both fields stay in the format so an
// existing cache still reads.)
template <class IO> void visit(IO& io, StrLit& n)   { F(io, n.v); F(io, n.nfcDone);
                                                      if (IO::reading) n.normalize(); }
template <class IO> void visit(IO& io, BoolLit& n)  { F(io, n.v); }
template <class IO> void visit(IO& io, AllomorphLit& n) { ioExpr(io, n.num); F(io, n.str); }
template <class IO> void visit(IO& io, RegexLit& n) { F(io, n.pattern); F(io, n.isRx); F(io, n.isM); F(io, n.declKind); }
template <class IO> void visit(IO& io, SubstLit& n) { F(io, n.pattern); F(io, n.repl); F(io, n.nonMut); }
template <class IO> void visit(IO& io, ChainExpr& n){ ioExprVec(io, n.operands); ioVec(io, n.ops); }
template <class IO> void visit(IO& io, InterpStr& n){ ioExprVec(io, n.parts); }
template <class IO> void visit(IO& io, VarExpr& n)  { F(io, n.name); F(io, n.declare); F(io, n.declScope);
                                                      F(io, n.declType); F(io, n.declCoerce);
                                                      ioExpr(io, n.declDefault);
                                                      F(io, n.declDynamic); F(io, n.declExport); F(io, n.pkgSymbol);
                                                      F(io, n.containerIs); F(io, n.containerOf);
                                                      ioExpr(io, n.declShape); F(io, n.namedBind);
                                                      F(io, n.processScoped);
                                                      ioExpr(io, n.declTypeExpr);
                                                      n.syncAttrCache(); }  // derived from `name`, not stored
template <class IO> void visit(IO& io, NameTerm& n) { F(io, n.name); F(io, n.ofType); F(io, n.defConstraint); }
template <class IO> void visit(IO& io, ListExpr& n) { ioExprVec(io, n.items); F(io, n.parenned); F(io, n.semicolon); }
template <class IO> void visit(IO& io, SymbolicRef& n) { ioExpr(io, n.nameExpr); ioExprVec(io, n.segs);
                                                         F(io, n.pkg); F(io, n.sigil); }
template <class IO> void visit(IO& io, ArrayLit& n) { ioExprVec(io, n.items); F(io, n.isList); F(io, n.fromCommaList); }
template <class IO> void visit(IO& io, HashLit& n)  { ioExprVec(io, n.items); }
template <class IO> void visit(IO& io, Assign& n)   { ioExpr(io, n.target); F(io, n.op); ioExpr(io, n.value); F(io, n.containerSigil); }
template <class IO> void visit(IO& io, Binary& n)   { F(io, n.op); ioExpr(io, n.lhs); ioExpr(io, n.rhs); }
template <class IO> void visit(IO& io, Unary& n)    { F(io, n.op); F(io, n.postfix); ioExpr(io, n.operand); }
template <class IO> void visit(IO& io, Call& n)     { F(io, n.name); ioExpr(io, n.callee); ioExprVec(io, n.args); }
template <class IO> void visit(IO& io, MethodCall& n) { ioExpr(io, n.inv); F(io, n.method); F(io, n.methodQual);
                                                        ioExpr(io, n.methodExpr); ioExprVec(io, n.args);
                                                        F(io, n.maybe); F(io, n.bang); F(io, n.mutate);
                                                        F(io, n.hyper); F(io, n.meta); }
template <class IO> void visit(IO& io, Index& n)    { ioExpr(io, n.base); ioExpr(io, n.index); F(io, n.isHash);
                                                      F(io, n.multiDim); F(io, n.semicolonSub); F(io, n.adverb); }
template <class IO> void visit(IO& io, Ternary& n)  { ioExpr(io, n.cond); ioExpr(io, n.then); ioExpr(io, n.els); }
template <class IO> void visit(IO& io, NqpOp& n)    { F(io, n.op); ioExprVec(io, n.args); }
template <class IO> void visit(IO& io, RangeExpr& n){ ioExpr(io, n.from); ioExpr(io, n.to); F(io, n.exFrom); F(io, n.exTo); }
template <class IO> void visit(IO& io, PairExpr& n) { F(io, n.key); F(io, n.colonForm); F(io, n.quotedKey);
                                                      F(io, n.parenned);   // ( :k(v) ) is POSITIONAL
                                                      ioExpr(io, n.keyExpr); ioExpr(io, n.value); }
template <class IO> void visit(IO& io, BlockExpr& n){ ioParams(io, n.params); ioStmtVec(io, n.body);
                                                      F(io, n.isSub); F(io, n.isMethodTerm); F(io, n.isPointy);
                                                      F(io, n.retType); }
template <class IO> void visit(IO&, SelfTerm&)      {}
template <class IO> void visit(IO& io, WhateverExpr& n) { F(io, n.hyper); }

template <class IO> void visit(IO& io, ExprStmt& n) { ioExpr(io, n.e); }
template <class IO> void visit(IO& io, NamedRegexDecl& n) { F(io, n.name); F(io, n.pattern); F(io, n.kind); }
template <class IO> void visit(IO& io, VarDecl& n)  { F(io, n.scope); ioVec(io, n.names); F(io, n.op); ioExpr(io, n.init); }
template <class IO> void visit(IO& io, SubDecl& n)  {
    F(io, n.name); ioExpr(io, n.nameExpr); ioParams(io, n.params);
    if constexpr (IO::reading) {
        size_t k = io.count(); n.altParams.clear(); n.altParams.resize(k);
        for (auto& a : n.altParams) ioParams(io, a);
    } else {
        io.uvar(n.altParams.size());
        for (auto& a : n.altParams) ioParams(io, a);
    }
    ioStmtVec(io, n.body);
    if constexpr (IO::reading) {
        size_t k = io.count(); n.traits.clear(); n.traits.resize(k);
        for (auto& t : n.traits) ioTrait(io, t);
    } else {
        io.uvar(n.traits.size());
        for (auto& t : n.traits) ioTrait(io, t);
    }
    ioExpr(io, n.retLiteral); F(io, n.retLiteralPresent);
    F(io, n.isMulti); F(io, n.isProto); F(io, n.hadSig); F(io, n.isMethod);
    F(io, n.isSubmethod); F(io, n.isPrivate);
    ioExprVec(io, n.immediateArgs); F(io, n.immediateCall);
    F(io, n.isExport); F(io, n.isOur); F(io, n.retType); F(io, n.pod);
    F(io, n.isNative); F(io, n.nativeLib); F(io, n.nativeLibSub);
    ioExpr(io, n.nativeLibExpr); F(io, n.nativeSym); ioExpr(io, n.nativeSymExpr);
    // `is raw`/`is rw` on the ROUTINE. The trait itself is not in n.traits (the
    // parser consumes it into this flag), so a cached module came back with the
    // flag off and every `self.AT-KEY($k) = $v` through an `is raw` accessor
    // assigned to a COPY — Hash::Ordered took its writes only on the run that
    // wrote the cache, and answered empty on every run that read it.
    F(io, n.retRw);
}
template <class IO> void visit(IO& io, ClassDecl& n) {
    F(io, n.name); F(io, n.parent); ioVec(io, n.extraParents); ioVec(io, n.roles);
    if constexpr (IO::reading) {
        size_t k = io.count(); n.attrs.clear(); n.attrs.resize(k);
        for (auto& a : n.attrs) ioAttr(io, a);
        k = io.count(); n.methods.clear(); n.methods.resize(k);
        for (auto& m : n.methods) ioSubDecl(io, m);
        k = io.count(); n.rules.clear(); n.rules.resize(k);
        for (auto& r : n.rules) ioRule(io, r);
    } else {
        io.uvar(n.attrs.size());   for (auto& a : n.attrs) ioAttr(io, a);
        io.uvar(n.methods.size()); for (auto& m : n.methods) ioSubDecl(io, m);
        io.uvar(n.rules.size());   for (auto& r : n.rules) ioRule(io, r);
    }
    F(io, n.isRole); F(io, n.parentIsDoes); F(io, n.isGrammar); F(io, n.isAugment);
    F(io, n.isStubDecl); F(io, n.pod); F(io, n.parameterized); F(io, n.isMy);
    ioExpr(io, n.nameExpr);
    F(io, n.ver); F(io, n.auth); F(io, n.api);
    ioExpr(io, n.verExpr); ioExpr(io, n.authExpr); ioExpr(io, n.apiExpr);
    F(io, n.repr); ioParams(io, n.roleParams);
    if constexpr (IO::reading) {
        size_t k = io.count(); n.roleArgs.clear(); n.roleArgs.resize(k);
        for (auto& ra : n.roleArgs) { F(io, ra.first); ioExprVec(io, ra.second); }
    } else {
        io.uvar(n.roleArgs.size());
        for (auto& ra : n.roleArgs) { F(io, ra.first); ioExprVec(io, ra.second); }
    }
    F(io, n.isPackage); ioStmtVec(io, n.body);
    F(io, n.isMonitor);
    F(io, n.classRw);
}
template <class IO> void visit(IO& io, Block& n)    { ioStmtVec(io, n.stmts); F(io, n.isCatch);
                                                      F(io, n.phaser); F(io, n.stmtForm); }
template <class IO> void visit(IO& io, EnumDecl& n) { F(io, n.name); ioExpr(io, n.values); F(io, n.isExport); }
template <class IO> void visit(IO& io, IfStmt& n)   {
    F(io, n.thenVar);
    if constexpr (IO::reading) {
        size_t k = io.count(); n.branches.clear(); n.branches.resize(k);
        for (auto& br : n.branches) { ioExpr(io, br.first); ioBlock(io, br.second); }
    } else {
        io.uvar(n.branches.size());
        for (auto& br : n.branches) { ioExpr(io, br.first); ioBlock(io, br.second); }
    }
    ioVec(io, n.branchVars); F(io, n.elseVar); ioBlock(io, n.elseBlock);
    F(io, n.isUnless); F(io, n.modifier);
    if constexpr (IO::reading) {
        size_t k = io.count(); n.branchParams.clear(); n.branchParams.resize(k);
        for (auto& bp : n.branchParams) ioParams(io, bp);
    } else {
        io.uvar(n.branchParams.size());
        for (auto& bp : n.branchParams) ioParams(io, bp);
    }
    ioParams(io, n.elseParams);
}
template <class IO> void visit(IO& io, WhileStmt& n){ ioExpr(io, n.cond); ioBlock(io, n.body); F(io, n.isUntil);
                                                      F(io, n.var); F(io, n.asExpr); F(io, n.modifier);
                                                      ioParams(io, n.params); }
template <class IO> void visit(IO& io, ForStmt& n)  { ioExpr(io, n.list); ioVec(io, n.vars); F(io, n.rwVars);
                                                      F(io, n.destructure); ioParams(io, n.params);
                                                      ioBlock(io, n.body); F(io, n.asExpr); F(io, n.modifier); }
template <class IO> void visit(IO& io, ReturnStmt& n) { ioExpr(io, n.value); F(io, n.isRw); }
template <class IO> void visit(IO& io, LastStmt& n) { F(io, n.target); }
template <class IO> void visit(IO& io, NextStmt& n) { F(io, n.target); }
template <class IO> void visit(IO& io, RedoStmt& n) { F(io, n.target); }
template <class IO> void visit(IO& io, UseStmt& n)  { F(io, n.module); F(io, n.arg); ioVec(io, n.importArgs);
                                                      ioExpr(io, n.argExpr); F(io, n.isNo); F(io, n.isNeed);
                                                      F(io, n.verReq);   // dropping the :ver<…> constraint from the
                                                                         // cache made run 2 load ANY version
                                                      ioExpr(io, n.ifCond); } // :if(EXPR) — same lesson
template <class IO> void visit(IO&, EmptyStmt&)     {}
template <class IO> void visit(IO& io, SubsetDecl& n) { F(io, n.name); F(io, n.baseType); ioExpr(io, n.where); }
template <class IO> void visit(IO& io, GivenStmt& n){ ioExpr(io, n.topic); F(io, n.var); F(io, n.modifier);
                                                      ioBlock(io, n.body); F(io, n.defGuard); F(io, n.hasElse);
                                                      ioBlock(io, n.elseBody); F(io, n.elseVar);
                                                      ioParams(io, n.params); ioParams(io, n.elseParams); }
template <class IO> void visit(IO& io, WhenStmt& n) { ioExpr(io, n.cond); F(io, n.isDefault); ioBlock(io, n.body); }
template <class IO> void visit(IO& io, LoopStmt& n) { ioExpr(io, n.init); ioExpr(io, n.cond); ioExpr(io, n.incr);
                                                      ioBlock(io, n.body); F(io, n.asExpr); }
template <class IO> void visit(IO& io, RepeatStmt& n) { ioExpr(io, n.cond); F(io, n.isUntil); ioBlock(io, n.body); }

// ---- polymorphic dispatch ----
//
// A null child is encoded as kind 0xFF, so an optional field costs one byte.
constexpr uint8_t kNull = 0xFF;

#define EXPR_KINDS(X) \
    X(IntLit, IntLit) X(NumLit, NumLit) X(StrLit, StrLit) X(InterpStr, InterpStr) \
    X(BoolLit, BoolLit) X(VarExpr, VarExpr) X(ListExpr, ListExpr) X(Assign, Assign) \
    X(Binary, Binary) X(Unary, Unary) X(Call, Call) X(MethodCall, MethodCall) \
    X(Index, Index) X(Ternary, Ternary) X(Range, RangeExpr) X(Pair, PairExpr) \
    X(BlockExpr, BlockExpr) X(ArrayLit, ArrayLit) X(HashLit, HashLit) X(NameTerm, NameTerm) \
    X(RegexLit, RegexLit) X(SubstLit, SubstLit) X(ChainExpr, ChainExpr) \
    X(SymbolicRef, SymbolicRef) X(AllomorphLit, AllomorphLit) X(NqpOp, NqpOp) \
    X(SelfTerm, SelfTerm) X(Whatever, WhateverExpr)

#define STMT_KINDS(X) \
    X(ExprStmt, ExprStmt) X(VarDecl, VarDecl) X(SubDecl, SubDecl) X(IfStmt, IfStmt) \
    X(WhileStmt, WhileStmt) X(ForStmt, ForStmt) X(LoopStmt, LoopStmt) X(Block, Block) \
    X(ReturnStmt, ReturnStmt) X(LastStmt, LastStmt) X(NextStmt, NextStmt) X(RedoStmt, RedoStmt) \
    X(UseStmt, UseStmt) X(EmptyStmt, EmptyStmt) X(GivenStmt, GivenStmt) X(WhenStmt, WhenStmt) \
    X(RepeatStmt, RepeatStmt) X(ClassDecl, ClassDecl) X(EnumDecl, EnumDecl) \
    X(NamedRegexDecl, NamedRegexDecl) X(SubsetDecl, SubsetDecl)

// Some node types need a constructor argument; make one uniformly.
template <class T> std::unique_ptr<T> makeNode();
template <> std::unique_ptr<IntLit>  makeNode() { return std::make_unique<IntLit>(0); }
template <> std::unique_ptr<NumLit>  makeNode() { return std::make_unique<NumLit>(0.0); }
template <> std::unique_ptr<StrLit>  makeNode() { return std::make_unique<StrLit>(std::string()); }
template <> std::unique_ptr<BoolLit> makeNode() { return std::make_unique<BoolLit>(false); }
template <> std::unique_ptr<RegexLit> makeNode() { return std::make_unique<RegexLit>(std::string()); }
template <> std::unique_ptr<SubstLit> makeNode() { return std::make_unique<SubstLit>(std::string(), std::string()); }
template <> std::unique_ptr<VarExpr> makeNode() { return std::make_unique<VarExpr>(std::string()); }
template <> std::unique_ptr<NameTerm> makeNode() { return std::make_unique<NameTerm>(std::string()); }
template <> std::unique_ptr<NqpOp>   makeNode() { return std::make_unique<NqpOp>(NqpOpc::Stmts); }
template <class T> std::unique_ptr<T> makeNode() { return std::make_unique<T>(); }

template <class IO> void ioExpr(IO& io, ExprPtr& e) {
    if constexpr (IO::reading) {
        uint8_t k = io.u8();
        if (k == kNull) { e.reset(); return; }
        int line = (int)io.uvar();
        switch ((NK)k) {
#define X(K, T) case NK::K: { auto n = makeNode<T>(); n->line = line; visit(io, *n); e = std::move(n); break; }
            EXPR_KINDS(X)
#undef X
            default: throw AstSerialError{"unknown expression kind in AST cache"};
        }
    } else {
        if (!e) { io.u8(kNull); return; }
        io.u8((uint8_t)e->kind);
        io.uvar((uint64_t)(e->line < 0 ? 0 : e->line));
        switch (e->kind) {
#define X(K, T) case NK::K: visit(io, static_cast<T&>(*e)); break;
            EXPR_KINDS(X)
#undef X
            default: throw AstSerialError{"unserializable expression kind"};
        }
    }
}

template <class IO> void ioStmt(IO& io, StmtPtr& s) {
    if constexpr (IO::reading) {
        uint8_t k = io.u8();
        if (k == kNull) { s.reset(); return; }
        int line = (int)io.uvar();
        std::string label; io_(io, label);
        switch ((NK)k) {
#define X(K, T) case NK::K: { auto n = makeNode<T>(); n->line = line; n->label = label; visit(io, *n); s = std::move(n); break; }
            STMT_KINDS(X)
#undef X
            default: throw AstSerialError{"unknown statement kind in AST cache"};
        }
    } else {
        if (!s) { io.u8(kNull); return; }
        io.u8((uint8_t)s->kind);
        io.uvar((uint64_t)(s->line < 0 ? 0 : s->line));
        io_(io, s->label);
        switch (s->kind) {
#define X(K, T) case NK::K: visit(io, static_cast<T&>(*s)); break;
            STMT_KINDS(X)
#undef X
            default: throw AstSerialError{"unserializable statement kind"};
        }
    }
}

// Block and SubDecl appear as concretely-typed members too.
template <class IO> void ioBlock(IO& io, std::unique_ptr<Block>& b) {
    if constexpr (IO::reading) {
        if (io.u8() == kNull) { b.reset(); return; }
        b = std::make_unique<Block>();
        b->line = (int)io.uvar();
        io_(io, b->label);
        visit(io, *b);
    } else {
        if (!b) { io.u8(kNull); return; }
        io.u8(0);
        io.uvar((uint64_t)(b->line < 0 ? 0 : b->line));
        io_(io, b->label);
        visit(io, *b);
    }
}
template <class IO> void ioSubDecl(IO& io, std::unique_ptr<SubDecl>& d) {
    if constexpr (IO::reading) {
        if (io.u8() == kNull) { d.reset(); return; }
        d = std::make_unique<SubDecl>();
        d->line = (int)io.uvar();
        io_(io, d->label);
        visit(io, *d);
    } else {
        if (!d) { io.u8(kNull); return; }
        io.u8(0);
        io.uvar((uint64_t)(d->line < 0 ? 0 : d->line));
        io_(io, d->label);
        visit(io, *d);
    }
}

} // namespace

// ---- the public entry points -------------------------------------------

std::string serializeAst(const Program& prog) {
    Writer w;
    w.raw(kMagic, 4);
    w.uvar(kAstSerialVersion);
    // const_cast: the visitors are one code path for both directions, and the
    // WRITE direction never mutates. Keeping two mirrored const/non-const
    // hierarchies is exactly the duplication this design exists to avoid.
    auto& stmts = const_cast<std::vector<StmtPtr>&>(prog.stmts);
    ioStmtVec(w, stmts);
    return std::move(w.buf);
}

void deserializeAst(const std::string& blob, Program& out) {
    if (blob.size() < 5 || std::memcmp(blob.data(), kMagic, 4) != 0)
        throw AstSerialError{"not an AST cache blob"};
    Reader r{blob.data() + 4, blob.data() + blob.size()};
    if (r.uvar() != kAstSerialVersion) throw AstSerialError{"AST cache version mismatch"};
    out.stmts.clear();
    ioStmtVec(r, out.stmts);
    if (r.p != r.end) throw AstSerialError{"trailing bytes in AST cache"};
}

} // namespace rakupp
