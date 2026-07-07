#pragma once
#include "Ast.h"
#include "Value.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace rakupp {

double randDouble(); // uniform random in [0,1)

struct Env {
    std::unordered_map<std::string, Value> vars;
    std::shared_ptr<Env> parent;

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
    std::vector<ValueList*> supplyStack;
    std::vector<Value*> makeTargets;
    std::string pkgPrefix;
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
    Value exec(Stmt* s);           // returns last value (for implicit return)
    Value execBlock(Block* b, std::shared_ptr<Env> scope);
    bool runLoopBody(Block* b, std::shared_ptr<Env> scope, const std::string& label = "",
                     bool isFirst = true, bool isLast = true,
                     ValueList* collect = nullptr); // handles redo/next/last + FIRST/LAST; false => last.
                                                    // collect!=null: append each iteration's value (value context)

    // calling
    Value callCallable(const Value& codeVal, ValueList args, const std::vector<ExprPtr>* rwArgs = nullptr);
    Value callBuiltin(const std::string& name, ValueList args); // invoke a named builtin (used by codegen)
    Value getArgs(); // @*ARGS as a List value (used by codegen)
    void syncEnvToProcess(); // push %*ENV into the real process environment, so children inherit it
    Value dynVar(const std::string& name); // $* / $? magical variables (used by codegen)
    Value idxW(const Value& base, Value key, bool isHash); // index with a Whatever/WhateverCode key (@a[*-1], @a[*])
    void materializeLazy(const Value& v, size_t n); // grow a lazy list's prefix to >= n elements (capped)
    Value methodCall(Value inv, const std::string& method, ValueList args, const std::vector<ExprPtr>* rwArgs = nullptr);
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
    void hoistSubs(const std::vector<StmtPtr>& stmts); // pre-register sub decls (whole-scope visibility)
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
    std::vector<std::thread> workers_;     // outstanding `start`/async worker threads
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

    // Per-thread execution registers — current scope, dyn-var chain, gather/supply/
    // make collectors, call depth, package prefix. Held in a `static thread_local`
    // so each real worker thread owns its own set: interpreter execution needs no
    // per-handoff register swap (the GIL still serialises WHO runs; saveCtx/loadCtx
    // now merely shuffle within a thread's own copy — a harmless round-trip). The
    // moved fields live in ExecContext. NB one Interpreter is live per thread, so a
    // static thread_local is safe. Access via the tctx_.<field> members below.
    static thread_local ExecContext tctx_;
    std::shared_ptr<Env> global_;
    int langRev_ = 1; // language revision: 0=6.c, 1=6.d (default, matches Rakudo), 2=6.e (via `use v6.e.PREVIEW`). Affects e.g. sqrt/roots of negatives -> Complex
    // Redispatch chain for callsame/callwith/nextsame/nextwith: each entry knows how to
    // invoke the NEXT candidate (e.g. a built-in shadowed by a user method) and the
    // current routine's args (for the *same variants).
    // next: the next-less-specific candidate (callsame/nextsame/callwith/nextwith).
    // restart: re-dispatch the SAME routine from scratch with new args (samewith).
    struct RedispatchCtx { std::function<Value(ValueList)> next; std::function<Value(ValueList)> restart; ValueList sameArgs; };
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
    bool docMode_ = false;            // --doc: run DOC phasers and print the rendered POD
    std::string srcFile_;             // source file path (for $?FILE)
    std::string execPath_;            // absolute path of the rakupp binary (for $*EXECUTABLE)
private:

    // test harness state
    long planned_ = -1;
    long testNum_ = 0;
    long failCount_ = 0;
    bool usedTest_ = false;
    int subtestDepth_ = 0;
    bool subtestFailed_ = false;
    int quietDepth_ = 0; // inside a `quietly {…}`, warn() output is suppressed
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
    Value evalAssign(Assign* a);
    Value evalValueOf(Expr* e); // like eval(), but a bare regex literal is a Regex object (value context)
    Value evalBinary(Binary* b);
    Value evalUnary(Unary* u);
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
std::string doSprintf(const std::string& fmt, const ValueList& args); // sprintf engine (also used by the Format type)
// indexing helpers used by native codegen (value-level, with autovivification on write)
Value  rtIndexGet(const Value& base, const Value& key, bool isHash);
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
