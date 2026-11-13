// The parser's side of STRING INTERPOLATION: `"…$x…@a[]…%h<k>…&f()…{ … }"`,
// read into an InterpStr of literal and embedded parts (parseInterpString).
// Split out of Parser.cpp, whose size it was (RAKUDO-ISSUES-PLAN R7.0); it
// rides the same `parse` archive (CMakeLists.txt).
#include "AsciiCtype.h"
#include "BuiltinsShared.h"
#include "CNumeric.h"
#include "IntOps.h"
#include "Lexer.h"
#include "Parser.h"
#include "Unicode.h"
#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace rakupp {

// Call from inside a `catch (...)` around an interpolation's sub-parse.
//
// An interpolated expression that does not parse is not an error, it is just
// text — `"@foo["` in a mail address, a stray brace in a CSS snippet — so every
// such catch degrades to the literal characters. An OBSOLESCENCE diagnostic is
// the opposite case: the text parsed perfectly well and is being refused on
// purpose. Swallowing that would quietly turn `"@a[-1]"` from a compile error
// into the literal string `@a[-1]`, which is worse than either answer.
static void rethrowIfObsolete() {
    try { throw; }
    catch (ParseError& pe) { if (pe.exType == "X::Obsolete") throw; }
    catch (...) {}
}
// Scan a balanced `{ … }` code block inside an interpolated string, RESPECTING
// quoted spans: a brace inside '…' or "…" is text, not nesting — `{sym('{')}`
// must not swallow the rest of the string. Returns the inner text and leaves
// `j` on the closing brace (or n).
std::string scanInterpBlock(const std::string& raw, size_t& j, char open, char close);
std::string scanInterpBlock(const std::string& raw, size_t& j,
                                   char open = '{', char close = '}') {
    size_t n = raw.size();
    int depth = 1; std::string inner;
    char q = 0;                    // active quote char inside the block, if any
    while (j < n && depth > 0) {
        char c = raw[j];
        if (q) {
            // `\'` inside '…' is an escape too, not its closer: `"a {'it\'s'} z"`
            // ended the quote there, ran to the end of the string looking for
            // the `}`, and the whole rest of the string vanished (issue #137)
            if (c == '\\' && j + 1 < n) { inner += c; inner += raw[j+1]; j += 2; continue; }
            if (c == q) q = 0;
        }
        // a “…” / ‘…’ / ｢…｣ string is the code's too: `{ $x ?? “doesn't” !! '' }`
        else if (size_t e = uniQuoteSpanEnd(raw, j)) { inner.append(raw, j, e - j); j = e; continue; }
        // …and an apostrophe inside a NAME (`don't`) opens no string
        else if (c == '\'' && !inner.empty() && (ascii::isalnum((unsigned char)inner.back()) || inner.back() == '_') &&
                 j + 1 < n && rakuIdentStart(raw[j + 1])) {}
        else if (c == '\'' || c == '"') q = c;
        else if (c == open) depth++;
        else if (c == close) { depth--; if (depth == 0) break; }
        inner += c; j++;
    }
    return inner;
}


// The argument list of a call inside an interpolated string, from just past
// its `(` (depth `d` already 1): copied into `var` up to and including the
// matching `)`. A quoted argument is CODE, so a `)` inside one closes nothing —
// `"$s.subst(")", "-")"` (the lexer reads the same group the same way). A
// quote that never closes falls back to the plain paren count.
static size_t interpCallArgsEnd(const std::string& raw, size_t j, int& d, std::string& var) {
    const size_t n = raw.size();
    size_t k = j; int dd = d;
    while (k < n && dd > 0) {
        const char ch = raw[k];
        if (ch == '\\' && k + 1 < n) { k += 2; continue; }
        if (ch == '"' || ch == '\'') {
            size_t e = k + 1;
            while (e < n && raw[e] != ch) e += raw[e] == '\\' ? 2 : 1;
            if (e >= n) { k = std::string::npos; break; }
            k = e + 1;
            continue;
        }
        if (ch == '(') dd++;
        else if (ch == ')') dd--;
        k++;
    }
    if (k != std::string::npos && dd == 0) { var.append(raw, j, k - j); d = 0; return k; }
    while (j < n && d > 0) { if (raw[j]=='(') d++; else if (raw[j]==')') d--; var += raw[j++]; }
    return j;
}

ExprPtr Parser::parseInterpString(const std::string& rawIn) {
    // interpolation-feature prefix from quoting adverbs (q:c / Q:s / qq:!s):
    // "\x02feats\x02" — s=scalars a=arrays h=hashes f=&calls c={blocks} b=backslashes
    std::string raw = rawIn;
    bool fS = true, fA = true, fH = true, fC = true, fB = true, fF = true;
    if (!raw.empty() && raw[0] == '\x02') {
        size_t fend = raw.find('\x02', 1);
        if (fend != std::string::npos) {
            std::string feats = raw.substr(1, fend - 1);
            raw = raw.substr(fend + 1);
            fS = feats.find('s') != std::string::npos;
            fA = feats.find('a') != std::string::npos;
            fH = feats.find('h') != std::string::npos;
            fC = feats.find('c') != std::string::npos;
            fB = feats.find('b') != std::string::npos;
            fF = feats.find('f') != std::string::npos;
        }
    }
    // An embedded expression is parsed by a parser of its own, whose tokens are
    // numbered from the string's line, the newlines before it counted: `"$nope"`
    // on line 301 is reported there, where it was line 1 (DeclCheck then looked
    // only 200 lines on) and a `{…}` block's runtime errors named its own line 1
    // (a string token records the line it ENDS on, a heredoc's its marker's)
    auto lineAt = [&](size_t at) {
        const Token& st = pos_ > 0 ? toks_[pos_ - 1] : cur();
        const int start = st.heredoc ? st.line + 1 : st.line - (int)std::count(st.text.begin(), st.text.end(), '\n');
        return start + (int)std::count(raw.begin(), raw.begin() + (long)std::min(at, raw.size()), '\n');
    };
    auto embedAt = [&](size_t at, const std::string& src, bool own = false) {
        embedLineBase_ = lineAt(at);
        struct Reset { int& b; ~Reset() { b = 0; } } reset{embedLineBase_};
        return parseEmbeddedExpr(src, own);
    };
    // A `$` that starts no variable must be backslashed: `"$"`, `"a $, b"`;
    // and `"${۳}"` is Perl 5's capture spelling. Top-level text only — a
    // `{ … }` closure in the string is code.
    if (fS) {
        int depth = 0;
        for (size_t i = 0; i < raw.size(); i++) {
            char c = raw[i];
            if (c == '\\' && fB) { i++; continue; }
            if (fC && c == '{') { depth++; continue; }
            if (fC && c == '}' && depth > 0) { depth--; continue; }
            if (depth || c != '$') continue;
            char d = i + 1 < raw.size() ? raw[i + 1] : '\0';
            if (d == '\0' || d == ' ' || d == '\t' || d == '\n' || d == ',')
                throw ParseError("Non-variable $ must be backslashed", cur().line,
                                 "X::Backslash::NonVariableDollar", {});
            if (d == '{') {
                size_t q = i + 2; long long val = 0; int nd = 0;
                while (q < raw.size()) {
                    unsigned char b = (unsigned char)raw[q];
                    int l = b < 0x80 ? 1 : (b >> 5) == 0x6 ? 2 : (b >> 4) == 0xE ? 3 : (b >> 3) == 0x1E ? 4 : 1;
                    if (q + l > raw.size()) l = 1;
                    uint32_t cp = l == 1 ? b : (uint32_t)(b & (0xFF >> (l + 1)));
                    for (int k = 1; k < l; k++) cp = (cp << 6) | ((unsigned char)raw[q + k] & 0x3F);
                    int dv = uniDigitValue(cp);
                    if (dv < 0) break;
                    val = val * 10 + dv; nd++; q += l;
                }
                if (nd && q < raw.size() && raw[q] == '}') {
                    std::string old = raw.substr(i, q + 1 - i);
                    std::string repl = "{$" + std::to_string(val > 0 ? val - 1 : 0) + "}";
                    throw ParseError("Unsupported use of " + old + ". In Raku please use: " + repl + ".",
                                     cur().line, "X::Obsolete", {{"old", old}, {"replacement", repl}});
                }
            }
        }
    }
    auto result = std::make_unique<InterpStr>();
    std::string lit;
    auto flush = [&]() {
        if (!lit.empty()) { result->parts.push_back(std::make_unique<StrLit>(lit)); lit.clear(); }
    };
    auto isIdentCont = [](char c) { return rakuIdentCont(c); };
    (void)fF;
    // How many bytes the UTF-8 sequence at `p` occupies, and its codepoint.
    auto cpAtRaw = [&](size_t p, size_t& len) -> uint32_t {
        unsigned char b = (unsigned char)raw[p];
        len = b < 0x80 ? 1 : (b >> 5) == 0x6 ? 2 : (b >> 4) == 0xE ? 3 : (b >> 3) == 0x1E ? 4 : 1;
        if (p + len > raw.size()) len = 1;
        if (len == 1) return b;
        uint32_t cp = (uint32_t)(b & (0xFF >> (len + 1)));
        for (size_t k = 1; k < len; k++) cp = (cp << 6) | ((unsigned char)raw[p + k] & 0x3F);
        return cp;
    };
    // Does the character at `p` continue an interpolated variable's name?
    // Raku identifiers really do take Unicode letters — "$vкнига" is ONE name in
    // both engines — but treating every non-ASCII BYTE as a name character swept up
    // punctuation and emoji too, so `"$v…"` printed its own source and `"$v🙂"`
    // reported an undeclared variable. ID_Continue is the actual rule; U+2026 (Po),
    // the guillemets (Pi/Pf), U+2013 (Pd) and emoji (So) are none of them.
    auto identContAt = [&](size_t p, size_t& len) -> bool {
        if ((unsigned char)raw[p] < 0x80) { len = 1; return isIdentCont(raw[p]); }
        uint32_t cp = cpAtRaw(p, len);
        return uniIdContinue(cp);   // rt's own copy: EVAL needs no unicode-props for it
    };
    // The `-`/`'` join, with a possibly NON-ASCII letter after it.
    // `rakuIdentJoins` is a byte test, so it answers no for `markup-Δ` — and
    // this scanner has to agree with the LEXER about where a name ends or
    // `"$markup-Δ"` interpolates differently from the bare name, which is the
    // exact failure the rule's own comment in Lexer.h records.
    auto identJoinsAt = [&](size_t p) -> bool {
        if (p + 1 >= raw.size() || (raw[p] != '-' && raw[p] != '\'')) return false;
        if ((unsigned char)raw[p + 1] < 0x80) return rakuIdentStart(raw[p + 1]);
        size_t l; return identContAt(p + 1, l);
    };

    size_t i = 0, n = raw.size();
    while (i < n) {
        char c = raw[i];
        if (fB && c == '\\' && i + 1 < n) {
            char e = raw[i + 1];
            // \x41 \x[263a] hex, \o77 \o[..] octal
            // \c65 is the DECIMAL codepoint form (\c[…] is handled below)
            if (e == 'x' || e == 'o' || (e == 'c' && i + 2 < n && ascii::isdigit((unsigned char)raw[i + 2]))) {
                int base = e == 'x' ? 16 : e == 'o' ? 8 : 10;
                size_t j = i + 2;
                auto emitCp = [&](long cp) { escapeCodepointCheck(cp, cur().line);
                    if (cp < 0x80) lit += (char)cp;
                    else if (cp < 0x800) { lit += (char)(0xC0 | (cp >> 6)); lit += (char)(0x80 | (cp & 0x3F)); }
                    else if (cp < 0x10000) { lit += (char)(0xE0 | (cp >> 12)); lit += (char)(0x80 | ((cp >> 6) & 0x3F)); lit += (char)(0x80 | (cp & 0x3F)); }
                    else { lit += (char)(0xF0 | (cp >> 18)); lit += (char)(0x80 | ((cp >> 12) & 0x3F)); lit += (char)(0x80 | ((cp >> 6) & 0x3F)); lit += (char)(0x80 | (cp & 0x3F)); }
                };
                if (j < n && raw[j] == '[') {
                    j++; std::string body;
                    while (j < n && raw[j] != ']') body += raw[j++];
                    if (j >= n)   // `"\x[6211"` — the bracket never closes
                        throw ParseError(std::string("Unable to parse expression in ") +
                                         (e == 'x' ? "hex" : e == 'o' ? "octal" : "decimal") +
                                         " character; couldn't find final ']'", cur().line,
                                         "X::Comp::AdHoc", {});
                    if (j < n) j++; // ]
                    // comma-separated list of codepoints:  \x[0042,0323]
                    size_t p = 0;
                    while (p < body.size()) {
                        size_t comma = body.find(',', p);
                        std::string tok = body.substr(p, comma == std::string::npos ? std::string::npos : comma - p);
                        size_t a = tok.find_first_not_of(" \t_"), b = tok.find_last_not_of(" \t");
                        if (a != std::string::npos) { tok = tok.substr(a, b - a + 1); emitCp(strtol(tok.c_str(), nullptr, base)); }
                        if (comma == std::string::npos) break;
                        p = comma + 1;
                    }
                } else {
                    std::string digits;
                    auto isd = [&](char ch) { return base == 16 ? ascii::isxdigit((unsigned char)ch)
                                                  : base == 8 ? (ch >= '0' && ch <= '7')
                                                  : ascii::isdigit((unsigned char)ch); };
                    while (j < n && isd(raw[j])) digits += raw[j++];
                    // `"\xZZ"` used to emit a silent NUL (strtol of "" is 0)
                    if (digits.empty())
                        throw ParseError("Unrecognized backslash sequence: '\\" + std::string(1, e) + "'",
                                         cur().line, "X::Backslash::UnrecognizedSequence",
                                         {{"sequence", std::string(1, e)}});
                    emitCp(strtol(digits.c_str(), nullptr, base));
                }
                i = j;
                continue;
            }
            // \c[NAME] / \c[65] / \c[NAME1, NAME2] — named or numeric codepoints
            if (e == 'c' && i + 2 < n && raw[i + 2] != '[' && !ascii::isdigit((unsigned char)raw[i + 2])) {
                unsigned char cc = (unsigned char)raw[i + 2];
                if ((cc >= '@' && cc <= '_') || cc == '?') { lit += (char)(cc == '?' ? 127 : cc - 0x40); i += 3; continue; }
                throw ParseError(std::string("Unrecognized \\c character '") + (char)cc + "'", cur().line);
            }
            if (e == 'c' && i + 2 < n && raw[i + 2] == '[') {
                size_t j = i + 3; std::string body;
                while (j < n && raw[j] != ']') body += raw[j++];
                if (j >= n)   // `"\c[6211"` — the bracket never closes
                    throw ParseError("Unable to parse expression in character name; couldn't find final ']'",
                                     cur().line, "X::Comp::AdHoc", {});
                if (j < n) j++; // ]
                auto emitCp = [&](long cp) { escapeCodepointCheck(cp, cur().line);
                    if (cp < 0x80) lit += (char)cp;
                    else if (cp < 0x800) { lit += (char)(0xC0 | (cp >> 6)); lit += (char)(0x80 | (cp & 0x3F)); }
                    else if (cp < 0x10000) { lit += (char)(0xE0 | (cp >> 12)); lit += (char)(0x80 | ((cp >> 6) & 0x3F)); lit += (char)(0x80 | (cp & 0x3F)); }
                    else { lit += (char)(0xF0 | (cp >> 18)); lit += (char)(0x80 | ((cp >> 12) & 0x3F)); lit += (char)(0x80 | ((cp >> 6) & 0x3F)); lit += (char)(0x80 | (cp & 0x3F)); }
                };
                size_t p = 0;
                while (p < body.size()) {
                    size_t comma = body.find(',', p);
                    std::string tok = body.substr(p, comma == std::string::npos ? std::string::npos : comma - p);
                    // A character name may be WRAPPED across lines in the source
                    // (MIME::Base64's test writes `\c[\nNEITHER LESS-THAN …]`), so a
                    // newline counts as whitespace: trim the ends, collapse the rest.
                    size_t a = tok.find_first_not_of(" \t\r\n"), b = tok.find_last_not_of(" \t\r\n");
                    if (a != std::string::npos) tok = tok.substr(a, b - a + 1);
                    { std::string flat; bool sp = false;
                      for (char ch : tok) {
                          if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') { sp = true; continue; }
                          if (sp && !flat.empty()) flat += ' ';
                          sp = false; flat += ch;
                      }
                      tok = flat; }
                    if (!tok.empty()) {
                        if (ascii::isdigit((unsigned char)tok[0])) emitCp(strtol(tok.c_str(), nullptr, 10));
                        else {
                            int32_t cp = uniCharByName(tok);
                            if (cp < 0) { // names are case-insensitive: "\c[arabic number sign]"
                                std::string up = tok;
                                for (auto& ch : up) ch = (char)ascii::toupper((unsigned char)ch);
                                cp = uniCharByName(up);
                            }
                            if (cp < 0) {
                                // a NAMED SEQUENCE: several codepoints under one
                                // name — `\c[woman facepalming]`, `\c[united states]`
                                std::string seq = uniSeqByName(tok);
                                if (seq.empty())
                                    throw ParseError("Unrecognized character name [" + tok + "]", cur().line);
                                lit += seq;
                            }
                            else emitCp(cp);
                        }
                    }
                    if (comma == std::string::npos) break;
                    p = comma + 1;
                }
                i = j;
                continue;
            }
            switch (e) {
                // (`\r\n` written out is a literal CRLF whatever `use newline` says)
                case 'n': lit += (i >= 2 && raw[i - 2] == '\\' && raw[i - 1] == 'r' && !(i >= 3 && raw[i - 3] == '\\')) ? std::string("\n") : newlineSeq_; break;
                case 't': lit += '\t'; break;
                case 'r': lit += '\r'; break;
                case '0': lit += '\0'; break;
                case 'e': lit += '\x1b'; break;
                case 'a': lit += '\a'; break;
                case 'b': lit += '\b'; break;
                case 'f': lit += '\f'; break;
                case '"': lit += '"'; break;
                case '\'': lit += '\''; break;
                case '\\': lit += '\\'; break;
                case '$': lit += '$'; break;
                case '@': lit += '@'; break;
                case '%': lit += '%'; break;
                case '{': lit += '{'; break;
                default:
                    // every unassigned alphabetic escape is reserved: `"\u"`.
                    // (\x \o \c with digits or brackets were handled above; a
                    // bare `\c` with nothing usable after it is reserved too)
                    if (ascii::isalpha((unsigned char)e))
                        throw ParseError("Unrecognized backslash sequence: '\\" +
                                         std::string(1, e) + "'", cur().line,
                                         "X::Backslash::UnrecognizedSequence",
                                         {{"sequence", std::string(1, e)}});
                    // Perl's octal `"\123"` / `"\10"` (a lone `\0` is still NUL)
                    if ((e >= '1' && e <= '9') ||
                        (e == '0' && i + 2 < n && ascii::isdigit((unsigned char)raw[i + 2]))) {
                        size_t q = i + 1; std::string ds;
                        while (q < n && ascii::isdigit((unsigned char)raw[q])) ds += raw[q++];
                        throw ParseError("Unrecognized backslash sequence: '\\" + ds + "'", cur().line,
                                         "X::Backslash::UnrecognizedSequence", {{"sequence", ds}});
                    }
                    lit += e;
                    break;
            }
            i += 2;
            continue;
        }
        if (fC && c == '{') {
            // balanced code block (quote-aware: a brace inside '…'/"…" is text)
            size_t j = i + 1;
            std::string inner = scanInterpBlock(raw, j);
            flush();
            // a declarator WORD (`my`, `our`, `state`, `constant`) followed by space
            auto declares = [&]() {
                for (const char* kw : {"my", "our", "state", "constant"}) {
                    size_t L = strlen(kw);
                    for (size_t at = inner.find(kw); at != std::string::npos; at = inner.find(kw, at + 1)) {
                        bool startOk = at == 0 || !(ascii::isalnum((unsigned char)inner[at - 1]) ||
                                                    strchr("_$@%&-", inner[at - 1]));
                        bool endOk = at + L < inner.size() && ascii::isspace((unsigned char)inner[at + L]);
                        if (startOk && endOk) return true;
                    }
                }
                // …and an ANONYMOUS state variable, `$++` / `($)`: its state is the closure's,
                // fresh each time the string is built, as in Rakudo (rakudo#1961: `"count {$++}"`
                // counted 0 1 2 across a loop, and two blocks in one string shared one `$`)
                for (size_t at = inner.find('$'); at != std::string::npos; at = inner.find('$', at + 1)) {
                    const char nx = at + 1 < inner.size() ? inner[at + 1] : '\0';
                    const bool prevOk = at == 0 || !(ascii::isalnum((unsigned char)inner[at - 1]) || inner[at - 1] == '\\');
                    if (prevOk && (nx == '\0' || nx == '+' || nx == '-' || nx == ')' || nx == ';' || nx == ',' ||
                                   nx == '}' || nx == ' ' || nx == '\t' || nx == '\n')) return true;
                }
                return false;
            };
            // A block is CODE, as it is anywhere else: code that does not parse
            // is a compile error, as in Rakudo (`"a {1 +} z"`: Missing required
            // term after infix). It was dropped without a word, and the string
            // read on as if the block had been empty. The error names the line
            // the block is on, not its line within the block's own text.
            try { auto& bp = result->parts.emplace_back(embedAt(i, inner, declares())); bp->blockPart = true; if (!bp->line) bp->line = lineAt(i); }
            catch (ParseError& pe) {
                if (pe.line <= 0) pe.line = lineAt(i);   // (a line of its own tokens is the file's now)
                throw;
            }
            catch (...) { rethrowIfObsolete(); }
            i = j + 1;
            continue;
        }
        // The postfix chain after an interpolated variable — ONE scanner for every
        // sigil branch below (`$/`, `$!`, `$0`, `$<name>`, `$var`, `@arr`, `%h`);
        // three private copies used to disagree on when a bare `.name` commits.
        // Links: [..] {..} <..> (..) .name .[..] .{..} .(..)
        // A method call interpolates only if it — or a later link in the
        // chain — has parens: "$x.ord" stays literal, but "$x.ord.fmt('%d')"
        // interpolates the whole chain up to the last parenthesised call
        // ("$x.ord.fmt('%d').flip" leaves the trailing bare .flip literal).
        // Subscripts always interpolate. Bare .method links are appended
        // tentatively and only "committed" once a parenthesised call or a
        // subscript follows; any uncommitted tail is dropped back to literal.
        auto scanChain = [&](size_t& j, std::string& var) -> bool {
        bool hadPostfix = false;
        size_t committedLen = var.size();   // var length confirmed for interpolation
        size_t committedJ = j;              // matching raw index
        auto commit = [&]() { hadPostfix = true; committedLen = var.size(); committedJ = j; };
        for (;;) {
            // A subscript or call marker after an UNCOMMITTED bare `.name` does not
            // commit it — only a parenthesised call does: "$s.uc().chars[0]" prints
            // ABC.chars[0] and "$s.uc.chars()" prints 3 (Rakudo's rule, probed).
            if (committedLen < var.size() && j < n &&
                (raw[j] == '[' || raw[j] == '{' || raw[j] == '<' || raw[j] == '(' ||
                 ((unsigned char)raw[j] == 0xC2 && j + 1 < n && (unsigned char)raw[j + 1] == 0xAB) ||
                 (raw[j] == '.' && j + 1 < n && (raw[j + 1] == '[' || raw[j + 1] == '{' || raw[j + 1] == '('))))
                break;
            if (j < n && raw[j] == '[') {
                int d = 1; var += raw[j++];
                while (j < n && d > 0) { if (raw[j]=='[') d++; else if (raw[j]==']') d--; var += raw[j++]; }
                commit();
            } else if (j < n && raw[j] == '(') {
                // postcircumfix call: `"$c(3)"` invokes the Callable, the same
                // postfix as `.( )`. Rakudo applies it to every sigil — `"@a(0)"`
                // and `"%h(0)"` reach CALL-ME on an Array/Hash and die there — so
                // this commits like any other subscript. Content that is not an
                // argument list still fails to parse and falls back to literal
                // text, which is what keeps `"$name(see note)"` printing.
                int d = 1; var += raw[j++];
                j = interpCallArgsEnd(raw, j, d, var);
                commit();
            } else if (j + 1 < n && ((raw[j] == '<' && raw[j + 1] == '<') ||
                                     ((unsigned char)raw[j] == 0xC2 && (unsigned char)raw[j + 1] == 0xAB))) {
                // interpolating word subscript: "%h<<a $x>>" / "%h«a»"
                const char* close = raw[j] == '<' ? ">>" : "\xC2\xBB";
                size_t e = raw.find(close, j + 2);
                if (e == std::string::npos) break;
                var += raw.substr(j, e + 2 - j);
                j = e + 2;
                commit();
            } else if (j < n && raw[j] == '<') {
                // angle-bracket hash subscript: %h<key>  @a<...>
                var += raw[j++];
                while (j < n && raw[j] != '>') var += raw[j++];
                if (j < n) var += raw[j++]; // closing >
                commit();
            } else if (j < n && raw[j] == '{') {
                int d = 1; var += raw[j++];
                while (j < n && d > 0) { if (raw[j]=='{') d++; else if (raw[j]=='}') d--; var += raw[j++]; }
                commit();
            } else if (j + 1 < n && raw[j] == '.' && (raw[j+1] == '\'' || raw[j+1] == '"') &&
                       raw.find(raw[j+1], j + 2) != std::string::npos &&
                       raw.find(raw[j+1], j + 2) + 1 < n && raw[raw.find(raw[j+1], j + 2) + 1] == '(') {
                // indirect method name: `"$x.'f'()"` / `"$x."f"()"` — parens required
                size_t qe = raw.find(raw[j+1], j + 2);
                var += raw.substr(j, qe + 1 - j);
                j = qe + 1;
                int d = 1; var += raw[j++];
                j = interpCallArgsEnd(raw, j, d, var);
                commit();
            } else if (j + 1 < n && raw[j] == '.' && (raw[j+1] == '[' || raw[j+1] == '{' || raw[j+1] == '(')) {
                var += raw[j++]; // .
                char open = raw[j], close = open == '[' ? ']' : open == '{' ? '}' : ')';
                int d = 1; var += raw[j++];
                while (j < n && d > 0) { if (raw[j]==open) d++; else if (raw[j]==close) d--; var += raw[j++]; }
                commit();
            } else if (j + 2 < n && raw[j + 2] == '.' && committedLen == var.size() &&
                       (((unsigned char)raw[j] == 0xC2 && (unsigned char)raw[j + 1] == 0xBB) || (raw[j] == '>' && raw[j + 1] == '>'))) {
                // a HYPER method call, `"@a».uc()"` / `"%h>>.sort.map(…)"`: the marker joins the
                // chain and the `.name` link after it decides, as any (rakudo#1460). Not after a
                // bare `.name` still pending: Rakudo leaves `"@a.sort».uc()"` literal.
                var.append(raw, j, 2); j += 2;
            } else if (j + 1 < n && raw[j] == '.' &&
                       // a method or routine name may be UNICODE: `"$n.&mööse()"`
                       // truncated at the ö, left the call uncommitted, and the
                       // `&name(` branch then compiled `mööse()` with NO invocant
                       (ascii::isalpha((unsigned char)raw[j+1]) || raw[j+1] == '_' ||
                        (unsigned char)raw[j+1] >= 0x80 ||
                        ((raw[j+1] == '^' || raw[j+1] == '?' || raw[j+1] == '&') && j + 2 < n &&
                         (ascii::isalpha((unsigned char)raw[j+2]) || raw[j+2] == '_' ||
                          (unsigned char)raw[j+2] >= 0x80)))) {
                // bare .method — tentative; consume its name, commit only if parens follow.
                // One META-SIGIL may sit between the dot and the name: `"$x.^name()"`
                // is a meta-method call, `.?meth()` a maybe-call, `.&f()` a sub call.
                // Without this the chain ended at `$x` and `.^name()` was literal text.
                var += raw[j++]; // .
                if (raw[j] == '^' || raw[j] == '?' || raw[j] == '&') var += raw[j++];
                for (size_t l; j < n && identContAt(j, l); ) { var.append(raw, j, l); j += l; }
                // a method name joins on `-`/`'` exactly as a variable name does:
                // "$n.is-prime()" — stopping at the hyphen left `.is` uncommitted
                // and printed `7.is-prime()` (Gnome::Gtk4, Pakku, REPL, Air…)
                while (identJoinsAt(j)) {
                    var += raw[j++];
                    for (size_t l; j < n && identContAt(j, l); ) { var.append(raw, j, l); j += l; }
                }
                if (j < n && raw[j] == '(') {
                    int d = 1; var += raw[j++];
                    j = interpCallArgsEnd(raw, j, d, var);
                    commit();
                }
                // else: leave uncommitted; a later link in the chain may still commit it
            } else break;
        }
        // drop any uncommitted trailing bare-method links back to literal text
        if (committedLen < var.size()) { var.resize(committedLen); j = committedJ; }
        return hadPostfix;
        };
        // `$/` (match) and `$!` (error) as standalone interpolated vars
        if (fS && c == '$' && (i + 1 < n) && (raw[i + 1] == '/' || raw[i + 1] == '!') &&
            !(i + 2 < n && (ascii::isalnum((unsigned char)raw[i + 2]) || raw[i + 2] == '_'))) {
            // …and it takes a postfix like any other variable: `$/[0]`, `$/<k>`,
            // `$/.Str`. Stopping at the sigil left the subscript as literal text,
            // so HTTP::Tinyish's `s/…/$/[0]/` replaced the match with the seven
            // characters `$/[0]`.
            std::string var = std::string("$") + raw[i + 1];
            size_t j = i + 2;
            scanChain(j, var); // a bare `.message` stays literal, as after any variable
            flush();
            if (var.size() == 2) result->parts.push_back(std::make_unique<VarExpr>(var));
            else try { result->parts.push_back(embedAt(i, var)); } catch (...) { rethrowIfObsolete(); lit += var; }
            i = j;
            continue;
        }
        // numbered regex captures `$0` `$1` … and named captures `$<name>`
        if (fS && c == '$' && (i + 1 < n) && (ascii::isdigit((unsigned char)raw[i + 1]) || raw[i + 1] == '<')) {
            size_t j = i + 1;
            std::string var("$");
            if (raw[j] == '<') {
                var += raw[j++];
                while (j < n && raw[j] != '>') var += raw[j++];
                if (j < n) var += raw[j++]; // closing >
            } else {
                while (j < n && ascii::isdigit((unsigned char)raw[j])) var += raw[j++];
            }
            scanChain(j, var); // postfix chain on the capture: $0[1] $<k>{...} $0.uc()
            flush();
            try { result->parts.push_back(embedAt(i, var)); } catch (...) { rethrowIfObsolete(); lit += var; }
            i = j;
            continue;
        }
        // `&name(args)` — a routine CALL interpolates (only with the parens):
        // Cro's route compiler builds "'&encode(@constraints[0])'" strings
        if (fF && c == '&' && i + 1 < n &&
            (ascii::isalpha((unsigned char)raw[i + 1]) || raw[i + 1] == '_')) {
            size_t j = i + 1;
            std::string fname;
            for (size_t l; j < n && identContAt(j, l); ) { fname.append(raw, j, l); j += l; }
            while (identJoinsAt(j)) {
                fname += raw[j++];
                for (size_t l; j < n && identContAt(j, l); ) { fname.append(raw, j, l); j += l; }
            }
            if (j < n && raw[j] == '(') {
                int depth = 1; size_t k2 = j + 1; std::string argsrc;
                while (k2 < n && depth > 0) {
                    if (raw[k2] == '(') depth++;
                    else if (raw[k2] == ')') { depth--; if (depth == 0) break; }
                    argsrc += raw[k2]; k2++;
                }
                flush();
                // The `&` goes back in. Re-parsing the call WITHOUT it made the
                // interpolated spelling mean something else than the same text
                // in code: `&` says "the routine of this name", and dropping it
                // let a TYPE of that name answer instead once a bare `Bar(…)`
                // started resolving to the type's coercion.
                try { result->parts.push_back(embedAt(i, "&" + fname + "(" + argsrc + ")")); }
                catch (...) { rethrowIfObsolete(); }
                i = k2 + 1;
                continue;
            }
            // `"@&code[]"` / `"%&code{}"` — a SIGIL contextualizer on the routine,
            // subscripted: `@(&code)[]` (S02-literals/string-interpolation.t)
            if (j < n && (raw[j] == '[' || raw[j] == '{') && !lit.empty() &&
                (lit.back() == '@' || lit.back() == '%') && (lit.back() == '@' ? fA : fH)) {
                char sig = lit.back();
                std::string var = std::string(1, sig) + "(&" + fname + ")";
                size_t k2 = j;
                if (scanChain(k2, var)) {
                    lit.pop_back();
                    flush();
                    try { result->parts.push_back(embedAt(i, var)); }
                    catch (...) { rethrowIfObsolete(); throw; }
                    i = k2;
                    continue;
                }
            }
            // bare &name without parens: stays literal text
        }
        // `"$( expr )"` — the item contextualizer interpolates its expression
        // (and a `my` inside it declares into the enclosing scope). Only `$`:
        // Rakudo leaves `@(…)`, `%(…)` and `&(…)` as literal text.
        if (c == '$' && fS && i + 1 < n && raw[i + 1] == '(') {
            size_t j = i + 2;
            std::string inner = scanInterpBlock(raw, j, '(', ')');
            if (j < n) {
                flush();
                try { result->parts.push_back(embedAt(i, "$(" + inner + ")")); }
                catch (...) { rethrowIfObsolete(); lit += raw.substr(i, j + 1 - i); }
                i = j + 1;
                continue;
            }
        }
        // `$:name` is the implicit NAMED placeholder parameter; it interpolates like
        // any other twigilled variable. (A bare `$:` is a compile error in Rakudo,
        // so requiring a name after the colon costs nothing.)
        bool colonPh = (i + 2 < n) && raw[i + 1] == ':' &&
                       (ascii::isalpha((unsigned char)raw[i + 2]) || raw[i + 2] == '_');
        if (((c == '$' && fS) || (c == '@' && fA) || (c == '%' && fH)) &&
            (i + 1 < n) && (ascii::isalpha((unsigned char)raw[i + 1]) || raw[i + 1] == '_' ||
                            // `%{…}` is NOT a hash interpolation: Rakudo leaves the
                            // '%' as text and interpolates the block, which is what
                            // makes "%{$width}s" a printf format rather than "s".
                            (raw[i + 1] == '{' && c != '%') ||
                            raw[i + 1] == '*' || raw[i + 1] == '!' ||
                            // `.` is the PUBLIC-ATTRIBUTE twigil, so an attribute
                            // NAME has to follow it. Accepting a bare `.` made
                            // `"%.{$n}g"` — the way a printf precision is built —
                            // parse as a hash subscript on an unnamed attribute,
                            // and the format string came back as just `"g"`:
                            // Astro::Sunrise's convergence test compared "g" with
                            // "g", declared success on the first pass, and its
                            // `:iter` mode returned midnight for every location.
                            (raw[i + 1] == '.' && i + 2 < n &&
                             (ascii::isalpha((unsigned char)raw[i + 2]) || raw[i + 2] == '_' ||
                              (unsigned char)raw[i + 2] >= 0x80)) ||
                            raw[i + 1] == '^' || colonPh ||
                            (raw[i + 1] == '?' && i + 2 < n &&
                             (ascii::isalpha((unsigned char)raw[i + 2]) || raw[i + 2] == '_')) ||
                            ((unsigned char)raw[i + 1] >= 0x80 &&
                             [&]{ size_t l; return identContAt(i + 1, l); }()))) {
            char sig = c;
            size_t j = i + 1;
            std::string var(1, sig);
            if (raw[j] == '{') {
                // ${ ... } (same quote-aware scan)
                j++;
                std::string inner = scanInterpBlock(raw, j);
                // …but `"${…}"` and `"@{…}"` are Perl 5 — a dereference, or a
                // variable name in braces — and Raku refuses both by name wherever
                // they are written, inside a string as much as outside one. A
                // bare name is `{$name}` in Raku; anything else `$(…)` or `$::(…)`.
                if (sig == '$' || sig == '@') {
                    std::string t = inner;
                    while (!t.empty() && (t.front() == ' ' || t.front() == '\t')) t.erase(t.begin());
                    while (!t.empty() && (t.back() == ' ' || t.back() == '\t')) t.pop_back();
                    bool word = !t.empty() && (ascii::isalpha((unsigned char)t[0]) || t[0] == '_');
                    for (char ch : t)
                        if (!(ascii::isalnum((unsigned char)ch) || ch == '_' || ch == '-')) { word = false; break; }
                    const std::string s1(1, sig);
                    const std::string old = s1 + "{" + (word ? t : inner) + "}";
                    const std::string repl = word ? "{" + s1 + t + "}"
                                                  : s1 + "(" + inner + ") for hard ref or " + s1 + "::(" + inner + ") for symbolic ref";
                    throw ParseError("Unsupported use of " + old + ". In Raku please use: " + repl + ".",
                                     pos_ > 0 ? toks_[pos_ - 1].line : cur().line, "X::Obsolete",
                                     {{"old", old}, {"replacement", repl}});
                }
                flush();
                try { result->parts.push_back(embedAt(i, std::string(1, sig) + "(" + inner + ")")); }
                catch (...) { rethrowIfObsolete(); }
                i = j + 1;
                continue;
            }
            // twigil ($*dyn, $!attr, $.attr, $^placeholder, $?LINE)
            if (raw[j] == '*' || raw[j] == '!' || raw[j] == '.' || raw[j] == '^' ||
                raw[j] == '?' || (raw[j] == ':' && colonPh)) var += raw[j++];
            for (size_t l; j < n && identContAt(j, l); ) { var.append(raw, j, l); j += l; }
            // hyphen/apostrophe continue the name when followed by an alphanumeric ($foo-bar)
            while (identJoinsAt(j)) {
                var += raw[j++];
                for (size_t l; j < n && identContAt(j, l); ) { var.append(raw, j, l); j += l; }
            }
            bool hadPostfix = scanChain(j, var);
            // @arr/%hash only interpolate when followed by a postcircumfix/method
            if ((sig == '@' || sig == '%') && !hadPostfix) { lit += var; i = j; continue; }
            const int strLine = pos_ > 0 ? toks_[pos_ - 1].line : cur().line;
            // `"$a->$b"` is Perl's arrow, refused in a string as anywhere (#109)
            if (j + 1 < n && raw[j] == '-' && raw[j + 1] == '>') {
                const std::string old = j + 2 < n && raw[j + 2] == '[' ? "->[]"
                                      : j + 2 < n && raw[j + 2] == '{' ? "->{}" : "->";
                const std::string repl = old == "->" ? "either . to call a method, or whitespace to have a pointy block"
                                                     : old == "->[]" ? "[] for array subscripting" : "{} for hash subscripting";
                throw ParseError("The arrow after " + var + " in a string is Perl's postfix " + old +
                                 ", which Raku does not have; use " + repl + ".",
                                 strLine, "X::Obsolete", {{"old", old + " as postfix"}, {"replacement", repl}});
            }
            // `"$h:Set(1)"`: a colon and a word after a plain `$name` continue
            // the NAME — `$h:Set<1>`, a long name with an adverb — which is no
            // variable in scope (#109). `"$h::x"` and `"$h: x"` are other things.
            if (sig == '$' && !hadPostfix && j + 1 < n && raw[j] == ':' &&
                (ascii::isalpha((unsigned char)raw[j + 1]) || raw[j + 1] == '_') &&
                var.size() > 1 && (nameLetterAt(var, 1) || var[1] == '_')) {
                size_t k = j + 1;
                std::string adv;
                for (size_t l; k < n && identContAt(k, l); ) { adv.append(raw, k, l); k += l; }
                std::string name = var + ":" + adv;
                if (k < n && (raw[k] == '(' || raw[k] == '<')) {
                    const char close = raw[k] == '(' ? ')' : '>';
                    size_t e = raw.find(close, k + 1);
                    if (e != std::string::npos) name += "<" + raw.substr(k + 1, e - k - 1) + ">";
                }
                throw ParseError("Variable '" + name + "' is not declared (in a string, a colon and a word "
                                 "after a variable continue its name; write {" + var + "}:" + adv + ")",
                                 strLine, "X::Undeclared", {{"symbol", name}, {"what", "Variable"}});
            }
            flush();
            try {
                ExprPtr part = embedAt(i, var);
                // a compile-time magical reads its NODE's line — stamp the
                // string's own position, not the sub-parser's line 1
                // ("line $?LINE" — Log::Async's context test)
                if (part && part->kind == NK::VarExpr && var.rfind("$?", 0) == 0)
                    static_cast<VarExpr*>(part.get())->line = lineAt(i);
                result->parts.push_back(std::move(part));
            } catch (...) { rethrowIfObsolete(); lit += var; }
            i = j;
            continue;
        }
        lit += c;
        i++;
    }
    flush();
    return result;
}

// `use trace`'s announcement of the statement in tokens [from, to): a call,
// run just before it, that writes `ID (FILE line N)` and its source text on
// $*ERR, as Rakudo's trace pragma does (rakudo#5546). The text is the source
// between the previous token and the statement's last, without the blanks
// and comments in front of it; without a source to cut it from, the tokens.
StmtPtr Parser::traceStmt(int id, size_t from, size_t to) {
    std::string text;
    if (to > from && to <= toks_.size() && src_) {
        const size_t b = from > 0 ? toks_[from - 1].off : 0, e = toks_[to - 1].off;
        if (b < e && e <= src_->size()) text = src_->substr(b, e - b);
        size_t k = 0;
        for (;;) {
            while (k < text.size() && ascii::isspace((unsigned char)text[k])) k++;
            if (k < text.size() && text[k] == '#') { while (k < text.size() && text[k] != '\n') k++; continue; }
            break;
        }
        text = text.substr(k);
        if (!text.empty() && text.back() == ';') text.pop_back();
    }
    if (text.empty())
        for (size_t k = from; k < to && k < toks_.size(); k++) text += (text.empty() ? "" : " ") + toks_[k].text;
    auto c = std::make_unique<Call>();
    c->name = "rakupp-trace";
    c->line = from < toks_.size() ? toks_[from].line : 0;
    c->args.push_back(std::make_unique<IntLit>(id));
    c->args.push_back(std::make_unique<StrLit>(srcFile_.empty() ? std::string("-e") : srcFile_));
    c->args.push_back(std::make_unique<IntLit>(c->line));
    c->args.push_back(std::make_unique<StrLit>(text));
    auto st = std::make_unique<ExprStmt>();
    st->line = c->line;
    st->e = std::move(c);
    return st;
}

} // namespace rakupp
