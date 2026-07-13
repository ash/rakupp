#pragma once
#include "Ast.h"
#include "Value.h"
#include "IntOps.h"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#if !defined(_WIN32)
#include <pthread.h>
#endif
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace rakupp {

double randDouble(); // uniform random in [0,1)
bool isKnownTypeName(const std::string& n); // core type-name set (Int, Str, …)
void srandSeed(long long s); // reseed the RNG (srand)

#if defined(_WIN32)
// Big-stack worker threads on Windows (_beginthreadex with a reserved stack).
// Defined in Runtime.cpp so <windows.h> stays out of this widely-included header.
std::uintptr_t bigStackCreate(void (*entry)(void*), void* arg, std::size_t stackBytes); // 0 on failure
void           bigStackJoin(std::uintptr_t h);
void           bigStackClose(std::uintptr_t h);
#endif

struct Env {
    std::unordered_map<std::string, Value> vars;
    std::shared_ptr<Env> parent;
    bool routineFrame = false; // a ROUTINE activation ($/ scopes here, like Rakudo's per-routine $/)
    std::vector<std::function<void()>> tempRestores; // `temp $x` value restorations, run when this scope leaves
    // container reset values: `is default(v)` stores v; a typed `my Int $x`
    // stores (Int). `$x = Nil` and .VAR.default read it. Empty for most scopes.
    std::map<std::string, Value> varDefault;

    Value* find(const std::string& name) {
        auto it = vars.find(name);
        if (it != vars.end()) return &it->second;
        if (parent) return parent->find(name);
        return nullptr;
    }
    void define(const std::string& name, Value v) { vars[name] = std::move(v); }
};

// control-flow signals
struct ReturnEx { Value v; };
struct ExitEx { int code = 0; };
struct LastEx { std::string label; };
struct NextEx { std::string label; };
struct RedoEx { std::string label; };
struct BreakGivenEx { Value v; bool hasVal = false; }; // `when`/`succeed` exits the enclosing given/loop, carrying its value
struct ResumeEx {}; // `.resume` inside a CATCH — resume execution after the throw point
struct StopGatherEx {}; // a lazy gather has produced enough — unwind the (possibly infinite) block
struct ProceedEx {};    // `proceed` leaves a `when` block but keeps matching later ones
struct RakuError { Value payload; std::string message; };
// Thrown at an interpreter safe point to unwind a background worker thread whose
// result is no longer wanted (the mainline has finished). NOT a Raku-visible
// exception — user CATCH handles RakuError, never this.
struct WorkerAbortEx {};
extern thread_local bool t_isWorker;     // true only on `start`/async worker threads
extern thread_local unsigned t_safePtCtr; // loop iterations since this worker last yielded the GIL

// Per-thread execution "registers": the state that belongs to a single thread
// of Raku execution — its current lexical scope, the dynamic-variable ($*foo)
// caller chain, recursion depth, and the gather/supply/make collectors that are
// active on its call stack. The interpreter keeps ONE live copy of these as
// members. When a real thread parks (e.g. inside `await`) or another thread is
// scheduled onto the interpreter, saveCtx/loadCtx swap the live registers with
// the parked thread's stash. Because only one thread runs interpreter code at a
// time (guarded by the GIL), the live members always reflect the running thread.
// This is the Stage-1 foundation for real concurrency; nothing swaps yet.
struct ExecContext {
    std::shared_ptr<Env> cur;
    std::vector<Env*> dynStack;
    int callDepth = 0;
    Env* curStateEnv = nullptr;
    std::vector<std::shared_ptr<ValueList>> gatherStack;
    std::vector<size_t> gatherLimits; // per-gather take cap (0 = unlimited); a take past it throws StopGatherEx
    std::vector<ValueList*> supplyStack;
    std::vector<Value*> makeTargets;
    std::string pkgPrefix;
    // Cooperative `return`: when a return executes with NO callable boundary
    // between it and its enclosing routine (frameTop == curRoutineFrame), it
    // sets `returning` instead of throwing ReturnEx — native statement/loop
    // executors break out, and callCallableRaw consumes the flag at the
    // routine boundary. Anything crossing a closure/builtin callback still
    // throws (exact old semantics), so intermediate C++ loops stay correct.
    bool returning = false;
    Value returnV;
    uint64_t frameTop = 0;        // incremented per callCallableRaw activation
    uint64_t curRoutineFrame = 0; // frameTop at the nearest enclosing ROUTINE entry
    // Cooperative unlabelled next/last/redo: set when the statement executes in
    // the SAME callable frame as the innermost native loop (no closure between);
    // labelled or cross-frame control still throws NextEx/LastEx/RedoEx.
    int loopCtl = 0;              // 0 none, 1 next, 2 last, 3 redo
    uint64_t curLoopFrame = 0;    // frameTop when the innermost native loop body runs
};

// Backs a lazy list (an infinite `… … *` sequence, or `.map` over one). The Value
// holds the materialised prefix in its `arr`; `appendNext` computes one more element
// on demand (returns false when exhausted). Reached via Value::ext.
struct LazySeqState {
    std::function<bool(ValueList&)> appendNext;
    bool infinite = false; // a truly unbounded source (…..Inf): elems/pop/tail/[*-1] must die
};

// Shared state behind a real (thread-backed) Promise. Copies of the Promise
// Value all reference the same PromiseState via Value::ext, so a worker thread
// keeping/breaking it is observed by every awaiter. `done`/`result`/`cause` are
// guarded by `m`; `cv` wakes threads blocked in awaitPromise.
struct PromiseState {
    std::mutex m;
    std::condition_variable cv;
    bool done = false;
    bool broken = false;
    Value result;        // the kept value
    Value cause;         // exception payload when broken
    std::string causeMsg; // exception message when broken
    std::vector<std::function<void()>> thens; // `.then` continuations, fired once on settle
};

// One active `react` block. `whenever` on a live Supplier registers a tap that
// enqueues events here; the react loop drains `queue` until every live source is
// done (or `done`/`last` closes it), releasing the GIL to wait for events pushed
// by other threads. from-list Supplies stay eager and never touch this.
struct ReactEvent { Value handler; Value value; bool isDone = false; };
struct ReactCtx {
    std::deque<ReactEvent> queue;
    int liveSources = 0;   // live taps not yet done
    bool closed = false;   // `done`/`last` called
    std::mutex m;
    std::condition_variable cv;
};

class Interpreter {
public:
    Interpreter();
    int run(Program& prog);
    void setArgs(std::vector<std::string> a) { argv_ = std::move(a); }

    // evaluation
    Value eval(Expr* e);
    // `sink`: the statement's value is discarded (loop bodies etc.), so an
    // assignment need not materialize its (possibly large) result — skips the copy.
    Value exec(Stmt* s, bool sink = false); // returns last value (for implicit return)
    Value execBlock(Block* b, std::shared_ptr<Env> scope, bool sink = false);
    bool runLoopBody(Block* b, std::shared_ptr<Env> scope, const std::string& label = "",
                     bool isFirst = true, bool isLast = true,
                     ValueList* collect = nullptr); // handles redo/next/last + FIRST/LAST; false => last.
                                                    // collect!=null: append each iteration's value (value context)

    // calling
    Value callCallable(const Value& codeVal, ValueList args, const std::vector<ExprPtr>* rwArgs = nullptr);
    Value callCallableRaw(const Value& codeVal, ValueList args, const std::vector<ExprPtr>* rwArgs); // no wrap layer
    Value callNative(Callable& c, ValueList& args); // `is native` C FFI
    // Live-Supply transform chain: run one emitted value through a tap's chain of
    // grep/map/head/… steps. Returns the values to forward; sets `complete` when the
    // chain has finished (head/first reached its limit) so `done` should fire.
    ValueList applyTapChain(Value& tap, const Value& in, bool& complete);
    Value callBuiltin(const std::string& name, ValueList args); // invoke a named builtin (used by codegen)
    Value seqOp(Value l, Value r, bool exclusive); // the `...` sequence operator (also used by codegen)
    Value rtGather(Value blockClosure); // gather with probe-and-double laziness (native codegen)
    // Emit text for say/print/put/note: route through a user-overridden $*OUT/$*ERR
    // (call its .print), else write to the real stream.
    Value ioEmit(const std::string& s, const char* dynVar, bool toErr);
    Value getArgs(); // @*ARGS as a List value (used by codegen)
    void syncEnvToProcess(); // push %*ENV into the real process environment, so children inherit it
    Value dynVar(const std::string& name); // $* / $? magical variables (used by codegen)
    Value& dynVarRef(const std::string& name); // assignable dynamic-var slot (used by codegen)
    Value& accessorRef(Value& base, const std::string& name); // $obj.accessor lvalue (used by codegen)
    Value postfixIPub(Value v) { return postfixI(std::move(v)); } // postfix:<i> (used by codegen)
    void rtUse(const std::string& module, const std::string& arg = ""); // `use MODULE` (used by codegen)
    Value rtNameTerm(const std::string& n); // bareword: env value / &call / builtin / type object (used by codegen)
    void registerNamedRegex(const std::string& name, const std::string& pattern, const std::string& kind) {
        namedRegex_[name] = pattern; namedRegexKind_[name] = kind; // (used by codegen)
    }
    Value idxW(const Value& base, Value key, bool isHash); // index with a Whatever/WhateverCode key (@a[*-1], @a[*])
    void materializeLazy(const Value& v, size_t n); // grow a lazy list's prefix to >= n elements (capped)
    Value methodCall(Value inv, const std::string& method, ValueList args, const std::vector<ExprPtr>* rwArgs = nullptr);
    Value exceptionFor(const RakuError& e); // $!/$_ value for a caught error: always a DEFINED exception instance
    std::string gistOf(const Value& v); // .gist, honouring a user-defined `method gist` (for say/note)
    std::string strOf(const Value& v);  // .Str,  honouring user `method Str`/`gist` (for print/put/interpolation)
    Value invokeMethod(const Value& codeVal, const Value& self, ValueList args, const std::vector<ExprPtr>* rwArgs = nullptr);
    // Invoke method `name` found from `startCls`, pushing a redispatch context so
    // callsame/nextsame reach the same method on the owning class's parent (recursively).
    Value invokeMethodChain(const std::string& name, ClassInfo* startCls, const Value& self,
                            ValueList args, const std::vector<ExprPtr>* rwArgs = nullptr);
    void copyOutRw(const std::vector<Param>* params, std::shared_ptr<Env>& env, const std::vector<ExprPtr>* rwArgs, bool methodCtx);
    int scoreCandidate(const Value& cand, const ValueList& args); // -1 = no match, else specificity
    bool boolify(const Value& v); // boolean context: honours a custom .Bool method on objects
    void setMatchVar(Value v); // set $/ (updates an enclosing scope's $/ if present)
    bool hoistSubs(const std::vector<StmtPtr>& stmts); // pre-register sub decls (whole-scope visibility); returns true if any named sub was hoisted
    void applySubTraits(SubDecl* sd); // run user `is` traits of a hoisted sub at its textual position
    static bool exprHasWhateverLit(const Expr* e); // does the expression contain a literal `*`? (curry test)
    bool hoistingSubs_ = false;       // true while hoistSubs is registering (defers trait application)
    void breakSelfClosures(Env* env); // drop the closure back-edge of any non-escaped nested sub, so a frame with a self-closured sub can be freed
    void runProcPromise(Value& promise, double timeoutSec); // run a Proc::Async .start promise (with optional timeout)
    void runEnterPhasers(const std::vector<StmtPtr>& stmts); // ENTER/FIRST at block entry (source order)
    void runFirstPhasers(const std::vector<StmtPtr>& stmts); // FIRST once before a loop's first iteration
    void runLastPhasers(const std::vector<StmtPtr>& stmts);  // LAST once after a loop's last iteration
    void runLeavePhasers(const std::vector<StmtPtr>& stmts); // LEAVE/KEEP/UNDO at block exit (reverse order)
    void runNextPhasers(const std::vector<StmtPtr>& stmts, std::shared_ptr<Env>& scope); // NEXT at each loop iteration's end
    bool suppressLoopFirst_ = false; // set while running a loop body so execBlock skips FIRST
    Value evalString(const std::string& src); // EVAL
    void loadModule(const std::string& name);  // `use Foo::Bar` -> compile lib file into global scope
    std::vector<std::string> libPaths_{"lib", ".", "rakulib"}; // + env-derived paths, filled in the ctor
    std::set<std::string> loadedModules_;
    Value regexMatch(const std::string& subject, const std::string& pattern); // sets $/ $0..
    Value regexSubst(const std::string& subject, const std::string& pattern,
                     const std::string& repl, std::string& out, bool& changed);
    // .subst / s/// with occurrence-selection adverbs (:g/:x/:nth/:p/:c) and the
    // sameX adverbs (:samecase/:samespace/:samemark). Sets nsub = # replacements.
    std::string substSelect(const std::string& subj, const std::string& pat,
                            Value* replArg, ValueList& args, long& nsub, bool literal = false,
                            const std::string* tmplRepl = nullptr, Value* matchResult = nullptr);
    Value grammarParse(ClassInfo* g, const std::string& input, bool subparse, const std::string& startRule, Value actions);

    std::unordered_map<std::string, std::shared_ptr<ClassInfo>> classes_;
    // `augment class Int {…}` on a built-in type: extra methods keyed by type name.
    // methodCall consults this for native values whose type has been augmented.
    std::unordered_map<std::string, std::unordered_map<std::string, Value>> builtinExt_;
    long anonTypeCounter_ = 0; // names anonymous `role {…}` / `class {…}` literals

    // GIL-removal step 2: symbol-table freeze. The shared symbol tables (classes_,
    // global_ vars, namedRegex_, loadedModules_, ClassInfo::methods) are mutated
    // freely while the program is single-threaded, but once concurrency engages
    // (engageGil) they must be treated as immutable so worker threads can READ them
    // without a lock. `symbolsFrozen_` flips true at that point; noteSymbolMutation()
    // is the tripwire wired into every structural writer — under RAKUPP_FREEZE_TRACE
    // it reports any post-freeze mutation (and which thread did it), the empirical
    // signal for whether lock-free reads are safe. Behaviour is otherwise unchanged.
    std::atomic<bool> symbolsFrozen_{false};
    std::thread::id mainThread_;
    void noteSymbolMutation(const char* what);

    // Concurrency. saveCtx moves the live execution registers into `c`; loadCtx
    // moves them back out. The GIL serialises Raku execution across real threads:
    // only its holder may touch interpreter state; a thread drops it while blocked
    // (await) so another can run, handing over the live registers via save/loadCtx.
    void saveCtx(ExecContext& c);
    void loadCtx(ExecContext& c);
    std::mutex gil_;                       // global interpreter lock (held while running Raku)
    bool gilHeld_ = false;                 // is the GIL currently engaged (any thread spawned)?
    // GIL-removal step 3: opt-in true parallelism (RAKUPP_PARALLEL). When true, worker
    // threads run interpreter compute concurrently instead of serialising on the GIL —
    // safe now that registers/stacks are thread_local (steps 1/3a) and the symbol tables
    // freeze once concurrency engages (step 2). The few genuinely-shared interpreter
    // internals that a parallel worker can still touch (test counters + TAP output,
    // workers_/keptPrograms_ vectors) are guarded by sharedMut_. User data mutated
    // without a Lock is the user's race, as in Rakudo. Default false ⇒ the GIL path is
    // byte-for-byte unchanged.
    bool parallelMode_ = false;
    std::mutex sharedMut_;
    // Worker threads must carry a big stack: the tree-walker recurses deeply and the
    // macOS default for non-main pthreads is 512 KB — a recursive Raku sub inside
    // `start {…}` overflows it within ~100 frames (SIGBUS, then a kill-proof wedge at
    // exit). Same trick as rakuppRunBigStack, wrapped std::thread-compatibly.
    class BigStackThread {
#if defined(_WIN32)
        std::uintptr_t h_ = 0; // HANDLE from _beginthreadex (impl in Runtime.cpp; keeps <windows.h> out of this header)
#else
        pthread_t h_{};
#endif
        bool joinable_ = false;
        struct Fn { std::function<void()> f; };
        static void run_(void* p) { std::unique_ptr<Fn> g(static_cast<Fn*>(p)); g->f(); }
    public:
        BigStackThread() = default;
        template <typename F> explicit BigStackThread(F f) {
            auto* fn = new Fn{std::move(f)};
            const size_t kStack = (size_t)256 << 20; // 256 MiB (virtual; committed on use)
#if defined(_WIN32)
            h_ = bigStackCreate(&BigStackThread::run_, fn, kStack);
            if (h_) joinable_ = true;
            else { std::unique_ptr<Fn> g(fn); g->f(); } // creation failed: run inline
#else
            pthread_attr_t attr;
            pthread_attr_init(&attr);
            pthread_attr_setstacksize(&attr, kStack);
            auto entry = [](void* p) -> void* { BigStackThread::run_(p); return nullptr; };
            if (pthread_create(&h_, &attr, entry, fn) == 0) joinable_ = true;
            else { std::unique_ptr<Fn> g(fn); g->f(); } // creation failed: run inline
            pthread_attr_destroy(&attr);
#endif
        }
        BigStackThread(BigStackThread&& o) noexcept : h_(o.h_), joinable_(o.joinable_) { o.joinable_ = false; }
        BigStackThread& operator=(BigStackThread&& o) noexcept {
            if (this != &o) { if (joinable_) detach(); h_ = o.h_; joinable_ = o.joinable_; o.joinable_ = false; }
            return *this;
        }
        BigStackThread(const BigStackThread&) = delete;
        BigStackThread& operator=(const BigStackThread&) = delete;
        bool joinable() const { return joinable_; }
        void join() {
            if (!joinable_) return;
#if defined(_WIN32)
            bigStackJoin(h_); bigStackClose(h_);
#else
            pthread_join(h_, nullptr);
#endif
            joinable_ = false;
        }
        void detach() {
            if (!joinable_) return;
#if defined(_WIN32)
            bigStackClose(h_);
#else
            pthread_detach(h_);
#endif
            joinable_ = false;
        }
        ~BigStackThread() { if (joinable_) detach(); } // daemon semantics, like drainWorkers' abandon
    };
    // A worker slot pairs the thread with a completion flag so finished workers can
    // be joined (and their big stacks released) as new ones spawn, instead of
    // accumulating unjoined until drainWorkers.
    struct WorkerSlot {
        BigStackThread th;
        std::shared_ptr<std::atomic<bool>> done;
    };
    std::vector<WorkerSlot> workers_;      // outstanding `start`/async worker threads
    void reapFinishedWorkers() {           // caller must hold the GIL or sharedMut_
        workers_.erase(std::remove_if(workers_.begin(), workers_.end(), [](WorkerSlot& w) {
            if (w.done && w.done->load(std::memory_order_acquire)) { w.th.join(); return true; }
            return false;
        }), workers_.end());
    }
    std::atomic<int> liveWorkers_{0};      // workers that have not yet finished
    bool abandonedWorkers_ = false;        // a fire-and-forget worker outlived the mainline (daemon)
    std::atomic<bool> workerAbort_{false}; // set at teardown: workers unwind at their next safe point
    // Interpreter safe point: cheap check woven into the hot loop so a background
    // `start {…}` worker doing pure compute (no I/O to yield at) can still be
    // unwound when the program is shutting down. A no-op on the main thread and
    // whenever no abort is pending — just a thread-local bool + relaxed atomic,
    // inlined so hot loops pay ~nothing.
    inline void safePoint() {
        if (!t_isWorker) return; // main-thread loops never park or abort here
        if (workerAbort_.load(std::memory_order_relaxed)) throw WorkerAbortEx{};
        // Periodically hand the GIL back so a compute-bound worker can't starve the
        // main thread (which may be parked in yieldToWorker waiting for exactly this).
        if (++t_safePtCtr >= 4096) { t_safePtCtr = 0; workerYield(); }
    }
    void workerYield(); // out-of-line: brief GIL release so siblings/main make progress
    // Cooperative handoff. gilYieldNotify() releases the GIL AND wakes a thread
    // parked in yieldToWorker (which the spawner uses to let a fresh worker run up
    // to its first blocking point — sleep/await — so pure-compute blocks finish
    // eagerly while blocking ones run concurrently).
    std::condition_variable gilReleased_;
    std::mutex gilRelMutex_;
    long gilReleaseCount_ = 0;
    void gilYieldNotify();                 // gil_.unlock() + bump the release counter + notify
    void yieldToWorker();                  // drop the GIL until some worker makes progress, then reacquire
    void sleepYield(double secs);          // sleep with the GIL released so other threads run concurrently
    // Release the GIL around a blocking syscall (e.g. waiting on a child process),
    // so sibling worker threads run — and spawn their OWN children — concurrently.
    // gilPark() returns true if it actually released (only when async is engaged);
    // gilUnpark(true) reacquires and restores this thread's registers. The parked
    // window must touch no interpreter state (only thread-local buffers + syscalls).
    bool gilPark();
    void gilUnpark(bool wasParked);
    // Spawn a worker running `code` (no args); returns a Promise Value backed by
    // a PromiseState. awaitPromise blocks the caller until `ps` completes, dropping
    // the GIL so the worker can run. drainWorkers joins everything at program end.
    Value spawnPromise(Value code);
    void awaitPromise(const std::shared_ptr<struct PromiseState>& ps);
    void runReactLoop(const std::shared_ptr<ReactCtx>& ctx); // block until live sources done
    void engageGil();                      // lazily lock the GIL on first async use
    void drainWorkers();
    void registerWriteHandle(const std::shared_ptr<std::map<std::string, Value>>& h) { openWriteHandles_.push_back(h); }
    void flushOpenWriteHandles();  // flush any unclosed write handle at program exit
    std::vector<std::shared_ptr<std::map<std::string, Value>>> openWriteHandles_;

    // Per-thread execution registers — current scope, dyn-var chain, gather/supply/
    // make collectors, call depth, package prefix. Held in a `static thread_local`
    // so each real worker thread owns its own set: interpreter execution needs no
    // per-handoff register swap (the GIL still serialises WHO runs; saveCtx/loadCtx
    // now merely shuffle within a thread's own copy — a harmless round-trip). The
    // moved fields live in ExecContext. NB one Interpreter is live per thread, so a
    // static thread_local is safe. Access via the tctx_.<field> members below.
    static thread_local ExecContext tctx_;
    std::shared_ptr<Env> global_;
    std::shared_ptr<Env> curPkgEnv_; // package scope `our` installs into (global_, or a module's env during load)
    int langRev_ = 1; // language revision: 0=6.c, 1=6.d (default, matches Rakudo), 2=6.e (via `use v6.e.PREVIEW`). Affects e.g. sqrt/roots of negatives -> Complex
    // Redispatch chain for callsame/callwith/nextsame/nextwith: each entry knows how to
    // invoke the NEXT candidate (e.g. a built-in shadowed by a user method) and the
    // current routine's args (for the *same variants).
    // next: the next-less-specific candidate (callsame/nextsame/callwith/nextwith).
    // restart: re-dispatch the SAME routine from scratch with new args (samewith).
    struct RedispatchCtx { std::function<Value(ValueList)> next; std::function<Value(ValueList)> restart; ValueList sameArgs; bool lastcall = false; bool fromChain = false; };
    // These three are per-thread call-stack state (a worker builds its own redispatch
    // chain / react stack / thread-depth). Left as plain members in step 1 because they
    // weren't in the swapped ExecContext set; made `static thread_local` here (step 3a)
    // so true parallel execution can't corrupt them. Same access syntax (`X_`), so no
    // call-site changes. Cross-thread emit uses the mutex-guarded ReactCtx, not reactStack_.
    static thread_local std::vector<RedispatchCtx> redispatchStack_;
    std::map<std::string, std::string> namedRegex_, namedRegexKind_; // lexical `my regex NAME {…}` -> pattern/kind
    std::vector<std::string> argv_;
    static thread_local std::vector<std::shared_ptr<ReactCtx>> reactStack_; // active `react {}` event loops
    static thread_local int threadDepth_; // >0 while running inside a Thread.start/Promise worker block (is-initial-thread)
    // (cur_/dynStack_/curStateEnv_/gatherStack_/supplyStack_/makeTargets_/pkgPrefix_/
    //  callDepth_ moved into the thread_local tctx_ above.)
public:
    std::string finishData_;          // $=finish data block of the module being run
    std::string podData_;             // rendered =pod content (printed at end in --doc mode)
    std::vector<Value> podDom_;       // $=pod structured DOM (Pod::Block values)
    bool docMode_ = false;            // --doc: run DOC phasers and print the rendered POD
    std::string srcFile_;             // source file path (for $?FILE)
    std::string execPath_;            // absolute path of the rakupp binary (for $*EXECUTABLE)
    int quietDepth_ = 0;              // inside a `quietly {…}`, warn() is suppressed (codegen bumps it too)
private:

    // test harness state
    long planned_ = -1;
    long testNum_ = 0;
    long failCount_ = 0;
    bool usedTest_ = false;
    bool noStrict_ = false;  // `no strict` — undeclared variables auto-vivify
    int subtestDepth_ = 0;
    bool subtestFailed_ = false;
    bool bailedOut_ = false; // bail-out was called: suppress the trailing auto-plan
    int curLine_ = 0;        // source line of the statement currently executing (for test diagnostics)
    int todoRemaining_ = 0;  // number of upcoming tests marked TODO by a bare `todo` statement
    std::string todoReason_; // reason for the pending TODO block
    int dieOnFail_ = -1;     // cached RAKU_TEST_DIE_ON_FAIL flag (-1 = not yet read)
    int todoSubtestDepth_ = 0; // inside a TODO-marked subtest: failures neither die nor count
    std::vector<std::pair<Block*, std::shared_ptr<Env>>> deferredEnds_; // END blocks from EVAL, run at program end
    bool envFlag(const std::string& name); // truthiness of %*ENV<name>
    std::string envStr(const std::string& name); // %*ENV<name> as a string
    std::string exceptionToJson(const Value& ex); // RAKU_EXCEPTIONS_HANDLER=JSON serialization
    void maybeDieOnFail();   // if RAKU_TEST_DIE_ON_FAIL, stop the suite after a real failure

    void emitTest(bool ok, const std::string& desc, const std::string& directive = "",
                  const std::string& extraDiag = "");

private:
    std::unordered_map<std::string, BuiltinFn> builtins_;
    std::vector<std::shared_ptr<Program>> keptPrograms_; // keep EVAL'd ASTs alive
    void registerBuiltins();

    Value* lvalue(Expr* e);
    ValueList evalArgs(const std::vector<ExprPtr>& exprs); // spreads `|@a`
    Value evalAssign(Assign* a, bool sink = false);
    Value evalValueOf(Expr* e); // like eval(), but a bare regex literal is a Regex object (value context)
    Value evalBinary(Binary* b);
    // apply a binary operator by name, resolving a user `sub infix:<op>` when the
    // operator isn't built-in (so meta-operators work over custom operators).
    Value applyBinOp(const std::string& op, const Value& l, const Value& r);
    // `$x does R` (in-place) / `$x but R` (copy) — mix role(s) or an attribute Pair
    // into a value, producing an object that also does R.
    Value mixinValue(Value base, const Value& rhs, bool copy);
    Value evalUnary(Unary* u);
    Value postfixI(Value v); // postfix:<i> — multiply by the imaginary unit
    Value applyReduce(std::string op, ValueList& items); // [op] reduce semantics
public:
    // $*TOLERANCE (dynamic, then lexical), default 1e-15 — Complex→Real coercions
    static double toleranceDyn();
private:
    Value evalCall(Call* c);
    Value evalIndex(Index* idx);
    Value evalInterp(InterpStr* s);

    Value makeClosure(BlockExpr* be);
    void bindParams(const std::vector<Param>& params, ValueList& args, std::shared_ptr<Env>& env);
};

// helpers
Value listToArray(const ValueList& items);
ValueList argsToPositional(ValueList& args, std::map<std::string, Value>& named);
Value applyArith(const std::string& op, const Value& l, const Value& r); // binary op dispatch (also used by codegen)

// -O fast-path binary ops for native codegen: inline the small-int (non-bignum)
// case as native int64, else fall back to the general applyArith. Semantics are
// identical — this only skips the string dispatch + boxing on the hot Int path.
inline bool rtBothInt(const Value& l, const Value& r) { return l.t == VT::Int && r.t == VT::Int && !l.big && !r.big; }
inline Value rtAdd(const Value& l, const Value& r) { long long z; if (rtBothInt(l, r) && !rakupp::add_ovf(l.i, r.i, &z)) return Value::integer(z); return applyArith("+", l, r); }
inline Value rtSub(const Value& l, const Value& r) { long long z; if (rtBothInt(l, r) && !rakupp::sub_ovf(l.i, r.i, &z)) return Value::integer(z); return applyArith("-", l, r); }
inline Value rtMul(const Value& l, const Value& r) { long long z; if (rtBothInt(l, r) && !rakupp::mul_ovf(l.i, r.i, &z)) return Value::integer(z); return applyArith("*", l, r); }
inline Value rtLt(const Value& l, const Value& r) { if (rtBothInt(l, r)) return Value::boolean(l.i <  r.i); return applyArith("<",  l, r); }
inline Value rtLe(const Value& l, const Value& r) { if (rtBothInt(l, r)) return Value::boolean(l.i <= r.i); return applyArith("<=", l, r); }
inline Value rtGt(const Value& l, const Value& r) { if (rtBothInt(l, r)) return Value::boolean(l.i >  r.i); return applyArith(">",  l, r); }
inline Value rtGe(const Value& l, const Value& r) { if (rtBothInt(l, r)) return Value::boolean(l.i >= r.i); return applyArith(">=", l, r); }
inline Value rtEq(const Value& l, const Value& r) { if (rtBothInt(l, r)) return Value::boolean(l.i == r.i); return applyArith("==", l, r); }
inline Value rtNe(const Value& l, const Value& r) { if (rtBothInt(l, r)) return Value::boolean(l.i != r.i); return applyArith("!=", l, r); }
// Native-bool comparison for boolean context (if/while/ternary): skips building
// a Bool Value and re-reading it. Int fast path, else the general operator.
inline bool rtLtB(const Value& l, const Value& r) { if (rtBothInt(l, r)) return l.i <  r.i; return applyArith("<",  l, r).truthy(); }
inline bool rtLeB(const Value& l, const Value& r) { if (rtBothInt(l, r)) return l.i <= r.i; return applyArith("<=", l, r).truthy(); }
inline bool rtGtB(const Value& l, const Value& r) { if (rtBothInt(l, r)) return l.i >  r.i; return applyArith(">",  l, r).truthy(); }
inline bool rtGeB(const Value& l, const Value& r) { if (rtBothInt(l, r)) return l.i >= r.i; return applyArith(">=", l, r).truthy(); }
inline bool rtEqB(const Value& l, const Value& r) { if (rtBothInt(l, r)) return l.i == r.i; return applyArith("==", l, r).truthy(); }
inline bool rtNeB(const Value& l, const Value& r) { if (rtBothInt(l, r)) return l.i != r.i; return applyArith("!=", l, r).truthy(); }
// Floor integer division (`div`) — Raku rounds toward -∞, unlike C++ `/`.
inline Value rtDiv(const Value& l, const Value& r) {
    if (rtBothInt(l, r) && r.i != 0) { long long q = l.i / r.i; if ((l.i % r.i) && ((l.i < 0) != (r.i < 0))) q--; return Value::integer(q); }
    return applyArith("div", l, r);
}
inline Value rtConcat(const Value& l, const Value& r) { if (l.t == VT::Str && r.t == VT::Str) return Value::str(l.s + r.s); return applyArith("~", l, r); }
// In-place `~=` append: mutate the accumulator's buffer instead of building a new
// string each step, turning repeated `$s ~= …` from O(n²) copying into O(n).
inline void rtCatAssign(Value& l, const Value& r) {
    if (l.t == VT::Str && r.t == VT::Str) { l.s += r.s; return; }
    l = applyArith("~", l, r);
}
inline Value rtMod(const Value& l, const Value& r) { if (rtBothInt(l, r) && r.i != 0) { long long m = l.i % r.i; if (m != 0 && ((m < 0) != (r.i < 0))) m += r.i; return Value::integer(m); } return applyArith("%", l, r); }
inline Value rtDivides(const Value& l, const Value& r) { if (rtBothInt(l, r) && r.i != 0) return Value::boolean(l.i % r.i == 0); return applyArith("%%", l, r); }
// Fast integer power by squaring, with overflow → bignum fallback (matches applyArith).
inline Value rtPow(const Value& l, const Value& r) {
    if (rtBothInt(l, r) && r.i >= 0) {
        long long base = l.i, e = r.i, res = 1; bool ovf = false;
        while (e > 0) {
            if ((e & 1) && rakupp::mul_ovf(res, base, &res)) { ovf = true; break; }
            e >>= 1;
            if (e && rakupp::mul_ovf(base, base, &base)) { ovf = true; break; }
        }
        if (!ovf) return Value::integer(res);
    }
    return applyArith("**", l, r);
}
std::string doSprintf(const std::string& fmt, const ValueList& args, int langRev = 1); // sprintf engine (also used by the Format type)
// indexing helpers used by native codegen (value-level, with autovivification on write)
Value  rtIndexGet(const Value& base, const Value& key, bool isHash);
std::vector<std::string> computePlaceholders(const std::vector<StmtPtr>& body); // $^a/$^b names, sorted (also used by codegen)
Value  rtArrayVal(const Value& v);  // list-assignment semantics for `@a = expr` (splice Lists, keep itemized rows)
void   rtSpreadArg(ValueList& as, const Value& v, bool argPos); // |x spread into an arg/list being built
Value  rtHyperMethod(Interpreter& I, const Value& inv, const std::string& m, ValueList args); // >>.method
Value  rtSlipVal(const Value& v);   // |x as a list element (a List that splices, pre-spread deep)
Value  rtSlipShallow(const Value& v); // |x in value position (one-level splice marker)
Value  rtHashLit(const ValueList& items); // { k => v, … } hash constructor
Value  rtNamedPair(const std::string& k, Value v); // k => v as a NAMED call argument
size_t rtPosCount(const ValueList& a, size_t from = 0); // positional-arg count (named pairs excluded)
Value  rtThrowNext(const std::string& label = ""); Value rtThrowLast(const std::string& label = "");
Value  rtThrowRedo(const std::string& label = ""); // expression-position / labelled loop control
Value  rtIndexAdverb(Value& base, const Value& keyIn, bool isHash, const std::string& adverb); // :exists/:delete/…
Value  rtSliceFrom(const Value& base, long long from, bool exFrom); // @a[$i .. *] tail slice
Value  rtRangeVal(const Value& from, const Value& to, bool exFrom, bool exTo); // from..to (string ranges too)
ValueList rtMainArgs(const std::vector<std::string>& argv); // argv -> MAIN args (--opt named, rest positional)
Value& rtIndexRef(Value& base, const Value& key, bool isHash);
Value  rtReduce(const std::string& op, const Value& list);  // [+] / [*] / … reduction metaop
Value  rtAttrGet(const Value& self, const std::string& name);   // $!attr / $.attr read (codegen)
Value& rtAttrRef(Value& self, const std::string& name);         // $!attr write (codegen)
bool   rtTypeMatch(const Value& v, const std::string& type);    // nominal type check for multi-dispatch (codegen)
// argument-binding helpers used by native codegen for flexible signatures
Value  rtPos(const ValueList& a, size_t idx);        // idx-th positional (non-Pair) arg, or Any
bool   rtHasPos(const ValueList& a, size_t idx);     // is an idx-th positional present?
Value  rtNamed(const ValueList& a, const std::string& key);    // named arg's value, or Any
bool   rtHasNamed(const ValueList& a, const std::string& key); // is a named arg present?
Value  rtSlurpyPos(const ValueList& a, size_t from);           // positional args [from..] as an Array
Value  rtSlurpyNamed(const ValueList& a);                      // all named args as a Hash
Value  rtCoerceHash(const Value& v);                           // pair/kv list → Hash (`my %h = a=>1,…`)

// IO::Spec::{Unix,QNX,Win32,Cygwin} class-method dispatch — pure path-string
// algorithms. Returns true (and sets `out`) when (cls, m) is handled.
bool ioSpecMethod(Interpreter& I, const std::string& cls, const std::string& m, ValueList& args, Value& out);

// Proleptic-Gregorian day count <-> civil date (for Date arithmetic).
long long civilToDays(long long y, long long m, long long d);
void daysToCivil(long long z, long long& y, long long& m, long long& d);
Value makeDate(long long days); // build a Date hash (hashKind="Date") from a day count

} // namespace rakupp
