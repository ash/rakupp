// CallLanes.h — what the call paths share: the frame pool, the lean lanes'
// parameter types, a body's entry work
//
// callCallableRaw, callPlainSub and invokeMethod (InterpreterCore.cpp) and
// callPlainMethod (PlainMethod.cpp) take their frames, and decide what a lean
// lane may bind, by these.
#pragma once

#include "InterpreterParts.h"

namespace rakupp {

// A per-thread pool of call frames — see the note at its first user; the frames
// themselves are the thread's ExecContext::framePool. It lives at
// file scope because BOTH call paths need it: subs come through callCallableRaw,
// methods through invokeMethod, and only the first one had it. That asymmetry was
// the whole difference between a sub call at ~2x Rakudo and a method call at
// 5.8x — every method allocated a control block, an Env and its hash buckets, and
// destroyed them again, per call (DISPATCH-PERF-PLAN.md). An `if`/`else` branch
// takes its scope from the same pool, which made one Env per branch entered.
struct FramePool {
    std::vector<std::shared_ptr<Env>>& free;
    std::shared_ptr<Env> acquire() {
        if (free.empty()) return makeSlabShared<Env>();
        auto e = std::move(free.back());
        free.pop_back();
        return e;
    }
    void release(std::shared_ptr<Env>&& e) {
        if (!e || e.use_count() != 1 || free.size() >= 32) {
            if (e && e.use_count() != 1) e->cycleNoteKeptSlots();   // a closure kept the frame
            e.reset();
            cycleMaybeCollect();
            return;
        }
        e->cycleNoteSlots();
        e->vars.clear();          // keeps the bucket array — the next call's
        e->selfSlot = nullptr;    // …and the recorded `self` dies with them
        e->vars.resetWalked();    // a fresh scope no `$*x` walk has passed
        e->parent.reset();        // "$_" insert does not rehash
        // every flag back to a fresh Env's: a pooled frame is also a block's
        // scope (an `if` branch), and a `no strict` or a `use fatal` inside
        // one must not outlive it
        e->routineFrame = e->staticSeeded = e->unitFrame = e->packageFrame = false;
        e->evalFrame = e->stateFrame = e->loopFrame = false;
        e->btBlock = e->callEnv = e->btCalled = e->btThunk = false;
        e->btLine = 0;
        e->routineFrameId = 0;
        e->strictPragma = e->fatalPragma = 0;
        e->declStmts = nullptr;
        e->ex.reset();
        e->layout = nullptr;        // pads: next call re-attaches its own layout;
        e->pad.clear();           // clear() keeps the vector's capacity, the
        e->padLive.store(0, std::memory_order_relaxed); // same trick the bucket array plays above
        e->padEscaped.store(0, std::memory_order_relaxed); // use_count 1: nothing kept an alias
        free.push_back(std::move(e));
        cycleMaybeCollect();
    }
};
// Hands out a pooled frame that returns itself on scope exit. A frame anything
// captured — a closure, an rwLink, a stateEnv chain — fails the use_count test in
// release() and is simply dropped instead of reused.
struct PooledFrame {
    FramePool pool;
    std::shared_ptr<Env> env;
    explicit PooledFrame(ExecContext& t) : pool{t.framePool}, env(pool.acquire()) {}
    ~PooledFrame() { pool.release(std::move(env)); }
};

// The core types a plain sub's parameters and return may name and still take
// the lean path (Param::leanType, CodeInfo::leanRet). A parameter of one binds
// an argument already of that type as the general binder does — typeCheckBind
// passes it, bindNativeParam makes a native of it — and any other argument
// declines the lean path for the call, so the general binder answers it. A
// return of a value of the type, native or boxed, is what the check passes, and
// anything else goes to checkRetType (`--> int` refuses a Str, as Rakudo's unbox does).
enum LeanType : signed char { LeanNone = 0, LeanNum = 1, LeanInt = 2, LeanBoxNum = 3, LeanBoxInt = 4,
                              LeanStr = 5 };
inline signed char leanTypeOf(const std::string& t, bool ret) {
    if (t.empty()) return LeanNone;
    if (t == "num" || t == "num64") return LeanNum;
    if (t == "int" || t == "int64") return LeanInt;
    if (ret && t == "str") return LeanStr;
    if (t == "Num") return LeanBoxNum;
    if (t == "Int") return LeanBoxInt;
    if (t == "Str") return LeanStr;
    return -1;
}
// …and the call-time half: an argument the lean binder takes for `k`
inline bool leanArgFits(const Value& v, signed char k) {
    if (k == LeanNone) return true;
    if (!v.hashKind.empty() || !v.enumName().empty()) return false;
    switch (k) {
        case LeanNum: case LeanBoxNum: return v.t == VT::Num;
        case LeanInt:                  return v.t == VT::Int && !v.big();
        case LeanBoxInt:               return v.t == VT::Int;
        case LeanStr:                  return v.t == VT::Str;
        default:                       return true;
    }
}
// run `let` restorations for the current env — only on UNSUCCESSFUL exits
inline void runLetRestoresOf(const std::shared_ptr<Env>& e) {
    if (!e || !e->ex || e->ex->letRestores.empty()) return;
    for (auto it = e->ex->letRestores.rbegin(); it != e->ex->letRestores.rend(); ++it) (*it)();
    e->ex->letRestores.clear();
}

// What a routine's body needs done on entry and exit (stmtsEntryWork's bits),
// decided once on its Callable (InterpreterCore.cpp)
int bodyEntryWork(Callable& c);
// Whether a sub takes callCallableRaw's lean lane (PlainMethod.cpp)
signed char plainSubShape(Callable& c);
// Whether a method takes invokeMethod's lean lane, and under which test of the call (PlainMethod.cpp)
signed char plainMethodShape(Callable& c);

}  // namespace rakupp
