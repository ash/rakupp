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
#include "Cnp.h"
#include "Codegen.h"
#include "Interpreter.h"
#include "Platform.h"   // dlopen/dlsym and their Win32 shims

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <sys/stat.h>
#if !defined(_WIN32)
#include <dirent.h>   // Windows gets the FindFirstFile-based shim from Platform.h
#endif
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

const char* tag() { return g_opt.backend == Backend::Cnp ? "[cnp] " : "[jit] "; }

void note(const std::string& s) {
    if (g_opt.verbose) std::cerr << tag() << s << "\n";
}

// ---- the kernel ------------------------------------------------------------

using KernelFn = int (*)(Interpreter*, Value**);

enum State : int { StNew = 0, StEligible = 1, StCompiling = 2, StReady = 3,
                   StIneligible = -1, StFailed = -2 };

}  // namespace

struct Site {
    Stmt* loop = nullptr;
    // What the EMITTER and the lowerer walk. For a `while` or a C-style `loop`
    // it is `loop` itself. For a `for` over an Int Range it is the synthetic
    // counted loop built below: the interpreter is standing in a ForStmt, but
    // what tiers up is the `loop (; $i <= END; $i++)` that its fast path is
    // already running. Line numbers and every message stay with `loop`, so
    // `--jit=verbose` names the line the user wrote.
    Stmt* emit = nullptr;
    bool countedFor = false;          // `emit` is synthetic; entry binds two extra names
    std::atomic<unsigned> count{0};
    std::atomic<int> state{StNew};
    std::atomic<KernelFn> fn{nullptr};
    std::atomic<bool> requested{false};
    std::vector<std::string> slots;   // written before state becomes Compiling; read-only after
    std::vector<bool> slotWritten;    // parallel to slots: does the kernel STORE to it?
    std::string why;                  // ineligibility reason, for --jit=verbose
    std::string src;                  // the emitted TU (also the cache key's material)
    std::string fnName;               // the kernel's extern "C" entry point
    // The copy-and-patch backend's kernel. Only one of `fn` and this is ever
    // set, and which one is decided once, by the command line.
    std::atomic<cnp::Kernel*> cnpKernel{nullptr};
    std::atomic<bool> notedThreads{false};   // the "a worker is live" line, said once
};

// The name a counted `for`'s synthetic condition reads its end bound from. It
// is defined by the interpreter in a frame of its own at kernel entry and dies
// with that frame, so it shadows anything of the same name for the kernel's
// duration and is invisible before and after. A body that spells this name
// itself would bind to the bound instead of its own variable — which is why it
// is spelled like nothing anyone writes.
const char* kCountedForEnd = "$__jit_for_end";
const char* countedForEndSlot() { return kCountedForEnd; }

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
    // The counted `for`'s loop variable. `$_` is refused everywhere else —
    // plainScalar says so, because the topic is frame state and not a lexical —
    // but for `for 1 .. N { … $_ … }` the interpreter hands the kernel a frame
    // of its own with the topic defined in it, and nothing else can reach that
    // frame, because the whitelist admits no calls. So here, and only here, the
    // topic IS a lexical. Empty for every other loop shape.
    std::string countedVar;

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
            const bool isCounted = !countedVar.empty() && v->name == countedVar;
            if (!plainScalar(v->name) && !isCounted) { fail("variable " + v->name); return; }
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
    if (g_opt.verbose) return std::system(cmd.c_str());
    // The GROUP is redirected, not the last command in it. Every command here
    // is a `&&`/`;` chain, and `a && b >/dev/null 2>&1` redirects only `b` — so
    // the compiler's own diagnostics were reaching the user's terminal on a
    // failed kernel, in the middle of their program's output.
    return std::system(("{ " + cmd + " ; }" + " >/dev/null 2>&1").c_str());
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
        // The TU is removed by the shell too, for the same reason the publish is
        // done there: a program that exits before its own compile finishes takes
        // this thread with it, and a `remove()` written below would never run.
        // That left one orphaned .cpp in the cache for every such run — which is
        // most of them, since a compile takes longer than a short program does.
        // Kept under --jit=verbose, where reading the emitted kernel is the point.
        if (!g_opt.verbose) cmd += " ; rm -f " + shq(cpp);
        int rc = run(cmd);
        if (!g_opt.verbose) ::remove(cpp.c_str());   // belt and braces if we outlive it
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

// `for 1 .. N -> $i { … }` is `loop (; $i <= N; $i++) { … }` with the init
// already run: the interpreter's own Range fast path walks a `long long` from
// `lo` to `hi`, which is a counted loop in every respect but its spelling. This
// builds that spelling once per loop node, so the scan, the C++ emitter and the
// copy-and-patch lowerer all see a shape they already handle and none of the
// three needs to learn a new statement kind.
//
// The node is LEAKED, exactly as the Site that owns it is, and that is what
// makes the borrowed body safe: `body` points at the real ForStmt's block, and
// running ~LoopStmt would free a block the interpreter is still executing. No
// destructor ever runs, so it never happens. Anything that later gives Sites a
// destructor has to release this pointer first.
LoopStmt* synthCountedLoop(ForStmt* fs, const std::string& var) {
    // VarExpr's constructor takes the name: it primes the attribute cache from
    // it, which assigning to `name` afterwards would not.
    auto mkVar = [&](const std::string& n) {
        auto* v = new VarExpr(n);
        v->line = fs->line;
        return v;
    };
    auto* cond = new Binary();
    cond->op = "<=";
    cond->line = fs->line;
    cond->lhs.reset(mkVar(var));
    cond->rhs.reset(mkVar(kCountedForEnd));
    auto* incr = new Unary();
    incr->op = "++";
    incr->postfix = true;
    incr->line = fs->line;
    incr->operand.reset(mkVar(var));
    auto* lp = new LoopStmt();
    lp->line = fs->line;
    lp->cond.reset(cond);
    lp->incr.reset(incr);
    lp->body.reset(fs->body.get());   // BORROWED — see above
    return lp;
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
    } else if (loop->kind == NK::ForStmt) {
        // The interpreter installs a site here ONLY from its counted Int-Range
        // fast path, so the source of the iteration is already known to be a
        // range of machine integers. What is left to ask about is the BINDING.
        auto* fs = static_cast<ForStmt*>(loop);
        const std::string var = fs->vars.empty() ? "$_" : fs->vars[0];
        if (!loop->label.empty() || fs->asExpr || fs->modifier || fs->destructure ||
            fs->rwVars || !fs->params.empty() || fs->vars.size() > 1)
            sc.fail("a loop shape the JIT does not take");
        else if (var != "$_" && !plainScalar(var))
            sc.fail("loop variable " + var);
        // The topic form, `for 1 .. N { … $_ … }`, is the copy-and-patch
        // backend's alone. The two share this whitelist in every other respect,
        // and this is the one place they cannot: the C++ backend emits a kernel
        // through the SAME Codegen `--exe` uses, and Codegen does not resolve
        // `$_` to a lexical at all — it emits the enclosing topic, or
        // `RT.dynVarRef("$_")` when there is no enclosing topic to emit, which
        // in a kernel there never is. So the slot bound under that name is
        // written by the synthetic `$_++` and read by nothing, while the body
        // reads whatever the interpreter's live topic happens to hold.
        //
        // That answers WRONG rather than failing: `for 1 .. 20 { $u = $u + $_ }`
        // printed 211 against the interpreter's 210, because the kernel picked
        // up the topic the last interpreted iteration left behind and ran the
        // whole range again from there. The differential gate caught it; nothing
        // about it looks like a failure from outside, which is the outcome this
        // whitelist exists to make impossible. Lowering `$_` for that backend
        // means giving Codegen a way to bind a topic to a name, and that is its
        // own piece of work.
        else if (var == "$_" && g_opt.backend != Backend::Cnp)
            sc.fail("the topic as a loop variable, which this backend emits as the live topic");
        else {
            LoopStmt* lp = synthCountedLoop(fs, var);
            sc.countedVar = var;
            // The BODY is walked first, before the synthetic header marks the
            // loop variable written, so that `written` can answer one question
            // the other loop shapes never have to: a `for` variable is a
            // READ-ONLY binding (`for 1..3 -> $i { $i = 9 }` is an error), and
            // the kernel would assign it like any other slot. A body that
            // writes it is refused rather than quietly given a mutable one.
            sc.block(lp->body.get(), true);
            if (sc.err.empty() && sc.written.count(var))
                sc.fail("an assignment to the loop variable, which a `for` binds read-only");
            sc.declRefused = true;
            sc.expr(lp->cond.get());
            sc.headerExpr(lp->incr.get());
            sc.declRefused = false;
            if (sc.err.empty()) { s->emit = lp; s->countedFor = true; }
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
    // Only the C++ backend needs a translation unit. Copy-and-patch lowers
    // straight from the AST when the loop is compiled, which is microseconds
    // later, so emitting C++ here would be work thrown away.
    if (g_opt.backend == Backend::Cxx) {
        try {
            s->src = emitJitKernel(s->emit, fnName, s->slots);
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
    if (g_opt.backend == Backend::Cnp) {
        // Microseconds, so it happens right here: no thread, no subprocess, no
        // file. That is also why the threshold this backend runs at is two
        // orders of magnitude below the C++ one.
        std::string why;
        cnp::Kernel* k = cnp::compile(s->emit, s->slots, why);
        if (!k) {
            s->why = why;
            s->state.store(StFailed, std::memory_order_release);
            g_failed.fetch_add(1, std::memory_order_relaxed);
            note("loop at line " + std::to_string(s->loop->line) + " did not lower: " + why);
            return;
        }
        s->cnpKernel.store(k, std::memory_order_release);
        s->state.store(StReady, std::memory_order_release);
        g_compiled.fetch_add(1, std::memory_order_relaxed);
        note("loop at line " + std::to_string(s->loop->line) + " lowered to " +
             std::to_string(cnp::opCount(k)) + " ops, " + std::to_string(cnp::regCount(k)) +
             " registers, " + std::to_string(cnp::codeBytes(k)) + " bytes");
        return;
    }
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

namespace {
// The spec words both flags share, plus the ones only a compiler-and-cache
// backend has. `flag` is what an error message calls itself.
std::string parseWords(const std::string& spec, Options& out, const char* flag, bool cnp) {
    if (spec.empty()) return "";
    size_t i = 0;
    while (i <= spec.size()) {
        size_t j = spec.find(',', i);
        std::string w = spec.substr(i, j == std::string::npos ? std::string::npos : j - i);
        if (!w.empty()) {
            if (w == "off") out.on = false;
            else if (w == "on") out.on = true;
            else if (w == "verbose") out.verbose = true;
            else if (w == "stats") out.stats = true;
            else if (!cnp && w == "sync") out.sync = true;
            else if (!cnp && w == "nocache") out.cache = false;
            else if (!cnp && w == "pch") out.pch = true;
            else if (!cnp && w == "nopch") out.pch = false;
            else if (w.rfind("threshold=", 0) == 0) {
                std::string n = w.substr(10);
                if (n.empty() || n.find_first_not_of("0123456789") != std::string::npos)
                    return std::string(flag) + ": threshold= wants a number, got '" + n + "'";
                out.threshold = (unsigned)std::strtoul(n.c_str(), nullptr, 10);
            } else {
                return std::string(flag) + ": unknown spec word '" + w + "' (known: off, on, verbose, "
                       "stats, threshold=N" + (cnp ? "" : ", sync, nocache, pch") + ")";
            }
        }
        if (j == std::string::npos) break;
        i = j + 1;
    }
    return "";
}
}  // namespace

// Switching backends resets the fields the other one owns, so that `--cnp
// --jit` means the C++ backend at ITS defaults rather than the C++ backend
// wearing copy-and-patch's threshold and its disabled cache. Repeating the SAME
// flag accumulates, as it always did: `--jit=sync --jit=verbose` is both.
std::string parseSpec(const std::string& spec, Options& out) {
    if (out.backend != Backend::Cxx) { out.sync = false; out.cache = true; out.threshold = 1000; }
    out.on = true;
    out.backend = Backend::Cxx;
    return parseWords(spec, out, "--jit", false);
}

// `--cnp` is the same harness with the copy-and-patch backend. Its default
// threshold is a hundredth of the C++ one because a kernel costs microseconds
// to produce rather than half a second, so waiting a thousand iterations to
// decide would throw away most of what there is to win.
std::string parseCnpSpec(const std::string& spec, Options& out) {
    if (out.backend != Backend::Cnp) {
        out.sync = true;       // there is nothing to do on another thread
        out.cache = false;     // and nothing to put on disk
        out.threshold = 100;
    }
    out.on = true;
    out.backend = Backend::Cnp;
    return parseWords(spec, out, "--cnp", true);
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
    if (g_opt.backend == Backend::Cnp) {
        // Nothing to look for on the machine: the stencils were compiled into
        // this binary. The only question is whether this build has any.
        if (!cnp::available()) {
            std::cerr << "--cnp: " << cnp::unavailableReason() << " — running interpreted\n";
            g_on = false;
            return;
        }
        g_on = true;
        note(std::string("on — copy-and-patch for ") + cnp::arch() + ", threshold " +
             std::to_string(g_opt.threshold));
        return;
    }
    if (g_cxx.empty() || g_inc.empty()) {
        std::cerr << "--jit: no C++ compiler or runtime headers found — running interpreted\n";
        g_on = false;
        return;
    }
    // Which compiler this is decides whether the PCH lane is available.
    // msvcrt spells the pair with an underscore, as every other capture in this
    // tree already accounts for (see __qx__ in Builtins.cpp).
    {
        std::string probe = shq(g_cxx) + " --version 2>/dev/null";
#if defined(_WIN32)
        FILE* p = _popen(probe.c_str(), "r");
#else
        FILE* p = ::popen(probe.c_str(), "r");
#endif
        if (p) {
            char buf[256] = {0};
            size_t n = std::fread(buf, 1, sizeof buf - 1, p);
#if defined(_WIN32)
            _pclose(p);
#else
            ::pclose(p);
#endif
            g_clang = std::string(buf, n).find("clang") != std::string::npos;
        }
    }
    g_on = true;
    note(std::string("on — compiler ") + g_cxx + (g_clang ? " (clang, PCH lane on)" : "") +
         ", threshold " + std::to_string(g_opt.threshold) + (g_opt.sync ? ", sync" : ""));
}

Site* siteFor(Stmt* loop) {
    if (loop->kind != NK::WhileStmt && loop->kind != NK::LoopStmt &&
        loop->kind != NK::ForStmt) return nullptr;
    if (Site* s = (Site*)loop->jitSite.get()) return s;
    // Two threads can meet the same loop node at once under RAKUPP_PARALLEL.
    // `publish` is a compare-exchange, so exactly one Site is ever installed and
    // the loser frees the one it built rather than leaking it — the same shape
    // every other PublishedOnce slot in this tree uses.
    Site* mine = new Site();
    mine->loop = loop;
    mine->emit = loop;                // replaced by the synthetic loop for a `for`
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
// Is a kernel published for this site? The counted-`for` entry has to know
// before it builds the frame the kernel wants, because building that frame
// costs an allocation and the answer is "no" on every iteration but one.
bool isReady(Site* s) {
    return s && s->state.load(std::memory_order_acquire) == StReady;
}

bool runIfReady(Site* s, Interpreter& I, Env* env) {
    if (!s) return false;
    if (s->state.load(std::memory_order_acquire) != StReady) return false;
    const bool isCnp = g_opt.backend == Backend::Cnp;
    KernelFn fn = s->fn.load(std::memory_order_acquire);
    cnp::Kernel* ck = s->cnpKernel.load(std::memory_order_acquire);
    if (!fn && !ck) return false;
    // A copy-and-patch kernel HOISTS its slots: it unboxes them into registers
    // at entry and writes them back at exit, which is where its speed comes
    // from. That is unobservable only while nothing else can touch those
    // containers. Nothing on THIS thread can — the whitelist admits no calls —
    // and `liveWorkers_ == 0` is what says nothing on any other thread can
    // either.
    //
    // Found by the corpus gate, not by reasoning: `my $stop = False; start {
    // until $stop {…} }; $stop = True` hung, because the kernel read `$stop`
    // once and the write it was waiting for landed in the container.
    // `--jit` binds a `Value&` and re-reads it per iteration, so it never had
    // this to answer.
    //
    // The test comes BEFORE the slot binding on purpose: a refusal has to cost
    // one relaxed load, because the interpreter asks again on every iteration.
    // The site is not retired — a program that joins its workers gets its
    // kernel back at the next entry.
    // Both counters, because that pair is what the rest of the engine means by
    // "is there concurrent work": a cued job is not a live worker yet, but it
    // becomes one without this thread doing anything.
    if (isCnp && (I.liveWorkers_.load(std::memory_order_acquire) > 0 ||
                  I.cuedLoads_.load(std::memory_order_acquire) > 0)) {
        if (!s->notedThreads.exchange(true, std::memory_order_relaxed))
            note("loop at line " + std::to_string(s->loop->line) +
                 " not entered while another thread is live — its variables are shared");
        return false;
    }

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
    if (isCnp) {
        I.restoreTestLine(s->loop->line);
        // A kernel that will not bind HERE is retired rather than retried —
        // `run` answers false for exactly one thing the guards above cannot
        // see, two names sharing a single container.
        std::string why;
        if (!cnp::run(ck, I, slots.empty() ? nullptr : slots.data(), s->slotWritten, why))
            return refuse(s, why);
        return true;
    }
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

}  // namespace jit

// A bundled binary has no option surface of its own — every argument after the
// executable belongs to the embedded program, which is the whole contract of
// `--bundle`. So the backend is decided when the bundle is BUILT (`rakupp
// --bundle --cnp prog.raku`) and baked into the stub, and one run can still be
// steered with RAKUPP_CNP: `0` off, `1` on, anything else a `--cnp` spec.
//
// Only the copy-and-patch backend is offered here. `--jit` would need a C++
// compiler and the runtime headers on the machine running the bundle, which is
// exactly what a single-file deliverable is meant not to need.
void rakuppCnpBundled(bool on, const std::string& selfExe) {
    std::string spec;
    if (const char* e = std::getenv("RAKUPP_CNP")) {
        std::string v = e;
        if (v == "0" || v == "off") return;
        on = true;
        if (v != "1") spec = v;
    }
    if (!on) return;
    jit::Options o;
    std::string err = jit::parseCnpSpec(spec, o);
    if (!err.empty()) { std::cerr << err << "\n"; return; }
    jit::configure(o, std::string(), std::string(), selfExe);
}

namespace jit {

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
    std::cerr << tag() << "examined " << g_examined.load() << ", eligible " << g_eligible.load()
              << ", compiled " << g_compiled.load();
    if (g_opt.backend == Backend::Cxx) std::cerr << ", cache hits " << g_cacheHits.load();
    std::cerr << ", failed " << g_failed.load() << ", kernels entered " << g_entered.load() << "\n";
}

}  // namespace jit
}  // namespace rakupp
