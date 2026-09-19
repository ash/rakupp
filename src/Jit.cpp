// The tier-up JIT — docs/dev/plans/JIT-PLAN.md.
//
// Off unless `--jit` was given. `jit::g_on` is the single gate; every call site
// in the interpreter tests `jit::on()` before it reaches anything here, so a
// default run pays one never-taken branch per loop iteration and touches none
// of this code.
//
// The pipeline, once a loop proves hot:
//
//   eligibility walk  ->  Codegen::emitJitKernel  ->  c++ -shared  ->  dlopen
//
// The kernel is linked with UNDEFINED runtime symbols and bound to the running
// executable at load time, so `rtAdd`, `applyArith` and every other runtime
// entry it calls are the host's own. A tiered loop and an interpreted one
// therefore cannot disagree about semantics: they execute the same machine code
// for everything but the loop's own control flow.
#include "Jit.h"
#include "Ast.h"
#include "Codegen.h"
#include "Interpreter.h"
#include "Platform.h"   // dlopen/dlsym and their Win32 shims

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <sys/stat.h>
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <mutex>
#include <utility>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace rakupp {
namespace jit {

bool g_on = false;
bool onSlow() { return g_on; }

namespace {

// What a loadable module is called here. The kernel is dlopen'd by the process
// that compiled it, so only this build's own convention matters.
#if defined(_WIN32)
constexpr const char* kJitSoExt = ".dll";
#elif defined(__APPLE__)
constexpr const char* kJitSoExt = ".dylib";
#else
constexpr const char* kJitSoExt = ".so";
#endif

Options g_opt;
std::string g_cxx, g_inc, g_self;  // filled by configure(); empty cxx/inc = JIT off
bool g_clang = false;          // the PCH lane is clang-only
std::mutex g_mu;               // guards the site list, the stats and the PCH build
std::atomic<int> g_inFlight{0};

// ---- statistics (--jit=stats) ---------------------------------------------
std::atomic<unsigned> g_examined{0}, g_eligible{0}, g_compiled{0},
                      g_cacheHits{0}, g_failed{0}, g_entered{0};

void note(const std::string& s) {
    if (g_opt.verbose) std::cerr << "[jit] " << s << "\n";
}

// ---- the kernel ------------------------------------------------------------

using KernelFn = int (*)(Interpreter*, Value**);

enum State : int { StNew = 0, StEligible = 1, StCompiling = 2, StReady = 3,
                   StIneligible = -1, StFailed = -2 };

}  // namespace

struct Site {
    Stmt* loop = nullptr;
    std::atomic<unsigned> count{0};
    std::atomic<int> state{StNew};
    std::atomic<KernelFn> fn{nullptr};
    std::atomic<bool> requested{false};
    std::vector<std::string> slots;   // written before state becomes Compiling; read-only after
    std::vector<bool> slotWritten;    // parallel to slots: does the kernel STORE to it?
    std::string why;                  // ineligibility reason, for --jit=verbose
    std::string src;                  // the emitted TU (also the cache key's material)
    std::string fnName;               // the kernel's extern "C" entry point
};

namespace {

std::vector<Site*>& siteList() { static std::vector<Site*> v; return v; }
// The candidate loops currently executing on THIS thread, outermost first. A
// tripped counter attributes the tier-up to the outermost eligible entry: in a
// nest it is the inner loop that gets hot and the outer one that is worth
// compiling, and an outer kernel swallows every loop inside it.
thread_local std::vector<Site*> t_stack;

// ---- the whitelist ---------------------------------------------------------
//
// Deliberately far narrower than what Codegen accepts. It is not a statement
// about what the technique can do: it is the smallest set that makes a kernel
// PROVABLY unable to reach interpreter state while it runs, which is what
// "cannot break the interpreter" has to mean. Above all it admits no calls of
// any kind, and that single rule is what pins the slot pointers — with nothing
// in the kernel able to insert into a scope it did not create, a `Value*` taken
// at entry stays valid for the whole loop. Every later widening has to answer
// that question again.
const std::set<std::string>& binOps() {
    static const std::set<std::string> s = {
        "+", "-", "*", "/", "%", "**", "div", "mod", "%%", "gcd", "lcm",
        "<", "<=", ">", ">=", "==", "!=", "<=>",
        "eq", "ne", "lt", "gt", "le", "ge", "leg", "cmp",
        "~", "x", "&&", "||", "//", "and", "or", "^^", "xor",
        "+&", "+|", "+^", "+<", "+>", "min", "max",
    };
    return s;
}
const std::set<std::string>& unOps() {
    static const std::set<std::string> s = { "-", "+", "!", "?", "~", "++", "--", "not", "+^" };
    return s;
}
const std::set<std::string>& asgOps() {
    static const std::set<std::string> s = {
        "=", "+=", "-=", "*=", "/=", "%=", "**=", "~=", "x=",
        "//=", "||=", "&&=", "min=", "max=", "div=", "mod=",
        "+&=", "+|=", "+^=", "+<=", "+>=",
    };
    return s;
}

// A plain lexical scalar: `$name`, no twigil, not `$_`, not a special.
bool plainScalar(const std::string& n) {
    if (n.size() < 2 || n[0] != '$') return false;
    char c = n[1];
    if (!(std::isalpha((unsigned char)c) || c == '_')) return false;  // excludes $*x $!x $.x $?x $/ $0
    if (n == "$_") return false;                                      // the topic is frame state, not a lexical
    return n.find("::") == std::string::npos;
}

struct Scan {
    std::string err;
    // How a `my` in the OUTERMOST loop's header is treated. A kernel is entered
    // at an iteration boundary, which for a C-style `loop (my $i = 0; …)` is
    // AFTER the interpreter has run the init and declared `$i` in the enclosing
    // frame — so `$i` is a slot the kernel writes through, never a local it
    // redeclares. `declIsSlot` is set only while that one expression is walked.
    bool declIsSlot = false;
    // Anywhere else in a loop header a declaration would give the kernel a local
    // that shadows a variable the interpreter has already made, leaving a stale
    // value behind after the loop. Refused instead.
    bool declRefused = false;
    std::vector<std::set<std::string>> scopes;  // declared names, innermost last
    std::set<std::string> seen;                 // slot names already recorded
    std::vector<std::string> slots;             // slot names, in first-reference order
    // Names the kernel ASSIGNS to. A slot the kernel only READS needs none of
    // the container guards at entry: a native width, a readonly binding, a
    // coercion and an `is rw` write-through all describe what happens when
    // something is STORED, and nothing is stored. Reading `$n` from a readonly
    // parameter is the single most common shape there is — `sub f($n) { while
    // $i < $n {…} }` — and refusing it cost every loop in a routine.
    std::set<std::string> written;

    void fail(const std::string& m) { if (err.empty()) err = m; }
    bool declaredHere(const std::string& n) const {
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it)
            if (it->count(n)) return true;
        return false;
    }
    void useName(const std::string& n) {
        if (declaredHere(n)) return;            // a local of the kernel
        if (seen.insert(n).second) slots.push_back(n);
    }
    void expr(Expr* e);
    void headerExpr(Expr* e);
    void stmt(Stmt* s);
    void block(Block* b, bool ownScope);
};

void Scan::expr(Expr* e) {
    if (!err.empty()) return;
    if (!e) return;
    switch (e->kind) {
        case NK::IntLit: case NK::NumLit: case NK::BoolLit: return;
        case NK::StrLit: return;
        case NK::InterpStr: {
            // `"even"` is an InterpStr, not a StrLit — every double-quoted
            // string in Raku is an interpolation that happens to have nothing
            // to interpolate. One with a variable or an expression in it would
            // stringify at run time, which can reach a user-written .Str, so
            // only the all-literal case is taken.
            for (auto& part : static_cast<InterpStr*>(e)->parts)
                if (!part || part->kind != NK::StrLit) { fail("an interpolated string"); return; }
            return;
        }
        case NK::VarExpr: {
            auto* v = static_cast<VarExpr*>(e);
            if (!plainScalar(v->name)) { fail("variable " + v->name); return; }
            if (v->declare && declRefused) { fail("a declaration in this loop's header"); return; }
            if (v->declare && declIsSlot) { useName(v->name); return; }
            if (v->declare) {
                // Anything beyond a bare `my $x` changes what an assignment
                // means (a type check, a coercion, a container trait, `our`/
                // `state` storage), and the kernel writes the container
                // directly. Refuse the lot.
                if (v->declScope != "my" && !v->declScope.empty()) { fail("a " + v->declScope + " declaration"); return; }
                if (!v->declType.empty() || !v->declCoerce.empty() || v->declDefault ||
                    v->declDynamic || v->declExport || !v->containerIs.empty() ||
                    v->declShape || v->declTypeExpr || v->pkgSymbol || v->viaPseudoPkg) {
                    fail("a declaration with a type or trait"); return;
                }
                if (scopes.empty()) { fail("a declaration outside any scope"); return; }
                scopes.back().insert(v->name);
                return;
            }
            useName(v->name);
            return;
        }
        case NK::Assign: {
            auto* a = static_cast<Assign*>(e);
            if (!asgOps().count(a->op)) { fail("assignment operator '" + a->op + "'"); return; }
            if (a->containerSigil) { fail("a container-sigil assignment"); return; }
            if (a->target->kind == NK::VarExpr) written.insert(static_cast<VarExpr*>(a->target.get())->name);
            // The VALUE is scanned first, so that `my $x = $x` records the OUTER
            // `$x` as a slot before the declaration shadows it — which is the
            // order Raku evaluates them in and the one the emitter assumes.
            expr(a->value.get());
            if (a->target->kind != NK::VarExpr) { fail("assignment to a non-variable"); return; }
            expr(a->target.get());
            return;
        }
        case NK::Binary: {
            auto* b = static_cast<Binary*>(e);
            if (!binOps().count(b->op)) { fail("operator '" + b->op + "'"); return; }
            expr(b->lhs.get()); expr(b->rhs.get());
            return;
        }
        case NK::Unary: {
            auto* u = static_cast<Unary*>(e);
            if (!unOps().count(u->op)) { fail("operator '" + u->op + "'"); return; }
            if (u->op == "++" || u->op == "--") {
                if (u->operand->kind != NK::VarExpr) { fail("++/-- on a non-variable"); return; }
                written.insert(static_cast<VarExpr*>(u->operand.get())->name);
            }
            expr(u->operand.get());
            return;
        }
        case NK::Ternary: {
            auto* t = static_cast<Ternary*>(e);
            expr(t->cond.get()); expr(t->then.get()); expr(t->els.get());
            return;
        }
        default: fail("an expression the kernel whitelist does not cover"); return;
    }
}

// A C-style loop's init and step, where a COMMA is a sequence of side effects
// rather than a list value: `loop ($r = $c, $i = $C, $k = 0; …; …)`. The list
// itself is discarded in both positions, so what has to be whitelisted is each
// element, and nothing here admits a list into a scalar slot — a `ListExpr`
// anywhere else is still refused. This is the shape the Parrot-descended
// Mandelbrot in examples/ is written in, and it was the only thing standing
// between its innermost loop (where nearly all of its time goes) and a kernel.
void Scan::headerExpr(Expr* e) {
    if (!err.empty() || !e) return;
    if (e->kind == NK::ListExpr) {
        for (auto& it : static_cast<ListExpr*>(e)->items) { expr(it.get()); if (!err.empty()) return; }
        return;
    }
    expr(e);
}

void Scan::block(Block* b, bool ownScope) {
    if (!b) return;
    if (!b->phaser.empty() || b->isCatch) { fail("a phaser or CATCH block"); return; }
    if (ownScope) scopes.push_back({});
    for (auto& s : b->stmts) { stmt(s.get()); if (!err.empty()) break; }
    if (ownScope) scopes.pop_back();
}

void Scan::stmt(Stmt* s) {
    if (!err.empty()) return;
    if (!s) return;
    switch (s->kind) {
        case NK::EmptyStmt: return;
        case NK::ExprStmt: expr(static_cast<ExprStmt*>(s)->e.get()); return;
        case NK::LastStmt:
            if (!static_cast<LastStmt*>(s)->target.empty()) fail("a labelled `last`");
            return;
        case NK::NextStmt:
            if (!static_cast<NextStmt*>(s)->target.empty()) fail("a labelled `next`");
            return;
        case NK::IfStmt: {
            auto* f = static_cast<IfStmt*>(s);
            if (!f->thenVar.empty() || !f->elseVar.empty() || !f->elseParams.empty()) { fail("an `if` binder"); return; }
            for (auto& bv : f->branchVars) if (!bv.empty()) { fail("an `elsif` binder"); return; }
            for (auto& bp : f->branchParams) if (!bp.empty()) { fail("an `if` destructuring binder"); return; }
            for (auto& br : f->branches) {
                expr(br.first.get());
                // A postfix `STMT if COND` has no block of its own: a `my` in it
                // declares in the ENCLOSING scope, so it must not get one here.
                block(br.second.get(), !f->modifier);
                if (!err.empty()) return;
            }
            if (f->elseBlock) block(f->elseBlock.get(), !f->modifier);
            return;
        }
        case NK::Block: block(static_cast<Block*>(s), true); return;
        case NK::WhileStmt: {
            auto* w = static_cast<WhileStmt*>(s);
            if (!s->label.empty()) { fail("a labelled loop"); return; }
            if (w->modifier) { fail("a postfix `while`"); return; }
            if (w->asExpr) { fail("a loop in expression position"); return; }
            if (!w->var.empty() || !w->params.empty()) { fail("`while EXPR -> $x`"); return; }
            expr(w->cond.get());
            block(w->body.get(), true);
            return;
        }
        case NK::LoopStmt: {
            auto* l = static_cast<LoopStmt*>(s);
            if (!s->label.empty()) { fail("a labelled loop"); return; }
            if (l->asExpr) { fail("a loop in expression position"); return; }
            // The init declares into the scope the loop's own body sees, and a
            // nested `loop` is emitted WITH its init, so it needs a scope of its
            // own. The outermost loop's init is not emitted at all (the
            // interpreter has already run it) — see emitJitKernel — but its
            // names are still locals of the kernel, which this models correctly.
            scopes.push_back({});
            headerExpr(l->init.get());
            expr(l->cond.get());
            headerExpr(l->incr.get());
            block(l->body.get(), true);
            scopes.pop_back();
            return;
        }
        default: fail("a statement the kernel whitelist does not cover"); return;
    }
}

// ---- cache, compiler, loader ----------------------------------------------

std::string fnv1a(const std::string& s) {
    unsigned long long h = 1469598103934665603ULL;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx", h);
    return buf;
}

std::string jitDir() {
    if (const char* d = std::getenv("RAKUPP_JIT_DIR")) return d;
    std::string base;
    if (const char* x = std::getenv("XDG_CACHE_HOME")) base = x;
    else if (std::string h = platHomeDir(); !h.empty()) base = h + "/.cache";
    else return "";
    return base + "/rakupp/jit";
}

bool fileThere(const std::string& p) { std::ifstream f(p); return (bool)f; }

void mkdirs(const std::string& p) {
    std::string acc;
    for (size_t i = 0; i <= p.size(); i++) {
        if (i == p.size() || p[i] == '/') {
            if (acc.size() > 1) ::mkdir(acc.c_str(), 0700);
        }
        if (i < p.size()) acc += p[i];
    }
}

std::string shq(const std::string& s) {
    std::string o = "'";
    for (char c : s) { if (c == '\'') o += "'\\''"; else o += c; }
    return o + "'";
}

int run(const std::string& cmd) {
    std::string c = cmd;
    if (!g_opt.verbose) c += " >/dev/null 2>&1";
    return std::system(c.c_str());
}

// What the cache key has to change with: the kernel source, the headers it is
// compiled against, and the compiler that compiles it. The BINARY's identity
// goes in too — an inline runtime helper that changed shape between builds
// would otherwise be answered by a stale kernel.
const std::string& buildId() {
    static const std::string id = [] {
        std::string s = std::string(
#ifdef RAKUPP_VERSION
            RAKUPP_VERSION
#else
            "0.0.0"
#endif
        ) + "|" + g_cxx + "|" + g_inc;
        // the running binary's size+mtime, so any rebuild invalidates
        struct stat st {};
        if (!g_self.empty() && ::stat(g_self.c_str(), &st) == 0)
            s += "|" + std::to_string((long long)st.st_size) + "|" + std::to_string((long long)st.st_mtime);
        return fnv1a(s);
    }();
    return id;
}

// Every regular file under the cache, as (path, bytes). One level of fan-out
// directories plus the headers at the top, which is the whole layout.
std::vector<std::pair<std::string, unsigned long long>> cacheFiles() {
    std::vector<std::pair<std::string, unsigned long long>> out;
    std::string dir = jitDir();
    if (dir.empty()) return out;
    auto add = [&](const std::string& path) {
        struct stat st {};
        if (::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode))
            out.push_back({path, (unsigned long long)st.st_size});
    };
    auto walk = [&](const std::string& d, bool descend) {
        DIR* h = ::opendir(d.c_str());
        if (!h) return;
        while (struct dirent* e = ::readdir(h)) {
            std::string n = e->d_name;
            if (n == "." || n == "..") continue;
            std::string full = d + "/" + n;
            struct stat st {};
            if (::stat(full.c_str(), &st) != 0) continue;
            if (S_ISDIR(st.st_mode)) { if (descend) { DIR* h2 = ::opendir(full.c_str());
                    if (h2) { while (struct dirent* e2 = ::readdir(h2)) {
                            std::string n2 = e2->d_name;
                            if (n2 == "." || n2 == "..") continue;
                            add(full + "/" + n2);
                        } ::closedir(h2); } } }
            else add(full);
        }
        ::closedir(h);
    };
    walk(dir, true);
    return out;
}

// A header belongs to ONE build of rakupp, and a rebuild orphans it. Nothing
// ever reads an orphan again, so it is removed when its replacement is built —
// which keeps the directory at one header rather than one per build ever run.
void pruneStalePch(const std::string& keep) {
    std::string dir = jitDir();
    if (dir.empty()) return;
    DIR* h = ::opendir(dir.c_str());
    if (!h) return;
    while (struct dirent* e = ::readdir(h)) {
        std::string n = e->d_name;
        if (n.rfind("rt-", 0) != 0) continue;
        if (n.size() < 5 || n.substr(n.size() - 4) != ".pch") continue;
        std::string full = dir + "/" + n;
        if (full == keep) continue;
        ::remove(full.c_str());
        note("removed a precompiled header left by an earlier build: " + n);
    }
    ::closedir(h);
}

// The precompiled header. Built once per (compiler, headers, binary) and reused
// by every kernel afterwards: it is what turns a 1.2 s compile into a 30 ms one,
// which is the difference between a build step and a JIT. clang only — GCC's
// PCH works by a different rule, and without one a kernel still compiles, just
// slower, so the lane is skipped rather than emulated.
std::string pchPath() {
    // OPT-IN, and the reason is the size. A header is 31 MB and saves 0.28 s on
    // each kernel compile — worth it for a sweep that compiles hundreds of
    // kernels, and a very poor trade for someone whose program has one hot loop
    // and who would be left with 31 MB per build of rakupp for a saving taken
    // once, in the background, where nobody is waiting for it. The first draft
    // of this file had it on by default and left THREE headers, 91 MB, in one
    // afternoon of rebuilding.
    if (!g_opt.pch) return "";
    if (!g_clang) return "";
    std::string dir = jitDir();
    if (dir.empty()) return "";
    return dir + "/rt-" + buildId() + ".pch";
}

// Returns the PCH path if one is usable, "" otherwise. Called on the compile
// thread, under g_mu, so two kernels cannot race to build it.
std::string ensurePch() {
    std::string p = pchPath();
    if (p.empty()) return "";
    if (fileThere(p)) return p;
    mkdirs(jitDir());
    pruneStalePch(p);
    std::string tmp = p + "." + std::to_string((long long)::getpid()) + ".tmp";
    // As with a kernel, the publish belongs to the shell command: a program that
    // exits before this 0.8 s build finishes would otherwise leave the .tmp
    // behind and rebuild the whole thing on every subsequent run.
    std::string cmd = shq(g_cxx) + " -std=c++17 -O2 -w -fPIC -I " + shq(g_inc) +
                      " -x c++-header " + shq(g_inc + "/Interpreter.h") + " -o " + shq(tmp) +
                      " && mv -f " + shq(tmp) + " " + shq(p);
    note("building the precompiled header (once per build)");
    if (run(cmd) != 0) { ::remove(tmp.c_str()); note("PCH build failed — kernels will compile the long way"); return ""; }
    return fileThere(p) ? p : "";
}

// Compile one kernel TU to a shared object and load it. Runs on the compile
// thread (or the interpreter thread under `--jit=sync`) and touches no
// interpreter state at all — only files, a subprocess, and dlopen.
KernelFn buildAndLoad(const std::string& src, const std::string& fnName) {
    std::string dir = jitDir();
    if (dir.empty()) return nullptr;
    std::string key = fnv1a(src + "|" + buildId());
    std::string sub = dir + "/" + key.substr(0, 2);
    std::string so  = sub + "/" + key + kJitSoExt;

    if (!(g_opt.cache && fileThere(so))) {
        std::string pch;
        { std::lock_guard<std::mutex> lk(g_mu); pch = ensurePch(); }
        mkdirs(sub);
        std::string stem = so + "." + std::to_string((long long)::getpid()) + ".tmp";
        std::string cpp = stem + ".cpp";
        { std::ofstream o(cpp); if (!o) return nullptr; o << src; }
        std::string tmp = stem + ".tmp";
        std::string cmd = shq(g_cxx) + " -std=c++17 -O2 -w -fPIC -I " + shq(g_inc);
        if (!pch.empty()) cmd += " -include-pch " + shq(pch);
        cmd += " -shared";
#ifdef __APPLE__
        // The kernel's runtime references are resolved against the EXECUTABLE
        // that loads it. Mach-O needs to be told to leave them undefined; ELF
        // allows undefined symbols in a shared object by default.
        cmd += " -Wl,-undefined,dynamic_lookup";
#endif
        cmd += " " + shq(cpp) + " -o " + shq(tmp);
        // The publish is part of the SHELL command, not a rename afterwards.
        // A program that finishes before its own compile does — anything
        // shorter than about half a second — exits and takes the compile
        // thread with it, and a rename written in C++ here would never run:
        // the kernel would be rebuilt from scratch on every subsequent run and
        // the cache would never fill. The compiler is a child process that
        // outlives us, so giving the publish to the shell lets that run's work
        // land for the next one. `mv` within a directory is atomic, so a reader
        // sees either no kernel or a complete one.
        cmd += " && mv -f " + shq(tmp) + " " + shq(so);
        int rc = run(cmd);
        if (!g_opt.verbose) ::remove(cpp.c_str());
        if (rc != 0) { ::remove(tmp.c_str()); note("compile failed for " + fnName); return nullptr; }
        g_compiled.fetch_add(1, std::memory_order_relaxed);
    } else {
        g_cacheHits.fetch_add(1, std::memory_order_relaxed);
        note("cache hit for " + fnName);
    }

    void* h = ::dlopen(so.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!h) { note(std::string("dlopen failed: ") + (::dlerror() ? ::dlerror() : "?")); return nullptr; }
    auto fn = (KernelFn)::dlsym(h, fnName.c_str());
    if (!fn) { note("dlsym failed for " + fnName); return nullptr; }
    return fn;
}

// ---- eligibility + emission (interpreter thread) ---------------------------

// Walk a candidate loop and, if it passes, emit its kernel source. Runs on the
// interpreter thread. Under RAKUPP_PARALLEL two threads can reach the same site
// together, and this writes `slots`, `slotWritten` and `src` before releasing
// the state that makes them readable — so it takes the lock and re-checks,
// rather than letting two walks interleave over one Site's fields.
void examine(Site* s) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (s->state.load(std::memory_order_acquire) != StNew) return;
    g_examined.fetch_add(1, std::memory_order_relaxed);
    Stmt* loop = s->loop;
    Scan sc;
    sc.scopes.push_back({});          // the kernel's own outermost scope
    if (loop->kind == NK::WhileStmt) {
        auto* w = static_cast<WhileStmt*>(loop);
        if (!loop->label.empty() || w->modifier || w->asExpr || !w->var.empty() || !w->params.empty())
            sc.fail("a loop shape the JIT does not take");
        else {
            sc.declRefused = true;            // `while (my $l = …)` — see Scan
            sc.expr(w->cond.get());
            sc.declRefused = false;
            sc.block(w->body.get(), true);
        }
    } else if (loop->kind == NK::LoopStmt) {
        auto* l = static_cast<LoopStmt*>(loop);
        if (!loop->label.empty() || l->asExpr) sc.fail("a loop shape the JIT does not take");
        else {
            // The init is NOT emitted — the interpreter ran it before the
            // kernel was entered — so the variables it declared already exist
            // in the frame and the kernel binds them as slots.
            sc.declIsSlot = true;
            sc.headerExpr(l->init.get());
            sc.declIsSlot = false;
            sc.declRefused = true;
            sc.expr(l->cond.get());
            sc.headerExpr(l->incr.get());
            sc.declRefused = false;
            sc.block(l->body.get(), true);
        }
    } else {
        sc.fail("not a while/loop");
    }
    if (!sc.err.empty()) {
        s->why = sc.err;
        s->state.store(StIneligible, std::memory_order_release);
        note("loop at line " + std::to_string(loop->line) + " not eligible: " + sc.err);
        return;
    }
    s->slots = sc.slots;
    s->slotWritten.clear();
    for (const std::string& n : sc.slots) s->slotWritten.push_back(sc.written.count(n) != 0);
    // ONE exported name, the same in every kernel. It has to be independent of
    // this run: the cache key is the kernel's SOURCE, so a name derived from the
    // AST node's address (which moves between runs) put a different name in the
    // source every time, which changed the key every time, which meant the cache
    // never hit and every run recompiled — measured, and the reason the first
    // draft of this file was no faster than the interpreter. The dylib's PATH is
    // what distinguishes one kernel from another.
    const std::string fnName = "rakupp_jit_kernel";
    try {
        s->src = emitJitKernel(loop, fnName, s->slots);
    } catch (const CodegenError& e) {
        s->why = e.msg;
        s->state.store(StIneligible, std::memory_order_release);
        note("loop at line " + std::to_string(loop->line) + " refused by the emitter: " + e.msg);
        return;
    } catch (...) {
        s->why = "the emitter raised";
        s->state.store(StIneligible, std::memory_order_release);
        return;
    }
    s->fnName = fnName;
    s->state.store(StEligible, std::memory_order_release);
    g_eligible.fetch_add(1, std::memory_order_relaxed);
    note("loop at line " + std::to_string(loop->line) + " is eligible, " +
         std::to_string(s->slots.size()) + " slot(s)");
}

void startCompile(Site* s) {
    if (s->requested.exchange(true, std::memory_order_acq_rel)) return;
    s->state.store(StCompiling, std::memory_order_release);
    std::string src = s->src, fn = s->fnName;
    auto work = [s, src, fn]() {
        KernelFn k = buildAndLoad(src, fn);
        if (k) {
            s->fn.store(k, std::memory_order_release);
            s->state.store(StReady, std::memory_order_release);
        } else {
            s->state.store(StFailed, std::memory_order_release);
            g_failed.fetch_add(1, std::memory_order_relaxed);
        }
        g_inFlight.fetch_sub(1, std::memory_order_acq_rel);
    };
    g_inFlight.fetch_add(1, std::memory_order_acq_rel);
    if (g_opt.sync) { work(); return; }
    // Detached, and the Site is deliberately never freed, so a compile that
    // finishes during shutdown cannot race a destructor. The thread touches no
    // interpreter state: a file, a subprocess, dlopen.
    try { std::thread(work).detach(); }
    catch (...) { work(); }   // no threads available: fall back to compiling here
}

}  // namespace

// ---- the public surface ----------------------------------------------------

std::string parseSpec(const std::string& spec, Options& out) {
    out.on = true;
    if (spec.empty()) return "";
    size_t i = 0;
    while (i <= spec.size()) {
        size_t j = spec.find(',', i);
        std::string w = spec.substr(i, j == std::string::npos ? std::string::npos : j - i);
        if (!w.empty()) {
            if (w == "off") out.on = false;
            else if (w == "on") out.on = true;
            else if (w == "sync") out.sync = true;
            else if (w == "verbose") out.verbose = true;
            else if (w == "stats") out.stats = true;
            else if (w == "nocache") out.cache = false;
            else if (w == "pch") out.pch = true;
            else if (w == "nopch") out.pch = false;
            else if (w.rfind("threshold=", 0) == 0) {
                std::string n = w.substr(10);
                if (n.empty() || n.find_first_not_of("0123456789") != std::string::npos)
                    return "--jit: threshold= wants a number, got '" + n + "'";
                out.threshold = (unsigned)std::strtoul(n.c_str(), nullptr, 10);
            } else {
                return "--jit: unknown spec word '" + w +
                       "' (known: off, on, sync, verbose, stats, nocache, pch, threshold=N)";
            }
        }
        if (j == std::string::npos) break;
        i = j + 1;
    }
    return "";
}

void configure(const Options& o, const std::string& cxx, const std::string& inc,
               const std::string& selfExe) {
    g_opt = o;
    g_cxx = cxx;
    g_inc = inc;
    g_self = selfExe;
    if (const char* t = std::getenv("RAKUPP_JIT_THRESHOLD"))
        g_opt.threshold = (unsigned)std::strtoul(t, nullptr, 10);
    if (std::getenv("RAKUPP_JIT_VERBOSE")) g_opt.verbose = true;
    if (!o.on) { g_on = false; return; }
    if (g_cxx.empty() || g_inc.empty()) {
        std::cerr << "--jit: no C++ compiler or runtime headers found — running interpreted\n";
        g_on = false;
        return;
    }
    // Which compiler this is decides whether the PCH lane is available.
    {
        std::string probe = shq(g_cxx) + " --version 2>/dev/null";
        if (FILE* p = ::popen(probe.c_str(), "r")) {
            char buf[256] = {0};
            size_t n = std::fread(buf, 1, sizeof buf - 1, p);
            ::pclose(p);
            g_clang = std::string(buf, n).find("clang") != std::string::npos;
        }
    }
    g_on = true;
    note(std::string("on — compiler ") + g_cxx + (g_clang ? " (clang, PCH lane on)" : "") +
         ", threshold " + std::to_string(g_opt.threshold) + (g_opt.sync ? ", sync" : ""));
}

Site* siteFor(Stmt* loop) {
    if (loop->kind != NK::WhileStmt && loop->kind != NK::LoopStmt) return nullptr;
    if (Site* s = (Site*)loop->jitSite.get()) return s;
    // Two threads can meet the same loop node at once under RAKUPP_PARALLEL.
    // `publish` is a compare-exchange, so exactly one Site is ever installed and
    // the loser frees the one it built rather than leaking it — the same shape
    // every other PublishedOnce slot in this tree uses.
    Site* mine = new Site();
    mine->loop = loop;
    Site* won = (Site*)loop->jitSite.publish(mine);
    if (won != mine) { delete mine; return won; }
    std::lock_guard<std::mutex> lk(g_mu);
    siteList().push_back(mine);       // never freed after this, by design
    return mine;
}

void pushLoop(Site* s) { if (s) t_stack.push_back(s); }
void popLoop(Site* s) {
    if (!s) return;
    for (size_t i = t_stack.size(); i-- > 0;)
        if (t_stack[i] == s) { t_stack.erase(t_stack.begin() + i); return; }
}

void tick(Site* s) {
    if (!s) return;
    int st = s->state.load(std::memory_order_acquire);
    if (st == StIneligible || st == StFailed || st == StCompiling || st == StReady) return;
    unsigned n = s->count.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= g_opt.threshold) return;
    // Hot. Pick the OUTERMOST eligible candidate on this thread's loop stack —
    // in a nest the inner loop is what trips and the outer one is what is worth
    // compiling, because an outer kernel subsumes every loop inside it. An
    // outer loop that is NOT eligible (it contains a call, say) is skipped, and
    // the search falls through to the loop that actually tripped.
    Site* target = nullptr;
    for (Site* c : t_stack) {
        int cs = c->state.load(std::memory_order_acquire);
        if (cs == StNew) { examine(c); cs = c->state.load(std::memory_order_acquire); }
        if (cs == StEligible) { target = c; break; }
        // An enclosing loop is already being compiled, or already has a kernel.
        // That kernel contains THIS loop, so compiling this one too would buy
        // nothing and cost a second compile — which is exactly what the first
        // draft did on a Mandelbrot, filling the cache with a kernel that could
        // never be reached.
        if (cs == StCompiling || cs == StReady) return;
    }
    if (!target) {
        if (s->state.load(std::memory_order_acquire) == StNew) examine(s);
        if (s->state.load(std::memory_order_acquire) != StEligible) return;
        target = s;
    }
    if (g_inFlight.load(std::memory_order_acquire) > 0 && !g_opt.sync) return;  // one compile at a time
    startCompile(target);
}

// A kernel that cannot be entered HERE is retired, not retried. Without this the
// bind ran again on every iteration — the lookups, the guards and (under
// --jit=verbose) a line of output per iteration — which is slower than simply
// interpreting the loop, and was measured as exactly that.
bool refuse(Site* s, const std::string& why) {
    s->why = why;
    s->state.store(StIneligible, std::memory_order_release);
    note(why + " — staying interpreted");
    return false;
}

// Bind the kernel's slots against the live frame and run it.
//
// Every slot is looked up ONCE here, and the pointer has to stay good for the
// whole loop. Two things make that true: the whitelist admits no calls, so
// nothing inside the kernel can insert into a scope the kernel did not create
// (an insert is the only thing that moves a map entry, and a pad never grows at
// all); and each slot is refused below unless its container is a plain one.
bool runIfReady(Site* s, Interpreter& I, Env* env) {
    if (!s) return false;
    if (s->state.load(std::memory_order_acquire) != StReady) return false;
    KernelFn fn = s->fn.load(std::memory_order_acquire);
    if (!fn) return false;

    std::vector<Value*> slots;
    slots.reserve(s->slots.size());
    for (size_t k = 0; k < s->slots.size(); k++) {
        const std::string& n = s->slots[k];
        Value* cell = nullptr;
        Env* owner = nullptr;
        for (Env* e = env; e; e = e->parent.get()) {
            auto it = e->vars.find(n);
            if (it != e->vars.end()) { cell = &it->second; owner = e; break; }
            if (e->layout) if (Value* p = e->padFind(n)) { cell = p; owner = e; break; }
        }
        if (!cell) return refuse(s, "slot " + n + " is not in scope at kernel entry");
        // Only a slot the kernel STORES to has to be a plain container. Every
        // guard below describes what a store must honour — a native width to
        // wrap at, a readonly binding to refuse, a coercion or default to
        // apply, an `is rw` caller slot to write through — and the kernel
        // assigns the container directly, so it would honour none of them. A
        // slot the kernel only reads is unaffected by all of it.
        if (!s->slotWritten[k]) { slots.push_back(cell); continue; }
        if (cell->natBits != 0 || cell->natFloat || cell->readonly)
            return refuse(s, "slot " + n + " is a native or readonly container the kernel would write");
        if (owner->ex) {
            const EnvExtras& x = *owner->ex;
            if (x.varDefault.count(n) || x.varCoerce.count(n) || x.varDynamic.count(n) ||
                x.rwLinks.count(n) || x.rwDirect.count(n) || x.rwRoots.count(n) ||
                x.rwSynced.count(n) || x.rwDead.count(n))
                return refuse(s, "slot " + n + " has container traits the kernel would have to honour");
        }
        slots.push_back(cell);
    }
    g_entered.fetch_add(1, std::memory_order_relaxed);
    // A kernel does not advance the interpreter's statement line — the whole
    // point is that it runs without touching interpreter state per statement —
    // so a fault raised inside one would be reported at whatever line happened
    // to run LAST before it, which is simply wrong. Pointing it at the loop is
    // both cheap (once per entry) and true: the fault did happen in this loop.
    // `--exe` reports no line at all for the same program, so this is the
    // compiled backends' shared limitation, answered a little better here.
    I.restoreTestLine(s->loop->line);
    fn(&I, slots.empty() ? nullptr : slots.data());
    return true;
}

std::string cacheDir() { return jitDir(); }

std::pair<unsigned long long, unsigned long long> clean() {
    unsigned long long n = 0, bytes = 0;
    for (auto& f : cacheFiles()) { if (::remove(f.first.c_str()) == 0) { n++; bytes += f.second; } }
    // The fan-out directories are empty now; leave the root, which is where the
    // next run will write.
    std::string dir = jitDir();
    if (!dir.empty()) {
        if (DIR* h = ::opendir(dir.c_str())) {
            while (struct dirent* e = ::readdir(h)) {
                std::string nm = e->d_name;
                if (nm == "." || nm == "..") continue;
                ::rmdir((dir + "/" + nm).c_str());   // fails harmlessly on a file
            }
            ::closedir(h);
        }
    }
    return {n, bytes};
}

void info() {
    std::string dir = jitDir();
    if (dir.empty()) { std::cout << "JIT kernel cache unavailable: no HOME, so there is nowhere to put one\n"; return; }
    auto files = cacheFiles();
    unsigned long long kernels = 0, kBytes = 0, heads = 0, hBytes = 0, other = 0, oBytes = 0;
    for (auto& f : files) {
        const std::string& p2 = f.first;
        bool isPch = p2.size() > 4 && p2.substr(p2.size() - 4) == ".pch";
        bool isKernel = p2.size() > 3 && (p2.find(".dylib") != std::string::npos ||
                                          p2.find(".so") != std::string::npos ||
                                          p2.find(".dll") != std::string::npos);
        if (isPch)         { heads++;   hBytes += f.second; }
        else if (isKernel) { kernels++; kBytes += f.second; }
        else               { other++;   oBytes += f.second; }
    }
    auto kb = [](unsigned long long b) { return (b + 1023) / 1024; };
    std::cout << dir << "\n";
    if (files.empty()) { std::cout << "empty\n"; return; }
    std::cout << "  kernels:            " << kernels << "  (" << kb(kBytes) << " KB)\n"
              << "  precompiled header: " << heads << "  (" << kb(hBytes) << " KB)\n";
    if (other) std::cout << "  other:              " << other << "  (" << kb(oBytes) << " KB)\n";
    std::cout << "\n" << files.size() << " file" << (files.size() == 1 ? "" : "s") << ", "
              << kb(kBytes + hBytes + oBytes) << " KB total\n";
    if (heads > 1)
        std::cout << heads << " headers means " << (heads - 1) << " left by earlier builds of rakupp. "
                     "Each belongs to one build and is never read again; --jit-clean removes them.\n";
    else if (heads == 1)
        std::cout << "The header is most of that. It is optional (--jit=pch) and halves each "
                     "kernel compile; --jit-clean removes it and it is rebuilt only if asked for again.\n";
}

void report() {
    if (!g_opt.stats || !g_on) return;
    std::cerr << "[jit] examined " << g_examined.load() << ", eligible " << g_eligible.load()
              << ", compiled " << g_compiled.load() << ", cache hits " << g_cacheHits.load()
              << ", failed " << g_failed.load() << ", kernels entered " << g_entered.load() << "\n";
}

}  // namespace jit
}  // namespace rakupp
