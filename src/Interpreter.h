#pragma once
#include "Ast.h"
#include "Value.h"
#include "IntOps.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <condition_variable>
#include <deque>
#include <functional>
#if !defined(_WIN32)
#include <pthread.h>
#endif
#include <memory>
#include <optional>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace rakupp {

// How a type-check failure renders the offending value (Rakudo: `.raku`, elided
// past 23 chars). Shared by the five message builders across Interpreter/Builtins.
std::string typeCheckRepr(const Value& v);

double randDouble(); // uniform random in [0,1)
// Bareword value constants (pi/e/i/tau/now/time/rand) — true when `n` is one,
// with the freshly computed value in `out`. Shared by the interpreter's
// NameTerm eval and the codegen's rtNameTerm so the two cannot drift.
bool nameTermConstant(const std::string& n, Value& out, bool sixE = false); // sixE: `nano` exists
bool isKnownTypeName(const std::string& n); // core type-name set (Int, Str, …)
// The NATIVE lowercase type names (int, num, str, int64, …). Deliberately
// separate from isKnownTypeName, which lists the boxed types.
bool isNativeTypeName(const std::string& n);
// Installation-repository prefixes, in resolution order.
const std::vector<std::string>& rakuRepoPrefixes();
int signalNumberOfName(const std::string& n); // Signal-enum name → OS number ("SIGINT"→2), -1 if unknown
void srandSeed(long long s); // reseed the RNG (srand)

#if defined(_WIN32)
// Big-stack worker threads on Windows (_beginthreadex with a reserved stack).
// Defined in Runtime.cpp so <windows.h> stays out of this widely-included header.
std::uintptr_t bigStackCreate(void (*entry)(void*), void* arg, std::size_t stackBytes); // 0 on failure
void           bigStackJoin(std::uintptr_t h);
void           bigStackClose(std::uintptr_t h);
#endif

// The canonical identity of a value (what `.WHICH` answers) and the LOOKUP key a
// quanthash uses for it — see Builtins.cpp. A plain Str keys on its own content;
// everything else on its identity, so `42`, `"42"` and `<42>` are three elements.
std::string whichOf(const Value& v);
std::string baggyKeyStr(const Value& v);
Value makeBaggy(const ValueList& items, const std::string& kind,
                bool pairsAsElements = false); // Set/Bag/Mix builder (Builtins.cpp)

// Numify a string with Raku-correct result type (Int/Rat/Num), BigInt-aware
// (defined in Interpreter.cpp). Non-numeric input yields an undefined value.
Value numifyStr(const std::string& in);
// numifyStr, but a string that isn't a number THROWS X::Str::Numeric the way
// Rakudo does in numeric context (`"a" + 1`), instead of silently reading as 0.
Value numifyStrOrThrow(const std::string& in);
// The quiet form: a non-numeric string becomes an unthrown Failure (Rakudo's `+"a"`).
Value numifyStrFailure(const std::string& in);
// `val()`: a fully-numeric string becomes the matching allomorph (IntStr/RatStr/
// NumStr/ComplexStr — the number AND its source spelling); anything else passes
// through unchanged. Shared by the `val` builtin, prompt(), and MAIN's argv.
Value valAllomorph(const Value& v);
// Build a shaped array (`my @a[2;3]` / `Array.new(:shape(2;3))`): a fixed row-major
// structure, optionally filled from a flat list, tagged with its dimensions.
Value makeShapedContainer(const std::vector<long long>& dims, const std::string& declType,
                          const ValueList* fill = nullptr);
// NFC-normalise a UTF-8 string (Raku's NFG storage); ASCII passes through. (Builtins.cpp)
std::string nfcNormalize(std::string in);

// What `$*RAKU.compiler.version` answers, in Rakudo's YEAR.MONTH scheme: the
// era of the Rakudo this tree is VERIFIED against (the conformance oracle and
// the battery's comparison engine). Bump when the oracle bumps and the gates
// have been re-run against it — never because the calendar advanced: the value
// is a verification claim, not a date. Ecosystem modules gate on it
// (`$*RAKU.compiler.version < v2023.12`), which is why it is not rakupp's own
// release number; that stays visible in .release/.id.
//
// One definition. It used to be spelled twice — kOracleEra in Builtins.cpp and
// a bare "2026.07" literal in Interpreter.cpp's raku-object builder — which is
// exactly the drift a future bump would have tripped over.
inline constexpr const char* kOracleEra = "2026.07";

// The one lock every runtime write to a stream or an open handle's buffer
// takes. Coarse on purpose: output is not a bottleneck, and one lock is far
// easier to reason about than a hierarchy — and than the two bugs it replaces,
// a data race on std::cout's state and a lost-update on a FileHandle's buffer.
// NEVER hold it across a call back into Raku (a user `print` method may print).
std::mutex& rtOutMutex();
// SHA-1 as UPPERCASE hex (Interpreter.cpp) — the CURI short-index / content-id scheme.
std::string sha1hex(const std::string& msg);

// The eight containers below are empty in the overwhelming majority of scopes —
// they exist for `is rw` write-through, `temp`/`let` restoration, `is default`
// and `is dynamic`. An Env is built for every routine call AND every block, so
// constructing and destroying eight containers per scope is pure overhead for
// the ordinary case. They live behind one lazily-allocated pointer instead.
// Split a search-path environment variable (RAKULIB) into directories. BOTH ','
// and ':' separate; a ':' right after a lone leading drive letter is part of the
// path (`C:\proj\lib`), not a separator. Shared so the PARSER's copy of the
// search path — which is what finds a `use`d module's operator declarations
// while the importing file is still being parsed — cannot drift from the
// interpreter's. It did: the interpreter learned ',' and the parser did not, so
// `RAKULIB=a,b` silently stopped importing operators.
std::vector<std::string> splitSearchPath(const std::string& spec);

// Modules EMBEDDED in a compiled binary. `--exe`/`--aot` resolve the `use` graph
// at build time and register each module's serialized AST here before the
// program runs; loadModule consults this before it looks at the disk, so the
// binary carries its dependencies and needs neither their sources nor a warm
// precomp cache to start. Registration is additive and happens once, at startup.
void rakuppRegisterModule(const std::string& name, const char* blob, size_t blobLen,
                          const std::string& finish);

// One module resolved and parsed ahead of time, ready to embed.
struct BundledModule { std::string name, blob, finish, src; };

// Resolve `prog`'s TRANSITIVE `use` graph against `searchPath` and return each
// module's serialized AST, dependencies first. Used by --exe/--aot to make a
// binary self-sufficient: it carries its modules and needs neither their sources
// nor a warm precomp cache on the machine that runs it.
//
// A module that cannot be found, parsed, or serialized is simply left out rather
// than failing the build — the binary falls back to loading it from disk, which
// is also what has to happen for anything only a running program can name
// (`require ::($x)`, a computed `use lib`). Pragmas are skipped.
//
// `exportsOut`, when given, collects the `is export` sub names of every module
// in the graph — including ones left out of the returned table. Codegen needs
// them: a compiled call is resolved by name at compile time, so it has to know
// which names a module is about to take over (see collectExportedSubNames).
// A module the graph could NOT embed, and why — the compile modes report
// these (MODULES-PLAN B1) and --standalone turns them into build errors (B2).
// `name` is "<dynamic require>" for a runtime-computed `require ::($n)`,
// whose target cannot be known at compile time.
struct ModuleSkip {
    std::string name;
    std::string reason;
};

std::vector<BundledModule> collectModuleGraph(const Program& prog,
                                              const std::vector<std::string>& searchPath,
                                              std::set<std::string>* exportsOut = nullptr,
                                              std::vector<ModuleSkip>* skipsOut = nullptr,
                                              std::set<std::string>* nativeLibsOut = nullptr);

// The `is export` sub names declared by `stmts` (recursing into braced
// module/package bodies). This is the scan that decides whether a module sub may
// shadow a built-in for its importer: an exported one wins, a plain one stays
// module-private. loadModule uses it to publish, codegen to emit the call.
void collectExportedSubNames(const std::vector<StmtPtr>& stmts, std::set<std::string>& out);

// The two independent caching switches, both OFF unless turned on. `modules`
// caches the parse of every module a program `use`s; `files` caches the main
// program's own parse. Persisted in the config file (precompConfigPath), which
// `precompSetSetting` rewrites a key at a time; RAKUPP_PRECOMP_MODULES /
// RAKUPP_PRECOMP_FILES override for one invocation, RAKUPP_NO_PRECOMP=1 forces
// both off. The *Source functions say where the answer came from, for reporting.
bool precompModulesOn();
bool precompFilesOn();
std::string precompModulesSource();
std::string precompFilesSource();
std::string precompConfigPath();
bool precompSetSetting(const std::string& key, bool on);

// Where the precompiled-AST cache lives (see loadModule), "" when disabled.
std::string precompCacheDir();

// The same cache, for the MAIN program — Python never caches __main__, but our
// entries live in a central content-validated directory rather than beside the
// source, so the objection does not apply and a big script gets the same saving
// its modules do. `searchPath` is folded into the key because it decides which
// file a `use` resolves to for operator scanning: the same source under a
// different -I can legitimately parse differently.
// Both are no-ops (false / nothing stored) when the cache is disabled or the
// source has no file behind it, as with -e.
bool precompLoadProgram(const std::string& srcPath, const std::string& src,
                        const std::vector<std::string>& searchPath,
                        Program& out, std::string& finishOut);
void precompStoreProgram(const std::string& srcPath, const std::string& src,
                         const std::vector<std::string>& searchPath,
                         const Program& prog, const std::string& finish,
                         const std::vector<std::pair<std::string, std::string>>& deps);
// Every cached entry: the SOURCE it was built from, its size on disk, and
// whether this rakupp can still use it (a build from another rakupp, or one
// whose source has since changed, is listed as stale). Sorted by source.
// `orphan` = the source file is gone, so this entry can never be reused OR
// rewritten; it is the only cache state that is pure garbage rather than a
// miss waiting to happen.
struct PrecompEntry { std::string source; unsigned long long bytes; bool usable; bool orphan; };
std::vector<PrecompEntry> precompCacheList();

// Delete every cached entry. Returns how many files went and how many bytes they
// held. Always safe: entries are derived data, rebuilt on the next run.
std::pair<size_t, unsigned long long> precompCacheClear();

struct EnvExtras {
    // rw-param write-through: paramName → (caller's argument expr, caller env).
    // An assignment to the param writes through the caller's lvalue IMMEDIATELY
    // (so the caller sees it mid-call); rwSynced records the last value pushed
    // through, letting the copy-out backstop skip unchanged/already-synced params
    // (a late copy-out would re-apply stale values after the callee's own edits).
    std::map<std::string, std::pair<Expr*, std::shared_ptr<Env>>> rwLinks;
    std::map<std::string, Value> rwSynced;
    // hyper element write-through: paramName → the caller's container slot
    // directly (no expr to re-evaluate); rwDead marks a raw/rw param bound to
    // an immutable (literal) — assigning it dies like Rakudo's X::Assignment::RO.
    std::map<std::string, Value*> rwDirect;
    std::set<std::string> rwDead;
    std::vector<std::function<void()>> tempRestores; // `temp $x` value restorations, run when this scope leaves
    std::vector<std::function<void()>> letRestores;  // `let $x` restorations, run ONLY on unsuccessful (exception) exit
    // container reset values: `is default(v)` stores v; a typed `my Int $x`
    // stores (Int). `$x = Nil` and .VAR.default read it. Empty for most scopes.
    std::map<std::string, Value> varDefault;
    std::set<std::string> varDynamic;   // names declared `is dynamic` in this scope
};

// The pad slot table for one pad OWNER — the main program's mainline, or a
// Callable body (PADS-PLAN.md). Built once per BODY (two Callables sharing a
// body — .assuming wrappers — must agree on slot numbers, because the slot
// annotations live on the shared AST nodes) and shared by every frame that
// body ever runs in. Slot i of a frame's pad holds the variable names[i];
// byName answers the slow-path lookups (EVAL, CALLER::, temp) that reach a
// pad variable through Env::find rather than through an annotated site.
struct PadLayout {
    std::vector<std::string> names;
    // TARG lever A (TARG-PLAN.md): 1 = this slot was declared UNTYPED and
    // unconstrained (or is a param — assignment to a param is not
    // type-enforced today), so the simple-assign lane may store into it
    // without the typed-container ceremony. Parallel to `names`.
    std::vector<uint8_t> simple;
    std::unordered_map<std::string, int> byName;
    int add(const std::string& n, bool simpleSlot = false) {
        auto it = byName.find(n);
        if (it != byName.end()) {
            // a redeclaration in the same owner keeps the STRICTER verdict
            if (!simpleSlot) simple[it->second] = 0;
            return it->second;
        }
        int s = (int)names.size();
        names.push_back(n);
        simple.push_back(simpleSlot ? 1 : 0);
        byName.emplace(n, s);
        return s;
    }
};

struct Env {
    std::unordered_map<std::string, Value> vars;
    std::shared_ptr<Env> parent;
    bool routineFrame = false; // a ROUTINE activation ($/ scopes here, like Rakudo's per-routine $/)
    bool loopFrame = false;    // a loop-statement `state` frame: plain `my` declares
                               // (e.g. in a while COND) skip past it to the enclosing
                               // scope, so they stay visible after the loop

    // `x()` materialises the extras (use for WRITES); `xr()` returns a shared
    // empty instance when there are none (use for READS, so a lookup never
    // allocates). Checking `ex` directly is fine too where the fast path matters.
    std::unique_ptr<EnvExtras> ex;
    EnvExtras& x() { if (!ex) ex = std::make_unique<EnvExtras>(); return *ex; }
    const EnvExtras& xr() const {
        static const EnvExtras kEmpty;
        return ex ? *ex : kEmpty;
    }

    // The pad (PADS-PLAN.md) — only on pad-owning frames: routine call frames
    // and the mainline global. `pad` is sized once at frame setup and NEVER
    // grows (lvalue() hands out Value* into it — the ValueHash stability
    // contract). A slot answers lookups only after its declaration executed
    // (the padLive bit), which keeps the outer variable visible before an
    // inner `my` of the same name runs. The layout is owned (shared_ptr): a
    // frame captured by a closure can outlive the Callable it came from.
    std::shared_ptr<const PadLayout> layout;
    std::vector<Value> pad;
    // ATOMIC, because a frame can be SHARED under RAKUPP_PARALLEL (two start
    // blocks closing over the mainline both reach global_'s layout) and the
    // bit is a PUBLICATION: define() writes the slot's Value first, then
    // release-stores the bit, and readers acquire it — a reader that sees the
    // bit sees the Value. The Linux TSan CI leg caught the plain uint64_t
    // (channel-pipeline/parallel): a |= RMW racing the liveness reads, with
    // the bit set BEFORE the value write on top. Under the GIL the ordering
    // is free; under parallel it is the contract.
    std::atomic<uint64_t> padLive{0};

    Value* padFind(const std::string& name) {
        auto it = layout->byName.find(name);
        if (it != layout->byName.end() &&
            ((padLive.load(std::memory_order_acquire) >> it->second) & 1))
            return &pad[it->second];
        return nullptr;
    }
    // THIS scope only — map or live pad slot, no parent walk (the declare
    // paths' "already exists in this scope" checks).
    Value* local(const std::string& name) {
        auto it = vars.find(name);
        if (it != vars.end()) return &it->second;
        return layout ? padFind(name) : nullptr;
    }
    Value* find(const std::string& name) {
        // Map FIRST: a slotted name never lives in the map (define redirects
        // and erases), so this order only costs the rare slow-path lookup of
        // a pad variable a second hash — and spares every builtin/special
        // lookup that walks THROUGH a layout frame from paying one.
        for (Env* e = this; e; e = e->parent.get()) {
            auto it = e->vars.find(name);
            if (it != e->vars.end()) return &it->second;
            if (e->layout)
                if (Value* p = e->padFind(name)) return p;
        }
        return nullptr;
    }
    // Returns the slot: a caller that needs the address of what it just defined
    // was hashing the name a second time to get it (`define(n, v); &vars[n]`).
    // Layout names land in the pad; the map-twin erase covers a lenient-mode
    // write that happened before the declaration executed.
    Value& define(const std::string& name, Value v) {
        if (layout) {
            auto it = layout->byName.find(name);
            if (it != layout->byName.end()) {
                // publication order: the Value lands BEFORE the bit that makes
                // it findable, and the map twin is erased after — a concurrent
                // reader sees the old map entry, or the published slot, never
                // a half-made one.
                Value& slotv = pad[it->second];
                slotv = std::move(v);
                padLive.fetch_or((uint64_t)1 << it->second, std::memory_order_release);
                if (!vars.empty()) vars.erase(name);
                return slotv;
            }
        }
        return vars[name] = std::move(v);
    }
    // Iteration that sees BOTH stores — for the introspection walkers
    // (breakSelfClosures, replNames, the __stash__ dump).
    template <typename F>
    void forEachVar(F&& f) {
        for (auto& kv : vars) f(kv.first, kv.second);
        if (layout) {
            uint64_t live = padLive.load(std::memory_order_acquire);
            for (size_t i = 0; i < pad.size(); i++)
                if ((live >> i) & 1) f(layout->names[i], pad[i]);
        }
    }
};

// control-flow signals
// Is this value DEFINED? Nil, Any, a type object — and a Failure, which is a
// Hash tagged "Failure". Exported because the --exe backend emits calls to it:
// codegen used to inline `t==VT::Nil||t==VT::Any||t==VT::Type` with a comment
// claiming Failure was covered, so `fail` under `--exe` did not answer to `//`.
bool rtIsDefined(const Value& v);

// `use JSON::Native` — installs native `from-json`/`to-json` into the using
// scope (JsonNative.cpp). Under RAKUPP_NATIVE_JSON=1 the same natives are also
// installed over `use JSON::Fast`, which is opt-in on purpose: silently
// answering another module's name would fork its semantics behind its back.
// Native extension modules (include/rakupp/rakupp_ext.h): dlopen `path`, check its ABI
// against RAKUPP_EXT_ABI, and hand back the subs it declares. On failure the
// message lands in errOut and the return value is undefined. ExtApi.cpp.
Value extLoadModule(const std::string& path, std::string& errOut,
                    std::vector<std::pair<std::string, Value>>& subsOut);

// "Cannot modify an immutable Set (Set(1 2))" — ONE sentence and ONE class for
// every immutable-container write. The `:delete` path threw X::Immutable where
// assignment threw X::Assignment::RO, so `CATCH { when X::Assignment::RO }`
// caught one and missed the other. Rakudo uses RO for both.
[[noreturn]] void throwImmutable(const Value& v);

// Dividing by zero yields a FAILURE carrying X::Numeric::DivideByZero — not a
// bare `Failure` type object, which has no exception to detonate and leaves `$!`
// unset. Three verbatim copies of the check inside one `if` block each returned
// the bare type object, and all three hard-coded `infix:<%%>` in the one message
// they did build.
Value divideByZero(const Value& lhs, const char* opName);          // the Failure form
[[noreturn]] void throwDivideByZero(const Value& lhs, const char* opName);
Value divZeroResult(const Value& lhs, const std::string& op);      // whichever the op wants

// The PUBLIC attributes of a class, in the order the default renderer shows them:
// the class's OWN first, then its parents' (Rakudo prints `Q.new(q => 2, p => 1)`
// for `class Q is P`). One walk, because `.gist` and `.raku` of a hookless object
// are the same string and were computing this two different ways.
void collectPubAttrs(ClassInfo* c, std::vector<const ClassAttr*>& out);

struct ReturnEx { Value v; };
struct ExitEx { int code = 0; };
// From 6.e, `next $v` / `last $v` supply a value for the iteration they end:
// `(1,2,3).map({ $_ == 2 ?? next(42) !! $_ })` is (1 42 3). hasVal separates
// "supplied Nil" from "supplied nothing"; before 6.e nothing ever sets it.
struct LastEx { std::string label; Value val; bool hasVal = false; };
struct NextEx { std::string label; Value val; bool hasVal = false; };
struct RedoEx { std::string label; };
struct DoneEx {}; // `done` control flow: exits the enclosing whenever block / supply body / react body
struct BreakGivenEx { Value v; bool hasVal = false; }; // `when`/`succeed` exits the enclosing given/loop, carrying its value
struct LeaveEx { Value v; bool hasVal = false; };       // `leave` exits the enclosing block (loop bodies skip NEXT)
struct ResumeEx {}; // `.resume` inside a CATCH — resume execution after the throw point
struct StopGatherEx {}; // a lazy gather has produced enough — unwind the (possibly infinite) block
struct ProceedEx {};    // `proceed` leaves a `when` block but keeps matching later ones
struct RakuError { Value payload; std::string message; };
// X::Feature::NotBuilt from a SLIM stub (FeatureGate.cpp). Its own C++ type so
// the LENIENT catch sites — regex code-block parse/exec, the regex ctor —
// can rethrow exactly this and keep swallowing ordinary errors: "this binary
// lacks the feature" must never degrade into a silent no-op or no-match.
struct FeatureNotBuilt : RakuError {};
// Thrown at an interpreter safe point to unwind a background worker thread whose
// result is no longer wanted (the mainline has finished). NOT a Raku-visible
// exception — user CATCH handles RakuError, never this.
struct WorkerAbortEx {};
extern thread_local bool t_isWorker;     // true only on `start`/async worker threads
extern thread_local Value t_threadSelf;   // the Thread instance running this worker (empty on main)
extern thread_local unsigned t_safePtCtr; // loop iterations since this worker last yielded the GIL

// A real (wired) tap of an on-demand `supply {…}` block. `closers` tear down
// inner taps / listening sockets; `closePhasers` are the block's CLOSE blocks.
// Shared by the Tap value (Value::ext) and every inner-tap wrapper.
struct TapHandle {
    std::mutex m;
    bool closed = false;
    std::vector<std::function<void()>> closers;
    std::vector<Value> closePhasers;
};
// One activation of an on-demand supply block (pushed on tctx_.tapStack while
// the block or one of its whenever-blocks runs). `emit` routes to emitCb — or
// appends to `collect` in eager-drain mode (the legacy synchronous semantics).
struct SupplyTapCtx {
    Value emitCb, doneCb, quitCb;   // downstream callbacks (may be empty)
    ValueList* collect = nullptr;   // eager drain: emits append here instead
    std::shared_ptr<TapHandle> tap;
    bool done = false;              // explicit `done` ran
    // implicit completion: the supply is done when its block has returned AND
    // every inner whenever-tap has signalled done (a listener's tap never does,
    // keeping that supply live)
    int pending = 0;                // inner taps not yet done
    bool blockDone = false;         // the supply block returned
    bool doneFired = false;         // downstream done already delivered
    ValueList closers;              // Supply.on-close callbacks for THIS activation
};

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
    // Reusable argument buffers for evalNqpOp, one per nesting depth. Every nqp
    // op used to build a fresh ValueList, so an nqp-heavy program (a tokenizer
    // written in Raku — JSON::Fast is 1.5M ops on a 278 KB document) paid a
    // malloc and a free per op for a vector of one to four Values. Keeping the
    // buffers keeps their capacity; the contents are still cleared on the way
    // out, so argument lifetimes are exactly what they were.
    //
    // A DEQUE, not a vector: an argument's own evaluation can re-enter
    // evalNqpOp, and growing a vector would reallocate under the outer frame's
    // live reference. deque never moves the elements it already holds.
    // Per-thread because ExecContext is (`static thread_local tctx_`).
    std::deque<ValueList> nqpArgs;
    size_t nqpDepth = 0;
    Env* curStateEnv = nullptr;
    std::vector<std::shared_ptr<ValueList>> gatherStack;
    std::vector<size_t> gatherLimits; // per-gather take cap (0 = unlimited); a take past it throws StopGatherEx
    // …and a per-gather TIME budget for the first probe, as a steady_clock
    // microsecond stamp (0 = none). 64 takes is a count, not a cost: a generator
    // whose takes get steadily more expensive must not be able to make merely
    // DECLARING it slow. A probe that runs out stops where it is, and the gather
    // becomes lazy — which is what a gather of more than 64 takes already is.
    std::vector<long long> gatherDeadlines;
    std::vector<ValueList*> supplyStack;
    std::vector<std::shared_ptr<SupplyTapCtx>> tapStack; // active on-demand supply activations
    std::vector<Value*> makeTargets;
    // Dynamically-enclosing CONTROL { } handlers (block + its closure env),
    // innermost last. `warn` runs the innermost IN PLACE — no unwinding, so
    // `.resume` is resumable by construction. Pushed/popped by the block
    // runner; a handler is popped WHILE it runs so its own warns escape
    // outward instead of recursing.
    std::vector<std::pair<Block*, std::shared_ptr<Env>>> controlHandlers;
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
    size_t redispatchFloor = 0;   // frames below this index are another routine's (callsame/nextsame can't see them)
    uint64_t curRoutineFrame = 0; // frameTop at the nearest enclosing ROUTINE entry
    // Cooperative unlabelled next/last/redo: set when the statement executes in
    // the SAME callable frame as the innermost native loop (no closure between);
    // labelled or cross-frame control still throws NextEx/LastEx/RedoEx.
    int loopCtl = 0;              // 0 none, 1 next, 2 last, 3 redo
    uint64_t curLoopFrame = 0;    // frameTop when the innermost native loop body runs
    // Cooperative `when`/`default`/`succeed`: a match in the SAME callable frame
    // as its enclosing given (or loop) body sets givenCtl instead of throwing
    // BreakGivenEx — the block executors break out and the given/loop consumes
    // the flag. A when behind a closure/builtin boundary still throws. This is
    // the hot-path shape (`given $v { when Int {…} … }` per row): on macOS a
    // C++ throw walks dyld unwind info under a lock, ~tens of µs each.
    int givenCtl = 0;             // 0 none, 1 when matched (break the given)
    Value givenV;                 // the matched when-block's value
    uint64_t curGivenFrame = 0;   // frameTop when a consuming given/loop body runs
    // current callable/routine for the &?BLOCK / &?ROUTINE magicals — raw pointers
    // into the live callCallableRaw frame (resolved lazily at lookup, zero per-call cost)
    const Value* curBlockVal = nullptr;
    const Value* curRoutineVal = nullptr;
    // One entry per live routine activation: the line its CALL was written on, and
    // the routine itself. `callframe(N)` walks it (Log::Async stamps every message
    // with `callframe(1)`). Pushed next to dynStack, which every call already pays.
    struct CallSite { int line; const Value* code; };
    std::vector<CallSite> callFrames;
    // Lvalue-mode method invocation: `$obj[i] = v` on a class whose AT-POS is
    // `return-rw @!arr[$i]` must write the REAL element, not a returned copy.
    // The subscript-lvalue path sets wantLvalue to callFrames.size()+1 before
    // invoking AT-POS/AT-KEY; a `return-rw` executing at exactly that frame
    // depth fills lvalueOut with lvalue(operand) — its target lives in the
    // object's shared containers, so the pointer survives the frame.
    int wantLvalue = 0;      // 0 off; else the callFrames depth being served
    Value* lvalueOut = nullptr;
    // `$obj.attr = v` — the attribute's declared type, recorded by the
    // MethodCall lvalue arm so the assignment can enforce it (a role-typed
    // `has C $.x is rw` must reject 42/Mu; roast S14-roles/basic.t)
    std::string lastLvalueAttrType;
};

// Backs a lazy list (an infinite `… … *` sequence, or `.map` over one). The Value
// holds the materialised prefix in its `arr`; `appendNext` computes one more element
// on demand (returns false when exhausted). Reached via Value::ext.
// State shared between a cued job's worker and its Cancellation value.
struct CueState { std::atomic<bool> cancelled{false}; };

struct LazySeqState {
    std::function<bool(ValueList&)> appendNext;
    bool infinite = false; // a truly unbounded source (…..Inf): elems/pop/tail/[*-1] must die
    // A `gather` block, which is not run until something pulls from it. Its
    // finiteness is therefore UNKNOWN until then, and `.is-lazy` — the one
    // question that inspects a sequence without consuming it — forces that
    // first pull so it can answer. `exhausted` records what the pull found.
    bool gatherSeq = false;
    bool exhausted = false;
    bool forceProbed = false;   // forceLazy has already asked once whether it ends
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
    // Externally-wired taps (e.g. OS-signal taps) whose teardown isn't driven by
    // supplyCloseStack_. When the react block ends they must be closed, or their
    // dispatcher keeps firing the handler after the block is gone (a second
    // Ctrl-C re-invoking `$server.stop` on an already-stopped service).
    std::vector<std::shared_ptr<TapHandle>> extTaps;
    // a whenever'd supply QUIT with no QUIT phaser: the react itself dies with
    // the cause once its loop unwinds (a refused connect fails the react)
    bool quitFlag = false;
    Value quitErr;
    std::vector<Value> closers; // Supply.on-close callbacks for THIS react (guard with m)
    // Deferred whenever activations (issue #18): Rakudo runs the react BODY
    // first and only then activates subscriptions — a `say` after a
    // `whenever <a b c>.Supply` prints before the first emitted value. The
    // synchronous drains queue here; B["react"] runs them after the body.
    std::vector<std::function<void()>> deferred;
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
                     ValueList* collect = nullptr,
                     const std::function<void()>& rebind = nullptr); // handles redo/next/last + FIRST/LAST; false => last.
                                                    // collect!=null: append each iteration's value (value context)

    // calling
    Value callCallable(const Value& codeVal, ValueList args, const std::vector<ExprPtr>* rwArgs = nullptr, bool ownFrame = false, bool arityCheck = false);
    // A routine by its sigilled name ("&foo"), resolved from the scope that is
    // executing right now and then outward — what rk_call needs so an extension
    // reaches the module that loaded it (ExtApi.cpp). Null when there is none.
    Value* extFindRoutine(const std::string& sigilName) {
        if (Value* v = tctx_.cur ? tctx_.cur->find(sigilName) : nullptr) return v;
        return global_ ? global_->find(sigilName) : nullptr;
    }
    // The one-shot CALL REGISTERS: set by a caller immediately before a call,
    // consumed at callCallableRaw's entry. `static thread_local`, like
    // redispatchStack_ — as plain members they were written by EVERY call on
    // EVERY thread, which was ThreadSanitizer's top report (2,761 lines on a
    // zero-sharing program) when the t/stress/ suite first ran under TSan.
    // The set→consume window is contiguous within one thread, so thread-local
    // is exactly their semantics.
    //
    // When set (one-shot), a paramless block's mutated implicit $_ is copied back
    // here after the call — `@a.grep({ $_++; True })` writes into @a's element.
    static thread_local Value* topicWriteback_;
    // The consumed topicWriteback_, re-exposed to a BUILTIN callable for the
    // duration of its run (builtins have no env for the $_ copy-back) — the
    // `++*` WhateverCode writes the driver's aliased element through it.
    static thread_local Value* builtinTopicWB_;
    // one-shot: the next callCallable does NOT autothread junction args
    // (Junction.THREAD passes each eigenstate — junctions included — whole)
    static thread_local bool noAutothread_;
    // one-shot: the next callCallable's activation is a ROUTINE frame even for a
    // bare block. `start { … }` sets it so `$/` scopes to the worker rather than
    // to the lexical scope every worker closes over — where all of them assigned
    // it into one std::map at once, which is a data race that corrupted the heap.
    static thread_local bool forceRoutineFrame_;
    // one-shot: loop-phaser control for the next callCallable, set by an
    // iterating driver (.map over a block with FIRST/NEXT/LAST). Bits:
    // 1 = this call is the first iteration (run FIRST), 2 = the last (run LAST),
    // 4 = run NEXT after the body. Phasers run in the invocation env so block
    // params are visible (Base64's LAST reads its $c).
    static thread_local int loopPhaserCtl_;
    // depth of live CATCH handlers: .resume outside any handler dies catchably
    // (a bare ResumeEx with nothing to absorb it would reach std::terminate)
    int catchDepth_ = 0;
    // (supplyCloseStack_ is gone: on-close callbacks now live ON the owning
    // ReactCtx / SupplyTapCtx. As a plain member the stack was shared across
    // worker threads — two concurrent reacts corrupted it (the syntax.t
    // crash); as a thread_local it cost loopsum 6% by reshuffling macOS TLV
    // layout under the interpreter's hot thread-locals. The context object
    // travels with the block, is already thread-correct, and is free.)
    Value callCallableRaw(const Value& codeVal, ValueList args, const std::vector<ExprPtr>* rwArgs, bool ownFrame = false, bool arityCheck = false); // no wrap layer
    Value callNative(Callable& c, ValueList& args, const std::vector<ExprPtr>* rwArgs = nullptr); // `is native` C FFI
    // NativeCall pointer helpers: a live Pointer / CArray return holds a raw
    // address whose deref/element access reads native memory.
    Value ncMakePointer(const std::string& type, void* p);   // Pointer / Pointer[T]
    Value ncMakeLiveCArray(const std::string& type, void* p);// CArray[T] over native memory
    static int ncElemSize(const std::string& ofType); // byte width of a CArray element (pointers are 8)
    static bool ncIsPointerElem(const std::string& ofType); // element is itself a pointer (Pointer/Str/CArray)
    static Value ncReadElem(long long addr, const std::string& ofType, long long index); // read one native element
    static void ncWriteElem(long long addr, const std::string& ofType, long long index, const Value& val);
    static long long ncFieldOffset(ClassInfo* ci, const std::string& field, std::string& type); // CStruct field byte offset
    static long long ncStructSize(ClassInfo* ci);   // CStruct total padded size
    static long long ncRawAddr(const Value& v); // extract a raw pointer from a native value (0 if none)
    Value cglobal(const std::string& lib, const std::string& sym, const std::string& type); // C global variable
    long runCallback(int slot, long a0, long a1, long a2, long a3, long a4, long a5); // NativeCall callback dispatch
    void runFfiClosure(void* closure, void* ret, void** args); // NativeCall ffi_closure dispatch
    // False on a thread the interpreter never entered — i.e. one the C library
    // made for itself. Such a thread has no lexical scope to run Raku in.
    bool onRakuThread() const { return (bool)tctx_.cur; }
    Value spawnTimerWhenever(double secs, Value blk, std::shared_ptr<ReactCtx> ctx); // `whenever Promise.in(N)` timer
    Value spawnIntervalWhenever(double interval, double delay, Value blk,
                                std::shared_ptr<ReactCtx> ctx,
                                std::shared_ptr<TapHandle> handle); // Supply.interval ticker
    Value spawnChannelWhenever(Value chan, Value blk, std::shared_ptr<ReactCtx> ctx); // `whenever $channel`
    Value spawnSupplyTimer(double secs, Value blk, std::shared_ptr<SupplyTapCtx> ctx); // same, inside a supply {} block
    Value spawnSupplyInterval(double interval, double delay, Value blk,
                              std::shared_ptr<SupplyTapCtx> ctx); // Supply.interval inside a supply {} block
    // anonymous pun of a parameterized role with `[...]` args bound (P[%h].new / Q[Int].mk)
    Value makeRolePun(ClassInfo* role, const std::string& roleName, ValueList& argv);
    // bind a composed role's `[...]` params into `dest`, so the role's method bodies
    // see them; `argv` may be empty — a bare `does R`/`but R` still binds the DEFAULTS
    void bindRoleParamsInto(ClassInfo* dest, ClassInfo* role, ValueList& argv,
                            const std::shared_ptr<Env>& scope);
    // Live-Supply transform chain: run one emitted value through a tap's chain of
    // grep/map/head/… steps. Returns the values to forward; sets `complete` when the
    // chain has finished (head/first reached its limit) so `done` should fire.
    ValueList applyTapChain(Value& tap, const Value& in, bool& complete);
    // Same chain, but for a KIND-based live supply (interval/signal/async-read):
    // those have no tap record to hang the chain on, so it rides on the Supply
    // value and this wraps the consumer block with it. See the definition.
    Value wrapSupplyChain(const Value& supply, Value consumer);
    Value callBuiltin(const std::string& name, ValueList args); // invoke a named builtin (used by codegen)
    // Same, but with the INTERPRETER's resolution order: a routine bound in the
    // environment wins over the built-in of that name (evalCall's
    // `find("&"+name)` before the builtin table). Codegen emits this for the
    // names a `use`d module exports — an `is export`ed `sub val` has to beat the
    // built-in `val` in compiled code exactly as it does in the interpreter.
    Value callEnvFirst(const std::string& name, ValueList args);
    // Resolve a builtin's function once (at compiled-program startup) so call
    // sites can go through the pointer directly, skipping callBuiltin's
    // per-call name hash + map lookup. Null when the name is not a registered
    // builtin (e.g. a module-loaded routine) — the call site then falls back
    // to the full callBuiltin path. Pointers into builtins_ stay valid: the
    // map is populated once in the constructor and never erased.
    const BuiltinFn* builtinPtr(const std::string& name) const {
        auto it = builtins_.find(name);
        return it == builtins_.end() ? nullptr : &it->second;
    }
    Value seqOp(Value l, Value r, bool exclusive); // the `...` sequence operator (also used by codegen)
    // `...` is list-associative: `1 ... 5 ... 1` and `'A'...'Z', 'a'...'z'` are ONE
    // operator over a list of lists. Each group's first element closes the previous
    // segment; the rest is emitted verbatim and seeds the next. Shared with codegen.
    Value seqOpGroups(Value seed, const std::vector<ValueList>& groups,
                      const std::vector<char>& exclEnd, bool exclSeed);
    std::vector<long long> evalShapeDims(Expr* shape); // `my @a[2;3]` dimension list
    Value rtGather(Value blockClosure); // gather with probe-and-double laziness (native codegen)
    // Emit text for say/print/put/note: route through a user-overridden $*OUT/$*ERR
    // (call its .print), else write to the real stream.
    Value ioEmit(const std::string& s, const char* dynVar, bool toErr);
    Value getArgs(); // @*ARGS as a List value (used by codegen)
    void syncEnvToProcess(); // push %*ENV into the real process environment, so children inherit it
    Value dynVar(const std::string& name);
    Value rakuIntrospection(bool compiler); // $*RAKU / $*RAKU.compiler // $* / $? magical variables (used by codegen)
    Value& dynVarRef(const std::string& name); // assignable dynamic-var slot (used by codegen)
    bool mainNamedAnywhere(); // %*SUB-MAIN-OPTS<named-anywhere> in force at MAIN dispatch (used by codegen)
    // The MAIN command-line protocol (pairing, scoring, usage/--help), shared by
    // the interpreter's auto-invoke and compiled binaries. -1 = matched (margs
    // filled); otherwise the exit code, usage already printed.
    int mainProtocol(Value& mainSub, ValueList& margs);
    void refreshArgvFromLiveArgs(); // MAIN parses the LIVE @*ARGS, not the process argv
    // --exe: adopt the embedded signature-only AST and define a metadata-rich
    // &MAIN wrapping the compiled entry point; then dispatch through it.
    void registerCompiledMain(const unsigned char* blob, size_t len, Value (*fn)(ValueList&));
    int runCompiledMain(Value (*fn)(ValueList&));
    Value& accessorRef(Value& base, const std::string& name); // $obj.accessor lvalue (used by codegen)
    Value postfixIPub(Value v) { return postfixI(std::move(v)); } // postfix:<i> (used by codegen)
    void rtUse(const std::string& module, const std::string& arg = ""); // `use MODULE` (used by codegen)
    Value rtNameTerm(const std::string& n); // bareword: env value / &call / builtin / type object (used by codegen)
    void registerNamedRegex(const std::string& name, const std::string& pattern, const std::string& kind) {
        namedRegex_[name] = pattern; namedRegexKind_[name] = kind; // (used by codegen)
    }
    Value idxW(const Value& base, Value key, bool isHash); // index with a Whatever/WhateverCode key (@a[*-1], @a[*])
    void materializeLazy(const Value& v, size_t n); // grow a lazy list's prefix to >= n elements (capped)
    void drainIfFiniteLazy(const Value& v);         // …all of it, when the list is lazy but finite
    // Store through an lvalue expression, refusing a readonly container. Used by
    // the `.=` write-backs, which do not go through the assignment operator and
    // so never met its guard.
    void assignChecked(struct Expr* target, Value v);
    // A code assertion (`<?{…}>` / `<!{…}>`) only evaluates when the engine is
    // handed a hook; with none it defaults to PASS. Every site that builds its
    // own Regex needs this, or one pattern answers differently in each of them.
    struct GrammarHooks codeAssertHooks();
    static bool patHasCodeAssert(const std::string& pat);
    Value methodCall(const Value& inv, const std::string& method, ValueList args, const std::vector<ExprPtr>* rwArgs = nullptr);
    Value methodCallInner(const Value& inv, const std::string& method, ValueList args, const std::vector<ExprPtr>* rwArgs);
    // Ordered SEGMENTS of the same dispatch chain, split out of methodCallInner to
    // get it under control (it was 9,138 lines). Each returns nullopt for "not
    // handled here"; they must be called in this order — see MethodCallTail.cpp.
    std::optional<Value> methodCallPart2(const Value& inv, const struct MName& m, ValueList& args,
                                         const std::vector<ExprPtr>* rwArgs);
    std::optional<Value> methodCallPart3(const Value& inv, const struct MName& m, ValueList& args,
                                         const std::vector<ExprPtr>* rwArgs);
    std::optional<Value> methodCallTail(const Value& inv, const struct MName& m, ValueList& args,
                                        const std::vector<ExprPtr>* rwArgs);
    Value exceptionFor(const RakuError& e); // $!/$_ value for a caught error: always a DEFINED exception instance
    // A Failure stores its exception as a bare TYPE plus a message; anything
    // that hands the exception to user code (`$!`, `.exception`, `.throw`) must
    // hand over a defined instance instead, or `if $!` is False and `.message`
    // is "no such method".
    Value failureException(const Value& failure);
    // Run the innermost CONTROL handler for a `warn` (CX::Warn). True = the
    // handler .resume'd, so the default stderr print is suppressed; false =
    // no handler, or it finished without resuming (default behaviour stands).
    bool runControlWarn(const std::string& msg);
    std::string gistOf(const Value& v); // .gist, honouring a user-defined `method gist` (for say/note)
    std::string strOf(const Value& v);  // .Str,  honouring user `method Str`/`gist` (for print/put/interpolation)
    // The string a regex matches AGAINST. An object matches on its Str form:
    // `$path ~~ /…/` where $path is a URI::Path must see "/a/b", as in Rakudo.
    std::string rxSubject(const Value& v) { return v.t == VT::Object ? strOf(v) : v.toStr(); }
    Value invokeMethod(const Value& codeVal, const Value& self, ValueList args, const std::vector<ExprPtr>* rwArgs = nullptr, bool ownFrame = false,
                       Value* selfBack = nullptr,  // selfBack: copy the frame's `self` out (rw invocant)
                       bool skipWrappers = false); // true: innermost wrap level reached — run the body
    // A method `augment`-ed onto a BUILT-IN type, if there is one for this invocant.
    Value* builtinExtMethod(const Value& inv, const std::string& m);
    // What an object contributes when assigned to a `%` container: its own
    // `.list`/`.iterator` (declared or `handles`-delegated), if it has one.
    bool objListItems(const Value& v, ValueList& out);
    // A Proxy read as a VALUE answers its FETCH. See the definition in Interpreter.cpp.
    Value deproxy(Value v);
    // `T($v)` coercion — see the definition in Interpreter.cpp.
    Value coerceToType(const Value& v, const std::string& type);
    // Run a Proxy's STORE for `$proxy = v`. See the definition in Interpreter.cpp.
    Value proxyStore(const Value& proxy, const Value& v);
    // The hash behind `for values %h` — see the definition in Interpreter.cpp.
    std::shared_ptr<ValueMap> valuesAliasSource(Expr* listExpr);
    // The array behind `for @$x` — likewise; the topic aliases its elements.
    std::shared_ptr<ValueList> derefArrayAlias(Expr* listExpr);
    // The containers behind `for $a, $b, $c` — likewise.
    bool scalarListAlias(Expr* listExpr, std::vector<Value*>& slots);
    Value* topicAliasSlot(Expr* topic, bool skip);  // the slot a given/with topic aliases
    // peel a `.grep(PRED)` off a loop source, so the alias sources above still
    // recognise `for %h.values.grep(…) { $_ = … }` (Rakudo's grep is `is raw`)
    Expr* peelGrepFilter(Expr* listExpr, Expr*& pred);
    bool grepFilterKeeps(Expr* pred, const Value& v);
    // Invoke method `name` found from `startCls`, pushing a redispatch context so
    // callsame/nextsame reach the same method on the owning class's parent (recursively).
    Value invokeMethodChain(const std::string& name, ClassInfo* startCls, const Value& self,
                            ValueList args, const std::vector<ExprPtr>* rwArgs = nullptr);
    void copyOutRw(const std::vector<Param>* params, std::shared_ptr<Env>& env, const std::vector<ExprPtr>* rwArgs);
    // `soleCandidate` = this routine is not part of a multi, so an `is rw`
    // parameter given a container-less argument is an error rather than a
    // reason to try the next candidate.
    void setupRwLinks(const std::vector<Param>* params, std::shared_ptr<Env>& env,
                      const std::vector<ExprPtr>* rwArgs, bool soleCandidate = false);
    void setupRwSlots(const std::vector<Param>* params, std::shared_ptr<Env>& env, const std::vector<Value*>* slots);
    // shared hyper-operator core for every spelling (>>op<<, »op«, >>[&op]<<)
    Value hyperCore(Value& l, Value& r, bool strictL, bool strictR,
                    const std::function<Value(const Value&, const Value&, Value*, Value*)>& apply,
                    Value* lroot = nullptr, Value* rroot = nullptr, bool wantSlots = false);
    // The shared container walk of the hyper-unary forms; the callers supply
    // only the leaf operation.
    Value hyperWalk(Value& v, const std::function<Value(Value&)>& leaf);
    Value hyperUnary(const std::string& op, Value v);       // -«(…), --«%h — deep prefix
    Value hyperPostfixApply(const std::string& op, Value v); // @a»++, %h»!, (2,3)»i — deep postfix
    void rwWriteThrough(Expr* target);
    // one-shot direct rw slots for the NEXT callCallableRaw activation (hyper-with
    // element calls — same consume-at-top pattern, and same thread_local
    // reasoning, as topicWriteback_)
    static thread_local const std::vector<Value*>* pendingRwSlots_;
    Value evalAssignInner(Assign* a, bool sink);
    bool anyRwLinks_ = false; // sticky: some frame created an rw link (guards the per-assignment hook)
    int scoreCandidate(const Value& cand, const ValueList& args); // -1 = no match, else specificity
    bool boolify(const Value& v); // boolean context: honours a custom .Bool method on objects
    // TARG lever B: a condition of a chapter-19-specialized comparison shape
    // over plain Ints answers as a C++ bool with no Value round trip.
    // Returns -1 when the shape/operands do not qualify (caller does the
    // ordinary eval+boolify), else 0/1.
    int tryCondBool(Expr* e);
    void setMatchVar(Value v); // set $/ (updates an enclosing scope's $/ if present)
    bool hoistSubs(const std::vector<StmtPtr>& stmts); // pre-register sub decls (whole-scope visibility); returns true if any named sub was hoisted
    // `cache` is the owner's decided-once flag (Block::hoistNeed / Callable::
    // hoistNeed): -1 undecided, 0 nothing to hoist, 1 something. See the definition.
    void hoistExprDecls(const std::vector<StmtPtr>& stmts, Env* env, DecidedOnce<signed char>* cache = nullptr);
    // Pads (PADS-PLAN.md): build (or fetch) the layout for one owner body and
    // annotate its dominated VarExprs with (slot, owner). Cached per BODY
    // address; thread-safe (padMu_). `params` supplies the owner's parameter
    // names (slots 0..k-1); null for the mainline. Returns null when the owner
    // has no slot candidates (or too many for the 64-bit liveness mask).
    std::shared_ptr<const PadLayout> resolvePads(const std::vector<StmtPtr>& stmts,
                                                 const std::vector<Param>* params);
    std::mutex padMu_;
    std::unordered_map<const void*, std::shared_ptr<const PadLayout>> padLayouts_;
    // May a reusing loop skip the per-iteration scope clear and overwrite the
    // topic in place? Cached on the Block (flatLoop). See the scan in
    // Interpreter.cpp for the exact rules.
    bool flatLoopBody(Block* b);
    // The pad pointer for an annotated reference, or null (not annotated, not
    // live, or running under a frame chain the annotation does not belong to
    // — the derive-and-compare rule from PADS-PLAN.md, shared by every fast
    // path that wants a slot).
    Value* padPtr(const VarExpr* ve) {
        int ps = ve->padSlot;
        if (ps < 0) return nullptr;
        for (Env* pf = tctx_.cur.get(); pf; pf = pf->parent.get()) {
            if (!pf->layout) continue;
            if ((const void*)pf->layout.get() == ve->padOwner &&
                ((pf->padLive.load(std::memory_order_acquire) >> ps) & 1))
                return &pf->pad[ps];
            break; // nearest layout frame decides — never skip past it
        }
        return nullptr;
    }
    void enforceTypedAssign(const std::string& nm, Value& rhs); // typed-assign contract, shared by `=` and atomic-assign
    // The striped-lock pool shared by cas, the atomic-* family, and Channel:
    // real mutual exclusion in parallel (no-GIL) mode, negligible uncontended
    // cost under the GIL. Recursive so a holder may re-enter its own stripe.
    static std::recursive_mutex& atomicStripe(const void* p);
    // Parallel-mode-only stripe over a USER container's structural op (Array
    // growth, Hash find-or-insert) — the no-crash half of the memory model
    // (P3). Under the GIL it constructs to nothing, so the single-threaded
    // hot path pays a predicted branch and nothing else. Never hold one
    // around user code.
    struct ParStripe {
        std::unique_lock<std::recursive_mutex> l;
        ParStripe(const Interpreter& I, const void* p) {
            // engage only in parallel mode AND while workers are LIVE: before
            // the first spawn and after the last join a single thread cannot
            // race itself, so a single-threaded program under
            // RAKUPP_PARALLEL=1 pays two predicted branches and nothing else
            // (the P5 gate: the machinery must be free when one thread runs —
            // the stripe tax was pushing big compute files past the roast
            // timeout with zero threads in them).
            if (I.parallelMode_ && I.liveWorkers_.load(std::memory_order_relaxed) > 0)
                l = std::unique_lock<std::recursive_mutex>(atomicStripe(p));
        }
    }; // pre-declare `my` vars buried in expressions (ternary/nqp branches) — Raku block scoping
    void applySubTraits(SubDecl* sd); // run user `is` traits of a hoisted sub at its textual position
    // subset NAME of BASE where EXPR — refinement types for dispatch and ~~
    // langRev: the revision the subset was DECLARED under. Rakudo's SubsetHOW
    // records it (that is what `Even.^ver` reports, "6.d" or "6.e"), and it is
    // not decoration — the metamodel keys type-check behaviour off it.
    struct SubsetInfo { std::string base; const Expr* where = nullptr; int langRev = 1; };
    std::unordered_map<std::string, SubsetInfo> subsets_;
    // per-site `ff`/`fff` flip-flop latch + how many elements since it fired
    // (the result while on is that count, not a Bool)
    long long anonMixinSeq_ = 0; // names the anonymous role a value mixin composes
    struct FlipFlop { bool on = false; long long seq = 0; };
    std::unordered_map<const void*, FlipFlop> ffState_;
    bool subsetMatches(const std::string& name, const Value& v, int depth = 0);
    bool typeOrSubsetMatches(const Value& v, const std::string& type); // typeMatchesArg + subsets
    Value evalNqpOp(NqpOp* n); // the `use nqp` compatibility subset (zero-cost when unused)
    // lone-candidate bind: throw X::TypeCheck::Binding on mismatch. blockParam
    // says the signature belongs to a Block, whose untyped parameters are
    // Mu-constrained where a Routine's are Any-constrained.
    void typeCheckBind(const Param& p, const Value& v, bool blockParam = false);
    std::string symRefName(SymbolicRef* sr); // effective name of a multi-segment symbolic ref
    [[noreturn]] void throwTyped(const std::string& type,
                    std::vector<std::pair<std::string, std::string>> attrs,
                    const std::string& message); // typed exception OBJECT with attributes
    [[noreturn]] void throwTypedV(const std::string& type,
                    std::vector<std::pair<std::string, Value>> attrs,
                    const std::string& message); // same, attribute values as Values
    Value checkRetType(const Callable& c, Value v); // enforce declared return type
    Value makeTypedEx(const std::string& type,
                    std::vector<std::pair<std::string, Value>> attrs,
                    const std::string& message); // build (don't throw) a typed exception object
    static bool exprHasWhateverLit(const Expr* e); // does the expression contain a literal `*`? (curry test)
    // `»`.method over a container, shared by the direct and the curried paths
    Value hyperMethodEach(const Value& inv, const std::string& m, ValueList& args);
    // thread_local like the call registers above: written per-block / per-
    // statement on every thread (the next TSan reports after the registers)
    static thread_local bool hoistingSubs_;
    // class/grammar/role declarations already created by a hoist pass, counted so
    // a recursive re-entry of the same statement list stays balanced
    std::unordered_map<const ClassDecl*, int> hoistedTypes_;
    // class/grammar/role declarations seen ahead in a scope but not yet created
    std::unordered_map<std::string, ClassDecl*> pendingTypes_;
    bool materializePendingType(const std::string& name); // true while hoistSubs is registering (defers trait application)
    void breakSelfClosures(Env* env); // drop the closure back-edge of any non-escaped nested sub, so a frame with a self-closured sub can be freed
    void runProcPromise(Value& promise, double timeoutSec); // run a Proc::Async .start promise (with optional timeout)
    void runEnterPhasers(const std::vector<StmtPtr>& stmts); // ENTER/FIRST at block entry (source order)
    void runFirstPhasers(const std::vector<StmtPtr>& stmts); // FIRST once before a loop's first iteration
    void runLastPhasers(const std::vector<StmtPtr>& stmts);  // LAST once after a loop's last iteration
    // LEAVE/KEEP/UNDO at block exit (reverse order). `ok` selects which conditional
    // phasers fire: LEAVE always, KEEP only on a successful exit, UNDO only on an
    // unsuccessful one (an exception / control exception leaving the block).
    // `tempMark` is how many `temp` restores the scope already owed on ENTRY: a
    // block that runs in an env it did not create (a statement-modifier loop body
    // reuses the caller's) must unwind only the temps IT pushed.
    void runLeavePhasers(const std::vector<StmtPtr>& stmts, bool ok = true, size_t tempMark = 0);
    void runNextPhasers(const std::vector<StmtPtr>& stmts, std::shared_ptr<Env>& scope); // NEXT at each loop iteration's end
    static thread_local bool suppressLoopFirst_; // set while running a loop body so execBlock skips FIRST (save/restore per thread, like the call registers)
    // EVAL. `incompleteOut` (REPL only) turns a parse that died on end-of-input
    // into a soft "give me more" answer instead of a thrown syntax error.
    Value evalString(const std::string& src, bool mainlinePH = false, bool* incompleteOut = nullptr);
    // ---- REPL support (src/Repl.cpp) ----------------------------------------
    // A REPL never calls run(): it keeps ONE Interpreter alive and feeds it
    // evalString per line, so the mainline scope IS the session. These two cover
    // what run() would otherwise have done at either end.
    void replStart(std::vector<std::string> args); // define @*ARGS, arm mainline `state`
    void replFinish();                             // run END/deferred-END phasers, once, at exit
    // Every name visible from the current scope, for tab completion.
    std::vector<std::string> replNames() const;
    // `use Foo::Bar` -> compile lib file into global scope. `quiet` suppresses the
    // not-found warning: a runtime `require` reports failure by THROWING instead
    // (so `try require ::($m)` is silent, as in Rakudo).
    void loadModule(const std::string& name, const std::vector<std::string>& importArgs = {}, bool doImport = true, bool quiet = false, const std::string& verReq = "");
    std::vector<std::string> libPaths_{"lib", ".", "rakulib"}; // + env-derived paths, filled in the ctor
    std::set<std::string> loadedModules_;
    // each loaded module's `sub EXPORT(*@_)`, kept so a REPEAT `use` can run the
    // import protocol again in the new scope (JSON::Fast's per-scope defaults)
    std::map<std::string, Value> moduleExportSubs_;
    // sets $/ $0..; rxVal (an anonymous `regex {…}` value) engages wired mode:
    // code blocks/assertions run for real, in the regex's closed-over scope
    // What a `for` walks: an object with its own `.iterator` decides for itself.
    Value iterationSourceOf(Value v);
    std::thread::id mainThreadId() const { return mainThread_; } // `exit` ends the process from any thread
    // Resolve `$var` atoms inside a regex SOURCE (regex splice / literal text).
    std::string interpRegexPattern(const std::string& in);
    std::string interpP5Pattern(const std::string& in); // :P5 — $vars splice as raw regex source
    // Bake regex-valued variables into an `rx//` source at construction time.
    std::string spliceRegexVars(const std::string& pat);
    Value regexMatch(const std::string& subject, const std::string& pattern,
                     const Value* rxVal = nullptr);
    std::string rxInterpArrays(const std::string& pat); // `/@arr/` -> longest-first literal alternation
    Value regexSubst(const std::string& subject, const std::string& pattern,
                     const std::string& repl, std::string& out, bool& changed);
    // .subst / s/// with occurrence-selection adverbs (:g/:x/:nth/:p/:c) and the
    // sameX adverbs (:samecase/:samespace/:samemark). Sets nsub = # replacements.
    std::string substSelect(const std::string& subj, const std::string& pat,
                            Value* replArg, ValueList& args, long& nsub, bool literal = false,
                            const std::string* tmplRepl = nullptr, Value* matchResult = nullptr);
    // `target ~~ s/pat/repl/` (or tr///, or non-mutating S///) as one call — the
    // native-codegen entry point. Mutates *target unless nonMut; returns what the
    // interpreted form returns (Match/List for s///, count for tr///, Str for S///).
    Value substApply(Value* target, const std::string& pattern, const std::string& repl, bool nonMut);
    Value grammarParse(ClassInfo* g, const std::string& input, bool subparse, const std::string& startRule, Value actions);

    std::unordered_map<std::string, std::shared_ptr<ClassInfo>> classes_;
    // Package-relative SHORT names: registering a qualified class `URI::Path`
    // aliases its tail `Path` -> `URI::Path` (first wins; a real class of the
    // short name always beats the alias). Approximates Rakudo's package-stash
    // lookup (inside `unit class URI`, bare `Path` finds `URI::Path`) in our
    // flat class-table model. Consulted ONLY on a classes_ miss.
    // `.WHO` stashes, one shared map per package NAME. The stash must be the SAME
    // map on every call or writing into it mutates a temporary — which is exactly
    // how `EXPORTHOW.WHO.<grammar> = SomeHOW` used to die "Target is not
    // assignable" (WHO built a fresh empty Hash each time). Reads re-sync the
    // package's qualified globals in, so `our`-scoped symbols show up too.
    std::map<std::string, std::shared_ptr<ValueMap>> pkgStashes_;
    std::shared_ptr<ClassInfo> howClsInfo_; // shared class of persistent .HOW metaobjects (see m == "HOW")
    std::unordered_map<std::string, std::string> classAliases_;
    const std::string& resolveClassAlias(const std::string& n) {
        // NativeCall's legacy untyped-pointer name, still written by real
        // dists (OpenSSL's suite) — one static alias, so every Pointer
        // special-case downstream just works
        if (n == "OpaquePointer") { static const std::string P = "Pointer"; return P; }
        if (classes_.count(n)) return n;
        auto it = classAliases_.find(n);
        if (it != classAliases_.end()) return it->second;
        // A nested type named by a PARTIAL path from inside its own package:
        // `Globber::Match` inside `unit class IO::Glob` is IO::Glob::Globber::Match.
        // Rakudo resolves it lexically; here the registry is flat, so accept the
        // unique registered name that ends with `::n`. Only qualified names take
        // this road — a bare `Match` must not silently find a nested one.
        if (n.find("::") != std::string::npos) {
            const std::string suffix = "::" + n;
            const std::string* hit = nullptr;
            for (auto& kv : classes_) {
                if (kv.first.size() <= suffix.size()) continue;
                if (kv.first.compare(kv.first.size() - suffix.size(), suffix.size(), suffix) != 0) continue;
                if (hit) return n;   // ambiguous — leave it alone
                hit = &kv.first;
            }
            if (hit) return classAliases_.emplace(n, *hit).first->second;
        }
        return n;
    }
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
    int loadingModuleDepth_ = 0; // >0 while a `use`d module file executes (export surfacing)
    bool moduleDoImport_ = true; // false while a `need`ed module loads (no symbol import)
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
    // The ONLY safe way to register a worker thread. In parallel mode spawns
    // happen from worker threads too (a start block tapping a supply spawns
    // the interval ticker), and the old per-site `reapFinishedWorkers(); …
    // push_back(…)` pattern mutated the vector from several threads at once —
    // vector::erase corrupted it and the process SEGFAULTED (syntax.t's
    // CLOSE-phaser stress, crash report: erase inside spawnSupplyInterval on
    // thread 5 of 292). Under the GIL the lock is uncontended.
    void addWorker(BigStackThread&& th, std::shared_ptr<std::atomic<bool>> fin) {
        // Collect finished slots under the lock, JOIN THEM OUTSIDE IT. Joining
        // under sharedMut_ deadlocked: the joinee had set its done flag but was
        // still unwinding through code that itself wants sharedMut_ (a worker
        // spawning a nested worker) — the reaper held the lock waiting for the
        // joinee, the joinee waited for the lock.
        std::vector<WorkerSlot> dead;
        {
            std::lock_guard<std::mutex> lk(sharedMut_);
            auto mid = std::partition(workers_.begin(), workers_.end(), [](WorkerSlot& w) {
                return !(w.done && w.done->load(std::memory_order_acquire));
            });
            for (auto it = mid; it != workers_.end(); ++it) dead.push_back(std::move(*it));
            workers_.erase(mid, workers_.end());
            workers_.push_back({std::move(th), std::move(fin)});
        }
        for (auto& w : dead) if (w.th.joinable()) w.th.join();
    }
    void reapFinishedWorkers() {           // caller must hold the GIL or sharedMut_
        workers_.erase(std::remove_if(workers_.begin(), workers_.end(), [](WorkerSlot& w) {
            if (w.done && w.done->load(std::memory_order_acquire)) { w.th.join(); return true; }
            return false;
        }), workers_.end());
    }
    std::atomic<int> liveWorkers_{0};      // workers that have not yet finished
    // Backpressure for worker creation, PARALLEL MODE ONLY. A tap-and-close
    // loop over interval supplies (syntax.t's CLOSE-phaser stress: 4 spawners
    // x 1000 activations, each a real thread with a 256 MiB virtual stack)
    // outran teardown and SEGFAULTED the process on thread/address-space
    // exhaustion. Above the cap a spawner reaps finished workers and waits
    // for the herd to thin — a closed ticker exits within one poll tick.
    // Under the GIL this must stay off: a spawner spinning while HOLDING the
    // lock would keep the very workers it waits for from finishing.
    void throttleSpawn() {
        if (!parallelMode_) return;
        while (liveWorkers_.load(std::memory_order_relaxed) >= 384) {
            std::vector<WorkerSlot> dead;
            {
                std::lock_guard<std::mutex> lk(sharedMut_);
                auto mid = std::partition(workers_.begin(), workers_.end(), [](WorkerSlot& w) {
                    return !(w.done && w.done->load(std::memory_order_acquire));
                });
                for (auto it = mid; it != workers_.end(); ++it) dead.push_back(std::move(*it));
                workers_.erase(mid, workers_.end());
            }
            for (auto& w : dead) if (w.th.joinable()) w.th.join(); // outside the lock
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
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
    bool yieldToWorkerFor(double secs);    // …bounded: false if the wait expired with no progress
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
    Value spawnPromise(Value code, Value threadVal = Value());
    Value cueJob(Value code, double delaySecs, double everySecs, long long times,
                 Value stopF, Value catchF); // $*SCHEDULER.cue — returns a Cancellation
    // Real (wired) tap of a Supply value: on-demand blocks run with emits routed
    // downstream; live Suppliers register a tap record; async-socket supplies
    // spawn their I/O worker. Returns a Tap value (ext = TapHandle when wired).
    Value tapSupply(const Value& s, Value emitCb, Value doneCb, Value quitCb);
    // Wire a `signal(SIGINT,…)` Supply: install a handler (self-pipe trick),
    // register the tap, and lazily spawn the dispatcher worker that runs the
    // whenever block under the GIL when a signal arrives. reactCtx (may be null)
    // is pushed on the worker's reactStack_ so a `done` in the block closes it.
    Value tapSignal(const std::vector<int>& sigs, Value emitCb, Value doneCb,
                    std::shared_ptr<ReactCtx> reactCtx);
    // Legacy eager semantics: run an on-demand supply block now, collecting its
    // emits into a values-backed Supply (what `supply {…}` used to return).
    Value drainSupplyBlock(const Value& s);
    void closeTapHandle(const std::shared_ptr<TapHandle>& h); // run closers + CLOSE phasers once
    void maybeFinishSupply(const std::shared_ptr<SupplyTapCtx>& ctx); // fire done when block returned + no live inner taps
    int noCycleBreak_ = 0; // >0: breakSelfClosures suspended (supply-block wiring; env outlives frame)
    std::atomic<long> cuedLoads_{0}; // outstanding cued jobs ($*SCHEDULER.loads)
    void awaitPromise(const std::shared_ptr<struct PromiseState>& ps);
    void runReactLoop(const std::shared_ptr<ReactCtx>& ctx); // block until live sources done
    void engageGil();                      // lazily lock the GIL on first async use
    void drainWorkers();
    void registerWriteHandle(const std::shared_ptr<ValueMap>& h) { openWriteHandles_.push_back(h); }
    void flushOpenWriteHandles();  // flush any unclosed write handle at program exit
    std::vector<std::shared_ptr<ValueMap>> openWriteHandles_;

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
    // True when the code being executed was written for 6.e or later. Every
    // 6.e-only routine, method and behaviour is gated on this, so a 6.d program
    // cannot tell which engine is running it. See docs/dev/plans/6E-PLAN.md.
    bool sixE() const { return langRev_ >= 2; }
    // True once anything in the process has been compiled under a revision other
    // than the default. Almost every program is one unit of 6.d, and then the
    // per-call revision switch has nothing to do — this lets the call path skip
    // it on a single bool instead of reaching into the callee.
    bool anyRevSwitch_ = false;
    // Subs that 6.e adds to CORE. Under 6.d they must not exist at all — a
    // program that calls one gets "Undefined routine", as it does in Rakudo,
    // where these live in CORE.e and a 6.d unit never loads it.
    static bool sixEOnlySub(const std::string& n) {
        return n == "snip" || n == "snitch" || n == "rotor" || n == "trans" || n == "nano";
    }
    // Callable under 6.d, but only because the caller's own revision says so:
    // resolves a builtin name unless this revision is not supposed to see it.
    bool builtinVisible(const std::string& n) const { return sixE() || !sixEOnlySub(n); }
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
    std::shared_ptr<Program> mainSigProg_; // --exe: owns the Params &MAIN's metadata borrows (registerCompiledMain)
    static thread_local std::vector<std::shared_ptr<ReactCtx>> reactStack_; // active `react {}` event loops
    static thread_local int threadDepth_; // >0 while running inside a Thread.start/Promise worker block (is-initial-thread)
    // (cur_/dynStack_/curStateEnv_/gatherStack_/supplyStack_/makeTargets_/pkgPrefix_/
    //  callDepth_ moved into the thread_local tctx_ above.)
public:
    std::string finishData_;          // $=finish data block of the module being run
    std::string podData_;             // rendered =pod content (printed at end in --doc mode)
    std::vector<Value> podDom_;       // $=pod structured DOM (Pod::Block values)
    bool docMode_ = false;            // --doc: run DOC phasers and print the rendered POD
    // A test builtin that RUNS user code before reporting (lives-ok/dies-ok/
    // throws-like/…) must name its own CALL SITE, not wherever the block ended up:
    // running the block advances curLine_ into the callee. Save on entry, restore
    // before emitTest.
    int testLine() const { return curLine_; }
    void restoreTestLine(int l) { curLine_ = l; }
    std::string srcFile_;             // source file path as invoked ($*PROGRAM-NAME)
    std::string srcFileAbs_;          // absolute source file path ($?FILE)
    std::string curDeclFile_;         // file whose top level is executing (module load switches it)
    // the file a routine declared NOW should record (backtrace .file)
    std::string curDeclFile() const {
        return !curDeclFile_.empty() ? curDeclFile_
             : (srcFileAbs_.empty() ? srcFile_ : srcFileAbs_);
    }
    Value captureBacktrace();         // innermost-first BacktraceFrame list (Exception.throw)
    // %?RESOURCES for the module currently being loaded (dist resource files);
    // a stack because module loads nest (a module can `use` another).
    std::vector<Value> resourceStack_;
    std::vector<Value> distStack_; // $?DISTRIBUTION of the module being compiled
    // run-script executes an installed script via a NESTED run(); the wrapper's
    // own &MAIN is still in scope there, and auto-dispatching it again would
    // recurse forever when the script defines no MAIN of its own. This holds
    // the &MAIN visible at run-script entry; run() refuses to auto-invoke it.
    const Value* inheritedMainBarrier_ = nullptr;
    // `module Zef:ver(…):auth(…):api(…)` — a PACKAGE has no ClassInfo, so its name
    // adverbs live here (name → ver/auth/api) for .^ver/.^auth/.^api to answer.
    struct PkgMeta { std::string ver, auth, api; };
    std::map<std::string, PkgMeta> pkgMeta_;
    Value buildResourceMap(const std::string& repo, const std::string& distId); // dist files → resource Hash
    Value buildSourceResourceMap(const std::string& distRoot); // source checkout META6 `resources` → resource Hash
    Value buildDistribution(const std::string& distRoot);      // source checkout META6 → $?DISTRIBUTION
    Value buildInstalledDistribution(const std::string& repo, const std::string& distId); // CURI dist/<id> meta → $?DISTRIBUTION
    std::string mainUsage();          // Rakudo-format usage text from &MAIN ($*USAGE)
    Value bufBitOp(Value& buf, const std::string& m, ValueList& args); // Buf read/write-(u)bits/-num/-int
    Value bufSplice(Value& buf, ValueList& args); // Buf.splice — mutates in place, answers the removed bytes
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
    // Source line of the statement currently executing (test diagnostics).
    // Written on EVERY statement, so a thread_local costs a TLV lookup per
    // statement on macOS — measured +6% on loopsum. A relaxed atomic member
    // is a plain store, defined under concurrency, and TSan-clean; the value
    // being process-wide is the same arbitrariness diagnostics always had.
    struct RelaxedLine {
        std::atomic<int> v{0};
        void operator=(int x) { v.store(x, std::memory_order_relaxed); }
        operator int() const { return v.load(std::memory_order_relaxed); }
    } curLine_;
    int todoRemaining_ = 0;  // number of upcoming tests marked TODO by a bare `todo` statement
    std::string todoReason_; // reason for the pending TODO block
    int dieOnFail_ = -1;     // cached RAKU_TEST_DIE_ON_FAIL flag (-1 = not yet read)
    int todoSubtestDepth_ = 0; // inside a TODO-marked subtest: failures neither die nor count
    // `&builtin` must answer the SAME Callable every time, so `&dir.wrap({…})`
    // mutates the object the call path consults (File::Find's suite mocks `dir`
    // exactly this way). Populated lazily; empty for programs that never take a
    // builtin reference, which keeps the call path's check free.
    std::map<std::string, Value> builtinRefs_;
    std::vector<std::pair<Block*, std::shared_ptr<Env>>> deferredEnds_; // END blocks from EVAL, run at program end
    const Value* envLookup(const std::string& name); // %*ENV<name>, or null
    bool envFlag(const std::string& name); // truthiness of %*ENV<name>
    std::string envStr(const std::string& name); // %*ENV<name> as a string
    std::string exceptionToJson(const Value& ex); // RAKU_EXCEPTIONS_HANDLER=JSON serialization
    void maybeDieOnFail();   // if RAKU_TEST_DIE_ON_FAIL, stop the suite after a real failure

    void emitTest(bool ok, const std::string& desc, const std::string& directive = "",
                  const std::string& extraDiag = "");

public:
    // DESTROY protocol. Timely destruction is not guaranteed in Raku — DESTROY
    // runs when garbage collection notices the object is unreachable. Here the
    // construction protocol parks every new instance whose class chain declares
    // a DESTROY submethod in this registry; a sweep finds entries the registry
    // alone still owns (use_count == 1 ⇔ the program dropped every reference)
    // and runs their DESTROY chain, child class first. Sweeps run from
    // `$*VM.request-garbage-collection` and at program end — the two moments
    // Rakudo's own test suite relies on.
    void maybeRegisterDestroy(const Value& self);
    void runPendingDestroys();

private:
    std::vector<std::shared_ptr<ObjectData>> destroyReg_;
    std::mutex destroyMu_;
    bool inDestroySweep_ = false; // a DESTROY that itself requests GC must not recurse
    // Allocation pressure: registration past this mark triggers a sweep, so a
    // construct-and-drop loop cannot pile up millions of corpses for one giant
    // end-of-program sweep (roles-6e.t spins C1.new for five seconds straight).
    // After each sweep the mark doubles past the surviving live entries, which
    // keeps the O(live) scan amortized-constant per registration.
    size_t destroySweepAt_ = 1024;

    std::unordered_map<std::string, BuiltinFn> builtins_;
    std::vector<std::shared_ptr<Program>> keptPrograms_; // keep EVAL'd ASTs alive
    // The compilation unit currently EXECUTING its top level (mainline, a
    // module load, an EVAL/REPL line) — consulted by the bare-name fallback
    // for Program::declaredTypeNames. Pushed/popped at unit boundaries only.
    std::vector<const Program*> unitStack_;
    void registerBuiltins();

    // asInvocant: we only need this slot to reach INTO the value (an invocant or a
    // subscript base), not to overwrite it — so a read-only attribute is fine
    // (`$obj.ro-attr.inner = v` mutates what ro-attr points at; `$obj.ro-attr = v` still dies).
    Value* lvalue(Expr* e, bool asInvocant = false);
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
    static long long tzOffsetDyn(); // $*TZ resolution: user dynamic, else system offset
    double initInstant_ = 0; // process start time ($*INIT-INSTANT)
    // $*INIT-INSTANT is ONE Instant read many times, and an Instant is a
    // reference type — so every read has to hand back the SAME identity token,
    // or `$*INIT-INSTANT === $*INIT-INSTANT` would be False. Both reader sites
    // go through here.
    std::shared_ptr<void> initInstantId_ = std::make_shared<char>();
    Value initInstantVal() {
        Value v = Value::number(initInstant_);
        v.hashKind = "Instant";
        v.extM() = initInstantId_;
        return v;
    }
    Value defaultScheduler_; // the ONE $*SCHEDULER (copies share .hash(), so attr writes persist)
private:
    Value evalCall(Call* c);
    Value evalTempLet(Call* c); // temp/let: snapshot BEFORE arg evaluation
    Value evalIndex(Index* idx);
    // Concrete index tuples for a multi-dimensional subscript — shared by the
    // read path (evalIndex) and the write path (evalAssignInner).
    std::vector<ValueList> expandDimTuples(const Value& root, const ValueList& keys);
    // Does a `{…}` subscript name MANY keys? See the definition in Interpreter.cpp.
    static bool keySubscriptIsSlice(const Expr* ixExpr, const Value& iv);
    Value evalInterp(InterpStr* s);

    Value makeClosure(BlockExpr* be);
    void bindParams(const std::vector<Param>& params, ValueList& args, std::shared_ptr<Env>& env,
                    bool methodCtx = false, // methods have an implicit *%_ (extra nameds ignored)
                    // A BLOCK's untyped parameter is Mu-constrained; a Routine's
                    // is Any-constrained, so only a routine refuses Mu. Rakudo:
                    // `sub f($x) {…}; f(Mu)` dies, `(-> $x {…})(Mu)` does not.
                    bool blockParams = false);
};

// helpers
Value listToArray(const ValueList& items);
Value applyArith(const std::string& op, const Value& l, const Value& r); // binary op dispatch (also used by codegen)

// -O fast-path binary ops for native codegen: inline the small-int (non-bignum)
// case as native int64, else fall back to the general applyArith. Semantics are
// identical — this only skips the string dispatch + boxing on the hot Int path.
inline bool rtBothInt(const Value& l, const Value& r) { return l.t == VT::Int && r.t == VT::Int && !l.big() && !r.big(); }
// -O int lanes (Codegen pass 3): may this box be read as / stored into a raw int64?
// A store additionally requires no enum identity (writing .i under an enumName
// would leave the stale name as the value's stringification).
inline bool rtIntBox(const Value& v)  { return v.t == VT::Int && !v.big(); }
// --exe `is rw` params: bind a reference into the (caller-visible) ValueList slot.
inline Value& rtPosRef(ValueList& a, size_t i) { if (a.size() <= i) a.resize(i + 1); return a[i]; }
inline bool rtIntSlot(const Value& v) { return v.t == VT::Int && !v.big() && v.enumName.empty(); }
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
// Cached-builtin call for native codegen: the per-name pointer is resolved once
// at program start (__rakupp_register → Interpreter::builtinPtr); a null (not a
// registered builtin — e.g. a module-loaded routine) falls back to the full
// by-name callBuiltin path, so semantics are identical.
inline Value rtCallB(Interpreter& I, const BuiltinFn* f, const char* name, ValueList args) {
    if (f) return (*f)(I, args);
    return I.callBuiltin(name, std::move(args));
}
// True named builtins (-O direct calls): a small set of hot, pure, fixed-arity
// builtins get a real C++ function — no ValueList, no lambda, and for the
// inline ones no call at all on the hot path. The slow paths preserve the
// exact original semantics (abs delegates back to methodCall so augment /
// user objects / junctions keep working; the inline abs is additionally
// disabled while any `augment` is live).
Value rtBAbsSlow(Interpreter& I, const Value& v);  // full abs (Builtins.cpp)
Value rtBChr(Interpreter& I, const Value& v);      // chr: codepoint → Str (Builtins.cpp)
Value rtBOrd(Interpreter& I, const Value& v);      // ord: Str → first codepoint (Builtins.cpp)
inline Value rtBAbs(Interpreter& I, const Value& v) {
    if (v.t == VT::Int && !v.big() && I.builtinExt_.empty())
        return Value::integer(v.i < 0 ? -v.i : v.i);   // plain-Int hot path, inlined at the call site
    return rtBAbsSlow(I, v);
}
// The sweep: every named builtin mirrors its sub form EXACTLY (each body is the
// old registered lambda's 1-arg case, verbatim). Delegators keep full
// methodCall semantics (augment, objects, junctions); the two with bypassing
// fast paths (abs above, sign below) are augment-guarded via builtinExt_.
Value rtBSay(Interpreter& I, const Value& v);      // say/print/put/note, 1-arg forms
Value rtBPrint(Interpreter& I, const Value& v);
Value rtBPut(Interpreter& I, const Value& v);
Value rtBNote(Interpreter& I, const Value& v);
Value rtBUc(Interpreter& I, const Value& v);       // Str case-mapping (Builtins.cpp statics)
Value rtBLc(Interpreter& I, const Value& v);
Value rtBChars(Interpreter& I, const Value& v);    // grapheme count
Value rtBSqrt(Interpreter& I, const Value& v);     // Complex/Object escapes + langRev negatives
Value rtBSignSlow(Interpreter& I, const Value& v); // full sign via methodCall
Value rtBTruncate(Interpreter& I, const Value& v); // delegators: exact sub behavior
Value rtBIsPrime(Interpreter& I, const Value& v);
Value rtBFlip(Interpreter& I, const Value& v);
Value rtBTrim(Interpreter& I, const Value& v);
Value rtBChomp(Interpreter& I, const Value& v);
Value rtBChop(Interpreter& I, const Value& v);
Value rtBSin(Interpreter& I, const Value& v);      // trig family: Complex/Object → method,
Value rtBCos(Interpreter& I, const Value& v);      // else std:: math on toNum (== numArg)
Value rtBTan(Interpreter& I, const Value& v);
Value rtBAsin(Interpreter& I, const Value& v);
Value rtBAcos(Interpreter& I, const Value& v);
Value rtBAtan(Interpreter& I, const Value& v);
Value rtBSinh(Interpreter& I, const Value& v);
Value rtBCosh(Interpreter& I, const Value& v);
Value rtBTanh(Interpreter& I, const Value& v);
Value rtBAsinh(Interpreter& I, const Value& v);
Value rtBAcosh(Interpreter& I, const Value& v);
Value rtBAtanh(Interpreter& I, const Value& v);
inline Value rtBSign(Interpreter& I, const Value& v) {
    if (v.t == VT::Int && !v.big() && I.builtinExt_.empty())
        return Value::integer(v.i < 0 ? -1 : v.i > 0 ? 1 : 0);
    return rtBSignSlow(I, v);
}
// Pure mirrors of the sub forms (deliberately including their double-precision
// behavior — the SUB form is what call sites hit today, not the exact
// bignum/Rat method forms).
inline Value rtBFloor(Interpreter&, const Value& v)   { return Value::integer((long long)std::floor(v.toNum())); }
inline Value rtBCeiling(Interpreter&, const Value& v) { return Value::integer((long long)std::ceil(v.toNum())); }
inline Value rtBRound(Interpreter&, const Value& v)   { return Value::integer((long long)std::llround(v.toNum())); }
// log / log10 / log2 of a NEGATIVE real. Before 6.e the answer is NaN; from 6.e
// on it is the complex logarithm, the same widening 6.e gave sqrt — ln|x| + iπ,
// divided by ln(base) when there is one. Written once here because all three
// entry points (method, sub, and the codegen runtime) come through it.
inline Value rtLogReal(Interpreter& I, double x, double base) {
    auto ln = [&](double v) { return base == 0.0 ? std::log(v) : std::log(v) / std::log(base); };
    if (x < 0 && I.sixE()) {
        std::complex<double> r = std::log(std::complex<double>(x, 0.0));
        if (base != 0.0) r /= std::log(base);
        return Value::complex(r.real(), r.imag());
    }
    return Value::number(ln(x));
}
inline Value rtBLog10(Interpreter& I, const Value& v) { return rtLogReal(I, v.toNum(), 10.0); }
inline Value rtBLog2(Interpreter& I, const Value& v)  { return rtLogReal(I, v.toNum(), 2.0); }
inline Value rtBExp(Interpreter& I, const Value& v) {
    if (v.t == VT::Complex || v.t == VT::Object) { ValueList none; return I.methodCall(v, "exp", none); }
    return Value::number(std::exp(v.toNum()));
}
inline Value rtBLog(Interpreter& I, const Value& v) {
    if (v.t == VT::Complex) { ValueList none; return I.methodCall(v, "log", none); }
    return rtLogReal(I, v.toNum(), 0.0);
}
// Fast-path STRING comparisons: two PLAIN Strs (no Version/IO/Buf hashKind tag,
// no enum identity) compare byte-wise — exactly what applyArith's tail does for
// them (a plain Str's toStr() is its `s`). Anything else falls back to the full
// operator chain (Version part-compare, enum stringification, junction
// autothreading, Whatever-currying, numeric coercions).
inline bool rtPlainStr(const Value& v) { return v.t == VT::Str && v.hashKind.empty() && v.enumName.empty(); }
inline Value rtEqS(const Value& l, const Value& r) { if (rtPlainStr(l) && rtPlainStr(r)) return Value::boolean(l.s == r.s); return applyArith("eq", l, r); }
inline Value rtNeS(const Value& l, const Value& r) { if (rtPlainStr(l) && rtPlainStr(r)) return Value::boolean(l.s != r.s); return applyArith("ne", l, r); }
inline Value rtLtS(const Value& l, const Value& r) { if (rtPlainStr(l) && rtPlainStr(r)) return Value::boolean(l.s <  r.s); return applyArith("lt", l, r); }
inline Value rtGtS(const Value& l, const Value& r) { if (rtPlainStr(l) && rtPlainStr(r)) return Value::boolean(l.s >  r.s); return applyArith("gt", l, r); }
inline Value rtLeS(const Value& l, const Value& r) { if (rtPlainStr(l) && rtPlainStr(r)) return Value::boolean(l.s <= r.s); return applyArith("le", l, r); }
inline Value rtGeS(const Value& l, const Value& r) { if (rtPlainStr(l) && rtPlainStr(r)) return Value::boolean(l.s >= r.s); return applyArith("ge", l, r); }
// Bool-context variants (if/while conditions): skip building the Bool Value.
inline bool rtEqSB(const Value& l, const Value& r) { if (rtPlainStr(l) && rtPlainStr(r)) return l.s == r.s; return applyArith("eq", l, r).truthy(); }
inline bool rtNeSB(const Value& l, const Value& r) { if (rtPlainStr(l) && rtPlainStr(r)) return l.s != r.s; return applyArith("ne", l, r).truthy(); }
inline bool rtLtSB(const Value& l, const Value& r) { if (rtPlainStr(l) && rtPlainStr(r)) return l.s <  r.s; return applyArith("lt", l, r).truthy(); }
inline bool rtGtSB(const Value& l, const Value& r) { if (rtPlainStr(l) && rtPlainStr(r)) return l.s >  r.s; return applyArith("gt", l, r).truthy(); }
inline bool rtLeSB(const Value& l, const Value& r) { if (rtPlainStr(l) && rtPlainStr(r)) return l.s <= r.s; return applyArith("le", l, r).truthy(); }
inline bool rtGeSB(const Value& l, const Value& r) { if (rtPlainStr(l) && rtPlainStr(r)) return l.s >= r.s; return applyArith("ge", l, r).truthy(); }
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
std::vector<std::string> collectAttrRefs(const std::vector<StmtPtr>& body); // $!x/@!x/%!x references in a body
std::string firstBlockPlaceholder(const std::vector<StmtPtr>& body); // first $^/$:/@_ in a signature-less body
void collectPHExprPublic(const Expr* e, std::set<std::string>& out); // expr-level placeholder walk
ValueList pathPartsPairs(const Value& v); // IO::Path::Parts in declaration order
Value  rtTypedDefault(const char* type, char sigil); // `my Int @a` / `my %h{Int}`: the declared empty container
long long nowMicros();              // steady_clock microseconds (the gather probe budget)
Value  rtArrayVal(const Value& v);  // list-assignment semantics for `@a = expr` (splice Lists, keep itemized rows)
Value  rtArrayVal(Value&& v);       // rvalue overload: steal a uniquely-owned Slip-free List's buffer instead of copying it
Value  rtShapedArray(const ValueList& dims, const std::string& declType); // `my @a[3;2]`
void   rtShapedStore(Value& lv, const Value& rhs, const std::string& keepType); // `@a[3;2] = …`
void   rtSpreadArg(ValueList& as, const Value& v, bool argPos); // |x spread into an arg/list being built
Value  rtHyperMethod(Interpreter& I, const Value& inv, const std::string& m, ValueList args); // >>.method
Value  rtSlipVal(const Value& v);   // |x as a list element (a List that splices, pre-spread deep)
Value  rtSlipShallow(const Value& v); // |x in value position (one-level splice marker)
Value  rtSpliceIfList(const Value& v); // [..] item: a List value splices one level
Value  rtOneArgItem(const Value& v);   // [..] one-arg rule: single list-valued item spreads
Value  rtHyperItem(const Value& v);    // [..] hyper item: stays one element, isList cleared
inline Value rtMarkList(Value v) { v.isList = true; return v; } // word-lists are flattening Lists
Value  rtHashLit(const ValueList& items); // { k => v, … } hash constructor
Value  rtNamedPair(const std::string& k, Value v); // k => v as a NAMED call argument
size_t rtPosCount(const ValueList& a, size_t from = 0); // positional-arg count (named pairs excluded)
Value  rtThrowNext(const std::string& label = ""); Value rtThrowLast(const std::string& label = "");
Value  rtThrowRedo(const std::string& label = ""); // expression-position / labelled loop control
Value  rtIndexAdverb(Value& base, const Value& keyIn, bool isHash, const std::string& adverb); // :exists/:delete/…
Value  rtSliceFrom(const Value& base, long long from, bool exFrom); // @a[$i .. *] tail slice
Value  rtRangeVal(const Value& from, const Value& to, bool exFrom, bool exTo); // from..to (string ranges too)
ValueList rtMainArgs(const std::vector<std::string>& argv, bool namedAnywhere = false); // argv -> MAIN args (--opt named, rest positional)
Value& rtIndexRef(Value& base, const Value& key, bool isHash);
Value  rtReduce(const std::string& op, const Value& list);  // [+] / [*] / … reduction metaop
// Endless operands — an infinite Range (1..Inf / 1..*, which carries the
// ±LLONG_MAX sentinel in its integer endpoints) or a lazy list with no end.
bool isEndlessRange(const Value& v);
bool isEndlessLazy(const Value& v);
// The limit of an endless Range's arithmetic series, which is what `.sum`
// answers: ±Inf, or NaN when the range runs from -Inf all the way to +Inf.
Value endlessRangeSum(const Value& v);
// A reduce over an endless operand: hands back the answer for the operators
// that have one without the elements, and otherwise throws X::Cannot::Lazy.
// Returns false when `v` is not endless at all (the caller folds as usual).
bool endlessReduce(const std::string& op, const Value& v, Value& out);
Value  rtNqpOp(NqpOpc op, ValueList& args);                 // eager `use nqp` leaf ops (interp + codegen)
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
