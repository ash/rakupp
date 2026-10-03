// The language server's view of a document: declarations, scopes and the
// built-in reference, from the token stream. See LspIndex.h for why tokens and
// not the AST.
//
// What it understands, and so what hover, completion and go-to-definition can
// find:
//   - `my`/`our`/`state`/`has`/`HAS` declarators (with a type, or a parenned
//     list of variables), and `constant`;
//   - `sub`/`method`/`submethod`/`token`/`rule`/`regex` (also after
//     `multi`/`proto`/`my`/`our`) and their signature's parameters;
//   - pointy-block parameters (`-> $x, $y { … }`, `for @a -> $x { … }`);
//   - `class`/`role`/`grammar`/`module`/`package`/`enum`/`subset`, an enum's
//     `<…>` values, and the class each attribute and method belongs to;
//   - `#|` comments above a declaration and `#=` after it.
// Scopes are the braces: a variable is visible from its declaration to the
// end of the block it was declared in; routines and types anywhere in theirs.
// A brace inside a string is part of the string's token and opens nothing.
#include "LspIndex.h"

#include "Lexer.h"
#include "Parser.h"
#include "Token.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <set>

namespace rakupp {
namespace lsp {
namespace {

constexpr size_t kNone = std::string::npos;

bool isAlnum(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '_' || (unsigned char)c >= 0x80;
}
bool isSigil(char c) { return c == '$' || c == '@' || c == '%' || c == '&'; }
bool isTwigil(char c) { return c == '*' || c == '!' || c == '.' || c == '?' || c == '^' || c == ':'; }

std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == kNone) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// Whitespace runs to one space, capped: a hover shows a declaration, not a page.
std::string collapse(const std::string& s, size_t cap = 200) {
    std::string out;
    bool sp = false;
    for (char c : s) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { sp = !out.empty(); continue; }
        if (sp) { out += ' '; sp = false; }
        out += c;
    }
    if (out.size() > cap) { out.resize(cap); out += " …"; }
    return out;
}

std::string stripTicks(const std::string& s) {
    std::string out;
    for (char c : s) if (c != '`') out += c;
    return out;
}

int utf16Units(const std::string& s, size_t from, size_t to) {
    int n = 0;
    for (size_t k = from; k < to && k < s.size();) {
        unsigned char c = s[k];
        int adv = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
        n += adv == 4 ? 2 : 1;
        k += adv;
    }
    return n;
}

// ------------------------------------------------------------- tokens -----
struct Tk {
    Tok kind;
    std::string text;
    size_t start = 0, end = 0;
    bool exact = false; // `text` is the source between start and end
};

std::vector<Tk> lexDoc(const std::string& src) {
    std::vector<Tk> out;
    std::vector<Token> toks;
    // Offsets into the source AS PASSED (see Token::off): no fudge rewrite.
    try { Lexer lx(src, /*honourFudge=*/false); toks = lx.tokenize(); }
    catch (...) { return out; }
    size_t cursor = 0; // the end of the last token placed
    for (auto& t : toks) {
        if (t.kind == Tok::End) break;
        Tk k{t.kind, t.text, t.off, t.off, false};
        // The lexer stamps a token where it ENDS; the start is only known for
        // tokens whose text is spelled as written (names, variables, operators).
        if (!t.text.empty() && t.off >= t.text.size() && t.off <= src.size() &&
            src.compare(t.off - t.text.size(), t.text.size(), t.text) == 0) {
            k.start = t.off - t.text.size();
            k.exact = true;
        } else if (!t.text.empty() &&
                   (t.kind == Tok::Ident || t.kind == Tok::Var || t.kind == Tok::Op)) {
            // A few tokens are stamped once their whole construct is read:
            // `token TOP { … }` stamps `token` and `TOP` with the END of the
            // body. Tokens come in source order, so the first spelling after
            // the previous token is this one.
            size_t at = src.find(t.text, cursor);
            if (at != kNone && at + t.text.size() <= t.off) {
                k.start = at;
                k.end = at + t.text.size();
                k.exact = true;
            }
        }
        if (k.exact) cursor = k.end;
        else cursor = std::max(cursor, std::min(t.off, src.size()));
        out.push_back(std::move(k));
    }
    return out;
}

// -------------------------------------------------------------- index -----
struct Scope {
    size_t open = 0, close = kNone;
    int parent = -1;
    std::string cls; // the class/role/grammar whose body this is, inherited inward
};

struct Decl {
    std::string name;    // `$x`/`@a` with sigil; attributes as `$!x`; routines/types bare
    char kind = 'v';     // v var, p param, a attribute, n constant, s sub, m method,
                         // r token/rule/regex, c class-like, e enum, k enum value, u subset
    std::string keyword; // my, has, sub, class, …
    size_t start = 0, end = 0;
    int scope = -1;      // -1: a parameter whose block never opened (a stub)
    std::string owner;   // attributes/methods/tokens: the class; params: the routine
    std::string header;  // the declaration as written
    std::string doc;     // #| and #= comments
    bool pub = false;    // attribute with a `.` twigil: `.x` reads it
};

struct Index {
    std::vector<Tk> toks;
    std::vector<Scope> scopes;
    std::vector<Decl> decls;
};

bool isOp(const Tk& t, const char* s) { return t.kind == Tok::Op && t.text == s; }
bool isWord(const Tk& t, const char* s) { return t.kind == Tok::Ident && t.text == s; }

// `.foo` / `.?foo` / `.^foo` / `!foo` — the token before a method name.
bool isCallDot(const Tk& t) {
    if (t.kind != Tok::Op) return false;
    const std::string& s = t.text;
    return s == "." || s == ".?" || s == ".+" || s == ".*" || s == ".^" || s == ".&" || s == "!";
}

// `#|` lines above the declaration's line, and a `#=` on the line or below it.
std::string docComments(const TextDoc& d, int line0) {
    std::vector<std::string> above;
    for (int l = line0 - 1; l >= 0; l--) {
        std::string t = trim(d.lineText(l));
        if (t.rfind("#|", 0) != 0) break;
        above.push_back(trim(t.substr(2)));
    }
    std::string out;
    for (auto it = above.rbegin(); it != above.rend(); ++it) {
        if (!out.empty()) out += ' ';
        out += *it;
    }
    std::string here = d.lineText(line0);
    size_t at = here.find("#=");
    if (at != kNone && (at == 0 || here[at - 1] == ' ' || here[at - 1] == '\t')) {
        if (!out.empty()) out += ' ';
        out += trim(here.substr(at + 2));
    }
    for (int l = line0 + 1; l < (int)d.lineStarts.size(); l++) {
        std::string t = trim(d.lineText(l));
        if (t.rfind("#=", 0) != 0) break;
        if (!out.empty()) out += ' ';
        out += trim(t.substr(2));
    }
    return out;
}

std::string lineHeader(const TextDoc& d, size_t off) {
    return collapse(trim(d.lineText(d.lineOf(off))), 160);
}

Index build(const TextDoc& d) {
    Index ix;
    ix.toks = lexDoc(d.text);
    const auto& T = ix.toks;
    const std::string& src = d.text;
    ix.scopes.push_back({});
    int cur = 0;
    std::vector<size_t> pendingParams; // decl indices waiting for their block
    std::string pendingClass;

    auto add = [&](const Tk& name, std::string nm, char kind, const std::string& kw,
                   std::string header, int scope, std::string owner) -> size_t {
        Decl x;
        x.name = std::move(nm);
        x.kind = kind;
        x.keyword = kw;
        x.start = name.start;
        x.end = name.end;
        x.scope = scope;
        x.owner = std::move(owner);
        x.header = std::move(header);
        // A parameter shares its routine's line, and the routine's `#|`.
        if (kind != 'p') x.doc = docComments(d, d.lineOf(name.start));
        ix.decls.push_back(std::move(x));
        return ix.decls.size() - 1;
    };
    // Variables between `from` and the next `{` (or `;`), at any paren depth —
    // a signature or a pointy block's parameter list. A default value is
    // skipped, so `$x = $y` declares `$x` only.
    auto collectParams = [&](size_t from, const std::string& owner) -> size_t {
        size_t k = from;
        int depth = 0;
        bool inDefault = false;
        // Each parameter's header is its own segment of the signature, from
        // after the `(`/`,` before it to the `,`/`)` after it: `:$loud = False`.
        size_t segStart = from < T.size() ? T[from].start : src.size();
        size_t segFirst = pendingParams.size();
        auto endSegment = [&](size_t at) {
            std::string h = at > segStart ? collapse(trim(src.substr(segStart, at - segStart))) : "";
            for (size_t p = segFirst; p < pendingParams.size(); p++)
                if (!h.empty()) ix.decls[pendingParams[p]].header = h;
            segFirst = pendingParams.size();
        };
        for (; k < T.size(); k++) {
            const Tk& t = T[k];
            if (t.kind == Tok::LBrace || t.kind == Tok::Semicolon || t.kind == Tok::RBrace ||
                isOp(t, "-->")) {
                endSegment(t.start);
                if (isOp(t, "-->")) { inDefault = true; continue; } // the return type
                break;
            }
            if (t.kind == Tok::LParen || t.kind == Tok::LBracket) {
                if (depth++ == 0) segStart = t.end;
                continue;
            }
            if (t.kind == Tok::RParen || t.kind == Tok::RBracket) {
                if (--depth == 0) endSegment(t.start);
                inDefault = false;
                continue;
            }
            if (t.kind == Tok::Comma && depth <= 1) {
                endSegment(t.start);
                segStart = t.end;
                inDefault = false;
                continue;
            }
            if (isOp(t, "=") || isOp(t, "where")) { inDefault = true; continue; }
            if (isWord(t, "where") || isWord(t, "is")) { inDefault = true; continue; }
            if (t.kind == Tok::Var && t.exact && !inDefault) {
                std::string nm = t.text;
                if (nm.size() > 2 && (nm[1] == '!' || nm[1] == '.')) continue; // `$!x` sets an attribute
                if (nm.size() > 1 && (nm[1] == '*')) continue;
                pendingParams.push_back(add(t, nm, 'p', "parameter", t.text, -1, owner));
            }
        }
        return k;
    };

    for (size_t i = 0; i < T.size(); i++) {
        const Tk& t = T[i];
        const Tk* prev = i ? &T[i - 1] : nullptr;
        const Tk* next = i + 1 < T.size() ? &T[i + 1] : nullptr;

        if (t.kind == Tok::LBrace) {
            Scope s;
            s.open = t.start;
            s.parent = cur;
            s.cls = !pendingClass.empty() ? pendingClass : ix.scopes[cur].cls;
            ix.scopes.push_back(s);
            cur = (int)ix.scopes.size() - 1;
            for (size_t p : pendingParams) ix.decls[p].scope = cur;
            pendingParams.clear();
            pendingClass.clear();
            continue;
        }
        if (t.kind == Tok::RBrace) {
            if (cur != 0) {
                ix.scopes[cur].close = t.end;
                cur = ix.scopes[cur].parent;
            }
            continue;
        }
        if (t.kind == Tok::Semicolon) {
            // `unit class Foo;` / `class Foo;`: the rest of the scope is its body.
            if (!pendingClass.empty()) ix.scopes[cur].cls = pendingClass;
            pendingClass.clear();
            pendingParams.clear(); // a stub's parameters scope over nothing
            continue;
        }
        if (isOp(t, "->") || isOp(t, "<->")) {
            collectParams(i + 1, "");
            continue;
        }
        if (t.kind != Tok::Ident) continue;
        // `.method`, `method => 1`, `:sub(…)`: a name used as data, not a keyword.
        if (prev && (isCallDot(*prev) || isOp(*prev, ":"))) continue;
        if (next && next->kind == Tok::FatArrow) continue;
        const std::string& w = t.text;

        if (w == "my" || w == "our" || w == "state" || w == "has" || w == "HAS") {
            size_t j = i + 1;
            // A type (`my Int $x`, `has Str:D $.name`) sits between.
            for (int skip = 0; skip < 4 && j < T.size() &&
                               (T[j].kind == Tok::Ident || (T[j].kind == Tok::Op && T[j].text != "=" &&
                                                            T[j].text != "->"));
                 skip++) {
                if (T[j].kind == Tok::Ident &&
                    (T[j].text == "sub" || T[j].text == "class" || T[j].text == "method" ||
                     T[j].text == "constant" || T[j].text == "enum" || T[j].text == "role" ||
                     T[j].text == "grammar" || T[j].text == "subset" || T[j].text == "multi" ||
                     T[j].text == "proto" || T[j].text == "token" || T[j].text == "regex" ||
                     T[j].text == "rule"))
                    break; // `my sub f` / `our class C`: that keyword declares it
                j++;
            }
            bool attr = w == "has" || w == "HAS";
            auto declVar = [&](const Tk& v) {
                if (v.kind != Tok::Var || !v.exact) return;
                std::string nm = v.text;
                if (attr) {
                    bool pub = nm.size() > 2 && nm[1] == '.';
                    std::string bare = (nm.size() > 2 && (nm[1] == '.' || nm[1] == '!')) ? nm.substr(2)
                                                                                         : nm.substr(1);
                    size_t k = add(v, std::string(1, nm[0]) + "!" + bare, 'a', w, lineHeader(d, v.start),
                                   cur, ix.scopes[cur].cls);
                    ix.decls[k].pub = pub;
                } else {
                    add(v, nm, 'v', w, lineHeader(d, v.start), cur, "");
                }
            };
            if (j < T.size() && T[j].kind == Tok::Var) declVar(T[j]);
            else if (j < T.size() && T[j].kind == Tok::LParen) {
                for (size_t k = j + 1; k < T.size() && T[k].kind != Tok::RParen; k++) declVar(T[k]);
            }
            continue;
        }
        if (w == "constant" && next && next->exact &&
            (next->kind == Tok::Ident || next->kind == Tok::Var)) {
            add(*next, next->text, 'n', w, lineHeader(d, next->start), cur, "");
            continue;
        }

        bool routine = w == "sub" || w == "method" || w == "submethod" || w == "token" ||
                       w == "rule" || w == "regex" || w == "macro";
        bool multiBare = (w == "multi" || w == "proto") && next && next->kind == Tok::Ident &&
                         next->text != "sub" && next->text != "method" && next->text != "submethod" &&
                         next->text != "token" && next->text != "rule" && next->text != "regex";
        if (routine || multiBare) {
            size_t n = i + 1;
            if (n < T.size() && T[n].kind == Tok::Op && (T[n].text == "!" || T[n].text == "^")) n++;
            if (n >= T.size() || T[n].kind != Tok::Ident || !T[n].exact) {
                collectParams(i + 1, w); // an anonymous `sub ($x) { … }`
                continue;
            }
            // The header starts at a leading multi/proto/my/our and runs to the body.
            size_t hs = t.start;
            for (size_t b = i; b-- > 0;) {
                const std::string& pw = T[b].text;
                if (T[b].kind == Tok::Ident && (pw == "multi" || pw == "proto" || pw == "my" || pw == "our"))
                    hs = T[b].start;
                else
                    break;
            }
            char kind = (w == "sub" || w == "macro" || multiBare) ? 's'
                      : (w == "token" || w == "rule" || w == "regex") ? 'r' : 'm';
            std::string owner = kind == 's' ? "" : ix.scopes[cur].cls;
            // `sub postfix:<!>` arrives as postfix : < ! > — one name.
            Tk nameTok = T[n];
            size_t last = n;
            if (n + 2 < T.size() && isOp(T[n + 1], ":") &&
                (isOp(T[n + 2], "<") || isOp(T[n + 2], "«") || isOp(T[n + 2], "<<"))) {
                size_t c = n + 3;
                while (c < T.size() && !(T[c].kind == Tok::Op &&
                                         (T[c].text == ">" || T[c].text == "»" || T[c].text == ">>")))
                    c++;
                if (c < T.size() && T[c].exact) {
                    last = c;
                    nameTok.end = T[c].end;
                    nameTok.text = src.substr(nameTok.start, nameTok.end - nameTok.start);
                }
            }
            size_t declIx = add(nameTok, nameTok.text, kind, multiBare ? "sub" : w, "", cur, owner);
            // A grammar rule's body is one regex token: there is no signature to read,
            // and the header is the whole rule.
            size_t end = last + 1 < T.size() && T[last + 1].kind == Tok::RegexLit
                             ? last + 1
                             : collectParams(last + 1, nameTok.text);
            size_t he = end < T.size() ? T[end].start : src.size();
            std::string header = (he > hs && T[end - 1].exact) ? collapse(src.substr(hs, he - hs))
                                                                : lineHeader(d, t.start);
            ix.decls[declIx].header = header;
            for (size_t p : pendingParams) ix.decls[p].owner = (multiBare ? std::string("sub") : w) + " " + nameTok.text;
            continue;
        }

        if (w == "class" || w == "role" || w == "grammar" || w == "module" || w == "package" ||
            w == "knowhow" || w == "enum" || w == "subset") {
            if (!next || next->kind != Tok::Ident || !next->exact) continue;
            char kind = w == "enum" ? 'e' : w == "subset" ? 'u' : 'c';
            size_t e = i + 1;
            while (e < T.size() && T[e].kind != Tok::LBrace && T[e].kind != Tok::Semicolon) e++;
            size_t he = e < T.size() ? (w == "enum" ? T[e].end : T[e].start) : src.size();
            std::string header = he > t.start ? collapse(src.substr(t.start, he - t.start)) : next->text;
            add(*next, next->text, kind, w, header, cur, "");
            if (kind == 'c' && w != "module" && w != "package") pendingClass = next->text;
            if (kind == 'e') {
                // `enum Color <red green blue>;` — the lexer hands `<…>` over word by word.
                size_t k = i + 2;
                if (k < T.size() && T[k].kind == Tok::Op && (T[k].text == "<" || T[k].text == "<<")) {
                    for (k++; k < T.size() && !(T[k].kind == Tok::Op && (T[k].text == ">" || T[k].text == ">>")); k++)
                        if (T[k].kind == Tok::Ident && T[k].exact)
                            add(T[k], T[k].text, 'k', "enum", header, cur, next->text);
                }
            }
            continue;
        }
    }
    return ix;
}

// The scopes containing `off`, innermost first.
std::vector<int> chainAt(const Index& ix, size_t off) {
    int best = 0;
    for (int s = 1; s < (int)ix.scopes.size(); s++) {
        const Scope& sc = ix.scopes[s];
        if (sc.open <= off && (sc.close == kNone || off < sc.close)) best = s; // later = deeper
    }
    std::vector<int> chain;
    for (int s = best; s >= 0; s = ix.scopes[s].parent) chain.push_back(s);
    return chain;
}

bool positional(char kind) { return kind == 'v' || kind == 'p' || kind == 'n'; }

// The declaration `name` refers to at `off`: the innermost scope that has one,
// and in it the latest declaration before `off` (variables must be declared
// first; routines and types need not).
const Decl* visible(const Index& ix, const std::string& name, const char* kinds, size_t off) {
    for (int s : chainAt(ix, off)) {
        const Decl* best = nullptr;
        for (const Decl& x : ix.decls) {
            if (x.scope != s || x.name != name || !std::strchr(kinds, x.kind)) continue;
            if (positional(x.kind) && x.start > off) continue;
            if (!best || (x.start > best->start && (!positional(x.kind) || x.start <= off))) best = &x;
        }
        if (best) return best;
    }
    return nullptr;
}

// Types are package-scoped: visible from anywhere in the file.
const Decl* typeNamed(const Index& ix, const std::string& name) {
    for (const Decl& x : ix.decls)
        if (x.name == name && (x.kind == 'c' || x.kind == 'e' || x.kind == 'u')) return &x;
    return nullptr;
}

int tokenAt(const Index& ix, size_t off) {
    for (int pass = 0; pass < 2; pass++) {
        size_t o = pass == 0 ? off : off - 1; // a cursor just past a word still means that word
        if (pass == 1 && off == 0) break;
        for (size_t i = 0; i < ix.toks.size(); i++) {
            const Tk& t = ix.toks[i];
            if (t.exact && t.start <= o && o < t.end) return (int)i;
        }
    }
    return -1;
}

// ---------------------------------------------------------- reference -----
struct RefEntry {
    std::string md, plain;
};
struct Reference {
    std::map<std::string, std::vector<RefEntry>> rows;    // table rows, by the names in cell 1
    std::map<std::string, std::vector<RefEntry>> methods; // §6 example lines, by `.name`
    std::vector<std::string> subs, methodNames, specials;
    std::set<std::string> subSet, methodSet;
};
Reference& ref() {
    static Reference r;
    return r;
}

std::vector<std::string> splitCells(const std::string& line) {
    std::vector<std::string> cells;
    std::string cur;
    bool tick = false;
    for (size_t i = 1; i < line.size(); i++) {
        char c = line[i];
        if (c == '\\' && i + 1 < line.size() && line[i + 1] == '|') { cur += '|'; i++; continue; }
        if (c == '`') tick = !tick;
        if (c == '|' && !tick) { cells.push_back(trim(cur)); cur.clear(); continue; }
        cur += c;
    }
    if (!trim(cur).empty()) cells.push_back(trim(cur));
    return cells;
}

std::string headingText(const std::string& h) {
    std::string s = trim(h.substr(h.find(' ') + 1));
    // "5. Built-in subroutines" / "3.1 Method / postfix" lose their numbers.
    size_t k = 0;
    while (k < s.size() && ((s[k] >= '0' && s[k] <= '9') || s[k] == '.')) k++;
    if (k && k < s.size() && s[k] == ' ') s = s.substr(k + 1);
    return s;
}

} // namespace

void setReference(const std::string& md) {
    Reference& R = ref();
    R = Reference();
    std::string h2, h3, h2raw;
    bool inCode = false, prevTable = false;
    std::vector<std::string> codeWords;
    enum { NONE, APPX_A, APPX_B } appx = NONE;
    size_t pos = 0;
    while (pos <= md.size()) {
        size_t nl = md.find('\n', pos);
        if (nl == kNone) nl = md.size();
        std::string line = md.substr(pos, nl - pos);
        pos = nl + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (line.rfind("```", 0) == 0) { inCode = !inCode; prevTable = false; continue; }
        if (inCode) {
            if (appx != NONE) {
                size_t k = 0;
                while (k < line.size()) {
                    while (k < line.size() && line[k] == ' ') k++;
                    size_t e = line.find(' ', k);
                    if (e == kNone) e = line.size();
                    std::string w = line.substr(k, e - k);
                    k = e;
                    if (w.empty() || !(isAlnum(w[0])) || w.rfind("__", 0) == 0) continue;
                    if (appx == APPX_A && R.subSet.insert(w).second) R.subs.push_back(w);
                    if (appx == APPX_B && R.methodSet.insert(w).second) R.methodNames.push_back(w);
                }
            } else if (h2raw.rfind("## 6.", 0) == 0) {
                // `say 'Hello'.uc;   # → HELLO` documents `uc`.
                std::string where = "Methods by receiver" + (h3.empty() ? "" : " › " + h3);
                // Comments are not code: `# → (a b)  (by key)` holds no call.
                std::string code = line.substr(0, line.find('#'));
                for (size_t k = 0; k + 1 < code.size(); k++) {
                    if (line[k] != '.' || !(isAlnum(line[k + 1]) && !(line[k + 1] >= '0' && line[k + 1] <= '9')))
                        continue;
                    if (k > 0 && line[k - 1] == '.') continue;
                    size_t e = k + 1;
                    while (e < line.size() && (isAlnum(line[e]) || (line[e] == '-' && e + 1 < line.size() && isAlnum(line[e + 1]))))
                        e++;
                    // Only the line's FIRST call is what it demonstrates:
                    // `<b a>.sort(*.uc)` is a `sort` example, not a `uc` one.
                    std::string name = line.substr(k + 1, e - k - 1);
                    auto& v = R.methods[name];
                    if (v.size() < 4) {
                        RefEntry en;
                        en.md = "```raku\n" + trim(line) + "\n```\n*" + where + "*";
                        en.plain = trim(line) + "\n(" + stripTicks(where) + ")";
                        v.push_back(std::move(en));
                    }
                    break;
                }
            }
            continue;
        }
        if (line.rfind("## ", 0) == 0) {
            h2raw = line;
            h2 = headingText(line);
            h3.clear();
            appx = line.rfind("## Appendix A", 0) == 0 ? APPX_A
                 : line.rfind("## Appendix B", 0) == 0 ? APPX_B : NONE;
            prevTable = false;
            continue;
        }
        if (line.rfind("### ", 0) == 0) { h3 = headingText(line); prevTable = false; continue; }
        if (line.rfind("|", 0) != 0) { prevTable = false; continue; }

        bool header = !prevTable; // the first row of a table names its columns
        prevTable = true;
        std::vector<std::string> cells = splitCells(line);
        if (header || cells.size() < 2) continue;
        if (cells[0].find_first_not_of("-: ") == kNone) continue; // |---|---|
        const std::string& first = cells[0];
        std::vector<std::string> keys;
        for (size_t k = first.find('`'); k != kNone;) {
            size_t e = first.find('`', k + 1);
            if (e == kNone) break;
            std::string span = first.substr(k + 1, e - k - 1);
            size_t a = 0;
            while (a < span.size()) {
                while (a < span.size() && span[a] == ' ') a++;
                size_t b = span.find(' ', a);
                if (b == kNone) b = span.size();
                if (b > a) keys.push_back(span.substr(a, b - a));
                a = b;
            }
            k = first.find('`', e + 1);
        }
        if (keys.empty()) continue;
        std::string where = h2 + (h3.empty() ? "" : " › " + h3);
        std::string rest;
        for (size_t c = 1; c < cells.size(); c++) {
            if (cells[c].empty()) continue;
            if (!rest.empty()) rest += " · ";
            rest += cells[c];
        }
        RefEntry en;
        en.md = first + (rest.empty() ? "" : " — " + rest) + "\n\n*" + where + "*";
        en.plain = stripTicks(first) + (rest.empty() ? "" : " — " + stripTicks(rest)) + "\n(" +
                   stripTicks(where) + ")";
        for (auto& k : keys) {
            auto& v = R.rows[k];
            if (v.size() < 4) v.push_back(en);
            if (h2raw.rfind("## 12.", 0) == 0 && isSigil(k[0]) &&
                std::find(R.specials.begin(), R.specials.end(), k) == R.specials.end())
                R.specials.push_back(k);
        }
    }
}

namespace {

const char* const kTypes[] = {
    "Any", "Array", "Bag", "BagHash", "Blob", "Bool", "Buf", "Callable", "Capture", "Channel",
    "Code", "Complex", "Cool", "Date", "DateTime", "Duration", "Exception", "Failure", "Hash",
    "IO::Path", "Instant", "Int", "IntStr", "Iterable", "Iterator", "Junction", "List", "Lock",
    "Map", "Match", "Method", "Mix", "MixHash", "Mu", "Nil", "Num", "Numeric", "Pair",
    "Positional", "Promise", "Proc", "Proc::Async", "Range", "Rat", "Real", "Regex", "Routine",
    "Scalar", "Seq", "Set", "SetHash", "Signature", "Slip", "Str", "Stringy", "Sub", "Supplier",
    "Supply", "Tap", "Thread", "Version", "Whatever", "WhateverCode",
};
const char* const kKeywords[] = {
    "my", "our", "has", "state", "constant", "sub", "method", "submethod", "multi", "proto",
    "class", "role", "grammar", "module", "package", "unit", "enum", "subset", "token", "rule",
    "regex", "use", "need", "require", "import", "if", "elsif", "else", "unless", "with",
    "orwith", "without", "for", "while", "until", "loop", "repeat", "given", "when", "default",
    "do", "gather", "take", "return", "last", "next", "redo", "try", "CATCH", "CONTROL", "BEGIN",
    "END", "ENTER", "LEAVE", "FIRST", "NEXT", "LAST", "INIT", "react", "whenever", "supply",
    "start", "is", "does", "but", "where", "self", "so", "not", "and", "or", "xor", "andthen",
    "orelse", "MAIN",
};
// Shown when REFERENCE.md lists none (an empty reference, a trimmed build).
const char* const kSpecials[] = {
    "$_", "$/", "$!", "@*ARGS", "%*ENV", "$*IN", "$*OUT", "$*ERR", "$*PROGRAM-NAME", "$*CWD",
    "$*PID", "$*KERNEL", "$*DISTRO", "$*RAKU", "$*VM", "$*TZ", "$*HOME", "$*TMPDIR",
    "$*EXECUTABLE",
};

const char* describe(const Decl& x) {
    switch (x.kind) {
        case 'v': return "variable";
        case 'p': return "parameter";
        case 'a': return "attribute";
        case 'n': return "constant";
        case 's': return "sub";
        case 'm': return x.keyword == "submethod" ? "submethod" : "method";
        case 'r': return x.keyword == "token" ? "token" : x.keyword == "rule" ? "rule" : "regex";
        case 'e': return "enum";
        case 'k': return "enum value";
        case 'u': return "subset";
        default: return x.keyword.c_str();
    }
}

// "Method of Point, line 4." — what a declaration is and where.
std::string where(const TextDoc& d, const Decl& x, bool md) {
    std::string kind = describe(x);
    kind[0] = (char)std::toupper((unsigned char)kind[0]);
    std::string s = kind;
    auto code = [&](const std::string& c) { return md ? "`" + c + "`" : c; };
    if (x.kind == 'p' && x.owner.empty()) s = "Block parameter";
    if ((x.kind == 'a' || x.kind == 'm' || x.kind == 'r') && !x.owner.empty()) s += " of " + code(x.owner);
    if (x.kind == 'k') s += " of " + code(x.owner);
    if (x.kind == 'p' && !x.owner.empty()) s += " of " + code(x.owner);
    s += ", line " + std::to_string(d.lineOf(x.start) + 1) + ".";
    if (x.kind == 'a' && x.pub) s += " Public: " + code("." + x.name.substr(2)) + " reads it.";
    return s;
}

void render(const TextDoc& d, const Decl& x, Hover& h) {
    h.markdown += (h.markdown.empty() ? "" : "\n\n---\n\n");
    h.plain += (h.plain.empty() ? "" : "\n\n");
    h.markdown += "```raku\n" + x.header + "\n```\n";
    h.plain += x.header + "\n";
    if (!x.doc.empty()) { h.markdown += x.doc + "\n\n"; h.plain += x.doc + "\n"; }
    h.markdown += "*" + where(d, x, true) + "*";
    h.plain += where(d, x, false);
    h.found = true;
}

void renderRef(const std::vector<RefEntry>& v, Hover& h) {
    for (auto& e : v) {
        h.markdown += (h.markdown.empty() ? "" : "\n\n") + e.md;
        h.plain += (h.plain.empty() ? "" : "\n\n") + e.plain;
        h.found = true;
    }
}

bool refRows(const std::string& key, Hover& h) {
    auto it = ref().rows.find(key);
    if (it == ref().rows.end()) return false;
    renderRef(it->second, h);
    return true;
}

bool refMethod(const std::string& name, Hover& h) {
    bool any = false;
    auto it = ref().methods.find(name);
    if (it != ref().methods.end()) { renderRef(it->second, h); any = true; }
    if (!any) any = refRows("." + name, h);
    if (!any && ref().methodSet.count(name)) {
        h.markdown += (h.markdown.empty() ? "" : "\n\n") + std::string("Built-in method `.") + name + "`.";
        h.plain += (h.plain.empty() ? "" : "\n\n") + std::string("Built-in method .") + name + ".";
        h.found = any = true;
    }
    return any;
}

// What the name in token `ti` refers to: a declaration in this file (first),
// else nothing. Methods are matched by name across every class.
std::vector<const Decl*> resolve(const Index& ix, int ti) {
    std::vector<const Decl*> out;
    const Tk& t = ix.toks[ti];
    bool afterDot = ti > 0 && isCallDot(ix.toks[ti - 1]);
    // The name in a declaration is that declaration — even a parameter, whose
    // scope is the block AFTER the signature it is written in.
    for (const Decl& x : ix.decls)
        if (x.start == t.start && x.end >= t.end) { out.push_back(&x); return out; }
    if (t.kind == Tok::Var) {
        const std::string& n = t.text;
        if (n.size() > 2 && (n[1] == '.' || n[1] == '!')) {
            std::string key = std::string(1, n[0]) + "!" + n.substr(2);
            std::string cls;
            auto chain = chainAt(ix, t.start);
            if (!chain.empty()) cls = ix.scopes[chain.front()].cls;
            const Decl* any = nullptr;
            for (const Decl& x : ix.decls) {
                if (x.kind != 'a' || x.name != key) continue;
                if (x.owner == cls) { out.push_back(&x); return out; }
                if (!any) any = &x;
            }
            if (any) out.push_back(any);
            return out;
        }
        if (n[0] == '&') {
            if (const Decl* x = visible(ix, n.substr(1), "sm", t.start)) out.push_back(x);
            else if (const Decl* x2 = visible(ix, n, "vpn", t.start)) out.push_back(x2);
            return out;
        }
        if (const Decl* x = visible(ix, n, "vpn", t.start)) out.push_back(x);
        return out;
    }
    if (t.kind == Tok::Op) {
        // `5!` calls a `sub postfix:<!>` declared here.
        for (const Decl& x : ix.decls) {
            if (x.kind != 's') continue;
            size_t c = x.name.find(':');
            if (c == kNone) continue;
            std::string sym = x.name.substr(c + 1);
            if (sym == "<" + t.text + ">" || sym == "«" + t.text + "»" || sym == "<<" + t.text + ">>")
                out.push_back(&x);
        }
        return out;
    }
    if (t.kind != Tok::Ident) return out;
    if (afterDot) {
        for (const Decl& x : ix.decls)
            if ((x.kind == 'm' || x.kind == 'r') && x.name == t.text) out.push_back(&x);
        for (const Decl& x : ix.decls)
            if (x.kind == 'a' && x.pub && x.name.substr(2) == t.text) out.push_back(&x);
        return out;
    }
    if (const Decl* x = visible(ix, t.text, "srnk", t.start)) { out.push_back(x); return out; }
    if (const Decl* x = typeNamed(ix, t.text)) { out.push_back(x); return out; }
    // A token/rule called from inside its grammar: `<word>` reaches it bare.
    for (const Decl& x : ix.decls)
        if ((x.kind == 'r' || x.kind == 'm') && x.name == t.text) { out.push_back(&x); break; }
    return out;
}

} // namespace

// ------------------------------------------------------------ TextDoc -----
TextDoc::TextDoc(std::string t) : text(std::move(t)) {
    lineStarts.push_back(0);
    for (size_t i = 0; i < text.size(); i++)
        if (text[i] == '\n') lineStarts.push_back(i + 1);
}

size_t TextDoc::offsetAt(int line0, int utf16Col) const {
    if (line0 < 0) return 0;
    if (line0 >= (int)lineStarts.size()) return text.size();
    size_t k = lineStarts[line0];
    int n = 0;
    while (k < text.size() && text[k] != '\n' && n < utf16Col) {
        unsigned char c = text[k];
        int adv = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
        n += adv == 4 ? 2 : 1;
        k += adv;
    }
    return std::min(k, text.size());
}

int TextDoc::lineOf(size_t off) const {
    auto it = std::upper_bound(lineStarts.begin(), lineStarts.end(), off);
    return (int)(it - lineStarts.begin()) - 1;
}

void TextDoc::positionOf(size_t off, int& line0, int& utf16Col) const {
    off = std::min(off, text.size());
    line0 = lineOf(off);
    utf16Col = utf16Units(text, lineStarts[line0], off);
}

std::string TextDoc::lineText(int line0) const {
    if (line0 < 0 || line0 >= (int)lineStarts.size()) return "";
    size_t b = lineStarts[line0];
    size_t e = text.find('\n', b);
    if (e == kNone) e = text.size();
    std::string s = text.substr(b, e - b);
    if (!s.empty() && s.back() == '\r') s.pop_back();
    return s;
}

// ------------------------------------------------------------- queries -----
Span locateOnLine(const TextDoc& d, int line1, const std::string& subject) {
    int line0 = std::max(0, std::min(line1 - 1, (int)d.lineStarts.size() - 1));
    size_t ls = d.lineStarts[line0];
    std::string lt = d.lineText(line0);
    if (!subject.empty()) {
        std::vector<std::string> wants{subject};
        if (subject.size() > 1 && subject[0] == '&') wants.push_back(subject.substr(1));
        std::vector<Tk> toks = lexDoc(d.text);
        for (auto& w : wants)
            for (auto& t : toks)
                if (t.exact && t.text == w && d.lineOf(t.start) == line0) return {t.start, t.end};
        for (auto& w : wants) {
            size_t at = lt.find(w);
            if (at != kNone) return {ls + at, ls + at + w.size()};
        }
    }
    size_t b = lt.find_first_not_of(" \t");
    if (b == kNone) return {ls, ls + lt.size()};
    size_t e = lt.find_last_not_of(" \t");
    return {ls + b, ls + e + 1};
}

Hover hoverAt(const TextDoc& d, size_t off) {
    Hover h;
    Index ix = build(d);
    int ti = tokenAt(ix, off);
    if (ti < 0) return h;
    const Tk& t = ix.toks[ti];
    h.span = {t.start, t.end};
    bool afterDot = ti > 0 && isCallDot(ix.toks[ti - 1]);

    for (const Decl* x : resolve(ix, ti)) render(d, *x, h);
    if (h.found) return h;

    if (t.kind == Tok::Var) {
        refRows(t.text, h);
    } else if (t.kind == Tok::Ident) {
        if (afterDot) refMethod(t.text, h);
        else if (!refRows(t.text, h)) {
            if (ref().subSet.count(t.text)) {
                h.markdown = "Built-in sub `" + t.text + "`.";
                h.plain = "Built-in sub " + t.text + ".";
                h.found = true;
            }
        }
    } else if (t.kind == Tok::Op) {
        refRows(t.text, h);
    }
    return h;
}

bool definitionAt(const TextDoc& d, size_t off, Span& target) {
    Index ix = build(d);
    int ti = tokenAt(ix, off);
    if (ti < 0) return false;
    auto found = resolve(ix, ti);
    if (found.empty()) return false;
    target = {found.front()->start, found.front()->end};
    return true;
}

Completions completeAt(const TextDoc& d, size_t off) {
    Completions out;
    const std::string& s = d.text;
    off = std::min(off, s.size());
    size_t ls = d.lineStarts[d.lineOf(off)];
    size_t p = off;
    while (p > ls && (isAlnum(s[p - 1]) ||
                      ((s[p - 1] == '-' || s[p - 1] == '\'') && p < off && isAlnum(s[p]) &&
                       p - 1 > ls && isAlnum(s[p - 2])) ||
                      (s[p - 1] == ':' && p - 1 > ls && s[p - 2] == ':')))
        p -= s[p - 1] == ':' ? 2 : 1;

    enum { WORD, VAR, METHOD } mode = WORD;
    size_t from = p;
    if (p > ls && isTwigil(s[p - 1]) && p - 1 > ls && isSigil(s[p - 2])) { mode = VAR; from = p - 2; }
    else if (p > ls && isSigil(s[p - 1])) { mode = VAR; from = p - 1; }
    else if (p > ls && s[p - 1] == '.' && !(p - 1 > ls && s[p - 2] == '.')) mode = METHOD;
    std::string prefix = s.substr(from, off - from);
    out.replace = {from, off};
    // Nothing typed yet, right after an operator (`..`, `+`): not a place to
    // offer every name there is. At a line start or after a space it is.
    if (mode == WORD && prefix.empty() && p > ls && !std::strchr(" \t({[,;=", s[p - 1])) return out;

    Index ix = build(d);
    std::set<std::string> seen;
    auto push = [&](CompletionItem it) {
        if (it.label.empty() || it.label == prefix || it.label.rfind(prefix, 0) != 0) return;
        if (!seen.insert(it.label).second) return;
        out.items.push_back(std::move(it));
    };
    auto local = [&](const std::string& label, ItemKind k, const Decl& x) {
        CompletionItem it;
        it.label = label;
        it.kind = k;
        it.detail = x.header;
        it.local = true;
        it.docPlain = where(d, x, false) + (x.doc.empty() ? "" : "\n" + x.doc);
        it.docMarkdown = "*" + where(d, x, true) + "*" + (x.doc.empty() ? "" : "\n\n" + x.doc);
        push(std::move(it));
    };
    auto chain = chainAt(ix, off);
    std::string cls = chain.empty() ? "" : ix.scopes[chain.front()].cls;
    auto inChain = [&](int sc) { return std::find(chain.begin(), chain.end(), sc) != chain.end(); };
    // The name being typed is not a candidate for itself (`my $co|`).
    auto isSelf = [&](const Decl& x) { return x.start == from; };

    if (mode == VAR) {
        char sigil = prefix[0];
        for (int sc : chain)
            for (auto it = ix.decls.rbegin(); it != ix.decls.rend(); ++it) {
                const Decl& x = *it;
                if (x.scope != sc || isSelf(x)) continue;
                if (positional(x.kind) && x.start <= off && isSigil(x.name[0]))
                    local(x.name, x.kind == 'n' ? ItemKind::Constant : ItemKind::Variable, x);
                if (sigil == '&' && x.kind == 's') local("&" + x.name, ItemKind::Function, x);
            }
        for (const Decl& x : ix.decls) {
            if (x.kind != 'a' || isSelf(x) || (x.owner != cls && !cls.empty())) continue;
            local(x.name, ItemKind::Field, x);
            if (x.pub) local(std::string(1, x.name[0]) + "." + x.name.substr(2), ItemKind::Field, x);
        }
        (void)inChain;
        std::vector<std::string> specials = ref().specials;
        for (auto* sp : kSpecials)
            if (std::find(specials.begin(), specials.end(), sp) == specials.end()) specials.push_back(sp);
        for (auto& sp : specials) {
            CompletionItem it;
            it.label = sp;
            it.kind = ItemKind::Variable;
            it.detail = "special variable";
            Hover h;
            if (refRows(sp, h)) { it.docPlain = h.plain; it.docMarkdown = h.markdown; }
            push(std::move(it));
        }
    } else if (mode == METHOD) {
        for (const Decl& x : ix.decls) {
            if (x.kind == 'm') local(x.name, ItemKind::Method, x);
            if (x.kind == 'a' && x.pub) local(x.name.substr(2), ItemKind::Field, x);
        }
        for (auto& m : ref().methodNames) {
            CompletionItem it;
            it.label = m;
            it.kind = ItemKind::Method;
            it.detail = "built-in method";
            push(std::move(it));
        }
    } else {
        for (const Decl& x : ix.decls) {
            if (isSelf(x)) continue;
            if (x.kind == 'c' || x.kind == 'e' || x.kind == 'u')
                local(x.name, x.kind == 'e' ? ItemKind::Class : x.keyword == "module" || x.keyword == "package"
                                                                      ? ItemKind::Module : ItemKind::Class, x);
            else if (x.kind == 'k') local(x.name, ItemKind::EnumMember, x);
            else if ((x.kind == 's' || (x.kind == 'n' && !isSigil(x.name[0]))) && inChain(x.scope))
                local(x.name, x.kind == 's' ? ItemKind::Function : ItemKind::Constant, x);
            else if (x.kind == 'r' && (x.owner == cls)) local(x.name, ItemKind::Function, x);
        }
        for (auto* k : kKeywords) {
            CompletionItem it;
            it.label = k;
            it.kind = ItemKind::Keyword;
            it.detail = "keyword";
            push(std::move(it));
        }
        for (auto& f : ref().subs) {
            CompletionItem it;
            it.label = f;
            it.kind = ItemKind::Function;
            it.detail = "built-in sub";
            push(std::move(it));
        }
        for (auto* ty : kTypes) {
            CompletionItem it;
            it.label = ty;
            it.kind = ItemKind::Class;
            it.detail = "built-in type";
            push(std::move(it));
        }
    }

    // Documentation for the built-ins, when the list is short enough to be
    // worth it (every client asks again as the prefix grows).
    if (out.items.size() <= 60) {
        for (auto& it : out.items) {
            if (it.local || !it.docPlain.empty()) continue;
            Hover h;
            if (it.kind == ItemKind::Method) refMethod(it.label, h);
            else refRows(it.label, h);
            if (h.found) { it.docPlain = h.plain; it.docMarkdown = h.markdown; }
        }
    }
    return out;
}

} // namespace lsp
} // namespace rakupp
