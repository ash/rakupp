#include "Interpreter.h"
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
#include <fcntl.h>
#include <poll.h>
#include <csignal>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace rakupp {

// Spawn a child process, capture its stdout, with an optional wall-clock timeout.
static void spawnCapture(const std::vector<std::string>& argv, double timeoutSec,
                         std::string& out, int& exitCode, bool& timedout) {
    out.clear(); exitCode = -1; timedout = false;
    if (argv.empty()) return;
    int pipefd[2];
    if (pipe(pipefd) != 0) return;
    pid_t pid = fork();
    if (pid < 0) { close(pipefd[0]); close(pipefd[1]); return; }
    if (pid == 0) { // child
        dup2(pipefd[1], STDOUT_FILENO);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) dup2(devnull, STDERR_FILENO);
        close(pipefd[0]); close(pipefd[1]);
        std::vector<char*> cargv;
        for (auto& s : argv) cargv.push_back(const_cast<char*>(s.c_str()));
        cargv.push_back(nullptr);
        execvp(cargv[0], cargv.data());
        _exit(127);
    }
    close(pipefd[1]);
    int fd = pipefd[0];
    fcntl(fd, F_SETFL, O_NONBLOCK);
    auto start = std::chrono::steady_clock::now();
    char buf[8192];
    bool done = false;
    while (!done) {
        struct pollfd pfd{fd, POLLIN, 0};
        poll(&pfd, 1, 50);
        ssize_t n;
        while ((n = read(fd, buf, sizeof buf)) > 0) out.append(buf, (size_t)n);
        int status;
        pid_t w = waitpid(pid, &status, WNOHANG);
        if (w == pid) {
            while ((n = read(fd, buf, sizeof buf)) > 0) out.append(buf, (size_t)n);
            exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            done = true;
        }
        double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        if (!done && timeoutSec > 0 && elapsed > timeoutSec) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            timedout = true;
            done = true;
        }
    }
    close(fd);
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
    spawnCapture(argv, timeoutSec, out, code, timedout);
    auto taps = proc.hash->find("taps");
    if (taps != proc.hash->end() && taps->second.arr)
        for (auto& cb : *taps->second.arr) { ValueList ca{Value::str(out)}; callCallable(cb, ca); }
    (*proc.hash)["exitcode"] = Value::integer(code);
    (*proc.hash)["timedout"] = Value::boolean(timedout);
    (*promise.hash)["status"] = Value::str(timedout ? "Broken" : "Kept");
}

static bool defined(const Value& v) { return v.t != VT::Nil && v.t != VT::Any && v.t != VT::Type; }

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
static std::string mapCase(const std::string& s, bool upper, int tcMode) {
    // tcMode: 0=none, 1=titlecase first only, 2=titlecase first + lowercase rest
    auto cps = utf8cp(s);
    std::string r;
    for (size_t i = 0; i < cps.size(); i++) {
        uint32_t c = cps[i];
        if (tcMode) c = (i == 0) ? toUpperCp(c) : (tcMode == 2 ? toLowerCp(c) : c);
        else c = upper ? toUpperCp(c) : toLowerCp(c);
        r += cpToUtf8(c);
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
static std::string doSprintf(const std::string& fmt, const ValueList& args) {
    std::string out;
    size_t ai = 0;
    for (size_t i = 0; i < fmt.size(); i++) {
        if (fmt[i] != '%') { out += fmt[i]; continue; }
        size_t j = i + 1;
        std::string spec = "%";
        while (j < fmt.size() && (std::strchr("-+ 0#", fmt[j]) || std::isdigit((unsigned char)fmt[j]) || fmt[j] == '.')) {
            spec += fmt[j++];
        }
        if (j >= fmt.size()) { out += spec; break; }
        char conv = fmt[j];
        spec += conv;
        char buf[256];
        Value a = ai < args.size() ? args[ai] : Value::any();
        switch (conv) {
            case '%': out += '%'; break;
            case 'd': case 'i': { std::string s2 = spec; s2.insert(s2.size()-1, "lld"); s2.pop_back();
                                  snprintf(buf, sizeof(buf), s2.c_str(), a.toInt()); out += buf; ai++; break; }
            case 'x': case 'X': case 'o': case 'b': {
                if (conv == 'b') { // manual binary
                    long long n = a.toInt(); std::string bin; bool neg = n < 0; unsigned long long u = neg ? -n : n;
                    if (!u) bin = "0"; while (u) { bin = char('0' + (u & 1)) + bin; u >>= 1; }
                    if (neg) bin = "-" + bin; out += bin;
                } else { std::string s2 = spec; s2.insert(s2.size()-1, "ll");
                         snprintf(buf, sizeof(buf), s2.c_str(), a.toInt()); out += buf; }
                ai++; break;
            }
            case 'c': { out += (char)a.toInt(); ai++; break; }
            case 'f': case 'F': case 'g': case 'G': case 'e': case 'E':
                snprintf(buf, sizeof(buf), spec.c_str(), a.toNum()); out += buf; ai++; break;
            case 's': { std::string sv = a.toStr(); std::string s2 = spec;
                        // use %s with std::string
                        snprintf(buf, sizeof(buf), s2.c_str(), sv.c_str()); out += buf; ai++; break; }
            default: out += spec; break;
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

// ---------------- method dispatch ----------------
Value Interpreter::methodCall(Value inv, const std::string& m, ValueList args, const std::vector<ExprPtr>* rwArgs) {
    auto a0 = [&]() -> Value { return args.empty() ? Value::any() : args[0]; };
    if (std::getenv("RAKUPP_TRACE")) std::cerr << "[M] ." << m << " on type=" << (int)inv.t << (inv.t==VT::Object && inv.obj && inv.obj->cls ? " ("+inv.obj->cls->name+")" : "") << "\n";

    // metamodel call .^method — .^name/.^WHAT answer the type; others dispatch by bare name
    if (!m.empty() && m[0] == '^') {
        std::string mm = m.substr(1);
        if (mm == "name") return Value::str(inv.typeName());
        if (mm == "WHAT") return Value::typeObj(inv.typeName());
        return methodCall(inv, mm, args, rwArgs);
    }

    // Lock / Semaphore: no-op concurrency primitives (rakupp is single-threaded).
    if (inv.t == VT::Type && (inv.s == "Lock" || inv.s == "Semaphore")) {
        if (m == "new") { Value v = Value::makeHash(); v.hashKind = "Lock"; return v; }
    }
    // IO::String / Text::IO::String: an in-memory read handle over a string.
    // $*RAKU / $?RAKU and their .compiler — the runtime/implementation introspection object
    if (inv.t == VT::Hash && (inv.hashKind == "Raku" || inv.hashKind == "Compiler")) {
        bool isComp = inv.hashKind == "Compiler";
        std::string nm = isComp ? "Raku++" : "Raku";
        if (m == "compiler") { Value c = Value::makeHash(); c.hashKind = "Compiler"; return c; }
        if (m == "backend") return Value::str("cpp"); // rakupp's engine is a C++ tree-walking interpreter, not MoarVM
        if (m == "name") return Value::str(nm);
        if (m == "version" || m == "lang-version") { Value v = Value::str("6.d"); v.hashKind = "Version"; return v; }
        if (m == "auth" || m == "authority") return Value::str("The Raku Community");
        if (m == "desc") return Value::str("Raku++ — a C++ Raku interpreter");
        if (m == "signature") { Value b = Value::str("Raku++"); b.hashKind = "Blob"; return b; } // non-empty Blob
        if (m == "id" || m == "release") return Value::str("2026.07");
        if (m == "codename") return Value::str("Raku++");
        if (m == "gist" || m == "Str" || m == "raku" || m == "perl") return Value::str(nm + " (6.d)");
    }
    if (inv.t == VT::Type && inv.s == "Promise") {
        Value p = Value::makeHash(); p.hashKind = "Promise";
        if (m == "in" || m == "at") { (*p.hash)["kind"] = Value::str("timer"); (*p.hash)["seconds"] = args.empty() ? Value::number(0) : args[0]; (*p.hash)["status"] = Value::str("Planned"); return p; }
        if (m == "anyof" || m == "allof") { (*p.hash)["kind"] = Value::str(m); Value ps = Value::array(); for (auto& x : flattenArgs(args)) ps.arr->push_back(x); (*p.hash)["promises"] = ps; (*p.hash)["status"] = Value::str("Planned"); return p; }
        if (m == "kept" || m == "new" || m == "start" || m == "broken") {
            (*p.hash)["result"] = args.empty() ? Value::boolean(true) : args[0];
            (*p.hash)["status"] = Value::str(m == "new" ? "Planned" : m == "broken" ? "Broken" : "Kept");
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
        if (m == "tap" || m == "act") {
            if (!args.empty() && args[0].t == VT::Code && (*inv.hash)["stream"].toStr() == "stdout") {
                Value proc = (*inv.hash)["proc"]; (*proc.hash)["taps"].arr->push_back(args[0]);
            }
            Value t = Value::makeHash(); t.hashKind = "Tap"; return t;
        }
        if (m == "Supply" || m == "list" || m == "lines" || m == "map" || m == "grep") return inv;
    }
    if (inv.t == VT::Hash && inv.hashKind == "Proc") { // standard Proc from run()
        if (m == "exitcode" || m == "signal") return m == "exitcode" ? (*inv.hash)["exitcode"] : Value::integer(0);
        if (m == "so" || m == "Bool") return Value::boolean((*inv.hash)["exitcode"].toInt() == 0);
        if (m == "out" || m == "err") { Value h = Value::makeHash(); h.hashKind = "FileHandle"; (*h.hash)["buffer"] = (*inv.hash)[m == "out" ? "out-str" : "err-str"]; (*h.hash)["mode"] = Value::str("r"); (*h.hash)["captured"] = Value::boolean(true); return h; }
        if (m == "sink" || m == "self") return inv;
        if (m == "pid") return Value::integer(0);
    }
    if (inv.t == VT::Hash && inv.hashKind == "Promise") {
        std::string st = inv.hash->count("status") ? (*inv.hash)["status"].toStr() : "Kept";
        if (m == "result") { auto it = inv.hash->find("result"); if (it != inv.hash->end()) return it->second;
            auto pr = inv.hash->find("proc"); if (pr != inv.hash->end()) return pr->second; return Value::nil(); }
        if (m == "status") return Value::enumVal(st, st == "Kept" ? 0 : st == "Broken" ? 1 : 2);
        if (m == "Bool" || m == "so") return Value::boolean(st == "Kept");
        if (m == "then") { Value p = Value::makeHash(); p.hashKind = "Promise"; (*p.hash)["status"] = Value::str("Kept"); return p; }
        if (m == "cause") return Value::nil();
    }
    if (inv.t == VT::Type && inv.s == "IO::Special") {
        if (m == "Str" || m == "gist" || m == "path") return Value::str("");
    }
    if (inv.t == VT::Type && inv.s == "Stash") {
        if (m == "new") { Value h = Value::makeHash(); h.hashKind = "Stash"; return h; }
    }
    if (inv.t == VT::Type && (inv.s == "Uni" || inv.s == "NFC" || inv.s == "NFD" || inv.s == "NFKC" || inv.s == "NFKD")) {
        if (m == "new") {
            std::vector<uint32_t> in; for (auto& a : args) if (a.t != VT::Pair) in.push_back((uint32_t)a.toInt());
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
        if (m == "list" || m == "List" || m == "values" || m == "Seq") { Value out = Value::array(); out.isList = true; if (inv.arr) out.arr = inv.arr; return out; }
        if (m == "codes" || m == "elems") return Value::integer(inv.arr ? (long long)inv.arr->size() : 0);
        if (m == "Str" || m == "gist" || m == "Stringy") { std::string s; if (inv.arr) for (auto& x : *inv.arr) s += cpToUtf8((uint32_t)x.toInt()); return Value::str(s); }
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
        if (m == "protect" || m == "protect-or-queue-on-recursion") // run the block, no locking
            return args.empty() ? Value::any() : (args[0].t == VT::Code ? callCallable(args[0], {}) : args[0]);
        if (m == "lock" || m == "unlock" || m == "acquire" || m == "release") return Value::boolean(true);
        if (m == "condition") { Value v = Value::makeHash(); v.hashKind = "Lock"; return v; }
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
        if (m == "now") {
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
        if (m == "truncated-to" || m == "earlier" || m == "later") return inv; // best-effort
    }

    if (inv.t == VT::Type) {
        auto cit = classes_.find(inv.s);
        if (cit != classes_.end()) {
            auto ci = cit->second;
            // grammar entry points
            if ((m == "parse" || m == "subparse" || m == "parsefile") && (ci->isGrammar || ci->findRule("TOP"))) {
                std::string startRule = "TOP";
                Value actions;
                for (auto& arg : args) {
                    if (arg.t == VT::Pair && arg.s == "rule") startRule = arg.pairVal ? arg.pairVal->toStr() : "TOP";
                    if (arg.t == VT::Pair && arg.s == "actions" && arg.pairVal) actions = *arg.pairVal;
                }
                std::string input = args.empty() ? "" : args[0].toStr();
                if (m == "parsefile") { // slurp the file, then parse its contents
                    std::ifstream in(input); std::ostringstream ss; ss << in.rdbuf(); input = ss.str();
                    if (!input.empty() && input.back() == '\n') input.pop_back();
                }
                return grammarParse(ci.get(), input, false, startRule, actions);
            }
            // metamodel (.^find_method / .^add_method / .^methods / .^lookup / .^can)
            if (m == "find_method" || m == "lookup") {
                std::string mn = args.empty() ? "" : args[0].toStr();
                Value* um = ci->findMethod(mn);
                return um ? *um : Value::nil();
            }
            if (m == "add_method") { // .^add_method($name, $code)
                if (args.size() >= 2) ci->methods[args[0].toStr()] = args[1];
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
                Value out = Value::array(); out.isList = true;
                for (auto& kv : ci->methods) out.arr->push_back(kv.second);
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
            } else if (Value* um = ci->findMethod(m)) {
                return invokeMethod(*um, inv, args, rwArgs);
            }
            if (m == "new" || m == "bless") {
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
    // user object: dispatch to class methods / public accessors first
    if (inv.t == VT::Object && inv.obj && inv.obj->cls) {
        auto ci = inv.obj->cls;
        if (Value* um = ci->findMethod(m)) return invokeMethod(*um, inv, args, rwArgs);
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

    // universal
    bool isFH = (inv.t == VT::Hash && inv.hashKind == "FileHandle");
    if (m == "say" && !isFH) { std::cout << inv.gist() << "\n"; return Value::boolean(true); }
    if (m == "print" && !isFH) { std::cout << inv.toStr(); return Value::boolean(true); }
    if (m == "put") { std::cout << inv.toStr() << "\n"; return Value::boolean(true); }
    if (m == "note") { std::cerr << inv.gist() << "\n"; return Value::boolean(true); }
    if (m == "Str") return Value::str(inv.toStr());
    if (m == "Int") return Value::integer(inv.toInt());
    if (m == "Num" || m == "Numeric" || m == "Real") return Value::number(inv.toNum());
    if (m == "Bool" || m == "so") return Value::boolean(inv.truthy());
    if (m == "not") return Value::boolean(!inv.truthy());
    if (m == "defined") return Value::boolean(defined(inv));
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
    if (m == "gist" || m == "raku" || m == "perl") return Value::str(inv.gist());
    if (m == "Slip") return inv; // Slip flattens in list context; map/args already flatten arrays
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
    if (m == "WHAT") return Value::typeObj(inv.typeName());
    if (m == "WHICH") { // object identity: value-based for immutables, pointer-based for objects
        if (inv.t == VT::Object && inv.obj) { char buf[24]; std::snprintf(buf, sizeof buf, "|%p", (void*)inv.obj.get()); return Value::str(inv.typeName() + buf); }
        return Value::str(inv.typeName() + "|" + inv.toStr());
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
        if (m == "Str" || m == "gist" || m == "Stringy" || m == "raku" || m == "perl") return Value::str(inv.toStr());
        if (m == "Num" || m == "Real" || m == "Int") { if (inv.im != 0) throw RakuError{Value::str("Complex"), "Can not convert Complex with nonzero imaginary part"}; return m == "Int" ? Value::integer((long long)inv.n) : Value::number(inv.n); }
        if (m == "narrow") return inv.im == 0 ? Value::number(inv.n) : inv;
    }

    // numeric
    if (m == "abs") {
        if (inv.t == VT::Int && inv.big) return Value::bigint(inv.big->abs());
        if (inv.t == VT::Int) return Value::integer(std::llabs(inv.toInt()));
        if (inv.t == VT::Rat) return Value::rat(inv.ratN->abs(), *inv.ratD);
        return Value::number(std::fabs(inv.toNum()));
    }
    if (m == "sqrt") return Value::number(std::sqrt(inv.toNum()));
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
    if (m == "sign") { double n = inv.toNum(); return Value::integer(n < 0 ? -1 : n > 0 ? 1 : 0); }
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
        if (inv.t == VT::Rat) return inv;
        if (inv.t == VT::Int || inv.t == VT::Bool) return Value::rat(inv.toBig(), BigInt(1));
        return inv; // Num->Rat approximation not implemented
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
        std::ifstream in(inv.toStr());
        if (!in) return Value::nil();
        std::ostringstream ss; ss << in.rdbuf();
        return Value::str(ss.str());
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
        return Value::boolean(true); // e/r/w/x
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
    if (m == "modified" || m == "created" || m == "accessed") {
        struct stat st;
        if (stat(inv.toStr().c_str(), &st) != 0) return Value::integer(0);
        return Value::integer((long long)st.st_mtime);
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
            std::ifstream in((*inv.hash)["path"].toStr()); std::ostringstream ss; ss << in.rdbuf(); return Value::str(ss.str());
        }
        // reading: lazily load the file into lines, track a cursor in "pos"
        if (m == "get" || m == "getline" || m == "lines" || m == "eof") {
            if (inv.hash->find("lines") == inv.hash->end()) {
                Value lines = Value::array();
                std::ifstream in((*inv.hash)["path"].toStr());
                std::string line;
                while (std::getline(in, line)) {
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    lines.arr->push_back(Value::str(line));
                }
                (*inv.hash)["lines"] = lines;
                (*inv.hash)["pos"] = Value::integer(0);
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
        if (m == "uniname") return Value::str(have ? uniNameOf(cp) : "");
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
    if ((m == "match" || m == "subst" || m == "comb" || m == "split" || m == "contains" || m == "subst-mutate")
        && !args.empty() && args[0].t == VT::Regex) {
        std::string subj = inv.toStr();
        const std::string& pat = args[0].s;
        if (m == "match") return regexMatch(subj, pat);
        if (m == "contains") { Regex re(pat); RxMatch mm; return Value::boolean(re.ok() && re.search(subj, 0, mm)); }
        if (m == "subst") {
            bool g = false;
            for (auto& a : args) if (a.t == VT::Pair && (a.s == "g" || a.s == "global") && a.pairVal && a.pairVal->truthy()) g = true;
            // closure replacement: call the block per match with $/ (and $_) bound to the Match
            if (args.size() > 1 && args[1].t == VT::Code) {
                Regex re(pat); std::string out; long pos = 0; RxMatch mm;
                while (re.ok() && pos <= (long)subj.size() && re.search(subj, pos, mm)) {
                    out += subj.substr(pos, mm.from - pos);
                    Value matchV = Value::matchVal(subj.substr(mm.from, mm.to - mm.from), mm.from, mm.to);
                    cur_->define("$/", matchV);
                    Value saved = cur_->vars.count("$_") ? cur_->vars["$_"] : Value::any();
                    cur_->define("$_", matchV);
                    out += callCallable(args[1], {matchV}).toStr();
                    cur_->vars["$_"] = saved;
                    pos = mm.to > mm.from ? mm.to : mm.to + 1;
                    if (!g) break;
                }
                out += subj.substr(std::min((size_t)pos, subj.size()));
                return Value::str(out);
            }
            std::string repl = args.size() > 1 ? args[1].toStr() : "";
            std::string out; bool ch;
            regexSubst(subj, (g ? ":g " : "") + pat, repl, out, ch);
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
        std::string s = inv.toStr(), from = a0().toStr(), to = args.size() > 1 ? args[1].toStr() : "";
        bool g = false;
        for (auto& a : args) if (a.t == VT::Pair && (a.s == "g" || a.s == "global") && a.pairVal && a.pairVal->truthy()) g = true;
        if (from.empty()) return Value::str(s);
        std::string out; size_t pos = 0, f;
        while ((f = s.find(from, pos)) != std::string::npos) {
            out += s.substr(pos, f - pos) + to; pos = f + from.size();
            if (!g) break;
        }
        out += s.substr(pos);
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
    if (m == "comb") { Value out = Value::array(); for (auto cp : utf8cp(inv.toStr())) out.arr->push_back(Value::str(cpToUtf8(cp))); return out; }
    if (m == "fmt" && inv.t != VT::Array && inv.t != VT::Range && inv.t != VT::Hash)
        return Value::str(doSprintf(args.empty() ? "%s" : a0().toStr(), {inv}));

    // Pair
    if (inv.t == VT::Pair) {
        if (m == "key") return Value::str(inv.s);
        if (m == "value") return inv.pairVal ? *inv.pairVal : Value::any();
        if (m == "kv") return Value::array({Value::str(inv.s), inv.pairVal ? *inv.pairVal : Value::any()});
        if (m == "antipair") return Value::pair((inv.pairVal ? inv.pairVal->toStr() : ""), Value::str(inv.s));
    }

    // list / array / range
    if (inv.t == VT::Array || inv.t == VT::Range || inv.t == VT::Hash) {
        ValueList items = toList(inv);
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
        if (m == "min") { if (items.empty()) return Value::any(); Value best = items[0]; for (auto& v : items) if (valueCmp(v, best) < 0) best = v; return best; }
        if (m == "max") { if (items.empty()) return Value::any(); Value best = items[0]; for (auto& v : items) if (valueCmp(v, best) > 0) best = v; return best; }
        if (m == "head") return items.empty() ? Value::any() : items.front();
        if (m == "tail") return items.empty() ? Value::any() : items.back();
        if (m == "first") {
            if (!args.empty() && args[0].t == VT::Code)
                for (auto& v : items) { if (callCallable(args[0], {v}).truthy()) return v; }
            else if (!items.empty()) return items.front();
            return Value::any();
        }
        if (m == "unique") {
            Value out = Value::array(); std::set<std::string> seen;
            for (auto& v : items) if (seen.insert(v.toStr()).second) out.arr->push_back(v);
            out.isList = true;
            return out;
        }
        if (m == "repeated") { // elements seen more than once (2nd+ occurrences)
            Value out = Value::array(); std::set<std::string> seen;
            for (auto& v : items) if (!seen.insert(v.toStr()).second) out.arr->push_back(v);
            out.isList = true;
            return out;
        }
        if (m == "squish") { // collapse adjacent duplicates
            Value out = Value::array();
            for (size_t i = 0; i < items.size(); i++)
                if (i == 0 || items[i].toStr() != items[i-1].toStr()) out.arr->push_back(items[i]);
            out.isList = true;
            return out;
        }
        if (m == "sort") {
            if (!args.empty() && args[0].t == VT::Code) {
                Value blk = args[0];
                size_t arity = blk.code->params && !blk.code->params->empty()
                    ? blk.code->params->size() : blk.code->placeholders.size();
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
                for (auto& v : items) {
                    Value r = callCallable(args[0], {v});
                    if (r.t == VT::Array) for (auto& x : *r.arr) out.arr->push_back(x);
                    else if (r.t == VT::Range) for (auto& x : r.flatten()) out.arr->push_back(x);
                    else out.arr->push_back(r);
                }
            }
            out.isList = true;
            return out;
        }
        if (m == "grep") {
            Value out = Value::array();
            if (!args.empty() && args[0].t == VT::Code)
                for (auto& v : items) if (callCallable(args[0], {v}).truthy()) out.arr->push_back(v);
            out.isList = true;
            return out;
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
            if (m == "append") { for (auto& a : flattenArgs(args)) inv.arr->push_back(a); return Value::integer((long long)inv.arr->size()); }
            if (m == "unshift") { inv.arr->insert(inv.arr->begin(), args.begin(), args.end()); return Value::integer((long long)inv.arr->size()); }
            if (m == "prepend") { auto f = flattenArgs(args); inv.arr->insert(inv.arr->begin(), f.begin(), f.end()); return Value::integer((long long)inv.arr->size()); }
            if (m == "pop") { if (inv.arr->empty()) return Value::typeObj("Failure"); Value v = inv.arr->back(); inv.arr->pop_back(); return v; }
            if (m == "shift") { if (inv.arr->empty()) return Value::typeObj("Failure"); Value v = inv.arr->front(); inv.arr->erase(inv.arr->begin()); return v; }
        }
        if (inv.t == VT::Hash && inv.hash) {
            if (m == "exists") return Value::boolean(inv.hash->count(a0().toStr()) > 0);
        }
    }

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

void Interpreter::registerBuiltins() {
    auto& B = builtins_;

    B["say"] = [](Interpreter&, ValueList& a) -> Value {
        ValueList f = a;
        std::string out;
        for (auto& v : f) out += v.gist();
        std::cout << out << "\n"; return Value::boolean(true);
    };
    B["print"] = [](Interpreter&, ValueList& a) -> Value {
        for (auto& v : a) std::cout << v.toStr(); return Value::boolean(true);
    };
    B["put"] = [](Interpreter&, ValueList& a) -> Value {
        std::string out; for (auto& v : a) out += v.toStr(); std::cout << out << "\n"; return Value::boolean(true);
    };
    B["note"] = [](Interpreter&, ValueList& a) -> Value {
        for (auto& v : a) std::cerr << v.gist(); std::cerr << "\n"; return Value::boolean(true);
    };
    B["warn"] = B["note"];
    B["die"] = [](Interpreter& I, ValueList& a) -> Value {
        Value payload = a.empty() ? Value::str("Died") : a[0];
        std::string msg = a.empty() ? "Died" : a[0].toStr();
        // exception objects: prefer a readable .message / .Str accessor
        if (!a.empty() && a[0].t == VT::Object && a[0].obj) {
            for (const char* acc : {"message", "Str"}) {
                try { ValueList none; Value m = I.methodCall(a[0], acc, none);
                      if (m.t == VT::Str && !m.s.empty()) { msg = m.s; break; } } catch (...) {}
            }
        }
        throw RakuError{payload, msg};
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
    B["is"] = [](Interpreter& I, ValueList& a) -> Value {
        Value got = a.size() > 0 ? a[0] : Value::any();
        Value exp = a.size() > 1 ? a[1] : Value::any();
        bool c;
        if (got.isNumeric() && exp.isNumeric()) {
            double g = got.toNum(), e = exp.toNum();
            c = (std::isnan(g) && std::isnan(e)) || g == e; // is NaN, NaN passes (matches Rakudo string semantics)
        } else c = (got.toStr() == exp.toStr());
        std::string dir = testDirective(a);
        I.emitTest(c, testDesc(a, 2), dir);
        if (!c && dir.empty()) { std::cout << "# expected: '" << exp.toStr() << "'\n# got:      '" << got.toStr() << "'\n"; }
        return Value::boolean(c);
    };
    B["isnt"] = [](Interpreter& I, ValueList& a) -> Value {
        Value got = a.size() > 0 ? a[0] : Value::any();
        Value exp = a.size() > 1 ? a[1] : Value::any();
        bool c = !((got.isNumeric() && exp.isNumeric()) ? (got.toNum() == exp.toNum()) : (got.toStr() == exp.toStr()));
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
        I.emitTest(c, testDesc(a, 2), dir);
        if (!c && dir.empty()) std::cout << "# got: '" << got << "'\n";
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
        }
        I.emitTest(c, a.size() > 3 ? a[3].toStr() : "");
        return Value::boolean(c);
    };
    B["pass"] = [](Interpreter& I, ValueList& a) -> Value { I.emitTest(true, a.empty() ? "" : a[0].toStr()); return Value::boolean(true); };
    B["flunk"] = [](Interpreter& I, ValueList& a) -> Value { I.emitTest(false, a.empty() ? "" : a[0].toStr()); return Value::boolean(false); };
    B["diag"] = [](Interpreter&, ValueList& a) -> Value { std::cout << "# " << (a.empty() ? "" : a[0].toStr()) << "\n"; return Value::boolean(true); };
    B["skip"] = [](Interpreter& I, ValueList& a) -> Value {
        long n = (a.size() > 1) ? a[1].toInt() : 1;
        std::string reason = a.empty() ? "" : a[0].toStr();
        for (long k = 0; k < n; k++) I.emitTest(true, "", "skip " + reason);
        return Value::boolean(true);
    };
    B["dies-ok"] = [](Interpreter& I, ValueList& a) -> Value {
        bool died = false;
        if (!a.empty() && a[0].t == VT::Code) { try { I.callCallable(a[0], {}); } catch (RakuError&) { died = true; } }
        I.emitTest(died, a.size() > 1 ? a[1].toStr() : "");
        return Value::boolean(died);
    };
    B["lives-ok"] = [](Interpreter& I, ValueList& a) -> Value {
        bool lived = true;
        if (!a.empty() && a[0].t == VT::Code) { try { I.callCallable(a[0], {}); } catch (RakuError&) { lived = false; } }
        I.emitTest(lived, a.size() > 1 ? a[1].toStr() : "");
        return Value::boolean(lived);
    };
    B["isa-ok"] = [](Interpreter& I, ValueList& a) -> Value {
        std::string want = a.size() > 1 ? (a[1].t == VT::Type ? a[1].s : a[1].toStr()) : "";
        std::string got = a.empty() ? "Any" : a[0].typeName();
        static const std::map<std::string, std::set<std::string>> isa = {
            {"Int", {"Int", "Cool", "Numeric", "Real", "Any", "Mu"}},
            {"Num", {"Num", "Cool", "Numeric", "Real", "Any", "Mu"}},
            {"Str", {"Str", "Cool", "Stringy", "Any", "Mu"}},
            {"Bool", {"Bool", "Any", "Mu"}},
            {"Array", {"Array", "List", "Any", "Mu", "Positional"}},
            {"Seq", {"Seq", "List", "Any", "Mu", "Positional", "Iterable"}},
            {"IO::Path", {"IO::Path", "IO", "Cool", "Any", "Mu"}},
            {"Version", {"Version", "Any", "Mu"}},
            {"Blob", {"Blob", "Buf", "Positional", "Any", "Mu"}},
            {"Compiler", {"Compiler", "Any", "Mu"}},
            {"Hash", {"Hash", "Map", "Any", "Mu", "Associative"}},
        };
        bool c = (got == want);
        auto it = isa.find(got);
        if (!c && it != isa.end()) c = it->second.count(want) > 0;
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
    B["run"] = [](Interpreter&, ValueList& a) -> Value {
        std::vector<std::string> argv; bool wantOut = false;
        for (auto& v : flattenArgs(a)) {
            if (v.t == VT::Pair) { if (v.s == "out") wantOut = v.pairVal ? v.pairVal->truthy() : true; }
            else argv.push_back(v.toStr());
        }
        std::string out; int code; bool timedout;
        spawnCapture(argv, 0, out, code, timedout);
        if (!wantOut) std::cout << out; // not capturing: echo child stdout (approximates inherit)
        Value p = Value::makeHash(); p.hashKind = "Proc"; // standard Proc object
        (*p.hash)["exitcode"] = Value::integer(code);
        (*p.hash)["out-str"] = Value::str(out);
        (*p.hash)["err-str"] = Value::str("");
        return p;
    };
    B["make"] = [](Interpreter& I, ValueList& a) -> Value {
        Value v = a.empty() ? Value::any() : (a.size() == 1 ? a[0] : Value::array(a));
        if (!I.makeTargets_.empty()) I.makeTargets_.back()->pairVal = std::make_shared<Value>(v);
        return v;
    };
    B["take"] = [](Interpreter& I, ValueList& a) -> Value {
        Value v = a.size() == 1 ? a[0] : Value::array(a);
        if (!I.gatherStack_.empty()) {
            for (auto& x : a) I.gatherStack_.back()->push_back(x);
        }
        return v;
    };
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
    B["spurt"] = [](Interpreter&, ValueList& a) -> Value {
        if (a.empty()) return Value::boolean(false);
        std::ofstream out(a[0].toStr());
        if (!out) return Value::boolean(false);
        out << (a.size() > 1 ? a[1].toStr() : "");
        return Value::boolean(true);
    };
    B["slurp"] = [](Interpreter&, ValueList& a) -> Value {
        if (a.empty()) return Value::nil();
        std::ifstream in(a[0].toStr());
        if (!in) return Value::nil();
        std::ostringstream ss; ss << in.rdbuf();
        return Value::str(ss.str());
    };
    B["open"] = [](Interpreter&, ValueList& a) -> Value { // sub form: open($path, :r/:w/:a)
        Value h = Value::makeHash(); h.hashKind = "FileHandle";
        (*h.hash)["path"] = Value::str(a.empty() ? "" : a[0].toStr());
        std::string mode = "r";
        for (auto& x : a) if (x.t == VT::Pair) { if (x.s == "w") mode = "w"; else if (x.s == "a") mode = "a"; else if (x.s == "r") mode = "r"; }
        (*h.hash)["mode"] = Value::str(mode);
        (*h.hash)["buffer"] = Value::str("");
        return h;
    };
    B["unlink"] = [](Interpreter&, ValueList& a) -> Value {
        for (auto& f : a) ::unlink(f.toStr().c_str());
        return Value::boolean(true);
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
        bool savedFailed = I.subtestFailed_;
        I.subtestDepth_++;
        I.subtestFailed_ = false;
        if (code.t == VT::Code) { try { I.callCallable(code, {}); } catch (RakuError&) { I.subtestFailed_ = true; } }
        bool ok = !I.subtestFailed_;
        I.subtestDepth_--;
        I.subtestFailed_ = savedFailed;
        I.emitTest(ok, desc);
        return Value::boolean(ok);
    };
    B["done-testing"] = [](Interpreter& I, ValueList&) -> Value {
        if (I.planned_ < 0) { std::cout << "1.." << I.testNum_ << "\n"; I.planned_ = I.testNum_; }
        return Value::boolean(true);
    };

    // --- utility functions ---
    B["abs"] = [](Interpreter& I, ValueList& a) -> Value { Value v = a.empty() ? Value::any() : a[0]; ValueList none; return I.methodCall(v, "abs", none); };
    B["sqrt"] = [](Interpreter&, ValueList& a) -> Value {
        if (!a.empty() && a[0].t == VT::Complex) { auto r = std::sqrt(std::complex<double>(a[0].n, a[0].im)); return Value::complex(r.real(), r.imag()); }
        return Value::number(std::sqrt(a.empty() ? 0 : a[0].toNum())); };
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
        for (auto& tf : tfs) { auto f = tf.fn; B[tf.name] = [f](Interpreter&, ValueList& a) -> Value { return Value::number(f(a.empty() ? 0 : a[0].toNum())); }; }
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
        bool ok; uint32_t cp = cpOfArg(a[0], ok); return Value::str(ok ? uniNameOf(cp) : "");
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
    B["so"] = [](Interpreter&, ValueList& a) -> Value { return Value::boolean(!a.empty() && a[0].truthy()); };
    B["not"] = [](Interpreter&, ValueList& a) -> Value { return Value::boolean(a.empty() || !a[0].truthy()); };
    B["ord"] = [](Interpreter&, ValueList& a) -> Value { auto c = a.empty() ? std::vector<uint32_t>{} : utf8cp(a[0].toStr()); return c.empty() ? Value::nil() : Value::integer(c[0]); };
    B["chr"] = [](Interpreter&, ValueList& a) -> Value { return Value::str(cpToUtf8((uint32_t)(a.empty() ? 0 : a[0].toInt()))); };
    B["join"] = [](Interpreter&, ValueList& a) -> Value {
        if (a.empty()) return Value::str("");
        std::string sep = a[0].toStr();
        ValueList items;
        for (size_t i = 1; i < a.size(); i++) { ValueList l = toList(a[i]); items.insert(items.end(), l.begin(), l.end()); }
        return Value::str(joinValues(items, sep));
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
    B["sprintf"] = [](Interpreter&, ValueList& a) -> Value {
        if (a.empty()) return Value::str("");
        ValueList rest(a.begin() + 1, a.end());
        return Value::str(doSprintf(a[0].toStr(), rest));
    };
    B["printf"] = [](Interpreter&, ValueList& a) -> Value {
        if (a.empty()) return Value::boolean(true);
        ValueList rest(a.begin() + 1, a.end());
        std::cout << doSprintf(a[0].toStr(), rest); return Value::boolean(true);
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
        if (!a.empty() && a[0].t == VT::Array && !a[0].arr->empty()) { Value v = a[0].arr->back(); a[0].arr->pop_back(); return v; }
        return Value::any();
    };
    B["shift"] = [](Interpreter&, ValueList& a) -> Value {
        if (!a.empty() && a[0].t == VT::Array && !a[0].arr->empty()) { Value v = a[0].arr->front(); a[0].arr->erase(a[0].arr->begin()); return v; }
        return Value::any();
    };
    B["flat"] = [](Interpreter&, ValueList& a) -> Value {
        Value out = Value::array();
        for (auto& v : a) { ValueList l = v.flatten(); for (auto& x : l) out.arr->push_back(x); }
        return out;
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
    B["quietly"] = [](Interpreter& I, ValueList& a) -> Value { // suppress warnings; run block/return arg
        if (!a.empty() && a[0].t == VT::Code) return I.callCallable(a[0], {});
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
    // Concurrency stubs (rakupp is single-threaded): start runs the block now, wrapped as a Kept Promise.
    B["start"] = [](Interpreter& I, ValueList& a) -> Value {
        Value res;
        if (!a.empty() && a[0].t == VT::Code) res = I.callCallable(a[0], {});
        else if (!a.empty()) res = a[0];
        Value p = Value::makeHash(); p.hashKind = "Promise";
        (*p.hash)["result"] = res; (*p.hash)["status"] = Value::str("Kept");
        return p;
    };
    B["await"] = [](Interpreter& I, ValueList& a) -> Value {
        // resolve a Promise, running any pending Proc::Async work (with the timeout from an anyof timer)
        std::function<Value(Value&)> resolve = [&](Value& p) -> Value {
            if (p.t != VT::Hash || p.hashKind != "Promise") return p;
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
