// Constructs `--exe` hands to the interpreter, and the call sites a compiled
// `try` block puts under `use fatal` (Codegen::builtinCall, the MethodCall
// case of Codegen::ex, Codegen::ex's fatal wrapper).
//
// A refusal is ROUTINE-WIDE when the construct is tied to the routine it is
// written in: a module routine (compiled on its own) would otherwise hand just
// that statement to the interpreter, which runs it in a frame of its own.
#include "../Ast.h"
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

}  // namespace rakupp
