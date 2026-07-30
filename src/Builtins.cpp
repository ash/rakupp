#include "Interpreter.h"
#if !defined(_WIN32)
#include <sys/resource.h>
#endif
#include <cstdint>
#include <climits>
#include <limits>
#include <memory>
#include <cstdlib>
#include "Unicode.h"
#include <complex>
#include <functional>
#include "Regex.h"
#include "MethodName.h"
#include "BuiltinsShared.h"
#include <algorithm>
#include <atomic>
#include <ctime>
#include <fstream>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <set>
#include <sstream>
#include <vector>
#include "Platform.h"   // POSIX headers on Unix; Winsock + shims (incl. dirent) on Windows
#if !defined(_WIN32)
#include <dirent.h>
#endif
#include <csignal>
#include <sys/stat.h>
#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/utsname.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/wait.h>
#endif
#include <condition_variable>
#include <mutex>

namespace rakupp {

// Real synchronization state behind a Lock / Semaphore, shared by every copy of the
// Value via Value::ext. Only populated in parallel mode (RAKUPP_PARALLEL): under the
// cooperative GIL these primitives stay no-ops (the GIL already serialises), and a
// real lock held across a GIL-yield could deadlock the cooperative scheduler.

// A small hardcoded slice of the built-in type lattice (narrowest-first, widest-last),
// used by `.are` to find the narrowest common type of a list's elements.
// A CORE type name — the set `.^add_method` may extend. isKnownTypeName is too
// loose for that: it blanket-accepts any X::, Metamodel:: or IO:: prefix, so a
// typo'd `Metamodel::Whatever.^add_method` would silently succeed there.
bool isCoreTypeName(const std::string& n) {
    if (n.empty()) return false;
    if (n.rfind("X::", 0) == 0 || n.rfind("Metamodel::", 0) == 0) return false;
    return isKnownTypeName(n);
}

const std::vector<std::string>& typeAncestry(const std::string& t) {
    static const std::map<std::string, std::vector<std::string>> A = {
        {"Int",     {"Int","Real","Numeric","Cool","Any","Mu"}},
        {"IntStr",  {"IntStr","Int","Real","Numeric","Cool","Any","Mu"}},
        {"RatStr",  {"RatStr","Rat","Real","Numeric","Cool","Any","Mu"}},
        {"NumStr",  {"NumStr","Num","Real","Numeric","Cool","Any","Mu"}},
        {"Rat",     {"Rat","Real","Numeric","Cool","Any","Mu"}},
        {"FatRat",  {"FatRat","Rat","Real","Numeric","Cool","Any","Mu"}},
        {"Num",     {"Num","Real","Numeric","Cool","Any","Mu"}},
        {"Complex", {"Complex","Numeric","Cool","Any","Mu"}},
        {"Real",    {"Real","Numeric","Cool","Any","Mu"}},
        {"Numeric", {"Numeric","Cool","Any","Mu"}},
        {"Str",     {"Str","Cool","Any","Mu"}},
        {"Bool",    {"Bool","Cool","Any","Mu"}},
        {"Cool",    {"Cool","Any","Mu"}},
        {"Date",    {"Date","Dateish","Any","Mu"}},
        {"DateTime",{"DateTime","Dateish","Any","Mu"}},
    };
    static const std::vector<std::string> fallback = {"Any","Mu"};
    auto it = A.find(t);
    return it != A.end() ? it->second : fallback;
}
// The names in the ancestry table that are ROLES, not classes. `.does` counts
// them; `.isa` and `.^mro` do not (Rakudo: `Date.isa(Dateish)` is False).
bool isBuiltinRole(const std::string& n) {
    static const std::set<std::string> roles = {
        "Real", "Numeric", "Stringy", "Dateish", "Rational", "Callable",
        "Positional", "Associative", "Iterable", "Baggy", "Setty", "Mixy"};
    return roles.count(n) > 0;
}
std::string typeOfVal(const Value& v) { return v.t == VT::Type ? v.s : v.typeName(); }
std::string lubType(const std::string& a, const std::string& b) {
    if (a == b) return a;
    // an allomorph does BOTH its numeric type (in the ancestry) AND Str — a linear
    // ancestry can't hold the diamond, so pair it with Str/Stringy explicitly
    auto allo = [](const std::string& t) { return t == "IntStr" || t == "RatStr" || t == "NumStr" || t == "ComplexStr"; };
    if (allo(a) && (b == "Str" || b == "Stringy")) return b;
    if (allo(b) && (a == "Str" || a == "Stringy")) return a;
    for (auto& x : typeAncestry(a)) for (auto& y : typeAncestry(b)) if (x == y) return x;
    return "Mu";
}

#if !defined(_WIN32)
extern "C" { extern char **environ; } // swapped in the child for run(:env(...)) — a pointer store, async-signal-safe
#endif

// Build the "K=V\0K=V\0\0" block CreateProcessA wants from a K=V list.
#if defined(_WIN32)
static std::string winEnvBlock(const std::vector<std::string>& kvs) {
    std::string blk;
    for (auto& kv : kvs) { blk += kv; blk += '\0'; }
    blk += '\0';
    return blk;
}
#endif

// Spawn a child process, capture its stdout, with an optional wall-clock timeout.
// `gil` (if non-null) is the interpreter: the GIL is released for the child-process
// WAIT so sibling worker threads run — and spawn their own children — concurrently.
// The fork itself happens with the GIL held, so forks serialise (safe in a
// multithreaded process); only the poll/read/reap loop runs GIL-free.
static void spawnCapture(const std::vector<std::string>& argv, double timeoutSec,
                         std::string& out, int& exitCode, bool& timedout,
                         Interpreter* gil = nullptr, std::string* errOut = nullptr,
                         const std::string& cwd = "", long long* pidOut = nullptr,
                         const std::vector<std::string>* envKV = nullptr) {
    out.clear(); exitCode = -1; timedout = false;
    if (errOut) errOut->clear();
    if (argv.empty()) return;
#if defined(_WIN32)
    // Windows: CreateProcess with inherited pipes; poll the read ends via
    // PeekNamedPipe (bounded by the wall-clock timeout). Compile-verified under
    // mingw g++; behaviour mirrors the POSIX path below.
    SECURITY_ATTRIBUTES sa; sa.nLength = sizeof(sa); sa.lpSecurityDescriptor = nullptr; sa.bInheritHandle = TRUE;
    HANDLE outR = nullptr, outW = nullptr, errR = nullptr, errW = nullptr;
    if (!CreatePipe(&outR, &outW, &sa, 0)) return;
    SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);
    if (errOut) {
        if (!CreatePipe(&errR, &errW, &sa, 0)) { CloseHandle(outR); CloseHandle(outW); return; }
        SetHandleInformation(errR, HANDLE_FLAG_INHERIT, 0);
    }
    HANDLE nul = errOut ? INVALID_HANDLE_VALUE : CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    // Quote an argument only when it NEEDS it. Quoting unconditionally breaks a
    // command processor switch — cmd.exe does not recognise a quoted `"/c"` — and
    // `cmd.exe /c "…"` is exactly the shape shell() and the not-an-.exe fallback
    // below both produce.
    std::string cmd;
    for (size_t i = 0; i < argv.size(); i++) {
        if (i) cmd += ' ';
        const std::string& a0 = argv[i];
        bool needQuote = a0.empty() || a0.find_first_of(" \t\"") != std::string::npos;
        if (!needQuote) { cmd += a0; continue; }
        cmd += '"';
        for (char c : a0) { if (c == '"') cmd += '\\'; cmd += c; }
        cmd += '"';
    }
    STARTUPINFOA si; ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    // STARTF_USESTDHANDLES requires ALL THREE handles to be valid AND inheritable.
    // GetStdHandle does not guarantee either — under a host that redirected stdin,
    // or with no console at all, it can hand back INVALID_HANDLE_VALUE, and then
    // CreateProcess fails for every command. Fall back to NUL and mark it
    // inheritable rather than passing a handle the child cannot use.
    HANDLE inH = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE inNul = INVALID_HANDLE_VALUE;
    if (inH == nullptr || inH == INVALID_HANDLE_VALUE) {
        inNul = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ, &sa, OPEN_EXISTING, 0, nullptr);
        inH = inNul;
    }
    else SetHandleInformation(inH, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    si.hStdInput = inH;
    si.hStdOutput = outW;
    si.hStdError = errOut ? errW : nul;
    PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof(pi));
    std::vector<char> cmdbuf(cmd.begin(), cmd.end()); cmdbuf.push_back('\0');
    std::string envblk; if (envKV) envblk = winEnvBlock(*envKV);
    BOOL started = CreateProcessA(nullptr, cmdbuf.data(), nullptr, nullptr, TRUE, 0, envKV ? (LPVOID)envblk.data() : nullptr, cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
    DWORD spawnErr = started ? 0 : GetLastError();
    // Not an .exe? It may be a .bat/.cmd, or a name the command processor knows.
    // CreateProcess cannot start those directly, so retry through COMSPEC — which
    // is what makes `run 'somescript.bat'` work at all on Windows.
    std::vector<char> cmdbuf2;
    if (!started && (spawnErr == ERROR_FILE_NOT_FOUND || spawnErr == ERROR_BAD_EXE_FORMAT ||
                     spawnErr == ERROR_ACCESS_DENIED)) {
        const char* comspec = std::getenv("COMSPEC");
        std::string shellCmd = std::string(comspec && *comspec ? comspec : "cmd.exe") + " /c " + cmd;
        cmdbuf2.assign(shellCmd.begin(), shellCmd.end()); cmdbuf2.push_back('\0');
        started = CreateProcessA(nullptr, cmdbuf2.data(), nullptr, nullptr, TRUE, 0, envKV ? (LPVOID)envblk.data() : nullptr, cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
        if (!started) spawnErr = GetLastError();
    }
    CloseHandle(outW); if (errW) CloseHandle(errW); if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (inNul != INVALID_HANDLE_VALUE) CloseHandle(inNul);
    if (!started) {
        // A silent -1 with no output is undiagnosable — say WHY, on the error
        // stream when one was asked for, otherwise on our own stderr.
        char msg[512] = {0};
        FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                       spawnErr, 0, msg, sizeof msg - 1, nullptr);
        std::string text = "Could not spawn '" + argv[0] + "': " + msg;
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
        if (errOut) *errOut = text + "\n"; else std::cerr << text << "\n";
        CloseHandle(outR); if (errR) CloseHandle(errR);
        return;
    }
    if (pidOut) *pidOut = (long long)pi.dwProcessId;
    bool parked = gil ? gil->gilPark() : false;
    auto start = std::chrono::steady_clock::now();
    char buf[8192]; bool oEof = false, eEof = (errR == nullptr);
    auto drain = [&](HANDLE h, std::string* dst, bool& eof) {
        DWORD avail = 0;
        if (!PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr)) { eof = true; return; }
        while (avail > 0) {
            DWORD want = avail > sizeof buf ? (DWORD)sizeof buf : avail, rd = 0;
            if (!ReadFile(h, buf, want, &rd, nullptr) || rd == 0) { eof = true; return; }
            dst->append(buf, rd);
            if (!PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr)) { eof = true; return; }
        }
    };
    while (!oEof || !eEof) {
        if (!oEof) drain(outR, &out, oEof);
        if (!eEof) drain(errR, errOut, eEof);
        if (oEof && eEof) break;
        bool exited = WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0;
        if (timeoutSec > 0) {
            double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            if (el > timeoutSec) { TerminateProcess(pi.hProcess, 1); timedout = true; break; }
        }
        if (!exited) Sleep(2);
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD ec = 0; if (!timedout && GetExitCodeProcess(pi.hProcess, &ec)) exitCode = (int)ec;
    CloseHandle(outR); if (errR) CloseHandle(errR);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    if (parked) gil->gilUnpark(true);
    return;
#else
    // Build the argv vector BEFORE fork — malloc between fork and execvp is unsafe
    // in a multithreaded process (another thread can hold the allocator lock at
    // fork, deadlocking the child pre-exec).
    std::vector<char*> cargv;
    cargv.reserve(argv.size() + 1);
    for (auto& s : argv) cargv.push_back(const_cast<char*>(s.c_str()));
    cargv.push_back(nullptr);
    // run(:env(...)) — the replacement environment, built BEFORE fork (no malloc
    // in the child); the child swaps `environ` (a pointer store) before execvp
    std::vector<char*> cenv;
    if (envKV) {
        cenv.reserve(envKV->size() + 1);
        for (auto& kv : *envKV) cenv.push_back(const_cast<char*>(kv.c_str()));
        cenv.push_back(nullptr);
    }
    int pipefd[2], errfd[2];
    if (pipe(pipefd) != 0) return;
    if (errOut && pipe(errfd) != 0) { close(pipefd[0]); close(pipefd[1]); return; }
    pid_t pid = fork();
    if (pid < 0) { close(pipefd[0]); close(pipefd[1]); if (errOut) { close(errfd[0]); close(errfd[1]); } return; }
    if (pid == 0) { // child — async-signal-safe only from here
        setpgid(0, 0); // own process group, so a timeout can kill grandchildren too
        dup2(pipefd[1], STDOUT_FILENO);
        if (errOut) dup2(errfd[1], STDERR_FILENO);
        else { int devnull = open("/dev/null", O_WRONLY); if (devnull >= 0) dup2(devnull, STDERR_FILENO); }
        close(pipefd[0]); close(pipefd[1]);
        if (errOut) { close(errfd[0]); close(errfd[1]); }
        if (!cwd.empty()) { if (::chdir(cwd.c_str()) != 0) _exit(126); }
        if (envKV) environ = cenv.data();
        execvp(cargv[0], cargv.data());
        _exit(127);
    }
    if (pidOut) *pidOut = (long long)pid;
    // parent: don't let a concurrent spawn (another worker) inherit our read ends
    // across its execvp — that would keep the write end open and defer our EOF.
    fcntl(pipefd[0], F_SETFD, FD_CLOEXEC);
    if (errOut) fcntl(errfd[0], F_SETFD, FD_CLOEXEC);
    close(pipefd[1]);
    int fd = pipefd[0];
    fcntl(fd, F_SETFL, O_NONBLOCK);
    int efd = -1;
    if (errOut) { close(errfd[1]); efd = errfd[0]; fcntl(efd, F_SETFL, O_NONBLOCK); }
    bool parked = gil ? gil->gilPark() : false; // drop the GIL for the wait below
    auto start = std::chrono::steady_clock::now();
    char buf[8192];
    // Read until the pipe(s) reach EOF — i.e. every write-end (the child AND any
    // grandchildren it spawned) has closed. EOF, not the child's exit, is the only
    // reliable "all output captured" signal: reaping the child with waitpid does not
    // guarantee its final buffered write has been drained, so keying `done` off the
    // exit races the last line away. The wall-clock timeout still bounds the wait.
    bool oEof = false, eEof = (efd < 0);
    while (!oEof || !eEof) {
        struct pollfd pfds[2]; int nf = 0, oi = -1, ei = -1;
        if (!oEof) { pfds[nf] = {fd, POLLIN, 0}; oi = nf; nf++; }
        if (!eEof) { pfds[nf] = {efd, POLLIN, 0}; ei = nf; nf++; }
        poll(pfds, nf, 50);
        if (!oEof) for (;;) {
            ssize_t n = read(fd, buf, sizeof buf);
            if (n > 0) { out.append(buf, (size_t)n); continue; }
            if (n == 0) oEof = true;
            break;
        }
        if (!eEof) for (;;) {
            ssize_t n = read(efd, buf, sizeof buf);
            if (n > 0) { errOut->append(buf, (size_t)n); continue; }
            if (n == 0) eEof = true;
            break;
        }
        if (oEof && eEof) break;
        if (timeoutSec > 0) {
            double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            if (elapsed > timeoutSec) { kill(-pid, SIGKILL); kill(pid, SIGKILL); timedout = true; break; }
        }
    }
    int status = 0;
    while (waitpid(pid, &status, 0) == -1 && errno == EINTR) {} // reap; retry on EINTR
    if (!timedout) exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    close(fd);
    if (efd >= 0) close(efd);
    if (parked) gil->gilUnpark(true);    // reacquire the GIL before touching interpreter state
#endif
}

// Spawn a child, feed `input` to its stdin, and capture its stdout. Uses poll on
// both pipes so it won't deadlock when the child's output exceeds the pipe buffer
// while we're still writing input (as pandoc can on a large page).
void spawnWithInput(const std::vector<std::string>& argv, const std::string& input,
                           std::string& out, int& exitCode, Interpreter* gil,
                           const std::vector<std::string>* envKV, const std::string& cwd) {
    out.clear(); exitCode = -1;
    if (argv.empty()) return;
#if defined(_WIN32)
    SECURITY_ATTRIBUTES sa; sa.nLength = sizeof(sa); sa.lpSecurityDescriptor = nullptr; sa.bInheritHandle = TRUE;
    HANDLE inR = nullptr, inW = nullptr, outR = nullptr, outW = nullptr;
    if (!CreatePipe(&inR, &inW, &sa, 0)) return;
    SetHandleInformation(inW, HANDLE_FLAG_INHERIT, 0);
    if (!CreatePipe(&outR, &outW, &sa, 0)) { CloseHandle(inR); CloseHandle(inW); return; }
    SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    std::string cmd;
    for (size_t i = 0; i < argv.size(); i++) { if (i) cmd += ' '; cmd += '"'; for (char c : argv[i]) { if (c == '"') cmd += '\\'; cmd += c; } cmd += '"'; }
    STARTUPINFOA si; ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si); si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = inR; si.hStdOutput = outW; si.hStdError = nul;
    PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof(pi));
    std::vector<char> cmdbuf(cmd.begin(), cmd.end()); cmdbuf.push_back('\0');
    std::string envblk; if (envKV) envblk = winEnvBlock(*envKV);
    BOOL started = CreateProcessA(nullptr, cmdbuf.data(), nullptr, nullptr, TRUE, 0, envKV ? (LPVOID)envblk.data() : nullptr, cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
    CloseHandle(inR); CloseHandle(outW); if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!started) { CloseHandle(inW); CloseHandle(outR); return; }
    bool parked = gil ? gil->gilPark() : false;
    size_t written = 0; char buf[8192]; bool wOpen = true, done = false;
    while (!done) {
        if (wOpen) {
            if (written < input.size()) {
                DWORD want = (DWORD)((input.size() - written < sizeof buf) ? input.size() - written : sizeof buf), wn = 0;
                if (WriteFile(inW, input.data() + written, want, &wn, nullptr) && wn) written += wn;
                else { CloseHandle(inW); wOpen = false; }
            } else { CloseHandle(inW); wOpen = false; }
        }
        DWORD avail = 0;
        if (!PeekNamedPipe(outR, nullptr, 0, nullptr, &avail, nullptr)) break; // child's write end closed
        while (avail > 0) {
            DWORD want = avail > sizeof buf ? (DWORD)sizeof buf : avail, rd = 0;
            if (!ReadFile(outR, buf, want, &rd, nullptr) || rd == 0) { avail = 0; break; }
            out.append(buf, rd);
            if (!PeekNamedPipe(outR, nullptr, 0, nullptr, &avail, nullptr)) { done = true; break; }
        }
        if (!wOpen && !done && WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0) {
            DWORD a2 = 0; PeekNamedPipe(outR, nullptr, 0, nullptr, &a2, nullptr); if (a2 == 0) break;
        } else if (!wOpen) Sleep(2);
    }
    if (wOpen) CloseHandle(inW);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD ec = 0; if (GetExitCodeProcess(pi.hProcess, &ec)) exitCode = (int)ec;
    CloseHandle(outR); CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    if (parked) gil->gilUnpark(true);
    return;
#else
    std::vector<char*> cargv; // build argv before fork (no malloc between fork & exec)
    cargv.reserve(argv.size() + 1);
    for (auto& s : argv) cargv.push_back(const_cast<char*>(s.c_str()));
    cargv.push_back(nullptr);
    std::vector<char*> cenv; // run(:env(...)) — see spawnCapture
    if (envKV) {
        cenv.reserve(envKV->size() + 1);
        for (auto& kv : *envKV) cenv.push_back(const_cast<char*>(kv.c_str()));
        cenv.push_back(nullptr);
    }
    int inPipe[2], outPipe[2];
    if (pipe(inPipe) != 0) return;
    if (pipe(outPipe) != 0) { close(inPipe[0]); close(inPipe[1]); return; }
    pid_t pid = fork();
    if (pid < 0) { close(inPipe[0]); close(inPipe[1]); close(outPipe[0]); close(outPipe[1]); return; }
    if (pid == 0) { // child — async-signal-safe from here
        dup2(inPipe[0], STDIN_FILENO);
        dup2(outPipe[1], STDOUT_FILENO);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) dup2(devnull, STDERR_FILENO);
        close(inPipe[0]); close(inPipe[1]); close(outPipe[0]); close(outPipe[1]);
        if (!cwd.empty()) { if (::chdir(cwd.c_str()) != 0) _exit(126); }
        if (envKV) environ = cenv.data();
        execvp(cargv[0], cargv.data());
        _exit(127);
    }
    close(inPipe[0]); close(outPipe[1]);
    fcntl(inPipe[1], F_SETFD, FD_CLOEXEC); fcntl(outPipe[0], F_SETFD, FD_CLOEXEC);
    int wfd = inPipe[1], rfd = outPipe[0];
    fcntl(wfd, F_SETFL, O_NONBLOCK);
    fcntl(rfd, F_SETFL, O_NONBLOCK);
    signal(SIGPIPE, SIG_IGN);
    bool parked = gil ? gil->gilPark() : false; // drop the GIL for the feed/read wait below
    size_t written = 0;
    char buf[8192];
    bool rOpen = true, wOpen = true;
    while (rOpen || wOpen) {
        struct pollfd pfds[2]; int nf = 0;
        int ri = -1, wi = -1;
        if (rOpen) { pfds[nf] = {rfd, POLLIN, 0}; ri = nf; nf++; }
        if (wOpen) { pfds[nf] = {wfd, POLLOUT, 0}; wi = nf; nf++; }
        poll(pfds, nf, 50);
        if (rOpen && ri >= 0 && (pfds[ri].revents & (POLLIN | POLLHUP))) {
            ssize_t n;
            while ((n = read(rfd, buf, sizeof buf)) > 0) out.append(buf, (size_t)n);
            if (n == 0) { rOpen = false; close(rfd); }
        }
        if (wOpen && wi >= 0 && (pfds[wi].revents & POLLOUT)) {
            if (written < input.size()) {
                ssize_t n = write(wfd, input.data() + written, input.size() - written);
                if (n > 0) written += (size_t)n;
                else if (n < 0 && errno != EAGAIN) { wOpen = false; close(wfd); }
            }
            if (written >= input.size()) { wOpen = false; close(wfd); } // done: signal EOF to child
        }
    }
    int status = 0;
    while (waitpid(pid, &status, 0) == -1 && errno == EINTR) {}
    exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    if (parked) gil->gilUnpark(true); // reacquire the GIL before touching interpreter state
#endif
}

// Run one emitted value through a live-Supply tap's transform chain (grep/map/head/…).
// Threads the value(s) through each step in order; per-step mutable state lives in the
// step's "state" hash. Sets `complete` when a head/first step reaches its limit.
ValueList Interpreter::applyTapChain(Value& tap, const Value& in, bool& complete) {
    complete = false;
    ValueList cur{in};
    if (!(tap.t == VT::Hash && tap.hash->count("chain"))) return cur;
    for (auto& step : *(*tap.hash)["chain"].arr) {
        const std::string op = (*step.hash)["op"].toStr();
        Value arg = step.hash->count("arg") ? (*step.hash)["arg"] : Value::nil();
        Value& state = (*step.hash)["state"];
        auto sInt = [&](const char* k) -> long long { auto it = state.hash->find(k); return it == state.hash->end() ? 0 : it->second.toInt(); };
        ValueList next;
        for (auto& v : cur) {
            if (op == "map") { next.push_back(arg.t == VT::Code ? callCallable(arg, ValueList{v}) : v); }
            else if (op == "grep") {
                bool match;
                if (arg.t == VT::Code) match = callCallable(arg, ValueList{v}).truthy();
                else if (arg.t == VT::Regex) match = regexMatch(v.toStr(), arg.s).truthy();
                else match = applyArith("~~", v, arg).truthy();
                if (match) next.push_back(v);
            }
            else if (op == "skip") { long long n = arg.toInt(); long long c = sInt("c"); if (c < n) (*state.hash)["c"] = Value::integer(c + 1); else next.push_back(v); }
            else if (op == "head") {
                double lim = arg.t == VT::Nil ? 1 : (arg.t == VT::Whatever ? std::numeric_limits<double>::infinity() : arg.toNum());
                long long c = sInt("c");
                if (c < lim) { next.push_back(v); (*state.hash)["c"] = Value::integer(c + 1); if (c + 1 >= lim) complete = true; }
                else complete = true;
            }
            else if (op == "first") {
                bool match = true;
                if (arg.t == VT::Code) match = callCallable(arg, ValueList{v}).truthy();
                else if (arg.t == VT::Regex) match = regexMatch(v.toStr(), arg.s).truthy();
                else if (arg.t != VT::Nil) match = applyArith("~~", v, arg).truthy();
                if (match) { next.push_back(v); complete = true; }
            }
            else if (op == "unique" || op == "squish") {
                Value asF = step.hash->count("as") ? (*step.hash)["as"] : Value::nil();
                Value key = asF.t == VT::Code ? callCallable(asF, ValueList{v}) : v;
                std::string ks = key.toStr();
                if (op == "unique") {
                    // remember seen keys as hash entries in state
                    if (!state.hash->count("seen")) (*state.hash)["seen"] = Value::makeHash();
                    Value& seen = (*state.hash)["seen"];
                    if (!seen.hash->count(ks)) { (*seen.hash)[ks] = Value::boolean(true); next.push_back(v); }
                } else { // squish: drop only if equal to the immediately preceding key
                    bool same = state.hash->count("has") && (*state.hash)["prev"].toStr() == ks;
                    if (!same) next.push_back(v);
                    (*state.hash)["prev"] = Value::str(ks); (*state.hash)["has"] = Value::boolean(true);
                }
            }
            else next.push_back(v);
        }
        cur = std::move(next);
        if (complete) break;
    }
    return cur;
}

// Run a Proc::Async .start promise: spawn the process (with optional timeout), feed captured
// stdout to the Supply taps, and mark the promise Kept (finished) or Broken (timed out).
void Interpreter::runProcPromise(Value& promise, double timeoutSec) {
    if (!promise.hash) return;
    if (promise.hash->count("status") && (*promise.hash)["status"].toStr() != "Planned") return; // already run
    auto pit = promise.hash->find("proc");
    if (pit == promise.hash->end() || !pit->second.hash) { (*promise.hash)["status"] = Value::str("Kept"); return; }
    Value& proc = pit->second;
    std::vector<std::string> argv;
    if (proc.hash->count("argv")) for (auto& x : *(*proc.hash)["argv"].arr) argv.push_back(x.toStr());
    std::string out, err; int code; bool timedout;
    std::string cwd;
    { auto c = promise.hash->find("cwd"); if (c != promise.hash->end()) cwd = c->second.toStr(); }
    // stderr is CAPTURED (not inherited): an async proc's noise must go to its
    // .stderr taps (or nowhere), never straight to the user's terminal.
    spawnCapture(argv, timeoutSec, out, code, timedout, this, &err, cwd);
    auto feed = [&](const char* key, const std::string& data) {
        auto taps = proc.hash->find(key);
        if (taps == proc.hash->end() || !taps->second.arr) return;
        for (auto& cb : *taps->second.arr) {
            Value chunk = Value::str(data);
            chunk.hashKind = "Blob"; // stdout(:bin) taps get bytes (Buf.append needs a Blob)
            ValueList ca{chunk};
            callCallable(cb, ca);
        }
    };
    feed("taps", out);
    feed("taps-err", err);
    (*proc.hash)["exitcode"] = Value::integer(code);
    (*proc.hash)["timedout"] = Value::boolean(timedout);
    (*promise.hash)["status"] = Value::str(timedout ? "Broken" : "Kept");
}

// An attribute's SIGIL is a container type: `has @.a` holds an Array and
// `has %.h` a Hash, whatever shape the initialiser produced. Without this a
// `has @.a = (1,2)` kept the List and `has %.h = (a=>1)` kept the bare Pair, so
// `.WHAT` and the default renderer both disagreed with Rakudo.
Value coerceToSigil(Value v, char sigil) {
    if (sigil == '@') {
        if (v.t == VT::Array) { v.isList = false; v.itemized = false; return v; }
        if (v.t == VT::Nil || v.t == VT::Any) return v;
        Value a = Value::array();
        if (v.t == VT::Range) *a.arr = v.flatten(); else a.arr->push_back(v);
        return a;
    }
    if (sigil == '%') {
        if (v.t == VT::Hash) return v;
        if (v.t == VT::Nil || v.t == VT::Any) return v;
        Value h = Value::makeHash();
        ValueList items = v.t == VT::Array && v.arr ? *v.arr : ValueList{v};
        for (auto& e : items)
            if (e.t == VT::Pair) (*h.hash)[e.s] = e.pairVal ? *e.pairVal : Value::any();
        return h;
    }
    return v;
}

bool defined(const Value& v) { return v.t != VT::Nil && v.t != VT::Any && v.t != VT::Type && !(v.t == VT::Hash && v.hashKind == "Failure"); }

// √z from the MODULUS, the way Rakudo computes it — not std::sqrt(std::complex),
// whose libc++ form loses a ULP on the real part: (-3+4i).sqrt came out as
// 1.0000000000000002+2i instead of 1+2i. Both the method and the sub must use
// this, or `$z.sqrt` and `sqrt($z)` disagree.
Value complexSqrt(double re, double im) {
    double a = std::hypot(re, im);
    return Value::complex(std::sqrt((a + re) / 2.0),
                          std::copysign(std::sqrt((a - re) / 2.0), im));
}

// The declared KEY TYPE of an OBJECT hash (`my %h{Int}`), or "" for a plain one.
// Parser.cpp records it as the second half of declType ("Any,Int") and typedDefault
// copies that into ofType; `.keyof` reads the same half.
std::string objHashKeyType(const Value& h) {
    if (h.t != VT::Hash || !h.hashKind.empty()) return "";
    size_t c = h.ofType.find(',');
    return c == std::string::npos ? "" : h.ofType.substr(c + 1);
}

// The REAL key of a hash entry, from whichever of the three sources has it.
//
// The store is `map<std::string, Value>`, so a key is a lookup STRING and its
// original value has to come from somewhere else. A Set/Bag/Mix parks it in the
// count's pairKey; an object hash reconstructs it from the declared key type; a
// plain hash genuinely keys on Str. Every site that hands a key back to a program
// — .keys, .pairs, .kv, iteration, .raku — needs the same answer, and before this
// existed each spelled the first two cases out for itself (or, mostly, didn't).
//
// A key type we cannot rebuild from a string (a class, or bare Any/Mu, where
// `%h{3}` and `%h<3>` are different keys in Rakudo and the same one here) stays a
// Str. That is the pre-existing gap — Hash keys being plain strings — narrowed to
// where it actually bites rather than papered over with a guess.
Value hashEntryKey(const Value& h, const std::string& k, const Value& stored) {
    if (stored.pairKey) return *stored.pairKey;
    const std::string kt = objHashKeyType(h);
    if (kt.empty()) return Value::str(k);
    static const std::set<std::string> numericKey = {
        "Int", "UInt", "int", "Num", "num", "Rat", "FatRat", "Numeric", "Real", "Cool"
    };
    if (numericKey.count(kt)) {
        // numifyStr already picks the Raku-correct type ("33" -> Int, "1.5" -> Rat),
        // which is the same ladder that put the key there; a key that will not
        // numify (a Cool hash keyed by a Str) stays the Str it was.
        Value n = numifyStrFailure(k);
        if (n.isNumeric()) return n;
    }
    return Value::str(k);
}

// `.raku` / `.perl` — an EVAL-round-trippable representation of a value (as opposed
// to `.gist`, which is the human-readable form). Recursive over containers.
std::string rakuRepr(const Value& v, int depth, std::set<const void*>& seen);
// True while rendering an element of an `[…]` Array. Every Array slot is its own
// scalar container, so an itemized value nested there needs no `$` marker —
// Rakudo prints `[[1, 2],]`. Inside a `(…)` List the marker does matter.
static bool g_reprInArrayElem = false;
std::string rakuRepr(const Value& v) { std::set<const void*> seen; return rakuRepr(v, 0, seen); }
// Value.cpp renders Range endpoints with .raku; hand it this implementation.
static const bool g_rakuReprInstalled = ((g_rakuRepr = &rakuRepr), true);
void rejectNulPath(const std::string& path) {
    if (path.find('\0') != std::string::npos)
        throw RakuError{Value::typeObj("X::IO::Null"),
            "Cannot use null character (U+0000) as part of the path"};
}
static std::string rakuStrLit(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
        else if (c == '\n') o += "\\n";
        else if (c == '\t') o += "\\t";
        else if (c == '\r') o += "\\r";
        else if (c == '$' || c == '@' || c == '%' || c == '&' || c == '{') { o += '\\'; o += (char)c; } // would interpolate
        else if (c == '\0') o += "\\0";
        else o += (char)c;
    }
    return o + "\"";
}
static bool rakuIdentKey(const std::string& s) {
    if (s.empty() || !(std::isalpha((unsigned char)s[0]) || s[0] == '_')) return false;
    for (unsigned char c : s) if (!(std::isalnum(c) || c == '_' || c == '-')) return false;
    return true;
}
std::string rakuRepr(const Value& v, int depth, std::set<const void*>& seen) {
    // Guard against self-referential / deeply-nested data (`$foo<b> = $foo`): recursing
    // blindly builds an unbounded string and exhausts memory. Detect a revisited
    // container (a cycle) and stop; a large depth cap backstops pathological nesting.
    if (depth > 512) return "...";
    if (v.isAllomorph()) { // IntStr.new(42, "42") — round-trips via EVAL
        Value num = v; num.hashKind.clear();
        std::string face = num.s; num.s.clear();
        return v.typeName() + ".new(" + rakuRepr(num, depth + 1, seen) + ", " + rakuStrLit(face) + ")";
    }
    switch (v.t) {
        case VT::Nil:  return "Nil";
        case VT::Any:  return "Any";
        case VT::Bool: return v.b ? "Bool::True" : "Bool::False";
        case VT::Type: return v.s;
        case VT::Str:
            // a Buf/Blob is a Str only in REPRESENTATION — its .raku is the
            // constructor that rebuilds it, over its ELEMENTS, not a string
            // literal of its raw bytes
            if (v.hashKind == "Buf" || v.hashKind == "Blob") {
                std::string nm = !v.enumName.empty() ? v.enumName   // an encoding names its own type
                               : v.hashKind + (v.ofType.empty() ? "" : "[" + v.ofType + "]");
                std::string o = nm + ".new("; bool f = true;
                for (auto& e : v.blobList()) { if (!f) o += ","; f = false; o += std::to_string(e.toInt()); }
                return o + ")";
            }
            // an IO::Path's .raku is its full constructor, SPEC and CWD included
            // (its .gist is the short `"foo/bar".IO` form)
            if (v.hashKind == "IO") {
                char cbuf[4096];
                std::string cwd = v.ofType.empty()                      // an explicit :CWD wins
                                ? (getcwd(cbuf, sizeof cbuf) ? cbuf : ".")
                                : v.ofType;
                // a FLAVORED path names its flavor in the class, so it needs no
                // :SPEC; the plain one spells the spec out
                if (!v.enumName.empty())
                    return "IO::Path::" + v.enumName + ".new(" + rakuStrLit(v.s) +
                           ", :CWD(" + rakuStrLit(cwd) + "))";
                return "IO::Path.new(" + rakuStrLit(v.s) +
                       ", :SPEC(IO::Spec::Unix), :CWD(" + rakuStrLit(cwd) + "))";
            }
            if (v.hashKind == "CArray") { // a locally-built CArray rebuilds the same way
                std::string o = "CArray.new("; bool f = true;
                for (auto& e : v.blobList()) { if (!f) o += ","; f = false; o += std::to_string(e.toInt()); }
                return o + ")";
            }
            return rakuStrLit(v.s);
        case VT::Int:  return v.toStr();
        case VT::Rat: {
            std::string n = v.ratN ? v.ratN->toString() : "0";
            std::string d = v.ratD ? v.ratD->toString() : "1";
            if (v.fatRat) return "FatRat.new(" + n + ", " + d + ")"; // FatRat.raku is explicit
            // Terminating decimal (denominator 2^a·5^b) prints as a decimal literal
            // with a fraction part kept, so EVAL round-trips to Rat: 0.25, -7.0, 0.1.
            // Anything else (incl. zero-denominator, or a denominator wider than
            // uint64 — 0.9999999999999999999999.raku) is the <n/d> form.
            if (v.ratD && !v.ratD->isZero() && v.ratD->fitsU64()) {
                BigInt den = *v.ratD; int p2 = 0, p5 = 0; BigInt q, r;
                while (true) { BigInt::divmod(den, BigInt(2), q, r); if (!r.isZero()) break; den = q; p2++; }
                while (true) { BigInt::divmod(den, BigInt(5), q, r); if (!r.isZero()) break; den = q; p5++; }
                if (den.fitsLL() && den.toLL() == 1) {
                    int k = std::max(p2, p5);
                    BigInt scaled = *v.ratN;
                    for (int t = 0; t < k - p2; t++) scaled = scaled * BigInt(2);
                    for (int t = 0; t < k - p5; t++) scaled = scaled * BigInt(5);
                    std::string digits = scaled.toString();
                    bool neg = !digits.empty() && digits[0] == '-';
                    if (neg) digits.erase(0, 1);
                    while ((int)digits.size() <= k) digits.insert(0, "0");
                    std::string out = digits.substr(0, digits.size() - k) + "." +
                                      (k ? digits.substr(digits.size() - k) : "0");
                    if (!k) out = digits + ".0";
                    return (neg ? "-" : "") + out;
                }
            }
            return "<" + n + "/" + d + ">";
        }
        case VT::Num: { // Nums round-trip with an exponent so EVAL doesn't read a Rat
            std::string g = v.toStr();
            if (g == "Inf" || g == "-Inf" || g == "NaN") return g;
            if (g.find('e') == std::string::npos && g.find('E') == std::string::npos) {
                if (g.find('.') == std::string::npos) g += "e0";
                else g += "e0";
            }
            return g;
        }
        case VT::Regex:
            return v.s.find('/') == std::string::npos ? "rx/" + v.s + "/" : "rx{" + v.s + "}";
        case VT::Complex: return "<" + v.gist() + ">";
        case VT::Range:
            if (v.ofType == "Str") // Str range: quoted endpoint form
                return "\"" + cpToU8((uint32_t)v.rFrom) + "\"" + (v.rExFrom ? "^" : "") + ".." +
                       (v.rExTo ? "^" : "") + "\"" + cpToU8((uint32_t)v.rTo) + "\"";
            // `0..^N` is `^N` here too — Rakudo shows the short form for .raku as
            // well as gist. Same Int-zero-only rule as Value::gist.
            if (!v.rExFrom && v.rExTo && v.rFrom == 0 && !v.rNum)
                return "^" + std::to_string(v.rTo);
            return std::to_string(v.rFrom) + (v.rExFrom ? "^" : "") + ".." + (v.rExTo ? "^" : "") + std::to_string(v.rTo);
        case VT::Pair: {
            Value val = v.pairVal ? *v.pairVal : Value::nil();
            if (v.pairKey) { // non-string key (Int, nested Pair, …)
                std::string krepr = rakuRepr(*v.pairKey, depth + 1, seen);
                if (v.pairKey->t == VT::Pair) krepr = "(" + krepr + ")"; // parenthesize a pair-key
                return krepr + " => " + rakuRepr(val, depth + 1, seen);
            }
            return rakuIdentKey(v.s) ? ":" + v.s + "(" + rakuRepr(val, depth + 1, seen) + ")"
                                     : rakuStrLit(v.s) + " => " + rakuRepr(val, depth + 1, seen);
        }
        case VT::Array: {
            if (v.s == "Slip" && (!v.arr || v.arr->empty())) return "Empty";
            if (v.hashKind == "Capture") { // \(…) literal round-trips as itself
                std::string o = "\\(";
                bool first = true;
                if (v.arr) for (auto& e : *v.arr) {
                    if (!first) o += ", "; first = false;
                    if (e.t == VT::Pair) o += ":" + e.s + "(" + rakuRepr(e.pairVal ? *e.pairVal : Value(), depth + 1, seen) + ")";
                    else o += rakuRepr(e, depth + 1, seen);
                }
                return o + ")";
            }
            // Junctions render as their constructor form: none(1, 2, 3)
            if (!v.enumName.empty() && v.arr &&
                (v.enumName == "any" || v.enumName == "all" || v.enumName == "one" || v.enumName == "none")) {
                std::string o = v.enumName + "(";
                bool first = true;
                for (auto& e : *v.arr) { if (!first) o += ", "; first = false; o += rakuRepr(e, depth + 1, seen); }
                return o + ")";
            }
            if (v.arr && !seen.insert(v.arr.get()).second) return v.isList ? "(...)" : "[...]"; // cycle
            std::string o(1, v.isList ? '(' : '[');
            bool wasElem = g_reprInArrayElem;
            if (v.arr) {
                bool first = true;
                g_reprInArrayElem = !v.isList;
                for (auto& e : *v.arr) { if (!first) o += ", "; first = false; o += rakuRepr(e, depth + 1, seen); }
                g_reprInArrayElem = wasElem;
                if (v.isList && v.arr->size() == 1) o += ",";
                // a 1-element ARRAY holding an iterable disambiguates with a
                // trailing comma too: [1..5,] (else the raku form would flatten)
                if (!v.isList && v.arr->size() == 1 &&
                    ((*v.arr)[0].t == VT::Range || (*v.arr)[0].t == VT::Array ||
                     (*v.arr)[0].t == VT::Hash))
                    o += ",";
                seen.erase(v.arr.get());
            }
            o += v.isList ? ')' : ']';
            // an ITEMIZED container carries its `$` marker — `($t,)` for a
            // `$`-held list is `($(1, 2),)` — except as an ARRAY element, whose
            // slot itemizes anyway (`[$x,]` is `[[1, 2],]`)
            if (v.itemized && !wasElem) o = "$" + o;
            // a Seq's .raku is the list form plus the coercion that rebuilds it:
            // `(1, 2).Seq`. Only .raku carries it — .gist/.Str stay `(1 2)`.
            if (v.isList && v.s == "Seq") o += ".Seq";
            return o;
        }
        case VT::Hash: {
            if (v.hash && !seen.insert(v.hash.get()).second) return "{...}"; // cycle
            std::vector<std::string> keys;
            if (v.hash) for (auto& kv : *v.hash) keys.push_back(kv.first);
            std::sort(keys.begin(), keys.end());
            // A QuantHash renders as the expression that rebuilds it, not as a
            // Hash literal: the weighted kinds as a pair list coerced to the
            // kind, the Set family as a constructor over their elements, and a
            // Map as Map.new((…)).
            // An ELEMENT renders as itself, not as its key string: the key is a
            // lookup string, and the original value rides in the count's pairKey
            // (baggyKey). `set(1,2).raku` is `Set.new(1,2)`, not `Set.new("1","2")`.
            auto elemRepr = [&](const std::string& k) {
                const Value& cnt = v.hash->at(k);
                return cnt.pairKey ? rakuRepr(*cnt.pairKey, depth + 1, seen) : rakuStrLit(k);
            };
            if (v.hashKind == "Set" || v.hashKind == "SetHash") {
                std::string o = v.hashKind + ".new("; bool f = true;
                for (auto& k : keys) { if (!f) o += ","; f = false; o += elemRepr(k); }
                if (v.hash) seen.erase(v.hash.get());
                return o + ")";
            }
            if (v.hashKind == "Bag" || v.hashKind == "BagHash" ||
                v.hashKind == "Mix" || v.hashKind == "MixHash") {
                std::string o = "("; bool f = true;
                for (auto& k : keys) {
                    if (!f) o += ","; f = false;
                    o += elemRepr(k) + "=>" + rakuRepr(v.hash->at(k), depth + 1, seen);
                }
                if (v.hash) seen.erase(v.hash.get());
                return o + ")." + v.hashKind;
            }
            if (v.hashKind == "Map") {
                std::string o = "Map.new(("; bool f = true;
                for (auto& k : keys) {
                    if (!f) o += ","; f = false;
                    Value val = v.hash->at(k);
                    o += rakuIdentKey(k) ? ":" + k + "(" + rakuRepr(val, depth + 1, seen) + ")"
                                         : rakuStrLit(k) + " => " + rakuRepr(val, depth + 1, seen);
                }
                if (v.hash) seen.erase(v.hash.get());
                return o + "))";
            }
            // An OBJECT hash renders as the DECLARATION that rebuilds it —
            // `(my Any %{Int} = 3 => "a")` — because `{3 => "a"}` would round-trip
            // through EVAL as a plain Str-keyed hash and lose the constraint. The
            // entries are the same as below, except the key is its real value.
            const std::string okt = objHashKeyType(v);
            if (!okt.empty()) {
                std::string vt = v.ofType.substr(0, v.ofType.find(','));
                std::string o = "(my " + (vt.empty() ? "Any" : vt) + " %{" + okt + "}";
                bool f = true;
                for (auto& k : keys) {
                    o += f ? " = " : ", "; f = false;
                    Value val = v.hash->at(k);
                    if (val.t == VT::Array || val.t == VT::Hash) val.itemized = true;
                    std::string rv = rakuRepr(val, depth + 1, seen);
                    Value rk = hashEntryKey(v, k, v.hash->at(k));
                    o += (rk.t == VT::Str && rakuIdentKey(k))
                             ? ":" + k + "(" + rv + ")"
                             : rakuRepr(rk, depth + 1, seen) + " => " + rv;
                }
                if (v.hash) seen.erase(v.hash.get());
                return o + ")";
            }
            std::string o = "{"; bool first = true;
            for (auto& k : keys) {
                if (!first) o += ", "; first = false;
                Value val = v.hash->at(k);
                // Every hash VALUE sits in a Scalar container, so an Array or Hash
                // stored there is itemized whether or not the flag happens to be set
                // — `%h<k> = [1,2]` set it, `my %h = k => [1,2]` never did, and
                // Rakudo renders `$[1, 2]` for both.
                if (val.t == VT::Array || val.t == VT::Hash) val.itemized = true;
                bool wasElem = g_reprInArrayElem; g_reprInArrayElem = false;
                std::string rv = rakuRepr(val, depth + 1, seen);
                g_reprInArrayElem = wasElem;
                o += rakuIdentKey(k) ? ":" + k + "(" + rv + ")"
                                     : rakuStrLit(k) + " => " + rv;
            }
            if (v.hash) seen.erase(v.hash.get());
            o += "}";
            // an itemized hash (one held in a `$`) shows the same `$` marker a
            // list does — except as an Array element, whose slot itemizes anyway
            if (v.itemized && !g_reprInArrayElem) o = "$" + o;
            return o;
        }
        case VT::Object: {
            if (!v.obj || !v.obj->cls) return v.gist();
            std::string r = v.obj->cls->name + ".new";
            // INHERITED attributes count: iterating only cls->attrs dropped every
            // one, so `class Q is P` reprd as `Q.new(q => 2)` and would not survive
            // a round trip through EVAL.
            std::vector<const ClassAttr*> pub;
            collectPubAttrs(v.obj->cls.get(), pub);
            std::string inner;
            for (auto* at : pub) {
                auto it = v.obj->attrs.find(at->name);
                // an UNSET typed attribute shows its declared type (`i => Int`)
                Value av = it != v.obj->attrs.end() ? it->second
                         : (at->type.empty() ? Value::any() : Value::typeObj(at->type));
                if (av.t == VT::Any && !at->type.empty()) av = Value::typeObj(at->type);
                if (!inner.empty()) inner += ", ";
                inner += at->name + " => " + rakuRepr(av, depth + 1, seen);
            }
            return inner.empty() ? r : r + "(" + inner + ")";
        }
        default: return v.gist();
    }
}

// Adverbs the shared occurrence-selection code (substSelect) understands. An
// ordinal written as one token (`:2nd`, `:3x`) counts too. `.match` routes
// through that code only when EVERY adverb is one of these — `:overlap` and
// `:exhaustive` are not implemented there and it THROWS on an unknown name,
// which would abort the caller rather than degrade to a plain match.
bool substSelectKnowsAdverb(const std::string& k) {
    static const std::set<std::string> known = {
        "g", "global", "x", "nth", "st", "nd", "rd", "th", "p", "pos",
        "c", "continue", "i", "ignorecase", "samecase", "ii", "s", "sigspace",
        "samespace", "ss", "samemark", "mm", "m", "ignoremark"};
    if (known.count(k)) return true;
    if (k.size() >= 2 && std::isdigit((unsigned char)k[0])) { // :2nd / :3x
        size_t d = 0; while (d < k.size() && std::isdigit((unsigned char)k[d])) d++;
        std::string suf = k.substr(d);
        return suf == "st" || suf == "nd" || suf == "rd" || suf == "th" || suf == "x";
    }
    return false;
}
// Does `v` satisfy a MATCHER argument (the thing `.grep`/`.first` take)?
// A Regex goes through the engine — the generic `~~` in applyArith does not know
// regexes — and a JUNCTION of matchers is tested eigenstate by eigenstate so a
// regex inside one still matches (`.grep(none /<[aeiou]>/)`).
bool matcherAccepts(Interpreter& I, const Value& v, const Value& mt) {
    if (mt.t == VT::Regex) return I.regexMatch(v.toStr(), mt.s).truthy();
    if (mt.t == VT::Array && mt.arr &&
        (mt.enumName == "any" || mt.enumName == "all" ||
         mt.enumName == "one" || mt.enumName == "none")) {
        size_t hits = 0;
        for (auto& e : *mt.arr) if (matcherAccepts(I, v, e)) hits++;
        if (mt.enumName == "any")  return hits > 0;
        if (mt.enumName == "all")  return hits == mt.arr->size();
        if (mt.enumName == "one")  return hits == 1;
        return hits == 0;                                     // none
    }
    if (mt.t == VT::Code) return I.callCallable(const_cast<Value&>(mt), ValueList{v}).truthy();
    return applyArith("~~", v, mt).truthy();
}

// The positional arity of a Code value — how many elements `.map`/`for` feed it
// per iteration (`-> $k,$v {…}` → 2; `{ $^a … $^b }` → 2; `{ $_ }` / builtin → 1).
size_t codeArity(const Value& code) {
    if (code.t != VT::Code || !code.code) return 1;
    if (code.code->params) {
        size_t n = 0;
        for (auto& p : *code.code->params) if (!p.named && !p.slurpy) n++;
        if (n) return n;
    }
    if (!code.code->placeholders.empty()) return code.code->placeholders.size();
    return 1;
}

// ---- UTF-8 / codepoint helpers ----
std::vector<uint32_t> utf8cp(const std::string& s) {
    std::vector<uint32_t> out;
    size_t i = 0, n = s.size();
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        uint32_t cp; int len;
        if (c < 0x80) { cp = c; len = 1; }
        else if ((c >> 5) == 0x6) { cp = c & 0x1f; len = 2; }
        else if ((c >> 4) == 0xe) { cp = c & 0x0f; len = 3; }
        else if ((c >> 3) == 0x1e) { cp = c & 0x07; len = 4; }
        else { cp = c; len = 1; }
        for (int k = 1; k < len && i + k < n; k++) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3f);
        out.push_back(cp); i += len;
    }
    return out;
}
// Drop every combining mark, keeping ONE base character per grapheme — the
// folding `:ignoremark` compares through. Character positions survive it, so an
// index into the folded text is an index into the original.
std::string markFold(const std::string& in) {
    std::vector<uint32_t> cps;
    for (uint32_t c : utf8cp(in)) cps.push_back(c);
    std::string out;
    for (size_t i = 0; i < cps.size();) {
        std::vector<uint32_t> g{cps[i++]};
        while (i < cps.size() && rakupp::uniCombiningClass(cps[i]) != 0) g.push_back(cps[i++]);
        bool wrote = false;
        for (uint32_t cp : rakupp::uniNormalize(g, 0))
            if (rakupp::uniCombiningClass(cp) == 0 && !wrote) { out += cpToU8(cp); wrote = true; }
        if (!wrote) out += cpToU8(g[0]); // a lone combining mark stands for itself
    }
    return out;
}
// A CHARACTER offset as a byte offset (Raku positions count characters).
size_t charToByte(const std::string& s, long long chars) {
    if (chars <= 0) return 0;
    size_t b = 0; long long n = 0;
    while (b < s.size() && n < chars) {
        b++;
        while (b < s.size() && (static_cast<unsigned char>(s[b]) & 0xC0) == 0x80) b++;
        n++;
    }
    return b;
}
std::string cpToUtf8(uint32_t cp) {
    std::string r;
    if (cp < 0x80) r += (char)cp;
    else if (cp < 0x800) { r += (char)(0xC0 | (cp >> 6)); r += (char)(0x80 | (cp & 0x3f)); }
    else if (cp < 0x10000) { r += (char)(0xE0 | (cp >> 12)); r += (char)(0x80 | ((cp >> 6) & 0x3f)); r += (char)(0x80 | (cp & 0x3f)); }
    else { r += (char)(0xF0 | (cp >> 18)); r += (char)(0x80 | ((cp >> 12) & 0x3f)); r += (char)(0x80 | ((cp >> 6) & 0x3f)); r += (char)(0x80 | (cp & 0x3f)); }
    return r;
}
// Simple (1:1) case mappings from the full UnicodeData tables.
uint32_t toLowerCp(uint32_t c) { return uniSimpleLower(c); }
uint32_t toUpperCp(uint32_t c) { return uniSimpleUpper(c); }
// Grapheme-level case change (NFG-aware), driven by the full Unicode case
// tables. `kind`: 0=lc, 1=uc, 3=fc (fold). `tcMode`: 0 = map every grapheme
// with `kind`; 1 = titlecase the first grapheme only (rest unchanged); 2 =
// titlecase first, lowercase the rest (.tclc).
//
// Within a grapheme cluster the base may expand (ﬀ→FF, ǰ→J+◌̌): the FIRST
// resulting codepoint takes the cluster's position, the cluster's own combining
// marks follow it, and any remaining expansion codepoints trail after — so
// "ﬀ+◌̣".uc is F, ◌̣, F (the combiner stays with the first F). The whole result
// is then NFC-normalised back to NFG.
std::string mapCase(const std::string& s, int kind, int tcMode) {
    auto cps = utf8cp(s);
    if (cps.empty()) return s;
    auto starts = uniGraphemeStarts(cps);
    std::vector<uint32_t> out;
    out.reserve(cps.size());
    for (size_t gi = 0; gi < starts.size(); gi++) {
        size_t b = starts[gi], e = (gi + 1 < starts.size()) ? starts[gi + 1] : cps.size();
        int k;                                   // -1 = leave this grapheme unchanged
        if (tcMode) k = (gi == 0) ? 2 : (tcMode == 2 ? 0 : -1);
        else        k = kind;
        std::vector<uint32_t> tail;
        for (size_t i = b; i < e; i++) {
            if (k < 0) { out.push_back(cps[i]); continue; }
            auto m = uniCaseMap(cps[i], k);
            out.push_back(m[0]);
            for (size_t j = 1; j < m.size(); j++) tail.push_back(m[j]);
        }
        for (uint32_t t : tail) out.push_back(t);
    }
    std::string r; r.reserve(s.size());
    for (uint32_t c : out) r += cpToUtf8(c);
    return nfcNormalize(r); // a case change is NFG-normalised (Ι+◌̈ composes to Ϊ)
}
long long cpCount(const std::string& s) { return (long long)utf8cp(s).size(); }

// NFC-normalise a UTF-8 string — Raku stores strings in NFG (NFC of graphemes),
// so `"e" ~ "\x[301]"` composes to "é" (1 codepoint). Pure-ASCII is already NFC
// (the hot path), and a string with no composable combiners returns unchanged.
std::string nfcNormalize(std::string s) { // by value: the ASCII fast path moves through, no copy
    bool ascii = true;
    for (unsigned char c : s) if (c >= 0x80) { ascii = false; break; }
    if (ascii) return s;
    auto cps = utf8cp(s);
    auto norm = uniNormalize(cps, 1 /*NFC*/);
    if (norm == cps) return s;
    std::string out; out.reserve(s.size());
    for (uint32_t cp : norm) out += cpToUtf8(cp);
    return out;
}

// Unicode combining marks (Mn/Mc/Me — the common ranges) — they attach to the preceding grapheme.
// Count grapheme clusters via the full UAX #29 algorithm (emoji/flags/Hangul-aware).
long long graphemeCount(const std::string& s) { return (long long)uniGraphemeCount(utf8cp(s)); }

// Rakudo dies opening a missing file for reading ("Failed to open file
// /abs/path: No such file or directory") — match it, absolute path included.
[[noreturn]] void throwFailedOpen(const std::string& path) {
    std::string abs = path;
    if (abs.empty() || (abs[0] != '/' && !(abs.size() > 1 && abs[1] == ':'))) {
        char buf[4096];
        if (getcwd(buf, sizeof buf)) abs = std::string(buf) + "/" + path;
    }
    throw RakuError{Value::typeObj("X::IO::Open"),
                    "Failed to open file " + abs + ": No such file or directory"};
}

std::string joinValues(const ValueList& items, const std::string& sep) {
    std::string out;
    for (size_t i = 0; i < items.size(); i++) {
        if (i) out += sep;
        out += items[i].toStr();
    }
    return out;
}

// A lazy @-array over the integers from `start` upward (an infinite `…..Inf` range).
Value makeInfArray(long long start) {
    Value a = Value::array(); a.isList = true;
    auto st = std::make_shared<LazySeqState>(); st->infinite = true;
    auto next = std::make_shared<long long>(start);
    st->appendNext = [next](ValueList& cache) -> bool { cache.push_back(Value::integer((*next)++)); return true; };
    a.ext = st;
    return a;
}

ValueList toList(const Value& v) {
    if (v.t == VT::Array && v.arr) return *v.arr;
    if (v.t == VT::Range) return v.flatten();
    // a Blob/Buf lists as its ELEMENTS (`$blob.rotor(3, :partial)` in Base64;
    // 32-bit words for blob32) — mirrors the `for`-iteration rule in the
    // interpreter (itemized stays one item)
    if (v.t == VT::Str && !v.itemized && (v.hashKind == "Blob" || v.hashKind == "Buf"))
        return v.blobList();
    if (v.t == VT::Hash && v.hash) {
        ValueList out;
        // The object-hash test is hoisted: this is the hot path for every hash
        // iteration, and a plain hash must cost exactly what it did before —
        // one shared_ptr copy, not a Value built and thrown away per entry.
        const bool objHash = !objHashKeyType(v).empty();
        for (auto& kv : *v.hash) {
            Value p = Value::pair(kv.first, kv.second);
            if (kv.second.pairKey) p.pairKey = kv.second.pairKey;
            else if (objHash) {
                Value rk = hashEntryKey(v, kv.first, kv.second);
                if (rk.t != VT::Str) p.pairKey = std::make_shared<Value>(std::move(rk));
            }
            out.push_back(std::move(p));
        }
        return out;
    }
    return {v};
}

// flatten all args (used by say/join/etc.)
static ValueList flattenArgs(ValueList& args) {
    ValueList out;
    for (auto& a : args) {
        if (a.t == VT::Array || a.t == VT::Range) {
            ValueList sub = a.flatten();
            out.insert(out.end(), sub.begin(), sub.end());
        } else out.push_back(a);
    }
    return out;
}

// simple sprintf supporting %s %d %i %x %o %b %c %f %g %e %%
// Format an integer in an arbitrary radix honouring the printf flag/width/precision
// grammar with Raku's conventions: sign-magnitude for negatives, `#` prefixes 0b/0o/0x,
// precision = minimum digits (precision 0 of value 0 → empty), `0` flag ignored with a
// precision or with left-justify.
static std::string fmtRadix(long long val, int base, bool upper, const std::string& flags,
                            int width, int prec, bool signFlags, int langRev = 1) {
    bool neg = val < 0;
    unsigned long long u = neg ? (unsigned long long)(0 - (unsigned long long)val)
                               : (unsigned long long)val;
    const char* dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    std::string digits;
    if (u == 0) digits = "0";
    else while (u) { digits = std::string(1, dig[u % base]) + digits; u /= base; }
    if (prec >= 0) {
        if (val == 0 && prec == 0) digits = "";
        else if ((int)digits.size() < prec) digits = std::string(prec - digits.size(), '0') + digits;
    }
    // `#` prefix: 0b/0B for binary, 0x/0X for hex; octal forces a single leading 0
    // (skipped when the digits already begin with 0, matching C/Raku).
    std::string prefix;
    if (flags.find('#') != std::string::npos && val != 0) {
        if (base == 2)       prefix = upper ? "0B" : "0b";
        else if (base == 16) prefix = upper ? "0X" : "0x";
        else if (base == 8 && !digits.empty() && digits[0] != '0') prefix = "0";
    }
    // A radix value that formats to no digits (precision 0 of 0) drops sign/prefix entirely:
    // sprintf("% .0b",0) → '' (only space width padding applies). Decimal is the exception —
    // sprintf("% .0d",0) → ' ' — so it falls through to keep the sign flag.
    if (digits.empty() && base != 10) {
        if ((int)width > 0) return std::string(width, ' ');
        return "";
    }
    // The +/space sign flags apply only to signed conversions (d/i/b); o/x/X/u ignore them.
    std::string sign;
    if (neg) sign = "-";
    else if (signFlags && flags.find('+') != std::string::npos) sign = "+";
    else if (signFlags && flags.find(' ') != std::string::npos) sign = " ";
    // 6.e (langRev>=2) puts the sign first for every base: sprintf("%#x",-256) →
    // "-0x100". 6.c/6.d keep the historical "bogus" prefix-before-sign for octal/hex
    // ("0x-100", "0-100" — roast's 6.d sprintf files assert exactly this); binary
    // always keeps the sign first.
    bool prefixFirst = (langRev < 2) && (base == 8 || base == 16);
    std::string core = prefixFirst ? prefix + sign + digits : sign + prefix + digits;
    if ((int)core.size() < width) {
        int pad = width - (int)core.size();
        if (flags.find('-') != std::string::npos) core += std::string(pad, ' ');
        else if (flags.find('0') != std::string::npos && prec < 0)
            // zero-pad fills after the prefix; for 6.c/6.d octal/hex it sits before
            // the sign, for 6.e (and binary) after sign+prefix.
            core = prefixFirst ? prefix + std::string(pad, '0') + sign + digits
                               : sign + prefix + std::string(pad, '0') + digits;
        else core = std::string(pad, ' ') + core;
    }
    return core;
}


// Digits of a BigInt in the given radix (2/8/16), sign included — for %b/%o/%x
// on arbitrary-precision Ints (toInt() would truncate at 64 bits).
static std::string bigRadixDigits(const BigInt& v, int radix, bool upper) {
    static const char* lo = "0123456789abcdef";
    static const char* up = "0123456789ABCDEF";
    const char* digs = upper ? up : lo;
    BigInt n = v.abs(), r10(radix);
    if (n.isZero()) return "0";
    std::string out;
    while (!n.isZero()) {
        BigInt q, r;
        BigInt::divmod(n, r10, q, r);
        out += digs[(int)r.toLL()];
        n = q;
    }
    if (v.sign < 0) out += '-';
    std::string rev(out.rbegin(), out.rend());
    return rev;
}

// Format an exact decimal-digit string (BigInt) for %d: honors width and the
// '-', '0', '+', ' ' flags (precision on integers is rare; digits stay exact).
static std::string fmtBigDec(std::string digits, const std::string& flags, long long width) {
    bool neg = !digits.empty() && digits[0] == '-';
    std::string sign = neg ? "-" : (flags.find('+') != std::string::npos ? "+" :
                                    flags.find(' ') != std::string::npos ? " " : "");
    if (neg) digits = digits.substr(1);
    std::string body = sign + digits;
    if ((long long)body.size() >= width) return body;
    if (flags.find('-') != std::string::npos) return body + std::string(width - body.size(), ' ');
    if (flags.find('0') != std::string::npos)
        return sign + std::string(width - body.size(), '0') + digits;
    return std::string(width - body.size(), ' ') + body;
}

std::string doSprintf(const std::string& fmt, const ValueList& args, int langRev) {
    std::string out;
    size_t ai = 0;
    auto nextArg = [&]() -> Value { return ai < args.size() ? args[ai++] : Value::any(); };
    for (size_t i = 0; i < fmt.size(); i++) {
        if (fmt[i] != '%') { out += fmt[i]; continue; }
        size_t j = i + 1;
        // explicit positional argument: %2$s (1-based index into the args)
        {
            size_t d = j;
            while (d < fmt.size() && std::isdigit((unsigned char)fmt[d])) d++;
            if (d > j && d < fmt.size() && fmt[d] == '$') {
                ai = (size_t)std::atoll(fmt.substr(j, d - j).c_str()) - 1;
                j = d + 1;
            }
        }
        std::string flags;
        while (j < fmt.size() && std::strchr("-+ 0#", fmt[j])) flags += fmt[j++];
        // width (digits or `*` = from argument; negative `*` implies left-justify)
        const long long SPRINTF_MAX = 10'000'000; // guard against int overflow (UB) and multi-GB pads
        int width = 0; bool hasWidth = false;
        if (j < fmt.size() && fmt[j] == '*') { j++; long long w = nextArg().toInt();
            if (w < 0) { flags += '-'; w = -w; } if (w > SPRINTF_MAX) w = SPRINTF_MAX; width = (int)w; hasWidth = true; }
        else { long long w = 0;
            while (j < fmt.size() && std::isdigit((unsigned char)fmt[j])) { w = w * 10 + (fmt[j]-'0'); if (w > SPRINTF_MAX) w = SPRINTF_MAX; hasWidth = true; j++; }
            width = (int)w; }
        // precision (.digits or .* ; a negative `.*` means "no precision")
        int prec = -1;
        if (j < fmt.size() && fmt[j] == '.') { j++; prec = 0;
            if (j < fmt.size() && fmt[j] == '*') { j++; long long p = nextArg().toInt(); prec = p < 0 ? -1 : (int)std::min(p, SPRINTF_MAX); }
            else { long long p = 0; while (j < fmt.size() && std::isdigit((unsigned char)fmt[j])) { p = p * 10 + (fmt[j]-'0'); if (p > SPRINTF_MAX) p = SPRINTF_MAX; j++; } prec = (int)p; }
        }
        while (j < fmt.size() && std::strchr("lhqLVjzt", fmt[j])) j++; // length modifiers, ignored
        if (j >= fmt.size()) break;
        char conv = fmt[j];
        switch (conv) {
            case '%': out += '%'; break;
            case 'd': case 'i': {
                // an arbitrary-precision Int (or a Rat/Num too big for long long)
                // formats from its exact decimal digits, not a saturated toInt()
                Value av = nextArg();
                if (av.t == VT::Int && av.big) { out += fmtBigDec(av.big->toString(), flags, width); break; }
                if (av.t == VT::Rat && av.ratN && av.ratD && !av.ratD->isZero()) {
                    BigInt q, r; BigInt::divmod(*av.ratN, *av.ratD, q, r);
                    if (q.toString().size() > 18) { out += fmtBigDec(q.toString(), flags, width); break; }
                }
                out += fmtRadix(av.toInt(), 10, false, flags, width, prec, true); break;
            }
            case 'u': case 'b': case 'B': case 'o': case 'x': case 'X': {
                int radix = (conv == 'u') ? 10 : (conv == 'o') ? 8 : (conv == 'x' || conv == 'X') ? 16 : 2;
                bool upper = (conv == 'B' || conv == 'X');
                bool prefixable = (conv == 'b' || conv == 'B');
                // 6.e: the space and + flags no longer apply to binary (`% b`, `%+b`) —
                // o/x already ignore them; the # prefix flag is kept.
                std::string flags2 = flags;
                if (langRev >= 2 && (conv == 'b' || conv == 'B')) {
                    size_t p2; while ((p2 = flags2.find(' ')) != std::string::npos) flags2.erase(p2, 1);
                    while ((p2 = flags2.find('+')) != std::string::npos) flags2.erase(p2, 1);
                }
                Value av = nextArg();
                if (av.t == VT::Int && av.big) { // arbitrary-precision: exact digits
                    out += fmtBigDec(bigRadixDigits(*av.big, radix, upper), flags2, width);
                    break;
                }
                if (av.t == VT::Rat && av.ratN && av.ratD && !av.ratD->isZero()) {
                    BigInt q, r;
                    BigInt::divmod(*av.ratN, *av.ratD, q, r); // truncate toward zero
                    if (q.toString().size() > 18) {
                        out += fmtBigDec(bigRadixDigits(q, radix, upper), flags2, width);
                        break;
                    }
                }
                out += fmtRadix(av.toInt(), radix, upper, flags2, width, prec, prefixable, langRev);
                break;
            }
            case 'c': { // codepoint → UTF-8; width counts characters, not bytes
                uint32_t cp = (uint32_t)nextArg().toInt();
                std::string s;
                if (cp < 0x80) s += (char)cp;
                else if (cp < 0x800) { s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 0x3F)); }
                else if (cp < 0x10000) { s += (char)(0xE0 | (cp >> 12)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
                else { s += (char)(0xF0 | (cp >> 18)); s += (char)(0x80 | ((cp >> 12) & 0x3F)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
                int pad = width - 1; // one character
                if (pad > 0) { char fill = (flags.find('0') != std::string::npos && flags.find('-') == std::string::npos) ? '0' : ' ';
                    s = (flags.find('-') != std::string::npos) ? s + std::string(pad,' ') : std::string(pad,fill) + s; }
                out += s; break; }
            case 'e': case 'E': case 'f': case 'F': case 'g': case 'G': case 'a': case 'A': {
                // `#` on a float is version-split: 6.e honors it (forces the decimal
                // point — sprintf("%#.0f",0) → "0."), 6.c/6.d ignore it (→ "0").
                std::string ff;
                for (char c : flags) if (c != '#' || langRev >= 2) ff += c;
                double fv = nextArg().toNum();
                // `%f` with both `-` and `0` is version-split. 6.e: `0` wins — the
                // value is zero-padded, not left-justified (opposite of C), precision
                // unchanged (sprintf("%-08.2f",0) → "00000.00"). 6.c/6.d: the historical
                // "bogus but provided" form — a non-negative value with no sign flag is
                // formatted with precision+1, zero-padded ("%-08.2f",0 → "0000.000").
                if ((conv == 'f' || conv == 'F') &&
                    ff.find('-') != std::string::npos && ff.find('0') != std::string::npos) {
                    if (langRev >= 2) {
                        std::string t; for (char c : ff) if (c != '-') t += c; ff = t;
                    } else if (prec >= 0 && hasWidth) {
                        bool signFlag = ff.find('+') != std::string::npos || ff.find(' ') != std::string::npos;
                        std::string sf = ff.find('+') != std::string::npos ? "+"
                                       : ff.find(' ') != std::string::npos ? " " : "";
                        int p = (!signFlag && fv >= 0) ? prec + 1 : prec;
                        std::string spec = "%" + sf + "0" + std::to_string(width) + "." + std::to_string(p) + "f";
                        std::vector<char> buf(std::max(64, width + prec + 64));
                        snprintf(buf.data(), buf.size(), spec.c_str(), fv);
                        out += buf.data(); break;
                    }
                }
                std::string spec = "%" + ff;
                if (hasWidth) spec += std::to_string(width);
                if (prec >= 0) spec += "." + std::to_string(prec);
                spec += conv;
                std::vector<char> buf(std::max(64, width + prec + 64));
                snprintf(buf.data(), buf.size(), spec.c_str(), fv);
                std::string fs = buf.data();
                if (std::isnan(fv) || std::isinf(fv)) { // Raku spells them NaN / Inf / -Inf
                    for (const char* bad : {"nan", "NAN", "inf", "INF"}) {
                        size_t at = fs.find(bad);
                        if (at != std::string::npos) fs.replace(at, 3, bad[0]=='n'||bad[0]=='N' ? "NaN" : "Inf");
                    }
                }
                out += fs; break;
            }
            case 's': {
                Value sa = nextArg();
                std::string sv = (sa.t == VT::Any || sa.t == VT::Nil) ? "" : sa.toStr();
                // Width/precision count characters (codepoints), not bytes, so multibyte
                // text pads correctly: sprintf("%8s","🦋🦋🦋") → "     🦋🦋🦋".
                auto cpCount = [](const std::string& s) { int n = 0; for (unsigned char c : s) if ((c & 0xC0) != 0x80) n++; return n; };
                if (prec >= 0 && cpCount(sv) > prec) { // keep the first `prec` codepoints
                    int n = 0; size_t bi = 0;
                    while (bi < sv.size() && n < prec) { bi++; while (bi < sv.size() && (((unsigned char)sv[bi]) & 0xC0) == 0x80) bi++; n++; }
                    sv = sv.substr(0, bi);
                }
                int chars = cpCount(sv);
                if (chars < width) { int pad = width - chars;
                    // `-` always wins over `0` for %s (left-justify with spaces). The
                    // `0` fill itself is version-split: 6.e zero-fills even with a
                    // precision (%08.2s of "Foo" → "000000Fo"); 6.c/6.d only zero-fill
                    // without a precision (with one it pads with spaces → "      Fo").
                    bool zeroFill = flags.find('0') != std::string::npos && flags.find('-') == std::string::npos
                                    && (langRev >= 2 || prec < 0);
                    char fill = zeroFill ? '0' : ' ';
                    sv = (flags.find('-') != std::string::npos) ? sv + std::string(pad,' ') : std::string(pad,fill) + sv; }
                out += sv; break;
            }
            case 'n': case 'p': // deliberately unsupported (Perl compat) — hard error
                throw RakuError{Value::typeObj("X::Str::Sprintf::Directives::Unsupported"),
                                std::string("Directive %") + conv + " is not valid in sprintf format"};
            default: { out += '%'; out += flags; if (hasWidth) out += std::to_string(width); out += conv; break; }
        }
        i = j;
    }
    return out;
}

bool deepEq(const Value& a, const Value& b) {
    // the undefined value (VT::Any) and the `Any` type object are the same thing
    auto anyish = [](const Value& v) { return v.t == VT::Any || (v.t == VT::Type && v.s == "Any"); };
    if (anyish(a) && anyish(b)) return true;
    // a Junction on either side autothreads (is-deeply $x, 'a'|'b';
    // is-deeply any(1,2,3), none(4,5,6) collapses to True)
    auto junct = [](const Value& v) {
        return v.t == VT::Array && v.arr &&
               (v.enumName == "any" || v.enumName == "all" || v.enumName == "one" || v.enumName == "none");
    };
    if (junct(b)) {
        int t = 0;
        for (auto& e : *b.arr) if (deepEq(a, e)) t++;
        return b.enumName == "any" ? t > 0 : b.enumName == "all" ? t == (int)b.arr->size()
             : b.enumName == "one" ? t == 1 : t == 0;
    }
    if (junct(a)) {
        int t = 0;
        for (auto& e : *a.arr) if (deepEq(e, b)) t++;
        return a.enumName == "any" ? t > 0 : a.enumName == "all" ? t == (int)a.arr->size()
             : a.enumName == "one" ? t == 1 : t == 0;
    }
    if (a.t == VT::Array && b.t == VT::Array) {
        if (a.arr->size() != b.arr->size()) return false;
        for (size_t i = 0; i < a.arr->size(); i++)
            if (!deepEq((*a.arr)[i], (*b.arr)[i])) return false;
        return true;
    }
    if (a.t == VT::Hash && b.t == VT::Hash) {
        if (a.hash->size() != b.hash->size()) return false;
        for (auto& kv : *a.hash) {
            auto it = b.hash->find(kv.first);
            if (it == b.hash->end() || !deepEq(kv.second, it->second)) return false;
        }
        return true;
    }
    if (a.t == VT::Pair && b.t == VT::Pair)
        return a.s == b.s && deepEq(a.pairVal ? *a.pairVal : Value::any(),
                                   b.pairVal ? *b.pairVal : Value::any());
    if (a.t == VT::Rat && b.t == VT::Rat) // structural (eqv): <0/0> eqv <0/0> is True; toNum would NaN-compare
        return a.fatRat == b.fatRat &&
               a.ratN && b.ratN && a.ratD && b.ratD &&
               BigInt::cmp(*a.ratN, *b.ratN) == 0 && BigInt::cmp(*a.ratD, *b.ratD) == 0;
    if (a.t == VT::Num && b.t == VT::Num && std::isnan(a.n) && std::isnan(b.n))
        return true; // structural: NaN eqv NaN (numeric == would say false)
    return valueEq(a, b);
}

// Build a Set/Bag/Mix (hash-backed, hashKind tag) from a flat list of values/pairs.
// Buf/Blob binary IO: bit-addressed (read|write)-(u)bits and byte-addressed
// numeric forms. Bits are MSB-first within the byte stream; values may exceed
// 64 bits (BigInt). Writes mutate `buf` in place (the caller routes an lvalue).
// `.splice` on a Buf replaces a window IN PLACE and answers the removed bytes.
// It needs the invocant's own slot: methodCall takes its invocant BY VALUE, and a
// Buf's bytes are a plain std::string rather than a shared_ptr the way an Array's
// elements are — so unlike Array.splice it cannot mutate through a copy. Same
// reason bufBitOp lives out here.
Value Interpreter::bufSplice(Value& buf, ValueList& args) {
    long long n = (long long)buf.s.size();
    long long from = args.size() > 0 && args[0].t != VT::Pair ? args[0].toInt() : 0;
    if (from < 0) from += n;
    if (from < 0) from = 0; if (from > n) from = n;
    long long len = args.size() > 1 && args[1].t != VT::Pair ? args[1].toInt() : n - from;
    if (len < 0) len = 0; if (from + len > n) len = n - from;
    std::string repl; // the replacement flattens: .splice(0, 3, <3 2 1>)
    for (size_t k = 2; k < args.size(); k++) {
        if (args[k].t == VT::Pair) continue;
        if (args[k].t == VT::Array || args[k].t == VT::Range)
            for (auto& e : args[k].flatten()) repl += (char)(unsigned char)(e.toInt() & 0xFF);
        else repl += (char)(unsigned char)(args[k].toInt() & 0xFF);
    }
    Value removed = Value::str(buf.s.substr((size_t)from, (size_t)len));
    removed.hashKind = buf.hashKind;
    buf.s.replace((size_t)from, (size_t)len, repl);
    return removed;
}

Value Interpreter::bufBitOp(Value& buf, const std::string& m, ValueList& args) {
    std::string& bytes = buf.s;
    auto endianOf = [&](const Value& v) -> int { // 0 native, 1 little, 2 big
        std::string e = !v.enumName.empty() ? v.enumName : v.toStr();
        if (e == "LittleEndian") return 1;
        if (e == "BigEndian") return 2;
        return 0;
    };
    static const bool hostLittle = [] { uint16_t x = 1; return *(uint8_t*)&x == 1; }();
    auto isLittle = [&](int e) { return e == 1 || (e == 0 && hostLittle); };
    if (m == "read-ubits" || m == "read-bits") {
        long long from = args.size() > 0 ? args[0].toInt() : 0;
        long long bits = args.size() > 1 ? args[1].toInt() : 0;
        long long total = (long long)bytes.size() * 8;
        if (from < 0 || bits < 1 || from + bits > total)
            throw RakuError{Value::typeObj("X::OutOfRange"),
                "bit range " + std::to_string(from) + "+" + std::to_string(bits) +
                " out of 0.." + std::to_string(total)};
        BigInt acc(0);
        for (long long i = 0; i < bits; i++) {
            long long bp = from + i;
            int bit = ((unsigned char)bytes[bp / 8] >> (7 - bp % 8)) & 1;
            acc = acc * BigInt(2) + BigInt(bit);
        }
        if (m == "read-bits" && bits > 0) { // two's complement sign
            long long tp = from;
            if (((unsigned char)bytes[tp / 8] >> (7 - tp % 8)) & 1)
                acc = acc - BigInt(2).pow(bits);
        }
        return acc.fitsLL() ? Value::integer(acc.toLL()) : Value::bigint(acc);
    }
    if (m == "write-ubits" || m == "write-bits") {
        long long from = args.size() > 0 ? args[0].toInt() : 0;
        long long bits = args.size() > 1 ? args[1].toInt() : 0;
        if (from < 0 || bits < 1)
            throw RakuError{Value::typeObj("X::OutOfRange"),
                "bit range " + std::to_string(from) + "+" + std::to_string(bits) + " out of range"};
        Value val = args.size() > 2 ? args[2] : Value::integer(0);
        BigInt v = val.big ? *val.big : BigInt(val.toInt());
        if (v.sign < 0) v = v + BigInt(2).pow(bits); // low `bits` bits of the 2's complement
        // grow to fit
        long long need = (from + bits + 7) / 8;
        if ((long long)bytes.size() < need) bytes.resize(need, '\0');
        // peel value bits LSB-first into positions from+bits-1 .. from
        for (long long i = bits - 1; i >= 0; i--) {
            BigInt q, r; BigInt::divmod(v, BigInt(2), q, r);
            v = q;
            long long bp = from + i;
            unsigned char& byte = (unsigned char&)bytes[bp / 8];
            unsigned char mask = (unsigned char)(1u << (7 - bp % 8));
            if (!r.isZero()) byte |= mask; else byte &= (unsigned char)~mask;
        }
        return buf;
    }
    // byte-addressed numeric forms: (read|write)-(num|int|uint)(32|64)?(offset[,value][,endian])
    bool isWrite = m.rfind("write-", 0) == 0;
    std::string kind = m.substr(isWrite ? 6 : 5); // num32 / num64 / uint64 / int32 / …
    int width = 0;
    if (!kind.empty() && isdigit((unsigned char)kind.back()))
        { size_t d = kind.find_first_of("0123456789"); width = std::atoi(kind.c_str() + d); kind = kind.substr(0, d); }
    if ((kind != "num" && kind != "int" && kind != "uint") ||
        (width != 0 && width != 8 && width != 16 && width != 32 && width != 64 && width != 128) ||
        (kind == "num" && width != 0 && width < 32))
        throw RakuError{Value::typeObj("X::Method::NotFound"), "No such method '" + m + "' for Buf"};
    long long off = args.size() > 0 ? args[0].toInt() : 0;
    size_t vi = isWrite ? 1 : 1; // value index for writes; endian index varies
    Value val = (isWrite && args.size() > 1) ? args[1] : Value::number(0);
    int endian = 0;
    for (size_t k = vi + (isWrite ? 1 : 0); k < args.size(); k++)
        if (args[k].t != VT::Pair) { endian = endianOf(args[k]); break; }
    int nb = width ? width / 8 : 8;
    if (off < 0)
        throw RakuError{Value::typeObj("X::OutOfRange"), "offset " + std::to_string(off) + " out of range"};
    if (nb > 8) { // int128/uint128: BigInt byte-peeling (the raw[8] fast path below caps at 64 bits)
        if (isWrite) {
            if ((long long)bytes.size() < off + nb) bytes.resize(off + nb, '\0');
            BigInt v = val.big ? *val.big : BigInt(val.toInt());
            if (v.sign < 0) v = v + BigInt(2).pow(nb * 8);
            for (int i = 0; i < nb; i++) { // peel LSB-first
                BigInt q, r; BigInt::divmod(v, BigInt(256), q, r); v = q;
                int pos = isLittle(endian) ? i : nb - 1 - i;
                bytes[off + pos] = (char)(unsigned char)r.toLL();
            }
            return buf;
        }
        if ((long long)bytes.size() < off + nb)
            throw RakuError{Value::typeObj("X::OutOfRange"), "read past end of buffer"};
        BigInt acc(0);
        for (int i = 0; i < nb; i++) { // accumulate MSB-first
            int pos = isLittle(endian) ? nb - 1 - i : i;
            acc = acc * BigInt(256) + BigInt((long long)(unsigned char)bytes[off + pos]);
        }
        if (kind == "int") { // two's complement sign
            int top = isLittle(endian) ? nb - 1 : 0;
            if ((unsigned char)bytes[off + top] & 0x80) acc = acc - BigInt(2).pow(nb * 8);
        }
        return acc.fitsLL() ? Value::integer(acc.toLL()) : Value::bigint(acc);
    }
    if (isWrite) {
        if ((long long)bytes.size() < off + nb) bytes.resize(off + nb, '\0');
        unsigned char raw[8] = {0};
        if (kind == "num") {
            if (nb == 4) { float f = (float)val.toNum(); std::memcpy(raw, &f, 4); }
            else { double d = val.toNum(); std::memcpy(raw, &d, 8); }
        } else {
            unsigned long long u;
            if (val.big) { // low 64 bits (toInt would saturate past int64)
                BigInt v = *val.big; if (v.sign < 0) v = v + BigInt(2).pow(64);
                BigInt q, lo; BigInt::divmod(v, BigInt(4294967296LL), q, lo);
                BigInt q2, hi; BigInt::divmod(q, BigInt(4294967296LL), q2, hi);
                u = ((unsigned long long)hi.toLL() << 32) | (unsigned long long)lo.toLL();
            }
            else u = (unsigned long long)val.toInt();
            std::memcpy(raw, &u, nb <= 8 ? nb : 8);
        }
        // raw[] is host order; reorder per requested endianness
        for (int i = 0; i < nb; i++) {
            int src = isLittle(endian) == hostLittle ? i : nb - 1 - i;
            bytes[off + i] = (char)raw[src];
        }
        return buf;
    }
    if ((long long)bytes.size() < off + nb)
        throw RakuError{Value::typeObj("X::OutOfRange"), "read past end of buffer"};
    unsigned char raw[8] = {0};
    for (int i = 0; i < nb; i++) {
        int dst = isLittle(endian) == hostLittle ? i : nb - 1 - i;
        raw[dst] = (unsigned char)bytes[off + i];
    }
    if (kind == "num") {
        if (nb == 4) { float f; std::memcpy(&f, raw, 4); return Value::number((double)f); }
        double d; std::memcpy(&d, raw, 8); return Value::number(d);
    }
    unsigned long long u = 0; std::memcpy(&u, raw, nb <= 8 ? nb : 8);
    if (kind == "int") { // sign-extend from nb bytes
        if (nb < 8 && (u & (1ULL << (nb * 8 - 1)))) u |= ~((1ULL << (nb * 8)) - 1);
        return Value::integer((long long)u);
    }
    if (nb == 8 && (u >> 63)) { // uint64 beyond long long
        BigInt b((long long)(u & 0x7FFFFFFFFFFFFFFFULL));
        return Value::bigint(b + BigInt(2).pow(63));
    }
    return Value::integer((long long)u);
}

// The canonical identity of a value — what `.WHICH` answers and what a Set/Bag/Mix
// keys on. Two values are the same element exactly when these agree, so it has to
// separate things that merely LOOK alike when stringified:
//   * an allomorph carries BOTH of its halves (`IntStr|Int|42|Str|42`), which is
//     what keeps `<42>` distinct from `42` and from `"42"`;
//   * a Rat identifies by its exact numerator/denominator (`Rat|421/10`), not by
//     its decimal rendering;
//   * a Complex by its two parts;
//   * a Bool by 1/0.
std::string whichOf(const Value& v) {
    auto ratPart = [](const Value& r) {
        if (r.ratN && r.ratD) return r.ratN->toString() + "/" + r.ratD->toString();
        return r.toStr();
    };
    if (v.isAllomorph()) {
        // the numeric half is the same value with the string side dropped
        Value num = v; num.hashKind.clear(); num.s.clear();
        std::string numName = v.typeName() == "IntStr"     ? "Int"
                            : v.typeName() == "RatStr"     ? "Rat"
                            : v.typeName() == "NumStr"     ? "Num"
                            : v.typeName() == "ComplexStr" ? "Complex" : "Num";
        std::string numId = numName == "Rat" ? ratPart(num) : num.toStr();
        if (numName == "Complex") numId = Value::number(num.n).toStr() + "|" + Value::number(num.im).toStr();
        return v.typeName() + "|" + numName + "|" + numId + "|Str|" + v.s;
    }
    switch (v.t) {
        case VT::Bool:    return "Bool|" + std::string(v.b ? "1" : "0");
        case VT::Rat:     return "Rat|" + ratPart(v);
        case VT::Complex: return "Complex|" + Value::number(v.n).toStr() + "|" + Value::number(v.im).toStr();
        // An OBJECT is identified by its address, not by its contents — two
        // instances with equal attributes are different elements of a Set. This
        // lived only in the `.WHICH` arm, so `.WHICH` said they differed while
        // baggyKeyStr (which keys on the RENDERING, `A<obj>` for every instance
        // of A) merged them: `set($x, $y).elems` was 1.
        case VT::Object:  if (v.obj) {
                              char buf[24];
                              std::snprintf(buf, sizeof buf, "|%p", (void*)v.obj.get());
                              return v.typeName() + buf;
                          }
                          return v.typeName() + "|<null>";
        // a Range's identity is its GIST, exclusion markers and all — expanding
        // it makes `1..^5` and `1..4` the same value, and builds a huge string
        // for a large range on the way
        case VT::Range:   return "Range|" + v.gist();
        default:          return v.typeName() + "|" + v.toStr();
    }
}

// The typed key of an element, preserved in the count Value's `pairKey` so
// .keys/.pairs/.min/.max recover the original type (a Bag of Ints keeps Int
// keys, not the stringified form). Null for a plain Str — that round-trips
// through the string key, so Set-of-strings behaviour stays byte-identical.
std::shared_ptr<Value> baggyKey(const Value& v) {
    if (v.t == VT::Str && v.hashKind.empty() && v.enumName.empty()) return nullptr;
    return std::make_shared<Value>(v);
}
// The LOOKUP key of a quanthash element.
//
// This SHOULD be the element's identity (`whichOf`), because two elements are the
// same one exactly when their `.WHICH` agrees — keying on the rendering makes
// `42`, `"42"` and `<42>` a single element and `set(1,"1")` one-element instead of
// two. That change was written, measured and backed out, because it exposes a
// deeper mismatch it cannot fix on its own: rakupp's Hash keys are plain strings,
// so `{42 => 'a'}` contributes the STRING "42" to a set while a literal `42`
// contributes an Int. Today both render "42" and compare equal; under identity
// keys they become different elements, and roast's set operators — which compare
// an operator result against a `set(…)` literal — lose about 30 assertions.
//
// Fixing it properly means Hash keys carrying their original key object, which is
// its own piece of work. Until then the rendering below is what makes results
// print correctly; the keys stay renderings.
std::string baggyKeyStr(const Value& v) {
    if (v.t == VT::Type || v.t == VT::Any) return v.gist(); // type objects ARE elements
    // An ALLOMORPH is neither of its halves: `<42>` renders "42" exactly like the
    // Int 42, so keying on the rendering merged them and `42 ∈ <42 55 1>` was True.
    // Narrow on purpose — plain Str and Int keys keep their rendering, so the set
    // operators that compare a result against a `set(…)` literal are untouched.
    if (v.isAllomorph()) return whichOf(v);
    // …and an OBJECT, whose rendering is `Class<obj>` for every instance, so two
    // distinct objects collapsed into one Set element.
    if (v.t == VT::Object) return whichOf(v);
    return v.toStr();
}
// pairsAsElements: constructors (set()/Set.new) treat a Pair item as ONE element
// (`set [foo=>1, bar=>2]` has two Pair elements); coercions (.Set/.Bag on a
// Hash, new-from-pairs) keep the pair→count reading.
Value makeBaggy(const ValueList& items, const std::string& kind, bool pairsAsElements) {
    Value h = Value::makeHash();
    h.hashKind = kind;
    bool isSet = kind.find("Set") == 0;
    bool isMix = kind.find("Mix") == 0; // Mix weights keep their full numeric value (2.5 stays a Rat)
    auto add = [&](const std::string& k, long long cnt, const std::shared_ptr<Value>& tk) {
        auto it = h.hash->find(k);
        auto keep = it != h.hash->end() && it->second.pairKey ? it->second.pairKey : tk;
        if (isSet) {
            if (cnt > 0) { Value b = Value::boolean(true); b.pairKey = keep; (*h.hash)[k] = std::move(b); }
            else h.hash->erase(k);
            return;
        }
        long long c = it != h.hash->end() ? it->second.toInt() : 0;
        c += cnt;
        if (c != 0) { Value cv = Value::integer(c); cv.pairKey = keep; (*h.hash)[k] = std::move(cv); }
        else h.hash->erase(k);
    };
    for (auto& v : items) {
        if (v.t == VT::Pair && pairsAsElements) {
            add(baggyKeyStr(v), 1, std::make_shared<Value>(v)); // the Pair itself is the element
            continue;
        }
        if (v.t == VT::Pair) {
            Value w = v.pairVal ? *v.pairVal : Value::integer(0);
            if (!isSet) { // a Bag/Mix weight must coerce to a real number
                if ((w.t == VT::Complex && w.im != 0.0) ||
                    (w.t == VT::Num && !std::isfinite(w.n)))
                    throw RakuError{Value::typeObj("X::Numeric::CannotConvert"),
                        "Cannot convert " + w.gist() + " to " + (isMix ? "Real" : "Int")};
                if (w.t == VT::Str && !w.isAllomorph()) {
                    const char* p = w.s.c_str();
                    while (*p == ' ' || *p == '\t' || *p == '\n') p++;
                    char* end = nullptr;
                    if (*p) std::strtod(p, &end);
                    while (end && (*end == ' ' || *end == '\t' || *end == '\n')) end++;
                    if (*p && (end == p || *end))
                        throw RakuError{Value::typeObj("X::Str::Numeric"),
                            "Cannot convert string to number: " + w.s};
                }
            }
            if (isMix && w.t != VT::Int && w.isNumeric()) { // fractional weight
                auto it = h.hash->find(v.s);
                auto keep = it != h.hash->end() && it->second.pairKey ? it->second.pairKey : v.pairKey;
                if (it != h.hash->end()) {
                    // through the EXACT tower, not a C double: the Rats 1/10 and 1/50
                    // summed as doubles gave 0.12000000000000001, and being a Num the
                    // result then printed at full Num precision too
                    Value sum = applyArith("+", it->second, w);
                    if (sum.toNum() == 0.0) h.hash->erase(v.s);
                    else { sum.pairKey = keep; (*h.hash)[v.s] = std::move(sum); }
                } else if (w.toNum() != 0.0) { w.pairKey = keep; (*h.hash)[v.s] = w; }
                continue;
            }
            // Set membership is the value's TRUTHINESS (`:e<meow>` joins, `:0d`/`:f('')`
            // do not); Bag/Mix use the numeric weight. (typed key travels in pairKey)
            add(v.s, isSet ? (w.truthy() ? 1 : 0) : w.toInt(), v.pairKey);
        }
        else add(baggyKeyStr(v), 1, baggyKey(v));
    }
    return h;
}

// Build a Signature introspection value from a routine's parameters.
// Rendered as a Hash tagged "Signature" carrying its .raku text and arity/count.
// A parameter's default as Rakudo renders it: only simple literals and type
// names constant-fold; anything else is a thunk, printed as `Code.new`.
static std::string renderDefault(const Param& p) {
    if (!p.defaultRaku.empty()) return p.defaultRaku;
    const Expr* d = p.defaultVal.get();
    if (!d) return "";
    switch (d->kind) {
        case NK::IntLit: {
            auto* n = static_cast<const IntLit*>(d);
            return n->big.empty() ? std::to_string(n->v) : n->big;
        }
        case NK::NumLit:  return rakuRepr(Value::number(static_cast<const NumLit*>(d)->v));
        case NK::StrLit:  return rakuStrLit(static_cast<const StrLit*>(d)->v);
        case NK::BoolLit: return static_cast<const BoolLit*>(d)->v ? "Bool::True" : "Bool::False";
        case NK::NameTerm: {
            const std::string& n = static_cast<const NameTerm*>(d)->name;
            // a bare type name folds; a called-by-name term does not
            if (!n.empty() && (isupper((unsigned char)n[0]) || n == "Nil")) return n;
            return "Code.new";
        }
        default: return "Code.new";
    }
}

// One parameter, Rakudo-style: `Int:D $x`, `:$a!`, `:b($a) = 2`, `*@r`, `|c`.
static std::string renderParam(const Param& p) {
    std::string o;
    if (!p.type.empty()) {
        o += p.type;
        if (p.defConstraint == 1) o += ":D";
        else if (p.defConstraint == 2) o += ":U";
        o += " ";
    }
    // `|c` is a capture, not a `*`-slurpy — it renders with its own leading `|`
    bool capture = p.slurpy && p.slurpyKind == 0 && (p.sigil == '|' || p.sigil == '\\');
    if (capture) return o + "|" + p.name;
    if (p.slurpy) o += p.slurpyKind == 'n' ? "**" : p.slurpyKind == '1' ? "+" : "*";
    std::string var = p.name.empty() ? std::string(1, p.sigil) : p.name;
    if (p.named) {
        std::string bare = p.name.size() > 1 ? p.name.substr(1) : p.name;
        // `:$a` when the external name matches the variable, else nested alias
        // layers `:b(:c($a))` — every key answers
        if ((p.namedKey.empty() || p.namedKey == bare) && p.aliasKeys.empty()) o += ":" + var;
        else {
            std::string inner = var;
            for (auto it = p.aliasKeys.rbegin(); it != p.aliasKeys.rend(); ++it)
                inner = ":" + *it + "(" + inner + ")";
            o += ":" + (p.namedKey.empty() ? bare : p.namedKey) + "(" + inner + ")";
        }
    }
    else o += var;
    std::string def = renderDefault(p);
    if (p.named) { if (p.required) o += "!"; }
    else if (p.optional && def.empty() && !p.slurpy) o += "?";
    if (p.isRw) o += " is rw";
    if (p.isCopy) o += " is copy";
    if (p.whereExpr || p.hadWhere) o += " where { ... }";
    if (!def.empty()) o += " = " + def;
    return o;
}

// Param owns unique_ptr expressions, so a residual signature can't hold copies of
// them — snapshot the plain fields and pre-render what the exprs contribute.
std::shared_ptr<Param> signatureParamCopy(const Param& p) {
    auto q = std::make_shared<Param>();
    q->name = p.name; q->sigil = p.sigil; q->type = p.type; q->namedKey = p.namedKey;
    q->aliasBoth = p.aliasBoth; q->aliasKeys = p.aliasKeys; q->pod = p.pod;
    q->slurpyKind = p.slurpyKind; q->named = p.named; q->slurpy = p.slurpy;
    q->optional = p.optional; q->required = p.required; q->invocant = p.invocant;
    q->defConstraint = p.defConstraint; q->coerce = p.coerce;
    q->isRw = p.isRw; q->isCopy = p.isCopy;
    q->hadWhere = p.whereExpr != nullptr || p.hadWhere;
    q->defaultRaku = renderDefault(p);
    return q;
}

Value makeSignature(const Callable* c) {
    // a .assuming wrapper carries its residual params; everything else renders
    // its declared params
    std::vector<const Param*> ps;
    if (c) {
        if (c->hasPrimed) { for (auto& sp : c->primedParams) ps.push_back(sp.get()); }
        else if (c->params) for (auto& p : *c->params) ps.push_back(&p);
    }
    // A bare `{ … }` block with no written signature carries an IMPLICIT `$_`:
    // `{;}.signature` is `(;; $_? is raw = OUTER::<$_>)`, arity 0 but count 1.
    // `-> {…}` and `sub {…}` do NOT (both are `()`), and a placeholder block has
    // its own real signature — hence the isBlock/!hadSig/no-placeholders guard.
    if (ps.empty() && c && c->isBlock && !c->hadSig && !c->isSigLiteral &&
        c->placeholders.empty()) {
        Value s = Value::makeHash(); s.hashKind = "Signature";
        (*s.hash)["str"] = Value::str("(;; $_? is raw = OUTER::<$_>)");
        (*s.hash)["arity"] = Value::integer(0);
        (*s.hash)["count"] = Value::integer(1);
        Value params = Value::array(); params.isList = true;
        Value pv = Value::makeHash(); pv.hashKind = "Parameter";
        (*pv.hash)["str"] = Value::str("$_? is raw = OUTER::<$_>");
        (*pv.hash)["name"] = Value::str("$_");
        (*pv.hash)["usage-name"] = Value::str("_");
        (*pv.hash)["type"] = Value::str("Any");
        (*pv.hash)["type-obj"] = Value::typeObj("Any");
        (*pv.hash)["optional"] = Value::boolean(true);
        (*pv.hash)["slurpy"] = Value::boolean(false);
        (*pv.hash)["named"] = Value::boolean(false);
        (*pv.hash)["raw"] = Value::boolean(true);
        (*pv.hash)["readonly"] = Value::boolean(false);
        (*pv.hash)["rw"] = Value::boolean(false);
        (*pv.hash)["suffix"] = Value::str("?");
        (*pv.hash)["multi-invocant"] = Value::boolean(false);
        params.arr->push_back(std::move(pv));
        (*s.hash)["params"] = std::move(params);
        return s;
    }
    std::string sig = "(";
    long long arity = 0, count = 0; bool slurpy = false, first = true;
    for (const Param* pp : ps) {
        const Param& p = *pp;
        if (p.invocant) continue;
        if (!first) sig += ", ";
        first = false;
        sig += renderParam(p);
        if (!p.named) { if (p.slurpy) slurpy = true; else { count++; if (!p.optional && !p.defaultVal && p.defaultRaku.empty()) arity++; } }
    }
    // a declared return type is part of the signature's rendering: `($x --> Int)`
    // (space-separated, no comma — and `(--> Int)` when there are no parameters)
    // (Rakudo separates with a space either way, so an empty parameter list
    // renders as `( --> Str)`)
    if (c && !c->retType.empty()) sig += " --> " + c->retType;
    sig += ")";
    Value s = Value::makeHash(); s.hashKind = "Signature";
    (*s.hash)["str"] = Value::str(sig);
    (*s.hash)["arity"] = Value::integer(arity);
    (*s.hash)["count"] = slurpy ? Value::number(std::numeric_limits<double>::infinity()) : Value::integer(count);
    Value params = Value::array(); params.isList = true;
    for (const Param* pp : ps) {
        const Param& p = *pp;
        Value pv = Value::makeHash(); pv.hashKind = "Parameter";
        // how the parameter renders on its own — Value::gist reads this, so a
        // `say $sig.params[0]` shows `Int $one` rather than the attribute dump
        (*pv.hash)["str"] = Value::str(renderParam(p));
        // an ANONYMOUS parameter has an empty .name, not its bare sigil
        (*pv.hash)["name"] = Value::str(p.name.size() > 1 ? p.name : std::string());
        // `.usage-name` is the name without its sigil AND its twigil, so the
        // dynamic `Str @*l` is usable as plain `l`
        {
            std::string un = p.name.size() > 1 ? p.name.substr(1) : std::string();
            if (!un.empty() && std::strchr("*?!.=~^:", un[0])) un = un.substr(1);
            (*pv.hash)["usage-name"] = Value::str(un);
        }
        (*pv.hash)["type"] = Value::str(p.type);
        // the TYPE OBJECT for `.type` (compared `=:= Str` etc. by Cro's router).
        // Unconstrained is Mu; a slurpy/@-sigil param is Positional, %-sigil
        // Associative, &-sigil Callable — the constraint its sigil implies.
        // An unconstrained parameter is Any on a ROUTINE and Mu on a bare
        // `:( … )` literal; the sigil implies its own constraint either way.
        (*pv.hash)["type-obj"] = Value::typeObj(
            !p.type.empty() ? p.type
            : p.sigil == '@' ? "Positional"
            : p.sigil == '%' ? "Associative"
            : p.sigil == '&' ? "Callable"
            : (c && c->isSigLiteral) ? "Mu" : "Any");
        // trait/shape flags the introspection API exposes one method each for
        (*pv.hash)["raw"]  = Value::boolean(p.isRaw || (p.sigil == '\\' && !p.slurpy && !p.isCopy));
        (*pv.hash)["copy"] = Value::boolean(p.isCopy);
        (*pv.hash)["readonly"] = Value::boolean(!(p.isRw || p.isCopy || p.isRaw ||
                                                  (p.sigil == '\\' && !p.slurpy)));
        (*pv.hash)["rw"]   = Value::boolean(p.isRw);
        (*pv.hash)["capture"] = Value::boolean(p.slurpy && p.slurpyKind == 0 &&
                                               (p.sigil == '|' || p.sigil == '\\'));
        (*pv.hash)["invocant"] = Value::boolean(p.invocant);
        (*pv.hash)["multi-invocant"] = Value::boolean(true); // only `;;` makes it False
        // `.prefix`/`.suffix`/`.modifier` — how the parameter is SPELLED
        (*pv.hash)["prefix"] = Value::str(
            !p.slurpy ? "" : p.slurpyKind == 'n' ? "**" : p.slurpyKind == '1' ? "+"
            : (p.sigil == '|' || p.sigil == '\\') ? "|" : "*");
        (*pv.hash)["suffix"] = Value::str(p.named ? (p.required ? "!" : "")
                                                  : (p.optional && !p.defaultVal ? "?" : ""));
        (*pv.hash)["modifier"] = Value::str(p.defConstraint == 1 ? ":D"
                                          : p.defConstraint == 2 ? ":U" : "");
        (*pv.hash)["named"] = Value::boolean(p.named);
        // `.default` is a Callable producing the default — undefined when the
        // parameter has none
        if (p.defaultVal) {
            const Expr* de = p.defaultVal.get();
            Value dc; dc.t = VT::Code; dc.code = std::make_shared<Callable>();
            dc.code->builtin = [de](Interpreter& I, ValueList&) -> Value {
                return I.eval(const_cast<Expr*>(de));
            };
            (*pv.hash)["default"] = dc;
        }
        // with no default at all `.default` is the Code TYPE OBJECT — the
        // attribute's declared type — not a bare Any
        else (*pv.hash)["default"] = Value::typeObj("Code");
        (*pv.hash)["optional"] = Value::boolean(p.optional || p.defaultVal != nullptr);
        (*pv.hash)["slurpy"] = Value::boolean(p.slurpy);
        // `.constraints`: a literal parameter ('greet' in `get -> 'greet', $n {}`)
        // answers its literal value; otherwise Mu (matches Rakudo's use in Cro)
        {   // literal constraint value — static context, so decode the common
            // literal node kinds directly (StrLit/IntLit); anything else -> Mu
            Value cv = Value::typeObj("Mu");
            if (p.litVal) {
                Expr* le = p.litVal.get();
                if (le->kind == NK::StrLit) cv = Value::str(static_cast<StrLit*>(le)->v);
                else if (le->kind == NK::IntLit) cv = Value::integer(static_cast<IntLit*>(le)->v);
                else if (le->kind == NK::NumLit) cv = Value::number(static_cast<NumLit*>(le)->v);
                else if (le->kind == NK::BoolLit) cv = Value::boolean(static_cast<BoolLit*>(le)->v);
            }
            (*pv.hash)["constraints"] = std::move(cv);
        }
        {   // `.named_names`: every name this named parameter answers to
            Value nn = Value::array(); nn.isList = true;
            if (p.named) {
                if (!p.namedKey.empty()) nn.arr->push_back(Value::str(p.namedKey));
                for (auto& ak : p.aliasKeys) nn.arr->push_back(Value::str(ak));
                if (p.namedKey.empty() || p.aliasBoth) {
                    std::string bare = p.name.size() > 2 && (p.name[1] == '!' || p.name[1] == '.')
                                     ? p.name.substr(2) : (p.name.size() > 1 ? p.name.substr(1) : p.name);
                    nn.arr->push_back(Value::str(bare));
                }
            }
            (*pv.hash)["named_names"] = nn;
        }
        params.arr->push_back(pv);
    }
    (*s.hash)["params"] = params;
    return s;
}

// say/print/put/note honour a user-overridden $*OUT/$*ERR: if the dynamic
// variable holds a user object (e.g. a mock IO capturing output), send the text
// to its .print method; otherwise write straight to the real stream.
Value Interpreter::ioEmit(const std::string& s, const char* dynVar, bool toErr) {
    // Dynamic ($*) lookup: the current lexical scope, then the caller chain.
    Value* h = nullptr;
    if (tctx_.cur) {
        h = tctx_.cur->find(dynVar);
        if (!h)
            for (auto it = tctx_.dynStack.rbegin(); it != tctx_.dynStack.rend(); ++it)
                if (*it && (h = (*it)->find(dynVar))) break;
    }
    if (h && h->t == VT::Object) {
        ValueList pa{Value::str(s)};
        return methodCall(*h, "print", pa);
    }
    (toErr ? std::cerr : std::cout) << s;
    return Value::boolean(true);
}

// ---------------- method dispatch ----------------
// quanthash value types: Set weighs on Bool, Bag on UInt, Mix on Real
const char* quantValueType(const std::string& kind) {
    if (kind == "Set" || kind == "SetHash") return "Bool";
    if (kind == "Bag" || kind == "BagHash") return "UInt";
    if (kind == "Mix" || kind == "MixHash") return "Real";
    return nullptr;
}

static Value makeAsyncSocket(int fd); // defined with the supply-wiring block below

// ---- minimal JSON parser (Rakudo::Internals::JSON.from-json) ----------------
// Recursive descent producing rakupp Values: object→Hash, array→List, string→
// Str, number→Int/Num, true/false→Bool, null→Any. Enough for module resource
// files and META-style data (OpenSSL's libraries.json, dist configs).
static void jsonSkipWs(const std::string& s, size_t& i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
}
static bool jsonParseValue(const std::string& s, size_t& i, Value& out);
static bool jsonParseString(const std::string& s, size_t& i, std::string& out) {
    if (i >= s.size() || s[i] != '"') return false;
    i++;
    out.clear();
    while (i < s.size() && s[i] != '"') {
        char c = s[i++];
        if (c == '\\' && i < s.size()) {
            char e = s[i++];
            switch (e) {
                case 'n': out += '\n'; break;  case 't': out += '\t'; break;
                case 'r': out += '\r'; break;  case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;  case '/': out += '/';  break;
                case '"': out += '"';  break;  case '\\': out += '\\'; break;
                case 'u': {
                    if (i + 4 > s.size()) return false;
                    unsigned cp = std::strtoul(s.substr(i, 4).c_str(), nullptr, 16); i += 4;
                    // encode the code point as UTF-8 (BMP only; surrogate pairs rare here)
                    if (cp < 0x80) out += (char)cp;
                    else if (cp < 0x800) { out += (char)(0xC0|(cp>>6)); out += (char)(0x80|(cp&0x3F)); }
                    else { out += (char)(0xE0|(cp>>12)); out += (char)(0x80|((cp>>6)&0x3F)); out += (char)(0x80|(cp&0x3F)); }
                    break;
                }
                default: out += e; break;
            }
        } else out += c;
    }
    if (i >= s.size()) return false;
    i++; // closing quote
    return true;
}
static bool jsonParseValue(const std::string& s, size_t& i, Value& out) {
    jsonSkipWs(s, i);
    if (i >= s.size()) return false;
    char c = s[i];
    if (c == '"') { std::string str; if (!jsonParseString(s, i, str)) return false; out = Value::str(str); return true; }
    if (c == '{') {
        i++; out = Value::makeHash();
        jsonSkipWs(s, i);
        if (i < s.size() && s[i] == '}') { i++; return true; }
        for (;;) {
            jsonSkipWs(s, i);
            std::string key; if (!jsonParseString(s, i, key)) return false;
            jsonSkipWs(s, i);
            if (i >= s.size() || s[i] != ':') return false; i++;
            Value v; if (!jsonParseValue(s, i, v)) return false;
            (*out.hash)[key] = v;
            jsonSkipWs(s, i);
            if (i < s.size() && s[i] == ',') { i++; continue; }
            if (i < s.size() && s[i] == '}') { i++; return true; }
            return false;
        }
    }
    if (c == '[') {
        i++; out = Value::array(); out.isList = true;
        jsonSkipWs(s, i);
        if (i < s.size() && s[i] == ']') { i++; return true; }
        for (;;) {
            Value v; if (!jsonParseValue(s, i, v)) return false;
            out.arr->push_back(v);
            jsonSkipWs(s, i);
            if (i < s.size() && s[i] == ',') { i++; continue; }
            if (i < s.size() && s[i] == ']') { i++; return true; }
            return false;
        }
    }
    if (s.compare(i, 4, "true") == 0)  { i += 4; out = Value::boolean(true);  return true; }
    if (s.compare(i, 5, "false") == 0) { i += 5; out = Value::boolean(false); return true; }
    if (s.compare(i, 4, "null") == 0)  { i += 4; out = Value::any();           return true; }
    // number
    size_t st = i;
    if (c == '-' || c == '+') i++;
    bool isFloat = false;
    while (i < s.size() && (std::isdigit((unsigned char)s[i]) || s[i]=='.' || s[i]=='e' || s[i]=='E' || s[i]=='+' || s[i]=='-')) {
        if (s[i]=='.' || s[i]=='e' || s[i]=='E') isFloat = true;
        i++;
    }
    if (i == st) return false;
    std::string num = s.substr(st, i - st);
    if (isFloat) out = Value::number(std::strtod(num.c_str(), nullptr));
    else         out = Value::integer(std::strtoll(num.c_str(), nullptr, 10));
    return true;
}

static std::string jsonEncode(const Value& v) {
    switch (v.t) {
        case VT::Nil: case VT::Any: case VT::Type: return "null";
        case VT::Bool: return v.b ? "true" : "false";
        case VT::Int:  return std::to_string(v.i);
        case VT::Num: case VT::Rat: { std::ostringstream o; o << v.toNum(); return o.str(); }
        case VT::Array: {
            std::string r = "[";
            if (v.arr) for (size_t k = 0; k < v.arr->size(); k++) { if (k) r += ","; r += jsonEncode((*v.arr)[k]); }
            return r + "]";
        }
        case VT::Hash: {
            std::string r = "{"; bool first = true;
            if (v.hash) for (auto& kv : *v.hash) { if (!first) r += ","; first = false; r += "\"" + kv.first + "\":" + jsonEncode(kv.second); }
            return r + "}";
        }
        default: { // string (and anything stringy)
            std::string r = "\"";
            for (char c : v.toStr()) {
                switch (c) {
                    case '"': r += "\\\""; break; case '\\': r += "\\\\"; break;
                    case '\n': r += "\\n"; break; case '\t': r += "\\t"; break;
                    case '\r': r += "\\r"; break;
                    default: r += c;
                }
            }
            return r + "\"";
        }
    }
}

// `.kv`/`.keys`/`.values`/`.pairs`/`.antipairs` answer a Seq on EVERY container in
// Rakudo — Hash, Array, List, Pair, Match alike. Marking them at the one dispatch
// point keeps that uniform instead of tagging a dozen construction sites.
Value Interpreter::methodCall(const Value& inv, const std::string& m, ValueList args, const std::vector<ExprPtr>* rwArgs) {
    Value r = methodCallInner(inv, m, std::move(args), rwArgs);
    if (r.t == VT::Array && r.isList && r.s.empty() &&
        (m == "kv" || m == "keys" || m == "values" || m == "pairs" ||
         m == "antipairs" || m == "invert" ||
         m == "reverse" || m == "sort" || m == "unique" || m == "squish" ||
         m == "head" || m == "tail" || m == "skip" || m == "rotor" || m == "batch"))
        r.s = "Seq";
    return r;
}

// The method NAME, compared against string literals. The literal's length is part
// of its TYPE, so `m == "chars"` is a size test plus an inline memcmp and never
// calls strlen — which matters because methodCallInner is ~8,900 lines with ~1,640
// such comparisons, far past the point where clang will still inline
// std::operator==(const string&, const char*). Out of line, that operator calls
// strlen on the literal every time: the two together were 60% of a profile of
// `for ^3000000 { "ab".chars }`.
// Always-inline, portably. GCC and Clang take the attribute; MSVC does not know
// `__attribute__` at all and derails on the `((` — which broke the windows-x64
// build (and only that one: MinGW uses GCC) from the commit that introduced this
// struct. `__forceinline` already implies `inline` on MSVC.
#if defined(_MSC_VER)
#define RAKUPP_ALWAYS_INLINE __forceinline
#else
#define RAKUPP_ALWAYS_INLINE __attribute__((always_inline)) inline
#endif



Value Interpreter::methodCallInner(const Value& invIn, const std::string& mName, ValueList args, const std::vector<ExprPtr>* rwArgs) {
    // The invocant arrives BY REFERENCE. It used to be by value, which cost a
    // 376-byte copy and up to eleven atomic refcount bumps on every method call —
    // to serve the handful of arms that actually rewrite it (the class-alias
    // rewrite just below, a Supply drain, one numeric coercion). Those few take a
    // copy on demand through `mutInv()`; everything else reads `inv` and pays
    // nothing. The compiler enforces it: `inv` is a const reference, so a write
    // that forgets to go through mutInv() will not compile.
    // Exactly one rewrite happens before dispatch — the package-relative alias
    // below — so it is done here, into a copy, and `inv` is bound afterwards.
    // std::optional, not a plain Value: a default-constructed Value is 376 bytes,
    // five std::strings and eleven shared_ptrs, and this runs on EVERY method call.
    // The optional's empty state is a flag — the Value is only built on the rare
    // path below that actually needs one.
    std::optional<Value> invCopy;
    const Value* invp = &invIn;
    // package-relative short name: a bare `Frog` type invocant answers as its
    // qualified nested class (`Forest::Frog`) when no real class claims the
    // short name — covers `.new`, `.= new` on typed decls, and user methods
    if (invIn.t == VT::Type && !invIn.s.empty() && !classes_.count(invIn.s)) {
        auto ai = classAliases_.find(invIn.s);
        if (ai != classAliases_.end()) { invCopy = invIn; invCopy->s = ai->second; invp = &*invCopy; }
    }
    const Value& inv = *invp;
    // `.perl` IS `.raku` — the old name for the same method. Aliasing once, here,
    // replaces sixteen `|| m == "perl"` clauses scattered down the ladder, each of
    // which had to be remembered by whoever added the next `.raku` arm. It has to
    // happen at construction: MName holds a REFERENCE to the string, so binding it
    // to a ternary temporary would dangle — hence the static lvalue.
    // …but a class that defines its OWN `method perl` keeps it: Rakudo's `.perl`
    // is a real method on Mu, so a user override wins over the forward to `.raku`.
    // The lookup only runs for the literal name "perl", which is rare.
    static const std::string kRaku = "raku";
    const bool userPerl = mName.size() == 4 && mName == "perl" &&
                          inv.t == VT::Object && inv.obj && inv.obj->cls &&
                          inv.obj->cls->findMethod("perl");
    const MName m{(mName == "perl" && !userPerl) ? kRaku : mName};
    auto a0 = [&]() -> Value { return args.empty() ? Value::any() : args[0]; };
    // read the environment ONCE, not on every method call — getenv walks environ
    static const bool kTrace = std::getenv("RAKUPP_TRACE") != nullptr;
    if (kTrace) std::cerr << "[M] ." << m << " on type=" << (int)inv.t << " s=[" << inv.s << "]" << (inv.t==VT::Object && inv.obj && inv.obj->cls ? " ("+inv.obj->cls->name+")" : "") << "\n";
    // ---- CompUnit repository machinery (what zef drives to query/install dists) ----
    {
        auto homeDir = []() -> std::string { const char* h = getenv("HOME"); return h ? h : ""; };
        auto mkCURI = [&](const std::string& name, const std::string& prefix) -> Value {
            auto od = std::make_shared<ObjectData>();
            od->cls = classes_["CompUnit::Repository::Installation"];
            od->attrs["name"] = Value::str(name);
            Value p = Value::str(prefix); p.hashKind = "IO"; // IO::Path
            od->attrs["prefix"] = p;
            return Value::object(od);
        };
        if (inv.t == VT::Type && inv.s == "CompUnit::RepositoryRegistry") {
            if (m == "repository-for-name") {
                std::string nm = args.empty() ? "" : args[0].toStr();
                // rakupp resolves `use` from ~/.raku, so every writable name maps there;
                // 'core'/'perl' get the same prefix but hold no CORE dist, so their
                // candidates come back empty (zef's ignore list ends up empty).
                return mkCURI(nm, homeDir() + "/.raku");
            }
            if (m == "repository-for-spec") {
                std::string spec = args.empty() ? "" : args[0].toStr();
                // `inst#/path` / `file#/path` / a bare name; take the path after '#'
                std::string prefix = homeDir() + "/.raku";
                auto hash = spec.find('#');
                if (hash != std::string::npos && hash + 1 < spec.size()) prefix = spec.substr(hash + 1);
                return mkCURI(spec, prefix);
            }
            if (m == "head") return mkCURI("home", homeDir() + "/.raku");
            if (m == "name-for-repository") return Value::str("home");
        }
        // A FileSystem repo is the non-installed sibling: it serves a source tree
        // directly. rakupp does not enumerate its dists either, so `.files` answers
        // the same empty list rather than being absent.
        if (inv.t == VT::Object && inv.obj && inv.obj->cls &&
            inv.obj->cls->name == "CompUnit::Repository::FileSystem" &&
            (m == "files" || m == "candidates" || m == "installed")) {
            Value e = Value::array(); e.isList = true; e.s = "Seq"; return e;
        }
        if (inv.t == VT::Object && inv.obj && inv.obj->cls &&
            inv.obj->cls->name == "CompUnit::Repository::Installation") {
            auto& at = inv.obj->attrs;
            std::string prefix = at.count("prefix") ? at["prefix"].toStr() : "";
            std::string name = at.count("name") ? at["name"].toStr() : "";
            if (m == "prefix") { Value p = Value::str(prefix); p.hashKind = "IO"; return p; }
            if (m == "name") return Value::str(name);
            if (m == "id" || m == "short-id") return Value::str(name.empty() ? std::string("inst") : name);
            if (m == "Str" || m == "gist" || m == "raku") return Value::str("inst#" + prefix);
            if (m == "path-spec") return Value::str("inst#" + prefix);
            if (m == "can-install") return Value::boolean(true);
            if (m == "repo-chain") { // this repo is the whole (single-link) chain
                Value e = Value::array(); e.isList = true; e.s = "Seq";
                e.arr->push_back(inv);
                return e;
            }
            if (m == "candidates") {
                // Phase 1: no dist enumeration yet — an empty candidate list is correct
                // for 'core' (rakupp has no CORE dist) and keeps zef's ignore list empty.
                Value e = Value::array(); e.isList = true; e.s = "Seq"; return e;
            }
            if (m == "installed") {
                Value e = Value::array(); e.isList = true; e.s = "Seq"; return e;
            }
            // `.files($name, :$ver, :$auth, :$api)` looks up an INSTALLED file (a
            // `bin/` script or a `resources/` entry) across the repo's distributions.
            // rakupp does not enumerate dists yet — the same Phase-1 gap as
            // `.candidates` — so the honest answer is the empty list every caller
            // already handles with `// "Nada"`, not a missing method.
            if (m == "files") {
                Value e = Value::array(); e.isList = true; e.s = "Seq"; return e;
            }
            if (m == "install") {
                // $cur.install($dist, :$force) — write the CURI layout under `prefix`
                // (sources/<sha>, short/<sha1(name)>/<dist-id>, dist/<dist-id> JSON,
                // resources/, bin/). rakupp reads exactly this to resolve `use`.
                Value dist = args.empty() ? Value::any() : args[0];
                bool force = false;
                for (auto& a : args)
                    if (a.t == VT::Pair && a.s == "force") force = a.pairVal && a.pairVal->truthy();
                Value metaV = methodCall(dist, "meta", ValueList{});
                if (metaV.t != VT::Hash || !metaV.hash)
                    throw RakuError{Value::typeObj("X::AdHoc"), "install: distribution has no meta"};
                auto& meta = *metaV.hash;
                auto mstr = [&](const char* k) -> std::string {
                    auto it = meta.find(k); return it != meta.end() ? it->second.toStr() : "";
                };
                std::string name = mstr("name");
                std::string ver  = meta.count("version") ? meta["version"].toStr() : mstr("ver");
                std::string auth = mstr("auth");
                std::string api  = mstr("api");
                std::string distId = sha1hex(name + "\0" + ver + "\0" + auth + "\0" + api);
                std::string distRoot = methodCall(dist, "IO", ValueList{}).toStr();
                auto mkdirp = [](const std::string& p) {
                    std::string acc;
                    for (size_t i = 0; i <= p.size(); i++) {
                        if (i == p.size() || p[i] == '/') { if (acc.size() > 1) ::mkdir(acc.c_str(), 0777); }
                        if (i < p.size()) acc += p[i];
                    }
                };
                auto slurp = [](const std::string& path) -> std::string {
                    std::ifstream in(path, std::ios::binary);
                    std::ostringstream ss; ss << in.rdbuf(); return ss.str();
                };
                // already installed? (a short entry for a provided module under this dist-id)
                Value provV = meta.count("provides") ? meta["provides"] : Value::makeHash();
                if (!force && provV.t == VT::Hash && provV.hash && !provV.hash->empty()) {
                    std::string firstMod = provV.hash->begin()->first;
                    std::string sentinel = prefix + "/short/" + sha1hex(firstMod) + "/" + distId;
                    if (std::ifstream(sentinel).good())
                        throw RakuError{Value::typeObj("X::AdHoc"),
                            name + ":ver<" + ver + ">:auth<" + auth + "> is already installed"};
                }
                mkdirp(prefix + "/sources"); mkdirp(prefix + "/short"); mkdirp(prefix + "/dist");
                Value filesOut = Value::makeHash();
                if (provV.t == VT::Hash && provV.hash)
                    for (auto& kv : *provV.hash) {
                        std::string mod = kv.first, srcRel;
                        // provides value is either the source path, or {path => {file,…}}
                        if (kv.second.t == VT::Hash && kv.second.hash && !kv.second.hash->empty())
                            srcRel = kv.second.hash->begin()->first;
                        else srcRel = kv.second.toStr();
                        std::string content = slurp(distRoot + "/" + srcRel);
                        std::string srcSha = sha1hex(content);
                        { std::ofstream o(prefix + "/sources/" + srcSha, std::ios::binary); o << content; }
                        std::string sdir = prefix + "/short/" + sha1hex(mod);
                        mkdirp(sdir);
                        std::ofstream o(sdir + "/" + distId);
                        o << ver << "\n" << auth << "\n" << api << "\n" << srcSha << "\n" << distId << "\n";
                        (*filesOut.hash)[srcRel] = Value::str(srcSha);
                    }
                // resources/ and bin/ — zef injects these into meta<files> (rel-path => src)
                if (meta.count("files") && meta["files"].t == VT::Hash && meta["files"].hash) {
                    mkdirp(prefix + "/resources"); mkdirp(prefix + "/bin");
                    for (auto& kv : *meta["files"].hash) {
                        std::string rel = kv.first, src = kv.second.toStr();
                        std::string content = slurp(src.empty() ? distRoot + "/" + rel : src);
                        std::string sha = sha1hex(content);
                        std::string sub = rel.rfind("bin/", 0) == 0 ? "/bin/" : "/resources/";
                        { std::ofstream o(prefix + sub + sha, std::ios::binary); o << content; }
                        (*filesOut.hash)[rel] = Value::str(sha);
                    }
                }
                // dist/<id> — the meta index (list-installed reads it; buildResourceMap
                // scans it for `resources/…` → the on-disk resource copy).
                Value distMeta = metaV; distMeta.hash = std::make_shared<std::map<std::string, Value>>(meta);
                (*distMeta.hash)["files"] = filesOut;
                { std::ofstream o(prefix + "/dist/" + distId); o << jsonEncode(distMeta); }
                return Value::boolean(true);
            }
        }
    }
    if (m == "WHY") {
        // declarator pod: `#| text` above a sub/method/class answers .WHY
        if (inv.t == VT::Code && inv.code && !inv.code->pod.empty())
            return Value::str(inv.code->pod);
        if (inv.t == VT::Type) {
            auto it = classes_.find(inv.s);
            if (it != classes_.end() && !it->second->pod.empty())
                return Value::str(it->second->pod);
        }
        if (inv.t == VT::Object && inv.obj && inv.obj->cls && !inv.obj->cls->pod.empty())
            return Value::str(inv.obj->cls->pod);
        return Value::nil();
    }
    // Any.hash is an empty Hash (Rakudo: `my $x; $x.hash` → {}) — zef reads
    // `$dist.meta<files>.hash.keys` where <files> may be absent.
    if (m == "hash" && (inv.t == VT::Any || (inv.t == VT::Type && inv.s == "Any")))
        return Value::makeHash();
    if (m == "pairup" && (inv.t == VT::Any || inv.t == VT::Type || inv.t == VT::Nil)) {
        Value e = Value::array(); e.isList = true; e.s = "Seq"; return e; // :U invocant
    }
    // a binary buffer has no string semantics: .Str is an error (use .decode)
    if (inv.t == VT::Str && (inv.hashKind == "Buf" || inv.hashKind == "Blob") &&
        m == "Str")
        throwTyped("X::Buf::AsStr", {{"method", "Str"}},
                   "Cannot use a Buf as a string, but you called the Str method on it");
    // reverse/rotate are illegal only on a MULTI-dimensional fixed array; a 1-dim
    // shaped array reverses/rotates fine (returns a reordered list, no resize).
    if (inv.t == VT::Array && inv.shape && inv.shape->size() >= 2 && (m == "reverse" || m == "rotate"))
        throw RakuError{Value::typeObj("X::IllegalOnFixedDimensionArray"),
                        "Cannot " + m + " a fixed-dimension array"};
    // Multi-dim shaped array (`my @a[2;2]`) — keys/values/kv/pairs/antipairs/flat/
    // iterator walk the LEAVES, keyed by index tuples. (A 1-dim shaped array uses
    // the ordinary Array handlers: keys are plain indices, .flat is a Seq, etc.)
    if (inv.t == VT::Array && inv.shape && inv.shape->size() >= 2 &&
        (m == "keys" || m == "values" || m == "kv" || m == "pairs" ||
         m == "antipairs" || m == "flat" || m == "iterator")) {
        size_t ndim = inv.shape->size();
        std::vector<std::pair<Value, Value>> ents; // (index key, leaf value)
        std::vector<long long> idx;
        std::function<void(const Value&)> walk = [&](const Value& node) {
            if (idx.size() == ndim) {
                Value key;
                if (ndim == 1) key = Value::integer(idx[0]);
                else { key = Value::array(); key.isList = true;
                       for (auto ix : idx) key.arr->push_back(Value::integer(ix)); }
                ents.push_back({key, node});
                return;
            }
            if (node.t == VT::Array && node.arr)
                for (size_t i = 0; i < node.arr->size(); i++) {
                    idx.push_back((long long)i); walk((*node.arr)[i]); idx.pop_back();
                }
        };
        walk(inv);
        if (m == "iterator") {
            Value it = Value::makeHash(); it.hashKind = "Iterator";
            Value items = Value::array();
            for (auto& e : ents) items.arr->push_back(e.second);
            (*it.hash)["items"] = items; (*it.hash)["pos"] = Value::integer(0);
            return it;
        }
        Value o = Value::array(); o.isList = true;
        for (auto& e : ents) {
            if (m == "keys") o.arr->push_back(e.first);
            else if (m == "values" || m == "flat") o.arr->push_back(e.second);
            else if (m == "kv") { o.arr->push_back(e.first); o.arr->push_back(e.second); }
            else if (m == "pairs") { Value p = Value::pair(e.first.toStr(), e.second); p.pairKey = std::make_shared<Value>(e.first); o.arr->push_back(std::move(p)); }
            else { Value p = Value::pair(e.second.toStr(), e.first); p.pairKey = std::make_shared<Value>(e.second); o.arr->push_back(std::move(p)); } // antipairs
        }
        return o;
    }
    // A multi-dim shaped array renders its structure: rows on their own lines for
    // .gist, and a `Array.new(:shape(…), row, …)` constructor for .raku.
    if (inv.t == VT::Array && inv.shape && inv.shape->size() >= 2 && inv.arr &&
        (m == "gist" || m == "raku")) {
        if (m == "gist") {
            std::string out = "[";
            for (size_t i = 0; i < inv.arr->size(); i++) { if (i) out += "\n "; out += gistOf((*inv.arr)[i]); }
            return Value::str(out + "]");
        }
        std::string ctor;
        if (inv.ofType.empty() || inv.ofType == "Any" || inv.ofType == "Mu") ctor = "Array";
        else if (std::islower((unsigned char)inv.ofType[0])) ctor = "array[" + inv.ofType + "]";
        else ctor = "Array[" + inv.ofType + "]";
        std::string out = ctor + ".new(:shape(";
        for (size_t i = 0; i < inv.shape->size(); i++) { if (i) out += ", "; out += std::to_string((*inv.shape)[i]); }
        out += ")";
        for (auto& row : *inv.arr) { ValueList none; out += ", " + methodCall(row, "raku", none).toStr(); }
        return Value::str(out + ")");
    }
    if (inv.t == VT::Array && inv.shape && !inv.shape->empty() && inv.arr && m == "clone") {
        Value c = inv; // deep-copy the nested storage so containers are independent
        std::function<Value(const Value&)> deep = [&](const Value& n) -> Value {
            if (n.t == VT::Array && n.arr) { Value a = n; a.arr = std::make_shared<ValueList>();
                for (auto& e : *n.arr) a.arr->push_back(deep(e)); return a; }
            return n;
        };
        c = deep(inv);
        c.shape = std::make_shared<std::vector<long long>>(*inv.shape);
        return c;
    }
    // Most list operations on a multi-dim shaped array run over its LEAVES — flatten
    // the fixed structure to a plain list and delegate.
    if (inv.t == VT::Array && inv.shape && inv.shape->size() >= 2 && inv.arr &&
        (m == "join" || m == "map" || m == "grep" || m == "combinations" ||
         m == "permutations" || m == "rotor" || m == "pick" || m == "roll" ||
         m == "first" || m == "reduce" || m == "sum" || m == "min" || m == "max" ||
         m == "sort" || m == "reverse" || m == "List" || m == "Slip" || m == "Bag")) {
        Value flat = Value::array(); flat.isList = true;
        std::function<void(const Value&)> collect = [&](const Value& n) {
            if (n.t == VT::Array && n.arr) for (auto& e : *n.arr) collect(e);
            else flat.arr->push_back(n);
        };
        for (auto& e : *inv.arr) collect(e);
        return methodCall(flat, m, args, rwArgs);
    }
    // Junction invocant: the Str-using routines operate on the WHOLE junction
    // (no autothreading — `$j.print` prints the junction's string form, calling
    // each eigenstate's .Str; `$j.printf` treats that form as the format).
    // enumName.empty() first: it rejects everything but junctions/enums in one load.
    if (!inv.enumName.empty() && inv.t == VT::Array && inv.arr &&
        (inv.enumName == "any" || inv.enumName == "all" || inv.enumName == "one" || inv.enumName == "none") &&
        (m == "printf" || m == "sprintf")) { // format verbs use the joined eigenstates as the FORMAT
        std::string s;
        for (size_t i = 0; i < inv.arr->size(); i++) {
            if (i) s += " ";
            s += methodCall((*inv.arr)[i], "Str", ValueList{}).toStr();
        }
        if (m == "Str") return Value::str(s);
        if (m == "sprintf") return Value::str(doSprintf(s, args, langRev_));
        if (m == "printf") { std::cout << doSprintf(s, args, langRev_); return Value::boolean(true); }
        if (m == "note") { std::cerr << s << "\n"; return Value::boolean(true); }
        std::cout << s << (m == "print" ? "" : "\n");
        return Value::boolean(true);
    }
    // any other method on a junction AUTOTHREADS: call it on each eigenstate,
    // return a junction of the results (`($a & $b).finish`, `$j.defined`, …)
    // (a METAMODEL call `.^name`/`.^WHAT`/… answers for the Junction ITSELF and is
    //  excluded here — `(1 & 2).^name` is "Junction", not a junction of "Int")
    if (!inv.enumName.empty() && inv.t == VT::Array && inv.arr && !(m.size() && m[0] == '^') &&
        (inv.enumName == "any" || inv.enumName == "all" || inv.enumName == "one" || inv.enumName == "none")) {
        static const std::set<std::string> junctionOwn = {
            "Bool", "so", "not", "gist", "raku", "perl", "WHAT", "WHO", "HOW",
            "return", "return-rw", // control flow acts on the junction, not each state
            "WHICH", "WHY", "item", "new", "defined-or", "THREAD",
            "DEFINITE",             // a Junction IS a defined object — but `.defined` is not that
                                    // question: it autothreads and collapses (see below)
            "say"};                 // .say gists the junction ("all(1, 2)"); .print autothreads
        // `.defined` autothreads over the eigenstates and COLLAPSES to a plain Bool —
        // `(none 3, Str).defined` is False. It is not an alias for `.Bool`:
        // `(any 0, "").defined` is True while `.Bool` is False.
        if (m == "defined") {
            Value j = Value::array(); j.enumName = inv.enumName;
            j.arr = std::make_shared<ValueList>();
            for (auto& el : *inv.arr) j.arr->push_back(Value::boolean(defined(el)));
            return Value::boolean(j.truthy());
        }
        if (m == "THREAD" && !args.empty()) {
            // shallow map: the block sees each eigenstate whole (junctions included)
            Value out = Value::array(); out.enumName = inv.enumName;
            out.arr = std::make_shared<ValueList>();
            for (auto& el : *inv.arr) {
                ValueList one{el};
                noAutothread_ = true;
                out.arr->push_back(callCallable(args[0], one));
            }
            return out;
        }
        if (!junctionOwn.count(m)) {
            Value out = Value::array(); out.enumName = inv.enumName;
            out.arr = std::make_shared<ValueList>();
            for (auto& el : *inv.arr) out.arr->push_back(methodCall(el, m, args, rwArgs));
            return out;
        }
    }
    // `augment class Int {…}`: methods added to a built-in type are parked in
    // builtinExt_ (keyed by type name). Consult it — walking the native ancestry,
    // so augmenting Cool/Any reaches Int/Str too — for native values and type
    // objects, ahead of the built-in method table.
    if (!builtinExt_.empty() && inv.t != VT::Object) {
        std::string tn = inv.t == VT::Type ? inv.s : inv.typeName();
        auto lookup = [&](const std::string& t) -> Value* {
            auto ti = builtinExt_.find(t);
            if (ti == builtinExt_.end()) return nullptr;
            auto mi = ti->second.find(m);
            return mi == ti->second.end() ? nullptr : &mi->second;
        };
        if (Value* f = lookup(tn)) return invokeMethod(*f, inv, std::move(args), rwArgs);
        for (const std::string& anc : typeAncestry(tn))
            if (anc != tn) if (Value* f = lookup(anc)) return invokeMethod(*f, inv, std::move(args), rwArgs);
    }
    // Any is not Cool: string methods on an UNDEFINED invocant die in Rakudo
    // ("Cannot resolve caller split(Any:U: …)"), typically after `prompt`/`get`
    // hit EOF. Everything else on Any stays lenient.
    if (inv.t == VT::Any) {
        static const std::set<std::string> strOnUndef = {
            "split", "comb", "words", "chars", "codes", "lc", "uc", "tc", "fc",
            "tclc", "wordcase", "flip", "substr", "subst", "trans", "index",
            "rindex", "starts-with", "ends-with", "contains", "match", "base",
            "ord", "ords", "encode", "parse-base"};
        if (strOnUndef.count(m))
            throw RakuError{Value::typeObj("X::Method::NotFound"),
                "Cannot resolve caller " + m + "(Any:U); the invocant is a type object, not an instance"};
    }
    // IterationBuffer — a low-level mutable element buffer (the iterator protocol's
    // scratch space), a growable list under the hood. Handled up front so its
    // `.elems`/`.List`/… win over the generic Hash methods (it is a hashKind Hash).
    if (inv.t == VT::Hash && inv.hashKind == "IterationBuffer") {
        auto& items = *(*inv.hash)["items"].arr;
        auto asList = [&]() { Value o = Value::array(); o.isList = true; *o.arr = items; return o; };
        if (m == "elems" || m == "Numeric" || m == "Int") return Value::integer((long long)items.size());
        if (m == "AT-POS") { long i = a0().toInt(); return (i >= 0 && i < (long)items.size()) ? items[i] : Value::typeObj("Mu"); }
        if (m == "push")    { items.push_back(a0()); return a0(); }
        if (m == "unshift") { items.insert(items.begin(), a0()); return a0(); }
        if (m == "BIND-POS") {
            long i = args.empty() ? 0 : args[0].toInt();
            Value val = args.size() > 1 ? args[1] : Value::any();
            if ((long)items.size() <= i) items.resize(i + 1);
            if (i >= 0) items[i] = val;
            return val;
        }
        if (m == "List" || m == "Seq" || m == "Slip" || m == "list") {
            Value r = asList();
            if (m == "Slip") r.s = "Slip"; // Slips splice into list-building contexts
            return r;
        }
        if (m == "append" || m == "prepend") {
            ValueList add;
            for (auto& a : args) {
                if (a.t == VT::Hash && a.hashKind == "IterationBuffer") for (auto& x : *(*a.hash)["items"].arr) add.push_back(x);
                else for (auto& x : toList(a)) add.push_back(x);
            }
            if (m == "append") items.insert(items.end(), add.begin(), add.end());
            else items.insert(items.begin(), add.begin(), add.end());
            return inv;
        }
        if (m == "clear") { items.clear(); return Value::nil(); }
        if (m == "raku" || m == "gist" || m == "Str") return Value::str("IterationBuffer.new(...)");
    }
    // A class inheriting a built-in type answers that type's identity coercion with
    // itself: `class D is Str {}` → D.new.Str === the D object (Str.Str is identity).
    if (inv.t == VT::Object && inv.obj && inv.obj->cls && !inv.obj->cls->findMethod(m)) {
        static const std::set<std::string> idTypes = {"Str", "Int", "Num", "Rat", "Bool", "Real", "Numeric"};
        if (idTypes.count(m))
            for (ClassInfo* ci = inv.obj->cls.get(); ci; ci = ci->parent.get())
                if (ci->nativeParent == m) return inv;
    }
    // `.resume` inside a CATCH: unwind to the enclosing block, which continues
    // execution at the statement after the one that threw. With NO handler on
    // the stack a ResumeEx would escape to std::terminate — die catchably.
    if (m == "resume") {
        if (catchDepth_ == 0)
            throw RakuError{Value::typeObj("X::Parameter::InvalidConcreteness"),
                            "Cannot resume without an active exception handler"};
        throw ResumeEx{};
    }
    // 6.e `.snitch`: run a tap (default: note the value) and return self — for
    // sticking a peek into a method chain. Universal, so handle it up front.
    if (m == "snitch") {
        if (!args.empty() && args[0].t == VT::Code) callCallable(args[0], {inv});
        else std::cerr << gistOf(inv) << "\n";
        return inv;
    }
    // `.are`/`.snip` on a type object or lone scalar treat it as a 1-element list
    // (so `Int.are` → Int, `42.are` → Int).
    if ((m == "are" || m == "snip") && inv.t != VT::Array && inv.t != VT::Range && inv.t != VT::Hash) {
        Value one = Value::array(); one.isList = true; one.arr->push_back(inv);
        return methodCall(one, m, args, rwArgs);
    }

    // an undefined invocant in list context is an empty list (e.g. an unmatched
    // named capture used as `@<x>».ast` or `@<x>.map(...)`).
    if ((inv.t == VT::Any || inv.t == VT::Nil) &&
        (m == "map" || m == "grep" || m == "list" || m == "flat" || m == "values" ||
         m == "keys" || m == "kv" || m == "pairs" || m == "reverse" || m == "sort")) {
        Value o = Value::array(); o.isList = true; return o;
    }
    // `.ast`/`.made` on an undefined capture (e.g. `$<optional><tag>.ast`) degrades to Nil.
    if ((inv.t == VT::Any || inv.t == VT::Nil) && (m == "ast" || m == "made")) return Value::nil();

    // metamodel call .^method — .^name/.^WHAT answer the type; others dispatch by bare name
    // a Scalar container record (from `.VAR` on a $-variable): its own name/default,
    // .^name = Scalar via typeName; anything else answers from the held value.
    if (inv.t == VT::Hash && inv.hashKind == "Scalar" && inv.hash &&
        m != "^name" && m != "WHAT" && m != "WHICH" && m != "raku") {
        if (m == "name")    { auto it = inv.hash->find("name");    return it != inv.hash->end() ? it->second : Value::any(); }
        if (m == "dynamic") { // a $*twigil variable is dynamic
            auto it = inv.hash->find("name");
            std::string n = it != inv.hash->end() ? it->second.toStr() : "";
            return Value::boolean(n.size() > 1 && n[1] == '*');
        }
        if (m == "default") { auto it = inv.hash->find("default"); return it != inv.hash->end() ? it->second : Value::any(); }
        if (m == "of")      { auto it = inv.hash->find("default"); return (it != inv.hash->end() && it->second.t == VT::Type) ? it->second : Value::typeObj("Mu"); }
        auto vi = inv.hash->find("value");
        if (vi != inv.hash->end()) return methodCall(vi->second, m, std::move(args), rwArgs);
    }
    if (!m.empty() && m[0] == '^') {
        std::string mm = m.substr(1);
        if (mm == "name") {
            if (inv.t == VT::Type && inv.s == "Metamodel::ClassHOW")
                return Value::str("Perl6::Metamodel::ClassHOW"); // Rakudo's full metaclass name
            // An OBJECT hash is a PARAMETERIZED Hash: `my Int %h{Str}` is a
            // Hash[Int,Str]. Only the name carries the parameters — typeName()
            // stays "Hash", because dispatch and error messages key on it.
            if (!objHashKeyType(inv).empty()) return Value::str("Hash[" + inv.ofType + "]");
            // a DEFINITENESS-constrained type reports its smiley: `Any:D.^name`
            if (inv.t == VT::Type && inv.i)
                return Value::str(inv.typeName() + (inv.i == 1 ? ":D" : ":U"));
            return Value::str(inv.typeName());
        }
        // `.^base_type` — the same type without its definiteness constraint
        if (mm == "base_type" && inv.t == VT::Type) {
            Value b = Value::typeObj(inv.s); b.ofType = inv.ofType; return b;
        }
        if (mm == "shortname") { // type name without its package qualifier
            std::string n = inv.typeName();
            size_t base = n.find('[');            // keep any [parametrization]
            size_t cut = n.rfind("::", base == std::string::npos ? std::string::npos : base);
            if (cut != std::string::npos) n = n.substr(cut + 2);
            return Value::str(n);
        }
        // `.^mixin(Role)` is a COPYING mixin — exactly `but`'s semantics. It has to
        // answer here, ahead of the `tobj` collapse below, which replaces an object
        // invocant with its bare type object and so loses the thing to mix into.
        if (mm == "mixin") {
            Value r = inv;
            for (auto& a : args) r = mixinValue(std::move(r), a, true);
            return r;
        }
        if (mm == "WHAT") return Value::typeObj(inv.typeName());
        // `X.^parameterize(T)` yields the parameterized type `X[T]` (same as `X[T]`)
        if (mm == "parameterize") {
            Value ty = Value::typeObj(inv.t == VT::Type ? inv.s : inv.typeName());
            for (auto& a : args) {
                std::string pn = a.t == VT::Type ? a.s : a.typeName();
                ty.ofType = ty.ofType.empty() ? pn : ty.ofType + "," + pn;
            }
            return ty;
        }
        // meta-methods (.^methods/.^attributes/.^parents/…) resolve against the
        // type (HOW), even when called on an instance.
        Value tobj = (inv.t == VT::Object && inv.obj && inv.obj->cls) ? Value::typeObj(inv.obj->cls->name) : inv;
        if ((mm == "lookup" || mm == "find_method") &&
            !(tobj.t == VT::Type && classes_.count(tobj.s))) {
            // builtin-type invocant (`().^lookup('elems')`): a "method object" —
            // a Callable that dispatches the named method on its first argument
            std::string mn = args.empty() ? "" : args[0].toStr();
            Value code; code.t = VT::Code; code.code = std::make_shared<Callable>();
            code.code->name = mn; code.code->isMethod = true;
            code.code->builtin = [mn](Interpreter& I, ValueList& a) -> Value {
                if (a.empty()) return Value::any();
                Value in2 = a[0]; ValueList rest(a.begin() + 1, a.end());
                return I.methodCall(in2, mn, rest);
            };
            return code;
        }
        return methodCall(tobj, mm, args, rwArgs);
    }

    // ---- Iterator protocol (S07). An iterator over a materialized list:
    // hashKind "Iterator", (*hash)["items"] = the values, (*hash)["pos"] = position.
    // Every copy of the Value shares the same hash map, so advancing `pos` through
    // one copy is visible through all of them (iterators are stateful objects).
    if (inv.t == VT::Hash && inv.hashKind == "Iterator" && inv.hash) {
        Value& itemsV = (*inv.hash)["items"];
        Value& posV = (*inv.hash)["pos"];
        ValueList& items = itemsV.arrRef();
        long long n = (long long)items.size();
        auto iterEnd = [] { return Value::typeObj("IterationEnd"); };
        auto pushInto = [&](const Value& tgt, long long count) -> long long {
            long long pushed = 0;
            if (tgt.t == VT::Array && tgt.arr)
                while (posV.i < n && pushed < count) { tgt.arr->push_back(items[posV.i++]); pushed++; }
            return pushed;
        };
        if (m == "pull-one") return posV.i < n ? items[posV.i++] : iterEnd();
        if (m == "push-all" || m == "push-until-lazy" || m == "push-exactly" || m == "push-at-least") {
            if (m == "push-all" || m == "push-until-lazy") {
                if (!args.empty()) pushInto(args[0], n);
                return iterEnd();
            }
            long long want = args.size() > 1 ? args[1].toInt() : 0;
            long long pushed = args.empty() ? 0 : pushInto(args[0], want);
            return pushed < want ? iterEnd() : Value::integer(pushed);
        }
        if (m == "sink-all") { posV.i = n; return iterEnd(); }
        // the skip methods answer an INT (1/0), not a Bool
        if (m == "skip-one") { bool ok = posV.i < n; if (ok) posV.i++; return Value::integer(ok ? 1 : 0); }
        if (m == "skip-at-least") {
            long long want = args.empty() ? 0 : args[0].toInt();
            long long skipped = std::min(want, n - posV.i); if (skipped < 0) skipped = 0;
            posV.i += skipped;
            return Value::integer(skipped >= want ? 1 : 0);
        }
        if (m == "skip-at-least-pull-one") {
            long long want = args.empty() ? 0 : args[0].toInt();
            posV.i = std::min(n, posV.i + std::max(0LL, want));
            return posV.i < n ? items[posV.i++] : iterEnd();
        }
        if (m == "count-only") return Value::integer(n - posV.i); // remaining, no advance
        if (m == "bool-only") return Value::boolean(posV.i < n);
        if (m == "is-lazy") { auto it = inv.hash->find("lazy"); return Value::boolean(it != inv.hash->end() && it->second.truthy()); }
        // an iterator over a RANDOMISED or unordered source promises neither a
        // stable order nor an increasing one; the flag rides on the iterator
        if (m == "is-deterministic" || m == "is-monotonically-increasing") {
            auto it = inv.hash->find("nondeterministic");
            return Value::boolean(it == inv.hash->end() || !it->second.truthy());
        }
        if (m == "can") { // introspection: which protocol methods this iterator supports
            static const std::set<std::string> ms = {
                "pull-one", "push-all", "push-until-lazy", "push-exactly", "push-at-least",
                "sink-all", "skip-one", "skip-at-least", "skip-at-least-pull-one",
                "count-only", "bool-only", "is-lazy", "iterator",
            };
            Value out = Value::array(); out.isList = true;
            std::string mn = args.empty() ? "" : args[0].toStr();
            if (ms.count(mn)) out.arr->push_back(Value::str(mn));
            return out;
        }
        if (m == "iterator") return inv; // an Iterator is its own .iterator
        if (m == "WHAT") return Value::typeObj("Iterator");
    }

    // Signature introspection value (from &routine.signature).
    if (inv.t == VT::Hash && inv.hashKind == "Signature") {
        if (m == "raku" || m == "gist" || m == "Str") {
            std::string body = inv.hash->count("str") ? (*inv.hash)["str"].toStr() : "()";
            // .raku is the signature literal; .gist/.Str are the bare parens
            return Value::str(m == "raku" ? ":" + body : body);
        }
        if (m == "arity") return inv.hash->count("arity") ? (*inv.hash)["arity"] : Value::integer(0);
        if (m == "count") return inv.hash->count("count") ? (*inv.hash)["count"] : Value::integer(0);
        if (m == "params" || m == "parameters") { Value p = inv.hash->count("params") ? (*inv.hash)["params"] : Value::array(); p.isList = true; return p; }
        if (m == "ACCEPTS") {
            if (args.empty()) return Value::boolean(false);
            // Signature ~~ Signature is a different question from Signature ~~
            // Capture: it asks whether EVERY call that binds the left also binds
            // the right, i.e. whether the left's accepted call-set is contained in
            // the right's. Arity/count windows answer most of it; a slurpy NAMED
            // does not widen the positional window, so `:(*%) ~~ :()` needs its own
            // test (both are [0,0] positionally, but only one takes nameds).
            if (args[0].t == VT::Hash && args[0].hashKind == "Signature" && args[0].hash) {
                const Value& lhs = args[0];
                auto num = [](const Value& sg, const char* k) {
                    auto it = sg.hash->find(k);
                    return it == sg.hash->end() ? 0.0 : it->second.toNum();
                };
                auto str = [](const Value& sg) {
                    auto it = sg.hash->find("str");
                    return it == sg.hash->end() ? std::string() : it->second.s;
                };
                if (!(num(lhs, "arity") >= num(inv, "arity") &&
                      num(lhs, "count") <= num(inv, "count"))) return Value::boolean(false);
                bool lAny = str(lhs).find("*%") != std::string::npos;
                bool rAny = str(inv).find("*%") != std::string::npos;
                if (lAny && !rAny) return Value::boolean(false);
                return Value::boolean(true);
            }
            // otherwise: would this CAPTURE bind? — arity window, literal
            // constraints, positional types, required nameds (Cro's router check)
            const Value& cap = args[0];
            ValueList pos; std::map<std::string, Value> named;
            if (cap.t == VT::Array && cap.arr)
                for (auto& e : *cap.arr) {
                    if (e.t == VT::Pair) named[e.s] = e.pairVal ? *e.pairVal : Value::any();
                    else pos.push_back(e);
                }
            long long arity = inv.hash->count("arity") ? (*inv.hash)["arity"].toInt() : 0;
            double cnt = inv.hash->count("count") ? (*inv.hash)["count"].toNum() : 0;
            if ((long long)pos.size() < arity) return Value::boolean(false);
            if (std::isfinite(cnt) && (double)pos.size() > cnt) return Value::boolean(false);
            size_t pi2 = 0;
            bool ok = true;
            if (inv.hash->count("params") && (*inv.hash)["params"].arr)
                for (auto& pv : *(*inv.hash)["params"].arr) {
                    if (pv.t != VT::Hash) continue;
                    auto& ph = *pv.hash;
                    bool isNamed = ph.count("named") && ph["named"].truthy();
                    bool isSlurpy = ph.count("slurpy") && ph["slurpy"].truthy();
                    if (isSlurpy) continue;
                    if (isNamed) {
                        bool opt = ph.count("optional") && ph["optional"].truthy();
                        if (!opt) {
                            std::string key;
                            if (ph.count("named_names") && ph["named_names"].arr && !ph["named_names"].arr->empty())
                                key = (*ph["named_names"].arr)[0].toStr();
                            else if (ph.count("name") && ph["name"].s.size() > 1)
                                key = ph["name"].s.substr(1);
                            if (!key.empty() && !named.count(key)) { ok = false; break; }
                        }
                        continue;
                    }
                    if (pi2 >= pos.size()) break; // optional tail
                    const Value& a2 = pos[pi2++];
                    if (ph.count("constraints") && !(ph["constraints"].t == VT::Type && ph["constraints"].s == "Mu")) {
                        const Value& cv = ph["constraints"];
                        bool eq = (a2.isNumeric() && cv.isNumeric()) ? a2.toNum() == cv.toNum()
                                                                     : a2.toStr() == cv.toStr();
                        if (!eq) { ok = false; break; }
                    }
                    if (ph.count("type") && !ph["type"].s.empty() &&
                        !typeOrSubsetMatches(a2, ph["type"].s)) { ok = false; break; }
                }
            return Value::boolean(ok);
        }
    }
    // a Parameter's introspection (.name/.type/.named/.optional/.slurpy)
    // `.dynamic` — was this container declared with a `*` twigil? `.default` —
    // its `is default(…)` element value (Any when it has none).
    // `.self` is the invocant itself — the identity method every type has
    if (m == "self" && args.empty()) return inv;

    // a value reached any other way is not a dynamic variable (the `*`-twigil
    // case is answered from the NAME, in the MethodCall evaluator)
    if (m == "dynamic" && (inv.t == VT::Array || inv.t == VT::Hash || inv.t == VT::Str ||
                           inv.t == VT::Int || inv.t == VT::Num || inv.t == VT::Any))
        return Value::boolean(false);
    if (m == "default" && (inv.t == VT::Array ||
                          (inv.t == VT::Hash && inv.hashKind != "Parameter"))) {
        if (inv.pairVal) return *inv.pairVal;
        // a QuantHash has a TYPED default, not Any: False for the Set family,
        // 0 for the weighted ones. Only those — every other tagged Hash
        // (Parameter, Failure, …) has its own `.default`, or none.
        static const std::set<std::string> quant = {"Set", "SetHash", "Bag", "BagHash", "Mix", "MixHash"};
        if (inv.t == VT::Hash && quant.count(inv.hashKind))
            return inv.hashKind.rfind("Set", 0) == 0 ? Value::boolean(false) : Value::integer(0);
        if (inv.t == VT::Hash && !inv.hashKind.empty() && inv.hashKind != "Map")
            return Value::any(); // not a container — fall through below
        return Value::any();
    }

    if (inv.t == VT::Hash && inv.hashKind == "Parameter") {
        if ((m == "name" || m == "named" || m == "optional" || m == "slurpy" ||
             m == "constraints" || m == "named_names" || m == "usage-name" ||
             m == "raw" || m == "copy" || m == "rw" || m == "capture" ||
             m == "invocant" || m == "multi-invocant" ||
             m == "prefix" || m == "suffix" || m == "modifier" ||
             m == "default" || m == "readonly") && inv.hash->count(m))
            return (*inv.hash)[m];
        // `.type` answers the TYPE OBJECT (Cro's router compares `=:= Str`);
        // the plain string form stays under the "type" key for legacy callers
        if (m == "type" && inv.hash->count("type-obj")) return (*inv.hash)["type-obj"];
        if (m == "type" && inv.hash->count(m)) return (*inv.hash)[m];
        if (m == "positional") return Value::boolean(!(*inv.hash)["named"].truthy() && !(*inv.hash)["slurpy"].truthy());
        if (m == "sigil") { const std::string& n = (*inv.hash)["name"].s; return Value::str(n.empty() ? "$" : n.substr(0, 1)); }
    }
    // a Capture's .list is its POSITIONAL args, .hash/.Map its NAMED (Pair) args
    // Capture.new(:list(...), :hash(...)) — build the \(…)-style capture value
    // Attribute.new(:name<$!x>, :type(Int), :package(Foo)) — build an Attribute
    // meta-object (for .^add_attribute and dynamic class construction).
    if (inv.t == VT::Type && inv.s == "IO::Path::Parts" && m == "new") {
        Value pp = Value::makeHash(); pp.hashKind = "IO::Path::Parts";
        auto pos = [&](size_t i) { return args.size() > i && args[i].t != VT::Pair ? args[i].toStr() : std::string(); };
        (*pp.hash)["volume"] = Value::str(pos(0));
        (*pp.hash)["dirname"] = Value::str(pos(1));
        (*pp.hash)["basename"] = Value::str(pos(2));
        for (auto& a : args) if (a.t == VT::Pair && a.pairVal &&
            (a.s == "volume" || a.s == "dirname" || a.s == "basename"))
            (*pp.hash)[a.s] = Value::str(a.pairVal->toStr());
        return pp;
    }
    if (inv.t == VT::Hash && inv.hashKind == "IO::Path::Parts") {
        if (m == "volume" || m == "dirname" || m == "basename") return (*inv.hash)[m];
        // It is Positional as well as Associative: `$parts[0]` is the volume PAIR
        // and `$parts[]` lists all three. The order is the declaration order —
        // volume, dirname, basename — not the map's sorted order, so this cannot
        // just walk the hash.
        if (m == "AT-POS" || m == "list" || m == "List" || m == "elems" || m == "Numeric") {
            static const char* kOrder[3] = {"volume", "dirname", "basename"};
            if (m == "elems" || m == "Numeric") return Value::integer(3);
            auto pairAt = [&](int i) {
                return Value::pair(kOrder[i], (*inv.hash)[kOrder[i]]);
            };
            if (m == "AT-POS") {
                long long i = args.empty() ? 0 : args[0].toInt();
                if (i < 0) i += 3;
                return (i >= 0 && i < 3) ? pairAt((int)i) : Value::any();
            }
            Value out = Value::array(); out.isList = true;
            for (int i = 0; i < 3; i++) out.arr->push_back(pairAt(i));
            return out;
        }
        if (m == "gist" || m == "raku" || m == "Str") {
            // the parts are STRING LITERALS, so a backslash in a Windows path has to
            // be escaped like any other Str.raku (`"\\a"`, not `"\a"`)
            auto q = [&](const char* k) {
                std::string v = (*inv.hash)[k].toStr(), o = "\"";
                for (char c : v) { if (c == '\\' || c == '"') o += '\\'; o += c; }
                return o + "\"";
            };
            return Value::str("IO::Path::Parts.new(" + q("volume") + "," + q("dirname") + "," + q("basename") + ")");
        }
        if (m == "elems") return Value::integer(3);
    }
    if (inv.t == VT::Type && inv.s == "Attribute" && m == "new") {
        Value at = Value::makeHash(); at.hashKind = "Attribute";
        (*at.hash)["name"] = Value::str(""); (*at.hash)["type"] = Value::typeObj("Mu");
        (*at.hash)["readonly"] = Value::boolean(true); (*at.hash)["has_accessor"] = Value::boolean(false);
        for (auto& a : args) if (a.t == VT::Pair && a.pairVal) {
            if (a.s == "name") (*at.hash)["name"] = *a.pairVal;
            else if (a.s == "type" || a.s == "of") (*at.hash)["type"] = *a.pairVal;
            else if (a.s == "rw") (*at.hash)["readonly"] = Value::boolean(!a.pairVal->truthy());
            else if (a.s == "has_accessor") (*at.hash)["has_accessor"] = *a.pairVal;
            else if (a.s == "package") (*at.hash)["package"] = *a.pairVal;
        }
        return at;
    }
    // Junction.new("any", (1, 2)) — the constructor spelling of any(1, 2)
    if (inv.t == VT::Type && inv.s == "Junction" && m == "new") {
        Value j = Value::array(); j.isList = true;
        j.enumName = args.empty() ? "any" : args[0].toStr();
        if (args.size() > 1) for (auto& e : args[1].flatten()) j.arr->push_back(e);
        return j;
    }
    // Format.new("%s|%s") — a reusable sprintf: calling it formats its arguments
    if (inv.t == VT::Type && (inv.s == "Format" || inv.s == "Formatter") && m == "new") {
        std::string fmt = args.empty() ? "" : args[0].toStr();
        // `.arity` is the number of directives the format consumes
        long long ar = 0;
        for (size_t k = 0; k + 1 < fmt.size(); k++)
            if (fmt[k] == '%') { if (fmt[k + 1] == '%') k++; else ar++; }
        Value code; code.t = VT::Code; code.code = std::make_shared<Callable>();
        code.code->name = "Format";
        code.code->builtin = [fmt](Interpreter& I, ValueList& a) -> Value {
            ValueList sa; sa.push_back(Value::str(fmt));
            for (auto& x : a) sa.push_back(x);
            return I.callBuiltin("sprintf", sa);
        };
        Value f = Value::makeHash(); f.hashKind = "Format";
        (*f.hash)["fmt"] = Value::str(fmt);
        (*f.hash)["arity"] = Value::integer(ar);
        (*f.hash)["code"] = code;
        return f;
    }
    if (inv.t == VT::Hash && inv.hashKind == "Format") {
        if (m == "Str" || m == "gist" || m == "raku") return (*inv.hash)["fmt"];
        if (m == "arity" || m == "count") return (*inv.hash)["arity"];
        // `.directives` names the conversion letter of each `%…` in order:
        // "%05d%3x:%s" is (d x s). Flags, width and precision are skipped —
        // the directive is the first ALPHABETIC character after the percent.
        if (m == "directives") {
            const std::string fmt = (*inv.hash)["fmt"].toStr();
            Value out = Value::array(); out.isList = true;
            for (size_t k = 0; k + 1 < fmt.size(); k++) {
                if (fmt[k] != '%') continue;
                if (fmt[k + 1] == '%') { k++; continue; } // a literal percent
                size_t j = k + 1;
                while (j < fmt.size() && !std::isalpha((unsigned char)fmt[j])) j++;
                if (j < fmt.size()) { out.arr->push_back(Value::str(std::string(1, fmt[j]))); k = j; }
            }
            return out;
        }
        if (m == "CALL-ME" || m == "()") return methodCall((*inv.hash)["code"], "CALL-ME", args, rwArgs);
    }
    if (inv.t == VT::Type && inv.s == "Capture" && m == "new") {
        Value c = Value::array(); c.hashKind = "Capture"; c.itemized = true;
        for (auto& a : args) {
            if (a.t != VT::Pair || !a.pairVal) continue;
            if (a.s == "list") {
                const Value& lv = *a.pairVal;
                if (lv.t == VT::Array && lv.arr) for (auto& e : *lv.arr) c.arr->push_back(e);
                else if (lv.t == VT::Range) for (auto& e : lv.flatten()) c.arr->push_back(e);
                else if (lv.t != VT::Nil && lv.t != VT::Any) c.arr->push_back(lv);
            }
            else if (a.s == "hash") {
                const Value& hv = *a.pairVal;
                if (hv.t == VT::Hash && hv.hash)
                    for (auto& kv : *hv.hash) c.arr->push_back(Value::pair(kv.first, kv.second));
            }
        }
        return c;
    }
    // A Capture is TWO collections, not one flat list: positionals indexed 0..n
    // and nameds keyed by name. Every accessor partitions accordingly —
    // `\(1, 2, :x(3)).elems` is 2 (the positionals), and .keys/.values/.pairs
    // run the positionals first, then the nameds.
    if (inv.t == VT::Array && inv.hashKind == "Capture" &&
        (m == "list" || m == "hash" || m == "Map" || m == "elems" || m == "Numeric" ||
         m == "keys" || m == "values" || m == "pairs" || m == "antipairs" || m == "kv" ||
         m == "Bool")) {
        ValueList pos; std::map<std::string, Value> named;
        if (inv.arr) for (auto& e : *inv.arr) {
            if (e.t == VT::Pair) named[e.s] = e.pairVal ? *e.pairVal : Value::any();
            else pos.push_back(e);
        }
        if (m == "list") { Value o = Value::array(); o.isList = true; *o.arr = pos; return o; }
        // `.Numeric` (and so prefix `+`) is the POSITIONAL count, like .elems — the
        // named parts do not add to it
        if (m == "elems" || m == "Numeric") return Value::integer((long long)pos.size());
        if (m == "Bool")  return Value::boolean(!pos.empty() || !named.empty());
        // `.kv` is key-then-value flattened: the positional INDEX then its value,
        // then each named's NAME then its value. Falling through to Array.kv
        // yielded the named part as (index, Pair) instead.
        if (m == "kv") {
            Value o = Value::array(); o.isList = true; o.s = "Seq";
            for (size_t i = 0; i < pos.size(); i++) {
                o.arr->push_back(Value::integer((long long)i));
                o.arr->push_back(pos[i]);
            }
            for (auto& kv : named) { o.arr->push_back(Value::str(kv.first)); o.arr->push_back(kv.second); }
            return o;
        }
        if (m == "keys" || m == "values" || m == "pairs" || m == "antipairs") {
            Value o = Value::array(); o.isList = true; o.s = "Seq";
            for (size_t i = 0; i < pos.size(); i++) {
                Value k = Value::integer((long long)i);
                if (m == "keys")      o.arr->push_back(k);
                else if (m == "values") o.arr->push_back(pos[i]);
                else if (m == "pairs")  { Value p = Value::pair(std::to_string(i), pos[i]);
                                          p.pairKey = std::make_shared<Value>(k); o.arr->push_back(p); }
                else { Value p = Value::pair(pos[i].toStr(), k); // antipairs: value => key
                       p.pairKey = std::make_shared<Value>(pos[i]); o.arr->push_back(p); }
            }
            for (auto& kv : named) {
                if (m == "keys")        o.arr->push_back(Value::str(kv.first));
                else if (m == "values") o.arr->push_back(kv.second);
                else if (m == "pairs")  { Value p = Value::pair(kv.first, kv.second);
                                          p.namedArg = true; o.arr->push_back(p); }
                else { Value p = Value::pair(kv.second.toStr(), Value::str(kv.first));
                       p.pairKey = std::make_shared<Value>(kv.second); o.arr->push_back(p); }
            }
            return o;
        }
        Value o = Value::makeHash(); o.hashKind = "Map";
        for (auto& kv : named) (*o.hash)[kv.first] = kv.second;
        return o;
    }

    // A `but`/`does` mixin over a non-object base: a composed role/class method wins,
    // object-identity/introspection methods stay on the object, and every other
    // method (coercions, arithmetic-ish, base-type methods) delegates to the box.
    if (inv.t == VT::Object && inv.obj && inv.obj->hasBoxed && inv.obj->cls &&
        !inv.obj->cls->findMethod(m) && !inv.obj->cls->findAttr(m)) {
        static const std::set<std::string> keepOnObj = {
            // `.can` must see the MIXIN's methods — forwarding it to the boxed
            // value hides them (`(Any but $failure).can('Failure')`)
            "does", "HOW", "WHAT", "WHICH", "defined", "DEFINITE", "isa", "WHERE", "can"};
        if (!keepOnObj.count(m)) return methodCall(inv.obj->boxed, m, args, rwArgs);
    }

    // Pair.new($key, $value) or Pair.new(:key(...), :value(...)) — same shape as `=>`.
    // IO::Socket::INET.new — a TCP client (:host/:port) or a listener (:listen).
    if (inv.t == VT::Type && inv.s == "IO::Socket::INET" && m == "new") {
        std::string host = "localhost", localhost; long port = 0, localport = 0; bool listen = false;
        long family = -2; // -2 = unspecified
        for (auto& a : args) {
            if (a.t != VT::Pair) continue;
            Value pv = a.pairVal ? *a.pairVal : Value::any();
            if (a.s == "host") host = pv.toStr();
            else if (a.s == "port") port = pv.toInt();
            else if (a.s == "localhost") localhost = pv.toStr();
            else if (a.s == "localport") localport = pv.toInt();
            else if (a.s == "listen") listen = pv.truthy();
            else if (a.s == "family") family = pv.toInt();
        }
        // Validate before touching the OS: port 0..65535, family a sane small value.
        long usePort = listen ? localport : port;
        if (usePort < 0 || usePort > 65535)
            throw RakuError{Value::typeObj("X::AdHoc"), "Invalid port: " + std::to_string(usePort)};
        if (family != -2 && (family < 0 || family > 255))
            throw RakuError{Value::typeObj("X::AdHoc"), "Invalid socket family: " + std::to_string(family)};
        auto resolve = [](const std::string& h, sockaddr_in& addr) {
            addr.sin_addr.s_addr = inet_addr(h.c_str());
            if (addr.sin_addr.s_addr == INADDR_NONE) {
                if (hostent* he = gethostbyname(h.c_str())) memcpy(&addr.sin_addr, he->h_addr, he->h_length);
            }
        };
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return Value::nil();
        sockaddr_in addr{}; addr.sin_family = AF_INET;
        if (listen) {
            int yes = 1; setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
            addr.sin_port = htons((uint16_t)localport);
            addr.sin_addr.s_addr = (localhost.empty() || localhost == "0.0.0.0") ? INADDR_ANY : inet_addr(localhost.c_str());
            if (::bind(fd, (sockaddr*)&addr, sizeof(addr)) < 0 || ::listen(fd, 128) < 0) { ::close(fd); return Value::nil(); }
        } else {
            addr.sin_port = htons((uint16_t)port);
            resolve(host, addr);
            bool p = gilPark(); int rc = ::connect(fd, (sockaddr*)&addr, sizeof(addr)); gilUnpark(p);
            if (rc < 0) { ::close(fd); return Value::nil(); }
        }
        Value s = Value::makeHash(); s.hashKind = "Socket"; (*s.hash)["fd"] = Value::integer(fd);
        return s;
    }
    // CArray[T].new(vals…) — a packed native array (NativeCall). Stored as raw
    // bytes in .s (like Blob); callNative passes a pointer to the bytes.
    // NativeCall CStruct field read: `$s.field` on a native-backed struct reads
    // native memory at the field's computed offset. (Writes go through the
    // assignment path.) Only for a repr('CStruct') class the accessor doesn't
    // otherwise define a real method for.
    if (inv.t == VT::Object && inv.obj && inv.obj->cls &&
        (inv.obj->cls->repr == "CStruct" || inv.obj->cls->repr == "CPPStruct") &&
        inv.obj->attrs.count("__native_ptr") && !inv.obj->cls->findMethod(m)) {
        std::string type; long long off = Interpreter::ncFieldOffset(inv.obj->cls.get(), m, type);
        if (off >= 0) {
            long long base = inv.obj->attrs["__native_ptr"].toInt();
            long long fa = base + off;
            // scalar field: read directly; pointer/Str/class field: read the 8-byte
            // pointer and box it appropriately.
            std::string bt = type.substr(0, type.find('['));
            if (bt == "Str") { long long p; std::memcpy(&p, (void*)(intptr_t)fa, 8); return Value::str(p ? std::string((const char*)(intptr_t)p) : ""); }
            if (bt == "Pointer") { long long p; std::memcpy(&p, (void*)(intptr_t)fa, 8); return ncMakePointer(type, (void*)(intptr_t)p); }
            if (bt == "CArray")  { long long p; std::memcpy(&p, (void*)(intptr_t)fa, 8); return ncMakeLiveCArray(type, (void*)(intptr_t)p); }
            auto cit = classes_.find(type);
            if (cit != classes_.end()) { // nested CStruct/CPointer field → box the pointer
                long long p; std::memcpy(&p, (void*)(intptr_t)fa, 8);
                Value o; o.t = VT::Object; o.obj = std::make_shared<ObjectData>();
                o.obj->cls = cit->second; o.obj->attrs["__native_ptr"] = Value::integer(p);
                return o;
            }
            return Interpreter::ncReadElem(fa, type, 0);
        }
    }
    // NativeCall Pointer[T]: `Pointer.new($addr)` / `Pointer[int32].new(...)`.
    if (inv.t == VT::Type && (inv.s == "Pointer" || inv.s.rfind("Pointer[", 0) == 0) &&
        (m == "new" || m == "allocate")) {
        std::string et = inv.s.rfind("Pointer[", 0) == 0 ? inv.s.substr(8, inv.s.size() - 9) : inv.ofType;
        void* p = args.empty() ? nullptr : (void*)(intptr_t)ncRawAddr(args[0]);
        return ncMakePointer(et.empty() ? "Pointer" : "Pointer[" + et + "]", p);
    }
    if (inv.t == VT::Hash && inv.hashKind == "Pointer") {
        long long addr = inv.hash->count("addr") ? (*inv.hash)["addr"].toInt() : 0;
        std::string of = inv.hash->count("of") ? (*inv.hash)["of"].toStr() : "";
        if (m == "Int" || m == "Numeric") return Value::integer(addr);
        if (m == "defined" || m == "Bool" || m == "so") return Value::boolean(addr != 0);
        if (m == "gist" || m == "Str" || m == "raku") return Value::str("Pointer" + std::string(of.empty() ? "" : "[" + of + "]") + "<" + std::to_string(addr) + ">");
        if (m == "deref") return ncReadElem(addr, of, 0);
        if (m == "of") return Value::typeObj(of.empty() ? "Pointer" : of);
    }
    // live CArray[T] over native memory (returned by a native call): element read
    if (inv.t == VT::Hash && inv.hashKind == "CArray" && inv.hash->count("addr")) {
        long long addr = (*inv.hash)["addr"].toInt();
        std::string of = inv.hash->count("of") ? (*inv.hash)["of"].toStr() : "int64";
        if (m == "AT-POS" || m == "[]") return ncReadElem(addr, of, args.empty() ? 0 : args[0].toInt());
        if (m == "Numeric" || m == "Int") return Value::integer(addr);
        if (m == "defined" || m == "Bool") return Value::boolean(addr != 0);
    }
    if (inv.t == VT::Type && (inv.s == "CArray" || inv.s.rfind("CArray[", 0) == 0)) {
        std::string et = inv.s.rfind("CArray[", 0) == 0 ? inv.s.substr(7, inv.s.size() - 8)
                                                        : inv.ofType; // parameter lives in ofType
        int esz = Interpreter::ncElemSize(et); // pointer element types are 8, not int32
        if (m == "new") {
            std::string bytes;
            for (auto& a : flattenArgs(args)) {
                if (et == "num32") { float f = (float)a.toNum(); bytes.append((const char*)&f, 4); }
                else if (et == "num64") { double d = a.toNum(); bytes.append((const char*)&d, 8); }
                else {
                    size_t at = bytes.size(); bytes.append((size_t)esz, '\0');
                    Interpreter::ncWriteElem((long long)(intptr_t)(bytes.data() + at), et, 0, a);
                }
            }
            Value c = Value::str(bytes); c.hashKind = "CArray";
            c.enumName = et; // remember the element type
            return c;
        }
        if (m == "allocate") {
            size_t n = args.empty() ? 0 : (size_t)args[0].toInt();
            Value c = Value::str(std::string(n * esz, '\0')); c.hashKind = "CArray";
            c.enumName = et;
            return c;
        }
    }
    if (inv.t == VT::Str && inv.hashKind == "CArray" && m == "elems") {
        const std::string& et = inv.enumName;
        int esz = Interpreter::ncElemSize(et);
        return Value::integer((long long)(inv.s.size() / esz));
    }
    // Encoding::Registry / streaming decoder — the Rakudo encoding API that
    // Cro's HTTP parsers drive. The decoder is a stateful byte buffer with
    // line-separator-aware consumption; our strings are byte strings, so
    // iso-8859-1/ascii/utf-8 all pass bytes through unchanged.
    // Rakudo::Internals — platform probes modules use at BEGIN time.
    // NativeHelpers::Blob picks its libc via `Rakudo::Internals.IS-WIN`.
    if (inv.t == VT::Type && inv.s == "Rakudo::Internals") {
        if (m == "IS-WIN") {
#ifdef _WIN32
            return Value::boolean(true);
#else
            return Value::boolean(false);
#endif
        }
    }
    // Rakudo::Internals::JSON — the built-in JSON codec several modules use at
    // load time (OpenSSL::NativeLib reads libraries.json through it).
    if (inv.t == VT::Type && inv.s == "Rakudo::Internals::JSON") {
        if (m == "from-json") {
            std::string j = args.empty() ? "" : args[0].toStr();
            size_t i = 0; Value out;
            if (!jsonParseValue(j, i, out))
                throw RakuError{Value::typeObj("X::AdHoc"), "Invalid JSON"};
            return out;
        }
        if (m == "to-json") {
            // pretty/spec flags are accepted but ignored (compact output)
            return Value::str(args.empty() ? "null" : jsonEncode(args[0]));
        }
    }
    if (inv.t == VT::Type && inv.s == "Encoding::Registry" && (m == "find" || m == "register")) {
        static std::map<std::string, Value> userEncodings; // fc name → registered Encoding
        auto fc = [](std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; };
        if (m == "register") {
            // pull name + alternative-names off the given Encoding-doing object
            if (!args.empty()) {
                Value& enc = args[0];
                try { userEncodings[fc(methodCall(enc, "name", {}).toStr())] = enc; } catch (...) {}
                try {
                    Value alts = methodCall(enc, "alternative-names", {});
                    if (alts.t == VT::Array && alts.arr)
                        for (auto& a : *alts.arr) userEncodings[fc(a.toStr())] = enc;
                } catch (...) {}
            }
            return Value::nil();
        }
        std::string name = args.empty() ? "utf-8" : args[0].toStr();
        std::string key = fc(name);
        auto uit = userEncodings.find(key);
        if (uit != userEncodings.end()) return uit->second;
        static const std::set<std::string> known = {
            "utf8", "utf-8", "ascii", "iso-8859-1", "latin-1", "latin1",
            "utf16", "utf-16", "utf16le", "utf-16le", "utf16-le", "utf-16-le",
            "utf16be", "utf-16be", "utf16-be", "utf-16-be",
            "windows932", "windows-932", "windows1251", "windows-1251",
            "windows1252", "windows-1252"};
        if (!known.count(key))
            throwTyped("X::Encoding::Unknown", {{"name", name}},
                       "Unknown string encoding '" + name + "'");
        Value e = Value::makeHash(); e.hashKind = "Encoding";
        (*e.hash)["name"] = Value::str(name);
        return e;
    }
    if (inv.t == VT::Hash && inv.hashKind == "Encoding") {
        if (m == "name") return (*inv.hash)["name"];
        if (m == "decoder") {
            Value d = Value::makeHash(); d.hashKind = "Decoder";
            (*d.hash)["buffer"] = Value::str("");
            Value seps = Value::array(); seps.arr->push_back(Value::str("\n"));
            (*d.hash)["seps"] = seps;
            return d;
        }
        if (m == "encoder") { // stateless: our strings are already UTF-8 bytes
            Value e = Value::makeHash(); e.hashKind = "Encoder";
            (*e.hash)["name"] = (*inv.hash)["name"];
            return e;
        }
    }
    if (inv.t == VT::Hash && inv.hashKind == "Encoder") {
        // encode-chars(Str) → Blob of the encoded bytes (CBOR::Simple uses utf8,
        // which is our internal string representation, so bytes pass through).
        if (m == "encode-chars" || m == "encode") {
            Value b = Value::str(args.empty() ? std::string() : args[0].toStr());
            b.hashKind = "Blob";
            return b;
        }
    }
    if (inv.t == VT::Hash && inv.hashKind == "Decoder") {
        Value& buf = (*inv.hash)["buffer"];
        if (m == "add-bytes") { if (!args.empty()) buf.s += args[0].s; return inv; }
        if (m == "set-line-separators") {
            Value seps = Value::array();
            for (auto& a : flattenArgs(args)) seps.arr->push_back(Value::str(a.toStr()));
            (*inv.hash)["seps"] = seps;
            return inv;
        }
        if (m == "consume-line-chars") {
            bool chomp = false, eof = false;
            for (auto& a : args) if (a.t == VT::Pair) {
                bool on = !a.pairVal || a.pairVal->truthy();
                if (a.s == "chomp") chomp = on;
                else if (a.s == "eof") eof = on;
            }
            size_t best = std::string::npos, bestLen = 0;
            if (inv.hash->count("seps"))
                for (auto& sep : *(*inv.hash)["seps"].arr) {
                    const std::string ss = sep.toStr();
                    if (ss.empty()) continue;
                    size_t pos = buf.s.find(ss);
                    if (pos == std::string::npos) continue;
                    // earliest match wins; on a tie the longer separator wins
                    if (pos < best || (pos == best && ss.size() > bestLen)) { best = pos; bestLen = ss.size(); }
                }
            if (best == std::string::npos) {
                if (eof && !buf.s.empty()) { std::string all = buf.s; buf.s.clear(); return Value::str(all); }
                return Value::typeObj("Str"); // no complete line yet
            }
            std::string line = buf.s.substr(0, chomp ? best : best + bestLen);
            buf.s.erase(0, best + bestLen);
            return Value::str(line);
        }
        if (m == "bytes-available") return Value::integer((long long)buf.s.size());
        if (m == "consume-exactly-bytes") {
            size_t n = args.empty() ? 0 : (size_t)args[0].toInt();
            if (buf.s.size() < n) return Value::typeObj("Blob");
            Value b = Value::str(buf.s.substr(0, n)); b.hashKind = "Blob";
            buf.s.erase(0, n);
            return b;
        }
        if (m == "consume-all-chars" || m == "consume-available-chars") {
            std::string all = buf.s; buf.s.clear(); return Value::str(all);
        }
        if (m == "consume-all-bytes" || m == "consume-available-bytes") {
            Value b = Value::str(buf.s); b.hashKind = "Blob"; buf.s.clear(); return b;
        }
        if (m == "is-empty") return Value::boolean(buf.s.empty());
    }
    // IO::Socket::Async — the async TCP surface Cro drives. listen() returns a
    // Supply that binds/accepts when tapped (see tapSupply); connect() returns a
    // kept Promise of a connected socket.
    if (inv.t == VT::Type && inv.s == "IO::Socket::Async") {
        if (m == "listen") {
            Value s = Value::makeHash(); s.hashKind = "Supply";
            (*s.hash)["kind"] = Value::str("async-listen");
            (*s.hash)["host"] = args.size() > 0 ? Value::str(args[0].toStr()) : Value::str("localhost");
            (*s.hash)["port"] = args.size() > 1 ? Value::integer(args[1].toInt()) : Value::integer(0);
            return s;
        }
        if (m == "connect") {
            std::string host = args.size() > 0 ? args[0].toStr() : "localhost";
            int port = args.size() > 1 ? (int)args[1].toInt() : 0;
            int fd = ::socket(AF_INET, SOCK_STREAM, 0);
            auto ps = std::make_shared<PromiseState>();
            Value p = Value::makeHash(); p.hashKind = "Promise"; p.ext = ps;
            if (fd < 0) {
                ps->done = true; ps->broken = true; ps->causeMsg = "Cannot create socket";
                (*p.hash)["status"] = Value::str("Broken");
                return p;
            }
            sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons((uint16_t)port);
            std::string rh = (host == "localhost") ? "127.0.0.1" : host;
            addr.sin_addr.s_addr = inet_addr(rh.c_str());
            if (addr.sin_addr.s_addr == INADDR_NONE) {
                if (hostent* he = gethostbyname(rh.c_str())) memcpy(&addr.sin_addr, he->h_addr, he->h_length);
            }
            bool parked = gilPark();
            int rc = ::connect(fd, (sockaddr*)&addr, sizeof(addr));
            gilUnpark(parked);
            if (rc < 0) {
                ::close(fd);
                ps->done = true; ps->broken = true;
                ps->cause = Value::typeObj("X::IO"); ps->causeMsg = "Cannot connect to " + host + ":" + std::to_string(port);
                (*p.hash)["status"] = Value::str("Broken");
                return p;
            }
            ps->done = true; ps->result = makeAsyncSocket(fd);
            (*p.hash)["status"] = Value::str("Kept");
            (*p.hash)["result"] = ps->result;
            return p;
        }
    }
    // A connected async socket: .Supply taps a read worker; write/print are
    // synchronous sends answered with a kept Promise (Cro awaits them via
    // `whenever $socket.write(…) {}`).
    if (inv.t == VT::Hash && inv.hashKind == "AsyncSocket") {
        int fd = inv.hash->count("fd") ? (int)(*inv.hash)["fd"].toInt() : -1;
        if (m == "Supply") {
            Value s = Value::makeHash(); s.hashKind = "Supply";
            (*s.hash)["kind"] = Value::str("async-read");
            (*s.hash)["socket"] = inv;
            bool bin = false;
            for (auto& a : args) if (a.t == VT::Pair && a.s == "bin") bin = !a.pairVal || a.pairVal->truthy();
            (*s.hash)["bin"] = Value::boolean(bin);
            return s;
        }
        if (m == "write" || m == "print" || m == "put" || m == "say") {
            std::string data = args.empty() ? "" : args[0].toStr();
            if (m == "put" || m == "say") data += "\n";
            auto ps = std::make_shared<PromiseState>();
            Value p = Value::makeHash(); p.hashKind = "Promise"; p.ext = ps;
            ssize_t off = 0; bool ok = fd >= 0;
            while (ok && off < (ssize_t)data.size()) {
                // no gilPark: send on a local socket won't block meaningfully,
                // and parking would clobber a caller already parked on this
                // thread's one-slot park context
                ssize_t n = ::send(fd, data.data() + off, data.size() - off, 0);
                if (n <= 0) { ok = false; break; }
                off += n;
            }
            ps->done = true;
            if (ok) { ps->result = Value::integer((long long)data.size()); (*p.hash)["status"] = Value::str("Kept"); (*p.hash)["result"] = ps->result; }
            else { ps->broken = true; ps->cause = Value::typeObj("X::IO"); ps->causeMsg = "Socket write failed"; (*p.hash)["status"] = Value::str("Broken"); }
            return p;
        }
        if (m == "close") { if (fd >= 0) { ::shutdown(fd, SHUT_WR); } return Value::boolean(true); }
        if (m == "native-descriptor") return Value::integer(fd);
        if (m == "socket-host" || m == "socket-port" || m == "peer-host" || m == "peer-port") {
            auto it = inv.hash->find(m);
            return it != inv.hash->end() ? it->second : Value::any();
        }
    }
    // CompUnit::DependencySpecification.new(:short-name<Foo>, …) — a module dependency
    // descriptor. Requires a Str short-name; the version/auth/api matchers default True.
    if (inv.t == VT::Type && inv.s == "CompUnit::DependencySpecification" && m == "new") {
        Value shortName; bool haveSN = false;
        for (auto& a : args) if (a.t == VT::Pair && a.s == "short-name") { shortName = a.pairVal ? *a.pairVal : Value::any(); haveSN = true; }
        if (!haveSN || shortName.t != VT::Str)
            throw RakuError{Value::typeObj("X::AdHoc"), "CompUnit::DependencySpecification requires a Str :short-name"};
        Value o = Value::makeHash(); o.hashKind = "DependencySpec";
        (*o.hash)["short-name"] = shortName;
        for (const char* k : {"version-matcher", "auth-matcher", "api-matcher"}) {
            Value v = Value::boolean(true);
            for (auto& a : args) if (a.t == VT::Pair && a.s == k && a.pairVal) v = *a.pairVal;
            (*o.hash)[k] = v;
        }
        return o;
    }
    // Buf/Blob.new(elem, elem, …) — a byte buffer, stored as a Str of bytes.
    // blob16/32/64 (and utf16/32) pack each element as a little-endian word;
    // ofType carries the element type (Digest's blob32 word arithmetic).
    // the named element-width types (blob8, buf32, …) and the equivalent
    // parameterized spellings (`Blob[uint8]`, `Buf[uint32]`) build the same thing
    if (inv.t == VT::Type && (m == "new" || m == "allocate") &&
        (inv.s == "buf8" || inv.s == "blob8" || inv.s == "utf8" ||
         inv.s == "buf16" || inv.s == "blob16" || inv.s == "utf16" ||
         inv.s == "buf32" || inv.s == "blob32" || inv.s == "utf32" ||
         inv.s == "buf64" || inv.s == "blob64" ||
         inv.s.rfind("Blob[", 0) == 0 || inv.s.rfind("Buf[", 0) == 0 ||
         ((inv.s == "Blob" || inv.s == "Buf") && !inv.ofType.empty()))) {
        // the width comes from the name (blob32) or the parameter (Blob[uint32])
        const std::string& wsrc = inv.ofType.empty() ? inv.s : inv.ofType;
        int w = wsrc.find("16") != std::string::npos ? 2
              : wsrc.find("32") != std::string::npos ? 4
              : wsrc.find("64") != std::string::npos ? 8 : 1;
        std::string bytes;
        std::function<void(const Value&)> add = [&](const Value& v) {
            if ((v.t == VT::Array || v.t == VT::Range) && !(v.t == VT::Array && !v.arr)) { for (auto& e : v.flatten()) add(e); }
            else if (v.t == VT::Str && (v.hashKind == "Blob" || v.hashKind == "Buf")) bytes += v.s; // copy an existing buffer's bytes
            else {
                unsigned long long x = (unsigned long long)v.toInt();
                for (int k = 0; k < w; k++) bytes += (char)(unsigned char)((x >> (8 * k)) & 0xFF);
            }
        };
        if (m == "allocate") {
            long long n2 = args.empty() ? 0 : args[0].toInt();
            bytes.assign((size_t)(n2 * w), '\0');
        }
        else for (auto& a : args) add(a);
        Value b = Value::str(bytes); // buf*/Buf[T] are the mutable spellings
        b.hashKind = (inv.s.rfind("buf", 0) == 0 || inv.s.rfind("Buf", 0) == 0) ? "Buf" : "Blob";
        b.ofType = "uint" + std::to_string(w * 8); // blob8 IS Blob[uint8] — the [T] always shows
        return b;
    }
    if (inv.t == VT::Type &&
        (inv.s == "Set" || inv.s == "SetHash" || inv.s == "Bag" || inv.s == "BagHash" ||
         inv.s == "Mix" || inv.s == "MixHash") && m == "new-from-pairs") {
        // pairs contribute key => WEIGHT (unlike .new, where a Pair is an element)
        ValueList items;
        for (auto& a : args) {
            if (a.t == VT::Range && a.rTo >= 9000000000000000000LL)
                throwTyped("X::Cannot::Lazy", {{"what", inv.s}},
                           "Cannot create a " + inv.s + " from a lazy list");
            if (a.t == VT::Array || a.t == VT::Range) for (auto& x : a.flatten()) items.push_back(x);
            else items.push_back(a);
        }
        for (auto& x : items)
            if (x.t != VT::Pair && x.t != VT::Str && !x.isNumeric())
                throw RakuError{Value::typeObj("X::AdHoc"),
                                "Found invalid value " + x.gist() + " in " + inv.s + ".new-from-pairs"};
        return makeBaggy(items, inv.s, /*pairsAsElements=*/false);
    }
    if (inv.t == VT::Type &&
        (inv.s == "Set" || inv.s == "SetHash" || inv.s == "Bag" || inv.s == "BagHash" ||
         inv.s == "Mix" || inv.s == "MixHash") && m == "new") {
        // single-arg rule: one iterable arg contributes its elements (an itemized
        // `$[...]` resists and stays whole); with several args each arg is ONE
        // element (`Set.new(@a, [3,4])` has two elements)
        ValueList pos; // bare `a => "b"` is a NAMED arg — .new swallows it silently
        for (auto& a : args) if (!a.namedArg) pos.push_back(a);
        ValueList items;
        for (auto& a : pos)
            if (a.t == VT::Range && a.rTo >= 9000000000000000000LL)
                throwTyped("X::Cannot::Lazy", {{"what", inv.s}},
                           "Cannot create a " + inv.s + " from a lazy list");
        // a quanthash arg is ONE element (Bag.new(set <a b c>) has 1 elem);
        // a plain Hash still iterates its pairs under the single-arg rule
        bool wholeQuant = pos.size() == 1 && pos[0].t == VT::Hash && quantValueType(pos[0].hashKind);
        if (pos.size() == 1 && !pos[0].itemized && !wholeQuant) {
            for (auto& x : toList(pos[0])) items.push_back(x);
        }
        else for (auto& a : pos) items.push_back(a);
        Value out = makeBaggy(items, inv.s, /*pairsAsElements=*/true);
        if (!inv.ofType.empty() && out.hash) { // Set[Str].new(...) enforces the key type
            for (auto& kv : *out.hash) {
                Value orig = kv.second.pairKey ? *kv.second.pairKey : Value::str(kv.first);
                if (!typeOrSubsetMatches(orig, inv.ofType))
                    throw RakuError{Value::typeObj("X::TypeCheck::Binding"),
                        "Type check failed for " + inv.s + " key; expected " +
                        inv.ofType + " but got " + orig.gist()};
            }
            out.ofType = inv.ofType;
        }
        return out;
    }
    if (inv.t == VT::Hash && inv.hashKind == "StrDistance") {
        auto fld = [&](const char* k) { auto it = inv.hash->find(k); return it != inv.hash->end() ? it->second : Value::str(""); };
        if (m == "before" || m == "after") return fld(m.c_str());
        if (m == "Str" || m == "gist") return fld("after"); // "$dist" interpolates the resulting string
        if (m == "Bool") return Value::boolean(fld("before").toStr() != fld("after").toStr());
        if (m == "Rat" || m == "FatRat" || m == "Numeric" || m == "Int" || m == "Num" || m == "chars") {
            // a tr/// result carries the substitution count; .new-built ones numify to .after.chars
            auto di = inv.hash->find("distance");
            long long c = di != inv.hash->end() ? di->second.toInt()
                        : methodCall(fld("after"), "chars", ValueList{}).toInt();
            if (m == "Num") return Value::number((double)c);
            if (m == "Int" || m == "Numeric" || m == "chars") return Value::integer(c);
            Value v = Value::rat(BigInt(c), BigInt(1));
            if (m == "FatRat") v.fatRat = true;
            return v;
        }
    }
    if (inv.t == VT::Str && inv.hashKind == "Version") {
        if (m == "parts") { // numeric parts as Ints, alpha parts as Strs, '*' as Whatever
            Value out = Value::array(); out.isList = true;
            const std::string& s = inv.s;
            size_t i = 0;
            while (i < s.size()) {
                unsigned char c = s[i];
                if (std::isdigit(c)) { size_t j = i; while (j < s.size() && std::isdigit((unsigned char)s[j])) j++;
                    out.arr->push_back(Value::integer(std::atoll(s.substr(i, j - i).c_str()))); i = j; }
                else if (std::isalpha(c)) { size_t j = i; while (j < s.size() && std::isalpha((unsigned char)s[j])) j++;
                    out.arr->push_back(Value::str(s.substr(i, j - i))); i = j; }
                else if (c == '*') { Value w; w.t = VT::Whatever; out.arr->push_back(w); i++; }
                else i++;
            }
            return out;
        }
        if (m == "Str") return Value::str(inv.s);
        if (m == "gist" || m == "raku") return Value::str("v" + inv.s);
        if (m == "plus") return Value::boolean(!inv.s.empty() && inv.s.back() == '+');
        if (m == "whatever") return Value::boolean(inv.s.find('*') != std::string::npos);
    }
    if (inv.t == VT::Type && inv.s == "Slip" && m == "new") {
        Value sl = Value::array(args); sl.isList = true; sl.s = "Slip"; return sl;
    }
    if (inv.t == VT::Type && (m == "Baggy" || m == "Setty" || m == "Mixy")) {
        // quanthash coercion types: Set.Baggy is Bag, BagHash.Setty is SetHash, …
        static const std::map<std::string, std::map<std::string, std::string>> co = {
            {"Baggy", {{"Set","Bag"},{"SetHash","BagHash"},{"Bag","Bag"},{"BagHash","BagHash"},{"Mix","Mix"},{"MixHash","MixHash"}}},
            {"Setty", {{"Set","Set"},{"SetHash","SetHash"},{"Bag","Set"},{"BagHash","SetHash"},{"Mix","Set"},{"MixHash","SetHash"}}},
            {"Mixy",  {{"Set","Mix"},{"SetHash","MixHash"},{"Bag","Mix"},{"BagHash","MixHash"},{"Mix","Mix"},{"MixHash","MixHash"}}},
        };
        auto ci2 = co.find(m); auto ti = ci2->second.find(inv.s);
        if (ti != ci2->second.end()) return Value::typeObj(ti->second);
    }
    if (inv.t == VT::Type && inv.s == "Bool" && (m == "pick" || m == "roll")) {
        ValueList tf{Value::boolean(false), Value::boolean(true)};
        Value l = Value::array(tf); l.isList = true;
        return methodCall(l, m, args); // Bool.pick(*) shuffles (False, True)
    }
    if (inv.t == VT::Type && inv.s == "IO::Path" && m == "new") {
        std::string path;
        for (auto& a : args) if (a.t != VT::Pair) { path = a.toStr(); break; }
        rejectNulPath(path);
        Value p = Value::str(path); p.hashKind = "IO";
        // an explicit `:CWD` is the directory this path is relative to; it rides
        // in ofType, which a path value has no other use for
        for (auto& a : args)
            if (a.t == VT::Pair && a.s == "CWD" && a.pairVal) p.ofType = a.pairVal->toStr();
        return p;
    }
    // IO::Spec::Unix / ::Win32 — the per-OS path grammar an IO::Path routes
    // through. A type object with class methods; `.new` answers itself.
    // IO::Spec::Unix / ::Win32 — only the pieces that were missing; everything
    // else (curupdir, rootdir, devnull, canonpath, …) already has a handler
    // further down and must not be shadowed here.
    if (inv.t == VT::Type && inv.s.rfind("IO::Spec::", 0) == 0) {
        if (m == "new") return inv;
        if (m == "dir-sep") return Value::str(inv.s.find("Win32") != std::string::npos ? "\\" : "/");
    }
    // IO::Path flavors: the path value keeps its OS flavor in enumName and
    // routes volume/dirname/basename/cleanup through that IO::Spec
    if (inv.t == VT::Type && inv.s.rfind("IO::Path::", 0) == 0 && m == "new") {
        static const std::set<std::string> kPathFlavors = {"Unix", "Win32", "Cygwin", "QNX"};
        std::string fl = inv.s.substr(10);
        if (kPathFlavors.count(fl)) {
            if (args.empty() || args[0].t == VT::Pair || args[0].toStr().empty())
                throw RakuError{Value::typeObj("X::AdHoc"),
                                "Must specify a non-empty string as a path"};
            std::string path = args[0].toStr();
            rejectNulPath(path);
            Value p = Value::str(path); p.hashKind = "IO"; p.enumName = fl;
            return p;
        }
    }
    if (inv.t == VT::Type && inv.s == "CurrentThreadScheduler" && m == "new") {
        Value v = Value::makeHash(); v.hashKind = "Scheduler";
        (*v.hash)["name"] = Value::str("CurrentThreadScheduler");
        (*v.hash)["sync"] = Value::boolean(true);
        return v;
    }
    if (inv.t == VT::Hash && inv.hashKind == "Scheduler") {
        if (m == "cue" && !args.empty()) {
            Value code = args[0];
            double delay = 0, every = 0; long long times = 0;
            bool sawIn = false, sawAt = false, sawTimes = false;
            Value stopF, catchF;
            for (auto& a : args) {
                if (a.t != VT::Pair || !a.pairVal) continue;
                if (a.s == "in") sawIn = true;
                if (a.s == "at") sawAt = true;
                if (a.s == "times") sawTimes = true;
                if (a.s == "in" || a.s == "at") {
                    double v = a.pairVal->toNum();
                    if (std::isnan(v)) throw RakuError{Value::typeObj("X::Scheduler::CueInNaNSeconds"),
                        "Cannot pass NaN as a number of seconds to Scheduler.cue"};
                    delay = a.s == "in" ? v : std::max(0.0, v - (double)::time(nullptr));
                }
                else if (a.s == "every") {
                    every = a.pairVal->toNum();
                    if (std::isnan(every)) throw RakuError{Value::typeObj("X::Scheduler::CueInNaNSeconds"),
                        "Cannot pass NaN as a number of seconds to Scheduler.cue"};
                    if (std::isinf(every)) every = 0; // ±Inf every: run once, immediately
                }
                else if (a.s == "times") times = a.pairVal->toInt();
                else if (a.s == "stop") stopF = *a.pairVal;
                else if (a.s == "catch") catchF = *a.pairVal;
            }
            if (catchF.t != VT::Code && inv.hash->count("uncaught_handler"))
                catchF = (*inv.hash)["uncaught_handler"]; // scheduler-level handler
            if (sawIn && sawAt)
                throw RakuError{Value::typeObj("X::Scheduler::Cue"), "Cannot specify both :at and :in"};
            if (every > 0 && sawTimes && stopF.t == VT::Code)
                throw RakuError{Value::typeObj("X::Scheduler::Cue"), "Cannot specify :every, :times and :stop together"};
            if (inv.hash->count("sync")) { // CurrentThreadScheduler: run inline, now
                bool sawEvery = false;
                for (auto& a : args) if (a.t == VT::Pair && a.s == "every") sawEvery = true;
                if (sawEvery) // no repetition on the inline scheduler, as in Rakudo
                    throw RakuError{Value::typeObj("X::Scheduler::Cue"),
                        "Cannot specify :every in cue on the CurrentThreadScheduler"};
                if (std::isinf(delay) && delay > 0) { // :in(Inf)/:at(Inf): never runs (-Inf runs NOW)
                    Value c = Value::makeHash(); c.hashKind = "Cancellation";
                    c.ext = std::make_shared<CueState>();
                    return c;
                }
                long long target = times > 0 ? times : 1;
                for (long long i = 0; i < target; i++) {
                    if (stopF.t == VT::Code) { ValueList na; if (callCallable(stopF, na).truthy()) break; }
                    try { ValueList na; callCallable(code, na); }
                    catch (const RakuError& e) {
                        if (catchF.t != VT::Code) throw;
                        ValueList ca{exceptionFor(e)}; callCallable(catchF, ca);
                    }
                }
                Value c = Value::makeHash(); c.hashKind = "Cancellation";
                c.ext = std::make_shared<CueState>();
                return c;
            }
            if (delay < 0 || std::isnan(delay)) delay = 0; // past instants / -Inf run immediately
            return cueJob(code, delay, every, times, stopF, catchF);
        }
        if (m == "loads") {
            sleepYield(0.002); // let cued workers run — a `1 while .loads` spin must not starve them
            return Value::integer(cuedLoads_.load());
        }
    }
    if (inv.t == VT::Hash && inv.hashKind == "Cancellation" && m == "can")
        return Value::boolean(!args.empty() && (args[0].toStr() == "cancel" || args[0].toStr() == "cancelled"));
    if (inv.t == VT::Hash && inv.hashKind == "Cancellation") {
        auto* cs = static_cast<CueState*>(inv.ext.get());
        if (m == "cancel")    { if (cs) cs->cancelled.store(true); return Value::boolean(true); }
        if (m == "cancelled") return Value::boolean(cs && cs->cancelled.load());
    }
    if (inv.t == VT::Type && inv.s == "IO::CatHandle" && m == "new") {
        // minimal CatHandle: a sequence of paths/handles slurped in order
        Value v = Value::makeHash(); v.hashKind = "CatHandle";
        Value files = Value::array();
        for (auto& a : args) {
            if (a.t == VT::Array && a.arr) for (auto& x : *a.arr) files.arr->push_back(x);
            else if (a.t != VT::Pair) files.arr->push_back(a);
        }
        (*v.hash)["files"] = files;
        return v;
    }
    if (inv.t == VT::Hash && inv.hashKind == "CatHandle") {
        if (m == "slurp") {
            std::string out;
            Value files = (*inv.hash)["files"];
            if (files.arr) for (auto& f : *files.arr) {
                ValueList none;
                Value one = methodCall(Value::str(f.toStr()), "slurp", none); // path-string slurp
                out += one.toStr();
            }
            return Value::str(out);
        }
        if (m == "close") return Value::boolean(true);
        // Rakudo has not implemented the write half of CatHandle either — these
        // throw X::NYI there, and roast (A02) checks exactly that
        static const std::set<std::string> catNyi = {
            "flush", "out-buffer", "print", "printf", "print-nl", "put", "say", "write",
            "WRITE", "READ", "EOF"};
        if (catNyi.count(m))
            throw RakuError{Value::typeObj("X::NYI"),
                            m + " is not yet implemented. Sorry."};
        if (m == "slurp-rest")
            throw RakuError{Value::typeObj("X::Obsolete"),
                            "Unsupported use of slurp-rest; in Raku please use slurp with IO::CatHandle"};
        if (m == "Str") return Value::str("<closed IO::CatHandle>");
    }
    if (inv.t == VT::Type && inv.s == "IO::Special" && m == "new") {
        Value sp = Value::str(args.empty() ? "" : args[0].toStr());
        sp.hashKind = "IO::Special"; return sp;
    }
    if (inv.t == VT::Type && m == "new" &&
        (inv.s == "IntStr" || inv.s == "NumStr" || inv.s == "RatStr" || inv.s == "ComplexStr")) {
        // IntStr.new(42, "42") — the number AND its string face
        Value n = args.empty() ? Value::integer(0) : args[0];
        std::string face = args.size() > 1 ? args[1].toStr() : n.toStr();
        n.hashKind = inv.s;
        n.s = face;
        return n;
    }
    if (inv.t == VT::Type && inv.s == "Version" && m == "new") {
        std::string s = args.empty() ? "" : args[0].toStr();
        if (!s.empty() && (s[0] == 'v' || s[0] == 'V')) s = s.substr(1);
        Value v = Value::str(s); v.hashKind = "Version"; return v;
    }
    if ((inv.t == VT::Type && inv.s == "Duration" ||
         inv.t == VT::Num && inv.hashKind == "Duration") && m == "new") {
        // Duration is a number of seconds, tagged so .WHAT/.^name answer Duration
        Value d = Value::number(args.empty() ? 0.0 : args[0].toNum());
        d.hashKind = "Duration";
        return d;
    }
    if (inv.t == VT::Num && inv.hashKind == "Duration") {
        if (m == "Num" || m == "Real") return Value::number(inv.n);
        if (m == "Int") return Value::integer((long long)inv.n);
    }
    if (inv.t == VT::Type && m == "bits") { // native int/num width (2026.06 addition)
        static const std::map<std::string, int> widths = {
            {"int",64},{"uint",64},{"int64",64},{"uint64",64},{"num",64},{"num64",64},
            {"int32",32},{"uint32",32},{"num32",32},{"int16",16},{"uint16",16},
            {"int8",8},{"uint8",8},{"byte",8}};
        auto it = widths.find(inv.s);
        if (it != widths.end()) return Value::integer(it->second);
    }
    // Instant.DateTime — an Instant is posix seconds (tagged Num); build the
    // DateTime from it (zef: `now.DateTime.earlier(:hours(N)).Instant`).
    // `$instant.to-posix` — the POSIX seconds and whether this is a leap second.
    // rakupp's Instant is TAI (POSIX + 10), so the trip back subtracts them.
    if (m == "to-posix" && (inv.hashKind == "Instant" || inv.isNumeric())) {
        Value o = Value::array(); o.isList = true;
        o.arr->push_back(applyArith("-", inv.hashKind == "Instant" ? inv : Value::number(inv.toNum()),
                                    Value::integer(10)));
        o.arr->push_back(Value::boolean(false));
        return o;
    }
    if (m == "DateTime" && inv.hashKind == "Instant" && inv.isNumeric())
        return methodCall(Value::typeObj("DateTime"), "new", ValueList{Value::number(inv.toNum())});
    // `Date.new-from-daycount($n)` — days since the Modified Julian Date epoch
    if (inv.t == VT::Type && inv.s == "Date" && m == "new-from-daycount" && !args.empty()) {
        // MJD day 0 is 1858-11-17, which is 40587 days before the civil epoch
        long long y, mo, d;
        daysToCivil(args[0].toInt() - 40587, y, mo, d);
        Value v = Value::makeHash(); v.hashKind = "Date";
        (*v.hash)["year"] = Value::integer(y);
        (*v.hash)["month"] = Value::integer(mo);
        (*v.hash)["day"] = Value::integer(d);
        return v;
    }
    if (inv.t == VT::Type && inv.s == "Instant" && m == "from-posix") {
        // TAI = POSIX + the 10 pre-1972 leap seconds (Instant.from-posix(32) is 42)
        return Value::number((args.empty() ? 0.0 : args[0].toNum()) + 10.0);
    }
    // `List.tree` / `Array.tree` on a type object is identity (returns the type)
    if (inv.t == VT::Type && m == "tree") return inv;
    // type-level coercions: `DateTime.Date` is Date:U, `Date.DateTime` is DateTime:U
    if (inv.t == VT::Type && (inv.s == "DateTime" || inv.s == "Date") &&
        (m == "Date" || m == "DateTime"))
        return Value::typeObj(m);
    if (inv.t == VT::Type && inv.s == "StrDistance" && m == "new") {
        // StrDistance (the tr/// result value): numifies to .after.chars
        Value h = Value::makeHash(); h.hashKind = "StrDistance";
        for (auto& a2 : args) if (a2.t == VT::Pair && a2.pairVal) (*h.hash)[a2.s] = *a2.pairVal;
        return h;
    }
    if (inv.t == VT::Type && (inv.s == "Buf" || inv.s == "Blob") && (m == "new" || m == "allocate")) {
        if (m == "allocate") {
            for (size_t k = 1; k < args.size(); k++) // fill args must be numeric-ish
                if (args[k].t == VT::Str && args[k].hashKind.empty())
                    throw RakuError{Value::typeObj("X::TypeCheck"),
                        "Cannot use a Str as a fill value in " + inv.s + ".allocate"};
            // allocate(N) → N zero bytes; allocate(N, fill) → N fills;
            // allocate(N, (list)) → the list repeated cyclically
            long long an = args.empty() ? 0 : args[0].toInt();
            if (an < 0) an = 0;
            std::string fill;
            if (args.size() > 1) {
                if (args[1].t == VT::Array || args[1].t == VT::Range)
                    for (auto& e : args[1].flatten()) fill += (char)(unsigned char)(e.toInt() & 0xFF);
                else fill += (char)(unsigned char)(args[1].toInt() & 0xFF);
            }
            if (fill.empty()) fill.push_back('\0');
            std::string bytes;
            for (long long k = 0; k < an; k++) bytes += fill[(size_t)(k % (long long)fill.size())];
            Value b = Value::str(bytes); b.hashKind = inv.s == "Buf" ? "Buf" : "Blob"; return b;
        }
        std::string bytes;
        std::function<void(const Value&)> add = [&](const Value& v) {
            if (v.t == VT::Array && v.arr) { for (auto& e : *v.arr) add(e); }
            else if (v.t == VT::Range) { for (auto& e : v.flatten()) add(e); } // Buf.new(^10)
            else bytes += (char)(unsigned char)(v.toInt() & 0xFF);
        };
        for (auto& a : args) add(a);
        Value b = Value::str(bytes); b.hashKind = inv.s == "Buf" ? "Buf" : "Blob"; return b; // Buf is mutable
    }
    if (inv.t == VT::Type && inv.s == "Pair" && m == "new") {
        Value key = Value::any(), val = Value::any();
        std::vector<Value> pos;
        for (auto& x : args) {
            if (x.t == VT::Pair && x.s == "key")        key = x.pairVal ? *x.pairVal : Value::any();
            else if (x.t == VT::Pair && x.s == "value") val = x.pairVal ? *x.pairVal : Value::any();
            else pos.push_back(x);
        }
        if (!pos.empty())      key = pos[0];
        if (pos.size() >= 2)   val = pos[1];
        Value p = Value::pair(key.toStr(), val);
        if (key.t != VT::Str) p.pairKey = std::make_shared<Value>(key);
        return p;
    }

    // Lock / Semaphore. No-ops under the GIL (it already serialises); backed by real
    // primitives in parallel mode so mutual exclusion actually holds.
    if (inv.t == VT::Type && (inv.s == "Lock" || inv.s == "Lock::Async" || inv.s == "Semaphore")) {
        if (m == "new") {
            Value v = Value::makeHash();
            if (inv.s == "Semaphore") {
                v.hashKind = "Semaphore";
                long n = args.empty() ? 1 : args[0].toInt();
                (*v.hash)["count"] = Value::integer(n);
                if (parallelMode_) { auto st = std::make_shared<SemaphoreState>(); st->count = n; v.ext = st; }
            }
            else {
                // Lock::Async keeps its own type identity (so a `Lock::Async $!l`
                // container accepts it) but shares Lock's method implementations
                // under the cooperative GIL.
                v.hashKind = (inv.s == "Lock::Async") ? "Lock::Async" : "Lock";
                if (parallelMode_) v.ext = std::make_shared<LockState>();
            }
            return v;
        }
    }
    // IO::String / Text::IO::String: an in-memory read handle over a string.
    // $*RAKU / $?RAKU and their .compiler — the runtime/implementation introspection object
    if (inv.t == VT::Hash && (inv.hashKind == "Raku" || inv.hashKind == "Compiler")) {
        bool isComp = inv.hashKind == "Compiler";
        std::string nm = isComp ? "Raku++" : "Raku";
        // Language revision the program is running under (6.c/6.d/6.e), from any
        // `use v6.*` pragma; the compiler object keeps its own version string.
        std::string langVer = langRev_ == 0 ? "6.c" : (langRev_ == 1 ? "6.d" : "6.e");
        if (m == "compiler") { Value c = Value::makeHash(); c.hashKind = "Compiler"; return c; }
        if (m == "backend") return Value::str("cpp"); // rakupp's engine is a C++ tree-walking interpreter, not MoarVM
        if (m == "KERNELnames" || m == "DISTROnames" || m == "VMnames") { // known-platform introspection lists
            Value out = Value::array(); out.isList = true;
            out.arr->push_back(Value::str(m == "KERNELnames" ? "darwin" : m == "DISTROnames" ? "macos" : "moar"));
            return out;
        }
        if (m == "name") return Value::str(nm);
        // The COMPILER's .version answers in Rakudo's YEAR.MONTH scheme — the era
        // of the Rakudo we verify byte-identity against (the conformance oracle;
        // bump when the oracle bumps). This REVERSES an earlier decision to report
        // rakupp's own release here: ecosystem modules gate with
        // `$*RAKU.compiler.version < v2023.12` to ask "do I have modern
        // semantics?", and answering v1.5.x reads as a pre-2000 Rakudo — every
        // such module refused to load (JSON::Class was the witness). Our own
        // release stays visible in .release/.id, and .name still says who we are.
#ifndef RAKUPP_VERSION
#define RAKUPP_VERSION "0.0.0"
#endif
        static const char* kOracleEra = "2026.07"; // Rakudo the battery/spec diff against
        if (m == "version" || m == "lang-version") { Value v = Value::str(isComp && m == "version" ? kOracleEra : langVer); v.hashKind = "Version"; return v; }
        if (m == "auth" || m == "authority") return Value::str("The Raku Community");
        if (m == "desc") return Value::str("Raku++ — a C++ Raku interpreter");
        if (m == "signature") { Value b = Value::str("Raku++"); b.hashKind = "Blob"; return b; } // non-empty Blob
        if (m == "id" || m == "release") return Value::str(RAKUPP_VERSION);
        if (m == "codename") return Value::str("Raku++");
        if (m == "gist" || m == "Str" || m == "raku") return Value::str(nm + " (" + (isComp ? "6.d" : langVer) + ")");
    }
    if (inv.t == VT::Type && (inv.s == "ThreadPoolScheduler" || inv.s == "CurrentThreadScheduler")) {
        if (m == "new") { Value s = Value::makeHash(); s.hashKind = "Scheduler"; (*s.hash)["name"] = Value::str(inv.s); return s; }
    }
    if (inv.t == VT::Type && inv.s == "Channel") {
        if (m == "new") {
            Value c = Value::makeHash(); c.hashKind = "Channel";
            (*c.hash)["queue"] = Value::array();
            (*c.hash)["closed"] = Value::boolean(false);
            auto ps = std::make_shared<PromiseState>();          // the `.closed` Promise
            c.ext = ps;
            Value cp = Value::makeHash(); cp.hashKind = "Promise"; cp.ext = ps;
            (*cp.hash)["status"] = Value::str("Planned");
            (*c.hash)["closedPromise"] = cp;
            return c;
        }
    }
    // Channel — a thread-safe queue. Under the GIL send/receive are simple deque
    // ops; `.closed` is a Promise kept once the channel is closed AND drained.
    if (inv.t == VT::Hash && inv.hashKind == "Channel") {
        auto& q = *(*inv.hash)["queue"].arr;
        auto isClosed = [&]() { return (*inv.hash)["closed"].b; };
        auto keepClosedIfDrained = [&]() {
            if (isClosed() && q.empty() && inv.ext) {
                auto ps = std::static_pointer_cast<PromiseState>(inv.ext);
                bool failed = inv.hash->count("failCause") > 0;
                std::lock_guard<std::mutex> lk(ps->m);
                if (!ps->done) {
                    if (failed) { ps->broken = true; ps->cause = (*inv.hash)["failCause"]; ps->causeMsg = (*inv.hash)["failCause"].toStr(); }
                    else ps->result = Value::boolean(true);
                    ps->done = true;
                }
                ps->cv.notify_all();
                if (inv.hash->count("closedPromise")) (*(*inv.hash)["closedPromise"].hash)["status"] = Value::str(failed ? "Broken" : "Kept");
            }
        };
        if (m == "send") {
            if (isClosed()) throw RakuError{Value::typeObj("X::Channel::SendOnClosed"), "Cannot send a message on a closed channel"};
            Value v = args.empty() ? Value::any() : args[0]; q.push_back(v); return v;
        }
        if (m == "poll") {
            if (q.empty()) { keepClosedIfDrained(); return Value::nil(); }
            Value v = q.front(); q.erase(q.begin()); keepClosedIfDrained(); return v;
        }
        if (m == "receive") {
            // `.receive` BLOCKS until an item arrives (or the channel closes) —
            // under the cooperative GIL that means handing off to the workers that
            // could send. With no async engaged nothing ever could, so answer Nil
            // rather than deadlock; likewise once every worker has finished.
            if (q.empty() && !isClosed() && gilHeld_) {
                // No wall-clock deadline. A 300 ms cap used to stand in for "blocks
                // until an item arrives", which made the wait a RACE against the
                // producer: `start { sleep 0.2; $c.send(...) }` lost it whenever
                // scheduling the worker cost the other 100 ms, and `.receive` then
                // answered Nil — silently, with the program carrying on. That flaked
                // the macOS CI job (t/regression/negated-reduce-and-blocking-receive)
                // on runs that were otherwise green.
                //
                // The condition that actually terminates the wait is already here:
                // once no worker is live and no load is cued, nobody CAN send, so
                // waiting on is a deadlock rather than patience. While a worker is
                // live this waits as long as Rakudo would (measured: a 2 s producer
                // answers at 2005 ms here, 2006 ms there).
                //
                // A finer or backing-off quantum was tried and is WORSE — more GIL
                // churn than the overshoot it saves: S17-channel/stress.t ran 12 s at
                // 20 ms, 29-42 s at 0.5-2 ms. Leave it at 20 ms.
                while (q.empty() && !isClosed()) {
                    if (liveWorkers_.load() <= 0 && cuedLoads_.load() <= 0) break; // nobody left to send
                    yieldToWorkerFor(0.02);
                }
            }
            if (q.empty()) {
                if (isClosed()) {
                    if (inv.hash->count("failCause")) throw RakuError{(*inv.hash)["failCause"], "Channel failed"};
                    throw RakuError{Value::typeObj("X::Channel::ReceiveOnClosed"), "Cannot receive a message on a closed channel"};
                }
                return Value::nil(); // nothing running that could ever send
            }
            Value v = q.front(); q.erase(q.begin()); keepClosedIfDrained(); return v;
        }
        if (m == "close") { (*inv.hash)["closed"] = Value::boolean(true); keepClosedIfDrained(); return Value::boolean(true); }
        if (m == "fail") {
            (*inv.hash)["closed"] = Value::boolean(true);
            Value cause = args.empty() ? Value::str("Died") : args[0];
            if (cause.t != VT::Object) { // wrap a plain cause in X::AdHoc (like die/break)
                auto xit = classes_.find("X::AdHoc");
                if (xit != classes_.end()) { Value ex; ex.t = VT::Object; ex.obj = std::make_shared<ObjectData>(); ex.obj->cls = xit->second; ex.obj->attrs["message"] = Value::str(cause.toStr()); cause = ex; }
            }
            (*inv.hash)["failCause"] = cause;
            // once drained, the .closed Promise breaks with the failure cause
            if (q.empty() && inv.ext) {
                auto ps = std::static_pointer_cast<PromiseState>(inv.ext);
                std::lock_guard<std::mutex> lk(ps->m);
                if (!ps->done) { ps->broken = true; ps->cause = cause; ps->causeMsg = cause.toStr(); ps->done = true; }
                ps->cv.notify_all();
                if (inv.hash->count("closedPromise")) (*(*inv.hash)["closedPromise"].hash)["status"] = Value::str("Broken");
            }
            return Value::boolean(true);
        }
        if (m == "closed") { return (*inv.hash)["closedPromise"]; }
        // `.list`/`.Seq` CONSUME a closed channel: they yield the queued values
        // and drain it (so the .closed Promise then keeps). `.Supply` snapshots
        // without draining (a Supply is a re-tappable stream).
        if (m == "list" || m == "Seq") {
            Value o = Value::array(); *o.arr = q; o.isList = true;
            q.clear(); keepClosedIfDrained();
            return o;
        }
        if (m == "Supply") {
            // A live channel (one carrying its source supplier) re-exposes a live
            // Supply on the SAME supplier, so `$s.Supply.Channel.Supply` forwards
            // emits (IO::Socket::Async::SSL's read path). A plain (from-list)
            // channel yields its queued snapshot as a list-backed Supply.
            if (inv.hash->count("supplier")) {
                Value s = Value::makeHash(); s.hashKind = "Supply";
                (*s.hash)["supplier"] = (*inv.hash)["supplier"];
                return s;
            }
            Value o = Value::array(); *o.arr = q; o.isList = true; return o;
        }
        if (m == "elems") return Value::integer((long long)q.size());
    }
    // Thread — under the GIL a Thread.start runs its block eagerly, but we bump
    // threadDepth_ so `is-initial-thread` correctly reads False inside the block.
    if (inv.t == VT::Type && inv.s == "Thread") {
        if (m == "is-initial-thread") return Value::boolean(threadDepth_ == 0 && !t_isWorker);
        if (m == "start" || m == "run") { // a REAL thread, via the promise machinery
            Value code; for (auto& x : args) if (x.t == VT::Code) code = x;
            Value t = Value::makeHash(); t.hashKind = "Thread";
            for (auto& x : args) if (x.t == VT::Pair && x.s == "name" && x.pairVal) (*t.hash)["name"] = *x.pairVal;
            static std::atomic<long long> nextThreadId{2}; // 1 = the initial thread
            (*t.hash)["id"] = Value::integer(nextThreadId++);
            (*t.hash)["initial"] = Value::boolean(false);
            if (code.t == VT::Code) {
                t.ext = std::static_pointer_cast<void>(
                    std::static_pointer_cast<PromiseState>(spawnPromise(code, t).ext));
                yieldToWorker();
            }
            return t;
        }
        if (m == "new") {
            Value t = Value::makeHash(); t.hashKind = "Thread";
            for (auto& x : args) { if (x.t == VT::Code) (*t.hash)["code"] = x; else if (x.t == VT::Pair && x.pairVal) (*t.hash)[x.s] = *x.pairVal; }
            return t;
        }
    }
    if (inv.t == VT::Hash && inv.hashKind == "Thread") {
        if (m == "is-initial-thread") return Value::boolean(inv.hash->count("initial") ? (*inv.hash)["initial"].b : (threadDepth_ == 0));
        if (m == "finish" || m == "join") {
            if (inv.ext) awaitPromise(std::static_pointer_cast<PromiseState>(inv.ext));
            return inv;
        }
        if (m == "run" || m == "start") { // Thread.new(:code).run — start it now
            if (inv.hash->count("code") && !inv.ext) {
                Value t = inv;
                t.ext = std::static_pointer_cast<void>(
                    std::static_pointer_cast<PromiseState>(spawnPromise((*inv.hash)["code"]).ext));
                yieldToWorker();
                return t;
            }
            return inv;
        }
        if (m == "id") return inv.hash->count("id") ? (*inv.hash)["id"] : Value::integer(1);
        if (m == "name") return inv.hash->count("name") ? (*inv.hash)["name"] : Value::str("<anon>");
        if (m == "Str" || m == "gist") { // Thread<ID>(NAME)
            std::string id = inv.hash->count("id") ? (*inv.hash)["id"].toStr() : "1";
            std::string nm = inv.hash->count("name") ? (*inv.hash)["name"].toStr() : "<anon>";
            return Value::str("Thread<" + id + ">(" + nm + ")");
        }
    }
    if (inv.t == VT::Type && (inv.s == "Supplier" || inv.s == "Supplier::Preserving")) {
        if (m == "new" || m == "preserving") {
            Value s = Value::makeHash(); s.hashKind = "Supplier"; (*s.hash)["taps"] = Value::array();
            // Supplier::Preserving buffers every emit and replays the buffer to any
            // tap that connects later (Cro emits the request into $!in before the
            // async connect pipeline taps it).
            if (inv.s == "Supplier::Preserving" || m == "preserving") {
                (*s.hash)["preserving"] = Value::boolean(true);
                (*s.hash)["buffer"] = Value::array();
            }
            return s;
        }
    }
    // Supplier: a live push source. Its Supply shares the taps list; emit/done fan out to them.
    if (inv.t == VT::Hash && inv.hashKind == "Supplier") {
        if (m == "Supply") { Value s = Value::makeHash(); s.hashKind = "Supply"; (*s.hash)["supplier"] = inv; return s; } // live (no "values")
        if (m == "emit") { Value v = args.empty() ? Value::any() : args[0];
            if (inv.hash->count("preserving") && (*inv.hash)["preserving"].truthy() && inv.hash->count("buffer"))
                (*inv.hash)["buffer"].arr->push_back(v); // replayed to late taps
            if (inv.hash->count("taps")) for (auto& t : *(*inv.hash)["taps"].arr) {
                if (t.t != VT::Hash) continue;
                if (t.hash->count("closed") && (*t.hash)["closed"].truthy()) continue; // head/first already finished
                bool complete = false;
                ValueList outs = applyTapChain(t, v, complete);
                if (t.hash->count("emit") && (*t.hash)["emit"].t == VT::Code) {
                    // push the tap's react ctx so `done` inside the whenever block
                    // closes the enclosing react (the block runs here, on whatever
                    // thread emitted — reactStack_ is thread-local, so it wasn't set)
                    std::shared_ptr<ReactCtx> rctx = t.ext ? std::static_pointer_cast<ReactCtx>(t.ext) : nullptr;
                    for (auto& o : outs) {
                        ValueList one{o};
                        if (rctx) reactStack_.push_back(rctx);
                        // `next` in a whenever skips this value; `last` closes the tap
                        try { callCallable((*t.hash)["emit"], one); if (rctx) reactStack_.pop_back(); }
                        catch (NextEx&) { if (rctx) reactStack_.pop_back(); }
                        catch (LastEx&) { if (rctx) reactStack_.pop_back(); (*t.hash)["closed"] = Value::boolean(true); complete = true; break; }
                        catch (...) { if (rctx) reactStack_.pop_back(); throw; }
                        if (rctx && rctx->closed) break; // `done` inside the block ended the react
                    }
                }
                if (complete) { // head(n)/first done → fire the tap's done and release a react source
                    (*t.hash)["closed"] = Value::boolean(true);
                    if (t.hash->count("done") && (*t.hash)["done"].t == VT::Code) { ValueList none; callCallable((*t.hash)["done"], none); }
                    if (t.ext) { auto ctx = std::static_pointer_cast<ReactCtx>(t.ext); std::lock_guard<std::mutex> lk(ctx->m); if (ctx->liveSources > 0) ctx->liveSources--; ctx->cv.notify_all(); }
                }
            }
            return Value::boolean(true); }
        if (m == "done") {
            // Remember the done state so a tap that registers LATER (an eager
            // `start { $s.emit(…); $s.done }` that ran before the react tapped it)
            // is closed immediately instead of leaving its react source live forever.
            (*inv.hash)["done_state"] = Value::boolean(true);
            if (inv.hash->count("taps")) for (auto& t : *(*inv.hash)["taps"].arr) {
                if (t.t == VT::Hash && t.hash->count("closed") && (*t.hash)["closed"].truthy()) continue; // already done (head/first)
                if (t.t == VT::Hash && t.hash->count("done") && (*t.hash)["done"].t == VT::Code) { ValueList none; callCallable((*t.hash)["done"], none); }
                if (t.ext) { auto ctx = std::static_pointer_cast<ReactCtx>(t.ext); std::lock_guard<std::mutex> lk(ctx->m); if (ctx->liveSources > 0) ctx->liveSources--; ctx->cv.notify_all(); }
            }
            return Value::boolean(true); }
        if (m == "quit") {
            Value ex = args.empty() ? Value::any() : args[0];
            if (inv.hash->count("taps")) for (auto& t : *(*inv.hash)["taps"].arr) { if (t.t == VT::Hash && t.hash->count("quit") && (*t.hash)["quit"].t == VT::Code) { ValueList one{ex}; callCallable((*t.hash)["quit"], one); } }
            return Value::boolean(true); }
        if (m == "Seq" || m == "list") { Value o = Value::array(); o.isList = true; return o; }
    }
    // Supply as a type object: constructors that build an eager, list-backed Supply.
    if (inv.t == VT::Type && inv.s == "Supply") {
        auto mkSupply = [&](ValueList vals) { Value s = Value::makeHash(); s.hashKind = "Supply"; Value v = Value::array(); *v.arr = std::move(vals); (*s.hash)["values"] = v; return s; };
        if (m == "from-list") {
            // +@values single-arg rule: ONE array arg (from-list(@source)) emits its
            // elements; with several args each stays whole (from-list([1,2],[3,4,5])
            // is two list values). A Range always expands.
            ValueList out;
            if (args.size() == 1 && args[0].t == VT::Array && args[0].arr && !args[0].itemized) {
                for (auto& x : *args[0].arr) out.push_back(x);
            } else for (auto& a : args) {
                if (a.t == VT::Range) { for (auto& x : a.flatten()) out.push_back(x); }
                else if (a.t == VT::Array && a.isList && a.arr) { for (auto& x : *a.arr) out.push_back(x); }
                else out.push_back(a);
            }
            return mkSupply(out);
        }
        if (m == "list") { Value o = Value::array(); o.isList = true; o.arr->push_back(inv); return o; } // Supply type → (Supply,)
        if (m == "merge") { ValueList all; for (auto& a : flattenArgs(args)) { if (a.t == VT::Hash && a.hashKind == "Supply" && a.hash->count("values")) for (auto& x : *(*a.hash)["values"].arr) all.push_back(x); } return mkSupply(all); }
        if (m == "zip") {
            // zip N list-backed supplies element-wise (stopping at the shortest); an
            // optional :with(&op) combines each row instead of emitting a tuple List.
            std::vector<Value> streams; Value withOp;
            for (auto& a : args) {
                if (a.t == VT::Pair && (a.s == "with" || a.s == "as") && a.pairVal) { withOp = *a.pairVal; continue; }
                if (!(a.t == VT::Hash && a.hashKind == "Supply" && a.hash->count("values")))
                    throw RakuError{Value::typeObj("X::Supply::Combinator"), "zip requires Supply arguments"};
                streams.push_back(a);
            }
            if (streams.size() == 1) return streams[0]; // zipping one supply is a === noop
            size_t n = SIZE_MAX;
            for (auto& s : streams) n = std::min(n, (*s.hash)["values"].arr->size());
            if (streams.empty()) n = 0;
            ValueList out;
            for (size_t i = 0; i < n; i++) {
                ValueList row; for (auto& s : streams) row.push_back((*(*s.hash)["values"].arr)[i]);
                if (withOp.t == VT::Code) out.push_back(callCallable(withOp, row));
                else { Value tup = Value::array(); tup.isList = true; *tup.arr = std::move(row); out.push_back(tup); }
            }
            return mkSupply(out);
        }
        if (m == "interval") { ValueList v; for (int i = 0; i < 5; i++) v.push_back(Value::integer(i)); return mkSupply(v); } // finite stand-in
        if (m == "empty") return mkSupply({});
    }
    if (inv.t == VT::Type && inv.s == "Promise") {
        Value p = Value::makeHash(); p.hashKind = "Promise";
        if (m == "in" || m == "at") { (*p.hash)["kind"] = Value::str("timer"); (*p.hash)["seconds"] = args.empty() ? Value::number(0) : args[0]; (*p.hash)["status"] = Value::str("Planned"); return p; }
        if (m == "anyof" || m == "allof") {
            (*p.hash)["kind"] = Value::str(m); Value ps = Value::array();
            for (auto& x : flattenArgs(args)) {
                if (!(x.t == VT::Hash && x.hashKind == "Promise"))
                    throw RakuError{Value::typeObj("X::Promise::Combinator"),
                        "Can only create a Promise combinator out of defined Promises"};
                ps.arr->push_back(x);
            }
            (*p.hash)["promises"] = ps; (*p.hash)["status"] = Value::str("Planned"); return p;
        }
        if (m == "new") {
            // A manual (vow-controlled) promise: starts Planned, later kept/broken.
            auto st = std::make_shared<PromiseState>();
            p.ext = st;
            (*p.hash)["status"] = Value::str("Planned");
            return p;
        }
        if (m == "start") {
            // Promise.start(&code): run on a worker + cooperative yield, like `start`.
            Value code; for (auto& x : args) if (x.t == VT::Code) code = x;
            if (code.t != VT::Code) {
                auto st = std::make_shared<PromiseState>(); st->done = true; st->result = args.empty() ? Value::any() : args[0];
                p.ext = st; (*p.hash)["result"] = st->result; (*p.hash)["status"] = Value::str("Kept");
                return p;
            }
            Value pr = spawnPromise(code);
            yieldToWorker();
            return pr;
        }
        if (m == "kept" || m == "broken") {
            auto st = std::make_shared<PromiseState>();
            Value v = args.empty() ? Value::boolean(true) : args[0];
            st->done = true;
            if (m == "broken") { st->broken = true; st->cause = v; st->causeMsg = v.toStr(); }
            else st->result = v;
            p.ext = st;
            (*p.hash)["result"] = v;
            (*p.hash)["status"] = Value::str(m == "broken" ? "Broken" : "Kept");
            return p;
        }
    }
    if (inv.t == VT::Type && inv.s == "Proc::Async") {
        if (m == "new") {
            Value p = Value::makeHash(); p.hashKind = "Proc::Async";
            Value argv = Value::array();
            for (auto& x : args) {
                if (x.t == VT::Pair) continue; // :w / :enc etc.
                // a Positional arg flattens into the command list (slurpy semantics):
                // zef's zrun-async passes ONE list — `Proc::Async.new((|@_).grep(…))`
                if (x.t == VT::Array && x.arr && !x.itemized)
                    for (auto& e : *x.arr) argv.arr->push_back(e);
                else argv.arr->push_back(x);
            }
            (*p.hash)["argv"] = argv; (*p.hash)["taps"] = Value::array();
            return p;
        }
    }
    if (inv.t == VT::Hash && inv.hashKind == "Proc::Async") {
        if (m == "stdout" || m == "stderr" || m == "Supply") { Value s = Value::makeHash(); s.hashKind = "Supply"; (*s.hash)["proc"] = inv; (*s.hash)["stream"] = Value::str(m); return s; }
        if (m == "start") {
            Value pr = Value::makeHash(); pr.hashKind = "Promise";
            (*pr.hash)["kind"] = Value::str("proc"); (*pr.hash)["proc"] = inv;
            (*pr.hash)["status"] = Value::str("Planned");
            // record :cwd so the (lazy) run happens in the right directory —
            // zef's tar extract runs `tar -zxvf <basename>` with :cwd(archive dir)
            for (auto& a : args)
                if (a.t == VT::Pair && a.s == "cwd" && a.pairVal)
                    (*pr.hash)["cwd"] = Value::str(a.pairVal->toStr());
            return pr;
        }
        // `.command` is the argv the process was constructed with, as a List
        if (m == "command") {
            auto it = inv.hash->find("argv");
            Value out = Value::array(); out.isList = true;
            if (it != inv.hash->end() && it->second.arr) *out.arr = *it->second.arr;
            return out;
        }
        if (m == "kill" || m == "close-stdin" || m == "print" || m == "say" || m == "write" || m == "put") return Value::boolean(true);
        // after runProcPromise stored the exit status on the proc:
        if (m == "exitcode") { auto it = inv.hash->find("exitcode"); return it != inv.hash->end() ? it->second : Value::integer(-1); }
        if (m == "so" || m == "Bool") { auto it = inv.hash->find("exitcode"); return Value::boolean(it != inv.hash->end() && it->second.toInt() == 0); }
    }
    // Segment continues in MethodCallPart2.cpp — same ordered chain.
    if (auto r = methodCallPart2(inv, m, args, rwArgs)) return std::move(*r);
    // Segment continues in MethodCallPart3.cpp — same ordered chain.
    if (auto r = methodCallPart3(inv, m, args, rwArgs)) return std::move(*r);
    if (auto r = methodCallTail(inv, m, args, rwArgs)) return std::move(*r);
    // fallthrough: unknown method — but any method call on Nil returns Nil
    if (inv.t == VT::Nil) return Value::nil();
    if (std::getenv("RAKUPP_TRACE"))
        std::cerr << "[NoMethod] ." << m << " on " << inv.typeName()
                  << " at " << (srcFile_.empty() ? "?" : srcFile_) << ":" << curLine_ << "\n";
    throwTyped("X::Method::NotFound",
               {{"method", m}, {"typename", inv.typeName()}},
               "No such method '" + m + "' for invocant of type '" + inv.typeName() + "'");
}

// ---------------- real supply wiring (on-demand supplies, async sockets) ----
// The tap-driven model that a live Cro server needs: `supply {…}` returns an
// on-demand Supply holding its block; tapping it runs the block with `emit`
// routed to the tap's callback and `whenever` wiring inner taps that stay live
// after the block returns (I/O workers push through them later). The legacy
// eager semantics survive as drainSupplyBlock for value-context consumers.

// A phaser block inside a supply/whenever body, as a callable closing over the
// body's definition scope (the phaser may run when the body never has).
static Value supplyPhaserCode(const Block* b, std::shared_ptr<Env> closure) {
    Value v; v.t = VT::Code; v.code = std::make_shared<Callable>();
    v.code->body = &b->stmts; v.code->isBlock = true; v.code->closure = std::move(closure);
    return v;
}
// Collect LAST/QUIT/CLOSE phasers from a block's top-level statements.
static void scanSupplyPhasers(const Value& blk, std::vector<Value>* lastP,
                              std::vector<Value>* quitP, std::vector<Value>* closeP) {
    if (blk.t != VT::Code || !blk.code || !blk.code->body) return;
    for (auto& s : *blk.code->body) {
        if (s->kind != NK::Block) continue;
        auto* b = static_cast<Block*>(s.get());
        if (lastP  && b->phaser == "LAST")  lastP->push_back(supplyPhaserCode(b, blk.code->closure));
        if (quitP  && b->phaser == "QUIT")  quitP->push_back(supplyPhaserCode(b, blk.code->closure));
        if (closeP && b->phaser == "CLOSE") closeP->push_back(supplyPhaserCode(b, blk.code->closure));
    }
}
// A callable that runs `fn` with `ctx` re-established as the active supply
// activation — used for whenever bodies and done/quit hooks that fire later
// (possibly from an I/O worker thread holding the GIL).
static Value ctxCallable(std::shared_ptr<SupplyTapCtx> ctx,
                         std::function<Value(Interpreter&, ValueList&)> fn) {
    Value v; v.t = VT::Code; v.code = std::make_shared<Callable>();
    v.code->builtin = [ctx, fn](Interpreter& I, ValueList& a) -> Value {
        I.tctx_.tapStack.push_back(ctx);
        struct G { std::vector<std::shared_ptr<SupplyTapCtx>>& s; ~G() { s.pop_back(); } } g{I.tctx_.tapStack};
        return fn(I, a);
    };
    return v;
}

void Interpreter::maybeFinishSupply(const std::shared_ptr<SupplyTapCtx>& ctx) {
    if (!ctx || ctx->doneFired || ctx->done) return;
    if (!ctx->blockDone || ctx->pending > 0) return;
    ctx->doneFired = true;
    if (ctx->doneCb.t == VT::Code) { ValueList na; try { callCallable(ctx->doneCb, na); } catch (...) {} }
    closeTapHandle(ctx->tap);
}

void Interpreter::closeTapHandle(const std::shared_ptr<TapHandle>& h) {
    if (!h) return;
    std::vector<std::function<void()>> closers;
    std::vector<Value> phasers;
    {
        std::lock_guard<std::mutex> lk(h->m);
        if (h->closed) return;
        h->closed = true;
        closers.swap(h->closers);
        phasers.swap(h->closePhasers);
    }
    for (auto& f : closers) { try { f(); } catch (...) {} }
    for (auto& p : phasers) if (p.t == VT::Code) { ValueList na; try { callCallable(p, na); } catch (...) {} }
}

Value Interpreter::drainSupplyBlock(const Value& s) {
    // Legacy eager semantics: run the block now, collecting emits; a die becomes
    // the supply's QUIT reason. Emits route to the collector via the tap stack.
    Value blk = (s.t == VT::Hash && s.hash->count("block")) ? (*s.hash)["block"] : Value::nil();
    ValueList vals; bool quit = false; Value quitReason; std::string quitMsg;
    auto ctx = std::make_shared<SupplyTapCtx>();
    ctx->collect = &vals;
    tctx_.tapStack.push_back(ctx);
    supplyCloseStack_.emplace_back();
    try {
        if (blk.t == VT::Code) { ValueList na; callCallable(blk, na); }
    }
    catch (RakuError& e) { quit = true; quitReason = exceptionFor(e); quitMsg = e.message; }
    catch (...) { tctx_.tapStack.pop_back(); supplyCloseStack_.pop_back(); throw; }
    tctx_.tapStack.pop_back();
    ctx->collect = nullptr; // vals is about to go out of scope with this frame
    {
        auto closers = std::move(supplyCloseStack_.back());
        supplyCloseStack_.pop_back();
        for (auto& cb : closers) if (cb.t == VT::Code) { try { ValueList na; callCallable(cb, na); } catch (...) {} }
    }
    Value out = Value::makeHash(); out.hashKind = "Supply";
    Value v = Value::array(); *v.arr = std::move(vals); (*out.hash)["values"] = v;
    if (quit) { (*out.hash)["quit-reason"] = quitReason; (*out.hash)["quit-message"] = Value::str(quitMsg); }
    return out;
}

// Build an IO::Socket::Async connection value around a connected fd.
static Value makeAsyncSocket(int fd) {
    Value s = Value::makeHash(); s.hashKind = "AsyncSocket";
    (*s.hash)["fd"] = Value::integer(fd);
    sockaddr_in a{}; socklen_t alen = sizeof(a);
    if (::getsockname(fd, (sockaddr*)&a, &alen) == 0) {
        (*s.hash)["socket-host"] = Value::str(inet_ntoa(a.sin_addr));
        (*s.hash)["socket-port"] = Value::integer(ntohs(a.sin_port));
    }
    alen = sizeof(a);
    if (::getpeername(fd, (sockaddr*)&a, &alen) == 0) {
        (*s.hash)["peer-host"] = Value::str(inet_ntoa(a.sin_addr));
        (*s.hash)["peer-port"] = Value::integer(ntohs(a.sin_port));
    }
    return s;
}

// ---- signal(SIGINT, …) : OS signals delivered as a Supply --------------------
// Self-pipe trick: the async-signal-safe handler just writes the signum to a
// pipe; a single dispatcher worker reads the pipe and fans each signal out to
// the registered taps, running their whenever block under the GIL. This needs
// no per-signal thread and stays robust with worker threads around.
struct SignalTapRec {
    Value emit, done;                     // whenever block + done callback
    std::shared_ptr<ReactCtx> react;      // react ctx (so `done` closes it), or null
    std::shared_ptr<TapHandle> handle;    // closed => skip
};
#if !defined(_WIN32)
static int g_sigPipe[2] = {-1, -1};
static std::mutex g_sigTapMutex;
static std::multimap<int, std::shared_ptr<SignalTapRec>> g_sigTaps;
static std::set<int> g_sigInstalled;      // signals whose handler is installed
static void rakuppSignalHandler(int sig) {
    if (g_sigPipe[1] >= 0) { unsigned char c = (unsigned char)sig; ssize_t r = ::write(g_sigPipe[1], &c, 1); (void)r; }
}
#endif

// Signal number → its enum name ("SIGINT"), or "" if unknown.
static std::string signalNameOfNumber(int sig);
// Build the Signal enum value passed to a whenever block ($_ / $sig).
static Value makeSignalEnumValue(int sig) {
    std::string name = signalNameOfNumber(sig);
    Value v = name.empty() ? Value::integer(sig) : Value::enumVal(name, sig);
    v.enumType = "Signal";
    return v;
}
// The Signal-enum names available on THIS platform. Each is `#ifdef`-guarded:
// Windows' <signal.h> defines only a handful (SIGINT/SIGILL/SIGFPE/SIGSEGV/
// SIGTERM/SIGABRT/SIGBREAK), so the rest are simply absent there.
static const std::map<std::string, int>& signalNameMap() {
    static const std::map<std::string, int> m = {
#ifdef SIGHUP
        {"SIGHUP", SIGHUP},
#endif
#ifdef SIGINT
        {"SIGINT", SIGINT},
#endif
#ifdef SIGQUIT
        {"SIGQUIT", SIGQUIT},
#endif
#ifdef SIGILL
        {"SIGILL", SIGILL},
#endif
#ifdef SIGTRAP
        {"SIGTRAP", SIGTRAP},
#endif
#ifdef SIGABRT
        {"SIGABRT", SIGABRT},
#endif
#ifdef SIGFPE
        {"SIGFPE", SIGFPE},
#endif
#ifdef SIGKILL
        {"SIGKILL", SIGKILL},
#endif
#ifdef SIGBUS
        {"SIGBUS", SIGBUS},
#endif
#ifdef SIGSEGV
        {"SIGSEGV", SIGSEGV},
#endif
#ifdef SIGSYS
        {"SIGSYS", SIGSYS},
#endif
#ifdef SIGPIPE
        {"SIGPIPE", SIGPIPE},
#endif
#ifdef SIGALRM
        {"SIGALRM", SIGALRM},
#endif
#ifdef SIGTERM
        {"SIGTERM", SIGTERM},
#endif
#ifdef SIGURG
        {"SIGURG", SIGURG},
#endif
#ifdef SIGSTOP
        {"SIGSTOP", SIGSTOP},
#endif
#ifdef SIGTSTP
        {"SIGTSTP", SIGTSTP},
#endif
#ifdef SIGCONT
        {"SIGCONT", SIGCONT},
#endif
#ifdef SIGCHLD
        {"SIGCHLD", SIGCHLD},
#endif
#ifdef SIGTTIN
        {"SIGTTIN", SIGTTIN},
#endif
#ifdef SIGTTOU
        {"SIGTTOU", SIGTTOU},
#endif
#ifdef SIGUSR1
        {"SIGUSR1", SIGUSR1},
#endif
#ifdef SIGUSR2
        {"SIGUSR2", SIGUSR2},
#endif
#ifdef SIGWINCH
        {"SIGWINCH", SIGWINCH},
#endif
#ifdef SIGBREAK
        {"SIGBREAK", SIGBREAK}, // Windows-only
#endif
    };
    return m;
}
int signalNumberOfName(const std::string& n) {
    auto it = signalNameMap().find(n);
    return it != signalNameMap().end() ? it->second : -1;
}
static std::string signalNameOfNumber(int sig) {
    for (auto& kv : signalNameMap()) if (kv.second == sig) return kv.first;
    return "";
}
// A signal value passed to `signal()`: the Signal type-object (`SIGINT`, s=name),
// an Int-backed enum value, or a plain integer.
static int signalNumberOf(const Value& v) {
    if (v.t == VT::Type) return signalNumberOfName(v.s);
    if (v.t == VT::Int && !v.enumName.empty()) { int n = signalNumberOfName(v.enumName); if (n > 0) return n; }
    if (v.isNumeric()) return (int)v.toInt();
    return -1;
}

// `whenever Promise.in(N) { … }` in a react: fire the block ONCE after a real N-second
// delay, as a live react source — a worker sleeps (GIL released, so sibling whenevers'
// I/O runs meanwhile), then runs the block and drops the source. Without this the
// timer resolved immediately (a timeout fired at t=0, defeating the guard).
// `whenever Promise.in(N)` inside a supply {} block: a real timer. Holds the
// supply open (pending) and fires the block after N seconds — unless the supply
// closes first, in which case the worker cancels early (so it can't delay exit).
Value Interpreter::spawnSupplyTimer(double secs, Value blk, std::shared_ptr<SupplyTapCtx> ctx) {
    engageGil();
    ctx->pending++;
    liveWorkers_++;
    reapFinishedWorkers();
    auto fin = std::make_shared<std::atomic<bool>>(false);
    auto spawnScope = tctx_.cur ? tctx_.cur : global_;
    Interpreter* self = this;
    if (secs < 0) secs = 0;
    Value fireW = ctxCallable(ctx, [blk, ctx](Interpreter& I2, ValueList&) -> Value {
        if (!ctx->done && !ctx->doneFired) { ValueList none; try { I2.callCallable(blk, none); } catch (NextEx&) {} catch (LastEx&) {} }
        ctx->pending--;
        I2.maybeFinishSupply(ctx);
        return Value::any();
    });
    workers_.push_back({BigStackThread([self, secs, fireW, fin, spawnScope, ctx]() mutable {
        t_isWorker = true;
        double slept = 0;
        while (slept < secs && !ctx->done && !ctx->doneFired) { // GIL not held; cancellable
            std::this_thread::sleep_for(std::chrono::duration<double>(0.05));
            slept += 0.05;
        }
        self->gil_.lock();
        ExecContext wctx; self->loadCtx(wctx);
        self->tctx_.cur = spawnScope;
        self->tctx_.dynStack.push_back(spawnScope.get());
        ValueList none; try { self->callCallable(fireW, none); } catch (...) {}
        self->gilYieldNotify();
        self->liveWorkers_--;
        fin->store(true, std::memory_order_release);
    }), fin});
    Value t = Value::makeHash(); t.hashKind = "Tap"; return t;
}

Value Interpreter::spawnTimerWhenever(double secs, Value blk, std::shared_ptr<ReactCtx> ctx) {
    engageGil();
    if (ctx) { std::lock_guard<std::mutex> lk(ctx->m); ctx->liveSources++; }
    liveWorkers_++;
    reapFinishedWorkers();
    auto fin = std::make_shared<std::atomic<bool>>(false);
    auto spawnScope = tctx_.cur ? tctx_.cur : global_;
    Interpreter* self = this;
    if (secs < 0) secs = 0;
    if (secs > 35) secs = 35; // bounded like sleepYield, so a stray huge timer can't hang the process
    workers_.push_back({BigStackThread([self, secs, blk, ctx, fin, spawnScope]() mutable {
        t_isWorker = true;
        std::this_thread::sleep_for(std::chrono::duration<double>(secs)); // GIL not held
        self->gil_.lock();
        ExecContext wctx; self->loadCtx(wctx);
        tctx_.cur = spawnScope;
        tctx_.dynStack.push_back(spawnScope.get());
        if (ctx) self->reactStack_.push_back(ctx);
        if (!(ctx && ctx->closed)) {
            ValueList none;
            try { self->callCallable(blk, none); } catch (NextEx&) {} catch (LastEx&) {} catch (...) {}
        }
        if (ctx) self->reactStack_.pop_back();
        if (ctx) { std::lock_guard<std::mutex> lk(ctx->m); if (ctx->liveSources > 0) ctx->liveSources--; ctx->cv.notify_all(); }
        self->gilYieldNotify();
        self->liveWorkers_--;
        fin->store(true, std::memory_order_release);
    }), fin});
    Value t = Value::makeHash(); t.hashKind = "Tap"; return t;
}

Value Interpreter::tapSignal(const std::vector<int>& sigs, Value emitCb, Value doneCb,
                             std::shared_ptr<ReactCtx> reactCtx) {
    engageGil();
    auto handle = std::make_shared<TapHandle>();
#if defined(_WIN32)
    // Windows: the POSIX self-pipe + sigaction machinery isn't available, and
    // <signal.h> exposes only a handful of signals. Return a non-emitting tap
    // (Ctrl-C falls to the default handler) so `signal()` still type-checks and
    // programs that merely construct the Supply keep working.
    (void)sigs; (void)emitCb; (void)doneCb; (void)reactCtx;
    Value t = Value::makeHash(); t.hashKind = "Tap"; t.ext = handle;
    (*t.hash)["wired"] = Value::boolean(true);
    return t;
#else
    auto rec = std::make_shared<SignalTapRec>();
    rec->emit = emitCb; rec->done = doneCb; rec->react = reactCtx; rec->handle = handle;

    // Lazily create the self-pipe + dispatcher worker exactly once.
    static std::once_flag pipeOnce;
    Interpreter* self = this;
    auto spawnScope = tctx_.cur ? tctx_.cur : global_;
    std::call_once(pipeOnce, [&] {
        if (::pipe(g_sigPipe) != 0) { g_sigPipe[0] = g_sigPipe[1] = -1; return; }
        liveWorkers_++;
        reapFinishedWorkers();
        auto fin = std::make_shared<std::atomic<bool>>(false);
        workers_.push_back({BigStackThread([self, spawnScope, fin]() mutable {
            t_isWorker = true;
            for (;;) {
                unsigned char c;
                ssize_t n = ::read(g_sigPipe[0], &c, 1);        // GIL not held
                if (n <= 0) break;
                int sig = c;
                std::vector<std::shared_ptr<SignalTapRec>> taps;
                {
                    std::lock_guard<std::mutex> lk(g_sigTapMutex);
                    auto range = g_sigTaps.equal_range(sig);
                    for (auto it = range.first; it != range.second; ++it) taps.push_back(it->second);
                }
                if (taps.empty()) continue;
                self->gil_.lock();
                ExecContext wctx; self->loadCtx(wctx);
                tctx_.cur = spawnScope;
                tctx_.dynStack.push_back(spawnScope.get());
                for (auto& t : taps) {
                    if (t->handle && t->handle->closed) continue;
                    // push the react ctx so a `done` inside the block closes it
                    if (t->react) self->reactStack_.push_back(t->react);
                    if (t->emit.t == VT::Code) {
                        Value sv = makeSignalEnumValue(sig);
                        ValueList one{sv};
                        try { self->callCallable(t->emit, one); }
                        catch (RakuError& e) { fprintf(stderr, "===WARNING=== signal handler died: %s\n", e.message.c_str()); }
                        catch (...) {}
                    }
                    if (t->react) self->reactStack_.pop_back();
                }
                self->gilYieldNotify();
            }
            self->liveWorkers_--;
            fin->store(true, std::memory_order_release);
        }), fin});
    });

    // Register the tap and install a handler for each signal (once each).
    {
        std::lock_guard<std::mutex> lk(g_sigTapMutex);
        for (int sig : sigs) {
            g_sigTaps.insert({sig, rec});
            if (g_sigInstalled.insert(sig).second) {
                struct sigaction sa{}; sa.sa_handler = rakuppSignalHandler;
                sigemptyset(&sa.sa_mask); sa.sa_flags = SA_RESTART;
                ::sigaction(sig, &sa, nullptr);
            }
        }
    }
    // closing the tap unregisters it (the handler stays; the dispatcher skips it)
    std::vector<int> sigsCopy = sigs;
    handle->closers.push_back([rec, sigsCopy] {
        std::lock_guard<std::mutex> lk(g_sigTapMutex);
        for (int sig : sigsCopy) {
            auto range = g_sigTaps.equal_range(sig);
            for (auto it = range.first; it != range.second; )
                it = (it->second == rec) ? g_sigTaps.erase(it) : std::next(it);
        }
    });
    // When wired into a react block, arrange for the tap to be torn down when the
    // block ends (via `done` or all sources completing) — otherwise the signal
    // dispatcher keeps this tap live and re-fires the handler on the next signal.
    if (reactCtx) {
        std::lock_guard<std::mutex> lk(reactCtx->m);
        reactCtx->extTaps.push_back(handle);
    }
    Value t = Value::makeHash(); t.hashKind = "Tap"; t.ext = handle;
    (*t.hash)["wired"] = Value::boolean(true);
    return t;
#endif // !_WIN32
}

Value Interpreter::tapSupply(const Value& s, Value emitCb, Value doneCb, Value quitCb) {
    if (!(s.t == VT::Hash && s.hashKind == "Supply" && s.hash)) {
        Value t = Value::makeHash(); t.hashKind = "Tap"; return t;
    }
    auto& h = *s.hash;
    // 1) on-demand block: run it now; whenevers inside wire inner taps that may
    //    outlive this call (fed by I/O workers).
    if (h.count("block")) {
        Value blk = h.at("block");
        auto handle = std::make_shared<TapHandle>();
        auto ctx = std::make_shared<SupplyTapCtx>();
        ctx->emitCb = emitCb; ctx->doneCb = doneCb; ctx->quitCb = quitCb; ctx->tap = handle;
        std::vector<Value> quitP;
        scanSupplyPhasers(blk, nullptr, &quitP, &handle->closePhasers);
        tctx_.tapStack.push_back(ctx);
        noCycleBreak_++;
        struct CBGuard { int& n; ~CBGuard() { n--; } } cbGuard{noCycleBreak_};
        try {
            if (blk.t == VT::Code) { ValueList na; callCallable(blk, na); }
            tctx_.tapStack.pop_back();
            // the block returned: with no live inner taps the supply is done
            ctx->blockDone = true;
            maybeFinishSupply(ctx);
        }
        catch (RakuError& e) {
            tctx_.tapStack.pop_back();
            Value ex = exceptionFor(e);
            bool handled = false;
            for (auto& q : quitP) { ValueList one{ex}; try { callCallable(q, one); handled = true; } catch (...) {} }
            if (!handled && quitCb.t == VT::Code) { ValueList one{ex}; try { callCallable(quitCb, one); handled = true; } catch (...) {} }
            closeTapHandle(handle);
            if (!handled) throw;
        }
        catch (...) { tctx_.tapStack.pop_back(); closeTapHandle(handle); throw; }
        Value t = Value::makeHash(); t.hashKind = "Tap"; t.ext = handle;
        (*t.hash)["wired"] = Value::boolean(true);
        return t;
    }
    // 2) live Supplier-backed supply: register a tap record; emit/done/quit fan out later
    if (h.count("supplier")) {
        Value tapRec = Value::makeHash();
        (*tapRec.hash)["emit"] = emitCb; (*tapRec.hash)["done"] = doneCb; (*tapRec.hash)["quit"] = quitCb;
        if (h.count("chain")) {
            Value chain = Value::array();
            for (auto& step : *h.at("chain").arr) {
                Value s2 = Value::makeHash(); *s2.hash = *step.hash;
                (*s2.hash)["state"] = Value::makeHash();
                chain.arr->push_back(s2);
            }
            (*tapRec.hash)["chain"] = chain;
        }
        Value sup = h.at("supplier");
        if (sup.t == VT::Hash && sup.hash->count("taps")) (*sup.hash)["taps"].arr->push_back(tapRec);
        // Supplier::Preserving: replay every buffered value to this fresh tap (through
        // its own transform chain), so a tap that connects after the emits still sees
        // them (Cro's request-into-$!in-before-connect pattern).
        if (sup.t == VT::Hash && sup.hash->count("preserving") && (*sup.hash)["preserving"].truthy() &&
            sup.hash->count("buffer") && emitCb.t == VT::Code) {
            for (auto& bv : *(*sup.hash)["buffer"].arr) {
                bool complete = false;
                ValueList outs = applyTapChain(tapRec, bv, complete);
                for (auto& o : outs) { ValueList one{o}; try { callCallable(emitCb, one); } catch (NextEx&) {} catch (LastEx&) { complete = true; break; } }
                if (complete) break;
            }
        }
        // already-done supplier: fire done immediately so wiring completes
        if (sup.t == VT::Hash && sup.hash->count("done_state") && (*sup.hash)["done_state"].truthy() &&
            doneCb.t == VT::Code) { ValueList na; try { callCallable(doneCb, na); } catch (...) {} }
        tapRec.hashKind = "Tap";
        return tapRec;
    }
    // 3) async listen: bind now, accept on a worker; each connection is emitted
    //    (under the GIL) through emitCb.
    // signal Supply tapped inside a `supply {…}` block (no react ctx — the
    // block's own `done`/tapStack drives closure)
    if (h.count("kind") && h.at("kind").toStr() == "signal") {
        std::vector<int> sigs;
        if (h.count("signals") && h.at("signals").arr)
            for (auto& n : *h.at("signals").arr) sigs.push_back((int)n.toInt());
        return tapSignal(sigs, emitCb, doneCb, nullptr);
    }
    if (h.count("kind") && h.at("kind").toStr() == "async-listen") {
        std::string host = h.count("host") ? h.at("host").toStr() : "localhost";
        int port = h.count("port") ? (int)h.at("port").toInt() : 0;
        int lfd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (lfd < 0) throw RakuError{Value::typeObj("X::IO"), "Cannot create socket"};
        int yes = 1; setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
        sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons((uint16_t)port);
        if (host.empty() || host == "0.0.0.0") addr.sin_addr.s_addr = INADDR_ANY;
        else {
            std::string rh = (host == "localhost") ? "127.0.0.1" : host;
            addr.sin_addr.s_addr = inet_addr(rh.c_str());
            if (addr.sin_addr.s_addr == INADDR_NONE) {
                if (hostent* he = gethostbyname(rh.c_str())) memcpy(&addr.sin_addr, he->h_addr, he->h_length);
            }
        }
        if (::bind(lfd, (sockaddr*)&addr, sizeof(addr)) < 0 || ::listen(lfd, 128) < 0) {
            ::close(lfd);
            throw RakuError{Value::typeObj("X::IO"), "Cannot listen on " + host + ":" + std::to_string(port)};
        }
        engageGil();
        auto handle = std::make_shared<TapHandle>();
        handle->closers.push_back([lfd] { ::shutdown(lfd, SHUT_RDWR); ::close(lfd); });
        liveWorkers_++;
        reapFinishedWorkers();
        auto fin = std::make_shared<std::atomic<bool>>(false);
        auto spawnScope = tctx_.cur ? tctx_.cur : global_;
        Interpreter* self = this;
        workers_.push_back({BigStackThread([self, lfd, emitCb, handle, fin, spawnScope]() mutable {
            t_isWorker = true;
            for (;;) {
                int cfd = ::accept(lfd, nullptr, nullptr);       // GIL not held
                if (cfd < 0) break;                              // closed / shutdown
                self->gil_.lock();
                ExecContext wctx; self->loadCtx(wctx);           // fresh registers
                tctx_.cur = spawnScope;
                tctx_.dynStack.push_back(spawnScope.get());
                Value sock = makeAsyncSocket(cfd);
                if (emitCb.t == VT::Code) {
                    ValueList one{sock};
                    try { self->callCallable(emitCb, one); }
                    catch (RakuError& e) { fprintf(stderr, "===WARNING=== async accept handler died: %s\n", e.message.c_str()); }
                    catch (...) {}
                }
                self->gilYieldNotify();
            }
            self->liveWorkers_--;
            fin->store(true, std::memory_order_release);
        }), fin});
        Value t = Value::makeHash(); t.hashKind = "Tap"; t.ext = handle;
        (*t.hash)["wired"] = Value::boolean(true);
        return t;
    }
    // 4) async read: a worker recv()s and emits Blob chunks; EOF fires done.
    if (h.count("kind") && h.at("kind").toStr() == "async-read") {
        Value sock = h.at("socket");
        int fd = (sock.t == VT::Hash && sock.hash->count("fd")) ? (int)(*sock.hash)["fd"].toInt() : -1;
        engageGil();
        auto handle = std::make_shared<TapHandle>();
        handle->closers.push_back([fd] { if (fd >= 0) ::shutdown(fd, SHUT_RD); });
        liveWorkers_++;
        reapFinishedWorkers();
        auto fin = std::make_shared<std::atomic<bool>>(false);
        auto spawnScope = tctx_.cur ? tctx_.cur : global_;
        bool bin = h.count("bin") && h.at("bin").truthy();
        Interpreter* self = this;
        workers_.push_back({BigStackThread([self, fd, emitCb, doneCb, handle, fin, spawnScope, bin]() mutable {
            t_isWorker = true;
            std::vector<char> buf(65536);
            for (;;) {
                ssize_t n = fd >= 0 ? ::recv(fd, buf.data(), buf.size(), 0) : -1;   // GIL not held
                if (n <= 0) break;
                self->gil_.lock();
                ExecContext wctx; self->loadCtx(wctx);
                tctx_.cur = spawnScope;
                tctx_.dynStack.push_back(spawnScope.get());
                Value chunk = Value::str(std::string(buf.data(), (size_t)n));
                if (bin) chunk.hashKind = "Blob";
                if (emitCb.t == VT::Code) {
                    ValueList one{chunk};
                    try { self->callCallable(emitCb, one); }
                    catch (RakuError& e) { fprintf(stderr, "===WARNING=== async read handler died: %s\n", e.message.c_str()); }
                    catch (...) {}
                }
                self->gilYieldNotify();
            }
            self->gil_.lock();
            ExecContext wctx; self->loadCtx(wctx);
            tctx_.cur = spawnScope;
            tctx_.dynStack.push_back(spawnScope.get());
            if (doneCb.t == VT::Code) { ValueList na; try { self->callCallable(doneCb, na); } catch (...) {} }
            self->gilYieldNotify();
            if (fd >= 0) ::close(fd);   // the read worker owns the fd's lifetime
            self->liveWorkers_--;
            fin->store(true, std::memory_order_release);
        }), fin});
        Value t = Value::makeHash(); t.hashKind = "Tap"; t.ext = handle;
        (*t.hash)["wired"] = Value::boolean(true);
        return t;
    }
    // 5) values-backed: eager push-through, then done (or quit)
    if (h.count("values")) {
        if (emitCb.t == VT::Code) for (auto& v : *h.at("values").arr) {
            ValueList one{v};
            try { callCallable(emitCb, one); }
            catch (NextEx&) {}
            catch (LastEx&) { break; }
        }
        if (h.count("quit-reason")) {
            if (quitCb.t == VT::Code) { ValueList one{h.at("quit-reason")}; callCallable(quitCb, one); }
            else throw RakuError{h.at("quit-reason"),
                                 h.count("quit-message") ? h.at("quit-message").toStr() : "Supply quit"};
        }
        else if (doneCb.t == VT::Code) { ValueList na; callCallable(doneCb, na); }
    }
    Value t = Value::makeHash(); t.hashKind = "Tap"; return t;
}

// ---------------- named builtins ----------------
// Test helpers: pull a `:todo`/`:skip` directive and the description out of trailing args.
static std::string testDirective(const ValueList& a) {
    for (auto& x : a) if (x.t == VT::Pair && (x.s == "todo" || x.s == "skip")) {
        std::string why = x.pairVal ? x.pairVal->toStr() : "";
        std::string kind = x.s == "todo" ? "TODO" : "SKIP";
        return (why.empty() || why == "1" || why == "True") ? kind : kind + " " + why;
    }
    return "";
}
static std::string testDesc(const ValueList& a, size_t from) {
    for (size_t i = from; i < a.size(); i++) if (a[i].t != VT::Pair) return a[i].toStr();
    return "";
}

// First numeric argument, coercing a Cool object via its .Bridge/.Numeric method.
static double numArg(Interpreter& I, ValueList& a) {
    if (a.empty()) return 0;
    Value v = a[0];
    if (v.t == VT::Object && v.obj) {
        for (const char* acc : {"Bridge", "Numeric"}) {
            try { ValueList none; Value nv = I.methodCall(v, acc, none);
                  if (nv.isNumeric()) { v = nv; break; } } catch (...) {}
        }
    }
    return v.toNum();
}

// True named builtins (see Interpreter.h): real functions behind the hot
// builtins, shared by the interpreter's map entries and -O's direct calls.
Value rtBAbsSlow(Interpreter& I, const Value& v) {
    ValueList none;
    return I.methodCall(v, "abs", none);   // full semantics: augment, objects, junctions, Rat/big/Num
}
Value rtBChr(Interpreter&, const Value& v) {
    long long cp = v.big ? LLONG_MAX : v.toInt();
    if (cp < 0 || cp > 0x10FFFF)
        throw RakuError{Value::typeObj("X::AdHoc"),
            "chr codepoint " + (v.big ? v.big->toString() : std::to_string(cp)) + " is out of bounds"};
    return Value::str(cpToUtf8((uint32_t)cp));
}
Value rtBOrd(Interpreter&, const Value& v) {
    auto c = utf8cp(v.toStr());
    return c.empty() ? Value::nil() : Value::integer(c[0]);
}
Value rtBSay(Interpreter& I, const Value& v)   { std::string out = I.gistOf(v); out += "\n"; return I.ioEmit(out, "$*OUT", false); }
Value rtBPrint(Interpreter& I, const Value& v) { return I.ioEmit(I.strOf(v), "$*OUT", false); }
Value rtBPut(Interpreter& I, const Value& v)   { std::string out = I.strOf(v); out += "\n"; return I.ioEmit(out, "$*OUT", false); }
Value rtBNote(Interpreter& I, const Value& v)  { std::string out = I.gistOf(v); out += "\n"; return I.ioEmit(out, "$*ERR", true); }
Value rtBUc(Interpreter&, const Value& v)    { return Value::str(mapCase(v.toStr(), 1, 0)); }
Value rtBLc(Interpreter&, const Value& v)    { return Value::str(mapCase(v.toStr(), 0, 0)); }
Value rtBChars(Interpreter&, const Value& v) { return Value::integer(graphemeCount(v.toStr())); }
Value rtBSqrt(Interpreter& I, const Value& v) {
    if (v.t == VT::Complex) return complexSqrt(v.n, v.im);
    ValueList one{v};
    double x = numArg(I, one);   // same coercion the sub form uses (Object → .Bridge/.Numeric)
    if (x < 0 && I.langRev_ >= 2) return Value::complex(0, std::sqrt(-x));
    return Value::number(std::sqrt(x));
}
// Delegators — one methodCall, exactly the sub form (augment/objects/junctions intact).
static Value rtBMeth(Interpreter& I, const Value& v, const char* m) { ValueList none; return I.methodCall(v, m, none); }
Value rtBSignSlow(Interpreter& I, const Value& v) { return rtBMeth(I, v, "sign"); }
Value rtBTruncate(Interpreter& I, const Value& v) { return rtBMeth(I, v, "truncate"); }
Value rtBIsPrime(Interpreter& I, const Value& v)  { return rtBMeth(I, v, "is-prime"); }
Value rtBFlip(Interpreter& I, const Value& v)     { return rtBMeth(I, v, "flip"); }
Value rtBTrim(Interpreter& I, const Value& v)     { return rtBMeth(I, v, "trim"); }
Value rtBChomp(Interpreter& I, const Value& v)    { return rtBMeth(I, v, "chomp"); }
Value rtBChop(Interpreter& I, const Value& v)     { return rtBMeth(I, v, "chop"); }
// Trig family: Complex — or an Object whose .Numeric/.Bridge may yield one —
// via the method path; everything else through numArg, like the sub forms.
static Value rtBMath1(Interpreter& I, const Value& v, const char* name, double (*f)(double)) {
    if (v.t == VT::Complex || v.t == VT::Object) { ValueList none; return I.methodCall(v, name, none); }
    ValueList one{v};
    return Value::number(f(numArg(I, one)));
}
Value rtBSin(Interpreter& I, const Value& v)   { return rtBMath1(I, v, "sin",   (double(*)(double))std::sin); }
Value rtBCos(Interpreter& I, const Value& v)   { return rtBMath1(I, v, "cos",   (double(*)(double))std::cos); }
Value rtBTan(Interpreter& I, const Value& v)   { return rtBMath1(I, v, "tan",   (double(*)(double))std::tan); }
Value rtBAsin(Interpreter& I, const Value& v)  { return rtBMath1(I, v, "asin",  (double(*)(double))std::asin); }
Value rtBAcos(Interpreter& I, const Value& v)  { return rtBMath1(I, v, "acos",  (double(*)(double))std::acos); }
Value rtBAtan(Interpreter& I, const Value& v)  { return rtBMath1(I, v, "atan",  (double(*)(double))std::atan); }
Value rtBSinh(Interpreter& I, const Value& v)  { return rtBMath1(I, v, "sinh",  (double(*)(double))std::sinh); }
Value rtBCosh(Interpreter& I, const Value& v)  { return rtBMath1(I, v, "cosh",  (double(*)(double))std::cosh); }
Value rtBTanh(Interpreter& I, const Value& v)  { return rtBMath1(I, v, "tanh",  (double(*)(double))std::tanh); }
Value rtBAsinh(Interpreter& I, const Value& v) { return rtBMath1(I, v, "asinh", (double(*)(double))std::asinh); }
Value rtBAcosh(Interpreter& I, const Value& v) { return rtBMath1(I, v, "acosh", (double(*)(double))std::acosh); }
Value rtBAtanh(Interpreter& I, const Value& v) { return rtBMath1(I, v, "atanh", (double(*)(double))std::atanh); }

void Interpreter::registerBuiltins() {
    auto& B = builtins_;

    B["say"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.size() == 1) return rtBSay(I, a[0]);
        std::string out;
        for (auto& v : a) out += I.gistOf(v);
        out += "\n"; return I.ioEmit(out, "$*OUT", false);
    };
    B["print"] = [](Interpreter& I, ValueList& a) -> Value {
        std::string out; for (auto& v : a) out += I.strOf(v);
        return I.ioEmit(out, "$*OUT", false);
    };
    B["put"] = [](Interpreter& I, ValueList& a) -> Value {
        // a Junction argument AUTOTHREADS the call: `put 1 & 2` writes "1\n2\n"
        // (unlike `print`, whose per-eigenstate output simply runs together).
        for (size_t i = 0; i < a.size(); i++) {
            const Value& j = a[i];
            if (j.t == VT::Array && j.arr &&
                (j.enumName == "any" || j.enumName == "all" || j.enumName == "one" || j.enumName == "none")) {
                for (auto& e : *j.arr) { ValueList a2 = a; a2[i] = e; I.callBuiltin("put", a2); }
                return Value::boolean(true);
            }
        }
        std::string out; for (auto& v : a) out += I.strOf(v); out += "\n";
        return I.ioEmit(out, "$*OUT", false);
    };
    B["gist"] = [](Interpreter& I, ValueList& a) -> Value {
        std::string out; bool first = true;
        for (auto& v : a) { if (!first) out += " "; first = false; out += I.gistOf(v); }
        return Value::str(out);
    };
    B["WHAT"] = [](Interpreter& I, ValueList& a) -> Value {
        return a.empty() ? Value::any() : I.methodCall(a[0], "WHAT", ValueList{});
    };
    B["note"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.empty()) return I.ioEmit("Noted\n", "$*ERR", true); // no-arg default
        std::string out; for (auto& v : a) out += I.gistOf(v); out += "\n";
        return I.ioEmit(out, "$*ERR", true);
    };
    B["warn"] = [](Interpreter& I, ValueList& a) -> Value {
        if (I.quietDepth_ > 0) return Value::boolean(true); // muted inside quietly {…}
        if (a.empty()) { std::cerr << "Warning: something's wrong\n"; return Value::boolean(true); }
        for (auto& v : a) std::cerr << I.gistOf(v); std::cerr << "\n"; return Value::boolean(true);
    };
    B["die"] = [](Interpreter& I, ValueList& a) -> Value {
        Value payload = a.empty() ? Value::str("Died") : a[0];
        // die with no argument reuses the current $! ("Died" only if $! is undefined)
        if (a.empty()) { Value* be = I.tctx_.cur->find("$!"); if (be && be->t != VT::Nil && be->t != VT::Type) payload = *be; }
        std::string msg = payload.toStr();
        // exception objects: prefer a readable .message / .Str accessor
        if (payload.t == VT::Object && payload.obj) {
            for (const char* acc : {"message", "Str"}) {
                try { ValueList none; Value m = I.methodCall(payload, acc, none);
                      if (m.t == VT::Str && !m.s.empty()) { msg = m.s; break; } } catch (...) {}
            }
        } else {
            // wrap a plain string/number into an X::AdHoc exception (so .message/.^name work in CATCH)
            auto it = I.classes_.find("X::AdHoc");
            if (it != I.classes_.end()) {
                Value ex; ex.t = VT::Object; ex.obj = std::make_shared<ObjectData>();
                ex.obj->cls = it->second;
                ex.obj->attrs["message"] = Value::str(msg);
                ex.obj->attrs["payload"] = a.empty() ? Value::str(msg) : a[0]; // .payload is what was thrown
                payload = ex;
            }
        }
        throw RakuError{payload, msg};
    };
    // Re-dispatch to the next candidate (currently: a built-in shadowed by a user method).
    // callsame/callwith return its result; nextsame/nextwith return it FROM the current routine.
    // `lastcall` marks the current candidate as the final one: a subsequent
    // callsame/nextsame finds no more candidates (returns Nil / an empty result).
    // Frames below the current routine activation's floor belong to a CALLER's
    // dispatch: invisible here. A visible-empty stack inside someone's dispatch
    // means "nothing further" — soft Nil (Rakudo: a bottom method's nextsame
    // does not die); a truly empty stack is the hard no-dispatcher error.
    auto dispTop = [](Interpreter& I) -> Interpreter::RedispatchCtx* {
        if (I.redispatchStack_.size() <= I.tctx_.redispatchFloor) return nullptr;
        return &I.redispatchStack_.back();
    };
    B["lastcall"] = [dispTop](Interpreter& I, ValueList&) -> Value {
        if (auto* d = dispTop(I)) d->lastcall = true;
        return Value::boolean(true);
    };
    B["callsame"] = [dispTop](Interpreter& I, ValueList&) -> Value {
        auto* d = dispTop(I);
        if (!d) {
            if (I.redispatchStack_.empty()) I.throwTypedV("X::NoDispatcher", {{"redispatcher", Value::str("callsame")}},
                              "callsame is not in the dynamic scope of a dispatcher");
            return Value::nil(); // exhausted chain bottom
        }
        if (d->lastcall) return Value::nil(); // trimmed by lastcall
        return d->next(d->sameArgs);
    };
    B["callwith"] = [dispTop](Interpreter& I, ValueList& a) -> Value {
        auto* d = dispTop(I);
        if (!d) {
            if (I.redispatchStack_.empty()) I.throwTypedV("X::NoDispatcher", {{"redispatcher", Value::str("callwith")}},
                              "callwith is not in the dynamic scope of a dispatcher");
            return Value::nil();
        }
        if (d->lastcall) return Value::nil();
        return d->next(a);
    };
    B["nextsame"] = [dispTop](Interpreter& I, ValueList&) -> Value {
        auto* d = dispTop(I);
        if (!d) {
            if (I.redispatchStack_.empty()) I.throwTypedV("X::NoDispatcher", {{"redispatcher", Value::str("nextsame")}},
                              "nextsame is not in the dynamic scope of a dispatcher");
            throw ReturnEx{Value::nil()};
        }
        if (d->lastcall) throw ReturnEx{Value::nil()};
        throw ReturnEx{d->next(d->sameArgs)};
    };
    B["samewith"] = [dispTop](Interpreter& I, ValueList& a) -> Value {
        // re-dispatch the CURRENT routine from scratch with new args, returning its result
        auto* d = dispTop(I);
        if (!d || !d->restart)
            I.throwTypedV("X::NoDispatcher", {{"redispatcher", Value::str("samewith")}},
                              "samewith is not in the dynamic scope of a dispatcher");
        return d->restart(a);
    };
    B["nextwith"] = [dispTop](Interpreter& I, ValueList& a) -> Value {
        auto* d = dispTop(I);
        if (!d) {
            if (I.redispatchStack_.empty()) I.throwTypedV("X::NoDispatcher", {{"redispatcher", Value::str("nextwith")}},
                              "nextwith is not in the dynamic scope of a dispatcher");
            throw ReturnEx{Value::nil()};
        }
        if (d->lastcall) throw ReturnEx{Value::nil()};
        throw ReturnEx{d->next(a)};
    };
    B["fail"] = [](Interpreter& I, ValueList& a) -> Value {
        // Return an (undefined) Failure from the enclosing sub carrying an exception:
        // `fail $ex` / `fail "msg"` (→ X::AdHoc) / bare `fail` (picks up $!). `//` /
        // .defined treat it as undefined, so a fallback value is chosen.
        Value ex;
        if (!a.empty() && a[0].t == VT::Object) {
            ex = a[0];
        } else if (!a.empty()) {
            auto it = I.classes_.find("X::AdHoc");
            if (it != I.classes_.end()) {
                ex.t = VT::Object; ex.obj = std::make_shared<ObjectData>(); ex.obj->cls = it->second;
                ex.obj->attrs["message"] = Value::str(a[0].toStr());
            } else ex = Value::str(a[0].toStr());
        } else {
            Value* be = I.tctx_.cur->find("$!");
            if (be && be->t != VT::Nil && be->t != VT::Type) ex = *be;
        }
        // a bare `fail` with no $! still carries an exception — X::AdHoc
        // "Failed" — so `.exception.message` answers rather than dying on Any
        if (ex.t != VT::Object)
            ex = I.makeTypedEx("X::AdHoc", {}, ex.t == VT::Str ? ex.s : "Failed");
        Value f = Value::makeHash(); f.hashKind = "Failure";
        (*f.hash)["exception"] = ex;
        (*f.hash)["message"] = ex.obj && ex.obj->attrs.count("message")
                             ? ex.obj->attrs["message"] : Value::str("Failed");
        throw ReturnEx{f};
    };
    // val(Str) — a fully-numeric string becomes the matching allomorph
    // (IntStr/RatStr/NumStr/ComplexStr: the number AND its source spelling);
    // anything else passes through unchanged. prompt() routes its line here.
    // The definition lives in Interpreter.cpp so MAIN's argv gets the identical
    // conversion — the two used to differ, and that is issue #11.
    auto valAllomorph = [](const Value& v) -> Value { return rakupp::valAllomorph(v); };
    B["val"] = [valAllomorph](Interpreter&, ValueList& a) -> Value {
        return a.empty() ? Value::nil() : valAllomorph(a[0]);
    };
    B["prompt"] = [valAllomorph](Interpreter&, ValueList& a) -> Value {
        if (!a.empty()) { std::cout << a[0].toStr(); std::cout.flush(); }
        std::string line;
        if (!std::getline(std::cin, line)) return Value::nil(); // EOF -> Nil
        if (!line.empty() && line.back() == '\r') line.pop_back();
        return valAllomorph(Value::str(line)); // Raku returns Str-with-val: numeric input is an allomorph
    };
    B["__qx__"] = [](Interpreter&, ValueList& a) -> Value { // qx// / qqx// shell capture
        std::string cmd = a.empty() ? "" : a[0].toStr();
        std::string outp; char buf[4096]; size_t n;
#if defined(_WIN32)
        FILE* p = _popen(cmd.c_str(), "r");
        if (!p) return Value::str("");
        while ((n = fread(buf, 1, sizeof buf, p)) > 0) outp.append(buf, n);
        _pclose(p);
#else
        FILE* p = popen(cmd.c_str(), "r");
        if (!p) return Value::str("");
        while ((n = fread(buf, 1, sizeof buf, p)) > 0) outp.append(buf, n);
        pclose(p);
#endif
        return Value::str(outp);
    };
    B["dd"] = [](Interpreter&, ValueList& a) -> Value {
        std::string out;
        for (size_t i = 0; i < a.size(); i++) { if (i) out += ", "; out += (a[i].t == VT::Str ? "\"" + a[i].s + "\"" : a[i].gist()); }
        std::cerr << out << "\n";
        return a.empty() ? Value::any() : a[0];
    };
    // mathematical constants (callable as bare terms: pi, tau, e, and π τ 𝑒)
    // NB: pi/tau/e are TERMS (handled in NameTerm eval), not subs — calling `pi()` must die.
    // junction list-op constructors: all(...)/any(...)/none(...)/one(...)
    for (const char* jn : {"all", "any", "none", "one"}) {
        std::string name = jn;
        B[name] = [name](Interpreter&, ValueList& a) -> Value {
            Value j = Value::array(); j.enumName = name;
            // one-arg rule: any(@a) spreads the single iterable one level;
            // any(x, y, …) keeps each argument as ONE eigenstate (lists whole)
            if (a.size() == 1 && a[0].t == VT::Array && a[0].arr)
                for (auto& x : *a[0].arr) j.arr->push_back(x);
            else if (a.size() == 1 && a[0].t == VT::Range)
                for (auto& x : a[0].flatten()) j.arr->push_back(x);
            else
                for (auto& v : a) j.arr->push_back(v);
            return j;
        };
    }

    // --- Test module ---
    B["plan"] = [](Interpreter& I, ValueList& a) -> Value {
        I.usedTest_ = true;
        // plan skip-all => "reason" : emit an empty SKIP plan and exit the test file
        bool skipAll = false; std::string reason;
        for (auto& x : a) {
            if (x.t == VT::Pair && x.s == "skip-all") { skipAll = true; reason = x.pairVal ? x.pairVal->toStr() : ""; }
            else if (x.t == VT::Str && x.s == "skip-all") skipAll = true;
        }
        if (skipAll) { I.planned_ = 0; std::cout << "1..0 # SKIP " << reason << "\n" << std::flush; throw ExitEx{0}; }
        if (!a.empty()) { I.planned_ = a[0].toInt(); std::cout << std::string(4 * I.subtestDepth_, ' ') << "1.." << I.planned_ << "\n"; }
        return Value::boolean(true);
    };
    B["ok"] = [](Interpreter& I, ValueList& a) -> Value {
        bool c = !a.empty() && a[0].truthy();
        I.emitTest(c, testDesc(a, 1), testDirective(a));
        return Value::boolean(c);
    };
    B["nok"] = [](Interpreter& I, ValueList& a) -> Value {
        bool c = a.empty() || !a[0].truthy();
        I.emitTest(c, testDesc(a, 1), testDirective(a));
        return Value::boolean(c);
    };
    // eq honouring a Junction expected value: `is $got, ("a"|"b")` autothreads the
    // comparison and collapses per the junction's kind (any/all/one/none).
    auto isEq = [](const Value& got, const Value& exp) -> bool {
        auto scalarEq = [](const Value& g, const Value& e) {
            // Two undefined values match regardless of how they stringify: Rakudo's
            // `is` treats an undefined expected as a definedness check, so
            // `is @a[11], Any` passes even though a bare undef stringifies to '' but
            // `Any` gists to '(Any)'. This is additive to the stringify compare below
            // so a defined-empty got still matches an undefined expected (`is Nil, ''`).
            if (!defined(g) && !defined(e)) return true;
            // Same-size lists compare elementwise FIRST so an undefined element
            // matches an undefined expected (`is %h{<B C>}, (Any, Any)` — the
            // sides stringify differently but are equal). Only an all-elements
            // match short-circuits; anything else falls through to the plain
            // string compare, so this can only ADD passes.
            if (g.t == VT::Array && e.t == VT::Array && g.arr && e.arr &&
                g.arr->size() == e.arr->size() && !g.arr->empty()) {
                bool all = true;
                for (size_t i = 0; i < g.arr->size() && all; i++) {
                    const Value& gi = (*g.arr)[i];
                    const Value& ei = (*e.arr)[i];
                    if (!defined(gi) && !defined(ei)) continue;
                    if (gi.toStr() != ei.toStr()) all = false;
                }
                if (all) return true;
            }
            // Otherwise Rakudo compares stringified values with `eq` (Test::is), so
            // `is 1/3, 0.333333` passes on matching decimal forms. (Exact-numeric
            // comparison lives in is-approx / cmp-ok, not plain `is`.)
            return g.toStr() == e.toStr();
        };
        if (exp.t == VT::Array && exp.arr &&
            (exp.enumName == "any" || exp.enumName == "all" || exp.enumName == "one" || exp.enumName == "none")) {
            int t = 0, total = (int)exp.arr->size();
            for (auto& br : *exp.arr) if (scalarEq(got, br)) t++;
            return exp.enumName == "any" ? t > 0 : exp.enumName == "all" ? t == total
                 : exp.enumName == "one" ? t == 1 : t == 0;
        }
        return scalarEq(got, exp);
    };
    // An object argument (e.g. an exception in `is $!, 'msg'`) compares by its Str —
    // which for an Exception is its .message, matching `~$!` (via strOf).
    auto isStrify = [](Interpreter& I, Value& v) { if (v.t == VT::Object) v = Value::str(I.strOf(v)); };
    B["is"] = [isEq, isStrify](Interpreter& I, ValueList& a) -> Value {
        Value got = a.size() > 0 ? a[0] : Value::any();
        Value exp = a.size() > 1 ? a[1] : Value::any();
        isStrify(I, got); isStrify(I, exp);
        bool c = isEq(got, exp);
        std::string dir = testDirective(a);
        std::string diag = (!c && dir.empty()) ? "# expected: '" + exp.toStr() + "'\n# got:      '" + got.toStr() + "'\n" : "";
        I.emitTest(c, testDesc(a, 2), dir, diag);
        return Value::boolean(c);
    };
    B["isnt"] = [isEq, isStrify](Interpreter& I, ValueList& a) -> Value {
        Value got = a.size() > 0 ? a[0] : Value::any();
        Value exp = a.size() > 1 ? a[1] : Value::any();
        isStrify(I, got); isStrify(I, exp);
        bool c = !isEq(got, exp);
        I.emitTest(c, a.size() > 2 ? a[2].toStr() : "");
        return Value::boolean(c);
    };
    auto likeTest = [](Interpreter& I, ValueList& a, bool want) -> Value {
        std::string got = a.empty() ? "" : a[0].toStr();
        bool m = false;
        if (a.size() > 1) {
            if (a[1].t == VT::Regex) m = I.regexMatch(got, a[1].s).truthy();
            else m = got.find(a[1].toStr()) != std::string::npos;
        }
        bool c = (m == want);
        std::string dir = testDirective(a);
        std::string diag = (!c && dir.empty()) ? "# got: '" + got + "'\n" : "";
        I.emitTest(c, testDesc(a, 2), dir, diag);
        return Value::boolean(c);
    };
    B["like"]   = [likeTest](Interpreter& I, ValueList& a) -> Value { return likeTest(I, a, true); };
    B["unlike"] = [likeTest](Interpreter& I, ValueList& a) -> Value { return likeTest(I, a, false); };
    B["is-deeply"] = [](Interpreter& I, ValueList& a) -> Value {
        bool c = a.size() >= 2 && deepEq(a[0], a[1]);
        I.emitTest(c, a.size() > 2 ? a[2].toStr() : "");
        if (!c && a.size() >= 2) { // failure diagnostics (stderr), Rakudo-style
            std::cerr << "# expected: " << rakuRepr(a[1]) << "\n"
                      << "#      got: " << rakuRepr(a[0]) << "\n";
        }
        return Value::boolean(c);
    };
    B["cmp-ok"] = [](Interpreter& I, ValueList& a) -> Value {
        // cmp-ok($a, $op, $b, $desc) — $op may be an operator NAME or a Code
        // (the roast idiom `cmp-ok $x, &[!==], $y`).
        bool c = false;
        if (a.size() >= 3 && a[1].t == VT::Code) {
            c = I.callCallable(a[1], ValueList{a[0], a[2]}).truthy();
        }
        else if (a.size() >= 3) {
            std::string op = a[1].toStr();
            const Value& x = a[0]; const Value& y = a[2];
            if (op == "==") c = x.toNum() == y.toNum();
            else if (op == "!=") c = x.toNum() != y.toNum();
            else if (op == "<") c = x.toNum() < y.toNum();
            else if (op == ">") c = x.toNum() > y.toNum();
            else if (op == "<=") c = x.toNum() <= y.toNum();
            else if (op == ">=") c = x.toNum() >= y.toNum();
            else if (op == "eq") c = x.toStr() == y.toStr();
            else if (op == "ne") c = x.toStr() != y.toStr();
            else c = applyArith(op, x, y).truthy(); // ===, eqv, ~~, before/after, user ops…
        }
        // On failure, present the operands via .raku (the "presentable" form) — not
        // .Str, which some objects make die — and name the matcher like Rakudo.
        std::string diag;
        if (!c && a.size() >= 3) {
            auto pres = [&](const Value& v) { Value vv = v; return I.methodCall(vv, "raku", ValueList{}).toStr(); };
            std::string mstr = a[1].t == VT::Code
                ? [&]{ Value m = a[1]; return I.methodCall(m, "gist", ValueList{}).toStr(); }()
                : "'infix:<" + a[1].toStr() + ">'";
            diag = "# expected: " + pres(a[2]) + "\n#  matcher: " + mstr + "\n#      got: " + pres(a[0]) + "\n";
        }
        I.emitTest(c, a.size() > 3 ? a[3].toStr() : "", "", diag);
        return Value::boolean(c);
    };
    B["todo"] = [](Interpreter& I, ValueList& a) -> Value { // todo($reason, $count=1): mark next tests TODO
        I.todoReason_ = a.empty() ? "" : a[0].toStr();
        I.todoRemaining_ = a.size() > 1 ? (int)a[1].toInt() : 1;
        return Value::boolean(true);
    };
    B["pass"] = [](Interpreter& I, ValueList& a) -> Value { I.emitTest(true, a.empty() ? "" : a[0].toStr()); return Value::boolean(true); };
    B["flunk"] = [](Interpreter& I, ValueList& a) -> Value { I.emitTest(false, a.empty() ? "" : a[0].toStr()); return Value::boolean(false); };
    B["diag"] = [](Interpreter&, ValueList& a) -> Value { std::cerr << "# " << (a.empty() ? "" : a[0].toStr()) << "\n"; return Value::boolean(true); };
    B["skip"] = [](Interpreter& I, ValueList& a) -> Value {
        long n = (a.size() > 1) ? a[1].toInt() : 1;
        std::string reason = a.empty() ? "" : a[0].toStr();
        for (long k = 0; k < n; k++) I.emitTest(true, "", "skip " + reason);
        return Value::boolean(true);
    };
    B["dies-ok"] = [](Interpreter& I, ValueList& a) -> Value {
        bool died = false;
        if (!a.empty() && a[0].t == VT::Code) {
            try { I.callCallable(a[0], {}); }
            catch (RakuError&) { died = true; }
            // a loop-control exception with no enclosing loop is a death (X::ControlFlow)
            catch (NextEx&) { died = true; }
            catch (LastEx&) { died = true; }
            catch (RedoEx&) { died = true; }
        }
        I.emitTest(died, a.size() > 1 ? a[1].toStr() : "");
        return Value::boolean(died);
    };
    B["lives-ok"] = [](Interpreter& I, ValueList& a) -> Value {
        bool lived = true;
        if (!a.empty() && a[0].t == VT::Code) { try { I.callCallable(a[0], {}); } catch (RakuError&) { lived = false; } }
        I.emitTest(lived, a.size() > 1 ? a[1].toStr() : "");
        return Value::boolean(lived);
    };
    B["use-ok"] = [](Interpreter& I, ValueList& a) -> Value {
        std::string mod = a.empty() ? "" : a[0].toStr();
        bool ok = true;
        try { I.loadModule(mod); } catch (...) { ok = false; }
        I.emitTest(ok, a.size() > 1 ? a[1].toStr() : ("The module can be use-d ok: " + mod));
        return Value::boolean(ok);
    };
    B["does-ok"] = [](Interpreter& I, ValueList& a) -> Value {
        // does-ok($obj, Role, $desc?) — role/type membership via .does
        bool c = false;
        if (a.size() >= 2) c = I.methodCall(a[0], "does", ValueList{a[1]}).truthy();
        std::string desc;
        for (size_t i = 2; i < a.size(); i++) if (a[i].t == VT::Str) { desc = a[i].s; break; }
        I.emitTest(c, desc);
        return Value::boolean(c);
    };
    B["isa-ok"] = [](Interpreter& I, ValueList& a) -> Value {
        // the "type" argument may be a type object, a type NAME string, or any
        // other value — a plain value stands for its own type (isa-ok 3, 3)
        std::string want = a.size() > 1
            ? (a[1].t == VT::Type ? a[1].s : a[1].t == VT::Str ? a[1].toStr() : a[1].typeName())
            : "";
        std::string got = a.empty() ? "Any" : a[0].typeName();
        // ONE default description for both exit paths — the fast path used to say
        // "isa Int" and the ancestry fallback said nothing at all
        std::string desc = a.size() > 2 ? a[2].toStr() : "The object is-a '" + want + "'";
        // the real .isa knows allomorphs and user-class chains — consult it first
        if (a.size() > 1) {
            ValueList ia{a[1]};
            Value r = I.methodCall(a[0], "isa", ia);
            if (r.truthy()) {
                I.emitTest(true, desc);
                return Value::boolean(true);
            }
        }
        static const std::map<std::string, std::set<std::string>> isa = {
            {"Int", {"Int", "Cool", "Numeric", "Real", "Any", "Mu"}},
            {"Num", {"Num", "Cool", "Numeric", "Real", "Any", "Mu"}},
            {"Str", {"Str", "Cool", "Stringy", "Any", "Mu"}},
            {"Bool", {"Bool", "Any", "Mu"}},
            {"Sub", {"Sub", "Routine", "Block", "Code", "Callable", "Any", "Mu"}},
            {"Method", {"Method", "Routine", "Block", "Code", "Callable", "Any", "Mu"}},
            {"Block", {"Block", "Code", "Callable", "Any", "Mu"}},
            {"Array", {"Array", "List", "Any", "Mu", "Positional"}},
            {"array", {"array", "Array", "List", "Any", "Mu", "Positional", "Iterable"}},
            {"Seq", {"Seq", "List", "Any", "Mu", "Positional", "Iterable"}},
            {"IO::Path", {"IO::Path", "IO", "Cool", "Any", "Mu"}},
            {"IO::Path::Unix", {"IO::Path::Unix", "IO::Path", "IO", "Cool", "Any", "Mu"}},
            {"IO::Path::Win32", {"IO::Path::Win32", "IO::Path", "IO", "Cool", "Any", "Mu"}},
            {"IO::Path::Cygwin", {"IO::Path::Cygwin", "IO::Path", "IO", "Cool", "Any", "Mu"}},
            {"IO::Path::QNX", {"IO::Path::QNX", "IO::Path", "IO", "Cool", "Any", "Mu"}},
            {"Version", {"Version", "Any", "Mu"}},
            {"Blob", {"Blob", "Buf", "Positional", "Any", "Mu"}},
            {"Compiler", {"Compiler", "Any", "Mu"}},
            {"Hash", {"Hash", "Map", "Any", "Mu", "Associative"}},
            {"Pod::Block", {"Pod::Block", "Any", "Mu"}},
            {"Pod::Block::Named", {"Pod::Block::Named", "Pod::Block", "Any", "Mu"}},
            {"Pod::Block::Para", {"Pod::Block::Para", "Pod::Block", "Any", "Mu"}},
            {"Pod::Block::Code", {"Pod::Block::Code", "Pod::Block", "Any", "Mu"}},
            {"Pod::Block::Comment", {"Pod::Block::Comment", "Pod::Block", "Any", "Mu"}},
            {"Pod::Block::Table", {"Pod::Block::Table", "Pod::Block", "Any", "Mu"}},
            {"Pod::Block::Declarator", {"Pod::Block::Declarator", "Pod::Block", "Any", "Mu"}},
            {"Pod::Heading", {"Pod::Heading", "Pod::Block", "Any", "Mu"}},
            {"Pod::Item", {"Pod::Item", "Pod::Block", "Any", "Mu"}},
        };
        // walk a class's ancestry: the built-in isa map, plus a user class's parent
        // chain (incl. a native parent like `is Str`) and extra `is` parents.
        std::function<bool(const std::string&)> ancestorHas = [&](const std::string& cn) -> bool {
            if (cn == want) return true;
            auto mit = isa.find(cn);
            if (mit != isa.end() && mit->second.count(want)) return true;
            auto cit = I.classes_.find(cn);
            if (cit != I.classes_.end() && cit->second) {
                if (cit->second->parent && ancestorHas(cit->second->parent->name)) return true;
                if (!cit->second->nativeParent.empty() && ancestorHas(cit->second->nativeParent)) return true;
                for (auto& ep : cit->second->extraParents) if (ep && ancestorHas(ep->name)) return true;
            }
            return false;
        };
        bool c = ancestorHas(got);
        I.emitTest(c, desc);
        return Value::boolean(c);
    };
    B["is-approx"] = [](Interpreter& I, ValueList& a) -> Value {
        // Complex-aware: compare as points in the plane, |got - exp|
        auto re = [](const Value& v) { return v.t == VT::Complex ? v.n : v.toNum(); };
        auto im = [](const Value& v) { return v.t == VT::Complex ? v.im : 0.0; };
        double gr = a.size() > 0 ? re(a[0]) : 0, gi = a.size() > 0 ? im(a[0]) : 0;
        double er = a.size() > 1 ? re(a[1]) : 0, ei = a.size() > 1 ? im(a[1]) : 0;
        double tol = 1e-5;
        std::string desc;
        bool haveRel = false, haveAbs = false, havePosTol = false;
        double relTol = 0, absTol = 0;
        // named :rel-tol / :abs-tol arrive as positional Pairs; a bare numeric 3rd
        // arg is the (relative) tolerance, a Str is the description.
        for (size_t i = 2; i < a.size(); i++) {
            if (a[i].t == VT::Pair) {
                std::string k = a[i].s; double val = a[i].pairVal ? a[i].pairVal->toNum() : 0;
                if (k == "rel-tol") { haveRel = true; relTol = val; }
                else if (k == "abs-tol") { haveAbs = true; absTol = val; }
            } else if (a[i].isNumeric() && !havePosTol) { tol = a[i].toNum(); havePosTol = true; }
            else if (a[i].t == VT::Str && desc.empty()) desc = a[i].toStr();
        }
        double diff = std::hypot(gr - er, gi - ei);
        double gm = std::hypot(gr, gi), em = std::hypot(er, ei);
        bool c;
        if (haveRel || haveAbs) {
            c = true;
            if (haveAbs) c = c && (diff <= absTol);
            if (haveRel) { double mx = std::max(gm, em); c = c && (mx == 0 ? true : diff / mx <= relTol); }
        } else {
            double scale = std::max({gm, em, 1.0});
            c = diff <= tol * scale;
        }
        I.emitTest(c, desc);
        return Value::boolean(c);
    };
    B["throws-like"] = [](Interpreter& I, ValueList& a) -> Value {
        bool threw = false;
        if (!a.empty()) {
            try {
                if (a[0].t == VT::Code) I.callCallable(a[0], {});
                else if (a[0].t == VT::Str) I.evalString(a[0].s, /*mainlinePH=*/true);
            } catch (RakuError&) { threw = true; }
        }
        std::string desc = a.size() > 2 ? a[2].toStr() : (a.size() > 1 && a[1].t == VT::Str ? a[1].toStr() : "");
        I.emitTest(threw, desc);
        return Value::boolean(threw);
    };
    B["fails-like"] = [](Interpreter& I, ValueList& a) -> Value {
        // Like throws-like, but the code is expected to RETURN a Failure (a soft
        // `fail`) rather than throw outright. Either a returned Failure or a thrown
        // error counts as failing. (The exception-type / matcher args are accepted
        // but, as with throws-like, not deeply checked.)
        bool failed = false;
        // fails-like BLOCK, TYPE, matchers…, desc? — passes only when the block
        // RETURNS an UNHANDLED Failure whose exception matches TYPE and every
        // named matcher. A thrown exception is NOT a pass (that's throws-like);
        // neither is a Failure the block already handled (.so / .Bool).
        std::string desc;
        std::vector<Value> matchers;
        for (size_t i = 2; i < a.size(); i++) {
            if (a[i].t == VT::Pair && a[i].namedArg) matchers.push_back(a[i]);
            else if (a[i].t == VT::Str && desc.empty()) desc = a[i].s;
        }
        if (!a.empty()) {
            try {
                Value r;
                if (a[0].t == VT::Code) r = I.callCallable(a[0], {});
                else if (a[0].t == VT::Str) r = I.evalString(a[0].s, /*mainlinePH=*/true);
                if (r.t == VT::Hash && r.hashKind == "Failure" &&
                    !(r.hash->count("handled") && (*r.hash)["handled"].truthy())) {
                    Value ex = r.hash->count("exception") ? (*r.hash)["exception"] : Value::any();
                    failed = true;
                    if (a.size() > 1 && a[1].t == VT::Type && a[1].s != "Exception")
                        failed = applyArith("~~", ex, a[1]).truthy();
                    for (auto& mp : matchers) {
                        if (!failed) break;
                        Value want = mp.pairVal ? *mp.pairVal : Value::boolean(true);
                        if (want.t == VT::Bool)
                            throw RakuError{Value::typeObj("X::Match::Bool"),
                                "Cannot use Bool as matcher for '" + mp.s + "'; did you mean to smartmatch the attribute?"};
                        Value got = I.methodCall(ex, mp.s, ValueList{});
                        failed = want.t == VT::Code ? I.callCallable(want, ValueList{got}).truthy()
                                                    : applyArith("~~", got, want).truthy();
                    }
                }
            } catch (RakuError& e) {
                if (e.payload.t == VT::Type && e.payload.s == "X::Match::Bool") throw; // matcher misuse propagates
                failed = false; // thrown exception: fails-like does not pass
            }
        }
        I.emitTest(failed, desc);
        return Value::boolean(failed);
    };
    B["eval-lives-ok"] = [](Interpreter& I, ValueList& a) -> Value {
        bool lived = true;
        try { if (!a.empty()) I.evalString(a[0].toStr(), /*mainlinePH=*/true); } catch (RakuError&) { lived = false; }
        I.emitTest(lived, a.size() > 1 ? a[1].toStr() : "");
        return Value::boolean(lived);
    };
    B["eval-dies-ok"] = [](Interpreter& I, ValueList& a) -> Value {
        bool died = false;
        try { if (!a.empty()) I.evalString(a[0].toStr(), /*mainlinePH=*/true); } catch (RakuError&) { died = true; }
        I.emitTest(died, a.size() > 1 ? a[1].toStr() : "");
        return Value::boolean(died);
    };
    B["EVAL"] = [](Interpreter& I, ValueList& a) -> Value {
        Value code; bool haveCode = false;
        for (auto& v : a) {
            if (v.t == VT::Pair && v.s == "lang") {
                std::string lang = v.pairVal ? v.pairVal->toStr() : "";
                if (lang != "Raku" && lang != "Perl6")
                    // the payload slot takes the exception TYPE, not a message —
                    // a Str payload only promotes to a real exception when it
                    // starts with "X::", so this used to surface as a bare Str
                    I.throwTypedV("X::Eval::NoSuchLang", {{"lang", Value::str(lang)}},
                                  "No compiler available for language '" + lang + "'");
            } else if (v.t != VT::Pair && !haveCode) { code = v; haveCode = true; }
        }
        if (!haveCode) return Value::any();
        // control flow may not escape an EVAL: a top-level `return`/`next`/… in
        // the string is X::ControlFlow, not a silent unwind of the whole program
        // evalString itself converts escaping control flow (routine-aware)
        return I.evalString(code.toStr(), /*mainlinePH=*/true);
    };
    // Default @*ARGS -> argument-list conversion (the built-in ARGS-TO-CAPTURE).
    // main-refactored.t adjudicates the rules; see rakuppMainCapture below.
    B["RUN-MAIN-args-to-capture"] = [](Interpreter& I, ValueList& a) -> Value {
        // The DEFAULT ARGS-TO-CAPTURE: parse the live @*ARGS per the CLI rules
        // main-refactored.t adjudicates: --name / --name=v / -n=v are named
        // (repeats collect into an array; values are val()-allomorphed;
        // `--` ends option parsing; --/name negates). In the DEFAULT mode an
        // option after the first positional is a plain positional; with
        // %*SUB-MAIN-OPTS<named-anywhere> options bind anywhere.
        (void)a;
        auto dynFind = [&](const char* n) -> Value* {
            if (Value* p = I.tctx_.cur->find(n)) return p;
            for (auto it = I.tctx_.dynStack.rbegin(); it != I.tctx_.dynStack.rend(); ++it)
                if (*it) if (Value* p = (*it)->find(n)) return p;
            return nullptr;
        };
        std::vector<std::string> argv;
        if (Value* av = dynFind("@*ARGS"))
            if (av->t == VT::Array && av->arr)
                for (auto& x : *av->arr) argv.push_back(x.toStr());
        bool namedAnywhere = false;
        if (Value* smo = dynFind("%*SUB-MAIN-OPTS"))
            if (smo->t == VT::Hash && smo->hash) {
                auto it = smo->hash->find("named-anywhere");
                namedAnywhere = it != smo->hash->end() && it->second.truthy();
            }
        auto valify = [&](const std::string& s) -> Value {
            auto it = I.builtins_.find("val");
            if (it != I.builtins_.end()) { ValueList va{Value::str(s)}; return it->second(I, va); }
            return Value::str(s);
        };
        ValueList pos;
        std::vector<std::pair<std::string, ValueList>> named; // insertion order
        bool noMoreNamed = false;
        for (auto& s : argv) {
            if (!noMoreNamed && s == "--") { noMoreNamed = true; continue; }
            bool isOpt = !noMoreNamed && s.size() > 1 && s[0] == '-' &&
                         !(s.size() == 2 && s[1] == '-');
            if (isOpt) {
                std::string body = s[1] == '-' ? s.substr(2) : s.substr(1);
                bool neg = !body.empty() && body[0] == '/';
                if (neg) body = body.substr(1);
                auto eq = body.find('=');
                std::string k = eq == std::string::npos ? body : body.substr(0, eq);
                Value v = eq == std::string::npos ? Value::boolean(!neg)
                                                  : valify(body.substr(eq + 1));
                auto slot = std::find_if(named.begin(), named.end(),
                                         [&](auto& kv) { return kv.first == k; });
                if (slot == named.end()) named.push_back({k, {v}});
                else slot->second.push_back(v);
                continue;
            }
            pos.push_back(Value::str(s));
            if (!namedAnywhere) noMoreNamed = true;
        }
        ValueList margs = std::move(pos);
        for (auto& kv : named) {
            Value v;
            if (kv.second.size() == 1) v = kv.second[0];
            else { v = Value::array(); *v.arr = kv.second; }
            Value p = Value::pair(kv.first, v);
            p.namedArg = true;
            margs.push_back(std::move(p));
        }
        Value cap = Value::array(); cap.hashKind = "Capture"; *cap.arr = std::move(margs);
        return cap;
    };
    // RUN-MAIN(&main, $mainline-result) — the 2018.10 command-line protocol:
    //   @*ARGS -> ARGS-TO-CAPTURE -> dispatch &main -> on failure GENERATE-USAGE
    //   (or the legacy USAGE), then exit through &*EXIT.
    // A user sub of any of those names, visible where RUN-MAIN is called,
    // REPLACES the built-in step, and is also what &*ARGS-TO-CAPTURE /
    // &*GENERATE-USAGE answer inside it. MAIN_HELPER is never called.
    B["RUN-MAIN"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.empty() || a[0].t != VT::Code) return Value::any();
        Value mainSub = a[0];
        auto lookup = [&](const char* n) -> Value* {
            if (Value* p = I.tctx_.cur->find(n)) return p;
            for (auto it = I.tctx_.dynStack.rbegin(); it != I.tctx_.dynStack.rend(); ++it)
                if (*it) if (Value* p = (*it)->find(n)) return p;
            return nullptr;
        };
        // the raw @*ARGS as an Array, for the ARGS-TO-CAPTURE hook
        Value argsArr = Value::array();
        if (Value* av = lookup("@*ARGS"))
            if (av->t == VT::Array && av->arr) *argsArr.arr = *av->arr;

        // --- 1. args -> capture -------------------------------------------
        Value userA2C;
        if (Value* p = lookup("&*ARGS-TO-CAPTURE")) userA2C = *p;
        if (userA2C.t != VT::Code) if (Value* p = lookup("&ARGS-TO-CAPTURE")) userA2C = *p;
        ValueList margs;
        {   // &*ARGS-TO-CAPTURE is the sub actually in force while it runs
            if (userA2C.t == VT::Code) {
                // &*ARGS-TO-CAPTURE names the sub actually in force while it runs
                auto scope = std::make_shared<Env>(); scope->parent = I.tctx_.cur;
                scope->define("&*ARGS-TO-CAPTURE", userA2C);
                auto saved = I.tctx_.cur; I.tctx_.cur = scope;
                I.tctx_.dynStack.push_back(scope.get());
                struct R { Interpreter& I; std::shared_ptr<Env> s; ~R(){ I.tctx_.cur = s; I.tctx_.dynStack.pop_back(); } } r{I, saved};
                Value cap = I.callCallable(userA2C, ValueList{mainSub, argsArr});
                if (cap.t == VT::Array && cap.arr) {
                    margs = *cap.arr;                       // a Capture IS the arg list…
                    for (auto& m : margs)                   // …its Pairs are NAMED args
                        if (m.t == VT::Pair) m.namedArg = true;
                } else if (cap.t != VT::Any && cap.t != VT::Nil) margs.push_back(cap);
            } else {
                Value cap = I.callBuiltin("RUN-MAIN-args-to-capture", ValueList{});
                if (cap.t == VT::Array && cap.arr) margs = *cap.arr;
            }
        }
        // did the user ask for help? (drives the exit code)
        bool wantsHelp = false;
        for (auto& m : margs) if (m.t == VT::Pair && m.s == "help" && m.namedArg &&
                                  (!m.pairVal || m.pairVal->truthy())) wantsHelp = true;

        // --- 2. dispatch --------------------------------------------------
        bool matches = true;
        if (mainSub.code && mainSub.code->isMultiDispatcher) {
            matches = false;
            for (auto& cand : mainSub.code->candidates)
                if (I.scoreCandidate(cand, margs) >= 0) { matches = true; break; }
        } else if (mainSub.code && mainSub.code->params) {
            matches = I.scoreCandidate(mainSub, margs) >= 0;
        }
        if (matches) return I.callCallable(mainSub, margs);

        // --- 3. no candidate: usage ---------------------------------------
        Value userGU;
        if (Value* p = lookup("&*GENERATE-USAGE")) userGU = *p;
        if (userGU.t != VT::Code) if (Value* p = lookup("&GENERATE-USAGE")) userGU = *p;
        Value usageText;
        if (userGU.t == VT::Code) {
            Value inForce = userGU;
            auto scope = std::make_shared<Env>(); scope->parent = I.tctx_.cur;
            scope->define("&*GENERATE-USAGE", inForce);
            auto saved = I.tctx_.cur; I.tctx_.cur = scope;
            I.tctx_.dynStack.push_back(scope.get());
            struct R { Interpreter& I; std::shared_ptr<Env> s; ~R(){ I.tctx_.cur = s; I.tctx_.dynStack.pop_back(); } } r{I, saved};
            ValueList ga{mainSub};
            for (auto& m : margs) ga.push_back(m);
            usageText = I.callCallable(userGU, ga);
        } else if (Value* p = lookup("&USAGE"); p && p->t == VT::Code) {
            I.callCallable(*p, ValueList{});   // the legacy hook PRINTS; it returns nothing useful
        } else {
            usageText = Value::str(I.mainUsage());
        }
        if (usageText.t == VT::Str && !usageText.s.empty())
            (wantsHelp ? std::cout : std::cerr) << usageText.s << "\n";

        // --- 4. exit through &*EXIT (so a test can intercept) --------------
        Value code = Value::integer(wantsHelp ? 0 : 2);
        if (Value* e = lookup("&*EXIT"); e && e->t == VT::Code)
            return I.callCallable(*e, ValueList{code});
        throw ExitEx{(int)code.toInt()};
    };
    B["samemark"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.size() < 2) return a.empty() ? Value::any() : a[0];
        ValueList rest(a.begin() + 1, a.end());
        return I.methodCall(a[0], "samemark", rest);
    };
    B["exit"] = [](Interpreter& I, ValueList& a) -> Value {
        int code = (int)(a.empty() ? 0 : a[0].toInt());
        // `exit` ends the PROCESS, wherever it is called from. Thrown on a worker
        // thread the ExitEx would only break that thread's Promise, leaving the
        // main thread to run on — `start { sleep 3; exit }` has to stop everything.
        if (std::this_thread::get_id() != I.mainThreadId()) {
            std::cout.flush(); std::cerr.flush();
            _exit(code);
        }
        throw ExitEx{code};
    };
    // stub / yada operators
    B["!!!"] = [](Interpreter&, ValueList& a) -> Value { throw RakuError{Value::typeObj("X::StubCode"), a.empty() ? "Stub code executed" : a[0].toStr()}; };
    B["..."] = [](Interpreter&, ValueList& a) -> Value { throw RakuError{Value::typeObj("X::StubCode"), a.empty() ? "Stub code executed" : a[0].toStr()}; };
    B["???"] = [](Interpreter&, ValueList& a) -> Value { std::cerr << (a.empty() ? "Stub code executed" : a[0].toStr()) << "\n"; return Value::nil(); };
    // run(prog, *@args, :timeout(N)) -> { out => Str, exitcode => Int, timedout => Bool }
    B["run"] = [](Interpreter& I, ValueList& a) -> Value {
        std::vector<std::string> argv; bool wantOut = false, wantIn = false, wantErr = false;
        int outMode = -1, errMode = -1; // -1 unspecified (inherit/echo), 0 :!x (discard), 1 :x (capture)
        std::vector<std::string> envKV; bool haveEnv = false; std::string cwd;
        for (auto& v : flattenArgs(a)) {
            if (v.t == VT::Pair) {
                if (v.s == "out") { wantOut = v.pairVal ? v.pairVal->truthy() : true; outMode = wantOut ? 1 : 0; }
                else if (v.s == "err") { wantErr = v.pairVal ? v.pairVal->truthy() : true; errMode = wantErr ? 1 : 0; }
                else if (v.s == "in") wantIn = v.pairVal ? v.pairVal->truthy() : true;
                else if (v.s == "env" && v.pairVal && v.pairVal->t == VT::Hash && v.pairVal->hash) {
                    // :env(%h) — the child's ENTIRE environment (Rakudo semantics).
                    // Silently ignored before: run(..., :env(%(%*ENV, RAKULIB =>
                    // ...))) inherited the parent env unchanged.
                    haveEnv = true;
                    for (auto& kv : *v.pairVal->hash) envKV.push_back(kv.first + "=" + kv.second.toStr());
                    std::sort(envKV.begin(), envKV.end()); // deterministic; Windows wants sorted blocks
                }
                else if (v.s == "cwd" && v.pairVal) cwd = v.pairVal->toStr(); // was silently ignored too
            }
            else argv.push_back(v.toStr());
        }
        Value av = Value::array(); av.isList = true; for (auto& s : argv) av.arr->push_back(Value::str(s));
        Value p = Value::makeHash(); p.hashKind = "Proc"; // standard Proc object
        (*p.hash)["argv"] = av; // for .command
        I.syncEnvToProcess(); // child inherits any %*ENV changes the program made
        if (wantIn) {
            // Defer spawning: the process runs when its stdin is written via
            // `.in.spurt(...)`, so we can feed input and capture output together.
            (*p.hash)["deferred"] = Value::boolean(true);
            if (haveEnv) { Value ev = Value::array(); for (auto& kv : envKV) ev.arr->push_back(Value::str(kv)); (*p.hash)["env-kv"] = ev; }
            if (!cwd.empty()) (*p.hash)["cwd"] = Value::str(cwd);
            (*p.hash)["out-str"] = Value::str("");
            (*p.hash)["err-str"] = Value::str("");
            (*p.hash)["exitcode"] = Value::integer(0);
            return p;
        }
        std::string out, err; int code; bool timedout;
        // :err captures; :!err captures-and-discards (so probes like
        // `zrun('git','--help', :!out, :!err)` stay silent); unspecified inherits.
        long long childPid = 0;
        spawnCapture(argv, 0, out, code, timedout, &I, errMode != -1 ? &err : nullptr, cwd, &childPid,
                     haveEnv ? &envKV : nullptr);
        if (outMode == -1) std::cout << out; // not capturing: echo child stdout (approximates inherit)
        if (errMode == -1) { /* stderr already inherited by the child */ }
        (*p.hash)["exitcode"] = Value::integer(code);
        (*p.hash)["out-str"] = Value::str(out);
        (*p.hash)["err-str"] = Value::str(err);
        if (childPid) (*p.hash)["pid"] = Value::integer(childPid);
        return p;
    };
    // shell(CMD, :out, :err) — run CMD through the system shell (`/bin/sh -c CMD`),
    // so redirections/pipes in CMD work. Returns a Proc; +$proc is the exit status.
    B["shell"] = [](Interpreter& I, ValueList& a) -> Value {
        std::string cmd; bool wantOut = false, wantErr = false;
        int outMode = -1, errMode = -1; // -1 unspecified, 0 :!x discard, 1 :x capture
        for (auto& v : flattenArgs(a)) {
            if (v.t == VT::Pair) {
                if (v.s == "out") { wantOut = v.pairVal ? v.pairVal->truthy() : true; outMode = wantOut ? 1 : 0; }
                else if (v.s == "err") { wantErr = v.pairVal ? v.pairVal->truthy() : true; errMode = wantErr ? 1 : 0; }
            }
            else if (cmd.empty()) cmd = v.toStr();
        }
        // `shell` runs its argument through the SYSTEM command processor, which is
        // cmd.exe on Windows — `/bin/sh` does not exist there, so every shell()
        // call failed with exitcode -1 and no output (issue #10).
#if defined(_WIN32)
        const char* comspec = std::getenv("COMSPEC");
        std::vector<std::string> argv = {comspec && *comspec ? comspec : "cmd.exe", "/c", cmd};
#else
        std::vector<std::string> argv = {"/bin/sh", "-c", cmd};
#endif
        I.syncEnvToProcess(); // child inherits any %*ENV changes the program made
        std::string out, err; int code = 0; bool timedout = false;
        long long childPid = 0;
        spawnCapture(argv, 0, out, code, timedout, &I, errMode != -1 ? &err : nullptr, "", &childPid);
        if (outMode == -1) std::cout << out;
        Value p = Value::makeHash(); p.hashKind = "Proc";
        Value av = Value::array(); av.isList = true; av.arr->push_back(Value::str(cmd));
        (*p.hash)["argv"] = av; // .command — shell reports the command string
        (*p.hash)["exitcode"] = Value::integer(code);
        (*p.hash)["out-str"] = Value::str(out);
        (*p.hash)["err-str"] = Value::str(err);
        if (childPid) (*p.hash)["pid"] = Value::integer(childPid);
        return p;
    };
    B["make"] = [](Interpreter& I, ValueList& a) -> Value {
        Value v = a.empty() ? Value::any() : (a.size() == 1 ? a[0] : Value::array(a));
        if (!I.tctx_.makeTargets.empty()) I.tctx_.makeTargets.back()->pairVal = std::make_shared<Value>(v);
        return v;
    };
    B["take"] = [](Interpreter& I, ValueList& a) -> Value {
        Value v = a.size() == 1 ? a[0] : Value::array(a);
        if (!I.tctx_.gatherStack.empty()) {
            auto& coll = *I.tctx_.gatherStack.back();
            for (auto& x : a) coll.push_back(x);
            // a lazy gather stops the block once it has produced enough elements
            size_t lim = I.tctx_.gatherLimits.empty() ? 0 : I.tctx_.gatherLimits.back();
            if (lim && coll.size() >= lim) throw StopGatherEx{};
        }
        return v;
    };
    // `succeed EXPR` exits the enclosing `when`/`given`, making the given evaluate to EXPR;
    // `proceed` leaves the current `when` but keeps testing later ones.
    B["succeed"] = [](Interpreter&, ValueList& a) -> Value {
        Value v = a.empty() ? Value::any() : (a.size() == 1 ? a[0] : Value::array(a));
        throw BreakGivenEx{v, !a.empty()};
    };
    B["proceed"] = [](Interpreter&, ValueList&) -> Value { throw ProceedEx{}; };
    B["dir"] = [](Interpreter& I, ValueList& a) -> Value {
        std::string path = a.empty() ? "." : a[0].toStr();
        // a `:test` matcher filters basenames (dir("x", test => /\.raku$/))
        Value test; bool haveTest = false;
        for (auto& x : a) if (x.t == VT::Pair && x.s == "test" && x.pairVal) { test = *x.pairVal; haveTest = true; }
        std::string base = path;
        while (base.size() > 1 && base.back() == '/') base.pop_back();
        Value out = Value::array();
        if (DIR* d = opendir(path.c_str())) {
            while (struct dirent* e = readdir(d)) {
                std::string n = e->d_name;
                if (n == "." || n == "..") continue;
                if (haveTest) { ValueList m{Value::str(n)}; if (!I.methodCall(test, "ACCEPTS", m).truthy()) continue; }
                // dir() yields IO::Path entries (Rakudo semantics) — File::Find,
                // and any `.d`/`.IO` on the result, need real IO::Path objects.
                Value p = Value::str(base + "/" + n); p.hashKind = "IO";
                out.arr->push_back(p);
            }
            closedir(d);
        }
        return out;
    };
    B["mkdir"] = [](Interpreter&, ValueList& a) -> Value {
        if (a.empty()) return Value::boolean(false);
        std::string path = a[0].toStr();
        // mkdir -p: create parent dirs as needed
        std::string acc;
        for (size_t i = 0; i <= path.size(); i++) {
            if (i == path.size() || path[i] == '/') {
                if (!acc.empty()) ::mkdir(acc.c_str(), 0777);
                if (i < path.size()) acc += '/';
            } else acc += path[i];
        }
        return Value::str(path);
    };
    B["rmdir"] = [](Interpreter&, ValueList& a) -> Value {
        if (a.empty()) return Value::boolean(false);
        return Value::boolean(::rmdir(a[0].toStr().c_str()) == 0);
    };
    B["spurt"] = [](Interpreter&, ValueList& a) -> Value {
        if (!a.empty()) rejectNulPath(a[0].toStr());
        if (a.empty()) return Value::boolean(false);
        bool append = false, createonly = false;
        std::string content;
        bool haveContent = false;
        for (size_t i = 1; i < a.size(); i++) {
            if (a[i].t == VT::Pair && a[i].namedArg) {
                if (a[i].s == "append") append = a[i].pairVal && a[i].pairVal->truthy();
                else if (a[i].s == "createonly" || a[i].s == "x") createonly = a[i].pairVal && a[i].pairVal->truthy();
            }
            else if (!haveContent) { content = a[i].toStr(); haveContent = true; }
        }
        std::string path = a[0].toStr();
        if (createonly) { std::ifstream probe(path); if (probe) return Value::boolean(false); }
        std::ofstream out(path, append ? (std::ios::out | std::ios::app) : std::ios::out);
        if (!out) return Value::boolean(false);
        out << content;
        return Value::boolean(true);
    };
    B["slurp"] = [](Interpreter&, ValueList& a) -> Value {
        if (!a.empty()) rejectNulPath(a[0].toStr());
        if (a.empty()) { std::ostringstream ss; ss << std::cin.rdbuf(); return Value::str(ss.str()); } // slurp() = $*IN.slurp
        std::ifstream in(a[0].toStr());
        if (!in) throwFailedOpen(a[0].toStr());
        std::ostringstream ss; ss << in.rdbuf();
        return Value::str(ss.str());
    };
    // lines() / get() / words() with no arg read from $*ARGFILES: the files named
    // in @*ARGS (awk/perl -n style), or standard input when there are none.
    B["lines"] = [](Interpreter& I, ValueList& a) -> Value {
        Value out = Value::array(); out.isList = true; out.s = "Seq";
        std::string line;
        // the STRING to split may sit behind a named argument
        // (`lines(:!chomp, "a\nb")`), so look past the adverbs for it
        // …and it is the first POSITIONAL whatever its type: `lines` is Cool, so
        // `lines(42)` is "42".lines. Requiring a Str let every other type fall
        // through to the $*IN branch below, which blocks on stdin.
        const Value* src = nullptr; ValueList named;
        for (auto& v : a) {
            if (v.t == VT::Pair && v.namedArg) { named.push_back(v); continue; }
            if (!src) src = &v;
            else named.push_back(v);            // a limit / :count rides along
        }
        if (src) return I.methodCall(*src, "lines", named);
        Value argv = I.getArgs();
        if (argv.arr && !argv.arr->empty()) {
            for (auto& fn : *argv.arr) {
                std::ifstream in(fn.toStr());
                while (std::getline(in, line)) { if (!line.empty() && line.back() == '\r') line.pop_back(); out.arr->push_back(Value::str(line)); }
            }
            return out;
        }
        while (std::getline(std::cin, line)) { if (!line.empty() && line.back() == '\r') line.pop_back(); out.arr->push_back(Value::str(line)); }
        return out;
    };
    B["get"] = [](Interpreter&, ValueList&) -> Value {
        std::string line; if (!std::getline(std::cin, line)) return Value::nil();
        if (!line.empty() && line.back() == '\r') line.pop_back();
        return Value::str(line);
    };
    B["words"] = [](Interpreter& I, ValueList& a) -> Value {
        Value out = Value::array(); out.isList = true; out.s = "Seq";
        std::string all, w;
        // The source is the first POSITIONAL, whatever its type — `words` is a Cool
        // routine, so `words(42)` is "42".words. Selecting it by VALUE TAG left every
        // other type falling through to the `$*IN` branch, which blocks on stdin.
        if (!a.empty() && a[0].t != VT::Pair) {
            ValueList rest(a.begin() + 1, a.end());
            return I.methodCall(a[0], "words", rest);
        }
        else { std::ostringstream ss; ss << std::cin.rdbuf(); all = ss.str(); } // words() = $*IN.words
        std::istringstream ws(all);
        while (ws >> w) out.arr->push_back(Value::str(w));
        return out;
    };
    B["open"] = [](Interpreter& I, ValueList& a) -> Value { // sub form: open($path, :r/:w/:a)
        // the path is the first POSITIONAL — `open :w, $path` puts the adverb first,
        // and taking args[0] blindly opened a file literally named "w\tTrue"
        std::string path;
        for (auto& x : a) if (x.t != VT::Pair) { path = x.toStr(); break; }
        rejectNulPath(path);
        std::string mode = "r";
        for (auto& x : a) if (x.t == VT::Pair) { if (x.s == "w") mode = "w"; else if (x.s == "a") mode = "a"; else if (x.s == "r") mode = "r"; }
        if (mode == "r") { // reading a nonexistent file fails, like Rakudo's X::IO::DoesNotExist
            std::ifstream probe(path);
            if (!probe) throw RakuError{Value::typeObj("X::IO::DoesNotExist"),
                "Failed to open file " + path + ": no such file or directory"};
        }
        Value h = Value::makeHash(); h.hashKind = "FileHandle";
        (*h.hash)["path"] = Value::str(path);
        (*h.hash)["mode"] = Value::str(mode);
        (*h.hash)["buffer"] = Value::str("");
        if (mode == "w") { std::ofstream create(path, std::ios::trunc); } // the file exists immediately
        if (mode == "w" || mode == "a") I.registerWriteHandle(h.hash); // flush at exit if not closed
        return h;
    };
    B["unlink"] = [](Interpreter&, ValueList& a) -> Value {
        for (auto& f : a) ::unlink(f.toStr().c_str());
        return Value::boolean(true);
    };
    B["close"] = [](Interpreter& I, ValueList& a) -> Value { // sub form: close($fh)
        if (a.empty()) return Value::boolean(true);
        return I.methodCall(a[0], "close", {});
    };
    B["getc"] = [](Interpreter& I, ValueList& a) -> Value { // sub form: getc($fh)
        if (a.empty()) return Value::nil();
        return I.methodCall(a[0], "getc", {});
    };
    B["chmod"] = [](Interpreter&, ValueList& a) -> Value { // chmod MODE, @paths → the paths changed
        Value out = Value::array(); out.isList = true;
        if (a.empty()) return out;
        // a permission string like IO.mode's "0777" is octal; an Int (0o644) is itself
        mode_t mode = a[0].t == VT::Str ? (mode_t)strtol(a[0].s.c_str(), nullptr, 8)
                                        : (mode_t)a[0].toInt();
        for (size_t k = 1; k < a.size(); k++) {
            std::string p = a[k].toStr();
            if (::chmod(p.c_str(), mode) == 0) out.arr->push_back(a[k]);
        }
        return out;
    };
    B["skip-rest"] = [](Interpreter& I, ValueList& a) -> Value {
        std::string reason = a.empty() ? "" : a[0].toStr();
        long remaining = (I.planned_ > 0 ? I.planned_ : 0) - I.testNum_;
        for (long k = 0; k < remaining; k++) I.emitTest(true, "", "skip " + reason);
        return Value::boolean(true);
    };
    B["subtest"] = [](Interpreter& I, ValueList& a) -> Value {
        Value code; std::string desc;
        // NB: the Pair form `subtest "title" => {…}` is deliberately NOT unpacked
        // yet — running those bodies exposes unimplemented features across ~23
        // roast files (is rw on a class, Mu.iterator, Rational subclassing, …).
        // Tracked as the subtest-Pair-form batch; unlock once those gaps close.
        for (auto& v : a) { if (v.t == VT::Code) code = v; else if (v.t == VT::Str) desc = v.s; }
        // A pending `todo` marks this whole subtest TODO: inner failures neither die nor count.
        bool todod = false; std::string todoReason;
        if (I.todoRemaining_ > 0) { todod = true; todoReason = I.todoReason_; I.todoRemaining_--; }
        bool savedFailed = I.subtestFailed_;
        int savedPlanned = I.planned_, savedTestNum = I.testNum_; // a subtest has its own plan + numbering
        long savedFailCount = I.failCount_;
        I.subtestDepth_++;
        if (todod) I.todoSubtestDepth_++;
        I.subtestFailed_ = false;
        I.planned_ = -1; I.testNum_ = 0;
        if (code.t == VT::Code) { try { I.callCallable(code, {}); } catch (RakuError&) { I.subtestFailed_ = true; } }
        bool ok = !I.subtestFailed_;
        if (todod) I.todoSubtestDepth_--;
        I.subtestDepth_--;
        I.subtestFailed_ = savedFailed;
        I.planned_ = savedPlanned; I.testNum_ = savedTestNum; I.failCount_ = savedFailCount;
        I.emitTest(ok, desc, todod ? ("TODO" + (todoReason.empty() ? "" : " " + todoReason)) : "");
        return Value::boolean(ok);
    };
    B["done-testing"] = [](Interpreter& I, ValueList&) -> Value {
        if (I.planned_ < 0) { std::cout << std::string(4 * I.subtestDepth_, ' ') << "1.." << I.testNum_ << "\n"; I.planned_ = I.testNum_; }
        // True only if every test passed and the ran count matched the plan.
        return Value::boolean(I.failCount_ == 0 && I.planned_ == I.testNum_);
    };
    B["done_testing"] = B["done-testing"];
    // bail_out(reason?) — emit "Bail out!" and stop the whole test run immediately.
    B["bail-out"] = [](Interpreter& I, ValueList& a) -> Value {
        std::string reason;
        for (auto& v : a) if (v.t != VT::Pair) { reason = v.toStr(); break; }
        std::cout << "Bail out!" << (reason.empty() ? "" : " " + reason) << "\n" << std::flush;
        I.bailedOut_ = true;
        throw ExitEx{255};
    };
    B["bail_out"] = B["bail-out"];

    // --- utility functions ---
    B["abs"] = [](Interpreter& I, ValueList& a) -> Value { return rtBAbs(I, a.empty() ? Value::any() : a[0]); };
    // Comparison operators usable as subs: `cmp($a,$b)`, `$a leg $b`, etc.
    for (auto op : {"cmp", "leg", "before", "after"})
        B[op] = [op](Interpreter&, ValueList& a) -> Value { return a.size() >= 2 ? applyArith(op, a[0], a[1]) : Value::any(); };
    // List routines that delegate to the method of the same name. An ADVERB the
    // routine understands is forwarded as a method argument rather than swept into
    // the list — `unique @a, as => {.abs}` was uniquing the adverb along with the
    // elements. The names are an allow-list so a genuine Pair ELEMENT still counts
    // as data (`unique (a => 1), (a => 1)`).
    // `rotor(CYCLE…, LIST)` — the 6.e sub form takes the cycle FIRST and the
    // iterable LAST, the opposite way round from the method. Sweeping every
    // positional into the list made `rotor(3, 'a'..'h')` rotor the cycle too.
    B["rotor"] = [](Interpreter& I, ValueList& a) -> Value {
        ValueList pos, opts;
        for (auto& x : a) {
            if (x.t == VT::Pair && x.namedArg) opts.push_back(x); // `:partial` may come last
            else pos.push_back(x);
        }
        if (pos.empty()) return Value::array();
        Value list = pos.back();                       // the LAST positional is the iterable
        ValueList cycle(pos.begin(), pos.end() - 1);   // everything before it is the cycle
        for (auto& o : opts) cycle.push_back(o);
        return I.methodCall(list, "rotor", cycle);
    };
    for (auto nm : {"permutations", "combinations", "unique", "repeated", "squish", "flat"})
        B[nm] = [nm](Interpreter& I, ValueList& a) -> Value {
            static const std::set<std::string> adv = {"as", "with", "partial"};
            ValueList items, opts;
            for (auto& x : a) {
                if (x.t == VT::Pair && adv.count(x.s)) opts.push_back(x);
                else items.push_back(x);
            }
            Value v = items.size() == 1 ? items[0] : Value::array(items);
            return I.methodCall(v, nm, opts);
        };
    // Same-named-method routines that forward the REMAINING args, invocant first:
    // rotate(@a,$n), substr($s,$f,$c), head(@a,$n), trim($s), samecase($s,$pat), …
    for (auto nm : {"rotate", "head", "tail", "substr", "substr-rw", "trim", "trim-leading",
                    "trim-trailing", "flip", "tc", "tclc", "wordcase", "pairs", "antipairs", "chop",
                    "samecase", "samemark", "chomp"})
        if (!B.count(nm)) B[nm] = [nm](Interpreter& I, ValueList& a) -> Value {
            if (a.empty()) return Value::nil();
            Value inv = a[0]; ValueList rest(a.begin() + 1, a.end());
            return I.methodCall(inv, nm, rest);
        };
    // head/tail sub forms take the count FIRST: head(5, @list) → @list.head(5);
    // the one-arg form is the method with no count (first/last element).
    for (auto nm : {"head", "tail"})
        B[nm] = [nm](Interpreter& I, ValueList& a) -> Value {
            if (a.empty()) return Value::nil();
            if (a.size() == 1) { ValueList none; return I.methodCall(a[0], nm, none); }
            Value n = a[0];
            Value list = a.size() == 2 ? a[1] : Value::array(ValueList(a.begin() + 1, a.end()));
            ValueList ma{n}; return I.methodCall(list, nm, ma);
        };
    // pick/roll sub forms take the count FIRST: pick(3, @list) → @list.pick(3).
    for (auto nm : {"pick", "roll"})
        B[nm] = [nm](Interpreter& I, ValueList& a) -> Value {
            if (a.empty()) return Value::any();
            Value n = a[0];
            Value list = a.size() == 2 ? a[1] : Value::array(ValueList(a.begin() + 1, a.end()));
            ValueList ma{n}; return I.methodCall(list, nm, ma);
        };
    B["srand"] = [](Interpreter&, ValueList& a) -> Value { // reseed the RNG; returns the seed
        long long seed = a.empty() ? (long long)::time(nullptr) : a[0].toInt();
        srandSeed(seed);
        return Value::integer(seed);
    };
    // `reduce &f, LIST` / `produce &f, LIST` delegate to the METHOD of the same
    // name — that is where the operator's associativity is read off the
    // callable's name, so `produce &[**], (2,3,4)` folds right like the method.
    for (const char* rf : {"reduce", "produce"}) {
        std::string rname = rf;
        B[rname] = [rname](Interpreter& I, ValueList& a) -> Value {
            if (a.empty()) return rname == "reduce" ? Value::any()
                                                    : [] { Value o = Value::array(); o.isList = true; return o; }();
            Value f = a[0];
            ValueList items;
            for (size_t i = 1; i < a.size(); i++)
                for (auto& x : a[i].flatten()) items.push_back(x);
            Value list = Value::array(items); list.isList = true;
            ValueList ma{f};
            return I.methodCall(list, rname, ma);
        };
    }
    B["cis"] = [](Interpreter&, ValueList& a) -> Value {
        double x = a.empty() ? 0.0 : a[0].toNum();
        return Value::complex(std::cos(x), std::sin(x)); // e^(ix)
    };
    B["unpolar"] = [](Interpreter&, ValueList& a) -> Value { // Complex from (magnitude, angle)
        double r = a.empty() ? 0.0 : a[0].toNum();
        double th = a.size() > 1 ? a[1].toNum() : 0.0;
        return Value::complex(r * std::cos(th), r * std::sin(th));
    };
    B["sqrt"] = [](Interpreter& I, ValueList& a) -> Value { return rtBSqrt(I, a.empty() ? Value::integer(0) : a[0]); };
    B["roots"] = [](Interpreter& I, ValueList& a) -> Value {
        // roots($x, $n): the $n n-th complex roots of $x (principal first,
        // stepping by 2π/n). n < 1 or NaN input yields a single NaN.
        Value out = Value::array(); out.isList = true;
        double re, im;
        Value x = a.empty() ? Value::integer(0) : a[0];
        if (x.t == VT::Complex) { re = x.n; im = x.im; }
        else { re = x.toNum(); im = 0.0; }
        long long n = a.size() > 1 ? a[1].toInt() : 1;
        if (n < 1 || std::isnan(re) || std::isnan(im)) {
            out.arr->push_back(Value::complex(std::nan(""), std::nan("")));
            return out;
        }
        double mag = std::pow(std::hypot(re, im), 1.0 / (double)n);
        double ang = std::atan2(im, re) / (double)n;
        for (long long k = 0; k < n; k++) {
            double th = ang + 2.0 * M_PI * (double)k / (double)n;
            out.arr->push_back(Value::complex(mag * std::cos(th), mag * std::sin(th)));
        }
        return out;
    };
    B["floor"] = [](Interpreter& I, ValueList& a) -> Value { return rtBFloor(I, a.empty() ? Value::integer(0) : a[0]); };
    B["ceiling"] = [](Interpreter& I, ValueList& a) -> Value { return rtBCeiling(I, a.empty() ? Value::integer(0) : a[0]); };
    B["round"] = [](Interpreter& I, ValueList& a) -> Value { // delegate so a scale arg (round($x, 0.1)) and NaN/Inf are honoured
        if (a.empty()) return Value::integer(0);
        ValueList rest(a.begin() + 1, a.end());
        return I.methodCall(a[0], "round", rest);
    };
    B["truncate"] = [](Interpreter& I, ValueList& a) -> Value { return rtBTruncate(I, a.empty() ? Value::integer(0) : a[0]); };
    B["exp"] = [](Interpreter& I, ValueList& a) -> Value {
        if (!a.empty() && (a[0].t == VT::Complex || a[0].t == VT::Object)) { ValueList none; return I.methodCall(a[0], "exp", none); }
        if (a.size() >= 2) return Value::number(std::pow(a[1].toNum(), a[0].toNum())); // exp($x,$base)
        return rtBExp(I, a.empty() ? Value::integer(0) : a[0]); };
    // Trigonometry (radians). Also available as methods below.
    {
        struct TF { const char* name; double (*fn)(double); };
        static const TF tfs[] = {
            {"sin", std::sin}, {"cos", std::cos}, {"tan", std::tan},
            {"asin", std::asin}, {"acos", std::acos}, {"atan", std::atan},
            {"sinh", std::sinh}, {"cosh", std::cosh}, {"tanh", std::tanh},
            {"asinh", std::asinh}, {"acosh", std::acosh}, {"atanh", std::atanh},
        };
        for (auto& tf : tfs) {
            auto f = tf.fn; std::string name = tf.name;
            B[tf.name] = [f, name](Interpreter& I, ValueList& a) -> Value {
                return rtBMath1(I, a.empty() ? Value::integer(0) : a[0], name.c_str(), f);
            };
        }
        B["sec"] = [](Interpreter& I, ValueList& a) -> Value {
            if (!a.empty() && (a[0].t == VT::Complex || a[0].t == VT::Object)) { ValueList none; return I.methodCall(a[0], "sec", none); }
            return Value::number(1.0 / std::cos(a.empty()?0:a[0].toNum()));
        };
        B["cosec"] = [](Interpreter& I, ValueList& a) -> Value {
            if (!a.empty() && (a[0].t == VT::Complex || a[0].t == VT::Object)) { ValueList none; return I.methodCall(a[0], "cosec", none); }
            return Value::number(1.0 / std::sin(a.empty()?0:a[0].toNum()));
        };
        B["cotan"] = [](Interpreter& I, ValueList& a) -> Value {
            if (!a.empty() && (a[0].t == VT::Complex || a[0].t == VT::Object)) { ValueList none; return I.methodCall(a[0], "cotan", none); }
            return Value::number(1.0 / std::tan(a.empty()?0:a[0].toNum()));
        };
        B["asec"] = [](Interpreter& I, ValueList& a) -> Value {
            if (!a.empty() && (a[0].t == VT::Complex || a[0].t == VT::Object)) { ValueList none; return I.methodCall(a[0], "asec", none); }
            return Value::number(std::acos(1.0 / (a.empty()?1:a[0].toNum())));
        };
        B["acosec"] = [](Interpreter& I, ValueList& a) -> Value {
            if (!a.empty() && (a[0].t == VT::Complex || a[0].t == VT::Object)) { ValueList none; return I.methodCall(a[0], "acosec", none); }
            return Value::number(std::asin(1.0 / (a.empty()?1:a[0].toNum())));
        };
        B["acotan"] = [](Interpreter& I, ValueList& a) -> Value {
            if (!a.empty() && (a[0].t == VT::Complex || a[0].t == VT::Object)) { ValueList none; return I.methodCall(a[0], "acotan", none); }
            return Value::number(std::atan(1.0 / (a.empty()?1:a[0].toNum())));
        };
        B["atan2"]  = [](Interpreter&, ValueList& a){ double y=a.empty()?0:a[0].toNum(), x=a.size()>1?a[1].toNum():1.0; return Value::number(std::atan2(y,x)); };
        B["sech"] = [](Interpreter& I, ValueList& a) -> Value {
            if (!a.empty() && (a[0].t == VT::Complex || a[0].t == VT::Object)) { ValueList none; return I.methodCall(a[0], "sech", none); }
            return Value::number(1.0 / std::cosh(a.empty()?0:a[0].toNum()));
        };
        B["cosech"] = [](Interpreter& I, ValueList& a) -> Value {
            if (!a.empty() && (a[0].t == VT::Complex || a[0].t == VT::Object)) { ValueList none; return I.methodCall(a[0], "cosech", none); }
            return Value::number(1.0 / std::sinh(a.empty()?0:a[0].toNum()));
        };
        B["cotanh"] = [](Interpreter& I, ValueList& a) -> Value {
            if (!a.empty() && (a[0].t == VT::Complex || a[0].t == VT::Object)) { ValueList none; return I.methodCall(a[0], "cotanh", none); }
            return Value::number(1.0 / std::tanh(a.empty()?0:a[0].toNum()));
        };
        B["asech"] = [](Interpreter& I, ValueList& a) -> Value {
            if (!a.empty() && (a[0].t == VT::Complex || a[0].t == VT::Object)) { ValueList none; return I.methodCall(a[0], "asech", none); }
            return Value::number(std::acosh(1.0 / (a.empty()?1:a[0].toNum())));
        };
        B["acosech"] = [](Interpreter& I, ValueList& a) -> Value {
            if (!a.empty() && (a[0].t == VT::Complex || a[0].t == VT::Object)) { ValueList none; return I.methodCall(a[0], "acosech", none); }
            return Value::number(std::asinh(1.0 / (a.empty()?1:a[0].toNum())));
        };
        B["acotanh"] = [](Interpreter& I, ValueList& a) -> Value {
            if (!a.empty() && (a[0].t == VT::Complex || a[0].t == VT::Object)) { ValueList none; return I.methodCall(a[0], "acotanh", none); }
            return Value::number(std::atanh(1.0 / (a.empty()?1:a[0].toNum())));
        };
    }
    B["log"] = [](Interpreter& I, ValueList& a) -> Value {
        if (!a.empty() && a[0].t == VT::Complex) { ValueList rest(a.begin() + 1, a.end()); return I.methodCall(a[0], "log", rest); }
        double x = a.empty() ? 0 : a[0].toNum();
        if (a.size() >= 2) return Value::number(std::log(x) / std::log(a[1].toNum())); // log($x, $base)
        return rtBLog(I, a.empty() ? Value::integer(0) : a[0]); };
    B["log10"] = [](Interpreter& I, ValueList& a) -> Value {
        if (!a.empty() && a[0].t == VT::Complex) return I.methodCall(a[0], "log10", {});
        return rtBLog10(I, a.empty() ? Value::integer(0) : a[0]); };
    B["log2"] = [](Interpreter& I, ValueList& a) -> Value {
        if (!a.empty() && a[0].t == VT::Complex) return I.methodCall(a[0], "log2", {});
        return rtBLog2(I, a.empty() ? Value::integer(0) : a[0]); };
    // the sub forms delegate to the methods so they share the char-based
    // positioning, junction autothreading, and negative/out-of-range validation
    B["index"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.empty()) return Value::nil();
        ValueList rest(a.begin() + 1, a.end());
        return I.methodCall(a[0], "index", rest); };
    B["rindex"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.empty()) return Value::nil();
        ValueList rest(a.begin() + 1, a.end());
        return I.methodCall(a[0], "rindex", rest); };
    // `min`/`max` as SUBS delegate to the method, so `:by`/`:k`/`:v`/`:kv`/`:p`
    // behave identically. A lone Positional/Associative argument IS the list;
    // several arguments are the list themselves — and they are NOT flattened, so
    // `min((1,2),(3,4))` compares the two sublists.
    for (const char* mm : {"min", "max"}) {
        std::string mname = mm;
        B[mname] = [mname](Interpreter& I, ValueList& a) -> Value {
            ValueList pos, named;
            for (auto& v : a) { if (v.t == VT::Pair && v.namedArg) named.push_back(v); else pos.push_back(v); }
            Value list;
            if (pos.size() == 1 && (pos[0].t == VT::Array || pos[0].t == VT::Hash || pos[0].t == VT::Range))
                list = pos[0];
            else { list = Value::array(pos); list.isList = true; }
            return I.methodCall(list, mname, named);
        };
    }
    // `minmax` as a SUB delegates to the method, so `:by(&code)` and a leading
    // &mapper mean there what they mean here
    B["minmax"] = [](Interpreter& I, ValueList& a) -> Value {
        ValueList pos, named;
        for (auto& v : a) { if (v.t == VT::Pair && v.namedArg) named.push_back(v); else pos.push_back(v); }
        Value mapper;
        if (!pos.empty() && pos[0].t == VT::Code) { mapper = pos[0]; pos.erase(pos.begin()); }
        Value list;
        if (pos.size() == 1 && (pos[0].t == VT::Array || pos[0].t == VT::Range)) list = pos[0];
        else { list = Value::array(flattenArgs(pos)); list.isList = true; }
        ValueList ma;
        if (mapper.t == VT::Code) ma.push_back(mapper);
        for (auto& nv : named) ma.push_back(nv);
        return I.methodCall(list, "minmax", ma);
    };
    B["chdir"] = [](Interpreter&, ValueList& a) -> Value {
        if (a.empty()) throw RakuError{Value::typeObj("X::TypeCheck::Argument"),
            "Cannot call chdir without an argument"};
        if (a[0].toStr().find('\0') != std::string::npos)
            throw RakuError{Value::typeObj("X::IO::Null"),
                "Cannot use null character (U+0000) as part of the path"};
        if (::chdir(a[0].toStr().c_str()) != 0) {
            Value f = Value::makeHash(); f.hashKind = "Failure";
            (*f.hash)["message"] = Value::str("Failed to change the working directory to '" + a[0].toStr() + "'");
            return f;
        }
        Value p = Value::str(a[0].toStr()); p.hashKind = "IO"; return p; // IO::Path of the new cwd
    };
    // indir($path, &code) — run the block with the process directory changed,
    // then put it back however the block exits
    // uniparse("LATIN SMALL LETTER A") — the inverse of .uniname. A comma-
    // separated list of names yields one character each.
    // `comb($matcher, $input [, $limit])` — the sub form takes the matcher FIRST
    B["comb"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.empty()) { Value o = Value::array(); o.isList = true; o.s = "Seq"; return o; }
        if (a.size() == 1) return I.methodCall(a[0], "comb", ValueList{});
        ValueList rest{a[0]};
        for (size_t i = 2; i < a.size(); i++) rest.push_back(a[i]);
        return I.methodCall(a[1], "comb", rest);
    };
    B["uniparse"] = [](Interpreter&, ValueList& a) -> Value {
        std::string out;
        for (auto& v : a) {
            std::string spec = v.toStr(), cur;
            auto emit = [&](std::string nm) {
                // trim
                size_t b = nm.find_first_not_of(" \t"), e = nm.find_last_not_of(" \t");
                if (b == std::string::npos) return;
                nm = nm.substr(b, e - b + 1);
                int32_t cp = uniCharByName(nm);
                if (cp < 0) throw RakuError{Value::typeObj("X::Str::InvalidCharName"),
                                            "Unrecognized character name [" + nm + "]"};
                out += cpToU8((uint32_t)cp);
            };
            for (char c : spec) { if (c == ',') { emit(cur); cur.clear(); } else cur += c; }
            emit(cur);
        }
        return Value::str(out);
    };
    B["indir"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.size() < 2) return Value::any();
        std::string to = a[0].toStr();
        char buf[4096];
        std::string from = getcwd(buf, sizeof buf) ? buf : ".";
        if (::chdir(to.c_str()) != 0)
            throw RakuError{Value::typeObj("X::IO::Chdir"),
                            "Failed to change the working directory to '" + to + "'"};
        Value r;
        try { ValueList none; r = I.callCallable(a[1], none); }
        catch (...) { ::chdir(from.c_str()); throw; }   // restore on ANY exit
        ::chdir(from.c_str());
        return r;
    };
    // (loop-control escaping a dies-ok/lives-ok block is a death — see those below)
    B["cross"] = [](Interpreter& I, ValueList& a) -> Value {
        Value withF;
        std::vector<ValueList> rows;
        for (auto& v : a) {
            if (v.t == VT::Pair && v.s == "with" && v.pairVal) { withF = *v.pairVal; continue; }
            if (v.t == VT::Array && v.arr) rows.push_back(*v.arr);
            else if (v.t == VT::Range) rows.push_back(v.flatten());
            else rows.push_back(ValueList{v});
        }
        Value out = Value::array(); out.isList = true; out.s = "Seq";
        bool any = !rows.empty();
        for (auto& r : rows) if (r.empty()) any = false;
        if (any) {
            std::vector<size_t> idx(rows.size(), 0);
            for (;;) {
                if (withF.t == VT::Code) {
                    ValueList tup;
                    for (size_t k = 0; k < rows.size(); k++) tup.push_back(rows[k][idx[k]]);
                    Value acc = tup[0];
                    for (size_t k = 1; k < tup.size(); k++) acc = I.callCallable(withF, ValueList{acc, tup[k]});
                    out.arr->push_back(acc);
                } else {
                    Value t = Value::array(); t.isList = true;
                    for (size_t k = 0; k < rows.size(); k++) t.arr->push_back(rows[k][idx[k]]);
                    out.arr->push_back(t);
                }
                size_t k = rows.size();
                while (k > 0 && ++idx[k - 1] == rows[k - 1].size()) idx[--k] = 0;
                if (k == 0) break;
            }
        }
        return out;
    };
    B["leave"] = [](Interpreter&, ValueList& a) -> Value {
        LeaveEx ex; if (!a.empty()) { ex.v = a[0]; ex.hasVal = true; }
        throw ex;
    };
    B["times"] = [](Interpreter&, ValueList&) -> Value {
#if defined(_WIN32)
        // clock() approximates CPU time on Windows; child times unavailable
        double u = (double)std::clock() / CLOCKS_PER_SEC;
        Value out = Value::array({Value::number(u), Value::number(0.0), Value::number(0.0), Value::number(0.0)});
#else
        struct rusage ru, rc;
        getrusage(RUSAGE_SELF, &ru); getrusage(RUSAGE_CHILDREN, &rc);
        auto sec = [](const timeval& tv) { return Value::number(tv.tv_sec + tv.tv_usec / 1e6); };
        Value out = Value::array({sec(ru.ru_utime), sec(ru.ru_stime), sec(rc.ru_utime), sec(rc.ru_stime)});
#endif
        out.isList = true; return out;
    };
    B["elems"] = [](Interpreter&, ValueList& a) -> Value {
        if (a.size() > 1) throw RakuError{Value::typeObj("X::TypeCheck::Argument"),
            "Calling elems() with more than one positional argument will never work"};
        return Value::integer(a.empty() ? 0 : (long long)toList(a[0]).size());
    };
    B["defined"] = [](Interpreter&, ValueList& a) -> Value { return Value::boolean(!a.empty() && defined(a[0])); };
    // Prefix forms of the metamethods: WHAT($x) === $x.WHAT, etc.
    for (const char* mm : {"WHAT", "WHO", "HOW", "VAR", "WHICH", "WHY"})
        B[mm] = [mm](Interpreter& I, ValueList& a) -> Value { ValueList none; return I.methodCall(a.empty() ? Value::any() : a[0], mm, none); };
    B["chars"] = [](Interpreter& I, ValueList& a) -> Value { return a.empty() ? Value::integer(0) : rtBChars(I, a[0]); };
    auto univalOf = [](uint32_t cp) -> Value {
        long long num, den; if (!uniNumValue(cp, num, den)) return Value::nil();
        return den == 1 ? Value::integer(num) : Value::rat(BigInt(num), BigInt(den));
    };
    auto cpOfArg = [](const Value& v, bool& ok) -> uint32_t {
        ok = true;
        if (v.t == VT::Int || v.t == VT::Bool) return (uint32_t)v.toInt();
        auto cps = utf8cp(v.toStr()); if (cps.empty()) { ok = false; return 0; } return cps[0];
    };
    B["expmod"] = [](Interpreter& I, ValueList& a) -> Value { // expmod($b, $e, $m)
        if (a.size() < 3) return Value::integer(0);
        ValueList rest{a[1], a[2]};
        return I.methodCall(a[0], "expmod", rest);
    };
    B["unival"] = [univalOf, cpOfArg](Interpreter&, ValueList& a) -> Value {
        if (a.empty() || a[0].t == VT::Type) throw RakuError{Value::typeObj("X::Numeric"), "Cannot get unival"};
        bool ok; uint32_t cp = cpOfArg(a[0], ok); return ok ? univalOf(cp) : Value::nil();
    };
    B["univals"] = [univalOf](Interpreter&, ValueList& a) -> Value {
        Value out = Value::array(); out.isList = true;
        if (!a.empty()) for (uint32_t cp : utf8cp(a[0].toStr())) out.arr->push_back(univalOf(cp));
        return out;
    };
    B["uninames"] = [](Interpreter& I, ValueList& a) -> Value {
        Value v = a.empty() ? Value::str("") : a[0];
        ValueList none; return I.methodCall(v, "uninames", none);
    };
    B["uniname"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.empty() || a[0].t == VT::Type)
            throw RakuError{Value::typeObj("X::Multi::NoMatch"), "Cannot call uniname with a type object"};
        ValueList none; return I.methodCall(a[0], "uniname", none);
    };
    B["uniprop"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.empty()) return Value::str("");
        Value inv = a[0]; ValueList rest(a.begin() + 1, a.end());
        return I.methodCall(inv, "uniprop", rest); // full property dispatch
    };
    for (const char* un : {"uniprops", "unival", "univals"}) {
        std::string mn = un;
        B[mn] = [mn](Interpreter& I, ValueList& a) -> Value {
            if (a.empty()) return Value::str("");
            Value inv = a[0]; ValueList rest(a.begin() + 1, a.end());
            return I.methodCall(inv, mn, rest);
        };
    }
    // (the Str/Int method form delegates here through the sub-as-method fallback)
    // unimatch($char, $propval [, $propname]) — property match; a bare value
    // tests the general category (major class prefix allowed: L matches Lu)
    B["unimatch"] = [cpOfArg](Interpreter&, ValueList& a) -> Value {
        if (a.size() < 2) return Value::boolean(false);
        if (a[0].t == VT::Type)
            throw RakuError{Value::typeObj("X::Multi::NoMatch"),
                            "Cannot call unimatch with a type object"};
        bool ok; uint32_t cp = cpOfArg(a[0], ok); if (!ok) return Value::nil(); // "" → Nil
        std::string want = a[1].toStr();
        auto loose = [](const std::string& s) {
            std::string o;
            for (char ch : s) if (std::isalnum((unsigned char)ch)) o += (char)std::tolower((unsigned char)ch);
            return o;
        };
        if (a.size() > 2) { // explicit property: unimatch($c, 'Hebrew', 'Block') etc.
            std::string prop = a[2].toStr();
            std::string got = (prop == "Script" || prop == "sc") ? uniScript(cp)
                            : (prop == "Block" || prop == "blk") ? uniBlockOf(cp)
                                                                 : uniGeneralCategory(cp);
            if (got == want) return Value::boolean(true);
            if (prop == "Block" || prop == "blk") return Value::boolean(loose(got) == loose(want));
            if (want.size() == 1 && !got.empty() && got[0] == want[0]) return Value::boolean(true);
            return Value::boolean(false);
        }
        // 2-arg: the name may be a general category, script, binary property, or block
        std::string got = uniGeneralCategory(cp);
        if (got == want) return Value::boolean(true);
        // major-class prefix: unimatch("A", "L") is true for Lu
        if (want.size() == 1 && !got.empty() && got[0] == want[0]) return Value::boolean(true);
        if ((want == "L&" || want == "LC") && got.size() == 2 && got[0] == 'L' &&
            (got[1] == 'u' || got[1] == 'l' || got[1] == 't')) return Value::boolean(true);
        if (uniMatchesProp(cp, want)) return Value::boolean(true); // script / binary / <:Prop> forms
        // a block name without the In prefix, loosely normalized
        std::string qn = loose(want);
        return Value::boolean(!qn.empty() && qn == loose(uniBlockOf(cp)));
    };
    B["uc"] = [](Interpreter& I, ValueList& a) -> Value { return a.empty() ? Value::str("") : rtBUc(I, a[0]); };
    B["lc"] = [](Interpreter& I, ValueList& a) -> Value { return a.empty() ? Value::str("") : rtBLc(I, a[0]); };
    B["tc"] = [](Interpreter&, ValueList& a) -> Value { return Value::str(a.empty() ? "" : mapCase(a[0].toStr(), 0, 1)); };
    // `so *` / `not *` curry like operators do (Rakudo: (so *).^name is WhateverCode)
    auto boolCurry = [](bool negate, const Value& w) -> Value {
        Value code; code.t = VT::Code; code.code = std::make_shared<Callable>();
        code.code->isWhateverCode = true;
        code.code->whateverArity = (w.t == VT::Code && w.code && w.code->whateverArity > 0) ? w.code->whateverArity : 1;
        Value inner = w;
        code.code->builtin = [negate, inner](Interpreter& I, ValueList& xs) -> Value {
            Value v = inner.t == VT::Whatever ? (xs.empty() ? Value::any() : xs[0])
                                              : I.callCallable(inner, xs);
            bool b = I.boolify(v);
            return Value::boolean(negate ? !b : b);
        };
        return code;
    };
    B["so"] = [boolCurry](Interpreter& I, ValueList& a) -> Value {
        if (a.size() == 1 && (a[0].t == VT::Whatever || (a[0].t == VT::Code && a[0].code && a[0].code->isWhateverCode)))
            return boolCurry(false, a[0]);
        return Value::boolean(!a.empty() && I.boolify(a[0]));
    };
    B["not"] = [boolCurry](Interpreter& I, ValueList& a) -> Value {
        if (a.size() == 1 && (a[0].t == VT::Whatever || (a[0].t == VT::Code && a[0].code && a[0].code->isWhateverCode)))
            return boolCurry(true, a[0]);
        return Value::boolean(a.empty() || !I.boolify(a[0]));
    };
    // Junction constructors: all()/any()/one()/none() (also written via & | ^).
    // (all/any/one/none are registered ONCE, earlier, with the one-arg rule —
    // a flattening duplicate here used to shadow it)
    B["ord"] = [](Interpreter& I, ValueList& a) -> Value {
        // bare `ord` (no argument) is the Perl-5-ism Rakudo rejects with X::Obsolete
        if (a.empty()) throw RakuError{Value::typeObj("X::Obsolete"),
            "Unsupported use of bare \"ord\". In Raku please use: .ord if you meant to call it as a method on $_, or use an explicit invocant or argument"};
        return rtBOrd(I, a[0]);
    };
    B["chr"] = [](Interpreter& I, ValueList& a) -> Value {
        return rtBChr(I, a.empty() ? Value::integer(0) : a[0]);
    };
    B["ords"] = [](Interpreter& I, ValueList& a) -> Value { Value v = a.empty() ? Value::any() : a[0]; ValueList none; return I.methodCall(v, "ords", none); };
    B["chrs"] = [](Interpreter&, ValueList& a) -> Value { std::string r; for (auto& x : flattenArgs(a)) r += cpToUtf8((uint32_t)x.toInt()); return Value::str(r); };
    B["sign"] = [](Interpreter& I, ValueList& a) -> Value { return rtBSign(I, a.empty() ? Value::any() : a[0]); };
    B["is-prime"] = [](Interpreter& I, ValueList& a) -> Value { return rtBIsPrime(I, a.empty() ? Value::any() : a[0]); };
    B["end"] = [](Interpreter& I, ValueList& a) -> Value { if (a.empty()) throw RakuError{Value::typeObj("X::Comp"), "Calling end() requires an argument"}; ValueList none; return I.methodCall(a[0], "end", none); };
    B["kv"] = [](Interpreter& I, ValueList& a) -> Value { if (a.empty()) throw RakuError{Value::typeObj("X::Comp"), "Calling kv() requires an argument"}; ValueList none; return I.methodCall(a[0], "kv", none); };
    B["prepend"] = [](Interpreter& I, ValueList& a) -> Value { if (a.empty()) return Value::any(); Value inv = a[0]; ValueList rest(a.begin() + 1, a.end()); return I.methodCall(inv, "prepend", rest); };
    B["append"] = [](Interpreter& I, ValueList& a) -> Value { if (a.empty()) return Value::any(); Value inv = a[0]; ValueList rest(a.begin() + 1, a.end()); return I.methodCall(inv, "append", rest); };
    B["join"] = [](Interpreter&, ValueList& a) -> Value {
        if (a.empty()) return Value::str("");
        std::string sep = a[0].toStr();
        ValueList items;
        for (size_t i = 1; i < a.size(); i++) { ValueList l = toList(a[i]); items.insert(items.end(), l.begin(), l.end()); }
        return Value::str(joinValues(items, sep));
    };
    // :16("2e") radix conversion — the value's digits parsed in the given base.
    // :256[a, b, c] — place-value digits in the given base; slips/arrays
    // in the list flatten (`:256[|@^a]`)
    B["__radix-list"] = [](Interpreter&, ValueList& a) -> Value {
        if (a.empty()) return Value::integer(0);
        long long base = a[0].toInt();
        long long val = 0;
        for (size_t k = 1; k < a.size(); k++) {
            if (a[k].t == VT::Array && a[k].arr)
                for (auto& e : *a[k].arr) val = val * base + e.toInt();
            else val = val * base + a[k].toInt();
        }
        return Value::integer(val);
    };
    B["__radix"] = [](Interpreter&, ValueList& a) -> Value {
        if (a.size() < 2) return Value::integer(0);
        int base = (int)a[0].toInt();
        std::string s = a[1].toStr();
        long long val = 0;
        for (char c : s) {
            if (c == '_') continue;
            int d = (c >= '0' && c <= '9') ? c - '0'
                  : (c >= 'a' && c <= 'z') ? c - 'a' + 10
                  : (c >= 'A' && c <= 'Z') ? c - 'A' + 10 : -1;
            if (d < 0 || d >= base) break;
            val = val * base + d;
        }
        return Value::integer(val);
    };
    // split(SEP, STR, …) is the sub form of STR.split(SEP, …)
    B["split"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.size() < 2) return Value::array();
        ValueList margs; margs.push_back(a[0]);
        for (size_t i = 2; i < a.size(); i++) margs.push_back(a[i]);
        return I.methodCall(a[1], "split", margs, nullptr);
    };
    B["reverse"] = [](Interpreter&, ValueList& a) -> Value {
        ValueList items; for (auto& v : a) { ValueList l = toList(v); items.insert(items.end(), l.begin(), l.end()); }
        std::reverse(items.begin(), items.end());
        Value o = Value::array(items); o.isList = true; o.s = "Seq"; return o;
    };
    B["sort"] = [](Interpreter& I, ValueList& a) -> Value {
        // `sort {comparator}, @list` / `sort &by, @list`: a leading Code is the
        // comparator/key extractor, not an element. `:by(&f)` names it; other
        // adverbs (:k) are forwarded. All such forms delegate to List.sort.
        Value cmp; bool haveCmp = false;
        ValueList named, pos;
        for (auto& v : a) {
            if (v.t == VT::Pair && v.namedArg) {
                if (v.s == "by" && v.pairVal) { cmp = *v.pairVal; haveCmp = true; }
                else named.push_back(v);
            }
            else pos.push_back(v);
        }
        if (!haveCmp && !pos.empty() && pos[0].t == VT::Code) {
            cmp = pos[0]; haveCmp = true;
            pos.erase(pos.begin());
        }
        ValueList items; for (auto& v : pos) { ValueList l = toList(v); items.insert(items.end(), l.begin(), l.end()); }
        if (haveCmp || !named.empty()) {
            Value lst = Value::list(items);
            ValueList ma;
            if (haveCmp) ma.push_back(cmp);
            for (auto& nv : named) ma.push_back(nv);
            return I.methodCall(lst, "sort", ma);
        }
        std::stable_sort(items.begin(), items.end(), [](const Value& x, const Value& y) { return valueCmp(x, y) < 0; });
        Value o = Value::array(items); o.isList = true; o.s = "Seq"; return o;
    };
    B["sum"] = [](Interpreter&, ValueList& a) -> Value {
        double s = 0; bool allInt = true;
        for (auto& v : a) for (auto& x : toList(v)) { s += x.toNum(); if (x.t != VT::Int) allInt = false; }
        return allInt ? Value::integer((long long)s) : Value::number(s);
    };
    B["keys"] = [](Interpreter&, ValueList& a) -> Value {
        Value out = Value::array();
        if (!a.empty() && a[0].t == VT::Hash) for (auto& kv : *a[0].hash) out.arr->push_back(Value::str(kv.first));
        else if (!a.empty()) { ValueList l = toList(a[0]); for (size_t i = 0; i < l.size(); i++) out.arr->push_back(Value::integer((long long)i)); }
        return out;
    };
    B["values"] = [](Interpreter&, ValueList& a) -> Value {
        Value out = Value::array();
        if (!a.empty() && a[0].t == VT::Hash) for (auto& kv : *a[0].hash) out.arr->push_back(kv.second);
        else if (!a.empty()) { ValueList l = toList(a[0]); for (auto& v : l) out.arr->push_back(v); }
        return out;
    };
    // Synchronous react/whenever/supply: eager, deterministic model.
    B["react"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.empty() || a.back().t != VT::Code) return Value::nil();
        auto ctx = std::make_shared<ReactCtx>();
        I.reactStack_.push_back(ctx);
        I.supplyCloseStack_.emplace_back();
        try { I.callCallable(a.back(), {}); }
        catch (...) { I.reactStack_.pop_back(); I.supplyCloseStack_.pop_back(); throw; }
        I.reactStack_.pop_back();
        I.runReactLoop(ctx); // block until every live whenever source is done
        {   // react is over: tear down externally-wired taps (OS-signal taps) so
            // their dispatcher stops firing the handler once the block is gone.
            std::vector<std::shared_ptr<TapHandle>> extTaps;
            { std::lock_guard<std::mutex> lk(ctx->m); extTaps.swap(ctx->extTaps); }
            for (auto& h : extTaps) if (h) I.closeTapHandle(h);
        }
        {   // react is over: its whenever taps close — run on-close callbacks
            auto closers = std::move(I.supplyCloseStack_.back());
            I.supplyCloseStack_.pop_back();
            for (auto& cb : closers) if (cb.t == VT::Code) { try { I.callCallable(cb, {}); } catch (...) {} }
        }
        return Value::nil();
    };
    B["whenever"] = [](Interpreter& I, ValueList& a) -> Value {
        // Inside an on-demand supply activation (real tap or eager drain): wire a
        // real inner tap. The body runs (now or later, from an I/O worker) with
        // this activation re-established, so its emits reach the downstream tap.
        if (!I.tctx_.tapStack.empty() && a.size() >= 2 && a.back().t == VT::Code) {
            auto ctx = I.tctx_.tapStack.back();
            Value src = a[0], blk = a.back();
            // whenever Promise.in(N)/at(T) in a supply block: a real timer (was
            // firing immediately — Cro's connection/headers timeouts rely on it).
            if (src.t == VT::Hash && src.hashKind == "Promise" && src.hash->count("kind") &&
                (*src.hash)["kind"].toStr() == "timer") {
                double secs = src.hash->count("seconds") ? (*src.hash)["seconds"].toNum() : 0;
                return I.spawnSupplyTimer(secs, blk, ctx);
            }
            // whenever over a Promise: register an ASYNC one-shot — the block runs
            // once, with the promise's RESULT, when the promise settles. Must NOT
            // block the supply-block setup: an unkept Promise stays dormant (Cro's
            // ResponseParser has `whenever $cancellation` that is normally never
            // kept — awaiting it synchronously hung the whole response pipeline).
            if (src.t == VT::Hash && src.hashKind == "Promise" && src.ext) {
                auto ps = std::static_pointer_cast<PromiseState>(src.ext);
                std::vector<Value> lastP, quitP;
                scanSupplyPhasers(blk, &lastP, &quitP, nullptr);
                // a promise is a live source until it settles: hold the supply open
                // (so the block that ends after registering it doesn't finish early),
                // and release the hold once it fires.
                ctx->pending++;
                // fire under the supply activation so the body's emits reach downstream
                Value fireW = ctxCallable(ctx, [blk, lastP, quitP, ps, ctx](Interpreter& I2, ValueList&) -> Value {
                    if (ps->broken) {
                        Value ex = ps->cause.t == VT::Nil ? Value::str(ps->causeMsg) : ps->cause;
                        for (auto& q : quitP) { ValueList one{ex}; try { I2.callCallable(q, one); } catch (...) {} }
                    } else {
                        ValueList one{ ps->result };
                        try { I2.callCallable(blk, one); } catch (NextEx&) {} catch (LastEx&) {}
                        for (auto& p : lastP) { ValueList na; try { I2.callCallable(p, na); } catch (...) {} }
                    }
                    ctx->pending--;
                    I2.maybeFinishSupply(ctx);
                    return Value::any();
                });
                Interpreter* self = &I;
                std::function<void()> run = [self, fireW]() { ValueList na; try { self->callCallable(fireW, na); } catch (...) {} };
                bool now = false;
                { std::lock_guard<std::mutex> lk(ps->m); if (ps->done) now = true; else ps->thens.push_back(run); }
                if (now) run();
                Value t = Value::makeHash(); t.hashKind = "Tap"; return t;
            }
            // whenever over a plain (non-Supply, non-Promise) value: run once with it
            if (!(src.t == VT::Hash && src.hashKind == "Supply")) {
                Value rv = src;
                if (src.t == VT::Hash && src.hashKind == "Promise" && src.hash->count("result")) rv = (*src.hash)["result"];
                ValueList one{rv};
                try { I.callCallable(blk, one); } catch (NextEx&) {} catch (LastEx&) {}
                Value t = Value::makeHash(); t.hashKind = "Tap"; return t;
            }
            std::vector<Value> lastP, quitP;
            scanSupplyPhasers(blk, &lastP, &quitP, nullptr);
            Value emitW = ctxCallable(ctx, [blk](Interpreter& I2, ValueList& args) -> Value {
                try { ValueList one = args; return I2.callCallable(blk, one); }
                catch (NextEx&) {} catch (LastEx&) {}
                return Value::any();
            });
            // every inner tap holds the supply open until its done fires; the
            // done hook runs LAST phasers, then releases this activation's hold
            ctx->pending++;
            Value doneW = ctxCallable(ctx, [lastP, ctx](Interpreter& I2, ValueList&) -> Value {
                for (auto& p : lastP) { ValueList na; try { I2.callCallable(p, na); } catch (...) {} }
                ctx->pending--;
                I2.maybeFinishSupply(ctx);
                return Value::any();
            });
            Value quitW;
            if (!quitP.empty())
                quitW = ctxCallable(ctx, [quitP](Interpreter& I2, ValueList& args) -> Value {
                    for (auto& p : quitP) { ValueList one = args; try { I2.callCallable(p, one); } catch (...) {} }
                    return Value::any();
                });
            Value tapV = I.tapSupply(src, emitW, doneW, quitW);
            // closing the outer tap closes this inner one
            if (ctx->tap && tapV.t == VT::Hash && tapV.ext &&
                tapV.hash->count("wired") && (*tapV.hash)["wired"].truthy()) {
                auto ih = std::static_pointer_cast<TapHandle>(tapV.ext);
                Interpreter* ip = &I;
                std::lock_guard<std::mutex> lk(ctx->tap->m);
                if (!ctx->tap->closed) ctx->tap->closers.push_back([ip, ih] { ip->closeTapHandle(ih); });
            }
            return tapV;
        }
        // a `done` in an earlier whenever closes the react — later whenevers don't run
        if (!I.reactStack_.empty()) {
            auto ctx = I.reactStack_.back();
            std::lock_guard<std::mutex> lk(ctx->m);
            if (ctx->closed) return Value::nil();
        }
        // whenever SUPPLY { BLOCK }: tap the supply, running BLOCK for each emitted value
        if (a.size() >= 2 && a.back().t == VT::Code) {
            Value s = a[0], blk = a.back();
            // whenever signal(SIGINT) { … } in a react: wire the OS-signal tap,
            // counting it as a live react source so the loop waits until `done`.
            if (s.t == VT::Hash && s.hashKind == "Supply" &&
                s.hash->count("kind") && (*s.hash)["kind"].toStr() == "signal") {
                std::vector<int> sigs;
                if (s.hash->count("signals") && (*s.hash)["signals"].arr)
                    for (auto& n : *(*s.hash)["signals"].arr) sigs.push_back((int)n.toInt());
                std::shared_ptr<ReactCtx> ctx;
                if (!I.reactStack_.empty()) {
                    ctx = I.reactStack_.back();
                    std::lock_guard<std::mutex> lk(ctx->m); ctx->liveSources++;
                }
                Value emitW; emitW.t = VT::Code; emitW.code = std::make_shared<Callable>();
                Value blkCopy = blk;
                emitW.code->builtin = [blkCopy](Interpreter& I2, ValueList& args) -> Value {
                    ValueList one = args;
                    try { return I2.callCallable(blkCopy, one); } catch (NextEx&) {} catch (LastEx&) {}
                    return Value::any();
                };
                return I.tapSignal(sigs, emitW, Value::nil(), ctx);
            }
            // whenever $socket.Supply { … } — an async-read/async-listen stream in a
            // react: count it as a live source so the block waits for data, and
            // decrement when the stream ends (connection close) so the react exits.
            if (s.t == VT::Hash && s.hashKind == "Supply" && s.hash->count("kind")) {
                std::string k = (*s.hash)["kind"].toStr();
                if (k == "async-read" || k == "async-listen") {
                    std::shared_ptr<ReactCtx> ctx;
                    if (!I.reactStack_.empty()) {
                        ctx = I.reactStack_.back();
                        std::lock_guard<std::mutex> lk(ctx->m); ctx->liveSources++;
                    }
                    Value doneW;
                    if (ctx) {
                        std::weak_ptr<ReactCtx> wctx = ctx;
                        doneW.t = VT::Code; doneW.code = std::make_shared<Callable>();
                        doneW.code->builtin = [wctx](Interpreter&, ValueList&) -> Value {
                            if (auto c = wctx.lock()) { std::lock_guard<std::mutex> lk(c->m); if (c->liveSources > 0) c->liveSources--; c->cv.notify_all(); }
                            return Value::any();
                        };
                    }
                    return I.tapSupply(s, blk, doneW, Value::nil());
                }
            }
            if (s.t == VT::Hash && s.hashKind == "Supply") {
                if (s.hash->count("supplier")) {
                    // live supply: register a tap; count it as a react source so the
                    // enclosing react blocks until this supplier signals done.
                    Value tapRec = Value::makeHash();
                    (*tapRec.hash)["emit"] = blk;
                    // Carry the Supply's transform chain (head/grep/map/…) onto the tap,
                    // each step with its OWN fresh state — same as tapSupply's live branch.
                    // Without it `whenever $s.Supply.head(1)` never limits and, worse,
                    // never reports completion, so the enclosing react waits forever.
                    if (s.hash->count("chain")) {
                        Value chain = Value::array();
                        for (auto& step : *(*s.hash)["chain"].arr) {
                            Value s2 = Value::makeHash(); *s2.hash = *step.hash;
                            (*s2.hash)["state"] = Value::makeHash();
                            chain.arr->push_back(s2);
                        }
                        (*tapRec.hash)["chain"] = chain;
                    }
                    if (!I.reactStack_.empty()) {
                        auto ctx = I.reactStack_.back();
                        tapRec.ext = ctx;
                        { std::lock_guard<std::mutex> lk(ctx->m); ctx->liveSources++; }
                    }
                    Value sup = (*s.hash)["supplier"];
                    if (sup.t == VT::Hash && sup.hash->count("taps")) (*sup.hash)["taps"].arr->push_back(tapRec);
                    // The supplier already signalled done before this tap registered
                    // (eager worker ran first): close the tap now, so runReactLoop
                    // doesn't wait on a source that will never complete.
                    if (sup.t == VT::Hash && sup.hash->count("done_state") &&
                        (*sup.hash)["done_state"].truthy() && tapRec.ext) {
                        auto ctx = std::static_pointer_cast<ReactCtx>(tapRec.ext);
                        std::lock_guard<std::mutex> lk(ctx->m);
                        if (ctx->liveSources > 0) ctx->liveSources--;
                        ctx->cv.notify_all();
                    }
                    Value t = Value::makeHash(); t.hashKind = "Tap"; return t;
                }
                ValueList ta{blk}; return I.methodCall(s, "tap", ta); // from-list: eager
            }
            // whenever Promise.in(N) { … } — a timer: fire once after the real delay
            // as a react source, so it doesn't defeat a timeout guard by firing at t=0.
            if (s.t == VT::Hash && s.hashKind == "Promise" &&
                s.hash->count("kind") && (*s.hash)["kind"].toStr() == "timer") {
                double secs = s.hash->count("seconds") ? (*s.hash)["seconds"].toNum() : 0;
                std::shared_ptr<ReactCtx> ctx = I.reactStack_.empty() ? nullptr : I.reactStack_.back();
                return I.spawnTimerWhenever(secs, blk, ctx);
            }
            // whenever $proc.start { … } — a lazy Proc::Async promise: the process
            // runs when the promise is realized (await does the same); run it NOW,
            // then fire the block once with the finished proc (its .so/.exitcode
            // reflect the exit status — zef's curl/wget fetch checks `$_.so`).
            if (s.t == VT::Hash && s.hashKind == "Promise" &&
                s.hash->count("kind") && (*s.hash)["kind"].toStr() == "proc") {
                I.runProcPromise(s, 0);
                Value procv = s.hash->count("proc") ? (*s.hash)["proc"] : s;
                ValueList one{procv};
                try { I.callCallable(blk, one); } catch (NextEx&) {} catch (LastEx&) {}
                Value t = Value::makeHash(); t.hashKind = "Tap"; return t;
            }
            // whenever over a SETTLED Promise binds the block to its RESULT, not the
            // promise object. (An unkept one still fires immediately with the object —
            // the full async react registration is still an open item.)
            if (s.t == VT::Hash && s.hashKind == "Promise" && s.ext) {
                auto ps = std::static_pointer_cast<PromiseState>(s.ext);
                bool done; { std::lock_guard<std::mutex> lk(ps->m); done = ps->done; }
                if (done) { ValueList one{ps->result}; return I.callCallable(blk, one); }
            }
            // whenever over a Promise/plain value: run the block once with it
            ValueList one{s}; return I.callCallable(blk, one);
        }
        return Value::nil();
    };
    // Because execution is synchronous (no real parallelism to wait for), sleep is
    // CAPPED to a small delay: it must be defined (many async tests call it) without
    // risking harness timeouts — e.g. a daemon thread's `sleep 10000`. No passing
    // test can depend on real elapsed time (sleep used to be undefined → an error).
    B["sleep"] = [](Interpreter& I, ValueList& a) -> Value {
        I.sleepYield(a.empty() ? 0 : a[0].toNum());  // GIL-released + capped (see sleepYield)
        return Value::any(); // sleep returns Nil
    };
    // `sleep-until $instant` sleeps until that moment and answers whether it
    // actually waited — an instant already past answers False without sleeping.
    B["sleep-until"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.empty()) return Value::boolean(false);
        // measured against the same high-resolution clock `now` reads, so a
        // fraction-of-a-second target is not lost to truncation
        auto d = std::chrono::system_clock::now().time_since_epoch();
        double now = std::chrono::duration<double>(d).count();
        double target = a[0].toNum();
        if (target <= now) return Value::boolean(false);
        I.sleepYield(target - now);
        return Value::boolean(true);
    };
    // signal(SIGINT, …) — a Supply that emits the Signal enum value each time the
    // process receives one of the named OS signals. Standard Ctrl-C shutdown:
    // `react { whenever signal(SIGINT) { $server.stop; done } }`.
    B["signal"] = [](Interpreter&, ValueList& a) -> Value {
        Value s = Value::makeHash(); s.hashKind = "Supply";
        (*s.hash)["kind"] = Value::str("signal");
        Value sigs = Value::array();
        for (auto& v : a) { int n = signalNumberOf(v); if (n > 0) sigs.arr->push_back(Value::integer(n)); }
        (*s.hash)["signals"] = sigs;
        return s;
    };
    B["sleep-timer"] = [](Interpreter& I, ValueList& a) -> Value {
        I.sleepYield(a.empty() ? 0 : a[0].toNum());
        return Value::number(0);
    };
    B["sleep-till"] = [](Interpreter&, ValueList&) -> Value { return Value::boolean(true); };
    B["done"] = [](Interpreter& I, ValueList&) -> Value {
        // `done` inside an on-demand supply activation ends its stream: fire the
        // downstream done callback and close the activation's inner taps.
        if (!I.tctx_.tapStack.empty()) {
            auto ctx = I.tctx_.tapStack.back();
            ctx->done = true;
            if (!ctx->collect) {
                if (ctx->doneCb.t == VT::Code) { ValueList na; try { I.callCallable(ctx->doneCb, na); } catch (...) {} }
                I.closeTapHandle(ctx->tap);
            }
            return Value::boolean(true);
        }
        // `done` inside a react block closes its loop.
        if (!I.reactStack_.empty()) {
            auto ctx = I.reactStack_.back();
            std::lock_guard<std::mutex> lk(ctx->m); ctx->closed = true; ctx->cv.notify_all();
        }
        return Value::boolean(true);
    };
    B["supply"] = [](Interpreter& I, ValueList& a) -> Value {
        // supply { … } is ON-DEMAND: the block runs when the supply is tapped
        // (tapSupply), with emit routed to the tap. Value-context consumers
        // (.list, for, await) drain it eagerly via drainSupplyBlock — the same
        // values the old eager model produced, just computed at consumption.
        Value s = Value::makeHash(); s.hashKind = "Supply";
        (*s.hash)["block"] = (!a.empty() && a.back().t == VT::Code) ? a.back() : Value::nil();
        return s;
    };
    B["emit"] = [](Interpreter& I, ValueList& a) -> Value {
        Value v = a.empty() ? Value::any() : a[0];
        if (std::getenv("RAKUPP_TAP_TRACE"))
            fprintf(stderr, "[emit] depth=%zu kind=%s collect=%d cb=%d\n", I.tctx_.tapStack.size(),
                    v.typeName().c_str(),
                    I.tctx_.tapStack.empty() ? -1 : (int)!!I.tctx_.tapStack.back()->collect,
                    I.tctx_.tapStack.empty() ? -1 : (int)(I.tctx_.tapStack.back()->emitCb.t == VT::Code));
        if (!I.tctx_.tapStack.empty()) {
            auto ctx = I.tctx_.tapStack.back();
            if (ctx->collect) { ctx->collect->push_back(v); return Value::boolean(true); }
            if (ctx->emitCb.t == VT::Code) { ValueList one{v}; I.callCallable(ctx->emitCb, one); }
            return Value::boolean(true);
        }
        if (!I.tctx_.supplyStack.empty()) I.tctx_.supplyStack.back()->push_back(v);
        return Value::boolean(true);
    };
    // printf/sprintf take **@args — a list/array argument flattens into the values,
    // so `printf $fmt, $x, f()` where f returns (a, b) fills three directives.
    auto sprintfArgs = [](const ValueList& a) -> ValueList {
        ValueList rest;
        for (size_t i = 1; i < a.size(); i++) {
            if (a[i].t == VT::Array && a[i].arr) for (auto& x : *a[i].arr) rest.push_back(x);
            else rest.push_back(a[i]);
        }
        return rest;
    };
    B["sprintf"] = [sprintfArgs](Interpreter& I, ValueList& a) -> Value {
        if (a.empty()) return Value::str("");
        ValueList rest = sprintfArgs(a);
        return Value::str(doSprintf(a[0].toStr(), rest, I.langRev_));
    };
    // Format object (6.e `q:o/…/` / `q:format/…/`): a callable sprintf template that
    // stringifies to its format string. Built by the parser from a flagged literal.
    B["__format__"] = [](Interpreter&, ValueList& a) -> Value {
        Value f = Value::makeHash(); f.hashKind = "Format";
        (*f.hash)["fmt"] = Value::str(a.empty() ? "" : a[0].toStr());
        return f;
    };
    B["printf"] = [sprintfArgs](Interpreter& I, ValueList& a) -> Value {
        if (a.empty()) return Value::boolean(true);
        // `printf($fmt, $junction)` PRINTS ONCE PER EIGENSTATE, in order — Rakudo has
        // a dedicated printf(Str(Cool), Junction:D) candidate. (sprintf does not:
        // there the junction stays one value.)
        if (a.size() == 2 && a[1].t == VT::Array && a[1].arr &&
            (a[1].enumName == "any" || a[1].enumName == "all" ||
             a[1].enumName == "one" || a[1].enumName == "none")) {
            for (auto& e : *a[1].arr) {
                ValueList one{e};
                std::cout << doSprintf(a[0].toStr(), one, I.langRev_);
            }
            return Value::boolean(true);
        }
        ValueList rest = sprintfArgs(a);
        std::cout << doSprintf(a[0].toStr(), rest, I.langRev_); return Value::boolean(true);
    };
    // 6.e sub form: snip(PRED(s), *@list) — first arg is the predicate or a (p1,p2)
    // list of predicates; the rest is the list. Delegates to the .snip method.
    B["snip"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.empty()) return Value::array();
        Value list = Value::array(); list.isList = true;
        for (size_t k = 1; k < a.size(); k++) for (auto& v : toList(a[k])) list.arr->push_back(v);
        return I.methodCall(list, "snip", {a[0]});
    };
    B["map"] = [](Interpreter& I, ValueList& a) -> Value {
        Value out = Value::array(); out.isList = true; out.s = "Seq";
        if (a.size() >= 2 && a[0].t == VT::Code)
            for (size_t i = 1; i < a.size(); i++) // `map fn, 1, 2, 3` — every list arg
                for (auto& v : toList(a[i])) {
                    Value r = I.callCallable(a[0], {v});
                    if (r.t == VT::Array && r.isList && r.s == "Slip")
                        for (auto& x : *r.arr) out.arr->push_back(x);
                    else out.arr->push_back(r);
                }
        return out;
    };
    B["grep"] = [](Interpreter& I, ValueList& a) -> Value {
        Value out = Value::array(); out.isList = true; out.s = "Seq";
        if (a.empty()) return out;
        Value mt = a[0];
        Value list = Value::array(); list.isList = true;
        ValueList margs{mt}, pos;
        for (size_t i = 1; i < a.size(); i++) {
            if (a[i].t == VT::Pair && (a[i].s == "k" || a[i].s == "v" || a[i].s == "kv" || a[i].s == "p"))
                margs.push_back(a[i]); // adverb, pass through
            else pos.push_back(a[i]);
        }
        // `grep`'s list is a +@values slurpy (single-arg rule): ONE Positional arg
        // is iterated; with several args each is one element, so a bare `[]` stays
        // an element instead of flattening away and renumbering :kv/:p indices.
        // EXCEPT a Slip, which always flattens into the surrounding list (that's
        // what `.Slip` is for) — `grep &p, (…).Slip, (…).Slip` merges both.
        bool singleList = (pos.size() == 1 && (pos[0].t == VT::Array || pos[0].t == VT::Range) && !pos[0].itemized);
        for (auto& x : pos) {
            bool isSlip = (x.t == VT::Array && x.arr && x.s == "Slip");
            if (isSlip || (singleList && (x.t == VT::Array || x.t == VT::Range))) {
                if (x.t == VT::Range) for (auto& e : x.flatten()) list.arr->push_back(e);
                else for (auto& e : *x.arr) list.arr->push_back(e);
            } else list.arr->push_back(x);
        }
        return I.methodCall(list, "grep", margs); // one implementation
    };
    B["first"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.size() >= 2 && a[0].t == VT::Code)
            for (auto& v : toList(a[1])) if (I.callCallable(a[0], {v}).truthy()) return v;
        return Value::any();
    };
    B["push"] = [](Interpreter&, ValueList& a) -> Value {
        if (!a.empty() && a[0].t == VT::Array) { for (size_t i = 1; i < a.size(); i++) a[0].arr->push_back(a[i]); return a[0]; }
        return Value::any();
    };
    B["pop"] = [](Interpreter& I, ValueList& a) -> Value {
        if (!a.empty() && a[0].t == VT::Array && a[0].ext && std::static_pointer_cast<LazySeqState>(a[0].ext)->infinite)
            throw RakuError{Value::typeObj("X::Cannot::Lazy"), "Cannot pop a lazy list"};
        if (!a.empty() && a[0].t == VT::Array && !a[0].arr->empty()) { Value v = a[0].arr->back(); a[0].arr->pop_back(); if (v.t == VT::Array) v.itemized = true; return v; }
        // empty: the METHOD's Failure, not a silent Any (see B["shift"])
        if (!a.empty() && a[0].t == VT::Array) { ValueList none; return I.methodCall(a[0], "pop", none); }
        return Value::any();
    };
    B["shift"] = [](Interpreter& I, ValueList& a) -> Value {
        if (!a.empty() && a[0].t == VT::Array && a[0].ext && std::static_pointer_cast<LazySeqState>(a[0].ext)->infinite) I.materializeLazy(a[0], 1);
        if (!a.empty() && a[0].t == VT::Array && !a[0].arr->empty()) { Value v = a[0].arr->front(); a[0].arr->erase(a[0].arr->begin()); if (v.t == VT::Array) v.itemized = true; return v; }
        // empty: hand off to the METHOD so the sub answers the same Failure
        // (X::Cannot::Empty) instead of a silent Any
        if (!a.empty() && a[0].t == VT::Array) { ValueList none; return I.methodCall(a[0], "shift", none); }
        return Value::any();
    };
    // `flat(…)` delegates to the METHOD, whose recursive walk is the one that
    // stops at an itemized element wherever it sits — `flat 1, (2, $(5,6))` keeps
    // the `$(5,6)` whole, which a one-level flatten of each argument does not.
    // `flat(…)` opens each ARGUMENT one level (an Array counts, so `flat(@a)` is
    // its elements) and then descends through non-itemized sublists — an
    // itemized one stays whole wherever it sits, so `flat 1, (2, $(5,6))` keeps
    // the `$(5,6)`. Delegating wholesale to the METHOD is not the same thing:
    // that rule never opens an Array below the top level, which is what Cro's
    // router walks.
    B["flat"] = [](Interpreter&, ValueList& a) -> Value {
        Value out = Value::array(); out.isList = true;
        std::function<void(const Value&)> deeper = [&](const Value& x) {
            if (x.t == VT::Array && x.arr && x.isList && !x.itemized)
                for (auto& e : *x.arr) deeper(e);
            else if (x.t == VT::Range) for (auto& e : x.flatten()) out.arr->push_back(e);
            else out.arr->push_back(x);
        };
        for (auto& v : a) {
            if (v.itemized) { out.arr->push_back(v); continue; }
            if (v.t == VT::Array && v.arr) { for (auto& e : *v.arr) deeper(e); continue; }
            if (v.t == VT::Range) { for (auto& e : v.flatten()) out.arr->push_back(e); continue; }
            out.arr->push_back(v);
        }
        return out;
    };
    B["cache"] = [](Interpreter&, ValueList& a) -> Value { // cache(list) — like .cache, a no-op for our eager values
        if (a.size() == 1) { if (a[0].t == VT::Range) return Value::array(a[0].flatten()); return a[0]; }
        Value out = Value::array(); out.isList = true;
        for (auto& v : a) out.arr->push_back(v);
        return out;
    };
    B["slip"] = [](Interpreter&, ValueList& a) -> Value { // slip(4,5) spreads into the enclosing list
        Value out = Value::array(); out.isList = true; out.s = "Slip";
        for (auto& v : a) { ValueList l = v.flatten(); for (auto& x : l) out.arr->push_back(x); }
        return out;
    };
    // NB: no B["Slip"] — a bareword `Slip` must stay a type object (Slip.new);
    // the call form Slip(...) routes through the evalCall coercer block.
    B["roundrobin"] = [](Interpreter&, ValueList& a) -> Value {
        // interleave the input lists: round 0 = one from each, round 1 = next, … skipping exhausted lists
        std::vector<ValueList> lists;
        bool slip = false; // `:slip` flattens the rounds into one list
        for (auto& v : a) {
            if (v.t == VT::Pair && v.namedArg) { if (v.s == "slip") slip = !v.pairVal || v.pairVal->truthy(); continue; }
            ValueList l = (v.t == VT::Array || v.t == VT::Range) ? v.flatten() : ValueList{v};
            lists.push_back(l);
        }
        size_t maxLen = 0; for (auto& l : lists) maxLen = std::max(maxLen, l.size());
        Value out = Value::array(); out.isList = true;
        for (size_t i = 0; i < maxLen; i++) {
            if (slip) { for (auto& l : lists) if (i < l.size()) out.arr->push_back(l[i]); continue; }
            Value round = Value::array(); round.isList = true;
            for (auto& l : lists) if (i < l.size()) round.arr->push_back(l[i]);
            out.arr->push_back(round);
        }
        return out;
    };
    // `lazy LIST` / `eager LIST` — rakupp lists are already index-materialised, so
    // both are identity passthroughs (single arg, or a List of the args).
    B["lazy"] = [](Interpreter&, ValueList& a) -> Value {
        if (a.size() == 1) {
            Value v = a[0];
            if (v.t == VT::Range || v.t == VT::Array) v.b = true; // b marks laziness for .is-lazy
            return v;
        }
        Value out = Value::array(); out.isList = true; out.b = true;
        for (auto& v : a) out.arr->push_back(v); return out;
    };
    B["eager"] = [](Interpreter&, ValueList& a) -> Value {
        if (a.size() == 1) return a[0];
        Value out = Value::array(); out.isList = true; for (auto& v : a) out.arr->push_back(v); return out;
    };
    B["hash"] = [](Interpreter&, ValueList& a) -> Value {
        Value h = Value::makeHash();
        ValueList items; // spread list args so hash(<a 1 b 2>) pairs up (and <1 2 3> dies)
        for (auto& v : a) {
            if (v.t == VT::Array && v.arr) for (auto& x : *v.arr) items.push_back(x);
            else if (v.t == VT::Hash && !v.hashKind.size()) { for (auto& kv : *v.hash) (*h.hash)[kv.first] = kv.second; }
            else items.push_back(v);
        }
        for (size_t i = 0; i < items.size(); i++) {
            if (items[i].t == VT::Pair) (*h.hash)[items[i].s] = items[i].pairVal ? *items[i].pairVal : Value::any();
            else if (i + 1 < items.size()) { (*h.hash)[items[i].toStr()] = items[i + 1]; i++; }
            else throw RakuError{Value::typeObj("X::AdHoc"),
                "Odd number of elements found where hash initializer expected: found " +
                std::to_string(items.size()) + " elements, last element seen: " + items[i].toStr()};
        }
        return h;
    };
    // `item($x)` is `$x.item` — a container becomes ONE non-flattening thing
    B["item"] = [](Interpreter&, ValueList& a) -> Value {
        if (a.empty()) return Value::any();
        Value v = a[0];
        if (v.t == VT::Array || v.t == VT::Hash) v.itemized = true;
        return v;
    };
    B["VAR"] = [](Interpreter&, ValueList& a) -> Value { return a.empty() ? Value::any() : a[0]; }; // container introspection: value is its own container
    B["sink"] = [](Interpreter& I, ValueList& a) -> Value {
        // sink EXPR / sink { … }: evaluate for side effects, discard the value
        if (!a.empty() && a[0].t == VT::Code) { ValueList none; I.callCallable(a[0], none); }
        return Value::nil();
    };
    // sub forms that delegate to the same-named method, invocant first
    B["splice"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.empty()) return Value::nil();
        Value inv = a[0]; ValueList rest(a.begin() + 1, a.end());
        return I.methodCall(inv, "splice", rest);
    };
    B["zip"] = [](Interpreter& I, ValueList& a) -> Value { // n-ary zip, like [Z]
        ValueList items;
        Value with;
        for (auto& v : a) {
            if (v.t == VT::Pair && v.namedArg && v.s == "with" && v.pairVal) { with = *v.pairVal; continue; }
            items.push_back(v);
        }
        Value z = I.applyReduce("Z", items);
        if (with.t == VT::Code && z.arr) { // zip(:with(&f)) folds each tuple with &f
            Value out = Value::array(); out.isList = true;
            for (auto& t : *z.arr) {
                ValueList parts = t.t == VT::Array && t.arr ? *t.arr : ValueList{t};
                Value acc = parts.empty() ? Value::any() : parts[0];
                for (size_t k = 1; k < parts.size(); k++) acc = I.callCallable(with, {acc, parts[k]});
                out.arr->push_back(acc);
            }
            return out;
        }
        return z;
    };
    B["classify"] = [](Interpreter& I, ValueList& a) -> Value {
        // `:into(%h)` classifies into an existing hash, APPENDING to its lists.
        Value* into = nullptr; ValueList pos, named;
        for (auto& x : a) {
            if (x.t == VT::Pair && x.s == "into" && x.pairVal) into = x.pairVal.get();
            else if (x.t == VT::Pair && x.namedArg) named.push_back(x); // `:as` and friends
            else pos.push_back(x);
        }
        if (pos.size() < 2) return into ? *into : Value::makeHash();
        Value mapper = pos[0];
        Value list = pos.size() == 2 ? pos[1] : Value::array(ValueList(pos.begin() + 1, pos.end()));
        ValueList ma{mapper};
        for (auto& nv : named) ma.push_back(nv);
        Value res = I.methodCall(list, "classify", ma);
        if (!into) return res;
        if (into->t != VT::Hash || !into->hash) *into = Value::makeHash();
        if (res.hash) for (auto& kv : *res.hash) { // append the grouped elements
            auto it = into->hash->find(kv.first);
            if (it == into->hash->end()) (*into->hash)[kv.first] = kv.second;
            else if (it->second.t == VT::Array && kv.second.t == VT::Array)
                for (auto& e : *kv.second.arr) it->second.arr->push_back(e);
        }
        return *into;
    };
    // sub forms of the mapper family: routine(&code, list) == list.routine(&code)
    for (const char* mf : {"categorize", "deepmap", "duckmap", "nodemap"}) {
        std::string mname = mf;
        B[mname] = [mname](Interpreter& I, ValueList& a) -> Value {
            // the ADVERBS (`:as`, `:into`) are not list elements — they ride
            // through to the method, which is where they mean something
            ValueList pos, named;
            for (auto& v : a) { if (v.t == VT::Pair && v.namedArg) named.push_back(v); else pos.push_back(v); }
            if (pos.size() < 2) return Value::array();
            Value list = pos.size() == 2 ? pos[1] : Value::array(ValueList(pos.begin() + 1, pos.end()));
            ValueList ma{pos[0]};
            for (auto& nv : named) ma.push_back(nv);
            return I.methodCall(list, mname, ma);
        };
    }
    B["quietly"] = [](Interpreter& I, ValueList& a) -> Value { // suppress warn() output; run block/return arg
        if (!a.empty() && a[0].t == VT::Code) {
            I.quietDepth_++;
            try { Value r = I.callCallable(a[0], {}); I.quietDepth_--; return r; }
            catch (...) { I.quietDepth_--; throw; }
        }
        return a.empty() ? Value::any() : a[0];
    };
    B["make-temp-file"] = [](Interpreter&, ValueList& a) -> Value {
        static long long ctr = 0; ctr++;
        std::string base = "/tmp/rakupp-tmp-" + std::to_string((long long)getpid()) + "-" + std::to_string(ctr);
        std::string content;
        for (auto& x : a) if (x.t == VT::Pair && x.s == "content") content = x.pairVal ? x.pairVal->toStr() : "";
        { std::ofstream out(base); out << content; }
        Value p = Value::str(base); p.hashKind = "IO"; return p;
    };
    B["make-temp-dir"] = [](Interpreter&, ValueList&) -> Value {
        static long long ctr = 0; ctr++;
        std::string base = "/tmp/rakupp-tmpdir-" + std::to_string((long long)getpid()) + "-" + std::to_string(ctr);
        ::mkdir(base.c_str(), 0700);
        Value p = Value::str(base); p.hashKind = "IO"; return p;
    };
    // `start` runs the block on a real worker thread, then cooperatively yields
    // the GIL until the worker reaches its first blocking point (or finishes). A
    // pure-compute block therefore runs to completion right away (its effects are
    // visible immediately, as under the old eager model), while a block that
    // sleeps/awaits releases the GIL and keeps running concurrently — which is
    // what lets genuinely-timed programs (sleep-sort) interleave. A thrown
    // exception becomes a Broken promise, rethrown at await.
    B["start"] = [](Interpreter& I, ValueList& a) -> Value {
        Value code; for (auto& x : a) if (x.t == VT::Code) code = x;
        if (code.t != VT::Code) { // `start VALUE` — an already-kept promise of the value
            auto ps = std::make_shared<PromiseState>(); ps->done = true; ps->result = a.empty() ? Value::any() : a[0];
            Value p = Value::makeHash(); p.hashKind = "Promise"; p.ext = ps;
            (*p.hash)["status"] = Value::str("Kept"); (*p.hash)["result"] = ps->result;
            return p;
        }
        Value p = I.spawnPromise(code);
        I.yieldToWorker();
        return p;
    };
    // NativeCall helpers: size of a native type; cglobal is a stub (0)
    B["nativesizeof"] = [](Interpreter&, ValueList& a) -> Value {
        std::string t = a.empty() ? "" : a[0].t == VT::Type ? a[0].s : a[0].toStr();
        long long sz = (t == "int8" || t == "uint8" || t == "byte" || t == "bool") ? 1
                     : (t == "int16" || t == "uint16") ? 2
                     : (t == "int64" || t == "uint64" || t == "long" || t == "longlong" ||
                        t == "num64" || t == "size_t" || t == "ssize_t" || t == "Pointer") ? 8
                     : (t == "num32" || t == "int32" || t == "uint32" || t == "int" || t == "uint") ? 4 : 8;
        return Value::integer(sz);
    };
    // parameterized native type name: `CArray[uint8]` is Type{s="CArray",
    // ofType="uint8"} — rebuild the "Name[elem]" string the FFI helpers expect.
    auto ncTypeName = [](const Value& v) -> std::string {
        if (v.t != VT::Type) return v.toStr();
        return (!v.ofType.empty() && v.s.find('[') == std::string::npos) ? v.s + "[" + v.ofType + "]" : v.s;
    };
    B["cglobal"] = [ncTypeName](Interpreter& I, ValueList& a) -> Value {
        std::string lib  = a.size() > 0 ? (a[0].t == VT::Type ? a[0].s : a[0].toStr()) : "";
        std::string sym  = a.size() > 1 ? a[1].toStr() : "";
        std::string type = a.size() > 2 ? ncTypeName(a[2]) : "Pointer";
        return I.cglobal(lib, sym, type);
    };
    // nativecast(TargetType, $value): reinterpret a native pointer/handle as another
    // native type (Pointer, CArray[T], or a CStruct/CPointer class).
    B["nativecast"] = [ncTypeName](Interpreter& I, ValueList& a) -> Value {
        if (a.size() < 2) return Value::any();
        std::string t = ncTypeName(a[0]);
        long long addr = Interpreter::ncRawAddr(a[1]);
        if (t == "Pointer" || t.rfind("Pointer[", 0) == 0) return I.ncMakePointer(t, (void*)(intptr_t)addr);
        if (t == "CArray"  || t.rfind("CArray[", 0)  == 0) return I.ncMakeLiveCArray(t, (void*)(intptr_t)addr);
        if (t == "Str") return Value::str(addr ? std::string((const char*)(intptr_t)addr) : "");
        // a CStruct/CPointer class: box the address as an object of that class
        auto it = I.classes_.find(t);
        if (it != I.classes_.end()) {
            Value o; o.t = VT::Object; o.obj = std::make_shared<ObjectData>();
            o.obj->cls = it->second; o.obj->attrs["__native_ptr"] = Value::integer(addr);
            return o;
        }
        return Value::integer(addr);
    };
    B["await"] = [](Interpreter& I, ValueList& a) -> Value {
        // resolve a Promise, running any pending Proc::Async work (with the timeout from an anyof timer)
        std::function<Value(Value&)> resolve = [&](Value& p) -> Value {
            // `await` a Supply drains it and yields its LAST emitted value
            if (p.t == VT::Hash && p.hashKind == "Supply" && p.hash->count("values")) {
                auto& vals = *(*p.hash)["values"].arr;
                return vals.empty() ? Value::any() : vals.back();
            }
            if (p.t != VT::Hash || p.hashKind != "Promise") return p;
            // PromiseState-backed promise (start / spawnPromise): block until it
            // settles, rethrowing the cause if it was broken.
            if (p.ext) {
                auto ps = std::static_pointer_cast<PromiseState>(p.ext);
                I.awaitPromise(ps);
                if (ps->broken)
                    throw RakuError{ ps->cause, ps->causeMsg.empty() ? std::string("Promise broken") : ps->causeMsg };
                return ps->result;
            }
            std::string kind = p.hash->count("kind") ? (*p.hash)["kind"].toStr() : "";
            if (kind == "anyof" || kind == "allof") {
                double timeout = 0; Value* procP = nullptr;
                std::vector<std::shared_ptr<PromiseState>> pss; // start/spawn promises in the combinator
                std::vector<Value*> psvals;
                if (p.hash->count("promises")) for (auto& q : *(*p.hash)["promises"].arr) {
                    if (q.t == VT::Hash && q.hashKind == "Promise") {
                        std::string k = q.hash->count("kind") ? (*q.hash)["kind"].toStr() : "";
                        if (k == "timer") timeout = (*q.hash)["seconds"].toNum();
                        else if (k == "proc") procP = &q;
                        else if (q.ext) { pss.push_back(std::static_pointer_cast<PromiseState>(q.ext)); psvals.push_back(&q); }
                    }
                }
                if (procP) I.runProcPromise(*procP, timeout);
                // WAIT for the member start-promises — anyof: until ANY settles (a
                // timer member is the deadline); allof: until EVERY one settles.
                // (They were ignored before, so `await Promise.anyof: $todo, $time-up`
                // returned at t=0 with $todo still Planned — zef's fetch timeout wrap.)
                if (!pss.empty()) {
                    auto anyDone = [&]() {
                        for (auto& ps : pss) { std::lock_guard<std::mutex> lk(ps->m); if (ps->done) return true; }
                        return false;
                    };
                    if (kind == "allof") {
                        for (auto& ps : pss) I.awaitPromise(ps);
                    } else {
                        auto deadline = std::chrono::steady_clock::now() +
                            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                std::chrono::duration<double>(timeout > 0 ? timeout : 3600));
                        while (!anyDone() && std::chrono::steady_clock::now() < deadline)
                            I.sleepYield(0.01); // GIL released so the workers can run
                    }
                    // reflect settled members onto their hashes so `.so`/`.status` read true
                    for (size_t i = 0; i < pss.size(); i++) {
                        std::lock_guard<std::mutex> lk(pss[i]->m);
                        if (pss[i]->done && psvals[i]->hash) {
                            (*psvals[i]->hash)["status"] = Value::str(pss[i]->broken ? "Broken" : "Kept");
                            if (!pss[i]->broken) (*psvals[i]->hash)["result"] = pss[i]->result;
                        }
                    }
                }
                (*p.hash)["status"] = Value::str("Kept");
                return p;
            }
            if (kind == "proc") { I.runProcPromise(p, 0); return p; }
            auto it = p.hash->find("result"); return it != p.hash->end() ? it->second : p; // plain/old-style
        };
        if (a.size() == 1 && a[0].t == VT::Array) {
            Value out = Value::array(); out.isList = true;
            for (auto& x : *a[0].arr) out.arr->push_back(resolve(x));
            return out;
        }
        if (a.size() == 1) return resolve(a[0]);
        Value out = Value::array(); out.isList = true;
        for (auto& x : a) out.arr->push_back(resolve(x));
        return out;
    };
    // set()/bag()/mix() flatten iterable args, but an itemized `$[...]` stays one
    // element, and a Pair arg is an ELEMENT (pair→count is only for coercions)
    auto settyArgs = [](ValueList& args) {
        ValueList out;
        for (auto& a : args) {
            if ((a.t == VT::Array || a.t == VT::Range) && !a.itemized) {
                ValueList sub = a.flatten();
                out.insert(out.end(), sub.begin(), sub.end());
            }
            else if (a.t == VT::Hash && !a.itemized && a.hash &&
                     (a.hashKind.empty() || a.hashKind == "Map")) {
                // a plain Hash contributes its pairs (a quanthash stays ONE element)
                for (auto& kv : *a.hash) {
                    Value p = Value::pair(kv.first, kv.second);
                    p.pairKey = kv.second.pairKey;
                    out.push_back(p);
                }
            }
            else out.push_back(a);
        }
        return out;
    };
    B["set"] = [settyArgs](Interpreter&, ValueList& a) -> Value { ValueList i = settyArgs(a); return makeBaggy(i, "Set", true); };
    B["bag"] = [settyArgs](Interpreter&, ValueList& a) -> Value { ValueList i = settyArgs(a); return makeBaggy(i, "Bag", true); };
    B["mix"] = [settyArgs](Interpreter&, ValueList& a) -> Value { ValueList i = settyArgs(a); return makeBaggy(i, "Mix", true); };
    // `list(…)` builds a List of its ARGUMENTS — it does not flatten them, so
    // `list((1,2),(3,4))` has two elements. The one-arg rule still applies: a
    // lone Positional spreads, unless it is ITEMIZED (`$(1,2)` stays one thing).
    B["list"] = [](Interpreter&, ValueList& a) -> Value {
        Value out = Value::array(); out.isList = true;
        if (a.size() == 1 && (a[0].t == VT::Array || a[0].t == VT::Range) && !a[0].itemized) {
            for (auto& x : toList(a[0])) out.arr->push_back(x);
            return out;
        }
        for (auto& v : a) out.arr->push_back(v);
        return out;
    };
    B["unshift"] = [](Interpreter&, ValueList& a) -> Value {
        if (!a.empty() && a[0].t == VT::Array) { for (size_t i = a.size(); i > 1; i--) a[0].arr->insert(a[0].arr->begin(), a[i - 1]); return Value::integer((long long)a[0].arr->size()); }
        return Value::any();
    };
}


// ---- nqp buffer read/write helpers -----------------------------------------
// MoarVM encodes (read|write)(u)int/num's last argument as size|endian: the low
// two bits pick the byte order (Endian enum — Native 0 / Little 1 / Big 2) and
// bits 2+ hold a size code, giving 1<<(flag>>2) bytes. See CBOR::Simple's $ne8/
// $be16/$be32/$be64 flags (nqp::bitor_i(BINARY_SIZE_*, Endian)).
static const bool g_hostLittle = [] { uint16_t x = 1; return *(uint8_t*)&x == 1; }();
static inline bool binFlagLittle(int e) { return e == 1 || (e == 0 && g_hostLittle); }
static inline void decodeBinFlag(long long flag, int& nbytes, int& endian) {
    endian = (int)(flag & 3);
    int code = (int)((flag >> 2) & 7);
    nbytes = 1 << code;                 // 0→1, 1→2, 2→4, 3→8
}
// kind: 'u' unsigned int, 'i' signed int, 'n' float/double
static void nqpBufWrite(std::string& bytes, long long off, const Value& val,
                        int nbytes, int endian, char kind) {
    if (off < 0) return;
    if ((long long)bytes.size() < off + nbytes) bytes.resize(off + nbytes, '\0');
    unsigned char raw[8] = {0};
    if (kind == 'n') {
        if (nbytes == 4) { float f = (float)val.toNum(); std::memcpy(raw, &f, 4); }
        else            { double d = val.toNum();        std::memcpy(raw, &d, 8); }
    } else {
        unsigned long long u = (unsigned long long)val.toInt();
        std::memcpy(raw, &u, nbytes <= 8 ? nbytes : 8);
    }
    for (int i = 0; i < nbytes; i++) {
        int src = (binFlagLittle(endian) == g_hostLittle) ? i : nbytes - 1 - i;
        bytes[off + i] = (char)raw[src];
    }
}
static Value nqpBufRead(const std::string& bytes, long long off,
                        int nbytes, int endian, char kind) {
    unsigned char raw[8] = {0};
    for (int i = 0; i < nbytes; i++) {
        long long p = off + i;
        if (p < 0 || p >= (long long)bytes.size()) continue;
        int dst = (binFlagLittle(endian) == g_hostLittle) ? i : nbytes - 1 - i;
        raw[dst] = (unsigned char)bytes[p];
    }
    if (kind == 'n') {
        if (nbytes == 4) { float f;  std::memcpy(&f, raw, 4); return Value::number((double)f); }
        double d; std::memcpy(&d, raw, 8); return Value::number(d);
    }
    unsigned long long u = 0; std::memcpy(&u, raw, nbytes <= 8 ? nbytes : 8);
    if (kind == 'i') { // sign-extend from nbytes
        if (nbytes < 8 && (u & (1ULL << (nbytes * 8 - 1)))) u |= ~((1ULL << (nbytes * 8)) - 1);
        return Value::integer((long long)u);
    }
    if (nbytes == 8 && (u >> 63)) { // uint64 beyond long long
        BigInt b((long long)(u & 0x7FFFFFFFFFFFFFFFULL));
        return Value::bigint(b + BigInt(2).pow(63));
    }
    return Value::integer((long long)u);
}

// ---- the `use nqp` compatibility subset ------------------------------------
// Reached only through NK::NqpOp nodes, which exist only in units that said
// `use nqp` — every other program pays nothing for any of this.
// Docs: docs/dev/MODULE-FINDINGS.md #4b. Int ops are plain int64; string ops
// are codepoint-indexed; comparisons return Int 1/0 (nqp truthiness).
Value Interpreter::evalNqpOp(NqpOp* n) {
    using O = NqpOpc;
    auto& a = n->args;
    // lazy forms first: they control their own argument evaluation
    switch (n->op) {
        case O::Stmts: {
            Value last = Value::nil();
            for (auto& e : a) {
                last = eval(e.get());
                // a cooperative return/last/next inside the sequence (no callable
                // boundary) sets a flag rather than throwing — stop evaluating
                // the rest and let it propagate (JSON::Fast's parse loops `return`
                // out of nqp::while(1, nqp::stmts(…)))
                if (tctx_.returning || tctx_.loopCtl) return last;
            }
            return last;
        }
        case O::While:
        case O::Until: {
            if (a.size() < 2) return Value::nil();
            long long guard = 0;
            while (boolify(eval(a[0].get())) == (n->op == O::While)) {
                if (tctx_.returning) return Value::nil();
                for (size_t i = 1; i < a.size(); i++) {
                    eval(a[i].get());
                    if (tctx_.returning) return Value::nil(); // cooperative return escapes
                    if (tctx_.loopCtl == 2) { tctx_.loopCtl = 0; return Value::nil(); } // last
                    if (tctx_.loopCtl == 1) { tctx_.loopCtl = 0; break; }               // next
                }
                if (++guard > 1000000000LL) break; // runaway backstop
            }
            return Value::nil();
        }
        case O::IfNull: {
            Value v = eval(a[0].get());
            if (v.t == VT::Nil || v.t == VT::Any) return a.size() > 1 ? eval(a[1].get()) : Value::nil();
            return v;
        }
        // nqp::bindattr(@container, T, '$!reified'/'$!storage', $buffer) rebinds
        // the container's BACKING STORE to the buffer — they must then SHARE it
        // (pushes to the buffer show through the container). Needs the caller's
        // lvalue: the container's shared_ptr is repointed at the buffer's, so
        // both alias one vector/map. (Value-copy semantics can't express this.)
        case O::Bindattr:
        case O::P6BindAttrInvRes: {
            if (a.size() >= 4) {
                std::string an = eval(a[2].get()).toStr();
                if (an == "$!reified" || an == "$!storage" || an == "$!array") {
                    Value* lv = nullptr;
                    try { lv = lvalue(a[0].get()); } catch (RakuError&) {}
                    Value buf = eval(a[3].get());
                    if (lv) {
                        if (buf.t == VT::Array || lv->t == VT::Array) {
                            if (lv->t != VT::Array) *lv = Value::array();
                            if (buf.t == VT::Array && buf.arr) lv->arr = buf.arr; // SHARE
                        } else if (buf.t == VT::Hash) {
                            lv->t = VT::Hash; lv->hash = buf.hash;               // SHARE
                        }
                        return n->op == O::P6BindAttrInvRes ? *lv : buf;
                    }
                }
            }
            break; // ordinary attr bind — fall through to the eager path
        }
        // Buffer writes mutate argument 0 in place, so they need its lvalue.
        case O::WriteUInt: case O::WriteInt: case O::WriteNum: {
            if (a.size() >= 4) {
                Value* lv = nullptr;
                try { lv = lvalue(a[0].get()); } catch (RakuError&) {}
                long long off  = eval(a[1].get()).toInt();
                Value     val  = eval(a[2].get());
                long long flag = eval(a[3].get()).toInt();
                int nb, en; decodeBinFlag(flag, nb, en);
                char kind = n->op == O::WriteNum ? 'n' : (n->op == O::WriteInt ? 'i' : 'u');
                if (lv) {
                    if (lv->t != VT::Str) { lv->t = VT::Str; lv->s.clear(); }
                    if (lv->hashKind.empty()) lv->hashKind = "Buf";
                    nqpBufWrite(lv->s, off, val, nb, en, kind);
                }
                return val;
            }
            break;
        }
        case O::SetElems: { // resize a buf (bytes) or array (elems) in place
            if (a.size() >= 2) {
                Value* lv = nullptr;
                try { lv = lvalue(a[0].get()); } catch (RakuError&) {}
                long long nn = eval(a[1].get()).toInt();
                if (nn < 0) nn = 0;
                if (lv) {
                    if (lv->t == VT::Str) lv->s.resize(nn * lv->blobElemSize(), '\0');
                    else if (lv->t == VT::Array && lv->arr) lv->arr->resize(nn, Value::number(0));
                }
                return lv ? *lv : Value::nil();
            }
            break;
        }
        case O::BindposN: { // native-num element store
            if (a.size() >= 3) {
                Value* lv = nullptr;
                try { lv = lvalue(a[0].get()); } catch (RakuError&) {}
                long long idx = eval(a[1].get()).toInt();
                Value val = eval(a[2].get());
                if (lv && lv->t == VT::Array && lv->arr && idx >= 0) {
                    if ((long long)lv->arr->size() <= idx) lv->arr->resize(idx + 1, Value::number(0));
                    (*lv->arr)[idx] = Value::number(val.toNum());
                }
                return val;
            }
            break;
        }
        case O::Splice: { // nqp::splice(target, source, offset, count)
            // Buf/Blob bytes live in Value::s by value (not shared like arrays),
            // so a byte-buffer splice must mutate through the lvalue. Arrays share
            // their backing vector, so they fall through to the eager path.
            Value src0 = a.size() > 0 ? eval(a[0].get()) : Value::nil();
            if (src0.t == VT::Str) {
                Value* lv = nullptr;
                try { lv = lvalue(a[0].get()); } catch (RakuError&) {}
                Value    src = a.size() > 1 ? eval(a[1].get()) : Value::str("");
                long long off = a.size() > 2 ? eval(a[2].get()).toInt() : 0;
                long long cnt = a.size() > 3 ? eval(a[3].get()).toInt() : 0;
                if (lv) {
                    std::string& t = lv->s;
                    if (off < 0) off = 0;
                    if (off > (long long)t.size()) t.resize(off, '\0');
                    if (cnt < 0 || off + cnt > (long long)t.size()) cnt = t.size() - off;
                    t.replace(off, cnt, src.s);
                }
                return lv ? *lv : src0;
            }
            break; // array splice: eager path (shared backing)
        }
        default: break;
    }
    ValueList v;
    v.reserve(a.size());
    for (auto& e : a) v.push_back(eval(e.get()));
    return rtNqpOp(n->op, v); // eager leaf ops — shared with native codegen
}

// The eager (non-control) nqp ops, operating on ALREADY-evaluated arguments.
// Free function so native `--exe` codegen can call it directly: the lazy control
// forms (Stmts/While/Until/IfNull) are emitted as native C++ by the codegen and
// nqp::if/unless are Ternaries, so only these leaf ops need a runtime entry.
Value rtNqpOp(NqpOpc op, ValueList& v) {
    using O = NqpOpc;
    auto I = [&](size_t i) -> long long { return i < v.size() ? v[i].toInt() : 0; };
    auto S = [&](size_t i) -> std::string { return i < v.size() ? v[i].toStr() : std::string(); };
    auto cclassHas = [](long long mask, uint32_t cp) -> bool {
        // masks follow Parser::nqpConstValue; only the classes real modules use
        const std::string cat = uniGeneralCategory(cp);
        bool digit = cat == "Nd";
        bool alpha = !cat.empty() && cat[0] == 'L';
        bool space = cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' ||
                     cat == "Zs" || cat == "Zl" || cat == "Zp";
        if ((mask & 8) && digit) return true;                      // NUMERIC
        if ((mask & 4) && alpha) return true;                      // ALPHABETIC
        if ((mask & 32) && space) return true;                     // WHITESPACE
        if ((mask & 2048) && (digit || alpha)) return true;        // ALPHANUMERIC
        if ((mask & 8192) && (digit || alpha || cp == '_')) return true; // WORD
        if ((mask & 4096) && (cp == '\n' || cp == '\r')) return true;    // NEWLINE
        if ((mask & 16) && ((cp >= '0' && cp <= '9') || (cp >= 'a' && cp <= 'f') ||
                            (cp >= 'A' && cp <= 'F'))) return true;      // HEXADECIMAL
        return false;
    };
    switch (op) {
        case O::IseqI: return Value::integer(I(0) == I(1));
        case O::IsneI: return Value::integer(I(0) != I(1));
        case O::IsltI: return Value::integer(I(0) <  I(1));
        case O::IsleI: return Value::integer(I(0) <= I(1));
        case O::IsgeI: return Value::integer(I(0) >= I(1));
        case O::IsgtI: return Value::integer(I(0) >  I(1));
        case O::AddI:  return Value::integer(I(0) + I(1));
        case O::SubI:  return Value::integer(I(0) - I(1));
        case O::MulI:  return Value::integer(I(0) * I(1));
        case O::BitandI: return Value::integer(I(0) & I(1));
        case O::BitorI:  return Value::integer(I(0) | I(1));
        case O::BitxorI: return Value::integer(I(0) ^ I(1));
        case O::BitshiftlI: return Value::integer(I(0) << I(1));
        case O::BitshiftrI: return Value::integer(I(0) >> I(1)); // arithmetic (signed)
        case O::Ordat: {
            auto cps = utf8cp(S(0));
            long long i = I(1);
            return Value::integer(i >= 0 && i < (long long)cps.size() ? (long long)cps[i] : -1);
        }
        case O::Eqat: {
            auto h = utf8cp(S(0)), nd = utf8cp(S(1));
            long long at = I(2);
            if (at < 0 || at + (long long)nd.size() > (long long)h.size()) return Value::integer(0);
            for (size_t k = 0; k < nd.size(); k++)
                if (h[at + k] != nd[k]) return Value::integer(0);
            return Value::integer(1);
        }
        case O::Substr: {
            auto cps = utf8cp(S(0));
            long long from = I(1);
            long long len = v.size() > 2 ? I(2) : (long long)cps.size() - from;
            if (from < 0) from = 0;
            if (from > (long long)cps.size()) from = cps.size();
            if (len < 0 || from + len > (long long)cps.size()) len = cps.size() - from;
            std::string out;
            for (long long k = from; k < from + len; k++) out += cpToU8(cps[k]);
            return Value::str(out);
        }
        case O::Chars: return Value::integer((long long)utf8cp(S(0)).size());
        case O::Concat: return Value::str(S(0) + S(1));
        case O::Join: {
            std::string sep = S(0), out;
            if (v.size() > 1 && v[1].t == VT::Array && v[1].arr) {
                bool first = true;
                for (auto& e : *v[1].arr) { if (!first) out += sep; out += e.toStr(); first = false; }
            }
            return Value::str(out);
        }
        case O::Index: {
            auto h = utf8cp(S(0)), nd = utf8cp(S(1));
            long long from = v.size() > 2 ? I(2) : 0;
            if (from < 0) from = 0;
            if (nd.empty()) return Value::integer(from <= (long long)h.size() ? from : -1);
            for (long long at = from; at + (long long)nd.size() <= (long long)h.size(); at++) {
                bool ok = true;
                for (size_t k = 0; k < nd.size() && ok; k++) ok = h[at + k] == nd[k];
                if (ok) return Value::integer(at);
            }
            return Value::integer(-1);
        }
        case O::Chr: return Value::str(cpToU8((uint32_t)I(0)));
        case O::StrFromCodes: {
            std::string out;
            if (!v.empty() && v[0].t == VT::Array && v[0].arr)
                for (auto& e : *v[0].arr) out += cpToU8((uint32_t)e.toInt());
            return Value::str(out);
        }
        case O::StrToCodes: {
            // (str, NORMALIZE_* const, target-list) — fills target, returns it
            auto cps = utf8cp(S(0));
            long long nm = I(1); // our const values: 1 NFC, 2 NFD, 3 NFKC, 4 NFKD
            int mode = nm == 1 ? 1 : nm == 2 ? 0 : nm == 3 ? 3 : nm == 4 ? 2 : -1;
            if (mode >= 0) cps = uniNormalize(cps, mode);
            Value target = v.size() > 2 ? v[2] : Value::array();
            if (target.t != VT::Array || !target.arr) target = Value::array();
            target.arr->clear();
            for (auto cp : cps) target.arr->push_back(Value::integer((long long)cp));
            return target;
        }
        case O::FindNotCClass: {
            auto cps = utf8cp(S(1));
            long long mask = I(0), start = I(2), len = I(3);
            long long end = std::min<long long>(start + len, (long long)cps.size());
            for (long long k = std::max<long long>(start, 0); k < end; k++)
                if (!cclassHas(mask, cps[k])) return Value::integer(k);
            return Value::integer(end);
        }
        case O::IsCClass: {
            auto cps = utf8cp(S(1));
            long long i = I(2);
            return Value::integer(i >= 0 && i < (long long)cps.size() &&
                                  cclassHas(I(0), cps[i]) ? 1 : 0);
        }
        case O::List: case O::ListI: case O::ListS: {
            Value out = Value::array();
            for (auto& x : v) out.arr->push_back(x);
            return out;
        }
        case O::Elems:
            if (v[0].t == VT::Str) return Value::integer(v[0].blobElems()); // Buf/Blob byte count
            return Value::integer(v[0].t == VT::Array && v[0].arr ? (long long)v[0].arr->size()
                                 : v[0].t == VT::Hash && v[0].hash ? (long long)v[0].hash->size() : 0);
        case O::Atpos: case O::AtposI: {
            long long i = I(1);
            if (v[0].t == VT::Array && v[0].arr && i >= 0 && i < (long long)v[0].arr->size())
                return (*v[0].arr)[i];
            if (v[0].t == VT::Str && i >= 0 && i < v[0].blobElems())  // Buf/Blob byte
                return Value::integer(v[0].blobWordAt(i));
            return op == O::AtposI ? Value::integer(0) : Value::nil();
        }
        case O::Bindpos: case O::BindposI: {
            if (v[0].t == VT::Array && v[0].arr) {
                long long i = I(1);
                while ((long long)v[0].arr->size() <= i) v[0].arr->push_back(Value::nil());
                (*v[0].arr)[i] = v[2];
            }
            return v.size() > 2 ? v[2] : Value::nil();
        }
        case O::Push: case O::PushI: case O::PushS:
            if (v[0].t == VT::Array && v[0].arr) v[0].arr->push_back(v[1]);
            return v[1];
        case O::PopS: {
            if (v[0].t == VT::Array && v[0].arr && !v[0].arr->empty()) {
                Value r = v[0].arr->back(); v[0].arr->pop_back(); return r;
            }
            return Value::nil();
        }
        case O::ShiftI: {
            if (v[0].t == VT::Array && v[0].arr && !v[0].arr->empty()) {
                Value r = v[0].arr->front(); v[0].arr->erase(v[0].arr->begin()); return r;
            }
            return Value::integer(0);
        }
        case O::Splice: {
            // nqp::splice(target, source, offset, count) — replace in place
            if (v[0].t == VT::Array && v[0].arr) {
                long long off = I(2), cnt = I(3);
                auto& t = *v[0].arr;
                if (off < 0) off = 0;
                if (off > (long long)t.size()) off = t.size();
                if (cnt < 0 || off + cnt > (long long)t.size()) cnt = t.size() - off;
                t.erase(t.begin() + off, t.begin() + off + cnt);
                if (v[1].t == VT::Array && v[1].arr)
                    t.insert(t.begin() + off, v[1].arr->begin(), v[1].arr->end());
            }
            return v[0];
        }
        case O::Hash: {
            Value h = Value::makeHash();
            for (size_t k = 0; k + 1 < v.size(); k += 2) (*h.hash)[v[k].toStr()] = v[k + 1];
            return h;
        }
        case O::Bindkey:
            if (v[0].t == VT::Hash && v[0].hash) (*v[0].hash)[S(1)] = v[2];
            return v.size() > 2 ? v[2] : Value::nil();
        case O::Create: {
            std::string tn = v[0].t == VT::Type ? v[0].s : v[0].typeName();
            if (tn == "Map" || tn == "Hash" || tn == "IterationMap") return Value::makeHash();
            if (tn == "List") { Value r = Value::array(); r.isList = true; return r; }
            return Value::array(); // IterationBuffer / NFD / Uni / … — a plain buffer
        }
        case O::Istype: {
            std::string tn = v[1].t == VT::Type ? v[1].s : v[1].typeName();
            return Value::integer(rtTypeMatch(v[0], tn) ? 1 : 0);
        }
        case O::Getattr: {
            const std::string& nm = S(2);
            // '$!reified' / '$!storage' name the container's own backing store
            if (v[0].t == VT::Array || v[0].t == VT::Hash) return v[0];
            if (v[0].t == VT::Object && v[0].obj) {
                std::string bare = nm.size() > 2 ? nm.substr(2) : nm;
                auto it = v[0].obj->attrs.find(bare);
                if (it != v[0].obj->attrs.end()) return it->second;
            }
            return Value::nil();
        }
        case O::Bindattr: case O::P6BindAttrInvRes: {
            const std::string& nm = S(2);
            if ((v[0].t == VT::Array && v[3].t == VT::Array && v[0].arr && v[3].arr)) {
                *v[0].arr = *v[3].arr;             // rebind the backing buffer
            } else if (v[0].t == VT::Hash && v[3].t == VT::Hash && v[0].hash && v[3].hash) {
                *v[0].hash = *v[3].hash;
            } else if (v[0].t == VT::Object && v[0].obj) {
                std::string bare = nm.size() > 2 ? nm.substr(2) : nm;
                v[0].obj->attrs[bare] = v[3];
            }
            return op == O::P6BindAttrInvRes ? v[0] : v[3];
        }
        case O::P6ScalarWithValue:
            return v.size() > 1 ? v[1] : Value::nil(); // container wrap is a no-op for us
        case O::Null: return Value::nil();
        case O::IsNanOrInf: {
            double d = v.empty() ? 0 : v[0].toNum();
            return Value::integer(std::isnan(d) || std::isinf(d) ? 1 : 0);
        }
        // num comparisons (NaN != NaN falls out of C++ float semantics)
        case O::IseqN: return Value::integer(v.size() > 1 && v[0].toNum() == v[1].toNum() ? 1 : 0);
        case O::IsneN: return Value::integer(v.size() > 1 && v[0].toNum() != v[1].toNum() ? 1 : 0);
        case O::AtposN: { // native-num element read
            if (!v.empty() && v[0].t == VT::Array && v[0].arr) {
                long long idx = I(1);
                if (idx >= 0 && idx < (long long)v[0].arr->size())
                    return Value::number((*v[0].arr)[idx].toNum());
            }
            return Value::number(0);
        }
        // buffer reads: (buf, offset, size|endian-flag)
        case O::ReadUInt: case O::ReadInt: case O::ReadNum: {
            if (v.empty() || v[0].t != VT::Str) return Value::integer(0);
            int nb, en; decodeBinFlag(I(2), nb, en);
            char kind = op == O::ReadNum ? 'n' : (op == O::ReadInt ? 'i' : 'u');
            return nqpBufRead(v[0].s, I(1), nb, en, kind);
        }
        case O::Slice: { // nqp::slice(buf, from, to) — inclusive byte range → Buf
            Value out = Value::str(""); out.hashKind = "Buf";
            if (!v.empty() && v[0].t == VT::Str) {
                long long from = I(1), to = I(2), len = (long long)v[0].s.size();
                if (from < 0) from = 0;
                if (to >= len) to = len - 1;
                if (to >= from) out.s = v[0].s.substr(from, to - from + 1);
            }
            return out;
        }
        case O::Decode: // nqp::decode(buf, 'utf8') — bytes → Str (rakupp strings are UTF-8)
            return Value::str(v.empty() ? std::string() : v[0].s);
        case O::AddBigI: { // nqp::add_I(a, b, Int) — bignum-safe add
            if (!v.empty() && (v[0].big || (v.size() > 1 && v[1].big))) {
                BigInt a = v[0].big ? *v[0].big : BigInt(v[0].toInt());
                BigInt b = (v.size() > 1) ? (v[1].big ? *v[1].big : BigInt(v[1].toInt())) : BigInt(0);
                BigInt r = a + b;
                return r.fitsLL() ? Value::integer(r.toLL()) : Value::bigint(r);
            }
            return Value::integer(I(0) + I(1));
        }
        case O::Decont: return v.empty() ? Value::nil() : v[0];        // container strip = identity
        case O::P6BoxS: return Value::str(v.empty() ? std::string() : v[0].toStr());
        default: break;
    }
    throw RakuError{Value::typeObj("X::NYI"), "nqp op not implemented in this build"};
}

} // namespace rakupp
