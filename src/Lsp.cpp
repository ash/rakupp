// Raku++ Language Server (LSP v1 — diagnostics).
//
// A self-contained JSON-RPC server over stdin/stdout. It wraps the *same*
// pipeline as `--lint` (Lexer -> Parser -> lintProgram + findUndeclaredVars)
// and reports parse errors, publishing all of them as LSP diagnostics. It is deliberately read-only
// against the engine: no interpreter, no codegen, nothing mutated. That keeps
// it decoupled from grammar/runtime churn — every parser improvement simply
// makes the diagnostics sharper for free.
//
// The JSON here is hand-rolled (the project has no JSON dependency and LSP
// traffic is small and regular). It handles exactly the message shapes the
// protocol uses: framed `Content-Length` headers wrapping a JSON body.

#include "Lsp.h"
#include "DeclCheck.h"
#include "Lexer.h"
#include "Lint.h"
#include "Parser.h"
#include "Runtime.h"

#include <algorithm>

#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace rakupp {
namespace {

// ---------------------------------------------------------------------------
// Minimal JSON value + parser + serializer.
// ---------------------------------------------------------------------------
struct Json {
    enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<Json> arr;
    std::map<std::string, Json> obj;

    static Json makeObj() { Json j; j.type = Obj; return j; }
    static Json makeArr() { Json j; j.type = Arr; return j; }
    static Json S(std::string s) { Json j; j.type = Str; j.str = std::move(s); return j; }
    static Json N(double n) { Json j; j.type = Num; j.num = n; return j; }
    static Json B(bool v) { Json j; j.type = Bool; j.b = v; return j; }

    bool isObj() const { return type == Obj; }
    // Object member lookup; returns a static Null when absent or wrong type.
    const Json& operator[](const std::string& k) const {
        static const Json nul;
        if (type != Obj) return nul;
        auto it = obj.find(k);
        return it == obj.end() ? nul : it->second;
    }
    Json& set(const std::string& k, Json v) { type = Obj; obj[k] = std::move(v); return *this; }
    void push(Json v) { type = Arr; arr.push_back(std::move(v)); }
};

// -- serialize --------------------------------------------------------------
void dumpStr(const std::string& s, std::string& out) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c); // pass UTF-8 bytes through unescaped
                }
        }
    }
    out += '"';
}

void dump(const Json& j, std::string& out) {
    switch (j.type) {
        case Json::Null: out += "null"; break;
        case Json::Bool: out += j.b ? "true" : "false"; break;
        case Json::Num: {
            // Integers (all LSP positions/ids are integral) print without a
            // trailing ".0"; fall back to full precision otherwise.
            if (j.num == static_cast<int64_t>(j.num)) {
                out += std::to_string(static_cast<int64_t>(j.num));
            } else {
                std::ostringstream ss; ss << j.num; out += ss.str();
            }
            break;
        }
        case Json::Str: dumpStr(j.str, out); break;
        case Json::Arr: {
            out += '[';
            for (size_t i = 0; i < j.arr.size(); i++) {
                if (i) out += ',';
                dump(j.arr[i], out);
            }
            out += ']';
            break;
        }
        case Json::Obj: {
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

std::string dump(const Json& j) { std::string s; dump(j, s); return s; }

// -- parse ------------------------------------------------------------------
// Four hex digits at `at`, or false — std::stoul threw std::invalid_argument on
// `\uZZZZ` and nothing caught it (the MCP/Jupyter servers' JsonLite already
// answers false here).
static bool hex4(const std::string& s, size_t at, unsigned& out) {
    if (at + 4 > s.size()) return false;
    out = 0;
    for (size_t k = 0; k < 4; k++) {
        char c = s[at + k]; unsigned d;
        if (c >= '0' && c <= '9') d = (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') d = (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = (unsigned)(c - 'A' + 10);
        else return false;
        out = out * 16 + d;
    }
    return true;
}

struct JsonParser {
    const std::string& s;
    size_t i = 0;
    int depth = 0; // nesting, capped as JsonLite caps it — 200k `[` was a stack overflow
    explicit JsonParser(const std::string& src) : s(src) {}

    void ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++; }

    Json parse() { ws(); return value(); }

    Json value() {
        ws();
        if (i >= s.size()) return Json();
        char c = s[i];
        if (c == '{' || c == '[') {
            if (++depth > 128) throw std::runtime_error("JSON nested too deeply");
            Json r = c == '{' ? object() : array();
            depth--;
            return r;
        }
        if (c == '"') { Json j; j.type = Json::Str; j.str = string(); return j; }
        if (c == 't') { i += 4; return Json::B(true); }
        if (c == 'f') { i += 5; return Json::B(false); }
        if (c == 'n') { i += 4; return Json(); }
        return number();
    }

    std::string string() {
        std::string out;
        i++; // opening quote
        while (i < s.size() && s[i] != '"') {
            char c = s[i++];
            if (c == '\\' && i < s.size()) {
                char e = s[i++];
                switch (e) {
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case '/': out += '/'; break;
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case 'u': {
                        if (i + 4 <= s.size()) {
                            unsigned cp = 0;
                            if (!hex4(s, i, cp)) { i += 4; break; } // not hex: drop the escape, keep parsing
                            i += 4;
                            // Surrogate pair -> astral code point.
                            if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 <= s.size()
                                && s[i] == '\\' && s[i + 1] == 'u') {
                                unsigned lo = 0;
                                if (hex4(s, i + 2, lo)) { i += 6; cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); }
                            }
                            appendUtf8(cp, out);
                        }
                        break;
                    }
                    default: out += e;
                }
            } else {
                out += c;
            }
        }
        if (i < s.size()) i++; // closing quote
        return out;
    }

    static void appendUtf8(unsigned cp, std::string& out) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    Json number() {
        size_t start = i;
        while (i < s.size() && (isdigit((unsigned char)s[i]) || s[i] == '-' || s[i] == '+'
               || s[i] == '.' || s[i] == 'e' || s[i] == 'E')) i++;
        Json j; j.type = Json::Num;
        try { j.num = std::stod(s.substr(start, i - start)); } catch (...) { j.num = 0; }
        return j;
    }

    Json array() {
        Json j = Json::makeArr();
        i++; // '['
        ws();
        if (i < s.size() && s[i] == ']') { i++; return j; }
        while (i < s.size()) {
            j.arr.push_back(value());
            ws();
            if (i < s.size() && s[i] == ',') { i++; continue; }
            break;
        }
        ws();
        if (i < s.size() && s[i] == ']') i++;
        return j;
    }

    Json object() {
        Json j = Json::makeObj();
        i++; // '{'
        ws();
        if (i < s.size() && s[i] == '}') { i++; return j; }
        while (i < s.size()) {
            ws();
            std::string key = string();
            ws();
            if (i < s.size() && s[i] == ':') i++;
            j.obj[key] = value();
            ws();
            if (i < s.size() && s[i] == ',') { i++; continue; }
            break;
        }
        ws();
        if (i < s.size() && s[i] == '}') i++;
        return j;
    }
};

// ---------------------------------------------------------------------------
// UTF-16 length of a UTF-8 line (LSP columns count UTF-16 code units).
// ---------------------------------------------------------------------------
int utf16Len(const std::string& line) {
    int n = 0;
    for (size_t k = 0; k < line.size();) {
        unsigned char c = line[k];
        int adv, units;
        if (c < 0x80) { adv = 1; units = 1; }
        else if ((c >> 5) == 0x6) { adv = 2; units = 1; }
        else if ((c >> 4) == 0xE) { adv = 3; units = 1; }
        else if ((c >> 3) == 0x1E) { adv = 4; units = 2; } // astral -> surrogate pair
        else { adv = 1; units = 1; }
        n += units;
        k += adv;
    }
    return n;
}

// ---------------------------------------------------------------------------
// Diagnostics: run the same pipeline as `--lint` over a document's text and
// return an LSP `diagnostics` array. Line numbers from the compiler are
// 1-based; LSP is 0-based.
// ---------------------------------------------------------------------------
Json computeDiagnostics(const std::string& src) {
    Json diags = Json::makeArr();

    // Split into lines once, to size each squiggle to its full line.
    std::vector<std::string> lines;
    {
        std::string cur;
        for (char c : src) {
            if (c == '\n') { lines.push_back(cur); cur.clear(); }
            else if (c != '\r') cur += c;
        }
        lines.push_back(cur);
    }
    auto lineLen = [&](int line0) -> int {
        if (line0 < 0 || line0 >= (int)lines.size()) return 0;
        return utf16Len(lines[line0]);
    };
    auto makeRange = [&](int line1) {
        int line0 = line1 > 0 ? line1 - 1 : 0;
        Json start = Json::makeObj();
        start.set("line", Json::N(line0)).set("character", Json::N(0));
        Json end = Json::makeObj();
        end.set("line", Json::N(line0)).set("character", Json::N(lineLen(line0)));
        Json range = Json::makeObj();
        range.set("start", std::move(start)).set("end", std::move(end));
        return range;
    };
    auto addDiag = [&](int line1, int severity, const std::string& code,
                       const std::string& message) {
        Json d = Json::makeObj();
        d.set("range", makeRange(line1));
        d.set("severity", Json::N(severity)); // 1=Error 2=Warning 3=Info 4=Hint
        if (!code.empty()) d.set("code", Json::S(code));
        d.set("source", Json::S("rakupp"));
        d.set("message", Json::S(message));
        diags.push(std::move(d));
    };

    Program prog;
    try {
        Lexer lexer(src);
        Parser parser(lexer.tokenize());
        prog = parser.parseProgram();
    } catch (const ParseError& e) {
        addDiag(e.line, 1 /*Error*/, "parse-error", e.what());
        return diags; // can't lint an unparseable program
    } catch (const std::exception& e) {
        addDiag(1, 1, "internal", e.what());
        return diags;
    }

    // The SAME list `--lint` builds, assembled the same way: the linter only
    // ever advises, so on its own it reports "no issues" for a file the compiler
    // refuses outright. An editor showing a clean file that will not run is that
    // failure one layer further out, and it is the one this server existed with
    // for its first release.
    std::vector<LintFinding> findings = lintProgram(prog);
    if (declCheckEnabled()) {
        // The relative defaults (`lib`, `.`, `rakulib`) resolve against the
        // server's working directory, which is the editor's project root — the
        // same answer `--lint` gives when run there with no -I.
        try {
            for (const auto& u : findUndeclaredVars(prog, src, effectiveSearchPath({})))
                findings.push_back({u.line, 'E', "undeclared-variable",
                                    "'" + u.name + "' is not declared"});
        } catch (const std::exception& e) {
            // A long-running server may not die of this. Say so rather than
            // silently dropping the check: a missing error is what this whole
            // paragraph is about.
            addDiag(1, 3 /*Info*/, "declcheck-unavailable",
                    std::string("the undeclared-variable check did not run: ") + e.what());
        } catch (...) {
            addDiag(1, 3, "declcheck-unavailable",
                    "the undeclared-variable check did not run");
        }
    }
    std::stable_sort(findings.begin(), findings.end(),
                     [](const LintFinding& a, const LintFinding& b) {
                         if (a.line != b.line) return a.line < b.line;
                         return a.rule < b.rule;
                     });
    for (const auto& f : findings) {
        // 'E' -> Error(1): the program will not run. 'W' -> Warning(2),
        // anything else (notes) -> Info(3).
        int sev = f.severity == 'E' ? 1 : f.severity == 'W' ? 2 : 3;
        addDiag(f.line, sev, f.rule, f.message);
    }
    return diags;
}

// ---------------------------------------------------------------------------
// Server.
// ---------------------------------------------------------------------------
class Server {
public:
    int run() {
        std::ios::sync_with_stdio(false);
        std::string body;
        while (readMessage(body)) {
            Json msg;
            try { JsonParser p(body); msg = p.parse(); }
            catch (std::exception&) { continue; } // hostile input is dropped, never a crash
            if (!msg.isObj()) continue;
            const Json& method = msg["method"];
            bool hasId = msg.obj.count("id") != 0;

            if (method.type != Json::Str) continue; // responses to our requests: ignore
            const std::string& m = method.str;

            if (m == "initialize") {
                reply(msg["id"], initializeResult());
            } else if (m == "initialized") {
                // notification, nothing to do
            } else if (m == "shutdown") {
                reply(msg["id"], Json()); // null result
                shuttingDown_ = true;
            } else if (m == "exit") {
                return shuttingDown_ ? 0 : 1;
            } else if (m == "textDocument/didOpen") {
                const Json& doc = msg["params"]["textDocument"];
                publish(doc["uri"].str, doc["text"].str);
            } else if (m == "textDocument/didChange") {
                const Json& params = msg["params"];
                const std::string& uri = params["textDocument"]["uri"].str;
                // Full sync (we advertise TextDocumentSyncKind.Full): the last
                // content change carries the whole new document.
                const Json& changes = params["contentChanges"];
                if (changes.type == Json::Arr && !changes.arr.empty()) {
                    publish(uri, changes.arr.back()["text"].str);
                }
            } else if (m == "textDocument/didClose") {
                const std::string& uri = msg["params"]["textDocument"]["uri"].str;
                // Clear this file's squiggles on close.
                Json empty = Json::makeArr();
                sendDiagnostics(uri, empty);
            } else if (hasId) {
                // Unknown request: MethodNotFound so the client isn't left hanging.
                Json err = Json::makeObj();
                err.set("code", Json::N(-32601)).set("message", Json::S("method not found: " + m));
                Json resp = Json::makeObj();
                resp.set("jsonrpc", Json::S("2.0")).set("id", msg["id"]).set("error", std::move(err));
                write(resp);
            }
            // Unknown notifications (no id): silently ignore, per LSP.
        }
        return 0;
    }

private:
    bool shuttingDown_ = false;

    // Read one `Content-Length`-framed message body from stdin.
    bool readMessage(std::string& body) {
        size_t contentLength = 0;
        std::string line;
        // Headers, terminated by a blank line.
        while (true) {
            if (!std::getline(std::cin, line)) return false;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) break; // end of headers
            auto colon = line.find(':');
            if (colon != std::string::npos) {
                std::string key = line.substr(0, colon);
                std::string val = line.substr(colon + 1);
                size_t b = val.find_first_not_of(" \t");
                if (b != std::string::npos) val = val.substr(b);
                if (key == "Content-Length") {
                    try { contentLength = std::stoul(val); } catch (...) { contentLength = 0; }
                }
            }
        }
        if (contentLength > (64u << 20)) return false; // a bogus header must not detonate the allocation
        body.resize(contentLength);
        std::cin.read(&body[0], (std::streamsize)contentLength);
        return std::cin.gcount() == (std::streamsize)contentLength;
    }

    void write(const Json& msg) {
        std::string payload = dump(msg);
        std::cout << "Content-Length: " << payload.size() << "\r\n\r\n" << payload;
        std::cout.flush();
    }

    void reply(const Json& id, Json result) {
        Json resp = Json::makeObj();
        resp.set("jsonrpc", Json::S("2.0"));
        resp.set("id", id);
        resp.set("result", std::move(result));
        write(resp);
    }

    void sendDiagnostics(const std::string& uri, Json& diags) {
        Json params = Json::makeObj();
        params.set("uri", Json::S(uri));
        params.set("diagnostics", std::move(diags));
        Json note = Json::makeObj();
        note.set("jsonrpc", Json::S("2.0"));
        note.set("method", Json::S("textDocument/publishDiagnostics"));
        note.set("params", std::move(params));
        write(note);
    }

    void publish(const std::string& uri, const std::string& text) {
        Json diags = computeDiagnostics(text);
        sendDiagnostics(uri, diags);
    }

    Json initializeResult() {
        Json textSync = Json::makeObj();
        textSync.set("openClose", Json::B(true));
        textSync.set("change", Json::N(1)); // 1 = Full document sync

        Json caps = Json::makeObj();
        caps.set("textDocumentSync", std::move(textSync));
        caps.set("diagnosticProvider", Json::B(false)); // we push, not pull

        Json info = Json::makeObj();
        info.set("name", Json::S("rakupp-lsp"));

        Json result = Json::makeObj();
        result.set("capabilities", std::move(caps));
        result.set("serverInfo", std::move(info));
        return result;
    }
};

} // namespace

int runLsp() {
    Server srv;
    return srv.run();
}

} // namespace rakupp
