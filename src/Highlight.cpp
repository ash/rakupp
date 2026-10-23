#include "AsciiCtype.h"
#include "Highlight.h"
#include "Unicode.h"
#include <cctype>
#include <cstdint>
#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace rakupp {


// ---- Raku vocabulary (curated for highlighting; mirrors Pygments' choices) ------

static bool isKeyword(const std::string& w) {
    static const std::set<std::string> kw = {
        // declarators & scope
        "my", "our", "has", "state", "let", "temp", "constant", "anon",
        "sub", "method", "submethod", "multi", "proto", "only", "macro",
        "class", "role", "grammar", "module", "package", "enum", "subset",
        "token", "regex", "rule",
        // control flow
        "if", "elsif", "else", "unless", "with", "orwith", "without",
        "while", "until", "repeat", "for", "loop", "given", "when", "default",
        "return", "return-rw", "make", "take", "gather", "do", "start",
        "last", "next", "redo", "succeed", "proceed", "leave",
        // phasers
        "BEGIN", "END", "ENTER", "LEAVE", "KEEP", "UNDO", "FIRST", "NEXT",
        "LAST", "PRE", "POST", "CATCH", "CONTROL", "DOC", "CHECK",
        // composition / traits / misc
        "is", "does", "but", "trusts", "handles", "where", "signature",
        "use", "no", "need", "import", "require", "unit", "augment", "supersede",
        "try", "quietly", "react", "whenever", "supply",
        "and", "or", "not", "andthen", "orelse", "xor", "so",
    };
    return kw.count(w) > 0;
}

// Built-in type names and routines Pygments colours as Name.Builtin (nb).
static bool isBuiltin(const std::string& w) {
    static const std::set<std::string> nb = {
        // types
        "Int", "UInt", "Str", "Num", "Rat", "FatRat", "Complex", "Bool",
        "Array", "List", "Seq", "Slip", "Hash", "Map", "Bag", "Set", "Mix",
        "BagHash", "SetHash", "MixHash", "Pair", "Range", "Buf", "Blob",
        "Any", "Mu", "Cool", "Numeric", "Real", "Stringy", "Callable", "Code",
        "Block", "Routine", "Sub", "Method", "Regex", "Match", "Grammar",
        "Junction", "Nil", "Whatever", "WhateverCode", "Capture", "Signature",
        "Parameter", "Iterable", "Iterator", "Positional", "Associative",
        "Instant", "Duration", "Date", "DateTime", "IO", "Proc", "Promise",
        "Channel", "Supply", "Lock", "Thread", "Version", "Order", "Exception",
        "Failure", "Label", "Enumeration", "Scalar", "Proxy", "utf8",
        "int", "int8", "int16", "int32", "int64", "uint", "uint8", "uint16",
        "uint32", "uint64", "num", "num32", "num64", "rat", "byte", "str",
        // special terms & universal methods
        "self", "now", "time", "new", "bless", "clone", "gist", "raku", "perl",
        "say", "print", "put", "note", "printf", "sprintf", "warn", "die",
        "fail", "abort", "exit", "sleep", "prompt", "dd",
        "defined", "sqrt", "abs", "floor", "ceiling", "round", "truncate",
        "exp", "log", "log10", "sin", "cos", "tan", "asin", "acos", "atan",
        "atan2", "sign", "min", "max", "sum", "produce", "roundrobin",
        "elems", "end", "keys", "values", "pairs", "kv", "antipairs", "invert",
        "map", "grep", "first", "sort", "reverse", "unique", "repeated",
        "squish", "rotor", "batch", "head", "tail", "flat", "list", "cache",
        "join", "split", "comb", "words", "lines", "chars", "codes", "chr",
        "ord", "chrs", "ords", "uc", "lc", "tc", "tclc", "fc", "flip",
        "trim", "trim-leading", "trim-trailing", "index", "rindex", "substr",
        "push", "pop", "shift", "unshift", "append", "prepend", "splice",
        "pick", "roll", "zip", "cross", "reduce", "classify", "categorize",
        "slurp", "spurt", "open", "close", "unlink", "mkdir", "chdir",
        "run", "shell", "signal", "await", "start",
        "so", "not", "bytes", "hyper", "race",
    };
    return nb.count(w) > 0;
}

// ---- lossless scanner ----------------------------------------------------------

namespace {
struct Scanner {
    const std::string& s;
    size_t i = 0;
    std::vector<Span> out;
    // Was the previous significant (non-space/comment) span a *value* — an
    // identifier(non-keyword)/variable/number/string or a closing `)`/`]`? This
    // decides whether `/` and `<` begin a term (regex/qw) or are operators.
    bool lastWasValue = false;
    // The previous significant span ended a method-call dot (`.`/`.^`/`!`), so the
    // next identifier is a method name — never a keyword.
    bool methodNext = false;
    // HEREDOCS opened on the current line, in order. `q:to/END/` puts only its
    // OPENER on this line — the body starts after the newline — so the body
    // cannot be consumed where the opener is scanned: the `;` in
    // `my $t = q:to/END/;` still belongs to the opener's line. Each entry is
    // the terminator word and the class its body should carry; the queue is
    // drained at the next newline. A line may open more than one.
    std::vector<std::pair<std::string, const char*>> pendingHeredocs;
    // The previous keyword introduces a routine/package/constant name (`method`, `sub`,
    // `class`, `role`, …), so the next identifier is that NAME even if spelled like a
    // keyword — e.g. `method role { … }` declares a method called `role`.
    bool declNext = false;
    // Where the last name that can be a LISTOP ended (npos when the last name
    // was a method, a declared name or a term such as `pi`). After it, a
    // spaced `<` with a word tight after it opens a word list: `is <a b>, …`.
    size_t listopEnd = std::string::npos;

    explicit Scanner(const std::string& src) : s(src) {}
    char cur() const { return i < s.size() ? s[i] : '\0'; }
    char at(size_t k) const { return k < s.size() ? s[k] : '\0'; }

    void emit(std::string t, const char* cls, bool value, bool method = false) {
        out.push_back({std::move(t), cls});
        lastWasValue = value;
        methodNext = method;
        declNext = false;
    }
    // plain text that is not significant for the value/method state machine
    void emitText(std::string t) { out.push_back({std::move(t), ""}); }

    // keywords that introduce a following NAME (routine / package / constant)
    static bool isNameIntroducer(const std::string& w) {
        static const std::set<std::string> in = {
            "sub", "method", "submethod", "macro", "token", "rule", "regex",
            "class", "role", "grammar", "module", "package", "enum", "subset", "constant",
        };
        return in.count(w) > 0;
    }

    static bool identStart(char c) { return ascii::isalpha((unsigned char)c) || c == '_'; }
    static bool identCont(char c) { return ascii::isalnum((unsigned char)c) || c == '_'; }

    // The byte length of the name character at s[k] (0 when there is none):
    // ASCII letters and `_`, or a codepoint the UCD calls a letter (L*, Nl),
    // start a name; digits and combining marks (M*) also continue one. An
    // ASCII-only scan cut `$соу` down to a lone `$` and `née` into three spans.
    size_t nameCharAt(size_t k, bool start) const {
        if (k >= s.size()) return 0;
        unsigned char b = (unsigned char)s[k];
        if (b < 0x80) return (start ? identStart((char)b) : identCont((char)b)) ? 1 : 0;
        int n = b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : b >= 0xC0 ? 2 : 1;
        if (n == 1 || k + n > s.size()) return 0;
        uint32_t cp = b & (0xFF >> (n + 1));
        for (int t = 1; t < n; t++) cp = (cp << 6) | ((unsigned char)s[k + t] & 0x3F);
        const std::string gc = uniGeneralCategory(cp);
        if (gc.size() != 2) return 0;
        bool ok = gc[0] == 'L' || gc == "Nl" || (!start && (gc[0] == 'M' || gc == "Nd"));
        return ok ? (size_t)n : 0;
    }
    // The end of the name that starts at j: name characters, `::` segments,
    // and a `-` or `'` only between a name character and a LETTER (`from-json`,
    // `isn't`), as the lexer joins them: `$n-1` is `$n - 1`, and `&'a'` is
    // `&` before a string, not a variable named `'a`.
    size_t nameEnd(size_t j) const {
        const size_t from = j;
        for (;;) {
            if (size_t n = nameCharAt(j, false)) { j += n; continue; }
            if ((at(j) == '-' || at(j) == '\'') && j > from && nameCharAt(j + 1, true)) { j++; continue; }
            if (at(j) == ':' && at(j + 1) == ':') { j += 2; continue; }
            return j;
        }
    }

    // matching close for a bracket delimiter (else the same char). ASCII only:
    // this used to carry `case '\xAB': return '\xBB'`, which is the SECOND byte
    // of « and » — so `q:to«END»` scanned for a lone 0xC2, stopped on the first
    // byte of the closer and left the second outside the span. A delimiter is a
    // codepoint, not a byte; delimPair() below reads the whole sequence.
    static char closeDelim(char o) {
        switch (o) { case '(': return ')'; case '[': return ']';
                     case '{': return '}'; case '<': return '>';
                     default: return o; }
    }

    static int utf8Len(unsigned char b) { return b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : b >= 0xC0 ? 2 : 1; }

    // The delimiter at `j` and the one that closes it, both as UTF-8 sequences.
    // Non-ASCII pairs follow the lexer's rule so the two agree on where a quote
    // ends: the Bidi mirror when the codepoint has one (« → », ｢ → ｣, 「 → 」),
    // else the next codepoint (〝 U+301D has no mirror and closes with 〞).
    void delimPair(size_t j, std::string& open, std::string& close) const {
        size_t dlen = (size_t)utf8Len((unsigned char)s[j]);
        if (j + dlen > s.size()) dlen = 1;
        open = s.substr(j, dlen);
        if (dlen == 1) { close = std::string(1, closeDelim(s[j])); return; }
        uint32_t cp = (unsigned char)open[0] & (0xFF >> (dlen + 1));
        for (size_t k = 1; k < dlen; k++) cp = (cp << 6) | ((unsigned char)open[k] & 0x3F);
        int32_t mir = uniBidiMirror(cp);
        uint32_t cc = mir >= 0 ? (uint32_t)mir : cp + 1;
        close.clear();
        if (cc < 0x800) { close += (char)(0xC0 | (cc >> 6)); close += (char)(0x80 | (cc & 0x3F)); }
        else if (cc < 0x10000) { close += (char)(0xE0 | (cc >> 12)); close += (char)(0x80 | ((cc >> 6) & 0x3F)); close += (char)(0x80 | (cc & 0x3F)); }
        else { close += (char)(0xF0 | (cc >> 18)); close += (char)(0x80 | ((cc >> 12) & 0x3F)); close += (char)(0x80 | ((cc >> 6) & 0x3F)); close += (char)(0x80 | (cc & 0x3F)); }
    }

    void run() {
        while (i < s.size()) {
            char c = cur();
            if (c == '\n' || c == ' ' || c == '\t' || c == '\r') { scanSpace(); continue; }
            if (c == '#') { scanLineComment(); continue; }
            if (c == '=' && atLineStart() && identStart(at(i + 1))) { scanPod(); continue; }
            if (c == '\'' || c == '"') { scanQuote(c); continue; }
            if ((unsigned char)c >= 0xE2 && scanUnicodeQuote()) continue;
            if (hyperOpEnd(i)) { scanOperator(); continue; }
            // `<<a "b c" $x>>` and `«a b»` are word lists in term position, and
            // `%h<<a>>` / `%h«a»` subscripts after a term
            if (size_t e = doubleAngleEnd(i)) { emit(s.substr(i, e - i), "s", true); i = e; continue; }
            // A reduction where a term is wanted is an operator: `[<=] @b` and
            // `[<] 1, 2` were an array holding a word list from the `<` on
            if (c == '[' && (!lastWasValue || spacedFromListop())) {
                if (size_t e = reduceOpEnd(i)) {
                    emitText(s.substr(i, e - i));
                    lastWasValue = false; methodNext = false; declNext = false;
                    i = e;
                    continue;
                }
            }
            if (c == '<' && !lastWasValue && looksLikeQw()) { scanAngleQw(); continue; }
            // A name is a value here, so `<` after it is an operator, but after
            // a listop, a keyword or an infix word it starts the term: `is <a
            // b>, …`, `for <a b> {…}`, `$a cmp <a b>`. Only spaced from the
            // name and tight against its first word: `elems < 3` compares.
            if (c == '<' && afterListop() && looksLikeQw()) { scanAngleQw(); continue; }
            if (c == '<' && lastWasValue && looksLikeAngleKey()) { scanAngleQw(); continue; } // sym<int> / %h<key> / (…)<last>
            if (c == '/' && !lastWasValue) { scanRegex(); continue; }
            if (c == '/' && lastWasValue) {
                // operator context after a term: division `/`, defined-or `//`, or
                // their assign forms `/=` / `//=`. Consume the whole operator so the
                // second slash of `//` is never mistaken for the start of a regex.
                size_t j = i + 1;
                if (j < s.size() && s[j] == '/') j++;     // //
                if (j < s.size() && s[j] == '=') j++;     // /=  or  //=
                emitText(s.substr(i, j - i));
                lastWasValue = false; methodNext = false; declNext = false;
                i = j;
                continue;
            }
            if (ascii::isdigit((unsigned char)c) ||
                (c == '.' && ascii::isdigit((unsigned char)at(i + 1)) && !lastWasValue)) { scanNumber(); continue; }
            // `:16<ff>`, `:12<1a.b*12**2>` — a RADIX number, not `:16` then a
            // `<ff>` word list: the digits in the brackets are a number's
            if (c == ':' && ascii::isdigit((unsigned char)at(i + 1))) {
                size_t j = i + 1;
                while (ascii::isdigit((unsigned char)at(j))) j++;
                if (at(j) == '<') {
                    size_t e = j + 1;
                    while (e < s.size() && s[e] != '>' && s[e] != '\n' && s[e] != ' ') e++;
                    if (at(e) == '>') { emit(s.substr(i, e + 1 - i), "mi", true); i = e + 1; continue; }
                }
            }
            if (c == '$' || c == '@' || c == '%' || c == '&') { scanVariable(); continue; }
            if (nameCharAt(i, true)) { scanIdent(); continue; }
            scanOperator();
        }
    }

    bool atLineStart() const { return i == 0 || s[i - 1] == '\n'; }

    // Everything from here to the line whose trimmed text is `term`, that line
    // included, is the heredoc's body — one string span. Raku reads the body
    // as text whatever it looks like, so a `#` in it is not a comment and an
    // `if` in it is not a keyword; scanning it as code is what made
    // `--highlight` colour a heredoc as a program, and would have let a
    // formatter reindent string contents.
    void drainHeredocs() {
        while (!pendingHeredocs.empty()) {
            const std::string term = pendingHeredocs.front().first;
            const char* cls = pendingHeredocs.front().second;
            pendingHeredocs.erase(pendingHeredocs.begin());
            size_t k = i;
            while (k < s.size()) {
                size_t eol = s.find('\n', k);
                size_t lineEnd = eol == std::string::npos ? s.size() : eol + 1;
                std::string ln = s.substr(k, lineEnd - k);
                size_t a = ln.find_first_not_of(" \t");
                size_t b = ln.find_last_not_of(" \t\r\n");
                bool isTerm = a != std::string::npos && b != std::string::npos &&
                              ln.substr(a, b - a + 1) == term;
                k = lineEnd;
                if (isTerm || eol == std::string::npos) break;
            }
            if (k > i) { emit(s.substr(i, k - i), cls, true); i = k; }
        }
    }

    void scanSpace() {
        // A newline with a heredoc open ends the whitespace run: what follows
        // is the body, not code.
        if (!pendingHeredocs.empty()) {
            size_t nl = i;
            while (nl < s.size() && (s[nl] == ' ' || s[nl] == '\t' || s[nl] == '\r')) nl++;
            if (nl < s.size() && s[nl] == '\n') {
                bool m = methodNext, v = lastWasValue, d = declNext;
                emitText(s.substr(i, nl + 1 - i));
                methodNext = m; lastWasValue = v; declNext = d;
                i = nl + 1;
                drainHeredocs();
                return;
            }
        }
        size_t j = i;
        while (j < s.size() && (s[j] == ' ' || s[j] == '\t' || s[j] == '\r' || s[j] == '\n')) j++;
        // whitespace doesn't reset the method-call dot or a pending declaration name
        // (`method  role`, or chained `.foo` on the next line); operators/other chars do.
        bool m = methodNext, v = lastWasValue, d = declNext;
        emitText(s.substr(i, j - i));
        methodNext = m; lastWasValue = v; declNext = d;
        i = j;
    }

    void scanLineComment() {
        // embedded (multi-line) comment: #`( … ) / #`[ … ] / #`{ … } / #`< … >
        // — and the declarator bracket forms #|( … ) / #=( … ). Brackets nest.
        if (i + 2 < s.size() && (s[i + 1] == '`' || s[i + 1] == '|' || s[i + 1] == '=') &&
            (s[i + 2] == '(' || s[i + 2] == '[' || s[i + 2] == '{' || s[i + 2] == '<')) {
            char open = s[i + 2], close = closeDelim(open);
            size_t j = i + 3;
            int d = 1;
            while (j < s.size() && d > 0) {
                if (s[j] == open) d++;
                else if (s[j] == close) d--;
                j++;
            }
            emit(s.substr(i, j - i), "cm", false);
            i = j;
            return;
        }
        size_t j = i;
        while (j < s.size() && s[j] != '\n') j++;
        emit(s.substr(i, j - i), "c1", false);
        i = j;
    }

    // POD: a `=directive` line, or a `=begin X … =end X` block. Coloured as comment.
    void scanPod() {
        size_t bol = i;
        size_t nl = s.find('\n', i);
        std::string firstLine = s.substr(i, (nl == std::string::npos ? s.size() : nl) - i);
        // =begin NAME … =end NAME  (multi-line)
        if (firstLine.rfind("=begin", 0) == 0) {
            size_t p = s.find("=end", i);
            size_t endLine = p == std::string::npos ? s.size() : s.find('\n', p);
            if (endLine == std::string::npos) endLine = s.size();
            emit(s.substr(bol, endLine - bol), "c1", false);
            i = endLine;
            return;
        }
        // single =directive line
        emit(firstLine, "c1", false);
        i = (nl == std::string::npos ? s.size() : nl);
    }

    // A quoted string from i, its opener `open` bytes long, to the first of
    // `closers`. In an interpolating one (`"…"`, `“…”`) a `{…}` block is code,
    // and a string inside it does not close the outer one: `"a {f("b")} c"`
    // ended at the inner `"`, and its code was read as strings. `｢…｣` has no
    // escapes.
    void scanQuoted(size_t open, std::initializer_list<const char*> closers, bool interp, bool escapes = true) {
        size_t j = i + open;
        while (j < s.size()) {
            if (escapes && s[j] == '\\') { j += 2; continue; }
            if (interp && s[j] == '{') { j = blockEnd(j); continue; }
            size_t n = 0;
            for (const char* c : closers) if (s.compare(j, std::strlen(c), c) == 0) { n = std::strlen(c); break; }
            if (n) { j += n; break; }
            j++;
        }
        j = std::min(j, s.size());
        emit(s.substr(i, j - i), "s", true);
        i = j;
    }
    void scanQuote(char q) { scanQuoted(1, {q == '"' ? "\"" : "'"}, q == '"'); }

    // Just past the `}` closing the block that opens at k; a string in the
    // block is skipped whole
    size_t blockEnd(size_t k) const {
        int d = 0;
        while (k < s.size()) {
            const char c = s[k];
            if (c == '\\') { k += 2; continue; }
            if (c == '{') d++;
            else if (c == '}' && --d == 0) return k + 1;
            else if (c == '"' || c == '\'') {
                for (k++; k < s.size() && s[k] != c; k++) if (s[k] == '\\') k++;
            }
            k++;
        }
        return s.size();
    }

    // The Unicode quotes, as Rakudo pairs them: `“…”`, `„…”`/`„…“`, `”…”`
    // interpolate as `"…"` does, `‘…’`, `‚…’`/`‚…‘`, `’…’` do not, and `｢…｣`
    // has no escapes. They were punctuation and names here, so FatRoast left
    // their letters alone and renamed what it took for code in them.
    bool scanUnicodeQuote() {
        if (s.compare(i, 2, "\xE2\x80") == 0 && i + 2 < s.size()) {
            const unsigned char b = (unsigned char)s[i + 2];
            const bool tail = b == 0x9D || b == 0x99;          // `”…”`, `’…’`: only in term position
            if (tail && lastWasValue && !spacedFromListop()) return false;
            switch (b) {
            case 0x9C: case 0x9D: scanQuoted(3, {"\xE2\x80\x9D"}, true); return true;
            case 0x9E: scanQuoted(3, {"\xE2\x80\x9D", "\xE2\x80\x9C"}, true); return true;
            case 0x98: case 0x99: scanQuoted(3, {"\xE2\x80\x99"}, false); return true;
            case 0x9A: scanQuoted(3, {"\xE2\x80\x99", "\xE2\x80\x98"}, false); return true;
            }
            return false;
        }
        if (s.compare(i, 3, "\xEF\xBD\xA2") == 0) { scanQuoted(3, {"\xEF\xBD\xA3"}, false, false); return true; }
        return false;
    }

    // `<word>` immediately after a value: an angle subscript / sym<…> / regex
    // subrule — word-only content (no spaces/operators), closing `>` required.
    // Comparisons never look like this (`$a < $b`, `2 <3 && 4>5` have non-word
    // content or spaces), so `<` stays an operator there.
    bool looksLikeAngleKey() const {
        size_t j = i + 1;
        if (j >= s.size() || !(ascii::isalnum((unsigned char)s[j]) || s[j] == '_')) return false;
        while (j < s.size() && (ascii::isalnum((unsigned char)s[j]) || s[j] == '_' || s[j] == '-' || s[j] == '\'')) j++;
        return j < s.size() && s[j] == '>';
    }

    // Where a `[…]` reduction starting at k ends when its operator holds a `<`
    // or `>` (`[<=]`, `[\<]`, `[R>]`, `[<=>]`), or 0
    size_t reduceOpEnd(size_t k) const {
        size_t j = k + 1;
        if (at(j) == '\\') j++;
        if (at(j) == 'R' || at(j) == 'Z' || at(j) == 'X') j++;
        const size_t ops = j;
        bool angle = false;
        while (j < s.size() && std::strchr("<>=!~+-*/%^&|?.", s[j]) && s[j]) { angle |= s[j] == '<' || s[j] == '>'; j++; }
        return angle && j > ops && at(j) == ']' ? j + 1 : 0;
    }
    bool spacedFromListop() const {
        if (listopEnd == std::string::npos || i <= listopEnd) return false;
        for (size_t k = listopEnd; k < i; k++) if (s[k] != ' ' && s[k] != '\t') return false;
        return true;
    }
    bool afterListop() const {
        if (!spacedFromListop()) return false;
        const char n = at(i + 1);
        return n && !ascii::isspace((unsigned char)n) && n != '=' && n != '<' && n != '>';
    }

    bool looksLikeQw() const {
        // `<...>` in term position with word-ish content and a closing `>` before a newline
        for (size_t j = i + 1; j < s.size(); j++) {
            if (s[j] == '>') return true;
            if (s[j] == '\n' || s[j] == '<') return false;
        }
        return false;
    }

    void scanAngleQw() {
        size_t j = i + 1;
        while (j < s.size() && s[j] != '>') j++;
        if (j < s.size()) j++;
        emit(s.substr(i, j - i), "s", true);
        i = j;
    }

    // Scan a `q`/`qq`/`Q`/`m`/`rx`/`s`/`tr` quote-like form: the leading word is
    // already consumed by the caller as `kwLen` bytes; scan adverbs + delimited body
    // (two bodies for s///, tr///). `cls` is the resulting class.
    void scanQuoteForm(size_t startBol, size_t bodyStart, const char* cls, bool twoParts) {
        size_t j = bodyStart;
        while (j < s.size() && (s[j] == ' ' || s[j] == '\t')) j++;
        // adverbs like :i :g :s:m — and `:to`/`:heredoc`, which makes the
        // delimited text a TERMINATOR rather than the string's content.
        bool heredoc = false;
        while (j < s.size() && s[j] == ':') {
            size_t advStart = ++j;
            if (j < s.size() && s[j] == '!') j++;   // a negated adverb: `qq:!c/…/`
            while (j < s.size() && (identCont(s[j]) || s[j] == '(')) { if (s[j]=='('){int d=1;j++;while(j<s.size()&&d){if(s[j]=='(')d++;else if(s[j]==')')d--;j++;}break;} j++; }
            std::string adv = s.substr(advStart, j - advStart);
            if (adv == "to" || adv == "heredoc") heredoc = true;
            while (j<s.size()&&(s[j]==' '||s[j]=='\t'))j++;
        }
        if (j >= s.size()) { emit(s.substr(startBol, j - startBol), cls, true); i = j; return; }
        std::string open, close;
        delimPair(j, open, close);
        auto scanBody = [&](size_t from) -> size_t {
            size_t k = from + open.size();
            int depth = 1;
            while (k < s.size() && depth) {
                if (s[k] == '\\') { k += 2; continue; }
                if (open != close && s.compare(k, open.size(), open) == 0) { depth++; k += open.size(); continue; }
                if (s.compare(k, close.size(), close) == 0) { depth--; k += close.size(); continue; }
                k++;
            }
            return k;
        };
        size_t k = scanBody(j);
        if (twoParts) {
            if (open == close) { k = scanBody(k - close.size()); }  // s/a/b/ shares middle delim
            else { while (k < s.size() && (s[k]==' '||s[k]=='\t')) k++; if (k < s.size()) k = scanBody(k); } // s[a][b]
        }
        if (heredoc) {
            // the delimited text is the terminator word, not the body
            size_t tb = j + open.size(), te = k >= close.size() ? k - close.size() : k;
            std::string term = te > tb ? s.substr(tb, te - tb) : std::string();
            size_t a = term.find_first_not_of(" \t");
            size_t b = term.find_last_not_of(" \t\r");
            if (a != std::string::npos) pendingHeredocs.emplace_back(term.substr(a, b - a + 1), cls);
        }
        emit(s.substr(startBol, k - startBol), cls, true);
        i = k;
    }

    void scanRegex() {
        // bare /.../ in term position. A `#` inside a regex starts a COMMENT
        // that runs to the end of the line, and a `/` in that comment does not
        // close the regex — `/ a  # mentions / and }` … `b /` is one regex over
        // two lines. Reading the comment's slash as the closer left the rest of
        // the pattern classified as code, which is how `--fmt` came to reindent
        // the inside of a regex; the semantic gate refused the file rather than
        // corrupt it, and this is the fix behind that refusal.
        size_t j = i + 1;
        while (j < s.size()) {
            if (s[j] == '\\') { j += 2; continue; }
            if (s[j] == '#') {
                // `#\`( … )` is an EMBEDDED comment, delimited and nestable,
                // not a run to the end of the line — and its content may hold
                // the regex's own closer. Treating it as a line comment
                // swallowed the rest of the line instead, which the semantic
                // gate then caught as a changed program.
                if (j + 2 < s.size() && s[j + 1] == '`' &&
                    (s[j + 2] == '(' || s[j + 2] == '[' || s[j + 2] == '{' || s[j + 2] == '<')) {
                    char open = s[j + 2], close = closeDelim(open);
                    size_t m = j + 3;
                    int d = 1;
                    while (m < s.size() && d > 0) {
                        if (s[m] == open) d++;
                        else if (s[m] == close) d--;
                        m++;
                    }
                    j = m;
                    continue;
                }
                while (j < s.size() && s[j] != '\n') j++;
                continue;
            }
            if (s[j] == '/') { j++; break; }
            j++;
        }
        emit(s.substr(i, j - i), "sr", true);
        i = j;
    }

    void scanNumber() {
        size_t j = i;
        const char* cls = "mi";
        if (s[j] == '0' && (at(j + 1) == 'x' || at(j + 1) == 'X')) {
            cls = "mh"; j += 2; while (j < s.size() && (ascii::isxdigit((unsigned char)s[j]) || s[j] == '_')) j++;
        } else if (s[j] == '0' && (at(j + 1) == 'o' || at(j + 1) == 'b')) {
            cls = (at(j + 1) == 'b') ? "mb" : "mo"; j += 2; while (j < s.size() && (identCont(s[j]) || s[j] == '_')) j++;
        } else {
            while (j < s.size() && (ascii::isdigit((unsigned char)s[j]) || s[j] == '_')) j++;
            // fractional part (but not a `..` range)
            if (j < s.size() && s[j] == '.' && ascii::isdigit((unsigned char)at(j + 1))) {
                cls = "mf"; j++; while (j < s.size() && (ascii::isdigit((unsigned char)s[j]) || s[j] == '_')) j++;
            }
            if (j < s.size() && (s[j] == 'e' || s[j] == 'E')) { // exponent
                size_t k = j + 1; if (k < s.size() && (s[k] == '+' || s[k] == '-')) k++;
                if (k < s.size() && ascii::isdigit((unsigned char)s[k])) { cls = "mf"; j = k; while (j < s.size() && ascii::isdigit((unsigned char)s[j])) j++; }
            }
        }
        emit(s.substr(i, j - i), cls, true);
        i = j;
    }

    void scanVariable() {
        size_t j = i + 1;
        // twigil
        if (j < s.size() && (s[j] == '.' || s[j] == '!' || s[j] == '*' || s[j] == '?' ||
                             s[j] == '^' || s[j] == ':' || s[j] == '<' || s[j] == '=' || s[j] == '~')) {
            if (s[j] == '<') { // $<capture>
                j++; while (j < s.size() && s[j] != '>') j++; if (j < s.size()) j++;
                emit(s.substr(i, j - i), "nv", true); i = j; return;
            }
            j++;
        }
        // special single-char vars: $/ $! $_ $0 $¢
        if (j < s.size() && (s[j] == '/' || s[j] == '!' || ascii::isdigit((unsigned char)s[j]))) {
            while (j < s.size() && ascii::isdigit((unsigned char)s[j])) j++;
            if (j == i + 1 && (s[j] == '/' || s[j] == '!')) j++;
            emit(s.substr(i, j - i), "nv", true); i = j; return;
        }
        // name (allowing internal - and ' and ::)
        j = nameEnd(j);
        if (j == i + 1) { // lone sigil (e.g. `&` as code ref sigil, or `%` operator misread)
            // only treat $ @ % & as variable sigils if a name/twigil followed; else operator
            emitText(s.substr(i, 1)); i = j; return;
        }
        emit(s.substr(i, j - i), "nv", true);
        i = j;
    }

    void scanIdent() {
        size_t j = nameEnd(i);
        std::string w = s.substr(i, j - i);

        // quote-like / regex-like word forms: q qq Q qw qqw (strings); m rx ms (regex);
        // s tr (regex, two-part). Only when a delimiter/adverb follows.
        auto formFollows = [&]() -> bool {
            size_t k = j; while (k < s.size() && (s[k] == ' ' || s[k] == '\t')) k++;
            if (k < s.size() && s[k] == ':') return true;                 // adverb → quote form
            // a name tight against `(` is always a call: `q($a, $b)` calls a `sub q`
            // (S02-literals/quoting.t), and only `q ($a)` quotes
            if (k == j && k < s.size() && s[k] == '(') return false;
            if (k < s.size() && (s[k]=='/'||s[k]=='{'||s[k]=='['||s[k]=='('||s[k]=='<'||s[k]=='!'||s[k]=='|'||s[k]=='\''||s[k]=='"')) return true;
            // An ADJACENT non-ASCII delimiter: `q«a b»`, `Q♥…♥`, `q:to｢END｣`.
            // Adjacency is what the lexer requires of these too — with no adverb
            // to commit the form, `q ♥…♥` is a call to a declared `q`.
            if (j < s.size() && (unsigned char)s[j] >= 0x80) return true;
            return false;
        };
        static const std::set<std::string> strForms = {"q", "qq", "Q", "qw", "qqw", "Qw", "qww"};
        static const std::set<std::string> rxForms = {"m", "rx", "ms", "mm"};
        static const std::set<std::string> substForms = {"s", "S", "tr", "TR", "Tr"};
        if (!methodNext && !declNext && formFollows()) {   // (`sub s { … }` declares a routine)
            if (strForms.count(w)) { scanQuoteForm(i, j, "s", false); return; }
            if (rxForms.count(w))  { scanQuoteForm(i, j, "sr", false); return; }
            if (substForms.count(w)) { scanQuoteForm(i, j, "sr", true); return; }
        }

        static const std::set<std::string> terms = {"self", "now", "time", "pi", "e", "i", "tau", "rand"};
        const bool listop = !methodNext && !declNext && ascii::islower((unsigned char)w[0]) && !terms.count(w);
        const char* cls;
        bool introduces = false;
        if (methodNext) {
            // a method call — never a keyword. This is the parse-aware fix: `$obj.role`
            // colours `role` as a method (n), not the `role` keyword (k). Built-in method
            // names (`.new`, `.say`) still get nb, matching a lexical highlighter.
            cls = isBuiltin(w) ? "nb" : "n";
        } else if (declNext) {
            // a declaration name — `method role {…}` names a method `role`, not a keyword.
            cls = "n";
        } else if (isKeyword(w)) {
            cls = "k";
            introduces = isNameIntroducer(w); // arms declNext for the name that follows
            // A NAMED REGEX body is not code. `token body { … }` holds a
            // pattern, and in a `rule` the whitespace in it is significant
            // (:sigspace) — so it is a regex span, the way a heredoc body is a
            // string span, and for the same reason: a tool that rewrites
            // whitespace between spans must not reach inside it. `--fmt`
            // re-indenting a grammar's rule bodies is what surfaced this; the
            // semantic gate refused five corpus files until it was fixed.
            if (w == "token" || w == "rule" || w == "regex") {
                size_t p = j;                     // `j` is just past the keyword
                while (p < s.size() && (s[p] == ' ' || s[p] == '\t')) p++;
                size_t nameStart = p;
                while (p < s.size() && (identCont(s[p]) || s[p] == '-' || s[p] == ':')) p++;
                // `token infix:sym<+>`, `rule t:sym«a b»`: the brackets are the
                // name's, and the body after them a pattern (it was read as code)
                if (s.substr(nameStart, p - nameStart).find(':') != std::string::npos &&
                    (at(p) == '<' || s.compare(p, 2, "\xC2\xAB") == 0)) {
                    const std::string close = at(p) == '<' ? ">" : "\xC2\xBB";
                    const size_t e = s.find(close, p + 1);
                    if (e != std::string::npos && s.find('\n', p) > e) p = e + close.size();
                }
                size_t nameEnd = p;
                while (p < s.size() && (s[p] == ' ' || s[p] == '\t')) p++;
                if (p < s.size() && s[p] == '(') { int d = 1; p++; while (p < s.size() && d) { if (s[p]=='(') d++; else if (s[p]==')') d--; p++; } while (p < s.size() && (s[p]==' '||s[p]=='\t')) p++; }
                if (nameEnd > nameStart && p < s.size() && s[p] == '{') {
                    emit(std::move(w), cls, true);                 // the declarator
                    emit(s.substr(j, p - j), "", false);           // name and signature
                    // Find the body's end by braces — but a pattern QUOTES its
                    // braces (`token block { '{' <statementlist> '}' }`) and
                    // puts them in character classes, and counting those ends
                    // the body in the wrong place. Skip a quoted run, a `<[…]>`
                    // class, and an embedded comment before counting.
                    size_t k = p + 1;
                    int d = 1;
                    while (k < s.size() && d) {
                        const char c = s[k];
                        if (c == '\\') { k += 2; continue; }
                        if (c == '\'' || c == '"') {
                            const char q = c;
                            k++;
                            while (k < s.size() && s[k] != q) { if (s[k] == '\\') k++; k++; }
                            k++;
                            continue;
                        }
                        if (c == '<' && k + 1 < s.size() && (s[k + 1] == '[' || s[k + 1] == '-')) {
                            while (k < s.size() && s[k] != '>') { if (s[k] == '\\') k++; k++; }
                            k++;
                            continue;
                        }
                        if (c == '#') {
                            if (k + 2 < s.size() && s[k + 1] == '`' &&
                                (s[k + 2] == '(' || s[k + 2] == '[' || s[k + 2] == '{' || s[k + 2] == '<')) {
                                char o = s[k + 2], cl = closeDelim(o);
                                size_t m = k + 3; int dd = 1;
                                while (m < s.size() && dd) { if (s[m] == o) dd++; else if (s[m] == cl) dd--; m++; }
                                k = m; continue;
                            }
                            while (k < s.size() && s[k] != '\n') k++;
                            continue;
                        }
                        if (c == '{') d++;
                        else if (c == '}') d--;
                        k++;
                    }
                    // The braces stay CODE and only the interior is the
                    // pattern. A consumer counting brackets has to see them —
                    // `--fmt` indented a grammar's closing `}` by one level
                    // because the `}` was inside the span and its line looked
                    // like a statement that had not ended.
                    emit(s.substr(p, 1), "", false);               // `{`
                    if (k > p + 2) emit(s.substr(p + 1, k - p - 2), "sr", true);
                    if (k > p + 1) emit(s.substr(k - 1, 1), "", false);   // `}`
                    declNext = false;
                    i = k;
                    return;
                }
            }
        } else if (isBuiltin(w)) {
            cls = "nb";
        } else {
            cls = "n";
        }
        emit(std::move(w), cls, true);
        declNext = introduces;
        i = j;
        listopEnd = listop ? j : std::string::npos;
    }

    // Where a HYPER operator starting at k ends — `>>+<<`, `<<~>>`, `»×«`,
    // `>>[&infix:<+>]<<` — or 0 when none starts there: a hyper bracket, an
    // infix spelled in punctuation or one in `[…]`, and a hyper bracket again,
    // with no space outside the brackets.
    size_t hyperOpEnd(size_t k) const {
        auto bracket = [&](size_t p) -> size_t {
            if ((at(p) == '>' && at(p + 1) == '>') || (at(p) == '<' && at(p + 1) == '<')) return 2;
            if ((unsigned char)at(p) == 0xC2 && ((unsigned char)at(p + 1) == 0xBB || (unsigned char)at(p + 1) == 0xAB)) return 2;
            return 0;
        };
        size_t b = bracket(k);
        if (!b) return 0;
        size_t j = k + b, body = j;
        // a word infix: `>>dot<<`, `»min«`, and after a term and a space
        // `<<cmp>>` (tight, `%h<<a>>` is a subscript; in term position a list)
        if (nameCharAt(j, true)) {
            const bool opens = at(k) == '>' || (unsigned char)at(k + 1) == 0xBB;
            if (!opens && !(lastWasValue && k > 0 && ascii::isspace((unsigned char)s[k - 1]))) return 0;
            const size_t e = nameEnd(j);
            return bracket(e) ? e + bracket(e) : 0;
        }
        if (at(j) == '[') {   // a bracketed infix: `>>[+]<<`, `»[&infix:<+>]«`
            for (int d = 0; j < s.size(); j++) {
                if (s[j] == '\n') return 0;
                if (s[j] == '[') d++;
                else if (s[j] == ']' && --d == 0) { j++; break; }
            }
            return j < s.size() && bracket(j) ? j + bracket(j) : 0;
        }
        while (j < s.size() && !bracket(j)) {
            unsigned char c = (unsigned char)s[j];
            if (c < 0x80 && (ascii::isspace(c) || ascii::isalnum(c) || c == '_' || c == '\'' || c == '"' ||
                             c == '(' || c == ')' || c == '{' || c == '}' || c == ';' || c == '<' || c == '>' ||
                             c == '$' || c == '@' || c == '#')) return 0;
            if (c >= 0x80 && nameCharAt(j, true)) return 0;
            j += c < 0x80 ? 1 : (size_t)utf8Len(c);
        }
        if (j == body || j >= s.size()) return 0;
        return j + bracket(j);
    }

    // Where a `<<…>>` or `«…»` word list starting at k ends, or 0. It may run
    // over lines and quote a word (`<<a "b >> c">>`). After a term — a
    // subscript (`%h<<a>>`, `%h«$k»`), or a list after a listop name, which
    // reads as a term here too (`say <<a b>>`) — it starts tight, so the
    // spaced `$x << 1` is left alone.
    size_t doubleAngleEnd(size_t k) const {
        std::string close;
        if (at(k) == '<' && at(k + 1) == '<') close = ">>";
        else if ((unsigned char)at(k) == 0xC2 && (unsigned char)at(k + 1) == 0xAB) close = "\xC2\xBB";
        else return 0;
        size_t j = k + 2;
        if (lastWasValue && (j >= s.size() || ascii::isspace((unsigned char)s[j]))) return 0;
        // tight after a prefix operator it is that operator's HYPER marker:
        // `~<<@a`, `-<<$x`, `!«@b`
        if (k > 0 && std::strchr("~-+!?^", s[k - 1]) && !ascii::isspace((unsigned char)at(j))) return 0;
        while (j < s.size()) {
            if (s.compare(j, close.size(), close) == 0) return j + close.size();
            if (s[j] == '\\') { j += 2; continue; }
            if (s[j] == '"' || s[j] == '\'') {
                const char q = s[j];
                for (j++; j < s.size() && s[j] != q; j++) if (s[j] == '\\') j++;
            }
            j++;
        }
        return 0;
    }

    void scanOperator() {
        // A method-call dot (or private `!`) arms methodNext for the following name.
        char c = cur();
        // A shift after a term is one operator, `=` form included: read by the
        // byte, the `<` of `$v +<= 1` began a word list in term position
        if ((c == '+' || c == '~') && lastWasValue && (at(i + 1) == '<' || at(i + 1) == '>') && at(i + 2) != at(i + 1)) {
            size_t j = i + 2;
            if (at(j) == '=') j++;
            emitText(s.substr(i, j - i));
            lastWasValue = false; methodNext = false; declNext = false;
            i = j;
            return;
        }
        // A hyper operator is one operator. Byte by byte, the `<<` that closes
        // `$a >>+<< $b` looked like a `<…>` word list in term position, and the
        // rest of the line up to the next `>` became a string: FatRoast mapped
        // letters in that "string" and missed the names in it.
        if (size_t e = hyperOpEnd(i)) {
            emitText(s.substr(i, e - i));
            lastWasValue = false; methodNext = false; declNext = false;
            i = e;
            return;
        }
        // A range or sequence operator is one operator: read by the byte, the
        // second dot of `1..10` began a number, `.10`.
        if (c == '.' && at(i + 1) == '.') {
            size_t j = i + 2;
            if (at(j) == '.' || at(j) == '^') j++;
            emitText(s.substr(i, j - i));
            lastWasValue = false; methodNext = false; declNext = false;
            i = j;
            return;
        }
        // A `*` where a term is wanted is the Whatever, and a term itself: what
        // follows it is an operator, so `(* < 5)` is no `< … >` word list and
        // `* / 2` no regex. After a term it is multiplication (`**` the power).
        if (c == '*') {
            const bool whatever = !lastWasValue;
            size_t j = i + 1;
            if (!whatever && at(j) == '*') j++;
            emitText(s.substr(i, j - i));
            lastWasValue = whatever; methodNext = false; declNext = false;
            i = j;
            return;
        }
        // detect `.` / `.^` / `.?` / `.+` / `.*` / `.&` / `!` method introducers
        if (c == '.' && at(i + 1) != '.') {
            size_t j = i + 1;
            if (j < s.size() && (s[j] == '^' || s[j] == '?' || s[j] == '+' || s[j] == '*' || s[j] == '&')) j++;
            emitText(s.substr(i, j - i));
            methodNext = true; lastWasValue = false; declNext = false;
            i = j;
            return;
        }
        // everything else: emit one byte as plain text; clears method/decl/value state.
        // (grouping several operator bytes doesn't matter — all are unspanned.)
        emitText(s.substr(i, 1));
        lastWasValue = (c == ')' || c == ']');
        methodNext = false; declNext = false;
        i++;
    }
};
} // namespace

// ---- renderers -----------------------------------------------------------------

static void htmlEscapeInto(const std::string& t, std::string& out) {
    for (char c : t) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
        }
    }
}

static std::string renderHtml(const std::vector<Span>& spans) {
    std::string out = "<div class=\"highlight\"><pre><span></span>";
    for (const auto& sp : spans) {
        if (sp.cls[0]) { out += "<span class=\""; out += sp.cls; out += "\">"; htmlEscapeInto(sp.text, out); out += "</span>"; }
        else htmlEscapeInto(sp.text, out);
    }
    out += "</pre></div>\n";
    return out;
}

// Map a Pygments class to an ANSI SGR sequence (light-theme intent). "" = default.
static const char* ansiFor(const char* cls) {
    if (!cls[0]) return "";
    switch (cls[0]) {
        case 'k': return "\x1b[32m";                 // keyword → green
        case 'c': return "\x1b[3;36m";               // comment → italic cyan
        case 's': return cls[1] == 'r' ? "\x1b[35m"  // regex → magenta
                                       : "\x1b[31m"; // string → red
        case 'm': return "\x1b[36m";                 // number → cyan
        case 'o': return "\x1b[90m";                 // operator → bright black
        case 'n':
            if (cls[1] == 'v') return "\x1b[34m";    // variable → blue
            if (cls[1] == 'b') return "\x1b[32m";    // builtin  → green
            if (cls[1] == 'f') return "\x1b[1;34m";  // function → bold blue
            return "";                               // plain name → default
        default: return "";
    }
}

static std::string renderAnsi(const std::vector<Span>& spans) {
    std::string out;
    for (const auto& sp : spans) {
        const char* a = ansiFor(sp.cls);
        if (a[0]) { out += a; out += sp.text; out += "\x1b[0m"; }
        else out += sp.text;
    }
    return out;
}

// The span stream itself, for a consumer that is not a renderer — `--fmt`
// rewrites the plain-text runs between spans and never the bytes inside one.
// (FMT-PLAN's step 1 calls for moving the scanner into its own
// SourceScan.{h,cpp} with `--highlight` as one renderer over it. That move is
// cosmetic; what a second consumer actually needs is this entry point, so it
// is the part that landed.)
std::vector<Span> scanSpans(const std::string& source) {
    Scanner sc(source);
    sc.run();
    return std::move(sc.out);
}

std::string highlight(const std::string& source, const std::string& format) {
    Scanner sc(source);
    sc.run();
    if (format == "ansi" || format == "terminal") return renderAnsi(sc.out);
    return renderHtml(sc.out);
}

} // namespace rakupp
