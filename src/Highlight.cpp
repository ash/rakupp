#include "Highlight.h"
#include <cctype>
#include <set>
#include <string>
#include <vector>

namespace rakupp {

// A classified run of source text. `cls` is the Pygments short class ("" = plain
// text, emitted without a <span> — matching how Pygments leaves operators bare).
struct Span {
    std::string text;
    const char* cls; // "" for plain text
};

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
        "fail", "abort", "exit", "sleep", "now", "time", "prompt", "dd",
        "defined", "sqrt", "abs", "floor", "ceiling", "round", "truncate",
        "exp", "log", "log10", "sin", "cos", "tan", "asin", "acos", "atan",
        "atan2", "sign", "min", "max", "sum", "sum", "produce", "roundrobin",
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
        "so", "not", "chars", "bytes", "hyper", "race",
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
    // The previous keyword introduces a routine/package/constant name (`method`, `sub`,
    // `class`, `role`, …), so the next identifier is that NAME even if spelled like a
    // keyword — e.g. `method role { … }` declares a method called `role`.
    bool declNext = false;

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

    static bool identStart(char c) { return std::isalpha((unsigned char)c) || c == '_'; }
    static bool identCont(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

    // matching close for a bracket delimiter (else the same char)
    static char closeDelim(char o) {
        switch (o) { case '(': return ')'; case '[': return ']';
                     case '{': return '}'; case '<': return '>';
                     case '\xAB': return '\xBB'; default: return o; }
    }

    void run() {
        while (i < s.size()) {
            char c = cur();
            if (c == '\n' || c == ' ' || c == '\t' || c == '\r') { scanSpace(); continue; }
            if (c == '#') { scanLineComment(); continue; }
            if (c == '=' && atLineStart() && identStart(at(i + 1))) { scanPod(); continue; }
            if (c == '\'' || c == '"') { scanQuote(c); continue; }
            if (c == '<' && !lastWasValue && looksLikeQw()) { scanAngleQw(); continue; }
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
            if (std::isdigit((unsigned char)c) ||
                (c == '.' && std::isdigit((unsigned char)at(i + 1)) && !lastWasValue)) { scanNumber(); continue; }
            if (c == '$' || c == '@' || c == '%' || c == '&') { scanVariable(); continue; }
            if (identStart(c)) { scanIdent(); continue; }
            scanOperator();
        }
    }

    bool atLineStart() const { return i == 0 || s[i - 1] == '\n'; }

    void scanSpace() {
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

    void scanQuote(char q) {
        size_t j = i + 1;
        while (j < s.size()) {
            if (s[j] == '\\') { j += 2; continue; }
            if (s[j] == q) { j++; break; }
            j++;
        }
        emit(s.substr(i, j - i), "s", true);
        i = j;
    }

    // `<word>` immediately after a value: an angle subscript / sym<…> / regex
    // subrule — word-only content (no spaces/operators), closing `>` required.
    // Comparisons never look like this (`$a < $b`, `2 <3 && 4>5` have non-word
    // content or spaces), so `<` stays an operator there.
    bool looksLikeAngleKey() const {
        size_t j = i + 1;
        if (j >= s.size() || !(std::isalnum((unsigned char)s[j]) || s[j] == '_')) return false;
        while (j < s.size() && (std::isalnum((unsigned char)s[j]) || s[j] == '_' || s[j] == '-' || s[j] == '\'')) j++;
        return j < s.size() && s[j] == '>';
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
        // adverbs like :i :g :s:m
        while (j < s.size() && s[j] == ':') { j++; while (j < s.size() && (identCont(s[j]) || s[j] == '(')) { if (s[j]=='('){int d=1;j++;while(j<s.size()&&d){if(s[j]=='(')d++;else if(s[j]==')')d--;j++;}break;} j++; } while (j<s.size()&&(s[j]==' '||s[j]=='\t'))j++; }
        if (j >= s.size()) { emit(s.substr(startBol, j - startBol), cls, true); i = j; return; }
        char open = s[j];
        char close = closeDelim(open);
        auto scanBody = [&](size_t from) -> size_t {
            size_t k = from + 1;
            if (open == close) { // non-bracketing delimiter
                while (k < s.size()) { if (s[k] == '\\') { k += 2; continue; } if (s[k] == close) { k++; break; } k++; }
            } else { // bracketing: track nesting
                int depth = 1; k = from + 1;
                while (k < s.size() && depth) { if (s[k] == '\\') { k += 2; continue; } if (s[k] == open) depth++; else if (s[k] == close) depth--; k++; }
            }
            return k;
        };
        size_t k = scanBody(j);
        if (twoParts) {
            if (open == close) { k = scanBody(k - 1); }            // s/a/b/ shares middle delim
            else { while (k < s.size() && (s[k]==' '||s[k]=='\t')) k++; if (k < s.size()) k = scanBody(k); } // s[a][b]
        }
        emit(s.substr(startBol, k - startBol), cls, true);
        i = k;
    }

    void scanRegex() {
        // bare /.../ in term position
        size_t j = i + 1;
        while (j < s.size()) { if (s[j] == '\\') { j += 2; continue; } if (s[j] == '/') { j++; break; } j++; }
        emit(s.substr(i, j - i), "sr", true);
        i = j;
    }

    void scanNumber() {
        size_t j = i;
        const char* cls = "mi";
        if (s[j] == '0' && (at(j + 1) == 'x' || at(j + 1) == 'X')) {
            cls = "mh"; j += 2; while (j < s.size() && (std::isxdigit((unsigned char)s[j]) || s[j] == '_')) j++;
        } else if (s[j] == '0' && (at(j + 1) == 'o' || at(j + 1) == 'b')) {
            cls = (at(j + 1) == 'b') ? "mb" : "mo"; j += 2; while (j < s.size() && (identCont(s[j]) || s[j] == '_')) j++;
        } else {
            while (j < s.size() && (std::isdigit((unsigned char)s[j]) || s[j] == '_')) j++;
            // fractional part (but not a `..` range)
            if (j < s.size() && s[j] == '.' && std::isdigit((unsigned char)at(j + 1))) {
                cls = "mf"; j++; while (j < s.size() && (std::isdigit((unsigned char)s[j]) || s[j] == '_')) j++;
            }
            if (j < s.size() && (s[j] == 'e' || s[j] == 'E')) { // exponent
                size_t k = j + 1; if (k < s.size() && (s[k] == '+' || s[k] == '-')) k++;
                if (k < s.size() && std::isdigit((unsigned char)s[k])) { cls = "mf"; j = k; while (j < s.size() && std::isdigit((unsigned char)s[j])) j++; }
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
        if (j < s.size() && (s[j] == '/' || s[j] == '!' || std::isdigit((unsigned char)s[j]))) {
            while (j < s.size() && std::isdigit((unsigned char)s[j])) j++;
            if (j == i + 1 && (s[j] == '/' || s[j] == '!')) j++;
            emit(s.substr(i, j - i), "nv", true); i = j; return;
        }
        // name (allowing internal - and ' and ::)
        while (j < s.size() && (identCont(s[j]) ||
               ((s[j] == '-' || s[j] == '\'') && identCont(at(j + 1))) ||
               (s[j] == ':' && at(j + 1) == ':'))) {
            if (s[j] == ':') j += 2; else j++;
        }
        if (j == i + 1) { // lone sigil (e.g. `&` as code ref sigil, or `%` operator misread)
            // only treat $ @ % & as variable sigils if a name/twigil followed; else operator
            emitText(s.substr(i, 1)); i = j; return;
        }
        emit(s.substr(i, j - i), "nv", true);
        i = j;
    }

    void scanIdent() {
        size_t j = i;
        while (j < s.size() && (identCont(s[j]) ||
               ((s[j] == '-' || s[j] == '\'') && identCont(at(j + 1))) ||
               (s[j] == ':' && at(j + 1) == ':'))) {
            if (s[j] == ':') j += 2; else j++;
        }
        std::string w = s.substr(i, j - i);

        // quote-like / regex-like word forms: q qq Q qw qqw (strings); m rx ms (regex);
        // s tr (regex, two-part). Only when a delimiter/adverb follows.
        auto formFollows = [&]() -> bool {
            size_t k = j; while (k < s.size() && (s[k] == ' ' || s[k] == '\t')) k++;
            if (k < s.size() && s[k] == ':') return true;                 // adverb → quote form
            if (k < s.size() && (s[k]=='/'||s[k]=='{'||s[k]=='['||s[k]=='('||s[k]=='<'||s[k]=='!'||s[k]=='|'||s[k]=='\''||s[k]=='"')) return true;
            return false;
        };
        static const std::set<std::string> strForms = {"q", "qq", "Q", "qw", "qqw", "Qw", "qww"};
        static const std::set<std::string> rxForms = {"m", "rx", "ms", "mm"};
        static const std::set<std::string> substForms = {"s", "S", "tr", "TR", "Tr"};
        if (!methodNext && formFollows()) {
            if (strForms.count(w)) { scanQuoteForm(i, j, "s", false); return; }
            if (rxForms.count(w))  { scanQuoteForm(i, j, "sr", false); return; }
            if (substForms.count(w)) { scanQuoteForm(i, j, "sr", true); return; }
        }

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
        } else if (isBuiltin(w)) {
            cls = "nb";
        } else {
            cls = "n";
        }
        emit(std::move(w), cls, true);
        declNext = introduces;
        i = j;
    }

    void scanOperator() {
        // A method-call dot (or private `!`) arms methodNext for the following name.
        char c = cur();
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

std::string highlight(const std::string& source, const std::string& format) {
    Scanner sc(source);
    sc.run();
    if (format == "ansi" || format == "terminal") return renderAnsi(sc.out);
    return renderHtml(sc.out);
}

} // namespace rakupp
