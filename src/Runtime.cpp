#include "Runtime.h"
#include "Interpreter.h"
#include "Lexer.h"
#include "Parser.h"
#include "Pod.h"
#include <cstdlib>
#include <functional>
#include <iostream>
#if defined(_WIN32)
#include "Platform.h"   // pulls <windows.h>
#include <process.h>    // _beginthreadex
#else
#include <pthread.h>
#endif

namespace rakupp {

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

int rakuppRun(const std::string& src, std::vector<std::string> args,
              const std::string& fileName, const std::string& exePath,
              const std::vector<std::string>& libPaths) {
    try {
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
        Program prog = parser.parseProgram();
        Interpreter interp;
        interp.setArgs(std::move(args));
        interp.finishData_ = finish;
        interp.podData_ = pod;
        interp.podDom_ = parsePod(src);   // $=pod structured DOM
        interp.docMode_ = g_docMode;
        interp.srcFile_ = fileName;
        interp.execPath_ = exePath;
        // -I <path> lib dirs take priority over the built-in / env-derived ones.
        interp.libPaths_.insert(interp.libPaths_.begin(), libPaths.begin(), libPaths.end());
        return interp.run(prog);
    } catch (const ParseError& e) {
        std::cerr << "===SORRY!=== Parse error at line " << e.line << ": " << e.what() << "\n";
        return 1; // a compile-time (syntax) error exits 1, like Rakudo
    } catch (const RakuError& e) {
        std::cerr << e.message << "\n";
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

int rakuppRunBigStack(const std::string& src, std::vector<std::string> args,
                      const std::string& fileName, const std::string& exePath,
                      const std::vector<std::string>& libPaths) {
    return onBigStack([&]() { return rakuppRun(src, std::move(args), fileName, exePath, libPaths); });
}

int rakuppRunProgramBigStack(Program& prog, std::vector<std::string> args,
                             const std::string& fileName, const std::string& exePath,
                             const std::string& finish) {
    return onBigStack([&]() { return rakuppRunProgram(prog, std::move(args), fileName, exePath, finish); });
}

}
