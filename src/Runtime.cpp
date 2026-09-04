#include "Runtime.h"
#include "DeclCheck.h"
#include "Interpreter.h"
#include "Lexer.h"
#include "Parser.h"
#include "Pod.h"
#include <cstdlib>
#include <csignal>
#include <functional>
#include <iostream>
#if defined(_WIN32)
#include "Platform.h"   // pulls <windows.h>
#include <process.h>    // _beginthreadex
#include <direct.h>     // _getcwd
#else
#include <pthread.h>
#include <unistd.h>     // getcwd
#endif

namespace rakupp {

// $?FILE is the source path as an absolute path: cwd-prefixed when invoked
// relatively (Rakudo keeps the `./` — no realpath canonicalization).
static std::string absSrcPath(const std::string& f) {
    if (f.empty()) return f;
#if defined(_WIN32)
    if (f[0] == '/' || f[0] == '\\' || (f.size() > 1 && f[1] == ':')) return f;
    char buf[4096]; if (_getcwd(buf, sizeof buf)) return std::string(buf) + "\\" + f;
#else
    if (f[0] == '/') return f;
    char buf[4096]; if (getcwd(buf, sizeof buf)) return std::string(buf) + "/" + f;
#endif
    return f;
}

void setConsoleUtf8() {
#if defined(_WIN32)
    // Console defaults to a legacy OEM codepage; rakupp emits UTF-8 everywhere.
    // Redirected output bypasses the console codepage, so files/pipes are
    // unaffected — this only changes what an interactive console renders.
    ::SetConsoleOutputCP(CP_UTF8);
    ::SetConsoleCP(CP_UTF8);
#endif
}

#if defined(_WIN32)
// Big-stack worker threads on Windows: _beginthreadex with the stack size treated
// as a reservation (STACK_SIZE_PARAM_IS_A_RESERVATION), matching pthread's virtual
// 256 MiB/1 GiB stacks. Declared in Interpreter.h; kept here so <windows.h> stays
// out of the core header.
namespace {
struct BigStackTramp { void (*entry)(void*); void* arg; };
unsigned __stdcall bigStackWinEntry(void* p) {
    BigStackTramp t = *static_cast<BigStackTramp*>(p);
    delete static_cast<BigStackTramp*>(p);
    t.entry(t.arg);
    return 0;
}
}
std::uintptr_t bigStackCreate(void (*entry)(void*), void* arg, std::size_t stackBytes) {
    auto* t = new BigStackTramp{entry, arg};
    std::uintptr_t h = _beginthreadex(nullptr, (unsigned)stackBytes, bigStackWinEntry, t,
                                      STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    if (!h) delete t;
    return h;
}
void bigStackJoin(std::uintptr_t h)  { ::WaitForSingleObject((HANDLE)h, INFINITE); }
void bigStackClose(std::uintptr_t h) { ::CloseHandle((HANDLE)h); }
#endif

static bool g_docMode = false;
void rakuppSetDocMode(bool on) { g_docMode = on; } // set by main when --doc is passed
static bool g_llException = false;
void rakuppSetLLException(bool on) { g_llException = on; } // --ll-exception (issue #67)

// The search path the PARSER will actually use, in order — which is what decides
// the file a `use` resolves to when scanned for operators, and so belongs in the
// precomp key. It must match how parser.libPaths_ is assembled below, defaults
// included: `.` and `lib` are relative, so they are precisely what makes the
// answer depend on the working directory. Leaving them out gave one entry to two
// directories, and a script cached where an operator existed was replayed where
// it did not.
static std::vector<std::string> precompSearchPath(const std::vector<std::string>&);
std::vector<std::string> effectiveSearchPath(const std::vector<std::string>& dashI) {
    return precompSearchPath(dashI);
}

static std::vector<std::string> precompSearchPath(const std::vector<std::string>& dashI) {
    std::vector<std::string> sp = dashI;
    sp.push_back("lib"); sp.push_back("."); sp.push_back("rakulib");
    if (const char* rl = std::getenv("RAKULIB"))
        for (auto& d : splitSearchPath(rl)) sp.push_back(d);
    return sp;
}

int rakuppRun(const std::string& src, std::vector<std::string> args,
              const std::string& fileName, const std::string& exePath,
              const std::vector<std::string>& libPaths) {
    // The CLI's shape: make an interpreter, then run the program in it.
    Interpreter interp;
    return rakuppRunOn(interp, src, std::move(args), fileName, exePath, libPaths,
                       /*declCheck*/ true);
}

int rakuppRunOn(Interpreter& interp, const std::string& src, std::vector<std::string> args,
                const std::string& fileName, const std::string& exePath,
                const std::vector<std::string>& libPaths, bool declCheck) {
    // Issue #32: an undeclared variable is a compile error, so the whole unit is
    // asked about before ANY of it runs — otherwise `say $x; say $typo;` prints
    // a line first and dies second. Answers -1 when the program may proceed.
    auto declCheckRc = [&](const Program& prog) -> int {
        if (!declCheck || !declCheckEnabled()) return -1;
        // The loader's own path, so an imported name is looked for where the
        // import will actually find it.
        auto us = findUndeclaredVars(prog, src, effectiveSearchPath(libPaths));
        return us.empty() ? -1 : reportUndeclaredVars(us, fileName, src);
    };
    try {
        // The main program gets the same precompiled-AST cache its modules do —
        // keyed on this file's path, validated against its contents. A cache hit
        // skips the lexer and parser outright; everything after this point sees a
        // Program indistinguishable from a freshly parsed one. `-e` code has no
        // file behind it, so it is never cached.
        {
            std::vector<std::string> sp = precompSearchPath(libPaths);
            Program cachedProg;
            std::string cachedFinish;
            if (fileName != "-e" && !fileName.empty() &&
                precompLoadProgram(fileName, src, sp, cachedProg, cachedFinish)) {
                interp.setArgs(std::move(args));
                interp.finishData_ = cachedFinish;
                if (g_docMode) { // pod comes from tokenize(); a fresh Lexer's podData() is empty
                    Lexer plx(src); plx.tokenize();
                    interp.podData_ = plx.podData();
                }
                interp.podDom_ = parsePod(src);
                interp.docMode_ = g_docMode;
                interp.llException_ = g_llException;
                interp.srcFile_ = fileName;
                interp.srcFileAbs_ = absSrcPath(fileName);
                interp.execPath_ = exePath;
                interp.libPaths_.insert(interp.libPaths_.begin(), libPaths.begin(), libPaths.end());
                if (int rc = declCheckRc(cachedProg); rc >= 0) return rc;
                return interp.run(cachedProg);
            }
        }
        Lexer lexer(src);
        auto tokens = lexer.tokenize();
        if (std::getenv("RAKUPP_DUMPTOKENS")) {
            for (auto& t : tokens) {
                std::string txt = t.text;
                if (txt.size() > 40) txt = txt.substr(0, 40) + "…";
                for (auto& c : txt) if (c == '\n') c = '|';
                std::cerr << "L" << t.line << " kind=" << (int)t.kind << " [" << txt << "]\n";
            }
        }
        std::string finish = lexer.finishData();
        std::string pod = lexer.podData();
        Parser parser(std::move(tokens));
        parser.declPod_ = lexer.declPod_; // `#=` param descriptions (drives $*USAGE)
        parser.leadPod_ = lexer.leadPod_; // `#|` leading declarator pod (.WHY)
        // the same search path the Interpreter will use, so a `use`d module's
        // operator declarations are found while this file is still being parsed
        parser.libPaths_.insert(parser.libPaths_.begin(), libPaths.begin(), libPaths.end());
        parser.srcFile_ = fileName;   // `use lib $*PROGRAM.sibling('lib')` resolves here too
        if (const char* rl = std::getenv("RAKULIB"))
            for (auto& d : splitSearchPath(rl)) parser.libPaths_.push_back(d);
        Program prog = parser.parseProgram();
        {
            std::vector<std::string> sp = precompSearchPath(libPaths);
            if (fileName != "-e" && !fileName.empty())
                precompStoreProgram(fileName, src, sp, prog, finish, parser.opScanned_);
        }
        interp.setArgs(std::move(args));
        interp.finishData_ = finish;
        interp.podData_ = pod;
        interp.podDom_ = parsePod(src);   // $=pod structured DOM
        interp.docMode_ = g_docMode;
        interp.llException_ = g_llException;
        interp.srcFile_ = fileName;
        interp.srcFileAbs_ = absSrcPath(fileName);
        interp.execPath_ = exePath;
        // -I <path> lib dirs take priority over the built-in / env-derived ones.
        interp.libPaths_.insert(interp.libPaths_.begin(), libPaths.begin(), libPaths.end());
        if (int rc = declCheckRc(prog); rc >= 0) return rc;
        return interp.run(prog);
    } catch (const ParseError& e) {
        std::cerr << "===SORRY!=== Parse error at line " << e.line << ": " << e.what() << "\n";
        // …and the line itself. A syntax error names a position the reader has
        // to go and look at; showing it here saves the trip (issue #67).
        std::string sl = interp.srcLineOf(interp.srcFileAbs_.empty() ? fileName : interp.srcFileAbs_, e.line);
        if (!sl.empty()) std::cerr << "      " << e.line << " | " << sl << "\n";
        return 1; // a compile-time (syntax) error exits 1, like Rakudo
    } catch (const RakuError& e) {
        // the same diagnostic the mainline handler prints — this is the path a
        // throw from a BEGIN/CHECK phaser or module load takes, and it used to
        // lose the frames the error was carrying all along
        std::cerr << interp.renderError(e, interp.btStyleForStderr());
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Internal error: " << e.what() << "\n";
        return 3;
    }
}

// Interpret an already-parsed Program (used by real AOT: the AST was built at
// this program's startup, so no lexing/parsing happens here).
int rakuppRunProgram(Program& prog, std::vector<std::string> args,
                     const std::string& fileName, const std::string& exePath,
                     const std::string& finish) {
    try {
        Interpreter interp;
        interp.setArgs(std::move(args));
        interp.finishData_ = finish;
        interp.srcFile_ = fileName;
        interp.srcFileAbs_ = absSrcPath(fileName);
        interp.execPath_ = exePath;
        return interp.run(prog);
    } catch (const RakuError& e) {
        std::cerr << e.message << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Internal error: " << e.what() << "\n";
        return 3;
    }
}

// Run `fn` on a large-stack thread (shared by both entry points).
// Flush stdout/stderr on the worker thread itself, before it returns. Otherwise the
// only flush is std::cout's static destructor on the MAIN thread during process exit,
// which can race the C runtime closing fd 1 — dropping the last buffered line when our
// stdout is a pipe (e.g. a parent capturing us). Flushing here makes output complete
// and deterministic regardless of exit ordering.
static int onBigStack(const std::function<int()>& fn) {
#if !defined(_WIN32)
    // Ignore SIGPIPE process-wide: writing to a socket whose peer has closed
    // must raise a catchable error (EPIPE), not terminate the process. Without
    // this a TCP server dies the moment any client disconnects mid-write.
    signal(SIGPIPE, SIG_IGN);
#endif
    // RAKUPP_MAIN_THREAD=1: run the program inline on the calling (process
    // main) thread instead of the spawned 1 GiB one. AppKit refuses to create
    // NSWindows off the main thread, so Cocoa GUI programs need this; the
    // recursion guard reads the real stack size via pthread_get_stacksize_np,
    // so the smaller main stack only lowers the recursion cap, never overflows.
    if (const char* mt = std::getenv("RAKUPP_MAIN_THREAD"))
        if (*mt && std::string(mt) != "0") return fn();
    struct Ctx { const std::function<int()>* fn; int rc; } ctx{&fn, 0};
#if defined(_WIN32)
    auto body = [](void* p) {
        auto* c = static_cast<Ctx*>(p);
        c->rc = (*c->fn)();
        std::cout.flush(); std::cerr.flush();
    };
    std::uintptr_t th = bigStackCreate(body, &ctx, (size_t)1 << 30); // 1 GiB
    if (th) { bigStackJoin(th); bigStackClose(th); }
    else ctx.rc = fn(); // creation failed: run inline
#else
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, (size_t)1 << 30); // 1 GiB
    pthread_t th;
    auto entry = [](void* p) -> void* {
        auto* c = static_cast<Ctx*>(p);
        c->rc = (*c->fn)();
        std::cout.flush(); std::cerr.flush();
        return nullptr;
    };
    if (pthread_create(&th, &attr, entry, &ctx) != 0) ctx.rc = fn(); // fallback: run inline
    else pthread_join(th, nullptr);
    pthread_attr_destroy(&attr);
#endif
    return ctx.rc;
}

// A COMPILED binary is NOT the interpreter: it carries one program and runs
// that one. `-e CODE` therefore has nothing to eval — and quietly passing it
// through as an ordinary argument means the binary re-runs its OWN program
// while the caller believes it ran the code it handed over.
//
// That is not theoretical. `t/regression/anton-batch-round2.raku` does
// `run $*EXECUTABLE, '-e', $code`, which under the interpreter is `rakupp -e …`.
// Compiled, `$*EXECUTABLE` is the binary, which re-ran its embedded program,
// reached the same line, and spawned another copy — each generation naming its
// temp dylib after the previous PID. It reached 1,253 processes and load average
// 450 during the v3.21.0 gate-4b run, which then reported ALL SLIM-DIFF CHECKS
// PASSED and left the chain running.
//
// Only the FIRST argument is examined, and only its exact `-e` / `--eval`
// spellings: that is the interpreter-invocation shape, and it leaves a compiled
// program free to take `-e=value` as a MAIN named argument, or its own `-e`
// anywhere else in its argv. Returns 0 to continue, or an exit code to return.
//
// Refusing `-e` is not enough on its own, because the confusion is not about
// `-e`: it is about $*EXECUTABLE. `t/regression/private-call-and-import-shadowing.raku`
// does `run $*EXECUTABLE, '-I', $dir, $prog` — no `-e` anywhere — and compiled
// that re-ran its own program, reached line 68 again, and spawned another copy.
// It reached 2,633 processes and load average 95 in about two minutes, with the
// `-e` guard already in place. So there are two rules here:
//
//   depth 0 — refuse only the unambiguous `-e`/`--eval`, which cannot mean
//             anything to a binary that embeds its program. Everything else a
//             top-level invocation passes is the program's own business: a
//             compiled linter invoked as `mylint foo.raku` must keep working.
//   depth 1 — refuse EVERYTHING. A compiled binary running a copy of ITSELF is
//             the $*EXECUTABLE confusion in every case this corpus contains, and
//             refusing at the first re-entry is what bounds the damage to
//             `1 + spawn-sites` short-lived processes instead of squaring it at
//             every generation.
//
// A program that re-executes itself ON PURPOSE sets RAKUPP_ALLOW_SELF_EXEC=1 and
// is left alone. That opt-out is deliberate: the guard cannot tell intent from
// argv, so the one who knows says so.
//
// Depth rides in the environment as `<count> <realpath>`, keyed to THIS binary,
// so a chain of different programs never accumulates a count.
static const char* kReentryVar = "RAKUPP_EXE_REENTRY";

static std::string selfPath(int argc, char** argv) {
    if (argc <= 0 || !argv[0]) return "";
    char rp[4096];
#ifdef _WIN32
    if (_fullpath(rp, argv[0], sizeof rp)) return rp;
#else
    if (realpath(argv[0], rp)) return rp;
#endif
    return argv[0];
}

static void setEnvVar(const char* name, const std::string& value) {
#ifdef _WIN32
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

int rakuppRefuseInterpreterEval(int argc, char** argv) {
    std::string me = selfPath(argc, argv);
    // The messages below deliberately do NOT name argv[0]. They are read by
    // tools/slim-diff.raku, which compares two builds of the same program
    // byte-for-byte, and those two builds differ only in their filename — so
    // naming it made every $*EXECUTABLE-spawning program report a spurious
    // "stderr differs (640 vs 640 chars)". The reader already knows what they
    // ran; a fixed prefix says everything the path would.

    // The variable is `<self> <total> <realpath>`: how many copies of THIS
    // binary are already above us, and how many rakupp-compiled binaries of any
    // kind are. The second number exists because the first can be defeated —
    // binary A spawning B spawning A resets A's count every time B rewrites the
    // path — and two programs calling each other is as unbounded as one calling
    // itself. Keying only on the path would have left that open.
    int depth = 0, total = 0;
    if (const char* prev = getenv(kReentryVar)) {
        std::string p = prev;
        size_t s1 = p.find(' ');
        size_t s2 = s1 == std::string::npos ? std::string::npos : p.find(' ', s1 + 1);
        if (s2 != std::string::npos) {
            total = std::atoi(p.substr(s1 + 1, s2 - s1 - 1).c_str());
            if (!me.empty() && p.substr(s2 + 1) == me)
                depth = std::atoi(p.substr(0, s1).c_str());
        }
    }
    // Generous, because a legitimate pipeline of distinct compiled tools could
    // plausibly nest a few deep; a runaway passes it in well under a second.
    const int kMaxTotal = 8;

    const char* allow = getenv("RAKUPP_ALLOW_SELF_EXEC");
    bool selfExecAllowed = allow && *allow && std::string(allow) != "0";

    if ((depth >= 1 || total >= kMaxTotal) && !selfExecAllowed) {
        std::cerr << "rakupp: refusing to run — this is a compiled binary re-running ITSELF.\n"
                  << "Something spawned $*EXECUTABLE expecting the interpreter. Compiled, that is\n"
                  << "this binary, which carries one program, so the spawn re-runs the program that\n"
                  << "did the spawning: a chain that grows until the machine stops. (One such chain\n"
                  << "reached 2,633 processes and load average 95.)\n"
                  << "If the code means `run $*EXECUTABLE, ...` to reach the Raku interpreter, it must\n"
                  << "name it: that is a different program from this one.\n"
                  << "If this program re-executes itself on purpose, set RAKUPP_ALLOW_SELF_EXEC=1.\n";
        // (the same message covers a ring of DISTINCT compiled binaries calling
        // one another, which the per-binary count alone cannot see)
        return 2;
    }

    if (argc >= 2) {
        std::string a1 = argv[1];
        if (a1 == "-e" || a1 == "--eval") {
            std::cerr << "rakupp: " << a1 << " is the INTERPRETER's flag, and this is a compiled binary.\n"
                      << "It carries one program — the one it was built from — so there is nothing to eval,\n"
                      << "and running it anyway would silently run that embedded program instead of your code.\n"
                      << "To evaluate source, use the interpreter: rakupp " << a1 << " 'CODE'\n";
            return 2;
        }
    }

    // record our own generation for any copy of us we go on to spawn
    if (!me.empty()) {
        setEnvVar(kReentryVar, std::to_string(depth + 1) + " "
                             + std::to_string(total + 1) + " " + me);
    }
    return 0;
}

int rakuppRunBigStack(const std::string& src, std::vector<std::string> args,
                      const std::string& fileName, const std::string& exePath,
                      const std::vector<std::string>& libPaths) {
    return onBigStack([&]() { return rakuppRun(src, std::move(args), fileName, exePath, libPaths); });
}

// Generated `--exe` binaries route their whole main() through this, so native
// programs get the same 1 GiB recursion budget as the interpreter on EVERY
// platform — link-time stack flags only cover macOS (-stack_size, capped at
// 512 MiB on arm64) and Windows (/STACK); Linux has no portable equivalent.
int rakuppMainOnBigStack(int (*body)(void*), void* ctx) {
    return onBigStack([&]() { return body(ctx); });
}

int rakuppRunProgramBigStack(Program& prog, std::vector<std::string> args,
                             const std::string& fileName, const std::string& exePath,
                             const std::string& finish) {
    return onBigStack([&]() { return rakuppRunProgram(prog, std::move(args), fileName, exePath, finish); });
}

}
