// Parser::checkNullRegex — the regex pre-scan the parser runs over a pattern's
// source before the regex compiler sees it: the compile-time refusals Rakudo
// makes (null patterns, unspace, quantifying a non-quantifiable atom, `$!attr`
// in a regex, old-style `%h<…>` interpolation, …). Split out of Parser.cpp,
// whose size it was; it rides the same `parse` archive (CMakeLists.txt).
#include "AsciiCtype.h"
#include "Parser.h"
#include "Unicode.h"
#include <cstring>
#include <cstdlib>
#include <string>

extern thread_local bool g_rxReservedHash;   // Parser.cpp: the RegexLit reads it

namespace rakupp {

void Parser::checkNullRegex(const std::string& pat, int line, bool branches) {
    // an empty regex (or an empty alternation branch / group) is X::Syntax::
    // Regex::NullRegex — `/ /`, `/ a | /`, `/ [] /`, `/ () /`, `s//x/`.
    // branches=false skips the trailing |/& heuristic (P5 regexes and
    // substitution patterns, where those chars can be literal).
    size_t b = pat.find_first_not_of(" \t\n");
    std::string t = b == std::string::npos
        ? std::string()
        : pat.substr(b, pat.find_last_not_of(" \t\n") - b + 1);
    bool null = t.empty() ||
        (branches && (t == "[]" || t == "()" || t == "[ ]" || t == "( )"));
    if (!null && branches && (t.back() == '|' || t.back() == '&') &&
        (t.size() < 2 || t[t.size() - 2] != '\\'))
        null = true;
    if (null)
        throw ParseError("Null regex not allowed", line,
                         "X::Syntax::Regex::NullRegex", {});
    if (!branches) return;
    // `/\ X/` — an unspace; `/m ** 1 ..2/` — a spaced bare range. Both are
    // compile-time refusals; quotes and character classes are left alone.
    char q = 0; int cls = 0;
    // What the previous significant atom was: nothing yet in this group (a
    // quantifier there quantifies nothing — `/* a/`, `[a| ?]`), or a branch
    // operator (another one right after it is an empty branch — `/b | | d/`,
    // `/a &| b/`). A LEADING `|` or `&` in a group is allowed.
    bool atomStart = true, groupStart = true, afterBranch = false;
    int branchPrec = -1;
    // what the last thing was, for the quantifier checks: an atom, a quantifier
    // (`a+ +` quantifies nothing), or something that matches nothing to repeat
    // (an anchor, a code block, a code assertion — X::Syntax::Regex::NonQuantifiable)
    enum { LkAtom, LkQuant, LkNonQuant } lastKind = LkAtom;
    int grpDepth = 0;   // ( ) / [ ] balance — a `#` comment can swallow a closer
    auto nonQuantifiable = [&]() {
        throw ParseError("Can only quantify a construct that produces a match", line,
                         "X::Syntax::Regex::NonQuantifiable", {});
    };
    // `/$!a/`, `/<$!a>/`, `/{ $!a }/` — a regex is a method on its cursor,
    // not on the class that wrote it, so an attribute is not reachable
    auto attrInRegex = [&](size_t at) {
        size_t e = at + 2;
        while (e < pat.size() && (ascii::isalnum((unsigned char)pat[e]) || pat[e] == '_' || pat[e] == '-')) e++;
        std::string sym = pat.substr(at, e - at);
        throw ParseError("Attribute " + sym + " not available inside of a regex, since regexes are "
                         "methods on Cursor.\nConsider storing the attribute in a lexical, and using that in the regex.",
                         line, "X::Attribute::Regex", {{"symbol", sym}});
    };
    for (size_t i = 0; i < pat.size(); i++) {
        char c = pat[i];
        if (q) { if (c == '\\') i++; else if (c == q) q = 0; continue; }
        if (cls) {
            if (c == '\\') {
                // `\c[NAME, NAME]` / `\x[41]` / `\o[101]`: the bracket belongs to
                // the escape, and a `-` inside a character NAME is not a range
                if (i + 2 < pat.size() && std::strchr("cCxXoO", pat[i + 1]) && pat[i + 2] == '[') {
                    size_t e = pat.find(']', i + 3);
                    if (e != std::string::npos) { i = e; continue; }
                }
                i++; continue;
            }
            if (c == ']') {
                cls--;
                // `<[abc] [def]>` — two members need a `+` or `-` between them
                size_t j = i + 1;
                while (j < pat.size() && (pat[j] == ' ' || pat[j] == '\t' || pat[j] == '\n')) j++;
                if (j < pat.size() && pat[j] == '[')
                    throw ParseError("Missing + or - between character class members in regex", line,
                                     "X::Syntax::Regex::Unterminated", {});
                // `<+[\S] -[#]>` — the class goes on after a `+`/`-` member
                if (cls == 0 && j + 1 < pat.size() && (pat[j] == '+' || pat[j] == '-')) {
                    size_t k = j + 1;
                    while (k < pat.size() && (pat[k] == ' ' || pat[k] == '\t' || pat[k] == '\n')) k++;
                    if (k < pat.size() && pat[k] == '[') { cls = 1; i = k; }
                }
                continue;
            }
            // `<[a-z]>` — Perl's range spelling
            if (c == '-' && i > 0 && i + 1 < pat.size() && ascii::isalnum((unsigned char)pat[i - 1]) &&
                !(i > 1 && pat[i - 2] == '\\') && ascii::isalnum((unsigned char)pat[i + 1]))
                throw ParseError("Unsupported use of - as character range. In Raku please use: .. for range, "
                                 "for explicit - in character class, escape it or place it as last thing.",
                                 line, "X::Obsolete",
                                 {{"old", "- as character range"},
                                  {"replacement", ".. for range, for explicit - in character class, escape it or place it as last thing"}});
            // `<[Ḍ̇..\x2FFF]>` — a range runs between CODEPOINTS; a grapheme of
            // several cannot be an endpoint
            if ((unsigned char)c >= 0xC0) {
                size_t e = uniClusterEndUtf8(pat, i, pat.size());
                size_t L = (unsigned char)c >= 0xF0 ? 4 : (unsigned char)c >= 0xE0 ? 3 : 2;
                size_t j = e;
                while (j < pat.size() && (pat[j] == ' ' || pat[j] == '\t')) j++;
                if (e > i + L && pat.compare(j, 2, "..") == 0)
                    throw ParseError("Cannot use a synthetic grapheme (" + pat.substr(i, e - i) +
                                     ") as a character class range endpoint", line,
                                     "X::Syntax::Regex::Unterminated", {});
            }
            // `<[d..b]>` — a reversed range is a compile-time error
            if ((unsigned char)c < 0x80 && c != ' ' && c != '\t' && c != '\n') {
                size_t j = i + 1;
                while (j < pat.size() && (pat[j] == ' ' || pat[j] == '\t')) j++;
                if (pat.compare(j, 2, "..") == 0) {
                    j += 2;
                    while (j < pat.size() && (pat[j] == ' ' || pat[j] == '\t')) j++;
                    if (j < pat.size() && (unsigned char)pat[j] < 0x80 && pat[j] != '\\' &&
                        pat[j] != ']' && (unsigned char)pat[j] < (unsigned char)c)
                        throw ParseError("Illegal reversed character range in regex: " +
                                         std::string(1, c) + ".." + std::string(1, pat[j]), line,
                                         "X::Syntax::Regex::ReversedRange", {});
                }
            }
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        // `o{1,3}` — Perl's general quantifier
        if (c == '{' && i > 0 && !atomStart && lastKind == LkAtom &&
            !std::strchr(" \t\n", pat[i - 1])) {
            size_t j = i + 1, d0 = j;
            while (j < pat.size() && ascii::isdigit((unsigned char)pat[j])) j++;
            if (j > d0 && j < pat.size() && (pat[j] == '}' || pat[j] == ',')) {
                size_t k = j;
                if (pat[k] == ',') { k++; while (k < pat.size() && ascii::isdigit((unsigned char)pat[k])) k++; }
                if (k < pat.size() && pat[k] == '}')
                    throw ParseError("Unsupported use of {N,M} as general quantifier. In Raku please use: ** N..M "
                                     "(or ** N..* if there is no maximum).", line, "X::Obsolete",
                                     {{"old", "{N,M} as general quantifier"}, {"replacement", "** N..M (or ** N..* if there is no maximum)"}});
            }
        }
        if (c == '{') {
            int d = 0;
            for (; i < pat.size(); i++) {
                if (pat[i] == '{') d++;
                else if (pat[i] == '}' && --d == 0) break;
                else if (pat[i] == '$' && i + 2 < pat.size() && pat[i + 1] == '!' &&
                         ascii::isalpha((unsigned char)pat[i + 2]))
                    attrInRegex(i);
            }
            atomStart = groupStart = afterBranch = false;
            lastKind = LkNonQuant;
            continue;
        }
        if (c == '|' || c == '&') {
            bool dbl = i + 1 < pat.size() && pat[i + 1] == c;
            // `||` binds loosest, then `&&`, `|`, `&`: a TIGHTER operator right
            // after a looser one leads its branch (`/a |& b/`), anything else
            // leaves an empty branch between them (`/a &| b/`)
            int prec = c == '|' ? (dbl ? 0 : 2) : (dbl ? 1 : 3);
            if (afterBranch && prec <= branchPrec)
                throw ParseError("Null regex not allowed", line, "X::Syntax::Regex::NullRegex", {});
            if (dbl) i++;
            afterBranch = !groupStart || afterBranch;
            if (afterBranch) branchPrec = prec;
            groupStart = false;
            atomStart = true;
            continue;
        }
        // a character-class COMBINATION, `<+digit +[!]>` / `<-[a] + [b]>`: its
        // bracketed sets hold literal characters (a `!` is one), so the whole
        // assertion is skipped to its `>`
        // (a plain `<-[…]>` is left to the checks below: `<-[d..b]>` must still die)
        // (…and `<+:Lu +:name(/SMALL/)>`: a property's call arguments are code,
        // so the skip respects parentheses too)
        if (c == '<' && i + 2 < pat.size() &&
            (pat[i + 1] == '+' || (pat[i + 1] == '-' && (ascii::isalpha((unsigned char)pat[i + 2]) || pat[i + 2] == ':'))) &&
            (pat.find('[', i) != std::string::npos || pat.find('(', i) != std::string::npos)) {
            size_t j = i + 2; int br = 0, pr = 0;
            for (; j < pat.size(); j++) {
                const char cj = pat[j];
                if (cj == '\\') { j++; continue; }
                if (cj == '[') br++;
                else if (cj == ']') br--;
                else if (cj == '(') pr++;
                else if (cj == ')') pr--;
                else if (cj == '>' && br == 0 && pr == 0) break;
            }
            if (j < pat.size()) {
                i = j;
                atomStart = groupStart = afterBranch = false;
                lastKind = LkAtom;
                continue;
            }
        }
        // `<&foo('a')>` / `<at(0)>` / `<foo: 'x)'>` — call arguments are code,
        // not regex: skip them whole
        if (c == '<' && i + 1 < pat.size()) {
            size_t j = i + 1;
            if (std::strchr("&.?!:", pat[j])) j++;
            // (…and the property sigil after a negation: `<!:name(/SMALL/)>`)
            if (j < pat.size() && (pat[j] == '.' || pat[j] == '!' || pat[j] == '+' || pat[j] == '-' ||
                                   (pat[j] == ':' && (pat[j - 1] == '!' || pat[j - 1] == '?')))) j++;
            if (j < pat.size() && pat[j] == ':' && (pat[j - 1] == '+' || pat[j - 1] == '-')) j++;   // `<+:name(…)>`
            size_t n0 = j;
            while (j < pat.size() && (ascii::isalnum((unsigned char)pat[j]) || pat[j] == '_' || pat[j] == '-' ||
                                      (pat[j] == ':' && j + 1 < pat.size() && pat[j + 1] == ':')))
                j += pat[j] == ':' ? 2 : 1;
            const bool symbolic = pat.compare(i + 1, 3, "::(") == 0; // `<::($name)>`
            if (symbolic) j = i + 3;
            if ((j > n0 || symbolic) && j < pat.size() && !(j > 0 && pat[j - 1] == ':' && !symbolic) && (pat[j] == '(' || (pat[j] == ':' && j + 1 < pat.size() && std::strchr(" \t\n", pat[j + 1])))) {
                const char closer = pat[j] == '(' ? ')' : '>';
                int d = 0; char qq = 0;
                for (; j < pat.size(); j++) {
                    char cj = pat[j];
                    if (qq) { if (cj == '\\') j++; else if (cj == qq) qq = 0; continue; }
                    if (cj == '\'' || cj == '"') { qq = cj; continue; }
                    if (cj == '\\') { j++; continue; }
                    if (closer == ')') {
                        if (cj == '(') d++;
                        else if (cj == ')' && --d == 0) break;
                    }
                    else if (cj == '>') break;
                }
                if (j < pat.size()) {
                    i = closer == ')' ? j : j - 1;
                    atomStart = groupStart = afterBranch = false;
                    lastKind = LkAtom;
                    continue;
                }
            }
        }
        // `@( … )` / `$( … )` is CODE: its `)` is no group's, and a `)>` after
        // it (`<thing=@($*ctx.items)>`) closes the assertion, not a capture
        if ((c == '@' || c == '$') && i + 1 < pat.size() && pat[i + 1] == '(') {
            size_t j = i + 1; int d = 0; char qq = 0;
            for (; j < pat.size(); j++) {
                char cj = pat[j];
                if (qq) { if (cj == '\\') j++; else if (cj == qq) qq = 0; continue; }
                if (cj == '\'' || cj == '"') { qq = cj; continue; }
                if (cj == '(') d++;
                else if (cj == ')' && --d == 0) break;
            }
            if (j < pat.size()) {
                i = j;
                atomStart = groupStart = afterBranch = false;
                lastKind = LkAtom;
                continue;
            }
        }
        // `<(` / `)>` are capture markers, not groups — either may stand alone
        if (c == '<' && i + 1 < pat.size() && pat[i + 1] == '(') { i++; continue; }
        if (c == ')' && i + 1 < pat.size() && pat[i + 1] == '>') { i++; continue; }
        if (c == '[' || c == '(') { atomStart = groupStart = true; afterBranch = false; grpDepth++; continue; }
        // `'[' ~? ']'` — the `~` goal operator takes an atom after it, not a quantifier
        if (c == '~' && !(i + 1 < pat.size() && pat[i + 1] == '~') && !(i > 0 && pat[i - 1] == '~')) {
            atomStart = true; afterBranch = false; lastKind = LkNonQuant; continue;
        }
        if (c == ']' || c == ')') grpDepth--;
        // a bare `;` means nothing in a regex (only a `:my …;` declaration ends with one)
        if (c == ';')
            throw ParseError("Unrecognized regex metacharacter ; (must be quoted to match literally)", line,
                             "X::Syntax::Regex::UnrecognizedMetachar", {{"metachar", ";"}});
        // …nor does a bare `!` (only `<!…>`, `:!adverb` and the `*!`-style greed
        // markers use one), nor a `-` where an atom should start
        if ((c == '!' && (i == 0 || !std::strchr("<:*+?!$@&%", pat[i - 1]))) || (c == '-' && atomStart))
            throw ParseError(std::string("Unrecognized regex metacharacter ") + c +
                                 " (must be quoted to match literally)", line,
                             "X::Syntax::Regex::UnrecognizedMetachar", {{"metachar", std::string(1, c)}});
        // `:11` — digits make a modifier only with a letter after them (`:2nd`, `:3x`)
        if (c == ':' && i + 1 < pat.size() && ascii::isdigit((unsigned char)pat[i + 1])) {
            size_t j = i + 1;
            while (j < pat.size() && ascii::isdigit((unsigned char)pat[j])) j++;
            if (j >= pat.size() || !ascii::isalpha((unsigned char)pat[j]))
                throw ParseError("Unrecognized regex modifier :" + pat.substr(i + 1, j - i - 1), line,
                                 "X::Syntax::Regex::UnrecognizedModifier", {{"modifier", pat.substr(i + 1, j - i - 1)}});
        }
        // `/:iabc/` — an inline modifier must be one the engine knows
        if (c == ':' && i + 1 < pat.size() && (ascii::isalpha((unsigned char)pat[i + 1]) ||
                                               (pat[i + 1] == '!' && i + 2 < pat.size() && ascii::isalpha((unsigned char)pat[i + 2]))) &&
            (i == 0 || std::strchr(" \t\n[(|&", pat[i - 1])) &&
            // (not a property in a composed class: `<+ :HexDigit - :Upper >`)
            [&]() { size_t b = i; while (b > 0 && std::strchr(" \t\n", pat[b - 1])) b--;
                    return b == 0 || !std::strchr("+-<!?", pat[b - 1]); }()) {
            static const std::set<std::string> kKnown = {
                "i", "ignorecase", "m", "ignoremark", "mm", "samemark", "ii", "samecase", "s", "sigspace",
                "ss", "samespace", "r", "ratchet", "g", "global", "ov", "overlap", "ex", "exhaustive",
                "c", "continue", "p", "pos", "x", "nth", "P5", "Perl5", "a", "aa", "ignoreaccent",
                "sameaccent", "dba", "my", "our", "constant", "state", "temp", "let", "st", "nd", "rd", "th"};
            size_t j = i + 1 + (pat[i + 1] == '!' ? 1 : 0), b = j;
            while (j < pat.size() && ascii::isalnum((unsigned char)pat[j])) j++;
            std::string mod = pat.substr(b, j - b);
            if (!kKnown.count(mod))
                throw ParseError("Unrecognized regex modifier :" + mod, line,
                                 "X::Syntax::Regex::UnrecognizedModifier", {{"modifier", mod}});
        }
        if (c == ':' && i + 1 < pat.size() && ascii::isalpha((unsigned char)pat[i + 1])) {
            // `:my $x = …;` and kin: code up to its `;`
            static const char* kDecl[] = {"my ", "our ", "constant ", "state ", "temp ", "let "};
            for (const char* d : kDecl)
                if (pat.compare(i + 1, std::strlen(d), d) == 0) {
                    int bd = 0; size_t j = i + 1; char qq = 0;
                    for (; j < pat.size(); j++) {
                        char cj = pat[j];
                        if (qq) { if (cj == '\\') j++; else if (cj == qq) qq = 0; continue; }
                        if (cj == '\'' || cj == '"') { qq = cj; continue; }
                        if (cj == '{') bd++;
                        else if (cj == '}') {
                            // `:my token t { … }` ends at a `}` that closes its line
                            if (--bd == 0) {
                                size_t k = j + 1;
                                while (k < pat.size() && (pat[k] == ' ' || pat[k] == '\t')) k++;
                                if (k >= pat.size() || pat[k] == '\n') break;
                            }
                        }
                        else if (cj == ';' && bd == 0) break;
                    }
                    i = j;
                    break;
                }
            if (i >= pat.size()) break;
            if (pat[i] == ';' || pat[i] == '}') continue;
        }
        if ((c == ']' || c == ')') && afterBranch)
            throw ParseError("Null regex not allowed", line, "X::Syntax::Regex::NullRegex", {});
        if ((c == '*' || c == '+' || c == '?') && atomStart)
            throw ParseError("Quantifier quantifies nothing", line,
                             "X::Syntax::Regex::SolitaryQuantifier", {});
        if (c == '*' || c == '+' || c == '?') {
            if (lastKind == LkNonQuant) nonQuantifiable();
            // `a+ +`: a second quantifier, across a blank, quantifies nothing
            // (`a+?` is the frugal modifier and stays legal)
            if (lastKind == LkQuant && i > 0 && std::strchr(" \t\n", pat[i - 1]))
                throw ParseError("Quantifier quantifies nothing", line,
                                 "X::Syntax::Regex::SolitaryQuantifier", {});
            atomStart = groupStart = afterBranch = false;
            lastKind = LkQuant;
            if (c == '*' && i + 1 < pat.size() && pat[i + 1] == '*') { /* `**` — handled below */ }
            else continue;
        }
        // a lone `:` with nothing before it controls no backtracking
        if (c == ':' && atomStart && (i + 1 >= pat.size() || !(ascii::isalpha((unsigned char)pat[i + 1]) ||
                                                               ascii::isdigit((unsigned char)pat[i + 1]) ||
                                                               pat[i + 1] == '!' || pat[i + 1] == ':')))
            throw ParseError("Backtrack control ':' does not seem to have a preceding atom to control", line,
                             "X::Syntax::Regex::SolitaryBacktrackControl", {});
        // anchors match a position, not something to repeat
        if (c == '^' || (c == '$' && (i + 1 >= pat.size() || pat[i + 1] == '$' ||
                                      std::strchr(" \t\n)]|&/", pat[i + 1]) ||
                                      // `$+` is the anchor quantified (`$*FOO`/`$?FILE` are variables)
                                      (std::strchr("+*?", pat[i + 1]) &&
                                       (i + 2 >= pat.size() || !(ascii::isalpha((unsigned char)pat[i + 2]) || pat[i + 2] == '_')))))) {
            if (i + 1 < pat.size() && pat[i + 1] == c) i++;   // `^^` / `$$`
            atomStart = groupStart = afterBranch = false;
            lastKind = LkNonQuant;
            continue;
        }
        if (c == '<' && i + 2 < pat.size() && (pat[i + 1] == '?' || pat[i + 1] == '!') && pat[i + 2] == '{') {
            // `<?{ … }>` / `<!{ … }>` — a code assertion: zero-width
            int d = 0; size_t j = i + 2;
            for (; j < pat.size(); j++) { if (pat[j] == '{') d++; else if (pat[j] == '}' && --d == 0) break; }
            if (j + 1 < pat.size() && pat[j + 1] == '>') {
                i = j + 1;
                atomStart = groupStart = afterBranch = false;
                lastKind = LkNonQuant;
                continue;
            }
        }
        if (!(c == '*' && i + 1 < pat.size() && pat[i + 1] == '*')) lastKind = LkAtom;
        atomStart = groupStart = afterBranch = false;
        if (c == '$' && i + 2 < pat.size() && pat[i + 1] == '!' && ascii::isalpha((unsigned char)pat[i + 2]))
            attrInRegex(i);
        // `/%h/` — a hash variable in a pattern is reserved. Not the alias
        // `%h=( … )`, and not a `%` separator after a quantifier.
        if (c == '%' && i + 1 < pat.size() && ascii::isalpha((unsigned char)pat[i + 1])) {
            size_t j = i + 1;
            while (j < pat.size() && (ascii::isalnum((unsigned char)pat[j]) || pat[j] == '_' || pat[j] == '-')) j++;
            while (j < pat.size() && (pat[j] == ' ' || pat[j] == '\t')) j++;
            size_t b = i;
            while (b > 0 && (pat[b - 1] == ' ' || pat[b - 1] == '\t' || pat[b - 1] == '\n')) b--;
            char prev = b > 0 ? pat[b - 1] : 0;
            bool sepCtx = prev == '*' || prev == '+' || prev == '?' || prev == '}' || prev == '%' ||
                          prev == '<' ||   // `<%h>` — the keys as subrules, not reserved
                          // `@(%h.keys)` / `$(%h<k>)` — a contextualizer holds CODE
                          (prev == '(' && b >= 2 && (pat[b - 2] == '@' || pat[b - 2] == '$')) ||
                          ascii::isdigit((unsigned char)prev);
            // Rakudo refuses this while compiling; here the literal refuses
            // when it is EVALUATED, so a file of old-spec hash-interpolation
            // tests loses the tests that use it rather than all of them
            if (!(j < pat.size() && pat[j] == '=') && !sepCtx) g_rxReservedHash = true;
        }
        if (c == '<') {   // `<IO::File=bar>` — an alias must be a short name
            size_t j = i + 1;
            while (j < pat.size() && (ascii::isalnum((unsigned char)pat[j]) || pat[j] == '_' ||
                                      pat[j] == '-' || pat[j] == ':')) {
                j++;
                if (j < pat.size() && pat[j] == '(' && pat[j - 1] == ':') {
                    int d = 0;
                    for (; j < pat.size(); j++) { if (pat[j] == '(') d++; else if (pat[j] == ')' && --d == 0) { j++; break; } }
                }
            }
            if (j < pat.size() && pat[j] == '=' && pat.substr(i + 1, j - i - 1).find("::") != std::string::npos)
                throw ParseError("Can only alias to a short name (without '::')", line,
                                 "X::Syntax::Regex::Alias::LongName", {});
            // `<...>` is the regex stub — it dies like `...` does in code
            if (pat.compare(i, 5, "<...>") == 0)
                throw ParseError("Unrecognized regex metacharacter < (must be quoted to match literally)",
                                 line, "X::Syntax::Regex::UnrecognizedMetachar", {{"metachar", "<"}});
            // `<name*>` / `<name|>` / `<name&>` — nothing but `>`, `=`, `(`, `:` or
            // whitespace may follow the name of an assertion (Rakudo: FailGoal)
            if (j > i + 1 && j < pat.size() && (ascii::isalpha((unsigned char)pat[i + 1]) || pat[i + 1] == '_') &&
                (pat[j] == '*' || pat[j] == '|' || pat[j] == '&'))
                throw ParseError("Unable to parse expression in metachar:sym<assert>; couldn't find final '>'",
                                 line, "X::Comp::FailGoal", {{"dba", "metachar:sym<assert>"}, {"goal", "'>'"}});
        }
        if (c == '\'' || c == '"') { q = c; continue; }
        if (c == '<' && i + 1 < pat.size() && (pat[i + 1] == '[' || ((pat[i + 1] == '-' || pat[i + 1] == '+' || pat[i + 1] == '!' || pat[i + 1] == '?') && i + 2 < pat.size() && pat[i + 2] == '['))) { cls = 1; continue; }
        // `<:Kata :Hira>` / `<+alpha digit>` — two members of a composed class
        // with neither `+` nor `-` between them
        if (c == '<' && i + 2 < pat.size() &&
            (pat[i + 1] == ':' || ((pat[i + 1] == '+' || pat[i + 1] == '-') &&
                                   (ascii::isalpha((unsigned char)pat[i + 2]) || pat[i + 2] == ':')))) {
            size_t j = i + 1;
            if (pat[j] == '+' || pat[j] == '-') j++;
            if (pat[j] == ':') { j++; if (j < pat.size() && pat[j] == '!') j++; }
            size_t b = j;
            while (j < pat.size() && (ascii::isalnum((unsigned char)pat[j]) || pat[j] == '_' || pat[j] == '-')) j++;
            if (j > b && j < pat.size() && (pat[j] == ' ' || pat[j] == '\t')) {
                size_t k = j;
                while (k < pat.size() && (pat[k] == ' ' || pat[k] == '\t')) k++;
                if (k < pat.size() && (pat[k] == ':' || ascii::isalpha((unsigned char)pat[k])))
                    throw ParseError("Missing + or - between character class elements", line,
                                     "X::Syntax::Regex::Unterminated", {});
            }
        }
        // `< # ^ / >` — a quote-word list: its words are literals, a `#` among
        // them no comment and a `)` no group
        if (c == '<' && i + 1 < pat.size() && (pat[i + 1] == ' ' || pat[i + 1] == '\t')) {
            size_t j = i + 1;
            while (j < pat.size() && pat[j] != '>') j += pat[j] == '\\' ? 2 : 1;
            if (j < pat.size()) { i = j; continue; }
        }
        if (c == '#') { size_t nl = pat.find('\n', i); if (nl == std::string::npos) break; i = nl; continue; }
        if (c == '\\' && i + 1 < pat.size()) {
            if (pat[i + 1] == ' ')
                throw ParseError("No unspace allowed in regex; if you meant to match the literal character, "
                                 "please enclose in single quotes (' ') or use a backslashed form like \\x20",
                                 line, "X::Syntax::Regex::Unspace",
                                 {{"char", " "}, {"pre", "/" + pat.substr(0, i + 2)}, {"post", pat.substr(i + 2) + "/"}});
            // `\۳` — a backslashed digit of ANY script is a Perl backreference
            if ((unsigned char)pat[i + 1] >= 0xC0) {
                unsigned char b0 = (unsigned char)pat[i + 1];
                size_t L = b0 >= 0xF0 ? 4 : b0 >= 0xE0 ? 3 : 2;
                uint32_t cp = b0 & (0xFF >> (L + 1));
                for (size_t k = 1; k < L && i + 1 + k < pat.size(); k++) cp = (cp << 6) | ((unsigned char)pat[i + 1 + k] & 0x3F);
                if (uniDigitValue(cp) >= 0)
                    throw ParseError("Unrecognized backslash sequence: '\\" + pat.substr(i + 1, L) + "'", line,
                                     "X::Backslash::UnrecognizedSequence", {{"sequence", pat.substr(i + 1, L)}});
            }
            if (pat[i + 1] == 'b' || pat[i + 1] == 'B') {
                const std::string old = std::string("\\") + pat[i + 1];
                const std::string repl = pat[i + 1] == 'b' ? "«, », or <|w>" : "<!|w>";
                throw ParseError("Unsupported use of " + old + "; in Raku please use " + repl, line,
                                 "X::Obsolete", {{"old", old}, {"replacement", repl}});
            }
            i++;
            continue;
        }
        if (c == '*' && i + 1 < pat.size() && pat[i + 1] == '*') {
            size_t j = i + 2;
            if (j < pat.size() && (pat[j] == '?' || pat[j] == '!' || pat[j] == ':')) j++; // frugal/greedy/ratchet
            while (j < pat.size() && (pat[j] == ' ' || pat[j] == '\t')) j++;
            size_t d0 = j;
            while (j < pat.size() && ascii::isdigit((unsigned char)pat[j])) j++;
            if (j == d0) { i++; continue; }
            size_t sp = j;
            while (j < pat.size() && (pat[j] == ' ' || pat[j] == '\t')) j++;
            bool sp1 = j > sp;
            if (pat.compare(j, 2, "..") != 0) { i++; continue; }
            size_t k = j + 2;
            if (k < pat.size() && pat[k] == '^') k++;
            size_t sp2s = k;
            while (k < pat.size() && (pat[k] == ' ' || pat[k] == '\t')) k++;
            bool sp2 = k > sp2s && k < pat.size() && (ascii::isdigit((unsigned char)pat[k]) || pat[k] == '*');
            // `/m ** 1..-1/` — a negative end point: the range is malformed and
            // the `-` an unrecognised metachar, so the regex never finds its end
            if (!sp1 && j + 2 < pat.size() && pat[j + 2] == '-')
                throw ParseError("Malformed Range\nUnrecognized regex metacharacter -\n"
                                 "Unable to parse regex; couldn't find final '/'", line, "X::Comp::Group",
                                 {{"sorrow", "X::Syntax::Regex::MalformedRange"}, {"sorrow-msg", "Malformed Range"},
                                  {"panic", "X::Comp::AdHoc"}, {"panic-msg", "Unable to parse regex; couldn't find final '/'"}});
            if (sp1)
                throw ParseError("Spaces not allowed in bare range.", line, "X::Syntax::Regex::SpacesInBareRange",
                                 {{"pre", "/" + pat.substr(0, j + 2)}, {"post", pat.substr(j + 2) + "/"}});
            if (sp2)
                throw ParseError("Malformed Range. If attempting to use variables for end points, "
                                 "wrap the entire range in curly braces.", line, "X::Syntax::Regex::MalformedRange",
                                 {{"pre", "/" + pat.substr(0, j + 2)}, {"post", pat.substr(j + 2) + "/"}});
            // `x ** 2..1` — an empty literal range
            if (k == sp2s && k < pat.size() && ascii::isdigit((unsigned char)pat[k])) {
                size_t e = k;
                while (e < pat.size() && ascii::isdigit((unsigned char)pat[e])) e++;
                if (sp - d0 < 18 && e - k < 18) {
                    long long lo = std::stoll(pat.substr(d0, sp - d0)), hi = std::stoll(pat.substr(k, e - k));
                    if (pat[j + 2] == '^') hi--;
                    if (lo > hi)
                        throw ParseError("Range of quantifier cannot be empty", line,
                                         "X::Syntax::Regex::QuantifierValue",
                                         {{"empty-range", "True"}});
                }
            }
            i++;
        }
    }
    if (grpDepth > 0)
        throw ParseError("Unable to parse regex; couldn't find final ')'", line, "X::Comp::Group",
                         {{"panic", "X::Comp::AdHoc"}, {"panic-msg", "Unable to parse regex; couldn't find final ')'"}});
}

} // namespace rakupp
