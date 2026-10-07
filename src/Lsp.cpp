// Raku++ Language Server: diagnostics, hover, completion, go-to-definition.
//
// A self-contained JSON-RPC server over stdin/stdout. Its diagnostics wrap the
// *same* pipeline as `--lint` (Lexer -> Parser -> lintProgram +
// findUndeclaredVars) plus parse errors, each underlined on the token it is
// about. Hover, completion and definition come from LspIndex.cpp, which reads
// the token stream and the baked REFERENCE.md. It is deliberately read-only
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
#include "LspIndex.h"
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
// Positions: LSP speaks (0-based line, UTF-16 column); LspIndex speaks byte
// offsets. The conversion lives in lsp::TextDoc.
// ---------------------------------------------------------------------------
Json toPosition(const lsp::TextDoc& doc, size_t off) {
    int line0 = 0, col = 0;
    doc.positionOf(off, line0, col);
    Json p = Json::makeObj();
    p.set("line", Json::N(line0)).set("character", Json::N(col));
    return p;
}

Json toRange(const lsp::TextDoc& doc, lsp::Span s) {
    Json r = Json::makeObj();
    r.set("start", toPosition(doc, s.start)).set("end", toPosition(doc, s.end));
    return r;
}

size_t fromPosition(const lsp::TextDoc& doc, const Json& pos) {
    const Json& l = pos["line"];
    const Json& c = pos["character"];
    return doc.offsetAt(l.type == Json::Num ? (int)l.num : 0, c.type == Json::Num ? (int)c.num : 0);
}

// ---------------------------------------------------------------------------
// Diagnostics: run the same pipeline as `--lint` over a document's text and
// return an LSP `diagnostics` array. Line numbers from the compiler are
// 1-based; LSP is 0-based.
// ---------------------------------------------------------------------------
Json computeDiagnostics(const std::string& src) {
    Json diags = Json::makeArr();

    // Each squiggle covers the token the finding is about (its subject) when
    // that can be found on the line, else the line without its indentation.
    lsp::TextDoc doc(src);
    auto addDiag = [&](int line1, int severity, const std::string& code,
                       const std::string& message, const std::string& subject = "") {
        Json d = Json::makeObj();
        d.set("range", toRange(doc, lsp::locateOnLine(doc, line1, subject)));
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
        addDiag(e.line, 1 /*Error*/, "parse-error", e.what(), e.got);
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
                                    "'" + u.name + "' is not declared", u.name});
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
        addDiag(f.line, sev, f.rule, f.message, f.subject);
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
            int rc = handle(body);
            if (rc >= 0) return rc;
        }
        return 0;
    }

    // Handle one message body (no framing) and return every body the server
    // sends in answer, in order. For a host that carries the messages itself.
    std::vector<std::string> exchange(const std::string& body) {
        std::vector<std::string> out;
        sink_ = &out;
        handle(body);
        sink_ = nullptr;
        return out;
    }

private:
    // One message. Returns the exit code after `exit`, otherwise -1.
    int handle(const std::string& body) {
        Json msg;
        try { JsonParser p(body); msg = p.parse(); }
        catch (std::exception&) { return -1; } // hostile input is dropped, never a crash
        if (!msg.isObj()) return -1;
        const Json& method = msg["method"];
        bool hasId = msg.obj.count("id") != 0;

        if (method.type != Json::Str) return -1; // responses to our requests: ignore
        const std::string& m = method.str;

        if (m == "initialize") {
            readClientCapabilities(msg["params"]["capabilities"]);
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
            docs_[doc["uri"].str] = doc["text"].str;
            publish(doc["uri"].str, doc["text"].str);
        } else if (m == "textDocument/didChange") {
            const Json& params = msg["params"];
            const std::string& uri = params["textDocument"]["uri"].str;
            // Full sync (we advertise TextDocumentSyncKind.Full): the last
            // content change carries the whole new document.
            const Json& changes = params["contentChanges"];
            if (changes.type == Json::Arr && !changes.arr.empty()) {
                docs_[uri] = changes.arr.back()["text"].str;
                publish(uri, docs_[uri]);
            }
        } else if (m == "textDocument/didClose") {
            const std::string& uri = msg["params"]["textDocument"]["uri"].str;
            // Clear this file's squiggles on close.
            docs_.erase(uri);
            Json empty = Json::makeArr();
            sendDiagnostics(uri, empty);
        } else if (m == "textDocument/hover" || m == "textDocument/completion" ||
                   m == "textDocument/definition") {
            // A query may never take the server down: whatever goes wrong
            // in the index, the client gets an empty answer.
            Json result;
            try {
                result = m == "textDocument/hover"      ? hover(msg["params"])
                       : m == "textDocument/completion" ? completion(msg["params"])
                                                        : definition(msg["params"]);
            } catch (...) {
                result = Json();
            }
            if (hasId) reply(msg["id"], std::move(result));
        } else if (hasId) {
            // Unknown request: MethodNotFound so the client isn't left hanging.
            Json err = Json::makeObj();
            err.set("code", Json::N(-32601)).set("message", Json::S("method not found: " + m));
            Json resp = Json::makeObj();
            resp.set("jsonrpc", Json::S("2.0")).set("id", msg["id"]).set("error", std::move(err));
            write(resp);
        }
        // Unknown notifications (no id): silently ignore, per LSP.
        return -1;
    }

    std::vector<std::string>* sink_ = nullptr; // set by exchange(): bodies go here, not to stdout
    bool shuttingDown_ = false;
    std::map<std::string, std::string> docs_; // uri -> the text the client last sent
    bool hoverMarkdown_ = false;               // the client renders Markdown in a hover
    bool docMarkdown_ = false;                 // … and in completion documentation

    static bool listsMarkdown(const Json& formats) {
        if (formats.type != Json::Arr) return false;
        for (auto& f : formats.arr)
            if (f.type == Json::Str && f.str == "markdown") return true;
        return false;
    }

    void readClientCapabilities(const Json& caps) {
        const Json& td = caps["textDocument"];
        hoverMarkdown_ = listsMarkdown(td["hover"]["contentFormat"]);
        docMarkdown_ = listsMarkdown(td["completion"]["completionItem"]["documentationFormat"]);
    }

    // The document a request names, or nullptr when the client never opened it.
    const std::string* docFor(const Json& params) const {
        auto it = docs_.find(params["textDocument"]["uri"].str);
        return it == docs_.end() ? nullptr : &it->second;
    }

    static Json markup(bool md, const std::string& markdown, const std::string& plain) {
        Json m = Json::makeObj();
        m.set("kind", Json::S(md ? "markdown" : "plaintext"));
        m.set("value", Json::S(md ? markdown : plain));
        return m;
    }

    Json hover(const Json& params) {
        const std::string* text = docFor(params);
        if (!text) return Json();
        lsp::TextDoc doc(*text);
        lsp::Hover h = lsp::hoverAt(doc, fromPosition(doc, params["position"]));
        if (!h.found) return Json();
        Json r = Json::makeObj();
        r.set("contents", markup(hoverMarkdown_, h.markdown, h.plain));
        r.set("range", toRange(doc, h.span));
        return r;
    }

    Json completion(const Json& params) {
        Json list = Json::makeObj();
        list.set("isIncomplete", Json::B(false));
        Json items = Json::makeArr();
        const std::string* text = docFor(params);
        if (text) {
            lsp::TextDoc doc(*text);
            lsp::Completions c = lsp::completeAt(doc, fromPosition(doc, params["position"]));
            Json range = toRange(doc, c.replace);
            for (auto& it : c.items) {
                Json item = Json::makeObj();
                item.set("label", Json::S(it.label));
                item.set("kind", Json::N((int)it.kind));
                if (!it.detail.empty()) item.set("detail", Json::S(it.detail));
                // Names from this file first, then the built-ins, each alphabetical.
                item.set("sortText", Json::S((it.local ? "0" : "1") + it.label));
                // The edit replaces the whole typed prefix, sigil and twigil
                // included: a client's own idea of a word stops at `$`.
                Json edit = Json::makeObj();
                edit.set("range", range).set("newText", Json::S(it.label));
                item.set("textEdit", std::move(edit));
                item.set("filterText", Json::S(it.label));
                if (!it.docPlain.empty())
                    item.set("documentation", markup(docMarkdown_, it.docMarkdown, it.docPlain));
                items.push(std::move(item));
            }
        }
        if (items.type != Json::Arr) items = Json::makeArr();
        list.set("items", std::move(items));
        return list;
    }

    Json definition(const Json& params) {
        const std::string* text = docFor(params);
        if (!text) return Json();
        lsp::TextDoc doc(*text);
        lsp::Span target;
        if (!lsp::definitionAt(doc, fromPosition(doc, params["position"]), target)) return Json();
        Json loc = Json::makeObj();
        loc.set("uri", Json::S(params["textDocument"]["uri"].str));
        loc.set("range", toRange(doc, target));
        return loc;
    }

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
        if (sink_) { sink_->push_back(std::move(payload)); return; }
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
        caps.set("hoverProvider", Json::B(true));
        caps.set("definitionProvider", Json::B(true));
        // Completion opens by itself after `.` (methods) and the `$`/`@`
        // sigils (variables). `%` and `&` are operators as often as sigils,
        // so those wait for the editor's own completion key.
        Json triggers = Json::makeArr();
        for (const char* t : {".", "$", "@"}) triggers.push(Json::S(t));
        Json completion = Json::makeObj();
        completion.set("triggerCharacters", std::move(triggers));
        caps.set("completionProvider", std::move(completion));
        // No "diagnosticProvider": we push diagnostics, and its absence is how
        // a server says so. The field takes an options object, never a
        // boolean; lsp-mode reads any value as "pull supported" and sends
        // textDocument/diagnostic requests.

        Json info = Json::makeObj();
        info.set("name", Json::S("rakupp-lsp"));

        Json result = Json::makeObj();
        result.set("capabilities", std::move(caps));
        result.set("serverInfo", std::move(info));
        return result;
    }
};

} // namespace

int runLsp(const std::string& reference) {
    lsp::setReference(reference);
    Server srv;
    return srv.run();
}

std::string lspExchange(const std::string& body, const std::string& reference) {
    static Server* srv = nullptr;
    if (!srv) {
        lsp::setReference(reference);
        srv = new Server;
    }
    std::string out = "[";
    for (const std::string& b : srv->exchange(body)) {
        if (out.size() > 1) out += ',';
        out += b;
    }
    return out + "]";
}

} // namespace rakupp
