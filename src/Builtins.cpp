#include "Interpreter.h"
#include <cstdint>
#include <memory>
#include <cstdlib>
#include "Unicode.h"
#include <complex>
#include <functional>
#include "Regex.h"
#include <algorithm>
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
#include <dirent.h>
#include "Platform.h"   // POSIX headers on Unix; Winsock + shims on Windows
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
struct LockState { std::recursive_mutex m; };            // Raku Lock (used reentrantly by protect)
struct SemaphoreState { std::mutex m; std::condition_variable cv; long count = 0; };

// A small hardcoded slice of the built-in type lattice (narrowest-first, widest-last),
// used by `.are` to find the narrowest common type of a list's elements.
static const std::vector<std::string>& typeAncestry(const std::string& t) {
    static const std::map<std::string, std::vector<std::string>> A = {
        {"Int",     {"Int","Real","Numeric","Cool","Any","Mu"}},
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
static std::string typeOfVal(const Value& v) { return v.t == VT::Type ? v.s : v.typeName(); }
static std::string lubType(const std::string& a, const std::string& b) {
    if (a == b) return a;
    for (auto& x : typeAncestry(a)) for (auto& y : typeAncestry(b)) if (x == y) return x;
    return "Mu";
}

// Spawn a child process, capture its stdout, with an optional wall-clock timeout.
// `gil` (if non-null) is the interpreter: the GIL is released for the child-process
// WAIT so sibling worker threads run — and spawn their own children — concurrently.
// The fork itself happens with the GIL held, so forks serialise (safe in a
// multithreaded process); only the poll/read/reap loop runs GIL-free.
static void spawnCapture(const std::vector<std::string>& argv, double timeoutSec,
                         std::string& out, int& exitCode, bool& timedout,
                         Interpreter* gil = nullptr, std::string* errOut = nullptr) {
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
    std::string cmd; // quoted command line
    for (size_t i = 0; i < argv.size(); i++) { if (i) cmd += ' '; cmd += '"'; for (char c : argv[i]) { if (c == '"') cmd += '\\'; cmd += c; } cmd += '"'; }
    STARTUPINFOA si; ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = outW;
    si.hStdError = errOut ? errW : nul;
    PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof(pi));
    std::vector<char> cmdbuf(cmd.begin(), cmd.end()); cmdbuf.push_back('\0');
    BOOL started = CreateProcessA(nullptr, cmdbuf.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi);
    CloseHandle(outW); if (errW) CloseHandle(errW); if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!started) { CloseHandle(outR); if (errR) CloseHandle(errR); return; }
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
    int pipefd[2], errfd[2];
    if (pipe(pipefd) != 0) return;
    if (errOut && pipe(errfd) != 0) { close(pipefd[0]); close(pipefd[1]); return; }
    pid_t pid = fork();
    if (pid < 0) { close(pipefd[0]); close(pipefd[1]); if (errOut) { close(errfd[0]); close(errfd[1]); } return; }
    if (pid == 0) { // child
        dup2(pipefd[1], STDOUT_FILENO);
        if (errOut) dup2(errfd[1], STDERR_FILENO);
        else { int devnull = open("/dev/null", O_WRONLY); if (devnull >= 0) dup2(devnull, STDERR_FILENO); }
        close(pipefd[0]); close(pipefd[1]);
        if (errOut) { close(errfd[0]); close(errfd[1]); }
        std::vector<char*> cargv;
        for (auto& s : argv) cargv.push_back(const_cast<char*>(s.c_str()));
        cargv.push_back(nullptr);
        execvp(cargv[0], cargv.data());
        _exit(127);
    }
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
            if (elapsed > timeoutSec) { kill(pid, SIGKILL); timedout = true; break; }
        }
    }
    int status;
    waitpid(pid, &status, 0);            // reap; safe now that the pipe is drained (or we killed it)
    if (!timedout) exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    close(fd);
    if (efd >= 0) close(efd);
    if (parked) gil->gilUnpark(true);    // reacquire the GIL before touching interpreter state
#endif
}

// Spawn a child, feed `input` to its stdin, and capture its stdout. Uses poll on
// both pipes so it won't deadlock when the child's output exceeds the pipe buffer
// while we're still writing input (as pandoc can on a large page).
static void spawnWithInput(const std::vector<std::string>& argv, const std::string& input,
                           std::string& out, int& exitCode, Interpreter* gil = nullptr) {
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
    BOOL started = CreateProcessA(nullptr, cmdbuf.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi);
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
    int inPipe[2], outPipe[2];
    if (pipe(inPipe) != 0) return;
    if (pipe(outPipe) != 0) { close(inPipe[0]); close(inPipe[1]); return; }
    pid_t pid = fork();
    if (pid < 0) { close(inPipe[0]); close(inPipe[1]); close(outPipe[0]); close(outPipe[1]); return; }
    if (pid == 0) { // child
        dup2(inPipe[0], STDIN_FILENO);
        dup2(outPipe[1], STDOUT_FILENO);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) dup2(devnull, STDERR_FILENO);
        close(inPipe[0]); close(inPipe[1]); close(outPipe[0]); close(outPipe[1]);
        std::vector<char*> cargv;
        for (auto& s : argv) cargv.push_back(const_cast<char*>(s.c_str()));
        cargv.push_back(nullptr);
        execvp(cargv[0], cargv.data());
        _exit(127);
    }
    close(inPipe[0]); close(outPipe[1]);
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
    int status;
    waitpid(pid, &status, 0);
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
                double lim = arg.t == VT::Nil ? 1 : (arg.t == VT::Whatever ? 1.0/0.0 : arg.toNum());
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
    std::string out; int code; bool timedout;
    spawnCapture(argv, timeoutSec, out, code, timedout, this);
    auto taps = proc.hash->find("taps");
    if (taps != proc.hash->end() && taps->second.arr)
        for (auto& cb : *taps->second.arr) { ValueList ca{Value::str(out)}; callCallable(cb, ca); }
    (*proc.hash)["exitcode"] = Value::integer(code);
    (*proc.hash)["timedout"] = Value::boolean(timedout);
    (*promise.hash)["status"] = Value::str(timedout ? "Broken" : "Kept");
}

static bool defined(const Value& v) { return v.t != VT::Nil && v.t != VT::Any && v.t != VT::Type && !(v.t == VT::Hash && v.hashKind == "Failure"); }

// `.raku` / `.perl` — an EVAL-round-trippable representation of a value (as opposed
// to `.gist`, which is the human-readable form). Recursive over containers.
static std::string rakuRepr(const Value& v, int depth, std::set<const void*>& seen);
static std::string rakuRepr(const Value& v) { std::set<const void*> seen; return rakuRepr(v, 0, seen); }
static std::string rakuStrLit(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
        else if (c == '\n') o += "\\n";
        else if (c == '\t') o += "\\t";
        else if (c == '\r') o += "\\r";
        else o += (char)c;
    }
    return o + "\"";
}
static bool rakuIdentKey(const std::string& s) {
    if (s.empty() || !(std::isalpha((unsigned char)s[0]) || s[0] == '_')) return false;
    for (unsigned char c : s) if (!(std::isalnum(c) || c == '_' || c == '-')) return false;
    return true;
}
static std::string rakuRepr(const Value& v, int depth, std::set<const void*>& seen) {
    // Guard against self-referential / deeply-nested data (`$foo<b> = $foo`): recursing
    // blindly builds an unbounded string and exhausts memory. Detect a revisited
    // container (a cycle) and stop; a large depth cap backstops pathological nesting.
    if (depth > 512) return "...";
    switch (v.t) {
        case VT::Nil:  return "Nil";
        case VT::Any:  return "Any";
        case VT::Bool: return v.b ? "Bool::True" : "Bool::False";
        case VT::Type: return v.s;
        case VT::Str:  return rakuStrLit(v.s);
        case VT::Int:  return v.toStr();
        case VT::Rat: {
            std::string n = v.ratN ? v.ratN->toString() : "0";
            std::string d = v.ratD ? v.ratD->toString() : "1";
            if (v.fatRat) return "FatRat.new(" + n + ", " + d + ")"; // FatRat.raku is explicit
            return d == "1" ? n : "<" + n + "/" + d + ">";
        }
        case VT::Complex: return "<" + v.gist() + ">";
        case VT::Range:
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
            if (v.arr && !seen.insert(v.arr.get()).second) return v.isList ? "(...)" : "[...]"; // cycle
            std::string o(1, v.isList ? '(' : '[');
            if (v.arr) {
                bool first = true;
                for (auto& e : *v.arr) { if (!first) o += ", "; first = false; o += rakuRepr(e, depth + 1, seen); }
                if (v.isList && v.arr->size() == 1) o += ",";
                seen.erase(v.arr.get());
            }
            o += v.isList ? ')' : ']';
            return o;
        }
        case VT::Hash: {
            if (v.hash && !seen.insert(v.hash.get()).second) return "{...}"; // cycle
            std::vector<std::string> keys;
            if (v.hash) for (auto& kv : *v.hash) keys.push_back(kv.first);
            std::sort(keys.begin(), keys.end());
            std::string o = "{"; bool first = true;
            for (auto& k : keys) {
                if (!first) o += ", "; first = false;
                Value val = v.hash->at(k);
                o += rakuIdentKey(k) ? ":" + k + "(" + rakuRepr(val, depth + 1, seen) + ")"
                                     : rakuStrLit(k) + " => " + rakuRepr(val, depth + 1, seen);
            }
            if (v.hash) seen.erase(v.hash.get());
            return o + "}";
        }
        default: return v.gist();
    }
}

// The positional arity of a Code value — how many elements `.map`/`for` feed it
// per iteration (`-> $k,$v {…}` → 2; `{ $^a … $^b }` → 2; `{ $_ }` / builtin → 1).
static size_t codeArity(const Value& code) {
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
static std::vector<uint32_t> utf8cp(const std::string& s) {
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
static std::string cpToUtf8(uint32_t cp) {
    std::string r;
    if (cp < 0x80) r += (char)cp;
    else if (cp < 0x800) { r += (char)(0xC0 | (cp >> 6)); r += (char)(0x80 | (cp & 0x3f)); }
    else if (cp < 0x10000) { r += (char)(0xE0 | (cp >> 12)); r += (char)(0x80 | ((cp >> 6) & 0x3f)); r += (char)(0x80 | (cp & 0x3f)); }
    else { r += (char)(0xF0 | (cp >> 18)); r += (char)(0x80 | ((cp >> 12) & 0x3f)); r += (char)(0x80 | ((cp >> 6) & 0x3f)); r += (char)(0x80 | (cp & 0x3f)); }
    return r;
}
static uint32_t toLowerCp(uint32_t c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    if ((c >= 0xC0 && c <= 0xD6) || (c >= 0xD8 && c <= 0xDE)) return c + 32; // Latin-1
    if (c >= 0x100 && c <= 0x17e && (c % 2 == 0)) return c + 1;              // Latin Ext-A
    if (c >= 0x391 && c <= 0x3A9 && c != 0x3A2) return c + 32;               // Greek
    return c;
}
static uint32_t toUpperCp(uint32_t c) {
    if (c >= 'a' && c <= 'z') return c - 32;
    if ((c >= 0xE0 && c <= 0xF6) || (c >= 0xF8 && c <= 0xFE)) return c - 32;
    if (c >= 0x101 && c <= 0x17f && (c % 2 == 1)) return c - 1;
    if (c >= 0x3B1 && c <= 0x3C9 && c != 0x3C2) return c - 32;
    return c;
}
// Full (multi-codepoint) uppercase mappings from SpecialCasing.txt — common subset.
// Most codepoints uppercase 1:1, but a few expand (e.g. ß -> "SS").
static std::string upperCase1(uint32_t c) {
    switch (c) {
        case 0x00DF: return "SS";                          // ß LATIN SMALL LETTER SHARP S
        case 0xFB00: return "FF";  case 0xFB01: return "FI";   // ﬀ ﬁ
        case 0xFB02: return "FL";  case 0xFB03: return "FFI";  // ﬂ ﬃ
        case 0xFB04: return "FFL"; case 0xFB05: return "ST";   // ﬄ ﬅ
        case 0xFB06: return "ST";                          // ﬆ
    }
    return cpToUtf8(toUpperCp(c));
}
static std::string mapCase(const std::string& s, bool upper, int tcMode) {
    // tcMode: 0=none, 1=titlecase first only, 2=titlecase first + lowercase rest
    auto cps = utf8cp(s);
    std::string r;
    for (size_t i = 0; i < cps.size(); i++) {
        uint32_t c = cps[i];
        if (tcMode) r += cpToUtf8((i == 0) ? toUpperCp(c) : (tcMode == 2 ? toLowerCp(c) : c));
        else if (upper) r += upperCase1(c);                // may expand (ß -> SS)
        else r += cpToUtf8(toLowerCp(c));
    }
    return r;
}
static long long cpCount(const std::string& s) { return (long long)utf8cp(s).size(); }

// Unicode combining marks (Mn/Mc/Me — the common ranges) — they attach to the preceding grapheme.
static bool isCombiningMark(uint32_t c) {
    return (c >= 0x0300 && c <= 0x036F) || (c >= 0x0483 && c <= 0x0489) ||
           (c >= 0x0591 && c <= 0x05BD) || c == 0x05BF || c == 0x05C1 || c == 0x05C2 ||
           c == 0x05C4 || c == 0x05C5 || c == 0x05C7 ||
           (c >= 0x0610 && c <= 0x061A) || (c >= 0x064B && c <= 0x065F) || c == 0x0670 ||
           (c >= 0x06D6 && c <= 0x06DC) || (c >= 0x06DF && c <= 0x06E4) ||
           (c >= 0x06E7 && c <= 0x06E8) || (c >= 0x06EA && c <= 0x06ED) ||
           c == 0x0711 || (c >= 0x0730 && c <= 0x074A) ||
           (c >= 0x07A6 && c <= 0x07B0) || (c >= 0x07EB && c <= 0x07F3) ||
           (c >= 0x0900 && c <= 0x0903) || (c >= 0x093A && c <= 0x094F) ||
           (c >= 0x0951 && c <= 0x0957) || (c >= 0x0E31 && c <= 0x0E3A) ||
           (c >= 0x0E47 && c <= 0x0E4E) || (c >= 0x1AB0 && c <= 0x1AFF) ||
           (c >= 0x1DC0 && c <= 0x1DFF) || (c >= 0x20D0 && c <= 0x20FF) ||
           (c >= 0xFE20 && c <= 0xFE2F);
}
// Count grapheme clusters via the full UAX #29 algorithm (emoji/flags/Hangul-aware).
static long long graphemeCount(const std::string& s) { return (long long)uniGraphemeCount(utf8cp(s)); }

static std::string joinValues(const ValueList& items, const std::string& sep) {
    std::string out;
    for (size_t i = 0; i < items.size(); i++) {
        if (i) out += sep;
        out += items[i].toStr();
    }
    return out;
}

// A lazy @-array over the integers from `start` upward (an infinite `…..Inf` range).
static Value makeInfArray(long long start) {
    Value a = Value::array(); a.isList = true;
    auto st = std::make_shared<LazySeqState>(); st->infinite = true;
    auto next = std::make_shared<long long>(start);
    st->appendNext = [next](ValueList& cache) -> bool { cache.push_back(Value::integer((*next)++)); return true; };
    a.ext = st;
    return a;
}

static ValueList toList(const Value& v) {
    if (v.t == VT::Array && v.arr) return *v.arr;
    if (v.t == VT::Range) return v.flatten();
    if (v.t == VT::Hash && v.hash) {
        ValueList out;
        for (auto& kv : *v.hash) out.push_back(Value::pair(kv.first, kv.second));
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
                            int width, int prec, bool signFlags) {
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
    // Octal/hex `#` prefixes sit ahead of the sign (Raku quirk: sprintf("%#o",-4) → "0-100",
    // sprintf("%#x",-256) → "0x-100"); binary keeps the sign first ("-0b100").
    bool prefixFirst = (base == 8 || base == 16);
    std::string core = prefixFirst ? prefix + sign + digits : sign + prefix + digits;
    if ((int)core.size() < width) {
        int pad = width - (int)core.size();
        if (flags.find('-') != std::string::npos) core += std::string(pad, ' ');
        else if (flags.find('0') != std::string::npos && prec < 0)
            // zero-pad: for o/x the fill sits after the prefix but BEFORE the sign
            // (sprintf("%08o",-64) → "0000-100"); for d/b it sits after sign+prefix.
            core = prefixFirst ? prefix + std::string(pad, '0') + sign + digits
                               : sign + prefix + std::string(pad, '0') + digits;
        else core = std::string(pad, ' ') + core;
    }
    return core;
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

std::string doSprintf(const std::string& fmt, const ValueList& args) {
    std::string out;
    size_t ai = 0;
    auto nextArg = [&]() -> Value { return ai < args.size() ? args[ai++] : Value::any(); };
    for (size_t i = 0; i < fmt.size(); i++) {
        if (fmt[i] != '%') { out += fmt[i]; continue; }
        size_t j = i + 1;
        std::string flags;
        while (j < fmt.size() && std::strchr("-+ 0#", fmt[j])) flags += fmt[j++];
        // width (digits or `*` = from argument; negative `*` implies left-justify)
        int width = 0; bool hasWidth = false;
        if (j < fmt.size() && fmt[j] == '*') { j++; long long w = nextArg().toInt();
            if (w < 0) { flags += '-'; w = -w; } width = (int)w; hasWidth = true; }
        else while (j < fmt.size() && std::isdigit((unsigned char)fmt[j])) { width = width*10 + (fmt[j]-'0'); hasWidth = true; j++; }
        // precision (.digits or .* ; a negative `.*` means "no precision")
        int prec = -1;
        if (j < fmt.size() && fmt[j] == '.') { j++; prec = 0;
            if (j < fmt.size() && fmt[j] == '*') { j++; long long p = nextArg().toInt(); prec = p < 0 ? -1 : (int)p; }
            else { prec = 0; while (j < fmt.size() && std::isdigit((unsigned char)fmt[j])) { prec = prec*10 + (fmt[j]-'0'); j++; } }
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
            case 'u':           out += fmtRadix(nextArg().toInt(), 10, false, flags, width, prec, false); break;
            case 'b':           out += fmtRadix(nextArg().toInt(), 2, false, flags, width, prec, true); break;
            case 'B':           out += fmtRadix(nextArg().toInt(), 2, true,  flags, width, prec, true); break;
            case 'o':           out += fmtRadix(nextArg().toInt(), 8, false, flags, width, prec, false); break;
            case 'x':           out += fmtRadix(nextArg().toInt(), 16, false, flags, width, prec, false); break;
            case 'X':           out += fmtRadix(nextArg().toInt(), 16, true,  flags, width, prec, false); break;
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
                // Raku ignores `#` for floats (no forced trailing dot): sprintf("%#.0f",0) → "0".
                std::string ff; for (char c : flags) if (c != '#') ff += c;
                std::string spec = "%" + ff;
                if (hasWidth) spec += std::to_string(width);
                if (prec >= 0) spec += "." + std::to_string(prec);
                spec += conv;
                std::vector<char> buf(std::max(64, width + prec + 64));
                snprintf(buf.data(), buf.size(), spec.c_str(), nextArg().toNum());
                out += buf.data(); break;
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
                    // `0` fill only applies without a precision (else spaces): %08.2s → "      Fo".
                    char fill = (flags.find('0') != std::string::npos && flags.find('-') == std::string::npos && prec < 0) ? '0' : ' ';
                    sv = (flags.find('-') != std::string::npos) ? sv + std::string(pad,' ') : std::string(pad,fill) + sv; }
                out += sv; break;
            }
            default: { out += '%'; out += flags; if (hasWidth) out += std::to_string(width); out += conv; break; }
        }
        i = j;
    }
    return out;
}

static bool deepEq(const Value& a, const Value& b) {
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
    return valueEq(a, b);
}

// Build a Set/Bag/Mix (hash-backed, hashKind tag) from a flat list of values/pairs.
static Value makeBaggy(const ValueList& items, const std::string& kind) {
    Value h = Value::makeHash();
    h.hashKind = kind;
    bool isSet = kind.find("Set") == 0;
    auto add = [&](const std::string& k, long long cnt) {
        if (isSet) { if (cnt > 0) (*h.hash)[k] = Value::boolean(true); else h.hash->erase(k); return; }
        long long c = 0; auto it = h.hash->find(k); if (it != h.hash->end()) c = it->second.toInt();
        c += cnt; if (c != 0) (*h.hash)[k] = Value::integer(c); else h.hash->erase(k);
    };
    for (auto& v : items) {
        if (v.t == VT::Pair) add(v.s, v.pairVal ? v.pairVal->toInt() : 0);
        else add(v.toStr(), 1);
    }
    return h;
}

// Build a Signature introspection value from a routine's parameters.
// Rendered as a Hash tagged "Signature" carrying its .raku text and arity/count.
static Value makeSignature(const Callable* c) {
    std::string sig = "(";
    long long arity = 0, count = 0; bool slurpy = false, first = true;
    if (c && c->params) for (auto& p : *c->params) {
        if (p.invocant) continue;
        if (!first) sig += ", ";
        first = false;
        if (!p.type.empty()) sig += p.type + " ";
        sig += p.name.empty() ? std::string(1, p.sigil) : p.name;
        if (p.named) sig += "!"; // marked as named; doesn't affect arity/count below
        else if (p.optional) sig += "?";
        if (!p.named) { if (p.slurpy) slurpy = true; else { count++; if (!p.optional) arity++; } }
    }
    sig += ")";
    Value s = Value::makeHash(); s.hashKind = "Signature";
    (*s.hash)["str"] = Value::str(sig);
    (*s.hash)["arity"] = Value::integer(arity);
    (*s.hash)["count"] = slurpy ? Value::number(1.0/0.0) : Value::integer(count);
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
Value Interpreter::methodCall(Value inv, const std::string& m, ValueList args, const std::vector<ExprPtr>* rwArgs) {
    auto a0 = [&]() -> Value { return args.empty() ? Value::any() : args[0]; };
    if (std::getenv("RAKUPP_TRACE")) std::cerr << "[M] ." << m << " on type=" << (int)inv.t << (inv.t==VT::Object && inv.obj && inv.obj->cls ? " ("+inv.obj->cls->name+")" : "") << "\n";
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
        if (m == "List" || m == "Seq" || m == "Slip" || m == "list") return asList();
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
    // execution at the statement after the one that threw.
    if (m == "resume") throw ResumeEx{};
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
    if (!m.empty() && m[0] == '^') {
        std::string mm = m.substr(1);
        if (mm == "name") return Value::str(inv.typeName());
        if (mm == "WHAT") return Value::typeObj(inv.typeName());
        // meta-methods (.^methods/.^attributes/.^parents/…) resolve against the
        // type (HOW), even when called on an instance.
        Value tobj = (inv.t == VT::Object && inv.obj && inv.obj->cls) ? Value::typeObj(inv.obj->cls->name) : inv;
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
        if (m == "skip-one") { bool ok = posV.i < n; if (ok) posV.i++; return Value::boolean(ok); }
        if (m == "skip-at-least") {
            long long want = args.empty() ? 0 : args[0].toInt();
            long long skipped = std::min(want, n - posV.i); if (skipped < 0) skipped = 0;
            posV.i += skipped;
            return Value::boolean(skipped >= want);
        }
        if (m == "skip-at-least-pull-one") {
            long long want = args.empty() ? 0 : args[0].toInt();
            posV.i = std::min(n, posV.i + std::max(0LL, want));
            return posV.i < n ? items[posV.i++] : iterEnd();
        }
        if (m == "count-only") return Value::integer(n - posV.i); // remaining, no advance
        if (m == "bool-only") return Value::boolean(posV.i < n);
        if (m == "is-lazy") return Value::boolean(false);
        if (m == "is-deterministic" || m == "is-monotonically-increasing") return Value::boolean(true);
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
        if (m == "raku" || m == "gist" || m == "Str" || m == "perl")
            return inv.hash->count("str") ? (*inv.hash)["str"] : Value::str("()");
        if (m == "arity") return inv.hash->count("arity") ? (*inv.hash)["arity"] : Value::integer(0);
        if (m == "count") return inv.hash->count("count") ? (*inv.hash)["count"] : Value::integer(0);
    }

    // A `but`/`does` mixin over a non-object base: a composed role/class method wins,
    // object-identity/introspection methods stay on the object, and every other
    // method (coercions, arithmetic-ish, base-type methods) delegates to the box.
    if (inv.t == VT::Object && inv.obj && inv.obj->hasBoxed && inv.obj->cls &&
        !inv.obj->cls->findMethod(m) && !inv.obj->cls->findAttr(m)) {
        static const std::set<std::string> keepOnObj = {
            "does", "HOW", "WHAT", "WHICH", "defined", "DEFINITE"};
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
    // Buf/Blob.new(byte, byte, …) — a byte buffer, stored as a Str of those bytes.
    if (inv.t == VT::Type && (inv.s == "Buf" || inv.s == "Blob") && (m == "new" || m == "allocate")) {
        std::string bytes;
        std::function<void(const Value&)> add = [&](const Value& v) {
            if (v.t == VT::Array && v.arr) { for (auto& e : *v.arr) add(e); }
            else bytes += (char)(unsigned char)(v.toInt() & 0xFF);
        };
        for (auto& a : args) add(a);
        Value b = Value::str(bytes); b.hashKind = "Blob"; return b;
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
    if (inv.t == VT::Type && (inv.s == "Lock" || inv.s == "Semaphore")) {
        if (m == "new") {
            Value v = Value::makeHash();
            if (inv.s == "Semaphore") {
                v.hashKind = "Semaphore";
                long n = args.empty() ? 1 : args[0].toInt();
                (*v.hash)["count"] = Value::integer(n);
                if (parallelMode_) { auto st = std::make_shared<SemaphoreState>(); st->count = n; v.ext = st; }
            }
            else {
                v.hashKind = "Lock";
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
        if (m == "version" || m == "lang-version") { Value v = Value::str(isComp ? "6.d" : langVer); v.hashKind = "Version"; return v; }
        if (m == "auth" || m == "authority") return Value::str("The Raku Community");
        if (m == "desc") return Value::str("Raku++ — a C++ Raku interpreter");
        if (m == "signature") { Value b = Value::str("Raku++"); b.hashKind = "Blob"; return b; } // non-empty Blob
        if (m == "id" || m == "release") return Value::str("2026.07");
        if (m == "codename") return Value::str("Raku++");
        if (m == "gist" || m == "Str" || m == "raku" || m == "perl") return Value::str(nm + " (" + (isComp ? "6.d" : langVer) + ")");
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
            if (isClosed()) throw RakuError{Value::typeObj("X::Channel::SendOnClosed"), "Cannot send on a closed channel"};
            Value v = args.empty() ? Value::any() : args[0]; q.push_back(v); return v;
        }
        if (m == "poll") {
            if (q.empty()) { keepClosedIfDrained(); return Value::nil(); }
            Value v = q.front(); q.erase(q.begin()); keepClosedIfDrained(); return v;
        }
        if (m == "receive") {
            if (q.empty()) {
                if (isClosed()) {
                    if (inv.hash->count("failCause")) throw RakuError{(*inv.hash)["failCause"], "Channel failed"};
                    throw RakuError{Value::typeObj("X::Channel::ReceiveOnClosed"), "Cannot receive on a closed channel"};
                }
                return Value::nil(); // would block; single-thread model returns Nil
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
        if (m == "list" || m == "Seq" || m == "Supply") { Value o = Value::array(); *o.arr = q; o.isList = true; return o; }
        if (m == "elems") return Value::integer((long long)q.size());
    }
    // Thread — under the GIL a Thread.start runs its block eagerly, but we bump
    // threadDepth_ so `is-initial-thread` correctly reads False inside the block.
    if (inv.t == VT::Type && inv.s == "Thread") {
        if (m == "is-initial-thread") return Value::boolean(threadDepth_ == 0);
        if (m == "start" || m == "run") {
            Value code; for (auto& x : args) if (x.t == VT::Code) code = x;
            Value t = Value::makeHash(); t.hashKind = "Thread";
            for (auto& x : args) if (x.t == VT::Pair && x.s == "name" && x.pairVal) (*t.hash)["name"] = *x.pairVal;
            if (code.t == VT::Code) { threadDepth_++; try { callCallable(code, {}); } catch (...) { threadDepth_--; throw; } threadDepth_--; }
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
        if (m == "finish" || m == "join") return inv;
        if (m == "run" || m == "start") { if (inv.hash->count("code")) { threadDepth_++; try { callCallable((*inv.hash)["code"], {}); } catch (...) { threadDepth_--; throw; } threadDepth_--; } return inv; }
        if (m == "id") return Value::integer(1);
        if (m == "name") return inv.hash->count("name") ? (*inv.hash)["name"] : Value::str("");
        if (m == "Str" || m == "gist") return Value::str("Thread");
    }
    if (inv.t == VT::Type && (inv.s == "Supplier" || inv.s == "Supplier::Preserving")) {
        if (m == "new" || m == "preserving") { Value s = Value::makeHash(); s.hashKind = "Supplier"; (*s.hash)["taps"] = Value::array(); return s; }
    }
    // Supplier: a live push source. Its Supply shares the taps list; emit/done fan out to them.
    if (inv.t == VT::Hash && inv.hashKind == "Supplier") {
        if (m == "Supply") { Value s = Value::makeHash(); s.hashKind = "Supply"; (*s.hash)["supplier"] = inv; return s; } // live (no "values")
        if (m == "emit") { Value v = args.empty() ? Value::any() : args[0];
            if (inv.hash->count("taps")) for (auto& t : *(*inv.hash)["taps"].arr) {
                if (t.t != VT::Hash) continue;
                if (t.hash->count("closed") && (*t.hash)["closed"].truthy()) continue; // head/first already finished
                bool complete = false;
                ValueList outs = applyTapChain(t, v, complete);
                if (t.hash->count("emit") && (*t.hash)["emit"].t == VT::Code)
                    for (auto& o : outs) { ValueList one{o}; callCallable((*t.hash)["emit"], one); }
                if (complete) { // head(n)/first done → fire the tap's done and release a react source
                    (*t.hash)["closed"] = Value::boolean(true);
                    if (t.hash->count("done") && (*t.hash)["done"].t == VT::Code) { ValueList none; callCallable((*t.hash)["done"], none); }
                    if (t.ext) { auto ctx = std::static_pointer_cast<ReactCtx>(t.ext); std::lock_guard<std::mutex> lk(ctx->m); if (ctx->liveSources > 0) ctx->liveSources--; ctx->cv.notify_all(); }
                }
            }
            return Value::boolean(true); }
        if (m == "done") {
            if (inv.hash->count("taps")) for (auto& t : *(*inv.hash)["taps"].arr) {
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
        if (m == "from-list") { // each arg is one value; Ranges/Lists expand, but [..] items don't
            ValueList out;
            for (auto& a : args) {
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
            for (auto& x : args) if (x.t != VT::Pair) argv.arr->push_back(x);
            (*p.hash)["argv"] = argv; (*p.hash)["taps"] = Value::array();
            return p;
        }
    }
    if (inv.t == VT::Hash && inv.hashKind == "Proc::Async") {
        if (m == "stdout" || m == "stderr" || m == "Supply") { Value s = Value::makeHash(); s.hashKind = "Supply"; (*s.hash)["proc"] = inv; (*s.hash)["stream"] = Value::str(m); return s; }
        if (m == "start") { Value pr = Value::makeHash(); pr.hashKind = "Promise"; (*pr.hash)["kind"] = Value::str("proc"); (*pr.hash)["proc"] = inv; (*pr.hash)["status"] = Value::str("Planned"); return pr; }
        if (m == "kill" || m == "close-stdin" || m == "print" || m == "say" || m == "write" || m == "put") return Value::boolean(true);
    }
    if (inv.t == VT::Hash && inv.hashKind == "Supply") {
        bool listy = inv.hash->count("values");
        auto vals = [&]() -> ValueList { return listy ? *(*inv.hash)["values"].arr : ValueList{}; };
        auto mkSupply = [&](ValueList v) { Value s = Value::makeHash(); s.hashKind = "Supply"; Value a = Value::array(); *a.arr = std::move(v); (*s.hash)["values"] = a; return s; };
        if (m == "live") return Value::boolean(inv.hash->count("supplier") > 0);
        if (m == "Supply") return inv;
        if (m == "list" || m == "List" || m == "Seq" || m == "eager") { Value o = Value::array(); *o.arr = vals(); o.isList = true; return o; }
        if (m == "Channel") { // drain a (from-list) Supply into a closed Channel
            Value c = Value::makeHash(); c.hashKind = "Channel";
            Value q = Value::array(); *q.arr = vals(); (*c.hash)["queue"] = q;
            (*c.hash)["closed"] = Value::boolean(true);
            auto ps = std::make_shared<PromiseState>(); ps->done = true; ps->result = Value::boolean(true); c.ext = ps;
            Value cp = Value::makeHash(); cp.hashKind = "Promise"; cp.ext = ps; (*cp.hash)["status"] = Value::str("Kept");
            (*c.hash)["closedPromise"] = cp;
            return c;
        }
        if (m == "elems") return Value::integer((long long)vals().size());
        if (m == "tap" || m == "act") {
            Value emit = args.empty() ? Value::nil() : args[0];
            Value done, quit;
            for (auto& a : args) if (a.t == VT::Pair) { if (a.s == "done" && a.pairVal) done = *a.pairVal; else if (a.s == "quit" && a.pairVal) quit = *a.pairVal; }
            if (inv.hash->count("supplier")) {
                // live Supply: register the callbacks with the Supplier; emit/done fan out later
                Value tapRec = Value::makeHash();
                (*tapRec.hash)["emit"] = emit; (*tapRec.hash)["done"] = done; (*tapRec.hash)["quit"] = quit;
                // carry any transform chain, giving each step its own fresh mutable state
                if (inv.hash->count("chain")) {
                    Value chain = Value::array();
                    for (auto& step : *(*inv.hash)["chain"].arr) {
                        Value s2 = Value::makeHash(); *s2.hash = *step.hash;
                        (*s2.hash)["state"] = Value::makeHash();
                        chain.arr->push_back(s2);
                    }
                    (*tapRec.hash)["chain"] = chain;
                }
                Value sup = (*inv.hash)["supplier"];
                if (sup.t == VT::Hash && sup.hash->count("taps")) (*sup.hash)["taps"].arr->push_back(tapRec);
                Value t = Value::makeHash(); t.hashKind = "Tap"; return t;
            }
            // eager: push every value to the emit callback, then run the done phaser.
            if (listy) {
                if (emit.t == VT::Code) for (auto& v : vals()) { ValueList one{v}; callCallable(emit, one); }
                if (done.t == VT::Code) { ValueList none; callCallable(done, none); }
            } else if (!args.empty() && args[0].t == VT::Code && (*inv.hash)["stream"].toStr() == "stdout") {
                Value proc = (*inv.hash)["proc"]; (*proc.hash)["taps"].arr->push_back(args[0]);
            }
            Value t = Value::makeHash(); t.hashKind = "Tap"; return t;
        }
        if (listy && (m == "min" || m == "max")) {
            // Supply.min/max is a *running* extreme: emit each value that is a new
            // minimum/maximum of the stream so far (compared by an optional &mapper).
            bool wantMax = (m == "max");
            Value mapper = (!args.empty() && args[0].t == VT::Code) ? args[0] : Value::nil();
            ValueList out; bool have = false; Value bestKey;
            for (auto& v : vals()) {
                Value key = v;
                if (mapper.t == VT::Code) { ValueList one{v}; key = callCallable(mapper, one); }
                if (!have || (wantMax ? valueCmp(key, bestKey) > 0 : valueCmp(key, bestKey) < 0)) { out.push_back(v); bestKey = key; have = true; }
            }
            return mkSupply(out);
        }
        if (listy && m == "do") { // run a block per value for its side effect; pass values through
            if (!args.empty() && args[0].t == VT::Code) for (auto& v : vals()) { ValueList one{v}; callCallable(args[0], one); }
            return mkSupply(vals());
        }
        if (listy && m == "grab") { // hand the whole stream (as $_) to a collector, emit its result
            if (!args.empty() && args[0].t == VT::Code) {
                Value listArg = Value::array(); *listArg.arr = vals(); listArg.isList = true;
                ValueList one{listArg}; Value r = callCallable(args[0], one);
                return mkSupply(r.t == VT::Array ? *r.arr : r.flatten());
            }
            return mkSupply(vals());
        }
        if (listy && (m == "produce" || m == "reduce")) { // scan (produce) / fold (reduce) over the stream
            Value op = (!args.empty() && args[0].t == VT::Code) ? args[0] : Value::nil();
            ValueList out; Value acc; bool first = true;
            for (auto& v : vals()) {
                if (first) { acc = v; first = false; }
                else if (op.t == VT::Code) { ValueList two{acc, v}; acc = callCallable(op, two); }
                if (m == "produce") out.push_back(acc);
            }
            if (m == "reduce") return mkSupply(first ? ValueList{} : ValueList{acc});
            return mkSupply(out);
        }
        if (listy && m == "minmax") { // emit the running (min..max) Range after each value
            Value mapper = (!args.empty() && args[0].t == VT::Code) ? args[0] : Value::nil();
            ValueList out; Value mn, mx, mnK, mxK; bool first = true;
            for (auto& v : vals()) {
                Value key = v;
                if (mapper.t == VT::Code) { ValueList one{v}; key = callCallable(mapper, one); }
                bool changed = first;
                if (first) { mn = mx = v; mnK = mxK = key; first = false; }
                else { if (valueCmp(key, mnK) < 0) { mn = v; mnK = key; changed = true; } if (valueCmp(key, mxK) > 0) { mx = v; mxK = key; changed = true; } }
                if (!changed) continue; // only emit when the running min..max actually widens
                // build the running Range from the actual endpoint values (Str or Int).
                // rakupp has no string-Range value, so a string range is emitted eagerly
                // flattened — matching how a `"a".."e"` literal evaluates on the other side.
                if (mn.t == VT::Str || mx.t == VT::Str) {
                    Value rg = Value::array();
                    std::string cur = mn.toStr(), end = mx.toStr();
                    for (int g = 0; g < 100000; g++) {
                        if (cur.length() > end.length() || (cur.length() == end.length() && cur > end)) break;
                        rg.arr->push_back(Value::str(cur));
                        if (cur == end) break;
                        cur = strSucc(cur);
                    }
                    out.push_back(rg);
                } else out.push_back(Value::range(mn.toInt(), mx.toInt(), false, false));
            }
            return mkSupply(out);
        }
        if (listy && (m == "zip" || m == "merge")) {
            // $s.zip($other, …) — the invocant is the first stream; reuse the class-method logic.
            ValueList a2; a2.push_back(inv); for (auto& a : args) a2.push_back(a);
            return methodCall(Value::typeObj("Supply"), m, a2, rwArgs);
        }
        // Live-Supply combinators: build a lazy transform chain that runs per emitted
        // value when the resulting Supply is tapped (see applyTapChain + emit fan-out).
        if (!listy && inv.hash->count("supplier") &&
            (m == "map" || m == "grep" || m == "head" || m == "skip" ||
             m == "first" || m == "unique" || m == "squish")) {
            Value s = Value::makeHash(); s.hashKind = "Supply";
            (*s.hash)["supplier"] = (*inv.hash)["supplier"];
            Value chain = Value::array();
            if (inv.hash->count("chain")) *chain.arr = *(*inv.hash)["chain"].arr;
            Value step = Value::makeHash();
            (*step.hash)["op"] = Value::str(m);
            for (auto& a : args) if (a.t != VT::Pair) { (*step.hash)["arg"] = a; break; }
            for (auto& a : args) if (a.t == VT::Pair && a.pairVal && (a.s == "as" || a.s == "with")) (*step.hash)[a.s] = *a.pairVal;
            (*step.hash)["state"] = Value::makeHash();
            chain.arr->push_back(step);
            (*s.hash)["chain"] = chain;
            return s;
        }
        if (listy && (m == "map" || m == "grep" || m == "head" || m == "tail" || m == "skip" ||
                      m == "first" ||
                      m == "reverse" || m == "sort" || m == "unique" || m == "squish" || m == "rotor" ||
                      m == "rotate" || m == "sum" ||
                      m == "batch" || m == "lines" || m == "words" || m == "flat" ||
                      m == "classify" || m == "categorize" || m == "start" || m == "schedule-on" ||
                      m == "stable" || m == "delayed" || m == "migrate" || m == "on-demand")) {
            // Delegate list-transform semantics to the Array method dispatcher, then re-wrap.
            Value arr = Value::array(); *arr.arr = vals(); arr.isList = true;
            if (m == "start" || m == "schedule-on" || m == "stable" || m == "delayed" ||
                m == "migrate" || m == "on-demand" || m == "batch") return inv; // scheduling no-ops
            Value r = methodCall(arr, m, args, rwArgs);
            if (r.t == VT::Array) return mkSupply(*r.arr);
            return mkSupply(ValueList{r});
        }
        if (m == "done" || m == "close" || m == "quit" || m == "wait") return Value::boolean(true);
    }
    if (inv.t == VT::Hash && inv.hashKind == "Tap") {
        if (m == "close" || m == "emit" || m == "done" || m == "quit") return Value::boolean(true);
    }
    if (inv.t == VT::Hash && inv.hashKind == "Attribute") {
        auto& h = *inv.hash;
        if (m == "name") return h.count("name") ? h["name"] : Value::str("");
        if (m == "type" || m == "of" || m == "returns") return h.count("type") ? h["type"] : Value::typeObj("Mu");
        if (m == "readonly") return h.count("readonly") ? h["readonly"] : Value::boolean(true);
        if (m == "rw") return Value::boolean(h.count("readonly") && !h["readonly"].truthy());
        if (m == "has_accessor") return h.count("has_accessor") ? h["has_accessor"] : Value::boolean(false);
        if (m == "gist" || m == "Str") return h.count("name") ? h["name"] : Value::str("");
        if (m == "defined" || m == "Bool") return Value::boolean(true);
    }
    if (inv.t == VT::Hash && inv.hashKind == "Failure") {
        Value ex = inv.hash->count("exception") ? (*inv.hash)["exception"] : Value::typeObj("Exception");
        if (m == "exception") return ex;
        if (m == "defined" || m == "Bool" || m == "so") return Value::boolean(false);
        if (m == "not") return Value::boolean(true);
        if (m == "handled") return Value::boolean(false);
        if (m == "self" || m == "Failure") return inv;
        if (m == "throw" || m == "sink") { if (ex.t == VT::Object) throw RakuError{ex, ex.toStr()}; throw RakuError{Value::typeObj("X::AdHoc"), ex.toStr()}; }
        // delegate message/Str/gist and other queries to the carried exception
        if (m == "message" || m == "Str" || m == "gist") return methodCall(ex, m, args, rwArgs);
    }
    if (inv.t == VT::Hash && inv.hashKind == "Pod") {
        auto& h = *inv.hash;
        if (m == "name")     return h.count("name") ? h["name"] : Value::str("");
        if (m == "contents") return h.count("contents") ? h["contents"] : Value::array();
        if (m == "level")    return h.count("level") ? h["level"] : Value::integer(1);
        if (m == "config")   return h.count("config") ? h["config"] : Value::makeHash();
        if (m == "WHAT")     return Value::typeObj(h.count("podclass") ? h["podclass"].s : "Pod::Block");
        if (m == "defined" || m == "Bool") return Value::boolean(true);
        if (m == "Str" || m == "gist" || m == "raku" || m == "perl") {
            // stringify to the concatenated text of the contents (paragraphs/children)
            std::function<std::string(const Value&)> flat = [&](const Value& v) -> std::string {
                if (v.t == VT::Str) return v.s;
                if (v.t == VT::Hash && v.hashKind == "Pod" && v.hash->count("contents")) {
                    std::string o; for (auto& c : *(*v.hash)["contents"].arr) o += flat(c); return o;
                }
                if (v.t == VT::Array && v.arr) { std::string o; for (auto& c : *v.arr) o += flat(c); return o; }
                return v.toStr();
            };
            return Value::str(flat(inv));
        }
    }
    if (inv.t == VT::Hash && (inv.hashKind == "Distro" || inv.hashKind == "Kernel" || inv.hashKind == "VM")) {
        std::string name = inv.hash->count("name") ? (*inv.hash)["name"].toStr() : "";
        if (m == "name" || m == "Str" || m == "gist" || m == "auth" || m == "desc") return Value::str(name);
        if (m == "is-win") return Value::boolean(false);
        if (m == "version") return Value::str("0");
        if (m == "signature") return Value::str("");
        if (m == "path-sep") return Value::str(":");
        if (m == "release") { // kernel release string (uname -r)
#if !defined(_WIN32)
            struct utsname u;
            if (uname(&u) == 0) return Value::str(u.release);
#endif
            return Value::str("0");
        }
        if (m == "cpu-cores") return Value::integer(1);
        if (m == "archname" || m == "cpu-arch") return Value::str("x86_64");
        return Value::str(name); // lenient: any other Distro/Kernel/VM accessor
    }
    if (inv.t == VT::Hash && inv.hashKind == "Proc") { // standard Proc from run()
        if (m == "exitcode" || m == "signal") return m == "exitcode" ? (*inv.hash)["exitcode"] : Value::integer(0);
        if (m == "so" || m == "Bool") return Value::boolean((*inv.hash)["exitcode"].toInt() == 0);
        if (m == "command") { auto it = inv.hash->find("argv"); return it != inv.hash->end() ? it->second : Value::array(); }
        if (m == "in") { Value h = inv; h.hashKind = "ProcIn"; return h; } // writable stdin handle (shares hash)
        if (m == "out" || m == "err") { Value h = Value::makeHash(); h.hashKind = "FileHandle"; (*h.hash)["buffer"] = (*inv.hash)[m == "out" ? "out-str" : "err-str"]; (*h.hash)["mode"] = Value::str("r"); (*h.hash)["captured"] = Value::boolean(true); return h; }
        if (m == "sink" || m == "self") return inv;
        if (m == "pid") return Value::integer(0);
    }
    if (inv.t == VT::Hash && inv.hashKind == "ProcIn") { // $proc.in — feed stdin, which runs a deferred proc
        if (m == "print" || m == "spurt" || m == "write" || m == "say") {
            std::string input = args.empty() ? "" : args[0].toStr();
            if (m == "say") input += "\n";
            std::vector<std::string> argv;
            auto it = inv.hash->find("argv");
            if (it != inv.hash->end() && it->second.arr) for (auto& x : *it->second.arr) argv.push_back(x.toStr());
            std::string out; int code;
            spawnWithInput(argv, input, out, code, this);
            (*inv.hash)["out-str"] = Value::str(out);      // shared hash: $proc.out.slurp sees this
            (*inv.hash)["exitcode"] = Value::integer(code);
            return Value::boolean(true);
        }
        if (m == "close") return Value::boolean(true);
    }
    if (inv.t == VT::Hash && (inv.hashKind == "Promise" || inv.hashKind == "Vow")) {
        auto ps = inv.ext ? std::static_pointer_cast<PromiseState>(inv.ext) : nullptr;
        std::string kind = inv.hash->count("kind") ? (*inv.hash)["kind"].toStr() : "";

        // keep / break — settle a manual promise (or the vow that controls it).
        if (m == "keep") {
            Value v = args.empty() ? Value::boolean(true) : args[0];
            std::vector<std::function<void()>> fire;
            if (ps) { std::lock_guard<std::mutex> lk(ps->m); if (!ps->done) { ps->result = v; ps->done = true; } fire.swap(ps->thens); ps->cv.notify_all(); }
            (*inv.hash)["status"] = Value::str("Kept"); (*inv.hash)["result"] = v;
            for (auto& f : fire) f(); // run `.then` continuations now that it's settled
            return inv;
        }
        if (m == "break") {
            Value c = args.empty() ? Value::str("Died") : args[0];
            // A non-exception cause (e.g. break("msg")) is wrapped in X::AdHoc so
            // that `$p.cause.message` works, mirroring `die "msg"`.
            if (c.t != VT::Object) {
                auto xit = classes_.find("X::AdHoc");
                if (xit != classes_.end()) {
                    Value ex; ex.t = VT::Object; ex.obj = std::make_shared<ObjectData>();
                    ex.obj->cls = xit->second; ex.obj->attrs["message"] = Value::str(c.toStr());
                    c = ex;
                }
            }
            std::vector<std::function<void()>> fire;
            if (ps) { std::lock_guard<std::mutex> lk(ps->m); if (!ps->done) { ps->broken = true; ps->cause = c; ps->causeMsg = c.toStr(); ps->done = true; } fire.swap(ps->thens); ps->cv.notify_all(); }
            (*inv.hash)["status"] = Value::str("Broken"); (*inv.hash)["cause"] = c;
            for (auto& f : fire) f();
            return inv;
        }
        if (m == "vow") { Value v = inv; v.hashKind = "Vow"; return v; }

        // Fold the state of an anyof/allof combinator lazily from its children.
        auto childState = [&](Value& c, bool& done, bool& broken) {
            done = broken = false;
            if (c.ext) { auto s = std::static_pointer_cast<PromiseState>(c.ext); done = s->done; broken = s->broken; }
            else if (c.hash && c.hash->count("status")) { auto s = (*c.hash)["status"].toStr(); broken = (s == "Broken"); done = (s == "Kept" || s == "Broken"); }
        };
        auto comboStatus = [&]() -> std::string {
            if (!inv.hash->count("promises")) return "Kept";
            auto& kids = *(*inv.hash)["promises"].arr;
            if (kids.empty()) return "Kept";
            if (kind == "anyof") { for (auto& c : kids) { bool d, b; childState(c, d, b); if (d) return "Kept"; } return "Planned"; }
            bool all = true; // allof: Kept once every child has settled (a broken child doesn't fail it)
            for (auto& c : kids) { bool d, b; childState(c, d, b); if (!d) { all = false; break; } }
            return all ? "Kept" : "Planned";
        };

        std::string st;
        if (kind == "anyof" || kind == "allof") st = comboStatus();
        else if (ps) st = ps->done ? (ps->broken ? "Broken" : "Kept") : "Planned";
        else st = inv.hash->count("status") ? (*inv.hash)["status"].toStr() : "Kept";

        // Return the PromiseStatus enum value (matches the Planned/Broken/Kept
        // barewords), so both `is $p.status, Kept` and `~$p.status eq 'Kept'` hold.
        if (m == "status") return Value::enumVal(st, st == "Planned" ? 0 : st == "Broken" ? 1 : 2);
        if (m == "Bool" || m == "so") return Value::boolean(st != "Planned");
        if (m == "cause") { if (ps && ps->broken) return ps->cause; auto it = inv.hash->find("cause"); return it != inv.hash->end() ? it->second : Value::nil(); }
        if (m == "result") {
            if (kind == "anyof" || kind == "allof") return Value::boolean(true);
            if (ps) { awaitPromise(ps); if (ps->broken) throw RakuError{ ps->cause, ps->causeMsg.empty() ? std::string("Promise broken") : ps->causeMsg }; return ps->result; }
            auto it = inv.hash->find("result"); if (it != inv.hash->end()) return it->second;
            auto pr = inv.hash->find("proc"); if (pr != inv.hash->end()) return pr->second; return Value::nil();
        }
        if (m == "then") {
            // Deferred: the block runs only once the promise settles, receiving the
            // (identical) promise; its return keeps the new Promise, a throw breaks it.
            Value cb = args.empty() ? Value::nil() : args[0];
            Value parent = inv; // shares hash/ext with the promise → `$res === $orig` holds
            auto childPs = std::make_shared<PromiseState>();
            Value np = Value::makeHash(); np.hashKind = "Promise"; np.ext = childPs;
            (*np.hash)["status"] = Value::str("Planned");
            Interpreter* self = this;
            std::function<void()> run = [self, cb, parent, childPs]() mutable {
                Value res; bool broke = false; Value cause; std::string cmsg;
                try { if (cb.t == VT::Code) { ValueList one{ parent }; res = self->callCallable(cb, one); } }
                catch (const RakuError& e) { broke = true; cause = e.payload; cmsg = e.message; }
                catch (...) { broke = true; }
                std::vector<std::function<void()>> chain;
                { std::lock_guard<std::mutex> lk(childPs->m);
                  if (broke) { childPs->broken = true; childPs->cause = cause; childPs->causeMsg = cmsg; }
                  else childPs->result = res;
                  childPs->done = true; chain.swap(childPs->thens); childPs->cv.notify_all(); }
                for (auto& f : chain) f();
            };
            bool now = false;
            if (ps) { std::lock_guard<std::mutex> lk(ps->m); if (ps->done) now = true; else ps->thens.push_back(run); }
            else now = true;
            if (now) run();
            return np;
        }
    }
    if (inv.t == VT::Type && inv.s == "IO::Special") {
        if (m == "Str" || m == "gist" || m == "path") return Value::str("");
    }
    if (inv.t == VT::Type && inv.s == "Stash") {
        if (m == "new") { Value h = Value::makeHash(); h.hashKind = "Stash"; return h; }
    }
    if (inv.t == VT::Type && (inv.s == "Uni" || inv.s == "NFC" || inv.s == "NFD" || inv.s == "NFKC" || inv.s == "NFKD")) {
        if (m == "new") {
            std::vector<uint32_t> in;
            for (auto& a : args) {
                if (a.t == VT::Pair) continue;
                if (a.t == VT::Array || a.t == VT::Range) { // a codepoint LIST flattens: Uni.new(@cps)
                    for (auto& x : a.flatten()) in.push_back((uint32_t)x.toInt());
                } else in.push_back((uint32_t)a.toInt());
            }
            if (inv.s != "Uni") in = uniNormalize(in, inv.s == "NFD" ? 0 : inv.s == "NFC" ? 1 : inv.s == "NFKD" ? 2 : 3);
            Value out = Value::array(); out.s = inv.s == "Uni" ? "Uni" : inv.s; for (uint32_t c : in) out.arr->push_back(Value::integer((long long)c));
            return out;
        }
    }
    // a Uni / NFC / NFD / NFKC / NFKD value is an array of codepoints tagged in `s`
    if (inv.t == VT::Array && (inv.s == "Uni" || inv.s == "NFC" || inv.s == "NFD" || inv.s == "NFKC" || inv.s == "NFKD")) {
        if (m == "NFC" || m == "NFD" || m == "NFKC" || m == "NFKD") {
            std::vector<uint32_t> in; if (inv.arr) for (auto& x : *inv.arr) in.push_back((uint32_t)x.toInt());
            auto norm = uniNormalize(in, m == "NFD" ? 0 : m == "NFC" ? 1 : m == "NFKD" ? 2 : 3);
            Value out = Value::array(); out.s = m; for (uint32_t c : norm) out.arr->push_back(Value::integer((long long)c));
            return out;
        }
        if (m == "list" || m == "List" || m == "values" || m == "Seq" || m == "cache") { Value out = Value::array(); out.isList = true; if (inv.arr) out.arr = inv.arr; return out; }
        if (m == "codes" || m == "elems") return Value::integer(inv.arr ? (long long)inv.arr->size() : 0);
        if (m == "Str" || m == "gist" || m == "Stringy") {
            // Raku Strs are NFG (NFC-normalized under the hood): canonically
            // equivalent codepoint orders must yield the SAME Str, so normalize
            // on the way from Uni to Str (mass-equality.t).
            std::vector<uint32_t> in; if (inv.arr) for (auto& x : *inv.arr) in.push_back((uint32_t)x.toInt());
            auto norm = uniNormalize(in, 1 /*NFC*/);
            std::string s; for (uint32_t c : norm) s += cpToUtf8(c);
            return Value::str(s);
        }
    }
    if (inv.t == VT::Type && inv.s == "Complex") {
        if (m == "new") return Value::complex(args.size() > 0 ? args[0].toNum() : 0.0,
                                              args.size() > 1 ? args[1].toNum() : 0.0);
    }
    if (inv.t == VT::Type && (inv.s == "IO::String" || inv.s == "Text::IO::String")) {
        if (m == "new") {
            std::string data = args.empty() ? "" : args[0].toStr();
            Value h = Value::makeHash(); h.hashKind = "FileHandle";
            (*h.hash)["path"] = Value::str(""); (*h.hash)["mode"] = Value::str("r");
            Value lines = Value::array();
            std::istringstream is(data); std::string line;
            while (std::getline(is, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                lines.arr->push_back(Value::str(line));
            }
            (*h.hash)["lines"] = lines; (*h.hash)["pos"] = Value::integer(0);
            return h;
        }
    }
    if (inv.t == VT::Hash && inv.hashKind == "Lock") {
        auto st = inv.ext ? std::static_pointer_cast<LockState>(inv.ext) : nullptr;
        if (m == "protect" || m == "protect-or-queue-on-recursion") {
            if (args.empty() || args[0].t != VT::Code) return args.empty() ? Value::any() : args[0];
            if (st) { // real mutual exclusion, released even if the block throws
                std::lock_guard<std::recursive_mutex> lk(st->m);
                return callCallable(args[0], {});
            }
            return callCallable(args[0], {});
        }
        if (m == "lock" || m == "acquire") { if (st) st->m.lock(); return Value::boolean(true); }
        if (m == "unlock" || m == "release") { if (st) st->m.unlock(); return Value::boolean(true); }
        if (m == "condition") { Value v = Value::makeHash(); v.hashKind = "Lock"; return v; }
    }
    if (inv.t == VT::Hash && inv.hashKind == "Semaphore") {
        auto st = inv.ext ? std::static_pointer_cast<SemaphoreState>(inv.ext) : nullptr;
        if (m == "acquire") {
            if (st) { std::unique_lock<std::mutex> lk(st->m); st->cv.wait(lk, [&]{ return st->count > 0; }); st->count--; }
            return Value::boolean(true);
        }
        if (m == "release") {
            if (st) { std::lock_guard<std::mutex> lk(st->m); st->count++; st->cv.notify_one(); }
            return Value::boolean(true);
        }
        if (m == "try_acquire" || m == "try-acquire") {
            if (!st) return Value::boolean(true);
            std::lock_guard<std::mutex> lk(st->m);
            if (st->count > 0) { st->count--; return Value::boolean(true); }
            return Value::boolean(false);
        }
    }

    // user-defined class: type-object methods (.new and custom constructors)
    // DateTime / Date constructors
    if (inv.t == VT::Type && (inv.s == "DateTime" || inv.s == "Date")) {
        auto mk = [&](long long y, long long mo, long long d, long long h, long long mi, long long s, long long posix) {
            Value v = Value::makeHash(); v.hashKind = inv.s;
            (*v.hash)["year"] = Value::integer(y); (*v.hash)["month"] = Value::integer(mo); (*v.hash)["day"] = Value::integer(d);
            (*v.hash)["hour"] = Value::integer(h); (*v.hash)["minute"] = Value::integer(mi); (*v.hash)["second"] = Value::integer(s);
            (*v.hash)["posix"] = Value::integer(posix);
            return v;
        };
        if (m == "now" || m == "today") {
            time_t t = time(nullptr); struct tm* lt = localtime(&t);
            return mk(lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday, lt->tm_hour, lt->tm_min, lt->tm_sec, (long long)t);
        }
        if (m == "new") {
            long long y = 0, mo = 1, d = 1, h = 0, mi = 0, s = 0;
            std::vector<long long> pos;
            for (auto& a : args) {
                if (a.t == VT::Pair) {
                    long long val = a.pairVal ? a.pairVal->toInt() : 0;
                    if (a.s == "year") y = val; else if (a.s == "month") mo = val; else if (a.s == "day") d = val;
                    else if (a.s == "hour") h = val; else if (a.s == "minute") mi = val; else if (a.s == "second") s = val;
                } else pos.push_back(a.toInt());
            }
            if (pos.size() >= 1) y = pos[0];
            if (pos.size() >= 2) mo = pos[1];
            if (pos.size() >= 3) d = pos[2];
            return mk(y, mo, d, h, mi, s, 0);
        }
    }
    if (inv.t == VT::Hash && (inv.hashKind == "DateTime" || inv.hashKind == "Date")) {
        auto fld = [&](const char* k) { auto it = inv.hash->find(k); return it != inv.hash->end() ? it->second.toInt() : 0; };
        if (m == "year" || m == "month" || m == "day" || m == "hour" || m == "minute" || m == "second" || m == "posix")
            return Value::integer(fld(m.c_str()));
        if (m == "Str" || m == "gist" || m == "yyyy-mm-dd" || m == "Date") {
            char buf[32];
            if (inv.hashKind == "Date") snprintf(buf, sizeof buf, "%04lld-%02lld-%02lld", fld("year"), fld("month"), fld("day"));
            else snprintf(buf, sizeof buf, "%04lld-%02lld-%02lldT%02lld:%02lld:%02lld", fld("year"), fld("month"), fld("day"), fld("hour"), fld("minute"), fld("second"));
            return Value::str(buf);
        }
        if (m == "day-of-week" || m == "dow") { // 1=Monday .. 7=Sunday (Sakamoto's algorithm)
            long long y = fld("year"), mo = fld("month"), d = fld("day");
            static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
            long long yy = (mo < 3) ? y - 1 : y;
            int sak = (int)(((yy + yy / 4 - yy / 100 + yy / 400 + t[(mo - 1) % 12] + d) % 7 + 7) % 7); // 0=Sun
            return Value::integer((sak + 6) % 7 + 1);
        }
        if ((m == "later" || m == "earlier") && inv.hashKind == "Date") {
            long long days = 0;
            for (auto& a : args) if (a.t == VT::Pair && a.s == "days" && a.pairVal) days = a.pairVal->toInt();
            return makeDate(civilToDays(fld("year"), fld("month"), fld("day")) + (m == "later" ? days : -days));
        }
        if (m == "truncated-to" || m == "earlier" || m == "later") return inv; // best-effort (weeks/months etc.)
    }

    // `.new` on a scalar built-in type object → that type's default value. This is
    // what real Raku does (Str.new → "", Int.new → 0) and lets `augment class Str {…}`
    // methods be reached via `Str.new.themethod`.
    if (inv.t == VT::Type && inv.s == "Failure" && m == "new") {
        // Failure.new (no args) picks up the current $! as its exception.
        Value ex; bool haveEx = false;
        for (auto& a : args) if (a.t == VT::Object) { ex = a; haveEx = true; } // Failure.new($ex) / :exception
        if (!haveEx) { Value* be = tctx_.cur->find("$!"); if (be && be->t != VT::Nil && be->t != VT::Type) ex = *be; }
        Value f = Value::makeHash(); f.hashKind = "Failure";
        (*f.hash)["exception"] = ex;
        return f;
    }
    if (inv.t == VT::Type && inv.s == "Proxy" && m == "new") {
        // Proxy.new(:FETCH(method(){…}), :STORE(method($v){…})) — a container whose
        // reads call FETCH and whose writes call STORE (see VarExpr eval / evalAssign).
        Value p = Value::makeHash(); p.hashKind = "Proxy";
        for (auto& a : args) if (a.t == VT::Pair && a.pairVal)
            { if (a.s == "FETCH" || a.s == "STORE") (*p.hash)[a.s] = *a.pairVal; }
        return p;
    }
    if (inv.t == VT::Type && m == "new") {
        const std::string& t = inv.s;
        if (t == "Str" || t == "Cool") return Value::str("");
        if (t == "Int") return Value::integer(0);
        if (t == "Num" || t == "Real" || t == "Numeric") return Value::number(0.0);
        if (t == "Bool") return Value::boolean(false);
    }
    if (inv.t == VT::Type && (inv.s == "List" || inv.s == "Array" || inv.s == "Seq") && m == "new") {
        Value v = Value::array(); v.isList = (inv.s != "Array"); v.ofType = inv.ofType;
        for (auto& a : args) for (auto& x : toList(a)) v.arr->push_back(x); return v;
    }
    if (inv.t == VT::Type && (inv.s == "Hash" || inv.s == "Map") && m == "new") {
        Value v = Value::makeHash(); v.ofType = inv.ofType;
        for (size_t k = 0; k < args.size(); k++) {
            if (args[k].t == VT::Pair) (*v.hash)[args[k].s] = args[k].pairVal ? *args[k].pairVal : Value::any();
            else if (k + 1 < args.size()) { (*v.hash)[args[k].toStr()] = args[k + 1]; k++; }
        }
        return v;
    }
    if (inv.t == VT::Type && inv.s == "IterationBuffer" && m == "new") {
        Value v = Value::makeHash(); v.hashKind = "IterationBuffer";
        Value items = Value::array();
        for (auto& a : args) for (auto& x : toList(a)) items.arr->push_back(x);
        (*v.hash)["items"] = items;
        return v;
    }
    if (inv.t == VT::Type) {
        if (inv.s.rfind("IO::Spec", 0) == 0) { Value r; if (ioSpecMethod(*this, inv.s, m, args, r)) return r; }
        // .^mro / .mro on a built-in type → the class-only linearisation (roles like
        // Real/Numeric are excluded, matching Rakudo's Int.^mro == (Int Cool Any Mu)).
        if (m == "mro" && !classes_.count(inv.s)) {
            static const std::set<std::string> roles = {"Real", "Numeric", "Stringy", "Dateish", "Rational", "Callable", "Positional", "Associative"};
            Value out = Value::array(); out.isList = true;
            for (auto& a : typeAncestry(inv.s)) if (!roles.count(a)) out.arr->push_back(Value::typeObj(a));
            if (out.arr->empty()) { out.arr->push_back(Value::typeObj(inv.s)); out.arr->push_back(Value::typeObj("Any")); out.arr->push_back(Value::typeObj("Mu")); }
            return out;
        }
        auto cit = classes_.find(inv.s);
        if (cit != classes_.end()) {
            auto ci = cit->second;
            // grammar entry points
            if ((m == "parse" || m == "subparse" || m == "parsefile") && (ci->isGrammar || ci->findRule("TOP"))) {
                bool sub = (m == "subparse");
                // the built-in parse behavior (also the `next` candidate for a user override)
                auto builtinParse = [this, ci, sub](ValueList a) -> Value {
                    std::string startRule = "TOP"; Value actions;
                    for (auto& arg : a) {
                        if (arg.t == VT::Pair && arg.s == "rule") startRule = arg.pairVal ? arg.pairVal->toStr() : "TOP";
                        if (arg.t == VT::Pair && arg.s == "actions" && arg.pairVal) actions = *arg.pairVal;
                    }
                    std::string input = a.empty() ? "" : a[0].toStr();
                    return grammarParse(ci.get(), input, sub, startRule, actions);
                };
                if (m == "parsefile") { // slurp the file, then parse its contents
                    std::string input = args.empty() ? "" : args[0].toStr();
                    std::ifstream in(input); std::ostringstream ss; ss << in.rdbuf(); input = ss.str();
                    if (!input.empty() && input.back() == '\n') input.pop_back();
                    ValueList a2 = args; if (!a2.empty()) a2[0] = Value::str(input); else a2.push_back(Value::str(input));
                    return builtinParse(a2);
                }
                // A user-defined `method parse`/`subparse` (e.g. YAMLish wires :actions via nextwith)
                // runs first; the built-in is its redispatch target.
                if (Value* um = ci->findMethod(m)) {
                    RedispatchCtx prc; prc.next = builtinParse; prc.sameArgs = args;
                    redispatchStack_.push_back(std::move(prc));
                    Value r;
                    try { r = invokeMethod(*um, inv, args, rwArgs); }
                    catch (...) { redispatchStack_.pop_back(); throw; }
                    redispatchStack_.pop_back();
                    return r;
                }
                return builtinParse(args);
            }
            // metamodel (.^find_method / .^add_method / .^methods / .^lookup / .^can)
            if (m == "find_method" || m == "lookup") {
                std::string mn = args.empty() ? "" : args[0].toStr();
                Value* um = ci->findMethod(mn);
                return um ? *um : Value::nil();
            }
            if (m == "add_method") { // .^add_method($name, $code)
                if (args.size() >= 2) { noteSymbolMutation("runtime .^add_method"); ci->methods[args[0].toStr()] = args[1]; }
                return args.size() >= 2 ? args[1] : Value::nil();
            }
            if (m == "can") {
                std::string mn = args.empty() ? "" : args[0].toStr();
                Value* um = ci->findMethod(mn);
                Value out = Value::array(); out.isList = true;
                if (um) out.arr->push_back(*um);
                return out;
            }
            if (m == "methods") {
                // `:local` → only this class's own methods; otherwise walk the user
                // inheritance chain (parents + roles), stopping before Any/Mu.
                bool local = false;
                for (auto& a : args) if (a.t == VT::Pair && a.s == "local")
                    local = a.pairVal ? a.pairVal->truthy() : true;
                Value out = Value::array(); out.isList = true;
                std::set<ClassInfo*> visited; // dedup by class (MRO), not by method name
                std::function<void(ClassInfo*)> walk = [&](ClassInfo* c) {
                    if (!c || !visited.insert(c).second) return;
                    for (auto& kv : c->methods) out.arr->push_back(kv.second);
                    if (local) return;
                    walk(c->parent.get());
                    for (auto& p : c->extraParents) walk(p.get());
                };
                walk(ci.get());
                return out;
            }
            if (m == "roles" || m == "role_typecheck_list") { // composed roles
                Value out = Value::array(); out.isList = true;
                for (auto& rn : ci->doneRoles) out.arr->push_back(Value::typeObj(rn));
                return out;
            }
            if (m == "parents") { // immediate parents; composed roles are not parents
                Value out = Value::array(); out.isList = true;
                if (ci->parent && !ci->parent->isRole) out.arr->push_back(Value::typeObj(ci->parent->name));
                for (auto& p : ci->extraParents) if (p && !p->isRole) out.arr->push_back(Value::typeObj(p->name));
                if (out.arr->empty() && !ci->isRole) out.arr->push_back(Value::typeObj("Any"));
                return out;
            }
            if (m == "mro") { // method resolution order: self, ancestors, then Any, Mu
                Value out = Value::array(); out.isList = true;
                // Depth-first over the primary + additional (multiple-inheritance) parents,
                // then dedup keeping the LAST occurrence — the C3 order for simple diamonds
                // (D is B is C, B/C is A → D, B, C, A).
                std::vector<std::string> lin;
                std::function<void(ClassInfo*)> visit = [&](ClassInfo* c) {
                    if (!c) return;
                    lin.push_back(c->name);
                    if (c->parent) visit(c->parent.get());
                    for (auto& p : c->extraParents) visit(p.get());
                };
                visit(ci.get());
                for (size_t i = 0; i < lin.size(); i++) {
                    bool later = false;
                    for (size_t j = i + 1; j < lin.size(); j++) if (lin[j] == lin[i]) { later = true; break; }
                    if (!later) out.arr->push_back(Value::typeObj(lin[i]));
                }
                out.arr->push_back(Value::typeObj("Any"));
                out.arr->push_back(Value::typeObj("Mu"));
                return out;
            }
            if (m == "attributes") { // Attribute objects: .name ($!x), .type, .readonly
                bool local = false;
                for (auto& a : args) if (a.t == VT::Pair && a.s == "local") local = a.pairVal ? a.pairVal->truthy() : true;
                Value out = Value::array(); out.isList = true;
                std::set<ClassInfo*> visited;
                std::function<void(ClassInfo*)> walk = [&](ClassInfo* c) {
                    if (!c || !visited.insert(c).second) return;
                    for (auto& a : c->attrs) {
                        Value at = Value::makeHash(); at.hashKind = "Attribute";
                        (*at.hash)["name"] = Value::str(std::string(1, a.sigil) + "!" + a.name);
                        (*at.hash)["type"] = Value::typeObj(a.type.empty() ? "Mu" : a.type);
                        (*at.hash)["readonly"] = Value::boolean(!a.rw);
                        (*at.hash)["has_accessor"] = Value::boolean(a.pub);
                        out.arr->push_back(at);
                    }
                    if (local) return;
                    walk(c->parent.get());
                    for (auto& p : c->extraParents) walk(p.get());
                };
                walk(ci.get());
                return out;
            }
            // `new`: a user-defined `new` (often a multi) coexists with the default
            // Mu.new. Use a custom candidate only if one matches the args; otherwise
            // fall back to default construction (named args / no args).
            if (m == "new") {
                Value* um = ci->findMethod("new");
                bool useCustom = um != nullptr;
                if (um && um->code && um->code->isMultiDispatcher) {
                    useCustom = false;
                    for (auto& cand : um->code->candidates)
                        if (scoreCandidate(cand, args) >= 0) { useCustom = true; break; }
                }
                if (useCustom) return invokeMethod(*um, inv, args, rwArgs);
            } else if (ci->findMethod(m)) {
                return invokeMethodChain(m, ci.get(), inv, args, rwArgs);
            }
            // accessing an attribute (public accessor) on a type object is illegal
            if (const ClassAttr* at = ci->findAttr(m)) {
                if (at->pub) throw RakuError{Value::typeObj("X::Method::NotFound"),
                    "Cannot look up attributes in a " + inv.s + " type object"};
            }
            if (m == "new" || m == "bless") {
                // A class subclassing a native container (`class A is Array`): the
                // instance is an object backed by a native Array/Hash (via ObjectData.boxed),
                // so it indexes/pushes natively while .WHAT answers the user type.
                std::string nb;
                for (ClassInfo* c = ci.get(); c && nb.empty(); c = c->parent.get()) nb = c->nativeParent;
                if (nb == "Array" || nb == "List" || nb == "Hash" || nb == "Map") {
                    auto od = std::make_shared<ObjectData>();
                    od->cls = ci; od->hasBoxed = true;
                    if (nb == "Hash" || nb == "Map") od->boxed = Value::makeHash();
                    else { od->boxed = Value::array(); od->boxed.isList = (nb == "List"); }
                    od->boxed.ofType = inv.ofType; // A[Int] -> element type on the box
                    for (auto& arg : args) if (arg.t == VT::Pair) od->attrs[arg.s] = arg.pairVal ? *arg.pairVal : Value::any();
                    Value self = Value::object(od);
                    if (Value* build = ci->findMethod("BUILD")) invokeMethod(*build, self, args);
                    return self;
                }
                auto od = std::make_shared<ObjectData>();
                od->cls = ci;
                std::vector<ClassInfo*> chain;
                for (ClassInfo* c = ci.get(); c; c = c->parent.get()) chain.push_back(c);
                for (auto it = chain.rbegin(); it != chain.rend(); ++it)
                    for (auto& at : (*it)->attrs) {
                        Value dv = at.hasDefVal ? at.defVal
                                 : at.def ? eval(const_cast<Expr*>(at.def))
                                          : (at.sigil == '@' ? Value::array()
                                             : at.sigil == '%' ? Value::makeHash() : Value::any());
                        od->attrs[at.name] = dv;
                    }
                for (auto& arg : args)
                    if (arg.t == VT::Pair) od->attrs[arg.s] = arg.pairVal ? *arg.pairVal : Value::any();
                Value self = Value::object(od);
                // bless does not re-run BUILD-from-new args the same way, but running
                // BUILD here matches the common `self.bless(:attr(...))` usage.
                if (Value* build = ci->findMethod("BUILD")) invokeMethod(*build, self, args);
                return self;
            }
            if (m == "gist" || m == "Str" || m == "raku") return Value::str("(" + inv.s + ")");
        }
    }
    // exception object .throw / .fail: raise it (message from its .message method)
    if ((m == "throw" || m == "rethrow" || m == "fail") && inv.t == VT::Object && inv.obj) {
        std::string msg;
        if (Value* mm = inv.obj->cls ? inv.obj->cls->findMethod("message") : nullptr)
            { try { ValueList none; msg = invokeMethod(*mm, inv, none).toStr(); } catch (...) {} }
        throw RakuError{inv, msg.empty() ? inv.typeName() : msg};
    }
    // user object: dispatch to class methods / public accessors first
    if (inv.t == VT::Object && inv.obj && inv.obj->cls) {
        auto ci = inv.obj->cls;
        if (ci->findMethod(m)) return invokeMethodChain(m, ci.get(), inv, args, rwArgs);
        if (m == "clone") { // shallow copy, with :name(val) attribute overrides
            Value nv = inv; auto ni = std::make_shared<ObjectData>();
            ni->cls = inv.obj->cls; ni->attrs = inv.obj->attrs;
            for (auto& a : args) if (a.t == VT::Pair) ni->attrs[a.s] = a.pairVal ? *a.pairVal : Value::any();
            nv.obj = ni; return nv;
        }
        // a grammar INSTANCE (`Grammar.new`) parses just like the type object
        if ((m == "parse" || m == "subparse" || m == "parsefile") && (ci->isGrammar || ci->findRule("TOP")))
            return methodCall(Value::typeObj(ci->name), m, args, rwArgs);
        const ClassAttr* at = ci->findAttr(m);
        if (at && at->pub) {
            auto it = inv.obj->attrs.find(m);
            return it != inv.obj->attrs.end() ? it->second : Value::any();
        }
        // else fall through to universal methods (.defined/.WHAT/.gist/...)
    }

    // `*.method` -> a WhateverCode that applies the method to its argument
    if (inv.t == VT::Whatever) {
        Value code; code.t = VT::Code; code.code = std::make_shared<Callable>();
        std::string mc = m; ValueList ar = args;
        code.code->builtin = [mc, ar](Interpreter& I, ValueList& a) -> Value {
            Value arg = a.empty() ? Value::any() : a[0];
            ValueList aa = ar;
            return I.methodCall(arg, mc, aa);
        };
        return code;
    }

    // Code introspection / currying
    if (inv.t == VT::Code && inv.code) {
        if (m == "assuming") { // partial application: &f.assuming(a,b)(c) == f(a,b,c)
            Value orig = inv; ValueList pre = args;
            Value code; code.t = VT::Code; code.code = std::make_shared<Callable>();
            code.code->builtin = [orig, pre](Interpreter& I, ValueList& a) -> Value {
                ValueList all = pre; for (auto& x : a) all.push_back(x);
                return I.callCallable(orig, all);
            };
            return code;
        }
        if (m == "arity") {
            long long n = 0;
            if (inv.code->params) { for (auto& p : *inv.code->params) if (!p.slurpy && !p.named && !p.optional) n++; }
            else n = (long long)inv.code->placeholders.size();
            return Value::integer(n);
        }
        if (m == "count") { // required + optional positionals; a slurpy makes it Inf
            long long n = 0; bool slurpy = false;
            if (inv.code->params) for (auto& p : *inv.code->params) {
                if (p.named) continue;
                if (p.slurpy) slurpy = true; else n++;
            } else n = (long long)inv.code->placeholders.size();
            return slurpy ? Value::number(1.0/0.0) : Value::integer(n);
        }
        if (m == "name") return Value::str(inv.code->name);
        if (m == "returns" || m == "of")
            return inv.code->retType.empty() ? Value::typeObj("Mu") : Value::typeObj(inv.code->retType);
        if (m == "signature") return makeSignature(inv.code.get());
        if (m == "multi" || m == "is_dispatcher") return Value::boolean(inv.code->isMultiDispatcher);
        if (m == "candidates") {
            Value out = Value::array(); out.isList = true;
            if (inv.code->isMultiDispatcher) for (auto& c : inv.code->candidates) out.arr->push_back(c);
            else out.arr->push_back(inv);
            return out;
        }
        // &routine.wrap(&wrapper): push a wrapper in front of the routine. Because
        // the Callable is shared (shared_ptr), every reference — including calls
        // through the routine's name — sees the wrap. Returns a handle for .unwrap.
        if (m == "wrap" && !args.empty()) {
            inv.code->wrappers.push_back(args[0]);
            noteSymbolMutation("routine .wrap");
            Value h = Value::makeHash(); h.hashKind = "WrapHandle";
            (*h.hash)["routine"] = inv;               // keep the Callable alive
            (*h.hash)["wrapper"] = args[0];           // identity for targeted .unwrap
            return h;
        }
        // &routine.unwrap($handle) / .unwrap — remove a wrapper. With a handle, remove
        // that specific wrapper; otherwise pop the most-recent one (LIFO).
        if (m == "unwrap") {
            auto& ws = inv.code->wrappers;
            if (!args.empty() && args[0].t == VT::Hash && args[0].hashKind == "WrapHandle" &&
                args[0].hash->count("wrapper")) {
                const Value& target = (*args[0].hash)["wrapper"];
                for (size_t k = ws.size(); k-- > 0; )
                    if (ws[k].code == target.code) { ws.erase(ws.begin() + k); break; }
            }
            else if (!ws.empty()) ws.pop_back();
            noteSymbolMutation("routine .unwrap");
            return inv;
        }
    }

    // CompUnit::DependencySpecification accessors.
    if (inv.t == VT::Hash && inv.hashKind == "DependencySpec") {
        if (m == "short-name" || m == "version-matcher" || m == "auth-matcher" || m == "api-matcher")
            return inv.hash->count(m) ? (*inv.hash)[m] : Value::any();
    }
    // IO::Socket::INET connection/listener methods.
    if (inv.t == VT::Hash && inv.hashKind == "Socket") {
        int fd = (int)(*inv.hash)["fd"].toInt();
        if (m == "accept") {
            bool p = gilPark(); int cfd = ::accept(fd, nullptr, nullptr); gilUnpark(p);
            if (cfd < 0) return Value::nil();
            Value s = Value::makeHash(); s.hashKind = "Socket"; (*s.hash)["fd"] = Value::integer(cfd);
            return s;
        }
        if (m == "recv" || m == "read") {
            size_t want = 65536;
            if (!args.empty() && args[0].isNumeric()) want = (size_t)args[0].toInt();
            std::vector<char> buf(want ? want : 1);
            bool p = gilPark(); ssize_t n = ::recv(fd, buf.data(), buf.size(), 0); gilUnpark(p);
            if (n < 0) return Value::nil();
            return Value::str(std::string(buf.data(), (size_t)n)); // n==0 => "" (peer closed)
        }
        if (m == "print" || m == "write" || m == "send" || m == "put") {
            std::string data = args.empty() ? "" : args[0].toStr(); // Blob is a byte-Str
            if (m == "put") data += "\n";
            size_t off = 0;
            bool p = gilPark();
            while (off < data.size()) { ssize_t n = ::send(fd, data.data() + off, data.size() - off, 0); if (n <= 0) break; off += (size_t)n; }
            gilUnpark(p);
            return Value::boolean(true);
        }
        if (m == "close") { if (fd >= 0) ::close(fd); (*inv.hash)["fd"] = Value::integer(-1); return Value::boolean(true); }
    }

    // universal
    bool isFH = (inv.t == VT::Hash && inv.hashKind == "FileHandle");
    if (m == "say" && !isFH) return ioEmit(gistOf(inv) + "\n", "$*OUT", false);
    if (m == "print" && !isFH) return ioEmit(strOf(inv), "$*OUT", false);
    if (m == "put") return ioEmit(strOf(inv) + "\n", "$*OUT", false);
    if (m == "note") return ioEmit(gistOf(inv) + "\n", "$*ERR", true);
    if (m == "Str") return Value::str(inv.toStr());
    if (m == "Int") {
        // Converting a string that carries an Nl/No numeral (Roman, circled,
        // Tamil ௰, …) is not supported — only Nd digits are numeric. (X::Str::Numeric)
        if (inv.t == VT::Str)
            for (size_t bi = 0; bi < inv.s.size(); ) {
                unsigned char b0 = inv.s[bi];
                int len = b0 < 0x80 ? 1 : b0 >= 0xF0 ? 4 : b0 >= 0xE0 ? 3 : 2;
                uint32_t cp = b0 < 0x80 ? b0 : (b0 & (0xFF >> (len + 1)));
                for (int k = 1; k < len && bi + k < inv.s.size(); k++) cp = (cp << 6) | ((unsigned char)inv.s[bi + k] & 0x3F);
                bi += len;
                if (cp >= 0x80) { std::string gc = uniGeneralCategory(cp);
                    if (gc == "Nl" || gc == "No")
                        throw RakuError{Value::typeObj("X::Str::Numeric"), "Cannot convert string to number: a numeral in category '" + gc + "' is not a digit"}; }
            }
        return Value::integer(inv.toInt());
    }
    if (m == "Num" || m == "Numeric" || m == "Real") return Value::number(inv.toNum());
    if (m == "Bool" || m == "so") return Value::boolean(inv.truthy());
    if (m == "not") return Value::boolean(!inv.truthy());
    if (m == "defined") return Value::boolean(defined(inv));
    if (m == "DEFINITE") return Value::boolean(defined(inv)); // defined instance vs type/undef
    if (m == "can") { // Mu.can($name): list of matching methods ([] if none)
        std::string mn = args.empty() ? "" : args[0].toStr();
        Value out = Value::array(); out.isList = true;
        ClassInfo* ci = nullptr;
        if (inv.t == VT::Object && inv.obj) ci = inv.obj->cls.get();
        else if (inv.t == VT::Type) { auto it = classes_.find(inv.s); if (it != classes_.end()) ci = it->second.get(); }
        if (ci) if (Value* um = ci->findMethod(mn)) out.arr->push_back(*um);
        return out;
    }
    if (inv.t == VT::Type && (m == "raku" || m == "perl")) return Value::str(inv.s); // Int.raku -> "Int" (no parens)
    if (m == "gist") return Value::str(inv.gist());
    if (m == "raku" || m == "perl") return Value::str(rakuRepr(inv));
    if (m == "Slip") { // a Slip flattens into any list-building context (from-list, list literals)
        if (inv.t == VT::Array) { Value r = inv; r.isList = true; return r; }
        if (inv.t == VT::Range) { Value r = Value::array(); *r.arr = inv.flatten(); r.isList = true; return r; }
        return inv;
    }
    // .list/.List/.flat/.eager on a *scalar* (Int/Str/Num/Rat/Bool/Complex/Pair/type object)
    // yields a one-element list. Restricted to scalar types so list/array/range/seq values —
    // which carry their own list semantics upstream — are never re-wrapped.
    if ((m == "list" || m == "List" || m == "flat" || m == "eager" || m == "cache") &&
        (inv.t == VT::Int || inv.t == VT::Num || inv.t == VT::Rat || inv.t == VT::Str ||
         inv.t == VT::Bool || inv.t == VT::Complex || inv.t == VT::Pair || inv.t == VT::Type ||
         inv.t == VT::Any || inv.t == VT::Nil)) {
        Value o = Value::array(); o.isList = true; o.arr->push_back(inv); return o;
    }
    if (m == "sink") return Value::nil(); // Mu.sink: evaluate for side effects, yield Nil (user `sink` dispatched earlier)
    if (m == "VAR" || m == "self") return inv; // container introspection: value is its own container
    // .VAR.name on an anonymous container is "element" in Rakudo; some code (Text::CSV)
    // uses `@x.VAR.name ne "element"` to detect an explicitly-passed array.
    if (m == "name" && (inv.t == VT::Array || inv.t == VT::Hash)) return Value::str("element");
    // enum value introspection (VT::Int carrying an enumName, e.g. `medium` of `enum Size <...>`)
    if (inv.t == VT::Int && !inv.enumName.empty()) {
        if (m == "key") return Value::str(inv.enumName);
        if (m == "value") return Value::integer(inv.toInt());
        if (m == "pair") return Value::pair(inv.enumName, Value::integer(inv.toInt()));
    }
    if (inv.t == VT::Match && (m == "made" || m == "ast")) return inv.pairVal ? *inv.pairVal : Value::nil();
    if (inv.t == VT::Match && m == "Str") return Value::str(inv.s);
    if (inv.t == VT::Match && (m == "from")) return Value::integer(inv.rFrom);
    if (inv.t == VT::Match && (m == "to")) return Value::integer(inv.rTo);
    if (inv.t == VT::Match && (m == "orig" || m == "prematch" || m == "postmatch")) {
        std::string orig = inv.ext ? *std::static_pointer_cast<std::string>(inv.ext) : inv.s;
        if (m == "orig") return Value::str(orig);
        if (m == "prematch") return Value::str(orig.substr(0, std::min((size_t)inv.rFrom, orig.size())));
        return Value::str((size_t)inv.rTo <= orig.size() ? orig.substr(inv.rTo) : "");
    }
    if (inv.t == VT::Match && (m == "keys" || m == "values" || m == "list" || m == "caps"
                               || m == "hash" || m == "pairs" || m == "kv" || m == "elems")) {
        if (m == "hash") { Value h = Value::makeHash(); if (inv.hash) *h.hash = *inv.hash; return h; }
        if (m == "elems") return Value::integer(inv.arr ? (long long)inv.arr->size() : 0);
        Value o = Value::array(); o.isList = true;
        if (m == "keys") {
            if (inv.arr) for (size_t i = 0; i < inv.arr->size(); i++) o.arr->push_back(Value::integer((long long)i));
            if (inv.hash) for (auto& kv : *inv.hash) o.arr->push_back(Value::str(kv.first));
        } else if (m == "values" || m == "list" || m == "caps") {
            if (inv.arr) for (auto& e : *inv.arr) o.arr->push_back(e);
            if ((m == "values") && inv.hash) for (auto& kv : *inv.hash) o.arr->push_back(kv.second);
        } else { // pairs / kv
            if (inv.arr) for (size_t i = 0; i < inv.arr->size(); i++) {
                if (m == "kv") { o.arr->push_back(Value::integer((long long)i)); o.arr->push_back((*inv.arr)[i]); }
                else o.arr->push_back(Value::pair(std::to_string(i), (*inv.arr)[i]));
            }
            if (inv.hash) for (auto& kv : *inv.hash) {
                if (m == "kv") { o.arr->push_back(Value::str(kv.first)); o.arr->push_back(kv.second); }
                else o.arr->push_back(Value::pair(kv.first, kv.second));
            }
        }
        return o;
    }
    // `.of` on a typed container: `my Int @a` / `my Int %h` → Int (Mu when untyped).
    // For Hash[V,K]/Array[T] the value/element type is the first parameter component.
    if (m == "of" && (inv.t == VT::Array || inv.t == VT::Hash)) {
        if (inv.ofType.empty()) return Value::typeObj("Mu");
        std::string ot = inv.ofType; auto c = ot.find(','); if (c != std::string::npos) ot = ot.substr(0, c);
        return Value::typeObj(ot);
    }
    if (m == "WHAT") {
        // typed container -> its parameterized type object (Array[Int] / Hash[Int,Str])
        if ((inv.t == VT::Array || inv.t == VT::Hash) && !inv.ofType.empty()) {
            Value ty = Value::typeObj(inv.t == VT::Array ? "Array" : "Hash");
            ty.ofType = inv.ofType;
            return ty;
        }
        if (inv.t == VT::Type) return inv; // a (parameterized) type object is its own .WHAT
        // native-container subclass instance parameterized as A[Int]
        if (inv.t == VT::Object && inv.obj && inv.obj->hasBoxed && inv.obj->cls &&
            !inv.obj->boxed.ofType.empty()) {
            Value ty = Value::typeObj(inv.obj->cls->name); ty.ofType = inv.obj->boxed.ofType; return ty;
        }
        return Value::typeObj(inv.typeName());
    }
    if (m == "iterator") { // S07: make an Iterator over this value's elements
        Value it = Value::makeHash(); it.hashKind = "Iterator";
        Value items = Value::array();
        if (inv.t == VT::Array && inv.arr) *items.arr = *inv.arr;
        else if (inv.t == VT::Range) *items.arr = inv.flatten();
        else if (inv.t == VT::Hash) { // plain hash and Set/Bag/Mix iterate their pairs
            ValueList none;
            Value ps = methodCall(inv, "pairs", none, nullptr);
            if (ps.t == VT::Array && ps.arr) *items.arr = *ps.arr;
        }
        else if (inv.t != VT::Nil && inv.t != VT::Any) items.arr->push_back(inv);
        (*it.hash)["items"] = items;
        (*it.hash)["pos"] = Value::integer(0);
        return it;
    }
    if (m == "clone") { // non-object clone: shallow copy of containers, self for immutables
        if (inv.t == VT::Array) { Value nv = inv; nv.arr = std::make_shared<ValueList>(*inv.arr); return nv; }
        if (inv.t == VT::Hash)  { Value nv = inv; nv.hash = std::make_shared<std::map<std::string, Value>>(*inv.hash); return nv; }
        return inv; // Int/Num/Rat/Str/Bool/… are immutable — clone is the value itself
    }
    if (m == "HOW") return Value::typeObj("Metamodel::ClassHOW"); // metaclass (its own .HOW returns a HOW too)
    if (m == "WHICH") { // object identity: value-based for immutables, pointer-based for objects
        if (inv.t == VT::Object && inv.obj) { char buf[24]; std::snprintf(buf, sizeof buf, "|%p", (void*)inv.obj.get()); return Value::str(inv.typeName() + buf); }
        return Value::str(inv.typeName() + "|" + inv.toStr());
    }
    if (m == "does") { // .does(Role/Type) — role/type membership introspection
        if (args.empty()) return Value::boolean(false);
        // HOW form: `$obj.HOW.does($obj, Role)` — the metaclass takes (object, role)
        if (inv.t == VT::Type && inv.s.rfind("Metamodel::", 0) == 0 && args.size() >= 2)
            return methodCall(args[0], "does", ValueList{args[1]}, rwArgs);
        std::string rn = args[0].t == VT::Type ? args[0].s : args[0].typeName();
        if (rn == "Any" || rn == "Mu") return Value::boolean(true);
        bool res = inv.typeName() == rn;
        ClassInfo* ci = nullptr;
        if (inv.t == VT::Object && inv.obj) ci = inv.obj->cls.get();
        else if (inv.t == VT::Type) { auto it = classes_.find(inv.s); if (it != classes_.end()) ci = it->second.get(); }
        if (!res && ci) {
            for (ClassInfo* c = ci; c; c = c->parent.get()) if (c->name == rn) { res = true; break; }
            if (!res) res = ci->doesRole(rn);
        }
        return Value::boolean(res);
    }
    if (m == "name" || m == "^name") return Value::str(inv.typeName());

    // Set/Bag/Mix coercions and queries
    if (m == "Set" || m == "SetHash" || m == "Bag" || m == "BagHash" || m == "Mix" || m == "MixHash")
        return makeBaggy(toList(inv), m);
    if (inv.t == VT::Hash && !inv.hashKind.empty()) {
        bool isSet = inv.hashKind.find("Set") == 0;
        if (m == "default") return isSet ? Value::boolean(false) : Value::integer(0);
        if (m == "total") { long long t = 0; for (auto& kv : *inv.hash) t += isSet ? 1 : kv.second.toInt(); return Value::integer(t); }
        if (m == "elems") return Value::integer((long long)inv.hash->size());
    }

    // numeric -> Complex coercion
    if (m == "Complex" && (inv.t == VT::Int || inv.t == VT::Num || inv.t == VT::Rat || inv.t == VT::Bool))
        return Value::complex(inv.toNum(), 0);
    // Complex
    if (inv.t == VT::Complex) {
        std::complex<double> z(inv.n, inv.im);
        if (m == "re" || m == "Real") return Value::number(inv.n);
        if (m == "im") return Value::number(inv.im);
        if (m == "reals") return Value::array({Value::number(inv.n), Value::number(inv.im)});
        if (m == "abs" || m == "magnitude") return Value::number(std::abs(z));
        if (m == "conj") return Value::complex(inv.n, -inv.im);
        if (m == "sqrt") { auto r = std::sqrt(z); return Value::complex(r.real(), r.imag()); }
        if (m == "polar") return Value::array({Value::number(std::abs(z)), Value::number(std::arg(z))});
        if (m == "arg") return Value::number(std::arg(z));
        if (m == "Complex") return inv;
        if (m == "isNaN") return Value::boolean(std::isnan(inv.n) || std::isnan(inv.im));
        if (m == "Str" || m == "gist" || m == "Stringy") return Value::str(inv.toStr());
        if (m == "raku" || m == "perl") return Value::str("<" + inv.toStr() + ">");
        if (m == "Num" || m == "Real" || m == "Int") { if (inv.im != 0) throw RakuError{Value::str("Complex"), "Can not convert Complex with nonzero imaginary part"}; return m == "Int" ? Value::integer((long long)inv.n) : Value::number(inv.n); }
        if (m == "narrow") return inv.im == 0 ? Value::number(inv.n) : inv;
    }

    // Cool-style numeric coercion: an object that defines .Numeric/.Bridge (but
    // not the numeric method itself) acts as its numeric value here.
    if (inv.t == VT::Object && inv.obj) {
        static const std::set<std::string> numMeths = {
            "abs","sqrt","sin","cos","tan","asin","acos","atan","atan2","sinh","cosh","tanh",
            "asinh","acosh","atanh","sec","cosec","csc","cotan","cot","asec","acosec","acsc",
            "acotan","acot","floor","ceiling","round","truncate","sign","exp","log","log10","log2"};
        if (numMeths.count(m)) {
            for (const char* acc : {"Bridge", "Numeric"}) {
                try { ValueList none; Value nv = methodCall(inv, acc, none);
                      if (nv.isNumeric()) { inv = nv; break; } } catch (...) {}
            }
        }
    }
    // numeric
    if (m == "abs") {
        if (inv.t == VT::Int && inv.big) return Value::bigint(inv.big->abs());
        if (inv.t == VT::Int) return Value::integer(std::llabs(inv.toInt()));
        if (inv.t == VT::Rat) return Value::rat(inv.ratN->abs(), *inv.ratD);
        return Value::number(std::fabs(inv.toNum()));
    }
    if (m == "sqrt") { double x = inv.toNum(); return (x < 0 && langRev_ >= 2) ? Value::complex(0, std::sqrt(-x)) : Value::number(std::sqrt(x)); }
    if (m == "rand") return Value::number(inv.toNum() * randDouble()); // $n.rand — Num in [0, $n)
    if (m == "base" && !args.empty() && (inv.t == VT::Int || inv.t == VT::Bool)) { // Int -> string in base 2..36
        long long b = args[0].toInt(); if (b < 2) b = 2; if (b > 36) b = 36;
        long long n = inv.toInt();
        if (n == 0) return Value::str("0");
        bool neg = n < 0; unsigned long long u = neg ? -(unsigned long long)n : (unsigned long long)n;
        static const char* D = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        std::string s;
        while (u) { s = std::string(1, D[u % b]) + s; u /= b; }
        return Value::str(neg ? "-" + s : s);
    }
    if (m == "polymod" && (inv.t == VT::Int || inv.t == VT::Bool)) { // successive divmod by each divisor
        Value out = Value::array(); out.isList = true;
        long long n = inv.toInt();
        ValueList divisors; for (auto& a : args) for (auto& d : a.flatten()) divisors.push_back(d);
        for (auto& d : divisors) {
            long long dv = d.toInt(); if (dv == 0) break;
            out.arr->push_back(Value::integer(n % dv));
            n /= dv;
        }
        out.arr->push_back(Value::integer(n)); // trailing remainder
        return out;
    }
    // trigonometry as methods (radians): $x.sin, $x.asin, ... (Str is Cool -> numeric)
    if (inv.isNumeric() || inv.t == VT::Str) {
        double x = inv.toNum();
        if (m == "sin") return Value::number(std::sin(x));
        if (m == "cos") return Value::number(std::cos(x));
        if (m == "tan") return Value::number(std::tan(x));
        if (m == "asin") return Value::number(std::asin(x));
        if (m == "acos") return Value::number(std::acos(x));
        if (m == "atan") return Value::number(std::atan(x));
        if (m == "atan2") return Value::number(std::atan2(x, args.empty() ? 1.0 : a0().toNum()));
        if (m == "sinh") return Value::number(std::sinh(x));
        if (m == "cosh") return Value::number(std::cosh(x));
        if (m == "tanh") return Value::number(std::tanh(x));
        if (m == "asinh") return Value::number(std::asinh(x));
        if (m == "acosh") return Value::number(std::acosh(x));
        if (m == "atanh") return Value::number(std::atanh(x));
        if (m == "sec") return Value::number(1.0 / std::cos(x));
        if (m == "cosec" || m == "csc") return Value::number(1.0 / std::sin(x));
        if (m == "cotan" || m == "cot") return Value::number(1.0 / std::tan(x));
        if (m == "asec") return Value::number(std::acos(1.0 / x));
        if (m == "acosec" || m == "acsc") return Value::number(std::asin(1.0 / x));
        if (m == "acotan" || m == "acot") return Value::number(std::atan(1.0 / x));
    }
    if (m == "floor") return Value::integer((long long)std::floor(inv.toNum()));
    if (m == "ceiling") return Value::integer((long long)std::ceil(inv.toNum()));
    if (m == "round") {
        double scale = args.empty() ? 1.0 : a0().toNum();
        if (scale == 0) scale = 1.0;
        return Value::number(std::round(inv.toNum() / scale) * scale);
    }
    if (m == "truncate") return Value::integer((long long)inv.toNum());
    if (m == "sign") {
        if (inv.t == VT::Complex) throw RakuError{Value::typeObj("X::Numeric::Real"), "Complex is not in the Real domain, so it has no sign"};
        double n = inv.toNum();
        if (std::isnan(n)) return Value::number(NAN); // sign(NaN) is NaN
        return Value::integer(n < 0 ? -1 : n > 0 ? 1 : 0);
    }
    if (m == "exp") return Value::number(std::exp(inv.toNum()));
    if (m == "log") return Value::number(std::log(inv.toNum()));
    if (m == "sin") return Value::number(std::sin(inv.toNum()));
    if (m == "cos") return Value::number(std::cos(inv.toNum()));
    if (m == "numerator") return inv.t == VT::Rat ? Value::bigint(*inv.ratN) : Value::integer(inv.toInt());
    if (m == "denominator") return inv.t == VT::Rat ? Value::bigint(*inv.ratD) : Value::integer(1);
    if (m == "nude") {
        if (inv.t == VT::Rat) return Value::array({Value::bigint(*inv.ratN), Value::bigint(*inv.ratD)});
        return Value::array({Value::integer(inv.toInt()), Value::integer(1)});
    }
    if (m == "Rat" || m == "FatRat") {
        bool fat = (m == "FatRat");
        Value r;
        if (inv.t == VT::Rat) r = inv;
        else if (inv.t == VT::Int || inv.t == VT::Bool) r = Value::rat(inv.toBig(), BigInt(1));
        else return inv; // Num->Rat approximation not implemented
        r.fatRat = fat; // FatRat is the arbitrary-precision Rat, tagged for type identity
        return r;
    }
    if (m == "succ") return inv.t == VT::Str ? Value::str(strSucc(inv.s)) : Value::integer(inv.toInt() + 1);
    if (m == "pred") return inv.t == VT::Str ? inv : Value::integer(inv.toInt() - 1);
    if (m == "is-prime") {
        long long n = inv.toInt(); bool p = n > 1;
        for (long long d = 2; d * d <= n && p; d++) if (n % d == 0) p = false;
        return Value::boolean(p);
    }

    // ---- IO::Path (string-as-path) ----
    if (m == "IO") { Value p = Value::str(inv.toStr()); p.hashKind = "IO"; return p; }
    if (m == "slurp" && !(inv.t == VT::Hash && inv.hashKind == "FileHandle")) { // FileHandle has its own slurp
        std::ifstream in(inv.toStr(), std::ios::binary);
        if (!in) return Value::nil();
        std::ostringstream ss; ss << in.rdbuf();
        Value v = Value::str(ss.str());
        // slurp(:bin) yields a Blob (the raw bytes), not a decoded Str
        for (auto& a : args) if (a.t == VT::Pair && a.s == "bin" && a.pairVal && a.pairVal->truthy()) v.hashKind = "Blob";
        return v;
    }
    if (m == "spurt") {
        std::ofstream out(inv.toStr());
        if (!out) return Value::boolean(false);
        out << (args.empty() ? "" : a0().toStr());
        return Value::boolean(true);
    }
    if ((m == "e" || m == "f" || m == "d" || m == "r" || m == "w" || m == "x") && inv.hashKind == "IO") {
        struct stat st;
        if (stat(inv.toStr().c_str(), &st) != 0) return Value::boolean(false);
        if (m == "d") return Value::boolean(S_ISDIR(st.st_mode));
        if (m == "f") return Value::boolean(S_ISREG(st.st_mode));
        if (m == "r") return Value::boolean(::access(inv.toStr().c_str(), R_OK) == 0);
        if (m == "w") return Value::boolean(::access(inv.toStr().c_str(), W_OK) == 0);
        if (m == "x") return Value::boolean(::access(inv.toStr().c_str(), X_OK) == 0);
        return Value::boolean(true); // e
    }
    if (m == "l" && inv.hashKind == "IO") { // symlink? (lstat, so broken links still count)
#if defined(_WIN32)
        return Value::boolean(false); // Windows: no POSIX symlink test here
#else
        struct stat st;
        return Value::boolean(::lstat(inv.toStr().c_str(), &st) == 0 && S_ISLNK(st.st_mode));
#endif
    }
    if ((m == "s" || m == "z") && inv.hashKind == "IO") { // size / zero-length; both fail if absent
        struct stat st;
        if (stat(inv.toStr().c_str(), &st) != 0)
            throw RakuError{Value::typeObj("X::IO::DoesNotExist"),
                "Failed to stat '" + inv.toStr() + "': no such file or directory"};
        if (m == "z") return Value::boolean(st.st_size == 0);
        return Value::integer((long long)st.st_size);
    }
    if (m == "mode" && inv.hashKind == "IO") { // permission bits as a 4-digit octal string
        struct stat st;
        if (stat(inv.toStr().c_str(), &st) != 0)
            throw RakuError{Value::typeObj("X::IO::DoesNotExist"),
                "Failed to stat '" + inv.toStr() + "': no such file or directory"};
        char buf[8]; snprintf(buf, sizeof buf, "0%03o", st.st_mode & 07777);
        return Value::str(buf);
    }
    if (m == "mkdir") { // $path.IO.mkdir(:parent) — create the directory (and parents)
        std::string path = inv.toStr();
        std::string acc;
        for (size_t i = 0; i <= path.size(); i++) {
            if (i == path.size() || path[i] == '/') {
                if (!acc.empty()) ::mkdir(acc.c_str(), 0777);
                if (i < path.size()) acc += '/';
            } else acc += path[i];
        }
        Value p = Value::str(path); p.hashKind = "IO"; return p;
    }
    if (m == "unlink") { // $path.IO.unlink — remove the file; True on success
        return Value::boolean(::unlink(inv.toStr().c_str()) == 0);
    }
    if (m == "rmdir") { // $path.IO.rmdir — remove the (empty) directory
        return Value::boolean(::rmdir(inv.toStr().c_str()) == 0);
    }
    if (m == "path") return Value::str(inv.toStr());
    if (m == "basename") { std::string s = inv.toStr(); auto p = s.find_last_of('/'); return Value::str(p == std::string::npos ? s : s.substr(p + 1)); }
    // ---- more IO::Path methods (operate on the path string) ----
    {
        auto asIO = [](std::string s) { Value v = Value::str(s); v.hashKind = "IO"; return v; };
        auto dirOf = [](const std::string& s) -> std::string {
            std::string t = s; while (t.size() > 1 && t.back() == '/') t.pop_back();
            auto p = t.find_last_of('/');
            if (p == std::string::npos) return ".";
            return p == 0 ? "/" : t.substr(0, p);
        };
        if (m == "parent") {
            long long up = args.empty() ? 1 : a0().toInt();
            std::string s = inv.toStr();
            for (long long k = 0; k < up; k++) s = dirOf(s);
            return asIO(s);
        }
        if (m == "dirname") return Value::str(dirOf(inv.toStr()));
        if (m == "sibling") return asIO(dirOf(inv.toStr()) + "/" + (args.empty() ? "" : a0().toStr()));
        if (m == "child" || m == "add") {
            std::string s = inv.toStr(); if (!s.empty() && s.back() == '/') s.pop_back();
            return asIO(s + "/" + (args.empty() ? "" : a0().toStr()));
        }
        if (m == "extension") { std::string b = inv.toStr(); auto sl = b.find_last_of('/'); if (sl != std::string::npos) b = b.substr(sl + 1); auto d = b.find_last_of('.'); return Value::str(d == std::string::npos || d == 0 ? "" : b.substr(d + 1)); }
        if (m == "absolute" || m == "resolve" || m == "canonpath" || m == "cleanup") {
            std::string s = inv.toStr();
            if ((m == "absolute" || m == "resolve") && !s.empty() && s[0] != '/') {
                char buf[4096]; if (getcwd(buf, sizeof buf)) s = std::string(buf) + "/" + s;
            }
            return (m == "canonpath" || m == "cleanup") ? Value::str(s) : asIO(s);
        }
        if (m == "is-absolute") return Value::boolean(!inv.toStr().empty() && inv.toStr()[0] == '/');
        if (m == "is-relative") return Value::boolean(inv.toStr().empty() || inv.toStr()[0] != '/');
        if (m == "contents" || m == "dir") {
            Value out = Value::array(); out.isList = true;
            std::string base = inv.toStr();
            if (DIR* d = opendir(base.c_str())) {
                while (struct dirent* e = readdir(d)) {
                    std::string nm = e->d_name;
                    if (nm == "." || nm == "..") continue;
                    out.arr->push_back(asIO(base + (base.empty() || base.back() == '/' ? "" : "/") + nm));
                }
                closedir(d);
            }
            return out;
        }
    }
    if (m == "modified" || m == "created" || m == "accessed" || m == "changed") {
        struct stat st;
        if (stat(inv.toStr().c_str(), &st) != 0)
            throw RakuError{Value::typeObj("X::IO::DoesNotExist"),
                "Failed to get the timestamp of '" + inv.toStr() + "': no such file or directory"};
        // an Instant. Sub-second field names differ by platform; Windows stat only
        // carries second precision.
        double secs;
#if defined(_WIN32)
        time_t t = (m == "accessed") ? st.st_atime : (m == "changed") ? st.st_ctime : st.st_mtime;
        secs = (double)t;
#else
  #if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
        const struct timespec& ats = st.st_atimespec, &cts = st.st_ctimespec, &mts = st.st_mtimespec;
  #else
        const struct timespec& ats = st.st_atim, &cts = st.st_ctim, &mts = st.st_mtim;
  #endif
        const struct timespec& ts = (m == "accessed") ? ats : (m == "changed") ? cts : mts;
        secs = (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
#endif
        return Value::number(secs);
    }
    if (m == "chmod" && inv.hashKind == "IO") { // $path.IO.chmod(0o644)
        ::chmod(inv.toStr().c_str(), (mode_t)(args.empty() ? 0 : args[0].toInt()));
        Value p = Value::str(inv.toStr()); p.hashKind = "IO"; return p;
    }
    if (m == "open") { // returns a buffered file handle
        Value h = Value::makeHash(); h.hashKind = "FileHandle";
        (*h.hash)["path"] = Value::str(inv.toStr());
        std::string mode = "r";
        for (auto& a : args) if (a.t == VT::Pair) { if (a.s == "w") mode = "w"; else if (a.s == "a") mode = "a"; else if (a.s == "r") mode = "r"; }
        (*h.hash)["mode"] = Value::str(mode);
        (*h.hash)["buffer"] = Value::str("");
        return h;
    }
    if (inv.t == VT::Hash && inv.hashKind == "FileHandle") {
        // IO::Handle accessors (with defaults); writable via lvalue()
        if (m == "chomp")  { auto it = inv.hash->find("chomp");  return it != inv.hash->end() ? it->second : Value::boolean(true); }
        if (m == "nl-in")  { auto it = inv.hash->find("nl-in");  return it != inv.hash->end() ? it->second : Value::str("\n"); }
        if (m == "nl-out") { auto it = inv.hash->find("nl-out"); return it != inv.hash->end() ? it->second : Value::str("\n"); }
        if (m == "path" || m == "IO") return (*inv.hash)["path"];
        if (m == "say" || m == "print" || m == "put") {
            auto stdit = inv.hash->find("std");
            if (stdit != inv.hash->end()) { // $*OUT / $*ERR — write straight to the stream
                std::string s; for (auto& a : args) s += (m == "say" ? a.gist() : a.toStr());
                if (m != "print") s += "\n";
                (stdit->second.toStr() == "err" ? std::cerr : std::cout) << s;
                return Value::boolean(true);
            }
            std::string s = (*inv.hash)["buffer"].toStr();
            for (auto& a : args) s += a.toStr();
            if (m != "print") s += "\n";
            (*inv.hash)["buffer"] = Value::str(s);
            return Value::boolean(true);
        }
        if (m == "close") {
            std::string mode = (*inv.hash)["mode"].toStr();
            if (mode == "w" || mode == "a") { // only flush write/append handles
                std::ofstream out((*inv.hash)["path"].toStr(), mode == "a" ? std::ios::app : std::ios::trunc);
                if (out) out << (*inv.hash)["buffer"].toStr();
            }
            return Value::boolean(true);
        }
        if (m == "slurp") {
            auto cap = inv.hash->find("captured"); // in-memory handle (e.g. Proc.out)
            if (cap != inv.hash->end() && cap->second.truthy()) return (*inv.hash)["buffer"];
            if (inv.hash->find("std") != inv.hash->end() && (*inv.hash)["std"].toStr() == "in") {
                std::ostringstream ss; ss << std::cin.rdbuf(); return Value::str(ss.str()); // $*IN.slurp
            }
            std::ifstream in((*inv.hash)["path"].toStr()); std::ostringstream ss; ss << in.rdbuf(); return Value::str(ss.str());
        }
        // .getc / .readchars: load the file's codepoints once, track a cursor in "cpos".
        if (m == "getc" || m == "readchars") {
            if (inv.hash->find("cps") == inv.hash->end()) {
                std::string path = (*inv.hash)["path"].toStr();
                struct stat st;
                if (::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
                    throw RakuError{Value::typeObj("X::IO"), "Cannot read characters from a directory: " + path};
                std::ifstream in(path); std::ostringstream ss; ss << in.rdbuf();
                Value cps = Value::array();
                for (auto cp : utf8cp(ss.str())) cps.arr->push_back(Value::str(cpToUtf8(cp)));
                (*inv.hash)["cps"] = cps;
                (*inv.hash)["cpos"] = Value::integer(0);
            }
            long pos = (*inv.hash)["cpos"].toInt();
            auto& cps = *(*inv.hash)["cps"].arr;
            if (m == "readchars") { // read up to N chars (default 65536), "" at EOF
                long want = args.empty() ? 65536 : args[0].toInt();
                std::string out; long got = 0;
                for (; got < want && pos < (long)cps.size(); got++, pos++) out += cps[pos].toStr();
                (*inv.hash)["cpos"] = Value::integer(pos);
                return Value::str(out);
            }
            if (pos >= (long)cps.size()) return Value::nil(); // getc at EOF → Nil
            (*inv.hash)["cpos"] = Value::integer(pos + 1);
            return cps[pos];
        }
        // reading: lazily load the file into lines, track a cursor in "pos"
        bool isStdin = inv.hash->find("std") != inv.hash->end() && (*inv.hash)["std"].toStr() == "in";
        if (m == "get" || m == "getline" || m == "lines" || m == "eof" || m == "words" || m == "slurp-rest") {
            if (inv.hash->find("lines") == inv.hash->end()) {
                Value lines = Value::array();
                std::string line;
                if (isStdin) { // $*IN — read standard input
                    while (std::getline(std::cin, line)) {
                        if (!line.empty() && line.back() == '\r') line.pop_back();
                        lines.arr->push_back(Value::str(line));
                    }
                } else {
                    std::ifstream in((*inv.hash)["path"].toStr());
                    while (std::getline(in, line)) {
                        if (!line.empty() && line.back() == '\r') line.pop_back();
                        lines.arr->push_back(Value::str(line));
                    }
                }
                (*inv.hash)["lines"] = lines;
                (*inv.hash)["pos"] = Value::integer(0);
            }
            if (m == "words") { // remaining input split on whitespace
                auto& ln = *(*inv.hash)["lines"].arr;
                long long p = (*inv.hash)["pos"].toInt();
                std::string all;
                for (long long i = p; i < (long long)ln.size(); i++) { if (!all.empty()) all += "\n"; all += ln[i].toStr(); }
                (*inv.hash)["pos"] = Value::integer((long long)ln.size());
                Value out = Value::array(); out.isList = true;
                std::istringstream ws(all); std::string w;
                while (ws >> w) out.arr->push_back(Value::str(w));
                return out;
            }
            if (m == "slurp-rest") {
                auto& ln = *(*inv.hash)["lines"].arr;
                long long p = (*inv.hash)["pos"].toInt();
                std::string all;
                for (long long i = p; i < (long long)ln.size(); i++) { all += ln[i].toStr(); all += "\n"; }
                (*inv.hash)["pos"] = Value::integer((long long)ln.size());
                return Value::str(all);
            }
            auto& lines = *(*inv.hash)["lines"].arr;
            long long pos = (*inv.hash)["pos"].toInt();
            if (m == "eof") return Value::boolean(pos >= (long long)lines.size());
            if (m == "lines") {
                Value out = Value::array(); out.isList = true;
                for (long long i = pos; i < (long long)lines.size(); i++) out.arr->push_back(lines[i]);
                (*inv.hash)["pos"] = Value::integer((long long)lines.size());
                return out;
            }
            // get / getline: next line or Nil at EOF
            if (pos >= (long long)lines.size()) return Value::nil();
            (*inv.hash)["pos"] = Value::integer(pos + 1);
            return lines[pos];
        }
    }
    if (m == "lines" && inv.hashKind == "IO") {
        std::ifstream in(inv.toStr()); Value out = Value::array();
        std::string line; while (std::getline(in, line)) out.arr->push_back(Value::str(line));
        return out;
    }

    // string
    // Str/Blob byte views. rakupp stores a Blob/Buf as a Str tagged hashKind="Blob";
    // its raw UTF-8 bytes are the buffer, so encode/decode are (tagged) identity.
    if (m == "new" && inv.t == VT::Str) return Value::str(""); // "literal".new — a fresh empty Str
    if (m == "bytes" && inv.t == VT::Str) return Value::integer((long long)inv.s.size());
    if (m == "encode" && inv.t == VT::Str) { Value b = Value::str(inv.s); b.hashKind = "Blob"; return b; }
    if (m == "decode" && inv.t == VT::Str) return Value::str(inv.s);
    if (m == "chars" || m == "codes" || m == "NFC" || m == "NFD" || m == "NFKC" || m == "NFKD") {
        if (m == "chars") return Value::integer(graphemeCount(inv.toStr())); // graphemes
        if (m == "codes") return Value::integer(cpCount(inv.toStr()));       // codepoints
        int mode = m == "NFD" ? 0 : m == "NFC" ? 1 : m == "NFKD" ? 2 : 3;
        auto norm = uniNormalize(utf8cp(inv.toStr()), mode);
        Value out = Value::array(); out.s = "Uni"; for (auto c : norm) out.arr->push_back(Value::integer((long long)c)); return out;
    }
    if (m == "unival" || m == "univals" || m == "uniname") {
        auto univ = [](uint32_t cp) -> Value { long long num, den; if (!uniNumValue(cp, num, den)) return Value::nil(); return den == 1 ? Value::integer(num) : Value::rat(BigInt(num), BigInt(den)); };
        if (m == "univals") { Value out = Value::array(); out.isList = true; for (uint32_t cp : utf8cp(inv.toStr())) out.arr->push_back(univ(cp)); return out; }
        uint32_t cp; bool have = true;
        if (inv.t == VT::Int || inv.t == VT::Bool) cp = (uint32_t)inv.toInt();
        else { auto cps = utf8cp(inv.toStr()); if (cps.empty()) have = false; else cp = cps[0]; }
        if (m == "uniname") { std::string nm = have ? uniNameOf(cp) : ""; return Value::str(nm.empty() ? "<unassigned>" : nm); }
        return have ? univ(cp) : Value::nil();
    }
    if (m == "uniprop") {
        uint32_t cp; bool have = true;
        if (inv.t == VT::Int || inv.t == VT::Bool) cp = (uint32_t)inv.toInt();
        else { auto cps = utf8cp(inv.toStr()); if (cps.empty()) have = false; else cp = cps[0]; }
        if (!have) return Value::str("");
        std::string prop = args.empty() ? "General_Category" : args[0].toStr();
        if (prop == "Script" || prop == "sc") return Value::str(uniScript(cp));
        return Value::str(uniGeneralCategory(cp)); // General_Category default
    }
    if (m == "uc") return Value::str(mapCase(inv.toStr(), true, 0));
    if (m == "lc") return Value::str(mapCase(inv.toStr(), false, 0));
    if (m == "tc") return Value::str(mapCase(inv.toStr(), false, 1));
    if (m == "tclc" || m == "wordcase") return Value::str(mapCase(inv.toStr(), false, 2));
    if (m == "fc") return Value::str(mapCase(inv.toStr(), false, 0));
    if (m == "samecase") { // copy the case pattern of the arg, position by position (last char repeats)
        auto src = utf8cp(inv.toStr());
        auto pat = utf8cp(args.empty() ? "" : args[0].toStr());
        std::string r;
        for (size_t i = 0; i < src.size(); i++) {
            uint32_t c = src[i];
            uint32_t mask = pat.empty() ? 0 : pat[std::min(i, pat.size() - 1)];
            if (mask && toLowerCp(mask) != mask) r += cpToUtf8(toUpperCp(c));      // mask is upper
            else if (mask && toUpperCp(mask) != mask) r += cpToUtf8(toLowerCp(c)); // mask is lower
            else r += cpToUtf8(c);                                                 // uncased: unchanged
        }
        return Value::str(r);
    }
    if (m == "flip") { auto cps = utf8cp(inv.toStr()); std::string r; for (auto it = cps.rbegin(); it != cps.rend(); ++it) r += cpToUtf8(*it); return Value::str(r); }
    if (m == "ords") { Value out = Value::array(); for (auto cp : utf8cp(inv.toStr())) out.arr->push_back(Value::integer(cp)); return out; }
    if (m == "chomp") { std::string s = inv.toStr(); if (!s.empty() && s.back() == '\n') s.pop_back(); return Value::str(s); }
    if (m == "trim") { std::string s = inv.toStr(); size_t a = s.find_first_not_of(" \t\n\r"); size_t b = s.find_last_not_of(" \t\n\r"); return Value::str(a == std::string::npos ? "" : s.substr(a, b - a + 1)); }
    if (m == "trim-leading") { std::string s = inv.toStr(); size_t a = s.find_first_not_of(" \t\n\r"); return Value::str(a == std::string::npos ? "" : s.substr(a)); }
    if (m == "trim-trailing") { std::string s = inv.toStr(); size_t b = s.find_last_not_of(" \t\n\r"); return Value::str(b == std::string::npos ? "" : s.substr(0, b + 1)); }
    if (m == "substr" || m == "substr-rw") {
        auto cps = utf8cp(inv.toStr());
        long long n = (long long)cps.size();
        long long start = a0().toInt();
        if (start < 0) start += n;
        if (start < 0) start = 0;
        if (start > n) start = n;
        long long len = args.size() > 1 ? args[1].toInt() : n - start;
        if (len < 0) len = n - start + len;
        if (len < 0) len = 0;
        if (start + len > n) len = n - start;
        std::string r; for (long long k = start; k < start + len; k++) r += cpToUtf8(cps[k]);
        return Value::str(r);
    }
    if (m == "index") {
        std::string s = inv.toStr(); auto p = s.find(a0().toStr());
        return p == std::string::npos ? Value::nil() : Value::integer((long long)p);
    }
    if (m == "rindex") {
        std::string s = inv.toStr(); auto p = s.rfind(a0().toStr());
        return p == std::string::npos ? Value::nil() : Value::integer((long long)p);
    }
    // ---- regex-argument string methods ----
    // Find the Regex argument and the replacement — a named adverb (`:g`) may come
    // before the regex (`.subst(:g, /re/, repl)`), so we can't assume positions.
    int rxIdx = -1;
    for (size_t i = 0; i < args.size(); i++) if (args[i].t == VT::Regex) { rxIdx = (int)i; break; }
    if ((m == "match" || m == "subst" || m == "comb" || m == "split" || m == "contains" || m == "subst-mutate")
        && rxIdx >= 0) {
        std::string subj = inv.toStr();
        const std::string& pat = args[rxIdx].s;
        // the replacement is the first positional (non-Pair) arg that isn't the regex
        Value* replArg = nullptr;
        for (size_t i = 0; i < args.size(); i++)
            if ((int)i != rxIdx && args[i].t != VT::Pair) { replArg = &args[i]; break; }
        if (m == "match") return regexMatch(subj, pat);
        if (m == "contains") { Regex re(pat); RxMatch mm; return Value::boolean(re.ok() && re.search(subj, 0, mm)); }
        if (m == "subst") {
            long nsub = 0;
            std::string out = substSelect(subj, pat, replArg, args, nsub);
            return Value::str(out);
        }
        if (m == "comb") {
            Regex re(pat); Value out = Value::array(); long pos = 0; RxMatch mm;
            while (re.ok() && pos <= (long)subj.size() && re.search(subj, pos, mm)) {
                out.arr->push_back(Value::str(subj.substr(mm.from, mm.to - mm.from)));
                pos = mm.to > mm.from ? mm.to : mm.to + 1;
            }
            return out;
        }
        if (m == "split") {
            Regex re(pat); Value out = Value::array(); long pos = 0; RxMatch mm;
            while (re.ok() && pos <= (long)subj.size() && re.search(subj, pos, mm)) {
                if (mm.to == mm.from && mm.from == pos) { if (pos >= (long)subj.size()) break; }
                out.arr->push_back(Value::str(subj.substr(pos, mm.from - pos)));
                pos = mm.to > mm.from ? mm.to : mm.to + 1;
            }
            out.arr->push_back(Value::str(subj.substr(std::min((size_t)pos, subj.size()))));
            return out;
        }
    }
    if (m == "subst" && args.size() >= 1) { // literal (string) substitution
        std::string s = inv.toStr(), from = a0().toStr();
        if (from.empty()) return Value::str(s);
        Value* replArg = nullptr;
        for (size_t i = 1; i < args.size(); i++) if (args[i].t != VT::Pair) { replArg = &args[i]; break; }
        long nsub = 0;
        return Value::str(substSelect(s, from, replArg, args, nsub, /*literal=*/true));
    }
    if (m == "trans") { // $s.trans(@from => @to) / .trans('abc' => 'xyz') / .trans('a..c' => 'A..C')
        std::string s = inv.toStr();
        // a string arg is taken char-by-char, but `X..Y` denotes an inclusive codepoint range
        auto expandTrans = [](const std::string& str) -> std::vector<std::string> {
            std::vector<std::string> out;
            auto cps = utf8cp(str);
            for (size_t i = 0; i < cps.size(); ) {
                if (i + 3 < cps.size() && cps[i + 1] == (uint32_t)'.' && cps[i + 2] == (uint32_t)'.') {
                    uint32_t lo = cps[i], hi = cps[i + 3];
                    if (lo <= hi) for (uint32_t c = lo; c <= hi; c++) out.push_back(cpToUtf8(c));
                    else for (uint32_t c = lo; ; c--) { out.push_back(cpToUtf8(c)); if (c == hi) break; }
                    i += 4;
                } else { out.push_back(cpToUtf8(cps[i])); i++; }
            }
            return out;
        };
        std::vector<std::pair<std::string, std::string>> maps;
        for (auto& a : args) {
            if (a.t != VT::Pair) continue;
            std::vector<std::string> froms, tos;
            if (a.pairKey && a.pairKey->t == VT::Array && a.pairKey->arr)
                for (auto& x : *a.pairKey->arr) froms.push_back(x.toStr());
            else froms = expandTrans(a.s); // string key: char-by-char, with `..` ranges
            if (a.pairVal && a.pairVal->t == VT::Array && a.pairVal->arr)
                for (auto& x : *a.pairVal->arr) tos.push_back(x.toStr());
            else if (a.pairVal) tos = expandTrans(a.pairVal->toStr());
            for (size_t i = 0; i < froms.size(); i++)
                maps.push_back({froms[i], i < tos.size() ? tos[i] : (tos.empty() ? std::string() : tos.back())});
        }
        std::string out;
        for (size_t pos = 0; pos < s.size(); ) {
            size_t bestLen = 0; const std::string* bestTo = nullptr;
            for (auto& kv : maps) {
                if (!kv.first.empty() && kv.first.size() > bestLen &&
                    s.compare(pos, kv.first.size(), kv.first) == 0) { bestLen = kv.first.size(); bestTo = &kv.second; }
            }
            if (bestTo) { out += *bestTo; pos += bestLen; }
            else { out += s[pos]; pos++; }
        }
        return Value::str(out);
    }
    if (m == "contains") return Value::boolean(inv.toStr().find(a0().toStr()) != std::string::npos);
    if (m == "starts-with") { std::string s = inv.toStr(), n = a0().toStr(); return Value::boolean(s.size() >= n.size() && s.compare(0, n.size(), n) == 0); }
    if (m == "ends-with") { std::string s = inv.toStr(), n = a0().toStr(); return Value::boolean(s.size() >= n.size() && s.compare(s.size() - n.size(), n.size(), n) == 0); }
    if (m == "ord") { auto c = utf8cp(inv.toStr()); return c.empty() ? Value::nil() : Value::integer(c[0]); }
    if (m == "chr") return Value::str(cpToUtf8((uint32_t)inv.toInt()));
    if (m == "split") {
        std::string s = inv.toStr();
        Value d0 = a0();
        struct Delim { bool isRx; std::string str; };
        std::vector<Delim> delims;
        auto add = [&](const Value& d) { if (d.t == VT::Regex) delims.push_back({true, d.s}); else delims.push_back({false, d.toStr()}); };
        if (d0.t == VT::Array) { for (auto& e : *d0.arr) add(e); } else add(d0);
        bool keepSep = false, skipEmpty = false;
        for (auto& a : args) if (a.t == VT::Pair && a.pairVal && a.pairVal->truthy()) {
            if (a.s == "v" || a.s == "kv") keepSep = true;
            else if (a.s == "skip-empty") skipEmpty = true;
        }
        Value out = Value::array();
        auto emit = [&](const std::string& piece) { if (!(skipEmpty && piece.empty())) out.arr->push_back(Value::str(piece)); };
        // empty single delimiter => split into characters
        if (delims.size() == 1 && !delims[0].isRx && delims[0].str.empty()) {
            for (auto cp : utf8cp(s)) out.arr->push_back(Value::str(cpToUtf8(cp)));
            return out;
        }
        size_t pos = 0;
        while (pos <= s.size()) {
            size_t bestStart = std::string::npos, bestLen = 0; // earliest, then longest match
            for (auto& d : delims) {
                if (d.isRx) {
                    Regex re(d.str); RxMatch mm;
                    if (re.ok() && re.search(s, (long)pos, mm) && mm.to > mm.from) {
                        size_t st = mm.from, ln = mm.to - mm.from;
                        if (st < bestStart || (st == bestStart && ln > bestLen)) { bestStart = st; bestLen = ln; }
                    }
                } else if (!d.str.empty()) {
                    size_t f = s.find(d.str, pos);
                    if (f != std::string::npos && (f < bestStart || (f == bestStart && d.str.size() > bestLen))) { bestStart = f; bestLen = d.str.size(); }
                }
            }
            if (bestStart == std::string::npos) { emit(s.substr(pos)); break; }
            emit(s.substr(pos, bestStart - pos));
            if (keepSep) out.arr->push_back(Value::str(s.substr(bestStart, bestLen)));
            pos = bestStart + bestLen;
        }
        return out;
    }
    if (m == "words") {
        std::istringstream is(inv.toStr()); std::string w; Value out = Value::array();
        while (is >> w) out.arr->push_back(Value::str(w));
        return out;
    }
    if (m == "lines") {
        std::istringstream is(inv.toStr()); std::string w; Value out = Value::array();
        while (std::getline(is, w)) out.arr->push_back(Value::str(w));
        return out;
    }
    if (m == "comb") {
        Value out = Value::array();
        // .comb($needle): every non-overlapping occurrence of the literal substring
        // (a regex needle is handled earlier); .comb() with no arg: one entry per codepoint.
        if (!args.empty() && args[0].t != VT::Int) {
            std::string subj = inv.toStr(), needle = args[0].toStr();
            if (!needle.empty())
                for (size_t p = subj.find(needle); p != std::string::npos; p = subj.find(needle, p + needle.size()))
                    out.arr->push_back(Value::str(needle));
            return out;
        }
        { // one entry per GRAPHEME (UAX #29 cluster), not per codepoint —
          // "e\x[301]" combs to one "é", emoji ZWJ sequences stay whole.
            auto cps = utf8cp(inv.toStr());
            auto starts = uniGraphemeStarts(cps);
            for (size_t gi = 0; gi < starts.size(); gi++) {
                size_t from = starts[gi], to = gi + 1 < starts.size() ? starts[gi + 1] : cps.size();
                std::string g;
                for (size_t k = from; k < to; k++) g += cpToUtf8(cps[k]);
                out.arr->push_back(Value::str(g));
            }
        }
        return out;
    }
    if (m == "fmt" && inv.t != VT::Array && inv.t != VT::Range && inv.t != VT::Hash)
        return Value::str(doSprintf(args.empty() ? "%s" : a0().toStr(), {inv}));

    // Pair
    if (inv.t == VT::Pair) {
        if (m == "key") return inv.pairKey ? *inv.pairKey : Value::str(inv.s); // object/array keys preserved
        if (m == "value") return inv.pairVal ? *inv.pairVal : Value::any();
        if (m == "kv") return Value::array({inv.pairKey ? *inv.pairKey : Value::str(inv.s), inv.pairVal ? *inv.pairVal : Value::any()});
        if (m == "antipair") return Value::pair((inv.pairVal ? inv.pairVal->toStr() : ""), Value::str(inv.s));
    }

    // list methods on a lone scalar treat it as a 1-element list: 42.grep(*>3), 'x'.map(...)
    if ((inv.t == VT::Int || inv.t == VT::Num || inv.t == VT::Rat || inv.t == VT::Bool ||
         inv.t == VT::Str || inv.t == VT::Complex || inv.t == VT::Pair) &&
        (m == "grep" || m == "map" || m == "first" || m == "sort" || m == "reverse" ||
         m == "flat" || m == "reduce" || m == "grep-index" || m == "first-index" || m == "Supply" ||
         m == "head" || m == "tail" || m == "skip" || m == "elems" || m == "end" ||
         m == "keys" || m == "values" || m == "kv" || m == "pairs" || m == "batch" || m == "rotor")) {
        Value one = Value::array(); one.arr->push_back(inv); one.isList = true;
        return methodCall(one, m, args, rwArgs);
    }

    // lazy list (infinite `… … *` or a lazy `.map` over one): keep `.map`/`.head`
    // lazy so consumers materialise only what they index.
    if (inv.t == VT::Array && inv.ext) {
        bool infinite = std::static_pointer_cast<LazySeqState>(inv.ext)->infinite;
        if (m == "is-lazy") return Value::boolean(true);
        if (infinite) {
            // operations that need the end of the list can't complete on an infinite source
            if (m == "elems" || m == "end" || m == "pop" || m == "tail" || m == "reverse" ||
                m == "sort" || m == "eager" || m == "List" || m == "Array" || m == "sum" ||
                m == "min" || m == "max" || m == "join" || m == "Str" || m == "gist")
                throw RakuError{Value::typeObj("X::Cannot::Lazy"), "Cannot " + m + " a lazy list onto an Array"};
            if (m == "shift") { materializeLazy(inv, 1); if (inv.arr->empty()) return Value::nil(); Value v = inv.arr->front(); inv.arr->erase(inv.arr->begin()); return v; }
        }
        if (m == "map" && !args.empty() && args[0].t == VT::Code && codeArity(args[0]) == 1) {
            Value fn = args[0], src = inv;                 // src shares arr+ext with inv
            Value out = Value::array(); out.isList = true; // 1:1 map → cache index == source index
            auto st = std::make_shared<LazySeqState>();
            Interpreter* self = this;
            st->appendNext = [self, src, fn](ValueList& cache) -> bool {
                size_t si = cache.size();
                self->materializeLazy(src, si + 1);
                if (si >= src.arr->size()) return false;
                ValueList one{ (*src.arr)[si] };
                cache.push_back(self->callCallable(fn, one));
                return true;
            };
            out.ext = st;
            return out;
        }
        if (m == "grep" && !args.empty()) {
            // lazy filter: each appendNext pulls source elements (bounded per call)
            // until the predicate matches, so `(^Inf).grep(…).head(3)` terminates.
            Value pred = args[0], src = inv;
            Value out = Value::array(); out.isList = true;
            auto st = std::make_shared<LazySeqState>();
            auto spos = std::make_shared<size_t>(0); // next unexamined source index
            Interpreter* self = this;
            st->appendNext = [self, src, pred, spos](ValueList& cache) -> bool {
                for (long long tries = 0; tries < 1000000; tries++) { // bail on a never-matching predicate
                    self->materializeLazy(src, *spos + 1);
                    if (*spos >= src.arr->size()) return false;
                    Value v = (*src.arr)[(*spos)++];
                    bool match = pred.t == VT::Code ? self->callCallable(pred, {v}).truthy()
                                                    : applyArith("~~", v, pred).truthy();
                    if (match) { cache.push_back(v); return true; }
                }
                return false;
            };
            out.ext = st;
            return out;
        }
        if (m == "first" && !args.empty()) { // first match, scanning lazily (bounded)
            Value pred = args[0];
            for (size_t si = 0; si < 1000000; si++) {
                materializeLazy(inv, si + 1);
                if (si >= inv.arr->size()) break;
                Value v = (*inv.arr)[si];
                bool match = pred.t == VT::Code ? callCallable(pred, {v}).truthy()
                                                : applyArith("~~", v, pred).truthy();
                if (match) return v;
            }
            return Value::nil();
        }
        if (m == "skip") { // lazy skip: shared view starting n further along the source
            long long n = args.empty() ? 1 : std::max(0LL, args[0].toInt());
            Value src = inv;
            Value out = Value::array(); out.isList = true;
            auto st = std::make_shared<LazySeqState>();
            Interpreter* self = this;
            st->appendNext = [self, src, n](ValueList& cache) -> bool {
                size_t si = cache.size() + (size_t)n;
                self->materializeLazy(src, si + 1);
                if (si >= src.arr->size()) return false;
                cache.push_back((*src.arr)[si]);
                return true;
            };
            out.ext = st;
            return out;
        }
        if (m == "head" && (args.empty() || args[0].t != VT::Whatever)) {
            size_t n = args.empty() ? 1 : (size_t)std::max(0LL, args[0].toInt());
            materializeLazy(inv, n);
            Value out = Value::array(); out.isList = true;
            if (args.empty()) return inv.arr->empty() ? Value::nil() : (*inv.arr)[0]; // scalar .head
            for (size_t i = 0; i < n && i < inv.arr->size(); i++) out.arr->push_back((*inv.arr)[i]);
            return out;
        }
    }

    // list / array / range
    // an infinite range (…..Inf) must not materialise: only lazy views are defined
    if (inv.t == VT::Range && inv.rTo >= 9000000000000000000LL) {
        long long lo = inv.rFrom + (inv.rExFrom ? 1 : 0);
        if (m == "is-lazy" || m == "infinite") return Value::boolean(true);
        if (m == "head" && args.empty()) return Value::integer(lo); // scalar first element
        if (m == "head") { long long n = std::max(0LL, args[0].toInt());
            Value o = Value::array(); o.isList = true; for (long long i = 0; i < n; i++) o.arr->push_back(Value::integer(lo + i)); return o; }
        if (m == "skip") { long long n = args.empty() ? 1 : std::max(0LL, args[0].toInt()); return Value::range(lo + n, inv.rTo, false, inv.rExTo); }
        if (m == "elems" || m == "Numeric" || m == "Int") return Value::number(INFINITY);
        if (m == "min") return Value::integer(lo);
        if (m == "list" || m == "List" || m == "Seq" || m == "cache" || m == "lazy" || m == "flat" ||
            m == "map" || m == "grep" || m == "first" || m == "iterator" || m == "rotor" || m == "batch")
            return (m == "map" || m == "grep" || m == "first") ? methodCall(makeInfArray(lo), m, args, rwArgs) : makeInfArray(lo);
        if (m == "AT-POS" && !args.empty()) return Value::integer(lo + args[0].toInt()); // infRange[i]
        if (m == "tail" || m == "pop" || m == "reverse" || m == "sort" || m == "max" || m == "sum" ||
            m == "Array" || m == "eager" || m == "join" || m == "Str" || m == "gist")
            throw RakuError{Value::typeObj("X::Cannot::Lazy"), "Cannot " + m + " an infinite range"};
    }
    if (inv.t == VT::Array || inv.t == VT::Range || inv.t == VT::Hash) {
        ValueList items = toList(inv);
        // junction methods: @a.any / .all / .none / .one — a tagged-Array junction
        if (m == "any" || m == "all" || m == "none" || m == "one") {
            Value j = Value::array(); j.enumName = m;
            j.arr = std::make_shared<ValueList>(items);
            return j;
        }
        if (m == "Supply") { Value s = Value::makeHash(); s.hashKind = "Supply"; Value v = Value::array(); *v.arr = items; (*s.hash)["values"] = v; return s; }
        if (m == "chrs") { std::string r; for (auto& x : items) r += cpToUtf8((uint32_t)x.toInt()); return Value::str(r); } // list of codepoints -> Str
        if (m == "of") return Value::typeObj("Mu"); // element type of an untyped Array/List
        if (m == "elems") return Value::integer((long long)items.size());
        if (m == "end") return Value::integer((long long)items.size() - 1);
        if (m == "Bool") return Value::boolean(!items.empty());
        if (m == "Array") return inv.t == VT::Array ? inv : Value::array(items);
        if (m == "values") {
            Value out = Value::array();
            if (inv.t == VT::Hash) { for (auto& kv : *inv.hash) out.arr->push_back(kv.second); }
            else out.arr = std::make_shared<ValueList>(items);
            out.isList = true; return out;
        }
        if (m == "list" || m == "flat" || m == "cache" || m == "eager" || m == "Seq" || m == "List")
            return Value::list(items);
        if (m == "reverse") { std::reverse(items.begin(), items.end()); return Value::list(items); }
        if (m == "rotate") { long n = args.empty() ? 1 : args[0].toInt(); long sz = (long)items.size();
            if (sz) { n = ((n % sz) + sz) % sz; std::rotate(items.begin(), items.begin() + n, items.end()); }
            return Value::list(items); }
        if (m == "permutations") {
            Value out = Value::array(); out.isList = true; out.s = "Seq";
            std::vector<size_t> idx(items.size());
            for (size_t i = 0; i < idx.size(); i++) idx[i] = i;
            // generate in lexicographic order of indices (matches Rakudo's ordering)
            do {
                Value perm = Value::array();
                for (size_t i : idx) perm.arr->push_back(items[i]);
                out.arr->push_back(perm);
            } while (std::next_permutation(idx.begin(), idx.end()));
            return out;
        }
        if (m == "combinations") {
            Value out = Value::array(); out.isList = true; out.s = "Seq";
            long long lo = 0, hi = (long long)items.size();
            if (!args.empty()) {
                Value k = a0();
                if (k.t == VT::Range) { lo = k.rFrom; hi = k.rExTo ? k.rTo - 1 : k.rTo; }
                else { lo = hi = k.toInt(); }
            }
            if (hi > (long long)items.size()) hi = (long long)items.size();
            for (long long k = lo; k <= hi; k++) {
                if (k < 0) continue;
                std::vector<bool> mask(items.size(), false);
                for (long long i = 0; i < k; i++) mask[items.size() - 1 - i] = true; // start: choose last k, then permute mask ascending
                std::vector<size_t> sel;
                // enumerate all k-subsets in index-ascending order
                std::vector<long long> c(k);
                for (long long i = 0; i < k; i++) c[i] = i;
                while (k == 0 ? (sel.empty()) : true) {
                    Value combo = Value::array();
                    for (long long i = 0; i < k; i++) combo.arr->push_back(items[c[i]]);
                    out.arr->push_back(combo);
                    if (k == 0) break;
                    long long i = k - 1;
                    while (i >= 0 && c[i] == (long long)items.size() - k + i) i--;
                    if (i < 0) break;
                    c[i]++;
                    for (long long j = i + 1; j < k; j++) c[j] = c[j-1] + 1;
                }
            }
            return out;
        }
        if (m == "join") return Value::str(joinValues(items, args.empty() ? "" : a0().toStr()));
        if (m == "fmt") {
            std::string fmt = args.empty() ? "%s" : a0().toStr();
            std::string sep = args.size() > 1 ? args[1].toStr() : " ";
            std::string out;
            for (size_t k = 0; k < items.size(); k++) { if (k) out += sep; out += doSprintf(fmt, {items[k]}); }
            return Value::str(out);
        }
        if (m == "sum") { double s = 0; bool allInt = true; for (auto& v : items) { s += v.toNum(); if (v.t != VT::Int) allInt = false; } return allInt ? Value::integer((long long)s) : Value::number(s); }
        if (m == "enums") { // enum type (a pair-list) -> Map of name => value
            Value h = Value::makeHash();
            for (auto& v : items) if (v.t == VT::Pair) (*h.hash)[v.s] = v.pairVal ? *v.pairVal : Value::any();
            return h;
        }
        if (m == "hyper" || m == "race") { Value o = Value::array(items); o.isList = true; return o; } // parallel -> sequential
        if (m == "is-lazy") return Value::boolean(false); // materialised list is not lazy
        if (m == "reduce" && !args.empty() && args[0].t == VT::Code) { // fold with a 2-arg op: (1,2,3).reduce(* + *)
            if (items.empty()) return Value::any();
            Value acc = items[0];
            for (size_t k = 1; k < items.size(); k++) acc = callCallable(args[0], {acc, items[k]});
            return acc;
        }
        if (m == "produce" && !args.empty() && args[0].t == VT::Code) { // scan: running reductions
            Value out = Value::array(); out.isList = true;
            if (items.empty()) return out;
            Value acc = items[0]; out.arr->push_back(acc);
            for (size_t k = 1; k < items.size(); k++) { acc = callCallable(args[0], {acc, items[k]}); out.arr->push_back(acc); }
            return out;
        }
        if (m == "classify" || m == "categorize") { // group elements by a mapper into a Hash of lists
            Value mapper = args.empty() ? Value::nil() : args[0];
            Value h = Value::makeHash();
            auto add = [&](const std::string& key, const Value& v) {
                auto it = h.hash->find(key);
                if (it == h.hash->end()) { Value a = Value::array(); a.arr->push_back(v); (*h.hash)[key] = a; }
                else { if (it->second.t != VT::Array) { Value a = Value::array(); a.arr->push_back(it->second); it->second = a; } it->second.arr->push_back(v); }
            };
            for (auto& v : items) {
                Value k = mapper.t == VT::Code ? callCallable(mapper, {v}) : v;
                if (m == "categorize" && k.t == VT::Array && k.arr) { for (auto& kk : *k.arr) add(kk.toStr(), v); }
                else add(k.toStr(), v);
            }
            return h;
        }
        if (m == "rotor" || m == "batch") { // chunk into sublists of a fixed size
            long long n = args.empty() ? 1 : args[0].toInt();
            if (n < 1) n = 1;
            bool partial = (m == "batch"); // batch always keeps a short final chunk; rotor drops it unless :partial
            for (auto& a : args) if (a.t == VT::Pair && a.s == "partial" && a.pairVal && a.pairVal->truthy()) partial = true;
            Value out = Value::array(); out.isList = true;
            for (size_t i = 0; i < items.size(); i += (size_t)n) {
                if (i + (size_t)n > items.size() && !partial) break;
                Value chunk = Value::array(); chunk.isList = true;
                for (size_t j = i; j < i + (size_t)n && j < items.size(); j++) chunk.arr->push_back(items[j]);
                out.arr->push_back(chunk);
            }
            return out;
        }
        if (m == "snip") { // 6.e: split into sublists — each predicate consumes the
            // leading run it matches; leftovers form the final sublist. The predicate
            // arg is one Callable/type-object, or a list of them.
            std::vector<Value> preds;
            for (auto& p : args) {
                if (p.t == VT::Array && p.arr) for (auto& q : *p.arr) preds.push_back(q); // a (p1,p2) list of preds
                else preds.push_back(p);
            }
            auto matches = [&](const Value& pred, const Value& el) -> bool {
                if (pred.t == VT::Code) return boolify(callCallable(pred, {el}));
                if (pred.t == VT::Type) return rtTypeMatch(el, pred.s);
                return deepEq(pred, el);
            };
            Value out = Value::array(); out.isList = true;
            size_t idx = 0;
            for (auto& pred : preds) {
                Value sub = Value::array(); sub.isList = true;
                while (idx < items.size() && matches(pred, items[idx])) sub.arr->push_back(items[idx++]);
                out.arr->push_back(sub);
            }
            if (idx < items.size()) {
                Value sub = Value::array(); sub.isList = true;
                while (idx < items.size()) sub.arr->push_back(items[idx++]);
                out.arr->push_back(sub);
            }
            return out;
        }
        if (m == "are") { // 6.e: narrowest common type, or `.are(T)` = all-conform check
            if (!args.empty()) {
                std::string t = typeOfVal(args[0]);
                for (auto& el : items) if (!rtTypeMatch(el, t))
                    throw RakuError{Value::typeObj("X::AdHoc"), "Not all list elements are of type " + t};
                return Value::boolean(true);
            }
            if (items.empty()) return Value::nil();
            std::string lub = typeOfVal(items[0]);
            for (size_t k = 1; k < items.size(); k++) lub = lubType(lub, typeOfVal(items[k]));
            return Value::typeObj(lub);
        }
        if (m == "min" || m == "max") {
            if (items.empty()) return Value::any();
            bool wantMax = (m == "max");
            // an optional &mapper: compare by mapper($_), returning the original element.
            Value mapper = (!args.empty() && args[0].t == VT::Code) ? args[0] : Value::nil();
            Value best, bestKey; bool started = false;
            for (auto& v : items) {
                Value key = v;
                if (mapper.t == VT::Code) { ValueList one{v}; key = callCallable(mapper, one); }
                if (!started) { best = v; bestKey = key; started = true; continue; }
                int c = valueCmp(key, bestKey); // strict compare keeps the FIRST on ties
                if ((!wantMax && c < 0) || (wantMax && c > 0)) { best = v; bestKey = key; }
            }
            return best;
        }
        // resolve a head/tail count arg: Int, `*` (all), or `*-N` (WhateverCode of the length)
        auto resolveCount = [&](Value a, long long sz) -> long long {
            if (a.t == VT::Whatever) return sz;
            if (a.isNumeric() && std::isinf(a.toNum())) return sz; // head(Inf) / tail(Inf) = all
            if (a.t == VT::Code && a.code && a.code->isWhateverCode) { ValueList one{Value::integer(sz)}; return callCallable(a, one).toInt(); }
            if (a.t == VT::Str) { // a non-numeric string count is an error (.skip("foo"))
                const std::string& s = a.s; bool num = !s.empty();
                for (char c : s) if (!std::isdigit((unsigned char)c) && c != '-' && c != '+' && c != '.' && c != ' ') { num = false; break; }
                if (!num) throw RakuError{Value::typeObj("X::Str::Numeric"), "Cannot convert string to number: '" + s + "'"};
            }
            return a.toInt();
        };
        if (m == "head") {
            if (args.empty()) return items.empty() ? Value::any() : items.front();
            long long n = resolveCount(a0(), (long long)items.size());
            if (n < 0) n = 0;
            Value o = Value::array(); o.isList = true;
            for (long long k = 0; k < n && k < (long long)items.size(); k++) o.arr->push_back(items[k]);
            return o;
        }
        if (m == "tail") {
            if (args.empty()) return items.empty() ? Value::any() : items.back();
            long long n = resolveCount(a0(), (long long)items.size());
            if (n < 0) n = 0;
            Value o = Value::array(); o.isList = true;
            long long start = std::max(0LL, (long long)items.size() - n);
            for (long long k = start; k < (long long)items.size(); k++) o.arr->push_back(items[k]);
            return o;
        }
        if (m == "skip") { // drop the first n elements (default 1)
            long long n = args.empty() ? 1 : resolveCount(a0(), (long long)items.size());
            if (n < 0) n = 0;
            Value o = Value::array(); o.isList = true;
            for (long long k = n; k < (long long)items.size(); k++) o.arr->push_back(items[k]);
            return o;
        }
        if (m == "first") {
            if (!args.empty() && args[0].t == VT::Code)
                for (auto& v : items) { if (callCallable(args[0], {v}).truthy()) return v; }
            else if (!items.empty()) return items.front();
            return Value::any();
        }
        if (m == "pick" || m == "roll") { // random element(s); pick = without replacement
            // an enum type picks from its VALUES (red/green/blue), not its (key=>val) pairs
            ValueList enumVals;
            for (auto& pr : items) if (!inv.enumType.empty() && pr.t == VT::Pair) {
                Value ev = Value::enumVal(pr.s, pr.pairVal ? pr.pairVal->toInt() : 0);
                ev.enumType = inv.enumType; enumVals.push_back(ev);
            }
            const ValueList& pool0 = inv.enumType.empty() ? items : enumVals;
            if (pool0.empty()) return args.empty() ? Value::nil() : Value::array();
            bool all = !args.empty() && (args[0].t == VT::Whatever ||
                       (args[0].t == VT::Str && (args[0].s == "*" || args[0].s == "Inf")) ||
                       (args[0].isNumeric() && std::isinf(args[0].toNum())));
            if (args.empty()) return pool0[(size_t)(randDouble() * pool0.size())]; // single element
            long long n = all ? (long long)pool0.size() : args[0].toInt();
            Value out = Value::array(); out.isList = true;
            if (m == "pick") { // without replacement
                ValueList pool = pool0;
                for (long long i = 0; i < n && !pool.empty(); i++) {
                    size_t j = (size_t)(randDouble() * pool.size());
                    out.arr->push_back(pool[j]); pool.erase(pool.begin() + j);
                }
            } else { // roll: with replacement
                for (long long i = 0; i < n; i++) out.arr->push_back(pool0[(size_t)(randDouble() * pool0.size())]);
            }
            return out;
        }
        if (m == "unique") {
            // :as(&mapper) compares mapped keys; :with(&eq) uses a custom equality (O(n²)).
            Value asF, withF;
            for (auto& a : args) if (a.t == VT::Pair && a.pairVal && a.pairVal->t == VT::Code) { if (a.s == "as") asF = *a.pairVal; else if (a.s == "with") withF = *a.pairVal; }
            auto keyOf = [&](const Value& v) { return asF.t == VT::Code ? callCallable(asF, ValueList{v}) : v; };
            Value out = Value::array(); out.isList = true;
            if (withF.t == VT::Code) {
                ValueList kept;
                for (auto& v : items) { Value k = keyOf(v); bool dup = false;
                    for (auto& kk : kept) if (callCallable(withF, ValueList{k, kk}).truthy()) { dup = true; break; }
                    if (!dup) { kept.push_back(k); out.arr->push_back(v); } }
            } else {
                std::set<std::string> seen;
                for (auto& v : items) if (seen.insert(keyOf(v).toStr()).second) out.arr->push_back(v);
            }
            return out;
        }
        if (m == "repeated") { // elements seen more than once (2nd+ occurrences)
            Value out = Value::array(); std::set<std::string> seen;
            for (auto& v : items) if (!seen.insert(v.toStr()).second) out.arr->push_back(v);
            out.isList = true;
            return out;
        }
        if (m == "squish") { // collapse adjacent duplicates (:as maps keys, :with compares them)
            Value asF, withF;
            for (auto& a : args) if (a.t == VT::Pair && a.pairVal && a.pairVal->t == VT::Code) { if (a.s == "as") asF = *a.pairVal; else if (a.s == "with") withF = *a.pairVal; }
            auto keyOf = [&](const Value& v) { return asF.t == VT::Code ? callCallable(asF, ValueList{v}) : v; };
            Value out = Value::array(); out.isList = true;
            bool first = true; Value prevKey;
            for (auto& v : items) {
                Value k = keyOf(v); bool same = false;
                if (!first) same = withF.t == VT::Code ? callCallable(withF, ValueList{k, prevKey}).truthy() : (k.toStr() == prevKey.toStr());
                if (first || !same) out.arr->push_back(v);
                prevKey = k; first = false;
            }
            return out;
        }
        if (m == "sort") {
            if (!args.empty() && args[0].t == VT::Code) {
                Value blk = args[0];
                size_t arity = blk.code->params && !blk.code->params->empty()
                    ? blk.code->params->size()
                    : (blk.code->placeholders.empty() ? (size_t)blk.code->whateverArity : blk.code->placeholders.size());
                if (arity >= 2) {
                    std::stable_sort(items.begin(), items.end(), [&](const Value& x, const Value& y) {
                        return callCallable(blk, {x, y}).toInt() < 0;
                    });
                } else {
                    std::stable_sort(items.begin(), items.end(), [&](const Value& x, const Value& y) {
                        Value kx = callCallable(blk, {x}); Value ky = callCallable(blk, {y});
                        return valueCmp(kx, ky) < 0;
                    });
                }
            } else {
                std::stable_sort(items.begin(), items.end(), [](const Value& x, const Value& y) { return valueCmp(x, y) < 0; });
            }
            return Value::list(items);
        }
        if (m == "map" || m == "flatmap") { // flatmap == map that flattens list results one level
            Value out = Value::array();
            if (!args.empty() && args[0].t == VT::Code) {
                // A block of arity N consumes N elements per iteration
                // (e.g. `%h.kv.map(-> $k, $v {…})` or `{ $^a … $^b }`).
                size_t ar = codeArity(args[0]);
                for (size_t i = 0; i < items.size(); i += ar) {
                    ValueList ca;
                    for (size_t k = 0; k < ar && i + k < items.size(); k++) ca.push_back(items[i + k]);
                    Value r = callCallable(args[0], ca);
                    if (r.t == VT::Array) for (auto& x : *r.arr) out.arr->push_back(x);
                    else if (r.t == VT::Range) for (auto& x : r.flatten()) out.arr->push_back(x);
                    else out.arr->push_back(r);
                }
            }
            out.isList = true;
            return out;
        }
        if (m == "grep") {
            Value out = Value::array(); out.isList = true;
            if (args.empty()) return out;
            Value mt = args[0];
            for (auto& v : items) {
                bool match;
                if (mt.t == VT::Code) match = callCallable(mt, {v}).truthy();
                else if (mt.t == VT::Regex) match = regexMatch(v.toStr(), mt.s).truthy(); // .grep(/re/)
                else match = applyArith("~~", v, mt).truthy();                            // .grep(Int) / value
                if (match) out.arr->push_back(v);
            }
            return out;
        }
        if (m == "hash" && inv.t == VT::Hash) return inv;   // %h.hash is the hash itself
        if ((m == "hash" || m == "Hash") && inv.t == VT::Array) { // list -> Hash
            // Pairs map directly; non-Pair elements pair up CONSECUTIVELY as
            // key, value — `(0,"a",1,"b").hash` is {0 => "a", 1 => "b"}, so
            // `@a.kv.reverse.hash` inverts an index map (value => index).
            Value h = Value::makeHash();
            for (size_t k = 0; k < items.size(); k++) {
                if (items[k].t == VT::Pair)
                    (*h.hash)[items[k].s] = items[k].pairVal ? *items[k].pairVal : Value::any();
                else if (k + 1 < items.size()) {
                    std::string key = items[k].toStr(); // sequenced explicitly: in `m[f(k)] = g(++k)`
                    (*h.hash)[key] = items[++k];        // the RHS would evaluate before the key!
                }
                else // odd trailing key (Rakudo dies; we stay lenient)
                    (*h.hash)[items[k].toStr()] = Value::any();
            }
            return h;
        }
        if ((m == "push" || m == "append") && inv.t == VT::Hash) { // %h.push(:a(1)) accumulates into a list
            for (auto& a : args) {
                if (a.t != VT::Pair) continue;
                std::string key = a.s; Value val = a.pairVal ? *a.pairVal : Value::any();
                auto it = inv.hash->find(key);
                if (it == inv.hash->end()) {
                    if (m == "append" || (val.t == VT::Array && val.isList)) { Value ar = Value::array(); for (auto& x : val.flatten()) ar.arr->push_back(x); (*inv.hash)[key] = ar; }
                    else (*inv.hash)[key] = val;
                } else {
                    if (it->second.t != VT::Array) { Value ar = Value::array(); ar.arr->push_back(it->second); it->second = ar; }
                    if (m == "append") for (auto& x : val.flatten()) it->second.arr->push_back(x);
                    else it->second.arr->push_back(val);
                }
            }
            return inv;
        }
        if (m == "keys") {
            Value out = Value::array();
            if (inv.t == VT::Hash) { for (auto& kv : *inv.hash) out.arr->push_back(Value::str(kv.first)); }
            else for (size_t i = 0; i < items.size(); i++) out.arr->push_back(Value::integer((long long)i));
            out.isList = true;
            return out;
        }
        if (m == "invert" && inv.t == VT::Hash) { // %h.invert -> list of (value => key)
            Value out = Value::array(); out.isList = true; out.s = "Seq";
            for (auto& kv : *inv.hash) {
                if (kv.second.t == VT::Array && kv.second.arr)
                    for (auto& v : *kv.second.arr) out.arr->push_back(Value::pair(v.toStr(), Value::str(kv.first)));
                else out.arr->push_back(Value::pair(kv.second.toStr(), Value::str(kv.first)));
            }
            return out;
        }
        if (m == "antipairs" && inv.t == VT::Hash) { // (value => key) pairs, like invert
            Value out = Value::array(); out.isList = true; out.s = "Seq";
            for (auto& kv : *inv.hash) out.arr->push_back(Value::pair(kv.second.toStr(), Value::str(kv.first)));
            return out;
        }
        if (m == "pairs" || m == "kv" || m == "antipairs") {
            Value out = Value::array();
            if (inv.t == VT::Hash) {
                for (auto& kv : *inv.hash) {
                    if (m == "kv") { out.arr->push_back(Value::str(kv.first)); out.arr->push_back(kv.second); }
                    else out.arr->push_back(Value::pair(kv.first, kv.second));
                }
            } else {
                for (size_t i = 0; i < items.size(); i++) {
                    if (m == "kv") { out.arr->push_back(Value::integer((long long)i)); out.arr->push_back(items[i]); }
                    else out.arr->push_back(Value::pair(std::to_string(i), items[i]));
                }
            }
            return out;
        }
        // mutators on real arrays
        if (inv.t == VT::Array && inv.arr) {
            // push/unshift add each argument as one element; append/prepend flatten
            if (m == "push") { for (auto& a : args) inv.arr->push_back(a); return Value::integer((long long)inv.arr->size()); }
            // append/prepend follow the single-argument rule: a lone Positional arg is
            // treated as the list of values (flattened one level); multiple args are each
            // added as-is (nested lists preserved, exactly like push).
            auto appendValues = [](ValueList& args) -> ValueList {
                if (args.size() == 1 && args[0].t == VT::Array && args[0].arr)
                    return *args[0].arr;   // one-level: the sole list's own elements
                return args;               // 2+ args: each as-is
            };
            if (m == "append") { for (auto& a : appendValues(args)) inv.arr->push_back(a); return Value::integer((long long)inv.arr->size()); }
            if (m == "unshift") { inv.arr->insert(inv.arr->begin(), args.begin(), args.end()); return Value::integer((long long)inv.arr->size()); }
            if (m == "prepend") { auto f = appendValues(args); inv.arr->insert(inv.arr->begin(), f.begin(), f.end()); return Value::integer((long long)inv.arr->size()); }
            if (m == "pop") { if (inv.arr->empty()) return Value::typeObj("Failure"); Value v = inv.arr->back(); inv.arr->pop_back(); return v; }
            if (m == "shift") { if (inv.arr->empty()) return Value::typeObj("Failure"); Value v = inv.arr->front(); inv.arr->erase(inv.arr->begin()); return v; }
            if (m == "splice") { // .splice($start?, $count?, *@replacement) → the removed elements
                long n = (long)inv.arr->size();
                long start = args.size() > 0 ? args[0].toInt() : 0;
                if (start < 0) start += n;
                start = std::max(0L, std::min(start, n));
                long count = args.size() > 1 ? args[1].toInt() : (n - start);
                count = std::max(0L, std::min(count, n - start));
                Value removed = Value::array(); removed.isList = true;
                for (long k = 0; k < count; k++) removed.arr->push_back((*inv.arr)[start + k]);
                ValueList repl;
                for (size_t k = 2; k < args.size(); k++) for (auto& x : toList(args[k])) repl.push_back(x);
                inv.arr->erase(inv.arr->begin() + start, inv.arr->begin() + start + count);
                inv.arr->insert(inv.arr->begin() + start, repl.begin(), repl.end());
                return removed;
            }
        }
        if (inv.t == VT::Hash && inv.hash) {
            if (m == "exists") return Value::boolean(inv.hash->count(a0().toStr()) > 0);
        }
    }

    // an undefined scalar still reports as a 1-item list for .elems (Any.elems == 1)
    if ((inv.t == VT::Any || inv.t == VT::Nil) && m == "elems") return Value::integer(1);
    // fallthrough: unknown method
    throw RakuError{Value::str("No method '" + m + "'"),
                    "No such method '" + m + "' for type " + inv.typeName()};
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

void Interpreter::registerBuiltins() {
    auto& B = builtins_;

    B["say"] = [](Interpreter& I, ValueList& a) -> Value {
        std::string out;
        for (auto& v : a) out += I.gistOf(v);
        out += "\n"; return I.ioEmit(out, "$*OUT", false);
    };
    B["print"] = [](Interpreter& I, ValueList& a) -> Value {
        std::string out; for (auto& v : a) out += I.strOf(v);
        return I.ioEmit(out, "$*OUT", false);
    };
    B["put"] = [](Interpreter& I, ValueList& a) -> Value {
        std::string out; for (auto& v : a) out += I.strOf(v); out += "\n";
        return I.ioEmit(out, "$*OUT", false);
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
                payload = ex;
            }
        }
        throw RakuError{payload, msg};
    };
    // Re-dispatch to the next candidate (currently: a built-in shadowed by a user method).
    // callsame/callwith return its result; nextsame/nextwith return it FROM the current routine.
    // `lastcall` marks the current candidate as the final one: a subsequent
    // callsame/nextsame finds no more candidates (returns Nil / an empty result).
    B["lastcall"] = [](Interpreter& I, ValueList&) -> Value {
        if (!I.redispatchStack_.empty()) I.redispatchStack_.back().lastcall = true;
        return Value::boolean(true);
    };
    B["callsame"] = [](Interpreter& I, ValueList&) -> Value {
        if (I.redispatchStack_.empty()) throw RakuError{Value::typeObj("X::NoDispatcher"), "callsame with no dispatcher in scope"};
        if (I.redispatchStack_.back().lastcall) return Value::nil(); // trimmed by lastcall
        return I.redispatchStack_.back().next(I.redispatchStack_.back().sameArgs);
    };
    B["callwith"] = [](Interpreter& I, ValueList& a) -> Value {
        if (I.redispatchStack_.empty()) throw RakuError{Value::typeObj("X::NoDispatcher"), "callwith with no dispatcher in scope"};
        if (I.redispatchStack_.back().lastcall) return Value::nil();
        return I.redispatchStack_.back().next(a);
    };
    B["nextsame"] = [](Interpreter& I, ValueList&) -> Value {
        if (I.redispatchStack_.empty()) throw RakuError{Value::typeObj("X::NoDispatcher"), "nextsame with no dispatcher in scope"};
        if (I.redispatchStack_.back().lastcall) throw ReturnEx{Value::nil()};
        throw ReturnEx{I.redispatchStack_.back().next(I.redispatchStack_.back().sameArgs)};
    };
    B["samewith"] = [](Interpreter& I, ValueList& a) -> Value {
        // re-dispatch the CURRENT routine from scratch with new args, returning its result
        if (I.redispatchStack_.empty() || !I.redispatchStack_.back().restart)
            throw RakuError{Value::typeObj("X::NoDispatcher"), "samewith with no dispatcher in scope"};
        return I.redispatchStack_.back().restart(a);
    };
    B["nextwith"] = [](Interpreter& I, ValueList& a) -> Value {
        if (I.redispatchStack_.empty()) throw RakuError{Value::typeObj("X::NoDispatcher"), "nextwith with no dispatcher in scope"};
        if (I.redispatchStack_.back().lastcall) throw ReturnEx{Value::nil()};
        throw ReturnEx{I.redispatchStack_.back().next(a)};
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
        Value f = Value::makeHash(); f.hashKind = "Failure";
        (*f.hash)["exception"] = ex;
        throw ReturnEx{f};
    };
    B["prompt"] = [](Interpreter&, ValueList& a) -> Value {
        if (!a.empty()) { std::cout << a[0].toStr(); std::cout.flush(); }
        std::string line;
        if (!std::getline(std::cin, line)) return Value::any();
        if (!line.empty() && line.back() == '\r') line.pop_back();
        Value s = Value::str(line); return s; // Raku returns a Str that numifies on demand
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
            for (auto& v : a) { if (v.t == VT::Array) { for (auto& x : *v.arr) j.arr->push_back(x); } else j.arr->push_back(v); }
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
        if (!a.empty()) { I.planned_ = a[0].toInt(); std::cout << "1.." << I.planned_ << "\n"; }
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
            if (g.isNumeric() && e.isNumeric()) { double a = g.toNum(), b = e.toNum(); return (std::isnan(a) && std::isnan(b)) || a == b; }
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
        return Value::boolean(c);
    };
    B["cmp-ok"] = [](Interpreter& I, ValueList& a) -> Value {
        // cmp-ok($a, $op, $b, $desc)
        bool c = false;
        if (a.size() >= 3) {
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
        I.emitTest(c, a.size() > 3 ? a[3].toStr() : "");
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
    B["isa-ok"] = [](Interpreter& I, ValueList& a) -> Value {
        std::string want = a.size() > 1 ? (a[1].t == VT::Type ? a[1].s : a[1].toStr()) : "";
        std::string got = a.empty() ? "Any" : a[0].typeName();
        static const std::map<std::string, std::set<std::string>> isa = {
            {"Int", {"Int", "Cool", "Numeric", "Real", "Any", "Mu"}},
            {"Num", {"Num", "Cool", "Numeric", "Real", "Any", "Mu"}},
            {"Str", {"Str", "Cool", "Stringy", "Any", "Mu"}},
            {"Bool", {"Bool", "Any", "Mu"}},
            {"Sub", {"Sub", "Routine", "Block", "Code", "Callable", "Any", "Mu"}},
            {"Method", {"Method", "Routine", "Block", "Code", "Callable", "Any", "Mu"}},
            {"Block", {"Block", "Code", "Callable", "Any", "Mu"}},
            {"Array", {"Array", "List", "Any", "Mu", "Positional"}},
            {"Seq", {"Seq", "List", "Any", "Mu", "Positional", "Iterable"}},
            {"IO::Path", {"IO::Path", "IO", "Cool", "Any", "Mu"}},
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
        I.emitTest(c, a.size() > 2 ? a[2].toStr() : "");
        return Value::boolean(c);
    };
    B["is-approx"] = [](Interpreter& I, ValueList& a) -> Value {
        double got = a.size() > 0 ? a[0].toNum() : 0;
        double exp = a.size() > 1 ? a[1].toNum() : 0;
        double tol = 1e-5;
        std::string desc;
        if (a.size() > 2) { if (a[2].isNumeric()) tol = a[2].toNum(); else desc = a[2].toStr(); }
        double scale = std::max({std::fabs(got), std::fabs(exp), 1.0});
        bool c = std::fabs(got - exp) <= tol * scale;
        I.emitTest(c, desc);
        return Value::boolean(c);
    };
    B["throws-like"] = [](Interpreter& I, ValueList& a) -> Value {
        bool threw = false;
        if (!a.empty()) {
            try {
                if (a[0].t == VT::Code) I.callCallable(a[0], {});
                else if (a[0].t == VT::Str) I.evalString(a[0].s);
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
        if (!a.empty()) {
            try {
                Value r;
                if (a[0].t == VT::Code) r = I.callCallable(a[0], {});
                else if (a[0].t == VT::Str) r = I.evalString(a[0].s);
                if (r.t == VT::Type && r.s == "Failure") failed = true;
            } catch (RakuError&) { failed = true; }
        }
        // description = the first Str positional after the exception type (index >= 2)
        std::string desc;
        for (size_t i = 2; i < a.size(); i++) if (a[i].t == VT::Str) { desc = a[i].s; break; }
        I.emitTest(failed, desc);
        return Value::boolean(failed);
    };
    B["eval-lives-ok"] = [](Interpreter& I, ValueList& a) -> Value {
        bool lived = true;
        try { if (!a.empty()) I.evalString(a[0].toStr()); } catch (RakuError&) { lived = false; }
        I.emitTest(lived, a.size() > 1 ? a[1].toStr() : "");
        return Value::boolean(lived);
    };
    B["eval-dies-ok"] = [](Interpreter& I, ValueList& a) -> Value {
        bool died = false;
        try { if (!a.empty()) I.evalString(a[0].toStr()); } catch (RakuError&) { died = true; }
        I.emitTest(died, a.size() > 1 ? a[1].toStr() : "");
        return Value::boolean(died);
    };
    B["EVAL"] = [](Interpreter& I, ValueList& a) -> Value {
        Value code; bool haveCode = false;
        for (auto& v : a) {
            if (v.t == VT::Pair && v.s == "lang") {
                std::string lang = v.pairVal ? v.pairVal->toStr() : "";
                if (lang != "Raku" && lang != "Perl6")
                    throw RakuError{Value::str("Cannot EVAL :lang<" + lang + ">"),
                                    "Cannot EVAL code in language '" + lang + "'"};
            } else if (v.t != VT::Pair && !haveCode) { code = v; haveCode = true; }
        }
        return haveCode ? I.evalString(code.toStr()) : Value::any();
    };
    B["exit"] = [](Interpreter&, ValueList& a) -> Value {
        throw ExitEx{(int)(a.empty() ? 0 : a[0].toInt())};
    };
    // stub / yada operators
    B["!!!"] = [](Interpreter&, ValueList& a) -> Value { throw RakuError{Value::str("X::StubCode"), a.empty() ? "Stub code executed" : a[0].toStr()}; };
    B["..."] = [](Interpreter&, ValueList& a) -> Value { throw RakuError{Value::str("X::StubCode"), a.empty() ? "Stub code executed" : a[0].toStr()}; };
    B["???"] = [](Interpreter&, ValueList& a) -> Value { std::cerr << (a.empty() ? "Stub code executed" : a[0].toStr()) << "\n"; return Value::nil(); };
    // run(prog, *@args, :timeout(N)) -> { out => Str, exitcode => Int, timedout => Bool }
    B["run"] = [](Interpreter& I, ValueList& a) -> Value {
        std::vector<std::string> argv; bool wantOut = false, wantIn = false, wantErr = false;
        for (auto& v : flattenArgs(a)) {
            if (v.t == VT::Pair) {
                if (v.s == "out") wantOut = v.pairVal ? v.pairVal->truthy() : true;
                else if (v.s == "err") wantErr = v.pairVal ? v.pairVal->truthy() : true;
                else if (v.s == "in") wantIn = v.pairVal ? v.pairVal->truthy() : true;
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
            (*p.hash)["out-str"] = Value::str("");
            (*p.hash)["err-str"] = Value::str("");
            (*p.hash)["exitcode"] = Value::integer(0);
            return p;
        }
        std::string out, err; int code; bool timedout;
        spawnCapture(argv, 0, out, code, timedout, &I, wantErr ? &err : nullptr);
        if (!wantOut) std::cout << out; // not capturing: echo child stdout (approximates inherit)
        (*p.hash)["exitcode"] = Value::integer(code);
        (*p.hash)["out-str"] = Value::str(out);
        (*p.hash)["err-str"] = Value::str(err);
        return p;
    };
    // shell(CMD, :out, :err) — run CMD through the system shell (`/bin/sh -c CMD`),
    // so redirections/pipes in CMD work. Returns a Proc; +$proc is the exit status.
    B["shell"] = [](Interpreter& I, ValueList& a) -> Value {
        std::string cmd; bool wantOut = false, wantErr = false;
        for (auto& v : flattenArgs(a)) {
            if (v.t == VT::Pair) {
                if (v.s == "out") wantOut = v.pairVal ? v.pairVal->truthy() : true;
                else if (v.s == "err") wantErr = v.pairVal ? v.pairVal->truthy() : true;
            }
            else if (cmd.empty()) cmd = v.toStr();
        }
        std::vector<std::string> argv = {"/bin/sh", "-c", cmd};
        I.syncEnvToProcess(); // child inherits any %*ENV changes the program made
        std::string out, err; int code = 0; bool timedout = false;
        spawnCapture(argv, 0, out, code, timedout, &I, wantErr ? &err : nullptr);
        if (!wantOut) std::cout << out;
        Value p = Value::makeHash(); p.hashKind = "Proc";
        Value av = Value::array(); av.isList = true; av.arr->push_back(Value::str(cmd));
        (*p.hash)["argv"] = av; // .command — shell reports the command string
        (*p.hash)["exitcode"] = Value::integer(code);
        (*p.hash)["out-str"] = Value::str(out);
        (*p.hash)["err-str"] = Value::str(err);
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
    B["dir"] = [](Interpreter&, ValueList& a) -> Value {
        std::string path = a.empty() ? "." : a[0].toStr();
        Value out = Value::array();
        if (DIR* d = opendir(path.c_str())) {
            while (struct dirent* e = readdir(d)) {
                std::string n = e->d_name;
                if (n == "." || n == "..") continue;
                out.arr->push_back(Value::str(path + "/" + n));
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
        if (a.empty()) return Value::boolean(false);
        std::ofstream out(a[0].toStr());
        if (!out) return Value::boolean(false);
        out << (a.size() > 1 ? a[1].toStr() : "");
        return Value::boolean(true);
    };
    B["slurp"] = [](Interpreter&, ValueList& a) -> Value {
        if (a.empty()) { std::ostringstream ss; ss << std::cin.rdbuf(); return Value::str(ss.str()); } // slurp() = $*IN.slurp
        std::ifstream in(a[0].toStr());
        if (!in) return Value::nil();
        std::ostringstream ss; ss << in.rdbuf();
        return Value::str(ss.str());
    };
    // lines() / get() / words() with no arg read from $*ARGFILES: the files named
    // in @*ARGS (awk/perl -n style), or standard input when there are none.
    B["lines"] = [](Interpreter& I, ValueList& a) -> Value {
        Value out = Value::array(); out.isList = true;
        std::string line;
        if (!a.empty() && a[0].t == VT::Str) { // lines("a\nb")
            std::istringstream is(a[0].toStr());
            while (std::getline(is, line)) out.arr->push_back(Value::str(line));
            return out;
        }
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
    B["words"] = [](Interpreter&, ValueList& a) -> Value {
        Value out = Value::array(); out.isList = true;
        std::string all, w;
        if (!a.empty() && a[0].t == VT::Str) all = a[0].toStr();          // words("a b c")
        else { std::ostringstream ss; ss << std::cin.rdbuf(); all = ss.str(); } // words() = $*IN.words
        std::istringstream ws(all);
        while (ws >> w) out.arr->push_back(Value::str(w));
        return out;
    };
    B["open"] = [](Interpreter&, ValueList& a) -> Value { // sub form: open($path, :r/:w/:a)
        std::string path = a.empty() ? "" : a[0].toStr();
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
        if (I.planned_ < 0) { std::cout << "1.." << I.testNum_ << "\n"; I.planned_ = I.testNum_; }
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
    B["abs"] = [](Interpreter& I, ValueList& a) -> Value { Value v = a.empty() ? Value::any() : a[0]; ValueList none; return I.methodCall(v, "abs", none); };
    // Comparison operators usable as subs: `cmp($a,$b)`, `$a leg $b`, etc.
    for (auto op : {"cmp", "leg", "before", "after"})
        B[op] = [op](Interpreter&, ValueList& a) -> Value { return a.size() >= 2 ? applyArith(op, a[0], a[1]) : Value::any(); };
    // List routines that delegate to the method of the same name.
    for (auto nm : {"permutations", "combinations", "unique", "repeated", "squish", "rotor", "flat"})
        B[nm] = [nm](Interpreter& I, ValueList& a) -> Value { Value v = a.size() == 1 ? a[0] : Value::array(a); ValueList none; return I.methodCall(v, nm, none); };
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
    B["sqrt"] = [](Interpreter& I, ValueList& a) -> Value {
        if (!a.empty() && a[0].t == VT::Complex) { auto r = std::sqrt(std::complex<double>(a[0].n, a[0].im)); return Value::complex(r.real(), r.imag()); }
        double x = numArg(I, a);
        // 6.e: sqrt of a negative real is imaginary (0+√|x|i); 6.c/6.d yield NaN
        if (x < 0 && I.langRev_ >= 2) return Value::complex(0, std::sqrt(-x));
        return Value::number(std::sqrt(x)); };
    B["floor"] = [](Interpreter&, ValueList& a) -> Value { return Value::integer((long long)std::floor(a.empty() ? 0 : a[0].toNum())); };
    B["ceiling"] = [](Interpreter&, ValueList& a) -> Value { return Value::integer((long long)std::ceil(a.empty() ? 0 : a[0].toNum())); };
    B["round"] = [](Interpreter&, ValueList& a) -> Value { return Value::integer((long long)std::llround(a.empty() ? 0 : a[0].toNum())); };
    B["exp"] = [](Interpreter&, ValueList& a) -> Value {
        if (a.size() >= 2) return Value::number(std::pow(a[1].toNum(), a[0].toNum())); // exp($x,$base)
        return Value::number(std::exp(a.empty() ? 0 : a[0].toNum())); };
    // Trigonometry (radians). Also available as methods below.
    {
        struct TF { const char* name; double (*fn)(double); };
        static const TF tfs[] = {
            {"sin", std::sin}, {"cos", std::cos}, {"tan", std::tan},
            {"asin", std::asin}, {"acos", std::acos}, {"atan", std::atan},
            {"sinh", std::sinh}, {"cosh", std::cosh}, {"tanh", std::tanh},
            {"asinh", std::asinh}, {"acosh", std::acosh}, {"atanh", std::atanh},
        };
        for (auto& tf : tfs) { auto f = tf.fn; B[tf.name] = [f](Interpreter& I, ValueList& a) -> Value { return Value::number(f(numArg(I, a))); }; }
        B["sec"]    = [](Interpreter&, ValueList& a){ return Value::number(1.0 / std::cos(a.empty()?0:a[0].toNum())); };
        B["cosec"]  = [](Interpreter&, ValueList& a){ return Value::number(1.0 / std::sin(a.empty()?0:a[0].toNum())); };
        B["cotan"]  = [](Interpreter&, ValueList& a){ return Value::number(1.0 / std::tan(a.empty()?0:a[0].toNum())); };
        B["asec"]   = [](Interpreter&, ValueList& a){ return Value::number(std::acos(1.0 / (a.empty()?1:a[0].toNum()))); };
        B["acosec"] = [](Interpreter&, ValueList& a){ return Value::number(std::asin(1.0 / (a.empty()?1:a[0].toNum()))); };
        B["acotan"] = [](Interpreter&, ValueList& a){ return Value::number(std::atan(1.0 / (a.empty()?1:a[0].toNum()))); };
        B["atan2"]  = [](Interpreter&, ValueList& a){ double y=a.empty()?0:a[0].toNum(), x=a.size()>1?a[1].toNum():1.0; return Value::number(std::atan2(y,x)); };
    }
    B["log"] = [](Interpreter&, ValueList& a) -> Value {
        double x = a.empty() ? 0 : a[0].toNum();
        if (a.size() >= 2) return Value::number(std::log(x) / std::log(a[1].toNum())); // log($x, $base)
        return Value::number(std::log(x)); };
    B["log10"] = [](Interpreter&, ValueList& a) -> Value { return Value::number(std::log10(a.empty() ? 0 : a[0].toNum())); };
    B["log2"] = [](Interpreter&, ValueList& a) -> Value { return Value::number(std::log2(a.empty() ? 0 : a[0].toNum())); };
    B["index"] = [](Interpreter&, ValueList& a) -> Value {
        if (a.size() < 2) return Value::nil();
        std::string s = a[0].toStr(), needle = a[1].toStr();
        size_t from = a.size() > 2 ? (size_t)a[2].toInt() : 0;
        size_t p = s.find(needle, from);
        return p == std::string::npos ? Value::nil() : Value::integer((long long)p); };
    B["rindex"] = [](Interpreter&, ValueList& a) -> Value {
        if (a.size() < 2) return Value::nil();
        std::string s = a[0].toStr(), needle = a[1].toStr();
        size_t p = a.size() > 2 ? s.rfind(needle, (size_t)a[2].toInt()) : s.rfind(needle);
        return p == std::string::npos ? Value::nil() : Value::integer((long long)p); };
    B["min"] = [](Interpreter&, ValueList& a) -> Value { ValueList f = a; Value best; bool s = false; for (auto& v : f) { if (!s || valueCmp(v, best) < 0) { best = v; s = true; } } return s ? best : Value::any(); };
    B["max"] = [](Interpreter&, ValueList& a) -> Value { ValueList f = a; Value best; bool s = false; for (auto& v : f) { if (!s || valueCmp(v, best) > 0) { best = v; s = true; } } return s ? best : Value::any(); };
    B["elems"] = [](Interpreter&, ValueList& a) -> Value { return Value::integer(a.empty() ? 0 : (long long)toList(a[0]).size()); };
    B["defined"] = [](Interpreter&, ValueList& a) -> Value { return Value::boolean(!a.empty() && defined(a[0])); };
    // Prefix forms of the metamethods: WHAT($x) === $x.WHAT, etc.
    for (const char* mm : {"WHAT", "WHO", "HOW", "VAR", "WHICH", "WHY"})
        B[mm] = [mm](Interpreter& I, ValueList& a) -> Value { ValueList none; return I.methodCall(a.empty() ? Value::any() : a[0], mm, none); };
    B["chars"] = [](Interpreter&, ValueList& a) -> Value { return Value::integer(a.empty() ? 0 : graphemeCount(a[0].toStr())); };
    auto univalOf = [](uint32_t cp) -> Value {
        long long num, den; if (!uniNumValue(cp, num, den)) return Value::nil();
        return den == 1 ? Value::integer(num) : Value::rat(BigInt(num), BigInt(den));
    };
    auto cpOfArg = [](const Value& v, bool& ok) -> uint32_t {
        ok = true;
        if (v.t == VT::Int || v.t == VT::Bool) return (uint32_t)v.toInt();
        auto cps = utf8cp(v.toStr()); if (cps.empty()) { ok = false; return 0; } return cps[0];
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
    B["uniname"] = [cpOfArg](Interpreter&, ValueList& a) -> Value {
        if (a.empty()) return Value::str("");
        bool ok; uint32_t cp = cpOfArg(a[0], ok);
        std::string nm = ok ? uniNameOf(cp) : "";
        return Value::str(nm.empty() ? "<unassigned>" : nm); // out-of-range / unassigned codepoints
    };
    B["uniprop"] = [cpOfArg](Interpreter&, ValueList& a) -> Value {
        if (a.empty()) return Value::str("");
        bool ok; uint32_t cp = cpOfArg(a[0], ok); if (!ok) return Value::str("");
        std::string prop = a.size() > 1 ? a[1].toStr() : "General_Category";
        if (prop == "Script" || prop == "sc") return Value::str(uniScript(cp));
        return Value::str(uniGeneralCategory(cp));
    };
    B["uc"] = [](Interpreter&, ValueList& a) -> Value { return Value::str(a.empty() ? "" : mapCase(a[0].toStr(), true, 0)); };
    B["lc"] = [](Interpreter&, ValueList& a) -> Value { return Value::str(a.empty() ? "" : mapCase(a[0].toStr(), false, 0)); };
    B["tc"] = [](Interpreter&, ValueList& a) -> Value { return Value::str(a.empty() ? "" : mapCase(a[0].toStr(), false, 1)); };
    B["so"] = [](Interpreter& I, ValueList& a) -> Value { return Value::boolean(!a.empty() && I.boolify(a[0])); };
    B["not"] = [](Interpreter& I, ValueList& a) -> Value { return Value::boolean(a.empty() || !I.boolify(a[0])); };
    // Junction constructors: all()/any()/one()/none() (also written via & | ^).
    for (const char* jt : {"all", "any", "one", "none"})
        B[jt] = [jt](Interpreter&, ValueList& a) -> Value {
            Value j = Value::array(); j.enumName = jt;
            for (auto& v : flattenArgs(a)) j.arr->push_back(v);
            return j;
        };
    B["ord"] = [](Interpreter&, ValueList& a) -> Value { auto c = a.empty() ? std::vector<uint32_t>{} : utf8cp(a[0].toStr()); return c.empty() ? Value::nil() : Value::integer(c[0]); };
    B["chr"] = [](Interpreter&, ValueList& a) -> Value { return Value::str(cpToUtf8((uint32_t)(a.empty() ? 0 : a[0].toInt()))); };
    B["ords"] = [](Interpreter& I, ValueList& a) -> Value { Value v = a.empty() ? Value::any() : a[0]; ValueList none; return I.methodCall(v, "ords", none); };
    B["chrs"] = [](Interpreter&, ValueList& a) -> Value { std::string r; for (auto& x : flattenArgs(a)) r += cpToUtf8((uint32_t)x.toInt()); return Value::str(r); };
    B["sign"] = [](Interpreter& I, ValueList& a) -> Value { Value v = a.empty() ? Value::any() : a[0]; ValueList none; return I.methodCall(v, "sign", none); };
    B["is-prime"] = [](Interpreter& I, ValueList& a) -> Value { Value v = a.empty() ? Value::any() : a[0]; ValueList none; return I.methodCall(v, "is-prime", none); };
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
        std::reverse(items.begin(), items.end()); return Value::array(items);
    };
    B["sort"] = [](Interpreter&, ValueList& a) -> Value {
        ValueList items; for (auto& v : a) { ValueList l = toList(v); items.insert(items.end(), l.begin(), l.end()); }
        std::stable_sort(items.begin(), items.end(), [](const Value& x, const Value& y) { return valueCmp(x, y) < 0; });
        return Value::array(items);
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
        try { I.callCallable(a.back(), {}); }
        catch (...) { I.reactStack_.pop_back(); throw; }
        I.reactStack_.pop_back();
        I.runReactLoop(ctx); // block until every live whenever source is done
        return Value::nil();
    };
    B["whenever"] = [](Interpreter& I, ValueList& a) -> Value {
        // whenever SUPPLY { BLOCK }: tap the supply, running BLOCK for each emitted value
        if (a.size() >= 2 && a.back().t == VT::Code) {
            Value s = a[0], blk = a.back();
            if (s.t == VT::Hash && s.hashKind == "Supply") {
                if (s.hash->count("supplier")) {
                    // live supply: register a tap; count it as a react source so the
                    // enclosing react blocks until this supplier signals done.
                    Value tapRec = Value::makeHash();
                    (*tapRec.hash)["emit"] = blk;
                    if (!I.reactStack_.empty()) {
                        auto ctx = I.reactStack_.back();
                        tapRec.ext = ctx;
                        { std::lock_guard<std::mutex> lk(ctx->m); ctx->liveSources++; }
                    }
                    Value sup = (*s.hash)["supplier"];
                    if (sup.t == VT::Hash && sup.hash->count("taps")) (*sup.hash)["taps"].arr->push_back(tapRec);
                    Value t = Value::makeHash(); t.hashKind = "Tap"; return t;
                }
                ValueList ta{blk}; return I.methodCall(s, "tap", ta); // from-list: eager
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
    B["sleep-timer"] = [](Interpreter& I, ValueList& a) -> Value {
        I.sleepYield(a.empty() ? 0 : a[0].toNum());
        return Value::number(0);
    };
    B["sleep-till"] = [](Interpreter&, ValueList&) -> Value { return Value::boolean(true); };
    B["done"] = [](Interpreter& I, ValueList&) -> Value {
        // `done` inside a react block closes its loop.
        if (!I.reactStack_.empty()) {
            auto ctx = I.reactStack_.back();
            std::lock_guard<std::mutex> lk(ctx->m); ctx->closed = true; ctx->cv.notify_all();
        }
        return Value::boolean(true);
    };
    B["supply"] = [](Interpreter& I, ValueList& a) -> Value {
        // supply { emit ...; done } : run the block now, collecting emitted values
        ValueList vals; I.tctx_.supplyStack.push_back(&vals);
        if (!a.empty() && a.back().t == VT::Code) I.callCallable(a.back(), {});
        I.tctx_.supplyStack.pop_back();
        Value s = Value::makeHash(); s.hashKind = "Supply"; Value v = Value::array(); *v.arr = std::move(vals); (*s.hash)["values"] = v;
        return s;
    };
    B["emit"] = [](Interpreter& I, ValueList& a) -> Value {
        if (!I.tctx_.supplyStack.empty()) I.tctx_.supplyStack.back()->push_back(a.empty() ? Value::any() : a[0]);
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
    B["sprintf"] = [sprintfArgs](Interpreter&, ValueList& a) -> Value {
        if (a.empty()) return Value::str("");
        ValueList rest = sprintfArgs(a);
        return Value::str(doSprintf(a[0].toStr(), rest));
    };
    // Format object (6.e `q:o/…/` / `q:format/…/`): a callable sprintf template that
    // stringifies to its format string. Built by the parser from a flagged literal.
    B["__format__"] = [](Interpreter&, ValueList& a) -> Value {
        Value f = Value::makeHash(); f.hashKind = "Format";
        (*f.hash)["fmt"] = Value::str(a.empty() ? "" : a[0].toStr());
        return f;
    };
    B["printf"] = [sprintfArgs](Interpreter&, ValueList& a) -> Value {
        if (a.empty()) return Value::boolean(true);
        ValueList rest = sprintfArgs(a);
        std::cout << doSprintf(a[0].toStr(), rest); return Value::boolean(true);
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
        Value out = Value::array();
        if (a.size() >= 2 && a[0].t == VT::Code)
            for (auto& v : toList(a[1])) { Value r = I.callCallable(a[0], {v}); if (r.t == VT::Array) for (auto& x : *r.arr) out.arr->push_back(x); else out.arr->push_back(r); }
        return out;
    };
    B["grep"] = [](Interpreter& I, ValueList& a) -> Value {
        Value out = Value::array();
        if (a.size() >= 2 && a[0].t == VT::Code)
            for (auto& v : toList(a[1])) if (I.callCallable(a[0], {v}).truthy()) out.arr->push_back(v);
        return out;
    };
    B["first"] = [](Interpreter& I, ValueList& a) -> Value {
        if (a.size() >= 2 && a[0].t == VT::Code)
            for (auto& v : toList(a[1])) if (I.callCallable(a[0], {v}).truthy()) return v;
        return Value::any();
    };
    B["push"] = [](Interpreter&, ValueList& a) -> Value {
        if (!a.empty() && a[0].t == VT::Array) { for (size_t i = 1; i < a.size(); i++) a[0].arr->push_back(a[i]); return Value::integer((long long)a[0].arr->size()); }
        return Value::any();
    };
    B["pop"] = [](Interpreter&, ValueList& a) -> Value {
        if (!a.empty() && a[0].t == VT::Array && a[0].ext && std::static_pointer_cast<LazySeqState>(a[0].ext)->infinite)
            throw RakuError{Value::typeObj("X::Cannot::Lazy"), "Cannot pop a lazy list"};
        if (!a.empty() && a[0].t == VT::Array && !a[0].arr->empty()) { Value v = a[0].arr->back(); a[0].arr->pop_back(); return v; }
        return Value::any();
    };
    B["shift"] = [](Interpreter& I, ValueList& a) -> Value {
        if (!a.empty() && a[0].t == VT::Array && a[0].ext && std::static_pointer_cast<LazySeqState>(a[0].ext)->infinite) I.materializeLazy(a[0], 1);
        if (!a.empty() && a[0].t == VT::Array && !a[0].arr->empty()) { Value v = a[0].arr->front(); a[0].arr->erase(a[0].arr->begin()); return v; }
        return Value::any();
    };
    B["flat"] = [](Interpreter&, ValueList& a) -> Value {
        Value out = Value::array(); out.isList = true; // flat is a List (flattens in list context)
        for (auto& v : a) { ValueList l = v.flatten(); for (auto& x : l) out.arr->push_back(x); }
        return out;
    };
    B["cache"] = [](Interpreter&, ValueList& a) -> Value { // cache(list) — like .cache, a no-op for our eager values
        if (a.size() == 1) { if (a[0].t == VT::Range) return Value::array(a[0].flatten()); return a[0]; }
        Value out = Value::array(); out.isList = true;
        for (auto& v : a) out.arr->push_back(v);
        return out;
    };
    B["slip"] = [](Interpreter&, ValueList& a) -> Value { // slip(4,5) spreads into the enclosing list
        Value out = Value::array(); out.isList = true;
        for (auto& v : a) { ValueList l = v.flatten(); for (auto& x : l) out.arr->push_back(x); }
        return out;
    };
    B["roundrobin"] = [](Interpreter&, ValueList& a) -> Value {
        // interleave the input lists: round 0 = one from each, round 1 = next, … skipping exhausted lists
        std::vector<ValueList> lists;
        for (auto& v : a) { ValueList l = (v.t == VT::Array || v.t == VT::Range) ? v.flatten() : ValueList{v}; lists.push_back(l); }
        size_t maxLen = 0; for (auto& l : lists) maxLen = std::max(maxLen, l.size());
        Value out = Value::array(); out.isList = true;
        for (size_t i = 0; i < maxLen; i++) {
            Value round = Value::array(); round.isList = true;
            for (auto& l : lists) if (i < l.size()) round.arr->push_back(l[i]);
            out.arr->push_back(round);
        }
        return out;
    };
    // `lazy LIST` / `eager LIST` — rakupp lists are already index-materialised, so
    // both are identity passthroughs (single arg, or a List of the args).
    B["lazy"] = [](Interpreter&, ValueList& a) -> Value {
        if (a.size() == 1) return a[0];
        Value out = Value::array(); out.isList = true; for (auto& v : a) out.arr->push_back(v); return out;
    };
    B["eager"] = [](Interpreter&, ValueList& a) -> Value {
        if (a.size() == 1) return a[0];
        Value out = Value::array(); out.isList = true; for (auto& v : a) out.arr->push_back(v); return out;
    };
    B["hash"] = [](Interpreter&, ValueList& a) -> Value {
        Value h = Value::makeHash();
        for (size_t i = 0; i < a.size(); i++) {
            if (a[i].t == VT::Pair) (*h.hash)[a[i].s] = a[i].pairVal ? *a[i].pairVal : Value::any();
            else if (i + 1 < a.size()) { (*h.hash)[a[i].toStr()] = a[i + 1]; i++; }
        }
        return h;
    };
    B["item"] = [](Interpreter&, ValueList& a) -> Value { return a.empty() ? Value::any() : a[0]; };
    B["VAR"] = [](Interpreter&, ValueList& a) -> Value { return a.empty() ? Value::any() : a[0]; }; // container introspection: value is its own container
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
    B["await"] = [](Interpreter& I, ValueList& a) -> Value {
        // resolve a Promise, running any pending Proc::Async work (with the timeout from an anyof timer)
        std::function<Value(Value&)> resolve = [&](Value& p) -> Value {
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
                if (p.hash->count("promises")) for (auto& q : *(*p.hash)["promises"].arr) {
                    if (q.t == VT::Hash && q.hashKind == "Promise") {
                        std::string k = q.hash->count("kind") ? (*q.hash)["kind"].toStr() : "";
                        if (k == "timer") timeout = (*q.hash)["seconds"].toNum();
                        else if (k == "proc") procP = &q;
                    }
                }
                if (procP) I.runProcPromise(*procP, timeout);
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
    B["set"] = [](Interpreter&, ValueList& a) -> Value { return makeBaggy(flattenArgs(a), "Set"); };
    B["bag"] = [](Interpreter&, ValueList& a) -> Value { return makeBaggy(flattenArgs(a), "Bag"); };
    B["mix"] = [](Interpreter&, ValueList& a) -> Value { return makeBaggy(flattenArgs(a), "Mix"); };
    B["list"] = [](Interpreter&, ValueList& a) -> Value {
        Value out = Value::array(); for (auto& v : a) { for (auto& x : toList(v)) out.arr->push_back(x); } return out;
    };
    B["unshift"] = [](Interpreter&, ValueList& a) -> Value {
        if (!a.empty() && a[0].t == VT::Array) { for (size_t i = a.size(); i > 1; i--) a[0].arr->insert(a[0].arr->begin(), a[i - 1]); return Value::integer((long long)a[0].arr->size()); }
        return Value::any();
    };
}

} // namespace rakupp
