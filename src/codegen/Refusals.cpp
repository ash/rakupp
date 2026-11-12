// Constructs `--exe` hands to the interpreter, and the call sites a compiled
// `try` block puts under `use fatal` (Codegen::builtinCall, the MethodCall
// case of Codegen::ex, Codegen::ex's fatal wrapper).
//
// A refusal is ROUTINE-WIDE when the construct is tied to the routine it is
// written in: a module routine (compiled on its own) would otherwise hand just
// that statement to the interpreter, which runs it in a frame of its own.
#include "../Ast.h"
#include "../Codegen.h"
#include <cstring>
#include <functional>
#include <set>
#include <string>

namespace rakupp {

// A call to the built-in `name` that native code cannot do as the interpreter
// does; nullptr for any other. `inTopic`: a native topic is in force (a routine
// body or a loop), which an EVAL'd string would not see.
const char* builtinRefusal(const std::string& name, bool inTopic) {
    // `callframe` reads the interpreter's frames, and native code runs in none
    // of them (`callframe(0).line` was 0, the caller's frame the wrong one)
    if (name == "callframe") return "callframe";
    // `samewith` calls the routine it is WRITTEN in again, found on those frames
    if (name == "samewith") return "samewith";
    // a `react` records the frames it stands in, for the X::React::Died of an
    // error that leaves it: the routine around it has to have one
    if (name == "react") return "react";
    // an EVAL'd string sees the interpreter's lexicals: its `$_` was the
    // mainline's, not the routine's own native one
    if ((name == "EVAL" || name == "EVALFILE") && inTopic) return "an EVAL in a routine";
    return nullptr;
}

// …and a method call; `routineWide` says whether the refusal is the routine's
const char* methodRefusal(const MethodCall* m, bool& routineWide) {
    routineWide = true;
    // the frames an exception or Failure carries are the interpreter's, and
    // native code keeps none (main.cpp keeps such a program's routines interpreted)
    if (m->method == "backtrace") return ".backtrace";
    if (m->method == "mess") return ".mess";
    routineWide = false;
    // a map/grep a routine hands back is a lazy Seq its caller reads (the
    // parser's markLazyTail); the native call ran every block where it stood
    if (m->lazyTail) return "a map or grep returned lazily";
    // a wrapper must see every call, and a native call reaches the body directly
    if (m->method == "wrap") return ".wrap";
    if (m->method == "unwrap") return ".unwrap";
    return nullptr;
}

// Is `e` a call that a `try` block's `use fatal` reaches — one whose Failure
// throws where it is handed back? Not in a boolean or definedness context (the
// parser's fatalExempt), and not a control call, which hands nothing back.
bool fatalCallSite(const Expr* e) {
    if (e->kind == NK::MethodCall) return !static_cast<const MethodCall*>(e)->fatalExempt;
    if (e->kind != NK::Call) return false;
    auto* c = static_cast<const Call*>(e);
    static const std::set<std::string> ctl = {"return", "return-rw", "fail", "die", "take", "take-rw", "emit",
        "done", "leave", "succeed", "proceed", "next", "last", "redo", "goto", "callsame", "nextsame",
        "callwith", "nextwith", "exit"};
    return !c->fatalExempt && !ctl.count(c->name);
}

// A LEAVE phaser written as a statement
bool isLeaveBlock(const Stmt* s) {
    return s->kind == NK::Block && static_cast<const Block*>(s)->phaser == "LEAVE" &&
           !static_cast<const Block*>(s)->isCatch;
}

// Does a sub declared inside a routine read a variable of the routine around
// it? Its Code object is one slot by name, so a closure handed out reaches the
// latest call's instance — which matters only when instances differ, that is,
// when the sub closes over the enclosing routine's lexicals. One that reads
// only its own parameters and locals, the program's globals and specials is
// the same sub in every call.
bool subReadsOuterLexicals(const SubDecl* d, const std::set<std::string>& globals) {
    std::set<std::string> own;
    for (auto& p : d->params) own.insert(p.name);
    bool outer = false;
    std::function<void(Node*)> walk = [&](Node* n) {
        if (!n || outer) return;
        if (n->kind == NK::SubDecl && n != d) { outer = true; return; }   // (a sub in it: be unsure)
        if (n->kind == NK::VarExpr) {
            auto* v = static_cast<VarExpr*>(n);
            const std::string& nm = v->name;
            if (v->declare) own.insert(nm);
            else if (nm.size() > 1 && std::strchr("$@%&", nm[0]) && !std::strchr("*?!.^:=~", nm[1]) &&
                     nm != "$_" && nm != "$/" && nm != "$!" && !own.count(nm) && !globals.count(nm))
                outer = true;
        }
        forEachAstChild(n, walk);
    };
    for (auto& st : d->body) walk(st.get());
    return outer;
}

// …and is such a sub's instance one a caller could tell from another's? It
// escapes when it is named as a value (`&f`) or called from a closure in its
// scope (which may run after this call has returned), and the routine around
// it is re-entered when it calls itself; otherwise every call of it runs in
// the one activation whose lexicals it reads (showcase/ledger's `walk`).
bool nestedSubUnsafe(const std::vector<StmtPtr>& scope, const SubDecl* d, const std::set<std::string>& globals,
                     const SubDecl* outer) {
    if (!subReadsOuterLexicals(d, globals)) return false;
    if (!outer) return true;
    bool unsafe = false;
    std::function<void(Node*, bool)> walk = [&](Node* n, bool inClosure) {
        if (!n || unsafe) return;
        if (n->kind == NK::VarExpr && static_cast<VarExpr*>(n)->name == "&" + d->name) unsafe = true;
        if (n->kind == NK::Call && static_cast<Call*>(n)->name == d->name && inClosure) unsafe = true;
        if (n->kind == NK::Call && static_cast<Call*>(n)->name == outer->name) unsafe = true;
        if (n->kind == NK::MethodCall && outer->isMethod && static_cast<MethodCall*>(n)->method == outer->name) unsafe = true;
        const bool closure = inClosure || n->kind == NK::BlockExpr || (n->kind == NK::SubDecl && n != d);
        forEachAstChild(n, [&](Node* c) { walk(c, closure); });
    };
    for (auto& st : scope) walk(st.get(), false);
    return unsafe;
}

// A positional Pair whose value is a variable (`"length=i" => $length`) holds
// that variable's CONTAINER, and a routine handed one may bind it and write
// through it — Getopt::Long's get-options does (issue #255). A native
// program's variable is a C++ value the Pair only copies, so such a call to a
// module's routine is the interpreter's to make.
bool pairHoldsVariable(const std::vector<ExprPtr>& args) {
    for (auto& a : args) {
        if (!a || a->kind != NK::Pair || syntacticNamedPair(a.get())) continue;
        const Expr* v = static_cast<const PairExpr*>(a.get())->value.get();
        if (v && v->kind == NK::VarExpr && !static_cast<const VarExpr*>(v)->declare &&
            static_cast<const VarExpr*>(v)->name.rfind('$', 0) == 0)
            return true;
    }
    return false;
}

// Does the LEAVE at body[j] read a name a statement before it declares? One
// that does not can be armed at the routine's start — where an error in an
// earlier statement cannot leave it unarmed — rather than keep the routine
// interpreted (examples/life.raku's `print …; LEAVE print …`).
bool leaveReadsEarlier(const std::vector<StmtPtr>& body, size_t j) {
    std::set<std::string> declared, read;
    std::function<void(Node*, std::set<std::string>&, bool)> walk = [&](Node* n, std::set<std::string>& out, bool decl) {
        if (!n) return;
        if (n->kind == NK::VarExpr && static_cast<VarExpr*>(n)->declare == decl) out.insert(static_cast<VarExpr*>(n)->name);
        // (a declaration the walk cannot see — a sub, a class, a VarDecl — counts as reading anything)
        if (decl && (n->kind == NK::SubDecl || n->kind == NK::ClassDecl || n->kind == NK::VarDecl ||
                     n->kind == NK::EnumDecl || n->kind == NK::SubsetDecl || n->kind == NK::NamedRegexDecl))
            out.insert("\x01");
        forEachAstChild(n, [&](Node* c) { walk(c, out, decl); });
    };
    for (size_t k = 0; k < j; k++) walk(body[k].get(), declared, true);
    if (declared.count("\x01")) return true;
    walk(body[j].get(), read, false);
    for (auto& n : read) if (declared.count(n)) return true;
    return false;
}

}  // namespace rakupp
