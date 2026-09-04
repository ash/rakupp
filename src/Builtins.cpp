#include "CNumeric.h"
#include "AsciiCtype.h"
#include "Interpreter.h"
#include "Lexer.h"
#include "Parser.h"
#if !defined(_WIN32)
#include <sys/resource.h>
#endif
#include <cstdint>
#include <climits>
#include <limits>
#include <memory>
#include <cstdlib>
#include "Unicode.h"
#include "Pod.h"
#include <complex>
#include <functional>
#include "Regex.h"
#include "MethodName.h"
#include "BuiltinsShared.h"
#include "BuildInfo.h"
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
#include <sys/file.h>   // flock (rakupp-repo-lock)
#include <sys/utsname.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/wait.h>
#else
#include <fcntl.h>      // _O_RDONLY & friends (nqp::open)
#include <io.h>         // _open/_read/_close
#endif
#include <condition_variable>
#include <mutex>

namespace rakupp {

// One mutex PER SUPPLIER, not a slot from the shared 64-stripe pool. The
// serialization contract (emissions are serialized per supplier, and done/quit
// callbacks join that order) requires holding a lock ACROSS user code — the
// tap blocks — and pool stripes must never do that: an unrelated container
// hashing onto the same slot turns hold-plus-wait into ABBA (the cas code
// form's livelock, thread.t). A dedicated mutex keeps the contract and removes
// cross-object collisions; the residual risk is a genuine user-level cycle
// (two suppliers emitting into each other's taps), which serializing
// implementations cannot avoid. Registry entries are never reclaimed — one
// mutex per supplier ever created, and address reuse just reuses a mutex.
std::recursive_mutex& supplierMutex(const void* key) {
    static std::mutex regM;
    static std::map<const void*, std::unique_ptr<std::recursive_mutex>> reg;
    std::lock_guard<std::mutex> lk(regM);
    auto& p = reg[key];
    if (!p) p = std::make_unique<std::recursive_mutex>();
    return *p;
}


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
        {"RatStr",  {"RatStr","Rat","Rational","Real","Numeric","Cool","Any","Mu"}},
        {"NumStr",  {"NumStr","Num","Real","Numeric","Cool","Any","Mu"}},
        {"Rat",     {"Rat","Rational","Real","Numeric","Cool","Any","Mu"}},
        // FatRat is NOT a Rat in Rakudo — both DO Rational, and its MRO is
        // FatRat/Cool/Any/Mu. Claiming the inheritance made `when Rat` swallow a
        // FatRat, so DBDish::mysql sent one as a double instead of a decimal.
        {"FatRat",  {"FatRat","Rational","Real","Numeric","Cool","Any","Mu"}},
        {"Rational",{"Rational","Real","Numeric","Cool","Any","Mu"}},
        {"Num",     {"Num","Real","Numeric","Cool","Any","Mu"}},
        {"Complex", {"Complex","Numeric","Cool","Any","Mu"}},
        {"Real",    {"Real","Numeric","Cool","Any","Mu"}},
        {"Numeric", {"Numeric","Cool","Any","Mu"}},
        {"Str",     {"Str","Cool","Any","Mu"}},
        {"Bool",    {"Bool","Cool","Any","Mu"}},
        {"Cool",    {"Cool","Any","Mu"}},
        {"Date",    {"Date","Dateish","Any","Mu"}},
        {"DateTime",{"DateTime","Dateish","Any","Mu"}},
        // the grammar/match family: a grammar IS a Match (that is how `self`
        // works inside rule methods), and a Match IS a Capture
        {"Grammar", {"Grammar","Match","Capture","Cool","Any","Mu"}},
        {"Match",   {"Match","Capture","Cool","Any","Mu"}},
        {"Capture", {"Capture","Any","Mu"}},
        // IO::Socket is the ROLE a synchronous socket does — every wrapper
        // declares its parameter as that (IO::Socket::SSL takes an IO::Socket).
        // It is not an IO, and IO::Socket::Async does not do it either.
        {"IO::Socket::INET", {"IO::Socket::INET","IO::Socket","Any","Mu"}},
        {"IO::Socket",       {"IO::Socket","Any","Mu"}},
        // The Uni family: each normalisation form is a Uni SUBCLASS, and Uni
        // does Positional/Iterable — `"x".NFD ~~ Uni` is True on Rakudo, and
        // JSON::Fast binds `Uni:D \codes` to exactly such a value.
        // The byte-buffer family: `Buf` DOES `Blob`, and both are Positional +
        // Stringy. `utf8` is a Blob but not Cool. Missing, `Buf ~~ Blob` was
        // False — which is how DBDish::mysql decided a BLOB column needed
        // .decode and choked on the first byte over 0x7F.
        {"Blob",  {"Blob","Positional","Stringy","Cool","Any","Mu"}},
        {"Buf",   {"Buf","Blob","Positional","Stringy","Cool","Any","Mu"}},
        {"blob8", {"blob8","Blob","Positional","Stringy","Cool","Any","Mu"}},
        {"blob16",{"blob16","Blob","Positional","Stringy","Cool","Any","Mu"}},
        {"blob32",{"blob32","Blob","Positional","Stringy","Cool","Any","Mu"}},
        {"blob64",{"blob64","Blob","Positional","Stringy","Cool","Any","Mu"}},
        {"buf8",  {"buf8","Buf","Blob","Positional","Stringy","Cool","Any","Mu"}},
        {"buf16", {"buf16","Buf","Blob","Positional","Stringy","Cool","Any","Mu"}},
        {"buf32", {"buf32","Buf","Blob","Positional","Stringy","Cool","Any","Mu"}},
        {"buf64", {"buf64","Buf","Blob","Positional","Stringy","Cool","Any","Mu"}},
        {"utf8",  {"utf8","Blob","Positional","Stringy","Any","Mu"}},
        {"Uni",  {"Uni","Positional","Iterable","Any","Mu"}},
        {"NFC",  {"NFC","Uni","Positional","Iterable","Any","Mu"}},
        {"NFD",  {"NFD","Uni","Positional","Iterable","Any","Mu"}},
        {"NFKC", {"NFKC","Uni","Positional","Iterable","Any","Mu"}},
        {"NFKD", {"NFKD","Uni","Positional","Iterable","Any","Mu"}},
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
        "Positional", "Associative", "Iterable", "Baggy", "Setty", "Mixy",
        "IO::Socket"};
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

// ---- child processes: spawn now, collect later -----------------------------
// spawnChildStart forks/execs immediately and hands back the pid plus the read
// ends of whatever pipes were asked for; spawnChildFinish drains those pipes to
// EOF (bounded by a wall-clock timeout) and reaps the child. run()/shell() use
// the halves back-to-back (spawnCapture below). Proc::Async.start parks the
// started child in g_spawned between the halves: Rakudo's `.start` means "the
// process is running from this moment on", so a never-awaited child must exist,
// run concurrently with us, and be left to the OS when we exit (issue #29) —
// realizing the promise only drains and reaps.

// How to wire the child's stdio. A capture pipe wins over an explicit fd (a
// bind-stdin pipe end), which wins over inheriting our own stream. `errToNull`
// keeps run()'s `:!err` meaning /dev/null rather than the terminal.
struct SpawnStdio {
    bool captureOut = false, captureErr = false;
    bool outToNull = false; // when !captureOut: /dev/null instead of inheriting (`:!out`)
    bool errToNull = false; // when !captureErr: /dev/null instead of inheriting
#if !defined(_WIN32)
    int stdinFd = -1, stdoutFd = -1, stderrFd = -1; // bind-* pipe ends
#endif
};

// Delivered each chunk of a child's output AS IT ARRIVES, so a Proc::Async tap
// fires while the process runs instead of once at the end. Called with the GIL
// HELD — the drain loop parks it and unparks around this — so the callback may
// re-enter the interpreter and run a `whenever` block.
using ChildChunkSink = std::function<void(bool isErr, const char* data, size_t n)>;

// A started-but-unreaped child.
struct SpawnedChild {
    long long pid = 0; // 0 = the spawn failed
#if defined(_WIN32)
    HANDLE hProcess = nullptr;
    HANDLE outR = nullptr, errR = nullptr; // pipe read ends (nullptr: not captured)
    std::string spawnErr;                  // CreateProcess failure text
#else
    int outFd = -1, errFd = -1;            // pipe read ends (-1: not captured)
    bool reaped = false;                   // the zombie sweep already waitpid()ed it…
    int rawStatus = 0;                     // …and this is the status it collected
#endif
};

// Children Proc::Async.start has running, keyed by the token stored on the proc
// hash. Mutex-guarded: realizations drain with the GIL parked, so one thread can
// be spawning (or .kill-ing) while another reaps.
static std::mutex g_spawnedM;
static std::map<long long, SpawnedChild> g_spawned;
static long long g_spawnedSeq = 0;

static int signalNumberOf(const Value& v); // Proc::Async.kill's argument; tables are with signal() below

// First half: spawn the child and return at once. The fork happens with the GIL
// held, so forks serialise (safe in a multithreaded process).
static SpawnedChild spawnChildStart(const std::vector<std::string>& argv, const std::string& cwd,
                                    const std::vector<std::string>* envKV, const SpawnStdio& io) {
    SpawnedChild sc;
    if (argv.empty()) return sc;
    // Anything we have written but not yet handed to the OS must go out BEFORE
    // the child starts. A child that inherits our stdout writes to the same fd
    // directly, so whatever is still sitting in std::cout's buffer would land
    // AFTER it — `print "building: "; run 'make'` came out in the wrong order
    // whenever our own output was redirected rather than a terminal.
    {
        std::lock_guard<std::mutex> lk(rtOutMutex());
        std::cout.flush(); std::cerr.flush();
    }
#if defined(_WIN32)
    // Windows: CreateProcess with inherited pipes; the finish half polls the
    // read ends via PeekNamedPipe. Compile-verified under mingw g++; behaviour
    // mirrors the POSIX path below.
    SECURITY_ATTRIBUTES sa; sa.nLength = sizeof(sa); sa.lpSecurityDescriptor = nullptr; sa.bInheritHandle = TRUE;
    HANDLE outR = nullptr, outW = nullptr, errR = nullptr, errW = nullptr;
    if (io.captureOut) {
        if (!CreatePipe(&outR, &outW, &sa, 0)) return sc;
        SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);
    }
    if (io.captureErr) {
        if (!CreatePipe(&errR, &errW, &sa, 0)) { if (outR) { CloseHandle(outR); CloseHandle(outW); } return sc; }
        SetHandleInformation(errR, HANDLE_FLAG_INHERIT, 0);
    }
    HANDLE nul = (!io.captureErr && io.errToNull) ? CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr) : INVALID_HANDLE_VALUE;
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
    // stdout: the capture pipe, or our own stdout (an un-tapped Proc::Async
    // stream goes where ours goes, like Rakudo) — with the same NUL fallback
    // as stdin, since STARTF_USESTDHANDLES needs all three handles usable.
    HANDLE outNul = INVALID_HANDLE_VALUE, outH = INVALID_HANDLE_VALUE;
    if (!io.captureOut) {
        // `:!out` wants the output GONE, not on our console — the same rule
        // `:!err` already follows.
        outH = io.outToNull ? INVALID_HANDLE_VALUE : GetStdHandle(STD_OUTPUT_HANDLE);
        if (outH == nullptr || outH == INVALID_HANDLE_VALUE) {
            outNul = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
            outH = outNul;
        }
        else SetHandleInformation(outH, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    }
    si.hStdOutput = io.captureOut ? outW : outH;
    // STARTF_USESTDHANDLES needs an inheritable handle for stderr too; the same
    // caveat as stdin above applies, so an unusable one falls back to NUL.
    HANDLE errNul = INVALID_HANDLE_VALUE, errH = INVALID_HANDLE_VALUE;
    if (!io.captureErr && !io.errToNull) {
        errH = GetStdHandle(STD_ERROR_HANDLE);
        if (errH == nullptr || errH == INVALID_HANDLE_VALUE) {
            errNul = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
            errH = errNul;
        }
        else SetHandleInformation(errH, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    }
    si.hStdError = io.captureErr ? errW : (io.errToNull ? nul : errH);
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
    if (outW) CloseHandle(outW); if (errW) CloseHandle(errW); if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (inNul != INVALID_HANDLE_VALUE) CloseHandle(inNul);
    if (outNul != INVALID_HANDLE_VALUE) CloseHandle(outNul);
    if (errNul != INVALID_HANDLE_VALUE) CloseHandle(errNul);
    if (!started) {
        // A silent -1 with no output is undiagnosable — say WHY. The caller
        // routes this to the error stream when one was asked for, otherwise
        // to our own stderr.
        char msg[512] = {0};
        FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                       spawnErr, 0, msg, sizeof msg - 1, nullptr);
        std::string text = "Could not spawn '" + argv[0] + "': " + msg;
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
        sc.spawnErr = text;
        if (outR) CloseHandle(outR); if (errR) CloseHandle(errR);
        return sc;
    }
    sc.pid = (long long)pi.dwProcessId;
    sc.hProcess = pi.hProcess;
    CloseHandle(pi.hThread);
    sc.outR = outR; sc.errR = errR;
    return sc;
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
    int pipefd[2] = {-1, -1}, errfd[2] = {-1, -1};
    if (io.captureOut && pipe(pipefd) != 0) return sc;
    if (io.captureErr && pipe(errfd) != 0) { if (io.captureOut) { close(pipefd[0]); close(pipefd[1]); } return sc; }
    pid_t pid = fork();
    if (pid < 0) {
        if (io.captureOut) { close(pipefd[0]); close(pipefd[1]); }
        if (io.captureErr) { close(errfd[0]); close(errfd[1]); }
        return sc;
    }
    if (pid == 0) { // child — async-signal-safe only from here
        setpgid(0, 0); // own process group, so a timeout can kill grandchildren too
        if (io.stdinFd >= 0) dup2(io.stdinFd, STDIN_FILENO);
        if (io.captureOut) dup2(pipefd[1], STDOUT_FILENO);
        else if (io.stdoutFd >= 0) dup2(io.stdoutFd, STDOUT_FILENO);
        else if (io.outToNull) { int devnull = open("/dev/null", O_WRONLY); if (devnull >= 0) dup2(devnull, STDOUT_FILENO); }
        // none of those: STDOUT_FILENO is left exactly as we got it (inherited)
        if (io.captureErr) dup2(errfd[1], STDERR_FILENO);
        else if (io.stderrFd >= 0) dup2(io.stderrFd, STDERR_FILENO);
        else if (io.errToNull) { int devnull = open("/dev/null", O_WRONLY); if (devnull >= 0) dup2(devnull, STDERR_FILENO); }
        close(pipefd[0]); close(pipefd[1]);
        close(errfd[0]); close(errfd[1]);
        // the bind-* fds carry FD_CLOEXEC: every copy but the dup2'd one (which
        // shed the flag) vanishes at exec, so a bound reader's EOF arrives
        // exactly when its writer exits
        if (!cwd.empty()) { if (::chdir(cwd.c_str()) != 0) _exit(126); }
        if (envKV) environ = cenv.data();
        execvp(cargv[0], cargv.data());
        _exit(127);
    }
    sc.pid = (long long)pid;
    // parent: don't let a concurrent spawn (another worker) inherit our read ends
    // across its execvp — that would keep the write end open and defer our EOF.
    if (io.captureOut) {
        fcntl(pipefd[0], F_SETFD, FD_CLOEXEC);
        close(pipefd[1]);
        fcntl(pipefd[0], F_SETFL, O_NONBLOCK);
        sc.outFd = pipefd[0];
    }
    if (io.captureErr) {
        fcntl(errfd[0], F_SETFD, FD_CLOEXEC);
        close(errfd[1]);
        fcntl(errfd[0], F_SETFL, O_NONBLOCK);
        sc.errFd = errfd[0];
    }
    return sc;
#endif
}

// Second half: drain the capture pipes to EOF, then reap. EOF, not the child's
// exit, is the only reliable "all output captured" signal: reaping with waitpid
// does not guarantee the final buffered write has been drained, and grandchildren
// may still hold the write end. `timeoutSec` bounds the whole wait; on expiry the
// child and its process group are killed. `gil` (if non-null) is the interpreter:
// the GIL is parked for the wait so sibling worker threads run — and spawn their
// own children — concurrently.
static void spawnChildFinish(SpawnedChild& sc, double timeoutSec,
                             std::string& out, std::string* errOut,
                             int& exitCode, bool& timedout, Interpreter* gil,
                             const ChildChunkSink* sink = nullptr,
                             std::exception_ptr* sinkErr = nullptr) {
    exitCode = -1; timedout = false;
    if (!sc.pid) return;
    bool parked = gil ? gil->gilPark() : false; // drop the GIL for the wait below
    auto start = std::chrono::steady_clock::now();
    char buf[8192];
    // Hand a chunk to the sink with the GIL back in hand, then park again for the
    // next wait. A throw from the callback (a user `whenever` block dying) is
    // held rather than let out: the child still has to be reaped and its
    // descriptors closed, so the caller rethrows once that is done.
    auto deliver = [&](bool isErr, const char* d, size_t n) {
        if (!sink || !*sink || n == 0) return;
        if (parked) { gil->gilUnpark(true); parked = false; }
        try { (*sink)(isErr, d, n); }
        catch (...) { if (sinkErr && !*sinkErr) *sinkErr = std::current_exception(); }
        if (gil) parked = gil->gilPark();
    };
#if defined(_WIN32)
    bool oEof = (sc.outR == nullptr), eEof = (sc.errR == nullptr);
    auto drain = [&](HANDLE h, std::string* dst, bool isErr, bool& eof) {
        DWORD avail = 0;
        if (!PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr)) { eof = true; return; }
        while (avail > 0) {
            DWORD want = avail > sizeof buf ? (DWORD)sizeof buf : avail, rd = 0;
            if (!ReadFile(h, buf, want, &rd, nullptr) || rd == 0) { eof = true; return; }
            if (dst) dst->append(buf, rd);
            deliver(isErr, buf, rd);
            if (!PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr)) { eof = true; return; }
        }
    };
    while (!oEof || !eEof) {
        if (!oEof) drain(sc.outR, &out, false, oEof);
        if (!eEof) drain(sc.errR, errOut, true, eEof);
        if (oEof && eEof) break;
        bool exited = WaitForSingleObject(sc.hProcess, 0) == WAIT_OBJECT_0;
        if (timeoutSec > 0) {
            double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            if (el > timeoutSec) { TerminateProcess(sc.hProcess, 1); timedout = true; break; }
        }
        if (!exited) Sleep(2);
    }
    if (!timedout) {
        // the deadline binds even with no pipe left to key on (none was asked
        // for, or the child closed its ends and lives on)
        DWORD waitMs = INFINITE;
        if (timeoutSec > 0) {
            double rem = timeoutSec - std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            waitMs = rem > 0 ? (DWORD)(rem * 1000) : 0;
        }
        if (WaitForSingleObject(sc.hProcess, waitMs) != WAIT_OBJECT_0 && timeoutSec > 0) {
            TerminateProcess(sc.hProcess, 1); timedout = true;
            WaitForSingleObject(sc.hProcess, INFINITE);
        }
    }
    else WaitForSingleObject(sc.hProcess, INFINITE);
    DWORD ec = 0; if (!timedout && GetExitCodeProcess(sc.hProcess, &ec)) exitCode = (int)ec;
    if (sc.outR) CloseHandle(sc.outR);
    if (sc.errR) CloseHandle(sc.errR);
    CloseHandle(sc.hProcess);
#else
    pid_t pid = (pid_t)sc.pid;
    int fd = sc.outFd, efd = sc.errFd;
    bool oEof = (fd < 0), eEof = (efd < 0);
    while (!oEof || !eEof) {
        struct pollfd pfds[2]; int nf = 0;
        if (!oEof) { pfds[nf] = {fd, POLLIN, 0}; nf++; }
        if (!eEof) { pfds[nf] = {efd, POLLIN, 0}; nf++; }
        poll(pfds, nf, 50);
        if (!oEof) for (;;) {
            ssize_t n = read(fd, buf, sizeof buf);
            if (n > 0) { out.append(buf, (size_t)n); deliver(false, buf, (size_t)n); continue; }
            if (n == 0) oEof = true;
            break;
        }
        if (!eEof) for (;;) {
            ssize_t n = read(efd, buf, sizeof buf);
            if (n > 0) { if (errOut) errOut->append(buf, (size_t)n); deliver(true, buf, (size_t)n); continue; }
            if (n == 0) eEof = true;
            break;
        }
        if (oEof && eEof) break;
        if (timeoutSec > 0) {
            double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            if (elapsed > timeoutSec) {
                // a child the zombie sweep already reaped must NOT be signalled:
                // the pid may have been recycled (only its grandchildren still
                // hold the pipe, and those we leave be)
                if (!sc.reaped) { kill(-pid, SIGKILL); kill(pid, SIGKILL); }
                timedout = true; break;
            }
        }
    }
    int status = 0;
    if (sc.reaped) status = sc.rawStatus; // the zombie sweep got there first
    else if (timedout) { while (waitpid(pid, &status, 0) == -1 && errno == EINTR) {} }
    else if (timeoutSec > 0) {
        // the deadline binds even with no pipe left to key on (none was asked
        // for, or the child closed its ends and lives on): poll for the exit
        for (;;) {
            pid_t r = waitpid(pid, &status, WNOHANG);
            if (r != 0) break;
            double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            if (elapsed > timeoutSec) {
                kill(-pid, SIGKILL); kill(pid, SIGKILL); timedout = true;
                while (waitpid(pid, &status, 0) == -1 && errno == EINTR) {}
                break;
            }
            poll(nullptr, 0, 10);
        }
    }
    else { while (waitpid(pid, &status, 0) == -1 && errno == EINTR) {} } // reap; retry on EINTR
    if (!timedout) exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    if (fd >= 0) close(fd);
    if (efd >= 0) close(efd);
#endif
    if (parked) gil->gilUnpark(true);    // reacquire the GIL before touching interpreter state
}

// Spawn a child process, capture its stdout, with an optional wall-clock timeout —
// the two halves back-to-back, which is what run()/shell()/qx need.
// `errOut` non-null captures the child's stderr; otherwise `errInherit` decides
// between INHERITING our own stderr (Rakudo's default for an un-adverbed run —
// the child's diagnostics reach the terminal or the CI log) and discarding it
// (`:!err`). Both used to mean /dev/null, which is how a MAIN usage message
// from a child rakupp vanished and left a failing raku-eye leg undiagnosable.
// `outMode` says the same three things about stdout: 1 capture (`:out`),
// 0 discard (`:!out`), -1 INHERIT ours — which is the un-adverbed default and
// the only one that is LIVE, the child writing to our fd as it goes.
static void spawnCapture(const std::vector<std::string>& argv, double timeoutSec,
                         std::string& out, int& exitCode, bool& timedout,
                         Interpreter* gil = nullptr, std::string* errOut = nullptr,
                         const std::string& cwd = "", long long* pidOut = nullptr,
                         const std::vector<std::string>* envKV = nullptr,
                         bool errInherit = false, int outMode = 1,
                         const ChildChunkSink* sink = nullptr,
                         std::exception_ptr* sinkErr = nullptr,
                         int stdinFd = -1) {
    out.clear(); exitCode = -1; timedout = false;
    if (errOut) errOut->clear();
    if (argv.empty()) return;
    SpawnStdio io;
#if !defined(_WIN32)
    io.stdinFd = stdinFd; // `:in($handle)` — the child's stdin IS this descriptor
#else
    (void)stdinFd;
#endif
    io.captureOut = outMode == 1;
    io.outToNull  = outMode == 0;
    io.captureErr = errOut != nullptr;
    io.errToNull = !errOut && !errInherit;
    SpawnedChild sc = spawnChildStart(argv, cwd, envKV, io);
    if (!sc.pid) {
#if defined(_WIN32)
        if (!sc.spawnErr.empty()) {
            if (errOut) *errOut = sc.spawnErr + "\n"; else std::cerr << sc.spawnErr << "\n";
        }
#endif
        return;
    }
    if (pidOut) *pidOut = sc.pid;
    spawnChildFinish(sc, timeoutSec, out, errOut, exitCode, timedout, gil, sink, sinkErr);
}

// Spawn a child, feed `input` to its stdin, and collect its output. Uses poll on
// every open pipe so it won't deadlock when the child's output exceeds the pipe
// buffer while we're still writing input (as pandoc can on a large page).
//
// `outMode` and `errOut`/`errInherit` mean exactly what they mean in
// spawnCapture: stdout is captured (1), discarded (0) or INHERITED (-1); stderr
// is captured when `errOut` is non-null, otherwise inherited or discarded as
// `errInherit` says. This path used to hardcode "capture stdout, send stderr to
// /dev/null", so `run(cmd, :in, :out, :err)` came back with an empty `.err` and
// `run(cmd, :in)` swallowed both streams — where Rakudo inherits both.
void spawnWithInput(const std::vector<std::string>& argv, const std::string& input,
                           std::string& out, int& exitCode, Interpreter* gil,
                           const std::vector<std::string>* envKV, const std::string& cwd,
                           std::string* errOut, bool errInherit, int outMode) {
    out.clear(); exitCode = -1;
    if (errOut) errOut->clear();
    if (argv.empty()) return;
    const bool capOut = outMode == 1, capErr = errOut != nullptr;
#if defined(_WIN32)
    SECURITY_ATTRIBUTES sa; sa.nLength = sizeof(sa); sa.lpSecurityDescriptor = nullptr; sa.bInheritHandle = TRUE;
    HANDLE inR = nullptr, inW = nullptr, outR = nullptr, outW = nullptr, errR = nullptr, errW = nullptr;
    if (!CreatePipe(&inR, &inW, &sa, 0)) return;
    SetHandleInformation(inW, HANDLE_FLAG_INHERIT, 0);
    if (capOut) {
        if (!CreatePipe(&outR, &outW, &sa, 0)) { CloseHandle(inR); CloseHandle(inW); return; }
        SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);
    }
    if (capErr) {
        if (!CreatePipe(&errR, &errW, &sa, 0)) {
            CloseHandle(inR); CloseHandle(inW);
            if (outR) { CloseHandle(outR); CloseHandle(outW); }
            return;
        }
        SetHandleInformation(errR, HANDLE_FLAG_INHERIT, 0);
    }
    HANDLE nul = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    std::string cmd;
    for (size_t i = 0; i < argv.size(); i++) { if (i) cmd += ' '; cmd += '"'; for (char c : argv[i]) { if (c == '"') cmd += '\\'; cmd += c; } cmd += '"'; }
    STARTUPINFOA si; ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si); si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = inR;
    si.hStdOutput = capOut ? outW : (outMode == 0 ? nul : GetStdHandle(STD_OUTPUT_HANDLE));
    si.hStdError  = capErr ? errW : (errInherit ? GetStdHandle(STD_ERROR_HANDLE) : nul);
    PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof(pi));
    std::vector<char> cmdbuf(cmd.begin(), cmd.end()); cmdbuf.push_back('\0');
    std::string envblk; if (envKV) envblk = winEnvBlock(*envKV);
    BOOL started = CreateProcessA(nullptr, cmdbuf.data(), nullptr, nullptr, TRUE, 0, envKV ? (LPVOID)envblk.data() : nullptr, cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
    CloseHandle(inR);
    if (outW) CloseHandle(outW);
    if (errW) CloseHandle(errW);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!started) { CloseHandle(inW); if (outR) CloseHandle(outR); if (errR) CloseHandle(errR); return; }
    bool parked = gil ? gil->gilPark() : false;
    size_t written = 0; char buf[8192]; bool wOpen = true;
    // Drain whatever is sitting in one pipe; false once it is closed or broken,
    // so a stream that ends early stops being polled.
    auto drain = [&buf](HANDLE h, std::string* into) -> bool {
        DWORD avail = 0;
        if (!PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr)) return false;
        while (avail > 0) {
            DWORD want = avail > sizeof buf ? (DWORD)sizeof buf : avail, rd = 0;
            if (!ReadFile(h, buf, want, &rd, nullptr) || rd == 0) return false;
            into->append(buf, rd);
            if (!PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr)) return false;
        }
        return true;
    };
    bool oAlive = outR != nullptr, eAlive = errR != nullptr;
    for (;;) {
        if (wOpen) {
            if (written < input.size()) {
                DWORD want = (DWORD)((input.size() - written < sizeof buf) ? input.size() - written : sizeof buf), wn = 0;
                if (WriteFile(inW, input.data() + written, want, &wn, nullptr) && wn) written += wn;
                else { CloseHandle(inW); wOpen = false; }
            } else { CloseHandle(inW); wOpen = false; }
        }
        if (oAlive) oAlive = drain(outR, &out);
        if (eAlive) eAlive = drain(errR, errOut);
        if (!wOpen) {
            if (WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0) {
                if (oAlive) drain(outR, &out);      // whatever the child left behind
                if (eAlive) drain(errR, errOut);
                break;
            }
            Sleep(2);
        }
    }
    if (wOpen) CloseHandle(inW);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD ec = 0; if (GetExitCodeProcess(pi.hProcess, &ec)) exitCode = (int)ec;
    if (outR) CloseHandle(outR);
    if (errR) CloseHandle(errR);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
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
    int inPipe[2], outPipe[2] = {-1, -1}, errPipe[2] = {-1, -1};
    if (pipe(inPipe) != 0) return;
    if (capOut && pipe(outPipe) != 0) { close(inPipe[0]); close(inPipe[1]); return; }
    if (capErr && pipe(errPipe) != 0) {
        close(inPipe[0]); close(inPipe[1]);
        if (capOut) { close(outPipe[0]); close(outPipe[1]); }
        return;
    }
    pid_t pid = fork();
    if (pid < 0) {
        close(inPipe[0]); close(inPipe[1]);
        if (capOut) { close(outPipe[0]); close(outPipe[1]); }
        if (capErr) { close(errPipe[0]); close(errPipe[1]); }
        return;
    }
    if (pid == 0) { // child — async-signal-safe from here
        dup2(inPipe[0], STDIN_FILENO);
        // A stream that is neither captured nor discarded is left alone, so the
        // child writes to OUR descriptor as it goes — Rakudo's default for an
        // un-adverbed run, and the only mode that is live rather than buffered.
        int devnull = -1;
        if (!capOut && outMode == 0) { devnull = open("/dev/null", O_WRONLY); if (devnull >= 0) dup2(devnull, STDOUT_FILENO); }
        if (!capErr && !errInherit) {
            if (devnull < 0) devnull = open("/dev/null", O_WRONLY);
            if (devnull >= 0) dup2(devnull, STDERR_FILENO);
        }
        if (capOut) dup2(outPipe[1], STDOUT_FILENO);   // capture and discard are exclusive
        if (capErr) dup2(errPipe[1], STDERR_FILENO);
        close(inPipe[0]); close(inPipe[1]);
        if (capOut) { close(outPipe[0]); close(outPipe[1]); }
        if (capErr) { close(errPipe[0]); close(errPipe[1]); }
        if (devnull >= 0) close(devnull);
        if (!cwd.empty()) { if (::chdir(cwd.c_str()) != 0) _exit(126); }
        if (envKV) environ = cenv.data();
        execvp(cargv[0], cargv.data());
        _exit(127);
    }
    close(inPipe[0]);
    if (capOut) close(outPipe[1]);
    if (capErr) close(errPipe[1]);
    fcntl(inPipe[1], F_SETFD, FD_CLOEXEC);
    int wfd = inPipe[1], rfd = capOut ? outPipe[0] : -1, efd = capErr ? errPipe[0] : -1;
    fcntl(wfd, F_SETFL, O_NONBLOCK);
    if (rfd >= 0) { fcntl(rfd, F_SETFD, FD_CLOEXEC); fcntl(rfd, F_SETFL, O_NONBLOCK); }
    if (efd >= 0) { fcntl(efd, F_SETFD, FD_CLOEXEC); fcntl(efd, F_SETFL, O_NONBLOCK); }
    // (SIGPIPE is ignored process-wide at startup — Runtime.cpp)
    bool parked = gil ? gil->gilPark() : false; // drop the GIL for the feed/read wait below
    size_t written = 0;
    char buf[8192];
    bool rOpen = rfd >= 0, eOpen = efd >= 0, wOpen = true;
    while (rOpen || eOpen || wOpen) {
        struct pollfd pfds[3]; int nf = 0;
        int ri = -1, ei = -1, wi = -1;
        if (rOpen) { pfds[nf] = {rfd, POLLIN, 0}; ri = nf; nf++; }
        if (eOpen) { pfds[nf] = {efd, POLLIN, 0}; ei = nf; nf++; }
        if (wOpen) { pfds[nf] = {wfd, POLLOUT, 0}; wi = nf; nf++; }
        poll(pfds, nf, 50);
        if (rOpen && ri >= 0 && (pfds[ri].revents & (POLLIN | POLLHUP))) {
            ssize_t n;
            while ((n = read(rfd, buf, sizeof buf)) > 0) out.append(buf, (size_t)n);
            if (n == 0) { rOpen = false; close(rfd); }
        }
        if (eOpen && ei >= 0 && (pfds[ei].revents & (POLLIN | POLLHUP))) {
            ssize_t n;
            while ((n = read(efd, buf, sizeof buf)) > 0) errOut->append(buf, (size_t)n);
            if (n == 0) { eOpen = false; close(efd); }
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

// The child's stdin for a `:in($handle)` adverb. Rakudo hands the child the
// handle's OWN descriptor, so a child that inspects its stdin sees the plain
// file — macOS `script` does a tcgetattr on it, `test -f /dev/stdin` holds —
// and reads the file's bytes. rakupp's file handle carries a path rather than
// a descriptor, so the path is opened here (from the start: the reader keeps a
// line cursor, not a byte offset); an in-memory handle — a captured Proc.out
// or .err, $*ARGFILES — is spooled to an unlinked temp file; a descriptor-
// backed one (nqp::open's, a socket's) is dup'd. Returns the descriptor the
// child dup2s onto 0 — the caller closes it after the spawn — or -1 for
// `:in($*IN)`, which is inherit. `resolved` says whether the value was a
// usable handle at all; anything else keeps its old meaning (a Bool).
// Before this, `run` read ANY `:in` value as the Bool that selects the
// deferred piped mode and `shell` ignored `:in` outright, so the child got OUR
// stdin: Roast's Test::Util run-with-tty passes a file handle precisely so that
// `script` sees a plain fd, and under a harness whose stdin is a pipe or a
// socket S32-io/out-buffering.t's "prompt does not hang" either lost its
// input or died in tcgetattr — a flap in every Roast sweep.
static int stdinFdForHandle(const Value& h, bool& resolved) {
    resolved = false;
#if defined(_WIN32)
    (void)h;
    return -1;
#else
    if (h.t != VT::Hash || !h.hash()) return -1;
    const auto& fh = *h.hash();
    auto field = [&](const char* k) -> const Value* {
        auto it = fh.find(k); return it != fh.end() ? &it->second : nullptr;
    };
    if (h.hashKind == "FileHandle") {
        if (const Value* std_ = field("std")) { resolved = std_->toStr() == "in"; return -1; }
        if (const Value* path = field("path")) {
            if (path->toStr().empty()) return -1;
            int fd = ::open(path->toStr().c_str(), O_RDONLY);
            if (fd < 0) return -1;
            resolved = true;
            return fd;
        }
        if (const Value* buf = field("buffer")) {
            const char* td = std::getenv("TMPDIR");
            std::string tmpl = td && *td ? td : "/tmp";
            if (tmpl.back() != '/') tmpl += '/';
            tmpl += "rakupp-stdin-XXXXXX";
            std::vector<char> name(tmpl.begin(), tmpl.end()); name.push_back('\0');
            int fd = ::mkstemp(name.data());
            if (fd < 0) return -1;
            ::unlink(name.data());
            const std::string content = buf->toStr();
            size_t off = 0;
            while (off < content.size()) {
                ssize_t n = ::write(fd, content.data() + off, content.size() - off);
                if (n <= 0) break;
                off += (size_t)n;
            }
            ::lseek(fd, 0, SEEK_SET);
            resolved = true;
            return fd;
        }
        return -1;
    }
    if (const Value* fdv = field("fd")) {
        int fd = ::dup((int)fdv->toInt());
        if (fd < 0) return -1;
        resolved = true;
        return fd;
    }
    return -1;
#endif
}

// Run one emitted value through a live-Supply tap's transform chain (grep/map/head/…).
// Threads the value(s) through each step in order; per-step mutable state lives in the
// step's "state" hash. Sets `complete` when a head/first step reaches its limit.
std::pair<size_t, size_t> nextLogicalNewline(const std::string& s, size_t from) {
    for (size_t i = from; i < s.size(); i++) {
        const unsigned char c = (unsigned char)s[i];
        if (c == 0x0D) // CR, and CRLF is ONE terminator
            return {i, (i + 1 < s.size() && s[i + 1] == 0x0A) ? 2u : 1u};
        if (c == 0x0A || c == 0x0B || c == 0x0C) return {i, 1};  // LF, VT, FF
        if (c == 0xC2 && i + 1 < s.size() && (unsigned char)s[i + 1] == 0x85)
            return {i, 2};                                        // NEL  U+0085
        if (c == 0xE2 && i + 2 < s.size() && (unsigned char)s[i + 1] == 0x80 &&
            ((unsigned char)s[i + 2] == 0xA8 || (unsigned char)s[i + 2] == 0xA9))
            return {i, 3};                                        // LS/PS U+2028-9
    }
    return {std::string::npos, 0};
}
size_t danglingNewlinePrefix(const std::string& s) {
    if (s.empty()) return 0;
    const unsigned char last = (unsigned char)s.back();
    if (last == 0x0D) return 1;                      // "\r" may yet become "\r\n"
    if (last == 0xC2) return 1;                      // NEL lead byte
    if (last == 0xE2) return 1;                      // LS/PS lead byte
    if (s.size() >= 2 && (unsigned char)s[s.size() - 2] == 0xE2 && last == 0x80) return 2;
    return 0;
}

ValueList Interpreter::applyTapChain(Value& tap, const Value& in, bool& complete, bool flush) {
    complete = false;
    ValueList cur;
    if (!flush) cur.push_back(in);
    if (!(tap.t == VT::Hash && tap.hash()->count("chain"))) return cur;
    for (auto& step : *(*tap.hash())["chain"].arr()) {
        const std::string op = (*step.hash())["op"].toStr();
        Value arg = step.hash()->count("arg") ? (*step.hash())["arg"] : Value::nil();
        Value& state = (*step.hash())["state"];
        auto sInt = [&](const char* k) -> long long { auto it = state.hash()->find(k); return it == state.hash()->end() ? 0 : it->second.toInt(); };
        ValueList next;
        for (auto& v : cur) {
            if (op == "map") { next.push_back(arg.t == VT::Code ? callCallable(arg, ValueList{v}) : v); }
            else if (op == "grep") {
                bool match;
                if (arg.t == VT::Code) match = predAnswerTruthy(*this, callCallable(arg, ValueList{v}), v);
                else if (arg.t == VT::Regex) match = regexMatch(v.toStr(), arg.s).truthy();
                else match = applyArith("~~", v, arg).truthy();
                if (match) next.push_back(v);
            }
            else if (op == "skip") { long long n = arg.toInt(); long long c = sInt("c"); if (c < n) (*state.hash())["c"] = Value::integer(c + 1); else next.push_back(v); }
            else if (op == "head") {
                double lim = arg.t == VT::Nil ? 1 : (arg.t == VT::Whatever ? std::numeric_limits<double>::infinity() : arg.toNum());
                long long c = sInt("c");
                if (c < lim) { next.push_back(v); (*state.hash())["c"] = Value::integer(c + 1); if (c + 1 >= lim) complete = true; }
                else complete = true;
            }
            else if (op == "first") {
                bool match = true;
                if (arg.t == VT::Code) match = predAnswerTruthy(*this, callCallable(arg, ValueList{v}), v);
                else if (arg.t == VT::Regex) match = regexMatch(v.toStr(), arg.s).truthy();
                else if (arg.t != VT::Nil) match = applyArith("~~", v, arg).truthy();
                if (match) { next.push_back(v); complete = true; }
            }
            else if (op == "unique" || op == "squish") {
                Value asF = step.hash()->count("as") ? (*step.hash())["as"] : Value::nil();
                Value key = asF.t == VT::Code ? callCallable(asF, ValueList{v}) : v;
                std::string ks = key.toStr();
                if (op == "unique") {
                    // remember seen keys as hash entries in state
                    if (!state.hash()->count("seen")) (*state.hash())["seen"] = Value::makeHash();
                    Value& seen = (*state.hash())["seen"];
                    if (!seen.hash()->count(ks)) { (*seen.hash())[ks] = Value::boolean(true); next.push_back(v); }
                } else { // squish: drop only if equal to the immediately preceding key
                    bool same = state.hash()->count("has") && (*state.hash())["prev"].toStr() == ks;
                    if (!same) next.push_back(v);
                    (*state.hash())["prev"] = Value::str(ks); (*state.hash())["has"] = Value::boolean(true);
                }
            }
            else if (op == "lines" || op == "words") {
                // A stream SPLITTER: the message boundaries are not the piece
                // boundaries, so the unfinished tail is held in state and joined to
                // the next message. `.lines(:!chomp)` keeps the newline on each line
                // (TAP feeds its parser that way — issue #34).
                bool chomp = !step.hash()->count("chomp") || (*step.hash())["chomp"].truthy();
                std::string buf = state.hash()->count("buf") ? (*state.hash())["buf"].toStr() : std::string();
                buf += v.toStr();
                size_t start = 0;
                if (op == "lines") {
                    // the same logical-newline set a Str breaks on — and a tail that
                    // could still GROW into one (a lone "\r" before an unseen "\n",
                    // a truncated NEL/LS/PS) is held for the next message rather than
                    // terminating a line early
                    const size_t safe = buf.size() - danglingNewlinePrefix(buf);
                    for (;;) {
                        auto [at, len] = nextLogicalNewline(buf, start);
                        if (at == std::string::npos || at + len > safe) break;
                        next.push_back(Value::str(buf.substr(start, at - start + (chomp ? 0 : len))));
                        start = at + len;
                    }
                }
                else { // words: a piece ends at whitespace, so a trailing partial word waits
                    size_t i = start;
                    for (;;) {
                        while (i < buf.size() && ascii::isspace((unsigned char)buf[i])) i++;
                        size_t b = i;
                        while (i < buf.size() && !ascii::isspace((unsigned char)buf[i])) i++;
                        if (b == i) break;                       // nothing but whitespace left
                        if (i == buf.size()) { i = b; break; }   // may still grow: hold it
                        next.push_back(Value::str(buf.substr(b, i - b)));
                    }
                    start = i;
                }
                (*state.hash())["buf"] = Value::str(buf.substr(start));
            }
            else next.push_back(v);
        }
        // Draining: nothing can grow any more, so what was held back as ambiguous
        // is now decided. A tail like "b\r" is a TERMINATED line (the "\r" can no
        // longer become "\r\n"), not a line whose text ends in a carriage return.
        if (flush && (op == "lines" || op == "words")) {
            bool chomp = !step.hash()->count("chomp") || (*step.hash())["chomp"].truthy();
            std::string buf = state.hash()->count("buf") ? (*state.hash())["buf"].toStr() : std::string();
            if (op == "lines") {
                size_t start = 0;
                for (;;) {
                    auto [at, len] = nextLogicalNewline(buf, start);
                    if (at == std::string::npos) break;
                    next.push_back(Value::str(buf.substr(start, at - start + (chomp ? 0 : len))));
                    start = at + len;
                }
                if (start < buf.size()) next.push_back(Value::str(buf.substr(start)));
            }
            else if (!buf.empty()) next.push_back(Value::str(buf)); // words: the last word
            (*state.hash())["buf"] = Value::str("");
        }
        cur = std::move(next);
        if (complete) break;
    }
    return cur;
}

// Realize a Proc::Async .start promise: the process has been RUNNING since
// `.start` (spawnChildStart in the method handler); this drains its capture
// pipes, feeds them to the Supply taps, reaps it, and marks the promise Kept
// (finished) or Broken (timed out). The fallback path spawns here, lazily —
// for a promise whose eager spawn never happened (empty argv, fork failure).
void Interpreter::runProcPromise(Value& promise, double timeoutSec) {
    if (!promise.hash()) return;
    if (promise.hash()->count("status") && (*promise.hash())["status"].toStr() != "Planned") return; // already run
    auto pit = promise.hash()->find("proc");
    if (pit == promise.hash()->end() || !pit->second.hash()) { (*promise.hash())["status"] = Value::str("Kept"); return; }
    Value& proc = pit->second;
    std::string out, err; int code = -1; bool timedout = false;
    long long childPid = 0;
    // Walk one stream's taps. A tap is a RECORD ({emit, done, quit, bin, lines}
    // — MethodCallPart2's tap branch); a bare callable is tolerated for older
    // callers. `body` decides what to do with each one.
    auto eachTap = [&](const char* key, const std::function<void(Value& cb, Value& done, bool bin, bool lines)>& body) {
        auto taps = proc.hash()->find(key);
        if (taps == proc.hash()->end() || !taps->second.arr()) return;
        for (auto& t : *taps->second.arr()) {
            Value cb = t, done; bool bin = false, lines = false;
            if (t.t == VT::Hash && t.hash()->count("emit")) {
                cb = (*t.hash())["emit"];
                auto d = t.hash()->find("done"); if (d != t.hash()->end()) done = d->second;
                auto b = t.hash()->find("bin");  bin = b != t.hash()->end() && b->second.truthy();
                auto l = t.hash()->find("lines"); lines = l != t.hash()->end() && l->second.truthy();
            }
            body(cb, done, bin, lines);
        }
    };
    // One chunk, straight from the pipe, to every tap on that stream. This runs
    // WHILE the child is alive — that is the whole point: `whenever
    // $proc.stdout.lines` used to fire only once the process had exited, so a
    // runner relaying a build's progress relayed it all after the build (issue
    // #51). rakupp's own react loop drives it, so a `whenever` block printing a
    // line prints it now.
    auto emitChunk = [&](const char* key, const std::string& data) {
        if (data.empty()) return;
        eachTap(key, [&](Value& cb, Value&, bool bin, bool) {
            if (cb.t != VT::Code) return;
            Value chunk = Value::str(data);
            // `.stdout(:bin)` taps get the bytes as a Blob (zef's fetcher
            // Buf.appends them); a plain tap gets the DECODED Str, as
            // Rakudo emits — a Blob chunk made every `whenever` block that
            // string-matched its lines see Blob.new(...) instead of text.
            if (bin) chunk.hashKind = "Blob";
            ValueList ca{chunk};
            callCallable(cb, ca);
        });
    };
    std::exception_ptr sinkErr;
    ChildChunkSink sink = [&](bool isErr, const char* d, size_t n) {
        emitChunk(isErr ? "taps-err" : "taps", std::string(d, n));
    };
    long long tok = 0;
    { auto t = proc.hash()->find("spawn-token");
      if (t != proc.hash()->end()) { tok = t->second.toInt(); proc.hash()->erase(t); } }
    if (tok) {
        SpawnedChild sc;
        { std::lock_guard<std::mutex> lk(g_spawnedM);
          auto it = g_spawned.find(tok);
          if (it != g_spawned.end()) { sc = it->second; g_spawned.erase(it); } }
        childPid = sc.pid;
        spawnChildFinish(sc, timeoutSec, out, &err, code, timedout, this, &sink, &sinkErr);
    }
    else {
        std::vector<std::string> argv;
        if (proc.hash()->count("argv")) for (auto& x : *(*proc.hash())["argv"].arr()) argv.push_back(x.toStr());
        std::string cwd;
        { auto c = promise.hash()->find("cwd"); if (c != promise.hash()->end()) cwd = c->second.toStr(); }
        spawnCapture(argv, timeoutSec, out, code, timedout, this, &err, cwd, &childPid,
                     nullptr, false, 1, &sink, &sinkErr);
    }
    if (childPid) (*proc.hash())["pid"] = Value::integer(childPid);
    // The stream is closed once its process ended: release a line-splitter's
    // unterminated tail, then fire the tap's :done (TAP's stderr relay is
    // `.act({…}, :done({$err.done}))`) — with or without output, and whatever
    // the exit code.
    auto finishTaps = [&](const char* key) {
        eachTap(key, [&](Value& cb, Value& done, bool, bool lines) {
            if (lines && cb.t == VT::Code) {
                ValueList fin{Value::str(""), Value::boolean(true)};
                callCallable(cb, fin);
            }
            if (done.t == VT::Code) { ValueList none; callCallable(done, none); }
        });
    };
    finishTaps("taps");
    finishTaps("taps-err");
    (*proc.hash())["exitcode"] = Value::integer(code);
    (*proc.hash())["timedout"] = Value::boolean(timedout);
    (*promise.hash())["status"] = Value::str(timedout ? "Broken" : "Kept");
    // A tap block that died threw inside the drain loop, where letting it out
    // would have left the child unreaped and its descriptors open. It was held
    // until here; now that the process is settled, it goes on its way.
    if (sinkErr) std::rethrow_exception(sinkErr);
}

// An attribute's SIGIL is a container type: `has @.a` holds an Array and
// `has %.h` a Hash, whatever shape the initialiser produced. Without this a
// `has @.a = (1,2)` kept the List and `has %.h = (a=>1)` kept the bare Pair, so
// `.WHAT` and the default renderer both disagreed with Rakudo.
Value coerceToSigil(Value v, char sigil) {
    if (sigil == '@') {
        // …and the attribute OWNS its buffer. Construction is assignment, not
        // binding: `T.new(pts => @src)` fills @!pts FROM @src, and passing the
        // caller's array through here let the class's own later
        // `@!pts = @!pts.pairs` rewrite @src under the caller's feet
        // (Algorithm::KDimensionalTree).
        if (v.t == VT::Array) {
            v.isList = false; v.itemized = false;
            if (v.arr() && !v.payloadUnique()) {
                Value own = Value::array();
                *own.arr() = *v.arr();
                own.ofTypeM() = v.ofType();
                own.elemDefaultM() = v.elemDefault();
                return own;
            }
            return v;
        }
        if (v.t == VT::Nil || v.t == VT::Any) return v;
        Value a = Value::array();
        if (v.t == VT::Range) *a.arr() = v.flatten(); else a.arr()->push_back(v);
        return a;
    }
    if (sigil == '%') {
        if (v.t == VT::Hash) return v;
        if (v.t == VT::Nil || v.t == VT::Any) return v;
        Value h = Value::makeHash();
        ValueList items;
        if (v.t == VT::Array && v.arr()) items = *v.arr();
        // an object that says what it holds (`.list`/`.iterator`, declared or
        // delegated) spreads its pairs here rather than vanishing — a `%` attribute
        // initialised from such a wrapper is how zef hands its config around
        else if (!(v.t == VT::Object && g_objListItems && g_objListItems(v, items)))
            items = ValueList{v};
        for (auto& e : items)
            if (e.t == VT::Pair) (*h.hash())[e.s] = e.pairVal() ? *e.pairVal() : Value::any();
        return h;
    }
    return v;
}

bool defined(const Value& v) { return rtIsDefined(v); } // one rule (rtIsDefined owns it: enum type objects are undefined)

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
    size_t c = h.ofType().find(',');
    return c == std::string::npos ? "" : h.ofType().substr(c + 1);
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
    if (stored.pairKey()) return *stored.pairKey();
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
        else if (c == '\b') o += "\\b";
        // any other C0 control (and DEL) has no literal spelling — it went out
        // RAW, so `"\x[3]".raku` printed a string that looked empty and could
        // not be read back. Rakudo's form is `\x[1B]`, hex digits upper-case.
        else if (c < 0x20 || c == 0x7F) {
            static const char* H = "0123456789ABCDEF";
            o += "\\x[";
            if (c >= 0x10) o += H[c >> 4];
            o += H[c & 0xF];
            o += ']';
        }
        else o += (char)c;
    }
    return o + "\"";
}
static bool rakuIdentKey(const std::string& s) {
    if (s.empty() || !(ascii::isalpha((unsigned char)s[0]) || s[0] == '_')) return false;
    for (unsigned char c : s) if (!(ascii::isalnum(c) || c == '_' || c == '-')) return false;
    return true;
}
std::string rakuRepr(const Value& v, int depth, std::set<const void*>& seen) {
    forceLazy(v);   // an unpulled gather renders its ELEMENTS, not `().Seq`
    // an ENDLESS sequence renders its cached prefix and MARKS the rest, Rakudo
    // style — nested occurrences included. (The .raku method arm pre-materialises
    // 100 elements for the top-level call; a nested one shows what is cached.)
    if (v.t == VT::Array && v.arr() && v.ext() &&
        std::static_pointer_cast<LazySeqState>(v.ext())->infinite) {
        std::string out = "(";
        for (size_t i = 0; i < v.arr()->size() && i < 100; i++) {
            if (i) out += ", ";
            out += rakuRepr((*v.arr())[i], depth + 1, seen);
        }
        return out + "...).lazy.Seq";
    }
    // `.raku` of a Proxy shows the VALUE it holds, not its FETCH/STORE pair —
    // URI::Query hands back lists of Proxy containers to keep them immutable.
    if (v.t == VT::Hash && v.hashKind == "Proxy" && v.hash() && g_deproxy)
        return rakuRepr(g_deproxy(v), depth, seen);
    // Guard against self-referential / deeply-nested data (`$foo<b> = $foo`): recursing
    // blindly builds an unbounded string and exhausts memory. Detect a revisited
    // container (a cycle) and stop; a large depth cap backstops pathological nesting.
    if (depth > 512) return "...";
    if (v.isAllomorph()) { // IntStr.new(42, "42") — round-trips via EVAL
        Value num = v; num.hashKind.clear();
        std::string face = num.s; num.s.clear();
        return v.typeName() + ".new(" + rakuRepr(num, depth + 1, seen) + ", " + rakuStrLit(face) + ")";
    }
    // An enum VALUE renders QUALIFIED — `Order::Less`, `Colour::Red` — because
    // .raku must round-trip through EVAL and the bare key need not be in scope.
    // Bool already spells itself out below; every other enum answered the bare
    // key, which is what `3 cmp 5` .raku showing `Less` instead of `Order::Less`
    // came from. Junctions tag themselves with enumName too (any/all/one/none)
    // and are NOT enums — they carry no enumType, which is what tells them apart.
    if (!v.enumName.empty() && !v.enumType.empty() && v.t != VT::Bool)
        return v.enumType.str() + "::" + v.enumName.str();
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
                               : v.hashKind + (v.ofType().empty() ? "" : "[" + v.ofType() + "]");
                std::string o = nm + ".new("; bool f = true;
                for (auto& e : v.blobList()) { if (!f) o += ","; f = false; o += std::to_string(e.toInt()); }
                return o + ")";
            }
            // an IO::Path's .raku is its full constructor, SPEC and CWD included
            // (its .gist is the short `"foo/bar".IO` form)
            if (v.hashKind == "IO") {
                char cbuf[4096];
                std::string cwd = v.ofType().empty()                      // an explicit :CWD wins
                                ? (getcwd(cbuf, sizeof cbuf) ? cbuf : ".")
                                : v.ofType();
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
            std::string n = v.ratN() ? v.ratN()->toString() : "0";
            std::string d = v.ratD() ? v.ratD()->toString() : "1";
            if (v.fatRat()) return "FatRat.new(" + n + ", " + d + ")"; // FatRat.raku is explicit
            // Terminating decimal (denominator 2^a·5^b) prints as a decimal literal
            // with a fraction part kept, so EVAL round-trips to Rat: 0.25, -7.0, 0.1.
            // Anything else (incl. zero-denominator, or a denominator wider than
            // uint64 — 0.9999999999999999999999.raku) is the <n/d> form.
            if (v.ratD() && !v.ratD()->isZero() && v.ratD()->fitsU64()) {
                BigInt den = *v.ratD(); int p2 = 0, p5 = 0; BigInt q, r;
                while (true) { BigInt::divmod(den, BigInt(2), q, r); if (!r.isZero()) break; den = q; p2++; }
                while (true) { BigInt::divmod(den, BigInt(5), q, r); if (!r.isZero()) break; den = q; p5++; }
                if (den.fitsLL() && den.toLL() == 1) {
                    int k = std::max(p2, p5);
                    BigInt scaled = *v.ratN();
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
            if (g.find('e') == std::string::npos && g.find('E') == std::string::npos)
                g += "e0";
            return g;
        }
        case VT::Match: {
            // Rakudo's Match.raku is a constructor call, not the ｢…｣ gist: the
            // positional captures come back as :list and the named ones as :hash.
            std::string o = "Match.new(:orig(" +
                (v.ext() ? rakuStrLit(*std::static_pointer_cast<std::string>(v.ext())) : rakuStrLit(v.s)) +
                "), :from(" + std::to_string(v.rFrom()) + "), :pos(" + std::to_string(v.rTo()) + ")";
            if (v.arr() && !v.arr()->empty()) {
                o += ", :list((";
                for (size_t k = 0; k < v.arr()->size(); k++) {
                    if (k) o += ", ";
                    o += rakuRepr((*v.arr())[k], depth + 1, seen);
                }
                o += (v.arr()->size() == 1 ? ",))" : "))");
            }
            if (v.hash() && !v.hash()->empty()) {
                o += ", :hash(Map.new((";
                bool first = true;
                for (auto& kv : *v.hash()) {
                    if (!first) o += ", ";
                    first = false;
                    o += ":" + kv.first + "(" + rakuRepr(kv.second, depth + 1, seen) + ")";
                }
                o += ")))";
            }
            return o + ")";
        }
        case VT::Regex:
            return v.s.find('/') == std::string::npos ? "rx/" + v.s + "/" : "rx{" + v.s + "}";
        case VT::Complex: return "<" + v.gist() + ">";
        case VT::Range:
            if (v.ofType() == "Str") // Str range: quoted endpoint form
                return "\"" + cpToU8((uint32_t)v.rFrom()) + "\"" + (v.rExFrom() ? "^" : "") + ".." +
                       (v.rExTo() ? "^" : "") + "\"" + cpToU8((uint32_t)v.rTo()) + "\"";
            // an endless endpoint is Inf, not the long long it is parked in, and
            // `^Inf` keeps the long form (0..^Inf) — gist already spells both
            if (v.rTo() >= 9000000000000000000LL || v.rFrom() <= -9000000000000000000LL)
                return v.gist();
            // `0..^N` is `^N` here too — Rakudo shows the short form for .raku as
            // well as gist. Same Int-zero-only rule as Value::gist.
            if (!v.rExFrom() && v.rExTo() && v.rFrom() == 0 && !v.rNum())
                return "^" + std::to_string(v.rTo());
            // A fractional range keeps its real endpoints in n/im and its integer
            // fields are their floors, so this printed `1.5..2.5` as `1..2`.
            // gist already spells the endpoints, including a Rat's.
            if (v.rNum()) return v.gist();
            return std::to_string(v.rFrom()) + (v.rExFrom() ? "^" : "") + ".." + (v.rExTo() ? "^" : "") + std::to_string(v.rTo());
        case VT::Pair: {
            Value val = v.pairVal() ? *v.pairVal() : Value::nil();
            if (v.pairKey()) { // non-string key (Int, nested Pair, …)
                std::string krepr = rakuRepr(*v.pairKey(), depth + 1, seen);
                if (v.pairKey()->t == VT::Pair) krepr = "(" + krepr + ")"; // parenthesize a pair-key
                return krepr + " => " + rakuRepr(val, depth + 1, seen);
            }
            return rakuIdentKey(v.s) ? ":" + v.s + "(" + rakuRepr(val, depth + 1, seen) + ")"
                                     : rakuStrLit(v.s) + " => " + rakuRepr(val, depth + 1, seen);
        }
        case VT::Array: {
            if (v.s == "Slip" && (!v.arr() || v.arr()->empty())) return "Empty";
            if (v.hashKind == "Capture") { // \(…) literal round-trips as itself
                std::string o = "\\(";
                bool first = true;
                if (v.arr()) for (auto& e : *v.arr()) {
                    if (!first) o += ", "; first = false;
                    if (e.t == VT::Pair) o += ":" + e.s + "(" + rakuRepr(e.pairVal() ? *e.pairVal() : Value(), depth + 1, seen) + ")";
                    else o += rakuRepr(e, depth + 1, seen);
                }
                return o + ")";
            }
            // Junctions render as their constructor form: none(1, 2, 3)
            if (!v.enumName.empty() && v.arr() &&
                (v.enumName == "any" || v.enumName == "all" || v.enumName == "one" || v.enumName == "none")) {
                std::string o = v.enumName + "(";
                bool first = true;
                for (auto& e : *v.arr()) { if (!first) o += ", "; first = false; o += rakuRepr(e, depth + 1, seen); }
                return o + ")";
            }
            if (v.arr() && !seen.insert(v.arr()).second) return v.isList ? "(...)" : "[...]"; // cycle
            std::string o(1, v.isList ? '(' : '[');
            bool wasElem = g_reprInArrayElem;
            if (v.arr()) {
                bool first = true;
                g_reprInArrayElem = !v.isList;
                for (auto& e : *v.arr()) { if (!first) o += ", "; first = false; o += rakuRepr(e, depth + 1, seen); }
                g_reprInArrayElem = wasElem;
                if (v.isList && v.arr()->size() == 1) o += ",";
                // a 1-element ARRAY holding an iterable disambiguates with a
                // trailing comma too: [1..5,] (else the raku form would flatten)
                if (!v.isList && v.arr()->size() == 1 &&
                    ((*v.arr())[0].t == VT::Range || (*v.arr())[0].t == VT::Array ||
                     (*v.arr())[0].t == VT::Hash))
                    o += ",";
                seen.erase(v.arr());
            }
            o += v.isList ? ')' : ']';
            // a Seq's .raku is the list form plus the coercion that rebuilds it:
            // `(1, 2).Seq`. Only .raku carries it — .gist/.Str stay `(1 2)`.
            // It goes on BEFORE the itemization marker: the `$( … )` has to
            // enclose the coercion as well, or `$(().Seq)` reads back as the
            // itemized empty list with a `.Seq` called on it.
            bool seq = v.isList && v.s == "Seq";
            if (seq) o += ".Seq";
            // an ITEMIZED container carries its `$` marker — `($t,)` for a
            // `$`-held list is `($(1, 2),)` — except as an ARRAY element, whose
            // slot itemizes anyway (`[$x,]` is `[[1, 2],]`)
            if (v.itemized && !wasElem) {
                if (seq) o = "$(" + o + ")";
                else if (v.isList && v.arr() && v.arr()->empty()) o = "$( )"; // Rakudo's empty item
                else o = "$" + o;
            }
            return o;
        }
        case VT::Hash: {
            if (v.hash() && !seen.insert(v.hash()).second) return "{...}"; // cycle
            std::vector<std::string> keys;
            if (v.hash()) for (auto& kv : *v.hash()) keys.push_back(kv.first);
            std::sort(keys.begin(), keys.end());
            // A QuantHash renders as the expression that rebuilds it, not as a
            // Hash literal: the weighted kinds as a pair list coerced to the
            // kind, the Set family as a constructor over their elements, and a
            // Map as Map.new((…)).
            // An ELEMENT renders as itself, not as its key string: the key is a
            // lookup string, and the original value rides in the count's pairKey
            // (baggyKey). `set(1,2).raku` is `Set.new(1,2)`, not `Set.new("1","2")`.
            auto elemRepr = [&](const std::string& k) {
                const Value& cnt = v.hash()->at(k);
                return cnt.pairKey() ? rakuRepr(*cnt.pairKey(), depth + 1, seen) : rakuStrLit(k);
            };
            // An EMPTY immutable one renders as its SUB form — `set()`, `bag()`,
            // `mix()` — which is how Rakudo writes it and what round-trips. The
            // constructor spelling is kept for the non-empty case, and for the
            // mutable *Hash kinds, which stay `SetHash.new()` / `().BagHash`
            // however empty they are.
            if (keys.empty() &&
                (v.hashKind == "Set" || v.hashKind == "Bag" || v.hashKind == "Mix")) {
                if (v.hash()) seen.erase(v.hash());
                std::string n = v.hashKind.str();
                for (auto& ch : n) ch = (char)ascii::tolower((unsigned char)ch);
                return n + "()";
            }
            if (v.hashKind == "Set" || v.hashKind == "SetHash") {
                std::string o = v.hashKind + ".new("; bool f = true;
                for (auto& k : keys) { if (!f) o += ","; f = false; o += elemRepr(k); }
                if (v.hash()) seen.erase(v.hash());
                return o + ")";
            }
            if (v.hashKind == "Bag" || v.hashKind == "BagHash" ||
                v.hashKind == "Mix" || v.hashKind == "MixHash") {
                std::string o = "("; bool f = true;
                for (auto& k : keys) {
                    if (!f) o += ","; f = false;
                    o += elemRepr(k) + "=>" + rakuRepr(v.hash()->at(k), depth + 1, seen);
                }
                if (v.hash()) seen.erase(v.hash());
                return o + ")." + v.hashKind;
            }
            if (v.hashKind == "Map") {
                std::string o = "Map.new(("; bool f = true;
                for (auto& k : keys) {
                    if (!f) o += ","; f = false;
                    Value val = v.hash()->at(k);
                    o += rakuIdentKey(k) ? ":" + k + "(" + rakuRepr(val, depth + 1, seen) + ")"
                                         : rakuStrLit(k) + " => " + rakuRepr(val, depth + 1, seen);
                }
                if (v.hash()) seen.erase(v.hash());
                return o + "))";
            }
            // An OBJECT hash renders as the DECLARATION that rebuilds it —
            // `(my Any %{Int} = 3 => "a")` — because `{3 => "a"}` would round-trip
            // through EVAL as a plain Str-keyed hash and lose the constraint. The
            // entries are the same as below, except the key is its real value.
            const std::string okt = objHashKeyType(v);
            if (!okt.empty()) {
                std::string vt = v.ofType().substr(0, v.ofType().find(','));
                std::string o = "(my " + (vt.empty() ? "Any" : vt) + " %{" + okt + "}";
                bool f = true;
                for (auto& k : keys) {
                    o += f ? " = " : ", "; f = false;
                    Value val = v.hash()->at(k);
                    if (val.t == VT::Array || val.t == VT::Hash) val.itemized = true;
                    std::string rv = rakuRepr(val, depth + 1, seen);
                    Value rk = hashEntryKey(v, k, v.hash()->at(k));
                    o += (rk.t == VT::Str && rakuIdentKey(k))
                             ? ":" + k + "(" + rv + ")"
                             : rakuRepr(rk, depth + 1, seen) + " => " + rv;
                }
                if (v.hash()) seen.erase(v.hash());
                return o + ")";
            }
            std::string o = "{"; bool first = true;
            for (auto& k : keys) {
                if (!first) o += ", "; first = false;
                Value val = v.hash()->at(k);
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
            if (v.hash()) seen.erase(v.hash());
            o += "}";
            // an itemized hash (one held in a `$`) shows the same `$` marker a
            // list does — except as an Array element, whose slot itemizes anyway
            if (v.itemized && !g_reprInArrayElem) o = "$" + o;
            return o;
        }
        case VT::Object: {
            if (!v.obj() || !v.obj()->cls) return v.gist();
            std::string r = v.obj()->cls->name + ".new";
            // INHERITED attributes count: iterating only cls->attrs dropped every
            // one, so `class Q is P` reprd as `Q.new(q => 2)` and would not survive
            // a round trip through EVAL.
            std::vector<const ClassAttr*> pub;
            collectPubAttrs(v.obj()->cls.get(), pub);
            std::string inner;
            for (auto* at : pub) {
                auto it = v.obj()->attrs.find(at->name);
                // an UNSET typed attribute shows its declared type (`i => Int`)
                Value av = it != v.obj()->attrs.end() ? it->second
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
    if (k.size() >= 2 && ascii::isdigit((unsigned char)k[0])) { // :2nd / :3x
        size_t d = 0; while (d < k.size() && ascii::isdigit((unsigned char)k[d])) d++;
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
    if (mt.t == VT::Array && mt.arr() &&
        (mt.enumName == "any" || mt.enumName == "all" ||
         mt.enumName == "one" || mt.enumName == "none")) {
        size_t hits = 0;
        for (auto& e : *mt.arr()) if (matcherAccepts(I, v, e)) hits++;
        if (mt.enumName == "any")  return hits > 0;
        if (mt.enumName == "all")  return hits == mt.arr()->size();
        if (mt.enumName == "one")  return hits == 1;
        return hits == 0;                                     // none
    }
    if (mt.t == VT::Code) return predAnswerTruthy(I, I.callCallable(const_cast<Value&>(mt), ValueList{v}), v);
    // A matcher OBJECT (one whose class defines ACCEPTS) is what `.grep`/`.first`
    // get handed when the pattern is a custom matcher — `.grep(glob("*.txt"))`.
    // applyArith knows nothing about ACCEPTS, so those greps came back empty.
    if (mt.t == VT::Object && mt.obj() && mt.obj()->cls) {
        for (ClassInfo* ci = mt.obj()->cls.get(); ci; ci = ci->parent.get())
            if (ci->methods.count("ACCEPTS")) {
                ValueList one{v};
                return I.methodCall(const_cast<Value&>(mt), "ACCEPTS", one).truthy();
            }
    }
    return I.smartmatchValue("~~", v, mt).truthy(); // an element that IS `*` is a value, not a curry
}

// Truth of a CODE matcher's ANSWER, for element `elem`. A block may answer a
// Regex — `{ .defined && /re/ }` returns the `&&` right side as the object —
// and Rakudo boolifies that answer by MATCHING it against the element, writing
// the caller's `$/` (Regex.Bool reads the topic and stores the match through
// getlexcaller). Reading the Regex value's plain truth instead was always-true
// and left `$/` unset: HTTP::Tiny's multipart-boundary `~$/` came back ""
// (battery regression, v3.20.1). The closure's frame is already popped when
// this runs, so setMatchVar lands the match in the CALLER's `$/`, which is
// exactly where Rakudo puts it.
bool predAnswerTruthy(Interpreter& I, const Value& res, const Value& elem) {
    if (res.t == VT::Regex) {
        Value m = I.regexMatch(elem.toStr(), res.s);
        bool t = m.truthy();
        I.setMatchVar(std::move(m));
        return t;
    }
    return res.truthy();
}

// The positional arity of a Code value — how many elements `.map`/`for` feed it
// per iteration (`-> $k,$v {…}` → 2; `{ $^a … $^b }` → 2; `{ $_ }` / builtin → 1).
size_t codeArity(const Value& code) {
    if (code.t != VT::Code || !code.code()) return 1;
    if (code.code()->params) {
        size_t n = 0;
        for (auto& p : *code.code()->params) if (!p.named && !p.slurpy) n++;
        if (n) return n;
    }
    if (!code.code()->placeholders.empty()) return code.code()->placeholders.size();
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
// How many leading bytes of `s` are plain ASCII, looking at no more than
// `limit` of them. Where the run reaches, a codepoint index IS a byte index,
// which is what lets the nqp string ops skip utf8cp() altogether — see the
// comment above their cases in rtNqpOp. Eight bytes at a time: these ops are
// how pure-Raku tokenizers walk text, so this runs once per character scanned.
size_t asciiRun(const std::string& s, size_t limit) {
    size_t n = std::min(limit, s.size()), i = 0;
    for (; i + 8 <= n; i += 8) {
        uint64_t w;
        std::memcpy(&w, s.data() + i, 8);
        if (w & 0x8080808080808080ULL) break;   // a high bit somewhere in this word
    }
    for (; i < n; i++) if ((unsigned char)s[i] & 0x80) break;
    return i;
}
bool allAscii(const std::string& s) { return asciiRun(s, s.size()) == s.size(); }

// The same three questions, answered once per string instead of once per
// character. A CowStr long enough to have been promoted carries an immutable
// body (StrBody), and these properties are pure functions of that text — so the
// answer can be cached there. Without the cache the "ASCII fast path" in the
// scanning ops below still walked the prefix on every single call, which is
// what made a pure-Raku tokenizer quadratic no matter how cheap the walk was.
// A short (unpromoted) string has no body; rescanning ~64 bytes is free.
bool cowAllAscii(const CowStr& s) {
    const StrBody* b = s.body();
    if (!b) return allAscii(s.str());
    signed char c = b->allAscii.load(std::memory_order_relaxed);
    if (c < 0) {
        c = allAscii(b->text) ? 1 : 0;
        b->allAscii.store(c, std::memory_order_relaxed);
    }
    return c == 1;
}
bool cowByteIsGraphemeIndex(const CowStr& s) {
    const StrBody* b = s.body();
    if (!b) return byteIsGraphemeIndex(s.str());
    if (!cowAllAscii(s)) return false;
    signed char c = b->crFree.load(std::memory_order_relaxed);
    if (c < 0) {
        c = std::memchr(b->text.data(), '\r', b->text.size()) == nullptr ? 1 : 0;
        b->crFree.store(c, std::memory_order_relaxed);
    }
    return c == 1;
}
long long cowGraphemeCount(const CowStr& s) {
    const StrBody* b = s.body();
    if (!b) return graphemeCount(s.str());
    long long n = b->nGraphemes.load(std::memory_order_relaxed);
    if (n < 0) {
        n = cowByteIsGraphemeIndex(s) ? (long long)b->text.size()
                                      : (long long)uniGraphemeCount(utf8cp(b->text));
        b->nGraphemes.store(n, std::memory_order_relaxed);
    }
    return n;
}

// The positional-op tables for NON-ASCII text (see StrBody in Value.h).
// cowCpIndex: byte offset of every codepoint + an end sentinel — the nqp ops
// (ordat/eqat/substr/index/…) index codepoints. cowGraphemeIndex: byte offset
// of every grapheme + sentinel — the Raku-level methods index graphemes. Both
// answer nullptr for a short (unpromoted) string, where a per-call rescan of
// ≤64 bytes is free and there is no body to cache on. Built at most once per
// body; the CAS loser deletes its copy.
const std::vector<uint32_t>* cowCpIndex(const CowStr& s) {
    const StrBody* b = s.body();
    if (!b) return nullptr;
    const std::vector<uint32_t>* t = b->cpIndex.load(std::memory_order_acquire);
    if (t) return t;
    const std::string& x = b->text;
    auto* mine = new std::vector<uint32_t>;
    mine->reserve(x.size() / 2 + 2);
    for (size_t i = 0; i < x.size(); i++)
        if ((static_cast<unsigned char>(x[i]) & 0xC0) != 0x80) mine->push_back((uint32_t)i);
    mine->push_back((uint32_t)x.size());
    const std::vector<uint32_t>* expect = nullptr;
    if (b->cpIndex.compare_exchange_strong(expect, mine, std::memory_order_acq_rel))
        return mine;
    delete mine;
    return expect;
}
const std::vector<uint32_t>* cowGraphemeIndex(const CowStr& s) {
    const StrBody* b = s.body();
    if (!b) return nullptr;
    const std::vector<uint32_t>* t = b->gIndex.load(std::memory_order_acquire);
    if (t) return t;
    const std::string& x = b->text;
    const std::vector<uint32_t>* ci = cowCpIndex(s); // byte offset per codepoint
    auto cps = utf8cp(x);
    auto* mine = new std::vector<uint32_t>;
    if (ci && cps.size() + 1 == ci->size()) {
        auto starts = uniGraphemeStarts(cps); // cluster starts, codepoint space
        mine->reserve(starts.size() + 1);
        for (size_t g : starts) mine->push_back((*ci)[g]);
        mine->push_back((uint32_t)x.size());
    }
    // else: decode disagreement (malformed UTF-8) — install the EMPTY table as
    // a cached negative so the disagreement is not re-derived per call; callers
    // treat empty as "no table" and keep the legacy path
    const std::vector<uint32_t>* expect = nullptr;
    if (!b->gIndex.compare_exchange_strong(expect, mine, std::memory_order_acq_rel)) {
        delete mine;
        mine = const_cast<std::vector<uint32_t>*>(expect);
    }
    return mine->empty() ? nullptr : mine;
}
// Decode the ONE codepoint whose lead byte sits at `b` — the per-call
// replacement for decoding the whole string.
uint32_t cpAtByte(const std::string& s, size_t b) {
    unsigned char c = (unsigned char)s[b];
    if (c < 0x80) return c;
    int w = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : (c >= 0xC0) ? 2 : 1;
    static const uint32_t mask[5] = {0, 0x7F, 0x1F, 0x0F, 0x07};
    uint32_t cp = c & mask[w];
    for (int i = 1; i < w && b + i < s.size(); i++)
        cp = (cp << 6) | ((unsigned char)s[b + i] & 0x3F);
    return cp;
}

// True when a BYTE index into `s` is also a GRAPHEME index — which is what Raku
// string positions actually are. Needs two things: every byte ASCII (so one byte
// is one codepoint), and no CR, because CR LF is the one ASCII sequence that
// clusters (GB3, which is why "a\r\nb".chars is 3). When it holds, .chars is a
// byte count and .substr is a byte slice, with nothing to decode.
bool byteIsGraphemeIndex(const std::string& s) {
    return allAscii(s) && std::memchr(s.data(), '\r', s.size()) == nullptr;
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
bool strHasNoUpper(const std::string& s) {
    for (auto c : utf8cp(s)) if (toLowerCp(c) != c) return false;
    return true;
}
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
    // ASCII is a byte map, and the general path below is a poor way to do one:
    // it decodes to codepoints, segments graphemes, allocates a case-mapping
    // vector per character and re-normalises the result. None of that can change
    // an ASCII answer — there are no multi-codepoint graphemes to segment, no
    // Final_Sigma (Greek), no multi-codepoint expansions (ß, ﬁ) and nothing for
    // NFC to compose. `.uc` on a short ASCII string was the single hottest thing
    // in a dispatch-heavy profile, almost all of it allocation.
    if (allAscii(s)) {
        std::string r = s;
        if (r.empty()) return r;
        if (tcMode) {
            r[0] = (char)ascii::toupper((unsigned char)r[0]);         // title == upper in ASCII
            if (tcMode == 2)
                for (size_t i = 1; i < r.size(); i++) r[i] = (char)ascii::tolower((unsigned char)r[i]);
            return r;
        }
        if (kind == 0 || kind == 3)                                                   // 0 = lc; 3 = fc, which
             for (char& c : r) c = (char)ascii::tolower((unsigned char)c);            // folds to lower in ASCII
        else for (char& c : r) c = (char)ascii::toupper((unsigned char)c);            // 1 = uc, 2 = tc
        return r;
    }
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
            // Final_Sigma (SpecialCasing): lowercased word-final Σ is ς — preceded
            // by a letter and not followed by one (case-ignorables approximated)
            if (k == 0 && cps[i] == 0x03A3) {
                auto isL = [](uint32_t c) { std::string g = uniGeneralCategory(c); return !g.empty() && g[0] == 'L'; };
                bool prevL = i > 0 && isL(cps[i - 1]);
                bool nextL = i + 1 < cps.size() && isL(cps[i + 1]);
                if (prevL && !nextL) { out.push_back(0x03C2); continue; }
            }
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
long long cpCount(const std::string& s) {
    if (allAscii(s)) return (long long)s.size();   // one byte, one codepoint
    return (long long)utf8cp(s).size();
}

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
long long graphemeCount(const std::string& s) {
    // `.chars` on a long ASCII string was the worst of the quadratics: called
    // once per character it decoded the whole text AND ran the full UAX #29 walk
    // over it, 11.5 s for a 30k scan. A byte count answers it outright.
    if (byteIsGraphemeIndex(s)) return (long long)s.size();
    return (long long)uniGraphemeCount(utf8cp(s));
}

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
    // NFC-composed, as concatenation is: joining ("a", COMBINING RING) yields
    // the composed grapheme in Rakudo's NFG strings
    return nfcNormalize(std::move(out));
}

// A lazy @-array over the integers from `start` upward (an infinite `…..Inf` range).
Value makeInfArray(long long start) {
    Value a = Value::array(); a.isList = true;
    auto st = std::make_shared<LazySeqState>(); st->infinite = true;
    auto next = std::make_shared<long long>(start);
    st->appendNext = [next](ValueList& cache) -> bool { cache.push_back(Value::integer((*next)++)); return true; };
    a.extM() = st;
    return a;
}

ValueList toList(const Value& v) {
    if (v.t == VT::Array && v.arr()) return *v.arr();
    if (v.t == VT::Range) return v.flatten();
    // a Blob/Buf lists as its ELEMENTS (`$blob.rotor(3, :partial)` in Base64;
    // 32-bit words for blob32) — mirrors the `for`-iteration rule in the
    // interpreter (itemized stays one item)
    if (v.t == VT::Str && !v.itemized && (v.hashKind == "Blob" || v.hashKind == "Buf"))
        return v.blobList();
    if (v.t == VT::Hash && v.hash()) {
        ValueList out;
        // The object-hash test is hoisted: this is the hot path for every hash
        // iteration, and a plain hash must cost exactly what it did before —
        // one shared_ptr copy, not a Value built and thrown away per entry.
        const bool objHash = !objHashKeyType(v).empty();
        for (auto& kv : *v.hash()) {
            Value p = Value::pair(kv.first, kv.second);
            if (kv.second.pairKey()) p.pairKeyM() = kv.second.pairKey();
            else if (objHash) {
                Value rk = hashEntryKey(v, kv.first, kv.second);
                if (rk.t != VT::Str) p.pairKeyM() = std::make_shared<Value>(std::move(rk));
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
static std::string fmtBigDec(std::string digits, const std::string& flags, long long width, long long prec = -1) {
    bool neg = !digits.empty() && digits[0] == '-';
    std::string sign = neg ? "-" : (flags.find('+') != std::string::npos ? "+" :
                                    flags.find(' ') != std::string::npos ? " " : "");
    if (neg) digits = digits.substr(1);
    // integer precision zero-pads the digit run itself (`%32.32x` of a 128-bit value)
    if (prec > (long long)digits.size()) digits = std::string(prec - digits.size(), '0') + digits;
    std::string body = sign + digits;
    if ((long long)body.size() >= width) return body;
    if (flags.find('-') != std::string::npos) return body + std::string(width - body.size(), ' ');
    if (flags.find('0') != std::string::npos)
        return sign + std::string(width - body.size(), '0') + digits;
    return std::string(width - body.size(), ' ') + body;
}

std::string doSprintf(const std::string& fmt, const ValueList& args, int langRev) {
    std::string out;
    size_t ai = 0;                 // the IMPLICIT cursor
    long long valIdx = -1;         // this directive's explicit `%N$`, 1-based; -1 = none
    auto argAt = [&](long long n1) -> Value {   // 1-based, out of range → Any
        size_t k = (size_t)(n1 - 1);
        return n1 >= 1 && k < args.size() ? args[k] : Value::any();
    };
    // An EXPLICIT index selects its argument and leaves the implicit cursor
    // exactly where it was: `sprintf('%2$d %d %d', 1, 2, 3)` is "2 1 2", not
    // "2 3 0" — the two implicit directives still read 1 then 2. (C and Perl 5
    // both work this way; so does Rakudo outside 6.e.)
    auto nextArg = [&]() -> Value {
        if (valIdx >= 1) return argAt(valIdx);
        return ai < args.size() ? args[ai++] : Value::any();
    };
    // A `*` width/precision takes its own argument: `%N$` if it carries one
    // (`%2$*1$d`), otherwise the next implicit one — never the directive's.
    auto starArg = [&](long long n1) -> Value {
        if (n1 >= 1) return argAt(n1);
        return ai < args.size() ? args[ai++] : Value::any();
    };
    // Parse an optional `digits $` at k, returning the 1-based index (or -1) and
    // consuming it only when the `$` is really there — a bare width like `%2d`
    // must be left alone.
    auto takeIndex = [&](size_t& k) -> long long {
        size_t d = k;
        while (d < fmt.size() && ascii::isdigit((unsigned char)fmt[d])) d++;
        if (d > k && d < fmt.size() && fmt[d] == '$') {
            long long n = std::atoll(fmt.substr(k, d - k).c_str());
            k = d + 1;
            return n;
        }
        return -1;
    };
    for (size_t i = 0; i < fmt.size(); i++) {
        if (fmt[i] != '%') { out += fmt[i]; continue; }
        size_t j = i + 1;
        valIdx = takeIndex(j); // explicit positional argument: %2$s
        std::string flags;
        while (j < fmt.size() && std::strchr("-+ 0#", fmt[j])) flags += fmt[j++];
        // width (digits or `*` = from argument; negative `*` implies left-justify)
        const long long SPRINTF_MAX = 10'000'000; // guard against int overflow (UB) and multi-GB pads
        int width = 0; bool hasWidth = false;
        if (j < fmt.size() && fmt[j] == '*') { j++; long long w = starArg(takeIndex(j)).toInt();
            if (w < 0) { flags += '-'; w = -w; } if (w > SPRINTF_MAX) w = SPRINTF_MAX; width = (int)w; hasWidth = true; }
        else { long long w = 0;
            while (j < fmt.size() && ascii::isdigit((unsigned char)fmt[j])) { w = w * 10 + (fmt[j]-'0'); if (w > SPRINTF_MAX) w = SPRINTF_MAX; hasWidth = true; j++; }
            width = (int)w; }
        // precision (.digits or .* ; a negative `.*` means "no precision")
        int prec = -1;
        if (j < fmt.size() && fmt[j] == '.') { j++; prec = 0;
            if (j < fmt.size() && fmt[j] == '*') { j++; long long p = starArg(takeIndex(j)).toInt(); prec = p < 0 ? -1 : (int)std::min(p, SPRINTF_MAX); }
            else { long long p = 0; while (j < fmt.size() && ascii::isdigit((unsigned char)fmt[j])) { p = p * 10 + (fmt[j]-'0'); if (p > SPRINTF_MAX) p = SPRINTF_MAX; j++; } prec = (int)p; }
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
                if (av.t == VT::Int && av.big()) { out += fmtBigDec(av.big()->toString(), flags, width, prec); break; }
                if (av.t == VT::Rat && av.ratN() && av.ratD() && !av.ratD()->isZero()) {
                    BigInt q, r; BigInt::divmod(*av.ratN(), *av.ratD(), q, r);
                    if (q.toString().size() > 18) { out += fmtBigDec(q.toString(), flags, width, prec); break; }
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
                if (av.t == VT::Int && av.big()) { // arbitrary-precision: exact digits
                    out += fmtBigDec(bigRadixDigits(*av.big(), radix, upper), flags2, width, prec);
                    break;
                }
                if (av.t == VT::Rat && av.ratN() && av.ratD() && !av.ratD()->isZero()) {
                    BigInt q, r;
                    BigInt::divmod(*av.ratN(), *av.ratD(), q, r); // truncate toward zero
                    if (q.toString().size() > 18) {
                        out += fmtBigDec(bigRadixDigits(q, radix, upper), flags2, width, prec);
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
                        cnum::snprintf(buf.data(), buf.size(), spec.c_str(), fv);
                        out += buf.data(); break;
                    }
                }
                std::string spec = "%" + ff;
                if (hasWidth) spec += std::to_string(width);
                if (prec >= 0) spec += "." + std::to_string(prec);
                spec += conv;
                std::vector<char> buf(std::max(64, width + prec + 64));
                cnum::snprintf(buf.data(), buf.size(), spec.c_str(), fv);
                std::string fs = buf.data();
                if (std::isnan(fv) || std::isinf(fv)) {
                    // Raku spells them NaN / Inf / -Inf. From 6.e %G — and only %G —
                    // uppercases them: `sprintf('%G', -Inf)` is "-INF" there and
                    // "-Inf" at 6.d. (C uppercases for %E and %F too; Raku does not,
                    // and the two Roast copies of S32-str/sprintf.t pin one version
                    // each: 6.d/…:234 wants "Inf", …/sprintf.t:241 wants "INF".)
                    bool up = (conv == 'G' && langRev >= 2);
                    const char* rep = std::isnan(fv) ? (up ? "NAN" : "NaN")
                                                     : (up ? "INF" : "Inf");
                    for (const char* bad : {"nan", "NAN", "inf", "INF"}) {
                        size_t at = fs.find(bad);
                        if (at != std::string::npos) { fs.replace(at, 3, rep); break; }
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

// Structural equality that ignores TYPE at the leaves ("11" == 11). This is
// NOT `eqv` and no longer backs is-deeply, which uses eqv; the one caller
// left is 6.e `snip`, where the value predicate is a smartmatch.
bool deepEq(const Value& a, const Value& b) {
    forceLazy(a); forceLazy(b);   // a lazy gather compares by its ELEMENTS
    // A Proxy is a container: compare what it HOLDS, at any depth. URI::Query
    // hands back lists of Proxy containers to keep them immutable, so
    // `is-deeply $q<foo>, $('1','3')` was comparing containers with strings.
    if (a.t == VT::Hash && a.hashKind == "Proxy" && a.hash() && g_deproxy)
        return deepEq(g_deproxy(a), b);
    if (b.t == VT::Hash && b.hashKind == "Proxy" && b.hash() && g_deproxy)
        return deepEq(a, g_deproxy(b));
    // the undefined value (VT::Any) and the `Any` type object are the same thing
    auto anyish = [](const Value& v) { return v.t == VT::Any || (v.t == VT::Type && v.s == "Any"); };
    if (anyish(a) && anyish(b)) return true;
    // a Junction on either side autothreads (is-deeply $x, 'a'|'b';
    // is-deeply any(1,2,3), none(4,5,6) collapses to True)
    auto junct = [](const Value& v) {
        return v.t == VT::Array && v.arr() &&
               (v.enumName == "any" || v.enumName == "all" || v.enumName == "one" || v.enumName == "none");
    };
    if (junct(b)) {
        int t = 0;
        for (auto& e : *b.arr()) if (deepEq(a, e)) t++;
        return b.enumName == "any" ? t > 0 : b.enumName == "all" ? t == (int)b.arr()->size()
             : b.enumName == "one" ? t == 1 : t == 0;
    }
    if (junct(a)) {
        int t = 0;
        for (auto& e : *a.arr()) if (deepEq(e, b)) t++;
        return a.enumName == "any" ? t > 0 : a.enumName == "all" ? t == (int)a.arr()->size()
             : a.enumName == "one" ? t == 1 : t == 0;
    }
    if (a.t == VT::Array && b.t == VT::Array) {
        if (a.arr()->size() != b.arr()->size()) return false;
        // A Capture's POSITIONALS are ordered but its NAMEDS are a map:
        // \(1, :a, :b) is-deeply \(1, :b, :a). Getopt::Long's suite compares
        // parse-order nameds against source-order literals.
        if (a.hashKind == "Capture" && b.hashKind == "Capture") {
            std::vector<const Value*> ap, bp;
            std::map<std::string, const Value*> an, bn;
            for (auto& e : *a.arr()) { if (e.t == VT::Pair) an[e.s] = &e; else ap.push_back(&e); }
            for (auto& e : *b.arr()) { if (e.t == VT::Pair) bn[e.s] = &e; else bp.push_back(&e); }
            if (ap.size() != bp.size() || an.size() != bn.size()) return false;
            for (size_t i = 0; i < ap.size(); i++) if (!deepEq(*ap[i], *bp[i])) return false;
            for (auto& kv : an) {
                auto it = bn.find(kv.first);
                if (it == bn.end() || !deepEq(*kv.second, *it->second)) return false;
            }
            return true;
        }
        for (size_t i = 0; i < a.arr()->size(); i++)
            if (!deepEq((*a.arr())[i], (*b.arr())[i])) return false;
        return true;
    }
    if (a.t == VT::Hash && b.t == VT::Hash) {
        if (a.hash()->size() != b.hash()->size()) return false;
        for (auto& kv : *a.hash()) {
            auto it = b.hash()->find(kv.first);
            if (it == b.hash()->end() || !deepEq(kv.second, it->second)) return false;
        }
        return true;
    }
    if (a.t == VT::Pair && b.t == VT::Pair) {
        // typed keys (an object key stringifies with its identity now, so the
        // rendering can't stand in for the key) compare structurally
        bool keyOk = a.pairKey() && b.pairKey() ? deepEq(*a.pairKey(), *b.pairKey())
                                            : a.s == b.s;
        return keyOk && deepEq(a.pairVal() ? *a.pairVal() : Value::any(),
                               b.pairVal() ? *b.pairVal() : Value::any());
    }
    if (a.t == VT::Rat && b.t == VT::Rat) // structural (eqv): <0/0> eqv <0/0> is True; toNum would NaN-compare
        return a.fatRat() == b.fatRat() &&
               a.ratN() && b.ratN() && a.ratD() && b.ratD() &&
               BigInt::cmp(*a.ratN(), *b.ratN()) == 0 && BigInt::cmp(*a.ratD(), *b.ratD()) == 0;
    if (a.t == VT::Num && b.t == VT::Num && std::isnan(a.n) && std::isnan(b.n))
        return true; // structural: NaN eqv NaN (numeric == would say false)
    // two objects: structural, not stringified — object .Str carries identity
    // now, so the toStr fallback would call every clone unequal to its source
    if (a.t == VT::Object && b.t == VT::Object)
        return objectStructEqv(a, b, deepEq);
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
        // A Blob/Buf replacement is Positional over its BYTES, and splicing one
        // buffer into another is the ordinary way to build a binary message.
        // It is a VT::Str internally, so it fell to the scalar arm below and
        // went in as ONE byte holding its element COUNT — BSON::Simple wrote
        // `5` where "hello" belonged.
        else if (args[k].t == VT::Str &&
                 (args[k].hashKind == "Buf" || args[k].hashKind == "Blob"))
            for (auto& e : args[k].blobList()) repl += (char)(unsigned char)(e.toInt() & 0xFF);
        else repl += (char)(unsigned char)(args[k].toInt() & 0xFF);
    }
    Value removed = Value::str(buf.s.substr((size_t)from, (size_t)len));
    removed.hashKind = buf.hashKind;
    removed.ofTypeM() = buf.ofType();   // the removed bytes are the same Buf[uint8]
    if (removed.hashKind == "Buf") identify(removed); // a fresh Buf, not the spliced one
    buf.s.replace((size_t)from, (size_t)len, repl);
    return removed;
}

Value Interpreter::bufBitOp(Value& buf, const std::string& m, ValueList& args) {
    std::string& bytes = buf.s.mut();
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
        BigInt v = val.big() ? *val.big() : BigInt(val.toInt());
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
    if (!kind.empty() && ascii::isdigit((unsigned char)kind.back()))
        { size_t d = kind.find_first_of("0123456789"); width = std::atoi(kind.c_str() + d); kind = kind.substr(0, d); }
    if ((kind != "num" && kind != "int" && kind != "uint") ||
        (width != 0 && width != 8 && width != 16 && width != 32 && width != 64 && width != 128) ||
        (kind == "num" && width != 0 && width < 32))
        throw RakuError{Value::typeObj("X::Method::NotFound"), "No such method '" + m + "' for Buf"};
    long long off = args.size() > 0 ? args[0].toInt() : 0;
    size_t vi = 1; // value index for writes; endian index varies
    Value val = (isWrite && args.size() > 1) ? args[1] : Value::number(0);
    int endian = 0;
    for (size_t k = vi + (isWrite ? 1 : 0); k < args.size(); k++)
        if (args[k].t != VT::Pair) { endian = endianOf(args[k]); break; }
    int nb = width ? width / 8 : 8;
    if (off < 0)
        throw RakuError{Value::typeObj("X::OutOfRange"), "offset " + std::to_string(off) + " out of range"};
    // Typed bufs (buf16/32/64): Rakudo addresses `pos` in ELEMENTS — the value
    // lands at byte pos*W — and grows the buffer to (pos + nb) ELEMENTS,
    // zero-filled (verified: buf32.new(1,2,3).write-uint64(3, v) puts v in
    // elems 3..4 and .elems becomes 11). Scale both accordingly; the byte
    // math below is then unchanged. Digest::MD5 appends its bit-length with
    // `$b.write-uint64: $b.elems, $bits` on a buf32 and relies on this.
    int elemW = buf.blobElemSize();
    if (elemW > 1) {
        long long growElems = off + nb;           // Rakudo's quirk: nb ELEMENTS, not bytes
        off *= elemW;
        if (isWrite && (long long)bytes.size() < growElems * elemW)
            bytes.resize((size_t)(growElems * elemW), '\0');
    }
    if (nb > 8) { // int128/uint128: BigInt byte-peeling (the raw[8] fast path below caps at 64 bits)
        if (isWrite) {
            if ((long long)bytes.size() < off + nb) bytes.resize(off + nb, '\0');
            BigInt v = val.big() ? *val.big() : BigInt(val.toInt());
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
            if (val.big()) { // low 64 bits (toInt would saturate past int64)
                BigInt v = *val.big(); if (v.sign < 0) v = v + BigInt(2).pow(64);
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
        if (r.ratN() && r.ratD()) return r.ratN()->toString() + "/" + r.ratD()->toString();
        return r.toStr();
    };
    // a Buf/Instant/Duration is a reference type wearing a scalar's clothes: its
    // identity is the token stamped at construction (see identify() in Value.h),
    // not its bytes or its seconds. Without this `Buf.new(1,2).WHICH` was the
    // useless "Buf|" — the bytes are unprintable — for every buffer alive.
    // A token-less one (a construction site that predates the stamp) keeps the
    // old value rendering rather than claiming to be a different object.
    if (identityScalar(v) && v.ext()) {
        char idb[24];
        std::snprintf(idb, sizeof idb, "|%p", v.ext().get());
        return v.typeName() + idb;
    }
    if (v.isAllomorph()) {
        // the numeric half is the same value with the string side dropped
        Value num = v; num.hashKind.clear(); num.s.clear();
        std::string numName = v.typeName() == "IntStr"     ? "Int"
                            : v.typeName() == "RatStr"     ? "Rat"
                            : v.typeName() == "NumStr"     ? "Num"
                            : v.typeName() == "ComplexStr" ? "Complex" : "Num";
        std::string numId = numName == "Rat" ? ratPart(num) : num.toStr();
        if (numName == "Complex") numId = Value::number(num.n).toStr() + "|" + Value::number(num.im()).toStr();
        return v.typeName() + "|" + numName + "|" + numId + "|Str|" + v.s;
    }
    switch (v.t) {
        case VT::Bool:    return "Bool|" + std::string(v.b ? "1" : "0");
        case VT::Rat:     return "Rat|" + ratPart(v);
        case VT::Complex: return "Complex|" + Value::number(v.n).toStr() + "|" + Value::number(v.im()).toStr();
        // An OBJECT is identified by its address, not by its contents — two
        // instances with equal attributes are different elements of a Set. This
        // lived only in the `.WHICH` arm, so `.WHICH` said they differed while
        // baggyKeyStr (which keys on the RENDERING, `A<obj>` for every instance
        // of A) merged them: `set($x, $y).elems` was 1.
        case VT::Object:  if (v.obj()) {
                              char buf[24];
                              std::snprintf(buf, sizeof buf, "|%p", (void*)v.obj());
                              return v.typeName() + buf;
                          }
                          return v.typeName() + "|<null>";
        // a Range's identity is its GIST, exclusion markers and all — expanding
        // it makes `1..^5` and `1..4` the same value, and builds a huge string
        // for a large range on the way
        case VT::Range:   return "Range|" + v.gist();
        // a CAPTURE is a VALUE — `\(1,2) === \(1,2)` is True in Rakudo, alone
        // among the Arrays — so it identifies by its PARTS, each with its own
        // identity. Rendering them (the old "Capture|1 2") merged `\(1)` with
        // `\("1")` and `\(:a)` with a positional "a".
        case VT::Array:   if (v.hashKind == "Capture") {
                              std::string pos;
                              std::vector<std::string> named; // named parts are unordered
                              if (v.arr()) for (auto& e : *v.arr()) {
                                  if (e.t == VT::Pair && e.namedArg)
                                      named.push_back(":" + e.s + "(" +
                                                      (e.pairVal() ? whichOf(*e.pairVal()) : "") + ")");
                                  else pos += "(" + whichOf(e) + ")";
                              }
                              std::sort(named.begin(), named.end());
                              for (auto& nm : named) pos += nm;
                              return "Capture|" + pos;
                          }
                          return v.typeName() + "|" + v.toStr();
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
// A Bag count must stay EXACT: the old long-long path saturated weights near
// 10^19 at LLONG_MAX, so distinct weights collapsed into equal ones and a
// weighted .roll drew ~uniform. Truncate to an Int through the BigInt tower.
static Value exactIntWeight(const Value& w) {
    if (w.t == VT::Int) return w;                        // Int / IntStr — big-capable as is
    if (w.t == VT::Str && !w.isAllomorph()) {
        Value nv = numifyStr(w.s);                       // callers pre-validate: this parses
        return nv.t == VT::Int ? nv : exactIntWeight(nv);
    }
    if (w.t == VT::Rat && w.ratN() && w.ratD() && !w.ratD()->isZero()) {
        BigInt q, r; BigInt::divmod(*w.ratN(), *w.ratD(), q, r);
        return Value::bigint(q);
    }
    double d = w.toNum();                                // Num / Bool / NumStr
    if (!std::isfinite(d)) return Value::integer(0);     // zero-denominator Rat: old toInt() gave 0
    if (d >= -9.19e18 && d <= 9.19e18) return Value::integer((long long)d);
    char buf[440]; std::snprintf(buf, sizeof buf, "%.0f", d); // a double this big is an exact integer
    return Value::bigint(BigInt::fromString(buf));
}
// pairsAsElements: constructors (set()/Set.new) treat a Pair item as ONE element
// (`set [foo=>1, bar=>2]` has two Pair elements); coercions (.Set/.Bag on a
// Hash, new-from-pairs) keep the pair→count reading.
Value makeBaggy(const ValueList& items, const std::string& kind, bool pairsAsElements) {
    Value h = Value::makeHash();
    h.hashKind = kind;
    bool isSet = kind.find("Set") == 0;
    bool isMix = kind.find("Mix") == 0; // Mix weights keep their full numeric value (2.5 stays a Rat)
    auto add = [&](const std::string& k, const Value& cnt, const std::shared_ptr<Value>& tk) {
        auto it = h.hash()->find(k);
        auto keep = it != h.hash()->end() && it->second.pairKey() ? it->second.pairKey() : tk;
        if (isSet) {
            if (cnt.big() ? cnt.big()->sign > 0 : cnt.i > 0) { Value b = Value::boolean(true); b.pairKeyM() = keep; (*h.hash())[k] = std::move(b); }
            else h.hash()->erase(k);
            return;
        }
        Value c = it != h.hash()->end() ? rtAdd(it->second, cnt) : cnt;
        bool zero = c.big() ? c.big()->isZero() : c.i == 0;
        if (!zero) { c.pairKeyM() = keep; (*h.hash())[k] = std::move(c); }
        else h.hash()->erase(k);
    };
    for (auto& v : items) {
        if (v.t == VT::Pair && pairsAsElements) {
            add(baggyKeyStr(v), Value::integer(1), std::make_shared<Value>(v)); // the Pair itself is the element
            continue;
        }
        if (v.t == VT::Pair) {
            Value w = v.pairVal() ? *v.pairVal() : Value::integer(0);
            if (!isSet) { // a Bag/Mix weight must coerce to a real number
                if ((w.t == VT::Complex && w.im() != 0.0) ||
                    (w.t == VT::Num && !std::isfinite(w.n)))
                    throw RakuError{Value::typeObj("X::Numeric::CannotConvert"),
                        "Cannot convert " + w.gist() + " to " + (isMix ? "Real" : "Int")};
                if (w.t == VT::Str && !w.isAllomorph()) {
                    const char* p = w.s.c_str();
                    while (*p == ' ' || *p == '\t' || *p == '\n') p++;
                    char* end = nullptr;
                    if (*p) cnum::strtod(p, &end);
                    while (end && (*end == ' ' || *end == '\t' || *end == '\n')) end++;
                    if (*p && (end == p || *end))
                        throw RakuError{Value::typeObj("X::Str::Numeric"),
                            "Cannot convert string to number: " + w.s};
                }
            }
            if (isMix && w.t != VT::Int && w.isNumeric()) { // fractional weight
                auto it = h.hash()->find(v.s);
                auto keep = it != h.hash()->end() && it->second.pairKey() ? it->second.pairKey() : v.pairKey();
                if (it != h.hash()->end()) {
                    // through the EXACT tower, not a C double: the Rats 1/10 and 1/50
                    // summed as doubles gave 0.12000000000000001, and being a Num the
                    // result then printed at full Num precision too
                    Value sum = applyArith("+", it->second, w);
                    if (sum.toNum() == 0.0) h.hash()->erase(v.s);
                    else { sum.pairKeyM() = keep; (*h.hash())[v.s] = std::move(sum); }
                } else if (w.toNum() != 0.0) { w.pairKeyM() = keep; (*h.hash())[v.s] = w; }
                continue;
            }
            // Set membership is the value's TRUTHINESS (`:e<meow>` joins, `:0d`/`:f('')`
            // do not); Bag/Mix use the numeric weight. (typed key travels in pairKey)
            add(v.s, isSet ? Value::integer(w.truthy() ? 1 : 0) : exactIntWeight(w), v.pairKey());
        }
        else add(baggyKeyStr(v), Value::integer(1), baggyKey(v));
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
            if (!n.empty() && (ascii::isupper((unsigned char)n[0]) || n == "Nil")) return n;
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
    // A multi group's signature is its PROTO's: `proto method relpath(Mu $path)`
    // answers `(Mu $path)`, not the empty signature a synthesized dispatcher
    // carries. `^lookup` hands back the dispatcher, so that is what introspection
    // sees — Path::Finder reads `.signature.count` off it to decide how to call
    // the matcher, and an empty one made every proto-declared matcher unusable.
    if (c && c->isMultiDispatcher && !c->params)
        for (auto& cand : c->candidates)
            if (cand.code() && (cand.code()->isProto || cand.code()->isProtoBody) &&
                cand.code()->params) { c = cand.code(); break; }
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
        (*s.hash())["str"] = Value::str("(;; $_? is raw = OUTER::<$_>)");
        (*s.hash())["arity"] = Value::integer(0);
        (*s.hash())["count"] = Value::integer(1);
        Value params = Value::array(); params.isList = true;
        Value pv = Value::makeHash(); pv.hashKind = "Parameter";
        (*pv.hash())["str"] = Value::str("$_? is raw = OUTER::<$_>");
        (*pv.hash())["name"] = Value::str("$_");
        (*pv.hash())["usage-name"] = Value::str("_");
        (*pv.hash())["type"] = Value::str("Any");
        (*pv.hash())["type-obj"] = Value::typeObj("Any");
        (*pv.hash())["optional"] = Value::boolean(true);
        (*pv.hash())["slurpy"] = Value::boolean(false);
        (*pv.hash())["named"] = Value::boolean(false);
        (*pv.hash())["raw"] = Value::boolean(true);
        (*pv.hash())["readonly"] = Value::boolean(false);
        (*pv.hash())["rw"] = Value::boolean(false);
        (*pv.hash())["suffix"] = Value::str("?");
        (*pv.hash())["multi-invocant"] = Value::boolean(false);
        params.arr()->push_back(std::move(pv));
        (*s.hash())["params"] = std::move(params);
        return s;
    }
    std::string sig = "(";
    long long arity = 0, count = 0; bool slurpy = false, first = true;
    for (const Param* pp : ps) {
        const Param& p = *pp;
        // The INVOCANT counts. Rakudo reports `method file(Bool $v = True)` as
        // count 2 / arity 1 — the invocant is a required positional like any
        // other, and code that asks how many arguments a method takes subtracts
        // one for it (`$method.signature.count - 1`, which is how Path::Finder
        // decides whether a matcher takes a value or a list). It is still not
        // RENDERED here: the Callable does not know the class it was declared
        // in, so `Mu $:` would be a worse answer than leaving it out.
        if (p.invocant) { count++; arity++; continue; }
        if (!first) sig += ", ";
        first = false;
        sig += renderParam(p);
        // `*%opts` slurps NAMED arguments and takes no positional at all, so it
        // does not make the count Inf — only *@ / **@ / +@ do. Every method
        // carries an implicit one, which is how this reached signatures that never
        // wrote it: Path::Finder builds a filter's Capture from
        // `signature.count`, and Inf sent one-argument filters down the
        // many-arguments branch, wrapping the pattern in a Seq.
        if (!p.named && !(p.slurpy && p.sigil == '%'))
        { if (p.slurpy) slurpy = true; else { count++; if (!p.optional && !p.defaultVal && p.defaultRaku.empty()) arity++; } }
    }
    // a declared return type is part of the signature's rendering: `($x --> Int)`
    // (space-separated, no comma — and `(--> Int)` when there are no parameters)
    // (Rakudo separates with a space either way, so an empty parameter list
    // renders as `( --> Str)`)
    if (c && !c->retType.empty()) sig += " --> " + c->retType;
    sig += ")";
    Value s = Value::makeHash(); s.hashKind = "Signature";
    (*s.hash())["str"] = Value::str(sig);
    // `.returns` / `.of` — the DECLARED return type. DBDish's TypeConverter keys
    // its conversion table by it (`%!Conversions{$_.signature.returns} = $_`), so
    // without this the whole table was built under one key.
    if (c && !c->retType.empty()) (*s.hash())["returns"] = Value::typeObj(c->retType);
    (*s.hash())["arity"] = Value::integer(arity);
    (*s.hash())["count"] = slurpy ? Value::number(std::numeric_limits<double>::infinity()) : Value::integer(count);
    Value params = Value::array(); params.isList = true;
    for (const Param* pp : ps) {
        const Param& p = *pp;
        // A parameter carrying user traits keeps the ONE meta-object its traits
        // were dispatched against: a `trait_mod:<is>` mixes roles into it at
        // declaration time, and a freshly rendered copy would have forgotten
        // them. Only trait-carrying parameters are cached — every other
        // signature renders as it always did.
        if (!p.userTraits.empty() && p.metaBox) {
            params.arr()->push_back(p.metaBox->v);
            continue;
        }
        Value pv = Value::makeHash(); pv.hashKind = "Parameter";
        // how the parameter renders on its own — Value::gist reads this, so a
        // `say $sig.params[0]` shows `Int $one` rather than the attribute dump
        (*pv.hash())["str"] = Value::str(renderParam(p));
        // an ANONYMOUS parameter has an empty .name, not its bare sigil
        (*pv.hash())["name"] = Value::str(p.name.size() > 1 ? p.name : std::string());
        // `.usage-name` is the name without its sigil AND its twigil, so the
        // dynamic `Str @*l` is usable as plain `l`
        {
            std::string un = p.name.size() > 1 ? p.name.substr(1) : std::string();
            if (!un.empty() && std::strchr("*?!.=~^:", un[0])) un = un.substr(1);
            (*pv.hash())["usage-name"] = Value::str(un);
        }
        // the parameter's declarator doc (`#= …` / a leading `#|`) — .WHY
        // reads it; it already drives $*USAGE's option list
        if (!p.pod.empty()) (*pv.hash())["why"] = Value::str(p.pod);
        (*pv.hash())["type"] = Value::str(p.type);
        // the TYPE OBJECT for `.type` (compared `=:= Str` etc. by Cro's router).
        // Unconstrained is Mu; a slurpy/@-sigil param is Positional, %-sigil
        // Associative, &-sigil Callable — the constraint its sigil implies.
        // An unconstrained parameter is Any on a ROUTINE and Mu on a bare
        // `:( … )` literal; the sigil implies its own constraint either way.
        {
            // a TYPED @/% parameter reports the PARAMETRIC container type, as
            // Rakudo does: `Str :@foo` is Positional[Str] with .of = Str —
            // Getopt::Long derives the option's element type from exactly that
            Value tv;
            if (!p.type.empty() && (p.sigil == '@' || p.sigil == '%') && !p.slurpy) {
                tv = Value::typeObj(p.sigil == '@' ? "Positional" : "Associative");
                tv.ofTypeM() = p.type; // renders as Positional[Str]; .of answers Str
            }
            // a COERCION parameter reports the coercion type itself, as Rakudo
            // does: `Foo(Str) :$foo` is `Foo(Str)` and a bare `Foo()` is
            // `Foo(Any)`. Both halves live in the name — see the ^coerce /
            // ^target_type / ^constraint_type surface, which is how
            // Getopt::Long decides what to parse an option's argument into.
            else if (p.coerce && !p.type.empty())
                tv = Value::typeObj(p.type + "(" +
                                    (p.coerceFrom.empty() ? std::string("Any") : p.coerceFrom) + ")");
            else tv = Value::typeObj(
                !p.type.empty() ? p.type
                : p.sigil == '@' ? "Positional"
                : p.sigil == '%' ? "Associative"
                : p.sigil == '&' ? "Callable"
                : (c && c->isSigLiteral) ? "Mu" : "Any");
            (*pv.hash())["type-obj"] = std::move(tv);
        }
        // trait/shape flags the introspection API exposes one method each for
        (*pv.hash())["raw"]  = Value::boolean(p.isRaw || (p.sigil == '\\' && !p.slurpy && !p.isCopy));
        (*pv.hash())["copy"] = Value::boolean(p.isCopy);
        (*pv.hash())["readonly"] = Value::boolean(!(p.isRw || p.isCopy || p.isRaw ||
                                                  (p.sigil == '\\' && !p.slurpy)));
        (*pv.hash())["rw"]   = Value::boolean(p.isRw);
        (*pv.hash())["capture"] = Value::boolean(p.slurpy && p.slurpyKind == 0 &&
                                               (p.sigil == '|' || p.sigil == '\\'));
        (*pv.hash())["invocant"] = Value::boolean(p.invocant);
        (*pv.hash())["multi-invocant"] = Value::boolean(true); // only `;;` makes it False
        // `.prefix`/`.suffix`/`.modifier` — how the parameter is SPELLED
        (*pv.hash())["prefix"] = Value::str(
            !p.slurpy ? "" : p.slurpyKind == 'n' ? "**" : p.slurpyKind == '1' ? "+"
            : (p.sigil == '|' || p.sigil == '\\') ? "|" : "*");
        (*pv.hash())["suffix"] = Value::str(p.named ? (p.required ? "!" : "")
                                                  : (p.optional && !p.defaultVal ? "?" : ""));
        (*pv.hash())["modifier"] = Value::str(p.defConstraint == 1 ? ":D"
                                          : p.defConstraint == 2 ? ":U" : "");
        (*pv.hash())["named"] = Value::boolean(p.named);
        // `.default` is a Callable producing the default — undefined when the
        // parameter has none
        if (p.defaultVal) {
            const Expr* de = p.defaultVal.get();
            Value dc; dc.t = VT::Code; dc.setCode(std::make_shared<Callable>());
            dc.code()->builtin = [de](Interpreter& I, ValueList&) -> Value {
                return I.eval(const_cast<Expr*>(de));
            };
            (*pv.hash())["default"] = dc;
        }
        // with no default at all `.default` is the Code TYPE OBJECT — the
        // attribute's declared type — not a bare Any
        else (*pv.hash())["default"] = Value::typeObj("Code");
        (*pv.hash())["optional"] = Value::boolean(p.optional || p.defaultVal != nullptr);
        (*pv.hash())["slurpy"] = Value::boolean(p.slurpy);
        // `.constraints`: a literal parameter ('greet' in `get -> 'greet', $n {}`)
        // answers its literal value; otherwise Mu (matches Rakudo's use in Cro)
        {   // `.constraints` is a JUNCTION, as in Rakudo: all() when the
            // parameter is unconstrained (Getopt::Long stores it into a
            // `has Junction:D $.constraints` attribute), all(<literal>) for a
            // literal parameter — static context, so decode the common literal
            // node kinds directly (StrLit/IntLit). A `where` clause is scored
            // at dispatch here and is not yet carried as a Code eigenstate.
            Value cj = Value::array(); cj.enumName = "all";
            if (p.litVal) {
                Expr* le = p.litVal.get();
                if (le->kind == NK::StrLit) cj.arr()->push_back(Value::str(static_cast<StrLit*>(le)->v));
                else if (le->kind == NK::IntLit) cj.arr()->push_back(Value::integer(static_cast<IntLit*>(le)->v));
                else if (le->kind == NK::NumLit) cj.arr()->push_back(Value::number(static_cast<NumLit*>(le)->v));
                else if (le->kind == NK::BoolLit) cj.arr()->push_back(Value::boolean(static_cast<BoolLit*>(le)->v));
            }
            (*pv.hash())["constraints"] = std::move(cj);
        }
        {   // `.named_names`: every name this named parameter answers to —
            // INNERMOST first, as Rakudo orders them: `:fooo(:f(:@foo))` is
            // ("foo", "f", "fooo"). Getopt::Long keys the option on
            // named_names[0], so the outermost-first order renamed --foo's
            // capture entry to :fooo.
            Value nn = Value::array(); nn.isList = true;
            if (p.named) {
                if (p.namedKey.empty() || p.aliasBoth) {
                    std::string bare = p.name.size() > 2 && (p.name[1] == '!' || p.name[1] == '.')
                                     ? p.name.substr(2) : (p.name.size() > 1 ? p.name.substr(1) : p.name);
                    nn.arr()->push_back(Value::str(bare));
                }
                for (auto it2 = p.aliasKeys.rbegin(); it2 != p.aliasKeys.rend(); ++it2)
                    nn.arr()->push_back(Value::str(*it2));
                if (!p.namedKey.empty()) nn.arr()->push_back(Value::str(p.namedKey));
            }
            (*pv.hash())["named_names"] = nn;
        }
        // …and remember it, for the identity the trait dispatch above relies on
        if (!p.userTraits.empty()) {
            p.metaBox = std::make_shared<ParamMetaBox>();
            p.metaBox->v = pv;
        }
        params.arr()->push_back(pv);
    }
    // A METHOD's reflected .params carries the implicit pieces Rakudo's do: the
    // invocant first (unless one was declared) and a trailing `*%_` slurpy
    // (unless the method declares its own named slurpy). Data::Dump renders
    // method signatures as `.params[1 .. *-2]`, which is only right with both
    // in place. The rendered `str` stays as it was — the Callable does not know
    // the class it was declared in, so `Mu $:` would be a worse answer than
    // leaving the invocant out of the rendering.
    if (c && c->isMethod) {
        bool haveInv = false, haveNamedSlurpy = false;
        for (const Param* pp : ps) {
            if (pp->invocant) haveInv = true;
            if (pp->slurpy && pp->sigil == '%') haveNamedSlurpy = true;
        }
        // …and it counts: Rakudo reports `method file(Bool $v = True)` as count 2
        // / arity 1. Code that asks how many arguments a method takes subtracts
        // one for the invocant — `$method.signature.count - 1` is how Path::Finder
        // tells a matcher that takes a value from one that takes a list, and with
        // the invocant uncounted every one of them looked unusable.
        if (!haveInv) {
            arity++;
            if (!slurpy) count++;
            (*s.hash())["arity"] = Value::integer(arity);
            (*s.hash())["count"] = slurpy ? Value::number(std::numeric_limits<double>::infinity())
                                          : Value::integer(count);
        }
        auto mkParam = [](const std::string& str, const std::string& name,
                          bool invocant, bool slurpy, bool named) {
            Value pv = Value::makeHash(); pv.hashKind = "Parameter";
            (*pv.hash())["str"] = Value::str(str);
            (*pv.hash())["name"] = Value::str(name);
            (*pv.hash())["usage-name"] = Value::str(name.size() > 1 ? name.substr(1) : "");
            (*pv.hash())["type"] = Value::str("Mu");
            (*pv.hash())["type-obj"] = Value::typeObj("Mu");
            (*pv.hash())["invocant"] = Value::boolean(invocant);
            (*pv.hash())["multi-invocant"] = Value::boolean(true);
            (*pv.hash())["named"] = Value::boolean(named);
            (*pv.hash())["slurpy"] = Value::boolean(slurpy);
            (*pv.hash())["optional"] = Value::boolean(named || slurpy);
            (*pv.hash())["raw"] = Value::boolean(invocant);
            (*pv.hash())["readonly"] = Value::boolean(true);
            (*pv.hash())["rw"] = Value::boolean(false);
            (*pv.hash())["copy"] = Value::boolean(false);
            (*pv.hash())["capture"] = Value::boolean(false);
            (*pv.hash())["prefix"] = Value::str(slurpy ? "*" : "");
            (*pv.hash())["suffix"] = Value::str("");
            (*pv.hash())["modifier"] = Value::str("");
            (*pv.hash())["default"] = Value::typeObj("Code");
            (*pv.hash())["constraints"] = Value::typeObj("Mu");
            Value nn = Value::array(); nn.isList = true;
            (*pv.hash())["named_names"] = nn;
            return pv;
        };
        if (!haveInv)
            // anonymous, as Rakudo's implicit invocant is: its .name is ""
            params.arr()->insert(params.arr()->begin(), mkParam("Mu $", "", true, false, false));
        if (!haveNamedSlurpy)
            params.arr()->push_back(mkParam("*%_", "%_", false, true, true));
    }
    (*s.hash())["params"] = params;
    return s;
}

// say/print/put/note honour a user-overridden $*OUT/$*ERR: if the dynamic
// variable holds a user object (e.g. a mock IO capturing output), send the text
// to its .print method; otherwise write straight to the real stream.
// The one lock every runtime write to the process's own streams takes. A
// function-local static rather than a namespace-scope object so it is
// constructed on first use — output can happen during static initialisation,
// and an ordering bug there would be maddening to find.
std::mutex& rtOutMutex() {
    static std::mutex m;
    return m;
}

// ---- IO::Handle.out-buffer ---------------------------------------------------
// $*OUT / $*ERR are synthesized fresh on every read of the dynamic — there is no
// container to write an attribute into — so `.out-buffer` for them lives here,
// one slot per stream, for as long as the process does.
//
// BOTH start at 0, which is what Rakudo reports and how Rakudo behaves: a `say`
// is due the moment it is written. std::cerr already was unbuffered (unitbuf);
// std::cout was not, and on a pipe or a file that meant every `say` sat in its
// block buffer until the program ended — so a rakupp program in a pipeline was
// silent while it ran (issue #51, hit three separate ways). The cost is one
// write(2) per say: a 200k-line filter piped out goes 0.25s -> 0.38s here,
// against 0.36-0.67s for Rakudo doing the same thing. A program that wants the
// block back asks for it — `$*OUT.out-buffer = 65536` — which is more than
// Rakudo offers, since it ignores the setting on its standard handles.
long long& rtStdOutBuffer(bool err) {
    static long long out = 0, er = 0;
    return err ? er : out;
}
// $*IN has no output to buffer; it answers 0 and keeps the two output slots
// out of reach — routing it to $*OUT's would let `$*IN.out-buffer = 0` silently
// unbuffer someone else's stream.
static long long g_stdInOutBuffer = 0;

// Rakudo's coercion: :!out-buffer / False is none, True is "the default size",
// an Int is that many bytes. A negative size is no buffer at all.
long long outBufferSize(const Value& v) {
    if (v.t == VT::Bool) return v.truthy() ? kDefaultOutBuffer : 0;
    if (v.t == VT::Any || v.t == VT::Nil) return kDefaultOutBuffer;
    long long n = v.toInt();
    return n < 0 ? 0 : n;
}

long long Interpreter::fhOutBuffer(const Value& h) {
    if (h.t != VT::Hash || !h.hash()) return kDefaultOutBuffer;
    auto st = h.hash()->find("std");
    if (st != h.hash()->end()) {
        const std::string which = st->second.toStr();
        return which == "in" ? g_stdInOutBuffer : rtStdOutBuffer(which == "err");
    }
    auto it = h.hash()->find("out-buffer");
    return it == h.hash()->end() ? kDefaultOutBuffer : it->second.toInt();
}

// Can this handle's pending bytes reach a file at all? An IN-MEMORY handle —
// Proc.out, $*ARGFILES, IO::String, anything opened read-only — has nowhere to
// put them: its "buffer" IS the content, and emptying it to make room would
// simply lose data. Those keep the unlimited buffer they always had.
static bool fhWritesToFile(const ValueMap& m) {
    auto p = m.find("path");
    if (p == m.end() || p->second.toStr().empty()) return false;
    auto mo = m.find("mode");
    if (mo == m.end()) return false;
    const std::string mode = mo->second.toStr();
    return mode == "w" || mode == "a" || mode == "rw" || mode == "update";
}

void Interpreter::fhAppendToFile(const std::shared_ptr<ValueMap>& h, const std::string& s) {
    if (!fhWritesToFile(*h)) return;
    std::string mode = (*h)["mode"].toStr();
    bool wrote = (*h)["wrote"].truthy();   // earlier bytes are already out there
    std::ofstream out((*h)["path"].toStr(),
                      std::ios::binary | ((mode == "a" || wrote) ? std::ios::app : std::ios::trunc));
    if (out) out << s;
    // Even an EMPTY write settles the truncate question: the file has been
    // opened for this handle, so the next one must append rather than start over.
    (*h)["wrote"] = Value::boolean(true);
}

bool Interpreter::fhFlush(const Value& h) {
    if (h.t != VT::Hash || !h.hash()) return false;
    auto st = h.hash()->find("std");
    if (st != h.hash()->end()) {
        std::lock_guard<std::mutex> lk(rtOutMutex());
        if (st->second.toStr() == "err") std::cerr.flush(); else std::cout.flush();
        return true;
    }
    auto m = h.hashS();
    if (!m || !fhWritesToFile(*m)) return false;
    std::lock_guard<std::mutex> lk(rtOutMutex());
    // A COPY, not a reference into the map: fhAppendToFile writes "wrote" back,
    // and an insert can rehash the map out from under a reference to a value in it.
    std::string pending = (*m)["buffer"].s;
    if (pending.empty()) return false;
    (*m)["buffer"] = Value::str("");
    fhAppendToFile(m, pending);
    return true;
}

void Interpreter::fhWrite(const Value& h, const std::string& s) {
    auto m = h.hashS();
    if (!m) return;
    // -1: no file behind this handle, so no size can ever force a write out.
    long long size = fhWritesToFile(*m) ? fhOutBuffer(h) : -1;
    // Appending is a READ-MODIFY-WRITE on state the handle shares, so two
    // threads writing to one file both read the buffer, both append, and one
    // write is simply lost. Under the output lock it is one update at a time.
    std::lock_guard<std::mutex> lk(rtOutMutex());
    {   // Room for it: hold it back. This is the ONLY branch a buffered handle
        // takes, and it is the hot one.
        Value& buf = (*m)["buffer"];
        if (size < 0 || (size > 0 && (long long)(buf.s.size() + s.size()) <= size)) {
            buf = Value::str(buf.s + s);
            return;
        }
    }
    std::string pending = (*m)["buffer"].s;      // copied for the same reason as above
    if (!pending.empty()) { (*m)["buffer"] = Value::str(""); fhAppendToFile(m, pending); }
    // A write at least a bufferful on its own has nothing to gain from the
    // buffer and goes straight out; a smaller remainder starts the next one.
    if (size == 0 || (long long)s.size() >= size) { if (!s.empty()) fhAppendToFile(m, s); }
    else (*m)["buffer"] = Value::str(s);
}

Value Interpreter::fhSetOutBuffer(const Value& h, const Value& n) {
    long long size = outBufferSize(n);
    if (h.t != VT::Hash || !h.hash()) return Value::integer(size);
    auto st = h.hash()->find("std");
    if (st != h.hash()->end()) {
        const std::string which = st->second.toStr();
        if (which == "in") { g_stdInOutBuffer = size; return Value::integer(size); }
        bool err = which == "err";
        rtStdOutBuffer(err) = size;
        // Whatever is already sitting in the stream's own buffer belongs to the
        // OLD size — the point of switching to 0 is that it comes out now.
        std::lock_guard<std::mutex> lk(rtOutMutex());
        if (err) std::cerr.flush(); else std::cout.flush();
        return Value::integer(size);
    }
    fhFlush(h);                                  // the resize itself flushes
    std::lock_guard<std::mutex> lk(rtOutMutex());
    (*h.hash())["out-buffer"] = Value::integer(size);
    return Value::integer(size);
}

Value Interpreter::ioEmit(const std::string& s, const char* dynVar, bool toErr) {
    // Dynamic ($*) lookup: the current lexical scope, then the caller chain.
    Value* h = nullptr;
    if (tctx_.cur) {
        h = tctx_.cur->find(dynVar);
        if (!h)
            for (auto it = tctx_.dynStack.rbegin(); it != tctx_.dynStack.rend(); ++it)
                if (*it && (h = (*it)->find(dynVar))) break;
    }
    // Route to the handle both for a user OBJECT (a custom IO class) and for a
    // real FileHandle — `my $*OUT = open(...); say "x"` writes to the file, as
    // in Rakudo. (The FileHandle arm was missing: rebinding $*OUT silently
    // leaked say/print to stdout — found building -i in-place editing.)
    // For an object, route by what the class DEFINES: its own print method,
    // else its WRITE(Blob) sink (the IO::Handle protocol — Test::Output's
    // capture classes override only WRITE). Routing print at an object that
    // defines neither used to re-enter the global print through the
    // native-parent delegation and recurse to a stack overflow.
    // …and a TYPE OBJECT is a sink too: `$*OUT = class { method print(*@a) {…} }`
    // hands over the class itself, which is the shortest way to capture output
    // and is exactly what IO::Capture::Simple does. Only instances were routed,
    // so every `say` walked past the capture to the real stream.
    if (h && h->t == VT::Type && !h->s.empty()) {
        auto ci = classes_.find(h->s);
        if (ci != classes_.end() && ci->second) {
            if (ci->second->findMethod("print")) {
                ValueList pa{Value::str(s)};
                return methodCall(*h, "print", pa);
            }
            if (Value* wm = ci->second->findMethod("WRITE")) {
                Value blob = Value::str(s);
                blob.hashKind = "Blob";
                return invokeMethod(*wm, *h, ValueList{blob}, nullptr);
            }
        }
    }
    if (h && h->t == VT::Object && h->obj() && h->obj()->cls) {
        // Outside the lock below on purpose: this re-enters the interpreter to
        // run a user method, which may itself print. Holding a non-recursive
        // mutex across that would deadlock.
        if (h->obj()->cls->findMethod("print")) {
            ValueList pa{Value::str(s)};
            return methodCall(*h, "print", pa);
        }
        if (Value* wm = h->obj()->cls->findMethod("WRITE")) {
            Value blob = Value::str(s);
            blob.hashKind = "Blob";
            return invokeMethod(*wm, *h, ValueList{blob}, nullptr);
        }
        // neither print nor WRITE: not a sink — fall through to the real stream
    }
    else if (h && h->hashKind == "FileHandle") {
        ValueList pa{Value::str(s)};
        return methodCall(*h, "print", pa);
    }
    // ONE writer at a time. std::cout guarantees the BYTES of a single `<<` are
    // not interleaved (libc++ writes through the FILE*, which locks), but the
    // stream's own state word is touched by every sentry without
    // synchronisation — ThreadSanitizer reports a data race on any program that
    // prints from two threads, which since v3.0.0 means any program using the
    // default parallelism. A real lock rather than a suppression: it is what
    // makes the report go away honestly, it keeps TSan quiet enough for the
    // NEXT race to be visible, and it guarantees whole-write atomicity on
    // platforms whose streams are not FILE-backed (the WASM build).
    //
    // One mutex for both streams, not one each: stdout and stderr are usually
    // the same terminal or the same redirected file, so interleaving BETWEEN
    // them matters as much as within one.
    {
        std::lock_guard<std::mutex> lk(rtOutMutex());
        std::ostream& os = toErr ? std::cerr : std::cout;
        os << s;
        // out-buffer 0: the bytes are due NOW. Without this a `say` whose output
        // is a pipe rather than a terminal sat in std::cout's block buffer until
        // the program ended, so anything watching the pipe live saw nothing
        // (issue #51 — a runner streaming a child's output through rakupp).
        if (rtStdOutBuffer(toErr) == 0) os.flush();
    }
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

// ---- JSON parser (internal readers + Rakudo::Internals::JSON) -----------------
// ONE recursive-descent parser, ours and original, serves the internal readers
// (jsonParseDoc: META6, resource maps, OpenSSL's libraries.json) and the
// Rakudo::Internals::JSON compatibility class (only the NAME is Rakudo's — the
// dependency-free codec toolchain code reaches for, e.g. rakupp's own
// install.raku): object→Hash/Map, array→Array/List, string→Str, number typed
// exactly like Str.Numeric (Int with arbitrary precision, Rat for decimals,
// Num for exponents), true/false→Bool, null→Any. The full JSON::Fast fidelity
// — surrogate pairs, strict escapes, :immutable containers, JSONC comments —
// dates from the one-day native `use JSON::Fast` era (added 8d43ed0, unvendored
// 2001a12) and stays: JSON::Native's engine backend leans on that typing.
struct JsonCfg {
    bool immutable = false; // containers become Map/List instead of Hash/Array
    bool jsonc     = false; // JSON::Fast :allow-jsonc — // and /* */ comments
    int  depth     = 0;     // recursion guard: hostile nesting must die, not crash
};
static void jsonSkipWs(const std::string& s, size_t& i, const JsonCfg& cfg) {
    for (;;) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
        if (!cfg.jsonc || i + 1 >= s.size() || s[i] != '/') return;
        if (s[i + 1] == '/') { i += 2; while (i < s.size() && s[i] != '\n') i++; }
        else if (s[i + 1] == '*') {
            i += 2;
            while (i + 1 < s.size() && !(s[i] == '*' && s[i + 1] == '/')) i++;
            i = i + 1 < s.size() ? i + 2 : s.size();
        }
        else return; // a lone '/' is the next token's parse error, not whitespace
    }
}
static bool jsonParseValue(const std::string& s, size_t& i, Value& out, JsonCfg cfg);
static bool jsonParseString(const std::string& s, size_t& i, std::string& out) {
    if (i >= s.size() || s[i] != '"') return false;
    i++;
    out.clear();
    while (i < s.size() && s[i] != '"') {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x20) return false; // raw control characters must arrive \u-escaped
        i++;
        if (c != '\\') { out += (char)c; continue; }
        if (i >= s.size()) return false;
        char e = s[i++];
        switch (e) {
            case 'n': out += '\n'; break;  case 't': out += '\t'; break;
            case 'r': out += '\r'; break;  case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;  case '/': out += '/';  break;
            case '"': out += '"';  break;  case '\\': out += '\\'; break;
            case 'u': {
                auto hex4 = [&](size_t p, unsigned& v) {
                    if (p + 4 > s.size()) return false;
                    v = 0;
                    for (int k = 0; k < 4; k++) {
                        char h = s[p + k]; v <<= 4;
                        if (h >= '0' && h <= '9') v |= (unsigned)(h - '0');
                        else if (h >= 'a' && h <= 'f') v |= (unsigned)(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') v |= (unsigned)(h - 'A' + 10);
                        else return false;
                    }
                    return true;
                };
                unsigned cp;
                if (!hex4(i, cp)) return false;
                i += 4;
                // a high surrogate followed by \u-escaped low surrogate is ONE
                // astral character (03-unicode.t round-trips flag emoji this way)
                if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 <= s.size() &&
                    s[i] == '\\' && s[i + 1] == 'u') {
                    unsigned lo;
                    if (hex4(i + 2, lo) && lo >= 0xDC00 && lo <= 0xDFFF) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        i += 6;
                    }
                }
                if (cp >= 0xD800 && cp <= 0xDFFF) return false; // a LONE surrogate is not a character — die, like chr() would
                out += cpToU8(cp);
                break;
            }
            default: return false; // JSON::Fast dies on unknown escapes; so do we
        }
    }
    if (i >= s.size()) return false;
    i++; // closing quote
    return true;
}
static bool jsonParseValue(const std::string& s, size_t& i, Value& out, JsonCfg cfg) {
    if (++cfg.depth > 20000) return false; // ~3 MB of C++ frames; no real document nests this deep
    jsonSkipWs(s, i, cfg);
    if (i >= s.size()) return false;
    char c = s[i];
    if (c == '"') { std::string str; if (!jsonParseString(s, i, str)) return false; out = Value::str(str); return true; }
    if (c == '{') {
        i++; out = Value::makeHash();
        if (cfg.immutable) out.hashKind = "Map";
        jsonSkipWs(s, i, cfg);
        if (i < s.size() && s[i] == '}') { i++; return true; }
        for (;;) {
            jsonSkipWs(s, i, cfg);
            std::string key; if (!jsonParseString(s, i, key)) return false;
            jsonSkipWs(s, i, cfg);
            if (i >= s.size() || s[i] != ':') return false; i++;
            Value v; if (!jsonParseValue(s, i, v, cfg)) return false;
            (*out.hash())[key] = v;
            jsonSkipWs(s, i, cfg);
            if (i < s.size() && s[i] == ',') { i++; continue; }
            if (i < s.size() && s[i] == '}') { i++; return true; }
            return false;
        }
    }
    if (c == '[') {
        i++; out = Value::array();
        out.isList = cfg.immutable; // List under :immutable, Array otherwise — is-deeply tells them apart
        jsonSkipWs(s, i, cfg);
        if (i < s.size() && s[i] == ']') { i++; return true; }
        for (;;) {
            Value v; if (!jsonParseValue(s, i, v, cfg)) return false;
            out.arr()->push_back(v);
            jsonSkipWs(s, i, cfg);
            if (i < s.size() && s[i] == ',') { i++; continue; }
            if (i < s.size() && s[i] == ']') { i++; return true; }
            return false;
        }
    }
    if (s.compare(i, 4, "true") == 0)  { i += 4; out = Value::boolean(true);  return true; }
    if (s.compare(i, 5, "false") == 0) { i += 5; out = Value::boolean(false); return true; }
    if (s.compare(i, 4, "null") == 0)  { i += 4; out = Value::any();           return true; }
    // number: scan the same loose token JSON::Fast does, then type it exactly
    // like Str.Numeric does — that is what the real module calls on the token.
    if (c != '-' && !ascii::isdigit((unsigned char)c)) return false; // JSON has no leading '+'
    size_t st = i;
    if (c == '-') i++;
    while (i < s.size() && (ascii::isdigit((unsigned char)s[i]) || s[i] == '.' ||
                            s[i] == 'e' || s[i] == 'E' || s[i] == '+' || s[i] == '-')) i++;
    if (i == st) return false;
    out = numifyStr(s.substr(st, i - st));
    return out.t != VT::Any && out.t != VT::Nil; // "5." scans but does not numify — die like .Numeric
}

static void jfEscape(const std::string& s, std::string& out); // defined below (JSON::Fast codec)

static std::string jsonEncode(const Value& v) {
    switch (v.t) {
        case VT::Nil: case VT::Any: case VT::Type: return "null";
        case VT::Bool: return v.b ? "true" : "false";
        case VT::Int:  return v.big() ? v.big()->toString() : std::to_string(v.i);
        case VT::Num: case VT::Rat: {
            // round-trip doubles (default 6 digits silently truncated); cnum so a
            // host's locale can never put a comma into JSON
            char b[40];
            cnum::snprintf(b, sizeof b, "%.17g", v.toNum());
            return b;
        }
        case VT::Array: {
            std::string r = "[";
            if (v.arr()) for (size_t k = 0; k < v.arr()->size(); k++) { if (k) r += ","; r += jsonEncode((*v.arr())[k]); }
            return r + "]";
        }
        case VT::Hash: {
            // keys go through the SAME escaper as string values — a quote or a
            // C0 control in a key used to be concatenated raw, emitting invalid
            // JSON from Rakudo::Internals::JSON.to-json and the dist-META writer
            std::string r = "{"; bool first = true;
            if (v.hash()) for (auto& kv : *v.hash()) {
                if (!first) r += ","; first = false;
                r += "\""; jfEscape(kv.first, r); r += "\":" + jsonEncode(kv.second);
            }
            return r + "}";
        }
        default: { // string (and anything stringy)
            std::string r = "\"";
            jfEscape(v.toStr(), r);
            return r + "\"";
        }
    }
}

// ---- JSON::Fast, natively --------------------------------------------------
// Sparrow6's check engine writes its whole context through JSON::Fast's
// to-json — 10,000 captured lines meant ~700 ms of interpreted jsonify/
// str-escape per task. The module itself stays exactly what the user
// installed (the v3.0.1 unvendoring stands: no pinned source in the binary);
// what changes is the CALL: loadModule wraps the loaded &to-json/&from-json,
// and a call whose arguments this replica covers runs the native codec.
// Anything it does not cover — an unknown adverb, a callable :sorted-keys, a
// NaN/Inf Num (whose rendering hangs on the $*JSON_NAN_INF_SUPPORT dynamic),
// or a type outside the ladder (DateTime, Version, objects) — falls through to
// the module's own sub, so behaviour is the module's in every uncovered case.
struct JsonFastUnsupported {};

static void jfEscape(const std::string& s, std::string& out) {
    // Measured against the module (the unicode block in the regression file
    // pins the bytes): \t \n \r, quote and backslash by name; other controls
    // as lower-case \u%04x; BMP text RAW — the module walks NFD codepoints,
    // but a Raku Str is NFG, so rebuilding those codepoints composes them
    // straight back to the bytes held here; and an ASTRAL codepoint as an
    // upper-case-hex surrogate PAIR — the one place the module and a raw
    // byte copy genuinely disagree. Falling back on any byte >= 0x80, the
    // old rule, sent Sparrow6-shaped writes to the interpreted module:
    // ~327 ms instead of ~4 ms on the 278 KB diagnose corpus.
    for (size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) {
            if (c == '\n') out += "\\n";
            else if (c == '\r') out += "\\r";
            else if (c == '\t') out += "\\t";
            else if (c == '"') out += "\\\"";
            else if (c == '\\') out += "\\\\";
            else if (c <= 31) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); out += b; }
            else out += (char)c;
            i++;
            continue;
        }
        int len = (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3
                : (c & 0xF8) == 0xF0 ? 4 : 1;
        if (len == 1 || i + (size_t)len > s.size()) { out += (char)c; i++; continue; } // not UTF-8: the byte, raw
        if (len < 4) { out.append(s, i, (size_t)len); i += (size_t)len; continue; }    // BMP: raw
        unsigned cp = (unsigned)(c & 0x07);
        for (int k = 1; k < 4; k++) cp = (cp << 6) | (unsigned)((unsigned char)s[i + k] & 0x3F);
        unsigned v = cp - 0x10000;
        char b[16];
        snprintf(b, sizeof b, "\\u%04X\\u%04X", 0xD800 + (v >> 10), 0xDC00 + (v & 0x3FF));
        out += b;
        i += 4;
    }
}

static void jfEncode(const Value& v, bool pretty, int spacing, bool sortedKeys,
                     bool enumsAsValue, int level, std::string& out) {
    // jsonify is one big `with obj` — anything undefined lands in its else and
    // renders "null". rtIsDefined is the one rule for that, and it knows what
    // the switch below cannot see: an enum TYPE object materializes as a
    // tagged pair-ARRAY here, not as VT::Type (to-json(Squee) is null, not
    // the member list). Mu is the exception the module itself makes: its
    // to-json takes Any, so to-json(Mu) dies in the BINDER — fall back and
    // let it (the fast-path regression file pins exactly that).
    if (!rtIsDefined(v)) {
        if (v.t == VT::Type && v.s == "Mu") throw JsonFastUnsupported{};
        out += "null";
        return;
    }
    // the ladder is jsonify's, in jsonify's order
    if (!v.enumName.empty()) {           // enum value: its KEY, or with
        if (!enumsAsValue) {             // :enums-as-value its underlying value
            out += '"'; jfEscape(v.enumName, out); out += '"';
            return;
        }
        // :enums-as-value wants .value, and this Value carries only the
        // ordinal — a string-valued enum (Blerp (One => "Eins")) would come
        // out as its position. The module reads the real .value; let it.
        throw JsonFastUnsupported{};
    }
    // an allomorph prints its NUMERIC half — jsonify re-dispatches IntStr /
    // RatStr / NumStr through .Int/.Rat/.Num before formatting, so the Str
    // face must not leak into the number: RatStr.new(0.0, '') is "0.0", not
    // "" plus the ".0" suffix (the module's own roundtrip suite feeds exactly
    // that). Any OTHER kinded numeric (Duration bridges to Num, say) is the
    // module's business.
    if ((v.t == VT::Int || v.t == VT::Rat || v.t == VT::Num) && !v.hashKind.empty()) {
        if (v.hashKind == "IntStr" || v.hashKind == "RatStr" || v.hashKind == "NumStr") {
            Value plain = v;
            plain.hashKind.clear();
            plain.s.clear();
            jfEncode(plain, pretty, spacing, sortedKeys, enumsAsValue, level, out);
            return;
        }
        throw JsonFastUnsupported{};
    }
    switch (v.t) {
        case VT::Nil: case VT::Any: out += "null"; return;
        case VT::Type:
            // jsonify's parameter is Any-constrained at EVERY level, so the one
            // type object it does not render as null is Mu itself — that call
            // dies in the module's own binder, and the module owns that error.
            if (v.s == "Mu") throw JsonFastUnsupported{};
            out += "null";
            return;
        case VT::Bool: out += v.b ? "true" : "false"; return;
        case VT::Int:
            out += v.big() ? v.big()->toString() : std::to_string(v.i);
            return;
        case VT::Rat: {                  // Rat.Str, plus ".0" when integral
            std::string r = v.toStr();
            out += r;
            if (r.find('.') == std::string::npos) out += ".0";
            return;
        }
        case VT::Num: {                  // Num.Str, plus "e0" when it lacks one
            double d = v.toNum();
            if (std::isnan(d) || std::isinf(d)) throw JsonFastUnsupported{};
            std::string r = v.toStr();
            out += r;
            if (r.find('e') == std::string::npos && r.find('E') == std::string::npos)
                out += "e0";
            return;
        }
        case VT::Str:
            if (!v.hashKind.empty()) throw JsonFastUnsupported{}; // Buf/Blob die in JSON::Fast
            out += '"'; jfEscape(v.s, out); out += '"';
            return;
        case VT::Pair: {                 // a Pair is Associative: one-entry object
            if (v.pairKey()) throw JsonFastUnsupported{};         // non-Str key object
            std::string open = "{", close = "}";
            if (pretty) {
                std::string ind((size_t)(spacing * (level + 1)), ' ');
                std::string outd((size_t)(spacing * level), ' ');
                out += "{\n" + ind + '"'; jfEscape(v.s, out); out += "\": ";
                jfEncode(v.pairVal() ? *v.pairVal() : Value::any(), pretty, spacing, sortedKeys, enumsAsValue, level + 1, out);
                out += "\n" + outd + "}";
            } else {
                out += "{\""; jfEscape(v.s, out); out += "\":";
                jfEncode(v.pairVal() ? *v.pairVal() : Value::any(), pretty, spacing, sortedKeys, enumsAsValue, level, out);
                out += "}";
            }
            return;
        }
        case VT::Range: {
            Value a = Value::array(); *a.arr() = v.flatten();
            jfEncode(a, pretty, spacing, sortedKeys, enumsAsValue, level, out);
            return;
        }
        case VT::Array: {
            if (!v.arr()) { out += pretty ? "[\n]" : "[]"; return; }
            auto& xs = *v.arr();
            if (pretty) {
                // JSON::Fast's exact shape: "[\n<ind>… ,\n<ind>… \n<outd>]",
                // and an EMPTY array renders as "[\n<outd>]"
                std::string ind((size_t)(spacing * (level + 1)), ' ');
                std::string outd((size_t)(spacing * level), ' ');
                out += "[";
                if (xs.empty()) { out += "\n" + outd + "]"; return; }
                for (size_t i = 0; i < xs.size(); i++) {
                    out += (i ? ",\n" + ind : "\n" + ind);
                    jfEncode(xs[i], pretty, spacing, sortedKeys, enumsAsValue, level + 1, out);
                }
                out += "\n" + outd + "]";
            } else {
                out += "[";
                for (size_t i = 0; i < xs.size(); i++) {
                    if (i) out += ",";
                    jfEncode(xs[i], pretty, spacing, sortedKeys, enumsAsValue, level, out);
                }
                out += "]";
            }
            return;
        }
        case VT::Hash: {
            // allomorphs re-dispatch on their numeric half; DateTime, Version,
            // Supply and every other kinded hash is outside the ladder
            if (v.hashKind == "IntStr" || v.hashKind == "RatStr" || v.hashKind == "NumStr")
                throw JsonFastUnsupported{}; // t is Hash only for exotic allomorph carriers
            if (!v.hashKind.empty()) throw JsonFastUnsupported{};
            if (!v.hash()) { out += pretty ? "{\n}" : "{}"; return; }
            auto& h = *v.hash();
            // hashes iterate in INSERTION order here; :sorted-keys sorts by
            // key, exactly like the module's `.sort(*.key)`
            std::vector<std::pair<const std::string*, const Value*>> kvs;
            for (auto& kv : h) kvs.emplace_back(&kv.first, &kv.second);
            if (sortedKeys)
                std::sort(kvs.begin(), kvs.end(),
                          [](auto& a, auto& b) { return *a.first < *b.first; });
            if (pretty) {
                std::string ind((size_t)(spacing * (level + 1)), ' ');
                std::string outd((size_t)(spacing * level), ' ');
                out += "{";
                if (kvs.empty()) { out += "\n" + outd + "}"; return; }
                bool first = true;
                for (auto& kv : kvs) {
                    out += (first ? "\n" + ind : ",\n" + ind); first = false;
                    out += '"'; jfEscape(*kv.first, out); out += "\": ";
                    jfEncode(*kv.second, pretty, spacing, sortedKeys, enumsAsValue, level + 1, out);
                }
                out += "\n" + outd + "}";
            } else {
                out += "{"; bool first = true;
                for (auto& kv : kvs) {
                    if (!first) out += ",";
                    first = false;
                    out += '"'; jfEscape(*kv.first, out); out += "\":";
                    jfEncode(*kv.second, pretty, spacing, sortedKeys, enumsAsValue, level, out);
                }
                out += "}";
            }
            return;
        }
        default: throw JsonFastUnsupported{}; // objects, code, … — jsonify dies; the module decides
    }
}

// The wrapped &to-json / &from-json: try native, fall back to the module's sub.
Value jsonFastToJsonCall(Interpreter& I, ValueList& a, const Value& orig) {
    const Value* obj = nullptr;
    bool pretty = true; long long level = 0, spacing = 2; bool enumsAsValue = false;
    bool sortedKeys = false;
    for (auto& x : a) {
        if (x.t == VT::Pair && x.namedArg) {
            bool tv = x.pairVal() && x.pairVal()->truthy();
            if (x.s == "pretty") pretty = tv;
            else if (x.s == "level") level = x.pairVal() ? x.pairVal()->toInt() : 0;
            else if (x.s == "spacing") spacing = x.pairVal() ? x.pairVal()->toInt() : 2;
            else if (x.s == "enums-as-value") enumsAsValue = tv;
            else if (x.s == "sorted-keys") {
                // a CALLABLE comparator is the module's business
                if (x.pairVal() && x.pairVal()->t == VT::Code) return I.callCallable(orig, a);
                sortedKeys = tv;
            }
            else return I.callCallable(orig, a);   // an adverb this replica does not know
        }
        else if (!obj) obj = &x;
        else return I.callCallable(orig, a);       // extra positional: let the module refuse it
    }
    if (!obj) return I.callCallable(orig, a);
    try {
        std::string out;
        jfEncode(*obj, pretty, (int)spacing, sortedKeys, enumsAsValue, (int)level, out);
        return Value::str(out);
    } catch (JsonFastUnsupported&) {
        return I.callCallable(orig, a);
    }
}

Value jsonFastFromJsonCall(Interpreter& I, ValueList& a, const Value& orig) {
    const Value* text = nullptr;
    JsonCfg cfg;
    for (auto& x : a) {
        if (x.t == VT::Pair && x.namedArg) {
            bool tv = x.pairVal() && x.pairVal()->truthy();
            if (x.s == "immutable") cfg.immutable = tv;
            else if (x.s == "allow-jsonc") cfg.jsonc = tv;
            else return I.callCallable(orig, a);
        }
        else if (!text) text = &x;
        else return I.callCallable(orig, a);
    }
    if (!text || !(text->t == VT::Str && text->hashKind.empty()))
        return I.callCallable(orig, a);            // Str() coercion is the module's
    size_t i = 0; Value out;
    if (!jsonParseValue(text->s, i, out, cfg)) return I.callCallable(orig, a);
    jsonSkipWs(text->s, i, cfg);
    if (i != text->s.size()) return I.callCallable(orig, a); // its typed trailing-content error
    return out;
}

// Called by loadModule right after JSON::Fast's tree has executed: wrap the
// module's own subs so both the EXPORT protocol (which hands out
// &JSON::Fast::to-json) and qualified calls resolve to the wrapped ones.
void Interpreter::wrapJsonFastExports(Env& moduleEnv) {
    auto wrap = [&](const char* name, Value (*fn)(Interpreter&, ValueList&, const Value&)) {
        // The subs live inside `module JSON::Fast { ... }`, so by the time the
        // tree has executed they exist as the QUALIFIED globals (and that
        // qualified spelling is what the module's own EXPORT sub hands out);
        // a bare moduleEnv entry is wrapped too when present.
        std::string qual = std::string("&JSON::Fast::") + (name + 1);
        Value* slot = nullptr;
        auto it = moduleEnv.vars.find(name);
        if (it != moduleEnv.vars.end() && it->second.t == VT::Code) slot = &it->second;
        Value* gslot = global_ ? global_->find(qual) : nullptr;
        if (!slot && !(gslot && gslot->t == VT::Code)) return;
        Value orig = slot ? *slot : *gslot;
        if (orig.code() && orig.code()->builtin) return;   // already wrapped
        Value w; w.t = VT::Code; w.setCode(std::make_shared<Callable>());
        w.code()->name = name + 1;                          // drop the '&'
        w.code()->builtin = [orig, fn](Interpreter& I2, ValueList& args) -> Value {
            return fn(I2, args, orig);
        };
        if (slot) *slot = w;
        if (gslot && gslot->t == VT::Code) *gslot = w;
        else if (global_) global_->define(qual, w);
    };
    wrap("&to-json", jsonFastToJsonCall);
    wrap("&from-json", jsonFastFromJsonCall);
}


// Does the user-declared method `m` on this invocant take a Junction WHOLE at
// argument position `ai`? A parameter typed `Mu` or `Junction` (or a slurpy)
// accepts one, and Rakudo hands the junction over intact instead of
// autothreading — `method name(Mu $name)` is exactly how Path::Finder takes
// `"*.pm" | "*.pod"` and threads it itself, one layer down. Same rule
// callCallable already applies to plain subs.
bool Interpreter::methodTakesJunction(const Value& inv, const std::string& m, size_t ai) {
    ClassInfo* cls = nullptr;
    if (inv.t == VT::Object && inv.obj()) cls = inv.obj()->cls.get();
    else if (inv.t == VT::Type) {
        auto it = classes_.find(inv.s.str());
        if (it != classes_.end()) cls = it->second.get();
    }
    if (!cls) return false;
    Value* mv = cls->findMethod(m);
    if (!mv || mv->t != VT::Code || !mv->code()) return false;
    std::vector<const Value*> cands;
    if (mv->code()->isMultiDispatcher) for (auto& c : mv->code()->candidates) cands.push_back(&c);
    else cands.push_back(mv);
    for (const Value* cv : cands) {
        if (!cv->code() || !cv->code()->params) continue;
        size_t seen = 0;
        for (const Param& p : *cv->code()->params) {
            if (p.named || p.invocant) continue;
            if (p.slurpy) return true;
            if (seen == ai) { if (p.type == "Mu" || p.type == "Junction") return true; break; }
            seen++;
        }
    }
    return false;
}

// `.kv`/`.keys`/`.values`/`.pairs`/`.antipairs` answer a Seq on EVERY container in
// Rakudo — Hash, Array, List, Pair, Match alike. Marking them at the one dispatch
// point keeps that uniform instead of tagging a dozen construction sites.
Value Interpreter::methodCall(const Value& inv, const std::string& m, ValueList args, const std::vector<ExprPtr>* rwArgs,
                              bool skipOwn) {
    // A JUNCTION argument autothreads: `$s.contains(none "01")` is a junction of
    // the per-eigenstate answers, which collapses later. This used to live only in
    // the MethodCall eval arm, so every internal caller lost it — the one that
    // reported it was a curried `*.contains(none "01")`, whose WhateverCode calls
    // methodCall directly and got a plain (wrong) Bool, so a sequence using it as
    // its endpoint never terminated (issue #22).
    //
    // MATCHER positions are exempt: a junction handed to grep/first/match is a
    // smartmatch target, not a value to thread over.
    for (size_t ai = 0; ai < args.size(); ai++) {
        if (!isJunction(args[ai])) continue;            // the common path stops here
        // Exactly the list the eval arm carried before this moved here — no
        // additions: this rule's job is to run everywhere, not to change.
        static const std::set<std::string> junctionMatcherMethods = {
            "grep", "first", "classify", "categorize", "index-of", "split", "comb", "match", "subst"};
        if (m.empty() || m[0] == '^' || junctionMatcherMethods.count(m)) break;
        if (methodTakesJunction(inv, m, ai)) continue;  // the signature asked for it whole
        Value jr = Value::array(); jr.enumName = args[ai].enumName; jr.isList = true;
        for (auto& e : *args[ai].arr()) {
            ValueList a2 = args; a2[ai] = e;
            jr.arr()->push_back(methodCall(inv, m, a2));
        }
        return jr;
    }
    Value r = methodCallInner(inv, m, std::move(args), rwArgs, skipOwn);
    if (r.t == VT::Array && r.isList && r.s.empty() &&
        (m == "kv" || m == "keys" || m == "values" || m == "pairs" ||
         m == "antipairs" || m == "invert" ||
         m == "reverse" || m == "sort" || m == "unique" || m == "squish" ||
         m == "head" || m == "tail" || m == "skip" || m == "rotor" || m == "batch"))
        r.s = "Seq";
    return r;
}

// A method `augment`-ed onto a BUILT-IN type: they are parked in builtinExt_ (keyed
// by type name), and the native ancestry is walked so augmenting Cool/Any reaches
// Int/Str too. Native values and type objects consult this ahead of the built-in
// method table.
Value jsonParseDoc(const std::string& text) {
    size_t i = 0; Value out;
    if (!jsonParseValue(text, i, out, JsonCfg{})) return Value::any();
    return out;
}

Value* Interpreter::builtinExtMethod(const Value& inv, const std::string& m) {
    if (builtinExt_.empty() || inv.t == VT::Object) return nullptr;
    std::string tn = inv.t == VT::Type ? inv.s : inv.typeName();
    auto lookup = [&](const std::string& t) -> Value* {
        auto ti = builtinExt_.find(t);
        if (ti == builtinExt_.end()) return nullptr;
        auto mi = ti->second.find(m);
        return mi == ti->second.end() ? nullptr : &mi->second;
    };
    if (Value* f = lookup(tn)) return f;
    for (const std::string& anc : typeAncestry(tn))
        if (anc != tn) if (Value* f = lookup(anc)) return f;
    return nullptr;
}

Value Interpreter::methodCallInner(const Value& invIn, const std::string& mName, ValueList args, const std::vector<ExprPtr>* rwArgs,
                                   bool skipOwn) {
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
    // A Proxy stamped with a SUBCLASS identity (`class AttrProxy is Proxy`,
    // constructed via callwith in its own .new) dispatches that class's methods
    // — public and private — with the proxy itself as self, BEFORE the FETCH
    // rule (AttrX::Mooish drives its whole lazy machinery this way).
    if (invIn.t == VT::Hash && invIn.hashKind == "Proxy" && invIn.hash()) {
        auto ki = invIn.hash()->find("\x01cls");
        if (ki != invIn.hash()->end()) {
            auto cit = classes_.find(ki->second.s);
            if (cit != classes_.end())
                if (Value* um = cit->second->findMethod(mName))
                    return invokeMethod(*um, invIn, args, rwArgs);
        }
    }
    // A method called ON a Proxy is a READ of what it stands for: FETCH first.
    // (`$doc.root[0].name` — AT-POS hands back a Proxy, and every method after it
    // was landing on the container.) `.VAR` deliberately still sees the Proxy.
    if (invIn.t == VT::Hash && invIn.hashKind == "Proxy" && invIn.hash() &&
        mName != "VAR" && mName != "FETCH" && mName != "STORE" && mName != "WHERE") {
        if (invIn.hash()->count("FETCH")) {
            Value fetched = deproxy(invIn);
            if (!(fetched.t == VT::Hash && fetched.hashKind == "Proxy"))
                return methodCallInner(fetched, mName, std::move(args), rwArgs, skipOwn);
        }
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
                          inv.t == VT::Object && inv.obj() && inv.obj()->cls &&
                          inv.obj()->cls->findMethod("perl");
    // `.Stringy` is Mu's string coercion — `self.Str` — and was missing entirely,
    // so it died on every type including the enum values LWP::Simple builds its
    // request line from. Forwarded the same way `.perl` forwards to `.raku`, with
    // the same escape: a class that defines its own `method Stringy` keeps it.
    static const std::string kStr = "Str";
    const bool userStringy = mName == "Stringy" &&
                             inv.t == VT::Object && inv.obj() && inv.obj()->cls &&
                             inv.obj()->cls->findMethod("Stringy");
    MName m{(mName == "perl" && !userPerl)      ? kRaku
          : (mName == "Stringy" && !userStringy) ? kStr
                                                 : mName};
    m.skipOwn = skipOwn;
    auto a0 = [&]() -> Value { return args.empty() ? Value::any() : args[0]; };
    // A USER OBJECT whose class defines the method dispatches HERE. The arm that
    // does it lives in methodCallPart2, which is reached ~3000 lines down this
    // function, so every call to a user method first walked the entire built-in
    // ladder — thousands of `m == "..."` string compares. That is why a plain
    // method call measured 5.6x Rakudo and a private one 10x, while rakupp's own
    // loop is 4x FASTER: the cost was dispatch, not the work.
    //
    // Only the plain case is taken. `new` (whose multis ADD to the default
    // constructor), a role STUB satisfied by an attribute, and a `handles`-
    // delegated name all have rules the full arm knows, so they fall through to
    // it unchanged. Everything in the ladder that touches a user object already
    // guards itself with `!cls->findMethod(m)`, so nothing there wanted this call.
    if (inv.t == VT::Object && inv.obj() && inv.obj()->cls && m != "new" && !m.skipOwn) {
        auto ci = inv.obj()->cls;
        if (ci->delegatedNames.empty() || !ci->delegatedNames.count(m)) {
            ClassInfo* owner = nullptr;
            if (Value* um = ci->findMethodForCall(m, langRev_ < 2, &owner))
                if (!(um->t == VT::Code && um->code() && um->code()->isStub))
                    // hand the resolution on — invokeMethodChain would otherwise
                    // hash and walk for the same name all over again
                    return invokeMethodChain(m, ci.get(), inv, std::move(args), rwArgs, um, owner);
        }
    }
    // A grammar CURSOR — the `self` of a method reached through `<.method>`
    // (issue #64) — answers its grammar's RULES as method calls (`self.b`,
    // `self.expr($x)`: matched in the running parse, at the cursor's position),
    // then the grammar's own methods (`self.panic(…)`), and is otherwise the
    // Match it is: `.pos`, `.target`, `.orig` fall through to the Match arms.
    if (inv.t == VT::Match && inv.md() && inv.md()->cursor && !m.skipOwn) {
        auto cur = std::static_pointer_cast<GrammarCursor>(inv.md()->cursor);
        RxCursorCall* call = cur->call();
        if (call && call->hasRule(m)) {
            // arguments travel as `<rule(…)>` call text: quoted strings, which is
            // what a rule parameter is once it is bound
            std::string argText;
            for (auto& a : args) {
                if (a.t == VT::Pair) continue;
                if (!argText.empty()) argText += ", ";
                argText += '\'';
                for (char c : a.toStr()) { if (c == '\\' || c == '\'') argText += '\\'; argText += c; }
                argText += '\'';
            }
            ParseNode node;
            if (!call->callRule(m, argText, (long)inv.rTo(), node)) return Value::nil();
            Value r = matchFromNode(node, *cur->input, cur->input);
            auto next = std::make_shared<GrammarCursor>(*cur); // shares `live`
            next->rule = m;
            r.mdW().cursor = next;
            return r;
        }
        if (call && cur->grammar) {
            ClassInfo* owner = nullptr;
            if (Value* um = cur->grammar->findMethodForCall(m, langRev_ < 2, &owner))
                return invokeMethodChain(m, cur->grammar, inv, std::move(args), rwArgs, um, owner);
        }
        else if (!call && cur->grammar && (cur->grammar->findRule(m) || cur->grammar->findMethodForCall(m)))
            throw RakuError{Value::typeObj("X::AdHoc"),
                "Cannot call '" + std::string(m) + "' on a grammar cursor outside the parse it belongs to"};
    }
    // `@a.BIND-POS($i, $container)` is answered in methodCallTail, which is the
    // LAST of the three dispatch parts — so every call walked the whole built-in
    // ladder to reach it. BinaryHeap's sift-down makes about eight per call and
    // 300k for one `Graph.diameter`, which put `std::string == const char*` at the
    // top of that profile. The arm itself still lives in methodCallTail (with the
    // immutable-List check and the rest); this is only the shortcut to it.
    if (inv.t == VT::Array && inv.arr() && args.size() >= 2 && m == "BIND-POS" && !inv.isList) {
        long long i = args[0].toInt();
        if (i < 0) i += (long long)inv.arr()->size();
        if (i >= 0) {
            while ((long long)inv.arr()->size() <= i) inv.arr()->push_back(Value::any());
            (*inv.arr())[(size_t)i] = args[1];
            return args[1];
        }
    }
    // read the environment ONCE, not on every method call — getenv walks environ
    static const bool kTrace = std::getenv("RAKUPP_TRACE") != nullptr;
    if (kTrace) std::cerr << "[M] ." << m << " on type=" << (int)inv.t << " s=[" << inv.s << "]" << (inv.t==VT::Object && inv.obj() && inv.obj()->cls ? " ("+inv.obj()->cls->name+")" : "") << "\n";
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
            // run-script("zef"): what an installed bin wrapper calls. Find the
            // installed dist whose files map carries "bin/<name>", load the stored
            // script from <repo>/resources/<id>, and run it AS THE PROGRAM (its
            // `use Zef::CLI` mainline + MAIN dispatch; run-script never returns).
            if (m == "run-script") {
                std::string script = args.empty() ? "" : args[0].toStr();
                for (auto& repo : rakuRepoPrefixes()) {
                    std::string distDir = repo + "/dist";
                    DIR* d = opendir(distDir.c_str());
                    if (!d) continue;
                    // among all dists providing this bin (stale versions linger
                    // after upgrades), pick the highest "ver"
                    auto verKey = [](const std::string& meta) {
                        std::vector<long long> key;
                        auto vp = meta.find("\"ver\"");
                        if (vp == std::string::npos) return key;
                        auto q1 = meta.find('"', meta.find(':', vp) + 1);
                        auto q2 = meta.find('"', q1 + 1);
                        if (q1 == std::string::npos || q2 == std::string::npos) return key;
                        std::string v = meta.substr(q1 + 1, q2 - q1 - 1);
                        long long cur = 0; bool any = false;
                        for (char c : v) {
                            if (c >= '0' && c <= '9') { cur = cur * 10 + (c - '0'); any = true; }
                            else if (any) { key.push_back(cur); cur = 0; any = false; }
                        }
                        if (any) key.push_back(cur);
                        return key;
                    };
                    std::string content, distId; std::vector<long long> bestVer;
                    while (struct dirent* e = readdir(d)) {
                        std::string n = e->d_name; if (n == "." || n == "..") continue;
                        std::ifstream mf(distDir + "/" + n);
                        if (!mf) continue;
                        std::ostringstream ms; ms << mf.rdbuf(); std::string meta = ms.str();
                        std::string tag = "\"bin/" + script + "\"";
                        auto p = meta.find(tag);
                        if (p == std::string::npos) continue;
                        p = meta.find(':', p + tag.size());
                        auto q1 = meta.find('"', p), q2 = q1 == std::string::npos ? q1 : meta.find('"', q1 + 1);
                        if (p == std::string::npos || q2 == std::string::npos) continue;
                        std::string blobId = meta.substr(q1 + 1, q2 - q1 - 1);
                        std::ifstream sf(repo + "/resources/" + blobId);
                        // older rakupp installs put bin blobs in bin/<sha>
                        if (!sf) sf.open(repo + "/bin/" + blobId);
                        if (!sf) continue;
                        auto vk = verKey(meta);
                        if (!distId.empty() && vk <= bestVer) continue;
                        std::ostringstream sc; sc << sf.rdbuf();
                        content = sc.str(); distId = n; bestVer = vk;
                    }
                    closedir(d);
                    if (content.empty()) continue;
                    resourceStack_.push_back(buildResourceMap(repo, distId));
                    distStack_.push_back(buildInstalledDistribution(repo, distId));
                    struct RG { ValueList& s; ~RG() { s.pop_back(); } } rg{resourceStack_};
                    struct DG { ValueList& s; ~DG() { s.pop_back(); } } dg{distStack_};
                    // the caller (an installed bin wrapper) has a &MAIN of its
                    // own in scope; the nested run() must not auto-invoke it
                    inheritedMainBarrier_ = tctx_.cur ? tctx_.cur->find("&MAIN") : nullptr;
                    int code = 0;
                    try {
                        Lexer lx(content);
                        Parser ps(lx.tokenize());
                        Program prog = ps.parseProgram();
                        code = run(prog);
                    } catch (const ParseError& pe) {
                        std::cerr << "===SORRY!=== Parse error at line " << pe.line
                                  << " in installed script '" << script << "': " << pe.what() << "\n";
                        code = 2;
                    }
                    throw ExitEx{code};
                }
                throw RakuError{Value::typeObj("X::AdHoc"),
                    "Could not find an installed script named '" + script + "'"};
            }
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
        if (inv.t == VT::Object && inv.obj() && inv.obj()->cls &&
            inv.obj()->cls->name == "CompUnit::Repository::FileSystem" &&
            (m == "files" || m == "candidates" || m == "installed")) {
            Value e = Value::array(); e.isList = true; e.s = "Seq"; return e;
        }
        if (inv.t == VT::Object && inv.obj() && inv.obj()->cls &&
            inv.obj()->cls->name == "CompUnit::Repository::Installation") {
            auto& at = inv.obj()->attrs;
            std::string prefix = at.count("prefix") ? at["prefix"].toStr() : "";
            std::string name = at.count("name") ? at["name"].toStr() : "";
            if (m == "prefix") { Value p = Value::str(prefix); p.hashKind = "IO"; return p; }
            if (m == "name") return Value::str(name);
            if (m == "id" || m == "short-id") return Value::str(name.empty() ? std::string("inst") : name);
            if (m == "Str" || m == "gist" || m == "raku") return Value::str("inst#" + prefix);
            if (m == "path-spec") return Value::str("inst#" + prefix);
            if (m == "can-install") return Value::boolean(true);
            if (m == "repo-chain") {
                // The whole chain the loader searches — home, then every site and
                // vendor prefix — not just this one link. A program that walks the
                // chain to enumerate installed distributions (rather than assuming
                // ~/.raku) saw one repository and missed everything zef installed
                // system-wide.
                Value e = Value::array(); e.isList = true; e.s = "Seq";
                const char* home = getenv("HOME");
                std::string homeRepo = std::string(home ? home : "") + "/.raku";
                for (const std::string& pre : rakuRepoPrefixes()) {
                    auto od = std::make_shared<ObjectData>();
                    od->cls = inv.obj()->cls;   // the Installation class, already in hand
                    std::string nm = pre == homeRepo ? "home"
                                   : pre.size() > 5 && pre.compare(pre.size()-5,5,"/site") == 0 ? "site"
                                   : pre.size() > 7 && pre.compare(pre.size()-7,7,"/vendor") == 0 ? "vendor"
                                   : "inst";
                    od->attrs["name"] = Value::str(nm);
                    Value p2 = Value::str(pre); p2.hashKind = "IO";
                    od->attrs["prefix"] = p2;
                    e.arr()->push_back(Value::object(od));
                }
                // …and the `core` repository, which sits beside each `site`. It is
                // REPORTED but never searched: rakupp answers the core types from
                // its own builtins rather than from Rakudo's sources, so it stays
                // out of rakuRepoPrefixes(). Listing it keeps the chain an honest
                // description of the installation.
                for (const std::string& pre : rakuRepoPrefixes()) {
                    if (!(pre.size() > 5 && pre.compare(pre.size()-5,5,"/site") == 0)) continue;
                    std::string core = pre.substr(0, pre.size()-5) + "/core";
                    struct stat st;
                    if (stat(core.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) continue;
                    auto od = std::make_shared<ObjectData>();
                    od->cls = inv.obj()->cls;
                    od->attrs["name"] = Value::str("core");
                    Value p3 = Value::str(core); p3.hashKind = "IO";
                    od->attrs["prefix"] = p3;
                    e.arr()->push_back(Value::object(od));
                }
                if (e.arr()->empty()) e.arr()->push_back(inv);
                return e;
            }
            if (m == "candidates") {
                // Phase 1: no dist enumeration yet — an empty candidate list is correct
                // for 'core' (rakupp has no CORE dist) and keeps zef's ignore list empty.
                Value e = Value::array(); e.isList = true; e.s = "Seq"; return e;
            }
            // `$curi.installed` — the distributions written under this repo. Each
            // `dist/<id>` file is the dist's META as JSON (that is what .install
            // writes), so the listing is those, parsed, as Distribution objects.
            // zef's `list --installed` walks this; answering an empty list made it
            // print nothing at all.
            if (m == "installed") {
                Value e = Value::array(); e.isList = true; e.s = "Seq";
                if (DIR* dd = opendir((prefix + "/dist").c_str())) {
                    while (struct dirent* de = readdir(dd)) {
                        std::string n = de->d_name;
                        if (n == "." || n == "..") continue;
                        std::ifstream in(prefix + "/dist/" + n);
                        if (!in) continue;
                        std::ostringstream ss; ss << in.rdbuf();
                        Value meta = jsonParseDoc(ss.str());
                        if (meta.t != VT::Hash) continue;
                        Value d = Value::makeHash(); d.hashKind = "Distribution";
                        (*d.hash())["meta"] = meta;
                        Value p2 = Value::str(prefix); p2.hashKind = "IO";
                        (*d.hash())["prefix"] = p2;
                        e.arr()->push_back(d);
                    }
                    closedir(dd);
                }
                return e;
            }
            // `.files($name, :$ver, :$auth, :$api)` looks up an INSTALLED file (a
            // `bin/` script or a `resources/` entry) across the repo's distributions.
            // rakupp does not enumerate dists yet — the same Phase-1 gap as
            // `.candidates` — so the honest answer is the empty list every caller
            // already handles with `// "Nada"`, not a missing method.
            if (m == "files") {
                Value e = Value::array(); e.isList = true; e.s = "Seq"; return e;
            }
            // `$*REPO.need($dep-spec)` — LOAD the named module, the way `use` does,
            // and answer a CompUnit for it (Nil if it will not load, so zef's
            // `unless try $*REPO.need($spec)` skips the plugin). zef 1.x loads every
            // one of its own backends through this instead of `require ::($name)`.
            if (m == "need" || m == "resolve") {
                std::string want;
                if (!args.empty()) {
                    if (args[0].t == VT::Str) want = args[0].s;
                    else if (args[0].t == VT::Hash && args[0].hash()) {
                        auto it = args[0].hash()->find("short-name");
                        if (it != args[0].hash()->end()) want = it->second.toStr();
                    }
                }
                if (want.empty()) return Value::nil();
                if (m == "need") loadModule(want);            // throws if it cannot load
                Value cu = Value::makeHash(); cu.hashKind = "CompUnit";
                (*cu.hash())["short-name"] = Value::str(want);
                (*cu.hash())["repo"] = inv;
                return cu;
            }
            if (m == "install") {
                // $cur.install($dist, :$force) — write the CURI layout under `prefix`
                // (sources/<sha>, short/<sha1(name)>/<dist-id>, dist/<dist-id> JSON,
                // resources/, bin/). rakupp reads exactly this to resolve `use`.
                // A prefix-less repository object would write into "/sources" and
                // fail SILENTLY on every file — refuse loudly instead (the
                // .new(prefix=>) spelling does not thread the prefix through;
                // repository-for-spec("inst#/path") does).
                if (prefix.empty())
                    throw RakuError{Value::typeObj("X::AdHoc"),
                        "install: this repository object carries no prefix — construct it with "
                        "CompUnit::RepositoryRegistry.repository-for-spec('inst#/path')"};
                Value dist = args.empty() ? Value::any() : args[0];
                bool force = false;
                for (auto& a : args)
                    if (a.t == VT::Pair && a.s == "force") force = a.pairVal() && a.pairVal()->truthy();
                Value metaV = methodCall(dist, "meta", ValueList{});
                if (metaV.t != VT::Hash || !metaV.hash())
                    throw RakuError{Value::typeObj("X::AdHoc"), "install: distribution has no meta"};
                auto& meta = *metaV.hash();
                auto mstr = [&](const char* k) -> std::string {
                    auto it = meta.find(k); return it != meta.end() ? it->second.toStr() : "";
                };
                std::string name = mstr("name");
                std::string ver  = meta.count("version") ? meta["version"].toStr() : mstr("ver");
                std::string auth = mstr("auth");
                std::string api  = mstr("api");
                // NB the "\0"s append NOTHING — std::string + const char* stops at
                // the terminator — so the id is the four fields CONCATENATED, and
                // every dist-id on every store on disk was computed that way.
                // Adding the separators would rename every record and orphan every
                // installed distribution: this line is a format, not a hash call.
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
                if (!force && provV.t == VT::Hash && provV.hash() && !provV.hash()->empty()) {
                    std::string firstMod = provV.hash()->begin()->first;
                    std::string sentinel = prefix + "/short/" + sha1hex(firstMod) + "/" + distId;
                    if (std::ifstream(sentinel).good())
                        throw RakuError{Value::typeObj("X::AdHoc"),
                            name + ":ver<" + ver + ">:auth<" + auth + "> is already installed"};
                }
                mkdirp(prefix + "/sources"); mkdirp(prefix + "/short"); mkdirp(prefix + "/dist");
                Value filesOut = Value::makeHash();
                // The dist RECORD's provides must be the NESTED store shape
                // Rakudo writes — {mod => {relpath => {file => <source-id>,
                // time => Nil}}} — not META6's flat mod=>path. zef's uninstall
                // walks provides{$mod}{$path}<file>, and a flat record died
                // there with "Type Str does not support associative indexing";
                // loading never noticed, since resolution reads short/ entries.
                Value provOut = Value::makeHash();
                if (provV.t == VT::Hash && provV.hash())
                    for (auto& kv : *provV.hash()) {
                        std::string mod = kv.first, srcRel;
                        // provides value is either the source path, or {path => {file,…}}
                        if (kv.second.t == VT::Hash && kv.second.hash() && !kv.second.hash()->empty())
                            srcRel = kv.second.hash()->begin()->first;
                        else srcRel = kv.second.toStr();
                        std::string content = slurp(distRoot + "/" + srcRel);
                        std::string srcSha = sha1hex(content);
                        { std::ofstream o(prefix + "/sources/" + srcSha, std::ios::binary); o << content; }
                        std::string sdir = prefix + "/short/" + sha1hex(mod);
                        mkdirp(sdir);
                        std::ofstream o(sdir + "/" + distId);
                        o << ver << "\n" << auth << "\n" << api << "\n" << srcSha << "\n" << distId << "\n";
                        (*filesOut.hash())[srcRel] = Value::str(srcSha);
                        Value leaf = Value::makeHash();
                        (*leaf.hash())["file"] = Value::str(srcSha);
                        (*leaf.hash())["time"] = Value::nil();
                        Value byPath = Value::makeHash();
                        (*byPath.hash())[srcRel] = leaf;
                        (*provOut.hash())[mod] = byPath;
                    }
                // resources/ and bin/ — zef injects these into meta<files> (rel-path => src)
                if (meta.count("files") && meta["files"].t == VT::Hash && meta["files"].hash()) {
                    mkdirp(prefix + "/resources"); mkdirp(prefix + "/bin");
                    for (auto& kv : *meta["files"].hash()) {
                        std::string rel = kv.first, src = kv.second.toStr();
                        std::string content = slurp(src.empty() ? distRoot + "/" + rel : src);
                        std::string sha = sha1hex(content);
                        // EVERY file blob lands in resources/ — bin/ scripts too.
                        // That is Rakudo's layout (bin/ holds only the named
                        // wrappers below), and it is what run-script reads. This
                        // repo's first cut put bin blobs in bin/<sha>, which left
                        // run-script blind to them.
                        { std::ofstream o(prefix + "/resources/" + sha, std::ios::binary); o << content; }
                        (*filesOut.hash())[rel] = Value::str(sha);
                        // ...and its short/ index entry: Rakudo's `.files("bin/x")`
                        // — the lookup its run-script resolves a bare script name
                        // through — reads short/<sha1(rel-path)>/<dist-id>, the
                        // same 5-line format the provides entries use. Without it
                        // a wrapper runs under rakupp but dies under Rakudo with
                        // "No candidate found".
                        {
                            std::string fdir = prefix + "/short/" + sha1hex(rel);
                            mkdirp(fdir);
                            std::ofstream o(fdir + "/" + distId);
                            o << ver << "\n" << auth << "\n" << api << "\n" << sha << "\n" << distId << "\n";
                        }
                        // bin/<name> also gets a NAMED, executable wrapper —
                        // Rakudo's own template — so an installed command runs by
                        // name under either engine once <prefix>/bin is on PATH.
                        // (Rakudo adds -m/-j/-js backend variants; the bare name
                        // is the one people run, and the only one written here.)
                        if (rel.rfind("bin/", 0) == 0 && rel.find('/', 4) == std::string::npos && rel.size() > 4) {
                            std::string script = rel.substr(4);
                            std::string wpath = prefix + "/bin/" + script;
                            { std::ofstream w(wpath, std::ios::binary);
                              w << "#!/usr/bin/env raku\n"
                                   "sub MAIN(:$name is copy, :$auth, :$ver, *@, *%) {\n"
                                   "    CompUnit::RepositoryRegistry.run-script(\"" << script
                                << "\", :dist-name<" << name << ">, :$name, :$auth, :$ver);\n"
                                   "}\n"; }
#ifndef _WIN32
                            ::chmod(wpath.c_str(), 0755);
#endif
                        }
                    }
                }
                // dist/<id> — the meta index (list-installed reads it; buildResourceMap
                // scans it for `resources/…` → the on-disk resource copy).
                Value distMeta = metaV; distMeta.setHash(std::make_shared<ValueMap>(meta));
                (*distMeta.hash())["files"] = filesOut;
                if (provOut.hash() && !provOut.hash()->empty())
                    (*distMeta.hash())["provides"] = provOut;
                // Rakudo's records carry `ver` beside META6's `version`; zef's
                // list/upgrade logic reads the short spelling (ver<0> without it)
                if (!meta.count("ver") && !ver.empty())
                    (*distMeta.hash())["ver"] = Value::str(ver);
                { std::ofstream o(prefix + "/dist/" + distId); o << jsonEncode(distMeta); }
                // the dist-id, so the CALLER can record provenance ("what did
                // rakupp install" is the question uninstall refuses without)
                return Value::str(distId);
            }
        }
    }
    if (m == "WHY") {
        // declarator pod: `#| text` above a sub/method/class answers .WHY
        if (inv.t == VT::Code && inv.code() && !inv.code()->pod.empty())
            return Value::str(inv.code()->pod);
        // a Parameter's own doc, plumbed from Param.pod at reflection time
        if (inv.t == VT::Hash && inv.hashKind == "Parameter" && inv.hash() && inv.hash()->count("why"))
            return (*inv.hash())["why"];
        if (inv.t == VT::Type) {
            auto it = classes_.find(inv.s);
            if (it != classes_.end() && !it->second->pod.empty())
                return Value::str(it->second->pod);
        }
        if (inv.t == VT::Object && inv.obj() && inv.obj()->cls && !inv.obj()->cls->pod.empty())
            return Value::str(inv.obj()->cls->pod);
        return Value::nil();
    }
    // Any.hash is an empty Hash (Rakudo: `my $x; $x.hash` → {}) — zef reads
    // `$dist.meta<files>.hash.keys` where <files> may be absent.
    if (m == "hash" && (inv.t == VT::Any || (inv.t == VT::Type && inv.s == "Any")))
        return Value::makeHash();
    if (m == "pairup" && (inv.t == VT::Any || inv.t == VT::Type || inv.t == VT::Nil)) {
        Value e = Value::array(); e.isList = true; e.s = "Seq"; return e; // :U invocant
    }
    // `.Str` / `.gist` / `~` on a LIST goes through each element's own .Str, so a
    // list of objects with a user `method Str` renders as those strings (see strOf).
    if (inv.t == VT::Array && inv.arr() && inv.enumName.empty() &&
        (m == "Str" || m == "Stringy")) {
        bool anyObj = false;
        for (auto& e : *inv.arr()) if (e.t == VT::Object) { anyObj = true; break; }
        if (anyObj) return Value::str(strOf(inv));
    }
    // a binary buffer has no string semantics: .Str is an error (use .decode)
    // — except an ENCODING-typed blob, which knows how to read itself: Rakudo's
    // `utf8.Str` is `.decode`, and PSGI stringifies a `Str.encode` body exactly
    // so (`$output ~= $segment.Str`).
    if (inv.t == VT::Str && (inv.hashKind == "Buf" || inv.hashKind == "Blob") &&
        m == "Str") {
        if (inv.enumName == "utf8" || inv.enumName == "utf16" || inv.enumName == "utf32") {
            ValueList none; return methodCall(inv, "decode", none);
        }
        throwTyped("X::Buf::AsStr", {{"method", "Str"}},
                   "Cannot use a Buf as a string, but you called the Str method on it");
    }
    // reverse/rotate are illegal only on a MULTI-dimensional fixed array; a 1-dim
    // shaped array reverses/rotates fine (returns a reordered list, no resize).
    if (inv.t == VT::Array && inv.shape() && inv.shape()->size() >= 2 && (m == "reverse" || m == "rotate"))
        throw RakuError{Value::typeObj("X::IllegalOnFixedDimensionArray"),
                        "Cannot " + m + " a fixed-dimension array"};
    // Multi-dim shaped array (`my @a[2;2]`) — keys/values/kv/pairs/antipairs/flat/
    // iterator walk the LEAVES, keyed by index tuples. (A 1-dim shaped array uses
    // the ordinary Array handlers: keys are plain indices, .flat is a Seq, etc.)
    if (inv.t == VT::Array && inv.shape() && inv.shape()->size() >= 2 &&
        (m == "keys" || m == "values" || m == "kv" || m == "pairs" ||
         m == "antipairs" || m == "flat" || m == "iterator")) {
        size_t ndim = inv.shape()->size();
        std::vector<std::pair<Value, Value>> ents; // (index key, leaf value)
        std::vector<long long> idx;
        std::function<void(const Value&)> walk = [&](const Value& node) {
            if (idx.size() == ndim) {
                Value key;
                if (ndim == 1) key = Value::integer(idx[0]);
                else { key = Value::array(); key.isList = true;
                       for (auto ix : idx) key.arr()->push_back(Value::integer(ix)); }
                ents.push_back({key, node});
                return;
            }
            if (node.t == VT::Array && node.arr())
                for (size_t i = 0; i < node.arr()->size(); i++) {
                    idx.push_back((long long)i); walk((*node.arr())[i]); idx.pop_back();
                }
        };
        walk(inv);
        if (m == "iterator") {
            Value it = Value::makeHash(); it.hashKind = "Iterator";
            Value items = Value::array();
            for (auto& e : ents) items.arr()->push_back(e.second);
            (*it.hash())["items"] = items; (*it.hash())["pos"] = Value::integer(0);
            return it;
        }
        Value o = Value::array(); o.isList = true;
        for (auto& e : ents) {
            if (m == "keys") o.arr()->push_back(e.first);
            else if (m == "values" || m == "flat") o.arr()->push_back(e.second);
            else if (m == "kv") { o.arr()->push_back(e.first); o.arr()->push_back(e.second); }
            else if (m == "pairs") { Value p = Value::pair(e.first.toStr(), e.second); p.pairKeyM() = std::make_shared<Value>(e.first); o.arr()->push_back(std::move(p)); }
            else { Value p = Value::pair(e.second.toStr(), e.first); p.pairKeyM() = std::make_shared<Value>(e.second); o.arr()->push_back(std::move(p)); } // antipairs
        }
        return o;
    }
    // A multi-dim shaped array renders its structure: rows on their own lines for
    // .gist, and a `Array.new(:shape(…), row, …)` constructor for .raku.
    if (inv.t == VT::Array && inv.shape() && inv.shape()->size() >= 2 && inv.arr() &&
        (m == "gist" || m == "raku")) {
        if (m == "gist") {
            std::string out = "[";
            for (size_t i = 0; i < inv.arr()->size(); i++) { if (i) out += "\n "; out += gistOf((*inv.arr())[i]); }
            return Value::str(out + "]");
        }
        std::string ctor;
        if (inv.ofType().empty() || inv.ofType() == "Any" || inv.ofType() == "Mu") ctor = "Array";
        else if (ascii::islower((unsigned char)inv.ofType()[0])) ctor = "array[" + inv.ofType() + "]";
        else ctor = "Array[" + inv.ofType() + "]";
        std::string out = ctor + ".new(:shape(";
        for (size_t i = 0; i < inv.shape()->size(); i++) { if (i) out += ", "; out += std::to_string((*inv.shape())[i]); }
        out += ")";
        for (auto& row : *inv.arr()) { ValueList none; out += ", " + methodCall(row, "raku", none).toStr(); }
        return Value::str(out + ")");
    }
    if (inv.t == VT::Array && inv.shape() && !inv.shape()->empty() && inv.arr() && m == "clone") {
        Value c = inv; // deep-copy the nested storage so containers are independent
        std::function<Value(const Value&)> deep = [&](const Value& n) -> Value {
            if (n.t == VT::Array && n.arr()) { Value a = n; a.setArr(std::make_shared<ValueList>());
                for (auto& e : *n.arr()) a.arr()->push_back(deep(e)); return a; }
            return n;
        };
        c = deep(inv);
        c.shapeM() = std::make_shared<std::vector<long long>>(*inv.shape());
        return c;
    }
    // Most list operations on a multi-dim shaped array run over its LEAVES — flatten
    // the fixed structure to a plain list and delegate.
    if (inv.t == VT::Array && inv.shape() && inv.shape()->size() >= 2 && inv.arr() &&
        (m == "join" || m == "map" || m == "grep" || m == "combinations" ||
         m == "permutations" || m == "rotor" || m == "pick" || m == "roll" ||
         m == "first" || m == "reduce" || m == "sum" || m == "min" || m == "max" ||
         m == "sort" || m == "reverse" || m == "List" || m == "Seq" || m == "Slip" ||
         m == "Bag")) {
        Value flat = Value::array(); flat.isList = true;
        std::function<void(const Value&)> collect = [&](const Value& n) {
            if (n.t == VT::Array && n.arr()) for (auto& e : *n.arr()) collect(e);
            else flat.arr()->push_back(n);
        };
        for (auto& e : *inv.arr()) collect(e);
        return methodCall(flat, m, args, rwArgs);
    }
    // Junction invocant: the Str-using routines operate on the WHOLE junction
    // (no autothreading — `$j.print` prints the junction's string form, calling
    // each eigenstate's .Str; `$j.printf` treats that form as the format).
    // enumName.empty() first: it rejects everything but junctions/enums in one load.
    if (!inv.enumName.empty() && inv.t == VT::Array && inv.arr() &&
        (inv.enumName == "any" || inv.enumName == "all" || inv.enumName == "one" || inv.enumName == "none") &&
        (m == "printf" || m == "sprintf")) { // format verbs use the joined eigenstates as the FORMAT
        std::string s;
        for (size_t i = 0; i < inv.arr()->size(); i++) {
            if (i) s += " ";
            s += methodCall((*inv.arr())[i], "Str", ValueList{}).toStr();
        }
        if (m == "sprintf") return Value::str(doSprintf(s, args, langRev_));
        // `.printf` — through ioEmit for the output lock and a rebound $*OUT,
        // exactly like the printf builtin.
        return ioEmit(doSprintf(s, args, langRev_), "$*OUT", false);
    }
    // any other method on a junction AUTOTHREADS: call it on each eigenstate,
    // return a junction of the results (`($a & $b).finish`, `$j.defined`, …)
    // (a METAMODEL call `.^name`/`.^WHAT`/… answers for the Junction ITSELF and is
    //  excluded here — `(1 & 2).^name` is "Junction", not a junction of "Int")
    if (!inv.enumName.empty() && inv.t == VT::Array && inv.arr() && !(m.size() && m[0] == '^') &&
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
            j.setArr(std::make_shared<ValueList>());
            for (auto& el : *inv.arr()) j.arr()->push_back(Value::boolean(defined(el)));
            return Value::boolean(j.truthy());
        }
        if (m == "THREAD" && !args.empty()) {
            // shallow map: the block sees each eigenstate whole (junctions included)
            Value out = Value::array(); out.enumName = inv.enumName;
            out.setArr(std::make_shared<ValueList>());
            for (auto& el : *inv.arr()) {
                ValueList one{el};
                noAutothread_ = true;
                out.arr()->push_back(callCallable(args[0], one));
            }
            return out;
        }
        // A SLICE-derived junction (`%h{any ^2}` / `@a[any(0,1)]`; s carries the
        // provenance) takes the resizing mutators on its backing array instead of
        // autothreading them: each thread would die on an Any eigenstate, where
        // Rakudo's slice junction threads over CONTAINERS that autovivify —
        // machinery rakupp does not have. Mutating the temporary keeps it the
        // soft no-op it always was, not a file-killing death.
        static const std::set<std::string> sliceResizers = {
            "push", "append", "pop", "unshift", "prepend", "shift", "splice"};
        if (!junctionOwn.count(m) && !(inv.s == "slice" && sliceResizers.count(m))) {
            Value out = Value::array(); out.enumName = inv.enumName;
            out.setArr(std::make_shared<ValueList>());
            for (auto& el : *inv.arr()) out.arr()->push_back(methodCall(el, m, args, rwArgs));
            return out;
        }
    }
    // `augment class Int {…}`: methods added to a built-in type are parked in
    // builtinExt_ (keyed by type name). Consult it — walking the native ancestry,
    // so augmenting Cool/Any reaches Int/Str too — for native values and type
    // objects, ahead of the built-in method table.
    if (Value* f = builtinExtMethod(inv, m)) {
        // An augment ADDS candidates; it does not hide the built-in ones. A multi
        // whose candidates all reject the args falls through to the built-in
        // method instead of dying — `augment class DateTime { multi method
        // new(Any:U) {…} }` must leave `DateTime.new($str)` working (JSON::Fast).
        if (f->code() && f->code()->isMultiDispatcher) {
            bool anyFits = false;
            ValueList withInv; withInv.push_back(inv);
            for (auto& a : args) withInv.push_back(a);
            for (auto& c : f->code()->candidates) {
                if (c.code() && c.code()->isProto) continue;
                if (scoreCandidate(c, withInv) >= 0 || scoreCandidate(c, args) >= 0) { anyFits = true; break; }
            }
            if (!anyFits) goto builtinExtFallthrough;
        }
        return invokeMethod(*f, inv, std::move(args), rwArgs);
    }
    builtinExtFallthrough:;
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
        auto& items = *(*inv.hash())["items"].arr();
        auto asList = [&]() { Value o = Value::array(); o.isList = true; *o.arr() = items; return o; };
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
                if (a.t == VT::Hash && a.hashKind == "IterationBuffer") for (auto& x : *(*a.hash())["items"].arr()) add.push_back(x);
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
    if (inv.t == VT::Object && inv.obj() && inv.obj()->cls && !inv.obj()->cls->findMethod(m)) {
        static const std::set<std::string> idTypes = {"Str", "Int", "Num", "Rat", "Bool", "Real", "Numeric"};
        if (idTypes.count(m))
            for (ClassInfo* ci = inv.obj()->cls.get(); ci; ci = ci->parent.get())
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
    // 6.e `.Callable($name)`: the named method as a callable, or a Failure when
    // there is none — a findable method reference, next to .can's list.
    if (m == "Callable" && sixE() && !args.empty() && args[0].t == VT::Str) {
        std::string want = args[0].toStr();
        Value found = methodCall(inv, "can", ValueList{Value::str(want)});
        if (found.t == VT::Array && found.arr() && !found.arr()->empty()) return (*found.arr())[0];
        Value f = rakuppNewFailure();
        (*f.hash())["exception"] = Value::typeObj("X::Method::NotFound");
        (*f.hash())["message"]   = Value::str("No such method '" + want + "' for invocant of type '" +
                                            inv.typeName() + "'");
        return f;
    }
    // 6.e `.snitch`: run a tap (default: note the value) and return self — for
    // sticking a peek into a method chain. Universal, so handle it up front.
    if (m == "snitch" && sixE()) {
        if (!args.empty() && args[0].t == VT::Code) callCallable(args[0], {inv});
        else std::cerr << gistOf(inv) << "\n";
        return inv;
    }
    // `.are`/`.snip` on a type object or lone scalar treat it as a 1-element list
    // (so `Int.are` → Int, `42.are` → Int).
    // …and a Date/DateTime is a VALUE, not a collection: it only happens to be
    // stored as a tagged Hash, so `$date.are` walked its FIELDS and answered
    // Pair. (Data::TypeSystem deduced Pair for every date column.)
    auto dateish = [](const Value& v) {
        return v.t == VT::Hash && (v.hashKind == "Date" || v.hashKind == "DateTime" ||
                                   v.hashKind == "Instant" || v.hashKind == "Duration");
    };
    if ((m == "are" || m == "snip") && inv.t != VT::Array && inv.t != VT::Range &&
        (inv.t != VT::Hash || dateish(inv))) {
        Value one = Value::array(); one.isList = true; one.arr()->push_back(inv);
        return methodCall(one, m, args, rwArgs);
    }

    // an undefined invocant in list context is an empty list (e.g. an unmatched
    // named capture used as `@<x>».ast` or `@<x>.map(...)`).
    if ((inv.t == VT::Any || inv.t == VT::Nil) &&
        (m == "map" || m == "grep" || m == "list" || m == "flat" || m == "values" ||
         m == "keys" || m == "kv" || m == "pairs" || m == "reverse" || m == "sort")) {
        Value o = Value::array(); o.isList = true; return o;
    }
    // …and the same undefined invocant answers the list methods that REDUCE a
    // list rather than return one. Rakudo reaches these through Any's
    // iterable methods (an undefined invocant iterates as one Any element),
    // so `%h<missing>.first({…})` is Nil, not a "no such method" death.
    if ((inv.t == VT::Any || inv.t == VT::Nil) &&
        (m == "first" || m == "head" || m == "tail" || m == "join" ||
         m == "sum" || m == "min" || m == "max" || m == "skip" || m == "unique")) {
        Value o = Value::array(); o.isList = true;
        return methodCall(o, m, args, rwArgs);
    }
    // `.ast`/`.made` on an undefined capture (e.g. `$<optional><tag>.ast`) degrades to Nil.
    if ((inv.t == VT::Any || inv.t == VT::Nil) && (m == "ast" || m == "made")) return Value::nil();

    // metamodel call .^method — .^name/.^WHAT answer the type; others dispatch by bare name
    // a Scalar container record (from `.VAR` on a $-variable): its own name/default,
    // .^name = Scalar via typeName; anything else answers from the held value.
    if (inv.t == VT::Hash && inv.hashKind == "Scalar" && inv.hash() &&
        m != "^name" && m != "WHAT" && m != "WHICH" && m != "raku") {
        if (m == "name")    { auto it = inv.hash()->find("name");    return it != inv.hash()->end() ? it->second : Value::any(); }
        if (m == "dynamic") { // a $*twigil variable is dynamic
            auto it = inv.hash()->find("name");
            std::string n = it != inv.hash()->end() ? it->second.toStr() : "";
            return Value::boolean(n.size() > 1 && n[1] == '*');
        }
        if (m == "default") { auto it = inv.hash()->find("default"); return it != inv.hash()->end() ? it->second : Value::any(); }
        if (m == "of")      { auto it = inv.hash()->find("default"); return (it != inv.hash()->end() && it->second.t == VT::Type) ? it->second : Value::typeObj("Mu"); }
        auto vi = inv.hash()->find("value");
        if (vi != inv.hash()->end()) return methodCall(vi->second, m, std::move(args), rwArgs);
    }
    if (!m.empty() && m[0] == '^') {
        std::string mm = m.substr(1);
        // A COERCION type object (`Foo(Str)`) carries both halves in its name,
        // which is the only place they fit — a type object IS a name here.
        // Getopt::Long picks an option's conversion through exactly this
        // surface: parse the argument as the CONSTRAINT type, and unless the
        // result already is the TARGET type, hand it to the target's COERCE.
        if (mm == "constraint_type" || mm == "target_type" || mm == "coerce") {
            size_t o = inv.t == VT::Type ? inv.s.find('(') : std::string::npos;
            if (o != std::string::npos && o > 0 && !inv.s.empty() && inv.s.back() == ')') {
                std::string target = inv.s.substr(0, o);
                std::string from = inv.s.substr(o + 1, inv.s.size() - o - 2);
                if (mm == "constraint_type") return Value::typeObj(from.empty() ? "Any" : from);
                if (mm == "target_type") return Value::typeObj(target);
                if (args.empty()) return Value::any();
                return methodCall(Value::typeObj(target), "COERCE", ValueList{args[0]});
            }
        }
        if (mm == "name") {
            if (inv.t == VT::Type && inv.s == "Metamodel::ClassHOW")
                return Value::str("Perl6::Metamodel::ClassHOW"); // Rakudo's full metaclass name
            // An OBJECT hash is a PARAMETERIZED Hash: `my Int %h{Str}` is a
            // Hash[Int,Str]. Only the name carries the parameters — typeName()
            // stays "Hash", because dispatch and error messages key on it.
            if (!objHashKeyType(inv).empty()) return Value::str("Hash[" + inv.ofType() + "]");
            // a DEFINITENESS-constrained type reports its smiley: `Any:D.^name`
            if (inv.t == VT::Type && inv.i)
                return Value::str(inv.typeName() + (inv.i == 1 ? ":D" : ":U"));
            // …and a class RENAMED through `.^set_name` answers its current name
            // even through a handle that still carries the old one — the name
            // lives on the metaobject, which is what set_name mutates.
            if (inv.t == VT::Type && inv.ofType().empty()) {
                auto cn = classes_.find(inv.s);
                if (cn != classes_.end() && cn->second && !cn->second->name.empty())
                    return Value::str(cn->second->name);
            }
            return Value::str(inv.typeName());
        }
        // `.^base_type` — the same type without its definiteness constraint
        if (mm == "base_type" && inv.t == VT::Type) {
            Value b = Value::typeObj(inv.s); b.ofTypeM() = inv.ofType(); return b;
        }
        // `.^array_type` — the ELEMENT type of a buffer, which is how
        // NativeHelpers::Blob decides what to allocate. A plain Blob/Buf is
        // uint8; the sized spellings carry theirs in ofType, and utf8 is uint8
        // under its own name. (Rakudo answers this for a buffer VALUE and for
        // utf8; a bare `blob8` type object has no such method there either.)
        if (mm == "array_type" && inv.t == VT::Str &&
            (inv.hashKind == "Buf" || inv.hashKind == "Blob"))
            return Value::typeObj(inv.ofType().empty() ? "uint8" : inv.ofType());
        // The utf* TYPE OBJECTS answer it as well — and only those. Measured:
        // Rakudo gives utf8/utf16/utf32 their element type and refuses blob8,
        // blob32, Buf and Blob, which are aliases rather than classes there.
        if (mm == "array_type" && inv.t == VT::Type) {
            if (inv.s == "utf8")  return Value::typeObj("uint8");
            if (inv.s == "utf16") return Value::typeObj("uint16");
            if (inv.s == "utf32") return Value::typeObj("uint32");
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
        // A user-declared META-METHOD wins over the builtin one: `method
        // ^parameterize(Mu:U \obj, **@pos)` is how a class makes ITSELF
        // parameterizable, and the builtin below would quietly answer `X[Int]`
        // instead of running it (Parameterizable).
        {
            std::string tn = inv.t == VT::Type ? inv.s : inv.typeName();
            auto cit2 = classes_.find(tn);
            if (cit2 != classes_.end() && cit2->second)
                if (Value* umm = cit2->second->findMethod("^" + mm)) {
                    Value tobj2 = Value::typeObj(tn);
                    ValueList a2; a2.reserve(args.size() + 1);
                    a2.push_back(tobj2);
                    for (auto& a : args) a2.push_back(a);
                    return invokeMethod(*umm, tobj2, a2, nullptr);
                }
        }
        // `X.^parameterize(T)` yields the parameterized type `X[T]` (same as `X[T]`)
        if (mm == "parameterize") {
            Value ty = Value::typeObj(inv.t == VT::Type ? inv.s : inv.typeName());
            for (auto& a : args) {
                std::string pn = a.t == VT::Type ? a.s : a.typeName();
                ty.ofTypeM() = ty.ofType().empty() ? pn : ty.ofType() + "," + pn;
            }
            return ty;
        }
        // meta-methods (.^methods/.^attributes/.^parents/…) resolve against the
        // type (HOW), even when called on an instance.
        Value tobj = (inv.t == VT::Object && inv.obj() && inv.obj()->cls) ? Value::typeObj(inv.obj()->cls->name) : inv;
        if ((mm == "lookup" || mm == "find_method") &&
            !(tobj.t == VT::Type && classes_.count(tobj.s))) {
            // builtin-type invocant (`().^lookup('elems')`): a "method object" —
            // a Callable that dispatches the named method on its first argument
            std::string mn = args.empty() ? "" : args[0].toStr();
            // …but Rakudo answers Mu when the type does NOT have the method, and
            // modules gate on exactly that: HTTP::Tiny tells an IO::Path form
            // field from a plain string with `$value.^lookup('slurp')`, and an
            // always-yes answer made it try to slurp the string. rakupp has no
            // table of builtin methods — dispatch is an if-chain over string
            // literals — so the only oracle is to TRY it and read the answer off
            // X::Method::NotFound. The probe runs on a sentinel of the invocant's
            // type (a real IO::Path is swapped for one that cannot exist, so a
            // reader throws "failed to open" rather than reading anything), and
            // names whose dispatch would WRITE are not probed at all: they keep
            // the old permissive answer rather than risk the side effect.
            // A TYPE object is not probeable — `Str.^lookup('parse-base')` is the
            // idiomatic way to reach a method object, and the instance dispatch it
            // names cannot run on the type. Only an INSTANCE invocant is probed.
            // (probeMethodExists carries the shared unsafe-names list and the
            // NotFound dance — one copy, shared with .can's fallback.)
            // A TYPE OBJECT of a builtin is probed through a SENTINEL instance,
            // the way `.can` already does: `Int.^find_method('type')` has to be
            // falsy, and answering a method object for every name made a module
            // walking `while $obj.^find_method('type')` loop past the leaf type
            // and call `.type` on an Int (Data::TypeSystem's is-full-array).
            Value probeInv = inv;
            if (inv.t == VT::Type && !classes_.count(inv.s)) {
                if (inv.s == "Str") probeInv = Value::str("");
                else if (inv.s == "Int") probeInv = Value::integer(0);
                else if (inv.s == "Num") probeInv = Value::number(0);
                else if (inv.s == "Bool") probeInv = Value::boolean(false);
            }
            if (!mn.empty() && probeInv.t != VT::Object && probeInv.t != VT::Type &&
                probeInv.t != VT::Any && probeInv.t != VT::Nil &&
                probeMethodExists(probeInv, mn, "/nonexistent/rakupp-lookup-probe") == -1)
                return Value::typeObj("Mu");
            Value code; code.t = VT::Code; code.setCode(std::make_shared<Callable>());
            code.code()->name = mn; code.code()->isMethod = true;
            code.code()->builtin = [mn](Interpreter& I, ValueList& a) -> Value {
                if (a.empty()) return Value::any();
                Value in2 = a[0]; ValueList rest(a.begin() + 1, a.end());
                return I.methodCall(in2, mn, rest);
            };
            return code;
        }
        // `.^methods` / `.^attributes` on a BUILTIN type: rakupp has no table to
        // enumerate (dispatch is an if-chain), so the honest answer is the empty
        // list — Data::Dump walks `.^mro[1..*]».^methods` to EXCLUDE inherited
        // methods, and dying on Any took the whole dump down.
        // Date/DateTime fall through: they DO answer .^attributes (the
        // synthesized Rakudo-shaped list JSON::Unmarshal rebuilds from).
        // .^method_names rides along with .^methods for the same reason.
        if ((mm == "methods" || mm == "method_names" || mm == "attributes") &&
            !(tobj.t == VT::Type && classes_.count(resolveClassAlias(tobj.s))) &&
            !(mm == "attributes" && tobj.t == VT::Type &&
              (tobj.s == "DateTime" || tobj.s == "Date"))) {
            Value o = Value::array(); o.isList = true; return o;
        }
        return methodCall(tobj, mm, args, rwArgs);
    }

    // ---- Iterator protocol (S07). An iterator over a materialized list:
    // hashKind "Iterator", (*hash)["items"] = the values, (*hash)["pos"] = position.
    // Every copy of the Value shares the same hash map, so advancing `pos` through
    // one copy is visible through all of them (iterators are stateful objects).
    if (inv.t == VT::Hash && inv.hashKind == "Iterator" && inv.hash()) {
        Value& itemsV = (*inv.hash())["items"];
        Value& posV = (*inv.hash())["pos"];
        ValueList& items = itemsV.arrRef();
        long long n = (long long)items.size();
        // A LAZY source: the items Value is the sequence itself (see the
        // .iterator arm), sharing its materialised prefix and its LazySeqState —
        // grow the prefix on demand instead of stopping at whatever happened to
        // be cached when the iterator was made (issue #30: `(1 xx *).iterator`
        // answered one element and then IterationEnd forever).
        auto lazySrc = itemsV.t == VT::Array && itemsV.ext()
                           ? std::static_pointer_cast<LazySeqState>(itemsV.ext())
                           : std::shared_ptr<LazySeqState>();
        auto ensure = [&](long long want) { // grow the cache to `want` elements while the source can
            if (!lazySrc || !lazySrc->appendNext) return;
            while (n < want && lazySrc->appendNext(items)) n = (long long)items.size();
        };
        auto drainFinite = [&] { // materialise ALL of a source that is known to end
            if (!lazySrc || !lazySrc->appendNext || lazySrc->infinite) return;
            while (lazySrc->appendNext(items)) {}
            n = (long long)items.size();
        };
        auto iterEnd = [] { return Value::typeObj("IterationEnd"); };
        auto pushInto = [&](const Value& tgt, long long count) -> long long {
            long long pushed = 0;
            if (tgt.t == VT::Array && tgt.arr())
                while (posV.i < n && pushed < count) { tgt.arr()->push_back(items[posV.i++]); pushed++; }
            return pushed;
        };
        if (m == "pull-one") { ensure(posV.i + 1); return posV.i < n ? items[posV.i++] : iterEnd(); }
        if (m == "push-all" || m == "push-until-lazy" || m == "push-exactly" || m == "push-at-least") {
            if (m == "push-all" || m == "push-until-lazy") {
                drainFinite(); // an endless source pushes only its materialised prefix
                if (!args.empty()) pushInto(args[0], n);
                // push-until-lazy stopping FOR laziness answers the iterator, not
                // IterationEnd: there is more, just not now
                if (m == "push-until-lazy" && lazySrc && lazySrc->infinite) return inv;
                return iterEnd();
            }
            long long want = args.size() > 1 ? args[1].toInt() : 0;
            ensure(posV.i + want);
            long long pushed = args.empty() ? 0 : pushInto(args[0], want);
            return pushed < want ? iterEnd() : Value::integer(pushed);
        }
        if (m == "sink-all") { drainFinite(); posV.i = n; return iterEnd(); }
        // the skip methods answer an INT (1/0), not a Bool
        if (m == "skip-one") { ensure(posV.i + 1); bool ok = posV.i < n; if (ok) posV.i++; return Value::integer(ok ? 1 : 0); }
        if (m == "skip-at-least") {
            long long want = args.empty() ? 0 : args[0].toInt();
            ensure(posV.i + want);
            long long skipped = std::min(want, n - posV.i); if (skipped < 0) skipped = 0;
            posV.i += skipped;
            return Value::integer(skipped >= want ? 1 : 0);
        }
        if (m == "skip-at-least-pull-one") {
            long long want = args.empty() ? 0 : args[0].toInt();
            ensure(posV.i + std::max(0LL, want) + 1);
            posV.i = std::min(n, posV.i + std::max(0LL, want));
            return posV.i < n ? items[posV.i++] : iterEnd();
        }
        if (m == "count-only") { drainFinite(); return Value::integer(n - posV.i); } // remaining, no advance
        if (m == "bool-only") { ensure(posV.i + 1); return Value::boolean(posV.i < n); }
        if (m == "is-lazy") { auto it = inv.hash()->find("lazy"); return Value::boolean(it != inv.hash()->end() && it->second.truthy()); }
        // an iterator over a RANDOMISED or unordered source promises neither a
        // stable order nor an increasing one; the flag rides on the iterator
        if (m == "is-deterministic" || m == "is-monotonically-increasing") {
            auto it = inv.hash()->find("nondeterministic");
            return Value::boolean(it == inv.hash()->end() || !it->second.truthy());
        }
        if (m == "can") { // introspection: which protocol methods this iterator supports
            static const std::set<std::string> ms = {
                "pull-one", "push-all", "push-until-lazy", "push-exactly", "push-at-least",
                "sink-all", "skip-one", "skip-at-least", "skip-at-least-pull-one",
                "count-only", "bool-only", "is-lazy", "iterator",
            };
            Value out = Value::array(); out.isList = true;
            std::string mn = args.empty() ? "" : args[0].toStr();
            if (ms.count(mn)) out.arr()->push_back(Value::str(mn));
            return out;
        }
        if (m == "iterator") return inv; // an Iterator is its own .iterator
        if (m == "WHAT") return Value::typeObj("Iterator");
    }

    // Signature introspection value (from &routine.signature).
    if (inv.t == VT::Hash && inv.hashKind == "Signature") {
        if (m == "raku" || m == "gist" || m == "Str") {
            std::string body = inv.hash()->count("str") ? (*inv.hash())["str"].toStr() : "()";
            // .raku is the signature literal; .gist/.Str are the bare parens
            return Value::str(m == "raku" ? ":" + body : body);
        }
        if (m == "returns" || m == "of") {
            auto it = inv.hash()->find("returns");
            return it != inv.hash()->end() ? it->second : Value::typeObj("Mu");
        }
        if (m == "arity") return inv.hash()->count("arity") ? (*inv.hash())["arity"] : Value::integer(0);
        if (m == "count") return inv.hash()->count("count") ? (*inv.hash())["count"] : Value::integer(0);
        if (m == "params" || m == "parameters") { Value p = inv.hash()->count("params") ? (*inv.hash())["params"] : Value::array(); p.isList = true; return p; }
        if (m == "ACCEPTS") {
            if (args.empty()) return Value::boolean(false);
            // Signature ~~ Signature is a different question from Signature ~~
            // Capture: it asks whether EVERY call that binds the left also binds
            // the right, i.e. whether the left's accepted call-set is contained in
            // the right's. Arity/count windows answer most of it; a slurpy NAMED
            // does not widen the positional window, so `:(*%) ~~ :()` needs its own
            // test (both are [0,0] positionally, but only one takes nameds).
            if (args[0].t == VT::Hash && args[0].hashKind == "Signature" && args[0].hash()) {
                const Value& lhs = args[0];
                auto num = [](const Value& sg, const char* k) {
                    auto it = sg.hash()->find(k);
                    return it == sg.hash()->end() ? 0.0 : it->second.toNum();
                };
                auto str = [](const Value& sg) {
                    auto it = sg.hash()->find("str");
                    return it == sg.hash()->end() ? std::string() : it->second.s;
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
            if (cap.t == VT::Array && cap.arr())
                for (auto& e : *cap.arr()) {
                    if (e.t == VT::Pair) named[e.s] = e.pairVal() ? *e.pairVal() : Value::any();
                    else pos.push_back(e);
                }
            long long arity = inv.hash()->count("arity") ? (*inv.hash())["arity"].toInt() : 0;
            double cnt = inv.hash()->count("count") ? (*inv.hash())["count"].toNum() : 0;
            if ((long long)pos.size() < arity) return Value::boolean(false);
            if (std::isfinite(cnt) && (double)pos.size() > cnt) return Value::boolean(false);
            size_t pi2 = 0;
            bool ok = true;
            if (inv.hash()->count("params") && (*inv.hash())["params"].arr())
                for (auto& pv : *(*inv.hash())["params"].arr()) {
                    if (pv.t != VT::Hash) continue;
                    auto& ph = *pv.hash();
                    bool isNamed = ph.count("named") && ph["named"].truthy();
                    bool isSlurpy = ph.count("slurpy") && ph["slurpy"].truthy();
                    if (isSlurpy) continue;
                    if (isNamed) {
                        bool opt = ph.count("optional") && ph["optional"].truthy();
                        if (!opt) {
                            std::string key;
                            if (ph.count("named_names") && ph["named_names"].arr() && !ph["named_names"].arr()->empty())
                                key = (*ph["named_names"].arr())[0].toStr();
                            else if (ph.count("name") && ph["name"].s.size() > 1)
                                key = ph["name"].s.substr(1);
                            if (!key.empty() && !named.count(key)) { ok = false; break; }
                        }
                        continue;
                    }
                    if (pi2 >= pos.size()) break; // optional tail
                    const Value& a2 = pos[pi2++];
                    if (ph.count("constraints")) {
                        // constraints is now the all(…) junction — an empty one
                        // constrains nothing; a literal eigenstate must match
                        const Value& cjv = ph["constraints"];
                        if (cjv.t == VT::Array && cjv.enumName == "all" && cjv.arr() && !cjv.arr()->empty()) {
                            const Value& cv = (*cjv.arr())[0];
                            bool eq = (a2.isNumeric() && cv.isNumeric()) ? a2.toNum() == cv.toNum()
                                                                         : a2.toStr() == cv.toStr();
                            if (!eq) { ok = false; break; }
                        }
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
        if (inv.elemDefault()) return *inv.elemDefault();
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
             m == "default" || m == "readonly") && inv.hash()->count(m))
            return (*inv.hash())[m];
        // `.type` answers the TYPE OBJECT (Cro's router compares `=:= Str`);
        // the plain string form stays under the "type" key for legacy callers
        if (m == "type" && inv.hash()->count("type-obj")) return (*inv.hash())["type-obj"];
        if (m == "type" && inv.hash()->count(m)) return (*inv.hash())[m];
        if (m == "positional") return Value::boolean(!(*inv.hash())["named"].truthy() && !(*inv.hash())["slurpy"].truthy());
        if (m == "sigil") { const std::string& n = (*inv.hash())["name"].s; return Value::str(n.empty() ? "$" : n.substr(0, 1)); }
        // An accessor of a role a parameter trait mixed in (`$param does
        // Formatted::Named(:$argument)` put the role's attributes into this same
        // map): `$param.argument`. Last, so it can never shadow a real
        // Parameter method — the Attribute meta-object ends the same way.
        {
            auto ai = inv.hash()->find(m);
            if (ai != inv.hash()->end()) return ai->second;
        }
    }
    // a Capture's .list is its POSITIONAL args, .hash/.Map its NAMED (Pair) args
    // Capture.new(:list(...), :hash(...)) — build the \(…)-style capture value
    // Attribute.new(:name<$!x>, :type(Int), :package(Foo)) — build an Attribute
    // meta-object (for .^add_attribute and dynamic class construction).
    if (inv.t == VT::Type && inv.s == "IO::Path::Parts" && m == "new") {
        Value pp = Value::makeHash(); pp.hashKind = "IO::Path::Parts";
        auto pos = [&](size_t i) { return args.size() > i && args[i].t != VT::Pair ? args[i].toStr() : std::string(); };
        (*pp.hash())["volume"] = Value::str(pos(0));
        (*pp.hash())["dirname"] = Value::str(pos(1));
        (*pp.hash())["basename"] = Value::str(pos(2));
        for (auto& a : args) if (a.t == VT::Pair && a.pairVal() &&
            (a.s == "volume" || a.s == "dirname" || a.s == "basename"))
            (*pp.hash())[a.s] = Value::str(a.pairVal()->toStr());
        return pp;
    }
    if (inv.t == VT::Hash && inv.hashKind == "IO::Path::Parts") {
        if (m == "volume" || m == "dirname" || m == "basename") return (*inv.hash())[m];
        // It is Positional as well as Associative: `$parts[0]` is the volume PAIR
        // and `$parts[]` lists all three. The order is the declaration order —
        // volume, dirname, basename — not the map's sorted order, so this cannot
        // just walk the hash.
        if (m == "AT-POS" || m == "list" || m == "List" || m == "elems" || m == "Numeric") {
            static const char* kOrder[3] = {"volume", "dirname", "basename"};
            if (m == "elems" || m == "Numeric") return Value::integer(3);
            auto pairAt = [&](int i) {
                return Value::pair(kOrder[i], (*inv.hash())[kOrder[i]]);
            };
            if (m == "AT-POS") {
                long long i = args.empty() ? 0 : args[0].toInt();
                if (i < 0) i += 3;
                return (i >= 0 && i < 3) ? pairAt((int)i) : Value::any();
            }
            Value out = Value::array(); out.isList = true;
            for (int i = 0; i < 3; i++) out.arr()->push_back(pairAt(i));
            return out;
        }
        if (m == "gist" || m == "raku" || m == "Str") {
            // the parts are STRING LITERALS, so a backslash in a Windows path has to
            // be escaped like any other Str.raku (`"\\a"`, not `"\a"`)
            auto q = [&](const char* k) {
                std::string v = (*inv.hash())[k].toStr(), o = "\"";
                for (char c : v) { if (c == '\\' || c == '"') o += '\\'; o += c; }
                return o + "\"";
            };
            return Value::str("IO::Path::Parts.new(" + q("volume") + "," + q("dirname") + "," + q("basename") + ")");
        }
        if (m == "elems") return Value::integer(3);
    }
    if (inv.t == VT::Type && inv.s == "Attribute" && m == "new") {
        Value at = Value::makeHash(); at.hashKind = "Attribute";
        (*at.hash())["name"] = Value::str(""); (*at.hash())["type"] = Value::typeObj("Mu");
        (*at.hash())["readonly"] = Value::boolean(true); (*at.hash())["has_accessor"] = Value::boolean(false);
        for (auto& a : args) if (a.t == VT::Pair && a.pairVal()) {
            if (a.s == "name") (*at.hash())["name"] = *a.pairVal();
            else if (a.s == "type" || a.s == "of") (*at.hash())["type"] = *a.pairVal();
            else if (a.s == "rw") (*at.hash())["readonly"] = Value::boolean(!a.pairVal()->truthy());
            else if (a.s == "has_accessor") (*at.hash())["has_accessor"] = *a.pairVal();
            else if (a.s == "package") (*at.hash())["package"] = *a.pairVal();
        }
        return at;
    }
    // Junction.new("any", (1, 2)) — the constructor spelling of any(1, 2)
    if (inv.t == VT::Type && inv.s == "Junction" && m == "new") {
        Value j = Value::array(); j.isList = true;
        j.enumName = args.empty() ? "any" : args[0].toStr();
        if (args.size() > 1) for (auto& e : args[1].flatten()) j.arr()->push_back(e);
        return j;
    }
    // Format.new("%s|%s") — a reusable sprintf: calling it formats its arguments
    if (inv.t == VT::Type && (inv.s == "Format" || inv.s == "Formatter") && m == "new") {
        std::string fmt = args.empty() ? "" : args[0].toStr();
        // `.arity` is the number of directives the format consumes
        long long ar = 0;
        for (size_t k = 0; k + 1 < fmt.size(); k++)
            if (fmt[k] == '%') { if (fmt[k + 1] == '%') k++; else ar++; }
        Value code; code.t = VT::Code; code.setCode(std::make_shared<Callable>());
        code.code()->name = "Format";
        code.code()->builtin = [fmt](Interpreter& I, ValueList& a) -> Value {
            ValueList sa; sa.push_back(Value::str(fmt));
            for (auto& x : a) sa.push_back(x);
            return I.callBuiltin("sprintf", sa);
        };
        Value f = Value::makeHash(); f.hashKind = "Format";
        (*f.hash())["fmt"] = Value::str(fmt);
        (*f.hash())["arity"] = Value::integer(ar);
        (*f.hash())["code"] = code;
        return f;
    }
    if (inv.t == VT::Hash && inv.hashKind == "Format") {
        if (m == "Str" || m == "gist" || m == "raku") return (*inv.hash())["fmt"];
        if (m == "arity" || m == "count") return (*inv.hash())["arity"];
        // `.directives` names the conversion letter of each `%…` in order:
        // "%05d%3x:%s" is (d x s). Flags, width and precision are skipped —
        // the directive is the first ALPHABETIC character after the percent.
        if (m == "directives") {
            const std::string fmt = (*inv.hash())["fmt"].toStr();
            Value out = Value::array(); out.isList = true;
            for (size_t k = 0; k + 1 < fmt.size(); k++) {
                if (fmt[k] != '%') continue;
                if (fmt[k + 1] == '%') { k++; continue; } // a literal percent
                size_t j = k + 1;
                while (j < fmt.size() && !ascii::isalpha((unsigned char)fmt[j])) j++;
                if (j < fmt.size()) { out.arr()->push_back(Value::str(std::string(1, fmt[j]))); k = j; }
            }
            return out;
        }
        if (m == "CALL-ME" || m == "()") return methodCall((*inv.hash())["code"], "CALL-ME", args, rwArgs);
    }
    if (inv.t == VT::Type && inv.s == "Capture" && m == "new") {
        Value c = Value::array(); c.hashKind = "Capture"; c.itemized = true;
        for (auto& a : args) {
            if (a.t != VT::Pair || !a.pairVal()) continue;
            if (a.s == "list") {
                const Value& lv = *a.pairVal();
                if (lv.t == VT::Array && lv.arr()) for (auto& e : *lv.arr()) c.arr()->push_back(e);
                else if (lv.t == VT::Range) for (auto& e : lv.flatten()) c.arr()->push_back(e);
                else if (lv.t != VT::Nil && lv.t != VT::Any) c.arr()->push_back(lv);
            }
            else if (a.s == "hash") {
                const Value& hv = *a.pairVal();
                if (hv.t == VT::Hash && hv.hash())
                    for (auto& kv : *hv.hash()) c.arr()->push_back(Value::pair(kv.first, kv.second));
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
        // A Pair is a named part only if it WENT IN as one: `\(:a(1))` is named,
        // `\(('a' => 1))` and a Pair slurped from a positional argument are not.
        if (inv.arr()) for (auto& e : *inv.arr()) {
            if (e.t == VT::Pair && e.namedArg) named[e.s] = e.pairVal() ? *e.pairVal() : Value::any();
            else pos.push_back(e);
        }
        if (m == "list") { Value o = Value::array(); o.isList = true; *o.arr() = pos; return o; }
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
                o.arr()->push_back(Value::integer((long long)i));
                o.arr()->push_back(pos[i]);
            }
            for (auto& kv : named) { o.arr()->push_back(Value::str(kv.first)); o.arr()->push_back(kv.second); }
            return o;
        }
        if (m == "keys" || m == "values" || m == "pairs" || m == "antipairs") {
            Value o = Value::array(); o.isList = true; o.s = "Seq";
            for (size_t i = 0; i < pos.size(); i++) {
                Value k = Value::integer((long long)i);
                if (m == "keys")      o.arr()->push_back(k);
                else if (m == "values") o.arr()->push_back(pos[i]);
                else if (m == "pairs")  { Value p = Value::pair(std::to_string(i), pos[i]);
                                          p.pairKeyM() = std::make_shared<Value>(k); o.arr()->push_back(p); }
                else { Value p = Value::pair(pos[i].toStr(), k); // antipairs: value => key
                       p.pairKeyM() = std::make_shared<Value>(pos[i]); o.arr()->push_back(p); }
            }
            for (auto& kv : named) {
                if (m == "keys")        o.arr()->push_back(Value::str(kv.first));
                else if (m == "values") o.arr()->push_back(kv.second);
                else if (m == "pairs")  { Value p = Value::pair(kv.first, kv.second);
                                          p.namedArg = true; o.arr()->push_back(p); }
                else { Value p = Value::pair(kv.second.toStr(), Value::str(kv.first));
                       p.pairKeyM() = std::make_shared<Value>(kv.second); o.arr()->push_back(p); }
            }
            return o;
        }
        Value o = Value::makeHash(); o.hashKind = "Map";
        for (auto& kv : named) (*o.hash())[kv.first] = kv.second;
        return o;
    }

    // A `but`/`does` mixin over a non-object base: a composed role/class method wins,
    // object-identity/introspection methods stay on the object, and every other
    // method (coercions, arithmetic-ish, base-type methods) delegates to the box.
    if (inv.t == VT::Object && inv.obj() && inv.obj()->hasBoxed && inv.obj()->cls &&
        !inv.obj()->cls->findMethod(m) && !inv.obj()->cls->findAttr(m)) {
        static const std::set<std::string> keepOnObj = {
            // `.can` must see the MIXIN's methods — forwarding it to the boxed
            // value hides them (`(Any but $failure).can('Failure')`)
            "does", "HOW", "WHAT", "WHICH", "defined", "DEFINITE", "isa", "WHERE", "can"};
        if (!keepOnObj.count(m)) return methodCall(inv.obj()->boxed, m, args, rwArgs);
    }

    // Pair.new($key, $value) or Pair.new(:key(...), :value(...)) — same shape as `=>`.
    // IO::Socket::INET.new — a TCP client (:host/:port) or a listener (:listen).
    if (inv.t == VT::Type && inv.s == "IO::Socket::INET" && m == "new") {
        std::string host = "localhost", localhost; long port = 0, localport = 0; bool listen = false;
        long family = -2; // -2 = unspecified
        for (auto& a : args) {
            if (a.t != VT::Pair) continue;
            Value pv = a.pairVal() ? *a.pairVal() : Value::any();
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
            // A NAME is as valid here as a dotted quad — `:localhost<localhost>`
            // is how a test spins up a server on the loopback. Only the client
            // path resolved, so the listener bound to INADDR_NONE and failed,
            // handing back Nil with nothing to say why.
            if (localhost.empty() || localhost == "0.0.0.0") addr.sin_addr.s_addr = INADDR_ANY;
            else resolve(localhost, addr);
            if (::bind(fd, (sockaddr*)&addr, sizeof(addr)) < 0 || ::listen(fd, 128) < 0) { ::close(fd); return Value::nil(); }
        } else {
            addr.sin_port = htons((uint16_t)port);
            resolve(host, addr);
            bool p = gilPark(); int rc = ::connect(fd, (sockaddr*)&addr, sizeof(addr)); gilUnpark(p);
            if (rc < 0) { ::close(fd); return Value::nil(); }
        }
        Value s = Value::makeHash(); s.hashKind = "Socket"; (*s.hash())["fd"] = Value::integer(fd);
        // Keep the name as GIVEN: `.localhost` answers what was asked for, not
        // what it resolved to, which is what Rakudo reports.
        if (listen) { if (!localhost.empty()) (*s.hash())["localhost"] = Value::str(localhost); }
        else (*s.hash())["peerhost"] = Value::str(host);
        return s;
    }
    // CArray[T].new(vals…) — a packed native array (NativeCall). Stored as raw
    // bytes in .s (like Blob); callNative passes a pointer to the bytes.
    // NativeCall CStruct field read: `$s.field` on a native-backed struct reads
    // native memory at the field's computed offset. (Writes go through the
    // assignment path.) Only for a repr('CStruct') class the accessor doesn't
    // otherwise define a real method for.
    if (inv.t == VT::Object && inv.obj() && inv.obj()->cls &&
        (inv.obj()->cls->repr == "CStruct" || inv.obj()->cls->repr == "CPPStruct" ||
         inv.obj()->cls->repr == "CUnion") &&
        inv.obj()->attrs.count("__native_ptr") && !inv.obj()->cls->findMethod(m)) {
        std::string type; long long off = Interpreter::ncFieldOffset(inv.obj()->cls.get(), m, type);
        if (off >= 0) {
            long long base = inv.obj()->attrs["__native_ptr"].toInt();
            long long fa = base + off;
            // scalar field: read directly; pointer/Str/class field: read the 8-byte
            // pointer and box it appropriately.
            // An INLINE member (`HAS Inner $.in`) IS these bytes — hand back a
            // view onto them, so `$o.in.x` reads and `$o.in.x = 1` writes the
            // outer struct's own memory. ncFieldOffset marks it; a plain `has`
            // of the same type stores a POINTER and is dereferenced below.
            if (type.rfind("HAS ", 0) == 0) {
                std::string inner = type.substr(4);
                auto ict = classes_.find(inner);
                if (ict == classes_.end()) ict = classes_.find(resolveClassAlias(inner));
                if (ict != classes_.end()) {
                    Value o; o.t = VT::Object; o.setObj(std::make_shared<ObjectData>());
                    o.obj()->cls = ict->second;
                    o.obj()->attrs["__native_ptr"] = Value::integer(fa);
                    return o;   // borrowed: the OUTER struct owns the memory
                }
                type = inner;
            }
            std::string bt = type.substr(0, type.find('['));
            if (bt == "Str") { long long p; std::memcpy(&p, (void*)(intptr_t)fa, 8); return Value::str(p ? std::string((const char*)(intptr_t)p) : ""); }
            if (bt == "Pointer") { long long p; std::memcpy(&p, (void*)(intptr_t)fa, 8); return ncMakePointer(type, (void*)(intptr_t)p); }
            if (bt == "CArray")  { long long p; std::memcpy(&p, (void*)(intptr_t)fa, 8); return ncMakeLiveCArray(type, (void*)(intptr_t)p); }
            auto cit = classes_.find(type);
            if (cit != classes_.end()) { // nested CStruct/CPointer field → box the pointer
                long long p; std::memcpy(&p, (void*)(intptr_t)fa, 8);
                Value o; o.t = VT::Object; o.setObj(std::make_shared<ObjectData>());
                o.obj()->cls = cit->second; o.obj()->attrs["__native_ptr"] = Value::integer(p);
                return o;
            }
            return Interpreter::ncReadElem(fa, type, 0);
        }
    }
    // NativeCall Pointer[T]: `Pointer.new($addr)` / `Pointer[int32].new(...)`.
    if (inv.t == VT::Type && (inv.s == "Pointer" || inv.s.rfind("Pointer[", 0) == 0) &&
        (m == "new" || m == "allocate")) {
        std::string et = inv.s.rfind("Pointer[", 0) == 0 ? inv.s.substr(8, inv.s.size() - 9) : inv.ofType();
        void* p = args.empty() ? nullptr : (void*)(intptr_t)ncRawAddr(args[0]);
        return ncMakePointer(et.empty() ? "Pointer" : "Pointer[" + et + "]", p);
    }
    if (inv.t == VT::Hash && inv.hashKind == "Pointer") {
        long long addr = inv.hash()->count("addr") ? (*inv.hash())["addr"].toInt() : 0;
        std::string of = inv.hash()->count("of") ? (*inv.hash())["of"].toStr() : "";
        if (m == "Int" || m == "Numeric") return Value::integer(addr);
        // An instantiated Pointer is DEFINED whatever it points at — `Pointer.new`
        // is a real object holding NULL. Emptiness is .Bool's job, and the two
        // were the same test here, which inverted both against Rakudo.
        if (m == "defined") return Value::boolean(true);
        if (m == "Bool" || m == "so") return Value::boolean(addr != 0);
        // Rakudo prints the address in HEX and spells NULL out; `.raku` is the
        // constructor form, not the angle-bracket gist. (The class name stays
        // short here, as every other NativeCall type's `.^name` does.)
        if (m == "gist" || m == "Str") return Value::str(ncPointerText("Pointer", of, addr));
        if (m == "raku") return Value::str("Pointer" + std::string(of.empty() ? "" : "[" + of + "]") +
                                           ".new(" + std::to_string(addr) + ")");
        if (m == "deref") return ncReadElem(addr, of, 0);
        // pointer arithmetic in ELEMENTS, as NativeHelpers::Pointer grafts onto
        // Pointer: `.succ`/`.pred` step one element, `.add($n)` steps n. A
        // `void *` has no element size and dies, as in C.
        if (m == "succ" || m == "pred" || m == "add") {
            int w = of.empty() ? 0 : ncElemSize(of);   // a scalar's width, else pointer-sized
            if (w == 0) throw RakuError{Value::typeObj("X::AdHoc"), "Can't do arithmetic with a void pointer"};
            long long n = m == "add" ? (args.empty() ? 0 : args[0].toInt()) : (m == "succ" ? 1 : -1);
            return ncMakePointer("Pointer[" + of + "]", (void*)(intptr_t)(addr + n * w));
        }
        // an UNPARAMETERISED Pointer is C's `void *`, and that is what Rakudo's
        // `Pointer.of` answers — NativeHelpers::Pointer refuses arithmetic on
        // exactly this test, and "Pointer" made it look like an 8-byte element.
        if (m == "of") return Value::typeObj(of.empty() ? "void" : of);
    }
    // live CArray[T] over native memory (returned by a native call): element read
    if (inv.t == VT::Hash && inv.hashKind == "CArray" && inv.hash()->count("addr")) {
        // a LIVE array (from C, or a nativecast view) has no known length —
        // Rakudo dies the same way; NativeHelpers::Blob's suite asserts it
        if (m == "elems")
            throw RakuError{Value::typeObj("X::AdHoc"),
                            "Don't know how many elements a C array returned from a library has"};
        long long addr = (*inv.hash())["addr"].toInt();
        std::string of = inv.hash()->count("of") ? (*inv.hash())["of"].toStr() : "int64";
        if (m == "AT-POS" || m == "[]") return ncReadElem(addr, of, args.empty() ? 0 : args[0].toInt());
        if (m == "Numeric" || m == "Int") return Value::integer(addr);
        if (m == "defined") return Value::boolean(true);   // as for Pointer above
        if (m == "Bool") return Value::boolean(addr != 0);
        if (m == "of" && !of.empty()) return Value::typeObj(of);
    }
    if (inv.t == VT::Type && (inv.s == "CArray" || inv.s.rfind("CArray[", 0) == 0)) {
        std::string et = inv.s.rfind("CArray[", 0) == 0 ? inv.s.substr(7, inv.s.size() - 8)
                                                        : inv.ofType(); // parameter lives in ofType
        int esz = Interpreter::ncElemSize(et); // pointer element types are 8, not int32
        if (m == "new") {
            std::string bytes;
            ValueList strArgs;   // CArray[Str]: the array owns the strings it points at
            // `CArray[uint8].new($blob)` — a Blob/Buf argument supplies its BYTES as
            // the elements (IO::Socket::Async::SSL hands a PKCS12 file to
            // d2i_PKCS12 this way); it numified to one element before
            ValueList items;
            if (args.size() == 1 && args[0].t == VT::Str &&
                (args[0].hashKind == "Buf" || args[0].hashKind == "Blob"))
                items = args[0].blobList();
            else items = flattenArgs(args);
            for (auto& a : items) {
                if (et == "num32") { float f = (float)a.toNum(); bytes.append((const char*)&f, 4); }
                else if (et == "num64") { double d = a.toNum(); bytes.append((const char*)&d, 8); }
                else if (et == "Str") { bytes.append((size_t)esz, '\0'); strArgs.push_back(a); }
                else {
                    size_t at = bytes.size(); bytes.append((size_t)esz, '\0');
                    Interpreter::ncWriteElem((long long)(intptr_t)(bytes.data() + at), et, 0, a);
                }
            }
            Value c = Value::str(bytes); c.hashKind = "CArray";
            // A CArray IS a native buffer: its address is what C is handed, so the
            // storage must be SHARED by every copy of the value rather than
            // duplicated on the first copy. Without this, `nativecast(Pointer, $c)`
            // could only ever point at whichever copy it happened to be given.
            c.s.promote();
            // the pointers can only be filled once `c` exists to own the strings
            for (size_t k = 0; k < strArgs.size(); k++) {
                long long p = Interpreter::ncOwnStrElem(c, strArgs[k]);
                std::memcpy(c.s.mutInPlace() + k * (size_t)esz, &p, sizeof p);
            }
            c.enumName = et; // remember the element type
            return c;
        }
        // `CArray[int32].of` is the element type. Only the PARAMETERISED one
        // answers it — a bare `CArray.of` is no method under Rakudo either.
        if (m == "of" && !et.empty()) return Value::typeObj(et);
        if (m == "allocate") {
            long long n = args.empty() ? 0 : args[0].toInt();
            if (n < 0) throw RakuError{Value::typeObj("X::AdHoc"), // Rakudo's message for a negative count
                "Unable to allocate an array of " + std::to_string((unsigned long long)n) + " elements"};
            Value c = Value::str(std::string((size_t)n * esz, '\0')); c.hashKind = "CArray";
            c.s.promote();   // shared storage, as `new` above
            c.enumName = et;
            return c;
        }
    }
    if (inv.t == VT::Str && inv.hashKind == "CArray" && m == "of" && !inv.enumName.empty())
        return Value::typeObj(inv.enumName.str());
    if (inv.t == VT::Str && inv.hashKind == "CArray" && m == "elems") {
        const std::string& et = inv.enumName;
        int esz = Interpreter::ncElemSize(et);
        return Value::integer((long long)(inv.s.size() / esz));
    }
    // A locally-built CArray lists its ELEMENTS, decoded by its type — the
    // logical size Rakudo tracks is our byte length over the element width.
    // Digest::SHA256::Native pre-sizes one with `$hash[127] = 0`, lets the C
    // side fill the bytes, and reads the digest back with `.list».chr`;
    // falling through to the generic Str path answered ONE element (itself).
    if (inv.t == VT::Str && inv.hashKind == "CArray" &&
        (m == "list" || m == "values" || m == "List" || m == "Array" || m == "Seq" ||
         // …and every other whole-list question asks THROUGH the element list:
         // `.all ~~ Numeric` is how a module validates a CArray argument, and
         // the generic Str path junction'd the ARRAY as one item (and `Z`/`map`
         // walked its raw BYTES). One decode, then the ordinary list dispatch.
         m == "all" || m == "any" || m == "one" || m == "none" ||
         m == "map" || m == "grep" || m == "first" || m == "sort" || m == "reverse" ||
         m == "sum" || m == "min" || m == "max" || m == "join" || m == "kv" ||
         m == "pairs" || m == "keys" || m == "head" || m == "tail" || m == "flat" ||
         m == "cache" || m == "reduce" || m == "batch" || m == "rotor")) {
        std::string et = inv.enumName.empty() ? std::string("int64") : inv.enumName.str();
        int w = Interpreter::ncElemSize(et);
        long long n = w > 0 ? (long long)(inv.s.size() / (size_t)w) : 0;
        Value out = Value::array(); out.isList = (m != "Array");
        for (long long i = 0; i < n; i++)
            out.arr()->push_back(Interpreter::ncReadElem((long long)(intptr_t)inv.s.data(), et, i));
        if (m == "Seq") out.s = "Seq";
        static const std::set<std::string> direct = {"list", "values", "List", "Array", "Seq"};
        if (direct.count(m)) return out;
        return methodCall(out, m, args, rwArgs); // the rest re-dispatch on the list
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
        // REGISTER-DYNAMIC '$*NAME', { PROCESS::<$NAME> = … } — the initializer
        // a module supplies for a process-wide dynamic it owns. Rakudo defers it
        // to the variable's first lookup; we run it at registration instead,
        // which needs no hook in every lookup path and differs only in WHEN.
        // The one case where that is visible is a value already in place, so an
        // existing binding is left alone rather than overwritten.
        if (m == "REGISTER-DYNAMIC" && args.size() >= 2) {
            std::string name = args[0].toStr();
            if (!name.empty() && global_ && !global_->find(name)) {
                ValueList none;
                callCallable(args[1], none);
            }
            return Value::any();
        }
    }
    // ---- REPL — a read-eval scope as an object -----------------------------
    // Rakudo's REPL is a class, and a whole family of modules drives it
    // DIRECTLY rather than through EVAL, because EVAL forgets: the sandbox in
    // Jupyter::Kernel — copied verbatim into Text::CodeProcessing, and from
    // there into the notebook/weaving dists — wants `my $x = 42` typed in one
    // cell to still be there in the next. The idiom is always the same three
    // lines: `nqp::getcomp('Raku')`, `REPL.new($compiler, {})`, then
    // `.repl-eval($code, $exception, :outer_ctx(…), :interactive(1))`.
    //
    // Rakudo persists the scope by handing back the eval'd code's CONTEXT and
    // taking it again as :outer_ctx next time. rakupp keeps the scope on the
    // REPL object instead (in `ext`), which is the same promise with none of
    // the context plumbing: two REPLs are two independent sessions, and
    // :outer_ctx is accepted and ignored. $*MAIN_CTX therefore stays
    // undefined, which the sandboxes already handle — they only assign
    // $!save_ctx `if $*MAIN_CTX`.
    if (inv.t == VT::Type && inv.s == "REPL" && m == "new") {
        Value r = Value::makeHash();
        r.hashKind = "REPL";
        if (!args.empty()) (*r.hash())["compiler"] = args[0];
        auto sess = std::make_shared<Env>();
        sess->parent = global_;
        // A ROUTINE frame, so a dynamic the session does not declare itself is
        // looked for in the CALLER rather than in global: a weaver wraps each
        // chunk in `my $*OUT = $*OUT but role {…}` to capture its output, and
        // walking through to the global $*OUT would print past the capture.
        sess->routineFrame = true;
        r.extM() = std::static_pointer_cast<void>(sess);
        return r;
    }
    if (inv.t == VT::Hash && inv.hashKind == "REPL") {
        if (m == "repl-eval") {
            auto sess = std::static_pointer_cast<Env>(inv.ext());
            if (!sess) throw RakuError{Value::typeObj("X::AdHoc"), "REPL has no session scope"};
            std::string code = args.empty() ? "" : args[0].toStr();
            Value out;
            bool failed = false; RakuError err;
            {
                // The line runs in the SESSION scope, with the caller's frame
                // still on the dynamic stack: `my` lands in the session (that
                // is the persistence), while $*OUT/$*ERR and every other
                // dynamic resolve exactly where they would have at the call.
                auto saved = tctx_.cur;
                Env* savedState = tctx_.curStateEnv;
                tctx_.dynStack.push_back(saved.get());
                struct Guard {
                    ExecContext& t; std::shared_ptr<Env> cur; Env* st;
                    ~Guard() { t.cur = std::move(cur); t.curStateEnv = st; t.dynStack.pop_back(); }
                } g{tctx_, saved, savedState};
                tctx_.cur = sess;
                tctx_.curStateEnv = sess.get(); // mainline `state` belongs to the session
                try { out = evalString(code, /*mainlinePH=*/true); }
                catch (FeatureNotBuilt&) { throw; } // a SLIM stub: loud, never a reported "line failed"
                catch (RakuError& e) { failed = true; err = e; }
            }
            if (!failed) return out;
            // Rakudo reports a failed line through the second parameter (it is
            // declared raw, so the assignment reaches the caller's variable)
            // and returns Nil. Do the same through the caller's argument
            // expression; with no lvalue to write — a literal, a call whose
            // arguments this dispatch never saw — the exception is thrown
            // instead, which the sandboxes' own CATCH picks up.
            if (rwArgs && rwArgs->size() > 1 && args.size() > 1 && args[1].t != VT::Pair) {
                Value* lv = nullptr;
                try { lv = lvalue((*rwArgs)[1].get()); } catch (RakuError&) {}
                if (lv) { *lv = exceptionFor(err); return Value::nil(); }
            }
            throw err;
        }
        // Whether the last line was cut off mid-expression (`my $x = 42 +`).
        // rakupp answers False: a REPL asks for a continuation line only when
        // it is reading from a human, and this object is being driven by a
        // program, which has no more lines to offer — the same reading Rakudo
        // takes with multi-line input disabled, and the one the weavers want,
        // since an unfinished chunk has to become a visible error.
        if (m == "input-incomplete") return Value::boolean(false);
        if (m == "ctxsave") return Value::nil(); // the context is the object; nothing to save
        if (m == "compiler") return inv.hash()->count("compiler") ? (*inv.hash())["compiler"] : Value::any();
    }
    // The built-in JSON codec, under two names. Rakupp::Internals::JSON is
    // the first-party, durable one — what rakupp's own tooling calls.
    // Rakudo::Internals::JSON is COMPATIBILITY surface: real ecosystem code
    // (zef, OpenSSL, Pakku — see docs/dev/ecosystem/RAKUDO-INTERNALS.md)
    // calls Rakudo's internal class because the language offers no
    // dependency-free JSON, and an implementation that runs real code
    // inherits that. The plan of record: someday the Rakudo spelling warns
    // that the program leans on another implementation's internals —
    // deliberately NOT yet, while the battery still measures those dists.
    if (inv.t == VT::Type && (inv.s == "Rakudo::Internals::JSON" ||
                              inv.s == "Rakupp::Internals::JSON")) {
        if (m == "from-json") {
            std::string j = args.empty() ? "" : args[0].toStr();
            size_t i = 0; Value out;
            JsonCfg cfg;
            if (!jsonParseValue(j, i, out, cfg))
                throw RakuError{Value::typeObj("X::AdHoc"), "Invalid JSON"};
            // trailing content after the top-level value is a parse error in
            // JSON::Fast, and this class advertises its semantics (the
            // internal jsonParseDoc reader stays lenient on purpose — META
            // files are trusted input, user JSON is not)
            jsonSkipWs(j, i, cfg);
            if (i != j.size())
                throw RakuError{Value::typeObj("X::AdHoc"), "Invalid JSON"};
            return out;
        }
        if (m == "to-json") {
            // pretty/spec flags are accepted but ignored (compact output)
            return Value::str(args.empty() ? "null" : jsonEncode(args[0]));
        }
    }
    // Rakupp::Internals::Blob — the engine half of the rakulib
    // NativeHelpers::Blob shadow. The ecosystem dist of that name reads
    // MoarVM's REPR memory layout by design (it scans object headers for a
    // sentinel), which no other engine can satisfy; what its DEPENDENTS
    // actually need is a data pointer into a Blob/CArray and bytes back from
    // a pointer, and those are engine primitives here.
    if (inv.t == VT::Type && inv.s == "Rakupp::Internals::Blob") {
        // Pointers handed to C must outlive the Value COPY they were taken
        // from: a promoted CowStr's body is retained in a ring (sharing the
        // caller's buffer, so C sees the same bytes), an inline small is
        // copied into it. 256 live buffers is far beyond any driver's
        // in-flight set; the ring exists so the process never leaks unboundedly.
        static std::deque<std::shared_ptr<const StrBody>> retained;
        static std::deque<std::string> smalls;
        if (m == "addr") {
            if (args.empty()) return Value::integer(0);
            Value& b = args[0];
            if (b.t == VT::Hash && b.hash() && b.hash()->count("addr"))
                return ncMakePointer("Pointer", (void*)(intptr_t)(*b.hash())["addr"].toInt());
            // a CStruct instance carries its native body's address already —
            // the CStruct shadow's pointer-to reads it here
            if (b.t == VT::Object && b.obj()) {
                auto it = b.obj()->attrs.find("__native_ptr");
                if (it != b.obj()->attrs.end())
                    return ncMakePointer("Pointer", (void*)(intptr_t)it->second.toInt());
            }
            if (b.t != VT::Str) return Value::integer(0);
            const void* p;
            if (auto body = b.s.bodyPtr()) {
                retained.push_back(body);
                if (retained.size() > 256) retained.pop_front();
                p = body->text.data();
            }
            else {
                smalls.push_back(b.s.str());
                if (smalls.size() > 256) smalls.pop_front();
                p = smalls.back().data();
            }
            return ncMakePointer("Pointer", (void*)p);
        }
        if (m == "read") {   // read(addr-or-Pointer, bytes, kind) -> a kinded Blob
            long long addr = args.size() > 0 ? Interpreter::ncRawAddr(args[0]) : 0;
            long long n    = args.size() > 1 ? args[1].toInt() : 0;
            std::string kind = args.size() > 2 ? args[2].toStr() : "Buf";
            if (!addr || n < 0) throw RakuError{Value::typeObj("X::AdHoc"), "Blob.read: null pointer"};
            Value r = Value::str(std::string((const char*)(intptr_t)addr, (size_t)n));
            r.hashKind = kind == "utf8" ? "utf8" : kind;
            identify(r);
            return r;
        }
        if (m == "managed") { // a byte-backed CArray owns its storage; a live one borrows
            return Value::boolean(!args.empty() && args[0].t == VT::Str);
        }
    }
    if (inv.t == VT::Type && inv.s == "Encoding::Registry" && (m == "find" || m == "register")) {
        static std::map<std::string, Value> userEncodings; // fc name → registered Encoding
        auto fc = [](std::string s) { for (auto& c : s) c = (char)ascii::tolower((unsigned char)c); return s; };
        if (m == "register") {
            // pull name + alternative-names off the given Encoding-doing object
            if (!args.empty()) {
                Value& enc = args[0];
                try { userEncodings[fc(methodCall(enc, "name", {}).toStr())] = enc; } catch (...) {}
                try {
                    Value alts = methodCall(enc, "alternative-names", {});
                    if (alts.t == VT::Array && alts.arr())
                        for (auto& a : *alts.arr()) userEncodings[fc(a.toStr())] = enc;
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
        (*e.hash())["name"] = Value::str(name);
        return e;
    }
    if (inv.t == VT::Hash && inv.hashKind == "Encoding") {
        if (m == "name") return (*inv.hash())["name"];
        if (m == "decoder") {
            Value d = Value::makeHash(); d.hashKind = "Decoder";
            (*d.hash())["buffer"] = Value::str("");
            Value seps = Value::array(); seps.arr()->push_back(Value::str("\n"));
            (*d.hash())["seps"] = seps;
            return d;
        }
        if (m == "encoder") { // stateless: our strings are already UTF-8 bytes
            Value e = Value::makeHash(); e.hashKind = "Encoder";
            (*e.hash())["name"] = (*inv.hash())["name"];
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
        Value& buf = (*inv.hash())["buffer"];
        if (m == "add-bytes") { if (!args.empty()) buf.s += args[0].s; return inv; }
        if (m == "set-line-separators") {
            Value seps = Value::array();
            for (auto& a : flattenArgs(args)) seps.arr()->push_back(Value::str(a.toStr()));
            (*inv.hash())["seps"] = seps;
            return inv;
        }
        if (m == "consume-line-chars") {
            bool chomp = false, eof = false;
            for (auto& a : args) if (a.t == VT::Pair) {
                bool on = !a.pairVal() || a.pairVal()->truthy();
                if (a.s == "chomp") chomp = on;
                else if (a.s == "eof") eof = on;
            }
            size_t best = std::string::npos, bestLen = 0;
            if (inv.hash()->count("seps"))
                for (auto& sep : *(*inv.hash())["seps"].arr()) {
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
            (*s.hash())["kind"] = Value::str("async-listen");
            (*s.hash())["host"] = args.size() > 0 ? Value::str(args[0].toStr()) : Value::str("localhost");
            (*s.hash())["port"] = args.size() > 1 ? Value::integer(args[1].toInt()) : Value::integer(0);
            return s;
        }
        if (m == "connect") {
            std::string host = args.size() > 0 ? args[0].toStr() : "localhost";
            int port = args.size() > 1 ? (int)args[1].toInt() : 0;
            int fd = ::socket(AF_INET, SOCK_STREAM, 0);
            auto ps = std::make_shared<PromiseState>();
            Value p = Value::makeHash(); p.hashKind = "Promise"; p.extM() = ps;
            if (fd < 0) {
                ps->done = true; ps->broken = true; ps->causeMsg = "Cannot create socket";
                (*p.hash())["status"] = Value::str("Broken");
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
                (*p.hash())["status"] = Value::str("Broken");
                return p;
            }
            ps->done = true; ps->result = makeAsyncSocket(fd);
            (*p.hash())["status"] = Value::str("Kept");
            (*p.hash())["result"] = ps->result;
            return p;
        }
    }
    // A connected async socket: .Supply taps a read worker; write/print are
    // synchronous sends answered with a kept Promise (Cro awaits them via
    // `whenever $socket.write(…) {}`).
    if (inv.t == VT::Hash && inv.hashKind == "AsyncSocket") {
        int fd = inv.hash()->count("fd") ? (int)(*inv.hash())["fd"].toInt() : -1;
        if (m == "Supply") {
            Value s = Value::makeHash(); s.hashKind = "Supply";
            (*s.hash())["kind"] = Value::str("async-read");
            (*s.hash())["socket"] = inv;
            bool bin = false;
            for (auto& a : args) if (a.t == VT::Pair && a.s == "bin") bin = !a.pairVal() || a.pairVal()->truthy();
            (*s.hash())["bin"] = Value::boolean(bin);
            return s;
        }
        if (m == "write" || m == "print" || m == "put" || m == "say") {
            std::string data = args.empty() ? "" : args[0].toStr();
            if (m == "put" || m == "say") data += "\n";
            auto ps = std::make_shared<PromiseState>();
            Value p = Value::makeHash(); p.hashKind = "Promise"; p.extM() = ps;
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
            if (ok) { ps->result = Value::integer((long long)data.size()); (*p.hash())["status"] = Value::str("Kept"); (*p.hash())["result"] = ps->result; }
            else { ps->broken = true; ps->cause = Value::typeObj("X::IO"); ps->causeMsg = "Socket write failed"; (*p.hash())["status"] = Value::str("Broken"); }
            return p;
        }
        if (m == "close") { if (fd >= 0) { ::shutdown(fd, SHUT_WR); } return Value::boolean(true); }
        if (m == "native-descriptor") return Value::integer(fd);
        if (m == "socket-host" || m == "socket-port" || m == "peer-host" || m == "peer-port") {
            auto it = inv.hash()->find(m);
            return it != inv.hash()->end() ? it->second : Value::any();
        }
    }
    // CompUnit::PrecompilationId.new-from-string($src) — an opaque id; the
    // source spelling is as good an identity as any here.
    if (inv.t == VT::Type && inv.s == "CompUnit::PrecompilationId" && m == "new-from-string") {
        Value o; o.t = VT::Object; o.setObj(std::make_shared<ObjectData>());
        o.obj()->cls = classes_["CompUnit::PrecompilationId"];
        o.obj()->attrs["id"] = Value::str(args.empty() ? "" : args[0].toStr());
        return o;
    }
    // CompUnit::PrecompilationRepository::Default.try-load($dependency) — compile
    // the dependency's source and hand back a handle whose `.unit` holds its
    // `$=pod`. The file is wrapped as a module the way Pod::Load's own string
    // path does it, so a script's mainline never runs twice and its pod is what
    // comes out. A failed compile answers Nil, which is what try-load means.
    if (inv.t == VT::Object && inv.obj() && inv.obj()->cls &&
        inv.obj()->cls->name == "CompUnit::PrecompilationRepository::Default" &&
        (m == "try-load" || m == "load")) {
        std::string src;
        if (!args.empty() && args[0].t == VT::Object && args[0].obj()) {
            auto it = args[0].obj()->attrs.find("src");
            if (it != args[0].obj()->attrs.end()) src = ioFsPath(it->second);
        }
        std::ifstream in(src);
        if (!in) {
            if (m == "load") throwTyped("X::AdHoc", {}, "Cannot load " + src);
            return Value::nil();
        }
        std::ostringstream ss; ss << in.rdbuf();
        std::string text = ss.str();
        // Rakudo refuses to precompile `use lib`: it is a compile-time statement
        // that reshapes the repository chain, so a module loaded THROUGH the
        // precompilation store (which is what this path is) dies with
        // "'use lib' cannot be precompiled and thus cannot be used in a module".
        // Pod::Load's own suite loads a script that opens with `use lib <. ./t>`
        // and asserts that refusal reaches it as X::Pod::Load::SourceErrors. The
        // scan is by statement start, skipping comment lines and pod blocks.
        {
            std::istringstream ls(text);
            std::string ln; int lineNo = 0; bool inPod = false;
            while (std::getline(ls, ln)) {
                lineNo++;
                size_t a = ln.find_first_not_of(" \t");
                if (a == std::string::npos) continue;
                std::string t = ln.substr(a);
                if (t[0] == '=') { // pod directive: `=begin`/`=pod` open, `=end`/`=cut` close
                    if (t.rfind("=begin", 0) == 0 || t.rfind("=pod", 0) == 0) inPod = true;
                    else if (t.rfind("=end", 0) == 0 || t.rfind("=cut", 0) == 0) inPod = false;
                    continue;
                }
                if (inPod || t[0] == '#') continue;
                if (t.rfind("use lib", 0) == 0 && (t.size() == 7 || !(ascii::isalnum((unsigned char)t[7]) || t[7] == '-' || t[7] == ':')))
                    throw RakuError{Value::typeObj("X::AdHoc"),
                        "===SORRY!=== Error while compiling " + src + "\n"
                        "'use lib' cannot be precompiled and thus cannot be used in a module\n"
                        "at " + src + ":" + std::to_string(lineNo)};
            }
        }
        // Precompilation never RUNS a mainline, and `$=pod` is a parse-time
        // product here — so the pod DOM is read straight off the source, and a
        // `unit module` file (Pod::Load's own t/unit.pod6) needs no wrapping
        // that would make it illegal.
        Value pod = Value::array();
        *pod.arr() = parsePod(text);
        Value unit = Value::makeHash();
        (*unit.hash())["$=pod"] = pod;
        Value h; h.t = VT::Object; h.setObj(std::make_shared<ObjectData>());
        h.obj()->cls = classes_["CompUnit::Handle"];
        h.obj()->attrs["unit"] = unit;
        return h;
    }
    // CompUnit::DependencySpecification.new(:short-name<Foo>, …) — a module dependency
    // descriptor. Requires a Str short-name; the version/auth/api matchers default True.
    if (inv.t == VT::Type && inv.s == "CompUnit::DependencySpecification" && m == "new") {
        Value shortName; bool haveSN = false;
        for (auto& a : args) if (a.t == VT::Pair && a.s == "short-name") { shortName = a.pairVal() ? *a.pairVal() : Value::any(); haveSN = true; }
        if (!haveSN || shortName.t != VT::Str)
            throw RakuError{Value::typeObj("X::AdHoc"), "CompUnit::DependencySpecification requires a Str :short-name"};
        Value o = Value::makeHash(); o.hashKind = "DependencySpec";
        (*o.hash())["short-name"] = shortName;
        for (const char* k : {"version-matcher", "auth-matcher", "api-matcher"}) {
            Value v = Value::boolean(true);
            for (auto& a : args) if (a.t == VT::Pair && a.s == k && a.pairVal()) v = *a.pairVal();
            (*o.hash())[k] = v;
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
         ((inv.s == "Blob" || inv.s == "Buf") && !inv.ofType().empty()))) {
        // the width comes from the name (blob32) or the parameter (Blob[uint32])
        const std::string& wsrc = inv.ofType().empty() ? inv.s : inv.ofType();
        int w = wsrc.find("16") != std::string::npos ? 2
              : wsrc.find("32") != std::string::npos ? 4
              : wsrc.find("64") != std::string::npos ? 8 : 1;
        std::string bytes;
        std::function<void(const Value&)> add = [&](const Value& v) {
            if ((v.t == VT::Array || v.t == VT::Range) && !(v.t == VT::Array && !v.arr())) { for (auto& e : v.flatten()) add(e); }
            else if (v.t == VT::Str && (v.hashKind == "Blob" || v.hashKind == "Buf")) bytes += v.s; // copy an existing buffer's bytes
            else {
                // low bits, not a saturated toInt(): a 64-bit word above 2^63-1 is
                // ordinary in a blob64 (SHA-512's constants are full of them)
                unsigned long long x = (v.t == VT::Int && v.big())
                    ? v.big()->toU64Wrap() : (unsigned long long)v.toInt();
                for (int k = 0; k < w; k++) bytes += (char)(unsigned char)((x >> (8 * k)) & 0xFF);
            }
        };
        if (m == "allocate") {
            long long n2 = args.empty() ? 0 : args[0].toInt();
            if (n2 < 0) throw RakuError{Value::typeObj("X::AdHoc"), // Rakudo's message for a negative count
                "Unable to allocate an array of " + std::to_string((unsigned long long)n2) + " elements"};
            bytes.assign((size_t)(n2 * w), '\0');
        }
        else for (auto& a : args) add(a);
        Value b = Value::str(bytes); // buf*/Buf[T] are the mutable spellings
        b.hashKind = (inv.s.rfind("buf", 0) == 0 || inv.s.rfind("Buf", 0) == 0) ? "Buf" : "Blob";
        b.ofTypeM() = "uint" + std::to_string(w * 8); // blob8 IS Blob[uint8] — the [T] always shows
        b.s.promote();   // a native buffer needs stable, shared storage
        if (b.hashKind == "Buf") identify(b);
        return b;
    }
    if (inv.t == VT::Type &&
        (inv.s == "Set" || inv.s == "SetHash" || inv.s == "Bag" || inv.s == "BagHash" ||
         inv.s == "Mix" || inv.s == "MixHash") && m == "new-from-pairs") {
        // pairs contribute key => WEIGHT (unlike .new, where a Pair is an element)
        ValueList items;
        for (auto& a : args) {
            if (a.t == VT::Range && a.rTo() >= 9000000000000000000LL)
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
            if (a.t == VT::Range && a.rTo() >= 9000000000000000000LL)
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
        if (!inv.ofType().empty() && out.hash()) { // Set[Str].new(...) enforces the key type
            for (auto& kv : *out.hash()) {
                Value orig = kv.second.pairKey() ? *kv.second.pairKey() : Value::str(kv.first);
                if (!typeOrSubsetMatches(orig, inv.ofType()))
                    throw RakuError{Value::typeObj("X::TypeCheck::Binding"),
                        "Type check failed for " + inv.s + " key; expected " +
                        inv.ofType() + " but got " + orig.gist()};
            }
            out.ofTypeM() = inv.ofType();
        }
        return out;
    }
    if (inv.t == VT::Hash && inv.hashKind == "StrDistance") {
        auto fld = [&](const char* k) { auto it = inv.hash()->find(k); return it != inv.hash()->end() ? it->second : Value::str(""); };
        if (m == "before" || m == "after") return fld(m.c_str());
        if (m == "Str" || m == "gist") return fld("after"); // "$dist" interpolates the resulting string
        if (m == "Bool") return Value::boolean(fld("before").toStr() != fld("after").toStr());
        if (m == "Rat" || m == "FatRat" || m == "Numeric" || m == "Int" || m == "Num" || m == "chars") {
            // a tr/// result carries the substitution count; .new-built ones numify to .after.chars
            auto di = inv.hash()->find("distance");
            long long c = di != inv.hash()->end() ? di->second.toInt()
                        : methodCall(fld("after"), "chars", ValueList{}).toInt();
            if (m == "Num") return Value::number((double)c);
            if (m == "Int" || m == "Numeric" || m == "chars") return Value::integer(c);
            Value v = Value::rat(BigInt(c), BigInt(1));
            if (m == "FatRat") v.fatRatM() = true;
            return v;
        }
    }
    if (inv.t == VT::Str && inv.hashKind == "Version") {
        if (m == "parts") { // numeric parts as Ints, everything else as Strs
            Value out = Value::array(); out.isList = true;
            const std::string& s = inv.s;
            size_t i = 0;
            while (i < s.size()) {
                unsigned char c = s[i];
                if (ascii::isdigit(c)) { size_t j = i; while (j < s.size() && ascii::isdigit((unsigned char)s[j])) j++;
                    out.arr()->push_back(Value::integer(std::atoll(s.substr(i, j - i).c_str()))); i = j; }
                else if (ascii::isalpha(c)) { size_t j = i; while (j < s.size() && ascii::isalpha((unsigned char)s[j])) j++;
                    out.arr()->push_back(Value::str(s.substr(i, j - i))); i = j; }
                // a '*' part is the STRING "*", as Rakudo stores it — a Whatever
                // here made META6's `$ver.parts[0] eq 'v'` curry into a (truthy)
                // WhateverCode, which shifted v"*" down to an EMPTY version and
                // let Test::META's asterisk check pass vacuously
                else if (c == '*') { out.arr()->push_back(Value::str("*")); i++; }
                else i++;
            }
            return out;
        }
        if (m == "Str") return Value::str(inv.s);
        if (m == "gist") return Value::str("v" + inv.s);
        // .raku round-trips: only a version whose spelling can follow a bare
        // `v` (it starts with a digit) prints as the literal; Rakudo spells
        // Version.new('*') out in full, since `v*` is not valid source
        if (m == "raku")
            return Value::str(!inv.s.empty() && ascii::isdigit((unsigned char)inv.s[0])
                              ? "v" + inv.s : "Version.new('" + inv.s + "')");
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
        // the `:CWD` is the directory this path is relative to; it rides in
        // ofType, which a path value has no other use for. Captured from the
        // current $*CWD by default (Rakudo's model); an explicit :CWD wins.
        p.ofTypeM() = cwdName();
        for (auto& a : args)
            if (a.t == VT::Pair && a.s == "CWD" && a.pairVal()) p.ofTypeM() = a.pairVal()->toStr();
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
            p.ofTypeM() = cwdName();
            return p;
        }
    }
    if (inv.t == VT::Type && inv.s == "CurrentThreadScheduler" && m == "new") {
        Value v = Value::makeHash(); v.hashKind = "Scheduler";
        (*v.hash())["name"] = Value::str("CurrentThreadScheduler");
        (*v.hash())["sync"] = Value::boolean(true);
        return v;
    }
    if (inv.t == VT::Hash && inv.hashKind == "Scheduler") {
        if (m == "cue" && !args.empty()) {
            Value code = args[0];
            double delay = 0, every = 0; long long times = 0;
            bool sawIn = false, sawAt = false, sawTimes = false;
            Value stopF, catchF;
            for (auto& a : args) {
                if (a.t != VT::Pair || !a.pairVal()) continue;
                if (a.s == "in") sawIn = true;
                if (a.s == "at") sawAt = true;
                if (a.s == "times") sawTimes = true;
                if (a.s == "in" || a.s == "at") {
                    double v = a.s == "at" ? instantSecsOf(*a.pairVal()) : a.pairVal()->toNum();
                    if (std::isnan(v)) throw RakuError{Value::typeObj("X::Scheduler::CueInNaNSeconds"),
                        "Cannot pass NaN as a number of seconds to Scheduler.cue"};
                    delay = a.s == "in" ? v : std::max(0.0, v - epochNowSecs()); // :at is absolute, on the `now` clock (whole-second time() lost the fraction)
                }
                else if (a.s == "every") {
                    every = a.pairVal()->toNum();
                    if (std::isnan(every)) throw RakuError{Value::typeObj("X::Scheduler::CueInNaNSeconds"),
                        "Cannot pass NaN as a number of seconds to Scheduler.cue"};
                    if (std::isinf(every)) every = 0; // ±Inf every: run once, immediately
                }
                else if (a.s == "times") times = a.pairVal()->toInt();
                else if (a.s == "stop") stopF = *a.pairVal();
                else if (a.s == "catch") catchF = *a.pairVal();
            }
            if (catchF.t != VT::Code && inv.hash()->count("uncaught_handler"))
                catchF = (*inv.hash())["uncaught_handler"]; // scheduler-level handler
            if (sawIn && sawAt)
                throw RakuError{Value::typeObj("X::Scheduler::Cue"), "Cannot specify both :at and :in"};
            if (every > 0 && sawTimes && stopF.t == VT::Code)
                throw RakuError{Value::typeObj("X::Scheduler::Cue"), "Cannot specify :every, :times and :stop together"};
            if (inv.hash()->count("sync")) { // CurrentThreadScheduler: run inline, now
                bool sawEvery = false;
                for (auto& a : args) if (a.t == VT::Pair && a.s == "every") sawEvery = true;
                if (sawEvery) // no repetition on the inline scheduler, as in Rakudo
                    throw RakuError{Value::typeObj("X::Scheduler::Cue"),
                        "Cannot specify :every in cue on the CurrentThreadScheduler"};
                if (std::isinf(delay) && delay > 0) { // :in(Inf)/:at(Inf): never runs (-Inf runs NOW)
                    Value c = Value::makeHash(); c.hashKind = "Cancellation";
                    c.extM() = std::make_shared<CueState>();
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
                c.extM() = std::make_shared<CueState>();
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
        auto* cs = static_cast<CueState*>(inv.ext().get());
        if (m == "cancel")    { if (cs) cs->cancelled.store(true); return Value::boolean(true); }
        if (m == "cancelled") return Value::boolean(cs && cs->cancelled.load());
    }
    if (inv.t == VT::Type && inv.s == "IO::CatHandle" && m == "new") {
        // minimal CatHandle: a sequence of paths/handles slurped in order
        Value v = Value::makeHash(); v.hashKind = "CatHandle";
        Value files = Value::array();
        for (auto& a : args) {
            if (a.t == VT::Array && a.arr()) for (auto& x : *a.arr()) files.arr()->push_back(x);
            else if (a.t != VT::Pair) files.arr()->push_back(a);
        }
        (*v.hash())["files"] = files;
        return v;
    }
    if (inv.t == VT::Hash && inv.hashKind == "CatHandle") {
        if (m == "slurp") {
            std::string out;
            Value files = (*inv.hash())["files"];
            if (files.arr()) for (auto& f : *files.arr()) {
                ValueList none;
                Value p = Value::str(f.toStr()); p.hashKind = "IO"; // .slurp is IO::Path's
                Value one = methodCall(p, "slurp", none);
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
        // A leading `v` in the STRING is a literal PART, not syntax: Rakudo reads
        // `Version.new("v0.0.1")` as parts ("v", 0, 0, 1) — which looks odd, and
        // is exactly how a module detects the mistake. META6 warns 'prefix "v"
        // seen in version string' off `.parts[0] eq "v"` and strips it; stripping
        // it here instead meant that check never fired. (The `v0.0.1` LITERAL is a
        // different path and still parses as 0.0.1.)
        Value v = Value::str(args.empty() ? "" : args[0].toStr());
        v.hashKind = "Version";
        return v;
    }
    if ((inv.t == VT::Type && inv.s == "Duration" ||
         inv.t == VT::Num && inv.hashKind == "Duration") && m == "new") {
        // Duration is a number of seconds, tagged so .WHAT/.^name answer Duration
        Value d = Value::number(args.empty() ? 0.0 : args[0].toNum());
        d.hashKind = "Duration";
        return identify(d);
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
        o.arr()->push_back(applyArith("-", inv.hashKind == "Instant" ? inv : Value::number(inv.toNum()),
                                    Value::integer(10)));
        o.arr()->push_back(Value::boolean(false));
        return o;
    }
    if (m == "DateTime" && inv.hashKind == "Instant" && inv.isNumeric()) {
        ValueList mk{Value::number(inv.toNum() - 10.0)};  // Instant is POSIX + 10
        if (sixE()) { // 6.e: `.DateTime(:timezone = $*TZ)`, as on Date
            bool given = false;
            for (auto& a2 : args)
                if (a2.t == VT::Pair && a2.s == "timezone" && a2.pairVal()) {
                    mk.push_back(Value::pair("timezone", *a2.pairVal()));
                    given = true;
                }
            if (!given) mk.push_back(Value::pair("timezone", Value::integer(tzOffsetDyn())));
        }
        return methodCall(Value::typeObj("DateTime"), "new", mk);
    }
    // `Date.new-from-daycount($n)` — days since the Modified Julian Date epoch
    if (inv.t == VT::Type && inv.s == "Date" && m == "new-from-daycount" && !args.empty()) {
        // MJD day 0 is 1858-11-17, which is 40587 days before the civil epoch
        long long y, mo, d;
        daysToCivil(args[0].toInt() - 40587, y, mo, d);
        Value v = Value::makeHash(); v.hashKind = "Date";
        (*v.hash())["year"] = Value::integer(y);
        (*v.hash())["month"] = Value::integer(mo);
        (*v.hash())["day"] = Value::integer(d);
        return v;
    }
    if (inv.t == VT::Type && inv.s == "Instant" && m == "from-posix") {
        // TAI = POSIX + the 10 pre-1972 leap seconds (Instant.from-posix(32) is 42)
        // — and it is an Instant, not a bare Num: untagged, its .^name was "Num"
        // and `===` compared it by value.
        Value v = Value::number((args.empty() ? 0.0 : args[0].toNum()) + 10.0);
        v.hashKind = "Instant";
        return identify(v);
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
        for (auto& a2 : args) if (a2.t == VT::Pair && a2.pairVal()) (*h.hash())[a2.s] = *a2.pairVal();
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
            if (an < 0) throw RakuError{Value::typeObj("X::AdHoc"), // Rakudo's message for a negative count
                "Unable to allocate an array of " + std::to_string((unsigned long long)an) + " elements"};
            std::string fill;
            if (args.size() > 1) {
                if (args[1].t == VT::Array || args[1].t == VT::Range)
                    for (auto& e : args[1].flatten()) fill += (char)(unsigned char)(e.toInt() & 0xFF);
                else fill += (char)(unsigned char)(args[1].toInt() & 0xFF);
            }
            if (fill.empty()) fill.push_back('\0');
            std::string bytes;
            for (long long k = 0; k < an; k++) bytes += fill[(size_t)(k % (long long)fill.size())];
            Value b = Value::str(bytes); b.hashKind = inv.s == "Buf" ? "Buf" : "Blob";
            b.s.promote();   // a native buffer needs stable, shared storage
            if (b.hashKind == "Buf") identify(b);
            return b;
        }
        std::string bytes;
        std::function<void(const Value&)> add = [&](const Value& v) {
            if (v.t == VT::Array && v.arr()) { for (auto& e : *v.arr()) add(e); }
            else if (v.t == VT::Range) { for (auto& e : v.flatten()) add(e); } // Buf.new(^10)
            // Buf.new($blob) — the copy candidate — takes the bytes; numifying the
            // Blob here would silently store its element COUNT as the one byte.
            // Rakudo accepts it only as the SOLE argument: anything else goes to
            // `new(*@codes)`, where each element must be a uint8, and a Blob is not.
            else if (v.t == VT::Str && (v.hashKind == "Buf" || v.hashKind == "Blob")) {
                if (args.size() == 1) bytes += v.s;
                else throw RakuError{Value::typeObj("X::TypeCheck"),
                    "Type check failed in initializing an element of " + inv.s +
                    "; expected uint8 but got " + v.hashKind};
            }
            else bytes += (char)(unsigned char)(v.toInt() & 0xFF);
        };
        for (auto& a : args) add(a);
        Value b = Value::str(bytes); b.hashKind = inv.s == "Buf" ? "Buf" : "Blob"; // Buf is mutable
        if (b.hashKind == "Buf") identify(b);
        return b;
    }
    if (inv.t == VT::Type && inv.s == "Pair" && m == "new") {
        Value key = Value::any(), val = Value::any();
        ValueList pos;
        for (auto& x : args) {
            if (x.t == VT::Pair && x.s == "key")        key = x.pairVal() ? *x.pairVal() : Value::any();
            else if (x.t == VT::Pair && x.s == "value") val = x.pairVal() ? *x.pairVal() : Value::any();
            else pos.push_back(x);
        }
        if (!pos.empty())      key = pos[0];
        if (pos.size() >= 2)   val = pos[1];
        Value p = Value::pair(key.toStr(), val);
        if (key.t != VT::Str) p.pairKeyM() = std::make_shared<Value>(key);
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
                (*v.hash())["count"] = Value::integer(n);
                if (parallelMode_) { auto st = std::make_shared<SemaphoreState>(); st->count = n; v.extM() = st; }
            }
            else {
                // Lock::Async keeps its own type identity (so a `Lock::Async $!l`
                // container accepts it) but shares Lock's method implementations
                // under the cooperative GIL.
                v.hashKind = (inv.s == "Lock::Async") ? "Lock::Async" : "Lock";
                // NOT a real mutex under the GIL, and that is a known deviation
                // rather than a decision: the GIL serialises execution but is
                // released at every blocking point, so a protected block that
                // sleeps or does I/O IS interleaved (measured: a `start` block
                // taking the same Lock runs inside the holder's critical section,
                // where Rakudo makes it wait). Giving it a real recursive_mutex
                // here deadlocks IO::Socket::Async::SSL, whose module-level
                // $lib-lock is held across socket I/O by every socket in the
                // process — so the honest state is a no-op lock plus this note,
                // until await-inside-a-lock releases the lock the way Rakudo's
                // thread-pool await does.
                if (parallelMode_) v.extM() = std::make_shared<LockState>();
            }
            return v;
        }
    }
    // Raku.legacy — a CLASS method (type object only, as in Rakudo): does this
    // runtime still speak the classic pre-RakuAST compiler dialect? Rakudo
    // 2026.07 answers True, and so does rakupp — its use/EXPORT machinery is
    // the classic protocol, not RakuAST. The ecosystem `if` dist gates on
    // exactly this to pick which compiler guts to patch (neither of which
    // exists here — rakupp honors `:if` natively instead, see UseStmt).
    if (inv.t == VT::Type && inv.s == "Raku" && m == "legacy")
        return Value::boolean(true);
    // IO::String / Text::IO::String: an in-memory read handle over a string.
    // $*RAKU / $?RAKU and their .compiler — the runtime/implementation introspection object
    if (inv.t == VT::Hash && (inv.hashKind == "Raku" || inv.hashKind == "Compiler")) {
        bool isComp = inv.hashKind == "Compiler";
        // instance spelling mirrors Rakudo: .legacy is Raku:U:-constrained
        if (m == "legacy" && !isComp)
            throw RakuError{Value::typeObj("X::Parameter::InvalidConcreteness"),
                "Invocant of method 'legacy' must be a type object of type 'Raku', "
                "not an object instance of type 'Raku'.  Did you forget a 'multi'?"};
        std::string nm = isComp ? "Raku++" : "Raku";
        // Language revision the program is running under (6.c/6.d/6.e), from any
        // `use v6.*` pragma; the compiler object keeps its own version string.
        std::string langVer = langRev_ == 0 ? "6.c" : (langRev_ == 1 ? "6.d" : "6.e");
        if (m == "compiler") return rakuIntrospection(true);
        if (m == "backend") return Value::str("cpp"); // rakupp's engine is a C++ tree-walking interpreter, not MoarVM
        if (m == "KERNELnames" || m == "DISTROnames" || m == "VMnames") { // known-platform introspection lists
            Value out = Value::array(); out.isList = true;
            out.arr()->push_back(Value::str(m == "KERNELnames" ? platKernelName() : m == "DISTROnames" ? platDistroName() : "moar"));
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
        // Documented for users in docs/faq/differences.md + REFERENCE.md §12.
        // kOracleEra now lives in Interpreter.h — one definition for this site
        // and the $*RAKU builder in Interpreter.cpp, which used to carry its
        // own copy of the literal.
        if (m == "version" || m == "lang-version") { Value v = Value::str(isComp && m == "version" ? kOracleEra : langVer); v.hashKind = "Version"; return v; }
        // The LANGUAGE's authority is the Raku community; the COMPILER's is
        // whoever wrote it, which for this one is a person, not a foundation.
        if (m == "auth" || m == "authority")
            return Value::str(isComp ? "Andrew Shitov" : "The Raku Community");
        if (m == "desc") return Value::str("Raku++ — a C++ Raku interpreter");
        if (m == "signature") { Value b = Value::str("Raku++"); b.hashKind = "Blob"; return b; } // non-empty Blob
        if (m == "id" || m == "release") return Value::str(RAKUPP_VERSION);
        // .build / .build-date identify THIS binary, which .id and .release
        // cannot: every build between two releases reports the same version, so
        // a bug report, a Rakugrid oracle stamp and a benchmark row all pointed
        // at "3.14.0" and nothing narrower. `git describe` gives
        // both an ordering (commits since the tag) and an exact commit.
        // Compiler-only: the LANGUAGE has no build.
        if (isComp && m == "build") return Value::str(rakupp::buildId());
        if (isComp && m == "build-date") return Value::str(rakupp::buildDate());
        if (m == "codename") return Value::str("Raku++");
        // Rakudo: `Raku (6.d)` for the language, `rakudo (2026.08)` for the
        // compiler — name plus the version THAT object reports, not the language
        // revision in both. .Str is the bare name.
        if (m == "Str") return Value::str(nm);
        if (m == "gist")
            return Value::str(nm + " (" + (isComp ? kOracleEra : langVer.c_str()) + ")");
        // .raku is the constructor form, which is what `dd $*RAKU` shows. Ours
        // reports real values where Rakudo's are undefined type objects (its
        // .desc and .signature are Str/Blob), so those fields differ in content
        // while the shape matches.
        if (m == "raku") {
            auto q = [](const std::string& x) { return "\"" + x + "\""; };
            std::string self = std::string(isComp ? "Compiler" : "Raku") + ".new("
                + (isComp ? "" : "compiler => " + [&]{ ValueList none;
                       Value c = rakuIntrospection(true);
                       return methodCall(c, "raku", none).toStr(); }() + ", ")
                + "id => " + q(RAKUPP_VERSION) + ", release => " + q(RAKUPP_VERSION)
                + (isComp ? ", build => " + q(rakupp::buildId())
                          + ", build-date => " + q(rakupp::buildDate()) : "")
                + ", codename => " + q("Raku++")
                + ", name => " + q(nm)
                + ", auth => " + q(isComp ? "Andrew Shitov" : "The Raku Community")
                + ", version => v" + (isComp ? kOracleEra : langVer.c_str())
                + ", signature => Blob"
                + ", desc => " + q("Raku++ — a C++ Raku interpreter") + ")";
            return Value::str(self);
        }
    }
    if (inv.t == VT::Type && (inv.s == "ThreadPoolScheduler" || inv.s == "CurrentThreadScheduler")) {
        if (m == "new") { Value s = Value::makeHash(); s.hashKind = "Scheduler"; (*s.hash())["name"] = Value::str(inv.s); return s; }
    }
    if (inv.t == VT::Type && inv.s == "Channel") {
        if (m == "new") {
            Value c = Value::makeHash(); c.hashKind = "Channel";
            (*c.hash())["queue"] = Value::array();
            (*c.hash())["closed"] = Value::boolean(false);
            auto ps = std::make_shared<PromiseState>();          // the `.closed` Promise
            c.extM() = ps;
            Value cp = Value::makeHash(); cp.hashKind = "Promise"; cp.extM() = ps;
            (*cp.hash())["status"] = Value::str("Planned");
            (*c.hash())["closedPromise"] = cp;
            return c;
        }
    }
    // Channel — a thread-safe queue. Under the GIL send/receive are simple deque
    // ops; `.closed` is a Promise kept once the channel is closed AND drained.
    if (inv.t == VT::Hash && inv.hashKind == "Channel") {
        // "a thread-safe queue" is now true OUTSIDE the GIL too: every touch of
        // the queue and the closed/failCause flags happens under the channel's
        // stripe (keyed on the hash — the same pool cas and atomic-* use). The
        // GIL made this free before; in parallel mode concurrent sends corrupted
        // the vector — hangs, lost items, and a spurious "Promise broken" were
        // all one bug. Blocking waits happen OUTSIDE the stripe, or no producer
        // could ever get in to send.
        std::recursive_mutex& chm = atomicStripe(inv.hash());
        std::shared_ptr<ValueList> qp;
        { std::lock_guard<std::recursive_mutex> lk(chm); qp = (*inv.hash())["queue"].arrS(); }
        auto& q = *qp;
        auto isClosed = [&]() { return (*inv.hash())["closed"].b; };
        auto keepClosedIfDrained = [&]() {
            if (isClosed() && q.empty() && inv.ext()) {
                auto ps = std::static_pointer_cast<PromiseState>(inv.ext());
                bool failed = inv.hash()->count("failCause") > 0;
                std::lock_guard<std::mutex> lk(ps->m);
                if (!ps->done) {
                    if (failed) { ps->broken = true; ps->cause = (*inv.hash())["failCause"]; ps->causeMsg = (*inv.hash())["failCause"].toStr(); }
                    else ps->result = Value::boolean(true);
                    ps->done = true;
                }
                ps->cv.notify_all();
                if (inv.hash()->count("closedPromise")) (*(*inv.hash())["closedPromise"].hash())["status"] = Value::str(failed ? "Broken" : "Kept");
            }
        };
        if (m == "send") {
            Value v = args.empty() ? Value::any() : args[0];
            std::lock_guard<std::recursive_mutex> lk(chm);
            if (isClosed()) throw RakuError{Value::typeObj("X::Channel::SendOnClosed"), "Cannot send a message on a closed channel"};
            q.push_back(v); return v;
        }
        if (m == "poll") {
            std::lock_guard<std::recursive_mutex> lk(chm);
            if (q.empty()) { keepClosedIfDrained(); return Value::nil(); }
            Value v = q.front(); q.erase(q.begin()); keepClosedIfDrained(); return v;
        }
        if (m == "receive") {
            // `.receive` BLOCKS until an item arrives (or the channel closes) —
            // under the cooperative GIL that means handing off to the workers that
            // could send. With no async engaged nothing ever could, so answer Nil
            // rather than deadlock; likewise once every worker has finished.
            if (q.empty() && !isClosed() && (gilHeld_ || parallelMode_)) {
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
                for (;;) {
                    { std::lock_guard<std::recursive_mutex> lk(chm);
                      if (!q.empty() || isClosed()) break; }
                    if (liveWorkers_.load() <= 0 && cuedLoads_.load() <= 0) break; // nobody left to send
                    if (gilHeld_) yieldToWorkerFor(0.02);
                    else std::this_thread::sleep_for(std::chrono::milliseconds(2)); // parallel mode: real wait
                }
            }
            std::lock_guard<std::recursive_mutex> lk(chm);
            if (q.empty()) {
                if (isClosed()) {
                    if (inv.hash()->count("failCause")) throw RakuError{(*inv.hash())["failCause"], "Channel failed"};
                    throw RakuError{Value::typeObj("X::Channel::ReceiveOnClosed"), "Cannot receive a message on a closed channel"};
                }
                return Value::nil(); // nothing running that could ever send
            }
            Value v = q.front(); q.erase(q.begin()); keepClosedIfDrained(); return v;
        }
        if (m == "close") { std::lock_guard<std::recursive_mutex> lk(chm); (*inv.hash())["closed"] = Value::boolean(true); keepClosedIfDrained(); return Value::boolean(true); }
        if (m == "fail") {
            std::lock_guard<std::recursive_mutex> lk(chm);
            (*inv.hash())["closed"] = Value::boolean(true);
            Value cause = args.empty() ? Value::str("Died") : args[0];
            if (cause.t != VT::Object) { // wrap a plain cause in X::AdHoc (like die/break)
                auto xit = classes_.find("X::AdHoc");
                if (xit != classes_.end()) { Value ex; ex.t = VT::Object; ex.setObj(std::make_shared<ObjectData>()); ex.obj()->cls = xit->second; ex.obj()->attrs["message"] = Value::str(cause.toStr()); cause = ex; }
            }
            (*inv.hash())["failCause"] = cause;
            // once drained, the .closed Promise breaks with the failure cause
            if (q.empty() && inv.ext()) {
                auto ps = std::static_pointer_cast<PromiseState>(inv.ext());
                std::lock_guard<std::mutex> lk(ps->m);
                if (!ps->done) { ps->broken = true; ps->cause = cause; ps->causeMsg = cause.toStr(); ps->done = true; }
                ps->cv.notify_all();
                if (inv.hash()->count("closedPromise")) (*(*inv.hash())["closedPromise"].hash())["status"] = Value::str("Broken");
            }
            return Value::boolean(true);
        }
        if (m == "closed") { std::lock_guard<std::recursive_mutex> lk(chm); return (*inv.hash())["closedPromise"]; }
        // `.list`/`.Seq` CONSUME a closed channel: they yield the queued values
        // and drain it (so the .closed Promise then keeps). `.Supply` snapshots
        // without draining (a Supply is a re-tappable stream).
        if (m == "list" || m == "Seq") {
            // Rakudo: `.list` DRAINS the channel — it blocks until close and
            // yields everything sent. Snapshotting only the current queue
            // raced the producer once parallel became the default (the GIL's
            // spawn-time handoff used to let the producer finish first).
            // Same wait discipline as `.receive`: patience while somebody
            // could still send, never a deadlock once nobody can.
            Value o = Value::array(); o.isList = true;
            for (;;) {
                bool done = false;
                {   std::lock_guard<std::recursive_mutex> lk(chm);
                    for (auto& x : q) o.arr()->push_back(x);
                    q.clear();
                    done = isClosed();
                }
                if (done) break;
                if (liveWorkers_.load() <= 0 && cuedLoads_.load() <= 0) {
                    // nobody left to send — but the last sender may have sent and
                    // closed BETWEEN the drain above and this check (it decrements
                    // the worker count only after its close lands). One final
                    // locked sweep, or that tail is silently dropped: t/run's
                    // parallel example lost "100 121 144" exactly here once,
                    // under machine load. `.receive` re-checks under its lock
                    // after the same break; `.list` returned without looking.
                    std::lock_guard<std::recursive_mutex> lk(chm);
                    for (auto& x : q) o.arr()->push_back(x);
                    q.clear();
                    break;
                }
                if (gilHeld_) yieldToWorkerFor(0.02);
                else std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            std::lock_guard<std::recursive_mutex> lk(chm);
            keepClosedIfDrained();
            return o;
        }
        if (m == "Supply") {
            // A live channel (one carrying its source supplier) re-exposes a live
            // Supply on the SAME supplier, so `$s.Supply.Channel.Supply` forwards
            // emits (IO::Socket::Async::SSL's read path). A plain (from-list)
            // channel yields its queued snapshot as a list-backed Supply.
            std::lock_guard<std::recursive_mutex> lk(chm);
            if (inv.hash()->count("supplier")) {
                Value s = Value::makeHash(); s.hashKind = "Supply";
                (*s.hash())["supplier"] = (*inv.hash())["supplier"];
                return s;
            }
            Value o = Value::array(); *o.arr() = q; o.isList = true; return o;
        }
        if (m == "elems") { std::lock_guard<std::recursive_mutex> lk(chm); return Value::integer((long long)q.size()); }
    }
    // Thread — under the GIL a Thread.start runs its block eagerly, but we bump
    // threadDepth_ so `is-initial-thread` correctly reads False inside the block.
    if (inv.t == VT::Type && inv.s == "Thread") {
        if (m == "is-initial-thread") return Value::boolean(threadDepth_ == 0 && !t_isWorker);
        if (m == "start" || m == "run") { // a REAL thread, via the promise machinery
            Value code; for (auto& x : args) if (x.t == VT::Code) code = x;
            Value t = Value::makeHash(); t.hashKind = "Thread";
            for (auto& x : args) if (x.t == VT::Pair && x.s == "name" && x.pairVal()) (*t.hash())["name"] = *x.pairVal();
            static std::atomic<long long> nextThreadId{2}; // 1 = the initial thread
            (*t.hash())["id"] = Value::integer(nextThreadId++);
            (*t.hash())["initial"] = Value::boolean(false);
            if (code.t == VT::Code) {
                t.extM() = std::static_pointer_cast<void>(
                    std::static_pointer_cast<PromiseState>(spawnPromise(code, t).ext()));
                yieldToWorker();
            }
            return t;
        }
        if (m == "new") {
            Value t = Value::makeHash(); t.hashKind = "Thread";
            for (auto& x : args) { if (x.t == VT::Code) (*t.hash())["code"] = x; else if (x.t == VT::Pair && x.pairVal()) (*t.hash())[x.s] = *x.pairVal(); }
            return t;
        }
    }
    if (inv.t == VT::Hash && inv.hashKind == "Thread") {
        if (m == "is-initial-thread") return Value::boolean(inv.hash()->count("initial") ? (*inv.hash())["initial"].b : (threadDepth_ == 0));
        if (m == "finish" || m == "join") {
            if (inv.ext()) awaitPromise(std::static_pointer_cast<PromiseState>(inv.ext()));
            return inv;
        }
        if (m == "run" || m == "start") { // Thread.new(:code).run — start it now
            if (inv.hash()->count("code") && !inv.ext()) {
                Value t = inv;
                t.extM() = std::static_pointer_cast<void>(
                    std::static_pointer_cast<PromiseState>(spawnPromise((*inv.hash())["code"]).ext()));
                yieldToWorker();
                return t;
            }
            return inv;
        }
        if (m == "id") return inv.hash()->count("id") ? (*inv.hash())["id"] : Value::integer(1);
        if (m == "name") return inv.hash()->count("name") ? (*inv.hash())["name"] : Value::str("<anon>");
        if (m == "Str" || m == "gist") { // Thread<ID>(NAME)
            std::string id = inv.hash()->count("id") ? (*inv.hash())["id"].toStr() : "1";
            std::string nm = inv.hash()->count("name") ? (*inv.hash())["name"].toStr() : "<anon>";
            return Value::str("Thread<" + id + ">(" + nm + ")");
        }
    }
    if (inv.t == VT::Type && (inv.s == "Supplier" || inv.s == "Supplier::Preserving")) {
        if (m == "new" || m == "preserving") {
            Value s = Value::makeHash(); s.hashKind = "Supplier"; (*s.hash())["taps"] = Value::array();
            // Supplier::Preserving buffers every emit and replays the buffer to any
            // tap that connects later (Cro emits the request into $!in before the
            // async connect pipeline taps it).
            if (inv.s == "Supplier::Preserving" || m == "preserving") {
                (*s.hash())["preserving"] = Value::boolean(true);
                (*s.hash())["buffer"] = Value::array();
            }
            return s;
        }
    }
    // Supplier: a live push source. Its Supply shares the taps list; emit/done fan out to them.
    if (inv.t == VT::Hash && inv.hashKind == "Supplier") {
        if (m == "Supply") { Value s = Value::makeHash(); s.hashKind = "Supply"; (*s.hash())["supplier"] = inv; return s; } // live (no "values")
        if (m == "emit") { Value v = args.empty() ? Value::any() : args[0];
            // Rakudo's contract: a Supplier's emissions are SERIALIZED per tap.
            // Under the GIL that held by accident; in parallel mode concurrent
            // emits ran the tap blocks simultaneously and interleaved into the
            // taps vector — the stress suite measured 1,694 of 2,000 arrivals.
            // The supplier's stripe serializes emit/done/quit AND registration.
            std::lock_guard<std::recursive_mutex> emitLk(supplierMutex(inv.hash()));
            if (inv.hash()->count("preserving") && (*inv.hash())["preserving"].truthy() && inv.hash()->count("buffer"))
                (*inv.hash())["buffer"].arr()->push_back(v); // replayed to late taps
            if (inv.hash()->count("taps")) for (auto& t : *(*inv.hash())["taps"].arr()) {
                if (t.t != VT::Hash) continue;
                if (t.hash()->count("closed") && (*t.hash())["closed"].truthy()) continue; // head/first already finished
                bool complete = false;
                ValueList outs = applyTapChain(t, v, complete);
                if (t.hash()->count("emit") && (*t.hash())["emit"].t == VT::Code) {
                    // push the tap's react ctx so `done` inside the whenever block
                    // closes the enclosing react (the block runs here, on whatever
                    // thread emitted — reactStack_ is thread-local, so it wasn't set)
                    std::shared_ptr<ReactCtx> rctx = t.ext() ? std::static_pointer_cast<ReactCtx>(t.ext()) : nullptr;
                    for (auto& o : outs) {
                        ValueList one{o};
                        if (rctx) reactStack_.push_back(rctx);
                        // `next` in a whenever skips this value; `last` closes the tap
                        try { callCallable((*t.hash())["emit"], one); if (rctx) reactStack_.pop_back(); }
                        catch (NextEx&) { if (rctx) reactStack_.pop_back(); }
                        catch (LastEx&) { if (rctx) reactStack_.pop_back(); (*t.hash())["closed"] = Value::boolean(true); complete = true; break; }
                        catch (DoneEx&) { if (rctx) reactStack_.pop_back(); (*t.hash())["closed"] = Value::boolean(true); complete = true; break; }
                        catch (RakuError& e) {
                            // an exception in the tap's block QUITS the tap — the
                            // quit handler gets the exception and the EMITTER is
                            // not unwound (Cro's frame parser dies per malformed
                            // frame; the test taps `quit => { when X::… }`)
                            if (rctx) reactStack_.pop_back();
                            (*t.hash())["closed"] = Value::boolean(true);
                            // …but inside a REACT that rule is inverted: a die in a
                            // whenever BODY kills the whole react and propagates,
                            // and QUIT does NOT see it — QUIT is for the SOURCE's
                            // own quit (issue #18). The interval path already did
                            // this; a Supplier-fed whenever handed the body's death
                            // to QUIT and carried on, so `react { whenever
                            // $s.Supply { die } }` printed the QUIT message and
                            // exited 0 where Rakudo dies.
                            if (rctx) {
                                std::lock_guard<std::mutex> lk(rctx->m);
                                if (!rctx->quitFlag) {
                                    rctx->quitFlag = true;
                                    rctx->quitErr = e.payload.t == VT::Nil ? Value::str(e.message) : e.payload;
                                }
                                rctx->closed = true;
                                if (rctx->liveSources > 0) rctx->liveSources--;
                                rctx->cv.notify_all();
                                break;
                            }
                            if (t.hash()->count("quit") && (*t.hash())["quit"].t == VT::Code) {
                                ValueList one2{exceptionFor(e)};
                                try { callCallable((*t.hash())["quit"], one2); } catch (...) {}
                            }
                            if (t.ext()) { auto ctx2 = std::static_pointer_cast<ReactCtx>(t.ext()); std::lock_guard<std::mutex> lk(ctx2->m); if (ctx2->liveSources > 0) ctx2->liveSources--; ctx2->cv.notify_all(); }
                            break;
                        }
                        catch (...) { if (rctx) reactStack_.pop_back(); throw; }
                        if (rctx && rctx->closed) break; // `done` inside the block ended the react
                    }
                }
                if (complete) { // head(n)/first done → fire the tap's done and release a react source
                    (*t.hash())["closed"] = Value::boolean(true);
                    if (t.hash()->count("done") && (*t.hash())["done"].t == VT::Code) { ValueList none; callCallable((*t.hash())["done"], none); }
                    if (t.ext()) { auto ctx = std::static_pointer_cast<ReactCtx>(t.ext()); std::lock_guard<std::mutex> lk(ctx->m); if (ctx->liveSources > 0) ctx->liveSources--; ctx->cv.notify_all(); }
                }
            }
            return Value::boolean(true); }
        if (m == "done") {
            std::lock_guard<std::recursive_mutex> doneLk(supplierMutex(inv.hash()));
            // Remember the done state so a tap that registers LATER (an eager
            // `start { $s.emit(…); $s.done }` that ran before the react tapped it)
            // is closed immediately instead of leaving its react source live forever.
            (*inv.hash())["done_state"] = Value::boolean(true);
            if (inv.hash()->count("taps")) for (auto& t : *(*inv.hash())["taps"].arr()) {
                if (t.t == VT::Hash && t.hash()->count("closed") && (*t.hash())["closed"].truthy()) continue; // already done (head/first)
                // a `.lines`/`.words` chain may still hold an unterminated last piece:
                // the stream ending is what completes it, so deliver it before `done`
                if (t.t == VT::Hash && t.hash()->count("chain") &&
                    t.hash()->count("emit") && (*t.hash())["emit"].t == VT::Code) {
                    bool complete = false;
                    ValueList tail = applyTapChain(t, Value::any(), complete, /*flush=*/true);
                    for (auto& o : tail) {
                        ValueList one{o};
                        try { callCallable((*t.hash())["emit"], one); }
                        catch (NextEx&) {} catch (LastEx&) { break; } catch (DoneEx&) { break; }
                    }
                }
                if (t.t == VT::Hash && t.hash()->count("done") && (*t.hash())["done"].t == VT::Code) { ValueList none; callCallable((*t.hash())["done"], none); }
                if (t.ext()) { auto ctx = std::static_pointer_cast<ReactCtx>(t.ext()); std::lock_guard<std::mutex> lk(ctx->m); if (ctx->liveSources > 0) ctx->liveSources--; ctx->cv.notify_all(); }
            }
            return Value::boolean(true); }
        if (m == "quit") {
            std::lock_guard<std::recursive_mutex> quitLk(supplierMutex(inv.hash()));
            // Recorded for the same reason as done_state: a Supply.wait on a
            // supplier that quits must return, not block forever.
            (*inv.hash())["quit_state"] = Value::boolean(true);
            Value ex = args.empty() ? Value::any() : args[0];
            if (inv.hash()->count("taps")) for (auto& t : *(*inv.hash())["taps"].arr()) { if (t.t == VT::Hash && t.hash()->count("quit") && (*t.hash())["quit"].t == VT::Code) { ValueList one{ex}; callCallable((*t.hash())["quit"], one); } }
            return Value::boolean(true); }
        if (m == "Seq" || m == "list") { Value o = Value::array(); o.isList = true; return o; }
    }
    // Supply as a type object: constructors that build an eager, list-backed Supply.
    if (inv.t == VT::Type && inv.s == "Supply") {
        // Supply's own INSTANCE methods need a Supply, not the type object.
        // Without this they fell through to the generic list handlers and
        // answered a Seq, so `dies-ok { Supply.reverse }` — the first assertion
        // of five S17-supply files — did not die. Only the methods Supply
        // actually defines: `Supply.sort` is Any.sort on a type object and must
        // keep working, as it does in Rakudo.
        static const std::set<std::string> kSupplyInstance = {
            "reverse", "words", "lines", "rotor", "produce", "reduce", "batch",
            "head", "tail", "skip", "squish", "unique", "elems", "min", "max",
            "minmax", "sum", "collate", "repeated", "roll", "pick",
        };
        if (kSupplyInstance.count(m.s))
            throw RakuError{Value::typeObj("X::Parameter::InvalidConcreteness"),
                            "Invocant of method '" + m.s + "' must be an object instance of type "
                            "'Supply', not a type object"};
        auto mkSupply = [&](ValueList vals) { Value s = Value::makeHash(); s.hashKind = "Supply"; Value v = Value::array(); *v.arr() = std::move(vals); (*s.hash())["values"] = v; return s; };
        if (m == "from-list") {
            // +@values single-arg rule: ONE array arg (from-list(@source)) emits its
            // elements; with several args each stays whole (from-list([1,2],[3,4,5])
            // is two list values). A Range always expands.
            ValueList out;
            if (args.size() == 1 && args[0].t == VT::Array && args[0].arr() && !args[0].itemized) {
                for (auto& x : *args[0].arr()) out.push_back(x);
            } else for (auto& a : args) {
                if (a.t == VT::Range) { for (auto& x : a.flatten()) out.push_back(x); }
                else if (a.t == VT::Array && a.isList && a.arr()) { for (auto& x : *a.arr()) out.push_back(x); }
                else out.push_back(a);
            }
            return mkSupply(out);
        }
        if (m == "list") { Value o = Value::array(); o.isList = true; o.arr()->push_back(inv); return o; } // Supply type → (Supply,)
        if (m == "merge") { ValueList all; for (auto& a : flattenArgs(args)) { if (a.t == VT::Hash && a.hashKind == "Supply" && a.hash()->count("values")) for (auto& x : *(*a.hash())["values"].arr()) all.push_back(x); } return mkSupply(all); }
        if (m == "zip") {
            // zip N list-backed supplies element-wise (stopping at the shortest); an
            // optional :with(&op) combines each row instead of emitting a tuple List.
            ValueList streams; Value withOp;
            for (auto& a : args) {
                if (a.t == VT::Pair && (a.s == "with" || a.s == "as") && a.pairVal()) { withOp = *a.pairVal(); continue; }
                if (!(a.t == VT::Hash && a.hashKind == "Supply" && a.hash()->count("values")))
                    throw RakuError{Value::typeObj("X::Supply::Combinator"), "zip requires Supply arguments"};
                streams.push_back(a);
            }
            if (streams.size() == 1) return streams[0]; // zipping one supply is a === noop
            size_t n = SIZE_MAX;
            for (auto& s : streams) n = std::min(n, (*s.hash())["values"].arr()->size());
            if (streams.empty()) n = 0;
            ValueList out;
            for (size_t i = 0; i < n; i++) {
                ValueList row; for (auto& s : streams) row.push_back((*(*s.hash())["values"].arr())[i]);
                if (withOp.t == VT::Code) out.push_back(callCallable(withOp, row));
                else { Value tup = Value::array(); tup.isList = true; *tup.arr() = std::move(row); out.push_back(tup); }
            }
            return mkSupply(out);
        }
        if (m == "interval") { // real ticker: 0 after $delay (default: immediately), then one per $interval
            Value s = Value::makeHash(); s.hashKind = "Supply";
            (*s.hash())["kind"] = Value::str("interval");
            (*s.hash())["interval"] = args.empty() ? Value::number(1) : args[0];
            double delay = 0;
            for (size_t i = 1; i < args.size(); i++)
                if (args[i].t != VT::Pair) { delay = args[i].toNum(); break; }
            (*s.hash())["delay"] = Value::number(delay);
            return s;
        }
        if (m == "empty") return mkSupply({});
    }
    if (inv.t == VT::Type && inv.s == "Promise") {
        Value p = Value::makeHash(); p.hashKind = "Promise";
        if (m == "in" || m == "at") {
            // `.in($n)` is relative, `.at($instant)` absolute — normalize to the
            // absolute fire time (epoch seconds, the `now` clock) at creation, so
            // every consumer waits exactly the remainder (see timerRemainingSecs).
            (*p.hash())["kind"] = Value::str("timer");
            double arg = args.empty() ? 0 : (m == "at" ? instantSecsOf(args[0]) : args[0].toNum());
            (*p.hash())["seconds"] = args.empty() ? Value::number(0) : args[0];
            (*p.hash())["fires_at"] = Value::number(m == "in" ? epochNowSecs() + arg : arg);
            (*p.hash())["status"] = Value::str("Planned");
            return p;
        }
        if (m == "anyof" || m == "allof") {
            (*p.hash())["kind"] = Value::str(m); Value ps = Value::array();
            for (auto& x : flattenArgs(args)) {
                if (!(x.t == VT::Hash && x.hashKind == "Promise"))
                    throw RakuError{Value::typeObj("X::Promise::Combinator"),
                        "Can only create a Promise combinator out of defined Promises"};
                ps.arr()->push_back(x);
            }
            (*p.hash())["promises"] = ps; (*p.hash())["status"] = Value::str("Planned"); return p;
        }
        if (m == "new") {
            // A manual (vow-controlled) promise: starts Planned, later kept/broken.
            auto st = std::make_shared<PromiseState>();
            p.extM() = st;
            (*p.hash())["status"] = Value::str("Planned");
            return p;
        }
        if (m == "start") {
            // Promise.start(&code): run on a worker + cooperative yield, like `start`.
            Value code; for (auto& x : args) if (x.t == VT::Code) code = x;
            if (code.t != VT::Code) {
                auto st = std::make_shared<PromiseState>(); st->done = true; st->result = args.empty() ? Value::any() : args[0];
                p.extM() = st; (*p.hash())["result"] = st->result; (*p.hash())["status"] = Value::str("Kept");
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
            p.extM() = st;
            (*p.hash())["result"] = v;
            (*p.hash())["status"] = Value::str(m == "broken" ? "Broken" : "Kept");
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
                if (x.t == VT::Array && x.arr() && !x.itemized)
                    for (auto& e : *x.arr()) argv.arr()->push_back(e);
                else argv.arr()->push_back(x);
            }
            (*p.hash())["argv"] = argv; (*p.hash())["taps"] = Value::array();
            return p;
        }
    }
    if (inv.t == VT::Hash && inv.hashKind == "Proc::Async") {
        if (m == "stdout" || m == "stderr" || m == "Supply") {
            Value s = Value::makeHash(); s.hashKind = "Supply";
            (*s.hash())["proc"] = inv; (*s.hash())["stream"] = Value::str(m);
            // `.stdout(:bin)` asks for the raw bytes: its taps keep Blob-kinded
            // chunks (zef Buf.appends them); a plain stream emits decoded Strs
            for (auto& a : args)
                if (a.t == VT::Pair && a.s == "bin" && (!a.pairVal() || a.pairVal()->truthy()))
                    (*s.hash())["bin"] = Value::boolean(true);
            return s;
        }
        if (m == "start") {
            // Rakudo throws on a second .start; ours must too, or the realize
            // fallback would spawn the command a second time.
            if (inv.hash()->count("pid") || inv.hash()->count("spawn-token"))
                throw RakuError{Value::typeObj("X::Proc::Async::AlreadyStarted"),
                    "Process has already been started"};
            Value pr = Value::makeHash(); pr.hashKind = "Promise";
            (*pr.hash())["kind"] = Value::str("proc"); (*pr.hash())["proc"] = inv;
            (*pr.hash())["status"] = Value::str("Planned");
            // record :cwd so the run happens in the right directory — zef's tar
            // extract runs `tar -zxvf <basename>` with :cwd(archive dir)
            std::string cwd;
            for (auto& a : args)
                if (a.t == VT::Pair && a.s == "cwd" && a.pairVal()) {
                    cwd = a.pairVal()->toStr();
                    (*pr.hash())["cwd"] = Value::str(cwd);
                }
            // The process spawns HERE — Rakudo's .start means "running from this
            // moment", not "run when awaited" (#29: a fire-and-forget daemon must
            // exist without an await, and outlive us). The promise stays Planned;
            // realizing it (await / whenever / anyof) drains the pipes and reaps.
            std::vector<std::string> argvv;
            if (inv.hash()->count("argv") && (*inv.hash())["argv"].arr())
                for (auto& x : *(*inv.hash())["argv"].arr()) argvv.push_back(x.toStr());
            if (!argvv.empty()) {
                auto takeFd = [&](const char* k) -> long long {
                    auto f = inv.hash()->find(k); if (f == inv.hash()->end()) return -1;
                    long long v = f->second.toInt(); inv.hash()->erase(f); return v;
                };
                auto tapped = [&](const char* k) {
                    auto t = inv.hash()->find(k);
                    return t != inv.hash()->end() && t->second.arr() && !t->second.arr()->empty();
                };
                SpawnStdio io;
                bool outBound = false, errBound = false;
#if !defined(_WIN32)
                io.stdinFd  = (int)takeFd("bind-in-fd");
                io.stdoutFd = (int)takeFd("bind-out-fd");
                io.stderrFd = (int)takeFd("bind-err-fd");
                outBound = io.stdoutFd >= 0; errBound = io.stderrFd >= 0;
#else
                takeFd("bind-in-fd"); takeFd("bind-out-fd"); takeFd("bind-err-fd");
#endif
                // a tapped stream is captured and fed to the taps at realize; an
                // untapped, unbound one is INHERITED, like Rakudo (the old
                // capture-and-discard was a relic of the lazy model)
                io.captureOut = !outBound && tapped("taps");
                io.captureErr = !errBound && tapped("taps-err");
                SpawnedChild sc = spawnChildStart(argvv, cwd, nullptr, io);
#if !defined(_WIN32)
                // the child holds its dup2'd copies of the bind ends; ours must
                // go now, or a bound reader would never see EOF
                if (io.stdinFd  >= 0) close(io.stdinFd);
                if (io.stdoutFd >= 0) close(io.stdoutFd);
                if (io.stderrFd >= 0) close(io.stderrFd);
#endif
                if (sc.pid) {
                    (*inv.hash())["pid"] = Value::integer(sc.pid);
                    long long tok;
                    { std::lock_guard<std::mutex> lk(g_spawnedM);
#if !defined(_WIN32)
                      // reap what earlier fire-and-forgets left behind: a long-
                      // lived program (Sparky) may never await its children, and
                      // zombies must not accumulate. The status is kept, so a
                      // late await still sees the real exitcode.
                      for (auto& kv : g_spawned) {
                          if (kv.second.reaped) continue;
                          int st = 0;
                          if (waitpid((pid_t)kv.second.pid, &st, WNOHANG) == (pid_t)kv.second.pid) {
                              kv.second.reaped = true; kv.second.rawStatus = st;
                          }
                      }
#endif
                      tok = ++g_spawnedSeq; g_spawned[tok] = sc; }
                    (*inv.hash())["spawn-token"] = Value::integer(tok);
                }
            }
            return pr;
        }
        // `.command` is the argv the process was constructed with, as a List
        if (m == "command") {
            auto it = inv.hash()->find("argv");
            Value out = Value::array(); out.isList = true;
            if (it != inv.hash()->end() && it->second.arr()) *out.arr() = *it->second.arrS();
            return out;
        }
        // `.ready` is Rakudo's "the process has started" Promise, kept with the
        // PID — real from `.start` on, now that the spawn is eager. One consulted
        // BEFORE the start has no PID to give and answers Nil — which is also
        // what Rakudo before 2018.04 did, and what Sparrow6's
        // `whenever $proc.ready` (an empty body) expects either way.
        if (m == "ready") {
            Value pr = Value::makeHash(); pr.hashKind = "Promise";
            (*pr.hash())["kind"] = Value::str("proc-ready");
            (*pr.hash())["proc"] = inv;
            (*pr.hash())["status"] = Value::str("Planned");
            return pr;
        }
        // $cat.bind-stdin($echo.stdout) — one process's stream wired straight
        // into another's stdin, as a REAL pipe between the two children. Both
        // spawn at .start and run concurrently, so a stream larger than the pipe
        // buffer cannot deadlock. The pipe is made here, before either start;
        // each end waits on its proc's hash for the spawn to dup2 it into place.
        // Both ends carry FD_CLOEXEC, so the reader's EOF arrives exactly when
        // the writer exits. First binding wins; after a start it's too late.
        if (m == "bind-stdin") {
#if !defined(_WIN32)
            if (!args.empty() && args[0].t == VT::Hash && args[0].hashKind == "Supply" &&
                args[0].hash() && args[0].hash()->count("proc") &&
                !inv.hash()->count("pid") && !inv.hash()->count("bind-in-fd")) {
                Value src = (*args[0].hash())["proc"];
                std::string stream = args[0].hash()->count("stream") ? (*args[0].hash())["stream"].toStr() : "stdout";
                if (src.hash() && !src.hash()->count("pid")) {
                    int p[2];
                    if (pipe(p) == 0) {
                        fcntl(p[0], F_SETFD, FD_CLOEXEC); fcntl(p[1], F_SETFD, FD_CLOEXEC);
                        (*inv.hash())["bind-in-fd"] = Value::integer(p[0]);
                        (*src.hash())[stream == "stderr" ? "bind-err-fd" : "bind-out-fd"] = Value::integer(p[1]);
                    }
                }
            }
#endif
            return Value::boolean(true);
        }
        // .kill sends a real signal (default SIGHUP, like Rakudo) — the process
        // has existed since .start. Realizing the promise still drains and reaps,
        // so an `await` after the kill returns. Skipped once the exitcode is in:
        // that pid is dead and may have been recycled.
        if (m == "kill") {
            auto pidIt = inv.hash()->find("pid");
            if (pidIt != inv.hash()->end() && !inv.hash()->count("exitcode")) {
#if defined(_WIN32)
                auto tokIt = inv.hash()->find("spawn-token");
                if (tokIt != inv.hash()->end()) {
                    std::lock_guard<std::mutex> lk(g_spawnedM);
                    auto it2 = g_spawned.find(tokIt->second.toInt());
                    if (it2 != g_spawned.end() && it2->second.hProcess)
                        TerminateProcess(it2->second.hProcess, 1);
                }
#else
                // if the zombie sweep already reaped it, the pid may have been
                // recycled — signalling it would hit an innocent process
                bool gone = false;
                auto tokIt = inv.hash()->find("spawn-token");
                if (tokIt != inv.hash()->end()) {
                    std::lock_guard<std::mutex> lk(g_spawnedM);
                    auto it2 = g_spawned.find(tokIt->second.toInt());
                    gone = it2 != g_spawned.end() && it2->second.reaped;
                }
                if (!gone) {
                    int sig = -1;
                    for (auto& a : args) if (a.t != VT::Pair) { sig = signalNumberOf(a); break; }
                    if (sig <= 0) sig = SIGHUP;
                    ::kill((pid_t)pidIt->second.toInt(), sig);
                }
#endif
            }
            return Value::boolean(true);
        }
        if (m == "close-stdin" || m == "print" || m == "say" || m == "write" || m == "put") return Value::boolean(true);
        // after runProcPromise stored the exit status on the proc:
        if (m == "exitcode") { auto it = inv.hash()->find("exitcode"); return it != inv.hash()->end() ? it->second : Value::integer(-1); }
        // the signal the process died from; 0 for a normal exit (that is all this
        // spawn path can see — TAP's Status folds it as `exit +< 8 +| signal`,
        // and a MISSING .signal broke its whole exit-status relay mid-then)
        if (m == "signal") return Value::integer(0);
        if (m == "so" || m == "Bool") { auto it = inv.hash()->find("exitcode"); return Value::boolean(it != inv.hash()->end() && it->second.toInt() == 0); }
    }
    // Segment continues in MethodCallPart2.cpp — same ordered chain.
    if (auto r = methodCallPart2(inv, m, args, rwArgs)) return std::move(*r);
    // Segment continues in MethodCallPart3.cpp — same ordered chain.
    if (auto r = methodCallPart3(inv, m, args, rwArgs)) return std::move(*r);
    if (auto r = methodCallTail(inv, m, args, rwArgs)) return std::move(*r);
    // fallthrough: unknown method — but any method call on Nil returns Nil
    if (inv.t == VT::Nil) return Value::nil();
    // FALLBACK: a class may catch every unresolved method itself, receiving the
    // NAME first and then the original arguments (Terminal::ANSI::OO turns each
    // colour name into a method this way). Looked up last, so it never shadows
    // a real method, and skipped for FALLBACK itself to avoid a loop.
    if (m != "FALLBACK") {
        ClassInfo* fci = nullptr;
        if (inv.t == VT::Object && inv.obj()) fci = inv.obj()->cls.get();
        else if (inv.t == VT::Type) { auto it = classes_.find(inv.s); if (it != classes_.end()) fci = it->second.get(); }
        if (fci)
            if (Value* fb = fci->findMethod("FALLBACK")) {
                ValueList fargs; fargs.reserve(args.size() + 1);
                fargs.push_back(Value::str(m));
                for (auto& a : args) fargs.push_back(a);
                return invokeMethod(*fb, inv, fargs);
            }
    }
    // A grammar's RULE is also a method: `G.new.some-rule` is legal Raku. With no
    // string to match it starts on an empty cursor and fails, so Rakudo hands back
    // a falsy Cursor. Answer an undefined Match, which is falsy and matches
    // nothing — URI's suite asserts exactly that with
    // `nok 'foo' ~~ IETF::RFC_Grammar::URI.new().TOP-non-empty`.
    {
        ClassInfo* gci = inv.t == VT::Object && inv.obj() ? inv.obj()->cls.get()
                       : inv.t == VT::Type ? (classes_.count(inv.s) ? classes_[inv.s].get() : nullptr)
                       : nullptr;
        for (ClassInfo* c = gci; c; c = c->parent.get())
            if (c->isGrammar && c->findRule(m)) {
                // With no argument the rule runs on an EMPTY cursor, and the result
                // is that failed-or-successful Cursor. Smart-matching a string
                // against it yields the cursor's own truthiness, whatever the
                // string — which is why `'#foo' ~~ G.new.URI-reference` is True
                // (URI-reference matches "") and `'foo' ~~ G.new.absolute-URI` is
                // False (absolute-URI does not).
                return grammarParse(c, "", /*subparse=*/false, m, Value());
            }
    }
    // An ITERABLE object answers the list methods that Rakudo's Iterable role
    // supplies, by running its own `.iterator`: `glob('*.md').sort` works because
    // IO::Glob `does Iterable`. Deliberately NOT the whole list surface — Rakudo
    // splits it, and `.list`/`.elems`/`.reverse`/`.join`/`.kv` on an Iterable
    // object mean the invocant AS ONE ITEM (checked against Rakudo, one by one).
    if (inv.t == VT::Object && inv.obj() && inv.obj()->cls) {
        static const std::set<std::string> kIterableMethods = {
            "sort", "map", "grep", "first", "head", "tail", "unique", "squish",
            "Seq", "flat" };
        if (kIterableMethods.count(m)) {
            bool iterable = inv.obj()->cls->findMethod("iterator") != nullptr;
            for (ClassInfo* c = inv.obj()->cls.get(); c && !iterable; c = c->parent.get())
                if (c->doesRole("Iterable")) iterable = true;
            ValueList items;
            if (iterable && objListItems(inv, items)) {
                Value lst = Value::array(); lst.isList = true; *lst.arr() = std::move(items);
                return methodCall(lst, m, args, rwArgs);
            }
        }
    }
    // A user class deriving the BUILTIN IO::Handle (no ClassInfo behind it)
    // still owes the handle protocol's small setup surface — Test::Output's
    // capture classes call self.encoding('utf8') from their constructors.
    // Attr-backed, added per real need, measured by the battery.
    if (inv.t == VT::Object && inv.obj() && inv.obj()->cls) {
        std::string nb;
        for (ClassInfo* c = inv.obj()->cls.get(); c && nb.empty(); c = c->parent.get())
            nb = c->nativeParent;
        if (nb == "IO::Handle") {
            if (m == "encoding") {
                if (!args.empty() && args[0].t != VT::Pair) {
                    inv.obj()->attrs["__io-encoding"] = args[0];
                    return args[0];
                }
                auto it = inv.obj()->attrs.find("__io-encoding");
                return it != inv.obj()->attrs.end() ? it->second : Value::str("utf8");
            }
            // The handle WRITE protocol: print/say/put on a derived handle
            // encode and funnel into the class's own WRITE(Blob) — exactly
            // how Rakudo's IO::Handle routes them, and how Test::Output's
            // capture classes receive everything they capture.
            if (m == "print" || m == "say" || m == "put") {
                if (Value* wm = inv.obj()->cls->findMethod("WRITE")) {
                    std::string s;
                    for (auto& a : args) {
                        if (a.t == VT::Pair) continue;
                        s += m == "say" ? methodCall(a, "gist", ValueList{}, nullptr).toStr()
                                        : a.toStr();
                    }
                    if (m != "print") s += "\n";
                    Value blob = Value::str(s);
                    blob.hashKind = "Blob";
                    return invokeMethod(*wm, inv, ValueList{blob}, nullptr);
                }
            }
            if (m == "flush" || m == "close") return Value::boolean(true);
        }
    }
    // The invocant may be a type whose `class`/`grammar` declaration is further
    // down the file — those are compile-time in Rakudo, so `say f("x"); grammar
    // G {…}` works there. Create it now and try once more.
    if (inv.t == VT::Type && !inv.s.empty() && materializePendingType(inv.s))
        return methodCall(inv, m, args);
    // Rakudo's Any gives EVERY object the one-element-list interface:
    // `Foo.new.elems` is 1, `.list` is `(Foo.new)`, `.head` is the object
    // itself, `.keys` is `(0)`. rakupp had this for the simple scalars only
    // (42.elems is 1), so a module asking `$.type.elems` about an object of one
    // of its own classes died where Rakudo answers 1 — Data::TypeSystem's
    // Examiner, and the six dists queued behind it. Last resort, after every
    // user method and builtin path has already declined.
    if (inv.t == VT::Object) {
        static const std::set<std::string> kOneElem = {
            "elems", "end", "list", "List", "Array", "flat", "cache", "eager",
            "values", "keys", "pairs", "antipairs", "kv", "head", "tail",
            "first", "map", "grep", "sort", "reverse", "reduce", "unique",
            "squish", "skip", "batch", "rotor", "combinations", "permutations"};
        if (kOneElem.count(m)) {
            Value one = Value::array(); one.arr()->push_back(inv); one.isList = true;
            return methodCall(one, m, args);
        }
    }
    if (std::getenv("RAKUPP_TRACE"))
        std::cerr << "[NoMethod] ." << m << " on " << inv.typeName()
                  << " at " << (srcFile_.empty() ? "?" : srcFile_) << ":" << curLine_ << "\n";
    // A private call arrives as its DISPATCH KEY — `!wrong`, which is what the
    // class method tables are keyed by (`md->isPrivate ? "!" + name : name`).
    // The exception reports the method as it was WRITTEN, and says which kind
    // of call it was: roast's S12-methods/private.t asks for
    // `method => 'wrong', private => &so`, and Rakudo's message names the kind
    // too. Only the key is prefixed — a Raku method name cannot begin with `!`.
    const bool priv = m.size() > 1 && m[0] == '!';
    const std::string name = priv ? m.substr(1) : (const std::string&)m;
    throwTypedV("X::Method::NotFound",
                {{"method",   Value::str(name)},
                 {"typename", Value::str(inv.typeName())},
                 {"private",  Value::boolean(priv)}},
                std::string("No such ") + (priv ? "private " : "") + "method '" +
                    (const std::string&)m + "' for invocant of type '" + inv.typeName() + "'");
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
    Value v; v.t = VT::Code; v.setCode(std::make_shared<Callable>());
    v.code()->body = &b->stmts; v.code()->isBlock = true; v.code()->closure = std::move(closure);
    return v;
}
// Collect LAST/QUIT/CLOSE phasers from a block's top-level statements.
static void scanSupplyPhasers(const Value& blk, ValueList* lastP,
                              ValueList* quitP, ValueList* closeP,
                              std::shared_ptr<Env> phaserEnv = nullptr) {
    if (blk.t != VT::Code || !blk.code() || !blk.code()->body) return;
    // phaserEnv (issue #18): a LAST phaser reads the block's parameters as of
    // the LAST invocation (`LAST { say "Done with $c" }`). The block's
    // DEFINITION closure never holds $c — callers that drain through a
    // param-mirroring shim pass the shim's env here instead.
    auto env = [&](const Value& b2) { return phaserEnv ? phaserEnv : b2.code()->closure; };
    for (auto& s : *blk.code()->body) {
        if (s->kind != NK::Block) continue;
        auto* b = static_cast<Block*>(s.get());
        if (lastP  && b->phaser == "LAST")  lastP->push_back(supplyPhaserCode(b, env(blk)));
        if (quitP  && b->phaser == "QUIT")  quitP->push_back(supplyPhaserCode(b, env(blk)));
        if (closeP && b->phaser == "CLOSE") closeP->push_back(supplyPhaserCode(b, env(blk)));
    }
}
// A callable that runs `fn` with `ctx` re-established as the active supply
// activation — used for whenever bodies and done/quit hooks that fire later
// (possibly from an I/O worker thread holding the GIL).
static Value ctxCallable(std::shared_ptr<SupplyTapCtx> ctx,
                         std::function<Value(Interpreter&, ValueList&)> fn) {
    Value v; v.t = VT::Code; v.setCode(std::make_shared<Callable>());
    v.code()->builtin = [ctx, fn](Interpreter& I, ValueList& a) -> Value {
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
    ValueList phasers;
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
    Value blk = (s.t == VT::Hash && s.hash()->count("block")) ? (*s.hash())["block"] : Value::nil();
    ValueList vals; bool quit = false; Value quitReason; std::string quitMsg;
    auto ctx = std::make_shared<SupplyTapCtx>();
    ctx->collect = &vals;
    tctx_.tapStack.push_back(ctx);
    try {
        if (blk.t == VT::Code) { ValueList na; callCallable(blk, na); }
    }
    catch (RakuError& e) { quit = true; quitReason = exceptionFor(e); quitMsg = e.message; }
    catch (DoneEx&) {} // `done` in the body: normal end of the stream
    catch (...) { tctx_.tapStack.pop_back(); throw; }
    tctx_.tapStack.pop_back();
    // A whenever on a still-pending Promise holds the supply open (Cro's connector
    // `establish` awaits connect() inside the supply block). The eager drain must
    // wait for those one-shots to fire before treating the supply as finished —
    // but only while a worker exists that could still settle them, so a
    // never-kept promise doesn't hang `.list`.
    if (ctx->pending > 0 && parallelMode_) {
        // parallel mode: the one-shots fire on their own threads — just wait
        // for them (bounded by "somebody could still settle them"), or the
        // drain returns before a promise-whenever delivered (cro-core's
        // drain-waits test caught this the day parallel became the default)
        while (ctx->pending > 0 && liveWorkers_.load() > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    else if (ctx->pending > 0 && gilHeld_ && !parallelMode_ && liveWorkers_.load() > 0) {
        static thread_local ExecContext parked;
        saveCtx(parked);
        gilYieldNotify();
        for (;;) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            gil_.lock();
            if (ctx->pending <= 0 || liveWorkers_.load() == 0) break;
            gilYieldNotify();
        }
        loadCtx(parked);
    }
    ctx->collect = nullptr; // vals is about to go out of scope with this frame
    {
        auto closers = std::move(ctx->closers); // on-close callbacks registered during the drain
        for (auto& cb : closers) if (cb.t == VT::Code) { try { ValueList na; callCallable(cb, na); } catch (...) {} }
    }
    Value out = Value::makeHash(); out.hashKind = "Supply";
    Value v = Value::array(); *v.arr() = std::move(vals); (*out.hash())["values"] = v;
    if (quit) { (*out.hash())["quit-reason"] = quitReason; (*out.hash())["quit-message"] = Value::str(quitMsg); }
    return out;
}

// Build an IO::Socket::Async connection value around a connected fd.
static Value makeAsyncSocket(int fd) {
    Value s = Value::makeHash(); s.hashKind = "AsyncSocket";
    (*s.hash())["fd"] = Value::integer(fd);
    sockaddr_in a{}; socklen_t alen = sizeof(a);
    if (::getsockname(fd, (sockaddr*)&a, &alen) == 0) {
        (*s.hash())["socket-host"] = Value::str(inet_ntoa(a.sin_addr));
        (*s.hash())["socket-port"] = Value::integer(ntohs(a.sin_port));
    }
    alen = sizeof(a);
    if (::getpeername(fd, (sockaddr*)&a, &alen) == 0) {
        (*s.hash())["peer-host"] = Value::str(inet_ntoa(a.sin_addr));
        (*s.hash())["peer-port"] = Value::integer(ntohs(a.sin_port));
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

// drainWorkers' wake-up for the signal dispatcher: it blocks in read() on the
// self-pipe where workerAbort_ is invisible, so without this it held the whole
// 2 s shutdown grace (a GUI window's close button felt seconds slow). A 0 byte
// — no real signal is 0 — tells it to unwind.
void Interpreter::wakeSignalWorker() {
#if !defined(_WIN32)
    if (g_sigPipe[1] >= 0) { unsigned char z = 0; ssize_t r = ::write(g_sigPipe[1], &z, 1); (void)r; }
#endif
}

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
    auto fin = std::make_shared<std::atomic<bool>>(false);
    auto spawnScope = tctx_.cur ? tctx_.cur : global_;
    Interpreter* self = this;
    if (secs < 0) secs = 0;
    Value fireW = ctxCallable(ctx, [blk, ctx](Interpreter& I2, ValueList&) -> Value {
        // shutdown mid-delay: release the pending hold, but never run the block
        if (!I2.workerAbort_.load(std::memory_order_relaxed) && !ctx->done && !ctx->doneFired) { ValueList none; try { I2.callCallable(blk, none); } catch (NextEx&) {} catch (LastEx&) {} catch (DoneEx&) {} }
        ctx->pending--;
        I2.maybeFinishSupply(ctx);
        return Value::any();
    });
    throttleSpawn();
    addWorker(BigStackThread([self, secs, fireW, fin, spawnScope, ctx]() mutable {
        t_isWorker = true;
        // GIL not held; slices against a fixed deadline (drift-free, huge/Inf-safe)
        // and wakes early on `done`/`.close` or shutdown.
        auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(secs);
        while (!ctx->done && !ctx->doneFired && !self->workerAbort_.load(std::memory_order_relaxed)) {
            auto now = std::chrono::steady_clock::now();
            if (now >= end) break;
            double left = std::chrono::duration<double>(end - now).count();
            std::this_thread::sleep_for(std::chrono::duration<double>(left < 0.05 ? left : 0.05));
        }
        self->gil_.lock();
        ExecContext wctx; self->loadCtx(wctx);
        self->tctx_.cur = spawnScope;
        self->tctx_.dynStack.push_back(spawnScope.get());
        ValueList none; try { self->callCallable(fireW, none); } catch (...) {}
        self->gilYieldNotify();
        self->liveWorkers_--;
        fin->store(true, std::memory_order_release);
    }), fin);
    Value t = Value::makeHash(); t.hashKind = "Tap"; return t;
}

// `Supply.interval(1).map({…})` — a kind-based live supply keeps the combinators
// it was given as a transform CHAIN on the Supply value itself: unlike a
// Supplier-backed supply there is no tap record to hang them on, because the
// values come from a worker, not from a fan-out over registered taps. So the
// chain is applied here, by wrapping the consumer: each value goes through
// map/grep/head/… before the block or emit callback sees it.
//
// A transform that dies is the SOURCE failing, not the consumer, so it QUITS
// this subscription: the block's QUIT phasers get the exception and the
// subscription ends. A die in the block BODY is a different thing entirely and
// must not be caught here (it kills the react — see spawnIntervalWhenever).
Value Interpreter::wrapSupplyChain(const Value& supply, Value consumer) {
    if (consumer.t != VT::Code) return consumer;
    if (!(supply.t == VT::Hash && supply.hash() && supply.hash()->count("chain"))) return consumer;
    // this subscription's OWN copy of the chain, each step with fresh state
    // (head/skip/unique count per subscription, exactly as in tapSupply)
    auto rec = std::make_shared<Value>(Value::makeHash());
    Value chain = Value::array();
    for (auto& step : *supply.hash()->at("chain").arr()) {
        Value s2 = Value::makeHash(); *s2.hash() = *step.hash();
        (*s2.hash())["state"] = Value::makeHash();
        chain.arr()->push_back(s2);
    }
    (*rec->hash())["chain"] = chain;
    ValueList quitP;
    scanSupplyPhasers(consumer, nullptr, &quitP, nullptr);
    Value w; w.t = VT::Code; w.setCode(std::make_shared<Callable>());
    w.code()->builtin = [rec, consumer, quitP](Interpreter& I, ValueList& a) -> Value {
        Value in = a.empty() ? Value::any() : a[0];
        bool complete = false;
        ValueList outs;
        try { outs = I.applyTapChain(*rec, in, complete); }
        catch (RakuError& e) {
            if (quitP.empty()) throw;   // nothing handles the quit: it leaves the react
            Value ex = I.exceptionFor(e);
            for (auto& q : quitP) { ValueList one{ex}; I.callCallable(q, one); }
            throw LastEx{};             // handled: this subscription is over
        }
        Value last = Value::any();
        for (auto& o : outs) { ValueList one{o}; last = I.callCallable(consumer, one); }
        if (complete) throw LastEx{};   // head/first reached its limit
        return last;
    };
    return w;
}

// `whenever Supply.interval(N)` inside a supply {…} block: a repeating ticker
// that holds the activation open (pending) and fires each tick under it, so the
// body's emits reach the downstream tap. Stops on `done`, on the activation's
// tap closing (.tap.close — the CLOSE-phaser stress in S17 syntax.t spawns and
// closes thousands of these), or at interpreter shutdown.
Value Interpreter::spawnSupplyInterval(double interval, double delay, Value blk,
                                       std::shared_ptr<SupplyTapCtx> ctx) {
    engageGil();
    ctx->pending++;
    liveWorkers_++;
    auto fin = std::make_shared<std::atomic<bool>>(false);
    auto spawnScope = tctx_.cur ? tctx_.cur : global_;
    Interpreter* self = this;
    if (interval < 0.001) interval = 0.001; // Rakudo clamps a zero/negative interval
    if (delay < 0) delay = 0;
    auto tick = std::make_shared<long long>(0);
    ValueList quitP;
    scanSupplyPhasers(blk, nullptr, &quitP, nullptr);
    Value fireW = ctxCallable(ctx, [blk, ctx, tick, quitP](Interpreter& I2, ValueList&) -> Value {
        if (!ctx->done && !ctx->doneFired) {
            ValueList one{Value::integer((*tick)++)};
            try { I2.callCallable(blk, one); }
            catch (NextEx&) {} catch (LastEx&) { ctx->done = true; } catch (DoneEx&) {}
            catch (RakuError& e) {
                // a die in the tick body QUITS the enclosing supply (same rule as
                // the generic whenever path): its own QUIT phasers see it first,
                // otherwise it goes downstream and closes the activation. The
                // worker used to swallow it, so the failure vanished and the
                // ticker kept running.
                Value ex = I2.exceptionFor(e);
                bool handled = false;
                for (auto& q : quitP) { ValueList o2{ex}; try { I2.callCallable(q, o2); handled = true; } catch (...) {} }
                if (!handled && ctx->quitCb.t == VT::Code) { ValueList o2{ex}; try { I2.callCallable(ctx->quitCb, o2); } catch (...) {} }
                ctx->done = true;
                if (ctx->tap) I2.closeTapHandle(ctx->tap);
            }
        }
        return Value::any();
    });
    throttleSpawn();
    addWorker(BigStackThread([self, interval, delay, fireW, ctx, fin, spawnScope]() mutable {
        t_isWorker = true;
        auto stop = [&] {
            if (self->workerAbort_.load(std::memory_order_relaxed)) return true;
            if (ctx->done || ctx->doneFired) return true;
            if (ctx->tap) { std::lock_guard<std::mutex> lk(ctx->tap->m); if (ctx->tap->closed) return true; }
            return false;
        };
        auto sleepChunked = [&](double secs) { // GIL not held; wakes early on teardown
            double left = secs;
            while (left > 0 && !stop()) {
                double c = left < 0.25 ? left : 0.25;
                std::this_thread::sleep_for(std::chrono::duration<double>(c));
                left -= c;
            }
        };
        sleepChunked(delay);
        while (!stop()) {
            self->gil_.lock();
            ExecContext wctx; self->loadCtx(wctx);
            self->tctx_.cur = spawnScope;
            self->tctx_.dynStack.push_back(spawnScope.get());
            if (!stop()) { ValueList none; try { self->callCallable(fireW, none); } catch (...) {} }
            self->gilYieldNotify();
            if (stop()) break;
            sleepChunked(interval);
        }
        self->gil_.lock(); // release the activation hold and let the supply finish
        ExecContext wctx2; self->loadCtx(wctx2);
        self->tctx_.cur = spawnScope;
        self->tctx_.dynStack.push_back(spawnScope.get());
        ctx->pending--;
        try { self->maybeFinishSupply(ctx); } catch (...) {}
        self->gilYieldNotify();
        self->liveWorkers_--;
        fin->store(true, std::memory_order_release);
    }), fin);
    Value t = Value::makeHash(); t.hashKind = "Tap"; return t;
}

// Run a NATIVE continuation on a worker after a real delay — the `.then` of a
// timer promise (`Promise.in(3).then({…})` ran its block at t=0 before). The
// same sliced wait as spawnTimerWhenever: shutdown wakes it within ~50 ms, and
// an aborted worker never runs the continuation.
void Interpreter::spawnDelayedNative(double secs, std::function<void()> fn) {
    engageGil();
    liveWorkers_++;
    auto fin = std::make_shared<std::atomic<bool>>(false);
    auto spawnScope = tctx_.cur ? tctx_.cur : global_;
    Interpreter* self = this;
    if (secs < 0) secs = 0;
    throttleSpawn();
    addWorker(BigStackThread([self, secs, fn, fin, spawnScope]() mutable {
        t_isWorker = true;
        bool stopped = false;                                          // GIL not held
        auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(secs);
        for (;;) {
            if (self->workerAbort_.load(std::memory_order_relaxed)) { stopped = true; break; }
            auto now = std::chrono::steady_clock::now();
            if (now >= end) break;
            double left = std::chrono::duration<double>(end - now).count();
            std::this_thread::sleep_for(std::chrono::duration<double>(left < 0.05 ? left : 0.05));
        }
        self->gil_.lock();
        ExecContext wctx; self->loadCtx(wctx);
        tctx_.cur = spawnScope;
        tctx_.dynStack.push_back(spawnScope.get());
        if (!stopped) { try { fn(); } catch (...) {} }
        self->gilYieldNotify();
        self->liveWorkers_--;
        fin->store(true, std::memory_order_release);
    }), fin);
}

Value Interpreter::spawnTimerWhenever(double secs, Value blk, std::shared_ptr<ReactCtx> ctx) {
    engageGil();
    if (ctx) { std::lock_guard<std::mutex> lk(ctx->m); ctx->liveSources++; }
    liveWorkers_++;
    auto fin = std::make_shared<std::atomic<bool>>(false);
    auto spawnScope = tctx_.cur ? tctx_.cur : global_;
    Interpreter* self = this;
    if (secs < 0) secs = 0;
    throttleSpawn();
    addWorker(BigStackThread([self, secs, blk, ctx, fin, spawnScope]() mutable {
        t_isWorker = true;
        // The full delay is honored (issue #41 capped it at 35 s) — slept in
        // slices against a fixed deadline so shutdown or `done` wakes the worker
        // within ~50 ms, and in double-rep time so a huge/Inf timer can't
        // overflow sleep_for's int64 nanosecond range.
        bool stopped = false;                                          // GIL not held
        auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(secs);
        for (;;) {
            if (self->workerAbort_.load(std::memory_order_relaxed) || (ctx && ctx->closed)) { stopped = true; break; }
            auto now = std::chrono::steady_clock::now();
            if (now >= end) break;
            double left = std::chrono::duration<double>(end - now).count();
            std::this_thread::sleep_for(std::chrono::duration<double>(left < 0.05 ? left : 0.05));
        }
        self->gil_.lock();
        ExecContext wctx; self->loadCtx(wctx);
        tctx_.cur = spawnScope;
        tctx_.dynStack.push_back(spawnScope.get());
        if (ctx) self->reactStack_.push_back(ctx);
        if (!stopped && !(ctx && ctx->closed)) {
            ValueList none;
            try { self->callCallable(blk, none); } catch (NextEx&) {} catch (LastEx&) {} catch (DoneEx&) {} catch (...) {}
        }
        if (ctx) self->reactStack_.pop_back();
        if (ctx) { std::lock_guard<std::mutex> lk(ctx->m); if (ctx->liveSources > 0) ctx->liveSources--; ctx->cv.notify_all(); }
        self->gilYieldNotify();
        self->liveWorkers_--;
        fin->store(true, std::memory_order_release);
    }), fin);
    Value t = Value::makeHash(); t.hashKind = "Tap"; return t;
}

// `Supply.interval($interval, $delay)` tapped: a worker emits ascending Ints —
// the first after $delay (0 = immediately, like Rakudo), then one per $interval,
// forever. Sleeps in small chunks so `done` (react ctx closed) or `.close` (tap
// handle) tears the worker down promptly instead of after a whole interval.
Value Interpreter::spawnIntervalWhenever(double interval, double delay, Value blk,
                                         std::shared_ptr<ReactCtx> ctx,
                                         std::shared_ptr<TapHandle> handle) {
    engageGil();
    if (ctx) { std::lock_guard<std::mutex> lk(ctx->m); ctx->liveSources++; }
    liveWorkers_++;
    auto fin = std::make_shared<std::atomic<bool>>(false);
    auto spawnScope = tctx_.cur ? tctx_.cur : global_;
    Interpreter* self = this;
    if (interval < 0.001) interval = 0.001; // Rakudo clamps a zero/negative interval
    if (delay < 0) delay = 0;
    throttleSpawn();
    addWorker(BigStackThread([self, interval, delay, blk, ctx, handle, fin, spawnScope]() mutable {
        t_isWorker = true;
        auto closedNow = [&] {
            if (self->workerAbort_.load(std::memory_order_relaxed)) return true; // mainline done: stop ticking
            if (ctx && ctx->closed) return true;
            if (handle) { std::lock_guard<std::mutex> lk(handle->m); if (handle->closed) return true; }
            return false;
        };
        auto sleepChunked = [&](double secs) { // GIL not held; wakes early on teardown
            double left = secs;
            while (left > 0 && !closedNow()) {
                double c = left < 0.25 ? left : 0.25;
                std::this_thread::sleep_for(std::chrono::duration<double>(c));
                left -= c;
            }
        };
        sleepChunked(delay);
        long long tick = 0;
        bool lastEx = false;
        while (!closedNow() && !lastEx) {
            self->gil_.lock();
            ExecContext wctx; self->loadCtx(wctx);
            tctx_.cur = spawnScope;
            tctx_.dynStack.push_back(spawnScope.get());
            if (ctx) self->reactStack_.push_back(ctx);
            if (!closedNow()) {
                ValueList one{Value::integer(tick++)};
                try { self->callCallable(blk, one); }
                catch (NextEx&) {}
                catch (LastEx&) { lastEx = true; } // `last` ends THIS subscription
                catch (DoneEx&) {} // `done` closed the ctx in its bookkeeping
                catch (RakuError& e) {
                    // a die in the whenever BODY kills the whole react and
                    // propagates (issue #18 — the reference output shows QUIT
                    // phasers do NOT catch a body die; they are for the
                    // source's own quit). Without this the swallowed die left
                    // the ticker running and the react waiting forever.
                    if (ctx) {
                        std::lock_guard<std::mutex> lk(ctx->m);
                        if (!ctx->quitFlag) { ctx->quitFlag = true; ctx->quitErr = e.payload.t == VT::Nil ? Value::str(e.message) : e.payload; }
                        ctx->closed = true; ctx->cv.notify_all();
                    }
                    lastEx = true;
                }
                catch (...) {}
            }
            if (ctx) self->reactStack_.pop_back();
            self->gilYieldNotify();
            if (closedNow() || lastEx) break;
            sleepChunked(interval);
        }
        if (ctx) { std::lock_guard<std::mutex> lk(ctx->m); if (ctx->liveSources > 0) ctx->liveSources--; ctx->cv.notify_all(); }
        self->liveWorkers_--;
        fin->store(true, std::memory_order_release);
    }), fin);
    Value t = Value::makeHash(); t.hashKind = "Tap";
    if (handle) { t.extM() = handle; (*t.hash())["wired"] = Value::boolean(true); } // .close stops the ticker
    return t;
}

// `whenever $channel { … }` — a react source that runs the block once per value
// the channel receives, and completes when the channel closes. A worker does the
// waiting so the main thread's react loop stays free; without this the block ran
// ONCE, with the channel object itself as the topic.
Value Interpreter::spawnChannelWhenever(Value chan, Value blk, std::shared_ptr<ReactCtx> ctx) {
    engageGil();
    if (ctx) { std::lock_guard<std::mutex> lk(ctx->m); ctx->liveSources++; }
    liveWorkers_++;
    auto fin = std::make_shared<std::atomic<bool>>(false);
    auto spawnScope = tctx_.cur ? tctx_.cur : global_;
    Interpreter* self = this;
    throttleSpawn();
    addWorker(BigStackThread([self, chan, blk, ctx, fin, spawnScope]() mutable {
        t_isWorker = true;
        // Parallel mode has no GIL discipline: taking (and never releasing)
        // the lock here made the FIRST channel worker the accidental GIL
        // owner for its whole lifetime — every later channel whenever's
        // worker starved at this line (the two-channel react in
        // S17-supply/syntax.t, the last of the P5 isolation livelocks).
        // Under the GIL the lock is real and yieldToWorkerFor cycles it.
        if (!self->parallelMode_) self->gil_.lock();
        ExecContext wctx; self->loadCtx(wctx);
        tctx_.cur = spawnScope;
        tctx_.dynStack.push_back(spawnScope.get());
        if (ctx) self->reactStack_.push_back(ctx);
        for (;;) {
            if (ctx && ctx->closed) break;
            // Poll and POP under the channel's stripe — the same lock send and
            // receive use. The old loop read "closed"/"queue" and erased the
            // front element with NO lock, racing send's striped push_back and
            // close's map insert; under RAKUPP_PARALLEL a torn read made this
            // worker break early or miss a value, wedging every construct
            // downstream of the whenever (S17-supply/syntax.t's two-channel
            // react — the last isolation livelock of the P5 wall).
            Value v; bool got = false, fin = false;
            {   std::lock_guard<std::recursive_mutex> lk(Interpreter::atomicStripe(chan.hash()));
                auto qi = chan.hash() ? chan.hash()->find("queue") : ValueMap::iterator{};
                ValueList* q = chan.hash() && qi != chan.hash()->end() && qi->second.arr() ? qi->second.arr() : nullptr;
                if (!q) fin = true;
                else if (!q->empty()) { v = q->front(); q->erase(q->begin()); got = true; }
                else {
                    auto ci = chan.hash()->find("closed");
                    fin = ci != chan.hash()->end() && ci->second.truthy();
                }
            }
            if (fin) break;
            if (!got) {
                // yieldToWorkerFor is a no-op in parallel mode (nothing to
                // yield) — without the sleep this loop was a hot spin
                if (self->parallelMode_) std::this_thread::sleep_for(std::chrono::milliseconds(5));
                else self->yieldToWorkerFor(0.02);
                continue;
            }
            ValueList one{v};
            try { self->callCallable(blk, one); }
            catch (NextEx&) {} catch (LastEx&) { break; } catch (DoneEx&) { break; }
            catch (RakuError& e) {
                // a die in the whenever body kills the react and propagates
                // (issue-18 semantics, same as the interval arm) — the old
                // silent catch also swallowed real errors from the block,
                // which made this exact spot undebuggable
                if (ctx) {
                    std::lock_guard<std::mutex> lk(ctx->m);
                    if (!ctx->quitFlag) { ctx->quitFlag = true; ctx->quitErr = e.payload.t == VT::Nil ? Value::str(e.message) : e.payload; }
                    ctx->closed = true; ctx->cv.notify_all();
                }
                break;
            }
            catch (...) {}
            if (ctx) { std::lock_guard<std::mutex> lk(ctx->m); if (ctx->closed) break; }
        }
        if (ctx) self->reactStack_.pop_back();
        if (ctx) { std::lock_guard<std::mutex> lk(ctx->m); if (ctx->liveSources > 0) ctx->liveSources--; ctx->cv.notify_all(); }
        if (!self->parallelMode_) self->gilYieldNotify(); // unlocks the GIL — parallel never took it
        self->liveWorkers_--;
        fin->store(true, std::memory_order_release);
    }), fin);
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
    Value t = Value::makeHash(); t.hashKind = "Tap"; t.extM() = handle;
    (*t.hash())["wired"] = Value::boolean(true);
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
        auto fin = std::make_shared<std::atomic<bool>>(false);
        throttleSpawn();
        addWorker(BigStackThread([self, spawnScope, fin]() mutable {
            t_isWorker = true;
            for (;;) {
                unsigned char c;
                ssize_t n = ::read(g_sigPipe[0], &c, 1);        // GIL not held
                if (n <= 0) break;
                if (c == 0) break;                              // drainWorkers' quit byte
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
            // Shutdown (quit byte or pipe EOF): hand every registered tap's
            // react source back, or a react parked on `whenever signal(...)`
            // outlives us into drainWorkers' 2 s grace — the pause between a
            // GUI window's close button and the process actually exiting.
            {
                std::set<std::shared_ptr<SignalTapRec>> taps;   // dedup: one rec may serve several signals
                {
                    std::lock_guard<std::mutex> lk(g_sigTapMutex);
                    for (auto& kv : g_sigTaps) taps.insert(kv.second);
                    g_sigTaps.clear();
                }
                for (auto& t : taps) {
                    if (t->handle) { std::lock_guard<std::mutex> lk(t->handle->m); t->handle->closed = true; }
                    if (t->react) {
                        std::lock_guard<std::mutex> lk(t->react->m);
                        if (t->react->liveSources > 0) t->react->liveSources--;
                        t->react->cv.notify_all();
                    }
                }
            }
            self->liveWorkers_--;
            fin->store(true, std::memory_order_release);
        }), fin);
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
    Value t = Value::makeHash(); t.hashKind = "Tap"; t.extM() = handle;
    (*t.hash())["wired"] = Value::boolean(true);
    return t;
#endif // !_WIN32
}

Value Interpreter::tapSupply(const Value& s, Value emitCb, Value doneCb, Value quitCb) {
    if (!(s.t == VT::Hash && s.hashKind == "Supply" && s.hash())) {
        Value t = Value::makeHash(); t.hashKind = "Tap"; return t;
    }
    auto& h = *s.hash();
    // a chain on a kind-based supply rides on the Supply itself (there is no tap
    // record to carry it), so apply it to the emit callback for every kind below
    if (h.count("chain") && !h.count("supplier") && !h.count("values"))
        emitCb = wrapSupplyChain(s, emitCb);
    // 1) on-demand block: run it now; whenevers inside wire inner taps that may
    //    outlive this call (fed by I/O workers).
    if (h.count("block")) {
        Value blk = h.at("block");
        auto handle = std::make_shared<TapHandle>();
        auto ctx = std::make_shared<SupplyTapCtx>();
        ctx->emitCb = emitCb; ctx->doneCb = doneCb; ctx->quitCb = quitCb; ctx->tap = handle;
        ValueList quitP;
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
        catch (DoneEx&) { // `done` in the supply body: normal end (its bookkeeping already ran)
            tctx_.tapStack.pop_back();
            ctx->blockDone = true;
        }
        catch (...) { tctx_.tapStack.pop_back(); closeTapHandle(handle); throw; }
        Value t = Value::makeHash(); t.hashKind = "Tap"; t.extM() = handle;
        (*t.hash())["wired"] = Value::boolean(true);
        return t;
    }
    // 2) live Supplier-backed supply: register a tap record; emit/done/quit fan out later
    if (h.count("supplier")) {
        Value tapRec = Value::makeHash();
        (*tapRec.hash())["emit"] = emitCb; (*tapRec.hash())["done"] = doneCb; (*tapRec.hash())["quit"] = quitCb;
        if (h.count("chain")) {
            Value chain = Value::array();
            for (auto& step : *h.at("chain").arr()) {
                Value s2 = Value::makeHash(); *s2.hash() = *step.hash();
                (*s2.hash())["state"] = Value::makeHash();
                chain.arr()->push_back(s2);
            }
            (*tapRec.hash())["chain"] = chain;
        }
        Value sup = h.at("supplier");
        if (sup.t == VT::Hash && sup.hash()->count("taps")) { std::lock_guard<std::recursive_mutex> regLk(supplierMutex(sup.hash())); (*sup.hash())["taps"].arr()->push_back(tapRec); }
        // Supplier::Preserving: replay every buffered value to this fresh tap (through
        // its own transform chain), so a tap that connects after the emits still sees
        // them (Cro's request-into-$!in-before-connect pattern).
        if (sup.t == VT::Hash && sup.hash()->count("preserving") && (*sup.hash())["preserving"].truthy() &&
            sup.hash()->count("buffer") && emitCb.t == VT::Code) {
            for (auto& bv : *(*sup.hash())["buffer"].arr()) {
                bool complete = false;
                ValueList outs = applyTapChain(tapRec, bv, complete);
                for (auto& o : outs) { ValueList one{o}; try { callCallable(emitCb, one); } catch (NextEx&) {} catch (LastEx&) { complete = true; break; } catch (DoneEx&) { complete = true; break; } }
                if (complete) break;
            }
        }
        // already-done supplier: fire done immediately so wiring completes
        if (sup.t == VT::Hash && sup.hash()->count("done_state") && (*sup.hash())["done_state"].truthy() &&
            doneCb.t == VT::Code) { ValueList na; try { callCallable(doneCb, na); } catch (...) {} }
        tapRec.hashKind = "Tap";
        return tapRec;
    }
    // 2b) Proc::Async stream — `whenever $proc.stdout.lines(:!chomp) {…}`
    // inside a supply block (TAP's parse-stream is exactly this shape): park
    // the record tap; runProcPromise feeds it when the process runs. This
    // used to fall through to the bottom SILENTLY, so the stream was never
    // captured and the block never fired.
    if (h.count("proc")) {
        registerProcStreamTap(s, emitCb, doneCb, quitCb);
        Value t = Value::makeHash(); t.hashKind = "Tap"; return t;
    }
    // 3) async listen: bind now, accept on a worker; each connection is emitted
    //    (under the GIL) through emitCb.
    // signal Supply tapped inside a `supply {…}` block (no react ctx — the
    // block's own `done`/tapStack drives closure)
    if (h.count("kind") && h.at("kind").toStr() == "signal") {
        std::vector<int> sigs;
        if (h.count("signals") && h.at("signals").arr())
            for (auto& n : *h.at("signals").arr()) sigs.push_back((int)n.toInt());
        return tapSignal(sigs, emitCb, doneCb, nullptr);
    }
    // Supply.interval(N) tapped directly (.tap, or inside a supply {…} block):
    // each tap gets its OWN ticker; the returned Tap's handle stops it on .close.
    if (h.count("kind") && h.at("kind").toStr() == "interval") {
        double iv = h.count("interval") ? h.at("interval").toNum() : 1;
        double dl = h.count("delay") ? h.at("delay").toNum() : 0;
        auto handle = std::make_shared<TapHandle>();
        std::shared_ptr<ReactCtx> rctx = reactStack_.empty() ? nullptr : reactStack_.back();
        return spawnIntervalWhenever(iv, dl, emitCb, rctx, handle);
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
        auto fin = std::make_shared<std::atomic<bool>>(false);
        auto spawnScope = tctx_.cur ? tctx_.cur : global_;
        Interpreter* self = this;
        throttleSpawn();
        addWorker(BigStackThread([self, lfd, emitCb, handle, fin, spawnScope]() mutable {
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
        }), fin);
        Value t = Value::makeHash(); t.hashKind = "Tap"; t.extM() = handle;
        (*t.hash())["wired"] = Value::boolean(true);
        return t;
    }
    // 4) async read: a worker recv()s and emits Blob chunks; EOF fires done.
    if (h.count("kind") && h.at("kind").toStr() == "async-read") {
        Value sock = h.at("socket");
        int fd = (sock.t == VT::Hash && sock.hash()->count("fd")) ? (int)(*sock.hash())["fd"].toInt() : -1;
        engageGil();
        auto handle = std::make_shared<TapHandle>();
        handle->closers.push_back([fd] { if (fd >= 0) ::shutdown(fd, SHUT_RD); });
        liveWorkers_++;
        auto fin = std::make_shared<std::atomic<bool>>(false);
        auto spawnScope = tctx_.cur ? tctx_.cur : global_;
        bool bin = h.count("bin") && h.at("bin").truthy();
        Interpreter* self = this;
        throttleSpawn();
        addWorker(BigStackThread([self, fd, emitCb, doneCb, handle, fin, spawnScope, bin]() mutable {
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
        }), fin);
        Value t = Value::makeHash(); t.hashKind = "Tap"; t.extM() = handle;
        (*t.hash())["wired"] = Value::boolean(true);
        return t;
    }
    // 5) values-backed: eager push-through, then done (or quit)
    if (h.count("values")) {
        if (emitCb.t == VT::Code) for (auto& v : *h.at("values").arr()) {
            ValueList one{v};
            try { callCallable(emitCb, one); }
            catch (NextEx&) {}
            catch (LastEx&) { break; }
            catch (DoneEx&) { break; }
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
        std::string why = x.pairVal() ? x.pairVal()->toStr() : "";
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
    if (v.t == VT::Object && v.obj()) {
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
    long long cp = v.big() ? LLONG_MAX : v.toInt();
    if (cp < 0 || cp > 0x10FFFF)
        throw RakuError{Value::typeObj("X::AdHoc"),
            "chr codepoint " + (v.big() ? v.big()->toString() : std::to_string(cp)) + " is out of bounds"};
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
Value rtBChars(Interpreter&, const Value& v) {
    // Straight off the cache for a plain Str: `.chars` in a scanning loop was
    // the other per-character O(n).
    if (v.t == VT::Str) return Value::integer(cowGraphemeCount(v.s));
    return Value::integer(graphemeCount(v.toStr()));
}
Value rtBSqrt(Interpreter& I, const Value& v) {
    if (v.t == VT::Complex) return complexSqrt(v.n, v.im());
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

// The logical working-directory name after entering `p` from `base`: purely
// textual, matching Rakudo's $*CWD — symlinks stay as the program spelled them
// (the real chdir has already validated the target), `.` and `..` collapse.
static std::string logicalJoin(const std::string& base, const std::string& p) {
    std::string full = (!p.empty() && p[0] == '/') ? p : base + "/" + p;
    std::vector<std::string> keep;
    std::string cur;
    auto flush = [&] {
        if (cur.empty() || cur == ".") { cur.clear(); return; }
        if (cur == "..") { if (!keep.empty()) keep.pop_back(); }
        else keep.push_back(cur);
        cur.clear();
    };
    for (char c : full) { if (c == '/') flush(); else cur += c; }
    flush();
    std::string out;
    for (auto& s : keep) out += "/" + s;
    return out.empty() ? "/" : out;
}

// A relative IO::Path belongs to its own captured :CWD, and file operations
// must resolve against it: `IO::Path.new('x.txt', :CWD($dir)).e` used to stat
// x.txt wherever the PROCESS happened to stand (TAP::Harness runs whole suites
// through exactly that shape — SourceHandler dies "Failed to open file" on
// every .tap source given a :cwd). When the base IS the current directory —
// the overwhelmingly common case — keep the user's own spelling, so error
// texts and dir listings read the way they were written.
std::string Interpreter::ioFsPath(const Value& v) {
    if (v.hashKind != "IO" || v.t != VT::Str) return v.toStr();
    const std::string& p = v.s;
    if (p.empty() || p[0] == '/') return p;
    const std::string& base = v.ofType();
    if (base.empty() || base == cwdName()) return p;
    return logicalJoin(base, p);
}

void Interpreter::registerBuiltins() {
    auto& B = builtins_;

    // Native extension loading (include/rakupp/rakupp_ext.h). A BUILTIN rather than
    // something `use Rakupp::Ext` installs, because a module that wants a
    // compiled fast path with a portable fallback has to ask for it WITHOUT
    // writing anything Rakudo cannot compile:
    //
    //     my &load = try &::('rakupp-ext-load');   # Nil on Rakudo, sub here
    //
    // `&::(…)` is a runtime lookup, so that line compiles on both engines and
    // the module degrades to its pure-Raku path everywhere else. Reachable via
    // `use Rakupp::Ext` too, which is the discoverable spelling for code that is
    // rakupp-only by design.
    // MODULES-PLAN M6: REAL advisory locking on the shared CURI store's
    // SHA-1 of a STRING, for the installer's short/ index keys. The tool used
    // to spell this "write a temp file, spawn shasum, read a line" — one
    // subprocess per provided module and file, which turned `rakupp uninstall
    // fez` into forty seconds of spawning (~70 keys) and read as a hang.
    // The engine's own sha1hex (the CURI content addressing) answers in place.
    B["rakupp-sha1-hex"] = [](Interpreter&, ValueList& a) -> Value {
        return Value::str(a.empty() ? sha1hex("") : sha1hex(a[0].toStr()));
    };
    // repo.lock — the store is also zef's and Rakudo's, and a writer that
    // ignores the lock can corrupt it under a concurrent zef. IO::Handle
    // .lock is a stub here (buffered handles carry no live fd), so the
    // installer takes the lock through these instead. POSIX flock; on
    // Windows the installer proceeds unlocked (cross-tool locking there is
    // out of scope, and saying so beats pretending).
    B["rakupp-repo-lock"] = [](Interpreter&, ValueList& a) -> Value {
#ifndef _WIN32
        if (a.empty()) return Value::integer(-1);
        int fd = ::open(a[0].toStr().c_str(), O_CREAT | O_RDWR, 0666);
        if (fd < 0) return Value::integer(-1);
        if (::flock(fd, LOCK_EX) != 0) { ::close(fd); return Value::integer(-1); }
        return Value::integer(fd);
#else
        return Value::integer(-1);
#endif
    };
    B["rakupp-repo-unlock"] = [](Interpreter&, ValueList& a) -> Value {
#ifndef _WIN32
        if (!a.empty()) {
            int fd = (int)a[0].toInt();
            if (fd >= 0) { ::flock(fd, LOCK_UN); ::close(fd); }
        }
#endif
        return Value::boolean(true);
    };
    // G1 (GRAMMAR-PLAN): after a FAILED .parse/.subparse on this thread,
    // answers { pos => Int (characters), rule => Str } — the furthest point a
    // named rule failed at, and which rule. Any after a success. A rakupp
    // extension (Rakudo grammars have no diagnostics API); reach it portably
    // with the same `try &::('rakupp-parse-diagnosis')` idiom as ext-load.
    B["rakupp-parse-diagnosis"] = [](Interpreter&, ValueList&) -> Value {
        auto& d = grammarParseDiag();
        if (!d.valid) return Value::any();
        Value h = Value::makeHash();
        (*h.hash())["pos"] = Value::integer(d.pos);
        (*h.hash())["rule"] = Value::str(d.rule);
        return h;
    };
    B["rakupp-ext-load"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.empty()) return Value::boolean(false);
        std::string err;
        std::vector<std::pair<std::string, Value>> subs;
        Value ok = extLoadModule(a[0].toStr(), err, subs);
        if (!err.empty()) throw RakuError{Value::typeObj("X::AdHoc"), err};
        for (auto& s : subs) I.tctx_.cur->define("&" + s.first, s.second);
        return ok;
    };

    // `trait_mod:<of>($routine, Type)` — the return-type trait, spelled as a CALL.
    // rakupp does not constrain a routine by its return type, so the engine has
    // nothing to record; what matters is that the call SUCCEEDS. A user
    // `trait_mod:<is>` that delegates to it first (Path::Finder's `is constraint`
    // opens with `trait_mod:<of>($method, Path::Finder:D)`) otherwise died on the
    // very first line, and the trait dispatcher — which reads a throw as "not this
    // handler's trait" — dropped the whole trait silently.
    B["trait_mod:<of>"] = [](Interpreter&, ValueList& a) -> Value {
        return a.empty() ? Value::any() : a[0];
    };
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
            if (j.t == VT::Array && j.arr() &&
                (j.enumName == "any" || j.enumName == "all" || j.enumName == "one" || j.enumName == "none")) {
                for (auto& e : *j.arr()) { ValueList a2 = a; a2[i] = e; I.callBuiltin("put", a2); }
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
        std::string msg;
        if (a.empty()) msg = "Warning: something's wrong";
        else for (auto& v : a) msg += I.gistOf(v);
        // a dynamically-enclosing CONTROL {} sees the CX::Warn first; if it
        // .resume's, the warning is handled and nothing prints
        if (I.runControlWarn(msg)) return Value::boolean(true);
        // …and it goes through `$*ERR`, exactly as `note` does — writing to
        // std::cerr directly walked past a dynamically-overridden handle, so a
        // module that redirects $*ERR to capture output (silently, Trap) saw
        // every `note` and no `warn`.
        // …and where it was warned FROM. Rakudo prints the innermost frame only,
        // which is the right amount for a warning: the reader wants the line to
        // go look at, not the whole chain. RAKUPP_BACKTRACE=full gives the rest.
        return I.ioEmit(msg + "\n" + I.warnFrame(), "$*ERR", true);
    };
    B["die"] = [](Interpreter& I, ValueList& a) -> Value {
        Value payload = a.empty() ? Value::str("Died") : a[0];
        // die with no argument reuses the current $! ("Died" only if $! is undefined)
        if (a.empty()) { Value* be = I.tctx_.cur->find("$!"); if (be && be->t != VT::Nil && be->t != VT::Type) payload = *be; }
        std::string msg = payload.toStr();
        // exception objects: prefer a readable .message / .Str accessor
        if (payload.t == VT::Object && payload.obj()) {
            for (const char* acc : {"message", "Str"}) {
                try { ValueList none; Value m = I.methodCall(payload, acc, none);
                      if (m.t == VT::Str && !m.s.empty()) { msg = m.s; break; } } catch (...) {}
            }
        } else {
            // wrap a plain string/number into an X::AdHoc exception (so .message/.^name work in CATCH)
            auto it = I.classes_.find("X::AdHoc");
            if (it != I.classes_.end()) {
                Value ex; ex.t = VT::Object; ex.setObj(std::make_shared<ObjectData>());
                ex.obj()->cls = it->second;
                ex.obj()->attrs["message"] = Value::str(msg);
                ex.obj()->attrs["payload"] = a.empty() ? Value::str(msg) : a[0]; // .payload is what was thrown
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
    // …and with no frame at all there may still be a BUILT-IN under the running
    // method: `method clone { … callsame … }` overrides Mu.clone, and a user
    // method that overrides a built-in gets no redispatch frame (see
    // ExecContext::builtinFallback — one per method call would be too dear).
    // `skipOwn` re-enters the dispatch with the invocant's own methods bypassed,
    // which lands exactly on the built-in. The breadcrumb is cleared for the
    // duration, so a redispatch that somehow reaches the same method again
    // cannot loop on it. Returns nullopt when this activation has no built-in
    // behind it — then the no-dispatcher error stands, as before.
    auto builtinNext = [](Interpreter& I, const ValueList* with) -> std::optional<Value> {
        ExecContext::BuiltinFallback fb = I.tctx_.builtinFallback;
        if (!fb.name || !fb.self || !fb.args || fb.frame != I.tctx_.curRoutineFrame)
            return std::nullopt;
        struct Restore { ExecContext& t; ExecContext::BuiltinFallback s;
                         ~Restore() { t.builtinFallback = s; } } r{I.tctx_, fb};
        I.tctx_.builtinFallback = ExecContext::BuiltinFallback{};
        return I.methodCall(*fb.self, *fb.name, with ? *with : *fb.args,
                            nullptr, /*skipOwn=*/true);
    };
    B["lastcall"] = [dispTop](Interpreter& I, ValueList&) -> Value {
        if (auto* d = dispTop(I)) d->lastcall = true;
        return Value::boolean(true);
    };
    B["callsame"] = [dispTop, builtinNext](Interpreter& I, ValueList&) -> Value {
        auto* d = dispTop(I);
        if (!d) {
            if (auto b = builtinNext(I, nullptr)) return *b;
            if (I.redispatchStack_.empty()) I.throwTypedV("X::NoDispatcher", {{"redispatcher", Value::str("callsame")}},
                              "callsame is not in the dynamic scope of a dispatcher");
            return Value::nil(); // exhausted chain bottom
        }
        if (d->lastcall) return Value::nil(); // trimmed by lastcall
        return d->next(d->sameArgs);
    };
    B["callwith"] = [dispTop, builtinNext](Interpreter& I, ValueList& a) -> Value {
        auto* d = dispTop(I);
        if (!d) {
            if (auto b = builtinNext(I, &a)) return *b;
            if (I.redispatchStack_.empty()) I.throwTypedV("X::NoDispatcher", {{"redispatcher", Value::str("callwith")}},
                              "callwith is not in the dynamic scope of a dispatcher");
            return Value::nil();
        }
        if (d->lastcall) return Value::nil();
        return d->next(a);
    };
    B["nextsame"] = [dispTop, builtinNext](Interpreter& I, ValueList&) -> Value {
        auto* d = dispTop(I);
        if (!d) {
            if (auto b = builtinNext(I, nullptr)) throw ReturnEx{*b};
            if (I.redispatchStack_.empty()) I.throwTypedV("X::NoDispatcher", {{"redispatcher", Value::str("nextsame")}},
                              "nextsame is not in the dynamic scope of a dispatcher");
            throw ReturnEx{Value::nil()};
        }
        if (d->lastcall) throw ReturnEx{Value::nil()};
        throw ReturnEx{d->next(d->sameArgs)};
    };
    // `nextcallee` — the next-less-specific candidate as a Callable, WITHOUT
    // calling it. `my &orig = nextcallee; orig(self, |c)` is how a `.wrap`
    // wrapper reaches the routine it wrapped (Method::Protected's lock wrapper).
    B["nextcallee"] = [dispTop, builtinNext](Interpreter& I, ValueList&) -> Value {
        auto* d = dispTop(I);
        std::function<Value(ValueList)> nextFn;
        if (d && !d->lastcall) nextFn = d->next;
        else if (auto b = builtinNext(I, nullptr)) { Value bv = *b; nextFn = [bv](ValueList) { return bv; }; }
        else {
            if (I.redispatchStack_.empty())
                I.throwTypedV("X::NoDispatcher", {{"redispatcher", Value::str("nextcallee")}},
                              "nextcallee is not in the dynamic scope of a dispatcher");
            return Value::nil();
        }
        // the returned Callable runs the captured next candidate with WHATEVER
        // args it is handed (the wrapper passes `self, |c` back through)
        return Value::closure([nextFn](ValueList& a) -> Value {
            ValueList copy = a; return nextFn(std::move(copy));
        });
    };
    B["samewith"] = [dispTop](Interpreter& I, ValueList& a) -> Value {
        // re-dispatch the CURRENT routine from scratch with new args, returning its result
        auto* d = dispTop(I);
        if (!d || !d->restart)
            I.throwTypedV("X::NoDispatcher", {{"redispatcher", Value::str("samewith")}},
                              "samewith is not in the dynamic scope of a dispatcher");
        return d->restart(a);
    };
    B["nextwith"] = [dispTop, builtinNext](Interpreter& I, ValueList& a) -> Value {
        auto* d = dispTop(I);
        if (!d) {
            if (auto b = builtinNext(I, &a)) throw ReturnEx{*b};
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
                ex.t = VT::Object; ex.setObj(std::make_shared<ObjectData>()); ex.obj()->cls = it->second;
                ex.obj()->attrs["message"] = Value::str(a[0].toStr());
                // `fail %h` keeps the value as the exception's PAYLOAD, as Rakudo's
                // X::AdHoc does (Text::SubParsers reports a failed parse that way)
                ex.obj()->attrs["payload"] = a[0];
            } else ex = Value::str(a[0].toStr());
        } else {
            Value* be = I.tctx_.cur->find("$!");
            if (be && be->t != VT::Nil && be->t != VT::Type) ex = *be;
        }
        // a bare `fail` with no $! still carries an exception — X::AdHoc
        // "Failed" — so `.exception.message` answers rather than dying on Any
        if (ex.t != VT::Object)
            ex = I.makeTypedEx("X::AdHoc", {}, ex.t == VT::Str ? ex.s.str() : std::string("Failed"));
        Value f = rakuppNewFailure();
        (*f.hash())["exception"] = ex;
        (*f.hash())["message"] = ex.obj() && ex.obj()->attrs.count("message")
                             ? ex.obj()->attrs["message"] : Value::str("Failed");
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
    B["dd"] = [](Interpreter& I, ValueList& a) -> Value {
        // dd renders .raku, not .gist — and it must DISPATCH, so a type with a
        // .raku of its own (a user class, $*RAKU) shows that rather than a
        // generic rendering. Plain Str keeps its quoted short form.
        std::string out;
        for (size_t i = 0; i < a.size(); i++) {
            if (i) out += ", ";
            if (a[i].t == VT::Str && a[i].hashKind.empty()) { out += "\"" + a[i].s + "\""; continue; }
            ValueList none;
            out += I.methodCall(a[i], "raku", none).toStr();
        }
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
            if (a.size() == 1 && a[0].t == VT::Array && a[0].arr())
                for (auto& x : *a[0].arr()) j.arr()->push_back(x);
            else if (a.size() == 1 && a[0].t == VT::Range)
                for (auto& x : a[0].flatten()) j.arr()->push_back(x);
            else
                for (auto& v : a) j.arr()->push_back(v);
            return j;
        };
    }

    // --- Test module ---
    B["plan"] = [](Interpreter& I, ValueList& a) -> Value {
        I.usedTest_ = true;
        // plan skip-all => "reason" : emit an empty SKIP plan and exit the test file
        bool skipAll = false; std::string reason;
        for (auto& x : a) {
            if (x.t == VT::Pair && x.s == "skip-all") { skipAll = true; reason = x.pairVal() ? x.pairVal()->toStr() : ""; }
            else if (x.t == VT::Str && x.s == "skip-all") skipAll = true;
        }
        if (skipAll) { I.planned_ = 0; std::cout << "1..0 # SKIP " << reason << "\n" << std::flush; throw ExitEx{0}; }
        // `plan *` means "no plan" — the count comes from done-testing, and nothing
        // is printed up front (File::Which's suite opens with it)
        if (!a.empty() && a[0].t == VT::Whatever) return Value::boolean(true);
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
            if (g.t == VT::Array && e.t == VT::Array && g.arr() && e.arr() &&
                g.arr()->size() == e.arr()->size() && !g.arr()->empty()) {
                bool all = true;
                for (size_t i = 0; i < g.arr()->size() && all; i++) {
                    const Value& gi = (*g.arr())[i];
                    const Value& ei = (*e.arr())[i];
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
        if (exp.t == VT::Array && exp.arr() &&
            (exp.enumName == "any" || exp.enumName == "all" || exp.enumName == "one" || exp.enumName == "none")) {
            int t = 0, total = (int)exp.arr()->size();
            for (auto& br : *exp.arr()) if (scalarEq(got, br)) t++;
            return exp.enumName == "any" ? t > 0 : exp.enumName == "all" ? t == total
                 : exp.enumName == "one" ? t == 1 : t == 0;
        }
        // …and a junction GOT autothreads the same way: `is any(@names), 'a'`
        // collapses per the junction's kind (HTTP::UserAgent's header tests)
        if (got.t == VT::Array && got.arr() &&
            (got.enumName == "any" || got.enumName == "all" || got.enumName == "one" || got.enumName == "none")) {
            int t = 0, total = (int)got.arr()->size();
            for (auto& br : *got.arr()) if (scalarEq(br, exp)) t++;
            return got.enumName == "any" ? t > 0 : got.enumName == "all" ? t == total
                 : got.enumName == "one" ? t == 1 : t == 0;
        }
        return scalarEq(got, exp);
    };
    // An object argument (e.g. an exception in `is $!, 'msg'`) compares by its Str —
    // which for an Exception is its .message, matching `~$!` (via strOf).
    // …and a LIST of objects compares by the same rule, element by element:
    // `is $elem.contents, 'text'` where .contents is a list of XML::Text nodes.
    auto isStrify = [](Interpreter& I, Value& v) {
        // …a Proxy too: it is a container, and `is` compares the value it holds.
        auto proxyish = [](const Value& e) {
            return e.t == VT::Hash && e.hashKind == "Proxy" && e.hash();
        };
        if (v.t == VT::Object || proxyish(v)) { v = Value::str(I.strOf(v)); return; }
        if (v.t == VT::Array && v.arr() && v.enumName.empty())
            for (auto& e : *v.arr())
                if (e.t == VT::Object || proxyish(e)) { v = Value::str(I.strOf(v)); return; }
    };
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
        I.emitTest(c, testDesc(a, 2), testDirective(a)); // adverbs are not the description
        return Value::boolean(c);
    };
    auto likeTest = [](Interpreter& I, ValueList& a, bool want) -> Value {
        // `like` stringifies its subject: a non-Str (an Int, or an object with a
        // .Str) is matched by what it stringifies TO, not by an empty string
        std::string got = a.empty() ? "" : I.strOf(a[0]);
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
        // is-deeply IS `eqv` — type-strict — and not the looser structural
        // compare. Rakudo's Test.rakumod computes `$got eqv $expected`, so
        // `is-deeply "11", 11` FAILS there; routing this through deepEq made it
        // pass here, which is the dangerous shape: a suite that goes green
        // without agreeing on a single type. The one adjustment Test.rakumod
        // makes is its Seq candidates, which `.cache` a Seq operand into a List
        // before comparing (`(1,2).Seq eqv (1,2)` is False, but
        // `is-deeply (1,2).Seq, (1,2)` passes).
        auto cached = [](const Value& v) -> Value {
            if (v.t == VT::Array && v.isList && v.s == "Seq") {
                forceLazy(v);
                Value c = v; c.s = ""; // Seq.cache is a List
                return c;
            }
            return v;
        };
        // applyArith, not valueEqv directly: `eqv` autothreads a Junction
        // operand there, and `is-deeply 1, 1|2` relies on it.
        bool c = a.size() >= 2 && applyArith("eqv", cached(a[0]), cached(a[1])).truthy();
        I.emitTest(c, testDesc(a, 2), testDirective(a)); // adverbs are not the description
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
            // the numeric fast path only fits actual numbers — a Version (or any
            // tagged value) must go through the real operator (`cmp-ok $v, '>',
            // v0.0.0` flattened both sides to 0 and failed; Log::Async's suite)
            bool bothNum = x.isNumeric() && y.isNumeric();
            if (bothNum && op == "==") c = x.toNum() == y.toNum();
            else if (bothNum && op == "!=") c = x.toNum() != y.toNum();
            else if (bothNum && op == "<") c = x.toNum() < y.toNum();
            else if (bothNum && op == ">") c = x.toNum() > y.toNum();
            else if (bothNum && op == "<=") c = x.toNum() <= y.toNum();
            else if (bothNum && op == ">=") c = x.toNum() >= y.toNum();
            else if (op == "eq") c = x.toStr() == y.toStr();
            else if (op == "ne") c = x.toStr() != y.toStr();
            // `~~` is the FULL matcher: a block on the right is CALLED with the
            // value (`cmp-ok $out, '~~', { .contains: "FOO" & "bar" }` — Roast's
            // Test::Util run-with-tty judges a child's STDOUT that way), a regex
            // is matched, a junction of matchers threaded, an ACCEPTS object
            // asked. applyArith knows none of that and answered False for every
            // block, so S32-io/out-buffering.t's "prompt does not hang" failed
            // with both expected words sitting in the captured output.
            else if (op == "~~")  c = matcherAccepts(I, x, y);
            else if (op == "!~~") c = !matcherAccepts(I, x, y);
            else c = applyArith(op, x, y).truthy(); // ===, eqv, before/after, user ops…
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
        int callLine = I.testLine(); // the block runs first; report OUR line, not its
        bool died = false;
        if (!a.empty() && a[0].t == VT::Code) {
            // the block's value is SUNK, and sinking an unhandled Failure throws it —
            // `dies-ok { $c.to-string('bogus') }` over a routine that `fail`s (Color)
            try { Value r = I.callCallable(a[0], {});
                  if (r.t == VT::Hash && r.hashKind == "Failure") died = true; }
            catch (RakuError&) { died = true; }
            // a loop-control exception with no enclosing loop is a death (X::ControlFlow)
            catch (NextEx&) { died = true; }
            catch (LastEx&) { died = true; }
            catch (RedoEx&) { died = true; }
        }
        I.restoreTestLine(callLine);
        I.emitTest(died, a.size() > 1 ? a[1].toStr() : "");
        return Value::boolean(died);
    };
    B["lives-ok"] = [](Interpreter& I, ValueList& a) -> Value {
        int callLine = I.testLine();
        bool lived = true;
        if (!a.empty() && a[0].t == VT::Code) {
            try { Value r = I.callCallable(a[0], {});
                  if (r.t == VT::Hash && r.hashKind == "Failure") lived = false; } // sunk Failure throws
            catch (RakuError&) { lived = false; }
        }
        I.restoreTestLine(callLine);
        I.emitTest(lived, a.size() > 1 ? a[1].toStr() : "");
        return Value::boolean(lived);
    };
    B["use-ok"] = [](Interpreter& I, ValueList& a) -> Value {
        int callLine = I.testLine();
        std::string mod = a.empty() ? "" : a[0].toStr();
        // The argument is a `use` STATEMENT's module spec, adverbs and all:
        // `use-ok 'NativeLibs:v<0.0.9>'` (NativeLibs' own suite). loadModule takes
        // the bare name plus a version REQUIREMENT, so split them here — the whole
        // string named no module at all and every such use-ok reported a failure.
        std::string bare = mod, verReq;
        for (size_t i = 0; i + 1 < mod.size(); i++) {
            if (mod[i] != ':' || (i && mod[i - 1] == ':')) continue;
            size_t lt = mod.find('<', i);
            size_t gt = lt == std::string::npos ? std::string::npos : mod.find('>', lt);
            if (lt == std::string::npos || gt == std::string::npos) break;
            std::string adv = mod.substr(i + 1, lt - i - 1);
            if (adv == "ver" || adv == "v") verReq = mod.substr(lt + 1, gt - lt - 1);
            if (adv == "ver" || adv == "v" || adv == "auth" || adv == "api") {
                if (bare.size() > i) bare = mod.substr(0, i);
                i = gt;
            }
            else break;
        }
        bool ok = true;
        try { I.loadModule(bare, {}, /*doImport=*/true, /*quiet=*/false, verReq); } catch (...) { ok = false; }
        I.restoreTestLine(callLine);
        I.emitTest(ok, a.size() > 1 ? a[1].toStr() : ("The module can be use-d ok: " + mod));
        return Value::boolean(ok);
    };
    B["can-ok"] = [](Interpreter& I, ValueList& a) -> Value {
        // can-ok($obj, 'method', $desc?) — the default description names the type
        // and the method, the way Rakudo's Test does
        bool c = false;
        std::string meth = a.size() > 1 ? a[1].toStr() : "";
        if (a.size() >= 2) c = I.methodCall(a[0], "can", ValueList{Value::str(meth)}).truthy();
        std::string desc = a.size() > 2 ? a[2].toStr()
                                        : "An object of type '" +
                                          (a.empty() ? std::string() : a[0].typeName()) +
                                          "' can do the method '" + meth + "'";
        I.emitTest(c, desc);
        return Value::boolean(c);
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
        // the real .isa knows allomorphs and user-class chains — consult it first,
        // then .does, because that is what Rakudo's isa-ok is: a ROLE is never
        // something you `.isa`, so `isa-ok $obj, SomeRole` (and `isa-ok $buf,
        // buf8`, buf8 being a curried role there) would fail on both engines
        // otherwise. It stops short of full smartmatch — `isa-ok 5, 1..10` and
        // `isa-ok "abc", /b/` fail on Rakudo, and .does keeps them failing.
        if (a.size() > 1) {
            for (const char* probe : {"isa", "does"}) {
                ValueList ia{a[1]};
                Value r = I.methodCall(a[0], probe, ia);
                if (r.truthy()) {
                    I.emitTest(true, desc);
                    return Value::boolean(true);
                }
            }
            // a SUBSET target passes for a value the subset accepts
            // (`isa-ok 5, UInt` — Date::Event walks its enum map this way);
            // still short of smartmatch: the target must be a TYPE object
            if (a[1].t == VT::Type &&
                (a[1].s == "UInt" ? a[0].t == VT::Int && !(a[0].big() ? a[0].big()->sign < 0 : a[0].i < 0)
                                  : I.subsetMatches(a[1].s, a[0]))) {
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
            {"Pod::FormattingCode", {"Pod::FormattingCode", "Pod::Block", "Any", "Mu"}},
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
        auto im = [](const Value& v) { return v.t == VT::Complex ? v.im() : 0.0; };
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
                std::string k = a[i].s; double val = a[i].pairVal() ? a[i].pairVal()->toNum() : 0;
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
        ValueList matchers;
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
                    !(r.hash()->count("handled") && (*r.hash())["handled"].truthy())) {
                    Value ex = r.hash()->count("exception") ? (*r.hash())["exception"] : Value::any();
                    failed = true;
                    if (a.size() > 1 && a[1].t == VT::Type && a[1].s != "Exception")
                        failed = applyArith("~~", ex, a[1]).truthy();
                    for (auto& mp : matchers) {
                        if (!failed) break;
                        Value want = mp.pairVal() ? *mp.pairVal() : Value::boolean(true);
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
                std::string lang = v.pairVal() ? v.pairVal()->toStr() : "";
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
    // EVALFILE($path, :$lang) — Rakudo's is `EVAL slurp($filename), :$lang`,
    // so the file is read first (a missing one dies before the language is
    // looked at) and every adverb rides through to EVAL unchanged.
    B["EVALFILE"] = [](Interpreter& I, ValueList& a) -> Value {
        std::string path; bool havePath = false;
        ValueList fwd;
        for (auto& v : a) {
            if (v.t == VT::Pair && v.namedArg) { fwd.push_back(v); continue; }
            if (!havePath) { path = I.ioFsPath(v); havePath = true; }
        }
        if (!havePath) return Value::any();
        rejectNulPath(path);
        std::ifstream in(path);
        if (!in) throwFailedOpen(path);
        std::ostringstream ss; ss << in.rdbuf();
        std::string text = ss.str();
        if (text.find('\r') != std::string::npos) {   // same CRLF -> LF as slurp
            std::string outT; outT.reserve(text.size());
            for (size_t i = 0; i < text.size(); i++) {
                if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') continue;
                outT += text[i];
            }
            text.swap(outT);
        }
        ValueList args; args.push_back(Value::str(text));
        for (auto& n : fwd) args.push_back(n);
        // while this file's top level runs it IS the file being compiled: a
        // routine declared in it records the path in its .declFile, and a
        // backtrace taken from its mainline names the path rather than the
        // program that EVALFILE'd it (S29-context/evalfile.t asserts exactly
        // that). The path goes in AS GIVEN — Rakudo's backtrace does not
        // absolutise it. Same RAII switch a module load makes.
        struct DFGuard { std::string& f; std::string s; ~DFGuard() { f = s; } }
            dfG{I.curDeclFile_, I.curDeclFile_};
        I.curDeclFile_ = path;
        return I.callBuiltin("EVAL", args);
    };
    // Default @*ARGS -> argument-list conversion (the built-in ARGS-TO-CAPTURE).
    // main-refactored.t adjudicates the rules; see rakuppMainCapture below.
    B["RUN-MAIN-args-to-capture"] = [](Interpreter& I, ValueList& a) -> Value {
        // The DEFAULT ARGS-TO-CAPTURE: parse the live @*ARGS per the CLI rules
        // main-refactored.t adjudicates. Rakudo drives the real command line
        // and an explicit RUN-MAIN through the same default-args-to-capture,
        // so this delegates to the same rtMainArgs as the interpreter's MAIN
        // auto-invoke (--name / --name=v / -n=v / :n=v named, repeats collect,
        // values AND positionals val()-allomorphed, `--` consumed with the
        // rest positional, --/name negates, the named-anywhere opt).
        (void)a;
        auto dynFind = [&](const char* n) -> Value* {
            if (Value* p = I.tctx_.cur->find(n)) return p;
            for (auto it = I.tctx_.dynStack.rbegin(); it != I.tctx_.dynStack.rend(); ++it)
                if (*it) if (Value* p = (*it)->find(n)) return p;
            return nullptr;
        };
        std::vector<std::string> argv;
        if (Value* av = dynFind("@*ARGS"))
            if (av->t == VT::Array && av->arr())
                for (auto& x : *av->arr()) argv.push_back(x.toStr());
        bool namedAnywhere = false;
        if (Value* smo = dynFind("%*SUB-MAIN-OPTS"))
            if (smo->t == VT::Hash && smo->hash()) {
                auto it = smo->hash()->find("named-anywhere");
                namedAnywhere = it != smo->hash()->end() && it->second.truthy();
            }
        ValueList margs = rtMainArgs(argv, namedAnywhere);
        Value cap = Value::array(); cap.hashKind = "Capture"; *cap.arr() = std::move(margs);
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
            if (av->t == VT::Array && av->arr()) *argsArr.arr() = *av->arr();

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
                if (cap.t == VT::Array && cap.arr()) {
                    margs = *cap.arr();                       // a Capture IS the arg list…
                    for (auto& m : margs)                   // …its Pairs are NAMED args
                        if (m.t == VT::Pair) m.namedArg = true;
                } else if (cap.t != VT::Any && cap.t != VT::Nil) margs.push_back(cap);
            } else {
                Value cap = I.callBuiltin("RUN-MAIN-args-to-capture", ValueList{});
                if (cap.t == VT::Array && cap.arr()) margs = *cap.arr();
            }
        }
        // did the user ask for help? (drives the exit code)
        bool wantsHelp = false;
        for (auto& m : margs) if (m.t == VT::Pair && m.s == "help" && m.namedArg &&
                                  (!m.pairVal() || m.pairVal()->truthy())) wantsHelp = true;

        // --- 2. dispatch --------------------------------------------------
        bool matches = true;
        if (mainSub.code() && mainSub.code()->isMultiDispatcher) {
            matches = false;
            for (auto& cand : mainSub.code()->candidates)
                if (I.scoreCandidate(cand, margs) >= 0) { matches = true; break; }
        } else if (mainSub.code() && mainSub.code()->params) {
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
        int inFd = -1; bool haveInHandle = false; // `:in($handle)`: the child's stdin itself
        std::vector<std::string> envKV; bool haveEnv = false; std::string cwd;
        double timeoutSec = 0;
        // `:out($fh)` / `:err($fh)` — not a flag but a SINK: Rakudo sends the
        // child's stream to that handle. Read as a mere boolean it captured the
        // output and dropped it on the floor, which is how HTTP::Tinyish::Curl
        // (`run |@cmd, :out($out-fh)`, then slurp the file) fetched every page
        // as an empty body while its headers arrived intact.
        Value outSink, errSink;
        // A default-constructed Value is Any, not Nil — "was a sink given?" needs
        // its own flag, and testing `.t == VT::Nil` for it (as this did) answered
        // "yes" for every un-adverbed run.
        bool haveOutSink = false, haveErrSink = false;
        auto asSink = [](const Value* pv) {
            return pv && ((pv->t == VT::Hash && pv->hashKind == "FileHandle") || pv->t == VT::Object);
        };
        for (auto& v : flattenArgs(a)) {
            if (v.t == VT::Pair) {
                if (v.s == "out") { wantOut = v.pairVal() ? v.pairVal()->truthy() : true; outMode = wantOut ? 1 : 0;
                                    if (asSink(v.pairVal())) { outSink = *v.pairVal(); haveOutSink = true; } }
                else if (v.s == "err") { wantErr = v.pairVal() ? v.pairVal()->truthy() : true; errMode = wantErr ? 1 : 0;
                                    if (asSink(v.pairVal())) { errSink = *v.pairVal(); haveErrSink = true; } }
                else if (v.s == "in") {
                    // a HANDLE is the child's stdin (stdinFdForHandle); a Bool
                    // keeps the deferred piped mode below
                    bool resolved = false;
                    int fd = v.pairVal() ? stdinFdForHandle(*v.pairVal(), resolved) : -1;
                    if (resolved) { inFd = fd; haveInHandle = true; }
                    else wantIn = v.pairVal() ? v.pairVal()->truthy() : true;
                }
                // :timeout(N) — a rakupp extension (Rakudo's run has no such
                // adverb): SIGKILL the child's process group after N seconds.
                // Advertised in this builtin's comment since the initial
                // commit, but never actually parsed — every caller silently
                // ran unbounded, which the stress suite discovered when a
                // livelocked child pinned the machine for 25 minutes.
                else if (v.s == "timeout" && v.pairVal()) timeoutSec = v.pairVal()->toNum();
                else if (v.s == "env" && v.pairVal() && v.pairVal()->t == VT::Hash && v.pairVal()->hash()) {
                    // :env(%h) — the child's ENTIRE environment (Rakudo semantics).
                    // Silently ignored before: run(..., :env(%(%*ENV, RAKULIB =>
                    // ...))) inherited the parent env unchanged.
                    haveEnv = true;
                    for (auto& kv : *v.pairVal()->hash()) envKV.push_back(kv.first + "=" + kv.second.toStr());
                    std::sort(envKV.begin(), envKV.end()); // deterministic; Windows wants sorted blocks
                }
                else if (v.s == "cwd" && v.pairVal()) cwd = v.pairVal()->toStr(); // was silently ignored too
            }
            else argv.push_back(v.toStr());
        }
        Value av = Value::array(); av.isList = true; for (auto& s : argv) av.arr()->push_back(Value::str(s));
        Value p = Value::makeHash(); p.hashKind = "Proc"; // standard Proc object
        (*p.hash())["argv"] = av; // for .command
        I.syncEnvToProcess(); // child inherits any %*ENV changes the program made
        if (wantIn && !haveInHandle) {
            // Defer spawning: the process runs when its stdin is written via
            // `.in.spurt(...)`, so we can feed input and capture output together.
            (*p.hash())["deferred"] = Value::boolean(true);
            if (haveEnv) { Value ev = Value::array(); for (auto& kv : envKV) ev.arr()->push_back(Value::str(kv)); (*p.hash())["env-kv"] = ev; }
            if (!cwd.empty()) (*p.hash())["cwd"] = Value::str(cwd);
            // The spawn happens later, in ProcIn's `print`/`close`, so what the
            // adverbs asked for has to travel with the Proc. Without them that
            // path captured stdout and sent stderr to /dev/null whatever was
            // written, so `run(cmd, :in, :out, :err)` read back an empty `.err`.
            (*p.hash())["out-mode"] = Value::integer(outMode);
            (*p.hash())["err-mode"] = Value::integer(errMode);
            (*p.hash())["out-str"] = Value::str("");
            (*p.hash())["err-str"] = Value::str("");
            (*p.hash())["exitcode"] = Value::integer(0);
            return p;
        }
        std::string out, err; int code; bool timedout;
        // :err captures; :!err captures-and-discards (so probes like
        // `zrun('git','--help', :!out, :!err)` stay silent); unspecified inherits.
        long long childPid = 0;
        // No `:out` (and no sink to fill): the child gets OUR stdout, so its
        // output appears as it is produced. Capturing it and echoing at exit —
        // what this used to do — made every streaming child silent until it
        // finished (issue #51: a runner relaying a build's progress).
        int outSpawn = (outMode == -1 && !haveOutSink) ? -1 : (outMode == 0 ? 0 : 1);
        spawnCapture(argv, timeoutSec, out, code, timedout, &I, errMode != -1 ? &err : nullptr, cwd, &childPid,
                     haveEnv ? &envKV : nullptr, errMode == -1, outSpawn, nullptr, nullptr, inFd);
#if !defined(_WIN32)
        if (inFd >= 0) ::close(inFd); // the child holds its own copy
#endif
        // Deliver a redirected stream to its handle. The child has already
        // finished, so this is a copy rather than a live redirection — the
        // handle sees the whole stream at once, in order, which is what a
        // caller that closes and reads the file afterwards wants.
        auto drainTo = [&I](Value& sink, bool have, const std::string& text) {
            if (!have || text.empty()) return;
            ValueList pa{Value::str(text)};
            I.methodCall(sink, "print", pa);
        };
        drainTo(outSink, haveOutSink, out);
        drainTo(errSink, haveErrSink, err);
        // Neither stream needs echoing any more: an un-adverbed child wrote to
        // our own descriptors while it ran.
        (*p.hash())["exitcode"] = Value::integer(code);
        (*p.hash())["out-str"] = Value::str(out);
        (*p.hash())["err-str"] = Value::str(err);
        (*p.hash())["timedout"] = Value::boolean(timedout); // the shape the comment above promises
        if (childPid) (*p.hash())["pid"] = Value::integer(childPid);
        return p;
    };
    // shell(CMD, :out, :err) — run CMD through the system shell (`/bin/sh -c CMD`),
    // so redirections/pipes in CMD work. Returns a Proc; +$proc is the exit status.
    B["shell"] = [](Interpreter& I, ValueList& a) -> Value {
        std::string cmd; bool wantOut = false, wantErr = false;
        int outMode = -1, errMode = -1; // -1 unspecified, 0 :!x discard, 1 :x capture
        int inFd = -1; // `:in($handle)`: the child's stdin itself (a Bool `:in` is not a shell() mode)
        for (auto& v : flattenArgs(a)) {
            if (v.t == VT::Pair) {
                if (v.s == "out") { wantOut = v.pairVal() ? v.pairVal()->truthy() : true; outMode = wantOut ? 1 : 0; }
                else if (v.s == "err") { wantErr = v.pairVal() ? v.pairVal()->truthy() : true; errMode = wantErr ? 1 : 0; }
                else if (v.s == "in" && v.pairVal()) { bool resolved = false; int fd = stdinFdForHandle(*v.pairVal(), resolved); if (resolved) inFd = fd; }
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
        spawnCapture(argv, 0, out, code, timedout, &I, errMode != -1 ? &err : nullptr, "", &childPid,
                     nullptr, errMode == -1, outMode, nullptr, nullptr, inFd);  // no `:out`: the child writes to ours, live
#if !defined(_WIN32)
        if (inFd >= 0) ::close(inFd);
#endif
        Value p = Value::makeHash(); p.hashKind = "Proc";
        Value av = Value::array(); av.isList = true; av.arr()->push_back(Value::str(cmd));
        (*p.hash())["argv"] = av; // .command — shell reports the command string
        (*p.hash())["exitcode"] = Value::integer(code);
        (*p.hash())["out-str"] = Value::str(out);
        (*p.hash())["err-str"] = Value::str(err);
        if (childPid) (*p.hash())["pid"] = Value::integer(childPid);
        return p;
    };
    // full-barrier — a sequentially-consistent memory fence (the ⚛ family's
    // companion; under the GIL it is trivially a no-op that must still exist)
    B["full-barrier"] = [](Interpreter&, ValueList&) -> Value {
        std::atomic_thread_fence(std::memory_order_seq_cst);
        return Value::nil();
    };
    B["make"] = [](Interpreter& I, ValueList& a) -> Value {
        Value v = a.empty() ? Value::any() : (a.size() == 1 ? a[0] : Value::array(a));
        if (!I.tctx_.makeTargets.empty()) I.tctx_.makeTargets.back()->setPairVal(std::make_shared<Value>(v));
        return v;
    };
    B["take"] = [](Interpreter& I, ValueList& a) -> Value {
        Value v = a.size() == 1 ? a[0] : Value::array(a);
        return I.gatherTake(a, v);
    };
    // take-rw is normally a special form (evalCall takes its argument by
    // EXPRESSION and proxies the storage it names — see evalTakeRw). This is
    // the indirect-call fallback (`&take-rw($x)`, .map(&take-rw)): the argument
    // arrives as a detached copy, so the best on offer is a fresh writable
    // cell — the sequence's element can still be assigned to, it just no
    // longer aliases the caller's variable.
    B["take-rw"] = [](Interpreter& I, ValueList& a) -> Value {
        Value proxy = I.makeCellProxy(a.empty() ? Value::any() : a[0]);
        ValueList one{proxy};
        return I.gatherTake(one, proxy);
    };
    // `succeed EXPR` exits the enclosing `when`/`given`, making the given evaluate to EXPR;
    // `proceed` leaves the current `when` but keeps testing later ones.
    B["succeed"] = [](Interpreter&, ValueList& a) -> Value {
        Value v = a.empty() ? Value::any() : (a.size() == 1 ? a[0] : Value::array(a));
        throw BreakGivenEx{v, !a.empty()};
    };
    B["proceed"] = [](Interpreter&, ValueList&) -> Value { throw ProceedEx{}; };
    // `sub dir(Cool $path = '.', Mu :$test)` — the path is the first POSITIONAL
    // and defaults to `.`. Issue #62: this took a[0] blindly, so a named-only
    // call `dir(test => /csv$/)` stringified the Pair, opendir failed on
    // "test\t…", and the answer was a silent empty list. The listing itself
    // lived here as a second, worse copy of IO::Path.dir (its own `.`/`..`
    // rule, `./x` entries, `ACCEPTS` on a Block died) — now the sub is the
    // method, as in Rakudo: `$path.IO.dir(:$test)`.
    B["dir"] = [](Interpreter& I, ValueList& a) -> Value {
        Value path; bool havePath = false;
        ValueList named;
        for (auto& x : a) {
            if (x.t == VT::Pair && x.namedArg) named.push_back(x);
            else if (!havePath) { path = x; havePath = true; }
        }
        Value io;
        if (havePath && path.hashKind == "IO") io = path; // an IO argument's own :CWD wins
        else {
            io = Value::str(havePath ? path.toStr() : "."); io.hashKind = "IO";
            io.ofTypeM() = I.cwdName();   // entries capture the CALL-TIME base, as Rakudo's do
        }
        return I.methodCall(io, "dir", named);
    };
    B["mkdir"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.empty()) return Value::boolean(false);
        std::string path = I.ioFsPath(a[0]);
        long long mode = 0777;   // mkdir($path, 0o700) — the sub's positional mode
        for (size_t i = 1; i < a.size(); i++) {
            if (a[i].t == VT::Pair && a[i].namedArg && a[i].s == "mode" && a[i].pairVal()) mode = a[i].pairVal()->toInt();
            else if (a[i].t == VT::Int) mode = a[i].toInt();
        }
        // mkdir -p, but HONEST about the outcome — the mirror of the method
        // arm in MethodCallPart3.cpp, which tells the story (issue #26): the
        // old form swallowed every error and answered success. Success is the
        // IO::Path (as Rakudo answers, not the Str this used to hand back);
        // failure is the soft X::IO::Mkdir Failure that detonates when sunk.
        std::string acc;
        int err = 0;
        for (size_t i = 0; i <= path.size(); i++) {
            if (i == path.size() || path[i] == '/') {
                if (!acc.empty() && ::mkdir(acc.c_str(), (int)mode) != 0 && errno != EEXIST) {
                    err = errno;
                    break;
                }
                if (i < path.size()) acc += '/';
            } else acc += path[i];
        }
        struct stat st;
        bool isDir = false;
        if (::stat(path.c_str(), &st) == 0) isDir = S_ISDIR(st.st_mode);
        else if (!err) err = errno;
        if (!isDir) {
            if (!err) err = EEXIST;   // the path exists, and is not a directory
            char ob[24]; snprintf(ob, sizeof ob, "0o%llo", (unsigned long long)mode);
            Value f = rakuppNewFailure();
            (*f.hash())["exception"] = Value::typeObj("X::IO::Mkdir");
            (*f.hash())["message"] = Value::str(
                "Failed to create directory '" + path + "' with mode '" + std::string(ob) +
                "': Failed to mkdir: " + std::strerror(err));
            return f;
        }
        Value p = Value::str(path); p.hashKind = "IO";
        p.ofTypeM() = I.cwdName();
        return p;
    };
    B["rmdir"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.empty()) return Value::boolean(false);
        return Value::boolean(::rmdir(I.ioFsPath(a[0]).c_str()) == 0);
    };
    B["spurt"] = [](Interpreter& I, ValueList& a) -> Value {
        if (!a.empty()) rejectNulPath(a[0].toStr());
        if (a.empty()) return Value::boolean(false);
        bool append = false, createonly = false;
        std::string content;
        bool haveContent = false;
        for (size_t i = 1; i < a.size(); i++) {
            if (a[i].t == VT::Pair && a[i].namedArg) {
                if (a[i].s == "append") append = a[i].pairVal() && a[i].pairVal()->truthy();
                else if (a[i].s == "createonly" || a[i].s == "x") createonly = a[i].pairVal() && a[i].pairVal()->truthy();
            }
            else if (!haveContent) { content = a[i].toStr(); haveContent = true; }
        }
        std::string path = I.ioFsPath(a[0]);
        if (createonly) { std::ifstream probe(path); if (probe) return Value::boolean(false); }
        content = I.encodeTextEnc(content, Interpreter::encAdverb(a)); // `:enc`, and binary — as the method form
        std::ofstream out(path, std::ios::binary | (append ? std::ios::app : std::ios::trunc));
        if (!out) return Value::boolean(false);
        out << content;
        return Value::boolean(true);
    };
    B["slurp"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.empty()) { std::ostringstream ss; ss << std::cin.rdbuf(); return Value::str(ss.str()); } // slurp() = $*IN.slurp
        // Delegate to the METHOD form: one reader, one rule set. The old copy
        // here opened in text mode with no :bin arm at all — its own comment
        // claimed ":bin routes to the method" while `slurp $p, :bin` returned
        // a CRLF-squeezed Str where `$p.IO.slurp(:bin)` returned the raw Blob.
        Value io = a[0];
        if (io.t != VT::Hash && io.hashKind != "IO") { // a path: dispatch as IO, not bare Str
            rejectNulPath(io.toStr());       // (a Str invocant must NOT slurp — see the method's guard)
            io = Value::str(io.toStr());     // an IO::Path passes through AS-IS: rebuilding
            io.hashKind = "IO";              // it here dropped the path's own :CWD base
        }
        ValueList rest(a.begin() + 1, a.end());
        return I.methodCall(io, "slurp", rest);
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
        if (argv.arr() && !argv.arr()->empty()) {
            for (auto& fn : *argv.arr()) {
                std::ifstream in(fn.toStr());
                while (std::getline(in, line)) { if (!line.empty() && line.back() == '\r') line.pop_back(); out.arr()->push_back(Value::str(line)); }
            }
            return out;
        }
        while (std::getline(std::cin, line)) { if (!line.empty() && line.back() == '\r') line.pop_back(); out.arr()->push_back(Value::str(line)); }
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
        while (ws >> w) out.arr()->push_back(Value::str(w));
        return out;
    };
    B["open"] = [](Interpreter& I, ValueList& a) -> Value { // sub form: open($path, :r/:w/:a)
        // the path is the first POSITIONAL — `open :w, $path` puts the adverb first,
        // and taking args[0] blindly opened a file literally named "w\tTrue"
        std::string path;
        for (auto& x : a) if (x.t != VT::Pair) { path = I.ioFsPath(x); break; }
        rejectNulPath(path);
        std::string mode = "r"; bool excl = false;
        for (auto& x : a) if (x.t == VT::Pair) {
            if (x.s == "w") mode = "w"; else if (x.s == "a") mode = "a"; else if (x.s == "r") mode = "r";
            else if (x.s == "rw") mode = "rw";           // read/write, create if missing, NO truncate
            else if (x.s == "update") mode = "update";   // read/write, must exist
            else if (x.s == "exclusive" || x.s == "x") excl = true; // create-new-or-fail (O_EXCL)
        }
        // The SPELLED-OUT form Rakudo also takes: `:mode<ro|wo|rw>` with
        // `:create`/`:truncate`/`:append`. `$f.open(:mode<wo>, :create).close`
        // is the idiomatic `touch` (roast's own filetest.t does it), and
        // without this the adverbs were ignored, the handle opened read-only,
        // and the open threw "no such file or directory".
        {
            std::string modeAdv; bool wantCreate = false, wantTrunc = false, wantApp = false;
            for (auto& x : a) if (x.t == VT::Pair) {
                bool on = !x.pairVal() || x.pairVal()->truthy();
                if (x.s == "mode" && x.pairVal()) modeAdv = x.pairVal()->toStr();
                else if (x.s == "create")   wantCreate = on;
                else if (x.s == "truncate") wantTrunc = on;
                else if (x.s == "append")   wantApp = on;
            }
            if (!modeAdv.empty() || wantCreate || wantTrunc || wantApp) {
                if (modeAdv == "ro") mode = "r";
                else if (wantApp) mode = "a";
                else if (wantTrunc) mode = "w";
                else if (wantCreate) mode = "rw";       // create if missing, keep the content
                else if (modeAdv == "wo" || modeAdv == "rw") mode = "update"; // must exist
            }
        }
        // :nl-in(...) — custom input line separator(s); .lines/.get honour it
        Value nlIn;
        for (auto& x : a) if (x.t == VT::Pair && x.s == "nl-in" && x.pairVal()) nlIn = *x.pairVal();
        if (excl) { // File::Temp opens `:rw, :exclusive` to claim a fresh name
            std::ifstream probe(path);
            if (probe) throw RakuError{Value::typeObj("X::IO::Exclusive"),
                "Failed to open file " + path + ": file already exists"};
            if (mode == "r") mode = "w"; // bare :x implies write-create (Rakudo's :x)
        }
        if (mode == "r" || mode == "update") { // both need the file to exist
            std::ifstream probe(path);
            if (!probe) throw RakuError{Value::typeObj("X::IO::DoesNotExist"),
                "Failed to open file " + path + ": no such file or directory"};
        }
        Value h = Value::makeHash(); h.hashKind = "FileHandle";
        (*h.hash())["path"] = Value::str(path);
        (*h.hash())["mode"] = Value::str(mode);
        (*h.hash())["buffer"] = Value::str("");
        // :enc(...) — the handle's text encoding; every read through it decodes
        // with this instead of assuming the bytes are already UTF-8
        for (auto& x : a) if (x.t == VT::Pair && x.s == "enc" && x.pairVal() && x.pairVal()->t != VT::Any)
            (*h.hash())["encoding"] = Value::str(x.pairVal()->toStr());
        if (nlIn.t != VT::Any) (*h.hash())["nl-in"] = nlIn;
        // :out-buffer(N) / :!out-buffer — how many bytes the handle may hold
        // back before they must reach the file. Absent, it keeps the default
        // block; :!out-buffer (False) makes every write land immediately, which
        // is how a program writes a log another process is tailing.
        for (auto& x : a) if (x.t == VT::Pair && x.s == "out-buffer")
            (*h.hash())["out-buffer"] =
                Value::integer(outBufferSize(x.pairVal() ? *x.pairVal() : Value::boolean(true)));
        if (mode == "w") { std::ofstream create(path, std::ios::trunc); } // the file exists immediately
        if (mode == "rw") { std::ofstream create(path, std::ios::app); }  // exists immediately, kept intact
        if (mode != "r") I.registerWriteHandle(h.hashS()); // flush at exit if not closed
        return h;
    };
    // symlink($target, $name) / link($target, $name) — the sub forms Rakudo
    // exposes (File::Find's suite builds a symlinked directory with the sub form
    // before walking it). `readlink` is a METHOD only, as in Rakudo.
    B["symlink"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.size() < 2) return Value::boolean(false);
        std::string target = I.ioFsPath(a[0]), name = I.ioFsPath(a[1]);
        // Rakudo absolutizes the TARGET. It matters: a relative target is read
        // by the OS relative to the LINK's directory, not the cwd, so
        // `symlink("t/dir1/d", "t/dir2/link")` would otherwise dangle.
        if (!target.empty() && target[0] != '/') {
            char cbuf[4096];
            if (getcwd(cbuf, sizeof cbuf)) target = std::string(cbuf) + "/" + target;
        }
        if (platform_symlink(target.c_str(), name.c_str()) != 0)
            throw RakuError{Value::typeObj("X::IO::Symlink"),
                "Failed to create symlink called '" + name + "' on target '" + target +
                "': " + std::strerror(errno)};
        return Value::boolean(true);
    };
    B["link"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.size() < 2) return Value::boolean(false);
        std::string target = I.ioFsPath(a[0]), name = I.ioFsPath(a[1]);
        if (platform_link(target.c_str(), name.c_str()) != 0)
            throw RakuError{Value::typeObj("X::IO::Link"),
                "Failed to create link called '" + name + "' on target '" + target +
                "': " + std::strerror(errno)};
        return Value::boolean(true);
    };
    // 6.d hands back the list of paths it removed; 6.e takes one path and hands
    // back one Bool (its multi-path form still answers the old way, deprecated).
    // A path that was already absent counts as removed in both — Rakudo says
    // True for a file that does not exist, and the point of the call is the
    // state afterwards, not who did it.
    B["unlink"] = [](Interpreter& I, ValueList& a) -> Value {
        auto gone = [](const std::string& p) { return ::unlink(p.c_str()) == 0 || errno == ENOENT; };
        if (I.sixE() && a.size() == 1) return Value::boolean(gone(I.ioFsPath(a[0])));
        Value ok = Value::array();   // an Array, as Rakudo's `my @ok` is
        for (auto& f : a) if (gone(I.ioFsPath(f))) ok.arr()->push_back(f);
        return ok;
    };
    // sub forms of the IO::Path methods (Shell::Command calls them this way)
    B["copy"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.size() < 2) return Value::boolean(false);
        ValueList rest(a.begin() + 1, a.end());
        return I.methodCall(a[0], "copy", rest);
    };
    B["rename"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.size() < 2) return Value::boolean(false);
        ValueList rest(a.begin() + 1, a.end());
        return I.methodCall(a[0], "rename", rest);
    };
    B["move"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.size() < 2) return Value::boolean(false);
        ValueList rest(a.begin() + 1, a.end());
        return I.methodCall(a[0], "move", rest);
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
            if (::chmod(p.c_str(), mode) == 0) out.arr()->push_back(a[k]);
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
        for (auto& v : a) {
            if (v.t == VT::Code) code = v;
            else if (v.t == VT::Str) desc = v.s;
            // `subtest "title" => {…}` — the Pair form runs its body like any
            // other (it was once left unrun; every such subtest passed VACUOUSLY,
            // which inflated whole dist suites — AttrX::Mooish's most of all)
            else if (v.t == VT::Pair && v.pairVal()) {
                desc = v.s;
                if (v.pairVal()->t == VT::Code) code = *v.pairVal();
            }
        }
        // A pending `todo` marks this whole subtest TODO: inner failures neither die nor count.
        bool todod = false; std::string todoReason;
        if (I.todoRemaining_ > 0) { todod = true; todoReason = I.todoReason_; I.todoRemaining_--; }
        bool savedFailed = I.subtestFailed_;
        int savedPlanned = I.planned_, savedTestNum = I.testNum_; // a subtest has its own plan + numbering
        long savedFailCount = I.failCount_;
        // the "# Subtest: <name>" banner, at the ENCLOSING level's indent —
        // the TAP module's strict Sub-Test parser keys nested blocks off it
        std::cout << std::string(4 * I.subtestDepth_, ' ')
                  << "# Subtest" << (desc.empty() ? "" : ": " + desc) << "\n";
        I.subtestDepth_++;
        if (todod) I.todoSubtestDepth_++;
        I.subtestFailed_ = false;
        I.planned_ = -1; I.testNum_ = 0;
        if (code.t == VT::Code) { try { I.callCallable(code, {}); } catch (RakuError&) { I.subtestFailed_ = true; } }
        bool ok = !I.subtestFailed_;
        // no plan declared inside: the subtest's own trailing plan line closes
        // its block ("    1..N"), exactly as done-testing would have printed it
        if (I.planned_ < 0)
            std::cout << std::string(4 * I.subtestDepth_, ' ') << "1.." << I.testNum_ << "\n";
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
    for (auto nm : {"rotate", "substr", "substr-rw", "trim", "trim-leading",
                    "trim-trailing", "flip", "tc", "tclc", "wordcase", "pairs", "antipairs", "chop",
                    "samecase", "samemark", "chomp"}) // head/tail: count-first forms below
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
            // The ONE-ARG RULE, as Rakudo has it: a single Positional argument
            // spreads to its elements (`reduce &f, @a`, `reduce &f, 1..4`), and
            // several arguments are taken as they are — `reduce &f, "I", (1,2),
            // (3,4)` folds over three items, the last two being Lists. Deep
            // `flatten()`ing every argument destroyed exactly that: Digest::SHA2's
            // 16-word block arrived as sixteen separate values, so `$block[$t]`
            // was undefined for every t but 0.
            if (a.size() == 2 && (a[1].t == VT::Array || a[1].t == VT::Range)) {
                if (a[1].t == VT::Array && a[1].arr()) for (auto& x : *a[1].arr()) items.push_back(x);
                else items = a[1].flatten();
            }
            else for (size_t i = 1; i < a.size(); i++) items.push_back(a[i]);
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
        Value out = Value::array(); out.isList = true; out.s = "Seq"; // Rakudo hands back a Seq
        double re, im;
        Value x = a.empty() ? Value::integer(0) : a[0];
        if (x.t == VT::Complex) { re = x.n; im = x.im(); }
        else { re = x.toNum(); im = 0.0; }
        long long n = a.size() > 1 ? a[1].toInt() : 1;
        // a degenerate root count (or a NaN operand) is a bare NaN, not a
        // one-element list of one
        if (n < 1 || std::isnan(re) || std::isnan(im)) return Value::number(std::nan(""));
        // the ONE first root is handed back bare, not as a list of one
        if (n == 1) return Value::complex(re, im);
        double mag = std::pow(std::hypot(re, im), 1.0 / (double)n);
        double ang = std::atan2(im, re) / (double)n;
        for (long long k = 0; k < n; k++) {
            double th = ang + 2.0 * M_PI * (double)k / (double)n;
            out.arr()->push_back(Value::complex(mag * std::cos(th), mag * std::sin(th)));
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
    B["chdir"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.empty()) throw RakuError{Value::typeObj("X::TypeCheck::Argument"),
            "Cannot call chdir without an argument"};
        if (a[0].toStr().find('\0') != std::string::npos)
            throw RakuError{Value::typeObj("X::IO::Null"),
                "Cannot use null character (U+0000) as part of the path"};
        std::string old = I.cwdName(); // the base BEFORE the switch
        std::string to = a[0].toStr();
        // a relative IO::Path argument is relative to ITS OWN captured :CWD
        if (a[0].hashKind == "IO" && !a[0].ofType().empty() && !to.empty() && to[0] != '/')
            to = logicalJoin(a[0].ofType(), to);
        if (::chdir(to.c_str()) != 0) {
            Value f = rakuppNewFailure();
            (*f.hash())["message"] = Value::str("Failed to change the working directory to '" + a[0].toStr() + "'");
            return f;
        }
        I.logicalCwd_ = logicalJoin(old, to);
        // Rakudo's answer is the new cwd as an absolute IO::Path, based where you were
        Value p = Value::str(I.logicalCwd_); p.hashKind = "IO"; p.ofTypeM() = old;
        return p;
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
        // a relative IO::Path argument is relative to ITS OWN captured :CWD
        if (a[0].hashKind == "IO" && !a[0].ofType().empty() && !to.empty() && to[0] != '/')
            to = logicalJoin(a[0].ofType(), to);
        char buf[4096];
        std::string from = getcwd(buf, sizeof buf) ? buf : ".";
        std::string base = I.cwdName(), oldLogical = I.logicalCwd_;
        if (::chdir(to.c_str()) != 0)
            throw RakuError{Value::typeObj("X::IO::Chdir"),
                            "Failed to change the working directory to '" + to + "'"};
        I.logicalCwd_ = logicalJoin(base, to); // $*CWD keeps the caller's spelling
        Value r;
        try { ValueList none; r = I.callCallable(a[1], none); }
        catch (...) { ::chdir(from.c_str()); I.logicalCwd_ = oldLogical; throw; }   // restore on ANY exit
        ::chdir(from.c_str());
        I.logicalCwd_ = oldLogical;
        return r;
    };
    // (loop-control escaping a dies-ok/lives-ok block is a death — see those below)
    B["cross"] = [](Interpreter& I, ValueList& a) -> Value {
        Value withF;
        std::vector<ValueList> rows;
        for (auto& v : a) {
            if (v.t == VT::Pair && v.s == "with" && v.pairVal()) { withF = *v.pairVal(); continue; }
            if (v.t == VT::Array && v.arr()) rows.push_back(*v.arr());
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
                    out.arr()->push_back(acc);
                } else {
                    Value t = Value::array(); t.isList = true;
                    for (size_t k = 0; k < rows.size(); k++) t.arr()->push_back(rows[k][idx[k]]);
                    out.arr()->push_back(t);
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
        if (a.empty()) return Value::integer(0);
        // count without materializing (toList deep-copies every element)
        const Value& v = a[0];
        if (v.t == VT::Array && v.arr() && !v.ext()) return Value::integer((long long)v.arr()->size());
        if (v.t == VT::Hash && v.hash() && v.hashKind.empty()) return Value::integer((long long)v.hash()->size());
        return Value::integer((long long)toList(v).size());
    };
    B["defined"] = [](Interpreter&, ValueList& a) -> Value { return Value::boolean(!a.empty() && defined(a[0])); };
    // Prefix forms of the metamethods: WHAT($x) === $x.WHAT, etc.
    for (const char* mm : {"WHAT", "WHO", "HOW", "VAR", "WHICH", "WHY"})
        B[mm] = [mm](Interpreter& I, ValueList& a) -> Value { ValueList none; return I.methodCall(a.empty() ? Value::any() : a[0], mm, none); };
    B["chars"] = [](Interpreter& I, ValueList& a) -> Value { return a.empty() ? Value::integer(0) : rtBChars(I, a[0]); };
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
    // (unival/univals sub forms are the method-delegating loop below)
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
            for (char ch : s) if (ascii::isalnum((unsigned char)ch)) o += (char)ascii::tolower((unsigned char)ch);
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
        Value code; code.t = VT::Code; code.setCode(std::make_shared<Callable>());
        code.code()->isWhateverCode = true;
        code.code()->whateverArity = (w.t == VT::Code && w.code() && w.code()->whateverArity > 0) ? w.code()->whateverArity : 1;
        Value inner = w;
        code.code()->builtin = [negate, inner](Interpreter& I, ValueList& xs) -> Value {
            Value v = inner.t == VT::Whatever ? (xs.empty() ? Value::any() : xs[0])
                                              : I.callCallable(inner, xs);
            bool b = I.boolify(v);
            return Value::boolean(negate ? !b : b);
        };
        return code;
    };
    B["so"] = [boolCurry](Interpreter& I, ValueList& a) -> Value {
        if (a.size() == 1 && (a[0].t == VT::Whatever || (a[0].t == VT::Code && a[0].code() && a[0].code()->isWhateverCode)))
            return boolCurry(false, a[0]);
        return Value::boolean(!a.empty() && I.boolify(a[0]));
    };
    B["not"] = [boolCurry](Interpreter& I, ValueList& a) -> Value {
        if (a.size() == 1 && (a[0].t == VT::Whatever || (a[0].t == VT::Code && a[0].code() && a[0].code()->isWhateverCode)))
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
    // parse-base($str, $radix) — the SUB form of the method (Digest's md5.t
    // builds its expected digests with `parse-base($hex, 16).polymod(…)`)
    B["parse-base"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.empty()) return Value::any();
        Value v = a[0];
        ValueList rest(a.begin() + 1, a.end());
        return I.methodCall(v, "parse-base", rest);
    };
    // `pack TEMPLATE, @items` (use experimental :pack) — the inverse of Buf.unpack,
    // sharing its directive set: A/a/Z text, C/c bytes, S/v/n 16-bit, L/V/N 32-bit,
    // Q 64-bit, H hex digits, x a null byte. S/L/Q are native (little-endian here),
    // v/V little and n/N big. MIME::Base64's test suite builds UTF-16 input with it.
    B["pack"] = [](Interpreter&, ValueList& a) -> Value {
        if (a.empty()) { Value b = Value::str(""); b.hashKind = "Buf"; return identify(b); }
        std::string tmpl = a[0].toStr();
        ValueList items;
        for (size_t i = 1; i < a.size(); i++)
            for (auto& x : toList(a[i])) items.push_back(x);
        std::string out;
        size_t ai = 0;
        auto next = [&]() -> Value { return ai < items.size() ? items[ai++] : Value::integer(0); };
        auto putLE = [&](unsigned long long v, int w) { for (int k = 0; k < w; k++) out += (char)((v >> (8 * k)) & 0xFF); };
        auto putBE = [&](unsigned long long v, int w) { for (int k = w - 1; k >= 0; k--) out += (char)((v >> (8 * k)) & 0xFF); };
        for (size_t k = 0; k < tmpl.size(); k++) {
            char dir = tmpl[k];
            if (ascii::isspace((unsigned char)dir)) continue;
            bool all = false; long long cnt = 1;
            if (k + 1 < tmpl.size() && tmpl[k + 1] == '*') { all = true; k++; }
            else if (k + 1 < tmpl.size() && ascii::isdigit((unsigned char)tmpl[k + 1])) {
                size_t j = k + 1; std::string num;
                while (j < tmpl.size() && ascii::isdigit((unsigned char)tmpl[j])) num += tmpl[j++];
                cnt = std::stoll(num); k = j - 1;
            }
            if (dir == 'A' || dir == 'a' || dir == 'Z') {
                std::string t = next().toStr();
                long long w = all ? (long long)t.size() + (dir == 'Z' ? 1 : 0) : cnt;
                for (long long i2 = 0; i2 < w; i2++)
                    out += i2 < (long long)t.size() ? t[i2] : (dir == 'A' ? ' ' : '\0');
            }
            else if (dir == 'H') {
                std::string t = next().toStr();
                long long w = all ? (long long)t.size() : cnt;
                auto hv = [](char c) { return c >= '0' && c <= '9' ? c - '0'
                                            : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                            : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 0; };
                for (long long i2 = 0; i2 < w; i2 += 2) {
                    int hi = i2 < (long long)t.size() ? hv(t[i2]) : 0;
                    int lo = i2 + 1 < w && i2 + 1 < (long long)t.size() ? hv(t[i2 + 1]) : 0;
                    out += (char)((hi << 4) | lo);
                }
            }
            else if (dir == 'x') { for (long long i2 = 0; i2 < (all ? 1 : cnt); i2++) out += '\0'; }
            else {
                int w = (dir == 'C' || dir == 'c') ? 1
                      : (dir == 'S' || dir == 'v' || dir == 'n') ? 2
                      : (dir == 'Q' || dir == 'q') ? 8 : 4;
                bool bigEnd = (dir == 'n' || dir == 'N');
                long long r = all ? (long long)(items.size() - ai) : cnt;
                for (long long i2 = 0; i2 < r; i2++) {
                    unsigned long long v = (unsigned long long)next().toInt();
                    if (bigEnd) putBE(v, w); else putLE(v, w);
                }
            }
        }
        Value b = Value::str(out); b.hashKind = "Buf"; return identify(b);
    };
    // callframe($level) — the caller's frame $level steps up: its .file, the LINE
    // the call was written on, and .code (the routine it sits in, `<unit>` at
    // mainline). Log::Async stamps every log message with `callframe(1)`.
    B["callframe"] = [](Interpreter& I, ValueList& a) -> Value {
        long long lvl = a.empty() ? 0 : a[0].toInt();
        if (lvl < 0) lvl = 0;
        auto& fr = I.tctx_.callFrames;
        Value f = Value::makeHash(); f.hashKind = "CallFrame";
        (*f.hash())["file"] = Value::str(I.srcFileAbs_.empty() ? I.srcFile_ : I.srcFileAbs_);
        // level 0 is where we are now (the current statement's line); each further
        // level steps out one activation, taking that call's own line with it
        size_t idx = fr.size();                 // frames_[idx-1] is the innermost
        long long line = I.curLine_;
        const Value* code = nullptr;
        for (long long k = 0; k < lvl; k++) {
            if (idx == 0) break;
            line = fr[idx - 1].line;            // the line THIS activation was called from
            idx--;
        }
        if (idx > 0) code = fr[idx - 1].code;
        (*f.hash())["line"] = Value::integer(line);
        if (code) (*f.hash())["code"] = *code;
        return f;
    };
    // `MY::<&foo>:exists` — is that symbol declared in the current scope chain?
    B["__sym-exists"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.empty()) return Value::boolean(false);
        const std::string n = a[0].toStr();
        if (I.tctx_.cur && I.tctx_.cur->find(n)) return Value::boolean(true);
        return Value::boolean(false);
    };
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
        // accumulate in int64 until the next place-shift would overflow, then
        // spill to BigInt — :256[16 bytes] is a 128-bit value (UUID.Str)
        long long val = 0;
        bool big = false; BigInt bval;
        long long lim = (std::numeric_limits<long long>::max)() / (base > 1 ? base : 2) - base;
        auto add = [&](long long d) {
            if (!big) {
                if (val > lim) { big = true; bval = BigInt(val); }
                else { val = val * base + d; return; }
            }
            bval = bval * BigInt(base) + BigInt(d);
        };
        for (size_t k = 1; k < a.size(); k++) {
            if (a[k].t == VT::Array && a[k].arr())
                for (auto& e : *a[k].arr()) add(e.toInt());
            else add(a[k].toInt());
        }
        return big ? Value::bigint(bval) : Value::integer(val);
    };
    B["__radix"] = [](Interpreter&, ValueList& a) -> Value {
        if (a.size() < 2) return Value::integer(0);
        int base = (int)a[0].toInt();
        std::string s = a[1].toStr();
        // Every character has to be a digit OF THAT BASE (or a separating `_`, or the
        // one radix point): `:16<fo>` is X::Str::Numeric, not 15. Stopping at the first
        // bad character silently turned a malformed colour like 'foobar' into a number
        // — which is how Color.new('foobar') "worked".
        auto bad = [&](const std::string& why) -> Value {
            throw RakuError{Value::typeObj("X::Str::Numeric"),
                "Cannot convert string to number: " + why + " in ':" + std::to_string(base) +
                "<" + s + ">'"};
        };
        // BigInt throughout: a long long silently overflowed, so
        // `:16("FFFFFFFFFFFFFFFF")` answered -1 rather than 18446744073709551615
        BigInt val(0), den(0), bb((long long)base);   // den = 0 until a radix point is met
        bool any = false;
        for (size_t i = 0; i < s.size(); i++) {
            char c = s[i];
            if (c == '_') {
                if (i == 0 || i + 1 == s.size()) return bad("'_' must be between digits");
                continue;
            }
            if (c == '.') {
                if (!den.isZero()) return bad("more than one radix point");
                den = BigInt(1); continue;
            }
            int d = (c >= '0' && c <= '9') ? c - '0'
                  : (c >= 'a' && c <= 'z') ? c - 'a' + 10
                  : (c >= 'A' && c <= 'Z') ? c - 'A' + 10 : -1;
            if (d < 0 || d >= base)
                return bad("base-" + std::to_string(base) + " number must begin with valid digits or '.'");
            val = val * bb + BigInt(d);
            if (!den.isZero()) den = den * bb;
            any = true;
        }
        if (!any) return bad("base-" + std::to_string(base) + " number must begin with valid digits or '.'");
        if (!den.isZero() && BigInt::cmp(den, BigInt(1)) > 0) return Value::rat(val, den);
        return Value::bigint(val);
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
                if (v.s == "by" && v.pairVal()) { cmp = *v.pairVal(); haveCmp = true; }
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
    B["sum"] = [](Interpreter& I, ValueList& a) -> Value {
        // delegate to the method (like min/max): the exact tower keeps big Ints
        // and Rats exact where the old double accumulator silently rounded
        ValueList items;
        // A Range answers for itself — toList would hand back the truncated
        // prefix of an endless (`sum(1..Inf)` is Inf) or a huge one
        // (`sum(1..10**100)` is its Gauss sum). Summing per operand and adding
        // the parts is the same total.
        Value ranges; bool sawRange = false;
        for (auto& v : a) {
            if (v.t == VT::Range || isEndlessLazy(v)) {
                Value s = I.methodCall(v, "sum", ValueList{});
                ranges = sawRange ? applyArith("+", ranges, s) : s;
                sawRange = true;
                continue;
            }
            for (auto& x : toList(v)) items.push_back(x);
        }
        Value list = Value::array(items); list.isList = true;
        ValueList none;
        Value rest = I.methodCall(list, "sum", none);
        if (!sawRange) return rest;
        return items.empty() ? ranges : applyArith("+", rest, ranges);
    };
    B["keys"] = [](Interpreter&, ValueList& a) -> Value {
        Value out = Value::array();
        if (!a.empty() && a[0].t == VT::Hash) for (auto& kv : *a[0].hash()) out.arr()->push_back(Value::str(kv.first));
        else if (!a.empty()) { ValueList l = toList(a[0]); for (size_t i = 0; i < l.size(); i++) out.arr()->push_back(Value::integer((long long)i)); }
        return out;
    };
    B["values"] = [](Interpreter&, ValueList& a) -> Value {
        Value out = Value::array();
        if (!a.empty() && a[0].t == VT::Hash) for (auto& kv : *a[0].hash()) out.arr()->push_back(kv.second);
        else if (!a.empty()) { ValueList l = toList(a[0]); for (auto& v : l) out.arr()->push_back(v); }
        return out;
    };
    // Synchronous react/whenever/supply: eager, deterministic model.
    B["react"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.empty() || a.back().t != VT::Code) return Value::nil();
        auto ctx = std::make_shared<ReactCtx>();
        I.reactStack_.push_back(ctx);
        try { I.callCallable(a.back(), {}); }
        catch (DoneEx&) {} // `done` in the react body: normal completion (ctx already closed)
        catch (...) { I.reactStack_.pop_back(); throw; }
        I.reactStack_.pop_back();
        // Deferred whenever activations (issue #18): the body has finished, so
        // statements after a `whenever` have run — now drain the synchronous
        // sources, in registration order. A die inside a drained body kills
        // the react with that exception, exactly like the body itself dying.
        try {
            for (;;) {
                std::vector<std::function<void()>> ds;
                { std::lock_guard<std::mutex> lk(ctx->m); ds.swap(ctx->deferred); }
                if (ds.empty()) break;
                for (auto& d : ds) d();
            }
        }
        catch (DoneEx&) {}
        I.runReactLoop(ctx); // block until every live whenever source is done
        {   // react is over: tear down externally-wired taps (OS-signal taps) so
            // their dispatcher stops firing the handler once the block is gone.
            std::vector<std::shared_ptr<TapHandle>> extTaps;
            { std::lock_guard<std::mutex> lk(ctx->m); extTaps.swap(ctx->extTaps); }
            for (auto& h : extTaps) if (h) I.closeTapHandle(h);
        }
        {   // react is over: its whenever taps close — run on-close callbacks
            ValueList closers;
            { std::lock_guard<std::mutex> lk(ctx->m); closers.swap(ctx->closers); }
            for (auto& cb : closers) if (cb.t == VT::Code) { try { I.callCallable(cb, {}); } catch (...) {} }
        }
        if (ctx->quitFlag) { // a whenever'd supply quit unhandled: the react dies with it
            std::string qm = "Supply quit";
            try { ValueList na; Value mv = I.methodCall(ctx->quitErr, "message", na); if (mv.t == VT::Str) qm = mv.s; } catch (...) {}
            throw RakuError{ctx->quitErr, qm};
        }
        return Value::nil();
    };
    B["whenever"] = [](Interpreter& I, ValueList& a) -> Value {
        // `whenever $supplier` coerces via .Supply (Rakudo does the same): a raw
        // Supplier used to fall through to the run-once-with-the-value arm, which
        // ran the handler EAGERLY with the Supplier as topic. That deadlock was
        // masked by awaitPromise's no-workers escape until a real async source
        // (Supply.interval) engaged the GIL earlier in the program.
        if (!a.empty() && a[0].t == VT::Hash && a[0].hashKind == "Supplier") {
            ValueList none; a[0] = I.methodCall(a[0], "Supply", none);
        }
        // Inside an on-demand supply activation (real tap or eager drain): wire a
        // real inner tap. The body runs (now or later, from an I/O worker) with
        // this activation re-established, so its emits reach the downstream tap.
        if (!I.tctx_.tapStack.empty() && a.size() >= 2 && a.back().t == VT::Code) {
            auto ctx = I.tctx_.tapStack.back();
            Value src = a[0], blk = a.back();
            // whenever Promise.in(N)/at(T) in a supply block: a real timer (was
            // firing immediately — Cro's connection/headers timeouts rely on it).
            if (src.t == VT::Hash && src.hashKind == "Promise" && src.hash()->count("kind") &&
                (*src.hash())["kind"].toStr() == "timer") {
                return I.spawnSupplyTimer(timerRemainingSecs(src), blk, ctx);
            }
            // whenever Supply.interval(N) in a supply block: a repeating ticker
            // that keeps this activation open until done/close stops it.
            if (src.t == VT::Hash && src.hashKind == "Supply" && src.hash()->count("kind") &&
                (*src.hash())["kind"].toStr() == "interval") {
                double iv = src.hash()->count("interval") ? (*src.hash())["interval"].toNum() : 1;
                double dl = src.hash()->count("delay") ? (*src.hash())["delay"].toNum() : 0;
                return I.spawnSupplyInterval(iv, dl, I.wrapSupplyChain(src, blk), ctx);
            }
            // whenever over a Promise: register an ASYNC one-shot — the block runs
            // once, with the promise's RESULT, when the promise settles. Must NOT
            // block the supply-block setup: an unkept Promise stays dormant (Cro's
            // ResponseParser has `whenever $cancellation` that is normally never
            // kept — awaiting it synchronously hung the whole response pipeline).
            if (src.t == VT::Hash && src.hashKind == "Promise" && src.ext()) {
                auto ps = std::static_pointer_cast<PromiseState>(src.ext());
                ValueList lastP, quitP;
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
                        if (quitP.empty()) {
                            // no QUIT phaser: the break QUITS the enclosing supply —
                            // forward downstream and close (a refused connect inside
                            // Connector.establish must fail the whole pipeline)
                            if (ctx->quitCb.t == VT::Code) { ValueList one{ex}; try { I2.callCallable(ctx->quitCb, one); } catch (...) {} }
                            if (ctx->tap) I2.closeTapHandle(ctx->tap);
                        }
                    } else {
                        ValueList one{ ps->result };
                        try { I2.callCallable(blk, one); } catch (NextEx&) {} catch (LastEx&) {} catch (DoneEx&) {}
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
                if (src.t == VT::Hash && src.hashKind == "Promise" && src.hash()->count("result")) rv = (*src.hash())["result"];
                ValueList one{rv};
                try { I.callCallable(blk, one); } catch (NextEx&) {} catch (LastEx&) {} catch (DoneEx&) {}
                Value t = Value::makeHash(); t.hashKind = "Tap"; return t;
            }
            ValueList lastP, quitP;
            scanSupplyPhasers(blk, &lastP, &quitP, nullptr);
            Value emitW = ctxCallable(ctx, [blk](Interpreter& I2, ValueList& args) -> Value {
                try { ValueList one = args; return I2.callCallable(blk, one); }
                catch (NextEx&) {} catch (LastEx&) {} catch (DoneEx&) {}
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
            else
                // no QUIT phaser: an exception in the whenever body QUITS the
                // ENCLOSING supply — forward to the downstream tap's quit and
                // close (Cro's frame parser dies per malformed frame and the
                // test observes it on the OUTER tap's quit handler)
                quitW = ctxCallable(ctx, [ctx](Interpreter& I2, ValueList& args) -> Value {
                    if (ctx->quitCb.t == VT::Code) { ValueList one = args; try { I2.callCallable(ctx->quitCb, one); } catch (...) {} }
                    if (ctx->tap) I2.closeTapHandle(ctx->tap);
                    return Value::any();
                });
            Value tapV = I.tapSupply(src, emitW, doneW, quitW);
            // closing the outer tap closes this inner one
            if (ctx->tap && tapV.t == VT::Hash && tapV.ext() &&
                tapV.hash()->count("wired") && (*tapV.hash())["wired"].truthy()) {
                auto ih = std::static_pointer_cast<TapHandle>(tapV.ext());
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
                s.hash()->count("kind") && (*s.hash())["kind"].toStr() == "signal") {
                std::vector<int> sigs;
                if (s.hash()->count("signals") && (*s.hash())["signals"].arr())
                    for (auto& n : *(*s.hash())["signals"].arr()) sigs.push_back((int)n.toInt());
                std::shared_ptr<ReactCtx> ctx;
                if (!I.reactStack_.empty()) {
                    ctx = I.reactStack_.back();
                    std::lock_guard<std::mutex> lk(ctx->m); ctx->liveSources++;
                }
                Value emitW; emitW.t = VT::Code; emitW.setCode(std::make_shared<Callable>());
                Value blkCopy = blk;
                emitW.code()->builtin = [blkCopy](Interpreter& I2, ValueList& args) -> Value {
                    ValueList one = args;
                    try { return I2.callCallable(blkCopy, one); } catch (NextEx&) {} catch (LastEx&) {} catch (DoneEx&) {}
                    return Value::any();
                };
                return I.tapSignal(sigs, I.wrapSupplyChain(s, emitW), Value::nil(), ctx);
            }
            // whenever Supply.interval(N) { … } in a react: a live ticker source —
            // the react waits on it (forever, unless `done`/`last` ends it).
            if (s.t == VT::Hash && s.hashKind == "Supply" &&
                s.hash()->count("kind") && (*s.hash())["kind"].toStr() == "interval") {
                double iv = s.hash()->count("interval") ? (*s.hash())["interval"].toNum() : 1;
                double dl = s.hash()->count("delay") ? (*s.hash())["delay"].toNum() : 0;
                std::shared_ptr<ReactCtx> ctx = I.reactStack_.empty() ? nullptr : I.reactStack_.back();
                return I.spawnIntervalWhenever(iv, dl, I.wrapSupplyChain(s, blk), ctx, nullptr);
            }
            // whenever $socket.Supply { … } — an async-read/async-listen stream in a
            // react: count it as a live source so the block waits for data, and
            // decrement when the stream ends (connection close) so the react exits.
            if (s.t == VT::Hash && s.hashKind == "Supply" && s.hash()->count("kind")) {
                std::string k = (*s.hash())["kind"].toStr();
                if (k == "async-read" || k == "async-listen") {
                    std::shared_ptr<ReactCtx> ctx;
                    if (!I.reactStack_.empty()) {
                        ctx = I.reactStack_.back();
                        std::lock_guard<std::mutex> lk(ctx->m); ctx->liveSources++;
                    }
                    Value doneW;
                    if (ctx) {
                        std::weak_ptr<ReactCtx> wctx = ctx;
                        doneW.t = VT::Code; doneW.setCode(std::make_shared<Callable>());
                        doneW.code()->builtin = [wctx](Interpreter&, ValueList&) -> Value {
                            if (auto c = wctx.lock()) { std::lock_guard<std::mutex> lk(c->m); if (c->liveSources > 0) c->liveSources--; c->cv.notify_all(); }
                            return Value::any();
                        };
                    }
                    return I.tapSupply(s, blk, doneW, Value::nil());
                }
            }
            if (s.t == VT::Hash && s.hashKind == "Supply") {
                if (s.hash()->count("supplier")) {
                    // live supply: register a tap; count it as a react source so the
                    // enclosing react blocks until this supplier signals done.
                    //
                    // The block's LAST/QUIT phasers ARE this tap's done/quit
                    // handlers — `$supplier.done`/`.quit` fan out to them. Without
                    // them a LAST never fired and, worse, `.quit` left the react
                    // source live: the whole react waited forever on a supply that
                    // had already failed. The phasers close over a shim env that
                    // mirrors each call's parameter bindings, so `LAST { say $c }`
                    // sees the last value (same trick as the from-list path below).
                    auto phEnv = std::make_shared<Env>();
                    phEnv->parent = blk.code() ? blk.code()->closure : nullptr;
                    ValueList lastP, quitP;
                    scanSupplyPhasers(blk, &lastP, &quitP, nullptr, phEnv);
                    std::vector<std::string> pnames;
                    if (blk.code() && blk.code()->params)
                        for (auto& p : *blk.code()->params) if (!p.name.empty()) pnames.push_back(p.name);
                    Value blkInner = blk, shim; shim.t = VT::Code; shim.setCode(std::make_shared<Callable>());
                    shim.code()->builtin = [blkInner, phEnv, pnames](Interpreter& I2, ValueList& args) -> Value {
                        for (size_t i = 0; i < pnames.size(); i++)
                            phEnv->define(pnames[i], i < args.size() ? args[i] : Value::any());
                        if (!args.empty()) phEnv->define("$_", args[0]);
                        ValueList one = args;
                        return I2.callCallable(blkInner, one); // control exceptions reach the tap loop
                    };
                    Value tapRec = Value::makeHash();
                    (*tapRec.hash())["emit"] = lastP.empty() ? blk : shim; // the shim costs a frame: only when a LAST needs it
                    // Carry the Supply's transform chain (head/grep/map/…) onto the tap,
                    // each step with its OWN fresh state — same as tapSupply's live branch.
                    // Without it `whenever $s.Supply.head(1)` never limits and, worse,
                    // never reports completion, so the enclosing react waits forever.
                    if (s.hash()->count("chain")) {
                        Value chain = Value::array();
                        for (auto& step : *(*s.hash())["chain"].arr()) {
                            Value s2 = Value::makeHash(); *s2.hash() = *step.hash();
                            (*s2.hash())["state"] = Value::makeHash();
                            chain.arr()->push_back(s2);
                        }
                        (*tapRec.hash())["chain"] = chain;
                    }
                    std::shared_ptr<ReactCtx> rctx;
                    if (!I.reactStack_.empty()) {
                        rctx = I.reactStack_.back();
                        tapRec.extM() = rctx;
                        { std::lock_guard<std::mutex> lk(rctx->m); rctx->liveSources++; }
                    }
                    if (!lastP.empty()) {
                        Value doneW; doneW.t = VT::Code; doneW.setCode(std::make_shared<Callable>());
                        doneW.code()->builtin = [lastP](Interpreter& I2, ValueList&) -> Value {
                            for (auto& p : lastP) { ValueList na; try { I2.callCallable(p, na); } catch (...) {} }
                            return Value::any();
                        };
                        (*tapRec.hash())["done"] = doneW;
                    }
                    {   // the supply quitting ends THIS subscription: a QUIT phaser
                        // handles it (like a CATCH), and with no QUIT phaser the
                        // exception is fatal to the whole react, as in Rakudo.
                        std::weak_ptr<ReactCtx> wctx = rctx;
                        auto th = tapRec.hashS(); // shared: the quit lambda can fire after tapRec's last Value copy dies
                        Value quitW; quitW.t = VT::Code; quitW.setCode(std::make_shared<Callable>());
                        quitW.code()->builtin = [quitP, wctx, th](Interpreter& I2, ValueList& a) -> Value {
                            (*th)["closed"] = Value::boolean(true);
                            auto c = wctx.lock();
                            // run the phasers under the react ctx: `done` inside a
                            // QUIT block has to find the react it belongs to
                            if (c) I2.reactStack_.push_back(c);
                            for (auto& p : quitP) { ValueList one = a; try { I2.callCallable(p, one); } catch (...) {} }
                            if (c) I2.reactStack_.pop_back();
                            if (c) {
                                std::lock_guard<std::mutex> lk(c->m);
                                if (quitP.empty() && !c->quitFlag) { // nothing handled it
                                    c->quitFlag = true;
                                    c->quitErr = a.empty() ? Value::str("quit") : a[0];
                                    c->closed = true;
                                }
                                if (c->liveSources > 0) c->liveSources--;
                                c->cv.notify_all();
                            }
                            return Value::any();
                        };
                        (*tapRec.hash())["quit"] = quitW;
                    }
                    Value sup = (*s.hash())["supplier"];
                    if (sup.t == VT::Hash && sup.hash()->count("taps")) { std::lock_guard<std::recursive_mutex> regLk(supplierMutex(sup.hash())); (*sup.hash())["taps"].arr()->push_back(tapRec); }
                    // The supplier already signalled done before this tap registered
                    // (eager worker ran first): close the tap now, so runReactLoop
                    // doesn't wait on a source that will never complete.
                    if (sup.t == VT::Hash && sup.hash()->count("done_state") &&
                        (*sup.hash())["done_state"].truthy() && tapRec.ext()) {
                        auto ctx = std::static_pointer_cast<ReactCtx>(tapRec.ext());
                        std::lock_guard<std::mutex> lk(ctx->m);
                        if (ctx->liveSources > 0) ctx->liveSources--;
                        ctx->cv.notify_all();
                    }
                    Value t = Value::makeHash(); t.hashKind = "Tap"; return t;
                }
                if (s.hash()->count("block")) {
                    // on-demand supply in a react: tap it with a quit hook that runs
                    // the whenever's QUIT phasers, and — with no QUIT phaser — fails
                    // the react, the quit being fatal (Cro::TCP's `whenever
                    // Connector.establish(...)` on a dead port). A `supply {…}` block
                    // that dies quits its tap (see tapSupply), so this is the path
                    // `whenever $producer { QUIT {…} }` arrives on.
                    std::shared_ptr<ReactCtx> rctx = I.reactStack_.empty() ? nullptr : I.reactStack_.back();
                    ValueList lastP, quitP;
                    scanSupplyPhasers(blk, &lastP, &quitP, nullptr);
                    Value emitW; emitW.t = VT::Code; emitW.setCode(std::make_shared<Callable>());
                    Value blkCopy = blk;
                    emitW.code()->builtin = [blkCopy](Interpreter& I2, ValueList& args) -> Value {
                        ValueList one = args;
                        try { return I2.callCallable(blkCopy, one); } catch (NextEx&) {} catch (LastEx&) {} catch (DoneEx&) {}
                        return Value::any();
                    };
                    // The producer is a live react source until it says done or
                    // quits: an on-demand block whose own whenevers keep ticking
                    // outlives the react body, and without this the react returned
                    // at once and the program raced its own producer. `released`
                    // keeps the count balanced if done and quit both arrive.
                    auto released = std::make_shared<std::atomic<bool>>(false);
                    if (rctx) { std::lock_guard<std::mutex> lk(rctx->m); rctx->liveSources++; }
                    std::weak_ptr<ReactCtx> wctx = rctx;
                    auto release = [wctx, released]() {
                        if (released->exchange(true)) return;
                        if (auto c = wctx.lock()) {
                            std::lock_guard<std::mutex> lk(c->m);
                            if (c->liveSources > 0) c->liveSources--;
                            c->cv.notify_all();
                        }
                    };
                    Value quitW; quitW.t = VT::Code; quitW.setCode(std::make_shared<Callable>());
                    quitW.code()->builtin = [wctx, quitP, release](Interpreter& I2, ValueList& a) -> Value {
                        auto c = wctx.lock();
                        if (c) I2.reactStack_.push_back(c); // `done` in a QUIT block finds its react
                        for (auto& p : quitP) { ValueList one = a; try { I2.callCallable(p, one); } catch (...) {} }
                        if (c) I2.reactStack_.pop_back();
                        if (c && quitP.empty()) {   // unhandled: fatal to the react
                            std::lock_guard<std::mutex> lk(c->m);
                            if (!c->quitFlag) { c->quitFlag = true; c->quitErr = a.empty() ? Value::str("quit") : a[0]; }
                            c->closed = true; c->cv.notify_all();
                        }
                        release();
                        return Value::any();
                    };
                    Value doneW; doneW.t = VT::Code; doneW.setCode(std::make_shared<Callable>());
                    doneW.code()->builtin = [lastP, release](Interpreter& I2, ValueList&) -> Value {
                        for (auto& p : lastP) { ValueList na; try { I2.callCallable(p, na); } catch (...) {} }
                        release();
                        return Value::any();
                    };
                    return I.tapSupply(s, emitW, doneW, quitW);
                }
                {   // from-list: drain AFTER the react body (deferred activation,
                    // issue #18); LAST phasers fire when the list is exhausted or
                    // a `last` ends the subscription. A die in the body escapes
                    // the drain and kills the react, as in Rakudo.
                    std::shared_ptr<ReactCtx> rctx = I.reactStack_.empty() ? nullptr : I.reactStack_.back();
                    // the phasers close over a shim env that mirrors each
                    // call's parameter bindings, so `LAST { "Done with $c" }`
                    // sees the LAST value of $c (Rakudo semantics)
                    auto phEnv = std::make_shared<Env>();
                    phEnv->parent = blk.code() ? blk.code()->closure : nullptr;
                    ValueList lastP, quitP;
                    scanSupplyPhasers(blk, &lastP, &quitP, nullptr, phEnv);
                    std::vector<std::string> pnames;
                    if (blk.code() && blk.code()->params)
                        for (auto& p : *blk.code()->params) if (!p.name.empty()) pnames.push_back(p.name);
                    Value blkInner = blk;
                    Value shim; shim.t = VT::Code; shim.setCode(std::make_shared<Callable>());
                    shim.code()->builtin = [blkInner, phEnv, pnames](Interpreter& I2, ValueList& args) -> Value {
                        for (size_t i = 0; i < pnames.size(); i++)
                            phEnv->define(pnames[i], i < args.size() ? args[i] : Value::any());
                        if (!args.empty()) phEnv->define("$_", args[0]);
                        ValueList one = args;
                        return I2.callCallable(blkInner, one); // control exceptions reach the tap loop
                    };
                    Interpreter* self = &I;
                    Value sCopy = s, blkCopy = shim;
                    auto drain = [self, sCopy, blkCopy, lastP, rctx]() {
                        if (rctx) { std::lock_guard<std::mutex> lk(rctx->m); if (rctx->closed) return; }
                        if (rctx) self->reactStack_.push_back(rctx);
                        try { Value sv = sCopy; ValueList ta{blkCopy}; self->methodCall(sv, "tap", ta); }
                        catch (...) { if (rctx) self->reactStack_.pop_back(); throw; }
                        if (rctx) self->reactStack_.pop_back();
                        for (auto& p : lastP) { ValueList na; try { self->callCallable(p, na); } catch (...) {} }
                    };
                    // A Proc::Async stream registers EAGERLY. Tapping it only records
                    // the callback — nothing is emitted until the process runs — and
                    // the process runs inside the sibling `whenever $proc.start`,
                    // which is part of the same react body. Deferring the
                    // registration until after that body put it after the run, so
                    // the output had already been fed to an empty tap list and the
                    // block never fired.
                    bool procStream = s.t == VT::Hash && s.hashKind == "Supply" && s.hash() &&
                                      s.hash()->count("proc");
                    if (rctx && !procStream) {
                        { std::lock_guard<std::mutex> lk(rctx->m); rctx->deferred.push_back(drain); }
                        Value t = Value::makeHash(); t.hashKind = "Tap"; return t;
                    }
                    drain(); // no react ctx (bare whenever in a plain block): keep the eager order
                    Value t = Value::makeHash(); t.hashKind = "Tap"; return t;
                }
            }
            // `whenever $channel { … }` — one run per received value, completing when
            // the channel closes (Log::Async's tests pump their output through one)
            if (s.t == VT::Hash && s.hashKind == "Channel") {
                std::shared_ptr<ReactCtx> ctx = I.reactStack_.empty() ? nullptr : I.reactStack_.back();
                return I.spawnChannelWhenever(s, blk, ctx);
            }
            // whenever Promise.in(N) { … } — a timer: fire once after the real delay
            // as a react source, so it doesn't defeat a timeout guard by firing at t=0.
            if (s.t == VT::Hash && s.hashKind == "Promise" &&
                s.hash()->count("kind") && (*s.hash())["kind"].toStr() == "timer") {
                std::shared_ptr<ReactCtx> ctx = I.reactStack_.empty() ? nullptr : I.reactStack_.back();
                return I.spawnTimerWhenever(timerRemainingSecs(s), blk, ctx);
            }
            // whenever $proc.ready { … } — fires once with the PID if the process
            // has already run, and does NOT start it (that is `.start`'s job).
            if (s.t == VT::Hash && s.hashKind == "Promise" &&
                s.hash()->count("kind") && (*s.hash())["kind"].toStr() == "proc-ready") {
                Value pidv = Value::nil();
                if (s.hash()->count("proc") && (*s.hash())["proc"].hash()) {
                    auto& ph = *(*s.hash())["proc"].hash();
                    auto it = ph.find("pid"); if (it != ph.end()) pidv = it->second;
                }
                ValueList one{pidv};
                try { I.callCallable(blk, one); } catch (NextEx&) {} catch (LastEx&) {} catch (DoneEx&) {}
                Value t = Value::makeHash(); t.hashKind = "Tap"; return t;
            }
            // whenever $proc.start { … } — a lazy Proc::Async promise: the process
            // runs when the promise is realized (await does the same); run it NOW,
            // then fire the block once with the finished proc (its .so/.exitcode
            // reflect the exit status — zef's curl/wget fetch checks `$_.so`).
            if (s.t == VT::Hash && s.hashKind == "Promise" &&
                s.hash()->count("kind") && (*s.hash())["kind"].toStr() == "proc") {
                I.runProcPromise(s, 0);
                Value procv = s.hash()->count("proc") ? (*s.hash())["proc"] : s;
                if (procv.hashKind == "Proc::Async") procv.hashKind = "Proc"; // the block sees a Proc, as await answers
                ValueList one{procv};
                try { I.callCallable(blk, one); } catch (NextEx&) {} catch (LastEx&) {} catch (DoneEx&) {}
                Value t = Value::makeHash(); t.hashKind = "Tap"; return t;
            }
            // whenever over a SETTLED Promise binds the block to its RESULT, not the
            // promise object. (An unkept one still fires immediately with the object —
            // the full async react registration is still an open item.)
            if (s.t == VT::Hash && s.hashKind == "Promise" && s.ext()) {
                auto ps = std::static_pointer_cast<PromiseState>(s.ext());
                bool done, broken; Value cause; std::string causeMsg;
                { std::lock_guard<std::mutex> lk(ps->m);
                  done = ps->done; broken = ps->broken; cause = ps->cause; causeMsg = ps->causeMsg; }
                if (done) {
                    if (broken) {
                        // a BROKEN promise quits the whenever: a QUIT phaser in the
                        // block handles it, otherwise the react itself dies with the
                        // cause (a refused .connect must fail the react, not run the
                        // block with Any — Cro::TCP's dies-ok relies on it)
                        Value ex = cause.t == VT::Nil ? Value::str(causeMsg) : cause;
                        ValueList quitP;
                        scanSupplyPhasers(blk, nullptr, &quitP, nullptr);
                        if (!quitP.empty()) {
                            for (auto& q : quitP) { ValueList one{ex}; try { I.callCallable(q, one); } catch (...) {} }
                            Value t = Value::makeHash(); t.hashKind = "Tap"; return t;
                        }
                        throw RakuError{ex, causeMsg.empty() ? std::string("Promise broken") : causeMsg};
                    }
                    ValueList one{ps->result}; return I.callCallable(blk, one);
                }
                // UNKEPT promise in a react: REGISTER — fire the block once with the
                // result when it settles, counted as a live source. This is the
                // standard shutdown idiom (`whenever $kill { done }`); firing
                // immediately with the promise OBJECT ran `done` at registration
                // and tore the react down before its other whenevers wired up.
                if (!I.reactStack_.empty()) {
                    auto rctx = I.reactStack_.back();
                    { std::lock_guard<std::mutex> lk(rctx->m); rctx->liveSources++; }
                    ValueList quitP;
                    scanSupplyPhasers(blk, nullptr, &quitP, nullptr);
                    Interpreter* self = &I;
                    Value blkCopy = blk;
                    std::function<void()> fire = [self, blkCopy, ps, rctx, quitP]() {
                        // runs under the GIL, from the settler's thread (via ps->thens)
                        self->reactStack_.push_back(rctx);
                        if (!rctx->closed) {
                            if (ps->broken) {
                                Value ex = ps->cause.t == VT::Nil ? Value::str(ps->causeMsg) : ps->cause;
                                if (!quitP.empty()) {
                                    for (auto& q : quitP) { ValueList one{ex}; try { self->callCallable(q, one); } catch (...) {} }
                                } else {
                                    std::lock_guard<std::mutex> lk(rctx->m);
                                    if (!rctx->quitFlag) { rctx->quitFlag = true; rctx->quitErr = ex; }
                                    rctx->closed = true; rctx->cv.notify_all();
                                }
                            } else {
                                ValueList one{ps->result};
                                try { self->callCallable(blkCopy, one); }
                                catch (NextEx&) {} catch (LastEx&) {} catch (DoneEx&) {} catch (...) {}
                            }
                        }
                        self->reactStack_.pop_back();
                        { std::lock_guard<std::mutex> lk(rctx->m); if (rctx->liveSources > 0) rctx->liveSources--; rctx->cv.notify_all(); }
                    };
                    bool now = false;
                    { std::lock_guard<std::mutex> lk(ps->m); if (ps->done) now = true; else ps->thens.push_back(fire); }
                    if (now) fire();
                    Value t = Value::makeHash(); t.hashKind = "Tap"; return t;
                }
            }
            // whenever over a Promise/plain value: run the block once with it
            ValueList one{s}; return I.callCallable(blk, one);
        }
        return Value::nil();
    };
    B["sleep"] = [](Interpreter& I, ValueList& a) -> Value {
        I.sleepYield(a.empty() ? 0 : a[0].toNum());  // GIL-released, full duration (see sleepYield)
        return Value::nil(); // sleep returns Nil (roast: `$nil = sleep(…); $nil === Nil`)
    };
    // `sleep-until $instant` sleeps until that moment and answers whether it
    // actually waited — an instant already past answers False without sleeping.
    B["sleep-until"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.empty()) return Value::boolean(false);
        // measured against the same high-resolution clock `now` reads, so a
        // fraction-of-a-second target is not lost to truncation
        double now = epochNowSecs();
        double target = instantSecsOf(a[0]);
        if (target <= now) return Value::boolean(false);
        I.sleepYield(target - now);
        return Value::boolean(true);
    };
    // signal(SIGINT, …) — a Supply that emits the Signal enum value each time the
    // process receives one of the named OS signals. Standard Ctrl-C shutdown:
    // `react { whenever signal(SIGINT) { $server.stop; done } }`.
    B["signal"] = [](Interpreter&, ValueList& a) -> Value {
        Value s = Value::makeHash(); s.hashKind = "Supply";
        (*s.hash())["kind"] = Value::str("signal");
        Value sigs = Value::array();
        for (auto& v : a) { int n = signalNumberOf(v); if (n > 0) sigs.arr()->push_back(Value::integer(n)); }
        (*s.hash())["signals"] = sigs;
        return s;
    };
    B["sleep-timer"] = [](Interpreter& I, ValueList& a) -> Value {
        I.sleepYield(a.empty() ? 0 : a[0].toNum());
        // the time NOT slept — a Duration, and 0 unless the sleep was interrupted
        Value d = Value::number(0); d.hashKind = "Duration"; return identify(d);
    };
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
            throw DoneEx{}; // done also EXITS the enclosing whenever block / supply body (Rakudo)
        }
        // `done` inside a react block closes its loop.
        if (!I.reactStack_.empty()) {
            auto ctx = I.reactStack_.back();
            { std::lock_guard<std::mutex> lk(ctx->m); ctx->closed = true; ctx->cv.notify_all(); }
            throw DoneEx{}; // …and the enclosing whenever/react body
        }
        return Value::boolean(true);
    };
    B["supply"] = [](Interpreter& I, ValueList& a) -> Value {
        // supply { … } is ON-DEMAND: the block runs when the supply is tapped
        // (tapSupply), with emit routed to the tap. Value-context consumers
        // (.list, for, await) drain it eagerly via drainSupplyBlock — the same
        // values the old eager model produced, just computed at consumption.
        Value s = Value::makeHash(); s.hashKind = "Supply";
        (*s.hash())["block"] = (!a.empty() && a.back().t == VT::Code) ? a.back() : Value::nil();
        return s;
    };
    B["emit"] = [](Interpreter& I, ValueList& a) -> Value {
        Value v = a.empty() ? Value::any() : a[0];
        static const bool kTapTrace = std::getenv("RAKUPP_TAP_TRACE") != nullptr; // hot path: probe once
        if (kTapTrace)
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
        if (!I.tctx_.supplyStack.empty()) { I.tctx_.supplyStack.back()->push_back(v); return Value::boolean(true); }
        throw RakuError{Value::typeObj("X::ControlFlow"), "emit without supply or react"};
    };
    // printf/sprintf take **@args — a list/array argument flattens into the values,
    // so `printf $fmt, $x, f()` where f returns (a, b) fills three directives.
    auto sprintfArgs = [](const ValueList& a) -> ValueList {
        ValueList rest;
        for (size_t i = 1; i < a.size(); i++) {
            if (a[i].t == VT::Array && a[i].arr()) for (auto& x : *a[i].arr()) rest.push_back(x);
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
        (*f.hash())["fmt"] = Value::str(a.empty() ? "" : a[0].toStr());
        return f;
    };
    B["printf"] = [sprintfArgs](Interpreter& I, ValueList& a) -> Value {
        if (a.empty()) return Value::boolean(true);
        // `printf($fmt, $junction)` PRINTS ONCE PER EIGENSTATE, in order — Rakudo has
        // a dedicated printf(Str(Cool), Junction:D) candidate. (sprintf does not:
        // there the junction stays one value.)
        if (a.size() == 2 && a[1].t == VT::Array && a[1].arr() &&
            (a[1].enumName == "any" || a[1].enumName == "all" ||
             a[1].enumName == "one" || a[1].enumName == "none")) {
            for (auto& e : *a[1].arr()) {
                ValueList one{e};
                I.ioEmit(doSprintf(a[0].toStr(), one, I.langRev_), "$*OUT", false);
            }
            return Value::boolean(true);
        }
        ValueList rest = sprintfArgs(a);
        // ioEmit, not std::cout: it takes the output lock, and it honours a
        // rebound `$*OUT`. Writing the stream directly meant
        // `my $*OUT = open(…); printf(…)` printed to the terminal while `say`
        // on the next line went to the file.
        return I.ioEmit(doSprintf(a[0].toStr(), rest, I.langRev_), "$*OUT", false);
    };
    // 6.e sub form: snip(PRED(s), *@list) — first arg is the predicate or a (p1,p2)
    // list of predicates; the rest is the list. Delegates to the .snip method.
    // 6.e sub form: trans(PAIR…, TARGET) — the transliteration pairs come first
    // and the thing to transliterate is the LAST positional, so it composes in a
    // feed. A list target maps element-wise, as the method would.
    B["trans"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.size() < 2) return a.empty() ? Value::str("") : a.back();
        ValueList pairs(a.begin(), a.end() - 1);
        Value target = a.back();
        if (target.t == VT::Array && target.arr()) {
            Value out = Value::array(); out.isList = true;   // a Seq-ish list, not the Array back
            for (auto& el : *target.arr()) out.arr()->push_back(I.methodCall(el, "trans", pairs));
            return out;
        }
        return I.methodCall(target, "trans", pairs);
    };
    // 6.e sub form: snitch($value) notes it and hands it back; snitch(&tap, $value)
    // runs the tap instead. The value is returned unchanged either way — the
    // point of the routine is to see something without disturbing it.
    B["snitch"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.empty()) return Value::any();
        if (a.size() >= 2 && a[0].t == VT::Code)
            return I.methodCall(a[1], "snitch", ValueList{a[0]});
        return I.methodCall(a[0], "snitch", ValueList{});
    };
    B["snip"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.empty()) return Value::array();
        Value list = Value::array(); list.isList = true;
        for (size_t k = 1; k < a.size(); k++) for (auto& v : toList(a[k])) list.arr()->push_back(v);
        return I.methodCall(list, "snip", {a[0]});
    };
    B["map"] = [](Interpreter& I, ValueList& a) -> Value {
        // A LAZY source is mapped through the METHOD, which stays lazy and pulls
        // as its consumer asks. Building the result here instead walked only the
        // prefix already materialised, so `map &cis, (0, -tau/$n ... *)` came back
        // two elements long and the FFT's `Z*` twiddle silently ran short.
        if (a.size() == 2 && a[0].t == VT::Code && a[1].t == VT::Array && a[1].ext())
            return I.methodCall(a[1], "map", ValueList{a[0]});
        Value out = Value::array(); out.isList = true; out.s = "Seq";
        auto emit = [&](const Value& v) {
            Value r = I.callCallable(a[0], {v});
            if (r.t == VT::Array && r.isList && r.s == "Slip")
                for (auto& x : *r.arr()) out.arr()->push_back(x);
            else out.arr()->push_back(r);
        };
        if (a.size() >= 2 && a[0].t == VT::Code) {
            // the single-arg rule: ONE list argument is iterated; with SEVERAL,
            // each argument is one element (a parenthesized group stays whole —
            // Digest::RIPEMD maps a destructuring block over two tuples), except
            // a Slip, which always flattens in
            if (a.size() == 2) { for (auto& v : toList(a[1])) emit(v); }
            else for (size_t i = 1; i < a.size(); i++) {
                if (a[i].t == VT::Array && a[i].isList && a[i].s == "Slip")
                    for (auto& v : *a[i].arr()) emit(v);
                else emit(a[i]);
            }
        }
        return out;
    };
    B["grep"] = [](Interpreter& I, ValueList& a) -> Value {
        // …and the same for grep: the method form pulls lazily, this one did not
        if (a.size() == 2 && a[1].t == VT::Array && a[1].ext())
            return I.methodCall(a[1], "grep", ValueList{a[0]});
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
            bool isSlip = (x.t == VT::Array && x.arr() && x.s == "Slip");
            if (isSlip || (singleList && (x.t == VT::Array || x.t == VT::Range))) {
                if (x.t == VT::Range) for (auto& e : x.flatten()) list.arr()->push_back(e);
                else for (auto& e : *x.arr()) list.arr()->push_back(e);
            } else list.arr()->push_back(x);
        }
        return I.methodCall(list, "grep", margs); // one implementation
    };
    B["first"] = [](Interpreter& I, ValueList& a) -> Value {
        // delegate to the method (like grep): any matcher works — Code, regex,
        // literal, junction — and the :k/:v/:kv/:p/:end adverbs pass through
        if (a.empty()) return Value::any();
        Value mt = a[0];
        Value list = Value::array(); list.isList = true;
        ValueList margs{mt}, pos;
        for (size_t i = 1; i < a.size(); i++) {
            if (a[i].t == VT::Pair && (a[i].s == "k" || a[i].s == "v" || a[i].s == "kv" ||
                                       a[i].s == "p" || a[i].s == "end"))
                margs.push_back(a[i]); // adverb, pass through
            else pos.push_back(a[i]);
        }
        bool singleList = (pos.size() == 1 && (pos[0].t == VT::Array || pos[0].t == VT::Range) && !pos[0].itemized);
        for (auto& x : pos) {
            bool isSlip = (x.t == VT::Array && x.arr() && x.s == "Slip");
            if (isSlip || (singleList && (x.t == VT::Array || x.t == VT::Range))) {
                if (x.t == VT::Range) for (auto& e : x.flatten()) list.arr()->push_back(e);
                else for (auto& e : *x.arr()) list.arr()->push_back(e);
            } else list.arr()->push_back(x);
        }
        return I.methodCall(list, "first", margs); // one implementation
    };
    B["push"] = [](Interpreter& I, ValueList& a) -> Value {
        // a List refuses resizing — the METHOD arm owns the X::Immutable throw
        if (!a.empty() && a[0].t == VT::Array && a[0].isList) { Value inv = a[0]; ValueList rest(a.begin() + 1, a.end()); return I.methodCall(inv, "push", rest); }
        if (!a.empty() && a[0].t == VT::Array) { for (size_t i = 1; i < a.size(); i++) a[0].arr()->push_back(a[i]); return a[0]; }
        return Value::any();
    };
    B["pop"] = [](Interpreter& I, ValueList& a) -> Value {
        if (!a.empty() && a[0].t == VT::Array && a[0].ext() && std::static_pointer_cast<LazySeqState>(a[0].ext())->infinite)
            throw RakuError{Value::typeObj("X::Cannot::Lazy"), "Cannot pop a lazy list"};
        // a List refuses resizing — the METHOD arm owns the X::Immutable throw
        if (!a.empty() && a[0].t == VT::Array && a[0].isList) { ValueList none; return I.methodCall(a[0], "pop", none); }
        if (!a.empty() && a[0].t == VT::Array && !a[0].arr()->empty()) { Value v = a[0].arr()->back(); a[0].arr()->pop_back(); if (v.t == VT::Array) v.itemized = true; return v; }
        // empty: the METHOD's Failure, not a silent Any (see B["shift"])
        if (!a.empty() && a[0].t == VT::Array) { ValueList none; return I.methodCall(a[0], "pop", none); }
        return Value::any();
    };
    B["shift"] = [](Interpreter& I, ValueList& a) -> Value {
        if (!a.empty() && a[0].t == VT::Array && a[0].ext() && std::static_pointer_cast<LazySeqState>(a[0].ext())->infinite) I.materializeLazy(a[0], 1);
        // a List refuses resizing — the METHOD arm owns the X::Immutable throw
        if (!a.empty() && a[0].t == VT::Array && a[0].isList) { ValueList none; return I.methodCall(a[0], "shift", none); }
        if (!a.empty() && a[0].t == VT::Array && !a[0].arr()->empty()) { Value v = a[0].arr()->front(); a[0].arr()->erase(a[0].arr()->begin()); if (v.t == VT::Array) v.itemized = true; return v; }
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
        // Same rule as `.flat`, and it is about the SLOT: a bare list slot
        // spreads its Iterable, an ARRAY's slot never does — array assignment
        // itemises each element. `flat [[1,2],[3]]` stays two elements, and so
        // does `my @a = (1,2),(3,4); flat @a`, List elements or not.
        std::function<void(const Value&, bool)> deeper = [&](const Value& x, bool ofArray) {
            if (x.t == VT::Array && x.arr() && !x.itemized && !ofArray)
                for (auto& e : *x.arr()) deeper(e, !x.isList);
            else if (x.t == VT::Range) for (auto& e : x.flatten()) out.arr()->push_back(e);
            else out.arr()->push_back(x);
        };
        for (auto& v : a) {
            if (v.itemized) { out.arr()->push_back(v); continue; }
            // a shaped array contributes its leaves: `flat @a[3;2]` is six values
            if (isMultiDimShaped(v)) { for (auto& e : shapedLeaves(v)) out.arr()->push_back(e); continue; }
            if (v.t == VT::Array && v.arr()) { for (auto& e : *v.arr()) deeper(e, !v.isList); continue; }
            if (v.t == VT::Range) { for (auto& e : v.flatten()) out.arr()->push_back(e); continue; }
            // A HASH flattens to its Pairs — an EMPTY one therefore contributes
            // nothing. Pushing the hash itself made `flat %new, @new` (URI's
            // `*@new, *%bad` query setter, with no named arguments) hand a Hash
            // to code expecting a Pair: "No such method 'value'".
            if (v.t == VT::Hash && v.hash() && v.hashKind.empty()) {
                for (auto& kv : *v.hash()) out.arr()->push_back(Value::pair(kv.first, kv.second));
                continue;
            }
            out.arr()->push_back(v);
        }
        return out;
    };
    B["cache"] = [](Interpreter&, ValueList& a) -> Value { // cache(list) — like .cache, a no-op for our eager values
        if (a.size() == 1) { if (a[0].t == VT::Range) return Value::array(a[0].flatten()); return a[0]; }
        Value out = Value::array(); out.isList = true;
        for (auto& v : a) out.arr()->push_back(v);
        return out;
    };
    B["slip"] = [](Interpreter&, ValueList& a) -> Value { // slip(4,5) spreads into the enclosing list
        Value out = Value::array(); out.isList = true; out.s = "Slip";
        for (auto& v : a) { ValueList l = v.flatten(); for (auto& x : l) out.arr()->push_back(x); }
        return out;
    };
    // NB: no B["Slip"] — a bareword `Slip` must stay a type object (Slip.new);
    // the call form Slip(...) routes through the evalCall coercer block.
    B["roundrobin"] = [](Interpreter&, ValueList& a) -> Value {
        // interleave the input lists: round 0 = one from each, round 1 = next, … skipping exhausted lists
        std::vector<ValueList> lists;
        bool slip = false; // `:slip` flattens the rounds into one list
        for (auto& v : a) {
            if (v.t == VT::Pair && v.namedArg) { if (v.s == "slip") slip = !v.pairVal() || v.pairVal()->truthy(); continue; }
            ValueList l = (v.t == VT::Array || v.t == VT::Range) ? v.flatten() : ValueList{v};
            lists.push_back(l);
        }
        size_t maxLen = 0; for (auto& l : lists) maxLen = std::max(maxLen, l.size());
        Value out = Value::array(); out.isList = true;
        for (size_t i = 0; i < maxLen; i++) {
            if (slip) { for (auto& l : lists) if (i < l.size()) out.arr()->push_back(l[i]); continue; }
            Value round = Value::array(); round.isList = true;
            for (auto& l : lists) if (i < l.size()) round.arr()->push_back(l[i]);
            out.arr()->push_back(round);
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
        for (auto& v : a) out.arr()->push_back(v); return out;
    };
    B["eager"] = [](Interpreter&, ValueList& a) -> Value {
        if (a.size() == 1) return a[0];
        Value out = Value::array(); out.isList = true; for (auto& v : a) out.arr()->push_back(v); return out;
    };
    B["hash"] = [](Interpreter&, ValueList& a) -> Value {
        Value h = Value::makeHash();
        ValueList items; // spread list args so hash(<a 1 b 2>) pairs up (and <1 2 3> dies)
        for (auto& v : a) {
            if (v.t == VT::Array && v.arr()) for (auto& x : *v.arr()) items.push_back(x);
            else if (v.t == VT::Hash && !v.hashKind.size()) { for (auto& kv : *v.hash()) (*h.hash())[kv.first] = kv.second; }
            else items.push_back(v);
        }
        for (size_t i = 0; i < items.size(); i++) {
            if (items[i].t == VT::Pair) (*h.hash())[items[i].s] = items[i].pairVal() ? *items[i].pairVal() : Value::any();
            else if (i + 1 < items.size()) { (*h.hash())[items[i].toStr()] = items[i + 1]; i++; }
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
            if (v.t == VT::Pair && v.namedArg && v.s == "with" && v.pairVal()) { with = *v.pairVal(); continue; }
            items.push_back(v);
        }
        // The single-argument rule (`+lol`): ONE iterable argument is the list of
        // lists to zip, not itself a list — `zip(((1,2),(3,4)))` is `((1,3),(2,4))`
        // and `zip @fitted.map(*.[^$max])` transposes the rows. Treating that one
        // argument as the only list wrapped every element a level too deep, and
        // Text::MiscUtils' text-columns handed `("",)` to a Str:D parameter.
        if (items.size() == 1 && items[0].t == VT::Array && items[0].arr())
            items = *items[0].arr();
        Value z = I.applyReduce("Z", items);
        if (with.t == VT::Code && z.arr()) { // zip(:with(&f)) folds each tuple with &f
            Value out = Value::array(); out.isList = true;
            for (auto& t : *z.arr()) {
                ValueList parts = t.t == VT::Array && t.arr() ? *t.arr() : ValueList{t};
                Value acc = parts.empty() ? Value::any() : parts[0];
                for (size_t k = 1; k < parts.size(); k++) acc = I.callCallable(with, {acc, parts[k]});
                out.arr()->push_back(acc);
            }
            return out;
        }
        return z;
    };
    B["classify"] = [](Interpreter& I, ValueList& a) -> Value {
        // `:into(%h)` classifies into an existing hash, APPENDING to its lists.
        Value* into = nullptr; ValueList pos, named;
        for (auto& x : a) {
            if (x.t == VT::Pair && x.s == "into" && x.pairVal()) into = x.pairVal();
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
        if (into->t != VT::Hash || !into->hash()) *into = Value::makeHash();
        if (res.hash()) for (auto& kv : *res.hash()) { // append the grouped elements
            auto it = into->hash()->find(kv.first);
            if (it == into->hash()->end()) (*into->hash())[kv.first] = kv.second;
            else if (it->second.t == VT::Array && kv.second.t == VT::Array)
                for (auto& e : *kv.second.arr()) it->second.arr()->push_back(e);
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
        for (auto& x : a) if (x.t == VT::Pair && x.s == "content") content = x.pairVal() ? x.pairVal()->toStr() : "";
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
            Value p = Value::makeHash(); p.hashKind = "Promise"; p.extM() = ps;
            (*p.hash())["status"] = Value::str("Kept"); (*p.hash())["result"] = ps->result;
            return p;
        }
        Value p = I.spawnPromise(code);
        I.yieldToWorker();
        return p;
    };
    // NativeCall helpers: size of a native type; cglobal is a stub (0)
    // nativesizeof(T) — bytes T occupies in native memory. Scalars answer from
    // the SAME width table the marshaller uses (an independent one here used to
    // say `int` was 4 bytes while every call passed it as 8), and a
    // CStruct/CUnion class or instance answers its real laid-out size instead
    // of a flat pointer-width guess.
    B["nativesizeof"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.empty()) return Value::integer(8);
        ClassInfo* ci = nullptr;
        if (a[0].t == VT::Object && a[0].obj()) ci = a[0].obj()->cls.get();
        else if (a[0].t == VT::Type) {
            // …under the QUALIFIED name too: a role's `[::T]` parameter carries the
            // name as written at the parameterisation site, so `LinearArray[MYSQL_BIND]`
            // asked about a bare "MYSQL_BIND" while the registry key is
            // "DBDish::mysql::Native::MYSQL_BIND". Missing it answered the
            // pointer-sized default 8 for a 112-byte struct, and LinearArray then
            // calloc'd 16 bytes for an array C wrote 224 into.
            auto it = I.classes_.find(a[0].s);
            if (it == I.classes_.end()) it = I.classes_.find(I.resolveClassAlias(a[0].s));
            if (it != I.classes_.end()) ci = it->second.get();
        }
        if (ci && (ci->repr == "CStruct" || ci->repr == "CPPStruct" || ci->repr == "CUnion"))
            return Value::integer(Interpreter::ncStructSize(ci));
        std::string t = a[0].t == VT::Type ? a[0].s : a[0].toStr();
        return Value::integer(Interpreter::ncElemSize(t));
    };
    // parameterized native type name: `CArray[uint8]` is Type{s="CArray",
    // ofType="uint8"} — rebuild the "Name[elem]" string the FFI helpers expect.
    auto ncTypeName = [](const Value& v) -> std::string {
        if (v.t != VT::Type) return v.toStr();
        return (!v.ofType().empty() && v.s.find('[') == std::string::npos) ? v.s + "[" + v.ofType() + "]" : v.s;
    };
    // refresh($obj) — Rakudo re-reads a CStruct's native memory into the Raku
    // object after C wrote through the pointer. Here a CStruct attribute read
    // ALWAYS goes to native memory (there is no cached copy to invalidate), so
    // the call has nothing to do but answer Rakudo's 1.
    B["refresh"] = [](Interpreter&, ValueList&) -> Value { return Value::integer(1); };
    // explicitly-manage($str) — asks Rakudo to hand C a buffer that outlives the
    // call rather than a borrowed one. Our Str marshalling already owns every
    // buffer it passes (ncOwnStrElem keeps it alive for the value's lifetime),
    // so the string is returned unchanged; the point is that the NAME resolves,
    // since it is a DEFAULT export that dists call unconditionally.
    B["explicitly-manage"] = [](Interpreter&, ValueList& a) -> Value {
        return a.empty() ? Value::any() : a[0];
    };
    // check_routine_sanity(&sub) — Rakudo's own signature validator, warning
    // about parameter types NativeCall cannot marshal. The marshaller here
    // reports an unusable type at the call itself, with the offending type
    // named, so this answers True rather than duplicating the check.
    B["check_routine_sanity"] = [](Interpreter&, ValueList&) -> Value { return Value::boolean(true); };
    // guess_library_name($lib) — the file `is native($lib)` resolves to. Takes
    // the same shapes the trait does: a bare name, a full path, a (name, version)
    // list, or a provider sub that answers one.
    B["guess_library_name"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.empty()) return Value::str("");
        Value v = a[0];
        if (v.t == VT::Code && v.code()) { ValueList none; v = I.callCallable(v, none); }
        std::string lib;
        if (v.t == VT::Array && v.arr() && !v.arr()->empty()) {
            // (name, version): Rakudo appends the version to the decorated name
            lib = (*v.arr())[0].toStr();
            std::string ver = v.arr()->size() > 1 ? (*v.arr())[1].toStr() : "";
            std::string got = I.ncGuessLibraryName(lib);
            if (!ver.empty() && got.find(ver) == std::string::npos) {
#if defined(__APPLE__)
                return Value::str("lib" + lib + "." + ver + ".dylib");
#else
                return Value::str("lib" + lib + ".so." + ver);
#endif
            }
            return Value::str(got);
        }
        lib = v.t == VT::Type ? v.s.str() : v.toStr();
        // a path (or anything with a directory part) is taken as written, as the trait does
        if (lib.find('/') != std::string::npos) return Value::str(lib);
        return Value::str(I.ncGuessLibraryName(lib));
    };
    B["cglobal"] = [ncTypeName](Interpreter& I, ValueList& a) -> Value {
        // the library may arrive as a PROVIDER sub — cglobal(&LIB, …) is the
        // Math::Libgsl family's spelling — and its ANSWER is the library name,
        // exactly as `is native(&LIB)` treats it
        std::string lib;
        if (a.size() > 0) {
            if (a[0].t == VT::Code && a[0].code()) { ValueList none; lib = I.callCallable(a[0], none).toStr(); }
            // An UNDEFINED library means the running program itself — `cglobal(Str,
            // 'errno', int32)` is the documented spelling, and taking the type's
            // NAME for it sent dlopen looking for a library called "Str".
            else if (a[0].t == VT::Type &&
                     (a[0].s == "Str" || a[0].s == "Any" || a[0].s == "Mu" || a[0].s == "Nil")) lib = "";
            else lib = a[0].t == VT::Type ? a[0].s.str() : a[0].toStr();
        }
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
        // …under its QUALIFIED name too. A role's `[::T]` parameter carries the
        // name as WRITTEN at the parameterisation site, so `LinearArray[MYSQL_BIND]`
        // hands the role a bare "MYSQL_BIND" while the registry key is
        // "DBDish::mysql::Native::MYSQL_BIND" — and `nativecast(T, $p)` inside the
        // role answered a bare Int instead of a struct handle.
        if (it == I.classes_.end()) it = I.classes_.find(I.resolveClassAlias(t));
        if (it != I.classes_.end()) {
            Value o; o.t = VT::Object; o.setObj(std::make_shared<ObjectData>());
            o.obj()->cls = it->second; o.obj()->attrs["__native_ptr"] = Value::integer(addr);
            return o;
        }
        return Value::integer(addr);
    };
    // Every NativeCall routine answers to its QUALIFIED name as well. Two of them
    // (guess_library_name, check_routine_sanity) are :ALL-only exports, so the
    // qualified spelling is the ONLY one a plain `use NativeCall` program has —
    // and it resolved nowhere.
    for (const char* n : {"nativecast", "nativesizeof", "cglobal", "refresh",
                          "explicitly-manage", "guess_library_name", "check_routine_sanity"})
        B[std::string("NativeCall::") + n] = B[n];
    B["await"] = [](Interpreter& I, ValueList& a) -> Value {
        // resolve a Promise, running any pending Proc::Async work (with the timeout from an anyof timer)
        std::function<Value(Value&)> resolve = [&](Value& p) -> Value {
            // A user class that `does Awaitable` (TAP::Parser is one) supplies the
            // thing to wait on: Rakudo asks it for `get-await-handle`, and such a
            // class almost always delegates that to a Promise attribute. Resolve
            // through that Promise — without this `await $parser` handed back the
            // PARSER (issue #34).
            if (p.t == VT::Object && p.obj() && p.obj()->cls &&
                I.typeOrSubsetMatches(p, "Awaitable")) {
                for (const char* acc : {"promise", "Promise"}) {
                    Value inner;
                    try { inner = I.methodCall(p, acc, {}); } catch (RakuError&) { continue; }
                    if (inner.t == VT::Hash && inner.hashKind == "Promise") return resolve(inner);
                }
                try { return I.methodCall(p, "result", {}); } catch (RakuError&) {}
                return p;
            }
            // `await` a Supply drains it and yields its LAST emitted value
            if (p.t == VT::Hash && p.hashKind == "Supply" && p.hash()->count("values")) {
                auto& vals = *(*p.hash())["values"].arr();
                return vals.empty() ? Value::any() : vals.back();
            }
            if (p.t != VT::Hash || p.hashKind != "Promise") return p;
            // PromiseState-backed promise (start / spawnPromise): block until it
            // settles, rethrowing the cause if it was broken.
            if (p.ext()) {
                auto ps = std::static_pointer_cast<PromiseState>(p.ext());
                I.awaitPromise(ps);
                if (ps->broken) {
                    RakuError err{ ps->cause, ps->causeMsg.empty() ? std::string("Promise broken") : ps->causeMsg };
                    // the error happened in the WORKER; this thread's chain is
                    // merely where it was collected, so it goes under a label
                    if (ps->causeBt && !ps->causeBt->frames.empty()) {
                        err.altBt = err.bt;
                        err.altLabel = "Awaited at:";
                        err.bt = ps->causeBt;
                    }
                    throw err;
                }
                return ps->result;
            }
            std::string kind = p.hash()->count("kind") ? (*p.hash())["kind"].toStr() : "";
            if (kind == "timer") {
                // `await Promise.in($n)` / `.at($t)`: a real wait for the timer's
                // remainder, then Kept with True — it returned immediately before
                // (issue #41's family: a bare timer await was a no-op).
                double left = timerRemainingSecs(p);
                if (left > 0) I.sleepYield(left);
                (*p.hash())["status"] = Value::str("Kept");
                (*p.hash())["result"] = Value::boolean(true);
                return Value::boolean(true);
            }
            if (kind == "anyof" || kind == "allof") {
                Value* procP = nullptr;
                std::vector<Value*> timers;                     // timer members (Promise.in/.at)
                std::vector<std::shared_ptr<PromiseState>> pss; // start/spawn promises in the combinator
                std::vector<Value*> psvals;
                if (p.hash()->count("promises")) for (auto& q : *(*p.hash())["promises"].arr()) {
                    if (q.t == VT::Hash && q.hashKind == "Promise") {
                        std::string k = q.hash()->count("kind") ? (*q.hash())["kind"].toStr() : "";
                        if (k == "timer") timers.push_back(&q);
                        else if (k == "proc") procP = &q;
                        else if (q.ext()) { pss.push_back(std::static_pointer_cast<PromiseState>(q.ext())); psvals.push_back(&q); }
                    }
                }
                // The nearest/farthest timer member's remaining delay, at this moment.
                auto timerLeft = [&](bool nearest) {
                    double L = nearest ? std::numeric_limits<double>::infinity() : 0;
                    for (auto* t : timers) {
                        double r = timerRemainingSecs(*t); if (r < 0) r = 0;
                        L = nearest ? std::min(L, r) : std::max(L, r);
                    }
                    return L;
                };
                if (procP) I.runProcPromise(*procP, timers.empty() ? 0 : timerLeft(true));
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
                        // double-rep deadline: a huge/Inf timer must not overflow
                        // the int64 nanosecond range (it means "no deadline")
                        double dl = timers.empty() ? 3600 : timerLeft(true);
                        auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(dl);
                        while (!anyDone() && std::chrono::steady_clock::now() < deadline)
                            I.sleepYield(0.01); // GIL released so the workers can run
                    }
                    // reflect settled members onto their hashes so `.so`/`.status` read true
                    for (size_t i = 0; i < pss.size(); i++) {
                        std::lock_guard<std::mutex> lk(pss[i]->m);
                        if (pss[i]->done && psvals[i]->hash()) {
                            (*psvals[i]->hash())["status"] = Value::str(pss[i]->broken ? "Broken" : "Kept");
                            if (!pss[i]->broken) (*psvals[i]->hash())["result"] = pss[i]->result;
                        }
                    }
                }
                else if (kind == "anyof" && !timers.empty() && !procP) {
                    // only timers: anyof settles with the NEAREST one — it
                    // returned at t=0 before (issue #41's family)
                    double L = timerLeft(true);
                    if (L > 0) I.sleepYield(L);
                }
                if (kind == "allof" && !timers.empty()) {
                    // allof is not settled until every timer member has fired too
                    double L = timerLeft(false);
                    if (L > 0) I.sleepYield(L);
                }
                // timer members whose moment has passed are Kept now
                for (auto* t : timers) if (timerRemainingSecs(*t) <= 0) {
                    (*t->hash())["status"] = Value::str("Kept");
                    (*t->hash())["result"] = Value::boolean(true);
                }
                (*p.hash())["status"] = Value::str("Kept");
                return p;
            }
            if (kind == "proc") {
                I.runProcPromise(p, 0);
                // Rakudo keeps the .start promise with a Proc — hand back the
                // finished proc (.exitcode/.so are the exit status), the same
                // thing the whenever handler passes to its block. Returning the
                // promise wrapper made `(await $p.start).exitcode` a method
                // error here while Rakudo prints the code.
                auto fin = p.hash()->find("proc");
                if (fin == p.hash()->end()) return p;
                // …and it IS a Proc (shared hash, re-kinded): a `multi` with a
                // `Proc $proc` candidate must match it — TAP::Status.new
                // (Proc::Async !~~ Proc) silently blessed an EMPTY status from
                // the still-Async-kinded value, zeroing every harness Wstat.
                Value pv = fin->second; pv.hashKind = "Proc";
                return pv;
            }
            if (kind == "proc-ready") {
                if (p.hash()->count("proc") && (*p.hash())["proc"].hash()) {
                    auto& ph = *(*p.hash())["proc"].hash();
                    auto it = ph.find("pid"); if (it != ph.end()) return it->second;
                }
                return Value::nil();
            }
            auto it = p.hash()->find("result"); return it != p.hash()->end() ? it->second : p; // plain/old-style
        };
        if (a.size() == 1 && a[0].t == VT::Array) {
            Value out = Value::array(); out.isList = true;
            for (auto& x : *a[0].arr()) out.arr()->push_back(resolve(x));
            return out;
        }
        if (a.size() == 1) return resolve(a[0]);
        Value out = Value::array(); out.isList = true;
        for (auto& x : a) out.arr()->push_back(resolve(x));
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
            else if (a.t == VT::Hash && !a.itemized && a.hash() &&
                     (a.hashKind.empty() || a.hashKind == "Map")) {
                // a plain Hash contributes its pairs (a quanthash stays ONE element)
                for (auto& kv : *a.hash()) {
                    Value p = Value::pair(kv.first, kv.second);
                    p.pairKeyM() = kv.second.pairKey();
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
            for (auto& x : toList(a[0])) out.arr()->push_back(x);
            return out;
        }
        for (auto& v : a) out.arr()->push_back(v);
        return out;
    };
    B["unshift"] = [](Interpreter& I, ValueList& a) -> Value {
        // a List refuses resizing — the METHOD arm owns the X::Immutable throw
        if (!a.empty() && a[0].t == VT::Array && a[0].isList) { Value inv = a[0]; ValueList rest(a.begin() + 1, a.end()); return I.methodCall(inv, "unshift", rest); }
        if (!a.empty() && a[0].t == VT::Array) { for (size_t i = a.size(); i > 1; i--) a[0].arr()->insert(a[0].arr()->begin(), a[i - 1]); return Value::integer((long long)a[0].arr()->size()); }
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
                            if (buf.t == VT::Array && buf.arr()) lv->setArr(buf.arrS()); // SHARE
                        } else if (buf.t == VT::Hash) {
                            lv->t = VT::Hash; lv->setHash(buf.hashS());               // SHARE
                        }
                        return n->op == O::P6BindAttrInvRes ? *lv : buf;
                    }
                }
            }
            break; // ordinary attr bind — fall through to the eager path
        }
        case O::OpenFh: { // nqp::open(path, mode) — a raw OS handle
            // (Crypt::Random reads /dev/urandom through exactly this trio)
            if (a.empty()) return Value::nil();
            Value pathv = eval(a[0].get());
            std::string mode = a.size() > 1 ? eval(a[1].get()).toStr() : "r";
#ifdef _WIN32
            int flags = mode.find('w') != std::string::npos ? (_O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY)
                      : mode.find('a') != std::string::npos ? (_O_WRONLY | _O_CREAT | _O_APPEND | _O_BINARY)
                      : (_O_RDONLY | _O_BINARY);
            int fd = ::_open(pathv.toStr().c_str(), flags, 0644);
#else
            int flags = mode.find('w') != std::string::npos ? (O_WRONLY | O_CREAT | O_TRUNC)
                      : mode.find('a') != std::string::npos ? (O_WRONLY | O_CREAT | O_APPEND)
                      : O_RDONLY;
            int fd = ::open(pathv.toStr().c_str(), flags, 0644);
#endif
            if (fd < 0)
                throw RakuError{Value::typeObj("X::AdHoc"),
                    "Failed to open file " + pathv.toStr() + ": " + std::strerror(errno)};
            Value h = Value::makeHash(); h.hashKind = "NqpFh";
            (*h.hash())["fd"] = Value::integer(fd);
            return h;
        }
        case O::ReadFh: { // nqp::readfh(fh, buf, n) — append up to n bytes into buf
            if (a.size() < 3) return Value::nil();
            Value fhv = eval(a[0].get());
            long long want = eval(a[2].get()).toInt();
            int fd = fhv.t == VT::Hash && fhv.hash() && fhv.hash()->count("fd")
                   ? (int)(*fhv.hash())["fd"].toInt() : -1;
            std::string bytes(want > 0 ? (size_t)want : 0, '\0');
            long long got = 0;
            if (fd >= 0 && want > 0) {
                long long off = 0; // short reads are legal; loop to n or EOF
                while (off < want) {
#ifdef _WIN32
                    long long r = ::_read(fd, &bytes[(size_t)off], (unsigned)(want - off));
#else
                    long long r = ::read(fd, &bytes[(size_t)off], (size_t)(want - off));
#endif
                    if (r <= 0) break;
                    off += r;
                }
                got = off;
            }
            bytes.resize((size_t)got);
            Value* lv = nullptr;
            try { lv = lvalue(a[1].get()); } catch (RakuError&) {}
            if (lv) { // the buffer is FILLED in place (`my $bytes := Buf.new`)
                if (lv->t != VT::Str) { lv->t = VT::Str; lv->s.clear(); }
                if (lv->hashKind.empty() || lv->t != VT::Str) lv->hashKind = "Buf";
                lv->s = lv->s.str() + bytes;
                return *lv;
            }
            Value b = Value::str(std::move(bytes)); b.hashKind = "Buf";
            return b;
        }
        // nqp::stat($path, FIELD) / nqp::lstat(…) — one stat(2), one field, by the
        // MoarVM field numbering Parser::nqpConstValue already spells out. Every
        // caller in the wild reaches for a field IO::Path does not expose:
        // Path::Finder matches on inode/device/uid/gid/nlinks/blocks/blocksize/
        // devtype and keys its symlink-loop guard on inode+device.
        case O::Stat: case O::Lstat: {
            if (a.size() < 2) return Value::integer(-1);
            const std::string path = eval(a[0].get()).toStr();
            const long long field = eval(a[1].get()).toInt();
#ifdef _WIN32
            struct ::_stat64 st;
            const bool ok = ::_stat64(path.c_str(), &st) == 0;
            const bool lok = ok;                       // no lstat on Windows
            auto& lst = st;
#else
            struct ::stat st, lst;
            // ISLNK always needs the link's OWN inode, whichever op was called;
            // everything else follows the link unless this is nqp::lstat
            const bool lok = ::lstat(path.c_str(), &lst) == 0;
            const bool ok = n->op == O::Lstat ? lok : ::stat(path.c_str(), &st) == 0;
            if (n->op == O::Lstat) st = lst;
#endif
            // STAT_EXISTS answers the question rather than failing it. Every other
            // field on an unstattable path THROWS, as Rakudo does — a silent -1
            // would read as a real inode or uid to a caller comparing numbers.
            if (field == 0) return Value::integer(ok ? 1 : 0);
            if (field == 12 && lok) return Value::integer(S_ISLNK(lst.st_mode) ? 1 : 0);
            if (!ok || (field == 12 && !lok))
                throw RakuError{Value::typeObj("X::AdHoc"),
                    "Failed to stat file " + path + ": " + std::strerror(errno)};
            switch (field) {
                case  1: return Value::integer((long long)st.st_size);      // FILESIZE
                case  2: return Value::integer(S_ISDIR(st.st_mode) ? 1 : 0); // ISDIR
                case  3: return Value::integer(S_ISREG(st.st_mode) ? 1 : 0); // ISREG
                case  4: return Value::integer(S_ISCHR(st.st_mode) ||        // ISDEV
                                               S_ISBLK(st.st_mode) ? 1 : 0);
                case  6: return Value::integer((long long)st.st_atime);     // ACCESSTIME
                case  7: return Value::integer((long long)st.st_mtime);     // MODIFYTIME
                case  8: return Value::integer((long long)st.st_ctime);     // CHANGETIME
                case 10: return Value::integer((long long)st.st_uid);       // UID
                case 11: return Value::integer((long long)st.st_gid);       // GID
                case -1: return Value::integer((long long)st.st_dev);       // PLATFORM_DEV
                case -2: return Value::integer((long long)st.st_ino);       // PLATFORM_INODE
                case -3: return Value::integer((long long)st.st_mode);      // PLATFORM_MODE
                case -4: return Value::integer((long long)st.st_nlink);     // PLATFORM_NLINKS
                case -5: return Value::integer((long long)st.st_rdev);      // PLATFORM_DEVTYPE
                default: break;
            }
#ifndef _WIN32
            // st_blksize / st_blocks are POSIX-only, and BSD/macOS spells the
            // creation time st_birthtimespec where Linux has no portable one
            if (field == -6) return Value::integer((long long)st.st_blksize); // PLATFORM_BLOCKSIZE
            if (field == -7) return Value::integer((long long)st.st_blocks);  // PLATFORM_BLOCKS
#if defined(__OpenBSD__)
            // OpenBSD keeps birthtime in the reserved namespace: the member is
            // __st_birthtim and the one alias its headers define under EVERY
            // feature-test combination is __st_birthtime — plain st_birthtime
            // does not exist there.
            if (field == 5) return Value::integer((long long)st.__st_birthtime); // CREATETIME
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__)
            if (field == 5) return Value::integer((long long)st.st_birthtime); // CREATETIME
#endif
#endif
            // CREATETIME where the platform has none, and BACKUPTIME everywhere:
            // MoarVM answers 0 rather than failing
            if (field == 5 || field == 9) return Value::integer(0);
            return Value::integer(-1); // an unknown field number
        }
        case O::CloseFh: {
            if (a.empty()) return Value::nil();
            Value fhv = eval(a[0].get());
            if (fhv.t == VT::Hash && fhv.hash() && fhv.hash()->count("fd"))
#ifdef _WIN32
                ::_close((int)(*fhv.hash())["fd"].toInt());
#else
                ::close((int)(*fhv.hash())["fd"].toInt());
#endif
            return Value::nil();
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
                    if (lv->hashKind.empty()) { lv->hashKind = "Buf"; identify(*lv); }
                    nqpBufWrite(lv->s.mut(), off, val, nb, en, kind);
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
                    else if (lv->t == VT::Array && lv->arr()) lv->arr()->resize(nn, Value::number(0));
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
                if (lv && lv->t == VT::Array && lv->arr() && idx >= 0) {
                    if ((long long)lv->arr()->size() <= idx) lv->arr()->resize(idx + 1, Value::number(0));
                    (*lv->arr())[idx] = Value::number(val.toNum());
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
                    std::string& t = lv->s.mut();
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
    // The argument list comes from the per-thread depth-indexed pool rather than
    // a fresh vector: see ExecContext::nqpArgs. The guard both restores the depth
    // and clears the buffer on every exit path, including a throw — clearing is
    // what keeps argument lifetimes identical to the old local-vector version,
    // and it is why the capacity (not the contents) is what gets reused.
    if (tctx_.nqpDepth >= tctx_.nqpArgs.size()) tctx_.nqpArgs.emplace_back();
    ValueList& v = tctx_.nqpArgs[tctx_.nqpDepth];
    struct ArgGuard {
        ExecContext& t; ValueList& buf;
        ArgGuard(ExecContext& tc, ValueList& b) : t(tc), buf(b) { ++t.nqpDepth; }
        ~ArgGuard() { --t.nqpDepth; buf.clear(); }
    } argGuard{tctx_, v};
    v.clear();
    v.reserve(a.size());
    // nqp ops operate on CONTAINERS: a variable holding a Proxy passes the proxy
    // itself (nqp::istype_nd($attr-var, AttrProxy) / nqp::iscont must see it),
    // where an ordinary Raku read would FETCH. Ops that want the value decont
    // explicitly.
    for (auto& e : a) {
        if (e->kind == NK::VarExpr) {
            auto* ve = static_cast<VarExpr*>(e.get());
            if (Value* p = tctx_.cur->find(ve->name)) { v.push_back(*p); continue; }
        }
        v.push_back(eval(e.get()));
    }
    // `nqp::create(self)` on a USER class makes an instance of that class —
    // uninitialised attributes, no BUILD — which is what a hand-rolled `new`
    // then fills (Hash::int binds `$!hash` through p6bindattrinvres). The
    // shared leaf op knows only the core REPRs and answered a bare buffer, so
    // `my %h is Hash::int` got an Array back from `.new` and fell through to
    // a plain Hash — every method the class defines silently unused.
    // The Unicode property reads go through the `uniprop` method, which knows
    // every property name and its value forms; the "code" is the name itself
    // (see UniPropCode). `_bool` answers 0/1, `_int` a number, `_str` the
    // value's string form (General_Category → "Lu", East_Asian_Width → "W").
    if ((n->op == NqpOpc::GetUniPropStr || n->op == NqpOpc::GetUniPropBool ||
         n->op == NqpOpc::GetUniPropInt) && v.size() >= 2) {
        ValueList pa; pa.push_back(Value::str(v[1].toStr()));
        Value r = methodCall(Value::integer(v[0].toInt()), "uniprop", pa);
        if (n->op == NqpOpc::GetUniPropStr) return Value::str(r.toStr());
        if (n->op == NqpOpc::GetUniPropBool) return Value::integer(r.truthy() ? 1 : 0);
        return Value::integer(r.t == VT::Bool ? (r.truthy() ? 1 : 0) : r.toInt());
    }
    if (n->op == NqpOpc::Create && v.size() == 1 && v[0].t == VT::Type) {
        std::string tn = v[0].s;
        auto it = classes_.find(tn);
        if (it == classes_.end()) it = classes_.find(resolveClassAlias(tn));
        if (it != classes_.end() && it->second) {
            std::string bare = tn;
            if (auto q = bare.rfind("::"); q != std::string::npos) bare = bare.substr(q + 2);
            static const std::set<std::string> kCoreRepr = {
                "Map", "Hash", "IterationMap", "List", "Uni", "NFC", "NFD", "NFKC", "NFKD",
                "IterationBuffer", "Array" };
            if (!kCoreRepr.count(bare)) {
                Value o; o.t = VT::Object; o.setObj(std::make_shared<ObjectData>());
                o.obj()->cls = it->second;
                return o;
            }
        }
    }
    return rtNqpOp(n->op, v); // eager leaf ops — shared with native codegen
}

// The eager (non-control) nqp ops, operating on ALREADY-evaluated arguments.
// Free function so native `--exe` codegen can call it directly: the lazy control
// forms (Stmts/While/Until/IfNull) are emitted as native C++ by the codegen and
// nqp::if/unless are Ternaries, so only these leaf ops need a runtime entry.
Value rtNqpOp(NqpOpc op, ValueList& v) {
    using O = NqpOpc;
    auto I = [&](size_t i) -> long long { return i < v.size() ? v[i].toInt() : 0; };
    // By reference: a Str argument is returned as-is, so the scanning ops below
    // don't copy the whole haystack once per character examined.
    static const CowStr kEmptyStr;
    // One slot per argument: an op may hold references to two coerced operands
    // at once (nqp::concat, nqp::eqat), so they can't share a scratch buffer.
    // CowStr rather than std::string so a Str argument is handed back with its
    // cached ASCII/grapheme state intact — that cache is what keeps the
    // scanning ops below O(1) per character instead of O(position).
    CowStr sTmp[8];
    auto S = [&](size_t i) -> const CowStr& {
        if (i >= v.size()) return kEmptyStr;
        if (v[i].t == VT::Str) return v[i].s;
        if (i >= 8) { sTmp[7] = v[i].toStr(); return sTmp[7]; }
        sTmp[i] = v[i].toStr();
        return sTmp[i];
    };
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
        // The string-scanning ops below each take an ASCII fast path first.
        // They are what a tokenizer written in Raku calls once per character,
        // always handing over the WHOLE text, so decoding that text per call
        // turns an O(n) scan into O(n^2) — 68 KB of JSON cost 1.9 s. On an
        // ASCII run a codepoint index is a byte index, so no decode is needed;
        // anything non-ASCII still falls through to utf8cp() unchanged.
        //
        // The ASCII test itself must be CACHED, not merely cheap: as a per-call
        // prefix scan it was still O(position), which left the quadratic exactly
        // where it was (13.9 s to parse 421 KB of JSON). cowAllAscii answers it
        // once per string — see its definition above.
        case O::Ordat: {
            const CowStr& cs = S(0);
            const std::string& s = cs.str();
            long long i = I(1);
            if (i < 0) return Value::integer(-1);
            if (cowAllAscii(cs))
                return Value::integer((size_t)i < s.size() ? (long long)(unsigned char)s[i] : -1);
            if (auto* ci = cowCpIndex(cs)) { // decode ONE codepoint, not the string
                long long ncp = (long long)ci->size() - 1;
                return Value::integer(i < ncp ? (long long)cpAtByte(s, (*ci)[(size_t)i]) : -1);
            }
            auto cps = utf8cp(s);
            return Value::integer(i < (long long)cps.size() ? (long long)cps[i] : -1);
        }
        case O::Eqat: {
            const CowStr& hc0 = S(0);
            const CowStr& ndc0 = S(1);
            const std::string& h = hc0.str();
            const std::string& nd = ndc0.str();
            long long at = I(2);
            if (at < 0) return Value::integer(0);
            size_t want = (size_t)at + nd.size();
            if (cowAllAscii(hc0) && cowAllAscii(ndc0))
                return Value::integer(want <= h.size() && h.compare((size_t)at, nd.size(), nd) == 0 ? 1 : 0);
            if (auto* ci = cowCpIndex(hc0)) {
                // UTF-8 is injective: codepoint-sequence equality IS byte
                // equality, so compare the needle's bytes at the byte offset of
                // codepoint `at` — no whole-string decode
                long long ncp = (long long)ci->size() - 1;
                if (at > ncp) return Value::integer(0);
                size_t hb = (*ci)[(size_t)at];
                return Value::integer(hb + nd.size() <= h.size() &&
                                      std::memcmp(h.data() + hb, nd.data(), nd.size()) == 0 ? 1 : 0);
            }
            auto hc = utf8cp(h), ndc = utf8cp(nd);
            if (at + (long long)ndc.size() > (long long)hc.size()) return Value::integer(0);
            for (size_t k = 0; k < ndc.size(); k++)
                if (hc[at + k] != ndc[k]) return Value::integer(0);
            return Value::integer(1);
        }
        case O::Substr: {
            const CowStr& cs = S(0);
            const std::string& s = cs.str();
            long long from = I(1);
            if (cowAllAscii(cs)) {
                long long len = v.size() > 2 ? I(2) : (long long)s.size() - from;
                if (from < 0) from = 0;
                if (from > (long long)s.size()) from = s.size();
                if (len < 0 || from + len > (long long)s.size()) len = (long long)s.size() - from;
                return Value::str(s.substr((size_t)from, (size_t)len));
            }
            if (auto* ci = cowCpIndex(cs)) { // byte slice via the cached offsets
                long long ncp = (long long)ci->size() - 1;
                long long len = v.size() > 2 ? I(2) : ncp - from;
                if (from < 0) from = 0;
                if (from > ncp) from = ncp;
                if (len < 0 || from + len > ncp) len = ncp - from;
                size_t a = (*ci)[(size_t)from];
                return Value::str(s.substr(a, (*ci)[(size_t)(from + len)] - a));
            }
            auto cps = utf8cp(s);
            long long len = v.size() > 2 ? I(2) : (long long)cps.size() - from;
            if (from < 0) from = 0;
            if (from > (long long)cps.size()) from = cps.size();
            if (len < 0 || from + len > (long long)cps.size()) len = cps.size() - from;
            std::string out;
            for (long long k = from; k < from + len; k++) out += cpToU8(cps[k]);
            return Value::str(out);
        }
        case O::Chars: {
            const CowStr& cs = S(0);
            if (cowAllAscii(cs)) return Value::integer((long long)cs.size());
            if (auto* ci = cowCpIndex(cs)) return Value::integer((long long)ci->size() - 1);
            return Value::integer((long long)utf8cp(cs.str()).size());
        }
        // NFC-composed, as Rakudo's NFG strings are: chaining nqp::concat with a
        // combining char must yield the composed grapheme (JSON::Fast's \u parser)
        case O::Concat: return Value::str(nfcNormalize(S(0) + S(1)));
        case O::Join: {
            std::string sep = S(0), out;
            if (v.size() > 1 && v[1].t == VT::Array && v[1].arr()) {
                bool first = true;
                for (auto& e : *v[1].arr()) { if (!first) out += sep; out += e.toStr(); first = false; }
            }
            return Value::str(nfcNormalize(std::move(out))); // NFG: compose across the joins
        }
        case O::Index: {
            const CowStr& hcs = S(0);
            const CowStr& ncs = S(1);
            const std::string& hs = hcs.str();
            const std::string& nds = ncs.str();
            long long from = v.size() > 2 ? I(2) : 0;
            if (from < 0) from = 0;
            if (cowAllAscii(hcs) && cowAllAscii(ncs)) {   // byte search — std::string::find is vectorized
                if ((size_t)from > hs.size()) return Value::integer(-1);
                auto at = hs.find(nds, (size_t)from);
                return Value::integer(at == std::string::npos ? -1 : (long long)at);
            }
            if (auto* ci = cowCpIndex(hcs)) {
                // byte-level find (needle bytes ⟺ needle codepoints), accepted
                // only on a codepoint boundary; answer = codepoint index
                long long ncp = (long long)ci->size() - 1;
                if (nds.empty()) return Value::integer(from <= ncp ? from : -1);
                if (from > ncp) return Value::integer(-1);
                size_t b = (*ci)[(size_t)from];
                while (true) {
                    b = hs.find(nds, b);
                    if (b == std::string::npos) return Value::integer(-1);
                    if ((static_cast<unsigned char>(hs[b]) & 0xC0) != 0x80) {
                        auto it = std::lower_bound(ci->begin(), ci->end(), (uint32_t)b);
                        return Value::integer((long long)(it - ci->begin()));
                    }
                    b++;
                }
            }
            auto h = utf8cp(hs), nd = utf8cp(nds);
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
            if (!v.empty() && v[0].t == VT::Array && v[0].arr())
                for (auto& e : *v[0].arr()) out += cpToU8((uint32_t)e.toInt());
            // Rakudo strings are NFG: building one from codes COMPOSES, whatever
            // normalization the codes were in (JSON::Fast round-trips through
            // NFD codes and back)
            return Value::str(nfcNormalize(std::move(out)));
        }
        case O::StrToCodes: {
            // (str, NORMALIZE_* const, target-list) — fills target, returns it
            const CowStr& cs = S(0);
            auto cps = utf8cp(cs.str());
            long long nm = I(1); // our const values: 1 NFC, 2 NFD, 3 NFKC, 4 NFKD
            int mode = nm == 1 ? 1 : nm == 2 ? 0 : nm == 3 ? 3 : nm == 4 ? 2 : -1;
            // Every normalization form is the identity on ASCII: nothing there
            // composes, decomposes or reorders. JSON::Fast asks for NFD once per
            // escaped string, so the full uniNormalize walk was ~4 us a call for
            // text that could not change.
            if (mode >= 0 && !cowAllAscii(cs)) cps = uniNormalize(cps, mode);
            Value target = v.size() > 2 ? v[2] : Value::array();
            if (target.t != VT::Array || !target.arr()) target = Value::array();
            // the answer IS a Uni in the requested form — keep a created
            // target's own tag (nqp::create(NFD) above), name an untagged one
            if (target.s.empty() && mode >= 0)
                target.s = mode == 1 ? "NFC" : mode == 0 ? "NFD"
                         : mode == 3 ? "NFKC" : "NFKD";
            target.arr()->clear();
            target.arr()->reserve(cps.size());
            for (auto cp : cps) target.arr()->push_back(Value::integer((long long)cp));
            return target;
        }
        case O::FindNotCClass: {
            const CowStr& cs = S(1);
            const std::string& s = cs.str();
            long long mask = I(0), start = I(2), len = I(3);
            long long from = std::max<long long>(start, 0);
            long long want = start + len;
            if (cowAllAscii(cs)) {   // byte index == codepoint index throughout
                long long end = std::min<long long>(want, (long long)s.size());
                for (long long k = from; k < end; k++)
                    if (!cclassHas(mask, (uint32_t)(unsigned char)s[k])) return Value::integer(k);
                return Value::integer(end);
            }
            if (auto* ci = cowCpIndex(cs)) { // decode only the scanned window
                long long ncp = (long long)ci->size() - 1;
                long long end = std::min<long long>(want, ncp);
                for (long long k = from; k < end; k++)
                    if (!cclassHas(mask, cpAtByte(s, (*ci)[(size_t)k]))) return Value::integer(k);
                return Value::integer(end);
            }
            auto cps = utf8cp(s);
            long long end = std::min<long long>(want, (long long)cps.size());
            for (long long k = from; k < end; k++)
                if (!cclassHas(mask, cps[k])) return Value::integer(k);
            return Value::integer(end);
        }
        case O::IsCClass: {
            const CowStr& cs = S(1);
            const std::string& s = cs.str();
            long long i = I(2);
            if (i < 0) return Value::integer(0);
            if (cowAllAscii(cs))
                return Value::integer((size_t)i < s.size() &&
                                      cclassHas(I(0), (uint32_t)(unsigned char)s[i]) ? 1 : 0);
            if (auto* ci = cowCpIndex(cs)) {
                long long ncp = (long long)ci->size() - 1;
                return Value::integer(i < ncp && cclassHas(I(0), cpAtByte(s, (*ci)[(size_t)i])) ? 1 : 0);
            }
            auto cps = utf8cp(s);
            return Value::integer(i < (long long)cps.size() &&
                                  cclassHas(I(0), cps[i]) ? 1 : 0);
        }
        case O::List: case O::ListI: case O::ListS: {
            Value out = Value::array();
            for (auto& x : v) out.arr()->push_back(x);
            return out;
        }
        case O::Elems:
            if (v[0].t == VT::Str) return Value::integer(v[0].blobElems()); // Buf/Blob byte count
            return Value::integer(v[0].t == VT::Array && v[0].arr() ? (long long)v[0].arr()->size()
                                 : v[0].t == VT::Hash && v[0].hash() ? (long long)v[0].hash()->size() : 0);
        case O::Atpos: case O::AtposI: {
            long long i = I(1);
            if (v[0].t == VT::Array && v[0].arr() && i >= 0 && i < (long long)v[0].arr()->size())
                return (*v[0].arr())[i];
            if (v[0].t == VT::Str && i >= 0 && i < v[0].blobElems())  // Buf/Blob byte
                return v[0].blobElemAt(i);
            return op == O::AtposI ? Value::integer(0) : Value::nil();
        }
        case O::Bindpos: case O::BindposI: {
            if (v[0].t == VT::Array && v[0].arr()) {
                long long i = I(1);
                while ((long long)v[0].arr()->size() <= i) v[0].arr()->push_back(Value::nil());
                (*v[0].arr())[i] = v[2];
            }
            return v.size() > 2 ? v[2] : Value::nil();
        }
        case O::Push: case O::PushI: case O::PushS:
            if (v[0].t == VT::Array && v[0].arr()) v[0].arr()->push_back(v[1]);
            return v[1];
        case O::PopS: {
            if (v[0].t == VT::Array && v[0].arr() && !v[0].arr()->empty()) {
                Value r = v[0].arr()->back(); v[0].arr()->pop_back(); return r;
            }
            return Value::nil();
        }
        case O::ShiftI: {
            if (v[0].t == VT::Array && v[0].arr() && !v[0].arr()->empty()) {
                Value r = v[0].arr()->front(); v[0].arr()->erase(v[0].arr()->begin()); return r;
            }
            return Value::integer(0);
        }
        case O::Splice: {
            // nqp::splice(target, source, offset, count) — replace in place
            if (v[0].t == VT::Array && v[0].arr()) {
                long long off = I(2), cnt = I(3);
                auto& t = *v[0].arr();
                if (off < 0) off = 0;
                if (off > (long long)t.size()) off = t.size();
                if (cnt < 0 || off + cnt > (long long)t.size()) cnt = t.size() - off;
                t.erase(t.begin() + off, t.begin() + off + cnt);
                if (v[1].t == VT::Array && v[1].arr())
                    t.insert(t.begin() + off, v[1].arr()->begin(), v[1].arr()->end());
            }
            return v[0];
        }
        case O::Hash: {
            Value h = Value::makeHash();
            for (size_t k = 0; k + 1 < v.size(); k += 2) (*h.hash())[v[k].toStr()] = v[k + 1];
            return h;
        }
        case O::Bindkey:
            if (v[0].t == VT::Hash && v[0].hash()) (*v[0].hash())[S(1)] = v[2];
            return v.size() > 2 ? v[2] : Value::nil();
        case O::IsNull:
            // VM-level null, which is NOT Raku's undefined: Rakudo answers 0 for
            // both Nil and Any. Our nqp hash ops return Value::nil() for a missing
            // key, so that is what stands in for it here — and a type object,
            // being a real Raku value, is not null.
            return Value::integer(!v.empty() && v[0].t == VT::Nil ? 1 : 0);
        case O::Atkey: {
            if (v.size() < 2 || v[0].t != VT::Hash || !v[0].hash()) return Value::nil();
            auto it = v[0].hash()->find(S(1));
            return it == v[0].hash()->end() ? Value::nil() : it->second;
        }
        case O::ExistsKey:
            return Value::integer(v.size() >= 2 && v[0].t == VT::Hash && v[0].hash() &&
                                  v[0].hash()->count(S(1)) ? 1 : 0);
        case O::DeleteKey:
            if (v.size() >= 2 && v[0].t == VT::Hash && v[0].hash()) v[0].hash()->erase(S(1));
            return v.empty() ? Value::nil() : v[0];
        case O::Create: {
            std::string tn = v[0].t == VT::Type ? v[0].s : v[0].typeName();
            // a `my class IterationMap is repr("VMHash")` declared INSIDE a module
            // carries the package prefix (JSON::Fast::IterationMap) — the repr is
            // what matters, and the base name is our only proxy for it
            if (auto q = tn.rfind("::"); q != std::string::npos) tn = tn.substr(q + 2);
            if (tn == "Map") { Value m = Value::makeHash(); m.hashKind = "Map"; return m; } // keeps Map identity through p6bindattrinvres
            if (tn == "Hash" || tn == "IterationMap") return Value::makeHash();
            if (tn == "List") { Value r = Value::array(); r.isList = true; return r; }
            // the Uni family keeps its NAME: `nqp::create(NFD)` must answer a
            // value that binds `Uni:D \codes` (JSON::Fast's unjsonify-string)
            if (tn == "Uni" || tn == "NFC" || tn == "NFD" || tn == "NFKC" || tn == "NFKD") {
                Value r = Value::array(); r.s = tn; return r;
            }
            return Value::array(); // IterationBuffer / … — a plain buffer
        }
        // Identity, as `===` reads it: reference types by their reference, a type
        // object by its name (IterationEnd is one), everything else by value.
        case O::Eqaddr: {
            if (v.size() < 2) return Value::integer(0);
            const Value& l = v[0]; const Value& r = v[1];
            bool same;
            if (l.t != r.t) same = false;
            else if (l.t == VT::Object) same = l.obj() == r.obj();
            else if (l.t == VT::Type)   same = l.s == r.s;
            else if (l.t == VT::Code)   same = l.code() == r.code();
            else if (l.t == VT::Array)  same = l.arr() == r.arr();
            else if (l.t == VT::Hash)   same = l.hash() == r.hash();
            else same = l.toStr() == r.toStr() && l.hashKind == r.hashKind;
            return Value::integer(same ? 1 : 0);
        }
        // nqp::objprimspec(T): 0 for an object type, 1/2/3 for the native
        // int/num/str kinds (10 is MoarVM's unsigned int, which AttrX::Mooish
        // treats as an int too).
        case O::ObjPrimSpec: {
            if (v.empty() || v[0].t != VT::Type) return Value::integer(0);
            const std::string& n = v[0].s;
            if (n == "str") return Value::integer(3);
            if (n == "num" || n == "num32" || n == "num64") return Value::integer(2);
            if (n == "int" || n == "int8" || n == "int16" || n == "int32" || n == "int64" ||
                n == "uint" || n == "uint8" || n == "uint16" || n == "uint32" || n == "uint64" ||
                n == "byte" || n == "long" || n == "longlong" || n == "ulong" || n == "ulonglong" ||
                n == "size_t" || n == "ssize_t" || n == "bool" || n == "atomicint")
                return Value::integer(1);
            return Value::integer(0);
        }
        // nqp::unipropcode('General_Category'): MoarVM hands back a small
        // integer it later resolves the name from again. There is no table to
        // index here, so the code IS the name — carried as a Str, which every
        // getuniprop_* below accepts as the property. Unknown names answer 0.
        case O::UniPropCode: {
            if (v.empty()) return Value::integer(0);
            return Value::str(v[0].toStr());
        }
        case O::HllBool:
            return Value::boolean(!v.empty() && v[0].truthy());
        case O::Istype: {
            std::string tn = v[1].t == VT::Type ? v[1].s : v[1].typeName();
            // `nqp::istype(Mu, Any)` is 0: Any sits BELOW Mu, so the Mu type
            // object conforms only to Mu — the same rule `~~` applies. CBOR::Simple
            // tells CBOR null (Any:U) from CBOR undefined (Mu) by exactly this.
            if (tn == "Any" && v[0].t == VT::Type && v[0].s == "Mu") return Value::integer(0);
            return Value::integer(rtTypeMatch(v[0], tn) ? 1 : 0);
        }
        case O::Getattr: {
            const std::string& nm = S(2);
            // a stamped Proxy-subclass instance (AttrProxy) keeps its attrs as
            // prefixed keys on the proxy hash itself
            if (v[0].t == VT::Hash && v[0].hashKind == "Proxy" && v[0].hash() &&
                v[0].hash()->count("\x01cls")) {
                auto it = v[0].hash()->find("\x01" "a" + nm);
                return it != v[0].hash()->end() ? it->second : Value::nil();
            }
            // '$!reified' / '$!storage' name the container's own backing store
            if (v[0].t == VT::Array || v[0].t == VT::Hash) return v[0];
            // a Pair's two attributes: `nqp::getattr($p, Pair, '$!key')` is how
            // Hash::int's STORE reads each pair without a method call
            if (v[0].t == VT::Pair) {
                if (nm == "$!key" || nm == "key") return Value::str(v[0].s);
                if (nm == "$!value" || nm == "value") return v[0].pairVal() ? *v[0].pairVal() : Value::any();
            }
            if (v[0].t == VT::Object && v[0].obj()) {
                std::string bare = nm.size() > 2 ? nm.substr(2) : nm;
                auto it = v[0].obj()->attrs.find(bare);
                if (it != v[0].obj()->attrs.end()) return it->second;
            }
            return Value::nil();
        }
        case O::Bindattr: case O::P6BindAttrInvRes: {
            const std::string& nm = S(2);
            if (v[0].t == VT::Hash && v[0].hashKind == "Proxy" && v[0].hash() &&
                v[0].hash()->count("\x01cls")) { // stamped Proxy-subclass instance
                (*v[0].hash())["\x01" "a" + nm] = v[3];
            } else if ((v[0].t == VT::Array && v[3].t == VT::Array && v[0].arr() && v[3].arr())) {
                *v[0].arr() = *v[3].arr();             // rebind the backing buffer
            } else if (v[0].t == VT::Hash && v[3].t == VT::Hash && v[0].hash() && v[3].hash()) {
                *v[0].hash() = *v[3].hash();           // (hashKind stays the invocant's: a Map keeps being a Map)
            } else if (v[0].t == VT::Object && v[0].obj()) {
                std::string bare = nm.size() > 2 ? nm.substr(2) : nm;
                v[0].obj()->attrs[bare] = v[3];
            }
            return op == O::P6BindAttrInvRes ? v[0] : v[3];
        }
        case O::P6ScalarWithValue:
            return v.size() > 1 ? v[1] : Value::nil(); // container wrap is a no-op for us
        case O::What: { // the type object of the value (like .WHAT, no method dispatch)
            if (v.empty()) return Value::typeObj("Mu");
            if (v[0].t == VT::Type) return v[0];
            if (v[0].t == VT::Object && v[0].obj() && v[0].obj()->cls)
                return Value::typeObj(v[0].obj()->cls->name);
            return Value::typeObj(v[0].typeName());
        }
        case O::IsList:
            return Value::integer(!v.empty() && v[0].t == VT::Array ? 1 : 0);
        case O::IsCont:
            // a Proxy IS a container; ordinary values reach us decontainerized
            return Value::integer(!v.empty() && v[0].t == VT::Hash &&
                                  v[0].hashKind == "Proxy" ? 1 : 0);
        case O::IsTrue:
            return Value::integer(!v.empty() && v[0].truthy() ? 1 : 0);
        case O::IsConcrete:
            return Value::integer(!v.empty() && rtIsDefined(v[0]) ? 1 : 0);
        case O::CloneOp: { // shallow clone: fresh backing store, same elements
            if (v.empty()) return Value::nil();
            Value c = v[0];
            if (c.t == VT::Array && c.arr()) { auto na = std::make_shared<ValueList>(*c.arr()); c.setArr(na); }
            else if (c.t == VT::Hash && c.hash()) { auto nh = std::make_shared<ValueMap>(*c.hash()); c.setHash(nh); }
            else if (c.t == VT::Object && c.obj()) { auto no = std::make_shared<ObjectData>(*c.obj()); c.setObj(no); }
            return c;
        }
        case O::Shift: { // generic array shift (ShiftI is the int variant)
            if (!v.empty() && v[0].t == VT::Array && v[0].arr() && !v[0].arr()->empty()) {
                Value f = v[0].arr()->front(); v[0].arr()->erase(v[0].arr()->begin()); return f;
            }
            return Value::nil();
        }
        case O::LockOp: case O::UnlockOp:
            // nqp::lock/unlock guard concurrent lazy builds; under the GIL the
            // build is already atomic, so the guard is a no-op here
            return Value::nil();
        case O::Null: return Value::nil();
        case O::IsNanOrInf: {
            double d = v.empty() ? 0 : v[0].toNum();
            return Value::integer(std::isnan(d) || std::isinf(d) ? 1 : 0);
        }
        // num comparisons (NaN != NaN falls out of C++ float semantics)
        case O::IseqN: return Value::integer(v.size() > 1 && v[0].toNum() == v[1].toNum() ? 1 : 0);
        case O::IsneN: return Value::integer(v.size() > 1 && v[0].toNum() != v[1].toNum() ? 1 : 0);
        case O::AtposN: { // native-num element read
            if (!v.empty() && v[0].t == VT::Array && v[0].arr()) {
                long long idx = I(1);
                if (idx >= 0 && idx < (long long)v[0].arr()->size())
                    return Value::number((*v[0].arr())[idx].toNum());
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
            Value out = Value::str(""); out.hashKind = "Buf"; identify(out);
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
            if (!v.empty() && (v[0].big() || (v.size() > 1 && v[1].big()))) {
                BigInt a = v[0].big() ? *v[0].big() : BigInt(v[0].toInt());
                BigInt b = (v.size() > 1) ? (v[1].big() ? *v[1].big() : BigInt(v[1].toInt())) : BigInt(0);
                BigInt r = a + b;
                return r.fitsLL() ? Value::integer(r.toLL()) : Value::bigint(r);
            }
            return Value::integer(I(0) + I(1));
        }
        case O::Decont: return v.empty() ? Value::nil() : v[0];        // container strip = identity
        case O::P6BoxS: return Value::str(v.empty() ? std::string() : v[0].toStr());
        // nqp::getcomp('Raku') — the running compiler, the same object
        // $*RAKU.compiler answers. NQP names a compiler by HLL, and the only
        // one this process has is ours; an unknown name is null, as in NQP,
        // which is what makes `nqp::getcomp("Raku") || nqp::getcomp('perl6')`
        // (the REPL-sandbox idiom) pick the first spelling that exists.
        case O::GetComp: {
            const std::string n = v.empty() ? std::string() : v[0].toStr();
            if (n != "Raku" && n != "raku" && n != "perl6" && n != "Perl6") return Value::nil();
            Value r = Value::makeHash();
            r.hashKind = "Compiler";
            (*r.hash())["name"] = Value::str("Raku++");
            (*r.hash())["ver"] = Value::str(kOracleEra);
            return r;
        }
        default: break;
    }
    throw RakuError{Value::typeObj("X::NYI"), "nqp op not implemented in this build"};
}

} // namespace rakupp
