// BuiltinsParts.h — what the Builtins*.cpp and MethodCallPart1b/1c.cpp files share
//
// What the parts of the interpreter share: every type, helper and variable
// that one Builtins.cpp-family file defines and another one uses. The parts:
//   Builtins.cpp               helpers (processes, taps, .raku, sprintf, signatures, JSON), methodCall and methodCallInner — the first segment of the method-dispatch chain
//   MethodCallPart1b.cpp       segment 1b of the method-dispatch chain
//   MethodCallPart1c.cpp       segment 1c of the method-dispatch chain
//   BuiltinsSupply.cpp         supplies (taps, delivery, timers, signals, async sockets) and the --exe built-in natives (rtB*)
//   BuiltinsRegister.cpp       registerBuiltins, first two pieces: test functions, EVAL, atomics, gather/take, I/O subs
//   BuiltinsRegister2.cpp      registerBuiltins, pieces three and four: string and list subs, radix, coercions
//   BuiltinsRegister3.cpp      registerBuiltins, the last piece: sleep, signals, NativeCall subs, set operators
//   BuiltinsNqp.cpp            the nqp:: ops, interpreted (evalNqpOp) and compiled (rtNqpOp)
#pragma once
#include "CNumeric.h"
#include "AsciiCtype.h"
#include "Interpreter.h"
#if !defined(_WIN32)
#include <dlfcn.h>   // the extension-host check in rakupp-ext-load
#endif
#include "DataCsv.h"
#include "Digest.h"
#include "DataDigest.h"
#include "DataZlib.h"
#include "DataRandom.h"
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
#include "RakuAstClasses.h"
#include "BuildInfo.h"
#include <filesystem>
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
#if defined(_WIN32)
#include <conio.h>   // _getch for prompt(:hidden), where there is no termios
#include <io.h>
#else
#include <termios.h> // prompt(:hidden) clears ECHO for the duration of one line
#include <unistd.h>
#endif
#include <sys/stat.h>
#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/file.h>   // flock (rakupp-repo-lock)
#include <sys/utsname.h>
#include <sys/socket.h>
#include <sys/un.h>     // PF_UNIX sockets (IO::Socket::INET :family(PF_UNIX))
#include <poll.h>       // the async-read loop waits with a timeout so closing a
                        // TAP can stop it without touching the socket
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
bool rakuppFindModuleSource(const std::string& name, const std::vector<std::string>& searchPath,
                            std::string& pathOut, std::string& srcOut, bool sixE);
}
// `no precompilation;` keeps a unit out of the precomp cache, and so does
// loading one that says it (`need`/`use` of a unit found on the same paths)
static inline bool unitPrecompilable(const std::string& name, const std::vector<std::string>& paths) {
    bool precomp = true;
    std::set<std::string> seen;
    std::function<void(const std::string&)> scan = [&](const std::string& mod) {
        if (!precomp || !seen.insert(mod).second || seen.size() > 64) return;
        std::string path, src;
        if (!rakupp::rakuppFindModuleSource(mod, paths, path, src, false)) return;
        if (src.find("no precompilation") != std::string::npos) { precomp = false; return; }
        for (const char* kw : {"need ", "use "}) {
            for (size_t k = src.find(kw); k != std::string::npos; k = src.find(kw, k + 1)) {
                if (k > 0 && src[k - 1] != '\n' && src[k - 1] != ' ' && src[k - 1] != ';') continue;
                size_t b = k + std::strlen(kw), e = b;
                while (e < src.size() && (isalnum((unsigned char)src[e]) || src[e] == ':' || src[e] == '_' || src[e] == '-')) e++;
                if (e > b && isupper((unsigned char)src[b])) scan(src.substr(b, e - b));
            }
        }
    };
    scan(name);
    return precomp;
}

namespace rakupp {
const std::map<std::string, int>& signalNameMapFwd();
Value makeSignalEnumValueFwd(int sig);

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
    // `run(:merge)`: the child's stderr IS its stdout — the same pipe, so the
    // two streams interleave in the order the child wrote them rather than
    // arriving as two strings someone has to concatenate and guess about.
    // Requires captureOut; captureErr must be off, since there is no second
    // stream left to capture.
    bool mergeErr = false;
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
    // WHY it failed, when it did. On Windows that is CreateProcess's own text;
    // on POSIX it is reconstructed from the errno the child sends back through
    // the exec-status pipe, since a failed execvp is otherwise indistinguishable
    // from a program that ran and exited 127.
    std::string spawnErr;
#if defined(_WIN32)
    HANDLE hProcess = nullptr;
    HANDLE outR = nullptr, errR = nullptr; // pipe read ends (nullptr: not captured)
#else
    int outFd = -1, errFd = -1;            // pipe read ends (-1: not captured)
    bool reaped = false;                   // the zombie sweep already waitpid()ed it…
    int rawStatus = 0;                     // …and this is the status it collected
    bool ownPgroup = false;                // setpgid'd, so -pid names its group (see below)
#endif
};

// Children Proc::Async.start has running, keyed by the token stored on the proc
// hash. Mutex-guarded: realizations drain with the GIL parked, so one thread can
// be spawning (or .kill-ing) while another reaps.
extern std::mutex g_spawnedM;
extern std::map<long long, SpawnedChild> g_spawned;

static int signalNumberOf(const Value& v); // Proc::Async.kill's argument; tables are with signal() below
void arrayOpArgs(const std::string& op, const ValueList& a, bool oneArray); // the push/pop/shift/unshift sub forms' argument contract; defined with the nqp helpers

// First half: spawn the child and return at once. The fork happens with the GIL
// held, so forks serialise (safe in a multithreaded process).
//
// `ownPgroup` puts the child in a process group of its own so a timeout can kill
// its grandchildren along with it. It must stay OFF otherwise: a child in a group
// that is not the terminal's foreground group is a BACKGROUND job, and the kernel
// stops it with SIGTTOU the moment it calls tcsetattr, or SIGTTIN the moment it
// reads the terminal. `run 'stty', '-echo'` hung there forever — which is how
// `fez login` came to echo the password and then wedge (issue #72) — and so did
// every interactive child, `less` and `vi` and a `sudo` password prompt included.
SpawnedChild spawnChildStart(const std::vector<std::string>& argv, const std::string& cwd,
                                    const std::vector<std::string>* envKV, const SpawnStdio& io,
                                    bool ownPgroup = false);

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
void spawnCapture(const std::vector<std::string>& argv, double timeoutSec,
                         std::string& out, int& exitCode, bool& timedout,
                         Interpreter* gil = nullptr, std::string* errOut = nullptr,
                         const std::string& cwd = "", long long* pidOut = nullptr,
                         const std::vector<std::string>* envKV = nullptr,
                         bool errInherit = false, int outMode = 1,
                         const ChildChunkSink* sink = nullptr,
                         std::exception_ptr* sinkErr = nullptr,
                         int stdinFd = -1, bool mergeErr = false,
                         std::string* spawnErrOut = nullptr);

// Realize a Proc::Async .start promise: the process has been RUNNING since
// `.start` (spawnChildStart in the method handler); this drains its capture
// pipes, feeds them to the Supply taps, reaps it, and marks the promise Kept
// (finished) or Broken (timed out). The fallback path spawns here, lazily —
// for a promise whose eager spawn never happened (empty argv, fork failure).
// A Proc::Async whose program cannot be found on PATH will never start —
// the reason, in libuv's words, or "" when it looks runnable. (The spawn
// itself happens late, when something awaits or drives the process; its
// `.ready` and a `whenever` on its streams have to know sooner.)
std::string procSpawnMissing(const Value& proc);

// Unicode combining marks (Mn/Mc/Me — the common ranges) — they attach to the preceding grapheme.
// Count grapheme clusters via the full UAX #29 algorithm (emoji/flags/Hangul-aware).
// How many bytes of a growing UTF-8 buffer are safe to hand over as TEXT.
//
// A byte stream does not arrive on character boundaries, let alone grapheme
// ones, so two things are held back: an incomplete trailing UTF-8 sequence,
// and the final grapheme — the next chunk could open with a combining mark.
// The exception is a grapheme ending in a control nothing can extend or join
// (LF, TAB, NUL), which is why a line arrives whole; CR is NOT one of those,
// because CR LF is a single cluster, and neither is a space, because a mark
// can attach to it. Oracle-checked against Rakudo's own stream decoder.
size_t utf8TextPrefixLen(const std::string& b);

// flatten all args (used by say/join/etc.)
static inline ValueList flattenArgs(ValueList& args) {
    ValueList out;
    for (auto& a : args) {
        if (a.t == VT::Array || a.t == VT::Range) {
            ValueList sub = a.flatten();
            out.insert(out.end(), sub.begin(), sub.end());
        } else out.push_back(a);
    }
    return out;
}

Value makeAsyncSocket(int fd); // defined with the supply-wiring block below
// A UDP IO::Socket::Async (bind-udp / udp): the descriptor and the read
// workers polling it, shared by the socket value and every worker. A worker
// never touches the socket hash, only this; and the fd is released under `m`
// once no reader can still be polling it, so close() frees the port at once.
struct UdpSockState {
    std::mutex m; std::condition_variable cv;
    int fd = -1;
    bool closed = false, broadcast = false;
    int readers = 0;
    std::vector<std::thread::id> readerTids;   // a close from inside a tap must not wait on itself
};
static inline std::shared_ptr<UdpSockState> udpState(const Value& s) {
    return std::static_pointer_cast<UdpSockState>(s.ext());
}
static inline int udpOpen(int family, bool broadcast) {
    // no SO_REUSEADDR: on Linux it lets two sockets bind one port, and a
    // second bind-udp of a taken port has to die
    int fd = ::socket(family, SOCK_DGRAM, 0);
    if (fd >= 0 && broadcast) { int one = 1; ::setsockopt(fd, SOL_SOCKET, SO_BROADCAST, (const char*)&one, sizeof one); }
    return fd;
}

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
void jsonSkipWs(const std::string& s, size_t& i, const JsonCfg& cfg);
bool jsonParseValue(const std::string& s, size_t& i, Value& out, JsonCfg cfg);
bool jsonParseValue(const std::string& s, size_t& i, Value& out, JsonCfg cfg);

std::string jsonEncode(const Value& v);

// The wrapped &to-json / &from-json: try native, fall back to the module's sub.
// ---- the two JSON entry points: one body, two policies ---------------------
//
// The WRAPPER hands an uncovered case back to JSON::Fast, so the module's
// behaviour is the module's wherever this replica does not reach. The
// PRIMITIVE — `rakupp-to-json`, `rakupp-from-json`, what Data::Native binds —
// has no module behind it and must decide every case itself, which means
// raising where the wrapper delegates. That is the one real semantic
// difference between them, and it is why they share a body rather than being
// written twice: a case the wrapper punts on is a case the primitive has to
// answer, and the two must not drift on the cases both cover.
//
// `uncovered` is what a case neither can handle does. It returns a Value for
// the wrapper and never returns for the primitive.
using JsonUncovered = std::function<Value(const char* why)>;

Value jsonToJsonBody(Interpreter& I, ValueList& a, const JsonUncovered& uncovered);

Value jsonFromJsonBody(Interpreter& I, ValueList& a, const JsonUncovered& uncovered);

// Declared at namespace scope, not as block-scope `extern`s inside
// methodCallInner: MSVC gives a block-scope declaration GLOBAL linkage, so the
// use went unresolved against the rakupp:: definition.
bool asyncSockAddrFwd(const std::string&, int, sockaddr_storage&, socklen_t&);  // with makeAsyncSocket below
extern const char* const kCatHandleSrc;
// Collect LAST/QUIT/CLOSE phasers from a block's top-level statements.
void scanSupplyPhasers(const Value& blk, ValueList* lastP,
                              ValueList* quitP, ValueList* closeP,
                              std::shared_ptr<Env> phaserEnv = nullptr);
// A callable that runs `fn` with `ctx` re-established as the active supply
// activation — used for whenever bodies and done/quit hooks that fire later
// (possibly from an I/O worker thread holding the GIL).
static inline Value ctxCallable(std::shared_ptr<SupplyTapCtx> ctx,
                         std::function<Value(Interpreter&, ValueList&)> fn) {
    Value v; v.t = VT::Code; v.setCode(std::make_shared<Callable>());
    v.code()->builtin = [ctx, fn](Interpreter& I, ValueList& a) -> Value {
        I.tctx_.tapStack.push_back(ctx);
        struct G { std::vector<std::shared_ptr<SupplyTapCtx>>& s; ~G() { s.pop_back(); } } g{I.tctx_.tapStack};
        return fn(I, a);
    };
    return v;
}

// The cause of a broken Promise (or a failed Channel) as a real Exception: a
// plain string is the PAYLOAD of an X::AdHoc, so `.message` and `.payload` work
// wherever the cause surfaces — a whenever's QUIT phaser, a react's rethrow,
// the `$!` after a `try react` (S-60).
static inline Value causeException(Interpreter& I, const Value& cause, const std::string& msg) {
    if (cause.t == VT::Object) return cause;
    std::string m = cause.t == VT::Str ? cause.s.str() : msg;
    if (m.empty()) m = "Promise broken";
    if (cause.t == VT::Type) return I.exceptionFor(RakuError{cause, m, RakuError::NoCapture{}});
    return I.exceptionFor(RakuError{Value::typeObj("X::AdHoc"), m, RakuError::NoCapture{}});
}
bool asyncSockAddrFwd(const std::string& h, int p, sockaddr_storage& ss, socklen_t& len);

// Build an IO::Socket::Async connection value around a connected fd.
Value makeAsyncSocket(int fd);
const std::map<std::string, int>& signalNameMapFwd();
Value makeSignalEnumValueFwd(int sig);
// A signal value passed to `signal()`: the Signal type-object (`SIGINT`, s=name),
// an Int-backed enum value, or a plain integer.
static inline int signalNumberOf(const Value& v) {
    if (v.t == VT::Type) return signalNumberOfName(v.s);
    if (v.t == VT::Int && !v.enumName.empty()) { int n = signalNumberOfName(v.enumName); if (n > 0) return n; }
    if (v.isNumeric()) return (int)v.toInt();
    return -1;
}

// `$path.IO.watch` rides an interval ticker: this wraps the block a tick
// would run so that it runs only when the file's size or modification time
// has moved since the last look, with an IO::Notification-shaped event.
Value watchFilter(const Value& sup, const Value& blk);
static inline double numArg(Interpreter& I, ValueList& a) {
    return a.empty() ? 0 : numValueOf(I, a[0]);
}
// Trig family: Complex — or an Object whose .Numeric/.Bridge may yield one —
// via the method path; everything else through numArg, like the sub forms.
static inline Value rtBMath1(Interpreter& I, const Value& v, const char* name, double (*f)(double)) {
    if (v.t == VT::Complex || v.t == VT::Object) { ValueList none; return I.methodCall(v, name, none); }
    ValueList one{v};
    return Value::number(f(numArg(I, one)));
}

// The logical working-directory name after entering `p` from `base`: purely
// textual, matching Rakudo's $*CWD — symlinks stay as the program spelled them
// (the real chdir has already validated the target), `.` and `..` collapse.
std::string logicalJoin(const std::string& base, const std::string& p);

// True if a line was read; false at EOF. `echoed` says whether the terminal
// showed the keystrokes, which decides who has to supply the newline.
bool readHiddenLine(std::string& line, bool& echoed);


// The array-op SUB forms' argument contract (S32-array/{push,pop,shift,unshift}.t):
// `pop()` with nothing to pop from is a type error at Rakudo's compile time; the
// one-array forms (pop, shift) take no further positional; and none of them has
// a named parameter, so `push @a, a => 52` has nowhere to put the pair. Each
// used to answer a silent Any or push the Pair. `push([])` stays fine: the
// Array is the argument, and pushing nothing to it is allowed.
void arrayOpArgs(const std::string& op, const ValueList& a, bool oneArray);

} // namespace rakupp
