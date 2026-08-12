// EmbedApi.cpp — the host side of rakupp.h: Raku called FROM native code.
//
// The mirror of ExtApi.cpp, and deliberately thin. Almost everything a host
// needs already existed: Interpreter::evalString is what the REPL runs a line
// through, so a session that remembers `my $x` between calls is not a new
// mechanism but the one the REPL has always used. What this file adds is the
// three things a LIBRARY owes its host and a CLI does not:
//
//   * it never exits, signals or flushes uninvited — the RkConfig trio,
//     all default off;
//   * no C++ exception escapes, because unwinding through a host's frames is
//     undefined behaviour rather than a diagnosable bug;
//   * output is capturable, so a host is not forced to share the process's
//     stdout with the Raku code it embeds.
//
// The value surface is not here at all: rk_int_get, rk_at_pos, rk_call and the
// rest come from rakupp_ext.h and work on a host's values because rk_ctx hands
// back the same ExtCtx an extension is given. One vocabulary, two directions.
#include "BuiltinsShared.h" // grammarShimSource (GrammarShim.cpp, generated)
#include "ExtCtx.h"
#include "Interpreter.h"
#include "Parser.h"     // ParseError
#include "Runtime.h"
#include "Value.h"
#include "rakupp.h"

#include <atomic>
#include <csignal>
#include <fstream>
#include <iostream>
#include <sstream>
#include <streambuf>
#include <string>

namespace rakupp {

namespace {

// Everything Raku prints reaches std::cout/std::cerr (Interpreter::ioEmit), so
// capture is a streambuf that forwards to the host instead of a stream that
// writes. Raku.js performed this swap by hand for two years; making it API is
// the point of A2.
class OutBuf : public std::streambuf {
public:
    OutBuf(RkOutputFn fn, void* ud, bool isErr) : fn_(fn), ud_(ud), isErr_(isErr) {}
protected:
    // Both overridden: xsputn is the bulk path (a whole `say` line), overflow
    // the per-character fallback. Forwarding only one of them loses output in
    // whichever way the caller happened not to use.
    std::streamsize xsputn(const char* s, std::streamsize n) override {
        if (fn_ && n > 0) fn_(ud_, s, (size_t)n, isErr_ ? 1 : 0);
        return n;
    }
    int overflow(int ch) override {
        if (ch != traits_type::eof()) {
            char c = (char)ch;
            if (fn_) fn_(ud_, &c, 1, isErr_ ? 1 : 0);
        }
        return ch;
    }
private:
    RkOutputFn fn_;
    void*      ud_;
    bool       isErr_;
};

struct Interp {
    Interpreter interp;
    ExtCtx      ctx;          // the host's value context, cleared per evaluation
    RkConfig    cfg{};
    std::string lastError;
    bool        hasError = false;

    // Output capture, live only while a callback is installed.
    std::unique_ptr<OutBuf> outBuf, errBuf;
    std::streambuf *oldOut = nullptr, *oldErr = nullptr;
    // Fed stdin, so `get`/`lines` see text then EOF instead of blocking on a
    // terminal the host may not have.
    std::istringstream inBuf;
    std::streambuf* oldIn = nullptr;

    ~Interp() {
        if (oldOut) std::cout.rdbuf(oldOut);
        if (oldErr) std::cerr.rdbuf(oldErr);
        if (oldIn)  std::cin.rdbuf(oldIn);
    }
};

// The interpreter wires up process-global state in its constructor (the
// NativeCall callback target, the class registry for free-function smartmatch),
// so two live instances would fight over it and the newest would win. Refusing
// the second is louder than corrupting the first; sequential create/free is
// unaffected, and genuine concurrency is EMBED-PLAN's E5.
std::atomic<bool> g_live{false};

inline Interp* I(RkInterp rk) { return reinterpret_cast<Interp*>(rk); }

void setError(Interp* p, std::string msg) {
    p->lastError = std::move(msg);
    p->hasError = true;
}

// Every entry point that runs Raku funnels through here: one place that knows
// no exception may cross, rather than a discipline repeated per function.
int guarded(Interp* p, const std::function<void()>& body) {
    p->hasError = false;
    p->lastError.clear();
    try {
        body();
        return RK_OK;
    }
    catch (const ParseError& e)  { setError(p, e.what()); }
    catch (const RakuError& e)   { setError(p, e.message); }
    catch (const std::exception& e) { setError(p, std::string("Internal error: ") + e.what()); }
    catch (...)                  { setError(p, "unknown error escaped the interpreter"); }
    return RK_ERROR;
}

// Run `body`, optionally on the large-stack thread the CLI uses. rakuppRun's
// own big-stack helper takes a whole program, so the config flag is honoured
// here at evaluation granularity instead.
void runMaybeBigStack(Interp* p, const std::function<void()>& body) {
    if (!p->cfg.own_stack) { body(); return; }
    // rakuppMainOnBigStack takes a C callback, so the lambda rides across as
    // its void* context.
    struct Ctx { const std::function<void()>* fn; };
    Ctx c{&body};
    rakuppMainOnBigStack([](void* v) -> int { (*((Ctx*)v)->fn)(); return 0; }, &c);
}

} // namespace

extern "C" {

RkInterp rk_new(const RkConfig* cfg) {
    bool expected = false;
    if (!g_live.compare_exchange_strong(expected, true)) return nullptr;
    Interp* p = nullptr;
    try {
        p = new Interp();
    } catch (...) {
        g_live = false;
        return nullptr;
    }
    if (cfg) {
        // Copy only as many bytes as BOTH sides know about, so a host built
        // against an older (smaller) RkConfig is read correctly and the fields
        // it never heard of keep their zero defaults.
        size_t n = cfg->size && cfg->size < sizeof(RkConfig) ? cfg->size : sizeof(RkConfig);
        std::memcpy(&p->cfg, cfg, n);
    }
    p->cfg.size = sizeof(RkConfig);
    p->ctx.interp = &p->interp;
    // SIGPIPE is a PROCESS-wide disposition, so it happens only on request: a
    // Raku TCP server wants it, a host that has its own signal handling does
    // not want us behind its back.
    if (p->cfg.handle_sigpipe) {
#ifndef _WIN32
        std::signal(SIGPIPE, SIG_IGN);
#endif
    }
    return reinterpret_cast<RkInterp>(p);
}

void rk_free(RkInterp rk) {
    if (!rk) return;
    delete I(rk);
    g_live = false;
}

const char* rk_version(void) { return RAKUPP_VERSION; }

RkCtx rk_ctx(RkInterp rk) {
    return rk ? reinterpret_cast<RkCtx>(&I(rk)->ctx) : nullptr;
}

int rk_eval(RkInterp rk, const char* src, RkValue* out) {
    if (!rk) return RK_FATAL;
    Interp* p = I(rk);
    if (out) *out = nullptr;
    // "Valid until the next rk_eval" — the host-shaped spelling of the
    // extension rule that a handle dies with its call. rk_root is the way out.
    p->ctx.clear();
    Value result;
    int rc = guarded(p, [&] {
        runMaybeBigStack(p, [&] { result = p->interp.evalString(src ? src : ""); });
    });
    if (p->cfg.own_stdout) { std::cout.flush(); std::cerr.flush(); }
    if (rc == RK_OK && out) *out = p->ctx.make(std::move(result));
    return rc;
}

int rk_eval_file(RkInterp rk, const char* path, RkValue* out) {
    if (!rk) return RK_FATAL;
    Interp* p = I(rk);
    if (out) *out = nullptr;
    std::ifstream in(path ? path : "", std::ios::binary);
    if (!in) {
        p->ctx.clear();
        setError(p, std::string("cannot read '") + (path ? path : "") + "'");
        return RK_ERROR;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    // The file becomes the interpreter's idea of the running program, so $?FILE
    // and a relative `use lib` resolve as they would from the CLI.
    p->interp.srcFile_ = path ? path : "";
    return rk_eval(rk, ss.str().c_str(), out);
}

int rk_register(RkInterp rk, const char* name, RkHostFn fn, void* userdata) {
    if (!rk || !name || !*name || !fn) return RK_FATAL;
    Interp* p = I(rk);
    // The same wrapping extLoadModule gives an extension's subs — one
    // mechanism, so a registered host function is indistinguishable from an
    // extension sub, and both are ordinary Code values from Raku's side.
    Value code;
    code.t = VT::Code;
    code.code = std::make_shared<Callable>();
    code.code->name = name;
    std::string nm = name;
    code.code->builtin = [fn, userdata, nm](Interpreter& I, ValueList& a) -> Value {
        ExtCtx ctx;
        ctx.args = &a;
        ctx.interp = &I;
        RkValue r = fn(reinterpret_cast<RkCtx>(&ctx), userdata);
        if (ctx.failed)
            throw RakuError{Value::typeObj("X::AdHoc"), ctx.error};
        if (!r && ctx.hasPending)
            throw ctx.pending; // an unhandled rk_call failure resumes with its own type
        return r ? *reinterpret_cast<Value*>(r) : Value::any();
    };
    p->interp.global_->define("&" + nm, code);
    return RK_OK;
}

const char* rk_grammar_shim(void) { return grammarShimSource(); }

int rk_run(RkInterp rk, const char* src, const char* file_name, int* exit_code) {
    if (!rk) return RK_FATAL;
    Interp* p = I(rk);
    if (exit_code) *exit_code = 3;
    p->ctx.clear();
    int rc = 0;
    // rakuppRunOn, not rakuppRun: the program runs in THIS interpreter, so a
    // host does not silently acquire a second one — whose construction would
    // take the process globals over from the session it already holds.
    int status = guarded(p, [&] {
        runMaybeBigStack(p, [&] {
            rc = rakuppRunOn(p->interp, src ? src : "", {},
                             file_name && *file_name ? file_name : "-e",
                             /*exePath*/ "", /*libPaths*/ {});
        });
    });
    if (p->cfg.own_stdout) { std::cout.flush(); std::cerr.flush(); }
    if (exit_code) *exit_code = rc;
    return status;
}

const char* rk_last_error(RkInterp rk) {
    if (!rk) return nullptr;
    Interp* p = I(rk);
    return p->hasError ? p->lastError.c_str() : nullptr;
}

void rk_set_output(RkInterp rk, RkOutputFn fn, void* userdata) {
    if (!rk) return;
    Interp* p = I(rk);
    if (p->oldOut) { std::cout.rdbuf(p->oldOut); p->oldOut = nullptr; }
    if (p->oldErr) { std::cerr.rdbuf(p->oldErr); p->oldErr = nullptr; }
    p->outBuf.reset();
    p->errBuf.reset();
    if (!fn) return;
    p->outBuf.reset(new OutBuf(fn, userdata, false));
    p->errBuf.reset(new OutBuf(fn, userdata, true));
    p->oldOut = std::cout.rdbuf(p->outBuf.get());
    p->oldErr = std::cerr.rdbuf(p->errBuf.get());
}

void rk_set_input(RkInterp rk, const char* text, size_t len) {
    if (!rk) return;
    Interp* p = I(rk);
    if (!text) {
        if (p->oldIn) { std::cin.rdbuf(p->oldIn); p->oldIn = nullptr; }
        return;
    }
    // rdbuf() also clears cin's eof/fail bits, so successive evaluations do not
    // inherit a sticky EOF from the last one.
    p->inBuf.clear();
    p->inBuf.str(std::string(text, len));
    std::streambuf* prev = std::cin.rdbuf(p->inBuf.rdbuf());
    if (!p->oldIn) p->oldIn = prev;
}

} // extern "C"

} // namespace rakupp
