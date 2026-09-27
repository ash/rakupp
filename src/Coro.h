#pragma once
// Stackful coroutines: a function that runs on a stack of its own and can hand
// control back to whoever resumed it, from any depth, and later carry on from
// exactly there. `gather` is built on them (ROAST-TRACKS-PLAN track B): its
// block runs until a `take` has produced what the consumer asked for, then
// waits, suspended in the middle of whatever it was doing, for the next pull.
//
// A Coro is thread-affine once started. Its suspended frames hold whatever
// the compiler cached across a call — thread_local addresses among them — so
// resuming it on another OS thread would run those frames against the wrong
// thread's state. Coro::ownerThread() is what callers check.
//
// The entry function must not let an exception escape: there is no frame
// beneath it to catch one. Catch everything at the entry and carry it to the
// resumer (GatherCoro does).
#include <cstddef>
#include <thread>

// Is there a native context switch for this target? x86-64 and arm64 use a
// hand-written one (Coro.cpp) on every ELF and Mach-O system; Windows uses
// Fibers. Anything else — WebAssembly (Raku.js) among them — keeps the old
// re-running gather (Interpreter.cpp). `-DRAKUPP_HAVE_CORO=0` forces that form
// anywhere, which is how the fallback gets compiled and tested on a desktop.
#ifndef RAKUPP_HAVE_CORO
#  if defined(_WIN32)
#    define RAKUPP_HAVE_CORO 1
#  elif (defined(__x86_64__) || defined(__aarch64__)) && (defined(__GNUC__) || defined(__clang__)) && \
        !defined(__EMSCRIPTEN__)
#    define RAKUPP_HAVE_CORO 1
#  else
#    define RAKUPP_HAVE_CORO 0
#  endif
#endif

namespace rakupp {

struct CoroStack;   // a pooled stack, with the loop that runs coroutines on it

class Coro {
public:
    using Entry = void (*)(void* arg);
    // Allocates nothing: the stack is taken from the pool at the first resume.
    Coro(Entry entry, void* arg) : entry_(entry), arg_(arg) {}
    // A coroutine that never started or has finished gives its stack back to
    // the pool. One destroyed while SUSPENDED loses its stack outright: the
    // frames on it are never unwound, so whatever they own leaks. Callers
    // that can unwind first (GatherCoro) do.
    ~Coro();
    Coro(const Coro&) = delete;
    Coro& operator=(const Coro&) = delete;

    // Run until the coroutine calls yield() or its entry returns.
    void resume();
    // From inside the coroutine: back to the resume() that ran it.
    void yield();

    bool started() const { return started_; }
    bool finished() const { return finished_; }
    bool running() const { return running_; }
    std::thread::id ownerThread() const { return owner_; }
    // The coroutine's stack: the address it grows down from, and how much of
    // it may be used (the recursion guard reads both while it runs).
    char* stackTop() const;
    size_t stackUsable() const;

private:
    friend struct CoroStack;
    friend void coroRunLoop(CoroStack*);
    Entry entry_;
    void* arg_;
    CoroStack* stack_ = nullptr;
    void* callerCtx_ = nullptr;     // the resumer's saved context while this runs
    // AddressSanitizer's view of the two stacks (Coro.cpp; unused otherwise)
    void* asanCallerFake_ = nullptr;
    const void* asanCallerBottom_ = nullptr;
    size_t asanCallerSize_ = 0;
    bool started_ = false, finished_ = false, running_ = false;
    std::thread::id owner_;
};

// How many stacks this thread keeps for reuse, and how big one is. For the
// benchmarks and the tests; the interpreter never needs to ask.
size_t coroPooledStacks();
size_t coroStackReserve();

} // namespace rakupp
