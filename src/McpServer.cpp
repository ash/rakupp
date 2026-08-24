// McpServer.cpp — `rakupp --mcp`: the interpreter as an MCP server.
//
// MCP (the Model Context Protocol) is how AI agent clients — Claude Code,
// Claude Desktop, Cursor, and the rest — discover and call tools: JSON-RPC
// 2.0, one message per line, over the server's stdio. This file serves the
// engine to those clients as two tools:
//
//   raku       — evaluate source in ONE persistent session. rk_eval keeps
//                state, so the agent gets a REPL, not a one-shot: a sub
//                defined in one call is callable in the next. Named after
//                the language, kebab-free, because agents and people alike
//                say "run some raku".
//   raku-parse — compile a grammar from source text and parse with it, the
//                match tree crossing back as JSON; a failed parse answers
//                with the position and the deepest rule instead. Kebab-case
//                like the language's own identifiers.
//
// Everything engine-side goes through the public C ABI (rakupp.h) — the same
// surface every language binding uses, deliberately: this server is another
// host of that ABI, not a back door into the interpreter. The grammar service
// is rk_grammar_shim(), the exact shim the bindings load, so what an agent
// parses is byte-for-byte what a binding (or plain rakupp) would get.
//
// Two facts of the transport shape everything here:
//   * stdout belongs to the protocol: one JSON object per line, nothing else.
//     The session's own printing is therefore captured with rk_set_output
//     from the moment the interpreter exists, and its stdin is pinned to EOF
//     with rk_set_input so Raku code can never eat a protocol message.
//   * a call must answer or the client hangs. rk_eval cannot be interrupted
//     (an rk_interrupt is future ABI work), so a watchdog thread answers the
//     in-flight request with isError and EXITS; the client restarts the
//     server on its next call, with fresh session state — documented, loud,
//     and better than a wedged agent.
#include "McpServer.h"
#include "rakupp.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#  include <io.h>
#  define RK_MCP_READ ::_read
#else
#  include <unistd.h>
#  define RK_MCP_READ ::read
#endif

namespace rakupp::mcp {
namespace {

// ---------------------------------------------------------------------------
// A small JSON value, parser and writer. The protocol needs exactly this
// much; pulling in a library for it would be the only external dependency in
// the binary, so it stays hand-rolled like everything else here.
// ---------------------------------------------------------------------------

struct Json {
    enum class T { Null, Bool, Int, Num, Str, Arr, Obj };
    T t = T::Null;
    bool b = false;
    long long i = 0;
    double n = 0;
    std::string s;
    std::vector<Json> arr;
    std::vector<std::pair<std::string, Json>> obj; // insertion order kept

    static Json null() { return Json{}; }
    static Json boolean(bool v) { Json j; j.t = T::Bool; j.b = v; return j; }
    static Json integer(long long v) { Json j; j.t = T::Int; j.i = v; return j; }
    static Json number(double v) { Json j; j.t = T::Num; j.n = v; return j; }
    static Json str(std::string v) { Json j; j.t = T::Str; j.s = std::move(v); return j; }
    static Json array() { Json j; j.t = T::Arr; return j; }
    static Json object() { Json j; j.t = T::Obj; return j; }

    Json& set(const std::string& k, Json v) {
        obj.emplace_back(k, std::move(v));
        return *this;
    }
    const Json* find(const std::string& k) const {
        for (auto& kv : obj)
            if (kv.first == k) return &kv.second;
        return nullptr;
    }
    // The common "string field or fallback" read.
    std::string getStr(const std::string& k, const std::string& dflt = "") const {
        const Json* v = find(k);
        return v && v->t == T::Str ? v->s : dflt;
    }
};

// The shortest %g that strtod round-trips — so 3.88 prints as 3.88, not as
// seventeen digits of it.
std::string numToStr(double d) {
    char buf[40];
    for (int prec = 15; prec <= 17; prec++) {
        std::snprintf(buf, sizeof buf, "%.*g", prec, d);
        if (std::strtod(buf, nullptr) == d) break;
    }
    return buf;
}

void dumpStr(const std::string& s, std::string& out) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            default:
                if (c < 0x20) {
                    char u[8];
                    std::snprintf(u, sizeof u, "\\u%04x", c);
                    out += u;
                }
                else { out += (char)c; }
        }
    }
    out += '"';
}

void dump(const Json& j, std::string& out) {
    switch (j.t) {
        case Json::T::Null: out += "null"; break;
        case Json::T::Bool: out += j.b ? "true" : "false"; break;
        case Json::T::Int:  out += std::to_string(j.i); break;
        case Json::T::Num:
            // JSON has no spelling for these; a string is the honest crossing,
            // matching how the bindings document odd values (they stringify).
            if (std::isnan(j.n)) { out += "\"NaN\""; }
            else if (std::isinf(j.n)) { out += j.n > 0 ? "\"Inf\"" : "\"-Inf\""; }
            else { out += numToStr(j.n); }
            break;
        case Json::T::Str: dumpStr(j.s, out); break;
        case Json::T::Arr: {
            out += '[';
            bool first = true;
            for (auto& v : j.arr) {
                if (!first) out += ',';
                first = false;
                dump(v, out);
            }
            out += ']';
            break;
        }
        case Json::T::Obj: {
            out += '{';
            bool first = true;
            for (auto& kv : j.obj) {
                if (!first) out += ',';
                first = false;
                dumpStr(kv.first, out);
                out += ':';
                dump(kv.second, out);
            }
            out += '}';
            break;
        }
    }
}

std::string dumps(const Json& j) {
    std::string out;
    dump(j, out);
    return out;
}

// Recursive descent over one line. Depth-capped: a hostile client must not be
// able to blow the native stack with ten thousand '['.
struct Parser {
    const char* p;
    const char* end;
    int depth = 0;
    bool ok = true;

    explicit Parser(const std::string& s) : p(s.data()), end(s.data() + s.size()) {}

    void ws() { while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++; }
    bool lit(const char* w, size_t n) {
        if ((size_t)(end - p) < n || std::strncmp(p, w, n) != 0) return false;
        p += n;
        return true;
    }
    void utf8(unsigned cp, std::string& s) {
        if (cp < 0x80) { s += (char)cp; }
        else if (cp < 0x800) {
            s += (char)(0xC0 | (cp >> 6));
            s += (char)(0x80 | (cp & 0x3F));
        }
        else if (cp < 0x10000) {
            s += (char)(0xE0 | (cp >> 12));
            s += (char)(0x80 | ((cp >> 6) & 0x3F));
            s += (char)(0x80 | (cp & 0x3F));
        }
        else {
            s += (char)(0xF0 | (cp >> 18));
            s += (char)(0x80 | ((cp >> 12) & 0x3F));
            s += (char)(0x80 | ((cp >> 6) & 0x3F));
            s += (char)(0x80 | (cp & 0x3F));
        }
    }
    bool hex4(unsigned& v) {
        if (end - p < 4) return false;
        v = 0;
        for (int k = 0; k < 4; k++) {
            char c = *p++;
            v <<= 4;
            if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
            else return false;
        }
        return true;
    }
    bool string(std::string& s) {
        if (p >= end || *p != '"') return false;
        p++;
        while (p < end && *p != '"') {
            unsigned char c = (unsigned char)*p;
            if (c == '\\') {
                p++;
                if (p >= end) return false;
                switch (*p++) {
                    case '"':  s += '"';  break;
                    case '\\': s += '\\'; break;
                    case '/':  s += '/';  break;
                    case 'n':  s += '\n'; break;
                    case 't':  s += '\t'; break;
                    case 'r':  s += '\r'; break;
                    case 'b':  s += '\b'; break;
                    case 'f':  s += '\f'; break;
                    case 'u': {
                        unsigned u;
                        if (!hex4(u)) return false;
                        if (u >= 0xD800 && u <= 0xDBFF && end - p >= 6 &&
                            p[0] == '\\' && p[1] == 'u') {
                            p += 2;
                            unsigned lo;
                            if (!hex4(lo) || lo < 0xDC00 || lo > 0xDFFF) return false;
                            u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
                        }
                        utf8(u, s);
                        break;
                    }
                    default: return false;
                }
            }
            else if (c < 0x20) { return false; }
            else {
                s += (char)c;
                p++;
            }
        }
        if (p >= end) return false;
        p++; // closing quote
        return true;
    }
    bool value(Json& j) {
        if (++depth > 128) return false;
        struct Undepth { int& d; ~Undepth() { d--; } } u{depth};
        ws();
        if (p >= end) return false;
        char c = *p;
        if (c == '{') {
            p++;
            j = Json::object();
            ws();
            if (p < end && *p == '}') { p++; return true; }
            for (;;) {
                ws();
                std::string k;
                if (!string(k)) return false;
                ws();
                if (p >= end || *p++ != ':') return false;
                Json v;
                if (!value(v)) return false;
                j.obj.emplace_back(std::move(k), std::move(v));
                ws();
                if (p < end && *p == ',') { p++; continue; }
                if (p < end && *p == '}') { p++; return true; }
                return false;
            }
        }
        if (c == '[') {
            p++;
            j = Json::array();
            ws();
            if (p < end && *p == ']') { p++; return true; }
            for (;;) {
                Json v;
                if (!value(v)) return false;
                j.arr.push_back(std::move(v));
                ws();
                if (p < end && *p == ',') { p++; continue; }
                if (p < end && *p == ']') { p++; return true; }
                return false;
            }
        }
        if (c == '"') { j = Json::str(""); return string(j.s); }
        if (lit("true", 4))  { j = Json::boolean(true);  return true; }
        if (lit("false", 5)) { j = Json::boolean(false); return true; }
        if (lit("null", 4))  { j = Json::null();         return true; }
        // number
        const char* start = p;
        if (p < end && (*p == '-' || *p == '+')) p++;
        bool isNum = false, frac = false;
        while (p < end) {
            char d = *p;
            if (d >= '0' && d <= '9') { isNum = true; p++; }
            else if (d == '.' || d == 'e' || d == 'E' || d == '+' || d == '-') { frac = true; p++; }
            else { break; }
        }
        if (!isNum) return false;
        std::string tok(start, (size_t)(p - start));
        if (!frac) {
            errno = 0;
            char* rest = nullptr;
            long long v = std::strtoll(tok.c_str(), &rest, 10);
            if (errno == 0 && rest && *rest == '\0') { j = Json::integer(v); return true; }
        }
        j = Json::number(std::strtod(tok.c_str(), nullptr));
        return true;
    }
};

bool parse(const std::string& line, Json& out) {
    Parser ps(line);
    if (!ps.value(out)) return false;
    ps.ws();
    return ps.p == ps.end;
}

// ---------------------------------------------------------------------------
// The wire. One JSON object per line; stdout is shared by the reply path and
// the watchdog, so writes take a mutex and flush — a buffered half-line at
// exit would corrupt the stream for the client.
// ---------------------------------------------------------------------------

// std::cin cannot carry the protocol: the session pins the interpreter's
// stdin with rk_set_input, which (by design) redirects std::cin's buffer so
// Raku reads see EOF instead of eating protocol bytes — and would starve a
// std::getline here the same way. The protocol therefore reads fd 0 itself.
class LineReader {
public:
    bool next(std::string& line) {
        line.clear();
        for (;;) {
            while (pos_ < have_) {
                char c = buf_[pos_++];
                if (c == '\n') return true;
                line += c;
            }
            if (eof_) return !line.empty();
            auto n = RK_MCP_READ(0, buf_, (unsigned)sizeof buf_);
            if (n <= 0) {
                eof_ = true;
                if (line.empty()) return false;
                return true; // a final unterminated frame still counts
            }
            have_ = (size_t)n;
            pos_ = 0;
        }
    }

private:
    char buf_[65536];
    size_t have_ = 0, pos_ = 0;
    bool eof_ = false;
};

std::mutex g_writeMx;

void writeLine(const std::string& s) {
    std::lock_guard<std::mutex> lk(g_writeMx);
    std::fwrite(s.data(), 1, s.size(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

Json rpcResult(const Json& id, Json result) {
    Json r = Json::object();
    r.set("jsonrpc", Json::str("2.0"));
    r.obj.emplace_back("id", id);
    r.set("result", std::move(result));
    return r;
}

Json rpcError(const Json& id, int code, const std::string& msg) {
    Json e = Json::object();
    e.set("code", Json::integer(code));
    e.set("message", Json::str(msg));
    Json r = Json::object();
    r.set("jsonrpc", Json::str("2.0"));
    r.obj.emplace_back("id", id);
    r.set("error", std::move(e));
    return r;
}

// A tool's answer: one text block, isError only when true (the spec's
// default is false, and smaller messages are kinder to a context window).
Json toolText(const std::string& text, bool isError = false) {
    Json block = Json::object();
    block.set("type", Json::str("text"));
    block.set("text", Json::str(text));
    Json content = Json::array();
    content.arr.push_back(std::move(block));
    Json r = Json::object();
    r.set("content", std::move(content));
    if (isError) r.set("isError", Json::boolean(true));
    return r;
}

// ---------------------------------------------------------------------------
// The watchdog. Armed around every engine call with the request's id; if the
// deadline passes it answers THAT request with isError and exits the process.
// One thread for the server's lifetime, parked on a condition variable.
// ---------------------------------------------------------------------------

class Watchdog {
public:
    void start(int secs) {
        secs_ = secs;
        if (secs_ <= 0) return;
        th_ = std::thread([this] { run(); });
        th_.detach(); // exits with the process; nothing to join on the way out
    }
    void arm(const Json& id) {
        if (secs_ <= 0) return;
        std::lock_guard<std::mutex> lk(mx_);
        idJson_ = dumps(id);
        deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(secs_);
        armed_ = true;
        cv_.notify_one();
    }
    void disarm() {
        if (secs_ <= 0) return;
        std::lock_guard<std::mutex> lk(mx_);
        armed_ = false;
    }

private:
    void run() {
        std::unique_lock<std::mutex> lk(mx_);
        for (;;) {
            if (!armed_) {
                cv_.wait(lk);
                continue;
            }
            if (cv_.wait_until(lk, deadline_) == std::cv_status::timeout && armed_) {
                std::string text =
                    "the call ran longer than " + std::to_string(secs_) +
                    "s and the engine cannot be interrupted mid-evaluation, so this "
                    "server process exits now; the next call starts a fresh session "
                    "(state is lost). Raise the limit with `rakupp --mcp --timeout=SECS`.";
                // Hand-assembled on purpose: the reply must not allocate its way
                // through Json while the main thread might be wedged inside Raku.
                std::string msg = "{\"jsonrpc\":\"2.0\",\"id\":" + idJson_ +
                                  ",\"result\":{\"content\":[{\"type\":\"text\",\"text\":";
                dumpStr(text, msg);
                msg += "}],\"isError\":true}}";
                writeLine(msg);
                std::_Exit(0);
            }
        }
    }

    int secs_ = 0;
    std::mutex mx_;
    std::condition_variable cv_;
    std::thread th_;
    std::string idJson_;
    std::chrono::steady_clock::time_point deadline_;
    bool armed_ = false;
};

// ---------------------------------------------------------------------------
// The session: one interpreter, created lazily at the first tool call so
// `initialize` answers instantly, then living for the server's lifetime.
// ---------------------------------------------------------------------------

// Loaded at session start: how the raku tool renders a value and how anything
// crosses as JSON. rk-mcp-jsonable maps exotic leaves through .gist so the
// C++ walk below never needs to call back into Raku mid-walk (a call could
// recycle the arena the walk is reading).
const char* kPrelude = R"RKMCP(
sub rk-mcp-gist($v) { (try $v.gist) // ((try $v.raku) // '(unprintable value)') }
sub rk-mcp-jsonable($v) {
    return Any unless $v.defined;
    return $v if $v ~~ Bool or $v ~~ Int or $v ~~ Num or $v ~~ Str;
    return $v.Num if $v ~~ Rat or $v ~~ FatRat;
    if $v ~~ Positional {
        return $v.map({ rk-mcp-jsonable($_) }).Array;
    }
    if $v ~~ Associative {
        my %h;
        for $v.kv -> $k, $x { %h{~$k} = rk-mcp-jsonable($x) }
        return %h;
    }
    rk-mcp-gist($v)
}
)RKMCP";

// Loaded with the grammar shim at the first raku-parse: everything a matched
// parse answers with, gathered in ONE crossing.
const char* kParsePrelude = R"RKMCP(
sub rk-mcp-match-payload($m, Int $want-made) {
    my %p;
    %p<tree> = rk-match-tree($m);
    if $want-made == 1 {
        %p<made> = rk-mcp-jsonable($m.made);
    }
    %p
}
)RKMCP";

class Session {
public:
    explicit Session(std::vector<std::string> preload) : preload_(std::move(preload)) {}

    // Returns "" or the reason the session cannot exist. A failure is sticky:
    // a half-initialized interpreter answering some calls would be worse than
    // a server that says the same true thing every time.
    std::string ensure() {
        if (rk_) return "";
        if (!fatal_.empty()) return fatal_;
        RkConfig cfg{};
        cfg.size = sizeof cfg;
        cfg.own_stack = 1; // deep recursion meets the engine's guard, as the CLI's does
        rk_ = rk_new(&cfg);
        if (!rk_) return fatal_ = "rk_new refused: an interpreter is already live in this process";
        c_ = rk_ctx(rk_);
        rk_set_output(rk_, &Session::collect, this);
        rk_set_input(rk_, "", 0); // stdin is the PROTOCOL; Raku reads see EOF
        if (rk_eval(rk_, kPrelude, nullptr) != RK_OK)
            return fatal_ = std::string("mcp prelude failed: ") + lastError();
        for (auto& m : preload_) {
            std::string use = "use " + m + ";";
            if (rk_eval(rk_, use.c_str(), nullptr) != RK_OK)
                return fatal_ = "-M " + m + " failed: " + lastError();
        }
        return "";
    }

    std::string ensureShim() {
        std::string e = ensure();
        if (!e.empty()) return e;
        if (shim_) return "";
        if (rk_eval(rk_, rk_grammar_shim(), nullptr) != RK_OK)
            return std::string("grammar shim failed to load: ") + lastError();
        RkValue abi = rk_call(c_, "rk-shim-abi", nullptr, 0);
        if (const char* err = takeCallError())
            return std::string("grammar shim abi check failed: ") + err;
        if (rk_int_get(c_, abi) != 1)
            return "grammar shim ABI skew: this server speaks shim ABI 1";
        if (rk_eval(rk_, kParsePrelude, nullptr) != RK_OK)
            return std::string("mcp parse prelude failed: ") + lastError();
        shim_ = true;
        return "";
    }

    RkInterp rk() { return rk_; }
    RkCtx ctx() { return c_; }

    void clearCaptured() {
        std::lock_guard<std::mutex> lk(outMx_);
        out_.clear();
        err_.clear();
    }
    // One snapshot of both streams — Raku code may print from its own threads,
    // so reads share the writer's mutex.
    void captured(std::string& out, std::string& err) {
        std::lock_guard<std::mutex> lk(outMx_);
        out = out_;
        err = err_;
    }

    std::string lastError() {
        const char* e = rk_last_error(rk_);
        return e ? e : "unknown engine error";
    }
    // rk_call signals by leaving a pending error on the ctx; consuming it
    // here keeps a later "returned Nil" meaning exactly that.
    const char* takeCallError() {
        const char* e = rk_error(c_);
        if (e) {
            static thread_local std::string keep;
            keep = e;
            rk_clear_error(c_);
            return keep.c_str();
        }
        return nullptr;
    }

private:
    static void collect(void* ud, const char* text, size_t len, int isErr) {
        auto* self = (Session*)ud;
        std::lock_guard<std::mutex> lk(self->outMx_);
        (isErr ? self->err_ : self->out_).append(text, len);
    }

    RkInterp rk_ = nullptr;
    RkCtx c_ = nullptr;
    bool shim_ = false;
    std::string fatal_;
    std::vector<std::string> preload_;
    std::mutex outMx_;
    std::string out_, err_;
};

// RkValue -> Json, the exact table the bindings document (README section 5):
// RK_OTHER stringifies, a Rat crosses as a number, a too-wide Int has already
// crossed as its digits. Accessors only — no rk_call from inside the walk.
Json toJson(RkCtx c, RkValue v, int depth = 0) {
    if (!v || depth > 200) return Json::null();
    switch (rk_type(c, v)) {
        case RK_ANY:  return Json::null();
        case RK_BOOL: return Json::boolean(rk_truthy(c, v) != 0);
        case RK_INT:  return Json::integer(rk_int_get(c, v));
        case RK_NUM:
        case RK_RAT:  return Json::number(rk_num_get(c, v));
        case RK_ARRAY: {
            Json a = Json::array();
            size_t n = rk_elems(c, v);
            for (size_t i = 0; i < n; i++)
                a.arr.push_back(toJson(c, rk_at_pos(c, v, i), depth + 1));
            return a;
        }
        case RK_HASH: {
            Json o = Json::object();
            size_t n = rk_elems(c, v);
            for (size_t i = 0; i < n; i++) {
                size_t klen = 0;
                const char* k = rk_key_at(c, v, i, &klen);
                o.obj.emplace_back(std::string(k ? k : "", klen),
                                   toJson(c, rk_val_at(c, v, i), depth + 1));
            }
            return o;
        }
        case RK_STR:
        case RK_OTHER:
        default: {
            size_t len = 0;
            const char* s = rk_str_get(c, v, &len);
            return Json::str(std::string(s ? s : "", len));
        }
    }
}

// ---------------------------------------------------------------------------
// The two tools.
// ---------------------------------------------------------------------------

// What the captured streams contribute to a reply, shared by both tools:
// stdout first, stderr labeled — agents read this as one text block.
void appendStreams(std::string& text, const std::string& out, const std::string& err) {
    if (!out.empty()) {
        if (!text.empty() && text.back() != '\n') text += '\n';
        text += out;
    }
    if (!err.empty()) {
        if (!text.empty() && text.back() != '\n') text += '\n';
        text += "STDERR:\n";
        text += err;
    }
}

Json runEval(Session& ss, const std::string& code) {
    std::string e = ss.ensure();
    if (!e.empty()) return toolText(e, true);
    ss.clearCaptured();
    RkValue v = nullptr;
    int rc = rk_eval(ss.rk(), code.c_str(), &v);
    std::string out, err;
    ss.captured(out, err);
    if (rc != RK_OK) {
        std::string text = ss.lastError();
        appendStreams(text, out, err);
        return toolText(text, true);
    }
    std::string text;
    appendStreams(text, out, err);
    if (out.empty()) {
        // The REPL convention: show the value only when the code did not
        // already say something. Rooted across the gist call — a call may
        // recycle the arena the eval result lives in.
        RkValue rooted = rk_root(ss.ctx(), v);
        RkValue g = rk_call(ss.ctx(), "rk-mcp-gist", &rooted, 1);
        std::string gist;
        if (const char* callErr = ss.takeCallError()) { gist = callErr; }
        else {
            size_t len = 0;
            const char* s = rk_str_get(ss.ctx(), g, &len);
            gist.assign(s ? s : "", len);
        }
        rk_unroot(ss.ctx(), rooted);
        if (!text.empty() && text.back() != '\n') text += '\n';
        text += "=> " + gist;
    }
    return toolText(text);
}

Json runParse(Session& ss, const std::string& grammar, const std::string& text,
              const std::string& name, const std::string& actions,
              const std::string& rule) {
    std::string e = ss.ensureShim();
    if (!e.empty()) return toolText(e, true);
    ss.clearCaptured();
    RkCtx c = ss.ctx();

    RkValue cargs[3] = {
        rk_str(c, grammar.data(), grammar.size()),
        rk_str(c, name.data(), name.size()),
        rk_str(c, actions.data(), actions.size()),
    };
    RkValue idv = rk_call(c, "rk-grammar-compile", cargs, 3);
    if (const char* err = ss.takeCallError()) return toolText(err, true);
    long long gid = rk_int_get(c, idv);

    RkValue pargs[3] = {
        rk_int(c, gid),
        rk_str(c, text.data(), text.size()),
        rk_str(c, rule.data(), rule.size()),
    };
    RkValue m = rk_call(c, "rk-grammar-parse", pargs, 3);
    if (const char* err = ss.takeCallError()) return toolText(err, true);

    std::string out, err2;
    Json reply = Json::object();
    if (!m || rk_type(c, m) == RK_ANY) {
        reply.set("matched", Json::boolean(false));
        RkValue darg = rk_str(c, text.data(), text.size());
        RkValue d = rk_call(c, "rk-grammar-diagnosis", &darg, 1);
        if (const char* derr = ss.takeCallError()) {
            (void)derr; // no diagnosis is an answer too: matched:false stands alone
        }
        else if (d && rk_type(c, d) == RK_HASH) {
            Json dj = toJson(c, d);
            for (auto& kv : dj.obj)
                if (kv.first == "col") kv.first = "column"; // the bindings' field name
            reply.set("diagnosis", std::move(dj));
        }
    }
    else {
        // The match must survive the payload call, so it is rooted; the
        // payload itself is walked immediately, before any further call.
        RkValue rooted = rk_root(c, m);
        bool wantMade = !actions.empty();
        RkValue margs[2] = { rooted, rk_int(c, wantMade ? 1 : 0) };
        RkValue payload = rk_call(c, "rk-mcp-match-payload", margs, 2);
        const char* perr = ss.takeCallError();
        Json pj = perr ? Json::null() : toJson(c, payload);
        rk_unroot(c, rooted);
        if (perr) return toolText(perr, true);
        reply.set("matched", Json::boolean(true));
        if (const Json* tree = pj.find("tree")) reply.obj.emplace_back("tree", *tree);
        if (wantMade) {
            const Json* made = pj.find("made");
            reply.obj.emplace_back("made", made ? *made : Json::null());
        }
    }
    ss.captured(out, err2);          // actions classes may print; hand it over
    if (!out.empty()) reply.set("output", Json::str(out));
    if (!err2.empty()) reply.set("stderr", Json::str(err2));
    return toolText(dumps(reply));
}

// ---------------------------------------------------------------------------
// The protocol.
// ---------------------------------------------------------------------------

Json toolsList() {
    Json evalSchema = Json::object();
    evalSchema.set("type", Json::str("object"));
    {
        Json props = Json::object();
        Json code = Json::object();
        code.set("type", Json::str("string"));
        code.set("description", Json::str(
            "Raku source to evaluate in the persistent session"));
        props.obj.emplace_back("code", std::move(code));
        evalSchema.set("properties", std::move(props));
        Json req = Json::array();
        req.arr.push_back(Json::str("code"));
        evalSchema.set("required", std::move(req));
    }
    Json evalTool = Json::object();
    evalTool.set("name", Json::str("raku"));
    evalTool.set("description", Json::str(
        "Evaluate Raku source in a persistent session and return what it printed. "
        "State persists across calls exactly as in a REPL: a variable, sub, class "
        "or `use` from an earlier call is still there in later calls. When the "
        "code prints nothing, the value of its last statement is returned as "
        "`=> value`. Arithmetic is exact: decimal literals like 0.1 are rationals "
        "(0.1 + 0.2 == 0.3 is True) and integers never overflow. A `die` comes "
        "back as an error with its message; the session survives it."));
    evalTool.obj.emplace_back("inputSchema", std::move(evalSchema));

    Json parseSchema = Json::object();
    parseSchema.set("type", Json::str("object"));
    {
        Json props = Json::object();
        auto strProp = [&](const char* key, const char* desc) {
            Json p = Json::object();
            p.set("type", Json::str("string"));
            p.set("description", Json::str(desc));
            props.obj.emplace_back(key, std::move(p));
        };
        strProp("grammar", "Raku source declaring the grammar — the same text a "
                           ".raku file would hold");
        strProp("text", "the input text to parse");
        strProp("name", "the grammar's name inside the source. May be omitted "
                        "only when the grammar declaration is the source's last "
                        "statement; always pass it when iterating on a grammar, "
                        "so an edited version recompiles cleanly");
        strProp("actions", "an actions class named in the same source (requires "
                           "name); each parse runs a fresh instance, and the "
                           "top-level .made comes back as `made`");
        strProp("rule", "parse a fragment with this one rule instead of "
                        "anchoring TOP to the whole input");
        parseSchema.set("properties", std::move(props));
        Json req = Json::array();
        req.arr.push_back(Json::str("grammar"));
        req.arr.push_back(Json::str("text"));
        parseSchema.set("required", std::move(req));
    }
    Json parseTool = Json::object();
    parseTool.set("name", Json::str("raku-parse"));
    parseTool.set("description", Json::str(
        "Compile a Raku grammar from source and parse text with it. Answers JSON: "
        "{matched: true, tree: ...} — in the tree a node with no captures is its "
        "matched text, named captures are keys, positional captures are '0','1',…, "
        "and a quantified capture is an array — or {matched: false, diagnosis: "
        "{line, column, rule, pos}} locating where the parse stopped (the deepest "
        "failing rule; parses anchor to the WHOLE input unless `rule` says "
        "otherwise). Compilation is cached by (grammar, name, actions), so "
        "re-parsing with the same grammar is cheap."));
    parseTool.obj.emplace_back("inputSchema", std::move(parseSchema));

    Json tools = Json::array();
    tools.arr.push_back(std::move(evalTool));
    tools.arr.push_back(std::move(parseTool));
    Json r = Json::object();
    r.set("tools", std::move(tools));
    return r;
}

Json initializeResult(const Json* params) {
    // Echo the client's protocol revision: this server uses only the floor
    // every revision shares (initialize, tools/list, tools/call), so the
    // client's own dialect is the one it should hear back.
    std::string ver = "2025-06-18";
    if (params) {
        std::string got = params->getStr("protocolVersion");
        if (!got.empty()) ver = got;
    }
    Json caps = Json::object();
    caps.set("tools", Json::object());
    Json info = Json::object();
    info.set("name", Json::str("rakupp"));
    info.set("version", Json::str(rk_version()));
    Json r = Json::object();
    r.set("protocolVersion", Json::str(ver));
    r.set("capabilities", std::move(caps));
    r.set("serverInfo", std::move(info));
    r.set("instructions", Json::str(
        "rakupp is a Raku interpreter. The raku tool runs Raku in a session "
        "whose state persists across calls (define subs once, call them later); "
        "its arithmetic is exact — rationals and arbitrary-size integers — so it "
        "is also a calculator that does not round. The raku-parse tool compiles "
        "a Raku grammar and parses text into a JSON tree: deterministic "
        "structured extraction, with line/column/rule diagnosis when the parse "
        "fails."));
    return r;
}

} // namespace

int runServer(const Options& opt) {
    std::cerr << "rakupp --mcp: serving MCP over stdio (timeout "
              << (opt.timeoutSecs > 0 ? std::to_string(opt.timeoutSecs) + "s" : std::string("off"))
              << "); this is a machine protocol — point an MCP client here, not a keyboard\n";
    Session session(opt.preload);
    Watchdog dog;
    dog.start(opt.timeoutSecs);

    LineReader in;
    std::string line;
    while (in.next(line)) {
        if (line.empty() || line.find_first_not_of(" \t\r") == std::string::npos) continue;
        Json msg;
        if (!parse(line, msg) || msg.t != Json::T::Obj) {
            writeLine(dumps(rpcError(Json::null(), -32700, "parse error: one JSON-RPC object per line")));
            continue;
        }
        const Json* id = msg.find("id");
        std::string method = msg.getStr("method");
        if (method.empty()) continue;      // a response or malformed frame; nothing to answer
        if (!id) continue;                 // notifications (initialized, cancelled) need no reply
        const Json* params = msg.find("params");

        if (method == "initialize") {
            writeLine(dumps(rpcResult(*id, initializeResult(params))));
        }
        else if (method == "ping") {
            writeLine(dumps(rpcResult(*id, Json::object())));
        }
        else if (method == "tools/list") {
            writeLine(dumps(rpcResult(*id, toolsList())));
        }
        else if (method == "tools/call") {
            std::string tool = params ? params->getStr("name") : "";
            const Json* args = params ? params->find("arguments") : nullptr;
            auto need = [&](const char* key, std::string& into) -> bool {
                const Json* v = args ? args->find(key) : nullptr;
                if (!v || v->t != Json::T::Str) return false;
                into = v->s;
                return true;
            };
            if (tool == "raku") {
                std::string code;
                if (!need("code", code)) {
                    writeLine(dumps(rpcError(*id, -32602, "raku requires a string argument `code`")));
                    continue;
                }
                dog.arm(*id);
                Json result = runEval(session, code);
                dog.disarm();
                writeLine(dumps(rpcResult(*id, std::move(result))));
            }
            else if (tool == "raku-parse") {
                std::string grammar, text;
                if (!need("grammar", grammar) || !need("text", text)) {
                    writeLine(dumps(rpcError(*id, -32602, "raku-parse requires string arguments `grammar` and `text`")));
                    continue;
                }
                std::string name, actions, rule;
                need("name", name);
                need("actions", actions);
                need("rule", rule);
                if (!actions.empty() && name.empty()) {
                    writeLine(dumps(rpcError(*id, -32602, "raku-parse: `actions` needs `name` too (both ride the grammar source)")));
                    continue;
                }
                dog.arm(*id);
                Json result = runParse(session, grammar, text, name, actions, rule);
                dog.disarm();
                writeLine(dumps(rpcResult(*id, std::move(result))));
            }
            else {
                writeLine(dumps(rpcError(*id, -32602, "unknown tool '" + tool + "' (this server has raku and raku-parse)")));
            }
        }
        else {
            writeLine(dumps(rpcError(*id, -32601, "method not found: " + method)));
        }
    }
    return 0; // stdin closed: the client is done with us
}

} // namespace rakupp::mcp
