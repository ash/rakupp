#include "Lexer.h"
#include <cstdint>
#include "Parser.h"   // ParseError
#include "Unicode.h"  // uniGeneralCategory / uniNumValue
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace rakupp {

// Minimal POD → text render for `--doc`: a `=headN TEXT` / `=item TEXT` line keeps
// its text (the directive word is dropped); ordinary paragraph lines pass through.
static std::string renderPod(const std::string& content) {
    std::string out;
    std::istringstream ss(content);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line[0] == '=') {
            size_t sp = line.find_first_of(" \t");
            out += (sp == std::string::npos ? std::string() : line.substr(sp + 1));
            out += "\n";
        } else {
            out += line + "\n";
        }
    }
    return out;
}

// Roast "fudge" directives — `#?rakudo[.moar] [<N>] <verb> ['reason']` — mark the
// next N test statements for the Rakudo implementation. rakupp is a moar-like
// backend, so it honours bare `#?rakudo` and `#?rakudo.moar` (never `.jvm`/`.js…`).
// This is a faithful subset of roast's own `fudge` preprocessor:
//   todo  — rewrite the directive line into `todo('<reason>', N);` (marks next N tests)
//   skip  — comment out the next N test statements / column-0 `{…}` blocks, emitting
//           `skip('<reason>', numtests);` in front (numtests = test calls inside), so
//           the plan stays satisfied without running the guarded construct — some of
//           which even Rakudo can't compile (the fudger comments them out for it too)
//   emit  — replace the directive line with its argument code, verbatim
//   eval/try — treated as skip (they guard constructs that need EVAL protection)
//   #?DOES n — the next statement (or `sub NAME`) counts as n tests
// Line numbers are preserved throughout: one line in, one line out.
static const char* kFudgeTestFns[] = { // roast fudge's $IS list: statements starting
    "cmp_ok", "cmp-ok", "dies_ok", "dies-ok", "does-ok", // with these count as tests
    "eval_dies_ok", "eval-dies-ok", "eval_lives_ok", "eval-lives-ok", "flunk",
    "is", "is-approx", "is_deeply", "is-deeply", "isa_ok", "isa-ok", "isnt",
    "like", "lives_ok", "lives-ok", "nok", "ok", "pass",
    "throws_like", "throws-like", "unlike", "use_ok", "use-ok",
    "is_run", "is-run", "doesn't-hang", "doesn't-warn", "warns-like",
    "fails-like", "is-eqv", "is-path", "is-deeply-junction", "test-iter-opt",
    "throws-like-any", "run-with-tty", "no-fatal-throws-like", "tap-ok",
    "loads_ok", "loads-ok", "precomp_loads_ok", "precomp-loads-ok",
    "loads_is", "loads-is", "precomp_loads_is", "precomp-loads-is",
    "is-perl-idempotent", "is-primed-sig", "is-primed-call", "priming-fails-bind-ok",
};
// The leading identifier of a statement line (test-function names may contain - and ').
static std::string fudgeLeadingWord(const std::string& line) {
    size_t p = 0; while (p < line.size() && (line[p] == ' ' || line[p] == '\t')) p++;
    size_t s = p;
    while (p < line.size() && (std::isalnum((unsigned char)line[p]) || line[p] == '_' ||
                               line[p] == '-' || line[p] == '\'')) p++;
    return line.substr(s, p - s);
}
static std::string applyRakudoFudge(const std::string& src) {
    if (src.find("#?rakudo") == std::string::npos && src.find("#?DOES") == std::string::npos)
        return src; // fast path: no directives
    static const std::set<std::string> testFns(std::begin(kFudgeTestFns), std::end(kFudgeTestFns));

    // Split into lines (without terminators) once; reassemble at the end.
    std::vector<std::string> lines;
    { size_t i = 0;
      while (i <= src.size()) {
          size_t eol = src.find('\n', i);
          if (eol == std::string::npos) { lines.push_back(src.substr(i)); break; }
          lines.push_back(src.substr(i, eol - i));
          i = eol + 1;
      } }

    auto indentOf = [](const std::string& l) {
        size_t p = 0; while (p < l.size() && (l[p] == ' ' || l[p] == '\t')) p++; return p;
    };
    auto rtrim = [](std::string s) {
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.pop_back();
        return s;
    };
    // does a (joined) statement line end in `;` (allowing a trailing # comment)?
    auto endsStatement = [&](const std::string& l) {
        std::string t = rtrim(l);
        size_t h = t.rfind('#');
        if (h != std::string::npos && h > 0 && t.find(';', 0) != std::string::npos) {
            std::string beforeHash = rtrim(t.substr(0, h));
            if (!beforeHash.empty() && beforeHash.back() == ';') return true;
        }
        return !t.empty() && t.back() == ';';
    };
    // count of test calls in a range of lines (fudge: lines starting with a test fn)
    std::map<std::string, int> doesMap; // sub name -> tests-per-call (from #?DOES)
    auto countTests = [&](size_t from, size_t to) {
        int nt = 0;
        for (size_t k = from; k < to && k < lines.size(); k++) {
            std::string w = fudgeLeadingWord(lines[k]);
            if (testFns.count(w)) nt++;
            else if (doesMap.count(w)) nt += doesMap[w];
        }
        return nt;
    };

    int pending = 0;          // fudged statements still to process
    std::string verb, reason; // active directive
    int does = 0;             // pending `#?DOES n` for the next statement / sub
    bool fudged = false;

    for (size_t li = 0; li < lines.size(); li++) {
        std::string& line = lines[li];
        size_t p = indentOf(line);

        // ---- #?DOES n : next statement (or sub definition) counts as n tests
        if (line.compare(p, 6, "#?DOES") == 0) {
            size_t q = p + 6; while (q < line.size() && (line[q] == ':' || line[q] == ' ' || line[q] == '\t')) q++;
            does = std::atoi(line.c_str() + q);
            continue; // the line stays (it's a comment)
        }
        // ---- register `#?DOES n` on a following sub/multi/proto definition
        if (does > 0 && pending == 0) {
            std::string w = fudgeLeadingWord(line);
            if (w == "sub" || w == "multi" || w == "proto") {
                std::string rest = line.substr(indentOf(line) + w.size());
                // strip further declarator keywords: `multi sub name`, `proto sub name`
                std::string w2 = fudgeLeadingWord(rest);
                if (w2 == "sub" || w2 == "method") { size_t o = rest.find(w2); rest = rest.substr(o + w2.size()); }
                std::string nm = fudgeLeadingWord(rest);
                if (!nm.empty()) { doesMap[nm] = does; does = 0; continue; }
            }
        }

        // ---- directive lines: #?rakudo[.backend] [N] verb [reason]
        if (line.compare(p, 8, "#?rakudo") == 0) {
            size_t q = p + 8;
            std::string backend;
            if (q < line.size() && line[q] == '.') {
                q++; size_t b = q;
                while (q < line.size() && line[q] != ' ' && line[q] != '\t') q++;
                backend = line.substr(b, q - b);
            }
            if (!backend.empty() && backend != "moar") continue; // other backend: ignore
            while (q < line.size() && (line[q] == ' ' || line[q] == '\t')) q++;
            std::string count = "1";
            if (q < line.size() && std::isdigit((unsigned char)line[q])) {
                size_t c = q; while (q < line.size() && std::isdigit((unsigned char)line[q])) q++;
                count = line.substr(c, q - c);
                while (q < line.size() && (line[q] == ' ' || line[q] == '\t')) q++;
            }
            std::string v = fudgeLeadingWord(line.substr(q));
            size_t vq = q + v.size();
            while (vq < line.size() && (line[vq] == ' ' || line[vq] == '\t')) vq++;
            std::string arg = rtrim(line.substr(std::min(vq, line.size())));
            if (v == "todo") {
                if (arg.empty() || (arg[0] != '\'' && arg[0] != '"')) arg = "\"\"";
                line = line.substr(0, p) + "todo(" + arg + ", " + count + ");";
                fudged = true;
            }
            else if (v == "skip" || v == "eval" || v == "try") {
                pending = std::atoi(count.c_str());
                verb = "skip";
                if (arg.empty()) reason = "\"\"";
                else if (arg[0] == '\'' || arg[0] == '"') reason = arg;
                else { // unquoted reason: quote it defensively
                    std::string esc; for (char c : arg) { if (c == '"' || c == '\\') esc += '\\'; esc += c; }
                    reason = "\"" + esc + "\"";
                }
                fudged = true;
            }
            else if (v == "emit") {
                line = line.substr(0, p) + arg; // paste the emitted code, same line
                fudged = true;
            }
            continue;
        }

        if (pending <= 0) { does = 0; continue; }

        // ---- we are looking for the next statement/block to skip
        std::string t = rtrim(line);
        if (t.empty()) continue;                       // blank: pass through
        { size_t q = indentOf(line);
          if (q < line.size() && line[q] == '#') continue; } // comment: pass through

        if (!line.empty() && line[0] == '{') {
            // column-0 block: consume through the column-0 closing brace
            size_t end = li + 1;
            while (end < lines.size() && !(lines[end].size() && lines[end][0] == '}')) end++;
            int nt = countTests(li + 1, end);
            if (does) nt = does;
            if (nt < 1) nt = 1;
            for (size_t k = li; k <= end && k < lines.size(); k++) lines[k] = "# " + lines[k];
            lines[li] = "skip(" + reason + ", " + std::to_string(nt) + "); " + lines[li];
            li = std::min(end, lines.size() - 1);
            pending--; does = 0; fudged = true;
            continue;
        }

        // plain statement: join lines until one ends with `;` (bounded)
        size_t end = li;
        while (end < lines.size() && !endsStatement(lines[end]) && end - li < 49) end++;
        std::string w = fudgeLeadingWord(line);
        int nt = doesMap.count(w) ? doesMap[w] : (does ? does : (testFns.count(w) ? 1 : 0));
        if (nt > 0) {
            for (size_t k = li; k <= end && k < lines.size(); k++) lines[k] = "# " + lines[k];
            lines[li] = "skip(" + reason + ", " + std::to_string(nt) + "); " + lines[li];
            pending--;
        }
        // not a test statement: leave it running (matches roast's fudge), keep looking
        li = std::min(end, lines.size() ? lines.size() - 1 : 0);
        does = 0;
    }

    if (!fudged) return src;
    std::string out; out.reserve(src.size() + 256);
    for (size_t k = 0; k < lines.size(); k++) { if (k) out += '\n'; out += lines[k]; }
    return out;
}

Lexer::Lexer(std::string src) : src_(applyRakudoFudge(std::move(src))) {}

char Lexer::peek(size_t off) const {
    size_t p = pos_ + off;
    return p < src_.size() ? src_[p] : '\0';
}

char Lexer::advance() {
    char c = src_[pos_++];
    if (c == '\n') { line_++; col_ = 1; } else { col_++; }
    return c;
}

bool Lexer::match(char c) {
    if (peek() == c) { advance(); return true; }
    return false;
}

static int utf8Len(unsigned char b); // fwd (defined below)
static bool isLetterCP(uint32_t cp); // fwd
static bool isIdentMarkCP(uint32_t cp); // fwd
static bool isIdentCont(char c);     // fwd

uint32_t Lexer::codepointHere() const {
    if (pos_ >= src_.size()) return 0;
    unsigned char b0 = (unsigned char)src_[pos_];
    int n = utf8Len(b0);
    if (n == 1) return b0;
    uint32_t cp = b0 & (0xFF >> (n + 1));
    for (int i = 1; i < n; i++) {
        if (pos_ + i >= src_.size()) return 0;
        cp = (cp << 6) | ((unsigned char)src_[pos_ + i] & 0x3F);
    }
    return cp;
}

bool Lexer::unicodeLetterHere() const {
    unsigned char b = (unsigned char)peek();
    if (b < 0x80) return false;
    return isLetterCP(codepointHere());
}

// Translate one superscript codepoint to its ASCII digit/sign, or 0 if not a superscript.
static char superscriptChar(uint32_t cp) {
    switch (cp) {
        case 0x2070: return '0'; case 0x00B9: return '1'; case 0x00B2: return '2';
        case 0x00B3: return '3'; case 0x2074: return '4'; case 0x2075: return '5';
        case 0x2076: return '6'; case 0x2077: return '7'; case 0x2078: return '8';
        case 0x2079: return '9'; case 0x207B: return '-'; case 0x207A: return '+';
        default: return 0;
    }
}

bool Lexer::tryReadSuperscript(std::string& digits) {
    while (true) {
        if (pos_ >= src_.size() || (unsigned char)peek() < 0x80) break;
        char d = superscriptChar(codepointHere());
        if (!d) break;
        digits += d;
        int n = utf8Len((unsigned char)peek());
        for (int i = 0; i < n && !eof(); i++) advance();
    }
    return !digits.empty();
}

void Lexer::consumeIdentChars(std::string& name) {
    for (;;) {
        if (isIdentCont(peek())) { name += advance(); continue; }
        if (unicodeLetterHere() || ((unsigned char)peek() >= 0x80 && isIdentMarkCP(codepointHere()))) {
            int n = utf8Len((unsigned char)peek());
            for (int i = 0; i < n && !eof(); i++) name += advance();
            continue;
        }
        break;
    }
}

Token Lexer::make(Tok k, const std::string& t) {
    Token tk;
    tk.kind = k;
    tk.text = t;
    tk.line = line_;
    tk.col = col_;
    return tk;
}

static bool isIdentStart(char c) { return std::isalpha((unsigned char)c) || c == '_'; }
static bool isIdentCont(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

// UTF-8: byte length of the codepoint led by byte b
static int utf8Len(unsigned char b) {
    if (b < 0x80) return 1;
    if ((b >> 5) == 0x6) return 2;
    if ((b >> 4) == 0xE) return 3;
    if ((b >> 3) == 0x1E) return 4;
    return 1;
}
// Whitelist of Unicode codepoint ranges usable in identifiers (letters only).
// Deliberately excludes math operators (∪∩∈⊆≅ U+22xx), guillemets «» , superscripts
// (U+00B2/B3/B9, U+2070–209F) and other symbols, which must remain operators.
static bool isLetterCP(uint32_t cp) {
    return (cp >= 0x00C0 && cp <= 0x024F) ||   // Latin-1 + Latin Extended-A/B
           (cp >= 0x0250 && cp <= 0x02AF) ||   // IPA extensions
           (cp >= 0x0370 && cp <= 0x03FF) ||   // Greek (π α β γ τ …)
           (cp >= 0x0400 && cp <= 0x04FF) ||   // Cyrillic
           (cp >= 0x0531 && cp <= 0x058F) ||   // Armenian
           (cp >= 0x05D0 && cp <= 0x05EA) ||   // Hebrew letters
           (cp >= 0x0620 && cp <= 0x064A) ||   // Arabic letters
           (cp >= 0x1D00 && cp <= 0x1DBF) ||   // phonetic extensions
           (cp >= 0x1E00 && cp <= 0x1FFF) ||   // Latin Extended Additional + Greek Extended
           (cp >= 0x2100 && cp <= 0x214F) ||   // letterlike symbols (ℓ ℮ …)
           (cp >= 0x3040 && cp <= 0x30FF) ||   // Hiragana / Katakana
           (cp >= 0x3400 && cp <= 0x9FFF) ||   // CJK
           (cp >= 0xAC00 && cp <= 0xD7A3) ||   // Hangul
           (cp >= 0xFB00 && cp <= 0xFB4F) ||   // alphabetic presentation forms (ﬁ ﬂ …)
           (cp >= 0xFF21 && cp <= 0xFF3A) || (cp >= 0xFF41 && cp <= 0xFF5A) || // fullwidth A-Z a-z (ｆｏｏ)
           (cp >= 0xFF66 && cp <= 0xFFDC) ||   // halfwidth katakana/hangul letters
           (cp >= 0x10000);                     // astral letters (emoji/rare scripts, best effort)
}
// Combining marks are valid identifier-CONTINUE characters (e.g. $ẛ̣).
static bool isIdentMarkCP(uint32_t c) {
    return (c >= 0x0300 && c <= 0x036F) || (c >= 0x0483 && c <= 0x0489) ||
           (c >= 0x0591 && c <= 0x05BD) || (c >= 0x0610 && c <= 0x061A) ||
           (c >= 0x064B && c <= 0x065F) || (c >= 0x0300 && c <= 0x0489) ||
           (c >= 0x1AB0 && c <= 0x1AFF) || (c >= 0x1DC0 && c <= 0x1DFF) ||
           (c >= 0x20D0 && c <= 0x20FF) || (c >= 0xFE20 && c <= 0xFE2F);
}

void Lexer::skipWhitespaceAndComments() {
    for (;;) {
        char c = peek();
        bool atLineStart = (col_ == 1);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance();
            continue;
        }
        // atomic-operator marker ⚛ (U+269B): under the GIL, atomic ops are plain ops,
        // so `$x⚛++`/`⚛$x`/`$x ⚛= v` reduce to `$x++`/`$x`/`$x = v` — just drop the ⚛.
        // Remember where it ended: dropping it must NOT count as whitespace, or the
        // following `++` loses its tight postfix position ($x⚛++ became $x ++).
        if ((unsigned char)c == 0xE2 && (unsigned char)peek(1) == 0x9A && (unsigned char)peek(2) == 0x9B) {
            advance(); advance(); advance();
            atomDropEnd_ = pos_;
            continue;
        }
        // unspace: backslash followed by whitespace is ignored (token-joining)
        if (c == '\\' && (peek(1) == ' ' || peek(1) == '\t' || peek(1) == '\n' || peek(1) == '\r')) {
            advance();
            continue;
        }
        if (c == '#') {
            // embedded comment #`( ... ) / #`[ ... ] / #`{ ... }: skip the balanced
            // bracket group only — the rest of the line still parses. The declarator
            // comments #|[ ... ] / #=[ ... ] take the same multi-line bracket forms.
            if ((peek(1) == '`' || peek(1) == '|' || peek(1) == '=') &&
                (peek(2) == '(' || peek(2) == '[' || peek(2) == '{' || peek(2) == '<')) {
                char open = peek(2), close = open == '(' ? ')' : open == '[' ? ']' : open == '{' ? '}' : '>';
                advance(); advance(); advance(); // # ` open
                int d = 1;
                while (!eof() && d > 0) {
                    char ch = advance();
                    if (ch == open) d++;
                    else if (ch == close) d--;
                }
                continue;
            }
            while (!eof() && peek() != '\n') advance();
            continue;
        }
        // POD handling: directives begin with '=' at column 1
        if (atLineStart && c == '=' && isIdentStart(peek(1))) {
            // read the directive word
            size_t save = pos_;
            std::string word;
            advance(); // '='
            while (isIdentCont(peek())) word += advance();
            if (word == "begin") {
                // skip rest of line, capture block name
                std::string name;
                while (peek() == ' ' || peek() == '\t') advance();
                while (isIdentCont(peek())) name += advance();
                while (!eof() && peek() != '\n') advance();
                bool capture = (name == "pod"); // collect pod block content for `$=pod` / --doc
                size_t contentStart = eof() ? pos_ : pos_ + 1; // just after this line's newline
                // skip until the matching =end <name> (nested =begin/=end of other names are skipped over)
                for (;;) {
                    if (eof()) break;
                    if (col_ == 1 && peek() == '=' ) {
                        size_t s2 = pos_;
                        advance();
                        std::string w2;
                        while (isIdentCont(peek())) w2 += advance();
                        if (w2 == "end") {
                            std::string endName;
                            while (peek() == ' ' || peek() == '\t') advance();
                            while (isIdentCont(peek())) endName += advance();
                            if (endName == name) { // matching close
                                if (capture) podData_ += renderPod(src_.substr(contentStart, s2 - contentStart));
                                while (!eof() && peek() != '\n') advance();
                                break;
                            }
                            // =end of a different (nested) block — keep skipping
                        }
                        pos_ = s2; // restore; consume the line below
                    }
                    while (!eof() && peek() != '\n') advance();
                    if (!eof()) advance();
                }
                continue;
            } else if (word == "finish") {
                // rest of file (after this line) is the $=finish data block
                while (!eof() && peek() != '\n') advance();
                if (!eof()) advance(); // consume the newline after =finish
                finishData_ = src_.substr(pos_);
                pos_ = src_.size();
                continue;
            } else {
                // A paragraph/abbreviated directive (`=for X`, `=head1 …`, `=foo …`):
                // skip the directive line AND the paragraph body that follows it, up to
                // the next blank line — otherwise that text leaks out as code.
                while (!eof() && peek() != '\n') advance();
                if (!eof()) advance(); // directive line's newline
                for (;;) {
                    if (eof()) break;
                    // blank line? (whitespace-only up to newline) → stop, leave it
                    size_t p = pos_; bool blank = true;
                    while (p < src_.size() && src_[p] != '\n') { if (src_[p] != ' ' && src_[p] != '\t') { blank = false; break; } p++; }
                    if (blank) break;
                    while (!eof() && peek() != '\n') advance();
                    if (!eof()) advance();
                }
                continue;
            }
            (void)save;
        }
        break;
    }
}

// Decimal-digit (Unicode general category Nd) value of a codepoint, or -1.
// Nd codepoints occur in contiguous runs of ten (0..9) per script; the table
// lists each run's starting codepoint (the script's digit zero).
static int ndDigitValue(uint32_t cp) {
    static const uint32_t zeros[] = {
        0x0030, 0x0660, 0x06F0, 0x07C0, 0x0966, 0x09E6, 0x0A66, 0x0AE6, 0x0B66,
        0x0BE6, 0x0C66, 0x0CE6, 0x0D66, 0x0DE6, 0x0E50, 0x0ED0, 0x0F20, 0x1040,
        0x1090, 0x17E0, 0x1810, 0x1946, 0x19D0, 0x1A80, 0x1A90, 0x1B50, 0x1BB0,
        0x1C40, 0x1C50, 0xA620, 0xA8D0, 0xA900, 0xA9D0, 0xA9F0, 0xAA50, 0xABF0,
        0xFF10, 0x104A0, 0x10D30, 0x11066, 0x110F0, 0x11136, 0x111D0, 0x112F0,
        0x11450, 0x114D0, 0x11650, 0x116C0, 0x11730, 0x118E0, 0x11950, 0x11C50,
        0x11D50, 0x11DA0, 0x16A60, 0x16B50, 0x1D7CE, 0x1D7D8, 0x1D7E2, 0x1D7EC,
        0x1D7F6, 0x1E140, 0x1E2F0, 0x1E950, 0x1FBF0,
    };
    for (uint32_t z : zeros) if (cp >= z && cp <= z + 9) return (int)(cp - z);
    return -1;
}

// Numeric value of a standalone numeral in category Nl (letter-numbers: Roman,
// cuneiform, …) or No (other-numbers: fractions, circled, Ethiopic, …). Unlike Nd
// digits these do not combine positionally; each is a complete literal. Returns
// true and sets num/den (den>0) when `cp` is such a numeral.
static bool unicodeNumeralValue(uint32_t cp, long long& num, long long& den) {
    // Any Nl/No numeral with a UCD numeric value (⓷ ❷ ⒌ ㊄ Ⅳ ½ …) is a
    // standalone literal. Decimal (Nd) digits combine positionally instead,
    // so they are excluded here and handled by the digit path.
    if (cp < 0x80) return false;
    if (ndDigitValue(cp) >= 0) return false;
    return uniNumValue(cp, num, den);
}

// Fullwidth ASCII letter (Ａ-Ｚ / ａ-ｚ, U+FF21.., U+FF41..) folded to ASCII, or 0.
static char fullwidthLetter(uint32_t cp) {
    if (cp >= 0xFF21 && cp <= 0xFF3A) return (char)('A' + (cp - 0xFF21));
    if (cp >= 0xFF41 && cp <= 0xFF5A) return (char)('a' + (cp - 0xFF41));
    return 0;
}

Token Lexer::lexNumber() {
    std::string num;
    bool isFloat = false;
    // A standalone Nl/No numeral (Roman ↈ, fraction ⅐, Tibetan half ༳, …) is a
    // complete literal on its own — consume exactly this codepoint.
    if ((unsigned char)peek() >= 0x80) {
        long long nn, dd;
        if (unicodeNumeralValue(codepointHere(), nn, dd)) {
            std::string orig; // keep the numeral's own spelling as the token text, so
                              // re-joined source (`:36<utfⅧ>`) still shows the Nl/No char
            for (int k = utf8Len((unsigned char)peek()); k > 0; k--) orig += advance();
            // Nl/No numerals do not combine as digits: another numeral/digit
            // immediately after is malformed (e.g. `𒐀𒐀`, `⓿⓿`).
            if ((unsigned char)peek() >= 0x80) {
                uint32_t nx = codepointHere(); long long a, b;
                if (ndDigitValue(nx) >= 0 || unicodeNumeralValue(nx, a, b))
                    throw ParseError("Malformed numeric literal", line_);
            }
            if (dd == 1) { Token t = make(Tok::IntLit, orig); t.ival = nn; return t; }
            Token t = make(Tok::NumLit, orig); // fractional numeral (½): a Rat
            t.nval = (double)nn / (double)dd; t.flag = true; t.ival = nn; t.text2 = std::to_string(dd);
            return t;
        }
    }
    // Consume one decimal digit — ASCII or any Unicode Nd digit (folded to ASCII).
    // Returns false if the current char is not a decimal digit.
    auto takeDigit = [&](std::string& out) -> bool {
        char c = peek();
        if (std::isdigit((unsigned char)c)) { out += advance(); return true; }
        if ((unsigned char)c >= 0x80) {
            int v = ndDigitValue(codepointHere());
            if (v >= 0) { out += (char)('0' + v); for (int k = utf8Len((unsigned char)c); k > 0; k--) advance(); return true; }
        }
        return false;
    };
    if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'o' || peek(1) == 'b' || peek(1) == 'd') &&
        // `0x` with no digit at all is not a radix literal: `:0x` is the pair
        // shorthand x => 0, and `0x` followed by punctuation lexes as 0 then x
        (std::isalnum((unsigned char)peek(2)) || peek(2) == '_' || (unsigned char)peek(2) >= 0x80)) {
        char base = peek(1);
        advance(); advance();
        std::string digits;
        bool malformed = false;
        // Consume the whole contiguous digit "word": ASCII alnum, Nd digits, and
        // fullwidth ASCII letters (folded). Any other attached letter/numeral (an
        // Nl/No numeral or a non-radix script — `0xC𐏓FE`, `0b¹0`, `0xΓαfe`) is
        // consumed too but marks the literal malformed (X::Syntax::Confused).
        while (true) {
            char c = peek();
            if (c == '_') { advance(); continue; }
            if ((unsigned char)c < 0x80) {
                if (isIdentCont(c)) { digits += advance(); continue; }
                break;
            }
            uint32_t cp = codepointHere();
            int v = ndDigitValue(cp); char fw = fullwidthLetter(cp);
            if (v >= 0) digits += (char)('0' + v);
            else if (fw) digits += fw;
            else {
                std::string gc = uniGeneralCategory(cp);
                if (!gc.empty() && (gc[0] == 'L' || gc[0] == 'N')) malformed = true; // attached letter/numeral
                else break; // punctuation/space ends the literal
            }
            for (int k = utf8Len((unsigned char)c); k > 0; k--) advance();
        }
        int b = base == 'x' ? 16 : base == 'o' ? 8 : base == 'd' ? 10 : 2;
        // No valid digit after the 0x/0o/0b prefix — e.g. an Nl/No numeral or a
        // non-radix script (`0b¹0`, `0xΓαfe`) — is a malformed literal.
        if (digits.empty() || malformed) throw ParseError("Malformed radix number", line_);
        Token t = make(Tok::IntLit, digits);
        t.ival = std::strtoll(digits.c_str(), nullptr, b);
        return t;
    }
    while (takeDigit(num) || peek() == '_') { if (peek() == '_') advance(); }
    bool hasDot = false, hasExp = false;
    if (peek() == '.' && std::isdigit((unsigned char)peek(1))) {
        isFloat = true; hasDot = true;
        num += advance(); // .
        while (takeDigit(num) || peek() == '_') { if (peek() == '_') advance(); }
    }
    if (peek() == 'e' || peek() == 'E') {
        isFloat = true; hasExp = true;
        num += advance();
        if (peek() == '+' || peek() == '-') num += advance();
        while (std::isdigit((unsigned char)peek())) num += advance();
    }
    // imaginary literal suffix: 4i, 2.5i  (not part of an identifier like `4info`)
    if (peek() == 'i' && !isIdentCont(peek(1))) {
        advance();
        Token t = make(Tok::NumLit, num + "i");
        t.nval = std::strtod(num.c_str(), nullptr);
        return t;
    }
    if (isFloat) {
        Token t = make(Tok::NumLit, num);
        t.nval = std::strtod(num.c_str(), nullptr);
        t.flag = (hasDot && !hasExp); // a decimal literal with no exponent is a Rat (3.14), not a Num
        return t;
    }
    // A decimal integer written with a leading 0 (`069`) does not mean octal in
    // Raku; warn like Rakudo does, suggesting the 0o form when the digits are octal.
    if (num.size() > 1 && num[0] == '0' && !warnedLeadingZero_) {
        warnedLeadingZero_ = true;
        bool allOctal = true;
        for (char c : num) if (c < '0' || c > '7') { allOctal = false; break; }
        std::string suggest = allOctal ? ("0o" + num.substr(1)) : "0o";
        std::cerr << "Potential difficulties:\n    Leading 0 does not indicate octal in Raku; "
                  << "please use " << suggest << " if you meant that.\n";
    }
    Token t = make(Tok::IntLit, num);
    t.ival = std::strtoll(num.c_str(), nullptr, 10);
    return t;
}

Token Lexer::lexQuoted(char quote) {
    advance(); // opening quote
    std::string raw;
    // A single-quoted string is normally a plain StrLit. The `\qq[…]` (and `\q[…]`)
    // escape embeds an interpolating fragment inside it, which promotes the whole
    // token to StrInterp; once promoted, the literal (non-fragment) text must be
    // escaped so its own $ @ % & { \ are NOT treated as interpolation triggers.
    bool sqInterp = false;
    auto escInterp = [](const std::string& s) {
        std::string o;
        for (char ch : s) { if (strchr("\\$@%&{", ch)) o += '\\'; o += ch; }
        return o;
    };
    auto litAppend = [&](char ch) { if (sqInterp) raw += escInterp(std::string(1, ch)); else raw += ch; };
    while (!eof() && peek() != quote) {
        char c = advance();
        if (c == '\\') {
            char n = peek();
            if (quote == '\'') {
                // \qq[…] / \q[…] : an embedded interpolation escape. The bracketed
                // text (any of [] {} () <> delimiters) is processed as double-quoted.
                if (n == 'q') {
                    size_t k = (peek(1) == 'q') ? 2 : 1;   // \qq vs \q
                    char open = peek(k);
                    const char* opens = "[{(<", *closes = "]})>", *pp = open ? strchr(opens, open) : nullptr;
                    if (pp) {
                        char close = closes[pp - opens];
                        for (size_t j = 0; j <= k; j++) advance(); // consume q[q] + opening bracket
                        if (!sqInterp) { raw = escInterp(raw); sqInterp = true; }
                        int depth = 1;
                        while (!eof() && depth > 0) {
                            char b = peek();
                            if (b == open) depth++;
                            else if (b == close && --depth == 0) { advance(); break; }
                            raw += advance(); // fragment text stays raw for the interp parser
                        }
                        continue;
                    }
                }
                // single quotes: only \' and \\ are special
                if (n == '\'' || n == '\\') { litAppend(advance()); }
                else { raw += sqInterp ? "\\\\" : "\\"; }
            } else {
                // double quotes: keep escape raw, parser/interp resolves
                raw += '\\';
                if (!eof()) raw += advance();
            }
        } else if (c == '{' && quote != '\'') {
            // Interpolation code block: capture the balanced { … } as a unit so a
            // nested string ("…"/'…') inside it doesn't prematurely close the outer
            // string. The interpolation parser re-lexes the captured content later.
            raw += c;
            int depth = 1;
            while (!eof() && depth > 0) {
                char b = advance();
                raw += b;
                if (b == '\\') { if (!eof()) raw += advance(); continue; }
                if (b == '{') depth++;
                else if (b == '}') depth--;
                else if (b == '"' || b == '\'') { // skip a nested string literal whole
                    char qq = b;
                    while (!eof() && peek() != qq) {
                        char s = advance(); raw += s;
                        if (s == '\\' && !eof()) raw += advance();
                    }
                    if (!eof()) raw += advance();
                }
            }
        } else {
            litAppend(c);
        }
    }
    if (!eof()) advance(); // closing quote
    return make((quote == '\'' && !sqInterp) ? Tok::StrLit : Tok::StrInterp, raw);
}

bool Lexer::tryQuoteForm(Token& out) {
    size_t p = pos_;
    std::string w;
    while (p < src_.size() && std::isalpha((unsigned char)src_[p])) { w += src_[p]; p++; }
    bool isRegex = (w == "rx" || w == "m" || w == "ms" || w == "mm");
    bool isSubst = (w == "s" || w == "S" || w == "ss" || w == "SS");
    bool isTrans = (w == "tr" || w == "y"); // transliteration tr/from/to/
    bool isWords = (w == "qw" || w == "Qw" || w == "qqw" || w == "qww"); // word-list quotes
    if (w != "q" && w != "qq" && w != "Q" && !isRegex && !isSubst && !isWords && !isTrans) return false;
    // adverbs between keyword and delimiter, e.g. m:i/.../ , s:g/.../.../
    std::string adverbs;
    while (p < src_.size() && src_[p] == ':') {
        size_t q = p + 1;
        std::string a;
        if (q < src_.size() && src_[q] == '!') a += src_[q++]; // negated adverb (:!s)
        while (q < src_.size() && std::isalnum((unsigned char)src_[q])) a += src_[q++];
        if (a.empty() || a == "!") break;
        std::string arg; // parenthesized adverb argument: :x(2), :nth(3), :nth(1,3,5)
        if (q < src_.size() && src_[q] == '(') {
            int d = 0;
            do { char c = src_[q]; if (c == '(') d++; else if (c == ')') d--; arg += c; q++; } while (q < src_.size() && d > 0);
        }
        adverbs += ":" + a + arg + " ";
        p = q;
    }
    if (w == "ss" || w == "SS") adverbs = ":samespace " + adverbs; // ss/// == s:samespace///
    // whitespace is allowed before a bracketing delimiter: `s:g [ pat ] = repl`
    { size_t ws = p; while (ws < src_.size() && (src_[ws] == ' ' || src_[ws] == '\t')) ws++;
      if (ws < src_.size() && (src_[ws] == '(' || src_[ws] == '[' || src_[ws] == '{' || src_[ws] == '<')) p = ws; }
    if (p >= src_.size()) return false;
    // guillemet-delimited quote: Q«…» / Q««…»» (double «« matches »», so a single
    // » may appear inside). q interpolates nothing extra; qq interpolates.
    if (!isRegex && !isSubst && !isTrans && !isWords &&
        p + 1 < src_.size() && (unsigned char)src_[p] == 0xC2 && (unsigned char)src_[p + 1] == 0xAB) {
        while (pos_ < p) advance(); // consume the keyword (+ adverbs/whitespace)
        int opens = 0;
        while ((unsigned char)peek() == 0xC2 && (unsigned char)peek(1) == 0xAB) { advance(); advance(); opens++; }
        std::string raw;
        while (!eof()) {
            if ((unsigned char)peek() == 0xC2 && (unsigned char)peek(1) == 0xBB) {
                int closes = 0;
                while (closes < opens && (unsigned char)peek() == 0xC2 && (unsigned char)peek(1) == 0xBB) {
                    advance(); advance(); closes++;
                }
                if (closes == opens) break;                          // matched full run: done
                for (int k = 0; k < closes; k++) raw += "\xC2\xBB"; // shorter run is content
                continue;
            }
            raw += advance();
        }
        out = make(w == "qq" ? Tok::StrInterp : Tok::StrLit, raw);
        return true;
    }
    char d = src_[p];
    char close;
    bool bracket = true;
    switch (d) {
        case '(': close = ')'; break;
        case '{': close = '}'; break;
        case '[': close = ']'; break;
        case '<': close = '>'; break;
        case '/': case '|': case '!': close = d; bracket = false; break;
        case '\'': case '"': // q'…' / q:to'END' — quote or heredoc terminator in quotes
            if (isSubst || isTrans) return false; // s'…' isn't a substitution delimiter here
            close = d; bracket = false; break;
        default: return false;
    }
    // A bracketed substitution needs TWO groups: s(pat)(repl) / S[a][b], OR the
    // assignment form s[pat] = repl. If neither follows, this is really a call like
    // S(5) or s($x) — not a substitution — so let it lex as an identifier instead.
    bool assignForm = false;
    std::string assignOp; // s[pat] OP= repl : "" = plain `=`, else the op (`+`, `x`, …)
    if (isSubst && bracket) {
        int depth = 0; size_t q = p;
        for (; q < src_.size(); q++) {
            if (src_[q] == d) depth++;
            else if (src_[q] == close) { depth--; if (depth == 0) { q++; break; } }
        }
        while (q < src_.size() && std::isspace((unsigned char)src_[q])) q++;
        // any assignment operator: `=`, `+=`, `x=`, `~=`, `//=`, … (not `==`/`=~`/`=>`)
        size_t r = q; std::string op;
        while (r < src_.size() && (std::isalnum((unsigned char)src_[r]) || strchr("+-*/~%.|&^", src_[r]))) op += src_[r++];
        if (r < src_.size() && src_[r] == '=' && (r + 1 >= src_.size() || (src_[r + 1] != '=' && src_[r + 1] != '~' && src_[r + 1] != '>')))
            { assignForm = true; assignOp = op; }
        else if (q >= src_.size() || src_[q] != d) return false;
    }
    bool patQuoteAware = (isRegex || isSubst) && !bracket; // regex/subst PATTERN: '...'/{...}/<...>/[...] protect the delimiter
    bool codeBlocks = (isRegex || isSubst); // regex/subst may contain { ... } embedded code
    // Perl-5 regex mode (rx:P5/…/) uses Perl bracket semantics where `[` does not
    // nest like Raku char classes, so don't shield the delimiter inside `[ ]` there.
    bool p5 = adverbs.find("P5") != std::string::npos || adverbs.find("Perl5") != std::string::npos;
    auto readPart = [&](bool quoteAware, bool blocks) -> std::string {
        std::string raw;
        int depth = 1;
        char q = 0;
        int bd = 0; // { } embedded-code-block nesting
        int sd = 0; // [ ] nesting: a char class <-[/]> / group shields the delimiter
        while (!eof()) {
            char ch = peek();
            if (ch == '\\') { advance(); raw += '\\'; if (!eof()) raw += advance(); continue; }
            // inside a { ... } code block (regex pattern OR subst replacement): opaque, balance braces only
            if (blocks && bd > 0) {
                if (ch == '{') bd++;
                else if (ch == '}') bd--;
                raw += advance();
                continue;
            }
            if (quoteAware && q) { if (ch == q) q = 0; raw += advance(); continue; }
            if (quoteAware && (ch == '\'' || ch == '"')) { q = ch; raw += advance(); continue; }
            if (blocks && ch == '{') { bd++; raw += advance(); continue; } // enter code block
            // Raku regex/subst pattern: `[ ... ]` groups & char classes (incl. <-[/]>) shield the delimiter
            if (quoteAware && !p5 && ch == '[') { sd++; raw += advance(); continue; }
            if (quoteAware && !p5 && ch == ']' && sd > 0) { sd--; raw += advance(); continue; }
            if (sd == 0 && bracket && ch == d) { depth++; raw += advance(); continue; }
            if (sd == 0 && ch == close) { depth--; if (depth == 0) { advance(); break; } raw += advance(); continue; }
            raw += advance();
        }
        return raw;
    };
    while (pos_ < p) advance(); // consume keyword + adverbs
    advance();                  // opening delimiter
    std::string raw = readPart(patQuoteAware, codeBlocks);
    if (isWords) { // qw<...> / Qw<...> / qqw<...> : raw content, split on whitespace by the parser
        out = make(Tok::QwList, raw);
        return true;
    }
    // heredoc: q:to/MARKER/ — the delimited text is the terminator; body follows at line end
    if (adverbs.find(":to ") != std::string::npos || adverbs.find(":heredoc ") != std::string::npos) {
        heredocMarker_ = raw;
        heredocInterp_ = (w == "qq");
        out = make(heredocInterp_ ? Tok::StrInterp : Tok::StrLit, ""); // body filled at line end
        return true;
    }
    if (isSubst || isTrans) {
        std::string repl;
        if (assignForm) { // s[pat] OP= repl : the RHS applied per match
            while (!eof() && std::isspace((unsigned char)peek())) advance();
            for (size_t k = 0; k < assignOp.size() && !eof(); k++) advance(); // skip the op prefix
            if (peek() == '=') advance();
            while (!eof() && std::isspace((unsigned char)peek())) advance();
            std::string rhs; bool wasStr = false; char rq = peek();
            if (rq == '"' || rq == '\'') {
                wasStr = true; advance(); // opening quote
                while (!eof() && peek() != rq) {
                    if (peek() == '\\') { rhs += advance(); if (!eof()) rhs += advance(); }
                    else rhs += advance();
                }
                if (peek() == rq) advance(); // closing quote
            } else {
                // capture the RHS expression up to a statement/argument boundary
                int pd = 0, bd = 0, brd = 0;
                while (!eof()) {
                    char ch = peek();
                    if (pd == 0 && bd == 0 && brd == 0 &&
                        (ch == ',' || ch == ';' || ch == ')' || ch == '}' || ch == ']')) break;
                    if (ch == '(') pd++; else if (ch == ')') pd--;
                    else if (ch == '{') bd++; else if (ch == '}') bd--;
                    else if (ch == '[') brd++; else if (ch == ']') brd--;
                    rhs += advance();
                }
                while (!rhs.empty() && std::isspace((unsigned char)rhs.back())) rhs.pop_back();
            }
            if (assignOp.empty()) {
                // plain `= "str"` keeps its raw content (which may be a `{…}` code
                // replacement or an interpolated string); `= EXPR` becomes code.
                repl = wasStr ? rhs : ("{" + rhs + "}");
            } else {
                // `OP= X` → `$/ OP (X)` per match; re-quote a string operand.
                std::string operand = wasStr ? ("\"" + rhs + "\"") : rhs;
                repl = "{ $/ " + assignOp + " (" + operand + ") }";
            }
        } else if (bracket) { // s[..][..] / tr[..][..] : skip ws, expect a fresh bracket pair
            while (!eof() && std::isspace((unsigned char)peek())) advance();
            if (peek() == d) { advance(); repl = readPart(false, !isTrans); }
        } else {
            repl = readPart(false, !isTrans); // replacement (tr: raw, not brace-aware)
        }
        // tr/y: tag the pattern with a sentinel so the interpreter transliterates
        out = make(Tok::SubstLit, (isTrans ? std::string("\x01") : std::string()) + adverbs + raw);
        out.text2 = repl;
        out.flag = (w == "S" || w == "SS"); // uppercase S/// : non-mutating, returns the new string
        return true;
    }
    if (isRegex) {
        out = make(Tok::RegexLit, adverbs + raw);
        out.flag = (w == "rx"); // rx// is a Regex object, never an implicit $_ match
        return true;
    }
    bool interp = (w == "qq");
    // interpolation-feature adverbs (q:c / Q:s / qq:!s / q:s …) pick which
    // features interpolate: s=scalars a=arrays h=hashes f=&calls c={blocks}
    // b=backslashes. Encoded as a "\x02feats\x02" prefix for parseInterpString.
    {
        std::string feats = interp ? "sahfcb" : "";
        bool anyFeat = false;
        auto toggle = [&](const char* name, char f) {
            if (adverbs.find(":" + std::string(name) + " ") != std::string::npos) {
                anyFeat = true;
                if (feats.find(f) == std::string::npos) feats += f;
            }
            if (adverbs.find(":!" + std::string(name) + " ") != std::string::npos) {
                anyFeat = true;
                size_t at = feats.find(f);
                if (at != std::string::npos) feats.erase(at, 1);
            }
        };
        toggle("s", 's'); toggle("scalar", 's');
        toggle("a", 'a'); toggle("array", 'a');
        toggle("h", 'h'); toggle("hash", 'h');
        toggle("f", 'f'); toggle("function", 'f');
        toggle("c", 'c'); toggle("closure", 'c');
        toggle("b", 'b'); toggle("backslash", 'b');
        if (adverbs.find(":qq ") != std::string::npos) { anyFeat = true; feats = "sahfcb"; }
        if (anyFeat) {
            if (feats.empty()) { out = make(Tok::StrLit, raw); return true; }
            out = make(Tok::StrInterp, "\x02" + feats + "\x02" + raw);
            return true;
        }
    }
    // `q…` has single-quote semantics: collapse \\ → \ and \<delimiter> → <delimiter>
    // (qq handles escapes at interpolation time; Q leaves everything literal).
    if (w == "q") {
        std::string s;
        for (size_t k = 0; k < raw.size(); k++) {
            if (raw[k] == '\\' && k + 1 < raw.size() &&
                (raw[k + 1] == '\\' || raw[k + 1] == d || raw[k + 1] == close)) {
                s += raw[k + 1]; k++;
            } else s += raw[k];
        }
        raw = std::move(s);
    }
    out = make(interp ? Tok::StrInterp : Tok::StrLit, raw);
    // q:o/…/ and q:format/…/ (6.e) build a Format object, not a plain string.
    if (adverbs.find(":o ") != std::string::npos || adverbs.find(":format ") != std::string::npos)
        out.flag = true;
    return true;
}

Token Lexer::lexIdentOrVar() {
    char sig = peek();
    if (sig == '$' || sig == '@' || sig == '%' || sig == '&') {
        std::string name;
        name += advance(); // sigil
        // contextualizer: $@foo $%h $$x $(...) @(...) %(...) and $[...] (itemized array)
        if (sig != '&' && (peek() == '$' || peek() == '@' || peek() == '%' || peek() == '(' ||
                           (sig == '$' && peek() == '['))) {
            return make(Tok::Op, name);
        }
        // optional twigil
        char tw = peek();
        if ((tw == '*' || tw == '.' || tw == '!' || tw == '^' || tw == '?' ||
             tw == ':' || tw == '=' || tw == '~') &&
            (isIdentStart(peek(1)) )) {
            name += advance();
        }
        if (std::isdigit((unsigned char)peek())) {
            while (std::isdigit((unsigned char)peek())) name += advance();
        } else if (isIdentStart(peek()) || unicodeLetterHere()) {
            consumeIdentChars(name);
            // allow embedded - or ' between identifier chars
            while ((peek() == '-' || peek() == '\'') && std::isalpha((unsigned char)peek(1))) {
                name += advance();
                consumeIdentChars(name);
            }
            // package-qualified variable: $Foo::Bar::baz
            while (peek() == ':' && peek(1) == ':' && (isIdentStart(peek(2)) || (unsigned char)peek(2) >= 0x80)) {
                name += advance(); name += advance();
                consumeIdentChars(name);
            }
        } else if (peek() == '/' || peek() == '!' || peek() == '~') {
            name += advance(); // special vars $/ $! $~
        }
        // operator-name suffix: &infix:<cmp>  &prefix:<->  &infix:«+»
        if (peek() == ':' && peek(1) == '<') {
            name += advance(); name += advance(); // :<
            while (!eof() && peek() != '>') name += advance();
            if (peek() == '>') name += advance();
        }
        return make(Tok::Var, name);
    }
    // version literal: v0.48  v6  v1.2.3+  v6.*  — parts are digits or '*'
    // separated by '.', optional trailing '+'; `.WHAT` etc. stays a method call
    if (peek() == 'v' && std::isdigit((unsigned char)peek(1))) {
        std::string ver;
        advance(); // v (not stored)
        for (;;) {
            while (std::isdigit((unsigned char)peek()) || peek() == '*' ||
                   std::islower((unsigned char)peek())) ver += advance();
            // a dotted part continues the version if it's digits/'*' or a LOWERCASE
            // alpha run (v6.c); `.WHAT` and friends stay method calls
            if (peek() == '.' && (std::isdigit((unsigned char)peek(1)) || peek(1) == '*' ||
                                  std::islower((unsigned char)peek(1))))
                ver += advance();
            else break;
        }
        if (peek() == '+') ver += advance();
        return make(Tok::VersionLit, ver);
    }
    // bareword identifier (may start with a Unicode letter, e.g. π, αβγ)
    std::string name;
    if (unicodeLetterHere()) { int n = utf8Len((unsigned char)peek()); for (int i=0;i<n && !eof();i++) name += advance(); }
    else name += advance();
    consumeIdentChars(name);
    while ((peek() == '-' || peek() == '\'') && std::isalpha((unsigned char)peek(1))) {
        name += advance();
        consumeIdentChars(name);
    }
    // allow :: package separators
    while (peek() == ':' && peek(1) == ':') {
        name += advance(); name += advance();
        consumeIdentChars(name);
    }
    // word-operator compound assignment: `div= mod= gcd= lcm=` (one Op token, tight `=`)
    if ((name == "div" || name == "mod" || name == "gcd" || name == "lcm") &&
        peek() == '=' && peek(1) != '=' && peek(1) != ':') {
        name += advance();
        return make(Tok::Op, name);
    }
    return make(Tok::Ident, name);
}

bool Lexer::tryRuleDecl(std::vector<Token>& out, bool spaced) {
    // method-call position (.token) is not a rule declaration
    if (!out.empty() && out.back().kind == Tok::Op && out.back().text == ".") return false;
    size_t save = pos_;
    std::string kw;
    while (!eof() && std::isalpha((unsigned char)peek())) kw += advance();
    if (kw != "token" && kw != "rule" && kw != "regex") { pos_ = save; return false; }
    size_t afterKw = pos_;
    while (!eof() && (peek() == ' ' || peek() == '\t')) advance();
    // optional rule name (ident, may include - ' :sym<...>)
    std::string name;
    if (isIdentStart(peek())) {
        while (isIdentCont(peek()) || ((peek() == '-' || peek() == '\'') && isIdentCont(peek(1)))) name += advance();
        // protoregex multi variant: `:sym<dec>`, `:<null>`, or `:foo('x')`
        while (peek() == ':' && (peek(1) == '<' || isIdentStart(peek(1)))) {
            name += advance(); // ':'
            while (isIdentCont(peek())) name += advance(); // adverb key (e.g. sym)
            char open = peek();
            if (open == '<' || open == '(') {
                char close = open == '<' ? '>' : ')';
                int ad = 0;
                do { char ch = peek(); if (ch == open) ad++; else if (ch == close) ad--; name += advance(); } while (!eof() && ad > 0);
            } else if ((unsigned char)peek() == 0xC2 && (unsigned char)peek(1) == 0xAB) {
                // guillemet «…» delimiter (e.g. `token a:sym«=>» { … }`): read to the matching »
                name += advance(); name += advance(); // «
                while (!eof() && !((unsigned char)peek() == 0xC2 && (unsigned char)peek(1) == 0xBB)) name += advance();
                if (!eof()) { name += advance(); name += advance(); } // »
            }
        }
    }
    while (!eof() && (peek() == ' ' || peek() == '\t' || peek() == '\n')) advance();
    // optional signature: `token NAME(Str $indent) { … }` — capture the balanced
    // parameter list onto the name (parseClass parses out the param var names) so
    // parameterised subrule calls `<NAME($x)>` can bind arguments.
    if (peek() == '(') {
        int pd = 0;
        do { char ch = peek(); if (ch == '(') pd++; else if (ch == ')') pd--; name += advance(); } while (!eof() && pd > 0);
        while (!eof() && (peek() == ' ' || peek() == '\t' || peek() == '\n')) advance();
    }
    if (peek() != '{') { pos_ = save; return false; }
    advance(); // {
    std::string body;
    int depth = 1;
    while (!eof() && depth > 0) {
        char ch = peek();
        if (ch == '\\') { body += advance(); if (!eof()) body += advance(); continue; }
        if (ch == '{') depth++;
        else if (ch == '}') { depth--; if (depth == 0) { advance(); break; } }
        body += advance();
    }
    (void)afterKw;
    Token kt = make(Tok::Ident, kw); kt.spaceBefore = spaced; out.push_back(kt);
    Token nt = make(Tok::Ident, name); nt.spaceBefore = true; out.push_back(nt);
    Token bt = make(Tok::RegexLit, body); bt.spaceBefore = true; out.push_back(bt);
    return true;
}

// A quote form (m// s/// q// ...) is NOT a quote when the previous token is a
// method/sub call dot or a declarator keyword — there the word is a name.
static bool quoteBlockedHere(const std::vector<Token>& out, bool spaced) {
    if (out.empty()) return false;
    const Token& pv = out.back();
    if (pv.kind == Tok::Op && (pv.text == "." || pv.text == ".&" || pv.text == "->")) return true;
    // A name TIGHT after ':' is an adverb pair — `:y(2)` is y => 2, never a `y///`
    // transliteration; `:q<x>` is q => 'x'. (A spaced `$fh.say: q/hi/` stays a quote.)
    if (pv.kind == Tok::Op && pv.text == ":" && !spaced) return true;
    if (pv.kind == Tok::Ident) {
        static const std::set<std::string> decl = {
            "method", "submethod", "sub", "multi", "proto", "token", "rule",
            "regex", "macro", "my", "our", "has", "anon", "state", "class", "role",
        };
        return decl.count(pv.text) > 0;
    }
    return false;
}

bool Lexer::regexContext(const std::vector<Token>& out) {
    if (out.empty()) return true;
    const Token& pv = out.back();
    switch (pv.kind) {
        case Tok::Op:
            // postfix ++/-- and subscript/comparison closers => division follows, not a regex.
            // «…» is a quote-word list like <…>, so a `/` right after « is a word char.
            return pv.text != "++" && pv.text != "--" &&
                   pv.text != ">" && pv.text != "<" && pv.text != ">>" && pv.text != "<<" &&
                   pv.text != ">=" && pv.text != "<=" &&
                   pv.text != "\xC2\xAB" && pv.text != "\xC2\xBB"; // « »
        case Tok::LParen: case Tok::LBrace: case Tok::LBracket:
        case Tok::Comma: case Tok::Semicolon: case Tok::FatArrow:
            return true;
        case Tok::Ident: {
            static const std::set<std::string> kw = {
                "if", "unless", "while", "until", "when", "given", "return", "and", "or",
                "not", "so", "say", "print", "put", "note", "grep", "map", "first",
                "gather", "take", "ok", "nok", "is", "isnt", "like", "unlike", "split",
                "comb", "join", "for", "elsif", "where", "die", "warn",
            };
            return kw.count(pv.text) > 0;
        }
        default:
            return false; // IntLit/NumLit/Var/RParen/RBracket/StrLit/RegexLit => division
    }
}

bool Lexer::trySetOp(Token& out) {
    // current char is '('
    static const std::set<std::string> inners = {
        "|", "&", "-", "^", ".", "+", "elem", "cont", "<=", "<", ">=", ">",
        "==", "!=", "<>", "!elem", "(+)", "<+>", "<->",
    };
    size_t p = pos_ + 1;
    std::string inner;
    while (p < src_.size() && src_[p] != ')' && (p - pos_) < 8) { inner += src_[p]; p++; }
    if (p < src_.size() && src_[p] == ')' && inners.count(inner)) {
        for (size_t k = pos_; k <= p; k++) advance();
        out = make(Tok::Op, "(" + inner + ")");
        return true;
    }
    return false;
}

Token Lexer::lexOperator() {
    // hyper metaop with guillemet brackets: »op« / «op» / »op» / «op«  (2-byte UTF-8
    // « = C2 AB, » = C2 BB). Normalise to the ASCII >>op<< form the parser already
    // understands. Only reached in operator position, so it never shadows a «…» word
    // list (those are lexed where a term is expected).
    auto guill = [&](int off, bool& right) -> bool {
        if ((unsigned char)peek(off) != 0xC2) return false;
        unsigned char c1 = (unsigned char)peek(off + 1);
        if (c1 == 0xBB) { right = true;  return true; } // »
        if (c1 == 0xAB) { right = false; return true; } // «
        return false;
    };
    { bool ro;
      // Only a *symbolic* inner counts as hyper (`«+»`); an alphanumeric inner is a
      // guillemet word-list (`«x»`, `«ab»`), handled elsewhere — so require the first
      // inner char to be non-word.
      if (guill(0, ro) && (unsigned char)peek(2) < 0x80 &&
          !std::isalnum((unsigned char)peek(2)) && peek(2) != '_' && peek(2) != ' ') {
        size_t save = pos_;
        std::string open = ro ? ">>" : "<<";
        advance(); advance(); // opening guillemet
        std::string inner; bool rc;
        while (!eof() && !guill(0, rc) && !std::isspace((unsigned char)peek()) &&
               !std::isalnum((unsigned char)peek()) && inner.size() < 4)
            inner += advance();
        if (!inner.empty() && guill(0, rc)) {
            std::string close = rc ? ">>" : "<<";
            advance(); advance(); // closing guillemet
            return make(Tok::Op, open + inner + close);
        }
        pos_ = save; // not a hyper — fall through to the generic multibyte op
      }
    }
    // multibyte (UTF-8) operator, e.g. set ops ∪ ∩ ∈ ⊆
    if ((unsigned char)peek() >= 0x80) {
        unsigned char c0 = (unsigned char)peek();
        int len = (c0 >> 5) == 0x6 ? 2 : (c0 >> 4) == 0xe ? 3 : (c0 >> 3) == 0x1e ? 4 : 1;
        std::string s;
        for (int k = 0; k < len && !eof(); k++) s += advance();
        // metaop assign on set-combiners: ∩= ∪= ∖= ⊖= ⊎= ⊍= (X= for infix X)
        static const std::set<std::string> combines = {
            "\xE2\x88\xA9", "\xE2\x88\xAA", "\xE2\x88\x96",
            "\xE2\x8A\x96", "\xE2\x8A\x8E", "\xE2\x8A\x8D"};
        if (combines.count(s) && peek() == '=' && peek(1) != '=') { advance(); s += "="; }
        return make(Tok::Op, s);
    }
    // hyper binary metaop: >>OP>> / <<OP<< / >>OP<< / <<OP>>  (e.g. @a >>->> @b)
    if ((peek() == '>' && peek(1) == '>') || (peek() == '<' && peek(1) == '<')) {
        size_t save = pos_;
        std::string s; s += advance(); s += advance();
        std::string inner;
        while (!eof() && peek() != '>' && peek() != '<' && !std::isspace((unsigned char)peek()) && inner.size() < 4)
            inner += advance();
        if (!inner.empty() && ((peek() == '>' && peek(1) == '>') || (peek() == '<' && peek(1) == '<'))) {
            s += inner; s += advance(); s += advance();
            return make(Tok::Op, s);
        }
        pos_ = save; // not a hyper-binary; fall through to normal operator lexing
    }
    static const char* ops[] = {
        "==>", "<==", // feed operators (before == / <=)
        "!!!", "???", "...^", "...", "^..^", "..^", "^..",
        "!===", // negated value identity (before !== / ===)
        "=~=", "≅", "===", "!==", "!%%", "**=", "//=", "||=", "&&=", "^^=", "<=>", "<<=", ">>=", "!~~",
        // bitwise/boolean (numeric +&/+|/+^, string ~&/~|/~^, boolean ?&/?|/?^) before single + ? ~.
        // NB: the shift forms +</+>/~</~> are deliberately omitted — `<`/`>` collide with
        // word-lists (`+<a b>`), operator-name brackets, and comparison in one-pass parsing.
        "+&", "+|", "+^", "~&", "~|", "~^", "?&", "?|", "?^", "+>", "~>",
        "??", "!!", "**", "//", "||", "&&", "^^", "==", "!=", "<=", ">=", "~~", "=>",
        "-->", "->", "=:=", ":=", "++", "--", "+=", "-=", "*=", "/=", "~=", "%%", "%=",
        "x=", "..", "::", "<<", ">>", "andthen", // (textual handled elsewhere)
    };
    // `+<` / `~<` shifts: only when `<` clearly isn't opening a word list
    // (`+<a b>` stays prefix-plus on a QwList). Shift uses follow with space,
    // a digit, `$`, `(` or `=` (compound assign).
    if ((peek() == '+' || peek() == '~') && peek(1) == '<') {
        // Shift vs word list (one-pass-parsing/less-than.t): `+< foo bar >` and
        // `+<3 4>` are prefix-on-word-list; a shift's amount starts with a digit,
        // `$`, `(` or `-` after optional spaces — `$n +< 3`, `+< ($x-1)`, `+<= 2`.
        int k = 2;
        while (peek(k) == ' ' || peek(k) == '\t') k++;
        char cN = peek(k);
        bool shifty = (k > 2 && (std::isdigit((unsigned char)cN) || cN == '$' || cN == '(' || cN == '-'))
                    || peek(2) == '='; // compound `+<=` is always a shift
        if (shifty) {
            std::string op; op += advance(); op += advance(); // + <
            if (peek() == '=' && peek(1) != '=') { advance(); op += "="; }
            return make(Tok::Op, op);
        }
    }
    // try longest first (skip the textual placeholder)
    for (const char* op : ops) {
        std::string s(op);
        if (s == "andthen") continue;
        bool ok = true;
        for (size_t k = 0; k < s.size(); k++) {
            if (peek(k) != s[k]) { ok = false; break; }
        }
        if (ok) {
            for (size_t k = 0; k < s.size(); k++) advance();
            if (s == "=>") return make(Tok::FatArrow, s);
            // bitwise/shift compound assigns: +|= +&= +^= ?^= +<= +>= …
            static const std::set<std::string> bitwiseOps = {
                "+&", "+|", "+^", "~&", "~|", "~^", "?&", "?|", "?^", "+<", "+>", "~<", "~>"};
            if (bitwiseOps.count(s) && peek() == '=' && peek(1) != '=') { advance(); s += "="; }
            return make(Tok::Op, s);
        }
    }
    char c = advance();
    return make(Tok::Op, std::string(1, c));
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> out;
    for (;;) {
        // fill pending heredoc bodies once we reach the end of the current line
        if (!pendingHeredocs_.empty() && peek() == '\n') processHeredocs(out);
        size_t before = pos_;
        skipWhitespaceAndComments();
        // a dropped ⚛ marker isn't whitespace: `$x⚛++` keeps `++` tight-postfix
        bool spaced = (pos_ > before && pos_ != atomDropEnd_) || before == 0;
        if (eof()) break;
        char c = peek();
        Token t;
        // corner-bracket literal quote ｢...｣ (U+FF62 .. U+FF63)
        if ((unsigned char)c == 0xEF && (unsigned char)peek(1) == 0xBD && (unsigned char)peek(2) == 0xA2) {
            advance(); advance(); advance(); // ｢
            std::string raw;
            while (!eof() && !((unsigned char)peek() == 0xEF && (unsigned char)peek(1) == 0xBD && (unsigned char)peek(2) == 0xA3))
                raw += advance();
            if (!eof()) { advance(); advance(); advance(); } // ｣
            Token ct = make(Tok::StrLit, raw);
            ct.spaceBefore = spaced;
            out.push_back(ct);
            continue;
        }
        // Unicode string quotes: ‘…’ (U+2018/2019, literal) and “…” (U+201C/201D, interpolating)
        if ((unsigned char)c == 0xE2 && (unsigned char)peek(1) == 0x80 &&
            ((unsigned char)peek(2) == 0x98 || (unsigned char)peek(2) == 0x9C)) {
            bool interp = (unsigned char)peek(2) == 0x9C;                 // “ vs ‘
            unsigned char closeB = interp ? 0x9D : 0x99;                  // ” / ’
            advance(); advance(); advance();
            std::string raw; int depth = 1;
            while (!eof()) {
                if ((unsigned char)peek() == 0xE2 && (unsigned char)peek(1) == 0x80) {
                    unsigned char b2 = (unsigned char)peek(2);
                    if (b2 == closeB) { if (--depth == 0) { advance(); advance(); advance(); break; } }
                    else if (b2 == (interp ? 0x9C : 0x98)) depth++; // nested opener
                    raw += advance(); raw += advance(); raw += advance(); continue;
                }
                raw += advance();
            }
            Token qt = make(interp ? Tok::StrInterp : Tok::StrLit, raw);
            qt.spaceBefore = spaced;
            out.push_back(qt);
            continue;
        }
        // Unicode superscript power: a run of ⁰¹²³⁴… after a term means ** N
        if (!out.empty() && (unsigned char)c >= 0x80) {
            Tok lk = out.back().kind;
            bool afterTerm = lk == Tok::IntLit || lk == Tok::NumLit || lk == Tok::Var ||
                             lk == Tok::RParen || lk == Tok::RBracket || lk == Tok::Ident;
            std::string digits;
            if (afterTerm && tryReadSuperscript(digits)) {
                Token op = make(Tok::Op, "**"); op.spaceBefore = false; out.push_back(op);
                Token num = make(Tok::IntLit, digits); num.ival = std::strtoll(digits.c_str(), nullptr, 10);
                out.push_back(num);
                continue;
            }
        }
        if (isIdentStart(c) && tryRuleDecl(out, spaced)) continue;
        // leading-dot fraction `.5` / `.5i` — a method name can never start with a digit,
        // so `.<digit>` is always a fractional number (e.g. `say .5`, `$x + .5i`).
        if (c == '.' && std::isdigit((unsigned char)peek(1))) {
            t = lexNumber(); t.spaceBefore = spaced; out.push_back(t); continue;
        }
        // A Unicode digit/numeral is NOT a number when it directly follows a bare
        // sigil (`$১০kinds`): that is an invalid numeric-start identifier and must
        // stay a parse error, not lex as `$` + 10.
        bool afterBareSigil = !spaced && !out.empty() && out.back().kind == Tok::Var &&
                              out.back().text.size() == 1 && strchr("$@%&", out.back().text[0]);
        long long nvN, nvD;
        // Unicode ellipsis … (U+2026) is an alias for the sequence/yada operator `...`
        // (checked before the Unicode-letter dispatch, which would otherwise eat it).
        if ((unsigned char)c == 0xE2 && (unsigned char)peek(1) == 0x80 && (unsigned char)peek(2) == 0xA6) {
            advance(); advance(); advance();
            t = make(Tok::Op, "...");
        // Unicode operator aliases → ASCII: ≤ ≥ ≠ (U+2264/5/0) and × ÷ (U+00D7/F7).
        } else if ((unsigned char)c == 0xE2 && (unsigned char)peek(1) == 0x89 &&
                   ((unsigned char)peek(2) == 0xA4 || (unsigned char)peek(2) == 0xA5 || (unsigned char)peek(2) == 0xA0)) {
            unsigned char b = (unsigned char)peek(2); advance(); advance(); advance();
            t = make(Tok::Op, b == 0xA4 ? "<=" : b == 0xA5 ? ">=" : "!=");
        } else if ((unsigned char)c == 0xC3 && ((unsigned char)peek(1) == 0x97 || (unsigned char)peek(1) == 0xB7)) {
            bool mul = (unsigned char)peek(1) == 0x97; advance(); advance();
            std::string op = mul ? "*" : "/";
            if (peek() == '=' && peek(1) != '=') { advance(); op += "="; } // ×= ÷= compound assign
            t = make(Tok::Op, op);
        } else if ((unsigned char)c == 0xE2 && (unsigned char)peek(1) == 0x88 &&
                   (unsigned char)peek(2) == 0x92) {
            // − (U+2212 minus sign) → -
            advance(); advance(); advance();
            std::string op = "-";
            if (peek() == '=' && peek(1) != '=') { advance(); op += "="; } // −= compound assign
            t = make(Tok::Op, op);
        } else if (std::isdigit((unsigned char)c) ||
            (!afterBareSigil && (unsigned char)c >= 0x80 &&
             (ndDigitValue(codepointHere()) >= 0 || unicodeNumeralValue(codepointHere(), nvN, nvD)))) {
            t = lexNumber(); // ASCII digit, Unicode-Nd digit, or an Nl/No numeral
        } else if (c == '\'') {
            t = lexQuoted('\'');
        } else if (c == '"') {
            t = lexQuoted('"');
        } else if (c == '$' || c == '@' || c == '%' || c == '&' || isIdentStart(c) || unicodeLetterHere()) {
            // '%' and '&' could be operators; treat as var only if followed by name/twigil
            if ((c == '%' || c == '&') &&
                !(isIdentStart(peek(1)) || peek(1) == '*' || peek(1) == '.' ||
                  peek(1) == '!' || peek(1) == '^' ||
                  (peek(1) == ':' && peek(2) == ':') || // symbolic deref `%::($n)` / `&::($n)`
                  ((peek(1) == '?' || peek(1) == '=' || peek(1) == '~') && isIdentStart(peek(2))))) {
                t = lexOperator();
            } else if (isIdentStart(c) && !quoteBlockedHere(out, spaced) && tryQuoteForm(t)) {
                // t set by tryQuoteForm
            } else {
                t = lexIdentOrVar();
            }
        } else if (c == '(' && trySetOp(t)) { /* t set by trySetOp */ }
        else if (c == '(') { advance(); t = make(Tok::LParen, "("); }
        else if (c == ')') { advance(); t = make(Tok::RParen, ")"); }
        else if (c == '{') { advance(); t = make(Tok::LBrace, "{"); }
        else if (c == '}') { advance(); t = make(Tok::RBrace, "}"); }
        else if (c == '[') { advance(); t = make(Tok::LBracket, "["); }
        else if (c == ']') { advance(); t = make(Tok::RBracket, "]"); }
        else if (c == ';') { advance(); t = make(Tok::Semicolon, ";"); }
        else if (c == ',') { advance(); t = make(Tok::Comma, ","); }
        else if (c == '/' && peek(1) != '/' && peek(1) != '=' && regexContext(out) &&
                 // `[/]` (and `[\/]`) is the division reduce metaop, not a regex
                 !(peek(1) == ']' && !out.empty() &&
                   (out.back().kind == Tok::LBracket ||
                    (out.back().kind == Tok::Op && out.back().text == "\\")))) {
            advance(); // opening /
            std::string raw;
            char quote = 0;       // '...' / "..." protect an inner /
            int angle = 0, brack = 0; // <...> assertions/classes and [...] groups protect an inner /
            while (!eof()) {
                char ch = peek();
                if (ch == '\\') { raw += advance(); if (!eof()) raw += advance(); continue; }
                if (quote) { if (ch == quote) quote = 0; raw += advance(); continue; }
                // '...'/"..." protect an inner '/', but inside a <[...]> char class they are literal chars
                if ((ch == '\'' || ch == '"') && brack == 0) { quote = ch; raw += advance(); continue; }
                // `<(` and `)>` are the match-capture markers, not balanced <…> groups
                if (ch == '<' && peek(1) == '(') { raw += advance(); raw += advance(); continue; }
                if (ch == ')' && peek(1) == '>') { raw += advance(); raw += advance(); continue; }
                // `<<` / `>>` are word-boundary assertions, not angle groups
                if ((ch == '<' && peek(1) == '<') || (ch == '>' && peek(1) == '>'))
                    { raw += advance(); raw += advance(); continue; }
                if (ch == '<') angle++;
                else if (ch == '>' && angle > 0) angle--;
                else if (ch == '[') brack++;
                else if (ch == ']' && brack > 0) brack--;
                else if (ch == '/' && angle == 0 && brack == 0) break;
                raw += advance();
            }
            if (peek() == '/') advance();
            t = make(Tok::RegexLit, raw);
        }
        else {
            t = lexOperator();
        }
        t.spaceBefore = spaced;
        out.push_back(t);
        if (!heredocMarker_.empty()) { // a q:to/MARKER/ was just lexed
            pendingHeredocs_.emplace_back(heredocMarker_, out.size() - 1, heredocInterp_);
            heredocMarker_.clear();
        }
    }
    out.push_back(make(Tok::End, ""));
    return out;
}

void Lexer::processHeredocs(std::vector<Token>& out) {
    advance(); // consume the newline ending the marker line
    for (auto& [marker, idx, interp] : pendingHeredocs_) {
        std::vector<std::string> lines;
        std::string closeIndent;
        bool found = false;
        while (!eof()) {
            std::string line;
            while (!eof() && peek() != '\n') line += advance();
            if (!eof()) advance(); // newline
            // trimmed comparison to the marker
            size_t a = line.find_first_not_of(" \t");
            std::string trimmed = (a == std::string::npos) ? "" : line.substr(a);
            size_t b = trimmed.find_last_not_of(" \t");
            if (b != std::string::npos) trimmed = trimmed.substr(0, b + 1);
            if (trimmed == marker) { closeIndent = (a == std::string::npos) ? "" : line.substr(0, a); found = true; break; }
            lines.push_back(line);
        }
        if (!found) // runaway heredoc: the terminator never appears before EOF
            throw ParseError("Ending delimiter " + marker + " not found for heredoc", line_);
        // dedent by the closing marker's indentation
        std::string body;
        for (size_t i = 0; i < lines.size(); i++) {
            std::string ln = lines[i];
            if (!closeIndent.empty() && ln.compare(0, closeIndent.size(), closeIndent) == 0)
                ln = ln.substr(closeIndent.size());
            body += ln;
            body += "\n";
        }
        out[idx].text = body;
    }
    pendingHeredocs_.clear();
}

} // namespace rakupp
