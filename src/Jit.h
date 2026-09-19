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
#include <utility>
#include <vector>

namespace rakupp {

struct Stmt;
struct Env;
class Interpreter;

namespace jit {

// ---- the switch ------------------------------------------------------------

// Which machine-code backend a hot loop goes through.
//
//   Cxx — emit C++ with the `--exe` Codegen, run a compiler, dlopen the result.
//         General, and it needs a C++ compiler on the box plus a disk cache to
//         make the SECOND run of a program fast.
//   Cnp — copy-and-patch: stitch pre-compiled machine-code snippets that were
//         built when rakupp was. Narrower, but it needs no compiler and no
//         disk, and it makes the FIRST run fast. docs/dev/plans/CNP-PLAN.md.
enum class Backend { Cxx, Cnp };

struct Options {
    bool on = false;
    Backend backend = Backend::Cxx;
    bool sync = false;        // compile on the interpreter thread (deterministic gates)
    bool verbose = false;     // narrate decisions to stderr
    bool stats = false;       // one summary line at exit
    bool cache = true;        // read/write ~/.cache/rakupp/jit
    bool pch = false;         // build a precompiled header (31 MB, halves each compile)
    unsigned threshold = 1000;// iterations before a loop is considered hot
};

// Parse a `--jit[=SPEC]` spec (comma-separated words). Returns "" on success or
// a human-readable error naming what exists.
std::string parseSpec(const std::string& spec, Options& out);
// The same for `--cnp[=SPEC]`. Selects the copy-and-patch backend and takes the
// words that mean something to it; the ones that only describe a compiler and a
// cache (`sync`, `nocache`, `pch`) are not offered, because it has neither.
std::string parseCnpSpec(const std::string& spec, Options& out);

// Install the options. Called once, from main, before the program runs.
// `cxx` is the C++ compiler to build kernels with and `inc` the directory
// holding the runtime headers — the same two main.cpp resolves for `--exe`.
// Either one empty turns the JIT off with a message; `selfExe` goes into the
// kernel cache key so that any rebuild of rakupp invalidates it. The
// copy-and-patch backend uses none of the three: it asks only whether this
// build carries a stencil table.
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

// `--jit-info` / `--jit-clean`. Neither needs configure() to have run: they are
// about the directory on disk, not about a run. `clean` returns (files, bytes).
std::string cacheDir();
void info();
std::pair<unsigned long long, unsigned long long> clean();

}  // namespace jit

// Turn the copy-and-patch backend on inside a bundled binary. See Jit.cpp.
void rakuppCnpBundled(bool on, const std::string& selfExe);

}  // namespace rakupp
