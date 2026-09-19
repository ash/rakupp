#pragma once
// The tier-up JIT (docs/dev/plans/JIT-PLAN.md).
//
// A hot loop is emitted as C++ by the SAME Codegen the `--exe` backend uses,
// compiled to a shared object in the background, dlopen'd into the running
// process, and entered at an iteration boundary. The kernel calls the host's
// own runtime (applyArith, rtAdd, …) — it is linked with undefined symbols and
// bound to the executable at load time, so compiled and interpreted code cannot
// disagree about semantics.
//
// EVERYTHING here is behind `--jit`. `jit::on()` is false unless the flag was
// given, and every call site in the interpreter tests it first, so a default run
// reaches none of this code and pays one never-taken branch per loop iteration.
#include <string>
#include <vector>

namespace rakupp {

struct Stmt;
struct Env;
class Interpreter;

namespace jit {

// ---- the switch ------------------------------------------------------------

struct Options {
    bool on = false;
    bool sync = false;        // compile on the interpreter thread (deterministic gates)
    bool verbose = false;     // narrate decisions to stderr
    bool stats = false;       // one summary line at exit
    bool cache = true;        // read/write ~/.cache/rakupp/jit
    unsigned threshold = 1000;// iterations before a loop is considered hot
};

// Parse a `--jit[=SPEC]` spec (comma-separated words). Returns "" on success or
// a human-readable error naming what exists.
std::string parseSpec(const std::string& spec, Options& out);

// Install the options. Called once, from main, before the program runs.
// `cxx` is the C++ compiler to build kernels with and `inc` the directory
// holding the runtime headers — the same two main.cpp resolves for `--exe`.
// Either one empty turns the JIT off with a message; `selfExe` goes into the
// kernel cache key so that any rebuild of rakupp invalidates it.
void configure(const Options& o, const std::string& cxx, const std::string& inc,
               const std::string& selfExe);

// The one gate every hot-path call site tests first.
bool onSlow();
extern bool g_on;
inline bool on() { return g_on; }

// ---- per-loop state --------------------------------------------------------
//
// A Site is created on a candidate loop's first execution and lives as long as
// the process (the manager is deliberately leaked, so a compile thread that
// finishes during shutdown cannot race a destructor). `Stmt::jitSite` holds it.

struct Site;

// The site for this loop node, or null when the loop is not a candidate. The
// eligibility walk runs once per node; the answer is remembered on the site.
Site* siteFor(Stmt* loop);

// Entering / leaving a candidate loop, so a tripped counter can attribute the
// tier-up to the OUTERMOST candidate currently running (in a nest, the inner
// loop gets hot and the outer one is worth compiling).
void pushLoop(Site* s);
void popLoop(Site* s);

// The RAII form the loop runners use. A null site is the whole of the default
// run: construction and destruction are then two predictable branches, and the
// `site` member is the flag the runner tests. A loop can also leave through a
// `die`, so the pop has to be a destructor rather than a statement.
struct LoopGuard {
    Site* site;
    explicit LoopGuard(Site* s) : site(s) { if (site) pushLoop(site); }
    ~LoopGuard() { if (site) popLoop(site); }
    LoopGuard(const LoopGuard&) = delete;
    LoopGuard& operator=(const LoopGuard&) = delete;
};

// One iteration went by. Trips the threshold and requests a compile.
void tick(Site* s);

// A kernel is published for this site: bind its slots against `env` and run it.
// Returns true when the kernel ran the loop to completion and the interpreter
// should stop iterating. False means "not ready" or "cannot bind here" — the
// interpreter simply carries on.
bool runIfReady(Site* s, Interpreter& I, Env* env);

// Print the `--jit=stats` summary. Called at exit.
void report();

}  // namespace jit
}  // namespace rakupp
