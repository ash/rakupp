// InterpreterParts.h — what the Interpreter*.cpp files share
//
// What the parts of the interpreter share: every type, helper and variable
// that one Interpreter.cpp-family file defines and another one uses. The parts:
//   Interpreter.cpp            shared helpers, construction, the mainline run, the precompiled-module cache, --exe bundling
//   InterpreterModules.cpp     module loading, EVAL, the REPL's hooks, statements and declarations
//   InterpreterBinding.cpp     closures, signatures and parameter binding, multi dispatch, NativeCall
//   InterpreterCalls.cpp       calls, method invocation, lvalues and the assignment helpers
//   InterpreterRegex.cpp       regexes, substitution, grammars and hyperoperators
//   InterpreterOperators.cpp   operators, mixins, phasers, gather, reductions and subscripts
//   InterpreterCore.cpp        the hot paths: every function the perf-guard kernels spend time in, kept together so they inline into each other and reach tctx_ directly
#pragma once
#include "CNumeric.h"
#include "AsciiCtype.h"
#include "Interpreter.h"
#include "Jit.h"
#include "Digest.h"
#include "Runtime.h"           // consoleAnsi: does an escape sequence reach a terminal that obeys it
#include <functional>
#include <tuple>
#include <memory>
#include <cstring>
#include <limits>
#include "Platform.h"
#include <sys/stat.h>
#ifndef _WIN32
#include <pwd.h>              // getpwuid: $*USER's string face
#include <grp.h>              // getgrgid: $*GROUP's string face
#endif
#if defined(__APPLE__)
#include <mach-o/dyld.h>      // _NSGetExecutablePath: the running binary's identity
#endif
#ifdef __APPLE__
#include <crt_externs.h>
static inline char** rakupp_environ() { return *_NSGetEnviron(); }
#elif defined(_WIN32)
static inline char** rakupp_environ() { return _environ; }
#else
extern char** environ;
static inline char** rakupp_environ() { return environ; }
#endif
#include "Regex.h"
#include "Profiler.h"
#include "Ffi.h"
#include "Lexer.h"
#include "Parser.h"
#include "Unicode.h"
#include "BuiltinsShared.h"
#include "RakuAstClasses.h"
#include "Coro.h"
#include <algorithm>
#include <climits>
#include <cctype>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <chrono>
#include <filesystem>
#include "AstSerial.h"
#include "JsonLite.h"
#include <thread>
#include <ctime>
#include <fstream>
#include <iostream>
#include <regex>
#include <set>
#include "Pod.h"
#include "DeclCheck.h"
#include <sstream>
#if !defined(_WIN32)
#include <dirent.h>   // Windows gets the FindFirstFile-based shim from Platform.h
#endif
#if defined(__OpenBSD__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
#include <signal.h>      // stack_t (used by pthread_stackseg_np's out-param)
#include <pthread_np.h>  // pthread_stackseg_np (OpenBSD) / pthread_attr_get_np (Free/Net/DragonFly)
#endif

// A branch that is almost never taken (a warning path in a hot operator).
// MSVC has no __builtin_expect, so there it is only the condition.
#if defined(__GNUC__) || defined(__clang__)
#define RAKUPP_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define RAKUPP_UNLIKELY(x) (x)
#endif

namespace rakupp {
std::vector<ClassInfo*> c3Linearize(ClassInfo* c, bool& ok);
static bool hasMultipleInheritance(ClassInfo* c);
bool seqIsLazy(const Value& v);   // lazy as Rakudo means it, not merely unpulled
bool isEndlessTailSource(const Value& src);   // slipped last, makes the list endless

// A `{ ... }` / `{ !!! }` body: the routine is a STUB. Was a lambda local to the
// class-declaration case and so was only ever asked about methods — `.yada` on a
// plain `sub f() { ... }` had no answer at all.
static inline bool stmtIsStub(const std::vector<StmtPtr>& body) {
    if (body.size() != 1 || body[0]->kind != NK::ExprStmt) return false;
    Expr* e = static_cast<ExprStmt*>(body[0].get())->e.get();
    if (!e || e->kind != NK::Call) return false;
    auto* c = static_cast<Call*>(e);
    return (c->name == "..." || c->name == "!!!") && c->args.empty() && !c->callee;
}
// --trace (bash -x): a plain global read on every statement — the same cost
// the profiler's hooks were measured at, nothing.
extern bool g_traceStmts;

// --stagestats: the module-load collector. Loads can nest (a module's own
// `use`) and can happen on a worker thread, so a mutex and a per-thread depth.
extern bool g_stageStats;
extern std::mutex g_stageMu;
extern std::vector<StageModuleLoad> g_stageLoads;
[[noreturn]] static void throwCodeNumeric(const Value& v); // `+&f`: code has no number
static void spCallsS(const Stmt* s, std::set<std::string>& out);   // the routine names a statement calls
[[noreturn]] void undeclaredRoutine(const std::string& name);
Value composeCode(const Value& fV, const Value& gV);

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
extern Interpreter* g_revInterp;
// g_objMethodStr (Value.h): a user class's own .gist/.raku for a nested object
bool objMethodStrHook(const Value& v, const char* method, std::string& out);
RAKUPP_CONSTINIT extern thread_local const Value* g_rxRoutine;

// A block used as a subscript dimension that is NOT a WhateverCode: `{0,1}`.
static inline bool isSliceBlock(const Value& k) {
    return k.t == VT::Code && k.code() && !k.code()->isWhateverCode;
}
static inline bool isDefined(const Value& v) { return rtIsDefined(v); }

// A parenthesised LIST LITERAL whose items sit at their own positions: no
// `|slip` shifts them, so item k is element k.
static inline bool plainListLiteral(const ListExpr* le) {
    if (!le || le->items.empty()) return false;
    for (auto& it : le->items)
        if (!it || (it->kind == NK::Unary && static_cast<const Unary*>(it.get())->op == "|")) return false;
    return true;
}
std::string hashSubKey(const Value& k, const Value* base = nullptr);
extern std::function<Value*(const std::string&)> g_lexInfixLookup;

// `Any` has two spellings inside the interpreter: an untyped slot with nothing
// in it holds VT::Any (the default-constructed Value), while the TERM `Any`
// parses to VT::Type with s == "Any". They denote one object, and everything
// that looks at them already agrees — .WHAT, .defined, .gist, .raku, and
// whichOf(), which renders "Any|" for either. Only the identity operators
// disagreed, because both open by rejecting a tag mismatch, so the extremely
// ordinary `my $x; $x === Any` answered False while `my Int $y; $y === Int`
// (a typed slot, which stores the type object) answered True.
//
// Fixed here rather than by making every undefined slot store the type object:
// VT::Any IS the default-constructed Value, so it arrives from hundreds of
// places — a missing hash key, an empty return, an extension's rk_any(). The
// piecemeal approach was already tried and is visibly incomplete: the subscript
// miss at "the TYPE OBJECT (`Str` for `my Str @s`, `Any` untyped)" normalises
// its own result for exactly this reason, and `%h<nope> === Any` was still
// False. One rule where identity is decided beats a normalisation at every
// site that can produce an undefined value.
//
// NOT applied to =:=, which is container identity: `my $x; $x =:= Any` is
// False on Rakudo too, and a container is not the object it holds.
static bool syntacticNamedArg(const Expr* a, const Value& v);

static inline bool isAnyTypeObject(const Value& v) {
    return v.t == VT::Any || (v.t == VT::Type && v.s == "Any" && v.ofType().empty());
}

int ncScalarWidth(const std::string& t, bool& sign, bool& isFloat);
bool valueEqv(const Value& a, const Value& b);
bool isSetOpStr(const std::string& o);
static bool boxedIteration(const Value& v);
void spDeclaredInRaw(const std::vector<StmtPtr>& body, std::vector<const VarExpr*>& out);
bool pureSubscriptBase(const Expr* b);

bool valueEqv(const Value& a, const Value& b);

static inline Value defaultFor(char sigil) {
    if (sigil == '@') return Value::array();
    if (sigil == '%') return Value::makeHash();
    return Value::any();
}
// default value for a typed declaration: native lowercase types (num/int/str) have
// concrete defaults; a named type (`my Int $x`) defaults to that type object.
Value typedDefault(const std::string& type, char sigil);

// What a DECLARATION starts its variable at. Normally the declared type's empty
// container, but a PARAMETERIZED type is carried as the expression `Type[args]`
// too (VarExpr.declTypeExpr): its textual name is only source text, so
// `my BinaryHeap::MinHeap[{ $^a.tail <=> $^b.tail }] $h` — Graph's priority
// queues — named no registered class and every method on `$h` fell through to a
// built-in. Running the expression yields the real parameterized type object,
// which is what `$h.push` then dispatches on.
// A type NAME that is really a type CAPTURE bound in scope — `sub f(::T $x) {
// my T $y; … }`, `sub g(::T \t --> T)` — stands for the type the capture bound
// for THIS call, smiley and all (`f(Int:D)` binds Int:D). Not a name any type
// is declared under: a real type of that name wins.
// A type name the LEXICAL scope binds to a registered type under another
// registry name — an `import`ed `Bar` that means `Foo::Bar`, a `constant`
// naming a type — is that type, before the global registry's own `Bar`:
// Raku looks a name up lexically first (S11-modules/export.t).
static inline std::string lexicalTypeName(Interpreter& I, const std::string& n) {
    if (n.empty() || !Interpreter::tctx_.cur) return n;
    Value* tv = Interpreter::tctx_.cur->find(n);
    if (tv && tv->t == VT::Type && !tv->s.empty() && tv->s != n && I.classes_.count(tv->s))
        return tv->s;
    return n;
}
bool capturedType(Interpreter& I, Env* scope, const std::string& name, Value& out);

// ---- generic role bodies ---------------------------------------------------
// Every name a subtree MENTIONS — variables with their sigils, terms, routine
// names (bare and `&`), the words of a type spelled as text (`Array[T]`,
// `T:D`) — and every name it DECLARES. Deliberately generous: a name reported
// that is not really used only moves a role-body statement from "runs once"
// to "runs per parameterization".
namespace {
struct NameScan {
    std::set<std::string> refs, decls;
    bool dynamic = false;   // EVAL or a symbolic lookup: it may name anything
    void words(const std::string& s) {
        const size_t n = s.size();
        size_t i = 0;
        while (i < n) {
            const unsigned char ch = (unsigned char)s[i];
            if (ascii::isalpha(ch) || ch == '_' || ch >= 0x80) {
                size_t j = i;
                while (j < n) {
                    const unsigned char c = (unsigned char)s[j];
                    if (ascii::isalnum(c) || c == '_' || c == '-' || c == '\'' || c >= 0x80) { j++; continue; }
                    if (c == ':' && j + 2 < n && s[j + 1] == ':') { j += 2; continue; }
                    break;
                }
                refs.insert(s.substr(i, j - i));
                i = j;
            }
            else if ((ch == '$' || ch == '@' || ch == '%' || ch == '&') && i + 1 < n) {
                size_t j = i + 1;
                if (s[j] == '!' || s[j] == '.' || s[j] == '*' || s[j] == '?') j++;
                size_t k = j;
                while (k < n && (ascii::isalnum((unsigned char)s[k]) || s[k] == '_' || s[k] == '-' ||
                                 (unsigned char)s[k] >= 0x80)) k++;
                if (k > j) refs.insert(std::string(1, (char)ch) + s.substr(j, k - j));
                i = k > i + 1 ? k : i + 1;
            }
            else i++;
        }
    }
    void decl(const std::string& nm) {
        if (nm.empty()) return;
        decls.insert(nm);
        if (nm[0] == '\\' && nm.size() > 1) decls.insert(nm.substr(1));
    }
    void params(const std::vector<Param>& ps) {
        for (auto& p : ps) {
            words(p.type); words(p.coerceFrom);
            if (!p.captureName.empty()) decl(p.captureName);
            expr(p.whereExpr.get()); expr(p.litVal.get()); expr(p.defaultVal.get());
            for (auto& d : p.shapeDimExprs) expr(d.get());
            for (auto& t : p.userTraits) expr(t.second.get());
            if (p.subSig) params(*p.subSig);
            if (p.codeSig) params(*p.codeSig);
        }
    }
    void body(const std::vector<StmtPtr>& b) { for (auto& s : b) stmt(s.get()); }
    void expr(const Expr* e) {
        if (!e) return;
        switch (e->kind) {
        case NK::AllomorphLit: expr(static_cast<const AllomorphLit*>(e)->num.get()); break;
        case NK::RegexLit: words(static_cast<const RegexLit*>(e)->pattern); break;
        case NK::SubstLit: { auto* x = static_cast<const SubstLit*>(e); words(x->pattern); words(x->repl); break; }
        case NK::ChainExpr: for (auto& o : static_cast<const ChainExpr*>(e)->operands) expr(o.get()); break;
        case NK::InterpStr: for (auto& p : static_cast<const InterpStr*>(e)->parts) expr(p.get()); break;
        case NK::VarExpr: {
            auto* v = static_cast<const VarExpr*>(e);
            refs.insert(v->name);
            if (v->declare) decl(v->name);
            words(v->declType); words(v->declCoerce); words(v->declCoerceFrom);
            words(v->containerIs); words(v->containerOf);
            if (!v->declStubType.empty()) decl(v->declStubType);
            expr(v->declDefault.get()); expr(v->declShape.get()); expr(v->declTypeExpr.get());
            expr(v->declWhereExpr);
            break;
        }
        case NK::NameTerm: {
            auto* t = static_cast<const NameTerm*>(e);
            refs.insert(t->name); words(t->ofType);
            break;
        }
        case NK::ListExpr: for (auto& x : static_cast<const ListExpr*>(e)->items) expr(x.get()); break;
        case NK::ArrayLit: for (auto& x : static_cast<const ArrayLit*>(e)->items) expr(x.get()); break;
        case NK::HashLit: for (auto& x : static_cast<const HashLit*>(e)->items) expr(x.get()); break;
        case NK::SymbolicRef: {
            auto* x = static_cast<const SymbolicRef*>(e);
            dynamic = true;   // `::('$y')`: a name this scan cannot see
            words(x->pkg); expr(x->nameExpr.get());
            for (auto& sg : x->segs) expr(sg.get());
            break;
        }
        case NK::Assign: { auto* x = static_cast<const Assign*>(e); expr(x->target.get()); expr(x->value.get()); break; }
        case NK::Binary: { auto* x = static_cast<const Binary*>(e); expr(x->lhs.get()); expr(x->rhs.get()); break; }
        case NK::Unary: expr(static_cast<const Unary*>(e)->operand.get()); break;
        case NK::Call: {
            auto* x = static_cast<const Call*>(e);
            if (x->name == "EVAL" || x->name == "EVALFILE") dynamic = true;   // code compiled at run time
            if (!x->name.empty()) { refs.insert(x->name); refs.insert("&" + x->name); }
            expr(x->callee.get());
            for (auto& a : x->args) expr(a.get());
            break;
        }
        case NK::MethodCall: {
            auto* x = static_cast<const MethodCall*>(e);
            expr(x->inv.get()); expr(x->methodExpr.get()); words(x->methodQual);
            for (auto& a : x->args) expr(a.get());
            break;
        }
        case NK::Index: { auto* x = static_cast<const Index*>(e); expr(x->base.get()); expr(x->index.get()); break; }
        case NK::Ternary: {
            auto* x = static_cast<const Ternary*>(e);
            expr(x->cond.get()); expr(x->then.get()); expr(x->els.get());
            break;
        }
        case NK::Range: { auto* x = static_cast<const RangeExpr*>(e); expr(x->from.get()); expr(x->to.get()); break; }
        case NK::Pair: { auto* x = static_cast<const PairExpr*>(e); expr(x->keyExpr.get()); expr(x->value.get()); break; }
        case NK::NqpOp: for (auto& a : static_cast<const NqpOp*>(e)->args) expr(a.get()); break;
        case NK::BlockExpr: {
            auto* x = static_cast<const BlockExpr*>(e);
            params(x->params); body(x->body); words(x->retType);
            break;
        }
        default: break;
        }
    }
    void block(const Block* b) { if (b) body(b->stmts); }
    void stmt(const Stmt* s) {
        if (!s) return;
        switch (s->kind) {
        case NK::ExprStmt: expr(static_cast<const ExprStmt*>(s)->e.get()); break;
        case NK::VarDecl: {
            auto* x = static_cast<const VarDecl*>(s);
            for (auto& n : x->names) decl(n);
            expr(x->init.get());
            break;
        }
        case NK::SubDecl: {
            auto* x = static_cast<const SubDecl*>(s);
            if (!x->name.empty()) { decl(x->name); decl("&" + x->name); }
            expr(x->nameExpr.get());
            params(x->params);
            for (auto& ap : x->altParams) params(ap);
            body(x->body);
            words(x->retType);
            for (auto& t : x->traits) expr(t.arg.get());
            for (auto& a : x->immediateArgs) expr(a.get());
            expr(x->retLiteral.get()); expr(x->deprecatedWith.get());
            expr(x->nativeLibExpr.get()); expr(x->nativeSymExpr.get());
            break;
        }
        case NK::ClassDecl: {
            auto* x = static_cast<const ClassDecl*>(s);
            decl(x->name);
            expr(x->nameExpr.get());
            words(x->parent);
            for (auto& p : x->extraParents) words(p);
            for (auto& r : x->roles) words(r);
            for (auto& t : x->trustsNames) words(t);
            for (auto& ra : x->roleArgs) for (auto& a : ra.second) expr(a.get());
            for (auto& ut : x->userTraits) expr(ut.second.get());
            params(x->roleParams);
            for (auto& a : x->attrs) {
                words(a.type); words(a.containerIs); words(a.coerceFrom);
                expr(a.def.get()); expr(a.defaultTrait.get()); expr(a.whereExpr.get()); expr(a.shape.get());
                for (auto& ut : a.userTraits) expr(ut.second.get());
            }
            for (auto& m : x->methods) if (m) stmt(m.get());
            body(x->body);
            break;
        }
        case NK::Block: block(static_cast<const Block*>(s)); break;
        case NK::EnumDecl: {
            auto* x = static_cast<const EnumDecl*>(s);
            decl(x->name); expr(x->values.get()); words(x->ofType);
            break;
        }
        case NK::SubsetDecl: {
            auto* x = static_cast<const SubsetDecl*>(s);
            decl(x->name); words(x->baseType); words(x->coerceFrom); expr(x->where.get());
            break;
        }
        case NK::NamedRegexDecl: {
            auto* x = static_cast<const NamedRegexDecl*>(s);
            decl(x->name); words(x->pattern);
            break;
        }
        case NK::IfStmt: {
            auto* x = static_cast<const IfStmt*>(s);
            for (auto& br : x->branches) { expr(br.first.get()); block(br.second.get()); }
            for (auto& bp : x->branchParams) params(bp);
            params(x->elseParams);
            block(x->elseBlock.get());
            break;
        }
        case NK::WhileStmt: {
            auto* x = static_cast<const WhileStmt*>(s);
            expr(x->cond.get()); params(x->params); block(x->body.get());
            break;
        }
        case NK::ForStmt: {
            auto* x = static_cast<const ForStmt*>(s);
            expr(x->list.get()); params(x->params); block(x->body.get());
            break;
        }
        case NK::ReturnStmt: expr(static_cast<const ReturnStmt*>(s)->value.get()); break;
        case NK::UseStmt: {
            auto* x = static_cast<const UseStmt*>(s);
            expr(x->ifCond.get()); expr(x->fileExpr.get()); expr(x->argExpr.get());
            break;
        }
        case NK::GivenStmt: {
            auto* x = static_cast<const GivenStmt*>(s);
            expr(x->topic.get()); params(x->params); params(x->elseParams);
            block(x->body.get()); block(x->elseBody.get());
            break;
        }
        case NK::WhenStmt: {
            auto* x = static_cast<const WhenStmt*>(s);
            expr(x->cond.get()); block(x->body.get());
            break;
        }
        case NK::LoopStmt: {
            auto* x = static_cast<const LoopStmt*>(s);
            expr(x->init.get()); expr(x->cond.get()); expr(x->incr.get()); block(x->body.get());
            break;
        }
        case NK::RepeatStmt: {
            auto* x = static_cast<const RepeatStmt*>(s);
            expr(x->cond.get()); block(x->body.get());
            break;
        }
        default: break;
        }
    }
};

// Does a mentioned name fall in the tainted set? A qualified name is tainted
// by its first part: `G::A` is the package G's.
static inline bool taintedName(const std::set<std::string>& t, const std::string& r) {
    if (t.count(r)) return true;
    size_t c = r.find("::");
    return c != std::string::npos && c > 0 && t.count(r.substr(0, c)) > 0;
}
} // namespace

// What a NATIVE container refuses outright, before wrapNative truncates.
//
// Rakudo takes any Int it can unbox and truncates it to the container's width —
// `my int8 $x = 300` is 44 and `my uint8 $x = -1` is 255 — but a value too wide
// for a native int AT ALL is an error naming the bit count, and a Str is a type
// check failure however numeric its text. rakupp truncated both: `my int8 $x =
// 'foo'` stored 0 and `my int64 $x = 2**63` wrapped to the minimum
// (S02-types/int-uint.t; Int-Num-Rat sheet N-27).
//
// Nor is anything converted on the way in: a native int holds an Int (Bool
// and an Int-valued enum are Ints) and a native num holds a Num, so
// `my num $x = 1/2`, `my int $x = 7/2`, `my num $x = $some-int` and
// `my int $x = 1.5e0` all die at run time as they do in Rakudo — the literal
// spellings are refused at compile time already. rakupp stored 0.5 and 3.
//
// …except from ANOTHER native, which converts: `$an-int-native` into a num is
// its Num, a native num into an int truncates toward zero (saturating, NaN
// 0). A value carries a native's tags only when it is one — read from a
// native, or a native operation's result — since every boxed container drops
// them (dropNativeTags). Oracle-checked against Rakudo 2026.09.
static inline bool nativeRefusesKind(const Value& v, bool isFloat) {
    if (v.isNumeric() && (v.hashKind == "Duration" || v.hashKind == "Instant")) return true;
    switch (v.t) {
        case VT::Rat: case VT::Complex: return true;
        case VT::Num:                   return !isFloat && !(v.natBits && v.natFloat);
        case VT::Int: case VT::Bool:    return isFloat && !(v.natBits && !v.natFloat);
        default:                        return false;
    }
}

// The declared spelling of a native container from its slot tags.
static inline std::string nativeTypeName(int bits, bool isFloat, bool sign) {
    if (isFloat) return bits == 32 ? "num32" : "num";
    std::string base = sign ? "int" : "uint";
    return bits == 64 ? base : base + std::to_string(bits);
}

// The refusal: an X::AdHoc, the class Rakudo raises. `where` names the target
// (`$x`, or `parameter $x`).
[[noreturn]] static inline void throwNativeKind(const Value& v, const std::string& natType, bool isFloat,
                                         const std::string& where) {
    throw RakuError{Value::typeObj("X::AdHoc"),
        "Cannot put a " + v.typeName() + " (" + v.gist() + ") into the native " +
        natType + " " + where + ": it holds " +
        (isFloat ? "a Num" : "an Int") + " only (coerce it with ." + (isFloat ? "Num" : "Int") + ")"};
}
[[noreturn]] static inline void throwNativeKind(const Value& v, int bits, bool isFloat, bool sign,
                                         const std::string& where) {
    throwNativeKind(v, nativeTypeName(bits, isFloat, sign), isFloat, where);
}

// A value read from a native container carries its tags — operators and multi
// dispatch look at them — but a NON-native container that takes it must drop
// them, or it starts to wrap and refuse as a native would: `my $n = $an-int;
// $n = 1/2` stored 0 (and `$n /= 2` truncated) where Rakudo holds 0.5.
static inline void dropNativeTags(Value& v) { v.natBits = 0; v.natSigned = v.natFloat = false; }


// A native container cannot hold a type object, Nil included: `my int $x = Nil`
// dies "Cannot unbox a type object (Nil) to int." rather than storing (Any).
// The declared type is on a DECLARING target; a sized native assigned later is
// known by its slot's natBits.
void nativeUndefCheck(const Value& rv, const Expr* target, const Value* slot);

// Truncate an integer value to a native type's bit width (wraparound), keeping the tag.
void wrapNative(Value& v, int bits, bool sign, bool isFloat = false);

// ---- placeholder ($^a) collection ----
void collectPHExpr(const Expr* e, std::set<std::string>& out);
static std::string privMixinKey(const std::string& name);
void collectPHStmt(const Stmt* s, std::set<std::string>& out);
bool ifCondValueUsed(const IfStmt* is);   // an if reads its condition's VALUE (IfStmt::condValueUsed)
bool isDimslipIndex(const Expr* e);   // `@a[|| …]`, a dimension slip
bool assignSpawns(const Assign* a);   // the initializer runs a `start` (cached per node)

void collectPHExpr(const Expr* e, std::set<std::string>& out);

// --- gather replay support -------------------------------------------------
// A lazy gather extends itself by RE-RUNNING its block with a larger take cap
// (there are no coroutines here). That only reproduces the earlier elements if
// the block is REPLAYABLE — a block that mutates state it did not declare
// resumes from the mutated variables instead, and the elements the re-run then
// skipped are lost without a word. Math::NumberTheory's accel-asc keeps its
// partition state in three variables of the enclosing sub and answered 64 of
// the 77 partitions of 12. So: find those variables, and restore them before
// every re-run.
//
// Over-collecting is safe (an extra variable is snapshotted and restored to the
// same value); under-collecting only returns to the old behaviour.
void gatherWritesExpr(const Expr* e, std::set<std::string>& writes,
                             std::set<std::string>& declared);
void gatherWritesStmt(const Stmt* s, std::set<std::string>& writes,
                             std::set<std::string>& declared);
void gatherWritesExpr(const Expr* e, std::set<std::string>& writes,
                             std::set<std::string>& declared);
void gatherWritesStmt(const Stmt* s, std::set<std::string>& writes,
                             std::set<std::string>& declared);

// One walk serves both hoisted phasers: `want` names the one being collected,
// INIT (run once before the mainline) or END (run once at program exit). Both
// leave their textual position behind, so both need the SAME whole-unit walk —
// statements and expressions alike, because a phaser can be buried under
// `say(gather(for … ))`. A node kind the walk does not model leaves its phaser
// uncollected, and an uncollected phaser keeps running where it is written:
// a gap degrades to the old behaviour, never to a phaser that vanishes.
void collectPhasersStmt(Stmt* s, const char* want, std::vector<Block*>& out, bool topLevel = false);
// The statement lists an END walk is currently inside, and the (list, END) edges
// it has found. An END is re-captured by the entry of EVERY block that lexically
// holds it, not just its own — Rakudo's compiler flattens the blocks between a
// phaser and its routine into one frame, so `sub f($n) { if $n == 1 { END say $n } }`
// called f(1), f(2) says 2 there even though the `if` body never ran a second
// time. Kept beside the walk rather than threaded through its forty call sites;
// thread-local because an EVAL on a worker registers its own unit. Only an END
// walk fills them, and registerEnds empties them.
extern thread_local std::vector<const std::vector<StmtPtr>*> g_endChain;
extern thread_local std::vector<Block*> g_endBlockChain;
extern thread_local std::vector<std::pair<const std::vector<StmtPtr>*, Block*>> g_endEdges;
void collectPhasersStmt(Stmt* s, const char* want, std::vector<Block*>& out, bool topLevel);

void failureDetonate(const Value& v); // (defined with the exception helpers below)
void tagTemporal(const std::string& op, const Value& l, const Value& r, Value& res); // Instant/Duration algebra
// A RakuError raised inside a regex/grammar block that must NOT leave the parse:
// the block's own compile error ("EVAL parse error" — a parser gap, kept quiet
// as before the Grand Review) and loop control the EVAL turned into
// X::ControlFlow (`{ last }` in a regex block — the enclosing loop cannot see it
// yet; ledger). A user's `die` is neither and propagates (Rakudo).
static inline bool regexBlockErrorStaysQuiet(const RakuError& e) {
    return e.message.rfind("EVAL parse error", 0) == 0 ||
           e.message.find(" without a supporting loop construct") != std::string::npos;
}
void collectPHStmt(const Stmt* s, std::set<std::string>& out);

// gather { … } for native codegen: same probe-and-double laziness as the
// interpreter's gather — run the block collecting takes up to a cap; if the cap
// is hit the result is lazy and extends by re-running with a doubled cap.
Value gatherSeqForNative(Interpreter& I, Value blockClosure);

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
Value strRangeList(const std::string& min, const std::string& to, bool exFrom, bool exTo);

// `from .. to` for native codegen. Str endpoints follow the interpreter's NK::Range
// eval exactly — single-codepoint endpoints are a real Range VALUE, everything else
// the eager list above. The single-codepoint branch was missing here, so `'a'..'c'`
// A Range endpoint must be a single ordered value: a Range, a Complex or a Seq
// there is X::Range::InvalidArg, which names the offending type and carries it.
// (Rakudo's message is "<Type> objects are not valid endpoints for Ranges".)
// Does `$x..*` step by fractions? Only a FINITE Num or Rat start does: the
// elements are then x, x+1, x+2, … and not the integers around x. An Int start
// keeps the plain integer representation (the hot `1..*` path), and an infinite
// or NaN one has no fractional part to preserve.
static inline bool endlessFracStart(const Value& v) {
    return (v.t == VT::Num || v.t == VT::Rat) && std::isfinite(v.toNum());
}

// The integer field for a range's TOP endpoint. `Inf` saturates to the int64
// maximum on its own, which is the sentinel for "endless" — but `NaN.toInt()`
// is 0, so `1..NaN` came out as the empty `1..0` where Rakudo walks 1, 2, 3
// for ever (no comparison against NaN is ever true, so nothing stops it).
static inline long long rangeTopInt(const Value& v) {
    if (v.t == VT::Num && std::isnan(v.n)) return 9223372036854775807LL;
    return v.toInt();
}

Value strRangeList(const std::string& min, const std::string& to, bool exFrom, bool exTo);

// A MULTI-character string range is a Range VALUE: `.raku` is `"aa".."ad"`,
// `.min`/`.max`/`.minmax` are its endpoints and `~~` orders by string, while
// its ELEMENTS come from the successor walk above (g_strRangeElems, which
// Value::flatten calls). The integer fields hold the first codepoints only so
// code that reads them sees the right ordering of the two starts.
static inline Value strRangeVal(const Value& from, const Value& to, bool exFrom, bool exTo) {
    Value r = Value::range(u8FirstCp(from.s), u8FirstCp(to.s), exFrom, exTo);
    r.ofTypeM() = "Str";
    attachRangeEnds(r, Value::str(from.s.str()), Value::str(to.s.str()));
    return r;
}

// `"1"..9` — a STRING on the left and a number on the right. The right side is
// stringified rather than the left numified, so the elements are strings and
// the comparison is string order: `"a"..5` is empty because "a" comes after
// "5". Both endpoints are carried so `.min` is still the Str and `.raku` still
// shows the number on the right as a number.
static inline Value strLeftRange(const Value& from, const Value& to, bool exFrom, bool exTo) {
    std::string tos = to.toStr();
    if (u8CpLen(from.s) == 1 && u8CpLen(tos) == 1) {
        Value r = Value::range(u8FirstCp(from.s), u8FirstCp(tos), exFrom, exTo);
        r.ofTypeM() = "Str";
        attachRangeEnds(r, from, to);
        return r;
    }
    return strRangeList(from.s.str(), tos, exFrom, exTo);
}

static inline void checkRangeEndpoint(const Value& v) {
    const char* kind = nullptr;
    if (v.t == VT::Range) kind = "Range";
    else if (v.t == VT::Complex) kind = "Complex";
    else if (v.t == VT::Array && v.isList && v.s == "Seq") kind = "Seq";
    if (!kind) return;
    if (g_revInterp)
        g_revInterp->throwTypedV("X::Range::InvalidArg", {{"got", v}},
                                 std::string(kind) + " objects are not valid endpoints for Ranges");
    throw RakuError{Value::typeObj("X::Range::InvalidArg"),
                    std::string(kind) + " objects are not valid endpoints for Ranges"};
}

static inline Value hashToPairs(const Value& v) {
    Value out = Value::array(); out.isList = true;
    if (!v.hash()) return out;
    bool setty = v.hashKind == "Set" || v.hashKind == "SetHash";
    for (auto& kv : *v.hash()) {
        Value p = setty ? Value::pair(kv.first, Value::boolean(true)) : hashEntryPair(v, kv.first, kv.second);
        p.pairKeyM() = kv.second.elemKey(); // Set/Bag/Mix: recover the element's original type
        out.arr()->push_back(std::move(p));
    }
    return out;
}

bool isNativeScalarName(const std::string& t);
// A COPY's elements are values in fresh containers of its own, so the source's
// container state stays behind: an element bound to a VALUE (`@a[1] := 42`, which
// left it readonly) copies as a writable one, and an element bound to another
// container (a Proxy onto a shared cell, or any Proxy) copies as what it holds —
// `my %g = %h` after `%h<b> := $var` must not follow $var (S03-binding/hashes.t).
// One test of two bytes per element on the fresh, cache-hot buffer.
static inline void decontCopied(Value& e) {
    if (e.readonly | (e.t == VT::Hash)) {
        if (e.t == VT::Hash && e.hash() && g_deproxy && e.hashKind == "Proxy") e = g_deproxy(e);
        e.readonly = e.immutableBind = false;
    }
}
static inline void decontCopiedElems(ValueList& l) {
    for (auto& e : l) decontCopied(e);
}

Value coerceArray(const Value& v, bool nativeTarget = false);
Value coerceHash(const Value& v, bool store = false, bool objKeyed = false);
extern Interpreter* g_cbInterp;

// the live interpreter's class registry, for free-function smartmatch on user
// type objects (applyArith has no Interpreter&); the newest instance wins
extern std::unordered_map<std::string, std::shared_ptr<ClassInfo>>* g_matchClasses;
// …and the alias table beside it, for the identity operators (free functions)
extern const std::unordered_map<std::string, std::string>* g_classAliases;
// subset check for free-function ~~ (`5 ~~ Five`): returns true and sets `out`
// when the RHS names a live subset; the newest interpreter instance wins
extern std::function<bool(const std::string&, const Value&, bool&)> g_subsetCheck;
// applyArith is a free function, but the Whatever-curry it builds has to resolve
// a shadowing `&infix:<op>` in the scope it is being written in — same shape as
// g_deproxy, and set from the same place.
extern std::function<Value*(const std::string&)> g_lexInfixLookup;

void rtSetAliasView(const std::unordered_map<std::string, std::string>* a,
                    const std::unordered_map<std::string, std::shared_ptr<ClassInfo>>* c); // defined near typeMatchesArg

// ---- a block's lexicals exist from the moment the block is ENTERED ---------
//
// hoistSubs (just below) puts a named sub in scope before the block's first
// statement runs, because a named sub is visible across its whole enclosing
// scope whatever its textual position. The CONTAINER it closes over has to be
// there just as early — Raku allocates a block's lexicals when the block is
// entered, not when the declaration statement executes — and a bare block did
// not do that. So
//
//     { say foo(); say foo(); my $a; sub foo { $a++ } }
//
// gave each call a freshly auto-vivified `$a` and answered 0 twice, where the
// IDENTICAL program at file scope answers 0, 1: the mainline has pre-declared
// its top-level `my`s all along (the loop beside the pad install in run()).
// Seven assertions of roast S02-names-vars/variables-and-packages.t are that
// difference, across three idioms — a plain `my $a`, one with an initialiser,
// and one filled by a BEGIN block.
//
// Only the names a hoisted sub MENTIONS are pre-declared, never every `my` in
// the block. hoistExprDecls says what a blanket hoist costs — it disturbs loop
// and gather per-iteration freshness — and the narrow rule needs no such
// argument to be made: the sub's own capture is the thing being repaired, so
// the sub's own free names are the whole of the list.

// Every variable a body MENTIONS without declaring, nested bodies included
// (an inner closure captures through its outer one just the same). Liberal on
// purpose, and safe for being so: a name that is really the sub's own local
// costs nothing, because only names the BLOCK declares are ever acted on, and
// a mention this misses (inside a regex literal, say) simply leaves that case
// as it was. Cheap because it runs once per block, for blocks with a sub.
void collectMentionedS(const Stmt* s, std::set<std::string>& out);
static inline void collectMentionedB(const Block* b, std::set<std::string>& out) {
    if (b) for (auto& s : b->stmts) collectMentionedS(s.get(), out);
}
void collectMentionedE(const Expr* e, std::set<std::string>& out);
void collectMentionedS(const Stmt* s, std::set<std::string>& out);

void sinkWarnStmt(Stmt* s, bool nilHint, std::vector<std::string>& out);

void sinkWarnStmt(Stmt* s, bool nilHint, std::vector<std::string>& out);

// A search-path entry may be a repository SPEC rather than a plain directory —
// Rakudo's `use lib "inst#/prefix"` / `"file#/dir"` spellings, equally valid
// in RAKULIB and -I. inst# names a CURI installation store (what `rakupp
// install --to` writes): it belongs in the short/-index lookup, not the
// directory probe. file# is just the explicit spelling of the directory
// default. Before this, an inst# entry was probed as a literal directory
// named "inst#…" and silently found nothing.
static inline bool repoSpecStore(const std::string& entry, std::string& prefixOut) {
    if (entry.compare(0, 5, "inst#") == 0) { prefixOut = entry.substr(5); return true; }
    return false;
}
static inline std::string repoSpecDir(const std::string& entry) {
    if (entry.compare(0, 5, "file#") == 0) return entry.substr(5);
    return entry;
}
// The store prefixes one lookup consults: inst# entries from the search path
// first (an explicit `use lib` / -I outranks the machine's defaults), then
// the default repos (~/.raku, the Rakudo Cellar stores).
static inline std::vector<std::string> repoPrefixesFor(const std::vector<std::string>& searchPath) {
    std::vector<std::string> repos;
    std::string pre;
    for (auto& e : searchPath)
        if (repoSpecStore(e, pre)) repos.push_back(pre);
    for (auto& r : rakuRepoPrefixes()) repos.push_back(r);
    return repos;
}
std::map<std::string, std::string>& embeddedModuleDist();

// BOTH OFF for now. On the measurements, `modules` is a clear win and turning it
// on by default would be defensible — but nothing writes to a user's disk until
// they ask, and this cache is young: every bug found in it so far has been in
// deciding whether to REUSE an entry, which is precisely the kind of thing a
// default makes everyone's problem. Flipping this one argument to `true` is the
// whole change when that confidence is there.
bool precompModulesEnabled();

std::string precompPath(const std::string& srcPath,
                               const std::vector<std::string>& searchPath);

bool precompRead(const std::string& path, const std::string& src,
                        std::string& blobOut, std::string& finishOut);

void precompWrite(const std::string& path, const std::string& srcPath,
                         const std::string& src,
                         const std::string& blob, const std::string& finish,
                         const std::vector<std::pair<std::string, std::string>>& deps);


// Modules compiled into this binary (see rakuppRegisterModule in Interpreter.h).
// Written once at startup by generated code, read-only thereafter, so no lock:
// registration happens before the program — and therefore any thread — runs.
struct EmbeddedModule { std::string blob, finish; };
std::map<std::string, EmbeddedModule>& embeddedModules();


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
bool isPragmaName(const std::string& name);

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
bool dirEntryCaseExact(const std::string& cand);

// `use Foo:ver<...>` constraint check — defined below with the version
// helpers; the installed-dist picker needs it first.
bool verSatisfies(const std::string& have, const std::string& want);

// Version ORDER, for choosing among installed candidates: dot-separated
// segments compare numerically ("0.1.10" is newer than "0.1.9"), a missing
// segment is 0 — the same reading verSatisfies gives a segment. <0 / 0 / >0.
int verCmp(const std::string& a, const std::string& b);

// Pick ONE dist out of a short/<sha1(name)>/ index directory, which holds one
// 5-line entry file per installed dist providing the name (ver / auth / api /
// source-sha / dist-id). Several versions routinely COEXIST — installing a
// dependency pinned `:ver<0.1.7>` does not remove an already-installed 0.1.8 —
// and readdir order is filesystem-arbitrary (APFS hashes names), so taking the
// first entry loaded Statistics::Distributions as 0.1.7 or 0.1.8 by directory
// hash. The winner is chosen instead: the NEWEST version that satisfies
// verReq, which is how Rakudo resolves the same store. False when none does.
bool pickInstalledDist(const std::string& shortDir, const std::string& verReq,
                              std::string& entryOut, std::vector<std::string>& linesOut);

// Name → the export TAGS of an `is export(:foo :bar)` sub. Only tag-bearing subs
// are recorded; a plain `is export` (default) has no entry. A tag that is not
// DEFAULT/MANDATORY is a SELECTIVE export — published only when the importer
// asks for it (`use Mod :foo`), as in Rakudo. `sub prompt is export(:prompt)`.
void collectExportTagsByName(const std::vector<StmtPtr>& stmts,
                             std::map<std::string, std::vector<std::string>>& out);

// Does a candidate module version satisfy a `use Foo:ver<...>` constraint?
// Forms: "0.0.14+" (>= — the common ecosystem spelling), an exact version, or
// a prefix with ".*". Segments compare numerically; a missing segment is 0.
bool verSatisfies(const std::string& have, const std::string& want);

// The scopes EVALs are running in, innermost last: an EVAL's code runs in the
// scope that called it, and for as long as it runs that scope is its UNIT::.
extern thread_local std::vector<std::shared_ptr<Env>> g_evalUnits;
// the classes whose BODY is running right now, innermost last
extern thread_local std::vector<std::string> g_classBodies;

std::string btDisplayPath(const std::string& file, const std::string& srcAbs,
                                 const std::string& srcAsGiven); // defined with the backtrace renderer below
void blockDeclNames(const std::vector<StmtPtr>& stmts, std::vector<std::string>& out);
// The `my` names a statement list declares at ITS OWN level (an inner block's
// declarations belong to that block). Used to give a LEAVE/KEEP/UNDO body the
// slots its block would have had in a compile-time pad — see runLeavePhasers.
void blockDeclNames(const std::vector<StmtPtr>& stmts, std::vector<std::string>& out);

bool gatherCancelling();

// Does this subtree contain a `state` declaration the enclosing loop's state
// frame would host? CONSERVATIVE: any node kind the walk doesn't enumerate
// answers YES, so the loop keeps its frame; only provably state-free bodies
// skip it. The frame is one extra Env hop on EVERY lookup in the loop, which
// measured ~5% on the loopsum perf kernel — state-free hot loops shouldn't pay.
bool mayHaveStateDecl(const Expr* e);
bool mayHaveStateDecl(const Stmt* s);
static inline bool mayHaveStateDecl(const Block* b) {
    if (!b) return false;
    for (auto& st : b->stmts) if (mayHaveStateDecl(st.get())) return true;
    return false;
}
bool mayHaveStateDecl(const Expr* e);
bool mayHaveStateDecl(const Stmt* s);

void failureDetonate(const Value& v); // an unhandled Failure blows up when USED or SUNK

bool forTailVarsExpr(const Expr* e, std::vector<const void*>& out);

// A Pair argument counts as *named* only if it was written syntactically (k=>v / :k(v));
// a Pair that arrived as a value (from a variable, list, iteration, or return) is positional.
static inline bool isNamedArg(const Value& v) { return v.t == VT::Pair && v.namedArg; }

// Throw a typed exception as a real OBJECT carrying attributes, so
// throws-like matchers (`symbol => '$!bar'`) can introspect it. The class is
// registered on first use with exactly the attributes passed.
// An exception that IS an X::AdHoc — the class itself, or one of the engine's
// own names parented to it. X::AdHoc's whole content is `.payload`, so a handler
// written against Rakudo reads it: `CATCH { when X::AdHoc { note .payload } }`.
// Claiming the ancestry without carrying the attribute would make the claim a
// lie at exactly the point a program acts on it.
static inline bool isAdHocKind(const std::string& tn) {
    if (tn == "X::AdHoc") return true;
    for (auto& a : typeAncestry(tn)) if (a == "X::AdHoc") return true;
    return false;
}

// Value::natWidthOfType, memoised per parameter: it substr()s the type name, so
// calling it per bind allocated a std::string for every typed parameter of every
// call. Answer encoded as bits<<1 | signed (0 = not a native-width type).
int paramNatSpec(const Param& p);
// isNativeTypeName(p.type), memoised per parameter (see Param::nativeName)
inline bool paramIsNative(const Param& p) {
    signed char n = p.nativeName;
    if (n < 0) { n = isNativeTypeName(p.type) ? 1 : 0; p.nativeName = n; }
    return n != 0;
}

bool typeNameConforms(const std::string& lnIn, const std::string& rn,
                             const std::string& lOfType, const std::string& rOfType);

// Does an argument satisfy a parameter type-constraint name?
// Package-relative short-name view for the type matchers (free/static functions):
// set by the Interpreter so `has Path $.path` accepts a URI::Path object when
// `Path` is an alias. Null in the compiled-runtime path (no aliasing there).
extern const std::unordered_map<std::string, std::string>* s_classAliases;
extern const std::unordered_map<std::string, std::shared_ptr<ClassInfo>>* s_classesForAlias;
void rtSetAliasView(const std::unordered_map<std::string, std::string>* a,
                    const std::unordered_map<std::string, std::shared_ptr<ClassInfo>>* c);
// resolve a type-constraint name through the alias table — only when no REAL
// class claims the short name (a later genuine `class Path` wins over the alias)
static inline const std::string& aliasType(const std::string& type) {
    if (!s_classAliases || (s_classesForAlias && s_classesForAlias->count(type))) return type;
    auto it = s_classAliases->find(type);
    return it != s_classAliases->end() ? it->second : type;
}


// A hash-BACKED built-in is not thereby Associative. DateTime and Date are
// stored as hashes here (year/month/day/... slots), so the structural answer
// below made `$dt ~~ Associative` True where Rakudo says False — DateTime and
// Date do Dateish and nothing else. BSON::Simple's encoder tests Associative
// BEFORE Dateish, so a DateTime was written out as a sub-document of its own
// accessors instead of the 8-byte BSON datetime, silently and byte-wrong.
static inline bool hashKindIsAssociative(const std::string& kind) {
    return kind != "DateTime" && kind != "Date";
}


// The parameters of a type as the engine spells them, split into components
// with their definedness smileys reattached. A type object keeps `Array[Int:D]`
// as "Int,D" - the smiley rides as a trailing pseudo-parameter - and a hash's
// value and key types as "Int,Str", so "Int,D,Str" reads back as {"Int:D", "Str"}.
std::vector<std::string> typeParamParts(const std::string& ofType);

// One parameter's bare name and its smiley: "Int:D" is "Int" and "D".
static inline void splitTypeSmiley(const std::string& p, std::string& bare, std::string& smiley) {
    size_t c = p.rfind(':');
    if (c != std::string::npos && c + 2 == p.size() &&
        (p[c + 1] == 'D' || p[c + 1] == 'U' || p[c + 1] == '_')) {
        bare = p.substr(0, c); smiley = p.substr(c + 1);
    } else { bare = p; smiley.clear(); }
}

bool typeNameConforms(const std::string& lnIn, const std::string& rn,
                             const std::string& lOfType, const std::string& rOfType);

// Is the parameterized role `have` (as written: `R2[Int]`) a `want` (`R2[Cool]`)?
// The same role, and each argument of `have` conforms to the matching one of
// `want` — role parameters are covariant (`R1[Cool] ~~ R1[Any]`).
bool roleArgsCovariant(const std::string& have, const std::string& want);

// Does a container's parameter list satisfy a parameterized ROLE's - is an
// `Array[Int]` a `Positional[Cool]`? Its element type conforms to the role's
// (Int is Cool), a definedness smiley on the role's parameter narrows it
// (`Positional[Int:D]` refuses `Array[Int]`, while `Array[Int:D]` is a
// `Positional[Int]`), and an unparameterized container is not even a
// `Positional[Mu]`. A two-parameter spelling matches nothing a container
// does: a Hash[V,K] does Associative[V], never Associative[V,K]. Measured
// against Rakudo, each of them.
static inline bool roleParamConforms(const std::vector<std::string>& have,
                              const std::vector<std::string>& want) {
    if (want.size() != 1 || have.empty()) return false;
    std::string hb, hs, wb, ws;
    splitTypeSmiley(have[0], hb, hs); splitTypeSmiley(want[0], wb, ws);
    if (ws == "D" && hs != "D") return false;
    if (wb == "Mu") return true;
    if (wb == "Any") return hb != "Mu";
    return typeNameConforms(hb, wb, std::string(), std::string());
}

// Does the TYPE NAME `ln` conform to `rn`? The built-in "does" table, the
// numeric/string tower, and the user class/role ancestry — one answer, shared by
// the `~~` operator and by parameter dispatch. They used to disagree: `~~` knew
// all of this while dispatch answered a blanket true for any type object, so
// `uri-escape(Str)` bound a `Match $s` parameter.
bool typeNameConforms(const std::string& lnIn, const std::string& rn,
                             const std::string& lOfType, const std::string& rOfType);

bool typeMatchesArg(const Value& arg, const std::string& type);

// `try { …; CATCH {…} }` runs its block under `use fatal` (Rakudo): a Failure
// the block ENDS with is thrown inside it, where its own CATCH sees it. The try
// sets this for the one call it makes; that call consumes it on entry, so the
// block's own inner calls run as usual.
RAKUPP_CONSTINIT extern thread_local bool t_fatalTry;

// Per-thread stack accounting for the recursion guard. `t_stack.top` is a byte
// address near the top of this thread's stack (set once, lazily, from the first
// guarded frame); `t_stack.limit` is that thread's usable stack size. The main
// interpreter runs on a 1 GiB stack (Runtime.cpp) and workers on 256 MiB
// (BigStackThread) — a headroom check fits both, where a fixed frame count
// cannot. We stop with X::Recursion while ~2 MiB of stack remains, so the throw
// unwinds cleanly instead of the process taking SIGSEGV/SIGBUS (the latter
// wedging kill-proof under Rosetta).
// Both in one thread_local, so a guarded frame pays one thread-local lookup,
// not two.
struct StackBounds { char* top = nullptr; size_t limit = 0; };
RAKUPP_CONSTINIT extern thread_local StackBounds t_stack;
void ensureStackBounds(char* here);   // record t_stack from this frame if nothing has yet


// Does this expression contain a literal `*` (Whatever) term — walking only
// through composable expression forms, never into variables or calls? This is
// the syntactic test Whatever-currying keys on: `(* * 2).floor` composes,
// `my $d = * * 2; $d.floor` does not.
// `»`.method over a container: apply to each TOP-LEVEL element, preserving
// structure — a Hash keeps its keys and maps its values, and nothing deep-flattens.
// Shared by the direct `@a».m` path and the closure a `*.attr».m` WhateverCode
// curries into; the latter used to fall back to a plain method call, which
// dispatched to the CONTAINER ("No such method 'lang' for invocant of type 'Array'").
// The methods Rakudo marks `is nodal`: the ones that ask about a CONTAINER
// rather than about a value. A hyper applies those to each node and stops
// there; every other method reaches the LEAVES, descending through nested
// collections. Derived by running each name of Any/List/Array's method table
// through `[[1,2],]».NAME` under Rakudo and comparing the answer with the
// node's own and with the per-leaf one (scratch probe nodal2/nodal3).
bool isNodalMethod(const std::string& m);
[[noreturn]] void undeclaredRoutine(const std::string& name);

// Is this sequence LAZY in Rakudo's sense — endless, or lazy by declaration —
// rather than merely not reified yet? A gather, and a map/grep/skip view of
// one, is the second kind: `.is-lazy` says False, and whatever binds or
// assigns it reads it whole.
bool seqIsLazy(const Value& v);

// Rakudo words the two refusals differently, and the wording is the whole
// diagnosis: "readonly variable" says there IS a container and it is closed,
// "immutable value" says there is none to write to. See Value::immutableBind.
static inline const char* notWritableMsg(const Value& v) {
    return v.immutableBind ? "Cannot assign to an immutable value"
                           : "Cannot assign to a readonly variable or a value";
}
// …and BOTH of those are an X::AdHoc on Rakudo, not an X::Assignment::RO
// (sheet HM-06). The typed class is reserved there for "Cannot modify an
// immutable T (gist)" — a container that exists and refuses — while a slot with
// no container behind it, or a readonly one, carries the message alone.
[[noreturn]] static inline void throwNotWritable(const Value& v) {
    throw RakuError{Value::typeObj("X::AdHoc"), notWritableMsg(v)};
}

// what a MISSING (or deleted) element reads as: `is default(v)` beats the type default
static Value arrayMissingDefault(const Value& base);

// `$*VM.name` — `cpp` (the honest answer: this engine is a C++ tree-walking
// interpreter), unless RAKUPP_VM_NAME says otherwise. The ecosystem's build
// recipes gate on `moar` and have no other branch — LibraryMake's get-vars ends
// `else { die "Unknown VM; don't know how to build" }`, uniprop's `given
// $*VM.name` has a `default { die }`, and NativeHelpers::Blob's CompileTestLib
// the same — so a user who wants those distributions can say so at the call
// site: `RAKUPP_VM_NAME=moar rakupp …`. It is the USER asserting a compatibility
// dialect for one run, never this engine claiming an identity, and nothing sets
// it on their behalf: the ecosystem sweep must not, or the measurement inherits
// the claim. `rakupp install` already does the same thing scoped to a BUILD HOOK
// (tools/install.raku's vm-toolchain-shim); this is that, scoped to a process.
static inline std::string vmName() {
    if (const char* n = std::getenv("RAKUPP_VM_NAME"))
        if (*n) return std::string(n);
    return "cpp";
}

// Assignable slot for a dynamic/special variable (native codegen): the existing
// binding if one is visible, else a fresh one in the global env.
// One FRAME's view of a dynamic: this env up through its enclosing block
// scopes, stopping after the first ROUTINE activation (inclusive). That is
// the frame's own `my $*x` declarations — never the scopes it merely closes
// over, which is what full find() would leak in. A chain with no routine
// mark (mainline, top-level blocks) walks through to global.
// `stop`: a scope the walk must not enter — the one a gather's block was
// written in, when the block runs as a coroutine. Its dynamic variables are
// found through whoever is PULLING (the dynamic chain), not through the scope
// that happened to write the gather: Rakudo resolves a `$*x` in the block
// through the reifier, and the block may be read long after that scope left.
static inline Value* dynInFrame(Env* e, const std::string& name, const Env* stop = nullptr) {
    for (; e && e != stop; e = e->parent.get()) {
        if (Value* p = e->local(name)) return p;
        if (e->routineFrame) return nullptr;
    }
    return nullptr;
}
Env* gatherDynBoundary(GatherCoro* g);
size_t gatherDynBase(GatherCoro* g);

// The compiled-code face of `no strict`: a name with no declaration anywhere
// resolves through the live environment and is CREATED when nothing has it yet
// — with its sigil's empty default, so `@a.push(1)` on a lax name pushes onto
// an Array exactly as the interpreter's own undeclared-write path leaves it.
// Which names get here is decided at compile time by findLaxVars, so this is
// never reached for a name the program declares.
static std::string ourPublishedName(const std::string& name, const std::string& pkgPrefix); // defined with the `our` machinery below

// Value-level indexing for native codegen (no AST). Read returns Nil when absent.
// The default value for a missing element of a (possibly typed) container:
// a typed container answers its element type object, else Nil.
Value typedElemDefault(const Value& base);
static Value arrayMissingDefault(const Value& base);
static inline Value arrayMissingDefault(const Value& base) {
    if (base.elemDefault()) return *base.elemDefault(); // `is default(v)`
    return typedElemDefault(base);
}
// What a freshly GROWN slot of a container starts as. `is default(v)` wins over
// the element type: `my Int @a is default(0); @a[1] = 5` leaves @a[0] at 0, not
// at the (Int) type object — and `.raku` prints what a read would give
// (sheet LA-21). An untyped container grows empty holes.
Value containerFill(const Value& base);

// …and back out of whatever is carrying it.
static inline std::shared_ptr<BtRecord> btOf(const Value& v) {
    if (!v.ext()) return nullptr;
    auto r = std::static_pointer_cast<BtRecord>(v.ext());
    return r && !r->frames.empty() ? r : nullptr;
}

// A path as the reader should see it: the program as it was invoked, a module
// under the working directory relative to it, anything else absolute. (The
// `.file` the API answers stays absolute — Log::Async and backtrace-new.raku
// match on it by suffix.)
std::string btDisplayPath(const std::string& file, const std::string& srcAbs,
                                 const std::string& srcAsGiven);
// native scalar type → byte width and signedness (for is-rw copy-back, cglobal,
// Pointer.deref). 0 ⇒ not a plain native scalar.
int ncScalarWidth(const std::string& t, bool& sign, bool& isFloat);

// A libffi call interface prepared once and reused for every call with the same
// signature. It owns `atypes` because the prepared cif points into it.
struct NcCif {
    std::vector<ffi::Type*> atypes;
    ffi::Type*              rtype    = nullptr;
    bool                    variadic = false;
    unsigned                nfixed   = 0;
    ffi::Cif                cif;
};

bool argIsNeverContainer(const Expr* e);

// The lvalue TARGET of an rw write-back: `f(++$pos)` in Rakudo hands the
// CONTAINER through the increment, so the callee's writes land in $pos. Peel a
// prefix/postfix ++/-- (the increment already ran when the arg was evaluated)
// and aim at the variable underneath.
static inline Expr* peelIncDec(Expr* e) {
    while (e && e->kind == NK::Unary) {
        auto* u = static_cast<Unary*>(e);
        if (u->op != "++" && u->op != "--") break;
        e = u->operand.get();
    }
    return e;
}

bool argIsNeverContainer(const Expr* e);

// The C3 linearization of a class's CLASS ancestors (composed roles are not
// ancestors): the class, then the merge of its parents' linearizations and the
// parent list itself. `ok` turns false when no consistent order exists
// (`class confused is vh is hv` over `hv is h is v` / `vh is v is h`).
std::vector<ClassInfo*> c3Linearize(ClassInfo* c, bool& ok);
static inline bool hasMultipleInheritance(ClassInfo* c) {
    for (; c; c = c->parent.get()) {
        for (auto& p : c->extraParents) if (p && !p->isRole) return true;
    }
    return false;
}

// What should a `for` actually walk? An object with its OWN `.iterator` method
// says so itself — that is how a container class changes what iterating it means
// (the docs' SkippingArray skips undefined elements; DNA yields codon triples).
// The iterator it hands back may be a user object driven by `pull-one`, or a
// built-in one, whose remaining items are taken directly.
// Does iterationSourceOf walk this object's BOXED container (a class built on
// an Array or a Hash, with no `iterator` of its own)? Such an object is still
// one item through a `$` variable, as a real Array would be.
static inline bool boxedIteration(const Value& v) {
    return v.t == VT::Object && v.obj() && v.obj()->cls && v.obj()->hasBoxed &&
           !v.obj()->cls->findMethod("iterator") && !v.obj()->cls->findMethod("pull-one");
}

// The slot-proxy family: Proxies whose FETCH/STORE are native closures over a
// storage location, so anything that can deproxy/proxyStore — every read and
// assignment path — reads and writes the location itself. `:=` binds variables
// with the Env one; take-rw hands all four out depending on what its argument
// names. Copying such a proxy copies the closure pair, so every copy still
// reaches the same slot — which is exactly what lets one escape a gather.
static inline void slotProxyPair(Value& proxy, std::function<Value(Interpreter&, ValueList&)> f,
                          std::function<Value(Interpreter&, ValueList&)> s) {
    Value fetch; fetch.t = VT::Code; fetch.setCode(makePayload<Callable>());
    fetch.code()->builtin = std::move(f);
    Value store; store.t = VT::Code; store.setCode(makePayload<Callable>());
    store.code()->builtin = std::move(s);
    (*proxy.hash())["FETCH"] = fetch;
    (*proxy.hash())["STORE"] = store;
}

// `$a := $b` binds $a to the CONTAINER $b holds, not to the slot named $b —
// which is the whole difference between the two operators. Assignment writes
// through a shared container, so `$b = 5` is visible as `$a`; REBINDING does
// not, because `$b := 5` gives the name $b a different container and leaves $a
// holding the one it was bound to.
//
// A slot-name alias cannot express that: it follows the NAME, so a rebound
// source dragged every alias along with it. `my $prev := $pulled` inside a loop
// that rebinds `$pulled` each turn therefore saw the CURRENT item as its
// "previous" one, which is Hash::int's push (and the PDF family behind it)
// reporting `expected int but got Str`.
//
// So the source's slot is PROMOTED on first alias: its value moves into a cell
// both names then proxy. A later `$b := …` overwrites $b's slot outright,
// dropping its proxy and leaving the cell — and $a — exactly as they were.
extern const char* kCellKey;
static inline Value makeSharedCellProxy(PRef<Value> cell) {
    Value proxy = Value::makeHash(); proxy.hashKind = "Proxy";
    // the opaque `ext` handle is a shared_ptr<void>: the cell rides in it boxed
    Value handle = Value::any(); handle.extM() = std::make_shared<PRef<Value>>(cell);
    (*proxy.hash())[kCellKey] = handle;
    slotProxyPair(proxy,
        [cell](Interpreter&, ValueList&) -> Value { return *cell; },
        [cell](Interpreter&, ValueList& sa) -> Value {
            *cell = sa.empty() ? Value::any() : sa[0];
            return *cell;
        });
    return proxy;
}

// The cell a slot ALREADY shares, or nothing when it holds an ordinary value.
// `my $a := $b; my $c := $b` must reach one container, not two, so the cell is
// parked on the proxy itself under a hidden key — in the opaque `ext` handle,
// which is what that field is for.
static inline PRef<Value> cellOfProxy(const Value* slot) {
    if (!slot || slot->t != VT::Hash || slot->hashKind != "Proxy" || !slot->hash()) return nullptr;
    auto c = slot->hash()->find(kCellKey);
    if (c == slot->hash()->end() || !c->second.ext()) return nullptr;
    return *std::static_pointer_cast<PRef<Value>>(c->second.ext());
}
bool forTailVarsExpr(const Expr* e, std::vector<const void*>& out);

// The name an `our` declaration PUBLISHES. A name written QUALIFIED is
// absolute: `our $Bar::v` inside `class Foo` declares $Bar::v, and
// `our $Foo::v` there declares $Foo::v — not $Foo::Bar::v or $Foo::Foo::v
// (Rakudo). Prefixing an already-qualified name again published the DOUBLED
// name and left the one every reader asks for empty, so a package could set a
// variable that nothing outside it — including its own test suite — could see.
// `$::x` names no package and still takes the prefix, so the `::` has to have
// a real package name in front of it.
static inline std::string ourPublishedName(const std::string& name, const std::string& pkgPrefix) {
    size_t q = name.find("::");
    if (q != std::string::npos && q > 1) return name;
    return name.substr(0, 1) + pkgPrefix + name.substr(1);
}

static bool endlessLow(const Value& v);   // defined with the range reducers below
static bool endlessHigh(const Value& v);

void spCallsE(const Expr* e, std::set<std::string>& out);

bool isSetOpStr(const std::string& o);
// Set ops that return a Bool (membership/subset/equality). Over a junction
// operand these COLLAPSE per the junction kind (like `==`/`eq`); the Set-valued
// producers ((|)/(&)/(-)/…) instead build a junction of Sets.
bool isSetPredicateStr(const std::string& o);

// how "heavy" a set-op operand is: 0 = Setty (or coercible), 1 = Baggy, 2 = Mixy
static inline int settyTier(const Value& v) {
    if (v.t == VT::Hash) {
        if (v.hashKind.find("Mix") == 0) return 2;
        if (v.hashKind.find("Bag") == 0) return 1;
    }
    return 0;
}
static inline bool lazySetOperand(const Value& v) {
    if (v.t == VT::Range && v.rTo() >= 9000000000000000000LL) return true;
    // …endless, or lazy by declaration and not yet read out (`lazy 1, 2`):
    // Rakudo refuses both, as it refuses `eqv` between two of them
    if (v.t == VT::Array && v.ext()) {
        auto* st = static_cast<LazySeqState*>(v.ext().get());
        return st->infinite || (st->declaredLazy && !st->exhausted);
    }
    return false;
}
// Coerce one operand to key => weight at the JOINT tier. A plain Hash coerces
// per tier: truthy-filtered membership at Set tier, numeric counts at Bag/Mix
// tier ({a => 42, b => 0} is set <a>, but bag (a => 42)). Mix tier keeps
// negative and fractional weights; Set/Bag drop non-positive ones.
std::map<std::string, double> setWeights(const Value& v, int tier);
static inline bool isMutableQuantHash(const Value& v) {
    return v.t == VT::Hash && (v.hashKind == "SetHash" || v.hashKind == "BagHash" ||
                               v.hashKind == "MixHash");
}
// wrap a weight map as the tier's type (Set / Bag / Mix, or their Hash twins)
Value setWrap(const std::map<std::string, double>& res, int tier, bool mut = false);
static inline int setOpMinTier(const std::string& op) {
    return (op == "(+)" || op == "\xE2\x8A\x8E" || op == "(.)" || op == "\xE2\x8A\x8D") ? 1 : 0;
}

Value setOp(const std::string& op, const Value& l, const Value& r);

// Multi-arg symmetric difference. Rakudo's (^)/⊖ is a genuine list operator, not
// a left fold: for each key the result weight is (largest − second-largest) over
// the operands' weights, where an operand lacking the key contributes 0. This
// reduces to |a−b| for two operands but diverges from a pairwise fold for three
// or more (e.g. Bag(a×42) ⊖ Bag(a×7) ⊖ Bag(a×43) is a×1, not a×8).
Value setSymDiffN(const ValueList& operands);

// A set operator is N-ARY, and the tier of its answer is the JOINT tier of
// EVERY operand — not of each adjacent pair a left fold happens to see. `(-)`
// over (Set, Set, Mix) is Mixy from the start, so the first step must keep the
// keys whose weight has gone to zero or below: `[(-)] <a b c>, <c d e>, <e f>.Mix`
// ends with d at -1 and e at -2, and a Setty first step had already dropped both.
// Lifting every operand to the joint tier before folding is exactly that rule.
// Only for a MIXED-tier fold of three or more operands; std::nullopt means the
// caller's own left fold is already right. Two operands see their joint tier
// anyway — the pairwise operator computes exactly this — and so does a fold whose
// operands all share a tier; lifting there would only lose what the pairwise
// rules carry (the mutable flavour, the lazy-operand refusal).
std::optional<Value> setOpFoldN(const std::string& op, const ValueList& items);

// The numeric operand of a bitwise / repeat / approx-equal operator. `"a" +& "b"`
// is X::Str::Numeric in Rakudo, not a silent 0 — but these operators reached for
// toInt()/toNum() directly, which answer 0 for any non-numeric string. Ordinary
// arithmetic already goes through numifyStrOrThrow; this is the same rule for the
// operators that kept their own copy of it.
//
// A Blob/Buf is excluded: its "string" is bytes, and it numifies to its element
// count elsewhere. An allomorph already carries a real number.
static inline Value strictNum(const Value& v) {
    if (v.t == VT::Str && !v.isAllomorph() && v.hashKind.empty())
        return numifyStrOrThrow(v.s);
    return v;
}
static inline long long strictInt(const Value& v) { return strictNum(v).toInt(); }

// Past this many elements a `xx` repeat is generated on demand rather than
// built: the list would be gigabytes, and the counts the spec asks about
// (`2**62`, `2**99999`) could never be built at all.
static constexpr long long kXxEagerMax = 10000000;

// Is `op` the reverse metaop over a WORD base — `Rcmp`, `Rdiv`, `Rmin`? The
// symbolic forms are recognised by the non-alphanumeric character after the R,
// but a word base is alphanumeric and would make every identifier starting with
// R (Range, Rat…) look like one, so the bases are listed rather than guessed.
bool reverseWordOp(const std::string& op);

bool valueSmartmatchHook(const std::string& op, const Value& l, const Value& r, Value& out); // defined below, beside applyBinOp

bool objMethodStrHook(const Value& v, const char* method, std::string& out);

// `f o g` / `f ∘ g`: a callable computing f(g(…)). When f takes several
// arguments, g's result list is SLIPPED into it (`(* + *) o { $_ + 7, $_ * 6 }`
// — Rakudo's `f |g |args`); the composition takes what g takes (a 2-ary g maps
// two at a time) and returns what f returns.
Value composeCode(const Value& fV, const Value& gV);

// `rx/$base$tail/` where $base and $tail hold REGEXES composes them. In Rakudo
// the regex closes over those variables; here a Regex value is its source text,
// so the composition is baked in when the `rx//` term is evaluated — with the
// variables that are in scope at that moment. Deferring it to match time
// re-reads the names against whatever they mean *then*, which is wrong as soon
// as the result is fed back into the same variable, as IO::Glob does when it
// folds a glob's terms into one matcher. Str-valued variables are left for the
// match-time path, where they still interpolate literally.
// The `$var` atoms of a regex SOURCE, resolved against the current scope: a
// Regex value splices as a sub-pattern, anything else interpolates as literal
// text. Repeated until stable, because a spliced regex may name more of them.
// Shared by `~~` and by every builtin that compiles a raw pattern (`.split`).
// A `:P5`/`:Perl5` adverb among the leading `:adv ` tokens the lexer bakes into
// a regex literal — the whole pattern is Perl 5 syntax.
bool isP5Pattern(const std::string& pat);

// What `:temp $x = …;` inside a pattern overwrote, to be put back when the
// outermost match that reached it is done (see RxTempGuard in regexMatch).
// A `:let` one is put back only when that match FAILED.
struct RxTempSave { std::shared_ptr<Env> env; std::string var; Value old; bool let = false; };
extern thread_local std::vector<RxTempSave> g_rxTemps;

// COMPILED-PATTERN CACHE. A regex literal in a loop reaches the match/subst
// paths once per iteration with the same final (post-interpolation) text, and
// compiling — parse, tree build, first-use bytesets — dominated the whole match
// for small patterns. The key is the text the Regex constructor actually sees,
// so an interpolated pattern caches per distinct expansion, and a redefined
// `my regex` body is a different key by construction. Per-thread, so no locking
// and no cross-thread sharing of the nodes' lazy caches; entries are shared_ptr
// so a nested match (a {…} block matching the same pattern) or a wholesale
// eviction can never free an object still matching. The object is CONST: since
// compile-time state is only (pattern, flags), per-call hooks must go through
// the search/matchAt hooks parameter, never the runHooks member.
std::shared_ptr<const Regex> compileRegexCached(const std::string& pat, const std::string& flags);

// tr/from/to/ is Str.trans(from => to) with its adverbs (:d/:c/:s) passed
// along — Rakudo builds it the same way, so the replacement cycles and
// :delete/:complement/:squash all come from the one implementation. The
// pattern arrives as "\x01" + ":adv " … + raw.
std::string trApply(Interpreter& I, const std::string& subj, const std::string& pat,
                           const std::string& repl);
// tr-tagged SubstLit: pattern begins with '\x01'. Returns true and fills `result` (count) + `newStr`.
static inline bool isTrSubst(const std::string& pat) { return !pat.empty() && pat[0] == '\x01'; }

// Instant/Duration algebra: Instant−Instant→Duration, Instant±Duration→Instant,
// Duration±x→Duration; everything else drops to plain numbers (like Rakudo's *).
void tagTemporal(const std::string& op, const Value& l, const Value& r, Value& res);

// Shared hyper-operator core for every spelling (`>>op<<`, `»op«`, and the
// bracketed `>>[&op]<<` form). A `>>` on the left / `<<` on the right marks
// that side STRICT — it dictates the shape; a dwimmy side is CYCLED to the
// strict length (truncating when longer), but a strict SCALAR facing a list
// dies, as does a dwimmy side that is known-infinite in a position where no
// finite side dictates the length. Hash keysets follow Rakudo: both strict →
// union, one strict → that side's keys, both dwimmy → intersection.
// the infix a hyper is applying, for X::HyperOp::NonDWIM's `operator`
RAKUPP_CONSTINIT extern thread_local const std::string* g_hyperOpName;

// Smartmatch on two VALUES — what the native backend emits for `~~`/`!~~` and
// for every `when`, so that a compiled match runs the interpreter's rules.
//
// A CALLABLE matcher is invoked with the topic. That arm lives in evalBinary,
// which emitted code never reaches, so compiled `$x ~~ $matcher` fell through to
// applyArith and compared a Code object with a number: `200 ~~ (* > 100)`
// answered False. Everything else goes to smartmatchValue, the value-level entry
// the junction and `where` paths already use — it knows Regex values, Str/object
// ACCEPTS, Pair patterns, junctions, and that a Whatever which ARRIVED as a
// value is a value rather than a curry.
//
// That last point is what issue #96 was: `when * < 1` evaluates to a
// WhateverCode, and handing one to applyArith curried it a SECOND time. The
// result was a Code object — truthy whatever the topic — so the first `when` arm
// always won, and a compiled `given @a[$i] - @a[$i-1] { when * < 1 … }` printed
// the opposite of what the interpreter printed.
// Callable.ACCEPTS calls a ZERO-arity routine with no arguments: `$x ~~ &always`
// where `sub always { True }` asks only the sub (S03-smartmatch/any-callable.t)
ValueList smartmatchCallArgs(const Value& r, const Value& topic);

Value bridgeReal(Interpreter& I, const Value& v);
// The value-level smartmatch hooks, out of line on purpose: applyArith's Int/Int
// fast path and the junction collapse above it are hot enough that carrying
// these ~50 lines in the same body cost 3% per eigenstate on the width
// benchmark — the checks never even run there (Int ~~ Int returns earlier),
// so it was pure code layout. Gated by a cheap type test at the call site.
bool valueSmartmatchHook(const std::string& op, const Value& l, const Value& r, Value& out);

// Real-role bridge: a user object that defines .Bridge (or .Numeric) numifies
// through it, so numeric operators work on `class F does Real` instances.
Value bridgeReal(Interpreter& I, const Value& v);

// The value a mixed-in role's attribute starts at when nothing sets it: its
// DECLARED TYPE object, exactly as a class's own attribute does. Bare Any left
// `$att.cos .= new` with nothing to call `new` on — PDF::COS::Tie mixes the role
// carrying `has COSAttr $.cos` into an Attribute and then builds it in place.
// The map key a role's PRIVATE attribute takes when the role is mixed into an
// Attribute/Parameter meta-object (a Hash). Prefixed with a control byte so it
// can never be reached as a method name — see the seeding loop in mixinValue.
static inline std::string privMixinKey(const std::string& name) { return "\x01" "p" + name; }

// Code has no numeric value: `+&f` and `&f + 1` find no Numeric candidate
[[noreturn]] static inline void throwCodeNumeric(const Value& v) {
    const Callable* c = v.code();
    std::string t = c && c->isWhateverCode ? "WhateverCode" : c && c->isMethod ? "Method"
                  : c && c->isBlock ? "Block" : "Sub";
    throw RakuError{Value::typeObj("X::Multi::NoMatch"),
                    "Cannot resolve caller Numeric(" + t + ":D: ); none of these signatures matches:\n"
                    "    (Mu:U \\v: *%_)"};
}
void spDeclaredInRaw(const std::vector<StmtPtr>& body, std::vector<const VarExpr*>& out);
// The routine names a phaser body CALLS (`my-uc 'Ab'`), and whether it EVALs:
// a static run cannot see a routine declared around it, nor into a string.
void spCallsE(const Expr* e, std::set<std::string>& out);
static inline void spCallsS(const Stmt* s, std::set<std::string>& out) {
    if (!s) return;
    if (s->kind == NK::ExprStmt) spCallsE(static_cast<const ExprStmt*>(s)->e.get(), out);
    else if (s->kind == NK::Block) for (auto& st : static_cast<const Block*>(s)->stmts) spCallsS(st.get(), out);
    else if (s->kind == NK::ReturnStmt) spCallsE(static_cast<const ReturnStmt*>(s)->value.get(), out);
    else out.insert("\x01other");   // a statement kind not looked into: be careful
}
void spCallsE(const Expr* e, std::set<std::string>& out);
void spDeclaredInRaw(const std::vector<StmtPtr>& body, std::vector<const VarExpr*>& out);

// Only a syntactic pair (k=>v / :k(v), i.e. a NK::Pair expression) whose key is a
// bare identifier is a NAMED argument; a Pair value from a variable/call/list — or
// with a non-identifier key (`3 => 4`), a quoted key, or parens around it — is
// positional. A CAPTURE makes exactly the same split, which is why the test lives
// here rather than inside evalArgs.
static inline bool syntacticNamedArg(const Expr* a, const Value& v) {
    return v.t == VT::Pair && syntacticNamedPair(a);
}

// An UNHANDLED Failure detonates the moment its value is actually used —
// `say +"a"` throws, while `(+"a").defined` stays quiet. (`.handled`, set by
// `try`/CATCH/`.so`, makes it inert.)
void failureDetonate(const Value& v);

static inline bool endlessLow(const Value& v)  { return v.rNum() ? std::isinf(v.n) : v.rFrom() <= -9000000000000000000LL; }
static inline bool endlessHigh(const Value& v) { return v.rNum() ? std::isinf(v.im()) : v.rTo() >= 9000000000000000000LL; }

// A subscript base whose evaluation has no side effects: a variable, a literal,
// a Range or `^N` of those, a list of them — evaluating it twice is harmless.
bool pureSubscriptBase(const Expr* b);

// CATCH registrations' serials and the contexts' ids, drawn from one counter
// for the whole process, so two contexts (threads, gather coroutines) never
// share one and a mark made in one context reads as fresh in another.
extern std::atomic<uint64_t> g_catchSerial;
inline uint64_t ctxIdOf(ExecContext& t) {
    if (!t.ctxId) t.ctxId = g_catchSerial.fetch_add(1, std::memory_order_relaxed) + 1;
    return t.ctxId;
}

// A block's (or routine's) CATCH, registered for the run of its body — or a
// `try`, which takes whatever reaches it (catchBlk null).
struct Interpreter::CatchReg {
    ExecContext& t;
    bool on;
    uint64_t serial = 0;
    CatchReg(ExecContext& tc, Block* catchBlk, const std::vector<StmtPtr>* stmts,
             const std::shared_ptr<Env>& env, bool isTry = false)
        : t(tc), on(catchBlk != nullptr || isTry) {
        if (!on) return;
        ExecContext::CatchFrame f;
        f.catchBlk = catchBlk; f.stmts = stmts; f.env = env;
        f.serial = serial = g_catchSerial.fetch_add(1, std::memory_order_relaxed) + 1;
        f.frameTop = t.frameTop; f.transp = t.transpFrames;
        f.routineFrame = t.curRoutineFrame; f.routineEnv = t.curRoutineEnv; f.loopFrame = t.curLoopFrame;
        t.catchFrames.push_back(std::move(f));
    }
    ~CatchReg() { if (on) t.catchFrames.pop_back(); }
};

// A plain Raku call of a routine the program wrote (`f(…)`, `$f(…)`, a method
// call): the frame it makes hands an error straight back to the caller, so a
// handler beyond it may run ahead of the unwinding (ExecContext::transpFrames).
// A builtin's routine is not counted — it may call back into Raku and keep
// what dies there (`dies-ok`).
struct TranspCall {
    ExecContext* t = nullptr;
    TranspCall(ExecContext& tc, const Value& f) {
        // with no handler registered there is none a count could matter to
        if (!tc.catchFrames.empty() && f.t == VT::Code && f.code() && !f.code()->builtin && !f.code()->isNative) {
            t = &tc; ++t->transpFrames;
        }
    }
    ~TranspCall() { if (t) --t->transpFrames; }
};

// While a handler runs: an error raised inside it is not its own handler's to
// take, nor any handler's between it and where the error it handles was
// raised (all of those have had that error already).
struct Interpreter::CatchFence {
    ExecContext& t;
    bool on;
    CatchFence(ExecContext& tc, uint64_t serial) : t(tc), on(false) {
        for (size_t i = t.catchFrames.size(); i-- > 0; )
            if (!t.catchFrames[i].fence && t.catchFrames[i].serial == serial) {
                ExecContext::CatchFrame f; f.fence = true; f.fenceTo = i;
                t.catchFrames.push_back(std::move(f));
                on = true;
                break;
            }
    }
    ~CatchFence() { if (on) t.catchFrames.pop_back(); }
};

} // namespace rakupp
