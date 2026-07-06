#include "Lexer.h"
#include <cctype>
#include <cstdlib>
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

Lexer::Lexer(std::string src) : src_(std::move(src)) {}

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
        // unspace: backslash followed by whitespace is ignored (token-joining)
        if (c == '\\' && (peek(1) == ' ' || peek(1) == '\t' || peek(1) == '\n' || peek(1) == '\r')) {
            advance();
            continue;
        }
        if (c == '#') {
            // embedded comment #`( ... ) or #`[ ... ] -- skip for MVP as line comment
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
                // single pod directive paragraph: skip until blank line
                while (!eof() && peek() != '\n') advance();
                continue;
            }
            (void)save;
        }
        break;
    }
}

Token Lexer::lexNumber() {
    std::string num;
    bool isFloat = false;
    if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'o' || peek(1) == 'b')) {
        char base = peek(1);
        advance(); advance();
        std::string digits;
        while (isIdentCont(peek()) || peek() == '_') {
            char c = advance();
            if (c != '_') digits += c;
        }
        int b = base == 'x' ? 16 : base == 'o' ? 8 : 2;
        Token t = make(Tok::IntLit, digits);
        t.ival = std::strtoll(digits.c_str(), nullptr, b);
        return t;
    }
    while (std::isdigit((unsigned char)peek()) || peek() == '_') {
        char c = advance();
        if (c != '_') num += c;
    }
    bool hasDot = false, hasExp = false;
    if (peek() == '.' && std::isdigit((unsigned char)peek(1))) {
        isFloat = true; hasDot = true;
        num += advance(); // .
        while (std::isdigit((unsigned char)peek()) || peek() == '_') {
            char c = advance();
            if (c != '_') num += c;
        }
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
    Token t = make(Tok::IntLit, num);
    t.ival = std::strtoll(num.c_str(), nullptr, 10);
    return t;
}

Token Lexer::lexQuoted(char quote) {
    advance(); // opening quote
    std::string raw;
    while (!eof() && peek() != quote) {
        char c = advance();
        if (c == '\\') {
            char n = peek();
            if (quote == '\'') {
                // single quotes: only \' and \\ are special
                if (n == '\'' || n == '\\') { raw += advance(); }
                else { raw += '\\'; }
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
            raw += c;
        }
    }
    if (!eof()) advance(); // closing quote
    return make(quote == '\'' ? Tok::StrLit : Tok::StrInterp, raw);
}

bool Lexer::tryQuoteForm(Token& out) {
    size_t p = pos_;
    std::string w;
    while (p < src_.size() && std::isalpha((unsigned char)src_[p])) { w += src_[p]; p++; }
    bool isRegex = (w == "rx" || w == "m" || w == "ms" || w == "mm");
    bool isSubst = (w == "s" || w == "S");
    bool isTrans = (w == "tr" || w == "y"); // transliteration tr/from/to/
    bool isWords = (w == "qw" || w == "Qw" || w == "qqw" || w == "qww"); // word-list quotes
    if (w != "q" && w != "qq" && w != "Q" && !isRegex && !isSubst && !isWords && !isTrans) return false;
    // adverbs between keyword and delimiter, e.g. m:i/.../ , s:g/.../.../
    std::string adverbs;
    while (p < src_.size() && src_[p] == ':') {
        size_t q = p + 1;
        std::string a;
        while (q < src_.size() && std::isalnum((unsigned char)src_[q])) a += src_[q++];
        if (a.empty()) break;
        adverbs += ":" + a + " ";
        p = q;
    }
    if (p >= src_.size()) return false;
    char d = src_[p];
    char close;
    bool bracket = true;
    switch (d) {
        case '(': close = ')'; break;
        case '{': close = '}'; break;
        case '[': close = ']'; break;
        case '<': close = '>'; break;
        case '/': case '|': case '!': close = d; bracket = false; break;
        default: return false;
    }
    // A bracketed substitution needs TWO groups: s(pat)(repl) / S[a][b]. If the
    // second group is absent, this is really a call like S(5) or s($x) — not a
    // substitution — so let it lex as an identifier instead.
    if (isSubst && bracket) {
        int depth = 0; size_t q = p;
        for (; q < src_.size(); q++) {
            if (src_[q] == d) depth++;
            else if (src_[q] == close) { depth--; if (depth == 0) { q++; break; } }
        }
        while (q < src_.size() && std::isspace((unsigned char)src_[q])) q++;
        if (q >= src_.size() || src_[q] != d) return false;
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
        if (bracket) { // s[..][..] / tr[..][..] : skip ws, expect a fresh bracket pair
            while (!eof() && std::isspace((unsigned char)peek())) advance();
            if (peek() == d) { advance(); repl = readPart(false, !isTrans); }
        } else {
            repl = readPart(false, !isTrans); // replacement (tr: raw, not brace-aware)
        }
        // tr/y: tag the pattern with a sentinel so the interpreter transliterates
        out = make(Tok::SubstLit, (isTrans ? std::string("\x01") : std::string()) + adverbs + raw);
        out.text2 = repl;
        out.flag = (w == "S"); // uppercase S/// : non-mutating, returns the new string
        return true;
    }
    if (isRegex) {
        out = make(Tok::RegexLit, adverbs + raw);
        return true;
    }
    bool interp = (w == "qq");
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
    // version literal: v0.48  v6  v1.2.3+
    if (peek() == 'v' && std::isdigit((unsigned char)peek(1))) {
        std::string ver;
        ver += advance(); // v
        while (std::isalnum((unsigned char)peek()) || peek() == '.' || peek() == '+') ver += advance();
        return make(Tok::StrLit, ver);
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
static bool quoteBlockedHere(const std::vector<Token>& out) {
    if (out.empty()) return false;
    const Token& pv = out.back();
    if (pv.kind == Tok::Op && (pv.text == "." || pv.text == ".&" || pv.text == "->")) return true;
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
    // multibyte (UTF-8) operator, e.g. set ops ∪ ∩ ∈ ⊆
    if ((unsigned char)peek() >= 0x80) {
        unsigned char c0 = (unsigned char)peek();
        int len = (c0 >> 5) == 0x6 ? 2 : (c0 >> 4) == 0xe ? 3 : (c0 >> 3) == 0x1e ? 4 : 1;
        std::string s;
        for (int k = 0; k < len && !eof(); k++) s += advance();
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
        "=~=", "≅", "===", "!==", "!%%", "**=", "//=", "||=", "&&=", "^^=", "<=>", "<<=", ">>=", "!~~",
        "??", "!!", "**", "//", "||", "&&", "^^", "==", "!=", "<=", ">=", "~~", "=>",
        "-->", "->", "=:=", ":=", "++", "--", "+=", "-=", "*=", "/=", "~=", "%%", "%=",
        "x=", "..", "::", "<<", ">>", "andthen", // (textual handled elsewhere)
    };
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
        bool spaced = (pos_ > before) || before == 0;
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
        if (std::isdigit((unsigned char)c)) {
            t = lexNumber();
        } else if (c == '\'') {
            t = lexQuoted('\'');
        } else if (c == '"') {
            t = lexQuoted('"');
        } else if (c == '$' || c == '@' || c == '%' || c == '&' || isIdentStart(c) || unicodeLetterHere()) {
            // '%' and '&' could be operators; treat as var only if followed by name/twigil
            if ((c == '%' || c == '&') &&
                !(isIdentStart(peek(1)) || peek(1) == '*' || peek(1) == '.' ||
                  peek(1) == '!' || peek(1) == '^' ||
                  ((peek(1) == '?' || peek(1) == '=' || peek(1) == '~') && isIdentStart(peek(2))))) {
                t = lexOperator();
            } else if (isIdentStart(c) && !quoteBlockedHere(out) && tryQuoteForm(t)) {
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
        else if (c == '/' && peek(1) != '/' && peek(1) != '=' && regexContext(out)) {
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
        (void)found;
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
