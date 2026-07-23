#include "Parser.h"
#include <cstdint>
#include <memory>
#include <sstream>
#include "Lexer.h"
#include "Unicode.h"
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <climits>
#include <algorithm>
#include <limits>
#include <set>
#include <unordered_set>

namespace rakupp {

// Byte length of a Unicode whitespace char at s[i], or 0 if s[i] is not
// whitespace. Covers ASCII plus the multibyte forms (NEL, NBSP, OGHAM SPACE,
// the U+2000..200A run, LS/PS, NNBSP, U+205F, U+3000) — matches the lexer.
// When `breaking` is true (word-quote splitting), the non-breaking spaces —
// NBSP (U+00A0), FIGURE SPACE (U+2007), NARROW NBSP (U+202F) — are NOT treated
// as separators, so `<a<NBSP>b>` stays a single word (matches Rakudo).
static int uniWsLen(const std::string& s, size_t i, bool breaking = false) {
    unsigned char b0 = (unsigned char)s[i];
    if (b0 == ' ' || b0 == '\t' || b0 == '\n' || b0 == '\r' || b0 == '\v' || b0 == '\f') return 1;
    unsigned char b1 = i + 1 < s.size() ? (unsigned char)s[i + 1] : 0;
    unsigned char b2 = i + 2 < s.size() ? (unsigned char)s[i + 2] : 0;
    if (b0 == 0xC2 && b1 == 0x85) return 2;                                 // NEL
    if (b0 == 0xC2 && b1 == 0xA0) return breaking ? 0 : 2;                  // NBSP (non-breaking)
    if (b0 == 0xE1 && b1 == 0x9A && b2 == 0x80) return 3;                   // OGHAM SPACE MARK
    if (b0 == 0xE2 && b1 == 0x80 && b2 == 0x87) return breaking ? 0 : 3;    // FIGURE SPACE (non-breaking)
    if (b0 == 0xE2 && b1 == 0x80 && b2 == 0xAF) return breaking ? 0 : 3;    // NARROW NBSP (non-breaking)
    if (b0 == 0xE2 && b1 == 0x80 &&
        ((b2 >= 0x80 && b2 <= 0x8A) || b2 == 0xA8 || b2 == 0xA9)) return 3; // U+2000..200A, LS, PS
    if (b0 == 0xE2 && b1 == 0x81 && b2 == 0x9F) return 3;                   // MEDIUM MATH SPACE
    if (b0 == 0xE3 && b1 == 0x80 && b2 == 0x80) return 3;                   // IDEOGRAPHIC SPACE
    return 0;
}

// binding powers (higher = tighter)
// Raku orders these (tightest→loosest): additive `+ -` > replication `x xx` >
// concatenation `~` > range. So `"-" x $n + 2` == `"-" x ($n + 2)` and
// `"x" x 2 ~ "y"` == `("x" x 2) ~ "y"`. Each gets a distinct level.
// Levels are spaced by 10 so a user-defined operator's `is tighter`/`looser`
// trait can slot a fresh precedence *between* two built-in levels (e.g. tighter
// than `+` but looser than `*`).
enum {
    BP_OR = 10, BP_AND = 20, BP_ZIP = 25, BP_COMMA = 30, BP_ASSIGN = 40, BP_TERNARY = 50,
    BP_OROR = 60, BP_ANDAND = 70, BP_COMPARE = 80, BP_RANGE = 90,
    BP_CONCAT = 100, BP_REPLICATE = 110, BP_ADD = 120, BP_MUL = 130, BP_POW = 140, BP_PREFIX = 150
};

static const std::unordered_set<std::string> kAssignOps = {
    "=", "+=", "-=", "*=", "/=", "~=", "%=", "**=", "//=", "||=", "&&=", "^^=", "x=", ":=",
    "div=", "mod=", "gcd=", "lcm=", "xx=", "min=", "max=",
};
static const std::unordered_set<std::string> kBlockKeywords = {
    "if", "unless", "while", "until", "for", "else", "elsif", "given", "when",
    "loop", "repeat", "sub", "my", "our", "state", "has", "constant", "return",
    "last", "next", "redo", "use", "no", "class", "role", "grammar", "and", "or",
    "xor", "not",
};

struct InfixInfo {
    bool valid = false;
    int lbp = 0;
    bool rightAssoc = false;
    bool isAssign = false;
    bool isComma = false;
    bool isRange = false;
    bool isFatArrow = false;
    bool isTernary = false;
    std::string op;
};

Parser::Parser(std::vector<Token> toks) : toks_(std::move(toks)) {}

const Token& Parser::peek(int off) const {
    size_t p = pos_ + off;
    if (p >= toks_.size()) p = toks_.size() - 1;
    return toks_[p];
}

bool Parser::isOp(const std::string& s) const {
    return cur().kind == Tok::Op && cur().text == s;
}
bool Parser::isIdent(const std::string& s) const {
    return cur().kind == Tok::Ident && cur().text == s;
}
const Token& Parser::advance() { return toks_[pos_++]; }
bool Parser::matchOp(const std::string& s) { if (isOp(s)) { advance(); return true; } return false; }
bool Parser::matchKind(Tok k) { if (cur().kind == k) { advance(); return true; } return false; }
void Parser::expectKind(Tok k, const char* what) {
    if (cur().kind != k) error(std::string("expected ") + what);
    advance();
}
void Parser::error(const std::string& msg) {
    throw ParseError(msg + " (got '" + cur().text + "')", cur().line);
}

// ---------------- infix table ----------------
static InfixInfo classifyInfix(const Token& t) {
    InfixInfo in;
    if (t.kind == Tok::Comma) { in.valid = true; in.lbp = BP_COMMA; in.isComma = true; in.op = ","; return in; }
    if (t.kind == Tok::FatArrow) { in.valid = true; in.lbp = BP_ASSIGN; in.isFatArrow = true; in.op = "=>"; return in; }
    if (t.kind == Tok::Op) {
        const std::string& o = t.text;
        in.op = o;
        if (kAssignOps.count(o)) { in.valid = true; in.lbp = BP_ASSIGN; in.rightAssoc = true; in.isAssign = true; return in; }
        // reversed-metaop assignment `$x R~= $y` (= `$y ~= $x`, target on the right)
        if (o.size() > 2 && o[0] == 'R' && o.back() == '=' && kAssignOps.count(o.substr(1))) {
            in.valid = true; in.lbp = BP_ASSIGN; in.rightAssoc = true; in.isAssign = true; return in;
        }
        {   // bitwise/shift compound assigns: +|= ?^= +<= …
            static const std::set<std::string> bitwiseBase = {
                "+&", "+|", "+^", "~&", "~|", "~^", "?&", "?|", "?^", "+<", "+>", "~<", "~>"};
            if (o.size() > 2 && o.back() == '=' && bitwiseBase.count(o.substr(0, o.size() - 1))) {
                in.valid = true; in.lbp = BP_ASSIGN; in.rightAssoc = true; in.isAssign = true; return in;
            }
        }
        if (o == "??") { in.valid = true; in.lbp = BP_TERNARY; in.isTernary = true; return in; }
        if (o == "**") { in.valid = true; in.lbp = BP_POW; in.rightAssoc = true; return in; }
        if (o == "*" || o == "/" || o == "%" || o == "%%" || o == "!%%") { in.valid = true; in.lbp = BP_MUL; return in; }
        // bitwise/boolean: `+& ~& ?& +< +> ~< ~>` bind like `*` (multiplicative);
        // `+| +^ ~| ~^ ?| ?^` bind like `+` (additive)
        if (o == "+&" || o == "~&" || o == "?&" || o == "+<" || o == "+>" || o == "~<" || o == "~>") { in.valid = true; in.lbp = BP_MUL; return in; }
        if (o == "+|" || o == "+^" || o == "~|" || o == "~^" || o == "?|" || o == "?^") { in.valid = true; in.lbp = BP_ADD; return in; }
        if (o == "+" || o == "-") { in.valid = true; in.lbp = BP_ADD; return in; }
        if (o == "~") { in.valid = true; in.lbp = BP_CONCAT; return in; } // concatenation: looser than x/xx
        if (o == ".." || o == "..^" || o == "^.." || o == "^..^") { in.valid = true; in.lbp = BP_RANGE; in.isRange = true; return in; }
        if (o == "..." || o == "...^") { in.valid = true; in.lbp = BP_COMMA; return in; } // sequence op: looser than comma, so `1,3 ... 19` seeds with (1,3)
        if (o == "==>") { in.valid = true; in.lbp = BP_OR; return in; } // forward feed (left-assoc: data flows L→R)
        if (o == "<==") { in.valid = true; in.lbp = BP_OR; in.rightAssoc = true; return in; } // backward feed (right-assoc: the far-right source flows leftward)
        if (o == "==" || o == "!=" || o == "<" || o == "<=" || o == ">" || o == ">=" ||
            o == "<=>" || o == "~~" || o == "!~~" || o == "=:=" || o == "===" || o == "!==" || o == "!===" ||
            o == "=~=" || o == "≅") { in.valid = true; in.lbp = BP_COMPARE; return in; }
        if (o == "&&") { in.valid = true; in.lbp = BP_ANDAND; return in; }
        if (o == "||" || o == "//" || o == "^^") { in.valid = true; in.lbp = BP_OROR; return in; }
        if (o == "|" || o == "&" || o == "^") { in.valid = true; in.lbp = BP_ADD; return in; } // junctions any/all/one
        // hyper binary metaop  >>OP>>  etc.
        if (o.size() >= 5 && (o.substr(0, 2) == ">>" || o.substr(0, 2) == "<<") &&
            (o.substr(o.size() - 2) == ">>" || o.substr(o.size() - 2) == "<<")) {
            in.valid = true; in.lbp = BP_ADD; return in;
        }
        // set/bag combining operators -> additive-ish precedence (produce Set/Bag)
        static const std::set<std::string> setCombine = {
            "(|)", "∪", "(&)", "∩", "(-)", "∖", "(^)", "⊖",
            "(+)", "⊎", "(.)", "⊍",
        };
        // set membership/relational operators -> comparison precedence (produce Bool)
        static const std::set<std::string> setCompare = {
            "(elem)", "∈", "(!elem)", "∉", "(cont)", "∋", "(!cont)", "∌",
            "(<=)", "⊆", "(<)", "⊂", "(>=)", "⊇", "(>)", "⊃",
            "(==)", "(!=)", "(<>)",
        };
        if (setCombine.count(o)) { in.valid = true; in.lbp = BP_ADD; return in; }
        if (o.size() > 1 && o.back() == '=' && setCombine.count(o.substr(0, o.size() - 1))) {
            in.valid = true; in.lbp = BP_ASSIGN; in.rightAssoc = true; in.isAssign = true; return in; // ∩= ∪= …
        }
        if (setCompare.count(o)) { in.valid = true; in.lbp = BP_COMPARE; return in; }
        if (o == "\xE2\x88\x98") { in.valid = true; in.lbp = BP_MUL; return in; } // ∘ function composition
        return in;
    }
    if (t.kind == Tok::Ident) {
        const std::string& o = t.text;
        in.op = o;
        if (o == "eq" || o == "ne" || o == "lt" || o == "gt" || o == "le" || o == "ge" ||
            o == "cmp" || o == "leg" || o == "eqv" || o == "before" || o == "after" ||
            o == "unicmp" || o == "coll") { in.valid = true; in.lbp = BP_COMPARE; return in; }
        if (o == "x" || o == "xx") { in.valid = true; in.lbp = BP_REPLICATE; return in; }
        if (o == "div" || o == "mod" || o == "gcd" || o == "lcm") {
            in.valid = true; in.lbp = BP_MUL; return in;
        }
        if (o == "does" || o == "but") { in.valid = true; in.lbp = BP_MUL; return in; }
        if (o == "o") { in.valid = true; in.lbp = BP_MUL; return in; } // ASCII alias for ∘ (function composition)
        if (o == "Z" || o == "X") { in.valid = true; in.lbp = BP_ZIP; return in; } // zip / cross: list infix, looser than comma
        {   // stacked zip/cross metaops: XZ / ZZ / XX (optionally with a tight op after)
            bool allZX = o.size() > 1;
            for (char c : o) if (c != 'Z' && c != 'X') { allZX = false; break; }
            if (allZX) { in.valid = true; in.lbp = BP_ZIP; return in; }
        }
        {   // zip/cross metaop over a WORD-op base that lexed as one ident:
            // `Zcmp` `Xeq` `Zminmax` `XZcmp` — a Z/X run followed by a word infix.
            size_t i = 0;
            while (i < o.size() && (o[i] == 'Z' || o[i] == 'X')) i++;
            static const std::set<std::string> wordBase = {
                "cmp", "leg", "eqv", "eq", "ne", "lt", "gt", "le", "ge", "before",
                "after", "unicmp", "coll", "min", "max", "minmax", "gcd", "lcm",
                "div", "mod", "x", "xx", "and", "or", "andthen", "orelse", "but", "does"};
            if (i > 0 && i < o.size() && wordBase.count(o.substr(i))) {
                in.valid = true; in.lbp = BP_ZIP; return in;
            }
        }
        if (o == "minmax") { in.valid = true; in.lbp = BP_ZIP; return in; } // list infix
        if (o == "min" || o == "max") { in.valid = true; in.lbp = BP_ADD; return in; } // infix min/max
        if (o == "and" || o == "andthen" || o == "notandthen") { in.valid = true; in.lbp = BP_AND; return in; }
        if (o == "or" || o == "xor" || o == "orelse") { in.valid = true; in.lbp = BP_OR; return in; }
        if (o == "ff" || o == "fff") { in.valid = true; in.lbp = BP_TERNARY; return in; } // flip-flop
        return in;
    }
    return in;
}

// The left binding power of a named infix — a user-declared one, or a built-in
// (symbolic like `+` or word-form like `eqv`). Used to resolve `is tighter(&infix:<+>)`.
int Parser::infixBpOf(const std::string& op) const {
    auto it = userInfix_.find(op);
    if (it != userInfix_.end()) return it->second;
    Token t; t.text = op; t.kind = Tok::Op;
    InfixInfo in = classifyInfix(t);
    if (in.valid) return in.lbp;
    t.kind = Tok::Ident; // word-form built-ins: eqv/cmp/x/xx/Z/X/and/or/…
    in = classifyInfix(t);
    if (in.valid) return in.lbp;
    return BP_ADD; // unknown reference → additive default
}

// A loop used in value context (`(for … {…})`, `do while … {…}`) collects each
// iteration's value into a List — flag the parsed loop statement so exec knows.
static void markLoopAsExpr(Stmt* s) {
    if (!s) return;
    if (s->kind == NK::ForStmt) static_cast<ForStmt*>(s)->asExpr = true;
    else if (s->kind == NK::WhileStmt) static_cast<WhileStmt*>(s)->asExpr = true;
    else if (s->kind == NK::LoopStmt) static_cast<LoopStmt*>(s)->asExpr = true;
}

// A fused hyper op token (`«+»` lexes as `<<+>>`) written into an operator NAME
// (&infix:<…>) would collide with the `<<∈>>` double-angle-quote strip — keep
// hyper markers as Unicode «» inside names so the reader stays unambiguous.
static std::string hyperMarkersToUni(std::string s) {
    if (s.size() >= 5) {
        bool ls = s.compare(0, 2, "<<") == 0, lr = s.compare(0, 2, ">>") == 0;
        bool ts = s.compare(s.size() - 2, 2, "<<") == 0, tr = s.compare(s.size() - 2, 2, ">>") == 0;
        if ((ls || lr) && (ts || tr))
            s = std::string(ls ? "\xC2\xAB" : "\xC2\xBB") + s.substr(2, s.size() - 4) +
                (ts ? "\xC2\xAB" : "\xC2\xBB");
    }
    return s;
}

bool Parser::startsTermToken(const Token& t) const {
    switch (t.kind) {
        case Tok::IntLit: case Tok::NumLit: case Tok::StrLit: case Tok::VersionLit: case Tok::StrInterp: case Tok::RegexLit: case Tok::SubstLit:
        case Tok::QwList:
        case Tok::Var: case Tok::LParen: case Tok::LBracket: case Tok::LBrace:
            return true;
        case Tok::Op:
            // `&[+]` — the infix-as-Callable term (say &[+], @ops = "+", &[+], …)
            if (t.text == "&" && &t == &cur() && peek().kind == Tok::LBracket && !peek().spaceBefore)
                return true;
            return t.text == "!" || t.text == "~" || t.text == "\\" || t.text == "<" ||
                   t.text == "+" || t.text == "-" || t.text == "?" || t.text == ":" ||
                   t.text == "++" || t.text == "--" || // prefix incr/decr: `f 0, ++$x`
                   t.text == "*" || t.text == "->" || t.text == "<->" || t.text == "|" ||
                   t.text == "^" || // prefix `^N` (upto) after a comma: `1, ^10 .Seq` (infix ^ is impossible there)
                   t.text == "&" || // operator-as-value `&[+]` (bare `&` in term position is only `&[OP]`)
                   t.text == "." || // leading `.method` => $_.method (e.g. `1, .uc`)
                   t.text == "::" || // symbolic reference `::($name)` / `::Foo`
                   t.text == "\xE2\x88\x9E" || t.text == "\xC2\xAB" || t.text == "<<" || // ∞, «qw», <<qww>>
                   t.text == "$" || t.text == "@" || t.text == "%" || // contextualizers $( $[ @( %(
                   userPrefix_.count(t.text) || userCircumfix_.count(t.text); // user prefix / circumfix-open
        case Tok::Ident:
            // sub/method/do/start begin an expression (anonymous routine / do-block) even though block keywords;
            // my/our/state/has/constant begin a declaration expression (`ok my $x = 5, "d"`)
            // an inline type expression (`…, class Foo {}.new` / `role {…}`) also starts a term
            if ((t.text == "class" || t.text == "role" || t.text == "grammar") && &t == &cur() &&
                (peek().kind == Tok::LBrace || (peek().kind == Tok::Op && peek().text == "::") ||
                 (peek().kind == Tok::Ident && peek(2).kind == Tok::LBrace)))
                return true;
            // a keyword directly followed by `=>` is a bareword PAIR KEY, not the
            // keyword: `register('Anna', role => 'admin')`
            if (&t == &cur() && peek().kind == Tok::FatArrow) return true;
            return !kBlockKeywords.count(t.text) ||
                   t.text == "sub" || t.text == "method" || t.text == "do" || t.text == "start" ||
                   t.text == "my" || t.text == "our" || t.text == "state" || t.text == "has" || t.text == "constant" ||
                   // an ANONYMOUS class/role/grammar is an expression: `is class :: {…}.new.x, …`
                   ((t.text == "class" || t.text == "role" || t.text == "grammar") && &t == &cur() &&
                    (peek().kind == Tok::LBrace || (peek().kind == Tok::Op && peek().text == "::")));
        default:
            return false;
    }
}
// what may begin a list-op argument (no parens; conservative on leading symbols)
bool Parser::startsListopArg(const Token& t) const {
    switch (t.kind) {
        case Tok::IntLit: case Tok::NumLit: case Tok::StrLit: case Tok::VersionLit: case Tok::StrInterp: case Tok::RegexLit: case Tok::SubstLit:
        case Tok::QwList:
        case Tok::Var: case Tok::LParen:
            return true;
        case Tok::LBracket:
            // `foo [1,2]` (spaced) is an array-literal argument; TIGHT `foo[10]`
            // indexes the call's RESULT (Rakudo semantics: (foo())[10])
            return t.spaceBefore;
        case Tok::LBrace: {
            if (!stmtCond_) return true;
            // In a statement condition (`when foo { }`, `if foo { }`) a brace is
            // normally the control block — EXCEPT when the balanced group is
            // followed by a comma, which marks a block argument to a listop:
            // `for map { … }, ^5 { }`. Scan the token stream to check.
            size_t i = &t - &toks_[0]; // index of the LBrace in the stream
            int d = 0;
            for (size_t k = i; k < toks_.size(); k++) {
                if (toks_[k].kind == Tok::LBrace) d++;
                else if (toks_[k].kind == Tok::RBrace) {
                    if (--d == 0) return k + 1 < toks_.size() && toks_[k + 1].kind == Tok::Comma;
                }
                else if (toks_[k].kind == Tok::End) break;
            }
            return false;
        }
        case Tok::Op:
            return t.text == "!" || t.text == "~" || t.text == "\\" || t.text == "<" ||
                   t.text == ":" || t.text == "+" || t.text == "-" || t.text == "?" ||
                   t.text == "++" || t.text == "--" || // prefix incr/decr: `say 0, ++$x`
                   // `test *..1, …` — a Whatever RANGE endpoint can only be an arg
                   // (infix `*` would need a term after it, and `..` isn't one)
                   (t.text == "*" && &t == &cur() &&
                    ((peek().kind == Tok::Op &&
                      (peek().text == ".." || peek().text == "..^" || peek().text == "..." ||
                       peek().text == "^.." || peek().text == "^..^" ||
                       // `say *.abs` / `map *.abs, @a` — a tight `.method` on `*`
                       // is a WhateverCode argument, never infix multiplication
                       (peek().text == "." && !peek().spaceBefore &&
                        peek(2).kind == Tok::Ident))) ||
                     (peek().kind == Tok::Ident &&
                      (userInfix_.count(peek().text) ||
                       peek().text == "min" || peek().text == "max" || peek().text == "gcd" ||
                       peek().text == "lcm" || peek().text == "div" || peek().text == "mod")))) || // `*..1` / `* quack 5` / `* min 2`
                   t.text == "^" || // prefix `^N` (upto) as a listop arg: `flat ^15, 49`
                   (t.text == "|" && t.spaceBefore) || // slip first arg `run |@cmd` (space before |) — NOT infix junction `Any|Blob`
                   t.text == "!!" || // prefix boolify `say !!$x` (`!!` never starts a bare term otherwise)
                   (t.text == "." && t.spaceBefore) || // leading `.method` => $_.method (only after a space: `say .uc`)
                   t.text == "::" || // symbolic reference `say ::($name)` / `say ::Foo`
                   t.text == "$" || // item contextualizer `ok $%*ENV` / `say $(1,2)` (bare `$` is never an infix)
                   ((t.text == "%" || t.text == "@") && &t == &cur() &&
                    ((peek().kind == Tok::LParen && !peek().spaceBefore) ||
                     (peek().kind == Tok::Var && !peek().spaceBefore && peek().text.size() > 1))) || // `%(...)` / `@(...)` / `@$x` / `%$h` contextualizers
                   (t.text == "&" && &t == &cur() &&
                    peek().kind == Tok::LBracket && !peek().spaceBefore) || // infix-as-value `say &[+](2,3)`
                   t.text == "\xE2\x88\x9E" || t.text == "\xC2\xAB" || t.text == "<<" || // ∞, «qw», <<qww>>
                   userPrefix_.count(t.text) || userCircumfix_.count(t.text); // user prefix / circumfix-open
        case Tok::Ident: {
            // A word-infix operator right after a bareword term is an INFIX, not the
            // start of a listop argument: `Seq eqv Seq`, `Int eq Int`, `$x div $y`.
            static const std::set<std::string> wordInfix = {
                "eq", "ne", "lt", "gt", "le", "ge", "cmp", "leg", "eqv", "before", "after", "unicmp", "coll",
                "x", "xx", "and", "or", "andthen", "orelse", "div", "mod", "gcd", "lcm",
            };
            if (wordInfix.count(t.text)) return false;
            // a keyword directly followed by `=>` is a bareword PAIR KEY, not the
            // keyword: `register('Anna', role => 'admin')`
            if (&t == &cur() && peek().kind == Tok::FatArrow) return true;
            // sub/method/do/start begin an expression (anonymous routine / do-block); my/our/state/has/
            // constant begin a declaration expression that is a valid list-op argument (`ok my $x = 5, "d"`)
            return !kBlockKeywords.count(t.text) ||
                   t.text == "sub" || t.text == "method" || t.text == "do" || t.text == "start" ||
                   t.text == "my" || t.text == "our" || t.text == "state" || t.text == "has" || t.text == "constant" ||
                   // an ANONYMOUS class/role/grammar is an expression: `is class :: {…}.new.x, …`
                   ((t.text == "class" || t.text == "role" || t.text == "grammar") && &t == &cur() &&
                    (peek().kind == Tok::LBrace || (peek().kind == Tok::Op && peek().text == "::")));
        }
        default:
            return false;
    }
}

// ---------------- expressions ----------------
ExprPtr Parser::parseExpression() { return parseExpr(0); }

ExprPtr Parser::parseExpr(int minbp) {
    ExprPtr lhs = parsePrefix();
    for (;;) {
        // user-defined infix operator: `4 avg 10`  ==  infix:<avg>(4, 10)
        if (cur().kind == Tok::Ident && userInfix_.count(cur().text)) {
            // meta-assignment `$x op= y` — the infix tight against `=`
            if (peek().kind == Tok::Op && peek().text == "=" && !peek().spaceBefore) {
                if (BP_ASSIGN < minbp) break;
                std::string opname = advance().text; advance(); // op then '='
                auto a = std::make_unique<Assign>();
                a->target = std::move(lhs);
                a->op = opname + "=";
                a->value = parseExpr(BP_ASSIGN); // right-associative
                lhs = std::move(a);
                continue;
            }
            // precedence from the operator's declared trait (default additive);
            // right-assoc if declared `is assoc<right>`
            int bp = userInfix_[cur().text];
            bool rightAssoc = userInfixRight_.count(cur().text) != 0;
            if (bp < minbp) break;
            std::string opname = advance().text;
            auto call = std::make_unique<Call>();
            call->name = "infix:<" + opname + ">";
            call->args.push_back(std::move(lhs));
            call->args.push_back(parseExpr(rightAssoc ? bp : bp + 1));
            lhs = std::move(call);
            continue;
        }
        // reverse metaoperator `a R/ b` == `b / a` (R immediately before an infix op)
        if (cur().kind == Tok::Ident && cur().text == "R" && peek().kind == Tok::Op && !peek().spaceBefore) {
            InfixInfo base = classifyInfix(peek());
            if (base.valid && base.isAssign && BP_ASSIGN >= minbp) {
                // `$x R~= $y` — reversed-role assignment (assigns to the RIGHT operand)
                advance(); // R
                std::string baseOp = advance().text;
                auto as = std::make_unique<Assign>();
                as->op = "R" + baseOp;
                as->target = std::move(lhs);
                as->value = parseExpr(BP_ASSIGN);
                lhs = std::move(as);
                continue;
            }
            if (base.valid && base.lbp >= minbp) {
                advance(); // R
                std::string baseOp = advance().text;
                auto bin = std::make_unique<Binary>();
                bin->op = "R" + baseOp;
                bin->lhs = std::move(lhs);
                bin->rhs = parseExpr(base.lbp + 1);
                lhs = std::move(bin);
                continue;
            }
        }
        // bracketed infix: `A [op] B` (any infix may be enclosed in square
        // brackets) and the metaop-assignment form `A [op]= B` — LibraryMake:
        // `%vars{$k} [R//]= %*ENV{$k}`. Content must be exactly an operator
        // (optionally R-prefixed) then `]`; anything else backtracks untouched.
        if (cur().kind == Tok::LBracket && cur().spaceBefore) {
            size_t save = pos_;
            advance(); // [
            std::string rPfx;
            if (cur().kind == Tok::Ident && cur().text == "R" && peek().kind == Tok::Op && !peek().spaceBefore) {
                rPfx = "R"; advance();
            }
            bool made = false;
            if (cur().kind == Tok::Op && cur().text != "=" && peek().kind == Tok::RBracket) {
                InfixInfo base = classifyInfix(cur());
                if (base.valid) {
                    std::string baseOp = advance().text;
                    advance(); // ]
                    bool assignForm = isOp("=") && !cur().spaceBefore;
                    if (assignForm && BP_ASSIGN >= minbp) {
                        advance(); // =
                        auto as = std::make_unique<Assign>();
                        as->op = "[" + rPfx + baseOp + "]=";
                        as->target = std::move(lhs);
                        as->value = parseExpr(BP_ASSIGN);
                        lhs = std::move(as);
                        made = true;
                    }
                    else if (!assignForm && base.lbp >= minbp) {
                        auto bin = std::make_unique<Binary>();
                        bin->op = rPfx + baseOp;
                        bin->lhs = std::move(lhs);
                        bin->rhs = parseExpr(base.lbp + 1);
                        lhs = std::move(bin);
                        made = true;
                    }
                }
            }
            if (made) continue;
            pos_ = save; // not a bracketed infix — leave for other handlers
        }
        // Space-separated colon-pairs form a list: `%( :a{1} :b{2} :c(3) )`.
        // Only continue when we're already building a pair/list (so an adverb like
        // `f() :flag` is not mistaken for a new list element).
        if (minbp <= BP_COMMA && cur().kind == Tok::Op && cur().text == ":" &&
            (peek().kind == Tok::Ident || peek().kind == Tok::Var ||
             (peek().kind == Tok::Op && peek().text == "!")) &&
            (lhs->kind == NK::Pair || lhs->kind == NK::ListExpr)) {
            std::unique_ptr<ListExpr> list;
            if (lhs->kind == NK::ListExpr && !static_cast<ListExpr*>(lhs.get())->parenned)
                list.reset(static_cast<ListExpr*>(lhs.release()));
            else { list = std::make_unique<ListExpr>(); list->items.push_back(std::move(lhs)); }
            list->items.push_back(parseColonPair());
            lhs = std::move(list);
            continue;
        }
        // negated infix (`!eq`, `!%%` when lexed apart): `!` glued to a negatable
        // comparison op applies the op and negates its Bool
        if (cur().kind == Tok::Op && cur().text == "!" && !peek().spaceBefore) {
            static const std::set<std::string> negatable = {
                "eq", "ne", "lt", "gt", "le", "ge", "before", "after", "eqv",
                "%%", "==", "<", ">", "<=", ">=",
            };
            InfixInfo negIn = classifyInfix(peek());
            if (negIn.valid && negatable.count(negIn.op) && negIn.lbp >= minbp) {
                advance(); advance(); // ! and the op
                auto bin = std::make_unique<Binary>();
                bin->op = "!" + negIn.op;
                bin->lhs = std::move(lhs);
                bin->rhs = parseExpr(negIn.lbp + 1);
                lhs = std::move(bin);
                continue;
            }
        }
        InfixInfo in = classifyInfix(cur());
        // reduce-metaop assign `$x [+]= 6, 7` — just like `+=` (item assignment);
        // `[` is not an infix, so this must be recognized before the validity break
        if (!in.valid && cur().kind == Tok::LBracket && cur().spaceBefore &&
            ((peek().kind == Tok::Op && peek(2).kind == Tok::RBracket &&
              peek(3).kind == Tok::Op && peek(3).text == "=") ||
             (peek().kind == Tok::LBracket && peek(2).kind == Tok::Op &&
              peek(3).kind == Tok::RBracket && peek(4).kind == Tok::RBracket &&
              peek(5).kind == Tok::Op && peek(5).text == "=")) &&
            BP_ASSIGN >= minbp) {
            bool nested = peek().kind == Tok::LBracket; // [[+]]= — same as [+]=
            advance(); if (nested) advance(); // [ ([)
            std::string op = advance().text;
            advance(); if (nested) advance(); // ] (])
            advance(); // =
            auto as = std::make_unique<Assign>();
            as->op = op + "=";
            as->target = std::move(lhs);
            as->value = parseExpr(BP_ASSIGN);
            lhs = std::move(as);
            continue;
        }
        // hyper with a bracketed operator value: $a >>[&infix:<+>]<< $b — the
        // marker + [ + CALLABLE + ] + marker; desugars to zip(:with(CALLABLE))
        if (!in.valid && cur().kind == Tok::Op &&
            (cur().text == ">>" || cur().text == "<<" ||
             cur().text == "\xC2\xBB" || cur().text == "\xC2\xAB") &&
            peek().kind == Tok::LBracket && BP_ZIP >= minbp) {
            // the two markers carry the DWIM rules (»=strict left, «=dwimmy) —
            // normalize to ASCII and encode them in the call name.
            std::string m1 = (cur().text == "<<" || cur().text == "\xC2\xAB") ? "<<" : ">>";
            advance(); advance(); // marker [
            ExprPtr opv = parseExpression();
            expectKind(Tok::RBracket, "]");
            std::string m2 = ">>";
            if (cur().kind == Tok::Op &&
                (cur().text == ">>" || cur().text == "<<" ||
                 cur().text == "\xC2\xBB" || cur().text == "\xC2\xAB")) {
                m2 = (cur().text == "<<" || cur().text == "\xC2\xAB") ? "<<" : ">>";
                advance();
            }
            // rhs stops at a comma (BP_ZIP sits below BP_COMMA): in
            // `is-deeply $a >>[&op]<< $b, $result, "…"` the operand is just $b.
            ExprPtr rhs = parseExpr(BP_COMMA + 1);
            auto c = std::make_unique<Call>();
            c->name = "hyper-with:" + m1 + m2; // evalCall applies the callable element-wise;
            c->args.push_back(std::move(lhs)); // keeping the lhs AST lets an assign
            c->args.push_back(std::move(rhs)); // metaop (&[+=]) store back through it
            auto pw = std::make_unique<PairExpr>();
            pw->key = "with"; pw->value = std::move(opv);
            c->args.push_back(std::move(pw));
            lhs = std::move(c);
            continue;
        }
        // a word infix tight against `=` (`$a or= 3`, `$n gcd= 12`) binds as an
        // ASSIGNMENT op, not at the word's own (possibly looser) precedence —
        // `@p = $a or= 3, 4` nests as `@p = ($a or= (3, 4))`
        bool wordAssign = in.valid && cur().kind == Tok::Ident &&
                          peek().kind == Tok::Op && peek().text == "=" && !peek().spaceBefore;
        if (!in.valid || (wordAssign ? BP_ASSIGN : in.lbp) < minbp) break;

        if (in.isTernary) {
            advance(); // ??
            ExprPtr then = parseExpr(0);
            if (!matchOp("!!")) error("expected '!!' in ternary");
            ExprPtr els = parseExpr(BP_ASSIGN);
            auto tn = std::make_unique<Ternary>();
            tn->cond = std::move(lhs); tn->then = std::move(then); tn->els = std::move(els);
            lhs = std::move(tn);
            continue;
        }
        // chained comparisons:  2 < $x < 4  ==>  (2 < $x) && ($x < 4), each operand once
        static const std::set<std::string> chainOps = {
            "<", ">", "<=", ">=", "==", "!=", "eq", "ne", "lt", "gt", "le", "ge",
        };
        if (in.lbp == BP_COMPARE && chainOps.count(in.op)) {
            auto chain = std::make_unique<ChainExpr>();
            chain->operands.push_back(std::move(lhs));
            while (true) {
                InfixInfo ci = classifyInfix(cur());
                if (!ci.valid || ci.lbp != BP_COMPARE || !chainOps.count(ci.op)) break;
                advance();
                chain->ops.push_back(ci.op);
                chain->operands.push_back(parseExpr(BP_COMPARE + 1));
            }
            if (chain->ops.size() == 1) {
                auto bin = std::make_unique<Binary>();
                bin->op = chain->ops[0];
                bin->lhs = std::move(chain->operands[0]);
                bin->rhs = std::move(chain->operands[1]);
                lhs = std::move(bin);
            } else {
                lhs = std::move(chain);
            }
            continue;
        }
        if (in.isComma) {
            advance();
            std::unique_ptr<ListExpr> list;
            // Extend the running comma-chain, but only if lhs is an un-parenthesised
            // list. A `( … )` list is a distinct nested element (so `(1,2),(3,4)` is a
            // 2-element list of lists, not a flat 1,2,3,4).
            if (lhs->kind == NK::ListExpr && !static_cast<ListExpr*>(lhs.get())->parenned) {
                list.reset(static_cast<ListExpr*>(lhs.release()));
            } else {
                list = std::make_unique<ListExpr>();
                list->items.push_back(std::move(lhs));
            }
            // a statement-modifier keyword after a comma TERMINATES the list —
            // `(1,2, without Nil)` applies the modifier to the whole list
            bool modNext = cur().kind == Tok::Ident &&
                (cur().text == "if" || cur().text == "unless" || cur().text == "with" ||
                 cur().text == "without" || cur().text == "for" || cur().text == "while" ||
                 cur().text == "until" || cur().text == "given") &&
                peek().kind != Tok::FatArrow; // `if => 2` is a pair
            if (!modNext && startsTermToken(cur())) {
                list->items.push_back(parseExpr(BP_COMMA + 1));
            }
            lhs = std::move(list);
            continue;
        }
        if (in.isFatArrow) {
            // `1, => 2` — a fat-arrow directly after a comma has no key term
            if (pos_ > 0 && toks_[pos_ - 1].kind == Tok::Comma)
                throw ParseError("Preceding context expects a term, but found infix => instead",
                                 cur().line, "X::Syntax::InfixInTermPosition", {{"infix", "=>"}});
            advance();
            auto p = std::make_unique<PairExpr>();
            if (lhs->kind == NK::NameTerm) p->key = static_cast<NameTerm*>(lhs.get())->name;
            else if (lhs->kind == NK::StrLit) { p->key = static_cast<StrLit*>(lhs.get())->v; p->quotedKey = true; } // 'a' => 1 stays a POSITIONAL arg
            else p->keyExpr = std::move(lhs); // $var / "interp" / (expr) keys evaluated at runtime
            p->value = parseExpr(BP_ASSIGN);
            lhs = std::move(p);
            continue;
        }

        // zip/cross metaoperator with a trailing tight op: Z=> Z+ X* Zeq XZ+ ...
        bool zxStack = !in.op.empty();
        for (char c : in.op) if (c != 'Z' && c != 'X') { zxStack = false; break; }
        if (zxStack) {
            std::string meta;
            if (peek().kind == Tok::FatArrow) meta = "=>";
            else if (peek().kind == Tok::Comma) meta = ","; // Z, / X, — zip/cross into tuples
            else if (peek().kind == Tok::Op) meta = peek().text;
            if (!meta.empty() && !peek().spaceBefore) {
                advance(); advance(); // consume Z/X and the trailing op
                ExprPtr rhs = parseExpr(in.lbp + 1);
                auto bin = std::make_unique<Binary>();
                bin->op = in.op + meta; // "Z=>" / "Z+" / "X*"
                bin->lhs = std::move(lhs); bin->rhs = std::move(rhs);
                lhs = std::move(bin);
                continue;
            }
        }

        // word-operator compound assignment: `$p x= 3`, `$n gcd= 12` — a textual
        // infix tight against `=` (symbolic forms like `+=` are already one token).
        if (wordAssign) {
            std::string opname = advance().text; advance(); // the word op, then '='
            auto a = std::make_unique<Assign>();
            a->target = std::move(lhs);
            a->op = opname + "=";
            // the loose boolean words take the whole comma list (`$a or= 3, 4`
            // assigns the List); tight words (`$n gcd= 12`) stay item assignment
            static const std::set<std::string> looseWord = {
                "or", "and", "xor", "orelse", "andthen", "notandthen"};
            a->value = parseExpr(looseWord.count(opname) ? BP_ZIP : BP_ASSIGN);
            lhs = std::move(a);
            continue;
        }

        advance(); // consume infix op

        // list assignment: `@a = 1,2,3` / `my ($a,$b) = ...` grabs the whole comma
        // list; binding does too (`my @r := &min, &max, &minmax` is a 3-element bind)
        bool listAssign = false;
        if (in.isAssign && (in.op == "=" || in.op == ":=")) {
            // `$/ = "x"` (a bare string-literal rhs) is the P5 input-record-separator
            // idiom — a compile error in Raku. `$/ = ('x')` and non-string rhs are fine.
            if (in.op == "=" && lhs->kind == NK::VarExpr &&
                static_cast<VarExpr*>(lhs.get())->name == "$/" &&
                !static_cast<VarExpr*>(lhs.get())->declare &&
                (cur().kind == Tok::StrLit || cur().kind == Tok::StrInterp)) // rhs first token (op already consumed)
                throw ParseError("Unsupported use of $/ variable; in Raku please use the filehandle's .nl-in attribute", cur().line);
            if (lhs->kind == NK::ListExpr) listAssign = true;
            else if (lhs->kind == NK::VarExpr) {
                const std::string& nm = static_cast<VarExpr*>(lhs.get())->name;
                if (!nm.empty() && (nm[0] == '@' || nm[0] == '%')) listAssign = true;
            }
            else if (lhs->kind == NK::SymbolicRef) {
                // `@::($n) = 1,2,3` / `%::($n) = …` — a sigilled symbolic deref
                // is a list container, so it takes the whole comma list
                const std::string& sg = static_cast<SymbolicRef*>(lhs.get())->sigil;
                if (sg == "@" || sg == "%") listAssign = true;
            }
            else if (lhs->kind == NK::Index) {
                // a slice target (`@a[1,2] = …`, `%h{^3} = …`, `%h{@ks} = …`)
                // takes the whole comma list too
                auto* ix = static_cast<Index*>(lhs.get());
                if (ix->index && !ix->multiDim &&
                    (ix->index->kind == NK::ListExpr || ix->index->kind == NK::Range ||
                     (ix->index->kind == NK::VarExpr && !static_cast<VarExpr*>(ix->index.get())->name.empty() &&
                      static_cast<VarExpr*>(ix->index.get())->name[0] == '@') ||
                     (ix->index->kind == NK::Unary && static_cast<Unary*>(ix->index.get())->op == "^")))
                    listAssign = true;
            }
        }

        int nextMin = listAssign ? BP_ZIP : (in.rightAssoc ? in.lbp : in.lbp + 1); // list assign includes Z/X (looser than comma)
        ExprPtr rhs = parseExpr(nextMin);

        if (in.isAssign) {
            auto a = std::make_unique<Assign>();
            a->target = std::move(lhs); a->op = in.op; a->value = std::move(rhs);
            lhs = std::move(a);
        } else if (in.isRange) {
            auto r = std::make_unique<RangeExpr>();
            r->from = std::move(lhs); r->to = std::move(rhs);
            r->exTo = (in.op == "..^" || in.op == "^..^");
            r->exFrom = (in.op == "^.." || in.op == "^..^");
            lhs = std::move(r);
        } else {
            // non-associative operators cannot chain: `1 <=> 2 <=> 3`
            if ((in.op == "<=>" || in.op == "cmp" || in.op == "leg" ||
                 in.op == "unicmp" || in.op == "coll") &&
                cur().kind == Tok::Op && cur().text == in.op)
                throw ParseError("Operator " + in.op + " is not associative",
                                 cur().line, "X::Syntax::NonAssociative",
                                 {{"left", in.op}, {"right", in.op}});
            auto b = std::make_unique<Binary>();
            b->op = in.op; b->lhs = std::move(lhs); b->rhs = std::move(rhs);
            lhs = std::move(b);
        }
    }
    return lhs;
}

ExprPtr Parser::parsePrefix(bool tight) {
    if (cur().kind == Tok::Op) {
        const std::string& o = cur().text;
        // hyper prefix: -«(1,2) / -<<@a / --<<%h — apply the prefix op to every
        // element, descending into nested arrays (deep distribution); ++/--
        // mutate the elements in place
        if ((o == "!" || o == "-" || o == "+" || o == "~" || o == "?" ||
             o == "++" || o == "--") &&
            peek().kind == Tok::Op && !peek().spaceBefore &&
            (peek().text == "\xC2\xAB" || peek().text == "<<" ||
             peek().text == "\xC2\xBB" || peek().text == ">>")) {
            advance(); advance(); // op marker
            auto u = std::make_unique<Unary>();
            u->op = "hyper:" + o;
            u->operand = parsePrefix(true);
            return parsePostfix(std::move(u), tight);
        }
        if (o == "!" || o == "-" || o == "+" || o == "~" || o == "?" ||
            o == "++" || o == "--" || o == "^" || o == "|" ||
            o == "+^" || o == "?^" || o == "~^") { // prefix bitwise/boolean NOT
            advance();
            auto u = std::make_unique<Unary>();
            // the operand parses "tight" (its own postfixes stop at a space-preceded
            // `.method`), so `^30 .map` is (^30).map while `^30.map` stays ^(30.map).
            u->op = o; u->operand = parsePrefix(true);
            // `**` binds tighter than symbolic unary: -2**2 == -(2**2) and
            // ^2**64 == ^(2**64)  (++/-- keep their assignable-operand parse)
            if (o != "++" && o != "--" && cur().kind == Tok::Op && cur().text == "**") {
                advance();
                auto pw = std::make_unique<Binary>();
                pw->op = "**";
                pw->lhs = std::move(u->operand);
                pw->rhs = parseExpr(BP_POW); // right-assoc tier
                u->operand = std::move(pw);
            }
            return parsePostfix(std::move(u), tight);
        }
        if (o == "!!") { // prefix double-negation == boolify
            advance();
            auto u = std::make_unique<Unary>();
            u->op = "?"; u->operand = parseExpr(BP_PREFIX);
            return u;
        }
        // contextualizer: $(...) @(...) %(...) $[...] ${...} $@foo etc.
        if (o == "$" || o == "@" || o == "%") {
            advance();
            auto u = std::make_unique<Unary>();
            u->op = "ctx" + o;
            // circumfix forms complete the term at the closing bracket — postfixes
            // bind to the CONTEXTUALIZED value: %(:a(1)).raku is (%(:a(1))).raku,
            // ${:k(1)} is an itemized hash, $[1,2].elems counts the itemized array.
            if (isKind(Tok::LParen) && !cur().spaceBefore) {
                advance();
                u->operand = isKind(Tok::RParen) ? ExprPtr(std::make_unique<ListExpr>())
                                                 : parseExpression();
                // `$(stmt; stmt; expr)` — a statement sequence valued at its last
                // statement, like `do { … }` (let.t/temp.t idiom)
                if (isKind(Tok::Semicolon)) {
                    auto be = std::make_unique<BlockExpr>();
                    auto es = std::make_unique<ExprStmt>();
                    es->e = std::move(u->operand);
                    be->body.push_back(std::move(es));
                    while (matchKind(Tok::Semicolon)) {
                        if (isKind(Tok::RParen)) break;
                        auto es2 = std::make_unique<ExprStmt>();
                        es2->e = parseExpression();
                        be->body.push_back(std::move(es2));
                    }
                    auto dou = std::make_unique<Unary>();
                    dou->op = "stmtseq"; dou->operand = std::move(be);
                    u->operand = std::move(dou);
                }
                if (u->operand->kind == NK::ListExpr) static_cast<ListExpr*>(u->operand.get())->parenned = true;
                expectKind(Tok::RParen, ")");
                return parsePostfix(std::move(u), tight);
            }
            if ((isKind(Tok::LBrace) || isKind(Tok::LBracket)) && !cur().spaceBefore) {
                u->operand = parsePrimary();
                return parsePostfix(std::move(u), tight);
            }
            // sigil-on-variable form ($@foo, $%h): parsePrefix (not just primary) so
            // nested contextualizers work; tight, so a space-preceded `.method`
            // binds to the contextualized value.
            u->operand = parsePrefix(true);
            return parsePostfix(std::move(u), tight);
        }
    }
    // user-defined prefix operator: `sub prefix:<§>` — symbolic (Op) or word (Ident)
    if ((cur().kind == Tok::Op || cur().kind == Tok::Ident) && userPrefix_.count(cur().text)) {
        auto u = std::make_unique<Unary>();
        u->op = advance().text;
        u->operand = parsePrefix(true);
        return parsePostfix(std::move(u), tight);
    }
    return parsePostfix(parsePrimary(), tight);
}

std::vector<ExprPtr> Parser::parseCallArgs(ExprPtr* invocant) {
    std::vector<ExprPtr> args;
    if (isKind(Tok::RParen)) { advance(); return args; }
    bool svCond = stmtCond_; stmtCond_ = false; // parens re-allow block listop args
    struct CondRestore { bool* f; bool v; ~CondRestore() { *f = v; } } cr{&stmtCond_, svCond};
    ExprPtr e = parseExpression();
    // indirect-invocant colon: `key($pair: args)` == `$pair.key(args)` — a single
    // first expression followed by a tight `:` (not `::`, not a chained adverb) is
    // the method invocant, handed back to the caller to build a MethodCall.
    if (invocant && isOp(":") && !cur().spaceBefore &&
        e->kind != NK::Pair && e->kind != NK::ListExpr &&
        peek().text != ":") {
        advance(); // the invocant-marking ':'
        *invocant = std::move(e);
        if (isKind(Tok::RParen)) { advance(); return args; }
        e = parseExpression();
    }
    for (;;) {
        // a comma list spreads into arguments — but a PARENNED list `g((3,4))`
        // is one List argument (it destructures as a unit)
        if (e->kind == NK::ListExpr && !static_cast<ListExpr*>(e.get())->parenned) {
            auto* l = static_cast<ListExpr*>(e.get());
            for (auto& it : l->items) args.push_back(std::move(it));
        } else {
            args.push_back(std::move(e));
        }
        // chained adverbs without a comma — `f(:x("a"):y("b"))` — parseExpression
        // stops at the `:` (it isn't an infix), so pick up each following colonpair.
        if (isOp(":") && (peek().kind == Tok::Ident ||
                          (peek().kind == Tok::Op && peek().text == "!") ||
                          peek().kind == Tok::IntLit || peek().kind == Tok::Var)) {
            e = parseExpr(BP_ASSIGN); // exactly one colonpair
            continue;
        }
        // an adverb may be followed by a comma resuming the ordinary arg list
        if (matchKind(Tok::Comma)) {
            if (isKind(Tok::RParen)) break; // trailing comma
            e = parseExpression();
            continue;
        }
        break;
    }
    // semicolon argument segments — `zip(1,2; 3,4; 5,6)` / `f(@a; @b)`: each
    // `;`-segment is ONE argument (a List when it has several items, the bare
    // expression when it has one)
    if (isKind(Tok::Semicolon)) {
        auto segArg = [](std::vector<ExprPtr>& items) -> ExprPtr {
            if (items.size() == 1) return std::move(items[0]);
            auto seg = std::make_unique<ListExpr>();
            seg->parenned = true;
            for (auto& it : items) seg->items.push_back(std::move(it));
            return seg;
        };
        std::vector<ExprPtr> first = std::move(args);
        args.clear();
        args.push_back(segArg(first));
        while (matchKind(Tok::Semicolon)) {
            if (isKind(Tok::RParen)) break;
            ExprPtr se = parseExpression();
            std::vector<ExprPtr> items;
            if (se->kind == NK::ListExpr && !static_cast<ListExpr*>(se.get())->parenned) {
                auto* l = static_cast<ListExpr*>(se.get());
                for (auto& it : l->items) items.push_back(std::move(it));
            }
            else items.push_back(std::move(se));
            args.push_back(segArg(items));
        }
    }
    expectKind(Tok::RParen, ")");
    return args;
}

ExprPtr Parser::parsePostfix(ExprPtr base, bool stopAtSpaceDot) {
    bool hyperNext = false;
    for (;;) {
        // when parsing the operand of a prefix op, a space-preceded `.method` binds
        // to the whole prefix expression, not the operand — stop here so the caller grabs it.
        if (stopAtSpaceDot && isOp(".") && cur().spaceBefore) break;
        // user postcircumfix operator: `$x¦key¦` == postcircumfix:<¦ ¦>($x, key)
        if ((cur().kind == Tok::Op || cur().kind == Tok::Ident) && !cur().spaceBefore &&
            userPostcircumfix_.count(cur().text) && cur().text != pcfxClose_) {
            std::string open = advance().text, close = userPostcircumfix_[open];
            auto call = std::make_unique<Call>();
            call->name = "postcircumfix:<" + open + " " + close + ">";
            call->args.push_back(std::move(base));
            std::string savedClose = pcfxClose_; pcfxClose_ = close; // don't reopen the close inside content
            if (cur().text != close) call->args.push_back(parseExpression());
            pcfxClose_ = savedClose;
            if (cur().text == close) advance(); else error("expected postcircumfix closing '" + close + "'");
            base = std::move(call);
            continue;
        }
        // hyper method call: @a>>.method / @a<<.method (»« multibyte too)
        if ((isOp(">>") || isOp("<<") || isOp("»") || isOp("«")) && peek().kind == Tok::Op && peek().text == ".") {
            advance(); hyperNext = true; continue;
        }
        // hyper postfix index / power / increment: @w»[0], @n»**2 (from »²), %h{…}»++
        // — but NOT the hyper-infix bracket-op form `@a >>[&infix:<+>]<< @b`,
        // recognizable by a hyper marker right after the matching `]`.
        auto bracketOpHyper = [&]() {
            if (peek().kind != Tok::LBracket) return false;
            int d = 0;
            for (size_t k = 1; peek(k).kind != Tok::End; k++) {
                if (peek(k).kind == Tok::LBracket) d++;
                else if (peek(k).kind == Tok::RBracket) {
                    if (--d == 0) {
                        const Token& nx = peek(k + 1);
                        return nx.kind == Tok::Op &&
                               (nx.text == ">>" || nx.text == "<<" ||
                                nx.text == "\xC2\xBB" || nx.text == "\xC2\xAB");
                    }
                }
            }
            return false;
        };
        if ((isOp(">>") || isOp("»")) && !bracketOpHyper() &&
            (peek().kind == Tok::LBracket ||
             (peek().kind == Tok::Op && (peek().text == "**" || peek().text == "++" || peek().text == "--")) ||
             ((peek().kind == Tok::Op || peek().kind == Tok::Ident) &&
              (userPostfix_.count(peek().text) || peek().text == "i")) || // built-in postfix:<i>
             (peek().kind == Tok::Op && peek().text == "." &&
              peek(2).kind == Tok::Op && userPostfix_.count(peek(2).text)))) { // ».OP dotted form: symbolic only (».foo is a METHOD call)
            advance(); // the hyper marker
            // »++ / »-- / »OP (user postfix, postfix:<i>): a runtime hyper —
            // descends nested arrays, keeps hash keys, ++/-- mutate in place
            if (cur().text == "++" || cur().text == "--" ||
                (!isKind(Tok::LBracket) && cur().text != "**")) {
                if (cur().kind == Tok::Op && cur().text == ".") advance(); // dotted form
                std::string opname = advance().text;
                auto call = std::make_unique<Call>();
                call->name = "hyper-postfix:<" + opname + ">";
                call->args.push_back(std::move(base));
                base = std::move(call);
                continue;
            }
            auto mc = std::make_unique<MethodCall>();
            mc->inv = std::move(base);
            mc->method = "map";
            auto blk = std::make_unique<BlockExpr>();
            auto es = std::make_unique<ExprStmt>();
            if (isKind(Tok::LBracket)) { // »[i] — index each element
                advance();
                auto ix = std::make_unique<Index>();
                ix->base = std::make_unique<VarExpr>("$_");
                ix->index = parseExpression();
                ix->isHash = false;
                expectKind(Tok::RBracket, "]");
                es->e = std::move(ix);
            }
            else { // »**N (also »² via the superscript lexer)
                advance();
                auto bin = std::make_unique<Binary>();
                bin->op = "**";
                bin->lhs = std::make_unique<VarExpr>("$_");
                bin->rhs = parsePrefix(true);
                es->e = std::move(bin);
            }
            blk->body.push_back(std::move(es));
            mc->args.push_back(std::move(blk));
            base = std::move(mc);
            continue;
        }
        // dot-hyper spelling: @a.».method / @a.>>.method — same hyper call
        if (isOp(".") && peek().kind == Tok::Op && (peek().text == "»" || peek().text == ">>") &&
            peek(2).kind == Tok::Op && peek(2).text == ".") {
            advance(); advance(); hyperNext = true; continue;
        }
        // dot-form hyper postfix: (@r).»++ — drop the dot, let the »++ branch run
        if (isOp(".") && peek().kind == Tok::Op && (peek().text == "»" || peek().text == ">>") &&
            peek(2).kind == Tok::Op &&
            (peek(2).text == "++" || peek(2).text == "--" || peek(2).text == "**")) {
            advance();
            continue;
        }
        // dot-postfix operator: $x.++ / $x.??? (and hyper .».++ / ».??? forms).
        // SYMBOLIC postfixes only — `.foo` (wordy) is always a method call.
        // The postfix must touch the dot: `$o. ++` is the obsolete P5 concat form
        // and must fail to parse (minimal-whitespace.t).
        if (isOp(".") && peek().kind == Tok::Op &&
            peek().line == cur().line && peek().col == cur().col + (int)peek().text.size() &&
            (peek().text == "++" || peek().text == "--" || userPostfix_.count(peek().text))) {
            advance(); // .
            std::string op = advance().text;
            bool userOp = op != "++" && op != "--";
            auto mkApply = [&](ExprPtr operand) -> ExprPtr {
                if (userOp) {
                    auto call = std::make_unique<Call>();
                    call->name = "postfix:<" + op + ">";
                    call->args.push_back(std::move(operand));
                    return call;
                }
                auto u = std::make_unique<Unary>();
                u->op = op; u->postfix = true;
                u->operand = std::move(operand);
                return u;
            };
            if (hyperNext) { // map the postfix over the elements
                hyperNext = false;
                auto mc = std::make_unique<MethodCall>();
                mc->inv = std::move(base);
                mc->method = "map";
                auto blk = std::make_unique<BlockExpr>();
                auto es = std::make_unique<ExprStmt>();
                es->e = mkApply(std::make_unique<VarExpr>("$_"));
                blk->body.push_back(std::move(es));
                mc->args.push_back(std::move(blk));
                base = std::move(mc);
            }
            else base = mkApply(std::move(base));
            continue;
        }
        // subscript adverb: %h{k}:exists / :delete / :!exists / :kv / :k / :v / :p
        // and the variable form %h{k}:$delete (applied when the variable is true)
        if (isOp(":") && base->kind == NK::Index &&
            (peek().kind == Tok::Ident || peek().kind == Tok::Var ||
             (peek().kind == Tok::Op && peek().text == "!"))) {
            advance(); // :
            std::string adv;
            if (isOp("!")) { advance(); adv = "!"; }
            if (isKind(Tok::Ident)) adv += advance().text;
            else if (isKind(Tok::Var)) adv += advance().text; // "$delete" — conditional
            // argument forms: `:delete(0)` / `:delete(False)` — a falsy literal
            // negates the adverb; `:exists($var)` is conditional on the variable
            if (isKind(Tok::LParen) && !cur().spaceBefore) {
                advance();
                if (isKind(Tok::Var)) { // runtime conditional: encoded as name?$var
                    adv += "?" + cur().text;
                    advance();
                } else {
                    bool falsy = (isKind(Tok::IntLit) && cur().text == "0") ||
                                 (isKind(Tok::Ident) && cur().text == "False");
                    advance();
                    if (falsy && adv[0] != '!') adv = "!" + adv;
                }
                expectKind(Tok::RParen, ")");
            }
            // `:!delete` (and a falsified `:delete(0)`) is a plain non-deleting fetch
            if (adv == "!delete") { continue; }
            auto* ix = static_cast<Index*>(base.get());
            // stacked adverbs (`:exists:kv:$delete`) accumulate ':'-joined
            ix->adverb += (ix->adverb.empty() ? "" : ":") + adv;
            continue;
        }
        if (isKind(Tok::LBracket) && !cur().spaceBefore) {
            advance();
            if (isKind(Tok::RBracket)) { // zen slice @a[] == @a (an adverbed zen keeps an Index for :exists etc.)
                advance();
                if (isOp(":=")) // a zen slice is not a bindable container
                    throw ParseError("Cannot bind to a zen Array slice", cur().line,
                                     "X::Bind::ZenSlice", {{"type", "Array"}});
                if (isOp(":") && (peek().kind == Tok::Ident || peek().kind == Tok::Var ||
                                  (peek().kind == Tok::Op && peek().text == "!"))) {
                    auto zi = std::make_unique<Index>();
                    zi->base = std::move(base);
                    zi->index = std::make_unique<WhateverExpr>();
                    zi->isHash = false;
                    base = std::move(zi);
                }
                continue;
            }
            auto idx = std::make_unique<Index>();
            idx->base = std::move(base);
            idx->index = parseExpression();
            idx->isHash = false;
            // multidim subscript @a[X;Y]: a real multislice — each dim selects at
            // its level (Whatever = all), results flattened (`@aoa[*;1]` = column 1)
            if (isKind(Tok::Semicolon)) {
                auto dims = std::make_unique<ListExpr>();
                dims->items.push_back(std::move(idx->index));
                while (matchKind(Tok::Semicolon)) {
                    if (isKind(Tok::RBracket)) break;
                    dims->items.push_back(parseExpression());
                }
                idx->index = std::move(dims);
                idx->multiDim = true;
            }
            expectKind(Tok::RBracket, "]");
            base = std::move(idx);
        } else if (isKind(Tok::LBrace) && !cur().spaceBefore) {
            advance();
            if (isKind(Tok::RBrace)) { // zen slice %h{} == %h (adverbed zen keeps an Index)
                advance();
                if (isOp(":=")) // a zen slice is not a bindable container
                    throw ParseError("Cannot bind to a zen Hash slice", cur().line,
                                     "X::Bind::ZenSlice", {{"type", "Hash"}});
                if (isOp(":") && (peek().kind == Tok::Ident || peek().kind == Tok::Var ||
                                  (peek().kind == Tok::Op && peek().text == "!"))) {
                    auto zi = std::make_unique<Index>();
                    zi->base = std::move(base);
                    zi->index = std::make_unique<WhateverExpr>();
                    zi->isHash = true;
                    base = std::move(zi);
                }
                continue;
            }
            auto idx = std::make_unique<Index>();
            idx->base = std::move(base);
            idx->index = parseExpression();
            idx->isHash = true;
            if (isKind(Tok::Semicolon)) { // `%h{a;b;c}` — ONE multidim subscript, not a chain
                auto dims = std::make_unique<ListExpr>();
                dims->items.push_back(std::move(idx->index));
                while (matchKind(Tok::Semicolon)) {
                    if (isKind(Tok::RBrace)) break;
                    dims->items.push_back(parseExpression());
                }
                idx->index = std::move(dims);
                idx->multiDim = true;
                idx->semicolonSub = true;
            }
            expectKind(Tok::RBrace, "}");
            base = std::move(idx);
        } else if (isOp("<") && !cur().spaceBefore) {
            // word-key hash subscript: %h<key>  (and $<name>/@<name>/%<name> capture sugar for $/<name>)
            // On a numeric literal (`1<2`) this can only be a mistyped comparison —
            // infix `<` requires whitespace before it (S03), so it's a parse error.
            if ((base->kind == NK::IntLit || base->kind == NK::NumLit) &&
                // `1<2` is a mistyped comparison (S03: whitespace required);
                // `5<c>` parses as a hash subscript that dies at runtime
                (peek().kind == Tok::IntLit || peek().kind == Tok::NumLit))
                error("Whitespace required before < operator");
            advance();
            std::vector<std::string> words = readAngleWords(">");
            if (words.empty()) continue; // `$x<>` / `@x<>`: zen-slice / decontainerize — the value itself
            char sigilCtx = 0;
            if (base->kind == NK::VarExpr) {
                auto* ve = static_cast<VarExpr*>(base.get());
                if (ve->name == "$") ve->name = "$/";                          // $<name> == $/<name>
                else if (ve->name == "@") { ve->name = "$/"; sigilCtx = '@'; } // @<name> == @($/<name>)
                else if (ve->name == "%") { ve->name = "$/"; sigilCtx = '%'; } // %<name> == %($/<name>)
            }
            auto idx = std::make_unique<Index>();
            idx->base = std::move(base);
            idx->isHash = true;
            if (words.size() == 1) {
                idx->index = std::make_unique<StrLit>(words[0]);
            } else {
                auto al = std::make_unique<ArrayLit>();
                for (auto& w : words) al->items.push_back(std::make_unique<StrLit>(w));
                idx->index = std::move(al);
            }
            base = std::move(idx);
            if (sigilCtx) { // @<name>/%<name>: wrap the capture in the list/hash contextualizer
                auto u = std::make_unique<Unary>();
                u->op = std::string("ctx") + sigilCtx;
                u->operand = std::move(base);
                base = std::move(u);
            }
        } else if (isOp("!") && !cur().spaceBefore && !peek().spaceBefore &&
                   (peek().kind == Tok::Ident || peek().kind == Tok::StrLit || peek().kind == Tok::StrInterp)) {
            // private method call: self!method / self!"method"() / $obj!method
            advance(); // !
            auto mc = std::make_unique<MethodCall>();
            mc->inv = std::move(base);
            mc->method = advance().text;
            mc->bang = true; // private call: only valid with a self in scope
            if (isKind(Tok::LParen)) { advance(); mc->args = parseCallArgs(); }
            else if (isOp(":") && (startsTermToken(peek()) ||
                     (peek().kind == Tok::Ident && (peek().text == "my" || peek().text == "our" || peek().text == "state")))) {
                // colon listop args on a PRIVATE call too:  self!client-setup: { … }, :$enc
                // (IO::Socket::Async::SSL) — mirrors the public `.method: args` form
                advance(); // :
                do {
                    mc->args.push_back(parseExpr(BP_COMMA + 1));
                } while (matchKind(Tok::Comma) && startsTermToken(cur()));
            }
            base = std::move(mc);
            continue;
        } else if (isOp(".")) {
            advance();
            bool mutate = false;
            if (isOp("=")) { advance(); mutate = true; } // .= mutating method call
            // .<key> postcircumfix:  $_.<key>  ==  $_<key>
            if (isOp("<") && !cur().spaceBefore) {
                advance();
                std::vector<std::string> words = readAngleWords(">");
                auto keyIndex = [&](ExprPtr b) {
                    auto idx = std::make_unique<Index>();
                    idx->base = std::move(b); idx->isHash = true;
                    if (words.size() == 1) idx->index = std::make_unique<StrLit>(words[0]);
                    else { auto al = std::make_unique<ArrayLit>(); for (auto& w : words) al->items.push_back(std::make_unique<StrLit>(w)); idx->index = std::move(al); }
                    return idx;
                };
                if (hyperNext) { // @a».<key> — index each element
                    hyperNext = false;
                    auto mc2 = std::make_unique<MethodCall>();
                    mc2->inv = std::move(base);
                    mc2->method = "map";
                    auto blk = std::make_unique<BlockExpr>();
                    auto es = std::make_unique<ExprStmt>();
                    es->e = keyIndex(std::make_unique<VarExpr>("$_"));
                    blk->body.push_back(std::move(es));
                    mc2->args.push_back(std::move(blk));
                    base = std::move(mc2);
                    continue;
                }
                base = keyIndex(std::move(base));
                continue;
            }
            // postcircumfix method syntax: .{ } .[ ] .( )
            if (isKind(Tok::LBrace)) {
                advance();
                auto idx = std::make_unique<Index>();
                idx->base = std::move(base); idx->isHash = true;
                idx->index = parseExpression();
                expectKind(Tok::RBrace, "}");
                base = std::move(idx);
                continue;
            }
            if (isKind(Tok::LBracket)) {
                advance();
                if (isKind(Tok::RBracket)) { advance(); continue; } // .[] zen slice
                auto idx = std::make_unique<Index>();
                idx->base = std::move(base); idx->isHash = false;
                idx->index = parseExpression();
                if (isKind(Tok::Semicolon)) { // .[X;Y] multislice, same as @a[X;Y]
                    auto dims = std::make_unique<ListExpr>();
                    dims->items.push_back(std::move(idx->index));
                    while (matchKind(Tok::Semicolon)) {
                        if (isKind(Tok::RBracket)) break;
                        dims->items.push_back(parseExpression());
                    }
                    idx->index = std::move(dims);
                    idx->multiDim = true;
                }
                expectKind(Tok::RBracket, "]");
                base = std::move(idx);
                continue;
            }
            if (isKind(Tok::LParen)) {
                advance();
                if (hyperNext) { // @a».() — invoke each element as a callable
                    hyperNext = false;
                    auto mc2 = std::make_unique<MethodCall>();
                    mc2->inv = std::move(base);
                    mc2->method = "map";
                    auto blk = std::make_unique<BlockExpr>();
                    auto es = std::make_unique<ExprStmt>();
                    auto ci = std::make_unique<Call>();
                    ci->callee = std::make_unique<VarExpr>("$_");
                    ci->args = parseCallArgs();
                    es->e = std::move(ci);
                    blk->body.push_back(std::move(es));
                    mc2->args.push_back(std::move(blk));
                    base = std::move(mc2);
                    continue;
                }
                auto c = std::make_unique<Call>();
                c->callee = std::move(base);
                c->args = parseCallArgs();
                base = std::move(c);
                continue;
            }
            bool maybe = false;
            if (isOp("?")) { advance(); maybe = true; }
            else if (isOp("?&") && peek().kind == Tok::Ident) {
                // `.?&elems` — the lexer fused `?&`; maybe-call of a sub-as-method
                advance(); maybe = true;
            }
            else if ((isOp("+&") || isOp("*&")) && peek().kind == Tok::Ident) {
                // `.+&elems` / `.*&elems` — fused dispatch-mode + sub-as-method
                advance(); // best-effort: treat as a plain call of the sub
            }
            bool metaCall = false;
            if (isOp("^")) { metaCall = true; advance(); } // .^meta
            else if (isOp("&") && (peek().kind == Tok::LBrace || peek().kind == Tok::Var ||
                                   peek().kind == Tok::LParen)) {
                // .&{ … } / .&$callable / .&(EXPR) — call it with the invocant as the argument
                advance(); // &
                if (hyperNext) { // >>.&{ … } maps the callable over the elements
                    auto mc2 = std::make_unique<MethodCall>();
                    mc2->inv = std::move(base);
                    mc2->method = "map";
                    mc2->args.push_back(parsePrimary());
                    base = std::move(mc2);
                    hyperNext = false;
                }
                else {
                    auto c = std::make_unique<Call>();
                    c->callee = parsePrimary();
                    c->args.push_back(std::move(base));
                    base = std::move(c);
                }
                continue;
            }
            else if (isOp("&") || isOp("*") || isOp("+")) advance(); // .&fn / .*all / .+all — best effort
            auto mc = std::make_unique<MethodCall>();
            mc->inv = std::move(base);
            mc->maybe = maybe;
            mc->meta = metaCall;
            mc->mutate = mutate;
            mc->hyper = hyperNext; hyperNext = false;
            bool indirectName = false;
            if (cur().kind == Tok::Var) {
                // `.$var` — the var holds a Callable (or a name); computed at runtime
                mc->methodExpr = std::make_unique<VarExpr>(advance().text);
            } else if (cur().kind == Tok::Ident) {
                mc->method = advance().text;
                // qualified `Any::elems` — method lookup takes the last segment
                auto q = mc->method.rfind("::");
                if (q != std::string::npos && q + 2 < mc->method.size())
                    mc->method = mc->method.substr(q + 2);
            } else if (cur().kind == Tok::StrLit) {
                mc->method = advance().text; indirectName = true;   // ."literal-name"()
            } else if (cur().kind == Tok::StrInterp) {
                mc->methodExpr = parsePrimary(); indirectName = true; // ."$name"() — computed at runtime
            } else {
                error("expected method name after '.'");
            }
            // An indirect (quoted/computed) method name must be immediately called:
            // `$x.'foo'()` is legal, bare `$x.'foo'` is not (S12).
            if (indirectName && !isKind(Tok::LParen))
                error("indirect method call requires parentheses: $obj.'name'()");
            if (isKind(Tok::LParen) && !cur().spaceBefore) { advance(); mc->args = parseCallArgs(); } // .method(args) — tight only; `.doit ()` is Confused (use unspace)
            else if (isOp(":") && (startsTermToken(peek()) ||
                     (peek().kind == Tok::Ident && (peek().text == "my" || peek().text == "our" || peek().text == "state")))) {
                // colon method-args:  @x.sort: -*.value   ==  @x.sort(-*.value)
                // also blocks / pointy blocks:  @x.map: { ... } / @x.map: -> $a { ... }
                // and declarator args:  @a.push: my \p = ...
                advance(); // :
                do {
                    mc->args.push_back(parseExpr(BP_COMMA + 1));
                } while (matchKind(Tok::Comma) && startsTermToken(cur()));
            }
            base = std::move(mc);
        } else if ((isOp("++") || isOp("--")) && !cur().spaceBefore) {
            // postfix ++/-- binds tight only — `say ++$a` is prefix ++ on the argument
            auto u = std::make_unique<Unary>();
            u->op = advance().text; u->postfix = true; u->operand = std::move(base);
            base = std::move(u);
        } else if ((cur().kind == Tok::Ident && cur().text == "i" && !cur().spaceBefore) ||
                   (isOp("\\") && !cur().spaceBefore && peek().kind == Tok::Ident &&
                    peek().text == "i" && !peek().spaceBefore)) {
            // postfix:<i> — imaginary unit: (3)i, (2i)i, and the unspace form 3\i
            if (isOp("\\")) advance();
            advance(); // i
            auto u = std::make_unique<Unary>();
            u->op = "i"; u->postfix = true; u->operand = std::move(base);
            base = std::move(u);
        } else if ((cur().kind == Tok::Op ||
                    (cur().kind == Tok::Ident && !cur().spaceBefore)) &&
                   userPostfix_.count(cur().text)) {
            // user-defined postfix operator:  5!  ==  postfix:<!>(5)
            // (a preceding private-method branch already claimed `!ident`, so we
            //  only reach here when the operator is a genuine postfix)
            std::string opname = advance().text;
            auto call = std::make_unique<Call>();
            call->name = "postfix:<" + opname + ">";
            call->args.push_back(std::move(base));
            base = std::move(call);
        } else if (isKind(Tok::LParen) && !cur().spaceBefore) {
            // invocation of a callable expression (e.g. NameTerm or coderef)
            advance();
            auto c = std::make_unique<Call>();
            if (base->kind == NK::NameTerm) c->name = static_cast<NameTerm*>(base.get())->name;
            else c->callee = std::move(base);
            c->args = parseCallArgs();
            base = std::move(c);
        } else {
            break;
        }
    }
    return base;
}

// skip `is trait` / `does Role` / `where ...` clauses up to '=' , ; or end
void Parser::skipTraits(bool onVarDecl, ExprPtr* defaultOut) {
    while (isIdent("is") || isIdent("does") || isIdent("returns") || isIdent("of")) {
        bool wasIs = isIdent("is");
        advance();
        // `is readonly` is a PARAMETER trait; on a variable declaration it's a
        // compile error (X::Comp::Trait::Unknown) — roast S03-binding/ro.t.
        if (onVarDecl && wasIs && isIdent("readonly"))
            error("Can't use unknown trait 'is readonly' in a variable declaration");
        // `is default(EXPR)` — capture the container default for the declaration
        if (wasIs && isIdent("default") && peek().kind == Tok::LParen && defaultOut) {
            advance(); advance(); // default (
            *defaultOut = parseExpression();
            expectKind(Tok::RParen, ")");
            continue;
        }
        if (isKind(Tok::Ident) || isKind(Tok::Var)) {
            static const std::set<std::string> containers = {
                "Set", "SetHash", "Bag", "BagHash", "Mix", "MixHash", "List"};
            bool wasContainer = wasIs && containers.count(cur().text);
            if (wasContainer) lastContainerIs_ = cur().text; // my %h is Set / my @a is List
            advance(); // trait name / type
            if (wasContainer && isKind(Tok::LBracket)) { // is Bag[Int] — key-type parameter
                advance();
                if (isKind(Tok::Ident)) { lastContainerOf_ = cur().text; advance(); }
                while (!isKind(Tok::RBracket) && !isKind(Tok::End)) advance();
                if (isKind(Tok::RBracket)) advance();
            }
        }
        if (isKind(Tok::LParen)) { int d = 0; do { if (isKind(Tok::LParen)) d++; else if (isKind(Tok::RParen)) d--; advance(); } while (d > 0 && !isKind(Tok::End)); }
        if (isKind(Tok::LBracket)) { int d = 0; do { if (isKind(Tok::LBracket)) d++; else if (isKind(Tok::RBracket)) d--; advance(); } while (d > 0 && !isKind(Tok::End)); }
    }
}

ExprPtr Parser::parseDeclarator(const std::string& scope) {
    if (matchOp("\\")) {
        // sigilless: my \x = ...
        std::string nm = (isKind(Tok::Ident) || isKind(Tok::Var)) ? advance().text : "";
        if (!nm.empty()) sigilless_.insert(nm);
        auto ve = std::make_unique<VarExpr>(nm);
        ve->declare = true; ve->declScope = scope;
        return ve;
    }
    // `my sub {42}()` — an anonymous sub expression under `my` is just the sub
    // term; `my $p = my package X {}` routes type declarations the same way
    if (isKind(Tok::Ident) &&
        (((cur().text == "sub" || cur().text == "method") &&
          (peek().kind == Tok::LBrace || peek().kind == Tok::LParen ||
           (peek().kind == Tok::Ident &&
            (peek(2).kind == Tok::LBrace || peek(2).kind == Tok::LParen ||
             (peek(2).kind == Tok::Op && peek(2).text.rfind("(|", 0) == 0))))) || // `my method bar (|) {…}` — "(|)" lexes as the set-op
         ((cur().text == "class" || cur().text == "role" || cur().text == "grammar" ||
           cur().text == "package" || cur().text == "module") &&
          (peek().kind == Tok::LBrace ||
           (peek().kind == Tok::Ident && peek(2).kind == Tok::LBrace)))))
        return parsePrefix();
    // type-capture declaration:  my ::T $x  (binds T to the type of $x; we just parse it)
    if (isOp("::") && peek().kind == Tok::Ident) { advance(); advance(); }
    std::string type, coerceTo;
    if (isKind(Tok::Ident)) {
        bool looksType = peek().kind == Tok::Var || peek().kind == Tok::LParen ||
                         peek().kind == Tok::LBracket ||
                         (peek().kind == Tok::Ident && peek().text == "of") || // `my Array of Int @box`
                         (peek().kind == Tok::Op &&
                          (peek().text == ":" || peek().text == "\\" ||
                           (peek().text.size() == 1 && std::strchr("$@%&", peek().text[0])))); // typed anon `my Int % = …`
        if (looksType) {
            type = advance().text;
            if (isOp(":") && peek().kind == Tok::Ident) { advance(); advance(); } // :D / :U / :_ smiley
            if (isKind(Tok::LBracket)) { int d = 0; do { if (isKind(Tok::LBracket)) d++; else if (isKind(Tok::RBracket)) d--; advance(); } while (d > 0 && !isKind(Tok::End)); }
            // `my Array of Int @box` — of-chained element type before the variable
            while (isIdent("of") && peek().kind == Tok::Ident) { advance(); type = advance().text; }
            // coercion type `Int(Str)`: assigned values are coerced to `type`
            if (isKind(Tok::LParen) && peek().kind == Tok::Ident && peek(2).kind == Tok::RParen) {
                advance(); advance(); advance(); // ( SourceType )
                coerceTo = type;
            }
        }
    }
    // sigilless after an optional type:  my Mu \x = …   (bare `my \x` handled above)
    if (matchOp("\\")) {
        std::string nm = (isKind(Tok::Ident) || isKind(Tok::Var)) ? advance().text : "";
        if (!nm.empty()) sigilless_.insert(nm);
        auto ve = std::make_unique<VarExpr>(nm);
        ve->declare = true; ve->declScope = scope; ve->declType = type;
        return ve;
    }
    if (isKind(Tok::LParen)) {
        advance();
        auto list = std::make_unique<ListExpr>();
        while (!isKind(Tok::RParen) && !isKind(Tok::End)) {
            if (isKind(Tok::LParen)) { // nested destructure:  my (\a, (\b, \c)) = …
                list->items.push_back(parseDeclarator(scope));
                if (!matchKind(Tok::Comma)) break;
                continue;
            }
            if (matchOp("\\")) { // sigilless item:  my (\x, $y) = …
                std::string nm = (isKind(Tok::Ident) || isKind(Tok::Var)) ? advance().text : "";
                if (!nm.empty()) sigilless_.insert(nm);
                auto ve = std::make_unique<VarExpr>(nm);
                ve->declare = true; ve->declScope = scope;
                if (isIdent("where")) { advance(); parseExpr(BP_COMMA + 1); } // constraint parsed, not enforced
                list->items.push_back(std::move(ve));
                if (!matchKind(Tok::Comma)) break;
                continue;
            }
            if (isOp("*") && peek().kind == Tok::Var &&
                (peek().text == "@" || peek().text == "%")) {
                // anonymous slurpy `my ($one, *@) = 1..4` — swallow the rest
                advance(); advance();
                auto ve = std::make_unique<VarExpr>("");
                ve->declare = true; ve->declScope = scope;
                list->items.push_back(std::move(ve));
                if (!matchKind(Tok::Comma)) break;
                continue;
            }
            std::string t2;
            if (isKind(Tok::Ident) &&
                (peek().kind == Tok::Var ||
                 peek().kind == Tok::LBracket || // parameterized: Array[UInt] $x
                 (peek().kind == Tok::Op && peek().text == ":" && peek(2).kind == Tok::Ident &&
                  peek(3).kind == Tok::Var))) {
                t2 = advance().text;
                if (isKind(Tok::LBracket)) { // skip the [T] parameter group
                    int d = 0;
                    do { if (isKind(Tok::LBracket)) d++; else if (isKind(Tok::RBracket)) d--; advance(); }
                    while (d > 0 && !isKind(Tok::End));
                }
                if (isOp(":") && peek().kind == Tok::Ident) { advance(); advance(); } // :D/:U smiley
            }
            // a literal element in a destructuring declaration (`my ($a, "foo")`)
            // binds nothing — an anonymous slot stands in
            if (isKind(Tok::StrLit) || isKind(Tok::StrInterp) || isKind(Tok::IntLit) || isKind(Tok::NumLit)) {
                advance();
                auto anon = std::make_unique<VarExpr>("$!anon");
                anon->declare = true; anon->declScope = scope;
                list->items.push_back(std::move(anon));
                if (!matchKind(Tok::Comma)) break;
                continue;
            }
            if (!isKind(Tok::Var)) error("expected variable in declaration");
            auto ve = std::make_unique<VarExpr>(advance().text);
            ve->declare = true; ve->declScope = scope; ve->declType = t2;
            if (isIdent("where")) { advance(); parseExpr(BP_COMMA + 1); } // constraint parsed, not yet enforced here
            if (isOp("=") ) { // per-item initializer: `my Int:D ($x = 5)`
                advance();
                auto as = std::make_unique<Assign>();
                as->op = "=";
                as->target = std::move(ve);
                as->value = parseExpr(BP_COMMA + 1);
                list->items.push_back(std::move(as));
            }
            else list->items.push_back(std::move(ve));
            if (!matchKind(Tok::Comma)) break;
        }
        expectKind(Tok::RParen, ")");
        return list;
    }
    if (isKind(Tok::Var)) {
        {   // `my $0` — numeric names are reserved for captures; `my $!x`/`my $?X`
            // — those twigils cannot take a `my`-style scope
            const std::string& vn = cur().text;
            if (vn.size() > 1 && std::isdigit((unsigned char)vn[1]))
                throw ParseError("Cannot declare a numeric variable " + vn, cur().line,
                                 "X::Syntax::Variable::Numeric", {});
            if (vn.size() > 2 && (vn[1] == '!' || vn[1] == '?') &&
                (scope == "my" || scope == "our" || scope == "state" || scope == "constant"))
                throw ParseError("Cannot use twigil '" + std::string(1, vn[1]) +
                                 "' on a '" + scope + "'-scoped variable", cur().line,
                                 "X::Syntax::Variable::Twigil",
                                 {{"twigil", std::string(1, vn[1])}, {"scope", scope}});
            // `my $<a>` — match variables cannot be declared
            if (vn.size() == 1 && peek().kind == Tok::Op && peek().text == "<" &&
                !peek().spaceBefore)
                throw ParseError("Cannot declare a match variable", cur().line,
                                 "X::Syntax::Variable::Match", {});
        }
        auto ve = std::make_unique<VarExpr>(advance().text);
        ve->declare = true; ve->declScope = scope; ve->declType = type; ve->declCoerce = coerceTo;
        // shaped array `my @a[3]` / `my @a[2;2]`: the `[...]` right after the sigil
        // (no space) is a dimension list, not a subscript. Semicolons separate dims.
        if (ve->name[0] == '@' && isKind(Tok::LBracket) && !cur().spaceBefore) {
            advance(); // '['
            auto dims = std::make_unique<ListExpr>();
            dims->semicolon = true;
            while (!isKind(Tok::RBracket) && !isKind(Tok::End)) {
                dims->items.push_back(parseExpr(BP_COMMA + 1));
                if (isKind(Tok::Semicolon) || isKind(Tok::Comma)) { advance(); continue; } // `[3;3]` or `[3,3]`
                break;
            }
            expectKind(Tok::RBracket, "]");
            ve->declShape = dims->items.size() == 1 ? std::move(dims->items[0]) : std::move(dims);
        }
        // `%a{Str}` — hash key-type shape declaration
        std::string keyType;
        if (isKind(Tok::LBrace) && peek().kind == Tok::Ident && peek(2).kind == Tok::RBrace) {
            advance(); keyType = advance().text; advance();
        }
        // `of Type` postfix trait sets the value/element type
        if (isIdent("of") && peek().kind == Tok::Ident) { advance(); ve->declType = advance().text; }
        // Hash[valueType,keyType] — an object hash with no explicit value type
        // is Hash[Mu, KeyType]: its missing-key default is Mu, not Any
        if (!keyType.empty()) ve->declType = (ve->declType.empty() ? "Mu" : ve->declType) + "," + keyType;
        lastContainerIs_.clear(); lastContainerOf_.clear();
        skipTraits(scope != "has", &ve->declDefault);
        if (!lastContainerIs_.empty()) { ve->containerIs = lastContainerIs_; lastContainerIs_.clear(); }
        if (!lastContainerOf_.empty()) { ve->containerOf = lastContainerOf_; lastContainerOf_.clear(); }
        // `my $a is default(42) where * == 42` — constraint parsed, not yet enforced
        if (isIdent("where")) { advance(); parseExpr(BP_ASSIGN + 1); }
        return ve;
    }
    if (scope == "constant" && isKind(Tok::Ident)) {
        auto ve = std::make_unique<VarExpr>(advance().text);
        ve->declare = true; ve->declScope = scope;
        skipTraits();
        // `constant foo;` — a constant must be initialized
        if (isKind(Tok::Semicolon) || isKind(Tok::End))
            throw ParseError("Missing initializer on constant declaration",
                             cur().line, "X::Syntax::Missing",
                             {{"what", "initializer on constant declaration"}});
        return ve;
    }
    // bare sigil = anonymous variable: `my $`, `my @`, `my %`, `my Int % = …`
    if ((isKind(Tok::Op) && cur().text.size() == 1 && strchr("$@%&", cur().text[0])) ||
        (isKind(Tok::Var) && cur().text.size() == 1)) {
        std::string sig = advance().text;
        auto ve = std::make_unique<VarExpr>(sig + "!anon");
        ve->declare = true; ve->declScope = scope; ve->declType = type;
        // anonymous object hash `my %{Str:D} is default(…)` — the braced key type
        if (sig == "%" && isKind(Tok::LBrace) && peek().kind == Tok::Ident) {
            advance();
            std::string keyType = advance().text;
            if (isOp(":") && peek().kind == Tok::Ident) { advance(); advance(); } // :D/:U smiley
            matchKind(Tok::RBrace);
            ve->declType = (ve->declType.empty() ? "Mu" : ve->declType) + "," + keyType;
        }
        skipTraits(scope != "has", &ve->declDefault);
        return ve;
    }
    error("expected variable after declarator");
}

// Pseudo-packages (S02): scope-qualified symbol access. rakupp approximates them
// with its ordinary scope-chain lookup — `$MY::x`/`MY::<$x>` become plain `$x`,
// `$PROCESS::IN`/`PROCESS::<$IN>` become the dynamic `$*IN`, `CORE::<&not>` becomes
// `&not` (builtins resolve through the normal &-lookup). CALLER/OUTER/DYNAMIC are
// best-effort: dynvars already travel the dynamic chain, lexicals use the current
// scope (exact caller/outer frame semantics are not modelled).
static bool isPseudoPkg(const std::string& p) {
    static const std::set<std::string> ps = {
        "MY", "OUR", "CORE", "GLOBAL", "PROCESS", "COMPILING", "DYNAMIC",
        "CALLER", "CALLERS", "OUTER", "OUTERS", "LEXICAL", "UNIT", "SETTING",
        "PARENT", "CLIENT",
    };
    return ps.count(p) > 0;
}
// "$MY::x" → "$x"; "$PROCESS::IN" → "$*IN"; unqualified / non-pseudo names unchanged.
static std::string stripPseudoPkg(const std::string& name) {
    if (name.size() < 4 || !std::strchr("$@%&", name[0])) return name;
    size_t twi = 1; // an optional twigil between sigil and the qualifier: `$*DYNAMIC::x`
    if (twi < name.size() && std::strchr("*!?.^:", name[twi])) twi++;
    size_t sep = name.find("::", twi);
    if (sep == std::string::npos) return name;
    std::string pkg = name.substr(twi, sep - twi);
    if (!isPseudoPkg(pkg)) return name;
    std::string rest = name.substr(sep + 2);
    if (rest.empty()) return name;
    std::string sig(1, name[0]);
    // PROCESS holds the process-wide dynamics: `$PROCESS::IN` is `$*IN`.
    // GLOBAL holds `our` symbols — a plain strip finds them via the scope chain.
    if (pkg == "PROCESS" && !std::strchr("*!?.^", rest[0])) sig += "*";
    return sig + rest;
}
// A pseudo-package angle access `MY::<$x>` / `CORE::<&not>` — symbol via scope chain.
static std::string pseudoAngleSymbol(const std::string& pkg, const std::string& sym) {
    if (sym.empty()) return "$_";
    std::string s = sym;
    if (pkg == "PROCESS" && s.size() > 1 &&
        std::strchr("$@%&", s[0]) && !std::strchr("*!?.^", s[1])) s = s.substr(0, 1) + "*" + s.substr(1);
    return s;
}

static ExprPtr angleWordNumeric(const std::string& w); // defined below

ExprPtr Parser::parseColonPair() {
    // ':' already current
    advance();
    // object-hash literal `:{ :42a, ... }` — parse the brace content as a hash
    // composer (keys typed Any; representation-wise a plain Hash for now)
    if (isKind(Tok::LBrace) && !cur().spaceBefore) {
        advance();
        auto u = std::make_unique<Unary>();
        u->op = "ctx%";
        u->operand = isKind(Tok::RBrace) ? ExprPtr(std::make_unique<ListExpr>())
                                         : parseExpression();
        expectKind(Tok::RBrace, "}");
        return u;
    }
    bool negate = false;
    if (isOp("!")) { advance(); negate = true; }
    auto pair = std::make_unique<PairExpr>();
    pair->colonForm = true;
    // numeric adverb shorthand: :3c  ==  c => 3   (also :2.5x)
    if ((isKind(Tok::IntLit) || isKind(Tok::NumLit)) && peek().kind == Tok::Ident) {
        ExprPtr num = parsePrimary();
        pair->key = advance().text;
        pair->value = std::move(num);
        return pair;
    }
    if (isKind(Tok::Var)) {
        // :$x -> x => $x ; a twigil is dropped from the KEY (`:$^radius` is
        // `radius => $^radius`, `:$*foo` is `foo => $*foo`) but kept in the value.
        std::string vn = cur().text;
        advance();
        std::string key = vn.size() > 1 ? vn.substr(1) : vn;
        if (!key.empty() && std::strchr("^.!*?:=~", key[0])) key = key.substr(1);
        pair->key = key;
        pair->value = std::make_unique<VarExpr>(vn);
        return pair;
    }
    // radix literal: :16<FF> / :2<1010> / :8<777>  (a number written in the given base)
    if (isKind(Tok::IntLit) && peek().kind == Tok::Op && peek().text == "<" && !peek().spaceBefore) {
        int base = std::atoi(cur().text.c_str());
        if (base < 2 || base > 36)
            throw ParseError("Radix " + std::to_string(base) +
                             " out of range (allowed: 2..36)", cur().line,
                             "X::Syntax::Number::RadixOutOfRange",
                             {{"radix", std::to_string(base)}});
        advance(); advance(); // radix and '<'
        std::vector<std::string> words = readAngleWords(">");
        std::string digits = words.empty() ? "" : words[0];
        // exponent form `:16<dead_beef*16**8>` / `:2<1.1*10**10>`: the part after
        // `*` is a base**exp multiplier, applied as a runtime multiplication so
        // BigInt-range results work
        std::string expPart;
        size_t star = digits.find('*');
        if (star != std::string::npos) { expPart = digits.substr(star + 1); digits = digits.substr(0, star); }
        long long val = 0, frac = 0, fdiv = 1; bool infrac = false;
        // Iterate codepoints: accept ASCII alnum, fullwidth ASCII letters (folded),
        // and Nd digits (their 0-9 value). A single `.` starts the fractional part
        // (`:16<FF.F>` → a Rat). Nl/No numerals or non-radix scripts are malformed.
        for (size_t bi = 0; bi < digits.size(); ) {
            unsigned char b0 = digits[bi];
            int len = b0 < 0x80 ? 1 : b0 >= 0xF0 ? 4 : b0 >= 0xE0 ? 3 : 2;
            uint32_t cp = b0 < 0x80 ? b0 : (b0 & (0xFF >> (len + 1)));
            for (int k = 1; k < len && bi + k < digits.size(); k++) cp = (cp << 6) | ((unsigned char)digits[bi + k] & 0x3F);
            bi += len;
            if (cp == '_') continue;
            if (cp == '.' && !infrac) { infrac = true; continue; }
            long long d = -1;
            char fw = (cp >= 0xFF21 && cp <= 0xFF3A) ? (char)('A' + cp - 0xFF21)
                    : (cp >= 0xFF41 && cp <= 0xFF5A) ? (char)('a' + cp - 0xFF41) : 0;
            char c = fw ? fw : (cp < 0x80 ? (char)cp : 0);
            if (c) d = (c >= '0' && c <= '9') ? c - '0'
                     : (c >= 'a' && c <= 'z') ? c - 'a' + 10
                     : (c >= 'A' && c <= 'Z') ? c - 'A' + 10 : -1;
            else { // non-ASCII: only Nd digits are allowed
                long long nn, dd;
                if (uniGeneralCategory(cp) == "Nd" && uniNumValue(cp, nn, dd) && dd == 1) d = nn;
            }
            if (d < 0 || d >= base) error("Malformed radix number");
            if (infrac) { frac = frac * base + d; fdiv *= base; }
            else val = val * base + d;
        }
        ExprPtr numNode;
        if (infrac) { // fractional radix → a Rat: (val*fdiv + frac) / fdiv
            auto nl = std::make_unique<NumLit>((double)(val * fdiv + frac) / (double)fdiv);
            nl->isRat = true; nl->ratNum = val * fdiv + frac; nl->ratDen = fdiv;
            numNode = std::move(nl);
        }
        else numNode = std::make_unique<IntLit>(val);
        size_t ss = expPart.find("**");
        if (ss != std::string::npos) {
            long long eb = std::atoll(expPart.substr(0, ss).c_str());
            long long ee = std::atoll(expPart.substr(ss + 2).c_str());
            auto pw = std::make_unique<Binary>(); pw->op = "**";
            pw->lhs = std::make_unique<IntLit>(eb); pw->rhs = std::make_unique<IntLit>(ee);
            auto mul = std::make_unique<Binary>(); mul->op = "*";
            mul->lhs = std::move(numNode); mul->rhs = std::move(pw);
            return mul;
        }
        return numNode;
    }
    // radix conversion of a runtime value: :16("2e") / :2($bits) — the argument's
    // string form parsed in the given base (an Int).
    if (isKind(Tok::IntLit) && peek().kind == Tok::LParen && !peek().spaceBefore) {
        int base = std::atoi(cur().text.c_str());
        if (base < 2 || base > 36)
            throw ParseError("Radix " + std::to_string(base) +
                             " out of range (allowed: 2..36)", cur().line,
                             "X::Syntax::Number::RadixOutOfRange",
                             {{"radix", std::to_string(base)}});
        advance(); advance(); // radix and '('
        ExprPtr arg = isKind(Tok::RParen) ? std::make_unique<StrLit>("") : parseExpression();
        expectKind(Tok::RParen, ")");
        auto c = std::make_unique<Call>();
        c->name = "__radix";
        c->args.push_back(std::make_unique<IntLit>(base));
        c->args.push_back(std::move(arg));
        return c;
    }
    if (isKind(Tok::Ident) || isKind(Tok::IntLit)) {
        pair->key = cur().text;
        advance();
        if (negate) {
            if (isKind(Tok::LParen) && !cur().spaceBefore)
                throw ParseError("Argument not allowed on negated pair with key '" +
                                 pair->key + "'", cur().line,
                                 "X::Syntax::NegatedPair", {{"key", pair->key}});
            pair->value = std::make_unique<BoolLit>(false);
            return pair;
        }
        if (isKind(Tok::LParen) && !cur().spaceBefore) {
            advance();
            if (isKind(Tok::RParen)) { pair->value = std::make_unique<ListExpr>(); advance(); return pair; }
            ExprPtr v = parseExpression();
            // `:shape(2;2)` — a semicolon-list value (multidim shape/index)
            if (isKind(Tok::Semicolon)) {
                auto lst = std::make_unique<ListExpr>();
                lst->parenned = true; lst->semicolon = true;
                lst->items.push_back(std::move(v));
                while (matchKind(Tok::Semicolon)) { if (isKind(Tok::RParen)) break; lst->items.push_back(parseExpression()); }
                v = std::move(lst);
            }
            pair->value = std::move(v);
            expectKind(Tok::RParen, ")");
            return pair;
        }
        if (isOp("<") && !cur().spaceBefore) {
            advance();
            std::vector<std::string> words = readAngleWords(">");
            // a single numeric word is an allomorph, same as the term form:
            // :chmod<0o777> passes IntStr 511, not the string "0o777"
            auto mkWord = [](const std::string& w) -> ExprPtr {
                if (ExprPtr num = angleWordNumeric(w)) {
                    if (w.find('/') != std::string::npos || num->kind == NK::Binary)
                        return num;
                    auto al = std::make_unique<AllomorphLit>();
                    al->num = std::move(num); al->str = w;
                    return al;
                }
                return std::make_unique<StrLit>(w);
            };
            if (words.size() == 1) pair->value = mkWord(words[0]);
            else { auto al = std::make_unique<ArrayLit>(); for (auto& w : words) al->items.push_back(mkWord(w)); pair->value = std::move(al); }
            return pair;
        }
        if (isKind(Tok::LBracket) && !cur().spaceBefore) {
            pair->value = parsePrimary(); // parses [ ... ] as ArrayLit
            return pair;
        }
        if (isKind(Tok::LBrace) && !cur().spaceBefore) {
            pair->value = parsePrimary(); // :name{ … } — a Block (or Hash) value
            return pair;
        }
        pair->value = std::make_unique<BoolLit>(true);
        return pair;
    }
    // bare ':' fallback
    pair->key = "";
    pair->value = std::make_unique<BoolLit>(!negate);
    return pair;
}

// A single angle word that is a numeric literal is an allomorph in Raku
// (<42> is IntStr, <1/3> RatStr, <1e5> NumStr); we produce the numeric part.
// Returns nullptr when the word isn't (recognizably) numeric — stays a word.
static ExprPtr angleWordNumeric(const std::string& w) {
    // Complex allomorph <1+3i> / <3i> / <-2.5-1e3i> (the .raku form round-trips via EVAL)
    if (!w.empty() && w.back() == 'i') {
        const char* s = w.c_str();
        char* end = nullptr;
        double re = std::strtod(s, &end);
        if (end != s && end < s + w.size() - 1 && (*end == '+' || *end == '-')) {
            char* end2 = nullptr;
            double im = std::strtod(end, &end2);
            if (end2 == s + w.size() - 1) {
                auto nl = std::make_unique<NumLit>(re);
                auto ni = std::make_unique<NumLit>(im); ni->imaginary = true;
                auto b = std::make_unique<Binary>(); b->op = "+";
                b->lhs = std::move(nl); b->rhs = std::move(ni);
                return b;
            }
        } else if (end == s + w.size() - 1 && end != s) { // pure imaginary <3i>
            auto ni = std::make_unique<NumLit>(re); ni->imaginary = true;
            return ni;
        }
    }
    // radix literal <0o777> / <0x1F> / <0b1010> / <0d42> — IntStr in Rakudo
    {
        size_t j = 0; bool rneg = false;
        if (j < w.size() && (w[j] == '+' || w[j] == '-')) { rneg = w[j] == '-'; j++; }
        if (j + 2 < w.size() && w[j] == '0') {
            int base = w[j+1] == 'x' ? 16 : w[j+1] == 'o' ? 8 : w[j+1] == 'b' ? 2 : w[j+1] == 'd' ? 10 : 0;
            if (base) {
                long long v = 0; bool ok = true;
                for (size_t k = j + 2; k < w.size(); k++) {
                    char c = w[k];
                    if (c == '_') continue; // digit separator
                    int dv = c >= '0' && c <= '9' ? c - '0'
                           : c >= 'a' && c <= 'z' ? c - 'a' + 10
                           : c >= 'A' && c <= 'Z' ? c - 'A' + 10 : -1;
                    if (dv < 0 || dv >= base) { ok = false; break; }
                    v = v * base + dv;
                }
                if (ok) return std::make_unique<IntLit>(rneg ? -v : v);
            }
        }
    }
    size_t i = 0; bool neg = false;
    if (i < w.size() && (w[i] == '+' || w[i] == '-')) { neg = w[i] == '-'; i++; }
    size_t d0 = i;
    while (i < w.size() && std::isdigit((unsigned char)w[i])) i++;
    std::string intPart = w.substr(d0, i - d0);
    if (intPart.empty() && !(i < w.size() && w[i] == '.')) return nullptr; // allow <.5>
    auto toLL = [](const std::string& ds, long long& out) -> bool {
        if (ds.size() > 18) return false;
        out = 0; for (char c : ds) out = out * 10 + (c - '0');
        return true;
    };
    // build a Rat literal, falling back to big-decimal strings past long long
    auto mkRat = [&](const std::string& numDigits, const std::string& denDigits, double approx) -> ExprPtr {
        auto nl = std::make_unique<NumLit>(approx);
        nl->isRat = true;
        long long num, den;
        if (toLL(numDigits, num) && toLL(denDigits, den)) { nl->ratNum = neg ? -num : num; nl->ratDen = den; }
        else { nl->bigNum = (neg ? "-" : "") + numDigits; nl->bigDen = denDigits; }
        return nl;
    };
    if (!intPart.empty() && i == w.size()) { // pure integer
        long long v;
        auto il = std::make_unique<IntLit>(0);
        if (toLL(intPart, v)) il->v = neg ? -v : v;
        else il->big = (neg ? "-" : "") + intPart;
        return il;
    }
    if (!intPart.empty() && w[i] == '/') { // fraction  <1/3>
        size_t j = ++i;
        while (i < w.size() && std::isdigit((unsigned char)w[i])) i++;
        if (i == j || i != w.size()) return nullptr;
        std::string denPart = w.substr(j, i - j);
        return mkRat(intPart, denPart,
                     (neg ? -1.0 : 1.0) * std::strtod(intPart.c_str(), nullptr) /
                     std::max(1.0, std::strtod(denPart.c_str(), nullptr)));
    }
    if (i < w.size() && w[i] == '.') { // decimal  <4.25> / <.5>
        size_t j = ++i;
        while (i < w.size() && std::isdigit((unsigned char)w[i])) i++;
        if (i == j) return nullptr;
        if (i == w.size()) {
            std::string frac = w.substr(j, i - j);
            return mkRat(intPart + frac, "1" + std::string(frac.size(), '0'),
                         (neg ? -1.0 : 1.0) * std::strtod((intPart + "." + frac).c_str(), nullptr));
        }
    }
    if (i < w.size() && (w[i] == 'e' || w[i] == 'E')) { // e-notation  <1e5> <4.5e-2>
        errno = 0; char* end = nullptr;
        double v = std::strtod(w.c_str(), &end);
        if (end == w.c_str() + w.size() && errno != ERANGE)
            return std::make_unique<NumLit>(v);
    }
    return nullptr;
}

ExprPtr Parser::parsePrimary() {
    // `&[+]` / `&[~=]` — the infix operator as a Callable term (same value as
    // `&infix:<+>`); the op may span several tokens (`**`, `~^`, `max`)
    if (isOp("&") && peek().kind == Tok::LBracket && !peek().spaceBefore) {
        advance(); advance(); // & [
        std::string op;
        while (!isKind(Tok::End) && !isKind(Tok::RBracket)) {
            // `&[«+»]`: the lexer made «+» a qw-list — restore its markers
            if (isKind(Tok::QwList)) op += "\xC2\xAB" + advance().text + "\xC2\xBB";
            else op += advance().text;
        }
        expectKind(Tok::RBracket, "]");
        return std::make_unique<VarExpr>("&infix:<" + hyperMarkersToUni(op) + ">");
    }
    // user circumfix operator: `⟦ … ⟧`  ==  circumfix:<⟦ ⟧>( … )
    if ((cur().kind == Tok::Op || cur().kind == Tok::Ident) && userCircumfix_.count(cur().text)) {
        std::string open = advance().text, close = userCircumfix_[open];
        auto call = std::make_unique<Call>();
        call->name = "circumfix:<" + open + " " + close + ">";
        if (cur().text != close) call->args.push_back(parseExpression());
        if (cur().text == close) advance(); else error("expected circumfix closing '" + close + "'");
        return call;
    }
    // ::?CLASS / ::?ROLE / ::?PACKAGE — the lexically-enclosing type (compile-time)
    if (isOp("::") && peek().text == "?" && peek(2).kind == Tok::Ident) {
        advance(); advance(); // :: ?
        advance();            // CLASS / ROLE / PACKAGE
        std::string nm = typeStack_.empty() ? "" : typeStack_.back();
        return std::make_unique<NameTerm>(nm.empty() ? "Mu" : nm);
    }
    // root symbol access: `::<$x>` — the symbol looked up through the scope chain
    if (isOp("::") && peek().kind == Tok::Op && peek().text == "<" && !peek().spaceBefore) {
        advance(); advance(); // :: <
        std::vector<std::string> words = readAngleWords(">");
        return std::make_unique<VarExpr>(words.empty() ? "$_" : words[0]);
    }
    // trailing path segments after a symbolic ref: `::($a)::name`, `::($a)::('$x')`
    auto parseSymSegs = [&](SymbolicRef* sr) {
        while (isOp("::")) {
            if (peek().kind == Tok::LParen) {
                advance(); advance(); // :: (
                sr->segs.push_back(cur().kind == Tok::RParen ? ExprPtr(std::make_unique<StrLit>(""))
                                                             : parseExpression());
                expectKind(Tok::RParen, "expected ')' in ::() path segment");
            }
            else if (peek().kind == Tok::Ident) {
                advance();
                sr->segs.push_back(std::make_unique<StrLit>(advance().text));
            }
            else break;
        }
    };
    // signature literal `:($a, $b?)` — a first-class Signature term. Exotic
    // forms our signature parser can't express yet (`:(\SELF: …)`) fall back
    // to an opaque empty Signature so the surrounding file still parses.
    // `:(|)` — the lexer fuses `(|)` into the set-union op token; here it is a
    // signature literal holding one anonymous capture parameter
    if (isOp(":") && peek().kind == Tok::Op && peek().text == "(|)" && !peek().spaceBefore) {
        advance(); advance(); // : (|)
        auto be = std::make_unique<BlockExpr>();
        Param cp; cp.sigil = '|'; cp.slurpy = true;
        be->params.push_back(std::move(cp));
        auto u = std::make_unique<Unary>();
        u->op = "siglit";
        u->operand = std::move(be);
        return u;
    }
    if (isOp(":") && peek().kind == Tok::LParen && !peek().spaceBefore) {
        size_t save = pos_;
        advance(); advance(); // : (
        try {
            auto be = std::make_unique<BlockExpr>();
            be->params = parseSignature();
            expectKind(Tok::RParen, ")");
            auto u = std::make_unique<Unary>();
            u->op = "siglit";
            u->operand = std::move(be);
            return u;
        } catch (ParseError&) {
            pos_ = save;
            advance(); advance(); // : (
            int d = 1;
            while (d > 0 && !isKind(Tok::End)) {
                if (isKind(Tok::LParen)) d++;
                else if (isKind(Tok::RParen)) d--;
                advance();
            }
            auto u = std::make_unique<Unary>();
            u->op = "siglit";
            u->operand = std::make_unique<BlockExpr>();
            return u;
        }
    }
    // bare `::` term — the current-scope stash: `::.keys`, `::{'$bear'}`
    if (isOp("::") && ((peek().kind == Tok::Op && peek().text == ".") || peek().kind == Tok::LBrace)) {
        advance();
        auto c = std::make_unique<Call>();
        c->name = "__stash__";
        return c;
    }
    // symbolic reference: `::($name)` — look up a symbol at runtime by string name
    if (isOp("::") && peek().kind == Tok::LParen) {
        advance(); advance(); // :: (
        auto sr = std::make_unique<SymbolicRef>();
        if (cur().kind != Tok::RParen) sr->nameExpr = parseExpression();
        expectKind(Tok::RParen, "expected ')' after ::(");
        parseSymSegs(sr.get());
        return sr;
    }
    // sigil-prefixed symbolic deref: `$::($name)` / `@::(…)` / `%::(…)` / `&::(…)`
    // → look up the variable named SIGIL ~ $name at runtime. A longer Var head
    // is a literal package qualifier: `$Terrain::($m)` / `$Foo::Bar::($x)::tail`.
    if (cur().kind == Tok::Var && !cur().text.empty() &&
        std::strchr("$@%&", cur().text[0]) &&
        peek().kind == Tok::Op && peek().text == "::" && peek(2).kind == Tok::LParen) {
        auto sr = std::make_unique<SymbolicRef>();
        std::string head = advance().text;
        sr->sigil = head.substr(0, 1);
        sr->pkg = head.substr(1); // "" for the bare-sigil form
        while (!sr->pkg.empty() && sr->pkg.back() == ':') sr->pkg.pop_back();
        advance(); advance();       // :: (
        if (cur().kind != Tok::RParen) sr->nameExpr = parseExpression();
        expectKind(Tok::RParen, "expected ')' after sigil::(");
        parseSymSegs(sr.get());
        return sr;
    }
    // symbolic name reference in term position: `::Foo::Bar` → the named type/package
    if (isOp("::") && peek().kind == Tok::Ident) {
        advance(); // ::
        std::string name = advance().text;
        while (isOp("::") && peek().kind == Tok::Ident) { advance(); name += "::" + advance().text; }
        // via SymbolicRef so an unknown name is X::NoSuchSymbol (`::a`), while
        // known types/classes resolve exactly as the bare NameTerm would
        auto sr = std::make_unique<SymbolicRef>();
        sr->nameExpr = std::make_unique<StrLit>(name);
        return sr;
    }
    const Token& t = cur();
    switch (t.kind) {
        case Tok::FatArrow:
            // `1, => 2` — an infix pair-arrow with no key is a term-position infix
            throw ParseError("Preceding context expects a term, but found infix => instead",
                             cur().line, "X::Syntax::InfixInTermPosition", {{"infix", "=>"}});
        case Tok::IntLit: {
            const Token& tk = advance();
            std::string bare; for (char c : tk.text) if (c != '_') bare += c; // spelling keeps separators; value drops them
            auto e = std::make_unique<IntLit>(tk.ival);
            e->raw = bare;
            if (bare.size() > 18 && bare.find_first_not_of("0123456789") == std::string::npos) {
                try { (void)std::stoll(bare); } catch (...) { e->big = bare; }
            }
            return e;
        }
        case Tok::NumLit: {
            bool imag = !t.text.empty() && t.text.back() == 'i';
            bool isRat = t.flag && !imag; // decimal literal with no exponent -> Rat
            std::string txt; for (char c : t.text) if (c != '_') txt += c; // value drops separators
            std::string den2 = t.text2; long long numeralNum = t.ival;
            auto e = std::make_unique<NumLit>(advance().nval);
            e->raw = txt;
            e->imaginary = imag;
            if (isRat && !den2.empty()) {
                // a Unicode vulgar-fraction numeral (½): num in ival, den in text2
                e->isRat = true;
                e->ratNum = numeralNum;
                e->ratDen = std::strtoll(den2.c_str(), nullptr, 10);
            } else if (isRat && txt.find('/') != std::string::npos) {
                // explicit numerator/denominator
                size_t sl = txt.find('/');
                e->isRat = true;
                e->ratNum = std::strtoll(txt.substr(0, sl).c_str(), nullptr, 10);
                e->ratDen = std::strtoll(txt.substr(sl + 1).c_str(), nullptr, 10);
            } else if (isRat) {
                // "15.8972" -> numerator 158972, denominator 10^(fractional digits)
                size_t dot = txt.find('.');
                std::string digits = txt.substr(0, dot) + txt.substr(dot + 1);
                long long fracLen = (long long)(txt.size() - dot - 1);
                e->isRat = true;
                if (digits.size() <= 18) {
                    long long den = 1; for (long long k = 0; k < fracLen; k++) den *= 10;
                    e->ratNum = std::strtoll(digits.c_str(), nullptr, 10);
                    e->ratDen = den;
                } else { // exact big Rat: 4.99…(45 digits) must not pick up f.p. noise
                    e->bigNum = digits;
                    e->bigDen = "1" + std::string((size_t)fracLen, '0');
                }
            }
            return e;
        }
        case Tok::StrLit: {
            bool fmt = cur().flag; bool qx = cur().text2 == "qx";
            auto e = std::make_unique<StrLit>(advance().text);
            if (qx) { auto c = std::make_unique<Call>(); c->name = "__qx__"; c->args.push_back(std::move(e)); return c; }
            if (fmt) { auto c = std::make_unique<Call>(); c->name = "__format__"; c->args.push_back(std::move(e)); return c; }
            return e;
        }
        case Tok::VersionLit: { // v1.2.3 — sugar for Version.new("1.2.3")
            auto mc = std::make_unique<MethodCall>();
            mc->inv = std::make_unique<NameTerm>("Version");
            mc->method = "new";
            mc->args.push_back(std::make_unique<StrLit>(advance().text));
            return mc;
        }
        case Tok::StrInterp: {
            bool fmt = cur().flag; bool qx = cur().text2 == "qx";
            std::string raw = advance().text; auto e = parseInterpString(raw);
            if (qx) { auto c = std::make_unique<Call>(); c->name = "__qx__"; c->args.push_back(std::move(e)); return c; }
            if (fmt) { auto c = std::make_unique<Call>(); c->name = "__format__"; c->args.push_back(std::move(e)); return c; }
            return e;
        }
        case Tok::RegexLit: {
            Token tk = advance();
            // the lexer prepends the adverbs (":P5 ", ":i ") to the pattern
            // text — strip them for the null check, and note a P5 regex
            // (where a trailing | or & is a literal, not an empty branch)
            const std::string& full = tk.text;
            size_t i = 0;
            bool p5 = false;
            while (i < full.size() && full[i] == ':') {
                size_t j = i + 1;
                int d = 0;
                while (j < full.size() && (d > 0 || full[j] != ' ')) {
                    if (full[j] == '(') d++;
                    else if (full[j] == ')') d--;
                    j++;
                }
                // lexer-prepended adverbs always end with a space; a tight
                // inline adverb (`/:s^.../`) is part of the pattern — stop
                if (j >= full.size() || full[j] != ' ') break;
                std::string adv = full.substr(i + 1, j - i - 1);
                if (adv == "P5" || adv == "Perl5") p5 = true;
                i = j;
                while (i < full.size() && full[i] == ' ') i++;
            }
            checkNullRegex(full.substr(i), tk.line, !p5);
            auto e = std::make_unique<RegexLit>(tk.text);
            e->isRx = tk.flag;
            return e;
        }
        case Tok::SubstLit: {
            const Token& t = advance();
            checkNullRegex(t.text, t.line, /*branches=*/false);
            return std::make_unique<SubstLit>(t.text, t.text2, t.flag);
        }
        case Tok::QwList: { // qw<...> : split raw content on whitespace into a list of strings
            std::string raw = advance().text;
            auto arr = std::make_unique<ArrayLit>();
            arr->isList = true;
            size_t i = 0, n = raw.size();
            while (i < n) {
                for (int w; i < n && (w = uniWsLen(raw, i, true)); ) i += w;
                size_t start = i;
                while (i < n && !uniWsLen(raw, i, true)) i++;
                if (i > start) {
                    std::string word = raw.substr(start, i - start);
                    // a numeric word is an allomorph (<42> IntStr, <1/3> RatStr, …) —
                    // in a multi-word list too: <1 2 3>[0].WHAT is IntStr
                    if (ExprPtr num = angleWordNumeric(word)) {
                        auto al = std::make_unique<AllomorphLit>();
                        al->num = std::move(num); al->str = word;
                        arr->items.push_back(std::move(al));
                    } else
                        arr->items.push_back(std::make_unique<StrLit>(word));
                }
            }
            // a single `<word>` is that element itself, not a one-item list
            if (arr->items.size() == 1) return std::move(arr->items[0]);
            return arr;
        }
        case Tok::Var: {
            if (cur().text.rfind("&?ROUTINE", 0) == 0 && routineDepth_ == 0)
                throw ParseError("&?ROUTINE is only available inside a routine (X::Undeclared::Symbols)", cur().line);
            // sigil contextualizer glued to a variable: `@$x` == @($x), `%$h` == %($h)
            if (cur().text.size() == 1 &&
                (cur().text[0] == '@' || cur().text[0] == '%' || cur().text[0] == '$') &&
                peek().kind == Tok::Var && !peek().spaceBefore && peek().text.size() > 1) {
                char sig = advance().text[0];
                auto u = std::make_unique<Unary>();
                u->op = sig == '@' ? "ctx@" : sig == '%' ? "ctx%" : "ctx$";
                u->operand = std::make_unique<VarExpr>(advance().text);
                return u;
            }
            // `$.^name` / `$.?meth` etc.: a bare `$` glued to a `.` postfix is the
            // `$.` self-shortcut (self followed by a method/meta call). `$.foo` is a
            // single twigil token handled elsewhere; this only fires when the char
            // after the dot isn't an identifier (so the lexer split off a lone `$`).
            if (cur().text == "$" && peek().kind == Tok::Op && peek().text == "." && !peek().spaceBefore) {
                int ln = cur().line; advance(); auto s = std::make_unique<SelfTerm>(); s->line = ln; return s;
            }
            // `${ ... }` — itemized hash literal (the lexer split off a lone `$`;
            // without this, the braces would parse as a hash-index on `$`).
            if (cur().text == "$" && peek().kind == Tok::LBrace && !peek().spaceBefore) {
                // Perl-5 `${a}` (a single bareword inside) is obsolete Raku
                if (peek(2).kind == Tok::Ident && peek(3).kind == Tok::RBrace)
                    throw ParseError("Unsupported use of ${" + peek(2).text + "}; in Raku please use :key or $()",
                                     cur().line, "X::Obsolete", {{"old", "${" + peek(2).text + "}"}});
                advance();
                auto u = std::make_unique<Unary>();
                u->op = "ctx$"; u->operand = parsePrimary();
                return u;
            }
            // bare `$` as a TERM is an anonymous STATE variable — each textual
            // occurrence is its own persistent slot (`say ++$ ~ ". " ~ $_`
            // numbers lines; two `$`s in one expression are independent).
            // NOT when a glued `<` follows: `$<name>` is the $/<name> capture
            // (the postfix pass rewrites the bare-$ VarExpr to $/)
            if (cur().text == "$" &&
                !(peek().kind == Tok::Op && peek().text == "<" && !peek().spaceBefore)) {
                int ln = cur().line; advance();
                auto e = std::make_unique<VarExpr>("$anon--state--" + std::to_string(anonStateN_++));
                e->declare = true; e->declScope = "state";
                e->line = ln;
                return e;
            }
            int ln = cur().line; auto e = std::make_unique<VarExpr>(stripPseudoPkg(advance().text)); e->line = ln; return e;
        }
        case Tok::LParen: {
            advance();
            if (isKind(Tok::RParen)) {
                advance();
                auto empty = std::make_unique<ListExpr>();
                empty->parenned = true; // `(), a, b` is a 3-element list — the comma chain must not extend ()
                return empty;
            }
            bool svCond = stmtCond_; stmtCond_ = false; // parens re-allow block listop args
            struct CondRestore { bool* f; bool v; ~CondRestore() { *f = v; } } cr{&stmtCond_, svCond};
            // `(LABEL: for … { })` / `(for LIST { })` — a loop STATEMENT in term
            // position collects each iteration's value (labels stay attached, so
            // `next LABEL` works from nested loops)
            {
                bool loopKw = (isIdent("for") || isIdent("loop") || isIdent("while") ||
                               isIdent("until") || isIdent("repeat")) &&
                              peek().kind != Tok::FatArrow && peek().kind != Tok::Comma;
                bool labeled = isKind(Tok::Ident) && peek().kind == Tok::Op && peek().text == ":" &&
                               !peek().spaceBefore && peek(2).kind == Tok::Ident &&
                               (peek(2).text == "for" || peek(2).text == "while" ||
                                peek(2).text == "until" || peek(2).text == "loop" || peek(2).text == "repeat");
                if (loopKw || labeled) {
                    auto st = parseStatement();
                    if (st) {
                        if (st->kind == NK::ForStmt) static_cast<ForStmt*>(st.get())->asExpr = true;
                        else if (st->kind == NK::WhileStmt) static_cast<WhileStmt*>(st.get())->asExpr = true;
                        else if (st->kind == NK::LoopStmt) static_cast<LoopStmt*>(st.get())->asExpr = true;
                    }
                    auto be = std::make_unique<BlockExpr>();
                    be->body.push_back(std::move(st));
                    auto u = std::make_unique<Unary>(); u->op = "do"; u->operand = std::move(be);
                    ExprPtr e = std::move(u);
                    expectKind(Tok::RParen, ")");
                    return e;
                }
            }
            // `(if COND { } elsif ... else { })` — an if STATEMENT in term position
            // evaluates to the chosen block's value (likewise unless)
            if ((isIdent("if") || isIdent("unless")) &&
                peek().kind != Tok::FatArrow && peek().kind != Tok::Comma) {
                bool isUnless = cur().text == "unless";
                advance();
                auto st = parseIf(isUnless);
                auto be = std::make_unique<BlockExpr>();
                be->body.push_back(std::move(st));
                auto u = std::make_unique<Unary>(); u->op = "do"; u->operand = std::move(be);
                ExprPtr e = std::move(u);
                expectKind(Tok::RParen, ")");
                return e;
            }
            ExprPtr e = parseExpression();
            // statement modifiers inside parens CHAIN: `($_ * $_ if $_ %% 2 for
            // ^10)` is a list comprehension — each modifier wraps the value so far
            for (;;) {
            // statement modifier inside parens:  (42 if $x)  (42 unless $x)  (42 with $y)
            if (isIdent("if") || isIdent("unless") || isIdent("with") || isIdent("without")) {
                std::string mod = advance().text;
                ExprPtr cond = parseExpression();
                if (mod == "with" || mod == "without") {
                    // `(EXPR with X)` also BINDS $_ to X inside EXPR (it's a topicalizer,
                    // not just a definedness test): (S:th(1,3)/./Z/ with 'abcd') works
                    // on 'abcd'. Desugar to  do { with X { EXPR } }.
                    auto gs = std::make_unique<GivenStmt>();
                    gs->topic = std::move(cond);
                    gs->defGuard = (mod == "with") ? 1 : 2;
                    auto es = std::make_unique<ExprStmt>(); es->e = std::move(e);
                    gs->body = std::make_unique<Block>();
                    gs->body->stmts.push_back(std::move(es));
                    auto be = std::make_unique<BlockExpr>();
                    be->body.push_back(std::move(gs));
                    auto u = std::make_unique<Unary>(); u->op = "do"; u->operand = std::move(be);
                    e = std::move(u);
                } else {
                    bool neg = (mod == "unless");
                    auto tern = std::make_unique<Ternary>();
                    tern->cond = std::move(cond);
                    ExprPtr empty = std::make_unique<NameTerm>("Empty"); // vanishes in list context
                    if (neg) { tern->then = std::move(empty); tern->els = std::move(e); }
                    else { tern->then = std::move(e); tern->els = std::move(empty); }
                    e = std::move(tern);
                }
                continue;
            }
            if (isIdent("for")) {
                // (EXPR for LIST) — a for statement-modifier inside parens; run EXPR
                // per item ($_ bound). Desugar to map({ EXPR }, LIST).
                advance();
                ExprPtr list = parseExpression();
                auto blk = std::make_unique<BlockExpr>();
                auto es = std::make_unique<ExprStmt>(); es->e = std::move(e);
                blk->body.push_back(std::move(es));
                auto call = std::make_unique<Call>(); call->name = "map";
                call->args.push_back(std::move(blk));
                call->args.push_back(std::move(list));
                e = std::move(call);
                continue;
            }
            // (EXPR while COND) / (EXPR until COND) — collect the value per iteration
            if (isIdent("while") || isIdent("until")) {
                bool untl = cur().text == "until";
                advance();
                auto ws = std::make_unique<WhileStmt>();
                ws->cond = parseExpression();
                ws->isUntil = untl;
                ws->asExpr = true;
                ws->body = std::make_unique<Block>();
                auto es = std::make_unique<ExprStmt>(); es->e = std::move(e);
                ws->body->stmts.push_back(std::move(es));
                auto be = std::make_unique<BlockExpr>();
                be->body.push_back(std::move(ws));
                auto u = std::make_unique<Unary>(); u->op = "do"; u->operand = std::move(be);
                e = std::move(u);
                continue;
            }
            // (EXPR given TOPIC) — evaluate EXPR with $_ bound to TOPIC
            if (isIdent("given") && peek().kind != Tok::FatArrow && peek().kind != Tok::Comma) {
                advance();
                auto gs = std::make_unique<GivenStmt>();
                gs->topic = parseExpression();
                auto es = std::make_unique<ExprStmt>(); es->e = std::move(e);
                gs->body = std::make_unique<Block>();
                gs->body->stmts.push_back(std::move(es));
                auto be = std::make_unique<BlockExpr>();
                be->body.push_back(std::move(gs));
                auto u = std::make_unique<Unary>(); u->op = "do"; u->operand = std::move(be);
                e = std::move(u);
                continue;
            }
            break;
            }
            // `( a; b; … )` — a SEMICOLON LIST: each `;`-separated segment is
            // evaluated (its comma-list value) and the results collected into a
            // list. `(2;2)` is `(2, 2)`; `(1,2; 3,4)` is `((1,2),(3,4))`; used for
            // multidim subscripts and `:shape(2;2)`. Side effects still run in
            // order, so `( say $_; last )` behaves as before (last exits first).
            if (isKind(Tok::Semicolon)) {
                auto lst = std::make_unique<ListExpr>();
                lst->parenned = true;
                lst->semicolon = true; // a semicolon-list: segments don't flatten as a plain comma list
                lst->items.push_back(std::move(e));
                while (matchKind(Tok::Semicolon)) {
                    if (isKind(Tok::RParen)) break; // trailing ;
                    lst->items.push_back(parseExpression());
                }
                expectKind(Tok::RParen, ")");
                return lst;
            }
            expectKind(Tok::RParen, ")");
            if (e && e->kind == NK::ListExpr) static_cast<ListExpr*>(e.get())->parenned = true;
            return e;
        }
        case Tok::LBracket: {
            // reduction metaoperator: [+] [*] [~] [max] [<] [Z] [X] ...
            // A named-operator reduce is either an infix symbol, a lowercase word op
            // (max/min/gcd/…), or the meta bases Z/X. A capitalized type name — `[Any]`,
            // `[Int]`, `[Foo]` — is NOT a reduce; it's a one-element array literal.
            bool identReduce = peek(1).kind == Tok::Ident && !peek(1).text.empty() &&
                (peek(1).text == "Z" || peek(1).text == "X" || std::islower((unsigned char)peek(1).text[0]) ||
                 // Z/X/R fused with a word op lex as one ident: [Zand] [Xor] [Rmax]
                 ((peek(1).text[0] == 'Z' || peek(1).text[0] == 'X' || peek(1).text[0] == 'R') &&
                  peek(1).text.size() > 1 &&
                  std::all_of(peek(1).text.begin() + 1, peek(1).text.end(),
                              [](unsigned char c){ return std::islower(c); })));
            if ((peek(1).kind == Tok::FatArrow || peek(1).kind == Tok::Comma) &&
                peek(2).kind == Tok::RBracket) {
                advance(); // [
                std::string innerOp = cur().kind == Tok::FatArrow ? "=>" : ",";
                advance(); advance(); // op ]
                auto u = std::make_unique<Unary>();
                u->op = "[" + innerOp + "]"; // [=>] pair-consing (right-assoc) / [,] list
                u->operand = parseExpr(BP_ZIP);  // reduce is a list-prefix: looser than Z/X and comma
                return u;
            }
            if (((peek(1).kind == Tok::Op && peek(1).text != "\\") || identReduce) &&
                peek(1).text != "]") {
                // the op may span several TIGHT tokens ([!=:=] lexes as `!=` + `:=`)
                int j = 2;
                while (peek(j).kind == Tok::Op && !peek(j).spaceBefore && peek(j).text != "]") j++;
                if (peek(j).kind == Tok::RBracket) {
                    advance(); // [
                    std::string innerOp;
                    for (int k = 1; k < j; k++) innerOp += advance().text;
                    advance(); // ]
                    auto u = std::make_unique<Unary>();
                    u->op = "[" + innerOp + "]";
                    // A reduction metaop is a list-prefix: its argument needs whitespace
                    // (`[+] @a`) or the parenthesised function form (`[+](@a)`). A term
                    // glued directly to `]` — `[+]@a` — is Confused.
                    if (!cur().spaceBefore && !isKind(Tok::LParen) && !isKind(Tok::Comma) &&
                        !isKind(Tok::RParen) && !isKind(Tok::RBracket) && !isKind(Tok::Semicolon) &&
                        !isKind(Tok::End) && startsTermToken(cur()))
                        throw ParseError("Confused (whitespace required before a reduction metaop's argument) (X::Syntax::Confused)", cur().line);
                    // listop-style forms: `([op], 42)` (comma before the args) and
                    // the zero-argument `([op])`
                    if (isKind(Tok::Comma)) advance();
                    if (isKind(Tok::RParen) || isKind(Tok::Semicolon) || isKind(Tok::End)) {
                        auto empty = std::make_unique<ListExpr>();
                        u->operand = std::move(empty);
                    }
                    else if (isKind(Tok::LParen) && !cur().spaceBefore) {
                        // function-call form `[+](@a)`: the parens bound the args, so
                        // parse a FULL expression up to `)` — this includes looser
                        // list-infix metaops like `Z*`/`X~` (`[+](@a Z* @b)`). A
                        // comma AFTER the `)` belongs to the enclosing list.
                        advance();
                        if (isKind(Tok::RParen)) { advance(); u->operand = std::make_unique<ListExpr>(); }
                        else {
                            u->operand = parseExpression();
                            expectKind(Tok::RParen, ")");
                        }
                    }
                    else u->operand = parseExpr(BP_ZIP);  // reduce is a list-prefix: looser than Z/X and comma
                    return u;
                }
            }
            // triangular / scan reduce: [\+] [\~] [\*] — yields the list of running
            // partial reductions (1, 1+2, 1+2+3, …) rather than the final value.
            if (peek(1).kind == Tok::Op && peek(1).text == "\\" &&
                peek(2).kind == Tok::Comma && peek(3).kind == Tok::RBracket) {
                advance(); advance(); advance(); advance(); // [ \ , ]
                auto u = std::make_unique<Unary>();
                u->op = "[\\,]";
                u->operand = parseExpr(BP_ZIP);  // reduce is a list-prefix: looser than Z/X and comma
                return u;
            }
            if (peek(1).kind == Tok::Op && peek(1).text == "\\" &&
                (peek(2).kind == Tok::Op || (peek(2).kind == Tok::Ident && !peek(2).text.empty() &&
                    (std::isupper((unsigned char)peek(2).text[0]) ? // Z/X(+word) meta bases
                         (peek(2).text[0] == 'Z' || peek(2).text[0] == 'X' || peek(2).text[0] == 'R')
                       : std::islower((unsigned char)peek(2).text[0])))) &&
                !peek(2).spaceBefore && peek(2).text != "]") {
                // multi-token base ops too: [\X~] lexes as `\` `X` `~`
                int j = 3;
                while (peek(j).kind == Tok::Op && !peek(j).spaceBefore && peek(j).text != "]") j++;
                if (peek(j).kind == Tok::RBracket) {
                    advance(); // [
                    advance(); // '\'
                    std::string innerOp;
                    for (int k = 2; k < j; k++) innerOp += advance().text;
                    advance(); // ]
                    auto u = std::make_unique<Unary>();
                    u->op = "[\\" + innerOp + "]";
                    u->operand = parseExpr(BP_ZIP);  // reduce is a list-prefix: looser than Z/X and comma
                    return u;
                }
            }
            // reverse-metaop reduce: [R-] [R~] [R/] — `R` tight against the base op
            if (peek(1).kind == Tok::Ident && peek(1).text == "R" &&
                (peek(2).kind == Tok::Op || peek(2).kind == Tok::Ident) &&
                !peek(2).spaceBefore && peek(3).kind == Tok::RBracket) {
                advance(); // [
                std::string innerOp = advance().text; innerOp += advance().text; // R + base op
                advance(); // ]
                auto u = std::make_unique<Unary>();
                u->op = "[" + innerOp + "]";
                u->operand = parseExpr(BP_ZIP);  // reduce is a list-prefix: looser than Z/X and comma
                return u;
            }
            advance();
            auto arr = std::make_unique<ArrayLit>();
            if (!isKind(Tok::RBracket)) {
                ExprPtr e = parseExpression();
                if (e->kind == NK::ListExpr) {
                    auto* l = static_cast<ListExpr*>(e.get());
                    for (auto& it : l->items) arr->items.push_back(std::move(it));
                    // `[<a b>,]` / `[x, y]`: comma members are items — a List member
                    // stays one element ([<a b>] without the comma still flattens).
                    arr->fromCommaList = true;
                } else arr->items.push_back(std::move(e));
            }
            expectKind(Tok::RBracket, "]");
            return arr;
        }
        case Tok::LBrace: {
            // Hash-literal vs block disambiguation (Raku rule): a {...} is a Hash
            // constructor when it's empty, or starts with a Pair (key => / :key),
            // or starts with a %-var followed by , or }.
            bool isHash = false;
            const Token& a = peek(1), &b = peek(2);
            if (a.kind == Tok::RBrace) isHash = true; // {}
            else if ((a.kind == Tok::Ident || a.kind == Tok::StrLit ||
                      a.kind == Tok::StrInterp || a.kind == Tok::IntLit) &&
                     b.kind == Tok::FatArrow)
                isHash = true;
            else if (a.kind == Tok::Op && a.text == ":" &&
                     (b.kind == Tok::Ident || b.kind == Tok::IntLit || b.kind == Tok::Var ||
                      (b.kind == Tok::Op && b.text == "!")) &&
                     // `:16(...)` / `:16<...>` is a RADIX literal, not a colon-pair —
                     // so `{ :16($_) }` is a CODE block, not a hash
                     !(b.kind == Tok::IntLit &&
                       (peek(3).kind == Tok::LParen ||
                        (peek(3).kind == Tok::Op && peek(3).text == "<"))))
                isHash = true; // starts with a colon-pair: :name / :1n / :$v / :!flag
            else if (a.kind == Tok::Var && !a.text.empty() && a.text[0] == '%' &&
                     (b.kind == Tok::RBrace || b.kind == Tok::Comma))
                isHash = true;
            if (isHash) {
                advance(); // {
                auto h = std::make_unique<HashLit>();
                if (!isKind(Tok::RBrace)) {
                    ExprPtr e = parseExpression();
                    if (e->kind == NK::ListExpr) {
                        auto* l = static_cast<ListExpr*>(e.get());
                        for (auto& it : l->items) h->items.push_back(std::move(it));
                    } else h->items.push_back(std::move(e));
                }
                expectKind(Tok::RBrace, "}");
                return h;
            }
            // anonymous block / closure
            auto blk = parseBlock();
            auto be = std::make_unique<BlockExpr>();
            be->body = std::move(blk->stmts);
            return be;
        }
        case Tok::Op: {
            if (t.text == "..." || t.text == "!!!" || t.text == "???") { // stub / yada operators
                auto c = std::make_unique<Call>(); c->name = advance().text; return c;
            }
            if (t.text == ":") return parseColonPair();
            // sink-assignment to an anonymous container: `@ = (…)` / `$ = …`
            if ((t.text == "@" || t.text == "%" || t.text == "$") &&
                peek().kind == Tok::Op && peek().text == "=" && peek().spaceBefore) {
                advance();
                auto ve = std::make_unique<VarExpr>(t.text + "!anon");
                ve->declare = true; ve->declScope = "my";
                return ve;
            }
            if (t.text == "*") { advance(); return std::make_unique<WhateverExpr>(); }
            if (t.text == "**") { advance(); auto w = std::make_unique<WhateverExpr>(); w->hyper = true; return w; } // HyperWhatever (e.g. %h{**})
            if (t.text == "||") { // slip-subscript `@a[|| @dims]` / `%h{|| @keys}`: navigate by a list of indices/keys
                advance(); auto u = std::make_unique<Unary>(); u->op = "dimslip"; u->operand = parseExpr(BP_COMMA + 1); return u;
            }
            if (t.text == "\xE2\x88\x9E") { advance(); auto inf = std::make_unique<NumLit>(std::numeric_limits<double>::infinity()); inf->raw = "\xE2\x88\x9E"; return inf; } // ∞
            if (t.text == ".") return std::make_unique<VarExpr>("$_"); // .method => $_.method
            if (t.text == "\\") { // capture: \(…) builds a Capture (assoc-indexable); bare \x itemizes
                advance();
                if (isKind(Tok::LParen) && !cur().spaceBefore) {
                    auto u = std::make_unique<Unary>();
                    u->op = "capture"; u->operand = parsePrefix(true);
                    return u; // caller's parsePostfix attaches any postfixes
                }
                return parsePrefix();
            }
            if (t.text == "<") {
                // qw word list  < a b c > — a numeric word is an allomorph
                // (<42> IntStr, <1/3> RatStr, <1e5> NumStr), in a multi-word list too
                advance();
                auto words = readAngleWords(">");
                auto mkWord = [](const std::string& w) -> ExprPtr {
                    if (ExprPtr num = angleWordNumeric(w)) {
                        // only a single numeric TOKEN is an allomorph (42, 1.5, 1e5,
                        // 3i, -2). `<1/3>` is a plain Rat and `<1+2i>` a plain Complex
                        // (they carry an infix), so their .raku round-trips exactly.
                        if (w.find('/') != std::string::npos || num->kind == NK::Binary)
                            return num;
                        auto al = std::make_unique<AllomorphLit>();
                        al->num = std::move(num); al->str = w;
                        return al;
                    }
                    return std::make_unique<StrLit>(w);
                };
                if (words.size() == 1) return mkWord(words[0]); // <42> is the value itself, not a list
                auto arr = std::make_unique<ArrayLit>();
                for (auto& w : words) arr->items.push_back(mkWord(w));
                arr->isList = true;
                return arr;
            }
            if (t.text == "\xC2\xAB") {
                // guillemet qw word list  « a b c »  (interpolating qw, treated as plain here)
                advance();
                auto arr = std::make_unique<ArrayLit>();
                for (auto& w : readAngleWords("\xC2\xBB")) arr->items.push_back(std::make_unique<StrLit>(w));
                arr->isList = true;
                return arr;
            }
            if (t.text == "<<") {
                // ASCII guillemets: `<<0 +4 'a b'>>` — a qww word list in term position
                advance();
                auto arr = std::make_unique<ArrayLit>();
                for (auto& w : readAngleWords(">>")) {
                    // strip one level of quotes around a word ('a b' / "a b")
                    std::string s = w;
                    if (s.size() >= 2 && (s.front() == '\'' || s.front() == '"') && s.back() == s.front())
                        s = s.substr(1, s.size() - 2);
                    arr->items.push_back(std::make_unique<StrLit>(s));
                }
                arr->isList = true;
                return arr;
            }
            if (t.text == "->" || t.text == "<->") {
                advance();
                auto be = std::make_unique<BlockExpr>();
                be->params = parsePointyParams();
                auto blk = parseBlock();
                be->body = std::move(blk->stmts);
                return be;
            }
            // `&[+]` / `&[!~~]` / `&[max]` — the infix operator as a value == `&infix:<OP>`.
            if (t.text == "&" && peek().kind == Tok::LBracket) {
                advance(); advance(); // & [
                std::string op;
                while (!isKind(Tok::RBracket) && !isKind(Tok::End)) {
                    // `&[«+»]`: the lexer made «+» a qw-list — restore its markers
                    if (isKind(Tok::QwList)) op += "\xC2\xAB" + advance().text + "\xC2\xBB";
                    else op += advance().text;
                }
                matchKind(Tok::RBracket);
                return std::make_unique<VarExpr>("&infix:<" + hyperMarkersToUni(op) + ">");
            }
            error("unexpected operator in term position");
        }
        case Tok::Ident: {
            std::string name = t.text;
            if (name == "True") { advance(); return std::make_unique<BoolLit>(true); }
            if (name == "False") { advance(); return std::make_unique<BoolLit>(false); }
            if (name == "self" && peek().kind != Tok::FatArrow) {
                advance(); // `self => v` stays an autoquoted pair key
                return std::make_unique<SelfTerm>();
            }
            if (name == "undef" && peek().kind != Tok::FatArrow)
                throw ParseError("Unsupported use of undef as a value; in Raku "
                                 "please use something more specific: an undefined "
                                 "type object such as Any, or Nil as the absence "
                                 "of a value",
                                 cur().line, "X::Obsolete",
                                 {{"old", "undef as a value"},
                                  {"replacement", "something more specific"}});
            // mathematical constants are TERMS, never listops: `e + 1`, `pi + 0`,
            // `Inf+100` (else `+100` is misread as a listop argument to `Inf`).
            // A tight `pi()` is left as a call so it dies as an undeclared routine.
            if ((name == "pi" || name == "tau" || name == "e" || name == "\xCF\x80" || name == "\xCF\x84"
                 || name == "Inf" || name == "NaN")
                && !(peek().kind == Tok::LParen && !peek().spaceBefore))
                { advance(); return std::make_unique<NameTerm>(name); }
            // the bare type objects Any/Mu/Cool are TERMS too (`Any ~ $x` concats;
            // `~` must not be misread as a listop argument's prefix) — a tight
            // `Any(...)` stays a coercion call, and a tight `:` (the `Any:U`
            // smiley) keeps the general type-name path
            if ((name == "Any" || name == "Mu" || name == "Cool")
                && !(peek().kind == Tok::LParen && !peek().spaceBefore)
                && !(peek().kind == Tok::Op && !peek().text.empty() && peek().text[0] == ':' && !peek().spaceBefore))
                { advance(); return std::make_unique<NameTerm>(name); }
            // control-flow in expression position: `... or return X`, `... or last`
            // — but `next => 42` / `last => 1` is a fat-arrow Pair, the key autoquotes
            if ((name == "return" || name == "return-rw" || name == "last" || name == "next" || name == "redo") &&
                peek().kind != Tok::FatArrow) {
                advance();
                auto u = std::make_unique<Unary>();
                u->op = name;
                bool retTerm = startsTermToken(cur()) && !kBlockKeywords.count(cur().text);
                // sub/method/do/start blocks are valid expression operands too, as is
                // an anonymous type literal (`return role {…}`)
                if (isIdent("sub") || isIdent("method") || isIdent("do") || isIdent("start")) retTerm = true;
                if ((isIdent("role") || isIdent("class") || isIdent("grammar")) &&
                    (peek().kind == Tok::LBrace || (peek().kind == Tok::Op && peek().text == "::"))) retTerm = true;
                if ((name == "return" || name == "return-rw") && retTerm &&
                    !isKind(Tok::RParen) && !isKind(Tok::Semicolon) && !isKind(Tok::RBrace))
                    u->operand = parseExpr(BP_ASSIGN);
                return u;
            }
            if (name == "INIT" && peek().kind != Tok::LBrace &&
                peek().kind != Tok::Semicolon && peek().kind != Tok::End) { // INIT <expr> — a value phaser
                advance(); // consume INIT
                return parseExpr(BP_ASSIGN);
            }
            if (name == "sub" || name == "method") {
                advance();
                auto be = std::make_unique<BlockExpr>();
                be->isSub = true; // `sub {…}` as a term is a Sub, not a bare Block
                if (isKind(Tok::Ident)) advance(); // optional name (anon use)
                if (isKind(Tok::LParen)) { advance(); be->params = parseSignature(); expectKind(Tok::RParen, ")"); }
                while (!isKind(Tok::LBrace) && !isKind(Tok::End) && !isKind(Tok::Semicolon)) advance();
                if (isKind(Tok::LBrace)) {
                    routineDepth_++; // &?ROUTINE is legal inside an anon sub too
                    auto blk = parseBlock();
                    routineDepth_--;
                    be->body = std::move(blk->stmts);
                }
                return be;
            }
            // anonymous type as an expression term: `$x does role {…}`, `my $r = role {…}`,
            // the explicit form `class :: does R {…}`, and a NAMED inline class
            // in value position (`class Foo {}.new` as a list element)
            if ((name == "role" || name == "class" || name == "grammar" ||
                 name == "package" || name == "module") &&
                (peek().kind == Tok::LBrace ||
                 (peek().kind == Tok::Op && peek().text == "::") ||
                 (peek().kind == Tok::Ident && peek(2).kind == Tok::LBrace))) {
                advance(); // consume the keyword (parseClass expects it already consumed)
                auto decl = parseClass(name == "role", name == "grammar",
                                       name == "package" || name == "module",
                                       false, name);
                auto be = std::make_unique<BlockExpr>();
                be->body.push_back(std::move(decl));
                auto u = std::make_unique<Unary>(); u->op = "do"; u->operand = std::move(be);
                return u; // `do { <anon type decl> }` — evals to the type object
            }
            // phaser in expression position: `my $x = BEGIN { … }` — run the block, yield its value
            if ((name == "BEGIN" || name == "INIT" || name == "CHECK" || name == "END" ||
                 name == "ENTER" || name == "LEAVE" || name == "FIRST") && peek().kind == Tok::LBrace) {
                advance();
                auto u = std::make_unique<Unary>();
                u->op = "do";
                auto blk = parseBlock();
                auto be = std::make_unique<BlockExpr>();
                be->body = std::move(blk->stmts);
                u->operand = std::move(be);
                return u;
            }
            if (name == "react" || name == "supply") {
                advance();
                auto call = std::make_unique<Call>(); call->name = name;
                if (isKind(Tok::LBrace)) {
                    bool saved = inReactBlock_; inReactBlock_ = true;
                    auto blk = parseBlock();
                    inReactBlock_ = saved;
                    auto be = std::make_unique<BlockExpr>(); be->body = std::move(blk->stmts);
                    call->args.push_back(std::move(be));
                } else {
                    // statement form: `react whenever $c { … }` — the body is a single statement.
                    bool saved = inReactBlock_; inReactBlock_ = true;
                    auto inner = parseExpr(BP_COMMA + 1);
                    inReactBlock_ = saved;
                    auto be = std::make_unique<BlockExpr>();
                    auto es = std::make_unique<ExprStmt>(); es->e = std::move(inner);
                    be->body.push_back(std::move(es));
                    call->args.push_back(std::move(be));
                }
                return call;
            }
            if (name == "whenever") {
                if (!inReactBlock_) error("whenever outside the lexical scope of a react/supply block");
                advance();
                auto call = std::make_unique<Call>(); call->name = "whenever";
                call->args.push_back(parseExpr(BP_COMMA + 1)); // the supply/promise expression
                auto be = std::make_unique<BlockExpr>();
                if (isOp("->") || isOp("<->")) { advance(); be->params = parsePointyParams(); } // whenever $s -> $v { }
                if (isKind(Tok::LBrace)) { auto blk = parseBlock(); be->body = std::move(blk->stmts); }
                call->args.push_back(std::move(be));
                return call;
            }
            if (name == "do" || name == "try" || name == "gather" || name == "quietly" ||
                name == "once" ||
                name == "BEGIN" || name == "ENTER") {
                advance();
                auto u = std::make_unique<Unary>();
                // BEGIN/ENTER in value position evaluate their block/expr and yield it
                // (a tree-walker has no separate compile phase, so `do` semantics suffice).
                u->op = (name == "BEGIN" || name == "ENTER") ? "do" : name;
                if (isKind(Tok::LBrace)) {
                    auto blk = parseBlock();
                    auto be = std::make_unique<BlockExpr>();
                    be->body = std::move(blk->stmts);
                    u->operand = std::move(be);
                } else if (isIdent("for") || isIdent("if") || isIdent("unless") || isIdent("while") ||
                           isIdent("until") || isIdent("loop") || isIdent("repeat") || isIdent("given") ||
                           isIdent("when") || isIdent("with") || isIdent("without")) {
                    // `gather for … {}` / `do if … {}` — the operand is a whole statement
                    auto be = std::make_unique<BlockExpr>();
                    auto st = parseStatement();
                    markLoopAsExpr(st.get()); // `do for …` collects the loop's values
                    be->body.push_back(std::move(st));
                    u->operand = std::move(be);
                } else {
                    // statement prefixes take a whole expression incl. assignment:
                    // `try target = values` is try(target = values), not (try target) = values
                    u->operand = parseExpr(BP_ASSIGN);
                }
                return u;
            }
            if (name == "for" || name == "while" || name == "until" ||
                name == "loop" || name == "repeat") {
                // a loop in term/value position: `(for … {…})».Str`, `my @x = while …`.
                // parseStatement consumes the loop keyword; wrap it as a `do` so it
                // evaluates to the collected List of per-iteration values.
                auto u = std::make_unique<Unary>();
                u->op = "do";
                auto be = std::make_unique<BlockExpr>();
                auto st = parseStatement();
                markLoopAsExpr(st.get());
                be->body.push_back(std::move(st));
                u->operand = std::move(be);
                return u;
            }
            if (name == "start" && peek().kind != Tok::FatArrow) {
                // `start` thunks its argument so it runs on the worker, not eagerly on
                // the current thread. `start { … }` already carries its block; `start EXPR`
                // (e.g. `start render-page($d)`) must wrap EXPR in a deferred block, else
                // it evaluates before the promise is even spawned (defeating parallelism).
                advance();
                auto call = std::make_unique<Call>();
                call->name = "start";
                auto be = std::make_unique<BlockExpr>();
                if (isKind(Tok::LBrace)) {
                    auto blk = parseBlock();
                    be->body = std::move(blk->stmts);
                } else if (isIdent("for") || isIdent("if") || isIdent("unless") || isIdent("while") ||
                           isIdent("until") || isIdent("loop") || isIdent("repeat") || isIdent("given") ||
                           isIdent("when") || isIdent("with") || isIdent("without")) {
                    auto st = parseStatement();
                    markLoopAsExpr(st.get());
                    be->body.push_back(std::move(st));
                } else {
                    auto es = std::make_unique<ExprStmt>();
                    es->e = parseExpr(BP_PREFIX);
                    be->body.push_back(std::move(es));
                }
                call->args.push_back(std::move(be));
                return call;
            }
            if (name == "my" || name == "our" || name == "state" ||
                name == "has" || name == "constant") {
                advance();
                // `my class Foo {…}` / `my role …` as an expression — evaluates to the type
                if (isIdent("class") || isIdent("role") || isIdent("grammar") ||
                    isIdent("enum") || isIdent("subset")) {
                    auto u = std::make_unique<Unary>(); u->op = "do";
                    auto be = std::make_unique<BlockExpr>();
                    be->body.push_back(parseStatement());
                    u->operand = std::move(be);
                    return u;
                }
                {
                    // Expression-position declaration: the initializer binds to the
                    // DECLARATOR, tighter than any pending prefix op — Rakudo parses
                    // `plan +my @r := a, b, c` as `plan +(my @r := (a, b, c))`.
                    // Consume `= / :=` here so a caller's prefix can't capture the
                    // bare declarator as its operand first.
                    ExprPtr decl = parseDeclarator(name);
                    if (isKind(Tok::Op) && (cur().text == "=" || cur().text == ":=")) {
                        bool listTarget = decl->kind == NK::ListExpr;
                        if (decl->kind == NK::VarExpr) {
                            const std::string& nm = static_cast<VarExpr*>(decl.get())->name;
                            listTarget = !nm.empty() && (nm[0] == '@' || nm[0] == '%');
                        }
                        auto as = std::make_unique<Assign>();
                        as->op = advance().text;
                        as->target = std::move(decl);
                        as->value = parseExpr(listTarget ? BP_ZIP : BP_ASSIGN); // list decls include Z/X
                        return as;
                    }
                    return decl;
                }
            }
            advance(); // consume the name
            // operator-name call: infix:<+>(1,2) / postfix:<i>($x) — canonical op name
            if ((name == "infix" || name == "prefix" || name == "postfix") &&
                isOp(":") && !cur().spaceBefore &&
                peek().kind == Tok::Op && peek().text == "<" &&
                peek(2).kind == Tok::Op && peek(2).text.size() > 1 &&
                peek(3).kind == Tok::Op && peek(3).text == ">") {
                // `: < OPTOKEN >` where OPTOKEN is a fused multi-char op (e.g. the
                // hyper `>>+<<` from `infix:<»+«>`) — take it whole (must run before
                // the generic readAngleWords path, whose prefix-glue would split it);
                // hyper markers become «» in the name so `<<+>>` isn't taken for a
                // double-angle-quoted `+`
                advance(); advance(); // : <
                name += ":<" + hyperMarkersToUni(advance().text) + ">";
                advance(); // >
            }
            else if ((name == "infix" || name == "prefix" || name == "postfix") &&
                isOp(":") && !cur().spaceBefore &&
                peek().kind == Tok::Op && peek().text == "<" && !peek().spaceBefore) {
                advance(); advance(); // : <
                auto w = readAngleWords(">");
                name += ":<" + (w.empty() ? std::string() : w[0]) + ">";
            }
            else if ((name == "infix" || name == "prefix" || name == "postfix") &&
                     isOp(":") && !cur().spaceBefore && peek().kind == Tok::QwList &&
                     !peek().spaceBefore) {
                // the lexer took `<»+«>` as a word list — its text is the op name
                advance(); // :
                name += ":<" + advance().text + ">";
            }
            else if ((name == "infix" || name == "prefix" || name == "postfix") &&
                     isOp(":") && !cur().spaceBefore &&
                     peek().kind == Tok::Op && peek().text.size() > 1 &&
                     peek().text[0] == '<' && peek().text.compare(0, 2, "<<") != 0 &&
                     !peek().spaceBefore) {
                // fused op token: `infix:<=>` lexes `<=>` whole, `infix:<==>` lexes
                // `<==`+`>` — strip the leading '<' and let readAngleWords close it
                advance(); // :
                toks_[pos_].text = cur().text.substr(1);
                auto w = readAngleWords(">");
                name += ":<" + (w.empty() ? std::string() : w[0]) + ">";
            }
            // pseudo-package angle access: `MY::<$x>` / `CORE::<&not>` / `PROCESS::<$IN>`
            // (the lexer folds the trailing `::` into the identifier) — the symbol is
            // resolved through the ordinary scope chain.
            if (name.size() > 2 && name.compare(name.size() - 2, 2, "::") == 0 &&
                isPseudoPkg(name.substr(0, name.size() - 2)) &&
                isOp("<") && !cur().spaceBefore) {
                advance(); // <
                std::vector<std::string> words = readAngleWords(">");
                return std::make_unique<VarExpr>(
                    pseudoAngleSymbol(name.substr(0, name.size() - 2),
                                      words.empty() ? "" : words[0]));
            }
            // parameterized type: `Array[Int]`, `Hash[Int,Str]`, `Foo[Bar]` — a capitalized
            // runtime type parameterization with a variable: array[$T].new / Blob[$T]
            if (!name.empty() && (std::isupper((unsigned char)name[0]) || name == "array") &&
                isKind(Tok::LBracket) && !cur().spaceBefore &&
                peek().kind == Tok::Var && peek(2).kind == Tok::RBracket) {
                advance(); // [
                auto ix = std::make_unique<Index>();
                ix->base = std::make_unique<NameTerm>(name);
                ix->index = std::make_unique<VarExpr>(advance().text);
                ix->isHash = false;
                advance(); // ]
                return ix;
            }
            // bareword tight against `[` whose first arg is a (capitalized) type name.
            if (!name.empty() && (std::isupper((unsigned char)name[0]) || name == "array") &&
                isKind(Tok::LBracket) && !cur().spaceBefore &&
                peek().kind == Tok::Ident && !peek().text.empty() &&
                (std::isupper((unsigned char)peek().text[0]) ||
                 [&]{ static const std::set<std::string> nat = {
                          "int","int8","int16","int32","int64","uint","uint8","uint16",
                          "uint32","uint64","byte","num","num32","num64","str"};
                      return nat.count(peek().text) > 0; }())) {
                advance(); // [
                std::string params;
                int tpd = 1; // nesting: Baz[Foo[Int], Bar[Int]]
                while (tpd > 0 && !isKind(Tok::End)) {
                    if (isKind(Tok::LBracket)) { tpd++; params += "["; advance(); continue; }
                    if (isKind(Tok::RBracket)) {
                        tpd--;
                        if (tpd == 0) break;
                        params += "]"; advance(); continue;
                    }
                    if (isKind(Tok::Ident)) {
                        if (!params.empty() &&
                            (std::isalnum((unsigned char)params.back()) || params.back() == ']'))
                            params += ",";
                        params += advance().text;
                        // package-qualified segment: Foo::Bar
                        while (isOp("::") && peek().kind == Tok::Ident) {
                            advance(); params += "::" + advance().text;
                        }
                        continue;
                    }
                    advance(); // commas / smileys — the comma is re-added implicitly
                }
                expectKind(Tok::RBracket, "]");
                auto nt = std::make_unique<NameTerm>(name);
                nt->ofType = params;
                return nt;
            }
            // type smiley on a type name: Channel:U / Foo:D / Bar:_  (definedness ignored)
            if (!name.empty() && std::isupper((unsigned char)name[0]) &&
                isOp(":") && !cur().spaceBefore && peek().kind == Tok::Ident &&
                (peek().text == "U" || peek().text == "D" || peek().text == "_")) {
                advance(); advance(); // : and the smiley letter
                return std::make_unique<NameTerm>(name);
            }
            // `use nqp` compatibility subset: nqp::const::X resolves to an
            // IntLit at parse time; nqp::op(...) becomes a dedicated NqpOp
            // node (or native lazy constructs) below. Programs without
            // `use nqp` never enter this branch — zero cost elsewhere.
            if (useNqp_ && name.size() > 5 && name.compare(0, 5, "nqp::") == 0) {
                if (name.compare(0, 12, "nqp::const::") == 0) {
                    if (long long cv; nqpConstValue(name.substr(12), cv))
                        return std::make_unique<IntLit>(cv);
                }
                // a bare `nqp::op` with NO argument list is a zero-arg op call
                // (`nqp::list_i`, `nqp::null`). Only when no `(` follows; the
                // parenthesized form is handled below.
                if (!(isKind(Tok::LParen) && !cur().spaceBefore)) {
                    std::vector<ExprPtr> none;
                    if (ExprPtr n = makeNqpOp(name.substr(5), none)) return n;
                }
            }
            if (isKind(Tok::LParen) && !cur().spaceBefore) {
                advance();
                ExprPtr invocant;
                auto callArgs = parseCallArgs(&invocant);
                if (invocant) { // indirect invocant: `key($pair: args)` == `$pair.key(args)`
                    auto mc = std::make_unique<MethodCall>();
                    mc->inv = std::move(invocant);
                    mc->method = name;
                    mc->args = std::move(callArgs);
                    return mc;
                }
                if (useNqp_ && name.size() > 5 && name.compare(0, 5, "nqp::") == 0)
                    if (ExprPtr n = makeNqpOp(name.substr(5), callArgs))
                        return n;
                auto c = std::make_unique<Call>();
                c->name = name;
                c->args = std::move(callArgs);
                return c;
            }
            // list-op style call without parens
            // but a capitalized bareword followed by a block is a type + block body, e.g. `if Mu { }`
            if (isKind(Tok::LBrace) && !name.empty() && std::isupper((unsigned char)name[0]))
                return std::make_unique<NameTerm>(name);
            // A capitalized bareword (a type) followed by whitespace then `.method` is
            // a postfix method call on the type — `Thing .new` is `Thing.new`, NOT a
            // listop call `Thing(.new)`. (Whitespace before a postfix `.` is allowed.)
            // Require an identifier after the dot so `.method` is meant, not `.=`/`.(`.
            if (!name.empty() && std::isupper((unsigned char)name[0]) &&
                cur().kind == Tok::Op && cur().text == "." && cur().spaceBefore &&
                peek().kind == Tok::Ident)
                return std::make_unique<NameTerm>(name);
            // A name declared sigilless (`my \x`, `\a` param, `-> \d`) is a TERM, not a
            // listop: `x2 < 0 || 1 > 7` is two comparisons, never `x2(< 0 || 1 >) 7`.
            // (A tight `name(...)` call was already handled above, so invoking a
            // Callable held in a sigilless var still works.)
            if (sigilless_.count(name)) return std::make_unique<NameTerm>(name);
            // For +/-/? the prefix reading is only valid when the operand is
            // tight against the operator (`f -5` => f(-5), but `f - 5` => f() - 5).
            bool listopOk = startsListopArg(cur());
            // Higher-order list builtins accept a pointy-block first arg without parens:
            // `map -> $v {…}, @list` (a bare `{…}` already works via startsListopArg). Gated
            // to these names so a general `bareword ->` isn't misread as a listop call.
            if (!listopOk && (cur().text == "->" || cur().text == "<->")) {
                static const std::set<std::string> blockListops = {
                    "map", "grep", "first", "sort", "reduce", "produce",
                    "classify", "categorize", "grep-index", "first-index",
                };
                if (blockListops.count(name)) listopOk = true;
            }
            // callsame/nextsame are nullary redispatchers — they reuse the current
            // args and take none of their own, so `"[" ~ callsame ~ "]"` must not read
            // the trailing `~ "]"` (prefix ~) as an argument. (callwith/nextwith/
            // samewith DO take args and are left alone.)
            if (name == "callsame" || name == "nextsame") listopOk = false;
            // `so *` / `not *` — a bare Whatever curries through the boolish prefix
            // (a general `name *` stays multiplication)
            if (!listopOk && cur().kind == Tok::Op && cur().text == "*" &&
                (name == "so" || name == "not")) listopOk = true;
            if (listopOk && cur().kind == Tok::Op &&
                (cur().text == "+" || cur().text == "-" || cur().text == "?" || cur().text == "|" || cur().text == "!!") &&
                peek(1).spaceBefore)
                listopOk = false; // `f -5` => f(-5) but `f - 5` => f() - 5; likewise `run |@x` slip;
                                   // and `Nil !! Any` (space after !!) is a ternary else-marker, not `Nil(!!Any)`
            // `foo < 1` (space after `<`) is infix less-than, not the word-list `foo(< 1 >)` —
            // UNLESS a matching `>` actually closes a word-list first (`is < foo bar >, exp`).
            if (listopOk && cur().kind == Tok::Op && cur().text == "<" && peek(1).spaceBefore) {
                bool wordlist = false; int depth = 0;
                for (size_t k = 1; peek(k).kind != Tok::End; k++) {
                    const Token& tk = peek(k);
                    if (tk.kind == Tok::LParen || tk.kind == Tok::LBracket || tk.kind == Tok::LBrace) depth++;
                    else if (tk.kind == Tok::RParen || tk.kind == Tok::RBracket || tk.kind == Tok::RBrace) { if (depth == 0) break; depth--; }
                    else if (tk.kind == Tok::Semicolon && depth == 0) break;
                    else if (tk.kind == Tok::Op && tk.text == ">" && depth == 0) { wordlist = true; break; }
                }
                if (!wordlist) listopOk = false;
            }
            if (listopOk) {
                auto c = std::make_unique<Call>();
                c->name = name;
                // parse the WHOLE argument expression including the list-infix tier,
                // so `flat @a Z @b` is flat((@a Z @b)); a bare top-level comma list
                // spreads back into individual arguments below.
                ExprPtr firstArg = parseExpr(BP_ZIP);
                // Indirect-object (dative): `print $*OUT: 'ok'` == `$*OUT.print('ok')`.
                // The colon must be TIGHT against the invocant (no space before) — a
                // space before ':' is an adverb (`slurp $p :bin`), not an invocant.
                // Restricted to the IO writer verbs to keep colon parsing unambiguous.
                // Any method name may be called indirect-object style (`doit $obj: args`);
                // the tight ':' after the invocant expression is the marker.
                if (isOp(":") && !cur().spaceBefore) {
                    advance(); // consume the invocant-marking ':'
                    auto mc = std::make_unique<MethodCall>();
                    mc->inv = std::move(firstArg);
                    mc->method = name;
                    if (startsTermToken(cur()))
                        do { mc->args.push_back(parseExpr(BP_ASSIGN)); } while (matchKind(Tok::Comma) && startsTermToken(cur()));
                    return mc;
                }
                if (firstArg->kind == NK::ListExpr && !static_cast<ListExpr*>(firstArg.get())->parenned) {
                    auto* le = static_cast<ListExpr*>(firstArg.get());
                    for (auto& it : le->items) c->args.push_back(std::move(it));
                }
                else c->args.push_back(std::move(firstArg));
                while (matchKind(Tok::Comma) && startsTermToken(cur())) {
                    c->args.push_back(parseExpr(BP_ASSIGN));
                }
                // `say 1, 3 ... 19` — the gathered args are the seed of a sequence
                if (isOp("...") || isOp("...^")) {
                    auto seq = std::make_unique<Binary>();
                    seq->op = advance().text;
                    if (c->args.size() == 1) seq->lhs = std::move(c->args[0]);
                    else { auto le = std::make_unique<ListExpr>(); for (auto& a : c->args) le->items.push_back(std::move(a)); seq->lhs = std::move(le); }
                    seq->rhs = parseExpr(BP_COMMA + 1);
                    c->args.clear();
                    c->args.push_back(std::move(seq));
                    return c;
                }
                // `f (a, b)` (space then a parenthesised comma-list) passes ONE List
                // argument — unlike the tight call form `f(a, b)` (Rakudo semantics).
                // An empty `f ()` still means no arguments.
                if (c->args.size() == 1 && c->args[0]->kind == NK::ListExpr &&
                    static_cast<ListExpr*>(c->args[0].get())->items.empty())
                    c->args.clear();
                return c;
            }
            // a bare IO verb as a term is a compile error in Raku ('put;' vs 'put()')
            // — only when the statement truly ends here (`say if …` has a modifier)
            if ((name == "put" || name == "say" || name == "print") &&
                (isKind(Tok::Semicolon) || isKind(Tok::End) || isKind(Tok::RBrace)))
                throw ParseError("Unsupported use of bare '" + name + "'. In Raku please use: '" + name + "()'", cur().line);
            return std::make_unique<NameTerm>(name);
        }
        default:
            if (isKind(Tok::End)) // `42 +` — an infix with no right-hand side
                error("Confused: missing required term after infix");
            error("Confused"); // Rakudo's generic "confused parse" message
    }
}

// ---------------- string interpolation ----------------
ExprPtr Parser::parseEmbeddedExpr(const std::string& src) {
    Lexer lx(src);
    Parser p(lx.tokenize());
    // Inherit user-defined operators so `"{ 4! }"` sees this program's `postfix:<!>`
    // (and custom infix/prefix/circumfix) just as top-level code does.
    p.userInfix_ = userInfix_;
    p.userPrefix_ = userPrefix_;
    p.useNqp_ = useNqp_; // `"{ nqp::chr($o) }"` in a `use nqp` unit sees the subset
    p.userPostfix_ = userPostfix_;
    p.userCircumfix_ = userCircumfix_;
    p.userPostcircumfix_ = userPostcircumfix_;
    Program prog = p.parseProgram();
    // a single bare expression interpolates directly
    if (prog.stmts.size() == 1 && prog.stmts[0]->kind == NK::ExprStmt)
        return std::move(static_cast<ExprStmt*>(prog.stmts[0].get())->e);
    // statements (with modifiers, e.g. `{ 'x' if $y }`) run as a do-block
    auto be = std::make_unique<BlockExpr>();
    be->body = std::move(prog.stmts);
    auto u = std::make_unique<Unary>();
    u->op = "do"; u->operand = std::move(be);
    return u;
}

// Read a `<...>` / `«...»` word list (the opening delimiter is already consumed).
// Tokens with no intervening whitespace join into one word, so "/usr/bin" and
// "1/0" stay whole rather than splitting on '/'.
std::vector<std::string> Parser::readAngleWords(const std::string& close) {
    std::vector<std::string> words;
    // a `<…>` word list may CONTAIN nested angle forms (`< :10<42> :16<2a> >`):
    // an inner `<` opens a nested group whose `>` is content, not the closer
    int depth = 0;
    while (!(isOp(close) && depth == 0) && !isKind(Tok::End)) {
        if (close == ">" && cur().kind == Tok::Op && cur().text == "<") depth++;
        else if (close == ">" && depth > 0 && cur().kind == Tok::Op && cur().text == ">") depth--;
        // The closing delimiter may be glued to a following operator by the lexer:
        // `%h<a>=9` lexes `>=` as one token, `%h<a>>` lexes `>>`. Split it: the leading
        // `>` closes the word list, the remainder stays as the next token. Only when the
        // token is glued to the preceding word (no space) — a spaced `< a >= b >` word
        // list legitimately contains `>=` as a word and must not be truncated.
        if (depth == 0 && cur().kind == Tok::Op && !cur().spaceBefore &&
            cur().text.size() > close.size() &&
            cur().text.compare(0, close.size(), close) == 0) {
            toks_[pos_].text = cur().text.substr(close.size());
            toks_[pos_].spaceBefore = false;
            // re-glue a hyper split by the close: `<a b>>>.uc` lexes `>>`+`>` —
            // the leftover `>` joins a following glued `>` back into hyper `>>`
            if (toks_[pos_].text == ">" && pos_ + 1 < toks_.size() &&
                toks_[pos_ + 1].kind == Tok::Op && !toks_[pos_ + 1].spaceBefore &&
                toks_[pos_ + 1].text == ">") {
                toks_[pos_].text = ">>";
                toks_.erase(toks_.begin() + pos_ + 1);
            }
            return words;
        }
        // symmetric end-glue: `infix:<+>` lexes `+>` as ONE op token — the
        // trailing close belongs to the word list; the front is the word.
        if (depth == 0 && cur().kind == Tok::Op && cur().text.size() > close.size() &&
            cur().text.compare(cur().text.size() - close.size(), close.size(), close) == 0) {
            std::string word = cur().text.substr(0, cur().text.size() - close.size());
            if (words.empty() || cur().spaceBefore) words.push_back(word);
            else words.back() += word;
            advance();
            return words;
        }
        const Token& t = cur();
        if (words.empty() || t.spaceBefore) words.push_back(t.text);
        else words.back() += t.text;
        advance();
    }
    matchOp(close);
    return words;
}

ExprPtr Parser::parseInterpString(const std::string& rawIn) {
    // interpolation-feature prefix from quoting adverbs (q:c / Q:s / qq:!s):
    // "\x02feats\x02" — s=scalars a=arrays h=hashes f=&calls c={blocks} b=backslashes
    std::string raw = rawIn;
    bool fS = true, fA = true, fH = true, fC = true, fB = true;
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
        }
    }
    auto result = std::make_unique<InterpStr>();
    std::string lit;
    auto flush = [&]() {
        if (!lit.empty()) { result->parts.push_back(std::make_unique<StrLit>(lit)); lit.clear(); }
    };
    auto isIdentCont = [](char c) { return std::isalnum((unsigned char)c) || c == '_'; };

    size_t i = 0, n = raw.size();
    while (i < n) {
        char c = raw[i];
        if (fB && c == '\\' && i + 1 < n) {
            char e = raw[i + 1];
            // \x41 \x[263a] hex, \o77 \o[..] octal
            if (e == 'x' || e == 'o') {
                int base = (e == 'x') ? 16 : 8;
                size_t j = i + 2;
                auto emitCp = [&](long cp) {
                    if (cp < 0x80) lit += (char)cp;
                    else if (cp < 0x800) { lit += (char)(0xC0 | (cp >> 6)); lit += (char)(0x80 | (cp & 0x3F)); }
                    else if (cp < 0x10000) { lit += (char)(0xE0 | (cp >> 12)); lit += (char)(0x80 | ((cp >> 6) & 0x3F)); lit += (char)(0x80 | (cp & 0x3F)); }
                    else { lit += (char)(0xF0 | (cp >> 18)); lit += (char)(0x80 | ((cp >> 12) & 0x3F)); lit += (char)(0x80 | ((cp >> 6) & 0x3F)); lit += (char)(0x80 | (cp & 0x3F)); }
                };
                if (j < n && raw[j] == '[') {
                    j++; std::string body;
                    while (j < n && raw[j] != ']') body += raw[j++];
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
                    auto isd = [&](char ch) { return base == 16 ? std::isxdigit((unsigned char)ch) : (ch >= '0' && ch <= '7'); };
                    while (j < n && isd(raw[j])) digits += raw[j++];
                    emitCp(strtol(digits.c_str(), nullptr, base));
                }
                i = j;
                continue;
            }
            // \c[NAME] / \c[65] / \c[NAME1, NAME2] — named or numeric codepoints
            if (e == 'c' && i + 2 < n && raw[i + 2] == '[') {
                size_t j = i + 3; std::string body;
                while (j < n && raw[j] != ']') body += raw[j++];
                if (j < n) j++; // ]
                auto emitCp = [&](long cp) {
                    if (cp < 0x80) lit += (char)cp;
                    else if (cp < 0x800) { lit += (char)(0xC0 | (cp >> 6)); lit += (char)(0x80 | (cp & 0x3F)); }
                    else if (cp < 0x10000) { lit += (char)(0xE0 | (cp >> 12)); lit += (char)(0x80 | ((cp >> 6) & 0x3F)); lit += (char)(0x80 | (cp & 0x3F)); }
                    else { lit += (char)(0xF0 | (cp >> 18)); lit += (char)(0x80 | ((cp >> 12) & 0x3F)); lit += (char)(0x80 | ((cp >> 6) & 0x3F)); lit += (char)(0x80 | (cp & 0x3F)); }
                };
                size_t p = 0;
                while (p < body.size()) {
                    size_t comma = body.find(',', p);
                    std::string tok = body.substr(p, comma == std::string::npos ? std::string::npos : comma - p);
                    size_t a = tok.find_first_not_of(" \t"), b = tok.find_last_not_of(" \t");
                    if (a != std::string::npos) tok = tok.substr(a, b - a + 1);
                    if (!tok.empty()) {
                        if (std::isdigit((unsigned char)tok[0])) emitCp(strtol(tok.c_str(), nullptr, 10));
                        else { int32_t cp = uniCharByName(tok); if (cp >= 0) emitCp(cp); }
                    }
                    if (comma == std::string::npos) break;
                    p = comma + 1;
                }
                i = j;
                continue;
            }
            switch (e) {
                case 'n': lit += '\n'; break;
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
                    // c/x/o pass through — their bracketed forms are handled
                    // above and the bare-digit forms (\c10, \x41) downstream.
                    if (std::isalpha((unsigned char)e) &&
                        e != 'c' && e != 'x' && e != 'o')
                        throw ParseError("Unrecognized backslash sequence: '\\" +
                                         std::string(1, e) + "'", cur().line,
                                         "X::Backslash::UnrecognizedSequence",
                                         {{"sequence", std::string(1, e)}});
                    lit += e;
                    break;
            }
            i += 2;
            continue;
        }
        if (fC && c == '{') {
            // balanced code block
            int depth = 1; size_t j = i + 1; std::string inner;
            while (j < n && depth > 0) {
                if (raw[j] == '{') depth++;
                else if (raw[j] == '}') { depth--; if (depth == 0) break; }
                inner += raw[j]; j++;
            }
            flush();
            try { result->parts.push_back(parseEmbeddedExpr(inner)); } catch (...) {}
            i = j + 1;
            continue;
        }
        // `$/` (match) and `$!` (error) as standalone interpolated vars
        if (fS && c == '$' && (i + 1 < n) && (raw[i + 1] == '/' || raw[i + 1] == '!') &&
            !(i + 2 < n && (std::isalnum((unsigned char)raw[i + 2]) || raw[i + 2] == '_'))) {
            flush();
            result->parts.push_back(std::make_unique<VarExpr>(std::string("$") + raw[i + 1]));
            i += 2;
            continue;
        }
        // numbered regex captures `$0` `$1` … and named captures `$<name>`
        if (fS && c == '$' && (i + 1 < n) && (std::isdigit((unsigned char)raw[i + 1]) || raw[i + 1] == '<')) {
            size_t j = i + 1;
            std::string var("$");
            if (raw[j] == '<') {
                var += raw[j++];
                while (j < n && raw[j] != '>') var += raw[j++];
                if (j < n) var += raw[j++]; // closing >
            } else {
                while (j < n && std::isdigit((unsigned char)raw[j])) var += raw[j++];
            }
            // optional postfix subscript on the capture: $0[1] $<k>{...}
            for (;;) {
                if (j < n && raw[j] == '[') {
                    int d = 1; var += raw[j++];
                    while (j < n && d > 0) { if (raw[j]=='[') d++; else if (raw[j]==']') d--; var += raw[j++]; }
                } else if (j < n && raw[j] == '{') {
                    int d = 1; var += raw[j++];
                    while (j < n && d > 0) { if (raw[j]=='{') d++; else if (raw[j]=='}') d--; var += raw[j++]; }
                } else break;
            }
            flush();
            try { result->parts.push_back(parseEmbeddedExpr(var)); } catch (...) { lit += var; }
            i = j;
            continue;
        }
        if (((c == '$' && fS) || (c == '@' && fA) || (c == '%' && fH)) &&
            (i + 1 < n) && (std::isalpha((unsigned char)raw[i + 1]) || raw[i + 1] == '_' ||
                            raw[i + 1] == '{' || raw[i + 1] == '*' || raw[i + 1] == '!' ||
                            raw[i + 1] == '.' || raw[i + 1] == '^' ||
                            (unsigned char)raw[i + 1] >= 0x80)) {
            char sig = c;
            size_t j = i + 1;
            std::string var(1, sig);
            if (raw[j] == '{') {
                // ${ ... }
                int depth = 1; j++; std::string inner;
                while (j < n && depth > 0) {
                    if (raw[j] == '{') depth++;
                    else if (raw[j] == '}') { depth--; if (depth == 0) break; }
                    inner += raw[j]; j++;
                }
                flush();
                try { result->parts.push_back(parseEmbeddedExpr(std::string(1, sig) + "(" + inner + ")")); } catch (...) {}
                i = j + 1;
                continue;
            }
            // twigil ($*dyn, $!attr, $.attr, $^placeholder)
            if (raw[j] == '*' || raw[j] == '!' || raw[j] == '.' || raw[j] == '^') var += raw[j++];
            while (j < n && (isIdentCont(raw[j]) || (unsigned char)raw[j] >= 0x80)) var += raw[j++];
            // hyphen/apostrophe continue the name when followed by an alphanumeric ($foo-bar)
            while (j + 1 < n && (raw[j] == '-' || raw[j] == '\'') && std::isalnum((unsigned char)raw[j + 1])) {
                var += raw[j++];
                while (j < n && isIdentCont(raw[j])) var += raw[j++];
            }
            // postfix chain: [..] {..} <..> .name .[..] .{..} .(..)
            // A method call interpolates only if it — or a later link in the
            // chain — has parens: "$x.ord" stays literal, but "$x.ord.fmt('%d')"
            // interpolates the whole chain up to the last parenthesised call
            // ("$x.ord.fmt('%d').flip" leaves the trailing bare .flip literal).
            // Subscripts always interpolate. Bare .method links are appended
            // tentatively and only "committed" once a parenthesised call or a
            // subscript follows; any uncommitted tail is dropped back to literal.
            bool hadPostfix = false;
            size_t committedLen = var.size();   // var length confirmed for interpolation
            size_t committedJ = j;              // matching raw index
            auto commit = [&]() { hadPostfix = true; committedLen = var.size(); committedJ = j; };
            for (;;) {
                if (j < n && raw[j] == '[') {
                    int d = 1; var += raw[j++];
                    while (j < n && d > 0) { if (raw[j]=='[') d++; else if (raw[j]==']') d--; var += raw[j++]; }
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
                } else if (j + 1 < n && raw[j] == '.' && (raw[j+1] == '[' || raw[j+1] == '{' || raw[j+1] == '(')) {
                    var += raw[j++]; // .
                    char open = raw[j], close = open == '[' ? ']' : open == '{' ? '}' : ')';
                    int d = 1; var += raw[j++];
                    while (j < n && d > 0) { if (raw[j]==open) d++; else if (raw[j]==close) d--; var += raw[j++]; }
                    commit();
                } else if (j + 1 < n && raw[j] == '.' && (std::isalpha((unsigned char)raw[j+1]) || raw[j+1]=='_')) {
                    // bare .method — tentative; consume its name, commit only if parens follow
                    var += raw[j++]; // .
                    while (j < n && isIdentCont(raw[j])) var += raw[j++];
                    if (j < n && raw[j] == '(') {
                        int d = 1; var += raw[j++];
                        while (j < n && d > 0) { if (raw[j]=='(') d++; else if (raw[j]==')') d--; var += raw[j++]; }
                        commit();
                    }
                    // else: leave uncommitted; a later link in the chain may still commit it
                } else break;
            }
            // drop any uncommitted trailing bare-method links back to literal text
            if (committedLen < var.size()) { var.resize(committedLen); j = committedJ; }
            // @arr/%hash only interpolate when followed by a postcircumfix/method
            if ((sig == '@' || sig == '%') && !hadPostfix) { lit += var; i = j; continue; }
            flush();
            try { result->parts.push_back(parseEmbeddedExpr(var)); } catch (...) { lit += var; }
            i = j;
            continue;
        }
        lit += c;
        i++;
    }
    flush();
    return result;
}

// ---------------- statements ----------------
std::unique_ptr<Block> Parser::parseBlock() {
    expectKind(Tok::LBrace, "{");
    size_t opMark = opUndo_.size(); // user operators are lexically scoped
    monkeyScopes_.push_back(0);
    auto blk = std::make_unique<Block>();
    while (!isKind(Tok::RBrace) && !isKind(Tok::End)) {
        if (matchKind(Tok::Semicolon)) continue;
        blk->stmts.push_back(parseStatement());
        if (!isKind(Tok::Semicolon)) enforceStmtSep();
    }
    checkRedeclarations(blk->stmts);
    monkeyScopes_.pop_back();
    expectKind(Tok::RBrace, "}");
    opRollback(opMark);
    return blk;
}

std::vector<Param> Parser::parseSignature(Tok closeTok) {
    std::vector<Param> params;
    while (!isKind(closeTok) && !isKind(Tok::End)) {
        if (matchKind(Tok::Semicolon)) continue; // multi-frame separator `;` / `;;` in signatures
        Param p;
        // return-type constraint `--> Type` — always last; discarded. Skip to the
        // end of the signature so smileys (IO::Path:D) and parametrised types
        // (Positional[Int], (Int, Str)) don't trip the `)`-expectation.
        if (matchOp("-->")) {
            if (isKind(Tok::Ident) && (cur().text == "True" || cur().text == "False" || cur().text == "Nil"))
                sigRetLiteral_ = parsePrimary(); // `--> True` : a literal Bool/Nil return value
            else if (isKind(Tok::Ident) && (cur().text == "True" || cur().text == "False" || cur().text == "Nil"))
                sigRetLiteral_ = parsePrimary(); // `--> True` : a literal Bool/Nil return value
            else if (isKind(Tok::Ident)) sigRetType_ = cur().text; // remember the return type
            else if (isKind(Tok::IntLit) || isKind(Tok::NumLit) || isKind(Tok::StrLit) || isKind(Tok::StrInterp))
                sigRetLiteral_ = parsePrimary(); // `(… --> 1)`: literal return value
            int depth = 0;
            while (!isKind(Tok::End)) {
                if (depth == 0 && (isKind(Tok::RParen) || isKind(Tok::Semicolon) ||
                                   isKind(closeTok))) break; // pointy sigs close at `{`
                if (isKind(Tok::LParen) || isKind(Tok::LBracket)) depth++;
                else if (isKind(Tok::RParen) || isKind(Tok::RBracket)) depth--;
                advance();
            }
            break;
        }
        // literal parameter, e.g. multi MAIN('population') / multi fact(0)
        if (isKind(Tok::StrLit) || isKind(Tok::StrInterp) || isKind(Tok::IntLit) || isKind(Tok::NumLit)) {
            p.litVal = parsePrimary();
            params.push_back(std::move(p));
            if (!matchKind(Tok::Comma)) break;
            continue;
        }
        // Destructuring / sub-signature parameter: `[$a, $b]` (array) or `($a, $b)`
        // (list). Parse the inner signature and record it; bindParams unpacks the
        // argument's elements into these inner params at call time.
        if (isKind(Tok::LBracket) || isKind(Tok::LParen)) {
            Tok close = isKind(Tok::LBracket) ? Tok::RBracket : Tok::RParen;
            advance(); // consume '[' or '('
            p.subSig = std::make_shared<std::vector<Param>>(parseSignature(close));
            if (!matchKind(close)) error("expected closing bracket in sub-signature");
            p.name = ""; p.sigil = '$';
            if (matchOp("=")) p.defaultVal = parseExpr(BP_ASSIGN);
            params.push_back(std::move(p));
            if (!matchKind(Tok::Comma) && !matchKind(Tok::Semicolon)) break;
            continue;
        }
        // typed capture `Capture |cap`: the type is informational — skip to the `|`
        if (isKind(Tok::Ident) && peek().kind == Tok::Op && peek().text == "|" &&
            (peek(2).kind == Tok::Ident || peek(2).kind == Tok::Var))
            advance();
        if (matchOp("|")) {
            // capture parameter `|c` — slurps remaining positional+named args. Don't
            // swallow a trailing trait keyword (`| where …`) as the capture name.
            if ((isKind(Tok::Ident) && !isIdent("where") && !isIdent("is") && !isIdent("of")) || isKind(Tok::Var))
                p.name = advance().text;
            // optional sub-signature `|c( … )` / `| ( … )` — parse it (destructures
            // the capture's positionals into these inner params at call time)
            if (isKind(Tok::LParen)) {
                advance();
                p.subSig = std::make_shared<std::vector<Param>>(parseSignature(Tok::RParen));
                if (!matchKind(Tok::RParen)) error("expected ')' in capture sub-signature");
            }
            // a capture can carry a `where`/trait constraint: `| where { … }`
            while (isIdent("where") || isIdent("is") || isIdent("of")) {
                std::string trait = advance().text;
                if (trait == "where") p.whereExpr = parseExpr(BP_ASSIGN + 1); // stop before the `= default`
                else if (!isKind(Tok::Comma) && !isKind(Tok::RParen) && !isKind(Tok::End)) advance();
            }
            p.slurpy = true; p.sigil = '\\';
            params.push_back(std::move(p));
            if (!matchKind(Tok::Comma)) break;
            continue;
        }
        if (matchOp("**")) { p.slurpy = true; p.slurpyKind = 'n'; }      // **@a — no flatten
        else if (matchOp("*")) { p.slurpy = true; p.slurpyKind = 'f'; }  // *@a  — flatten iterables
        else if (matchOp("+")) { p.slurpy = true; p.slurpyKind = '1'; }  // +@a  — single-argument rule
        // slurpy with a sub-signature: `*[$a,$b]` — destructure the slurped args
        if (p.slurpy && isKind(Tok::LBracket)) {
            advance();
            p.subSig = std::make_shared<std::vector<Param>>(parseSignature(Tok::RBracket));
            if (!matchKind(Tok::RBracket)) error("expected ']' in slurpy sub-signature");
            params.push_back(std::move(p));
            if (!matchKind(Tok::Comma)) break;
            continue;
        }
        // sigilless slurpy: `+ints` — binds the list under the bare name
        if (p.slurpy && isKind(Tok::Ident)) {
            p.name = advance().text;
            p.sigil = '\\';
            sigilless_.insert(p.name);
            params.push_back(std::move(p));
            if (!matchKind(Tok::Comma)) break;
            continue;
        }
        if (matchOp("\\")) {
            // sigilless capture parameter: \a  -> bound under bare name
            if (isKind(Tok::Ident) || isKind(Tok::Var)) p.name = advance().text;
            p.sigil = '\\';
            if (!p.name.empty()) sigilless_.insert(p.name);
            if (matchOp("=")) p.defaultVal = parseExpr(BP_ASSIGN);
            params.push_back(std::move(p));
            if (!matchKind(Tok::Comma)) break;
            continue;
        }
        bool named = matchOp(":");
        // named alias:  :name($var)  — external key `name`, binds `$var` (optional inner type)
        if (named && isKind(Tok::Ident) && peek().kind == Tok::LParen) {
            p.namedKey = advance().text;
            advance(); // (
            if (isKind(Tok::LParen)) { // nested sub-signature:  :value((Str :key($d), …))
                advance();
                p.subSig = std::make_shared<std::vector<Param>>(parseSignature(Tok::RParen));
                if (!matchKind(Tok::RParen)) error("expected ')' in nested sub-signature");
                p.name = ""; p.sigil = '$'; p.named = true;
                if (!matchKind(Tok::RParen)) error("expected ')' in named-parameter alias");
                if (matchOp("=")) p.defaultVal = parseExpr(BP_ASSIGN);
                params.push_back(std::move(p));
                if (!matchKind(Tok::Comma) && !matchKind(Tok::Semicolon)) break;
                continue;
            }
            if (isKind(Tok::Ident)) p.type = advance().text; // optional inner type constraint
            // nested alias layers: :x(:y(:z($a))) — every key answers
            int aliasDepth = 0;
            while (isOp(":") && peek().kind == Tok::Ident && peek(2).kind == Tok::LParen) {
                advance(); // :
                p.aliasKeys.push_back(advance().text);
                advance(); // (
                aliasDepth++;
            }
            p.aliasBoth = matchOp(":"); // :name(:$var) answers BOTH names
            if (isKind(Tok::Var)) { p.name = cur().text; p.sigil = cur().text[0]; advance(); }
            else error("expected variable in named-parameter alias");
            // `:in(:$in)` — the alias and the variable name collide
            if (p.aliasBoth && p.name.size() > 1 && p.namedKey == p.name.substr(1))
                throw ParseError("Name " + p.namedKey + " used for more than one named parameter",
                                 cur().line, "X::Signature::NameClash", {{"name", p.namedKey}});
            p.named = true;
            for (; aliasDepth > 0; aliasDepth--)
                if (!matchKind(Tok::RParen)) error("expected ')' in named-parameter alias");
            if (!matchKind(Tok::RParen)) error("expected ')' in named-parameter alias");
            if (matchOp("?")) p.optional = true;
            else if (matchOp("!")) p.required = true;
            while (isIdent("where") || isIdent("is") || isIdent("returns") || isIdent("of")) {
                std::string trait = advance().text;
                if (trait == "where") p.whereExpr = parseExpr(BP_ASSIGN + 1); // stop before the `= default`
                else if (!isKind(Tok::Comma) && !isKind(Tok::RParen) && !isKind(Tok::End) && !isOp("=")) {
                    if (trait == "is" && (isIdent("rw") || isIdent("copy"))) { p.isRw = (cur().text == "rw"); p.isCopy = (cur().text == "copy"); }
                    advance();
                }
            }
            if (matchOp("=")) p.defaultVal = parseExpr(BP_ASSIGN);
            params.push_back(std::move(p));
            if (!matchKind(Tok::Comma) && !matchKind(Tok::Semicolon)) break;
            continue;
        }
        // compile-time class type: ::?CLASS / ::?ROLE / ::?PACKAGE (with optional
        // :D/:U smiley and invocant colon) — no runtime constraint is enforced,
        // the name just parses like an unconstrained type
        if (isOp("::") && peek().kind == Tok::Op && peek().text == "?" &&
            peek(2).kind == Tok::Ident) {
            advance(); advance(); advance(); // :: ? CLASS
            if (isOp(":") && peek().kind == Tok::Ident &&
                (peek().text == "D" || peek().text == "U" || peek().text == "_")) {
                advance(); std::string sm = advance().text;
                if (sm == "D") p.defConstraint = 1; else if (sm == "U") p.defConstraint = 2;
            }
            // `(::?CLASS:U: Bar $b)` — a type-only INVOCANT marker: the bare
            // colon ends it; nothing binds, move on to the real parameters
            if (isOp(":")) { advance(); continue; }
        }
        // indirect/symbolic type constraint:  ::(EXPR) $p  (XML::Node uses
        // `method reparent(::(q<XML::Element>) $parent)`). The type is computed at
        // runtime, which we don't constrain against here — parse the `::(…)` and
        // leave the parameter unconstrained so it binds. The `~~ ::(EXPR)` and
        // `::(EXPR).method` expression forms already resolve at runtime.
        if (isOp("::") && peek().kind == Tok::LParen) {
            advance(); advance();               // :: (
            int depth = 1;
            while (depth > 0 && !isKind(Tok::End)) {
                if (isKind(Tok::LParen)) depth++;
                else if (isKind(Tok::RParen)) depth--;
                if (depth == 0) { advance(); break; }
                advance();
            }
        }
        // type-capture parameter:  ::T $x  /  ::T  /  ::Grammar:U :$named
        // Capture the type-variable name (+ optional smiley) and fall through to
        // the shared var/named/default handling so it can type a following param.
        if (isOp("::") && peek().kind == Tok::Ident) {
            advance(); p.type = advance().text;
            if (isOp(":") && peek().kind == Tok::Ident &&
                (peek().text == "D" || peek().text == "U" || peek().text == "_")) {
                advance(); std::string sm = advance().text;
                if (sm == "D") p.defConstraint = 1; else if (sm == "U") p.defConstraint = 2;
            }
        }
        // optional type constraint: a bare Ident (possibly Foo::Bar, with :D/:U smiley, [..])
        if (isKind(Tok::Ident)) {
            p.type = advance().text; // type name (used for multi-dispatch)
            if (isOp(":") && peek().kind == Tok::Ident &&
                (peek().text == "D" || peek().text == "U" || peek().text == "_")) { // :D/:U/:_ smiley
                advance();
                std::string sm = advance().text;
                if (sm == "D") p.defConstraint = 1;
                else if (sm == "U") p.defConstraint = 2;
            }
            if (isKind(Tok::LBracket) && !cur().spaceBefore) {
                int depth = 0;
                do { if (isKind(Tok::LBracket)) depth++; else if (isKind(Tok::RBracket)) depth--; advance(); }
                while (depth > 0 && !isKind(Tok::End));
            }
            // coercion type Str(Cool)  OR  destructuring sub-signature  Pair ( :key($k), … )
            // Coercion is the tight single-ident form; anything else is a sub-signature.
            if (isKind(Tok::LParen)) {
                bool coercion = !cur().spaceBefore &&
                                ((peek().kind == Tok::Ident && peek(2).kind == Tok::RParen) ||
                                 peek().kind == Tok::RParen); // `Foo()` — coerce from Any
                if (coercion) { p.coerce = true; advance(); if (isKind(Tok::Ident)) advance(); advance(); } // skip (Type)/(); bind coerces to p.type
                else {
                    advance(); // (
                    p.subSig = std::make_shared<std::vector<Param>>(parseSignature(Tok::RParen));
                    if (!matchKind(Tok::RParen)) error("expected ')' in sub-signature");
                }
            }
        }
        // named alias following a type constraint:  Int:D :key($plan)  /
        // Pair :value((Str:D :key($desc), :value(&tests)))  (nested sub-signature)
        bool aliasBound = false;
        // A named-alias colon is TIGHT against its key (`Int :key($x)`); an
        // invocant colon after a bare type has a SPACE after it (`URI: Str() $s`)
        // — the `!peek().spaceBefore` guard keeps the latter out of this branch
        // so it falls through to the invocant marker below.
        if (!named && isOp(":") && !peek().spaceBefore &&
            peek().kind == Tok::Ident && peek(2).kind == Tok::LParen) {
            advance(); // :
            p.namedKey = advance().text;
            advance(); // (
            if (isKind(Tok::LParen)) { // nested sub-signature form
                advance();
                p.subSig = std::make_shared<std::vector<Param>>(parseSignature(Tok::RParen));
                if (!matchKind(Tok::RParen)) error("expected ')' in nested sub-signature");
                p.name = ""; p.sigil = '$';
            } else {
                p.aliasBoth = matchOp(":"); // :name(:$var) answers BOTH names
                if (isKind(Tok::Var)) { p.name = cur().text; p.sigil = cur().text[0]; advance(); }
                else error("expected variable in named-parameter alias");
            }
            if (!matchKind(Tok::RParen)) error("expected ')' in named-parameter alias");
            p.named = true; named = true; aliasBound = true;
        }
        // `Type :$named` — the colon is TIGHT against the var; an invocant colon
        // after a smiley (`Query:D: $i`) has a SPACE after it, so `!peek().spaceBefore`
        // keeps it out of here and lets the invocant marker below claim it.
        if (!named && isOp(":") && !peek().spaceBefore && peek().kind == Tok::Var) { advance(); named = true; } // Type :$named
        if (aliasBound) { // name (or nested sub-sig) already bound by the alias above
        } else if (matchOp("\\")) { // typed sigilless capture:  Iterator:D \iter
            if (isKind(Tok::Ident) || isKind(Tok::Var)) p.name = advance().text;
            p.sigil = '\\';
            if (!p.name.empty()) sigilless_.insert(p.name);
        } else if (isKind(Tok::Var)) {
            // `sub f($0)` — numeric names can't be parameters either
            if (cur().text.size() > 1 && std::isdigit((unsigned char)cur().text[1]))
                throw ParseError("Cannot use a numeric variable as a parameter", cur().line,
                                 "X::Syntax::Variable::Numeric", {{"what", "parameter"}});
            // compile-time twigil vars ($?VERSION) can't be parameters —
            // dynamic ($*SCHEDULER) and accessor ($.x) parameters are legal
            if (cur().text.size() > 1 && cur().text[1] == '?')
                throw ParseError("Cannot use a variable with twigil '?' as a parameter",
                                 cur().line, "X::Parameter::Twigil",
                                 {{"parameter", cur().text}, {"twigil", "?"}});
            // a placeholder can't BE a declared parameter: `sub f($:x)` → :$x
            if (cur().text.size() > 2 && (cur().text[1] == ':' || cur().text[1] == '^')) {
                std::string pn = cur().text;
                std::string right = pn[1] == ':' ? (":" + std::string(1, pn[0]) + pn.substr(2))
                                                 : (std::string(1, pn[0]) + pn.substr(2));
                throw ParseError("In signature parameter, placeholder variables like " + pn +
                                 " are illegal; you probably meant a named parameter: '" + right + "'",
                                 cur().line, "X::Parameter::Placeholder",
                                 {{"parameter", pn}, {"right", right}});
            }
            p.name = cur().text; p.sigil = cur().text[0]; advance();
            p.named = named;
        } else if (isKind(Tok::Op) && cur().text.size() == 1 &&
                   (cur().text[0]=='%'||cur().text[0]=='@'||cur().text[0]=='&'||cur().text[0]=='$')) {
            // anonymous sigil-only parameter, e.g. method concretize($, $, %, %)
            p.sigil = cur().text[0]; p.name = ""; advance();
            p.named = named;
        } else if (!p.type.empty()) {
            // anonymous type-only parameter, e.g. (Str:U) / (Int) — used for dispatch, no binding
            p.name = ""; p.sigil = '$';
        } else error("expected parameter variable");
        // callable signature constraint `&code:(Int --> Bool)`: consumed and
        // ignored (we don't constrain callables by signature) — without this the
        // `(Int)` used to leak into the param list as a phantom anonymous Int
        if (p.sigil == '&' && isOp(":") && !cur().spaceBefore && peek().kind == Tok::LParen) {
            advance(); advance(); // : (
            int cd = 1;
            while (cd > 0 && !isKind(Tok::End)) {
                if (isKind(Tok::LParen)) cd++;
                else if (isKind(Tok::RParen)) cd--;
                advance();
            }
        }
        // destructuring sub-signature on a named param: `@a [$first, *@rest]` binds
        // @a to the argument AND unpacks its elements. A space before `[` distinguishes
        // it from the shaped-array form `@a[3]` handled just below.
        if (isKind(Tok::LBracket) && cur().spaceBefore) {
            advance(); // '['
            p.subSig = std::make_shared<std::vector<Param>>(parseSignature(Tok::RBracket));
            if (!matchKind(Tok::RBracket)) error("expected ']' in sub-signature");
        }
        // paren sub-signature after the variable:  Pair $p (Int :key($k), :value($v))
        if (!p.subSig && isKind(Tok::LParen) && cur().spaceBefore) {
            advance(); // '('
            p.subSig = std::make_shared<std::vector<Param>>(parseSignature(Tok::RParen));
            if (!matchKind(Tok::RParen)) error("expected ')' in sub-signature");
        }
        // shaped-array parameter:  @a[3] / @a[3;3]  — skip the shape (parse-only)
        if (isKind(Tok::LBracket) && !cur().spaceBefore) {
            int depth = 0;
            do { if (isKind(Tok::LBracket)) depth++; else if (isKind(Tok::RBracket)) depth--; advance(); }
            while (depth > 0 && !isKind(Tok::End));
        }
        if (matchOp("?")) p.optional = true;
        else if (matchOp("!")) p.required = true;
        // invocant marker:  method m ($self: $arg)  — ':' separates invocant from rest
        if (isOp(":")) { advance(); p.invocant = true; params.push_back(std::move(p)); continue; }
        // where / is / returns / of trait clauses
        while (isIdent("where") || isIdent("is") || isIdent("returns") || isIdent("of") || isIdent("of")) {
            std::string trait = advance().text;
            if (trait == "where") p.whereExpr = parseExpr(BP_ASSIGN + 1); // stop before the `= default`
            else if (!isKind(Tok::Comma) && !isKind(Tok::RParen) && !isKind(Tok::End) && !isOp("=")) {
                if (trait == "is" && (isIdent("rw") || isIdent("copy"))) p.isRw = (cur().text == "rw");
                advance(); // the trait word (rw/copy/encoded/…)
                // a parenthesised trait argument: `is encoded('utf8')` — skip it
                if (isKind(Tok::LParen)) {
                    int d = 0;
                    do { if (isKind(Tok::LParen)) d++; else if (isKind(Tok::RParen)) d--; advance(); } while (d > 0 && !isKind(Tok::End));
                }
            }
        }
        if (matchOp("=")) p.defaultVal = parseExpr(BP_ASSIGN);
        // `is rw` cannot combine with a default value (X::Trait::Invalid):
        // an rw param must bind a writable container, a default is a fresh value
        if (p.isRw && p.defaultVal)
            error("Cannot use trait 'is rw' on a parameter with a default value");
        { // trailing `#= description` on this param's line (or continued next line)
            auto di = declPod_.find(cur().line);
            if (di == declPod_.end() && pos_ > 0) di = declPod_.find(toks_[pos_ - 1].line);
            if (di != declPod_.end()) p.pod = di->second;
            if (p.pod.empty()) p.pod = leadingPodFor(cur().line); // `#|` above the param
        }
        params.push_back(std::move(p));
        if (matchOp("-->")) { // return type — remember the name; skip the rest to end of signature
            if (isKind(Tok::Ident) && (cur().text == "True" || cur().text == "False" || cur().text == "Nil"))
                sigRetLiteral_ = parsePrimary(); // `--> True` : a literal Bool/Nil return value
            else if (isKind(Tok::Ident)) sigRetType_ = cur().text;
            else if (isKind(Tok::IntLit) || isKind(Tok::NumLit) ||
                     isKind(Tok::StrLit) || isKind(Tok::StrInterp))
                sigRetLiteral_ = parsePrimary(); // `($n --> 99)`: literal return value
            int depth = 0;
            while (!isKind(Tok::End)) {
                if (depth == 0 && (isKind(Tok::RParen) || isKind(Tok::Semicolon) ||
                                   isKind(closeTok))) break; // pointy sigs close at `{`
                if (isKind(Tok::LParen) || isKind(Tok::LBracket)) depth++;
                else if (isKind(Tok::RParen) || isKind(Tok::RBracket)) depth--;
                advance();
            }
            break;
        }
        // `,` or `;` (multi-frame separator, e.g. `$;$ = Str`) both separate params
        if (!matchKind(Tok::Comma) && !matchKind(Tok::Semicolon)) break;
    }
    // some param branches break out before the in-loop handler sees a trailing
    // `--> Type` (e.g. `-> \x --> Int { }`) — catch it here
    if (matchOp("-->")) {
        if (isKind(Tok::Ident) && (cur().text == "True" || cur().text == "False" || cur().text == "Nil"))
                sigRetLiteral_ = parsePrimary(); // `--> True` : a literal Bool/Nil return value
            else if (isKind(Tok::Ident)) sigRetType_ = cur().text;
        else if (isKind(Tok::IntLit) || isKind(Tok::NumLit) ||
                 isKind(Tok::StrLit) || isKind(Tok::StrInterp))
            sigRetLiteral_ = parsePrimary();
        int depth = 0;
        while (!isKind(Tok::End)) {
            if (depth == 0 && (isKind(Tok::RParen) || isKind(Tok::Semicolon) ||
                               isKind(closeTok))) break;
            if (isKind(Tok::LParen) || isKind(Tok::LBracket)) depth++;
            else if (isKind(Tok::RParen) || isKind(Tok::RBracket)) depth--;
            advance();
        }
    }
    // X::Parameter::WrongOrder — required positionals come first, then
    // optional positionals, then variadics; positionals cannot follow nameds.
    {
        std::string seen; // last disqualifying group: optional / variadic / named
        for (const auto& p : params) {
            if (p.invocant || p.sigil == '|' || p.sigil == '\\') continue;
            if (p.named) { if (!p.slurpy) seen = "named"; continue; }
            if (p.slurpy) { if (p.sigil != '%') seen = "variadic"; continue; }
            bool opt = p.optional || p.defaultVal != nullptr;
            if (!opt && !seen.empty())
                throw ParseError("Cannot put required parameter " + p.name +
                                 " after " + seen + " parameters", cur().line,
                                 "X::Parameter::WrongOrder",
                                 {{"misplaced", "required"}, {"after", seen}});
            if (opt && seen == "variadic")
                throw ParseError("Cannot put optional positional parameter " + p.name +
                                 " after variadic parameters", cur().line,
                                 "X::Parameter::WrongOrder",
                                 {{"misplaced", "optional positional"}, {"after", seen}});
            if (opt && seen.empty()) seen = "optional";
        }
    }
    return params;
}

std::vector<Param> Parser::parsePointyParams() {
    // pointy blocks share the full signature grammar (types, coercions, slurpies,
    // named params, destructuring, traits) — the body brace ends the signature
    return parseSignature(Tok::LBrace);
}

StmtPtr Parser::parseSub(bool isMulti, bool isProto) {
    // 'sub' already consumed by caller
    auto s = std::make_unique<SubDecl>();
    s->pod = leadingPodFor(pos_ > 0 ? toks_[pos_ - 1].line : cur().line); // `#|` above the decl
    if (s->pod.empty()) // trailing `#=` run on/below the decl line
        s->pod = trailingPodFor(pos_ > 0 ? toks_[pos_ - 1].line : cur().line);
    s->isMulti = isMulti;
    s->isProto = isProto;
    std::string declInfix; // set when this is an `infix:<…>` declaration (for precedence traits)
    if (isOp("!")) advance(); // private method `method !name` — stored under its bare name
    if (isKind(Tok::Ident)) s->name = advance().text;
    else if (isKind(Tok::Var)) s->name = advance().text; // &-name
    // trait handler declaration: sub trait_mod:<is>(…) — name keeps the angle form
    // an unknown extension category (`sub twigil:<@>`) cannot be added
    static const std::set<std::string> kCats = {
        "infix", "prefix", "postfix", "circumfix", "postcircumfix", "trait_mod", "term"};
    if (!s->name.empty() && !kCats.count(s->name) && isOp(":") &&
        !peek().spaceBefore &&
        peek().kind == Tok::Op && (peek().text == "<" || peek().text == "<<"))
        throw ParseError("Cannot add tokens of category '" + s->name + "'",
                         cur().line, "X::Syntax::Extension::Category",
                         {{"category", s->name}});
    if (s->name == "trait_mod" && isOp(":")) {
        advance(); // :
        std::vector<std::string> w;
        if (isOp("<")) { advance(); w = readAngleWords(">"); }
        s->name += ":<" + (w.empty() ? std::string() : w[0]) + ">";
    }
    // operator declaration: sub infix:<avg> / prefix:<§> / postfix:<²>
    if ((s->name == "infix" || s->name == "prefix" || s->name == "postfix" ||
         s->name == "circumfix" || s->name == "postcircumfix") && isOp(":")) {
        std::string cat = s->name;
        advance(); // :
        if (isIdent("sym")) { // `sub infix:sym<op>` spelling
            advance();
            if (isOp("<")) {
                advance();
                std::vector<std::string> sw = readAngleWords(">");
                std::string sym = sw.empty() ? "" : sw[0];
                if (sym.empty() || sym.find_first_not_of(" \t") == std::string::npos)
                    throw ParseError("Null operator is not allowed", cur().line,
                                     "X::Syntax::Extension::Null", {});
                s->name = cat + ":<" + sym + ">";
            }
        }
        std::vector<std::string> w;
        if (isOp("<")) { advance(); w = readAngleWords(">"); }
        else if (cur().kind == Tok::Op && cur().text.size() > 1 && cur().text[0] == '<' &&
                 cur().text.compare(0, 2, "<<") != 0) {
            // fused op token: `sub infix:<=>` lexes `<=>` whole — strip the
            // leading '<' and let readAngleWords find the close
            toks_[pos_].text = cur().text.substr(1);
            w = readAngleWords(">");
        }
        else if (isOp("<<")) { advance(); w = readAngleWords(">>"); } // sub infix:<<M>>
        else if (cur().kind == Tok::Op && cur().text.size() > 4 &&
                 cur().text.compare(0, 2, "<<") == 0 &&
                 cur().text.compare(cur().text.size() - 2, 2, ">>") == 0) {
            // the lexer folded `<<M>>` into a single hyper-metaop token — unwrap it
            std::string tok = advance().text;
            w.push_back(tok.substr(2, tok.size() - 4));
        }
        else if (isOp("\xC2\xAB")) { advance(); w = readAngleWords("\xC2\xBB"); }
        std::string opname = w.empty() ? "" : w[0];
        if ((cat == "circumfix" || cat == "postcircumfix") && w.size() >= 2) {
            // two bracket words: `circumfix:<⟦ ⟧>` — name carries both, open→close registered
            s->name = cat + ":<" + w[0] + " " + w[1] + ">";
            if (cat == "circumfix") regMap('c', userCircumfix_, w[0], w[1]);
            else regMap('C', userPostcircumfix_, w[0], w[1]);
        } else if (!opname.empty()) {
            s->name = cat + ":<" + opname + ">";
            if (cat == "infix") { regInfix(opname, BP_ADD); declInfix = opname; } // default precedence; traits may adjust
            else if (cat == "prefix") regSet('p', userPrefix_, opname);
            else if (cat == "postfix") regSet('P', userPostfix_, opname);
        }
    }
    // proto-regex/method candidate suffix: `method foo:sym<bar>` / `token foo:sym«bar»`.
    // The :sym<…> adverb is part of the name (canonicalised to the angle form) so an
    // action method matches the grammar candidate it acts on.
    if (isOp(":") && peek().kind == Tok::Ident && peek().text == "sym") {
        advance(); advance(); // : sym
        std::vector<std::string> w;
        if (isOp("<")) { advance(); w = readAngleWords(">"); }
        else if (isOp("\xC2\xAB")) { advance(); w = readAngleWords("\xC2\xBB"); }
        s->name += ":sym<" + (w.empty() ? std::string() : w[0]) + ">";
    }
    if (isKind(Tok::LParen)) {
        s->hadSig = true;
        sigRetType_.clear(); sigRetLiteral_.reset(); advance(); s->params = parseSignature();
        // a `--> T` that follows a parameter (not comma-separated) is left for us
        if (isOp("-->")) { advance();
                           if (isKind(Tok::Ident) && (cur().text == "True" || cur().text == "False" || cur().text == "Nil"))
                sigRetLiteral_ = parsePrimary(); // `--> True` : a literal Bool/Nil return value
            else if (isKind(Tok::Ident)) sigRetType_ = cur().text;
                           else if (isKind(Tok::IntLit) || isKind(Tok::NumLit) ||
                                    isKind(Tok::StrLit) || isKind(Tok::StrInterp))
                               sigRetLiteral_ = parsePrimary(); // `(2 --> 1)`: literal return
                           while (!isKind(Tok::RParen) && !isKind(Tok::End)) advance(); }
        expectKind(Tok::RParen, ")");
        if (!sigRetType_.empty()) s->retType = sigRetType_; // `--> T` inside the signature
        if (sigRetLiteral_) s->retLiteral = std::move(sigRetLiteral_);
    }
    // alternative signatures sharing one body: `sub f (sig1) | (sig2) | (sig3) { … }`
    while (isOp("|") && peek().kind == Tok::LParen) {
        advance(); advance(); // '|' '('
        s->altParams.push_back(parseSignature());
        expectKind(Tok::RParen, ")");
    }
    // optional return type / traits up to block: skip until '{'
    // (note whether an `is export` trait is present — governs module visibility;
    //  capture `of T` / `returns T` / `--> T` as the return type)
    while (!isKind(Tok::LBrace) && !isKind(Tok::End) && !isKind(Tok::Semicolon)) {
        if (isIdent("export")) s->isExport = true;
        // NativeCall traits: `is native` / `is native('lib')` / `is symbol('name')`
        if (isIdent("native")) {
            s->isNative = true;
            if (peek().kind == Tok::LParen) {
                advance(); advance(); // native (
                if (isKind(Tok::StrLit) || isKind(Tok::StrInterp)) s->nativeLib = cur().text;
                int d = 1; while (d > 0 && !isKind(Tok::End)) { if (isKind(Tok::LParen)) d++; else if (isKind(Tok::RParen)) d--; advance(); }
                continue;
            }
        }
        if (isIdent("symbol") && peek().kind == Tok::LParen) {
            advance(); advance(); // symbol (
            if (isKind(Tok::StrLit) || isKind(Tok::StrInterp)) s->nativeSym = cur().text;
            int d = 1; while (d > 0 && !isKind(Tok::End)) { if (isKind(Tok::LParen)) d++; else if (isKind(Tok::RParen)) d--; advance(); }
            continue;
        }
        // precedence/associativity traits on a custom infix: `is tighter(&infix:<+>)`,
        // `is looser(&infix:<*>)`, `is equiv(&infix:<+>)`, `is assoc<left|right|non>`.
        if (!declInfix.empty() && isIdent("is") && peek().kind == Tok::Ident) {
            const std::string& trait = peek().text;
            if (trait == "tighter" || trait == "looser" || trait == "equiv") {
                advance(); advance(); // `is` `trait`
                std::string ref;
                if (isKind(Tok::LParen)) { // ( &infix:<+> )
                    advance();
                    while (!isKind(Tok::RParen) && !isKind(Tok::End)) ref += advance().text;
                    if (isKind(Tok::RParen)) advance();
                }
                // ref looks like `&infix:<+>` — pull the operator out of the <…>/«…»
                auto lt = ref.find('<'), gt = ref.rfind('>');
                if (lt == std::string::npos) { lt = ref.find("\xC2\xAB"); gt = ref.rfind("\xC2\xBB"); }
                std::string refOp = (lt != std::string::npos && gt != std::string::npos && gt > lt)
                                  ? ref.substr(lt + 1, gt - lt - 1) : ref;
                int refBp = infixBpOf(refOp);
                regInfix(declInfix, trait == "equiv" ? refBp : trait == "tighter" ? refBp + 5 : refBp - 5);
                continue;
            }
            if (trait == "assoc") {
                advance(); advance(); // `is` `assoc`
                std::string kind;
                if (isOp("<")) { advance(); auto w = readAngleWords(">"); if (!w.empty()) kind = w[0]; }
                if (kind == "right") regSet('r', userInfixRight_, declInfix);
                else userInfixRight_.erase(declInfix);
                continue;
            }
        }
        // a non-built-in `is NAME` / `is NAME(expr)` trait: captured for dispatch
        // to a user `multi sub trait_mod:<is>` at declaration time
        if (isIdent("is") && peek().kind == Tok::Ident) {
            static const std::set<std::string> builtinTraits = {
                "export", "native", "symbol", "tighter", "looser", "equiv", "assoc",
                "rw", "raw", "copy", "readonly", "pure", "default", "DEPRECATED",
                "test-assertion", "hidden-from-backtrace", "nodal", "implementation-detail",
            };
            if (!builtinTraits.count(peek().text)) {
                advance(); // is
                SubTraitSpec st; st.name = advance().text;
                if (isKind(Tok::LParen)) {
                    advance();
                    st.arg = parseExpression();
                    expectKind(Tok::RParen, ")");
                }
                s->traits.push_back(std::move(st));
                continue;
            }
        }
        if ((isIdent("of") || isIdent("returns")) && peek().kind == Tok::Ident) {
            advance(); s->retType = cur().text;
        } else if (isOp("-->") && peek().kind == Tok::Ident) {
            advance(); s->retType = cur().text;
        } else if (isOp("-->") && (peek().kind == Tok::IntLit || peek().kind == Tok::NumLit ||
                                   peek().kind == Tok::StrLit || peek().kind == Tok::StrInterp)) {
            advance(); // -->
            s->retLiteral = parsePrimary(); // `--> 1`: an empty body returns this literal
            continue;
        }
        advance();
    }
    bool hadBlock = false;
    if (isKind(Tok::LBrace)) {
        hadBlock = true;
        bool saved = inReactBlock_; inReactBlock_ = false; // whenever in a nested sub is out of scope
        routineDepth_++; // &?ROUTINE is legal inside
        auto blk = parseBlock();
        routineDepth_--;
        inReactBlock_ = saved;
        s->body = std::move(blk->stmts);
    }
    if (s->retLiteral) { // `--> 1` / `--> True` : the literal IS the return value.
        // The body still runs (for its side effects — parse-false does
        // `$pos = $pos + 5` and `--> False`), then the literal is the last
        // expression, hence the return. `return <value>` in the body is a
        // separate compile error, checked at registration via retLiteralPresent
        // (which outlives the move below).
        s->retLiteralPresent = true;
        auto es = std::make_unique<ExprStmt>();
        es->e = std::move(s->retLiteral);
        s->body.push_back(std::move(es));
    }
    // `sub MAIN (sig);` with no block: like `unit sub MAIN` — the rest of the
    // file is the body (a common PWC idiom Rakudo accepts). An explicit `{}`
    // (however empty) is a real body — no capture.
    if (!hadBlock && s->body.empty() && s->name == "MAIN" && isKind(Tok::Semicolon)) {
        advance();
        while (!isKind(Tok::End)) {
            if (matchKind(Tok::Semicolon)) continue;
            s->body.push_back(parseStatement());
        }
    }
    // A bodyless named `sub foo;` is only legal as the unit-scoped `unit sub foo;`
    // (whose body is the rest of the file). Any other bodyless declaration is a
    // compile error — you probably meant `unit sub` or forgot the block.
    if (!hadBlock && s->body.empty() && !s->name.empty() && s->name != "MAIN" &&
        !unitDecl_ && !s->isMulti && (isKind(Tok::Semicolon) || isKind(Tok::End)))
        throw ParseError("Semicolon form of 'sub' without unit scope is illegal. You probably want 'unit sub'. (X::UnitScope::Invalid)", cur().line);
    // `sub f($n) {…}(1)` — declaration immediately invoked
    if (isKind(Tok::LParen) && !cur().spaceBefore) {
        advance();
        s->immediateCall = true;
        if (!isKind(Tok::RParen)) s->immediateArgs = parseCallArgs();
        else advance();
    }
    return s;
}

StmtPtr Parser::parseSubset() {
    // 'subset' already consumed:  subset NAME [of TYPE] [where EXPR] ;
    auto sd = std::make_unique<SubsetDecl>();
    if (isKind(Tok::Ident)) sd->name = advance().text;
    if (isIdent("of")) { advance(); if (isKind(Tok::Ident)) sd->baseType = advance().text; }
    // traits may sit between the base type and the where clause:
    // `subset CookieName of Str is export where /…/` (Cro::HTTP::Cookie)
    while (isIdent("is")) { advance(); if (isKind(Tok::Ident)) advance(); }
    if (isIdent("where")) { advance(); sd->where = parseExpr(BP_ASSIGN); }
    return sd;
}

StmtPtr Parser::parseEnum() {
    // 'enum' already consumed:  enum [NAME] [of TYPE] ( <words> | (pairs) | «words» )
    auto ed = std::make_unique<EnumDecl>();
    if (isKind(Tok::Ident)) ed->name = advance().text;
    if (isIdent("of")) { advance(); if (isKind(Tok::Ident)) advance(); }
    while (isIdent("is")) { advance(); if (isKind(Tok::Ident) || isKind(Tok::Var)) advance(); }
    if (!isKind(Tok::Semicolon) && !isKind(Tok::End) && !isKind(Tok::RBrace))
        ed->values = parseExpr(BP_ASSIGN);
    return ed;
}

// Skip tokens up to the statement-terminating ';' or the enclosing block's '}',
// keeping nested ( ) [ ] { } balanced so a brace inside a skipped expression
// (e.g. a default value like %errors{$!error}) does not end the block early.
void Parser::skipToStatementEnd() {
    int depth = 0;
    while (!isKind(Tok::End)) {
        if (depth == 0 && (isKind(Tok::Semicolon) || isKind(Tok::RBrace))) break;
        if (isKind(Tok::LParen) || isKind(Tok::LBracket) || isKind(Tok::LBrace)) depth++;
        else if (isKind(Tok::RParen) || isKind(Tok::RBracket) || isKind(Tok::RBrace)) depth--;
        advance();
    }
}

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
}

void Parser::checkVirtualCallInDefault(size_t defStart) {
    // `has $.x = $.y` — a virtual call in an attribute default runs against a
    // partially constructed object; `has $.a = $^b` — a placeholder cannot
    // parameterize an attribute default. Rakudo rejects both at compile time.
    for (size_t i = defStart; i < pos_ && i < toks_.size(); i++) {
        const Token& tk = toks_[i];
        if (tk.kind != Tok::Var || tk.text.size() <= 2) continue;
        if (tk.text[1] == '.' &&
            (std::isalpha((unsigned char)tk.text[2]) || tk.text[2] == '_'))
            throw ParseError("Virtual call " + tk.text + " may not be used on "
                             "partially constructed object", tk.line,
                             "X::Syntax::VirtualCall", {{"call", tk.text}});
        if (tk.text[1] == '^' &&
            (std::isalpha((unsigned char)tk.text[2]) || tk.text[2] == '_'))
            throw ParseError("Placeholder variable " + tk.text + " may not be "
                             "used here because the surrounding block does not "
                             "take a signature", tk.line,
                             "X::Placeholder::Attribute", {{"placeholder", tk.text}});
    }
}

StmtPtr Parser::parseClass(bool isRole, bool isGrammar, bool isPackage, bool isUnit,
                           const std::string& kindKw) {
    // 'class'/'role'/'grammar'/'module'/'package' already consumed
    struct DepthGuard {
        int& d;
        DepthGuard(int& x) : d(x) { d++; }
        ~DepthGuard() { d--; }
    } classDepthGuard(classDepth_);
    auto cd = std::make_unique<ClassDecl>();
    cd->pod = leadingPodFor(pos_ > 0 ? toks_[pos_ - 1].line : cur().line); // `#|` above the decl
    if (cd->pod.empty()) // trailing `#=` run on/below the decl line
        cd->pod = trailingPodFor(pos_ > 0 ? toks_[pos_ - 1].line : cur().line);
    cd->isRole = isRole;
    cd->isGrammar = isGrammar;
    cd->isPackage = isPackage;
    if (isKind(Tok::Ident)) cd->name = advance().text;
    else if (isOp("::")) advance(); // anonymous type: `class :: does R { … }`
    // name adverbs: `module M:ver<0.19>:auth<zef:x>:api<2> { … }` — without
    // this, the `:` failed the brace check and the package took the UNIT-form
    // branch, swallowing the block up to its first `;` (JSON::Fast was
    // unloadable because of it)
    while (isOp(":") && !cur().spaceBefore && peek().kind == Tok::Ident) {
        advance();                          // :
        std::string adv = advance().text;   // ver / auth / api / …
        std::string val;
        if (isKind(Tok::QwList) && !cur().spaceBefore) val = advance().text;
        else if (isOp("<") && !cur().spaceBefore) {
            advance();
            auto ws = readAngleWords(">");
            val = ws.empty() ? std::string() : ws[0];
        }
        else if (isKind(Tok::LParen)) {     // :adverb(expr) — skip balanced
            int d = 0;
            do { if (isKind(Tok::LParen)) d++; else if (isKind(Tok::RParen)) d--; advance(); }
            while (d > 0 && !isKind(Tok::End));
        }
        if (adv == "ver") cd->ver = val;
        else if (adv == "auth") cd->auth = val;
        else if (adv == "api") cd->api = val;
    }
    if (isRole && isKind(Tok::LBracket)) {
        // parameterized role: role R[Any $desc] { } — the signature parses (so
        // the body's braces stay balanced); argument BINDING is not wired yet,
        // a body referencing the param sees an undeclared variable at runtime
        advance();
        parseSignature(Tok::RBracket);
        matchKind(Tok::RBracket);
        cd->parameterized = true;
    }
    if (isPackage) {
        // package/module: the BRACED form `module Foo { ... }` runs its body in a
        // namespace (qualified symbols). The file-scoped `unit module Foo;` form
        // declares the package but its body is the rest of the file, which runs in
        // the enclosing (global) scope so `is export` symbols stay bare-visible.
        while (isIdent("is") || isIdent("does")) { advance(); if (isKind(Tok::Ident)||isKind(Tok::Var)) advance(); }
        if (!isKind(Tok::LBrace)) {           // `module Foo;` file-scoped (unit) form
            while (!isKind(Tok::Semicolon) && !isKind(Tok::End)) advance();
            matchKind(Tok::Semicolon);
            return cd; // empty body => interpreter just registers the name
        }
        advance(); // {
        while (!isKind(Tok::RBrace) && !isKind(Tok::End)) {
            if (matchKind(Tok::Semicolon)) continue;
            if (isIdent("has") &&
                (peek().kind == Tok::Var || peek().kind == Tok::LParen ||
                 (peek().kind == Tok::Ident && peek(2).kind == Tok::Var)))
                throw ParseError("A " + kindKw + " cannot have attributes",
                                 cur().line, "X::Attribute::Package",
                                 {{"package-kind", kindKw}});
            cd->body.push_back(parseStatement());
        }
        expectKind(Tok::RBrace, "}");
        return cd;
    }
    while (isIdent("is") || isIdent("does")) {
        bool isDoes = isIdent("does");
        advance();
        if (!isDoes && isIdent("export")) { advance(); continue; } // trait, not a parent class
        // `is repr("VMHash")` — a VM-representation trait, not a parent class.
        // Recognize and skip (the arg too); our values pick their own storage.
        if (!isDoes && isIdent("repr")) {
            advance();
            if (isKind(Tok::LParen)) { int d = 0; do { if (isKind(Tok::LParen)) d++; else if (isKind(Tok::RParen)) d--; advance(); } while (d > 0 && !isKind(Tok::End)); }
            continue;
        }
        if (isKind(Tok::Ident) || isKind(Tok::Var)) {
            std::string t = advance().text;
            if (cd->parent.empty()) { cd->parent = t; cd->parentIsDoes = isDoes; }
            else if (isDoes) cd->roles.push_back(t);      // extra `does Role` — composed in
            else cd->extraParents.push_back(t);            // extra `is Class` — multiple inheritance
        }
        // skip type params / extra
        while (isKind(Tok::LBracket)) { int d = 0; do { if (isKind(Tok::LBracket)) d++; else if (isKind(Tok::RBracket)) d--; advance(); } while (d > 0 && !isKind(Tok::End)); }
    }
    bool braced = isKind(Tok::LBrace);
    if (!braced && !isUnit) { // forward declaration `class Foo;`
        while (!isKind(Tok::Semicolon) && !isKind(Tok::End)) advance();
        return cd;
    }
    // `unit class Foo;` — the remainder of the compilation unit is the class body.
    if (braced) advance();          // {
    else matchKind(Tok::Semicolon); // unit form
    typeStack_.push_back(cd->name); // enclosing type for ::?CLASS in the body
    while (!isKind(Tok::End) && (!braced || !isKind(Tok::RBrace))) {
        if (matchKind(Tok::Semicolon)) continue;
        // `also is Parent` / `also does Role` inside the body — same effect as the
        // header trait, added after the opening brace.
        if (isIdent("also") && (peek().text == "is" || peek().text == "does")) {
            advance(); // also
            while (isIdent("is") || isIdent("does")) {
                bool isDoes = isIdent("does");
                advance();
                if (isKind(Tok::Ident) || isKind(Tok::Var)) {
                    std::string t = advance().text;
                    if (cd->parent.empty()) { cd->parent = t; cd->parentIsDoes = isDoes; }
                    else if (isDoes) cd->roles.push_back(t);
                    else cd->extraParents.push_back(t);
                }
                while (isKind(Tok::LBracket)) { int d = 0; do { if (isKind(Tok::LBracket)) d++; else if (isKind(Tok::RBracket)) d--; advance(); } while (d > 0 && !isKind(Tok::End)); }
            }
            matchKind(Tok::Semicolon);
            continue;
        }
        if (isIdent("has")) {
            if (isPackage)
                throw ParseError("A " + kindKw + " cannot have attributes",
                                 cur().line, "X::Attribute::Package",
                                 {{"package-kind", kindKw}});
            advance();
            // optional type before the attribute var: `has Int $.x`, `has Int:D $.x`,
            // `has Array[Int] $.x` — consume the type name, any :D/:U/:_ smiley, and [..] params.
            std::string attrType;
            if (isKind(Tok::Ident)) {
                attrType = advance().text; // type name
                if (isOp(":") && (peek().kind == Tok::Ident)) { advance(); advance(); } // :D / :U / :_ smiley
                if (isKind(Tok::LBracket)) { int d = 0; do { if (isKind(Tok::LBracket)) d++; else if (isKind(Tok::RBracket)) d--; advance(); } while (d > 0 && !isKind(Tok::End)); }
            }
            // coercion-type attribute: `has IO::Path() $.filename` / `has Int(Cool) $.n`.
            // The declared type is the coercion TARGET; an assigned value is coerced
            // to it on construction. Consume the `(…)` and flag the attribute.
            bool attrCoerce = false;
            if (!attrType.empty() && isKind(Tok::LParen) && !cur().spaceBefore) {
                advance(); // (
                if (isKind(Tok::Ident)) advance(); // (Cool) source type — not enforced
                matchKind(Tok::RParen);
                attrCoerce = true;
            }
            if (attrType.empty() && isKind(Tok::LParen)) {
                // parenthesized attribute list: `has ( $.this, $.that, );`
                advance();
                while (!isKind(Tok::RParen) && !isKind(Tok::End)) {
                    if (isKind(Tok::Var)) {
                        std::string vn = advance().text;
                        AttrDecl a;
                        a.sigil = vn[0];
                        size_t idx = 1;
                        if (vn.size() > 1 && (vn[1] == '.' || vn[1] == '!')) { a.pub = (vn[1] == '.'); idx = 2; }
                        a.name = vn.substr(idx);
                        if (matchOp("=")) {
                            size_t defStart = pos_;
                            a.def = parseExpr(BP_ASSIGN);
                            checkVirtualCallInDefault(defStart);
                        }
                        cd->attrs.push_back(std::move(a));
                    } else advance();
                    matchKind(Tok::Comma);
                }
                matchKind(Tok::RParen);
                matchKind(Tok::Semicolon);
                continue;
            }
            if (isKind(Tok::Var)) {
                std::string vn = advance().text;
                AttrDecl a;
                a.type = attrType;
                a.coerce = attrCoerce;
                a.sigil = vn[0];
                size_t idx = 1;
                if (vn.size() > 1 && (vn[1] == '.' || vn[1] == '!')) { a.pub = (vn[1] == '.'); idx = 2; }
                a.name = vn.substr(idx);
                // traits before the default: is rw / is readonly / of Type / does Role / where EXPR / handles <...>
                while (isIdent("is") || isIdent("of") || isIdent("does") || isIdent("where") || isIdent("handles")) {
                    std::string tr = advance().text;
                    if (tr == "where") { parseExpr(BP_ASSIGN); continue; }
                    if (tr == "handles") { // handles <m1 m2> / handles "m" / handles 'm'
                        if (isOp("<")) { // bare angle list lexes as Op '<' + words
                            advance();
                            for (auto& w : readAngleWords(">")) a.handles.push_back(w);
                        }
                        else if (isKind(Tok::QwList)) {
                            std::istringstream ws(advance().text);
                            for (std::string w; ws >> w; ) a.handles.push_back(w);
                        }
                        else if (isKind(Tok::StrLit) || isKind(Tok::StrInterp) || isKind(Tok::Ident))
                            a.handles.push_back(advance().text);
                        else if (isOp("*")) { advance(); a.handles.push_back("*"); } // delegate any unknown method
                        continue;
                    }
                    if (tr == "is" && isIdent("rw")) a.rw = true;
                    if (tr == "is" && isKind(Tok::Ident)) {
                        static const std::set<std::string> containers = {
                            "Set", "SetHash", "Bag", "BagHash", "Mix", "MixHash"};
                        if (containers.count(cur().text)) a.containerIs = cur().text; // has %.a is Set
                    }
                    if (isKind(Tok::Ident) || isKind(Tok::Var)) advance();
                    if (isKind(Tok::LParen)) { int d = 0; do { if (isKind(Tok::LParen)) d++; else if (isKind(Tok::RParen)) d--; advance(); } while (d > 0 && !isKind(Tok::End)); }
                }
                if (matchOp("=") || matchOp(".=")) {
                    size_t defStart = pos_;
                    a.def = parseExpr(BP_ASSIGN);
                    checkVirtualCallInDefault(defStart);
                }
                // a newline may end the declaration (`has $.cl = { … }` with no
                // ';'): don't skip INTO the next class-body statement hunting one
                if (!(cur().kind == Tok::Ident &&
                      (cur().text == "method" || cur().text == "submethod" ||
                       cur().text == "multi" || cur().text == "proto" ||
                       cur().text == "has" || cur().text == "sub" ||
                       cur().text == "my" || cur().text == "our" ||
                       cur().text == "constant" || cur().text == "class" ||
                       cur().text == "role" || cur().text == "grammar" ||
                       cur().text == "token" || cur().text == "rule" || cur().text == "regex")))
                    skipToStatementEnd();
                cd->attrs.push_back(std::move(a));
            } else {
                skipToStatementEnd();
            }
            continue;
        }
        if (isIdent("trusts")) { // `trusts Foo;` — parsed; access is unenforced anyway
            advance();
            while (isKind(Tok::Ident) || isOp("::")) advance();
            matchKind(Tok::Semicolon);
            continue;
        }
        if (isIdent("method") || isIdent("submethod")) {
            bool sub = isIdent("submethod");
            int ln = cur().line;
            advance();
            auto s = parseSub(false);
            static_cast<SubDecl*>(s.get())->isMethod = true;
            static_cast<SubDecl*>(s.get())->isSubmethod = sub;
            if (s->line == 0) s->line = ln; // diagnostics (undeclared-attr location)
            cd->methods.push_back(std::unique_ptr<SubDecl>(static_cast<SubDecl*>(s.release())));
            continue;
        }
        if ((isIdent("multi") || isIdent("proto")) &&
            !(peek(1).text == "token" || peek(1).text == "rule" || peek(1).text == "regex")) {
            std::string multiness = cur().text;
            advance();
            bool isM = false, isSub = false;
            if (isIdent("method") || isIdent("submethod")) { isSub = isIdent("submethod"); advance(); isM = true; }
            else if (isIdent("sub")) advance();
            auto s = parseSub(true);
            if (static_cast<SubDecl*>(s.get())->name.empty())
                throw ParseError("Cannot put " + multiness + " on anonymous routine",
                                 cur().line, "X::Anon::Multi",
                                 {{"multiness", multiness},
                                  {"routine-type", isM ? "method" : "sub"}});
            static_cast<SubDecl*>(s.get())->isMethod = isM;
            static_cast<SubDecl*>(s.get())->isSubmethod = isSub;
            cd->methods.push_back(std::unique_ptr<SubDecl>(static_cast<SubDecl*>(s.release())));
            continue;
        }
        // class-body sub: lands in cd->body so it defines into the body scope the
        // methods close over (Cro::Uri's `sub remove-dot-segments` is called from
        // `method add`); was parsed-and-DISCARDED before
        if (isIdent("sub")) { advance(); cd->body.push_back(parseSub(false)); continue; }
        // grammar rules: [proto|multi] token|rule|regex NAME { <pattern> }
        {
            bool wasProtoMulti = false;
            if (isIdent("proto") || isIdent("multi")) { wasProtoMulti = true; }
            int la = wasProtoMulti ? 1 : 0;
            if ((peek(la).kind == Tok::Ident || cur().kind == Tok::Ident) &&
                (isIdent("token") || isIdent("rule") || isIdent("regex") ||
                 (wasProtoMulti && (peek(la).text == "token" || peek(la).text == "rule" || peek(la).text == "regex")))) {
                if (wasProtoMulti) advance(); // proto/multi
                if (isIdent("token") || isIdent("rule") || isIdent("regex")) {
                    std::string kind = advance().text;
                    std::string nm = isKind(Tok::Ident) ? advance().text : "";
                    std::string pat = isKind(Tok::RegexLit) ? advance().text : "";
                    if (kind == "regex" && !wasProtoMulti)
                        checkNullRegex(pat, cur().line);
                    // split a captured signature `NAME(Str $indent, …)` → name + param var names
                    std::vector<std::string> params;
                    auto lp = nm.find('(');
                    if (lp != std::string::npos) {
                        std::string sig = nm.substr(lp);
                        nm = nm.substr(0, lp);
                        for (size_t i = 0; i < sig.size(); i++)
                            if (sig[i] == '$' || sig[i] == '@' || sig[i] == '%') {
                                std::string v(1, sig[i]);
                                for (size_t j = i + 1; j < sig.size() && (std::isalnum((unsigned char)sig[j]) || sig[j] == '_' || sig[j] == '-'); j++) v += sig[j];
                                if (v.size() > 1) params.push_back(v);
                            }
                    }
                    if (!nm.empty()) cd->rules.push_back({nm, pat, kind, params});
                    continue;
                }
            }
        }
        // anything else in class body: nested classes/enums get registered globally;
        // `my` lexicals and plain expressions run in the body scope the methods
        // close over (`my $lex = ...; method m { $lex }`); the rest is discarded.
        auto st = parseStatement();
        // a bare `...`/`!!!`/`???` is the whole-type stub (`class Foo { ... }`) —
        // it must not execute at declaration time
        bool bareStub = false;
        if (st && st->kind == NK::ExprStmt) {
            Expr* e = static_cast<ExprStmt*>(st.get())->e.get();
            if (e && e->kind == NK::Call) {
                auto* c = static_cast<Call*>(e);
                bareStub = (c->name == "..." || c->name == "!!!" || c->name == "???") &&
                           c->args.empty() && !c->callee;
            }
        }
        if (bareStub) cd->isStubDecl = true; // `class A { ... }` — a redeclarable forward decl
        if (st && !bareStub &&
            (st->kind == NK::ClassDecl || st->kind == NK::EnumDecl || st->kind == NK::SubDecl ||
             st->kind == NK::VarDecl || st->kind == NK::ExprStmt ||
             st->kind == NK::UseStmt)) // `use X` inside a class body loads at declaration (URI does this after `unit class URI`)
            cd->body.push_back(std::move(st));
    }
    if (braced) expectKind(Tok::RBrace, "}");
    typeStack_.pop_back();
    return cd;
}

StmtPtr Parser::parseIf(bool isUnless) {
    auto s = std::make_unique<IfStmt>();
    s->isUnless = isUnless;
    ExprPtr cond;
    { bool sv = stmtCond_; stmtCond_ = true; cond = parseExpression(); stmtCond_ = sv; }
    if (matchOp("->")) { bool sl = matchOp("*"); if (isKind(Tok::Var)) s->thenVar = (sl ? "*" : "") + advance().text; }
    auto blk = parseBlock();
    s->branches.emplace_back(std::move(cond), std::move(blk));
    s->branchVars.push_back(s->thenVar);
    if (isUnless && (isIdent("else") || isIdent("elsif")))
        throw ParseError("\"unless\" does not take \"" + cur().text +
                         "\", please rewrite using \"if\"",
                         cur().line, "X::Syntax::UnlessElse", {});
    while (isIdent("elsif")) {
        advance();
        ExprPtr c;
        { bool sv = stmtCond_; stmtCond_ = true; c = parseExpression(); stmtCond_ = sv; }
        std::string bv;
        if (matchOp("->")) { bool sl = matchOp("*"); if (isKind(Tok::Var)) bv = (sl ? "*" : "") + advance().text; } // elsif EXPR -> $x / -> *@x
        auto b = parseBlock();
        s->branches.emplace_back(std::move(c), std::move(b));
        s->branchVars.push_back(bv);
    }
    if (isIdent("else")) {
        advance();
        if (isIdent("if")) // C-style `else if` — Raku spells it elsif
            throw ParseError("Please use 'elsif' instead of 'else if'",
                             cur().line, "X::Syntax::Malformed::Elsif", {});
        if (matchOp("->")) { bool sl = matchOp("*"); if (isKind(Tok::Var)) s->elseVar = (sl ? "*" : "") + advance().text; } // else -> $x / -> *@x
        s->elseBlock = parseBlock();
    }
    return s;
}

StmtPtr Parser::parseWhile(bool isUntil) {
    auto s = std::make_unique<WhileStmt>();
    s->isUntil = isUntil;
    { bool sv = stmtCond_; stmtCond_ = true; s->cond = parseExpression(); stmtCond_ = sv; }
    if (matchOp("->")) { if (isKind(Tok::Var)) s->var = advance().text; }
    s->body = parseBlock();
    return s;
}

StmtPtr Parser::parseFor() {
    // Perl 5 loop forms: `for my $x (...)` and C-style `for (a; b; c)`
    if (isKind(Tok::Ident) && cur().text == "my" && peek().kind == Tok::Var &&
        peek(2).kind == Tok::LParen)
        throw ParseError("This appears to be Perl 5 code", cur().line,
                         "X::Syntax::P5", {});
    if (isKind(Tok::LParen)) {
        int d = 0;
        for (size_t i = pos_; i < toks_.size(); i++) {
            if (toks_[i].kind == Tok::LParen) d++;
            else if (toks_[i].kind == Tok::RParen) { if (--d == 0) break; }
            else if (toks_[i].kind == Tok::LBrace) break;
            else if (toks_[i].kind == Tok::Semicolon && d == 1)
                throw ParseError("Unsupported use of C-style \"for (;;)\" loop; "
                                 "in Raku please use \"loop (;;)\"",
                                 cur().line, "X::Obsolete",
                                 {{"old", "C-style \"for (;;)\" loop"},
                                  {"replacement", "\"loop (;;)\""}});
        }
    }
    auto s = std::make_unique<ForStmt>();
    { bool sv = stmtCond_; stmtCond_ = true; s->list = parseExpression(); stmtCond_ = sv; }
    bool doubly = isOp("<->");
    if (matchOp("->") || matchOp("<->")) {
        if (doubly) s->rwVars = true; // `<->`: params alias the source elements
        if (isKind(Tok::LParen)) s->destructure = true; // `-> ($a,$b)`: unpack each element
        std::vector<Param> ps = parsePointyParams();
        bool anySub = false;
        for (auto& p : ps) { anySub = anySub || (bool)p.subSig; if (p.isRw) s->rwVars = true; }
        if (anySub) s->params = std::move(ps); // real signature binding (named/nested destructure)
        else for (auto& p : ps) s->vars.push_back(p.name);
    }
    s->body = parseBlock();
    return s;
}

static std::unique_ptr<Block> wrapStmt(StmtPtr s) {
    auto b = std::make_unique<Block>();
    b->stmts.push_back(std::move(s));
    return b;
}

StmtPtr Parser::applyModifiers(StmtPtr s) {
    // a `}` at end-of-line TERMINATES the statement (Rakudo's rule) — so
    // `x => {…}\n if COND {…}` starts a NEW if statement, while a modifier on
    // a continuation line after a non-brace token still attaches
    if (pos_ > 0 && toks_[pos_ - 1].kind == Tok::RBrace &&
        cur().line != toks_[pos_ - 1].line) return s;
    if (cur().kind == Tok::Ident) {
        const std::string& kw = cur().text;
        if (kw == "if" || kw == "unless") {
            advance();
            auto is = std::make_unique<IfStmt>();
            is->isUnless = (kw == "unless");
            ExprPtr cond = parseExpression();
            is->branches.emplace_back(std::move(cond), wrapStmt(std::move(s)));
            return applyModifiers(std::move(is)); // chained modifiers: `X if A for B`
        }
        if (kw == "while" || kw == "until") {
            advance();
            auto ws = std::make_unique<WhileStmt>();
            ws->isUntil = (kw == "until");
            ws->cond = parseExpression();
            ws->body = wrapStmt(std::move(s));
            return ws;
        }
        if (kw == "for") {
            advance();
            auto fs = std::make_unique<ForStmt>();
            fs->list = parseExpression();
            // `-> $i {...} for LIST` binds each topic to the pointy block's parameter(s)
            if (s->kind == NK::ExprStmt) {
                auto* es = static_cast<ExprStmt*>(s.get());
                if (es->e && es->e->kind == NK::BlockExpr) {
                    auto* be = static_cast<BlockExpr*>(es->e.get());
                    if (!be->params.empty()) {
                        bool anySub = false;
                        for (auto& p : be->params) anySub = anySub || (bool)p.subSig;
                        if (anySub) { fs->destructure = true; fs->params = std::move(be->params); }
                        else for (auto& p : be->params) fs->vars.push_back(p.name);
                        auto blk = std::make_unique<Block>();
                        blk->stmts = std::move(be->body);
                        fs->body = std::move(blk);
                        return fs;
                    }
                }
            }
            fs->modifier = true; // plain `EXPR for LIST`: no implicit block
            fs->body = wrapStmt(std::move(s));
            return fs;
        }
        if (kw == "given") {
            advance();
            auto g = std::make_unique<GivenStmt>();
            g->modifier = true; // no implicit block: a `my` in STMT leaks out
            g->topic = parseExpression();
            g->body = wrapStmt(std::move(s));
            return g;
        }
        if (kw == "when") { // STMT when X  ==  if $_ ~~ X { STMT }
            advance();
            auto bin = std::make_unique<Binary>();
            bin->op = "~~"; bin->lhs = std::make_unique<VarExpr>("$_"); bin->rhs = parseExpression();
            auto is = std::make_unique<IfStmt>();
            is->branches.emplace_back(std::move(bin), wrapStmt(std::move(s)));
            return is;
        }
        if (kw == "with" || kw == "without") {
            // STMT with X : bind $_ = X, run only if X is defined (without: if undefined)
            advance();
            auto g = std::make_unique<GivenStmt>();
            g->modifier = true; // no implicit block: a `my` in STMT leaks out
            g->defGuard = (kw == "with") ? 1 : 2;
            g->topic = parseExpression();
            g->body = wrapStmt(std::move(s));
            return g;
        }
    }
    return s;
}

StmtPtr Parser::parseStatement() {
    while (matchKind(Tok::Semicolon)) {}
    int stmtLine = cur().line;
    StmtPtr st = parseStatementImpl();
    if (st && st->line == 0) st->line = stmtLine; // stamp the source line for diagnostics
    return st;
}

StmtPtr Parser::parseStatementImpl() {
    while (matchKind(Tok::Semicolon)) {}
    // statement label:  LABEL: for ...   (ident + a colon with no space before it,
    // so `say :adverb` — space before the ':' — is a listop call, not a label)
    if (cur().kind == Tok::Ident && peek().kind == Tok::Op && peek().text == ":" &&
        !peek().spaceBefore && !kBlockKeywords.count(cur().text) &&
        // `infix:<=>(…)` is an operator-name call, not a label —
        // a tight `<`-starting op token after the colon disqualifies a label
        !(peek(2).kind == Tok::Op && !peek(2).spaceBefore && !peek(2).text.empty() &&
          peek(2).text[0] == '<')) {
        std::string lbl = cur().text;
        advance(); advance(); // consume LABEL and ':'
        auto st = parseStatement();
        if (st) st->label = lbl;
        return st;
    }
    const Token& t = cur();

    if (t.kind == Tok::Ident) {
        const std::string& kw = t.text;
        // scoped/unit declarations: my sub / our class / unit module / my constant ...
        static const std::set<std::string> declKw = {
            "sub", "method", "submethod", "multi", "proto", "class", "role",
            "grammar", "monitor", "constant", "enum", "subset", "package",
            "module", "token", "regex", "rule",
        };
        // `augment class/role/grammar Foo { … }` — reopen an existing type and
        // merge in the new methods (works on user types and built-ins like Int).
        if (kw == "augment" && peek().kind == Tok::Ident &&
            (peek().text == "class" || peek().text == "role" ||
             peek().text == "grammar" || peek().text == "monitor")) {
            advance(); // augment
            std::string what = advance().text; // class/role/grammar/monitor
            if (!monkeyActive())
                throw ParseError("augment not allowed without 'use MONKEY-TYPING'",
                                 t.line, "X::Syntax::Augment::WithoutMonkeyTyping", {});
            if (what == "role")
                throw ParseError("Cannot augment role " + cur().text +
                                 ", since roles are immutable",
                                 t.line, "X::Syntax::Augment::Illegal", {});
            // A smiley (`:D`/`:U`) or `:auth`/`:ver`/`:api` adverb on an augment
            // target is illegal — you can only augment a bare type name (S12).
            if (cur().kind == Tok::Ident && peek().kind == Tok::Op && peek().text == ":" &&
                peek(2).kind == Tok::Ident) {
                const std::string& adv = peek(2).text;
                if (adv == "D" || adv == "U" || adv == "_" || adv == "auth" || adv == "ver" || adv == "api")
                    error("cannot augment a type with a '" + adv + "' adverb");
            }
            auto st = parseClass(what == "role", what == "grammar");
            if (st->kind == NK::ClassDecl) {
                auto* acd = static_cast<ClassDecl*>(st.get());
                if (acd->name.empty())
                    throw ParseError("Cannot augment anonymous " + what, t.line,
                                     "X::Anon::Augment", {{"package-kind", what}});
                acd->isAugment = true;
            }
            return st;
        }
        // `unit class/role/grammar Foo;` — the rest of the file is the body.
        if (kw == "unit" && peek().kind == Tok::Ident &&
            (peek().text == "class" || peek().text == "role" || peek().text == "grammar")) {
            advance(); // unit
            std::string what = advance().text; // class/role/grammar
            return parseClass(what == "role", what == "grammar", false, /*isUnit=*/true);
        }
        // `has` reaches plain statement parsing either outside any class body
        // (an error) or nested in a block within one, e.g. `has` inside a
        // class-body sub, which Rakudo allows (parseClass consumes plain
        // class-body attribute declarations itself). Only diagnose at true
        // program top level: some anon-class parse routes (colon-args) also
        // deliver their body statements here.
        if (kw == "has" && classDepth_ == 0 && monkeyScopes_.size() == 1) {
            if (peek().kind == Tok::Ident && declKw.count(peek().text))
                throw ParseError("Cannot use 'has' with a " + peek().text + " declaration",
                                 t.line, "X::Declaration::Scope",
                                 {{"scope", "has"}, {"declaration", peek().text}});
            if (peek().kind == Tok::Var || peek().kind == Tok::LParen)
                throw ParseError("You cannot declare an attribute here; "
                                 "maybe you'd like a class or a role?",
                                 t.line, "X::Attribute::NoPackage", {});
        }
        if (kw == "my" || kw == "our" || kw == "state" || kw == "has" ||
            kw == "anon" || kw == "unit" || kw == "augment") {
            if (peek().kind == Tok::Ident && declKw.count(peek().text)) {
                bool wasOur = (kw == "our");
                bool wasUnit = (kw == "unit");
                advance(); // strip scope/unit; re-dispatch on the declaration keyword
                bool savedUnit = unitDecl_; unitDecl_ = wasUnit;
                StmtPtr st = parseStatement();
                unitDecl_ = savedUnit;
                // `our sub`/`our multi` — remember package scope so it installs globally.
                if (wasOur && st && st->kind == NK::SubDecl) {
                    auto* osd = static_cast<SubDecl*>(st.get());
                    if (osd->isProto) ourProtos_.insert(osd->name);
                    // candidates under an our-scoped proto are fine (roast
                    // S06-multi/type-based.t); only a bare `our multi` errs
                    if (osd->isMulti && !osd->isProto && !ourProtos_.count(osd->name))
                        throw ParseError("Cannot use 'our' with individual multi candidates",
                                         t.line, "X::Declaration::Scope::Multi",
                                         {{"scope", "our"}, {"declaration", "multi"}});
                    osd->isOur = true;
                }
                // `unit sub MAIN(…);` — no block: the REST OF THE FILE is the body
                if (wasUnit && st && st->kind == NK::SubDecl &&
                    static_cast<SubDecl*>(st.get())->body.empty()) {
                    auto* sd = static_cast<SubDecl*>(st.get());
                    matchKind(Tok::Semicolon);
                    while (!isKind(Tok::End)) {
                        if (matchKind(Tok::Semicolon)) continue;
                        sd->body.push_back(parseStatement());
                    }
                }
                return st;
            }
            // typed scoped decl:  my Int sub / my Num constant / our Str sub
            if (peek().kind == Tok::Ident && peek(2).kind == Tok::Ident && declKw.count(peek(2).text)) {
                advance(); advance(); // strip scope and type
                return parseStatement();
            }
            // typed scoped decl with a parameterized type: `my Foo::Bar[Ber::Meow] constant …`
            if (peek().kind == Tok::Ident && peek(2).kind == Tok::LBracket) {
                size_t save = pos_;
                advance(); advance(); // scope, type ident
                int d = 0; do { if (isKind(Tok::LBracket)) d++; else if (isKind(Tok::RBracket)) d--; advance(); } while (d > 0 && !isKind(Tok::End));
                if (isKind(Tok::Ident) && declKw.count(cur().text)) return parseStatement();
                pos_ = save; // not a typed scoped decl — restore
            }
        }
        if (kw == "method" || kw == "submethod") { advance(); return parseSub(false); }
        if (kw == "token" || kw == "rule" || kw == "regex") {
            std::string knd = advance().text;
            auto nr = std::make_unique<NamedRegexDecl>();
            nr->kind = knd;
            if (isKind(Tok::Ident) || isKind(Tok::Var)) nr->name = advance().text; // name
            if (isKind(Tok::RegexLit)) nr->pattern = advance().text; // lexer captured the body as a RegexLit
            else { // fallback: skip a brace body we couldn't capture
                while (!isKind(Tok::LBrace) && !isKind(Tok::End) && !isKind(Tok::Semicolon)) advance();
                if (isKind(Tok::LBrace)) { int d = 0; do { if (isKind(Tok::LBrace)) d++; else if (isKind(Tok::RBrace)) d--; advance(); } while (d > 0 && !isKind(Tok::End)); }
            }
            return nr;
        }
        if (kw == "require") {
            // `require Module` / `require Module <&sym, &sym2>` — runtime load.
            // loadModule() already publishes the module's subs globally, so the
            // import list is accepted syntactically and ignored.
            advance();
            auto u = std::make_unique<UseStmt>();
            if (isKind(Tok::Ident)) u->module = advance().text;
            while (!isKind(Tok::Semicolon) && !isKind(Tok::End)) advance(); // skip <...> import list
            matchKind(Tok::Semicolon);
            return u;
        }
        if (kw == "use" || kw == "no" || kw == "need") {
            advance();
            auto u = std::make_unique<UseStmt>();
            u->isNo = (kw == "no");
            u->isNeed = (kw == "need");
            if (!u->isNo && isKind(Tok::Ident) &&
                (cur().text == "nqp" || cur().text == "MONKEY-GUTS" || cur().text == "MONKEY"))
                useNqp_ = true; // enable the nqp:: op subset for the rest of the unit
            if (isKind(Tok::VersionLit)) { // `use v6;` / `use v6.d;` / `use v6.e.PREVIEW;`
                { std::string ver = advance().text; // VersionLit text is like "6.e" (no leading v)
                  // swallow any dotted tail the version lexer didn't take (.PREVIEW)
                  while (!isKind(Tok::Semicolon) && !isKind(Tok::End)) ver += advance().text;
                  u->module = (ver.empty() || ver[0] != 'v') ? "v" + ver : ver; } // exec() reads langRev from this
                matchKind(Tok::Semicolon);
                return u; // a version pragma loads no module — exec() only reads langRev from u->module
            }
            if (!isKind(Tok::Semicolon) && !isKind(Tok::End)) u->module = advance().text;
            if (!u->isNo && u->module.compare(0, 6, "MONKEY") == 0)
                monkeyScopes_.back() = 1; // use MONKEY-TYPING / use MONKEY (lexical)
            if (u->module == "lib" && !isKind(Tok::Semicolon) && !isKind(Tok::End) &&
                !isKind(Tok::StrLit) && !isKind(Tok::StrInterp)) {
                u->argExpr = parseExpression(); // `use lib $?FILE.IO.parent`
            } else {
                // capture first string argument, e.g. `use lib 'lib'`
                while (!isKind(Tok::Semicolon) && !isKind(Tok::End)) {
                    if (isKind(Tok::QwList)) { // `use Mod <immutable !pretty>` — EXPORT args
                        std::string ws = cur().text, w;
                        for (char c : ws) {
                            if (c == ' ' || c == '\t') { if (!w.empty()) u->importArgs.push_back(w); w.clear(); }
                            else w += c;
                        }
                        if (!w.empty()) u->importArgs.push_back(w);
                    }
                    if ((isKind(Tok::StrLit) || isKind(Tok::StrInterp)) && u->arg.empty()) u->arg = cur().text;
                    advance();
                }
            }
            matchKind(Tok::Semicolon);
            return u;
        }
        // ANONYMOUS sub at statement level (`sub { … }(args)` / `sub (…) {…}(…)`)
        // is an expression term (often immediately called), not a declaration.
        if (kw == "sub" && (peek().kind == Tok::LBrace || peek().kind == Tok::LParen)) {
            auto es = std::make_unique<ExprStmt>();
            es->e = parseExpression();
            return applyModifiers(std::move(es));
        }
        if (kw == "sub") { advance(); return parseSub(false); }
        if (kw == "multi" || kw == "proto") {
            advance();
            if (isIdent("sub")) advance();
            auto st = parseSub(true, kw == "proto");
            if (st && st->kind == NK::SubDecl &&
                static_cast<SubDecl*>(st.get())->name.empty())
                throw ParseError("Cannot put " + kw + " on anonymous routine", t.line,
                                 "X::Anon::Multi",
                                 {{"multiness", kw}, {"routine-type", "sub"}});
            return st;
        }
        if (kw == "foreach")
            throw ParseError("Unsupported use of 'foreach'; in Raku please use 'for'",
                             t.line, "X::Obsolete",
                             {{"old", "'foreach'"}, {"replacement", "'for'"}});
        if ((kw == "if" || kw == "unless" || kw == "with" || kw == "without" ||
             kw == "while" || kw == "until") &&
            peek().kind == Tok::LParen && !peek().spaceBefore &&
            peek(2).kind == Tok::RParen)
            throw ParseError("Word '" + kw + "' interpreted as '" + kw +
                             "()' function call; please use whitespace before any parentheses",
                             t.line, "X::Comp::Group",
                             {{"sorrow", "X::Syntax::KeywordAsFunction"}});
        if (kw == "if") { advance(); return parseIf(false); }
        if (kw == "unless") { advance(); return parseIf(true); }
        if (kw == "while") { advance(); return parseWhile(false); }
        if (kw == "until") { advance(); return parseWhile(true); }
        if (kw == "for") { advance(); return parseFor(); }
        if (kw == "loop") {
            advance();
            if (isKind(Tok::LParen)) {
                advance();
                // `loop ()` (no spec) and a trailing `;` after incr are malformed
                if (isKind(Tok::RParen))
                    throw ParseError("Malformed loop spec: missing semicolons",
                                     cur().line, "X::Syntax::Malformed",
                                     {{"what", "loop spec (expected 3 parts separated by semicolon)"}});
                auto ls = std::make_unique<LoopStmt>();
                if (!isKind(Tok::Semicolon)) ls->init = parseExpression();
                expectKind(Tok::Semicolon, ";");
                if (!isKind(Tok::Semicolon)) ls->cond = parseExpression();
                expectKind(Tok::Semicolon, ";");
                if (!isKind(Tok::RParen)) ls->incr = parseExpression();
                if (isKind(Tok::Semicolon))
                    throw ParseError("Malformed loop spec: too many semicolons",
                                     cur().line, "X::Syntax::Malformed",
                                     {{"what", "loop spec: unexpected trailing semicolon"}});
                expectKind(Tok::RParen, ")");
                ls->body = parseBlock();
                return ls;
            }
            auto ws = std::make_unique<WhileStmt>();
            ws->cond = std::make_unique<BoolLit>(true);
            ws->body = parseBlock();
            return ws;
        }
        if (kw == "given" || kw == "with" || kw == "without" || kw == "orwith" || kw == "orwithout") {
            bool isWith = (kw == "with" || kw == "orwith");
            bool isWithout = (kw == "without" || kw == "orwithout");
            advance();
            auto g = std::make_unique<GivenStmt>();
            g->defGuard = isWith ? 1 : isWithout ? 2 : 0;
            { bool sv = stmtCond_; stmtCond_ = true; g->topic = parseExpression(); stmtCond_ = sv; }
            if (isOp("->") || isOp("<->")) { // `given X -> $y is copy { }`
                advance();
                auto ps = parsePointyParams();
                if (!ps.empty() && !ps[0].name.empty()) g->var = ps[0].name; // "$_" too: marks an explicit binder
            }
            g->body = parseBlock();
            // orwith/orwithout chain:  with A {} orwith B {}  ==  with A {} else { with B {} }
            if (g->defGuard && (isIdent("orwith") || isIdent("orwithout"))) {
                auto blk = std::make_unique<Block>();
                blk->stmts.push_back(parseStatement());
                g->hasElse = true; g->elseBody = std::move(blk);
                return g;
            }
            g->hasElse = (g->defGuard && isIdent("else"));
            if (g->hasElse) {
                advance();
                if (isOp("->") || isOp("<->")) { // `else -> $pos { }` binds the topic
                    advance();
                    auto ps = parsePointyParams();
                    if (!ps.empty() && !ps[0].name.empty()) g->elseVar = ps[0].name;
                }
                g->elseBody = parseBlock();
            }
            return g;
        }
        if (kw == "when") {
            advance();
            auto w = std::make_unique<WhenStmt>();
            { bool sv = stmtCond_; stmtCond_ = true; w->cond = parseExpression(); stmtCond_ = sv; }
            w->body = parseBlock();
            return w;
        }
        if (kw == "default") {
            advance();
            auto w = std::make_unique<WhenStmt>();
            w->isDefault = true;
            w->body = parseBlock();
            return w;
        }
        if (kw == "repeat") {
            advance();
            auto r = std::make_unique<RepeatStmt>();
            if (isIdent("while") || isIdent("until")) {
                r->isUntil = (cur().text == "until"); advance();
                r->cond = parseExpression();
                if (matchOp("->")) { if (isKind(Tok::Var)) advance(); } // pointy var (ignored)
                r->body = parseBlock();
            } else {
                r->body = parseBlock();
                if (isIdent("while") || isIdent("until")) {
                    r->isUntil = (cur().text == "until"); advance();
                    r->cond = parseExpression();
                }
            }
            return r;
        }
        if (kw == "return" || kw == "return-rw") {
            advance();
            auto r = std::make_unique<ReturnStmt>();
            r->isRw = (kw == "return-rw");
            if (!isKind(Tok::Semicolon) && !isKind(Tok::End) && !isKind(Tok::RBrace) &&
                cur().kind != Tok::Ident) {
                r->value = parseExpression();
            } else if ((startsTermToken(cur()) && !kBlockKeywords.count(cur().text)) ||
                       isIdent("sub") || isIdent("method") || isIdent("do") || isIdent("start") ||
                       ((isIdent("role") || isIdent("class") || isIdent("grammar")) &&
                        (peek().kind == Tok::LBrace || (peek().kind == Tok::Op && peek().text == "::")))) {
                r->value = parseExpression();
            }
            return applyModifiers(std::move(r));
        }
        if (kw == "last" || kw == "next" || kw == "redo") {
            advance();
            // optional loop label:  `last OUTER`
            std::string tgt;
            if (cur().kind == Tok::Ident && !kBlockKeywords.count(cur().text) &&
                peek().kind != Tok::Op) { tgt = cur().text; advance(); }
            StmtPtr cs;
            if (kw == "last") { auto c = std::make_unique<LastStmt>(); c->target = tgt; cs = std::move(c); }
            else if (kw == "next") { auto c = std::make_unique<NextStmt>(); c->target = tgt; cs = std::move(c); }
            else { auto c = std::make_unique<RedoStmt>(); c->target = tgt; cs = std::move(c); }
            return applyModifiers(std::move(cs));
        }
        if (kw == "class" || kw == "role" || kw == "monitor" ||
            kw == "grammar" || kw == "module" || kw == "package") {
            advance(); return parseClass(kw == "role", kw == "grammar",
                                         kw == "module" || kw == "package",
                                         false, kw);
        }
        if (kw == "subset") { advance(); return parseSubset(); }
        if (kw == "enum") { advance(); return parseEnum(); }
        if (kw == "CATCH" || kw == "CONTROL") {
            std::string which = kw;
            advance();
            auto blk = parseBlock();
            blk->isCatch = true;
            blk->phaser = which; // distinguishes CATCH from CONTROL (one of each is fine)
            return blk;
        }
        if (kw == "BEGIN" || kw == "END" || kw == "INIT" || kw == "CHECK" ||
            kw == "ENTER" || kw == "LEAVE" || kw == "FIRST" || kw == "NEXT" ||
            kw == "LAST" || kw == "KEEP" || kw == "UNDO" || kw == "PRE" ||
            kw == "POST" || kw == "DOC") {
            advance();
            auto b = std::make_unique<Block>();
            b->phaser = kw; // run-timing handled by the interpreter
            if (isKind(Tok::LBrace)) { auto blk = parseBlock(); b->stmts = std::move(blk->stmts); }
            else b->stmts.push_back(parseStatement()); // PHASER statement; — same, just a 1-statement block
            return b;
        }
    }

    if (t.kind == Tok::LBrace) {
        // A statement-leading {...} is a hash literal if it starts with a Pair
        // (key=>/:key) or a %-var; otherwise it's a block. (Empty {} stays a block.)
        const Token& a = peek(1), &b = peek(2);
        bool looksHash =
            ((a.kind == Tok::Ident || a.kind == Tok::StrLit || a.kind == Tok::StrInterp ||
              a.kind == Tok::IntLit) && b.kind == Tok::FatArrow) ||
            // colon-pair: :name / :$var / :1n / :!flag  (mirror parsePrimary's isHash)
            (a.kind == Tok::Op && a.text == ":" &&
             (b.kind == Tok::Ident || b.kind == Tok::Var || b.kind == Tok::IntLit ||
              (b.kind == Tok::Op && b.text == "!")) &&
             !(b.kind == Tok::IntLit && // `:16(...)`/`:16<...>` is a radix literal
               (peek(3).kind == Tok::LParen ||
                (peek(3).kind == Tok::Op && peek(3).text == "<")))) ||
            (a.kind == Tok::Var && !a.text.empty() && a.text[0] == '%' &&
             (b.kind == Tok::RBrace || b.kind == Tok::Comma));
        if (!looksHash) {
            // If the matching } is immediately followed (no space) by a postfix,
            // the block is an expression term — `{...}()`, `{...}.method`,
            // `{...}[0]` — not a bare-block statement. Let parseExpression handle
            // it so the postfix (and any trailing infix) applies.
            bool exprBlock = false;
            for (size_t i = pos_, depth = 0; i < toks_.size(); i++) {
                Tok k = toks_[i].kind;
                if (k == Tok::LBrace) depth++;
                else if (k == Tok::RBrace && --depth == 0) {
                    if (i + 1 < toks_.size()) {
                        const Token& nx = toks_[i + 1];
                        if (!nx.spaceBefore &&
                            (nx.kind == Tok::LParen || nx.kind == Tok::LBracket ||
                             (nx.kind == Tok::Op && nx.text == ".")))
                            exprBlock = true;
                    }
                    break;
                }
            }
            if (!exprBlock) {
                auto blk = parseBlock();
                return applyModifiers(std::move(blk));
            }
        }
        // else fall through: parseExpression -> parsePrimary builds a HashLit/block
    }

    auto es = std::make_unique<ExprStmt>();
    es->e = parseExpression();
    StmtPtr s = applyModifiers(std::move(es));
    return s;
}


// Same-scope redeclarations: a second non-multi `sub a`, a non-multi/multi
// mix, or a duplicated type name (class/role/grammar/subset in any mix) is
// X::Redeclaration, checked per parsed statement list (parse-level, so sub
// hoisting and EVAL scoping cannot confuse it).
void Parser::checkRedeclarations(const std::vector<StmtPtr>& stmts) {
    std::map<std::string, int> subs;  // 1=non-multi seen, 2=multi seen, 3=both
    std::map<std::string, int> types;
    std::vector<std::string> stubbed; // `class Foo {...}` stubs not yet completed
    int catchBlocks = 0;
    for (auto& s : stmts) {
        if (!s) continue;
        if (s->kind == NK::Block && static_cast<const Block*>(s.get())->isCatch &&
            static_cast<const Block*>(s.get())->phaser == "CATCH" &&
            ++catchBlocks > 1)
            throw ParseError("Only one CATCH block is allowed in a block", s->line,
                             "X::Phaser::Multiple", {{"block", "CATCH"}});
        // `my &a; multi a { }` — a &-sigiled lexical already owns the name
        if (s->kind == NK::ExprStmt) {
            const Expr* e = static_cast<const ExprStmt*>(s.get())->e.get();
            if (e && e->kind == NK::Assign) e = static_cast<const Assign*>(e)->target.get();
            if (e && e->kind == NK::VarExpr) {
                auto* ve = static_cast<const VarExpr*>(e);
                if (ve->declare && ve->name.size() > 1 && ve->name[0] == '&')
                    subs[ve->name.substr(1)] |= 1;
            }
            continue;
        }
        if (s->kind == NK::SubDecl) {
            auto* sd = static_cast<const SubDecl*>(s.get());
            if (sd->name.empty() || sd->isProto || sd->isMethod) continue;
            // `sub f {...}` (bare yada body) is a redeclarable forward stub
            if (sd->body.size() == 1 && sd->body[0]->kind == NK::ExprStmt) {
                const Expr* e = static_cast<const ExprStmt*>(sd->body[0].get())->e.get();
                if (e && e->kind == NK::Call) {
                    const auto* c = static_cast<const Call*>(e);
                    if ((c->name == "..." || c->name == "!!!" || c->name == "???") &&
                        c->args.empty() && !c->callee) continue;
                }
            }
            int& f = subs[sd->name];
            int bit = sd->isMulti ? 2 : 1;
            if ((f & 1) && bit == 1)
                throw ParseError("Redeclaration of routine '" + sd->name + "'", sd->line,
                                 "X::Redeclaration", {{"symbol", sd->name}, {"what", "routine"}});
            if ((f && bit == 1) || ((f & 1) && bit == 2))
                throw ParseError("Redeclaration of routine '" + sd->name + "' (multi/only mix)", sd->line,
                                 "X::Redeclaration", {{"symbol", sd->name}, {"what", "routine"}});
            f |= bit;
        }
        else if (s->kind == NK::ClassDecl) {
            auto* cd = static_cast<const ClassDecl*>(s.get());
            if (cd->name.empty() || cd->isAugment || cd->parameterized) continue;
            if (cd->isStubDecl) {
                stubbed.push_back(cd->name);
                continue;
            }
            stubbed.erase(std::remove(stubbed.begin(), stubbed.end(), cd->name),
                          stubbed.end());
            // A bare `package`/`module` is a WEAK namespace declaration: it only
            // opens the name to hold `our`-scoped symbols and may coexist with a
            // later `class`/`role`/`grammar` of the same name that refines it
            // (Cro::ResourceIdentifier does exactly this). Don't count it, and
            // don't let it trip the redeclaration check for the real type below.
            if (cd->isPackage) continue;
            if (types[cd->name]++)
                throw ParseError("Redeclaration of symbol '" + cd->name + "'", cd->line,
                                 "X::Redeclaration", {{"symbol", cd->name}});
        }
        else if (s->kind == NK::SubsetDecl) {
            auto* su = static_cast<const SubsetDecl*>(s.get());
            if (su->name.empty()) continue;
            if (types[su->name]++)
                throw ParseError("Redeclaration of symbol '" + su->name + "'", su->line,
                                 "X::Redeclaration", {{"symbol", su->name}});
        }
    }
    if (!stubbed.empty()) {
        // a stub naming a `use`d module is a redeclaration hint, not a promise
        std::set<std::string> used;
        for (auto& s : stmts)
            if (s && s->kind == NK::UseStmt)
                used.insert(static_cast<const UseStmt*>(s.get())->module);
        std::string names;
        for (auto& n : stubbed)
            if (!used.count(n)) { if (!names.empty()) names += " "; names += n; }
        if (!names.empty())
            throw ParseError("The following packages were stubbed but not defined: " +
                             names, stmts.empty() ? 0 : stmts.back()->line,
                             "X::Package::Stubbed", {{"packages", names}});
    }
}

Program Parser::parseProgram() {
    Program prog;
    while (!isKind(Tok::End)) {
        if (matchKind(Tok::Semicolon)) continue;
        prog.stmts.push_back(parseStatement());
        if (!matchKind(Tok::Semicolon)) enforceStmtSep();
    }
    checkRedeclarations(prog.stmts);
    return prog;
}

// After a statement with no `;`: the next token must start a new line or follow
// a block's closing brace — `$obj.doit ()` is "two terms in a row" (Confused).
// Enforced only in EVAL'd snippets (strictSep_): whole test files keep the
// tolerant statement-split behavior so one bad line cannot kill their TAP.
void Parser::enforceStmtSep() {
    if (!strictSep_) return;
    if (isKind(Tok::End) || isKind(Tok::RBrace) || pos_ == 0) return;
    const Token& pv = toks_[pos_ - 1];
    if (pv.kind != Tok::RBrace && pv.kind != Tok::Semicolon && cur().line == pv.line)
        throw ParseError("Two terms in a row (missing semicolon?)", cur().line);
}


// ---- nqp:: compatibility subset (docs/dev/MODULE-FINDINGS.md #4b) ----------
// Constants resolve to plain IntLits at parse time; values follow MoarVM's
// tables and are only consumed by our own nqp ops, so self-consistency is
// what matters.
bool Parser::nqpConstValue(const std::string& name, long long& out) {
    static const std::map<std::string, long long> k = {
        {"CCLASS_UPPERCASE", 1},   {"CCLASS_LOWERCASE", 2},
        {"CCLASS_ALPHABETIC", 4},  {"CCLASS_NUMERIC", 8},
        {"CCLASS_HEXADECIMAL", 16},{"CCLASS_WHITESPACE", 32},
        {"CCLASS_PRINTING", 64},   {"CCLASS_BLANK", 256},
        {"CCLASS_CONTROL", 512},   {"CCLASS_PUNCTUATION", 1024},
        {"CCLASS_ALPHANUMERIC", 2048}, {"CCLASS_NEWLINE", 4096},
        {"CCLASS_WORD", 8192},
        {"NORMALIZE_NONE", 0}, {"NORMALIZE_NFC", 1}, {"NORMALIZE_NFD", 2},
        {"NORMALIZE_NFKC", 3}, {"NORMALIZE_NFKD", 4},
    };
    auto it = k.find(name);
    if (it == k.end()) return false;
    out = it->second;
    return true;
}

// Turn `nqp::op(args)` into its AST: nqp::if/unless become native Ternaries
// (lazy by construction); the loop/sequence forms become NqpOp nodes whose
// evaluator controls its own argument evaluation; leaf ops become eager NqpOp
// nodes. Unknown ops return null and stay ordinary Calls (loud runtime error).
ExprPtr Parser::makeNqpOp(const std::string& op, std::vector<ExprPtr>& args) {
    auto nilTerm = [] { return std::make_unique<NameTerm>("Nil"); };
    if (op == "if" && (args.size() == 2 || args.size() == 3)) {
        auto t = std::make_unique<Ternary>();
        t->cond = std::move(args[0]);
        t->then = std::move(args[1]);
        t->els  = args.size() == 3 ? std::move(args[2]) : ExprPtr(nilTerm());
        return t;
    }
    if (op == "unless" && (args.size() == 2 || args.size() == 3)) {
        auto t = std::make_unique<Ternary>();
        t->cond = std::move(args[0]);
        t->then = args.size() == 3 ? std::move(args[2]) : ExprPtr(nilTerm());
        t->els  = std::move(args[1]);
        return t;
    }
    static const std::map<std::string, NqpOpc> k = {
        {"stmts", NqpOpc::Stmts}, {"while", NqpOpc::While},
        {"until", NqpOpc::Until}, {"ifnull", NqpOpc::IfNull},
        {"iseq_i", NqpOpc::IseqI}, {"isne_i", NqpOpc::IsneI},
        {"islt_i", NqpOpc::IsltI}, {"isle_i", NqpOpc::IsleI},
        {"isge_i", NqpOpc::IsgeI}, {"isgt_i", NqpOpc::IsgtI},
        {"add_i", NqpOpc::AddI},   {"sub_i", NqpOpc::SubI},
        {"mul_i", NqpOpc::MulI},   {"bitand_i", NqpOpc::BitandI},
        {"ordat", NqpOpc::Ordat},  {"eqat", NqpOpc::Eqat},
        {"substr", NqpOpc::Substr},{"chars", NqpOpc::Chars},
        {"concat", NqpOpc::Concat},{"join", NqpOpc::Join},
        {"index", NqpOpc::Index},  {"chr", NqpOpc::Chr},
        {"strfromcodes", NqpOpc::StrFromCodes}, {"strtocodes", NqpOpc::StrToCodes},
        {"findnotcclass", NqpOpc::FindNotCClass}, {"iscclass", NqpOpc::IsCClass},
        {"list", NqpOpc::List}, {"list_i", NqpOpc::ListI}, {"list_s", NqpOpc::ListS},
        {"elems", NqpOpc::Elems}, {"atpos", NqpOpc::Atpos}, {"atpos_i", NqpOpc::AtposI},
        {"bindpos", NqpOpc::Bindpos}, {"bindpos_i", NqpOpc::BindposI},
        {"push", NqpOpc::Push}, {"push_i", NqpOpc::PushI}, {"push_s", NqpOpc::PushS},
        {"pop_s", NqpOpc::PopS}, {"shift_i", NqpOpc::ShiftI}, {"splice", NqpOpc::Splice},
        {"hash", NqpOpc::Hash}, {"bindkey", NqpOpc::Bindkey},
        {"create", NqpOpc::Create}, {"istype", NqpOpc::Istype},
        {"getattr", NqpOpc::Getattr}, {"bindattr", NqpOpc::Bindattr},
        {"p6bindattrinvres", NqpOpc::P6BindAttrInvRes},
        {"p6scalarwithvalue", NqpOpc::P6ScalarWithValue},
        {"null", NqpOpc::Null}, {"isnanorinf", NqpOpc::IsNanOrInf},
    };
    auto it = k.find(op);
    if (it == k.end()) return nullptr;
    auto n = std::make_unique<NqpOp>(it->second);
    n->args = std::move(args);
    return n;
}

} // namespace rakupp
