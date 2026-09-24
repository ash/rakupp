#include "CNumeric.h"
#include "AsciiCtype.h"
#include "Parser.h"
#include <sys/stat.h>
#include "IntOps.h"
#include <cstdint>
#include <memory>
#include <sstream>
#include <fstream>
#include "Lexer.h"
#include "BuiltinsShared.h"
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
bool isKnownTypeName(const std::string& n); // Interpreter.cpp

bool isPragmaName(const std::string& n);  // Interpreter.cpp — `use` names with no file behind them

// Byte length of a Unicode whitespace char at s[i], or 0 if s[i] is not
// whitespace. Covers ASCII plus the multibyte forms (NEL, NBSP, OGHAM SPACE,
// the U+2000..200A run, LS/PS, NNBSP, U+205F, U+3000) — matches the lexer,
// whose skipWhitespace draws the same line off the same byte patterns.
// When `breaking` is true (word-quote splitting), the non-breaking spaces —
// NBSP (U+00A0), FIGURE SPACE (U+2007), NARROW NBSP (U+202F) — are NOT treated
// as separators, so `<a<NBSP>b>` stays a single word (matches Rakudo). This
// splits the q-family blob (`qw[…]`, `«…»`); a bare `< … >` is tokenised
// instead, and the lexer gates the same three on its `angleWords_` depth.
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
    // the junction constructors sit between the structural comparisons and
    // concatenation, with `&` tighter than `|`/`^` (S03's precedence table)
    BP_JUNC_OR = 93, BP_JUNC_AND = 96,
    BP_CONCAT = 100, BP_REPLICATE = 110, BP_ADD = 120, BP_MUL = 130, BP_POW = 140, BP_PREFIX = 150
};

static const std::unordered_set<std::string> kAssignOps = {
    "=", "+=", "-=", "*=", "/=", "~=", "%=", "**=", "//=", "||=", "&&=", "^^=", ":=",
    // container-typed assignment: `=@=` assigns with ARRAY semantics whatever the
    // target's sigil, `=%=` with Hash, `=$=` with item (Color::Names spells its
    // exported constant `my constant \COLORS is export(:colors) =%= %( … )`)
    "=$=", "=@=", "=%=",
    "div=", "mod=", "gcd=", "lcm=", // (x= xx= min= max= arrive as Ident + `=` — the wordAssign path — never as one token)
    ",=",   // the metaop over `infix:<,>`: `%h ,= 5 => 4` is `%h = %h, 5 => 4`
    "\xE2\x9A\x9B=", "\xE2\x9A\x9B+=", "\xE2\x9A\x9B-=", // atomic assigns, lowered to atomic-* calls below
};
static const std::unordered_set<std::string> kBlockKeywords = {
    "if", "unless", "while", "until", "for", "else", "elsif", "given", "when",
    "loop", "repeat", "sub", "my", "our", "state", "has", "constant", "return",
    "last", "next", "redo", "use", "no", "class", "role", "grammar", "and", "or",
    "xor", "not",
};

// Keywords whose STATEMENT reading a following `(` cancels. Raku ends a
// keyword at `<.end_keyword>`, and one thing that ends it is a paren glued to
// it: `sub if {…}; if();` calls the sub, and roast declares and calls a routine
// for each of thirty-odd keywords this way (S02-names-vars/names.t). Only the
// names listed here are affected, and only once THIS unit has declared a
// routine of that name (Parser::kwNamedSubs_) — an undeclared `if(1) {…}` is
// left to the reading, and the error, it had before.
static const std::unordered_set<std::string> kNeedsEndKeyword = {
    "if", "unless", "while", "until", "for", "foreach", "loop", "repeat",
    "given", "when", "default", "has", "multi", "proto", "only", "subset",
    // …and the scope declarators, which took `my()` for a declaration of
    // nothing and warned about the useless `()` rather than calling the sub
    "my", "our", "state", "constant",
};

// every keyword that can follow a statement as a modifier — `with` and `without`
// are deliberately NOT in kBlockKeywords (they start a term there), so the loop
// controls need their own list to keep `next without $x` from reading `without`
// as a loop label
static const std::unordered_set<std::string> kStmtModifiers = {
    "if", "unless", "while", "until", "for", "given", "when", "with", "without",
};

struct InfixInfo {
    bool valid = false;
    int lbp = 0;
    bool rightAssoc = false;
    bool isAssign = false;
    bool isComma = false;
    bool isRange = false;
    bool isMinMax = false;   // `min` / `max`: the two do not mix without parentheses
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

// A negative subscript does not index from the end in Raku — `*-1` does — so
// the literal spelling `@a[-1]` is refused outright rather than left to fail at
// run time with an X::OutOfRange nobody reads until the branch executes. The
// range form `@a[0..-1]` goes the same way, for the same reason.
//
// The rule is deliberately as narrow as Rakudo's, which tests the SOURCE TEXT of
// each dimension for `^ \s* '-' \d+ \s* $` or a trailing `'..' \s* '-' \d+ \s* $`:
// the digits must be plain decimal and glued to the minus, and nothing may
// follow. So `[- 1]`, `[-1_0]`, `[-0x10]`, `[-1+0]`, `[-1, 2]`, `[(-1)]`,
// `[0..^-1]` and `[0..-$x]` all still parse and still fail at run time, exactly
// as they do there. Narrowness is the whole point: every one of those reaches
// the same negative index, and a broader rule would refuse a program Rakudo
// runs — including the one legitimate reader of a negative subscript, a class
// whose own AT-POS accepts one. Hash subscripts are untouched: -1 is a
// perfectly good key.
//
// Called with the parse position just past the dimension, which began at `from`.
void Parser::rejectNegativeIndex(size_t from) const {
    size_t to = pos_;
    if (to < from + 2) return;
    const Token& num = toks_[to - 1];   // the digits
    const Token& neg = toks_[to - 2];   // the minus glued to them
    if (num.kind != Tok::IntLit || num.spaceBefore || num.text.empty()) return;
    for (char c : num.text)
        if (!ascii::isdigit((unsigned char)c)) return; // 1_0, 0x10, 1.0, 1e0 are not `\d+`
    if (neg.kind != Tok::Op || neg.text != "-") return;
    // `[-1]`, or a range whose endpoint it is — `..`, `...`, `^..` end in the two
    // dots the rule looks for; `..^` and `^..^` do not, and Rakudo lets those by.
    bool bare = (to - from == 2);
    bool rangeEnd = false;
    if (!bare && to - from >= 3) {
        const Token& op = toks_[to - 3];
        rangeEnd = op.kind == Tok::Op && op.text.size() >= 2 &&
                   op.text.compare(op.text.size() - 2, 2, "..") == 0;
    }
    if (!bare && !rangeEnd) return;
    std::string old = "a negative -" + num.text + " subscript to index from the end";
    std::string repl = "a function such as *-" + num.text;
    throw ParseError("Unsupported use of " + old + ". In Raku please use: " + repl,
                     num.line, "X::Obsolete", {{"old", old}, {"replacement", repl}});
}
bool Parser::matchOp(const std::string& s) { if (isOp(s)) { advance(); return true; } return false; }
bool Parser::matchKind(Tok k) { if (cur().kind == k) { advance(); return true; } return false; }
void Parser::expectKind(Tok k, const char* what) {
    if (cur().kind != k) {
        // A TERM standing where a closing bracket belongs, on a later line than
        // the token before it: the author wrote a second statement and left out
        // the separator. Rakudo names that case rather than reporting the
        // missing bracket, and it is the naming that points at the fix — a
        // two-statement `$( … )` (how a REPL sandbox wraps a code chunk) read
        // as "expected )" told the author nothing about the missing semicolon.
        if ((k == Tok::RParen || k == Tok::RBracket) && pos_ > 0 &&
            cur().line > toks_[pos_ - 1].line) {
            switch (cur().kind) {
                case Tok::Var: case Tok::Ident: case Tok::IntLit: case Tok::NumLit:
                case Tok::StrLit: case Tok::StrInterp: case Tok::VersionLit:
                    throw ParseError("Two terms in a row across lines (missing semicolon or comma?)",
                                     cur().line, "X::Comp::AdHoc", {});
                default: break;
            }
        }
        // a block was due and none came: `for 1, 2`, `for () { sub }`
        // (…but `loop { … } while 1` is a misplaced modifier, Rakudo's
        // X::Syntax::Confused: a `while`/`until` straight after a `}`)
        auto modifierAfterBlock = [&]() {
            for (size_t j = pos_; j-- > 0; ) {
                const Token& tk = toks_[j];
                if (tk.kind == Tok::Ident && (tk.text == "while" || tk.text == "until"))
                    return j > 0 && toks_[j - 1].kind == Tok::RBrace;
                if (tk.kind == Tok::Semicolon || tk.kind == Tok::LBrace || tk.kind == Tok::RBrace) return false;
            }
            return false;
        };
        if (k == Tok::LBrace && (cur().kind == Tok::End || cur().kind == Tok::RBrace) &&
            !(cur().kind == Tok::End && cur().flag) && !modifierAfterBlock()) {
            // `for 1,2,3, { say 3 }`: the block WAS there, and the expression
            // before it swallowed it — a group whose sorrow says so
            if (pos_ > 0 && toks_[pos_ - 1].kind == Tok::RBrace) {
                int d = 0; size_t j = pos_ - 1;
                for (;; j--) {
                    if (toks_[j].kind == Tok::RBrace) d++;
                    else if (toks_[j].kind == Tok::LBrace && --d == 0) break;
                    if (j == 0) break;
                }
                if (d == 0 && j > 0) {
                    std::string fn;
                    if (toks_[j - 1].kind == Tok::Ident && toks_[j].spaceBefore) fn = toks_[j - 1].text;
                    std::string sm = fn.empty() ? std::string("Expression needs parens to avoid gobbling block")
                        : "Function '" + fn + "' needs parens to avoid gobbling block (or perhaps it's a class "
                          "that's not declared or available in this scope?)";
                    std::string pw = "block (apparently claimed by " + (fn.empty() ? std::string("expression") : "'" + fn + "'") + ")";
                    throw ParseError(sm + "\nMissing " + pw, cur().line, "X::Comp::Group",
                                     {{"sorrow", "X::Syntax::BlockGobbled"}, {"sorrow-msg", sm},
                                      {"sorrow-what", fn.empty() ? std::string("expression") : fn},
                                      {"panic", "X::Syntax::Missing"}, {"panic-msg", "Missing " + pw},
                                      {"panic-what", pw}});
                }
            }
            ParseError e("Missing block", cur().line, "X::Syntax::Missing", {{"what", "block"}});
            e.atEof = cur().kind == Tok::End;   // the REPL still asks for more
            throw e;
        }
        // …the source ran out inside a block or an array composer: an unclosed
        // `{ …` is X::Syntax::Missing, an unclosed `[1,2` X::Comp::FailGoal
        if (cur().kind == Tok::End && !cur().flag && (k == Tok::RBrace || k == Tok::RBracket)) {
            ParseError e(k == Tok::RBrace ? std::string("Missing block")
                                          : std::string("Unable to parse expression in array composer; couldn't find final ']'"),
                         cur().line, k == Tok::RBrace ? "X::Syntax::Missing" : "X::Comp::FailGoal",
                         k == Tok::RBrace ? std::vector<std::pair<std::string, std::string>>{{"what", "block"}}
                                          : std::vector<std::pair<std::string, std::string>>{{"dba", "array composer"}, {"goal", "']'"}});
            e.atEof = true;
            throw e;
        }
        // …and a TERM where a closing bracket belongs, on the same line: two
        // terms in a row (`["a" "b"]`), which is what Rakudo calls it
        if ((k == Tok::RBracket || k == Tok::RParen || k == Tok::RBrace)) {
            switch (cur().kind) {
                case Tok::Var: case Tok::Ident: case Tok::IntLit: case Tok::NumLit:
                case Tok::StrLit: case Tok::StrInterp:
                    throw ParseError("Two terms in a row", cur().line, "X::Syntax::Confused",
                                     {{"reason", "Two terms in a row"}});
                default: break;
            }
        }
        error(std::string("expected ") + what);
    }
    advance();
}
void Parser::error(const std::string& msg) {
    // A tolerant lex (see Lexer::tokenize) stopped at a construct it could not
    // read and left its error on the End token; it is THE error to report.
    if (cur().kind == Tok::End && cur().flag) throw Lexer::storedLexError((size_t)cur().ival);
    // Dying ON the end token means the source simply ran out — an unclosed block,
    // paren or signature. The REPL turns that into a continuation prompt instead
    // of an error; every other caller ignores the flag.
    throw ParseError(msg + " (got '" + cur().text + "')", cur().line, cur().kind == Tok::End);
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
        // Bare `R=` — the reverse metaop on plain assignment, so `1 R= my $x`
        // is `my $x = 1`. The general form below needs a base operator between
        // the R and the `=`, which this does not have.
        if (o == "R=") {
            in.valid = true; in.lbp = BP_ASSIGN; in.rightAssoc = true; in.isAssign = true; return in;
        }
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
        // sequence op: looser than comma, so `1,3 ... 19` seeds with (1,3).
        // `^` on either end excludes that endpoint (`1 ^... 5` is 2,3,4,5).
        // the sequence op is a LIST INFIX, like Z/X: looser than comma, so
        // `'A'...'Z', 'a'...'z', 0...9` is one chained sequence whose operands
        // are comma groups, not a comma list of sequences (evalBinary walks the
        // chain and applies Rakudo's list-of-lists semantics)
        if (o == "..." || o == "...^" || o == "^..." || o == "^...^") { in.valid = true; in.lbp = BP_ZIP; return in; }
        if (o == "==>") { in.valid = true; in.lbp = BP_OR; return in; } // forward feed (left-assoc: data flows L→R)
        if (o == "<==") { in.valid = true; in.lbp = BP_OR; in.rightAssoc = true; return in; } // backward feed (right-assoc: the far-right source flows leftward)
        if (o == "<=>") { in.valid = true; in.lbp = BP_RANGE; return in; } // structural infix
        if (o == "==" || o == "!=" || o == "<" || o == "<=" || o == ">" || o == ">=" ||
            o == "~~" || o == "!~~" || o == "=:=" || o == "!=:=" || o == "===" || o == "!==" || o == "!===" ||
            o == "!=~=" ||
            o == "=~=" || o == "≅") { in.valid = true; in.lbp = BP_COMPARE; return in; }
        if (o == "&&") { in.valid = true; in.lbp = BP_ANDAND; return in; }
        if (o == "||" || o == "//" || o == "^^") { in.valid = true; in.lbp = BP_OROR; return in; }
        // junctions any/all/one: LOOSER than concatenation and arithmetic, so
        // `1 ~ 2 & 12` is `(1~2) & 12` and `2 + 2 | 4 - 1` is `4 | 3`
        if (o == "&") { in.valid = true; in.lbp = BP_JUNC_AND; return in; }
        if (o == "|" || o == "^") { in.valid = true; in.lbp = BP_JUNC_OR; return in; }
        // hyper binary metaop  >>OP>>  etc. — it takes the PRECEDENCE (and
        // associativity) of the operator inside it, exactly as Rakudo's metaop
        // does. Giving them all additive precedence made
        // `@d >>*<< @b >>**<< @e` group as `(@d >>*<< @b) >>**<< @e`, so a
        // digits-times-base-to-the-power sum came out as (3136, 40, 1) where
        // Rakudo says (448, 40, 5) — found in a Weekly Challenge solution.
        if (o.size() >= 5 && (o.substr(0, 2) == ">>" || o.substr(0, 2) == "<<") &&
            (o.substr(o.size() - 2) == ">>" || o.substr(o.size() - 2) == "<<")) {
            Token inner = t;
            inner.text = o.substr(2, o.size() - 4);
            // `>>=><<` — the fat arrow arrives here as TEXT, not as its own token
            // kind, and would otherwise fall back to additive precedence and
            // outrank the `..` on its left
            if (inner.text == "=>") inner.kind = Tok::FatArrow;
            InfixInfo base = classifyInfix(inner);
            in.valid = true;
            in.lbp = base.valid ? base.lbp : BP_ADD;
            in.rightAssoc = base.valid && base.rightAssoc && !base.isAssign;
            return in;
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
            // removed in v6.d, but they still PARSE: the diagnostic that names
            // their replacement is thrown when one is evaluated, which is where
            // Rakudo throws it and therefore where a `try` can catch it.
            "≼", "≽", "(<+)", "(>+)",
        };
        if (setCombine.count(o)) { in.valid = true; in.lbp = BP_ADD; return in; }
        if (o.size() > 1 && o.back() == '=' && setCombine.count(o.substr(0, o.size() - 1))) {
            in.valid = true; in.lbp = BP_ASSIGN; in.rightAssoc = true; in.isAssign = true; return in; // ∩= ∪= …
        }
        if (setCompare.count(o)) { in.valid = true; in.lbp = BP_COMPARE; return in; }
        // …and their NEGATED spellings (⊄ ⊈ ⊅ ⊉, lexed to `!(<)` and friends),
        // at the same precedence. The evaluator needs nothing: its generic
        // negated-operator handler applies the base op and inverts the Bool.
        if (o.size() > 1 && o[0] == '!' && setCompare.count(o.substr(1))) {
            in.valid = true; in.lbp = BP_COMPARE; return in;
        }
        if (o == "\xE2\x88\x98") { in.valid = true; in.lbp = BP_RANGE; return in; } // ∘ function composition
        return in;
    }
    if (t.kind == Tok::Ident) {
        const std::string& o = t.text;
        in.op = o;
        if (o == "eq" || o == "ne" || o == "lt" || o == "gt" || o == "le" || o == "ge" ||
            o == "eqv" || o == "before" || o == "after") { in.valid = true; in.lbp = BP_COMPARE; return in; }
        // STRUCTURAL infix — one level tighter than the chaining comparisons, which
        // is what makes `1 == 3 <=> 2` mean `1 == (3 <=> 2)`.
        if (o == "cmp" || o == "leg" || o == "unicmp" || o == "coll") {
            in.valid = true; in.lbp = BP_RANGE; return in;
        }
        if (o == "x" || o == "xx") { in.valid = true; in.lbp = BP_REPLICATE; return in; }
        if (o == "div" || o == "mod" || o == "gcd" || o == "lcm") {
            in.valid = true; in.lbp = BP_MUL; return in;
        }
        if (o == "does" || o == "but") { in.valid = true; in.lbp = BP_MUL; return in; }
        if (o == "o") { in.valid = true; in.lbp = BP_RANGE; return in; } // ASCII alias for ∘ (function composition)
        if (o == "Z" || o == "X") { in.valid = true; in.lbp = BP_ZIP; return in; } // zip / cross: list infix, looser than comma
        {   // A zip/cross carrying a SYMBOLIC inner operator, arriving as one
            // string: `[Z=>]`, `[X~]`. In ordinary infix position the two halves
            // are separate tokens and are joined further down; the bracketed
            // citation hands them over already joined. Only a symbolic inner op
            // qualifies — a word would make `Xor` a cross-or.
            if (o.size() > 1 && (o[0] == 'Z' || o[0] == 'X') &&
                !ascii::isalnum((unsigned char)o[1])) {
                Token inner = t; inner.text = o.substr(1);
                inner.kind = inner.text == "=>" ? Tok::FatArrow : Tok::Op;
                if (classifyInfix(inner).valid) { in.valid = true; in.lbp = BP_ZIP; return in; }
            }
        }
        {   // stacked zip/cross metaops: XZ / ZZ / XX (optionally with a tight op after)
            bool allZX = o.size() > 1;
            for (char c : o) if (c != 'Z' && c != 'X') { allZX = false; break; }
            if (allZX) { in.valid = true; in.lbp = BP_ZIP; return in; }
        }
        {   // Reverse metaop over a WORD-op base, which lexes as ONE identifier:
            // `Rcmp` `Rleg` `Rmin` `Rdiv`. The symbolic forms (`R-`, `R~`) are
            // handled in the operator branch, where R and the base arrive as
            // separate tokens — a word base never splits, so `4 Rcmp 5` was a
            // parse error. The reversed form sits at the BASE operator's
            // precedence, which is what reversing the operands preserves.
            static const std::map<std::string, int> kRBase = {
                {"cmp", BP_COMPARE}, {"leg", BP_COMPARE}, {"eqv", BP_COMPARE},
                {"eq", BP_COMPARE}, {"ne", BP_COMPARE}, {"lt", BP_COMPARE},
                {"gt", BP_COMPARE}, {"le", BP_COMPARE}, {"ge", BP_COMPARE},
                {"before", BP_COMPARE}, {"after", BP_COMPARE},
                {"unicmp", BP_COMPARE}, {"coll", BP_COMPARE},
                {"div", BP_MUL}, {"mod", BP_MUL}, {"gcd", BP_MUL}, {"lcm", BP_MUL},
                {"min", BP_OROR}, {"max", BP_OROR}, {"x", BP_REPLICATE}, {"xx", BP_REPLICATE},
                {"minmax", BP_ZIP},
                {"and", BP_AND}, {"andthen", BP_AND}, {"notandthen", BP_AND},
                {"or", BP_OR}, {"xor", BP_OR}, {"orelse", BP_OR},
            };
            if (o.size() > 1 && o[0] == 'R') {
                // the Rs STACK (`RRxx`, `RRRxx`) — count the run, then look the
                // base up once; `o.substr(1)` alone left `RRxx` an undeclared name
                size_t i = 0; while (i < o.size() && o[i] == 'R') i++;
                auto it = kRBase.find(o.substr(i));
                if (it != kRBase.end()) { in.valid = true; in.lbp = it->second; return in; }
                // …and a SYMBOLIC base (`RR-`, `RRR+`) takes that operator's own
                // precedence; only the ONE-R symbolic form has its own reader.
                if (i >= 1 && i < o.size() && !ascii::isalnum((unsigned char)o[i])) {
                    Token t2 = t; t2.text = o.substr(i); t2.kind = Tok::Op;
                    InfixInfo f = classifyInfix(t2);
                    if (f.valid && !f.isAssign) { f.op = o; return f; }
                }
            }
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
        // The metaops NEST and their brackets are punctuation: `R[R[R-]]` is
        // three reversals of `-` and takes `-`'s own precedence. Strip them and
        // ask again rather than enumerating every nesting.
        if (o.find('[') != std::string::npos && o.back() == ']') {
            std::string flat;
            for (char c : o) if (c != '[' && c != ']') flat += c;
            if (!flat.empty() && flat != o) {
                Token t2 = t; t2.text = flat;
                t2.kind = ascii::isalpha((unsigned char)flat[0]) ? Tok::Ident : Tok::Op;
                InfixInfo f = classifyInfix(t2);
                if (f.valid) { f.op = o; return f; }
            }
        }
        if (o == "minmax") { in.valid = true; in.lbp = BP_ZIP; return in; } // list infix
        // `min`/`max` sit at Raku's "tight or", beside `||` and `//` — not with
        // the additives. So `($n max 0) min $h - 1` clamps into `0 .. $h-1`
        // (the `- 1` binds tighter and belongs to the bound, not to the result),
        // `5 max 1 - 10` is `5 max (1 - 10)`, and `1 max 0 && 7` is
        // `1 max (0 && 7)`. At BP_ADD all three read the other way round.
        // The parens are the author's own: `max` and `min` are list-associative
        // only with themselves, so a mixed chain stays a parse error either way.
        if (o == "min" || o == "max") { in.valid = true; in.lbp = BP_OROR; in.isMinMax = true; return in; } // infix min/max
        if (o == "and" || o == "andthen" || o == "notandthen") { in.valid = true; in.lbp = BP_AND; return in; }
        if (o == "or" || o == "xor" || o == "orelse") { in.valid = true; in.lbp = BP_OR; return in; }
        // flip-flop, all eight spellings: a leading `^` excludes the evaluation that
        // switches it ON, a trailing `^` the one that switches it OFF.
        if (o == "ff"   || o == "fff"   || o == "^ff"  || o == "^fff" ||
            o == "ff^"  || o == "fff^"  || o == "^ff^" || o == "^fff^") {
            in.valid = true; in.lbp = BP_TERNARY; return in;
        }
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

// An operator declared in another file still has to be known HERE, at parse time,
// or `$c ◐ 20` is a syntax error before the module is ever loaded. Rakudo gets this
// from compiling the module during compilation of the importer; rakupp parses the
// whole program up front, so on `use Foo` we find Foo's source and read its
// operator declarations out of it — a text scan, not a parse: only
// `sub`/`multi`/`proto`/`only` followed by `<category>:<name>` counts, which keeps a
// mention in a comment or a string from registering anything.
static std::map<std::string, std::string>& embeddedModuleSources() {
    static std::map<std::string, std::string> m;
    return m;
}
void rakuppRegisterModuleSource(const std::string& name, const char* src, size_t len) {
    embeddedModuleSources()[name] = std::string(src, len);
}
const std::string* rakuppEmbeddedModuleSource(const std::string& name) {
    auto& m = embeddedModuleSources();
    auto it = m.find(name);
    return it == m.end() ? nullptr : &it->second;
}

// ---- `use lib` at PARSE time -------------------------------------------------
// The operators a module declares are harvested from its source while the file
// that `use`s it is still being parsed (scanModuleOps), and that harvest walks
// libPaths_. A program that puts its own directory on the search path — every
// dist whose tests say `use lib $*PROGRAM.sibling('lib')` — therefore had to be
// followed HERE too, or the module was found at run time and invisible at parse
// time, and its operators never registered. (BSON::Simple's suite declares
// `circumfix:<⦃ ⦄>` in t/lib and uses it on the next line.)
//
// Only spellings that name a fixed place are evaluated: a literal, and the
// `$?FILE`/`$*PROGRAM` walks. Anything computed stays a run-time-only addition,
// exactly as it was before.
static std::string usePathParent(std::string p) {
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    size_t slash = p.find_last_of('/');
    if (slash == std::string::npos) return ".";
    if (slash == 0) return "/";
    return p.substr(0, slash);
}

static bool staticUsePath(const Expr* e, const std::string& srcFile, std::string& out) {
    if (!e) return false;
    switch (e->kind) {
        case NK::StrLit:
            out = static_cast<const StrLit*>(e)->v;
            return !out.empty();
        // A double-quoted path is an interpolating string even when nothing in it
        // interpolates (`.add("t/lib")`); the constant case is still a fixed place.
        case NK::InterpStr: {
            for (auto& part : static_cast<const InterpStr*>(e)->parts) {
                if (!part || part->kind != NK::StrLit) return false;
                out += static_cast<const StrLit*>(part.get())->v;
            }
            return !out.empty();
        }
        case NK::VarExpr: {
            const std::string& n = static_cast<const VarExpr*>(e)->name;
            if (n == "$?FILE" || n == "$*PROGRAM" || n == "$*PROGRAM-NAME") {
                if (srcFile.empty() || srcFile == "-e" || srcFile == "-") return false;
                out = srcFile;
                return true;
            }
            if (n == "$*CWD") { out = "."; return true; }
            return false;
        }
        case NK::Binary: {
            auto* b = static_cast<const Binary*>(e);
            std::string l, r;
            if (b->op != "~") return false;
            if (!staticUsePath(b->lhs.get(), srcFile, l)) return false;
            if (!staticUsePath(b->rhs.get(), srcFile, r)) return false;
            out = l + r;
            return true;
        }
        case NK::MethodCall: {
            auto* m = static_cast<const MethodCall*>(e);
            if (m->meta || m->bang || m->hyper || m->methodExpr || m->mutate) return false;
            std::string base;
            if (!staticUsePath(m->inv.get(), srcFile, base)) return false;
            const std::string& name = m->method;
            // The identity steps: they change the TYPE of the path object, or
            // spell it differently, but never which directory it names.
            if (name == "IO" || name == "Str" || name == "absolute" || name == "path" ||
                name == "resolve" || name == "cleanup" || name == "self") {
                if (!m->args.empty()) return false;
                out = base;
                return true;
            }
            if (name == "parent" || name == "dirname") {
                long long up = 1;
                if (m->args.size() == 1) {
                    if (name == "dirname" || m->args[0]->kind != NK::IntLit) return false;
                    up = static_cast<const IntLit*>(m->args[0].get())->v;
                    if (up < 0 || up > 64) return false;
                }
                else if (!m->args.empty()) return false;
                out = base;
                for (long long i = 0; i < up; i++) out = usePathParent(out);
                return true;
            }
            if (name == "sibling" || name == "add" || name == "child") {
                if (m->args.size() != 1) return false;
                std::string part;
                if (!staticUsePath(m->args[0].get(), srcFile, part)) return false;
                out = (name == "sibling" ? usePathParent(base) : base) + "/" + part;
                return true;
            }
            return false;
        }
        default: return false;
    }
}

// `use lib` takes one path or a list of them (`use lib <lib t/lib>`), so the
// argument may be a list expression; an item that cannot be evaluated statically
// drops the whole addition rather than adding a half-right search path.
static bool staticUsePaths(const Expr* e, const std::string& srcFile,
                           std::vector<std::string>& out) {
    if (!e) return false;
    if (e->kind == NK::ListExpr) {
        for (auto& item : static_cast<const ListExpr*>(e)->items) {
            std::string one;
            if (!staticUsePath(item.get(), srcFile, one)) return false;
            out.push_back(one);
        }
        return true;
    }
    std::string one;
    if (!staticUsePath(e, srcFile, one)) return false;
    out.push_back(one);
    return true;
}

// The operand of a user-defined circumfix or postcircumfix is a TERM, not an
// argument list, so `key => value` written between the brackets arrives as a
// POSITIONAL Pair — the same rule parens already carry (PairExpr::parenned).
// BSON::Simple's suite is written entirely in `⦃ hello => 'world' ⦄`, and
// passing that pair as a NAMED argument left `Hash::Ordered.new(|c)` empty.
static ExprPtr circumfixOperand(ExprPtr e) {
    if (e && e->kind == NK::Pair) static_cast<PairExpr*>(e.get())->parenned = true;
    return e;
}

// The word-shaped infixes — the ones spelled as a bare identifier, which is also
// a spelling a sub can have. `startsListopArg` and `scanOpsIn` share this list:
// the first vetoes a listop argument starting with one, the second learns when a
// module declares a SUB by that name and lifts the veto for it.
bool Parser::isWordInfixName(const std::string& n) {
    static const std::set<std::string> kWordInfix = {
        "eq", "ne", "lt", "gt", "le", "ge", "cmp", "leg", "eqv", "before", "after",
        "unicmp", "coll", "x", "xx", "and", "or", "andthen", "orelse", "div", "mod",
        "gcd", "lcm", "but", "does", "min", "max", "minmax",
    };
    return kWordInfix.count(n) > 0;
}

void Parser::scanModuleOps(const std::string& module) {
    lastScanSlang_ = false;
    if (module.empty() || module[0] == 'v' || !scannedMods_.insert(module).second) return;
    // A module compiled into this binary answers before the disk is consulted.
    if (const std::string* emb = rakuppEmbeddedModuleSource(module)) {
        lastScanSlang_ = module != "Slangify" && module != "if" && module.rfind("L10N::", 0) != 0 && rakuppIsSlangSource(*emb);
        scanOpsIn(*emb, "<embedded:" + module + ">");
        return;
    }
    // A module the COMPILER answers is never loaded, so its operators can never
    // be imported and scanning it is work for nothing — 10.2 ms when the name is
    // installed, which is the case this exists to avoid. An explicit lib path
    // still wins, exactly as it does in the loader, so a checkout under -I is
    // scanned normally; the check below is directory-only on purpose, since
    // consulting the store is the expense being skipped.
    if (rakuppCompilerAnswersModule(module) &&
        !moduleFileOnPath(module, libPaths_, langRev_ >= 2)) return;
    // The SAME resolution the loader performs: the lib search path first, then
    // the installed CompUnit repositories. Searching only lib paths here meant a
    // zef-INSTALLED module was never scanned, so the operators and the sigilless
    // constants it exports stayed invisible to the file that `use`s it — which is
    // the normal case, not the exotic one. (`SPACE ~ $word` in an installed
    // Text::Utils parsed as a call to `SPACE`.)
    std::string src, srcPath;
    if (!rakuppFindModuleSource(module, libPaths_, srcPath, src, langRev_ >= 2)) return;
    if (src.empty()) return;
    // Slangify is the interface, never a slang; an L10N dist registers one too,
    // but what it carries is a keyword table, applied by the token rewrite
    // (Interpreter::applyL10NSlang) before this parser ever ran
    // `if` is answered by the parser itself (the `:if(EXPR)` pair on a later
    // `use`), so its actions-only slang is never activated — see loadModule.
    lastScanSlang_ = module != "Slangify" && module != "if" && module.rfind("L10N::", 0) != 0 && rakuppIsSlangSource(src);
    scanOpsIn(src, srcPath);
}

// A module can add a PACKAGE DECLARATOR — its own spelling of `class` — by
// putting the HOW in `EXPORTHOW::DECLARE::<name>`:
//
//     my package EXPORTHOW { package DECLARE { constant model = MetamodelX::Red::Model } }
//
// after which `model Foo { … }` declares a class whose metaobject is that HOW.
// The keyword has to be known while the importing file is PARSED, so it is read
// off the module's source the same way its operators are — see scanOpsIn, whose
// `sub infix:<…>` scan this sits beside. The alternative spelling assigns the
// stash slot directly (`EXPORTHOW::DECLARE::<model> = …`), and both are read.
// `my &infix:<plus> = sub ($a, $b) {…}` and its siblings. The operator table is
// a PARSE-time thing, and the name is right here in the declarator, so this
// registers it exactly as the `sub infix:<plus>` path does a few thousand lines
// below — same categories, same default precedence, same two-word handling for
// circumfixes. Only the spelling differs, and a module that builds its operators
// inside `sub EXPORT` can use no other.
void Parser::registerOperatorVarName(const std::string& vname) {
    if (vname.size() < 4 || vname[0] != '&') return;
    for (const char* cat : {"infix", "prefix", "postfix", "circumfix", "postcircumfix"}) {
        const std::string head = std::string("&") + cat + ":<";
        if (vname.compare(0, head.size(), head) != 0) continue;
        if (vname.back() != '>') return;
        const std::string name = vname.substr(head.size(), vname.size() - head.size() - 1);
        if (name.empty()) return;
        const std::string c1 = cat;
        if (c1 == "circumfix" || c1 == "postcircumfix") {
            // two bracket words, `circumfix:<⟦ ⟧>` — open and close
            size_t sp = name.find(' ');
            if (sp == std::string::npos) return;
            if (c1 == "circumfix") regMap('c', userCircumfix_, name.substr(0, sp), name.substr(sp + 1));
            else regMap('C', userPostcircumfix_, name.substr(0, sp), name.substr(sp + 1));
        }
        else if (c1 == "infix")   regInfix(name, BP_ADD);   // traits may adjust, as for `sub`
        else if (c1 == "prefix")  regSet('p', userPrefix_, name);
        else                      regSet('P', userPostfix_, name);
        return;
    }
}

void Parser::scanDeclaratorsIn(const std::string& src) {
    auto ident = [&](size_t& i) {
        size_t b = i;
        while (i < src.size() && (ascii::isalnum((unsigned char)src[i]) || src[i] == '_' ||
                                  src[i] == '-' || (src[i] == ':' && i + 1 < src.size() && src[i+1] == ':')))
            i += src[i] == ':' ? 2 : 1;
        return src.substr(b, i - b);
    };
    auto skipSpace = [&](size_t& i) { while (i < src.size() && ascii::isspace((unsigned char)src[i])) i++; };
    for (size_t pos = src.find("EXPORTHOW"); pos != std::string::npos;
         pos = src.find("EXPORTHOW", pos + 9)) {
        // `EXPORTHOW::DECLARE::<name>` / `EXPORTHOW::DECLARE::{'name'}` — the
        // stash-slot spelling. The HOW is whatever the assignment names.
        size_t i = pos + 9;
        if (src.compare(i, 11, "::DECLARE::") == 0) {
            i += 11;
            if (i < src.size() && src[i] == '<') {
                size_t close = src.find('>', i);
                if (close == std::string::npos) continue;
                std::string name = src.substr(i + 1, close - i - 1);
                size_t eq = src.find_first_not_of(" \t", close + 1);
                if (eq == std::string::npos || (src[eq] != '=' && src[eq] != ':')) continue;
                eq = src.find_first_of("=", eq);                  // `=` or `:=`
                if (eq == std::string::npos) continue;
                size_t vs = eq + 1; skipSpace(vs);
                std::string how = ident(vs);
                if (!name.empty() && !how.empty()) userDeclarators_[name] = how;
            }
            continue;
        }
        // `package DECLARE { constant name = HOW; … }` — the block spelling.
        size_t decl = src.find("DECLARE", pos);
        if (decl == std::string::npos) continue;
        size_t open = src.find('{', decl);
        if (open == std::string::npos) continue;
        int depth = 0; size_t end = open;
        for (; end < src.size(); end++) {
            if (src[end] == '{') depth++;
            else if (src[end] == '}' && --depth == 0) break;
        }
        std::string body = src.substr(open + 1, end > open ? end - open - 1 : 0);
        for (size_t c = body.find("constant"); c != std::string::npos; c = body.find("constant", c + 8)) {
            size_t j = c + 8;
            while (j < body.size() && ascii::isspace((unsigned char)body[j])) j++;
            size_t nb = j;
            while (j < body.size() && (ascii::isalnum((unsigned char)body[j]) || body[j] == '_' || body[j] == '-')) j++;
            std::string name = body.substr(nb, j - nb);
            while (j < body.size() && ascii::isspace((unsigned char)body[j])) j++;
            if (j >= body.size() || body[j] != '=') continue;
            j++;
            while (j < body.size() && ascii::isspace((unsigned char)body[j])) j++;
            size_t hb = j;
            while (j < body.size() && (ascii::isalnum((unsigned char)body[j]) || body[j] == '_' ||
                                       body[j] == '-' ||
                                       (body[j] == ':' && j + 1 < body.size() && body[j+1] == ':')))
                j += body[j] == ':' ? 2 : 1;
            std::string how = body.substr(hb, j - hb);
            if (!name.empty() && !how.empty()) userDeclarators_[name] = how;
        }
    }
}

void Parser::scanOpsIn(const std::string& src, const std::string& srcPath) {
    opScanned_.push_back({srcPath, src});
    // A module that declares `sub div` / `sub min` exports a name this file will
    // otherwise read as the OPERATOR at term position, and `div "txt"` then binds
    // to nothing. The name has to be known while this file is parsed, the same
    // reason the operators just below are read off the source; a declaration is
    // `sub NAME` with NAME word-infix-shaped, optionally behind multi/proto/only.
    Lexer::scanDeclaredSubNames(src, [&](const std::string& n, size_t, size_t) {
        if (isWordInfixName(n)) wordInfixSubs_.insert(n);
    });
    // …and the ones that collide with a QUOTE form (`sub tr`, `sub q`). Those are
    // decided in the lexer, which ran before this file's `use` was reached, so the
    // rest of the unit is re-lexed with them vetoed — the path a slang takes.
    {
        // An IMPORTED name is in scope for the whole importing unit, so only the
        // names matter here — where the module happened to declare them says
        // nothing about where they are visible in THIS file.
        size_t before = quoteWordSubs_.size();
        Lexer::scanQuoteWordSubNames(src, quoteWordSubs_);
        if (quoteWordSubs_.size() != before) relexForQuoteWords();
    }
    // A module that RE-EXPORTS another one's names — `use Monarch::HTML;` plus an
    // `EXPORT` sub — passes on its operators and its `sub tr`/`sub div` too, so
    // the scan has to follow the `use`. Without this, `use Monarch` left `tr { … }`
    // reading as a transliteration: the block ran, built nothing, and the table
    // came out empty with no error anywhere. `scannedMods_` stops the recursion.
    for (size_t pos = 0; pos < src.size(); ) {
        size_t eol = src.find('\n', pos);
        std::string line = src.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
        pos = eol == std::string::npos ? src.size() : eol + 1;
        size_t i = line.find_first_not_of(" \t");
        if (i == std::string::npos || line.compare(i, 4, "use ") != 0) continue;
        i += 4;
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) i++;
        size_t b = i;
        while (i < line.size() && (ascii::isalnum((unsigned char)line[i]) || line[i] == '_' || line[i] == '-' ||
                                   (line[i] == ':' && i + 1 < line.size() && line[i + 1] == ':')))
            i += line[i] == ':' ? 2 : 1;
        std::string mod = line.substr(b, i - b);
        if (mod.empty() || !ascii::isalpha((unsigned char)mod[0])) continue;
        if (mod == "lib" || mod == "strict" || mod == "v6" || mod == "nqp" || mod == "js" || mod == "JS" || mod == "Test") continue;
        bool wasSlang = lastScanSlang_;
        scanModuleOps(mod);
        lastScanSlang_ = wasSlang;   // only the module the USER named may arm a slang
    }
    // an operator spelled only in ASCII operator characters is almost certainly a
    // REDECLARATION of a built-in (`multi infix:<*>(Color, Real)`); registering it
    // as a user op would give it the default precedence and silently reshape every
    // expression in the importing file
    // `gcd` / `div` / `eq` — an operator spelled as a bare identifier, which is
    // what the classifier recognises as a WORD infix.
    auto identShapedOpName = [](const std::string& n) {
        if (n.empty() || !(ascii::isalpha((unsigned char)n[0]) || n[0] == '_')) return false;
        for (unsigned char c : n) if (!ascii::isalnum(c) && c != '_') return false;
        return true;
    };
    auto asciiOnlyOp = [](const std::string& n) {
        if (n.empty()) return true;
        for (unsigned char c : n)
            if (c > 127 || ascii::isalnum(c)) return false;
        return true;
    };
    // `sub term:<name>` is harvested the same way the operators are: a term is
    // syntax, so the file that `use`s the module has to know the name while it
    // is still being PARSED. Lingua::EN::Numbers queries its flag with the term
    // `no-commas?`, which without this read as the sub `no-commas` and then a
    // stray `?` ("Confused"). Only the name is needed — the term is a 0-ary sub
    // by the time it is called.
    {
        const std::string needle = "term:<";
        for (size_t pos = src.find(needle); pos != std::string::npos;
             pos = src.find(needle, pos + 1)) {
            size_t b = pos;
            while (b > 0 && ascii::isspace((unsigned char)src[b - 1])) b--;
            if (b > 0 && src[b - 1] == '&') { b--; while (b > 0 && ascii::isspace((unsigned char)src[b - 1])) b--; }
            size_t e = b;
            while (b > 0 && (ascii::isalnum((unsigned char)src[b - 1]) || src[b - 1] == '_')) b--;
            const std::string kw = src.substr(b, e - b);
            if (kw != "sub" && kw != "multi" && kw != "proto" && kw != "only" &&
                kw != "my" && kw != "our") continue;
            size_t close = src.find('>', pos + needle.size());
            if (close == std::string::npos) continue;
            const std::string name = src.substr(pos + needle.size(), close - pos - needle.size());
            if (!name.empty() && name.find(' ') == std::string::npos) sigilless_.insert(name);
        }
    }
    for (const char* cat : {"infix", "prefix", "postfix", "circumfix", "postcircumfix"}) {
        std::string needle = std::string(cat) + ":<";
        for (size_t pos = src.find(needle); pos != std::string::npos;
             pos = src.find(needle, pos + 1)) {
            size_t b = pos;
            while (b > 0 && ascii::isspace((unsigned char)src[b - 1])) b--;
            // `my &infix:<加> = sub ($a, $b) {…}` — the operator declared by
            // ASSIGNMENT to a code variable rather than by `sub`. A module that
            // builds its operators inside `sub EXPORT` can spell it no other way,
            // and every one of the natural-language modules does (Chinese, French,
            // Japanese, Korean, Spanish, ClassicalChinese). Step back over the `&`
            // and take the scope keyword in place of the routine keyword; the
            // importing file then knows the operator, which is the whole job of
            // this scan.
            if (b > 0 && src[b - 1] == '&') {
                b--;
                while (b > 0 && ascii::isspace((unsigned char)src[b - 1])) b--;
            }
            bool isDecl = false;
            for (const char* kw : {"sub", "multi", "proto", "only",
                                   "my", "our", "state", "has"}) {
                size_t kl = std::strlen(kw);
                if (b >= kl && src.compare(b - kl, kl, kw) == 0 &&
                    (b == kl || !ascii::isalnum((unsigned char)src[b - kl - 1]))) { isDecl = true; break; }
            }
            if (!isDecl) continue;
            size_t close = src.find('>', pos + needle.size());
            if (close == std::string::npos) continue;
            std::string name = src.substr(pos + needle.size(), close - pos - needle.size());
            if (name.empty()) continue;
            std::string c1 = cat;
            if (c1 == "circumfix" || c1 == "postcircumfix") {
                size_t sp = name.find(' ');
                if (sp == std::string::npos) continue;
                if (c1 == "circumfix") regMap('c', userCircumfix_, name.substr(0, sp), name.substr(sp + 1));
                else regMap('C', userPostcircumfix_, name.substr(0, sp), name.substr(sp + 1));
                continue;
            }
            if (c1 == "infix") {
                // An ASCII-only spelling is USUALLY a redeclaration of a built-in
                // (`multi infix:<*>(Color, Real)`), and registering that as a user
                // op would give it the default precedence and reshape every
                // expression in the file. One that is NOT a built-in, though, is a
                // genuinely new operator (`sub infix:<%%%>`) and has to be known.
                Token t; t.text = name; t.kind = Tok::Op;
                bool builtin = classifyInfix(t).valid;
                Token ti; ti.text = name; ti.kind = Tok::Ident;
                InfixInfo word = classifyInfix(ti);
                if (!builtin) builtin = word.valid;
                if (!builtin && !userInfix_.count(name)) regInfix(name, BP_ADD);
                // A WORD operator the module redeclares (`multi infix:<gcd>(Complex,
                // Complex)` in Math::NumberTheory) still has to reach the user's
                // candidates: the built-in `gcd` coerces its operands to Int, so it
                // answers 25 for `(10 + 15i) gcd 25` instead of deferring. Register it
                // at the BUILT-IN's own precedence — the file's expressions must not
                // reshape — and let the runtime fall back to the built-in when no
                // candidate takes the operands. Word-shaped only: routing every `==`
                // through a user dispatcher is what the ASCII rule above avoids.
                else if (word.valid && identShapedOpName(name) && !userInfix_.count(name))
                    regInfix(name, word.lbp);
            }
            else if (asciiOnlyOp(name)) continue;
            else if (c1 == "prefix") regSet('p', userPrefix_, name);
            else regSet('P', userPostfix_, name);
        }
    }
    scanDeclaratorsIn(src);

    // A SIGILLESS `constant` the module exports is a TERM here, not a listop:
    // `SPACE ~ $word` is a concatenation, and without this it parsed as a call to
    // `SPACE` taking `~ $word` — "Undefined routine 'SPACE'" the moment the
    // importer ran (Text::Utils imports SPACE/EMPTY from Text::Utils::Vars).
    for (size_t pos = src.find("constant"); pos != std::string::npos;
         pos = src.find("constant", pos + 8)) {
        if (pos && (ascii::isalnum((unsigned char)src[pos - 1]) || src[pos - 1] == '-' ||
                    src[pos - 1] == '_')) continue;                       // part of a longer word
        size_t i = pos + 8;
        if (i >= src.size() || !ascii::isspace((unsigned char)src[i])) continue;
        while (i < src.size() && (src[i] == ' ' || src[i] == '\t')) i++;
        if (i >= src.size() || !(ascii::isalpha((unsigned char)src[i]) || src[i] == '_')) continue;
        size_t b = i;
        while (i < src.size() && (ascii::isalnum((unsigned char)src[i]) || src[i] == '_' ||
                                  (src[i] == '-' && i + 1 < src.size() &&
                                   ascii::isalpha((unsigned char)src[i + 1])))) i++;
        std::string name = src.substr(b, i - b);
        // The initializer has to be on the same line, and between the name and
        // the `=` only traits may appear — anything else means we misread the
        // declaration (a type name, say), so leave it alone.
        size_t eq = src.find('=', i), nl = src.find('\n', i);
        if (eq == std::string::npos || (nl != std::string::npos && nl < eq)) continue;
        std::string between = src.substr(i, eq - i);
        if (between.find_first_not_of(" \t") != std::string::npos &&
            between.find("is ") == std::string::npos) continue;
        sigilless_.insert(name);
    }
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

void Parser::checkRegexBoundaries(const std::string& pattern, int line) const {
    if (langRev_ < 2) return;                       // a silent no-op before 6.e
    for (size_t i = 0; i + 2 < pattern.size(); i++) {
        if (pattern[i] != '<' || pattern[i + 1] != '|') continue;
        if (i && pattern[i - 1] == '\\') continue;   // an escaped \< is a literal
        size_t j = i + 2, k = j;
        while (k < pattern.size() && (ascii::isalnum((unsigned char)pattern[k]) || pattern[k] == '-')) k++;
        if (k >= pattern.size() || pattern[k] != '>' || k == j) continue;  // not `<|name>`
        std::string nm = pattern.substr(j, k - j);
        if (nm == "w" || nm == "c") continue;
        throw ParseError("Unrecognized regex boundary '<|" + nm + ">'. The known boundaries are "
                         "'<|w>' (word) and '<|c>' (codepoint)", line);
    }
}

// Nothing can follow an infix operator except a term, so a token that CLOSES
// the construct settles a `foo *` / `foo **` ambiguity in favour of the bare
// Whatever being the listop's argument.
static bool closesTerm(Tok k) {
    return k == Tok::Semicolon || k == Tok::Comma || k == Tok::End ||
           k == Tok::RParen || k == Tok::RBracket || k == Tok::RBrace;
}

// `--> Map()` is a COERCION, not a check: the value is coerced to Map on the
// way out, and `(a => 1, b => 2)` — a List — is a perfectly good return from
// `method exports(--> Map())`, which is how Red hands its export set over. The
// parens are kept on the recorded name; retTypeName() takes them off for
// everyone who wants the plain type.
static std::string retCoercionMark(const Token& next) {
    return (next.kind == Tok::LParen && !next.spaceBefore) ? "()" : "";
}

// A NativeCall return type keeps its parameter: `--> CArray[Str]` names the
// element type the unmarshalling needs, exactly as a PARAMETER of that type
// does (see the `keep` flag in parseSignature). Dropping it left every element
// of a returned CArray reading as the raw pointer — Geo::Hash binds a
// `char **` and got eight addresses where it expected eight geohashes. Other
// parameterised return types stay bare, as they always were.
std::string Parser::nativeRetParam(size_t identPos) const {
    const Token& id = toks_[identPos];
    if (id.text != "CArray" && id.text != "Pointer") return "";
    size_t k = identPos + 1;
    if (toks_[k].kind != Tok::LBracket || toks_[k].spaceBefore) return "";
    std::string out;
    int depth = 0;
    for (; toks_[k].kind != Tok::End; k++) {
        if (toks_[k].kind == Tok::LBracket) depth++;
        else if (toks_[k].kind == Tok::RBracket) depth--;
        out += toks_[k].text;
        if (!depth) break;
    }
    return depth ? std::string() : out;
}

// `class` / `role` / `grammar` in a position where a DECLARATION cannot follow
// is an ordinary bareword — a sigilless variable, or a call. A declaration has
// to be followed by a name, a `{` or a `::`; anything else (a `)`, a comma, an
// infix, the end of the statement) means the word is being USED.
//
// RakuAST::Utils is where this surfaced: `nqp::eqaddr(type, role)` stopped the
// argument list dead — "expected ) (got 'role')" — because the word was a
// keyword wherever it stood. The quieter half is that `say role` had been
// parsing as an ANONYMOUS ROLE DECLARATION and printing a blank line.
bool Parser::typeWordAsTerm(const Token& t) const {
    if (t.text != "class" && t.text != "role" && t.text != "grammar") return false;
    if (&t != &cur()) return false;              // peek() only speaks for the current token
    const Token& n = peek();
    if (n.kind == Tok::Ident || n.kind == Tok::LBrace) return false;
    if (n.kind == Tok::Op && n.text == "::") return false;
    return true;
}

static bool fusedQqww(const Token& t); // `<<X>>` fused by the lexer; defined beside qqwwList

bool Parser::startsTermToken(const Token& t) const {
    switch (t.kind) {
        case Tok::IntLit: case Tok::NumLit: case Tok::StrLit: case Tok::VersionLit: case Tok::StrInterp: case Tok::RegexLit: case Tok::SubstLit:
        case Tok::QwList:
        case Tok::Var: case Tok::LParen: case Tok::LBracket: case Tok::LBrace:
            return true;
        case Tok::Op:
            return t.text == "!" || t.text == "~" || t.text == "\\" || t.text == "<" ||
                   t.text == "+" || t.text == "-" || t.text == "?" || t.text == ":" ||
                   t.text == "+^" || t.text == "~^" || t.text == "?^" || // prefix bitwise/bool NOT: `f 0, +^$x`
                   t.text == "++" || t.text == "--" || // prefix incr/decr: `f 0, ++$x`
                   t.text == "\xE2\x9A\x9B" ||               // prefix ⚛ (atomic read): `f 0, ⚛$x`
                   t.text == "*" || t.text == "**" || // `*` Whatever, `**` HyperWhatever: `(1, **, 8)`
                   t.text == "->" || t.text == "<->" || t.text == "|" ||
                   t.text == "^" || // prefix `^N` (upto) after a comma: `1, ^10 .Seq` (infix ^ is impossible there)
                   t.text == "&" || // operator-as-value `&[+]` (bare `&` in term position is only `&[OP]`)
                   t.text == "." || // leading `.method` => $_.method (e.g. `1, .uc`)
                   t.text == "::" || // symbolic reference `::($name)` / `::Foo`
                   t.text == "\xE2\x88\x9E" || t.text == "\xC2\xAB" || t.text == "<<" || fusedQqww(t) || // ∞, «qw», <<qww>>
                   t.text == "$" || t.text == "@" || t.text == "%" || // contextualizers $( $[ @( %(
                   (t.text == "//" && langRev_ >= 2) || // 6.e prefix `//$x` — "is it defined"

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
                   // `not` is a loose PREFIX, so it starts a term: `say not 0` says True
                   // (it is in the keyword set only because it is also an infix-ish word)
                   t.text == "not" ||
                   // control flow is a TERM in expression position — the comma
                   // list `nqp::if(cond, (return 'x'), return '')` (paths' directory
                   // walker) has `return` as its last element, and the list stopped
                   // at it: "expected ) (got 'return')"
                   t.text == "return" || t.text == "return-rw" ||
                   t.text == "last" || t.text == "next" || t.text == "redo" ||
                   // `proto sub NAME(|) {*}` / `multi sub NAME(…) {…}` are terms too
                   // (see the routine-declaration term in parsePrefix)
                   ((t.text == "proto" || t.text == "multi") && &t == &cur() &&
                    peek().kind == Tok::Ident && (peek().text == "sub" || peek().text == "method")) ||
                   // an ANONYMOUS class/role/grammar is an expression: `is class :: {…}.new.x, …`
                   ((t.text == "class" || t.text == "role" || t.text == "grammar") && &t == &cur() &&
                    (peek().kind == Tok::LBrace || (peek().kind == Tok::Op && peek().text == "::"))) ||
                   typeWordAsTerm(t);
        default:
            return false;
    }
}
// what may begin a list-op argument (no parens; conservative on leading symbols)
bool Parser::startsListopArg(const Token& t, const std::string& lhsName) const {
    switch (t.kind) {
        case Tok::StrLit: case Tok::StrInterp:
            // a QUOTE tight against the name is not an argument: `foo'...'` and
            // `say"x"` are two terms in a row, which is what Raku calls them.
            // (A `'` between two letters joins an identifier and never reaches
            // here — `isn't` is one name.) Enforced only in EVAL'd snippets, for
            // the reason enforceStmtSep gives: a whole test file must not die
            // over one line, and an unrecognised QUOTE WORD (`qa"…"`) arrives
            // here in exactly this shape.
            return t.spaceBefore || !strictSep_;
        case Tok::IntLit: case Tok::NumLit: case Tok::VersionLit: case Tok::RegexLit: case Tok::SubstLit:
        case Tok::Var: case Tok::LParen:
            return true;
        case Tok::QwList:
            // `f <a b>` (spaced) passes the word list as arguments; TIGHT `f<a b>`
            // subscripts the call's RESULT — `config<name>` is `config()<name>`,
            // the same rule `foo[10]` follows just below. Rakudo applies it
            // whatever the sub's signature says: `withargs<a>` reports
            // "Calling withargs() will never work", i.e. a NO-argument call.
            return t.spaceBefore;
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
            // a pointy block is a listop argument (`get -> { content … }` in a Cro
            // route block) — but NOT in a statement condition, where `->` binds the
            // statement's own block (`for @a -> $x { }`)
            if (t.text == "->" || t.text == "<->") return !stmtCond_;
            // A word list is an argument only when it is SPACED off the name:
            // `is < foo bar >, exp` passes the list, while TIGHT `config<name>`
            // subscripts the call's result — `config()<name>`, which is how
            // Rakudo reads it whatever the sub's signature says (`withargs<a>`
            // reports "Calling withargs() will never work", i.e. no arguments).
            // parsePostfix owns the tight form, as it does for `foo[10]`.
            if (t.text == "<") return t.spaceBefore;
            return t.text == "!" || t.text == "~" || t.text == "\\" ||
                   t.text == ":" || t.text == "+" || t.text == "-" || t.text == "?" ||
                   t.text == "+^" || t.text == "~^" || t.text == "?^" || // prefix bitwise/bool NOT: `say +^$x`
                   t.text == "++" || t.text == "--" || // prefix incr/decr: `say 0, ++$x`
                   t.text == "\xE2\x9A\x9B" ||               // prefix ⚛ (atomic read): `say ⚛$x`
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
                   // `plan *;` — a bare Whatever argument. Infix `*` would need a
                   // term after it, and what follows closes the construct
                   // instead: `[ast-value *]` and `{ av * }` are as much an
                   // argument as `(av *)` and `av *;` are.
                   (t.text == "*" && &t == &cur() && closesTerm(peek().kind)) ||
                   t.text == "^" || // prefix `^N` (upto) as a listop arg: `flat ^15, 49`
                   // `foo **` — a bare HyperWhatever argument, on the same
                   // reasoning as the bare `*` above.
                   (t.text == "**" && closesTerm(peek().kind)) ||
                   (t.text == "|" && t.spaceBefore) || // slip first arg `run |@cmd` (space before |) — NOT infix junction `Any|Blob`
                   t.text == "!!" || // prefix boolify `say !!$x` (`!!` never starts a bare term otherwise)
                   // 6.e prefix `//`: `say //$x`. Infix `//` cannot appear here —
                   // a listop argument position has no left operand — so the
                   // spelling is unambiguous once the revision allows it.
                   (t.text == "//" && langRev_ >= 2) ||
                   (t.text == "." && t.spaceBefore) || // leading `.method` => $_.method (only after a space: `say .uc`)
                   t.text == "::" || // symbolic reference `say ::($name)` / `say ::Foo`
                   t.text == "$" || // item contextualizer `ok $%*ENV` / `say $(1,2)` (bare `$` is never an infix)
                   ((t.text == "%" || t.text == "@") && &t == &cur() &&
                    ((peek().kind == Tok::LParen && !peek().spaceBefore) ||
                     // …and the HASH composer `%{ … }`. A listop argument
                     // position has no left operand, so a `%` there cannot be
                     // infix modulo and the brace cannot be a block. Without
                     // this, `make %{'k' => 1}` parsed as a nullary `make`
                     // followed by a stray block and stored nothing —
                     // FEN::Grammar builds its castling rights exactly that way,
                     // so every parsed position lost all four flags. (`@{ … }`
                     // is deliberately NOT here: it is the Perl 5 dereference,
                     // which Rakudo refuses outright and points at `@( … )`.)
                     (t.text == "%" && peek().kind == Tok::LBrace && !peek().spaceBefore) ||
                     (peek().kind == Tok::Var && !peek().spaceBefore && peek().text.size() > 1))) || // `%(...)` / `@(...)` / `%{...}` / `@$x` / `%$h` contextualizers
                   (t.text == "&" && &t == &cur() && !peek().spaceBefore &&
                    (peek().kind == Tok::LBracket ||   // infix-as-value `say &[+](2,3)`
                     peek().kind == Tok::LParen ||     // Callable contextualizer `say &(%h<k>)()`
                     (peek().kind == Tok::Var && peek().text.size() > 1 &&
                      (peek().text[0] == '%' || peek().text[0] == '@' || peek().text[0] == '$')))) || // `say &%h<k>()`
                   t.text == "\xE2\x88\x9E" || t.text == "\xC2\xAB" || t.text == "<<" || fusedQqww(t) || // ∞, «qw», <<qww>>
                   userPrefix_.count(t.text) || userCircumfix_.count(t.text); // user prefix / circumfix-open
        case Tok::Ident: {
            // A keyword that has nothing after it is not a keyword: `say if;` calls
            // the sub named `if` (roast S02-lexical-conventions/one-pass-parsing.t
            // declares one), while the spaced `say if ;` reads the modifier and
            // fails on the missing condition, exactly as Rakudo has it. Only a
            // GLUED closer counts — `if;`, `if,`, `if)` — so every keyword that is
            // actually followed by something stays a keyword.
            if (&t == &cur() && kStmtModifiers.count(t.text) && !peek().spaceBefore &&
                (peek().kind == Tok::Semicolon || peek().kind == Tok::Comma ||
                 peek().kind == Tok::RParen || peek().kind == Tok::RBracket ||
                 peek().kind == Tok::RBrace || peek().kind == Tok::End))
                return true;
            // A word-infix operator right after a bareword term is an INFIX, not the
            // start of a listop argument: `Seq eqv Seq`, `Int eq Int`, `$x div $y`.
            static const std::set<std::string> wordInfix = {
                "eq", "ne", "lt", "gt", "le", "ge", "cmp", "leg", "eqv", "before", "after", "unicmp", "coll",
                "x", "xx", "and", "or", "xor", "andthen", "orelse", "notandthen",
                "div", "mod", "gcd", "lcm",
                // …and the mixin infixes: `C but R` / `C does R` on a bare TYPE
                // NAME read as a call to `C` whose first argument was the routine
                // `but`, so mixing a role into a type object was a parse error
                // (Lingua::NumericWordForms picks its parser role that way).
                "but", "does", "min", "max", "minmax",
            };
            // …but an identifier TIGHT against `(` is a call, never an infix —
            // that is the rule Raku states, and it is what `say min(3, 9)` needs:
            // vetoing it made `say` a no-argument call (printing a blank line)
            // and `min` an infix between it and the parenthesised list. Same for
            // `max`/`minmax`, and for any user sub that happens to share a
            // word-operator's name.
            if (wordInfix.count(t.text) && &t == &cur() &&
                peek().kind == Tok::LParen && !peek().spaceBefore)
                return true;
            // Three of them are also list-op SUBS. Which reading wins is decided
            // by what is on the LEFT: after a bare TYPE NAME the infix is meant
            // (`Inf min 5` is 5), and that is the case this veto exists for —
            // but after a lowercase bareword, which is a ROUTINE name and not a
            // term, they start its argument. `say min @a` is say(min(@a)), and
            // `lower min 3, 9` is lower(min(3, 9)), both Rakudo-verified.
            static const std::set<std::string> alsoSubs = {"min", "max", "minmax"};
            if (alsoSubs.count(t.text) && !lhsName.empty() &&
                ascii::islower((unsigned char)lhsName[0]))
                return true;
            // …and a word infix this unit or an imported module declares as a SUB
            // is that sub here: `sub div($t) {…}; say div "x"` is say(div("x")),
            // which is what Rakudo answers once `&div` is in scope. Without this
            // `say` took no arguments and `div` was the operator between them —
            // the reason a `<div>` tag builder could not be called.
            if (wordInfix.count(t.text) && wordInfixSubs_.count(t.text)) return true;
            if (wordInfix.count(t.text)) return false;
            // …and a METAOP over one of them is just as much an infix: `rand Rxx 5`,
            // `@a Zcmp @b`, `$x RRxx 5`. Read as an argument instead, the metaop
            // became a routine name nobody declared. (A word that merely STARTS
            // with R/Z/X is unaffected — `Range`, `Zulu` — because what follows
            // the run has to be a word infix in its own right.)
            {
                size_t i = 0;
                while (i < t.text.size() && (t.text[i] == 'R' || t.text[i] == 'Z' || t.text[i] == 'X')) i++;
                if (i > 0 && i < t.text.size() && wordInfix.count(t.text.substr(i))) return false;
            }
            // a keyword directly followed by `=>` is a bareword PAIR KEY, not the
            // keyword: `register('Anna', role => 'admin')`
            if (&t == &cur() && peek().kind == Tok::FatArrow) return true;
            // `output-w with $w-first;` — after a parenless call, `with`/`without`
            // is the STATEMENT MODIFIER, never an argument. They are deliberately
            // absent from kBlockKeywords (as statements they start a term), so
            // without this the call swallowed the modifier and died looking for a
            // routine called `with`.
            // …unless GLUED to a paren, which is a call of a sub named `with`
            // (Crane's `:&with!` parameter, applied as `with(…)`) — Rakudo reads
            // `with(` the same way and says so
            if (t.text == "with" || t.text == "without")
                return &t == &cur() && peek().kind == Tok::LParen && !peek().spaceBefore;
            // sub/method/do/start begin an expression (anonymous routine / do-block); my/our/state/has/
            // constant begin a declaration expression that is a valid list-op argument (`ok my $x = 5, "d"`)
            if (typeWordAsTerm(t)) return true;
            return !kBlockKeywords.count(t.text) ||
                   t.text == "sub" || t.text == "method" || t.text == "do" || t.text == "start" ||
                   t.text == "my" || t.text == "our" || t.text == "state" || t.text == "has" || t.text == "constant" ||
                   t.text == "not" || // `say not 0` — the loose prefix, see startsTermToken
                   // an inline class/role/grammar is an expression: `is class :: {…}.new.x, …`
                   // — the NAMED form too, so `say class Foo {}` passes one argument
                   // instead of parsing as a nullary `say` (startsTermToken agrees)
                   ((t.text == "class" || t.text == "role" || t.text == "grammar") && &t == &cur() &&
                    (peek().kind == Tok::LBrace || (peek().kind == Tok::Op && peek().text == "::") ||
                     (peek().kind == Tok::Ident && peek(2).kind == Tok::LBrace)));
        }
        default:
            return false;
    }
}

// ---------------- expressions ----------------
ExprPtr Parser::parseExpression() { return parseExpr(0); }
// The contents of an already-opened `(` up to its `)`, where a `;` separates
// SEGMENTS: `[Z](1,2,3;4,5,6)` is a reduce over a list of lists, exactly as the
// same spelling means in a subscript or a `:shape`. A lone segment is the
// grouped value, as in the term form.
ExprPtr Parser::parseParenSemiList() {
    ExprPtr e = parseExpression();
    if (!isKind(Tok::Semicolon)) { expectKind(Tok::RParen, ")"); return e; }
    auto lst = std::make_unique<ListExpr>();
    lst->parenned = true;
    lst->semicolon = true;
    lst->items.push_back(std::move(e));
    while (matchKind(Tok::Semicolon)) {
        if (isKind(Tok::RParen)) break;             // trailing ;
        lst->items.push_back(parseExpression());
    }
    expectKind(Tok::RParen, ")");
    if (lst->items.size() == 1) return std::move(lst->items[0]);
    return lst;
}

// Did the previous token END a term? (so a tight `->` after it is Perl 5's
// method arrow rather than the head of a pointy block)
static bool termEndsHere(const Token& prev) {
    return prev.kind == Tok::Ident || prev.kind == Tok::Var || prev.kind == Tok::RParen ||
           prev.kind == Tok::RBracket || prev.kind == Tok::IntLit || prev.kind == Tok::NumLit ||
           prev.kind == Tok::StrLit || prev.kind == Tok::StrInterp;
}

// `anon class C {…}` names its type C but installs the symbol NOWHERE, so the
// bare `C` is undeclared afterwards and inside the body alike. The declaration
// may arrive as a statement or as an expression, so both spellings route here.
static void markAnonDecl(Expr* e) {
    if (!e) return;
    if (e->kind == NK::Unary) { markAnonDecl(static_cast<Unary*>(e)->operand.get()); return; }
    if (e->kind != NK::BlockExpr) return;
    for (auto& st : static_cast<BlockExpr*>(e)->body)
        if (st && st->kind == NK::ClassDecl) static_cast<ClassDecl*>(st.get())->isAnonDecl = true;
}

// `my Int $x = NaN` is a COMPILE error in Raku, and a named one: a numeric
// LITERAL must already BE the declared type — no widening and no narrowing, so
// `my Rat $x = 42` and `my Num $x = 1.5` are refused as firmly as the NaN.
// (A NEGATED literal is an expression, not a literal, and falls through to the
// ordinary run-time check: `my Int $x = -Inf` is X::TypeCheck::Assignment.)
// `varType` names the type of an ALREADY-declared target (`my Num $n; $n = 42`
// is refused the same way); without it only a declaring target is judged.
static void checkLiteralDeclType(const Expr* target, const Expr* value, int line,
                                 const std::string* varType = nullptr) {
    if (!target || !value || target->kind != NK::VarExpr) return;
    auto* ve = static_cast<const VarExpr*>(target);
    if (ve->name.empty() || ve->name[0] != '$') return;
    if (!ve->declare && !varType) return;
    const std::string& declType = ve->declare ? ve->declType : *varType;
    if (declType.empty()) return;
    // a COERCION type (`my Rat(Str) $v = 1`) decides at run time, where the
    // refusal is X::TypeCheck::Assignment, not this compile-time wording
    if (ve->declare && !ve->declCoerce.empty()) return;
    static const std::set<std::string> kInt = {"Int", "int", "int8", "int16", "int32",
        "int64", "uint", "uint8", "uint16", "uint32", "uint64", "byte"};
    static const std::set<std::string> kRat = {"Rat", "rat", "rat32", "rat64", "FatRat"};
    static const std::set<std::string> kNum = {"Num", "num", "num32", "num64"};
    const char* want = kInt.count(declType) ? "Int"
                     : kRat.count(declType) ? "Rat"
                     : kNum.count(declType) ? "Num"
                     : declType == "Complex" ? "Complex" : nullptr;
    if (!want) return;
    std::string got, spell;
    // `<42+0i>` — the Complex literal angleWordNumeric builds, marked by the
    // `<…>` spelling it leaves on the imaginary part. A source `42 + 0i` is an
    // expression and is not judged.
    if (value->kind == NK::Binary) {
        auto* b = static_cast<const Binary*>(value);
        if (b->op != "+" || !b->rhs || b->rhs->kind != NK::NumLit) return;
        auto* im = static_cast<const NumLit*>(b->rhs.get());
        if (!im->imaginary || im->raw.empty() || im->raw[0] != '<') return;
        got = "Complex"; spell = im->raw;
    }
    else
    if (value->kind == NK::IntLit) {
        auto* il = static_cast<const IntLit*>(value);
        got = "Int";
        spell = !il->raw.empty() ? il->raw : (!il->big.empty() ? il->big : std::to_string(il->v));
    }
    else if (value->kind == NK::NumLit) {
        auto* nl = static_cast<const NumLit*>(value);
        got = nl->imaginary ? "Complex" : nl->isRat ? "Rat" : "Num";
        spell = nl->raw;
    }
    else if (value->kind == NK::NameTerm) {
        const std::string& n = static_cast<const NameTerm*>(value)->name;
        if (n != "NaN" && n != "Inf") return;
        got = "Num"; spell = n;
    }
    else return;
    if (got == want || spell.empty()) return;
    // a native reads "native variable", and suggests the boxed type's coercer
    const bool native = !declType.empty() && ascii::islower((unsigned char)declType[0]);
    const std::string coerce = native ? std::string(want) : declType;
    throw ParseError("Cannot assign a literal of type " + got + " (" + spell +
                     ") to a " + (native ? "native " : "") + "variable (" + ve->name +
                     ") of type " + declType +
                     ". You can declare the variable to be of type Real, or try to "
                     "coerce the value with " + spell + "." + coerce + " or " +
                     coerce + "(" + spell + ").",
                     line, "X::Syntax::Number::LiteralType",
                     {{"vartype", declType}, {"value", spell}});
}

// A SPACED adverb after an expression, `:name` tight on its own, is a named
// argument to the LOOSEST operator before it (S03): `!$o.m() :adv` goes to
// `!`, `($o.m() :adv)` to `.m`, `(3 zin 4 :x(5))` to `zin`. A call or method
// call takes it as one more argument; a user-declared prefix or infix becomes a
// call of its routine with it. Anything else is left for the caller (the
// subscript adverbs, `@a[0..2] :kv`, are the postfix parser's).
bool Parser::spacedAdverbAhead(bool allowTight) {
    return isOp(":") && (cur().spaceBefore || allowTight) && !peek().spaceBefore &&
           (peek().kind == Tok::Ident || peek().kind == Tok::IntLit ||
            (peek().kind == Tok::Op && peek().text == "!" && peek(2).kind == Tok::Ident));
}

bool Parser::attachSpacedAdverb(ExprPtr& lhs) {
    // …tight too (`blub "bar":times(2)`): a call already took its own tight
    // adverbs, so one still here belongs further out
    if (!lhs || !spacedAdverbAhead(true)) return false;
    switch (lhs->kind) {
        case NK::Call:
            static_cast<Call*>(lhs.get())->args.push_back(parseColonPair());
            return true;
        case NK::MethodCall:
            static_cast<MethodCall*>(lhs.get())->args.push_back(parseColonPair());
            return true;
        case NK::Unary: {
            auto* u = static_cast<Unary*>(lhs.get());
            if (u->postfix || !userPrefix_.count(u->op)) return false;
            auto c = std::make_unique<Call>();
            c->name = "prefix:<" + u->op + ">";
            c->args.push_back(std::move(u->operand));
            c->args.push_back(parseColonPair());
            lhs = std::move(c);
            return true;
        }
        case NK::Binary: {
            auto* b = static_cast<Binary*>(lhs.get());
            if (!userInfix_.count(b->op)) return false;
            auto c = std::make_unique<Call>();
            c->name = "infix:<" + b->op + ">";
            c->args.push_back(std::move(b->lhs));
            c->args.push_back(std::move(b->rhs));
            c->args.push_back(parseColonPair());
            lhs = std::move(c);
            return true;
        }
        default: return false;
    }
}

ExprPtr Parser::parseExpr(int minbp) {
    ExprPtr lhs = parsePrefix();
    for (;;) {
        if (minbp <= BP_ASSIGN && attachSpacedAdverb(lhs)) continue;
        // 6.c's `≼`/`≽` and `(<+)`/`(>+)` ARE the subset/superset tests there;
        // later revisions keep the spelling only to say it was removed
        if (langRev_ == 0 && cur().kind == Tok::Op) {
            const std::string& o = cur().text;
            if (o == "\xE2\x89\xBC" || o == "(<+)") toks_[pos_].text = "(<=)";
            else if (o == "\xE2\x89\xBD" || o == "(>+)") toks_[pos_].text = "(>=)";
        }
        // a block-closing `}` at end of line ends the statement: whatever is on the
        // next line is a new one, not an infix continuation (see lastBlockClose_)
        // …unless an UNSPACE joins them: `} \` + newline + `==> sort()` is ONE
        // statement, and the backslash is the whole reason it is. An unspaced
        // token is the only way a token on a LATER line carries no space before
        // it, so that flag is the test (P6Repl::Helper feeds a gather that way,
        // and `do {1} \` + newline + `+ 2` silently answered 1).
        // …and a colon-pair on the next line is the next pair of the SAME list,
        // `%( :err{ … }` NEWLINE `:out{ … } )`: no statement starts with one
        if (pos_ > 0 && pos_ - 1 == lastBlockClose_ && cur().line != toks_[pos_ - 1].line &&
            cur().spaceBefore &&
            !(isOp(":") && !peek().spaceBefore && (peek().kind == Tok::Ident || peek().kind == Tok::IntLit)))
            break;
        // user-defined infix operator: `4 avg 10`  ==  infix:<avg>(4, 10)
        // A SYMBOLIC user infix (`sub infix:<±>`) arrives as Tok::Op, not Ident —
        // the declaration registered fine, but the use site then died "unexpected
        // operator in term position". Accept both kinds; the registry only holds
        // names the program itself declared.
        // …but only for ops the classifier does NOT already know: a `multi sub
        // infix:<==>` ADDS a candidate to the built-in, and routing every `==`
        // here sent Int == Int into the user multi — which then had no matching
        // candidate (roast's advent2009-day22.t, "Cannot resolve caller
        // infix:<==>()"). Built-in ops keep their normal parse; the runtime
        // already tries a user overload first for object operands.
        // A METAOP over a USER word infix arrives as ONE identifier — `Xwtf`,
        // `Zwtf`, `Rwtf`, `XZwtf` — because the lexer cannot know `wtf` is an
        // operator. The infix classifier's word-base table lists only built-ins,
        // so the declared-operator set is consulted here instead.
        // …and a metaop over a SYMBOLIC user infix arrives as the metaop letters
        // and then the operator, tight together: `R⋅`, `X==⨧`.
        bool metaPlusOp = false;
        if (cur().kind == Tok::Ident && !userInfix_.count(cur().text) &&
            cur().text.find_first_not_of("ZXR") == std::string::npos &&
            peek().kind == Tok::Op && !peek().spaceBefore && userInfix_.count(peek().text))
            metaPlusOp = true;
        if (cur().kind == Tok::Ident && (cur().text.size() > 1 || metaPlusOp) &&
            !userInfix_.count(cur().text)) {
            const std::string w = metaPlusOp ? cur().text + peek().text : cur().text;
            size_t i = 0;
            while (i < w.size() && (w[i] == 'Z' || w[i] == 'X' || w[i] == 'R')) i++;
            if (i > 0 && i < w.size() && userInfix_.count(w.substr(i))) {
                // Z/X are list infixes whatever they wrap; R keeps the base's own
                // precedence, since it only swaps the operands.
                int bp = (w[0] == 'Z' || w[0] == 'X') ? BP_ZIP : userInfix_[w.substr(i)];
                if (bp < minbp) break;
                advance();
                if (metaPlusOp) advance();
                auto bin = std::make_unique<Binary>();
                bin->op = w;
                bin->lhs = std::move(lhs);
                bin->rhs = parseExpr(bp + 1);
                lhs = std::move(bin);
                continue;
            }
        }
        if ((cur().kind == Tok::Ident ||
             (cur().kind == Tok::Op && !classifyInfix(cur()).valid)) &&
            userInfix_.count(cur().text)) {
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
        // reverse comma metaop: `a R, b` == the list `(b, a)`. (zef's plugin-probe
        // loop: `next() R, DEBUG(...)` — run DEBUG, then next.)
        if (cur().kind == Tok::Ident && cur().text == "R" && peek().kind == Tok::Comma &&
            !peek().spaceBefore && BP_COMMA >= minbp) {
            advance(); // R
            advance(); // ,
            ExprPtr rhs = parseExpr(BP_COMMA + 1);
            auto list = std::make_unique<ListExpr>();
            list->items.push_back(std::move(rhs));
            list->items.push_back(std::move(lhs));
            lhs = std::move(list);
            continue;
        }
            // reverse metaoperator `a R/ b` == `b / a` (R immediately before an infix op)
        if (cur().kind == Tok::Ident && cur().text == "R" && peek().kind == Tok::Op && !peek().spaceBefore) {
            InfixInfo base = classifyInfix(peek());
            // `|4 R.. 5` carries the same precedence worry the plain `..` does —
            // the prefix binds to the endpoint, not to the range.
            if (base.valid && base.isRange && lhs->kind == NK::Unary && !parenned_.count(lhs.get())) {
                const std::string& po = static_cast<Unary*>(lhs.get())->op;
                if (po == "|" || po == "~")
                    throw ParseError(
                        std::string("To apply a ") + (po == "|" ? "Slip flattener" : "string coercion") +
                        " to a range, parenthesize the whole range.\n"
                        "(Or parenthesize the whole endpoint expression, if you meant that.)",
                        cur().line, "X::Worry::Precedence::Range", {});
            }
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
        // The SEQUENTIAL metaop, `1 S& 2`: the operands are evaluated one after
        // another rather than in parallel, which on a single thread is what the
        // plain operator already does — so it IS the plain operator here.
        if (cur().kind == Tok::Ident && cur().text == "S" && peek().kind == Tok::Op &&
            !peek().spaceBefore && peek().text != "/" && peek().text != "[") {
            InfixInfo sBase = classifyInfix(peek());
            if (sBase.valid && !sBase.isAssign && sBase.lbp >= minbp) {
                advance();                                   // S
                std::string baseOp = advance().text;
                auto bin = std::make_unique<Binary>();
                bin->op = baseOp;
                bin->lhs = std::move(lhs);
                bin->rhs = parseExpr(sBase.lbp + 1);
                lhs = std::move(bin);
                continue;
            }
        }
        // bracketed infix: `A [op] B` (any infix may be enclosed in square
        // brackets) and the metaop-assignment form `A [op]= B` — LibraryMake:
        // `%vars{$k} [R//]= %*ENV{$k}`. Content must be exactly an operator
        // (optionally R-prefixed) then `]`; anything else backtracks untouched.
        // A metaop letter (or the `!` negation) written TIGHT against a bracketed
        // infix belongs to the bracket: `4 R[+] 5`, `"a" ![!eq] "a"`. Consume it
        // here so the bracketed-infix reader below sees its own `[`.
        std::string outerMeta;
        if (cur().spaceBefore && peek().kind == Tok::LBracket && !peek().spaceBefore &&
            ((cur().kind == Tok::Ident && !cur().text.empty() &&
              cur().text.find_first_not_of('R') == std::string::npos) ||  // R, RR, RRR…
             (cur().kind == Tok::Op && cur().text == "!")))
            outerMeta = cur().text;
        if ((cur().kind == Tok::LBracket && cur().spaceBefore) || !outerMeta.empty()) {
            size_t save = pos_;
            if (!outerMeta.empty()) advance(); // the R / ! before the bracket
            advance(); // [
            std::string rPfx;
            if (cur().kind == Tok::Ident && cur().text == "R" && peek().kind == Tok::Op && !peek().spaceBefore) {
                rPfx = "R"; advance();
            }
            bool made = false;
            // A CALLABLE used as an infix: `A [&f] B` is `f(A, B)`. Terminal::UI
            // picks `&infix:<+>` or `&infix:<->` at run time and applies it as
            // `$current [&($op)] 1`; the spelling gathering below only recognises
            // an operator's own text, so every `&` form died at "expected )".
            // Rakudo gives it additive precedence, left-associative:
            // `1 + 2 [&f] 3` is `(1+2) [&f] 3` and `2 [&f] 3 + 1` is `(2 [&f] 3) + 1`.
            if (BP_ADD >= minbp &&
                ((cur().kind == Tok::Var && cur().text.size() > 1 && cur().text[0] == '&') ||
                 (cur().kind == Tok::Op && cur().text == "&" && peek().kind == Tok::LParen))) {
                ExprPtr fn;
                bool ok = true;
                if (cur().kind == Tok::Var) fn = parsePrefix();
                else {
                    advance(); advance();                    // & (
                    fn = parseExpression();
                    if (!matchKind(Tok::RParen)) ok = false;
                }
                if (ok && isKind(Tok::RBracket)) {
                    advance();                               // ]
                    auto call = std::make_unique<Call>();
                    call->callee = std::move(fn);
                    call->parenned = true;
                    ExprPtr rhsE = parseExpr(BP_ADD + 1);
                    // `3 R[&atan2] 4` is atan2(4, 3) — the reverse metaop applies
                    // to the bracketed callable exactly as it does to an operator
                    if (outerMeta == "R" || rPfx == "R") {
                        call->args.push_back(std::move(rhsE));
                        call->args.push_back(std::move(lhs));
                    } else {
                        call->args.push_back(std::move(lhs));
                        call->args.push_back(std::move(rhsE));
                    }
                    lhs = std::move(call);
                    made = true;
                }
                if (!made) { pos_ = save; continue; }        // not this form — hand it back
            }
            // The content is the operator's own spelling, which may be more than
            // one token: `[max]` and `[eq]` are words, `[Z=>]`/`[X~]` are a word
            // plus an operator. Gather up to four adjacent tokens and ask the
            // infix classifier what they spell, exactly as the hyper metaop does.
            // The metaops NEST — `[Z[cmp]]`, `R[R[R-]]` — so the spelling is read
            // with a bracket depth rather than as a flat run of tokens.
            std::string spell;
            size_t k = pos_;
            {
                int depth = 0; bool ok = true;
                for (int n = 0; n < 12; n++) {
                    const Token& t = toks_[k];
                    if (t.kind == Tok::End) { ok = false; break; }
                    if (t.kind == Tok::RBracket) {
                        if (depth == 0) break;              // the closing bracket of the whole form
                        spell += "]"; depth--; k++; continue;
                    }
                    if (n && t.spaceBefore) { ok = false; break; }
                    if (t.kind == Tok::LBracket) { spell += "["; depth++; k++; continue; }
                    if (t.kind == Tok::FatArrow) spell += "=>";      // `[Z=>]`
                    else if (t.kind == Tok::Comma) spell += ",";     // `[R,]`
                    else if (t.kind == Tok::Op || t.kind == Tok::Ident) spell += t.text;
                    else { ok = false; break; }
                    k++;
                }
                if (!ok || depth != 0) spell.clear();
            }
            if (toks_[k].kind != Tok::RBracket) spell.clear();
            if (!spell.empty() && spell != "=" &&
                (cur().kind == Tok::Op || cur().kind == Tok::Ident)) {
                Token synth = cur(); synth.text = spell;
                InfixInfo base = classifyInfix(synth);
                // `[!eq]` — the negation metaop INSIDE the brackets. It is not an
                // operator the classifier knows by name; what it has to answer is
                // the base operator's precedence.
                // `[R,]` — the reverse metaop over the COMMA operator, which has
                // a token kind of its own and no entry in the infix tables
                if (!base.valid && spell.size() > 1 && spell.back() == ',') {
                    base.valid = true; base.lbp = BP_COMMA;
                }
                // a USER-DECLARED infix in brackets: `1031 [blue] 4`
                if (!base.valid && userInfix_.count(spell)) {
                    base.valid = true; base.lbp = userInfix_[spell]; base.op = spell;
                }
                if (!base.valid && spell.size() > 1 && spell[0] == '!') {
                    Token synth2 = cur(); synth2.text = spell.substr(1);
                    // a WORD base has to arrive as an Ident — the classifier reads
                    // the two kinds through different tables, and `[!eq]` starts
                    // with the `!` Op token
                    synth2.kind = ascii::isalpha((unsigned char)synth2.text[0]) ? Tok::Ident : Tok::Op;
                    InfixInfo b2 = classifyInfix(synth2);
                    if (b2.valid) { base = b2; base.op = spell; }
                }
                if (base.valid) {
                    // The brackets are punctuation once the spelling is known:
                    // `R[R-]` is `RR-` and `Z[cmp]` is `Zcmp`, which is what every
                    // metaop reader downstream already understands.
                    std::string baseOp;
                    for (char c : spell) if (c != '[' && c != ']') baseOp += c;
                    while (pos_ < k) advance();
                    advance(); // ]
                    bool assignForm = isOp("=") && !cur().spaceBefore;
                    if (assignForm && BP_ASSIGN >= minbp) {
                        advance(); // =
                        auto as = std::make_unique<Assign>();
                        as->op = "[" + outerMeta + rPfx + baseOp + "]=";
                        as->target = std::move(lhs);
                        as->value = parseExpr(BP_ASSIGN);
                        lhs = std::move(as);
                        made = true;
                    }
                    else if (!assignForm && base.lbp >= minbp) {
                        if (outerMeta.empty() && rPfx.empty() && userInfix_.count(baseOp)) {
                            auto call = std::make_unique<Call>();
                            call->name = "infix:<" + baseOp + ">";
                            call->args.push_back(std::move(lhs));
                            call->args.push_back(parseExpr(base.lbp + 1));
                            lhs = std::move(call);
                        } else {
                            auto bin = std::make_unique<Binary>();
                            bin->op = outerMeta + rPfx + baseOp;
                            bin->lhs = std::move(lhs);
                            bin->rhs = parseExpr(base.lbp + 1);
                            lhs = std::move(bin);
                        }
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
        // The Perl 5 match operators, which Raku spells `~~` and `!~~`. They are
        // caught HERE, in operator position, because `=~` and `!~` both parse as
        // something else otherwise — an assignment of a `~` prefix, and the
        // negation metaop over concatenation.
        if (!ismsPerl5_ &&
            cur().kind == Tok::Op && (cur().text == "=" || cur().text == "!") &&
            peek().kind == Tok::Op && peek().text == "~" && !peek().spaceBefore &&
            !(peek(2).kind == Tok::Op && peek(2).text == "~")) {
            bool neg = cur().text == "!";
            std::string old = std::string(neg ? "!~" : "=~") +
                              (neg ? " to do negated pattern matching" : " to do pattern matching");
            std::string repl = neg ? "!~~" : "~~";
            throw ParseError("Unsupported use of " + old + ". In Raku please use: " + repl + ".",
                             cur().line, "X::Obsolete", {{"old", old}, {"replacement", repl}});
        }
        // negated infix (`!eq`, `!%%` when lexed apart): `!` glued to a negatable
        // comparison op applies the op and negates its Bool
        if (cur().kind == Tok::Op && cur().text == "!" && !peek().spaceBefore) {
            static const std::set<std::string> kChainable = {
                "<", ">", "<=", ">=", "==", "!=", "eq", "ne", "lt", "gt", "le", "ge",
                "===", "!==", "!===", "eqv", "!eqv", "=:=", "!=:=",
                "=~=", "!=~=", "\xE2\x89\x85", "before", "after", "~~", "!~~"};
            static const std::set<std::string> negatable = {
                "eq", "ne", "lt", "gt", "le", "ge", "before", "after", "eqv",
                "%%", "==", "<", ">", "<=", ">=",
                // …and the boolean infixes, which are as iffy as an operator gets:
                // `True !&& False` is True (S03-metaops/not.t)
                "&&", "||", "^^", "and", "or", "xor",
            };
            InfixInfo negIn = classifyInfix(peek());
            if (negIn.valid && negatable.count(negIn.op) && negIn.lbp >= minbp) {
                advance(); advance(); // ! and the op
                // A negated COMPARISON chains like its base does: `3 !> 3 !> 1` is
                // `3 !> 3 && 3 !> 1`, and left-association made it `True !> 1`.
                auto chain = std::make_unique<ChainExpr>();
                chain->operands.push_back(std::move(lhs));
                chain->ops.push_back("!" + negIn.op);
                chain->operands.push_back(parseExpr(negIn.lbp + 1));
                while (negIn.lbp == BP_COMPARE) {
                    if (cur().kind == Tok::Op && cur().text == "!" && !peek().spaceBefore) {
                        InfixInfo n2 = classifyInfix(peek());
                        if (!n2.valid || !negatable.count(n2.op) || n2.lbp != BP_COMPARE) break;
                        advance(); advance();
                        chain->ops.push_back("!" + n2.op);
                        chain->operands.push_back(parseExpr(n2.lbp + 1));
                        continue;
                    }
                    InfixInfo n3 = classifyInfix(cur());
                    if (!n3.valid || n3.lbp != BP_COMPARE || !kChainable.count(n3.op)) break;
                    advance();
                    chain->ops.push_back(n3.op);
                    chain->operands.push_back(parseExpr(n3.lbp + 1));
                }
                if (chain->ops.size() == 1) {
                    auto bin = std::make_unique<Binary>();
                    bin->op = chain->ops[0];
                    bin->lhs = std::move(chain->operands[0]);
                    bin->rhs = std::move(chain->operands[1]);
                    lhs = std::move(bin);
                } else lhs = std::move(chain);
                continue;
            }
            // `!.` — a dotty postfix has no Bool to negate, and with a STRING on
            // the right it is the Perl 5 concatenation instead. Rakudo names both.
            if (peek().kind == Tok::Op && peek().text == "." &&
                (peek(2).kind == Tok::Ident || peek(2).kind == Tok::StrLit ||
                 peek(2).kind == Tok::StrInterp)) {
                if (peek(2).kind != Tok::Ident)
                    throw ParseError("Unsupported use of . to concatenate strings. In Raku please use: ~",
                                     cur().line, "X::Obsolete",
                                     {{"old", "."}, {"replacement", "~"}});
                throw ParseError("Cannot negate . because dotty operators are not iffy enough",
                                 cur().line, "X::Syntax::CannotMeta", {});
            }
            // `!` on an operator that does not answer a Bool has nothing to
            // negate, and Rakudo names the reason rather than reporting a
            // confused parse: `9 !% 0` is X::Syntax::CannotMeta.
            if (negIn.valid && negIn.lbp >= minbp && peek().kind == Tok::Op) {
                static const std::map<std::string, std::string> kFamily = {
                    {"%", "multiplicative"}, {"*", "multiplicative"}, {"/", "multiplicative"},
                    {"div", "multiplicative"}, {"mod", "multiplicative"}, {"x", "multiplicative"},
                    {"+", "additive"}, {"-", "additive"}, {"~", "concatenation"},
                };
                auto fam = kFamily.find(negIn.op);
                if (fam != kFamily.end())
                    throw ParseError("Cannot negate " + negIn.op + " because " + fam->second +
                                     " operators are not iffy enough", cur().line,
                                     "X::Syntax::CannotMeta", {});
            }
        }
        // Flip-flops with `^` edge markers — `^ff`, `ff^`, `^ff^` (likewise fff).
        // The lexer hands the `^` over as its own token, so stitch the operator
        // back together here, ahead of classifyInfix (which would otherwise read
        // the leading `^` as the one-junction infix).
        {
            int k = 0; std::string ffOp;
            if (cur().kind == Tok::Op && cur().text == "^" && !peek().spaceBefore &&
                peek().kind == Tok::Ident && (peek().text == "ff" || peek().text == "fff"))
                { ffOp = "^" + peek().text; k = 2; }
            else if (cur().kind == Tok::Ident && (cur().text == "ff" || cur().text == "fff"))
                { ffOp = cur().text; k = 1; }
            if (k && peek(k).kind == Tok::Op && peek(k).text == "^" && !peek(k).spaceBefore)
                { ffOp += "^"; k++; }
            // only claim the DECORATED forms; plain ff/fff keep the normal path
            if (k && (ffOp.front() == '^' || ffOp.back() == '^') && BP_TERNARY >= minbp) {
                for (int i = 0; i < k; i++) advance();
                auto bin = std::make_unique<Binary>();
                bin->op = ffOp;
                bin->lhs = std::move(lhs);
                bin->rhs = parseExpr(BP_TERNARY + 1);
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
        // hyper around a USER infix: `@a »=~=« @b` — marker + registered custom
        // operator + marker. Desugars exactly like the bracketed form below, to a
        // hyper-with call of &infix:<op> (JSON::Fast's datetime test compares
        // round-tripped arrays this way).
        if (!in.valid && cur().kind == Tok::Op &&
            (cur().text == ">>" || cur().text == "<<" ||
             cur().text == "\xC2\xBB" || cur().text == "\xC2\xAB") &&
            peek().kind == Tok::Op && userInfix_.count(peek().text) &&
            (peek(2).kind == Tok::Op &&
             (peek(2).text == ">>" || peek(2).text == "<<" ||
              peek(2).text == "\xC2\xBB" || peek(2).text == "\xC2\xAB")) &&
            BP_ZIP >= minbp) {
            std::string m1 = (cur().text == "<<" || cur().text == "\xC2\xAB") ? "<<" : ">>";
            advance();
            std::string opName = advance().text;
            std::string m2 = (cur().text == "<<" || cur().text == "\xC2\xAB") ? "<<" : ">>";
            advance();
            ExprPtr rhs = parseExpr(BP_COMMA + 1);
            auto c = std::make_unique<Call>();
            c->name = "hyper-with:" + m1 + m2;
            c->args.push_back(std::move(lhs));
            c->args.push_back(std::move(rhs));
            auto pw = std::make_unique<PairExpr>();
            pw->key = "with";
            pw->value = std::make_unique<VarExpr>("&infix:<" + opName + ">");
            c->args.push_back(std::move(pw));
            lhs = std::move(c);
            continue;
        }
        // hyper with a bracketed operator value: $a >>[&infix:<+>]<< $b — the
        // marker + [ + CALLABLE + ] + marker; desugars to zip(:with(CALLABLE))
        if (!in.valid && cur().kind == Tok::Op &&
            (cur().text == ">>" || cur().text == "<<" ||
             cur().text == "\xC2\xBB" || cur().text == "\xC2\xAB") &&
            peek().kind == Tok::LBracket &&
            // …but only when the brackets cite an OPERATOR (`>>[+]<<`) or name a
            // Callable (`>>[&infix:<+>]<<`). An INDEX in there is the hyper
            // SUBSCRIPT `@a>>[0]`, which parsePostfix owns — reading it here made
            // `@a>>[0]>>.Str` a hyper-with whose operator was the number 0.
            (peek(2).kind == Tok::Op ||
             (peek(2).kind == Tok::Var && !peek(2).text.empty() && peek(2).text[0] == '&')) &&
            BP_ZIP >= minbp) {
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
        // (Every operator at CHAINING precedence belongs here — `3 === 3 === 3`
        // is True in Raku, where left-association made it `True === 3`, and
        // `0 ~~ 0 ~~ 0` is `0 ~~ 0 && 0 ~~ 0`. `cmp` and `leg` stay out: they
        // answer an Order, and Rakudo refuses to chain them at all.)
        static const std::set<std::string> chainOps = {
            "<", ">", "<=", ">=", "==", "!=", "eq", "ne", "lt", "gt", "le", "ge",
            "===", "!==", "!===", "eqv", "!eqv", "=:=", "!=:=",
            "=~=", "!=~=", "\xE2\x89\x85", "before", "after",
            "~~", "!~~",
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
            // A trailing comma may close a list — `(1, 2,)`, `f(1,)` — but only
            // before something that ENDS it. An infix cannot: `%h, = 5 => 4` has
            // no term for the `=` to take, and it is a syntax error for the same
            // reason `1, => 2` is (the throw just below). We used to close the
            // list and assign to it, so `%h, = 5 => 4` silently REPLACED the hash
            // — which is also the wrong answer `,=` gave before it had a token of
            // its own, and is what makes the spaced spelling worth refusing rather
            // than quietly accepting. Issue #85.
            if (!modNext && !startsTermToken(cur())) {
                InfixInfo nx = classifyInfix(cur());
                if (nx.valid && nx.isAssign)
                    throw ParseError("Preceding context expects a term, but found infix "
                                     + nx.op + " instead",
                                     cur().line, "X::Syntax::InfixInTermPosition", {{"infix", nx.op}});
            }
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
            if (lhs->kind == NK::NameTerm && !static_cast<NameTerm*>(lhs.get())->noAutoQuote) p->key = static_cast<NameTerm*>(lhs.get())->name;
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
            int metaToks = 2; // Z/X + the trailing op
            if (peek().kind == Tok::FatArrow) meta = "=>";
            else if (peek().kind == Tok::Comma) meta = ","; // Z, / X, — zip/cross into tuples
            else if (peek().kind == Tok::Op) meta = peek().text;
            // `Z[+]` / `X[*]` — the metaop's operator written in BRACKETS. Without
            // this the `[+]` was parsed as a REDUCTION of the right operand, so
            // `@a Z[+] @b` answered a single pair whose second element was the sum of
            // all of @b. Digest's HMAC XORs its key pad with `@$key Z[+^] $i xx *`,
            // which is why every HMAC came out wrong.
            else if (peek().kind == Tok::LBracket && peek(2).kind == Tok::Op &&
                     peek(3).kind == Tok::RBracket) {
                meta = peek(2).text;
                metaToks = 4; // Z/X + [ + op + ]
            }
            // `X[&sprintf]` / `Z[&f]` — a bracketed CALLABLE as the inner operator.
            // The n-ary call form already folds each tuple with a `:with` named
            // argument, so desugar to that rather than inventing an operator name.
            else if (peek().kind == Tok::LBracket && !peek().spaceBefore &&
                     peek(2).kind == Tok::Var && peek(2).text.size() > 1 && peek(2).text[0] == '&' &&
                     peek(3).kind == Tok::RBracket && in.lbp >= minbp) {
                std::string fnName = peek(2).text;       // read before the stream moves
                for (int k = 0; k < 4; k++) advance();   // Z/X + [ + &fn + ]
                ExprPtr rhs = parseExpr(in.lbp + 1);
                auto c = std::make_unique<Call>();
                c->name = "infix:<" + in.op + ">";
                c->args.push_back(std::move(lhs));
                c->args.push_back(std::move(rhs));
                auto pw = std::make_unique<PairExpr>();
                pw->key = "with";
                pw->value = std::make_unique<VarExpr>(fnName);
                c->args.push_back(std::move(pw));
                lhs = std::move(c);
                continue;
            }
            // `X[R%]` / `Z[R~]` — the bracketed op itself R-reversed
            // (Digest::MD5 builds its index table with `16 X[R%] ...`)
            else if (peek().kind == Tok::LBracket && peek(2).kind == Tok::Ident &&
                     peek(2).text == "R" && peek(3).kind == Tok::Op &&
                     !peek(3).spaceBefore && peek(4).kind == Tok::RBracket) {
                meta = "R" + peek(3).text;
                metaToks = 5; // Z/X + [ + R + op + ]
            }
            // `@a X*= 10` / `@a Z+= @b` — the metaop fused with ASSIGNMENT, which
            // means `@a = @a X* 10`. Read as a plain metaop the `*=` became the
            // zip's INNER operator, and applyArith was asked to perform an
            // assignment with no container to write to.
            static const std::set<std::string> kMetaNotAssign = {
                "==", "!=", "<=", ">=", "===", "!==", "!===", "=:=", "!=:=", "=~=", ".=", "=>"};
            if (!meta.empty() && !peek().spaceBefore && BP_ASSIGN >= minbp &&
                meta.back() == '=' && !kMetaNotAssign.count(meta)) {
                for (int k = 0; k < metaToks; k++) advance();
                auto as = std::make_unique<Assign>();
                as->op = in.op + meta;            // "X*=" — evalAssign folds off the `=`
                as->target = std::move(lhs);
                as->value = parseExpr(BP_ASSIGN);
                lhs = std::move(as);
                continue;
            }
            if (!meta.empty() && !peek().spaceBefore) {
                for (int k = 0; k < metaToks; k++) advance();
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

        // `..` and its `^`-marked siblings are NON-ASSOCIATIVE: `1..2..3` has to
        // be parenthesized, and Rakudo says so rather than picking a grouping.
        {
            // `min` and `max` do not mix: `1 min 2 max 3` needs parentheses, and
            // Rakudo refuses it by name rather than picking a grouping.
            if (in.isMinMax && lhs->kind == NK::Binary && !parenned_.count(lhs.get())) {
                const std::string& lo = static_cast<Binary*>(lhs.get())->op;
                if ((lo == "min" || lo == "max") && lo != in.op)
                    throw ParseError("Operators '" + lo + "' and '" + in.op +
                                     "' are non-associative and require parentheses",
                                     cur().line, "X::Syntax::NonListAssociative",
                                     {{"left", lo}, {"right", in.op}});
            }
            static const std::set<std::string> kRangeOps = {"..", "^..", "..^", "^..^"};
            if (kRangeOps.count(in.op)) {
                if (lhs->kind == NK::Range && !parenned_.count(lhs.get()))
                    throw ParseError("Operators '..' and '" + in.op +
                                     "' are non-associative and require parentheses",
                                     cur().line, "X::Syntax::NonAssociative", {});
                // …and a PREFIX on the left endpoint binds tighter than the range,
                // so `|4 .. 5` slips the 4 and not the range. That is almost never
                // what was meant, and Rakudo worries about it by name.
                if (lhs->kind == NK::Unary && !parenned_.count(lhs.get())) {
                    const std::string& po = static_cast<Unary*>(lhs.get())->op;
                    if (po == "|" || po == "~")
                        throw ParseError(
                            std::string("To apply a ") + (po == "|" ? "Slip flattener" : "string coercion") +
                            " to a range, parenthesize the whole range.\n"
                            "(Or parenthesize the whole endpoint expression, if you meant that.)",
                            cur().line, "X::Worry::Precedence::Range", {});
                }
            }
        }
        advance(); // consume infix op

        // list assignment: `@a = 1,2,3` / `my ($a,$b) = ...` grabs the whole comma
        // list; binding does too (`my @r := &min, &max, &minmax` is a 3-element bind)
        bool listAssign = false;
        // `,=` is in here because it reaches as far right as the `=` it is built
        // on would: `%h ,= 5 => 4, 6 => 7` is `%h = %h, (5 => 4, 6 => 7)`, not
        // `(%h ,= 5 => 4), 6 => 7`.
        if (in.isAssign && (in.op == "=" || in.op == ":=" || in.op == ",=")) {
            // `$/ = "x"` (a bare string-literal rhs) is the P5 input-record-separator
            // idiom — a compile error in Raku. `$/ = ('x')` and non-string rhs are fine.
            if (in.op == "=" && lhs->kind == NK::VarExpr &&
                static_cast<VarExpr*>(lhs.get())->name == "$/" &&
                !static_cast<VarExpr*>(lhs.get())->declare &&
                (cur().kind == Tok::StrLit || cur().kind == Tok::StrInterp)) // rhs first token (op already consumed)
                throw ParseError("Unsupported use of $/ variable; in Raku please use the filehandle's .nl-in attribute", cur().line);
            // BINDING is a list operation whatever the target's sigil is:
            // `my $b := 1, 2` binds the List (1, 2), where `my $b = 1, 2` is item
            // assignment and warns about the 2. It is looser than the zip/cross
            // infixes too, so `my $s := 2..* Z* 2..*` binds the whole zip.
            if (in.op == ":=" || in.op == "::=") listAssign = true;
            else if (lhs->kind == NK::ListExpr) listAssign = true;
            else if (lhs->kind == NK::VarExpr) {
                const std::string& nm = static_cast<VarExpr*>(lhs.get())->name;
                if (!nm.empty() && (nm[0] == '@' || nm[0] == '%')) listAssign = true;
                // `constant C = 1, 2, 3` takes the whole list, whatever the sigil
                auto* dv = static_cast<VarExpr*>(lhs.get());
                if (dv->declare && dv->declScope == "constant") listAssign = true;
            }
            else if (lhs->kind == NK::SymbolicRef) {
                // `@::($n) = 1,2,3` / `%::($n) = …` — a sigilled symbolic deref
                // is a list container, so it takes the whole comma list
                const std::string& sg = static_cast<SymbolicRef*>(lhs.get())->sigil;
                if (sg == "@" || sg == "%") listAssign = true;
            }
            else if (lhs->kind == NK::Index) {
                // EVERY subscript target takes the whole comma list, not just a
                // slice: `%h<k> = 1, 2` stores the list $(1, 2) — unlike
                // `my $x = 1, 2`, which is item assignment and warns about the 2.
                // (A slice distributes it; a single element keeps it as one
                // itemized value.) URI::Query's `$q<foo> = '5', '6'` needs both
                // items to reach ASSIGN-KEY.
                auto* ix = static_cast<Index*>(lhs.get());
                // A multidim target is no exception: `@a[0;1] = 7, 8` stores (7 8)
                // and `@a[0..*;1] = 7, 8, 9, 10` distributes. Excluding it made
                // those item assignments that kept the 7 and sank the rest.
                if (ix->index) listAssign = true;
            }
        }

        int nextMin = listAssign ? BP_ZIP : (in.rightAssoc ? in.lbp : in.lbp + 1); // list assign includes Z/X (looser than comma)
        infixRhsPos_ = pos_;   // a term is REQUIRED here (see parsePrimary's default)
        // The other side of a FEED is a call, and a `{ … }` there is that call's
        // block argument — never the control block of the statement the feed sits
        // in. `for @pairs ==> map { .trim } -> $p { … }` was reading the map's
        // block as the loop's body, leaving `map` a bare name and the pointy
        // block a statement of its own. Same reasoning as the parenthesised case.
        bool feedOp = (in.op == "==>" || in.op == "<==");
        bool svFeedCond = stmtCond_;
        if (feedOp) stmtCond_ = false;
        struct FeedRestore { bool* f; bool v; bool on; ~FeedRestore() { if (on) *f = v; } }
            fr{&stmtCond_, svFeedCond, feedOp};
        ExprPtr rhs = parseExpr(nextMin);

        if (in.isAssign) {
            if (in.op.rfind("\xE2\x9A\x9B", 0) == 0) { // \u269b= / +=/-= forms -> atomic calls
                auto call = std::make_unique<Call>();
                call->name = in.op == "\xE2\x9A\x9B=" ? "atomic-assign"
                           : in.op == "\xE2\x9A\x9B+=" ? "atomic-add-fetch" // returns the value AFTER, per roast
                           : "atomic-sub-fetch";
                call->args.push_back(std::move(lhs));
                call->args.push_back(std::move(rhs));
                lhs = std::move(call);
                continue;
            }
            if (in.op == "=") {
                const std::string* known = nullptr;
                if (lhs && lhs->kind == NK::VarExpr && !static_cast<VarExpr*>(lhs.get())->declare) {
                    auto& frame = scalarDeclTypes_.back();
                    auto it = frame.find(static_cast<VarExpr*>(lhs.get())->name);
                    if (it != frame.end()) known = &it->second;
                }
                checkLiteralDeclType(lhs.get(), rhs.get(), lhs->line ? lhs->line : cur().line, known);
            }
            auto a = std::make_unique<Assign>();
            a->target = std::move(lhs); a->op = in.op; a->value = std::move(rhs);
            // `=@=` and friends ARE plain assignment; only the container semantics
            // differ, so record the sigil and let the op read as "=" from here on.
            if (in.op.size() == 3 && in.op[0] == '=' && in.op[2] == '=' &&
                (in.op[1] == '$' || in.op[1] == '@' || in.op[1] == '%')) {
                a->containerSigil = in.op[1];
                a->op = "=";
            }
            lhs = std::move(a);
        } else if (in.isRange) {
            auto r = std::make_unique<RangeExpr>();
            r->from = std::move(lhs); r->to = std::move(rhs);
            r->exTo = (in.op == "..^" || in.op == "^..^");
            r->exFrom = (in.op == "^.." || in.op == "^..^");
            lhs = std::move(r);
        } else {
            // The STRUCTURAL comparisons do not chain — with themselves or with
            // each other: `1 <=> 2 leg 3` needs parentheses, and so does
            // `1 <=> 2 <=> 3`.
            static const std::set<std::string> kStructural = {
                "<=>", "cmp", "leg", "unicmp", "coll"};
            if (kStructural.count(in.op)) {
                InfixInfo nx = classifyInfix(cur());
                if (nx.valid && kStructural.count(nx.op))
                    throw ParseError("Operators '" + in.op + "' and '" + nx.op +
                                     "' are non-associative and require parentheses",
                                     cur().line, "X::Syntax::NonAssociative",
                                     {{"left", in.op}, {"right", nx.op}});
            }
            // …and the junction constructors `|` and `^` do not mix: `1 | 2 ^ 3`
            // is ambiguous and Rakudo refuses it.
            if ((in.op == "|" || in.op == "^") && lhs && lhs->kind == NK::Binary &&
                !parenned_.count(lhs.get())) {
                const std::string& lo = static_cast<Binary*>(lhs.get())->op;
                if ((lo == "|" || lo == "^") && lo != in.op)
                    throw ParseError("Operators '" + lo + "' and '" + in.op +
                                     "' are non-associative and require parentheses",
                                     cur().line, "X::Syntax::NonListAssociative",
                                     {{"left", lo}, {"right", in.op}});
            }
            auto b = std::make_unique<Binary>();
            b->op = in.op; b->lhs = std::move(lhs); b->rhs = std::move(rhs);
            lhs = std::move(b);
        }
    }
    return lhs;
}

ExprPtr Parser::parsePrefix(bool tight) {
    // `anon` is a SCOPE declarator, like `my` — it says the declaration goes
    // into no namespace at all, and the value it evaluates to is the whole
    // point. Everything after it parses as the term it already was; only the
    // installation would differ, and an anonymous routine installs nothing
    // anyway. (Text::CSV builds its test helpers as `anon sub (…) {…}`.)
    if (isKind(Tok::Ident) && cur().text == "anon" && peek().kind == Tok::Ident &&
        (peek().text == "sub" || peek().text == "method" || peek().text == "submethod" ||
         peek().text == "class" || peek().text == "role" || peek().text == "grammar" ||
         peek().text == "package" || peek().text == "module" ||
         peek().text == "token" || peek().text == "rule" || peek().text == "regex" ||
         peek().text == "multi" || peek().text == "state" || peek().text == "my")) {
        advance();
        ExprPtr inner = parsePrefix(tight);
        markAnonDecl(inner.get());
        return inner;
    }
    if (cur().kind == Tok::Op) {
        const std::string& o = cur().text;
        // hyper prefix: -«(1,2) / -<<@a / --<<%h — apply the prefix op to every
        // element, descending into nested arrays (deep distribution); ++/--
        // mutate the elements in place
        if ((o == "!" || o == "-" || o == "+" || o == "~" || o == "?" || o == "|" ||
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
        if (o == "\xE2\x9A\x9B") { // prefix ⚛ — atomic read: ⚛$x → atomic-fetch($x)
            advance();
            auto call = std::make_unique<Call>();
            call->name = "atomic-fetch";
            call->args.push_back(parsePrefix(true));
            return parsePostfix(std::move(call), tight);
        }
        if (o == "!" || o == "-" || o == "+" || o == "~" || o == "?" ||
            o == "++" || o == "--" || o == "^" || o == "|" ||
            o == "+^" || o == "?^" || o == "~^") { // prefix bitwise/boolean NOT
            advance();
            // ++⚛$x / --⚛$x — the atomic prefix forms return the NEW value
            if ((o == "++" || o == "--") && isOp("\xE2\x9A\x9B")) {
                advance();
                auto call = std::make_unique<Call>();
                call->name = o == "++" ? "atomic-inc-fetch" : "atomic-dec-fetch";
                call->args.push_back(parsePrefix(true));
                return parsePostfix(std::move(call), tight);
            }
            auto u = std::make_unique<Unary>();
            // the operand parses "tight" (its own postfixes stop at a space-preceded
            // `.method`), so `^30 .map` is (^30).map while `^30.map` stays ^(30.map).
            // …unless a LEADING DOT follows the operator, which opens the OPERAND's
            // own term: `? .obj-num` is `?($_.obj-num)` in Rakudo, where `^30 .map`
            // stays `(^30).map` because there the operand is already complete.
            // Stopping at the space in both cases left the method attached to the
            // PREFIX's result — `? .obj-num` called it on a Bool, `+ .x` on a Num.
            // PDF::IO::Serializer decides indirectness with
            // `%!ref-count{$_} > 1 || ? .obj-num`.
            u->op = o;
            u->operand = parsePrefix(!(cur().kind == Tok::Op && cur().text == "."));
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
        // 6.e prefix `//` — "is it defined", the term-position counterpart of
        // infix `//`. Only reachable in term position (startsTermToken lets it
        // through under 6.e), so `$a // $b` is untouched. Before 6.e the same
        // spelling opens an empty regex, which Rakudo refuses.
        if (o == "//" && langRev_ >= 2) {
            advance();
            auto mc = std::make_unique<MethodCall>();
            mc->method = "defined";
            mc->inv = parseExpr(BP_PREFIX);
            return mc;
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
                // `$(;)` — only empty statements: the empty list, like `$()`
                while (isKind(Tok::Semicolon) &&
                       (peek().kind == Tok::RParen || peek().kind == Tok::Semicolon)) advance();
                u->operand = isKind(Tok::RParen) ? ExprPtr(std::make_unique<ListExpr>())
                                                 : applyExprModifiers(parseExpression());
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
            // A plain VARIABLE operand takes only the primary, so a postfix binds
            // to the CONTEXTUALIZED value like it does for the circumfix forms
            // above: `@$p[0]` is `(@$p)[0]`, not `@($p[0])`. Letting parsePrefix
            // run here would consume the subscript into the operand.
            //
            // The MATCH-CAPTURE shorthand is the exception: in `@$<seg>` the angle
            // subscript is part of the VARIABLE ($/<seg>), not a postfix on the
            // contextualized value — taking only the primary stranded it outside,
            // so it re-attached as an associative index on the Array
            // (`@($/)<seg>`), and Cro::Uri's `for @$<segment>` died on it. Build
            // the capture node here, the same shape the postfix pass builds.
            if (cur().kind == Tok::Var && cur().text == "$" &&
                peek().kind == Tok::Op && peek().text == "<" && !peek().spaceBefore) {
                advance(); advance(); // $  <
                std::vector<std::string> words = readAngleWords(">");
                auto ix = std::make_unique<Index>();
                ix->base = std::make_unique<VarExpr>("$/");
                ix->isHash = true;
                ix->index = std::make_unique<StrLit>(words.empty() ? "" : words[0]);
                u->operand = std::move(ix);
            }
            else if (cur().kind == Tok::Var)
                u->operand = parsePrimary();
            // …but a LEADING DOT right after the operator opens the OPERAND's own
            // term: `? .obj-num` is `?($_.obj-num)`, which is how Rakudo reads it.
            // Stopping at the space left the method attached to the PREFIX's
            // result, so the call landed on a Bool (or a Num, for `+ .x`).
            // PDF::IO::Serializer decides indirectness with
            // `%!ref-count{$_} > 1 || ? .obj-num`.
            else
                u->operand = parsePrefix(!(cur().kind == Tok::Op && cur().text == "."));
            return parsePostfix(std::move(u), tight);
        }
    }
    // user-defined prefix operator: `sub prefix:<§>` — symbolic (Op) or word (Ident)
    // A new candidate for a BUILT-IN word prefix keeps the built-in's
    // precedence: Red's `multi prefix:<so>(Red::AST $a)` does not make
    // `so $str ~~ /…/` read as `(so $str) ~~ /…/`. `so`/`not` are loose (the
    // identifier path below parses them), not tight like a symbolic prefix.
    if ((cur().kind == Tok::Op || cur().kind == Tok::Ident) && userPrefix_.count(cur().text) &&
        !(cur().kind == Tok::Ident && (cur().text == "so" || cur().text == "not"))) {
        auto u = std::make_unique<Unary>();
        u->op = advance().text;
        u->operand = parsePrefix(true);
        return parsePostfix(std::move(u), tight);
    }
    return parsePostfix(parsePrimary(), tight);
}

std::vector<ExprPtr> Parser::parseCallArgs(ExprPtr* invocant) {
    std::vector<ExprPtr> args;
    const int openedAt = cur().line;   // for the run-off-the-end diagnostic below
    if (isKind(Tok::RParen)) { advance(); return args; }
    bool svCond = stmtCond_; stmtCond_ = false; // parens re-allow block listop args
    struct CondRestore { bool* f; bool v; ~CondRestore() { *f = v; } } cr{&stmtCond_, svCond};
    // An argument expression that runs off the end of the file is an unclosed
    // bracket, whatever the token that finally confused the parser: report the
    // opener, not the symptom. Rakudo words this "Unable to parse expression in
    // argument list; couldn't find final ')'" — same diagnosis.
    auto argOrUnterminated = [&](auto&& parse) -> ExprPtr {
        try { return parse(); }
        catch (ParseError&) {
            if (isKind(Tok::End))
                throw ParseError("Couldn't find final ')' (corresponding ( was at line " +
                                 std::to_string(openedAt) + ")", cur().line);
            throw;
        }
    };
    ExprPtr e = argOrUnterminated([&] { return parseExpression(); });
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
    // semicolon argument segments — `zip(1,2; 3,4)` / `f(@a; @b)`. Each
    // `;`-segment is ONE POSITIONAL argument and is ALWAYS a List, even when it
    // holds a single item: Rakudo gives `f(1; 2)` the capture \((1,), (2,)),
    // which is exactly why `zip(@a; @b)` zips two ONE-element lists and yields a
    // single ([1,2], [3,4]) instead of zipping the arrays element-wise.
    // A TRAILING `;` contributes a final EMPTY segment — `f(1;)` is \((1,), ()).
    // And a NAMED argument inside a segment is absorbed by that segment's own
    // list construction and VANISHES, the way List.new(x => 1, 2) is (2,): so
    // `f(x => 1, 2; 3)` is \((2,), (3,)) and `f(x => 1;)` is \((), ()) with no
    // named argument at all. That last rule is why `Check.new(tr => self.clone;)`
    // reaches the default constructor with two positionals and no `tr`.
    if (isKind(Tok::Semicolon)) {
        auto segArg = [](std::vector<ExprPtr>& items) -> ExprPtr {
            auto seg = std::make_unique<ListExpr>();
            seg->parenned = true; // one argument, not a spread
            for (auto& it : items)
                if (!syntacticNamedPair(it.get())) seg->items.push_back(std::move(it));
            return seg;
        };
        std::vector<ExprPtr> first = std::move(args);
        args.clear();
        args.push_back(segArg(first));
        while (matchKind(Tok::Semicolon)) {
            std::vector<ExprPtr> items;
            if (!isKind(Tok::RParen)) {
                ExprPtr se = parseExpression();
                if (se->kind == NK::ListExpr && !static_cast<ListExpr*>(se.get())->parenned) {
                    auto* l = static_cast<ListExpr*>(se.get());
                    for (auto& it : l->items) items.push_back(std::move(it));
                }
                else items.push_back(std::move(se));
            }
            args.push_back(segArg(items));
            if (isKind(Tok::RParen)) break;
        }
    }
    // Running out of file inside an argument list is almost always an unclosed
    // bracket several lines up, so say where it opened rather than describing the
    // token we tripped over. (`Q(oops;` reaches here now that a paren is never a
    // quote delimiter — the same mistake Rakudo reports as "couldn't find final
    // ')'".)
    if (isKind(Tok::End))
        throw ParseError("Couldn't find final ')' (corresponding ( was at line " +
                         std::to_string(openedAt) + ")", cur().line);
    expectKind(Tok::RParen, ")");
    return args;
}

// The slot an ANONYMOUS declaration gets: `my %`, `my $`, the placeholder a
// literal takes in `my ($a, "foo")`, the target of a sink `@ = (…)`. It was
// spelled `!anon`, and that second character is the PRIVATE-ATTRIBUTE twigil —
// so evaluating the declaration (which a block does, for its value) walked into
// the attribute path and died with "Variable %!anon used where no 'self' is
// available". `\x01` is the house sentinel for a name no source text can spell
// (\x01cls, \x01pun, \x01arr), which is exactly what an anonymous slot wants:
// unreachable by the twigil test above it, and unreachable by a bare `%` term,
// which in Rakudo is a FRESH hash and not the anonymous variable next to it.
static const char* kAnonSlot = "\x01" "anon";

// `4.7kΩ` with `postfix:<k>` and `postfix:<Ω>` declared: the lexer read `kΩ`
// as ONE name, since both are letters. When the name is wholly a run of
// declared postfixes, longest first, replace its token with one per postfix.
bool Parser::splitPostfixRun() {
    const std::string w = cur().text;
    std::vector<std::string> parts;
    size_t i = 0;
    while (i < w.size()) {
        size_t best = 0;
        for (const std::string& pf : userPostfix_)
            if (pf.size() > best && w.compare(i, pf.size(), pf) == 0) best = pf.size();
        if (!best) return false;
        parts.push_back(w.substr(i, best));
        i += best;
    }
    if (parts.size() < 2) return false;
    Token proto = cur();
    std::vector<Token> repl;
    for (auto& pt : parts) { Token t = proto; t.text = pt; t.spaceBefore = false; repl.push_back(t); }
    repl[0].spaceBefore = proto.spaceBefore;
    toks_.erase(toks_.begin() + pos_);
    toks_.insert(toks_.begin() + pos_, repl.begin(), repl.end());
    return true;
}

// `.:<op>` ahead, with the colon at toks_[i]: a PREFIX operator called as a
// postfix, in any of its wrappers — `<->`, `«~»`, `<<~>>`, `["~"]`, `<<'~'>>`.
bool Parser::prefixOpCallAt(size_t i) const {
    if (i + 1 >= toks_.size()) return false;
    const Token& c = toks_[i];
    const Token& n = toks_[i + 1];
    if (c.kind != Tok::Op || c.text != ":" || n.spaceBefore) return false;
    if (n.kind == Tok::QwList || n.kind == Tok::LBracket) return true;
    return n.kind == Tok::Op && !n.text.empty() && (n.text[0] == '<' || n.text == "\xC2\xAB");
}

ExprPtr Parser::parsePostfix(ExprPtr base, bool stopAtSpaceDot) {
    bool hyperNext = false;
    // A zen slice — `$x<>`, `$x[]`, `$x{}` and their dotted spellings — DECONTAINERISES.
    // It was a plain no-op here, which is right for `@a<>` and wrong for a Scalar:
    // `for $aoa<>` then walked the ONE item the Scalar is instead of the elements it
    // holds, which is how a JSON::Fast array-of-arrays came back nested one deep
    // (issue #55). It stays a no-op on `@`/`%`, which are never itemised, and when a
    // subscript or an assignment follows: `$a<>[0] = 9` and `%h<> = …` write through
    // the container, and a decontainerised value has nothing to write to.
    auto zenDecont = [&](ExprPtr b) -> ExprPtr {
        if (b->kind == NK::VarExpr) {
            const std::string& n = static_cast<VarExpr*>(b.get())->name;
            if (n.size() > 1 && (n[0] == '@' || n[0] == '%')) return b;
        }
        if (!cur().spaceBefore &&
            (cur().kind == Tok::LBracket || cur().kind == Tok::LBrace || cur().kind == Tok::QwList ||
             (cur().kind == Tok::Op && (cur().text == "<" || cur().text == "\xC2\xAB"))))
            return b;
        if (cur().kind == Tok::Op && !cur().text.empty() && cur().text.back() == '=' &&
            cur().text != "==" && cur().text != "!=" && cur().text != "<=" &&
            cur().text != ">=" && cur().text != "=:=" && cur().text != "=~=")
            return b; // `=`, `:=`, `.=`, `+=`, … every one of them targets the container
        auto u = std::make_unique<Unary>();
        u->op = "decont";
        u->operand = std::move(b);
        return u;
    };
    for (;;) {
        // A block-closing `}` at end of line ends the statement — for a POSTFIX
        // continuation just as for an infix one. `@a .= sort: { .chars }` followed
        // by a line starting `.sum given …` is two statements; reading the `.sum`
        // as a method call on the sort's block was how one Weekly Challenge
        // solution came to ask a Block for its sum. The `}` has to belong to the
        // statement being parsed: when the NEXT statement starts right after it,
        // nothing has been consumed yet and breaking here would spin forever.
        // An unspace joins the lines: see the note in parseExpr.
        if (pos_ > 0 && pos_ - 1 == lastBlockClose_ && lastBlockClose_ >= stmtStart_ &&
            cur().line != toks_[pos_ - 1].line && cur().spaceBefore)
            break;
        // ZERO-WIDTH UNSPACE before a subscript: `@row\[$j - 1]` is `@row[$j - 1]`.
        // The lexer already drops `\` before a postfix dot and before a
        // whitespace run; glued to a bracket it survives to here, where the
        // position says it cannot be a Capture literal — a term is already in
        // hand. Without this the subscript was silently dropped and the ARRAY
        // itself took part in the arithmetic (Text::Levenshtein::Damerau's
        // inner loop, which then answered a distance of 1 for every pair).
        if (isOp("\\") && !cur().spaceBefore &&
            (peek().kind == Tok::LBracket || peek().kind == Tok::LBrace ||
             peek().kind == Tok::LParen)) { advance(); continue; }
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
            if (cur().text != close) call->args.push_back(circumfixOperand(parseExpression()));
            pcfxClose_ = savedClose;
            if (cur().text == close) advance();
            else {
                // Rakudo's goal-failure: the expression inside never found its closer
                const std::string dba = "postcircumfix:sym<" + open + " " + close + ">";
                throw ParseError("Unable to parse expression in " + dba + "; couldn't find final '" + close + "'",
                                 cur().line, "X::Comp::FailGoal", {{"dba", dba}, {"goal", "'" + close + "'"}});
            }
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
                        bool marker = nx.kind == Tok::Op &&
                                      (nx.text == ">>" || nx.text == "<<" ||
                                       nx.text == "\xC2\xBB" || nx.text == "\xC2\xAB");
                        if (!marker) return false;
                        // …unless a `.` follows that marker: `@a>>[0]>>.Str` is a
                        // hyper SUBSCRIPT followed by a hyper method call, not the
                        // bracket-op infix form (whose right side is a term)
                        const Token& after = peek(k + 2);
                        return !(after.kind == Tok::Op && after.text == ".");
                    }
                }
            }
            return false;
        };
        if ((isOp(">>") || isOp("»")) && !bracketOpHyper() &&
            (peek().kind == Tok::LBracket ||
             // »<key> / »{EXPR} — a hyper HASH subscript. The hyper INFIX forms
             // (`@a »<« @b`) are lexed as ONE operator token (`>><<<`), so a bare
             // marker followed by a tight `<`/`{` can only be the subscript.
             ((peek().kind == Tok::LBrace ||
               (peek().kind == Tok::Op && peek().text == "<")) && !peek().spaceBefore) ||
             (peek().kind == Tok::Op && (peek().text == "**" || peek().text == "++" || peek().text == "--")) ||
             ((peek().kind == Tok::Op || peek().kind == Tok::Ident) &&
              (userPostfix_.count(peek().text) || peek().text == "i")) || // built-in postfix:<i>
             (peek().kind == Tok::Op && peek().text == "." &&
              peek(2).kind == Tok::Op && userPostfix_.count(peek(2).text)))) { // ».OP dotted form: symbolic only (».foo is a METHOD call)
            advance(); // the hyper marker
            // »<key> / »{EXPR}: subscript every element — `@working»<done>` is how
            // TAP collects the promise out of each in-flight job (issue #34)
            if (isKind(Tok::LBrace) || isOp("<")) {
                auto hm = std::make_unique<MethodCall>();
                hm->inv = std::move(base);
                hm->method = "map";
                auto hblk = std::make_unique<BlockExpr>();
                auto hes = std::make_unique<ExprStmt>();
                auto hix = std::make_unique<Index>();
                hix->base = std::make_unique<VarExpr>("$_");
                hix->isHash = true;
                if (matchKind(Tok::LBrace)) {
                    hix->index = parseExpression();
                    expectKind(Tok::RBrace, "}");
                }
                else {
                    advance(); // '<'
                    std::vector<std::string> words = readAngleWords(">");
                    if (words.size() == 1) hix->index = std::make_unique<StrLit>(words[0]);
                    else {
                        auto al = std::make_unique<ArrayLit>();
                        for (auto& w : words) al->items.push_back(std::make_unique<StrLit>(w));
                        hix->index = std::move(al);
                    }
                }
                hes->e = std::move(hix);
                hblk->body.push_back(std::move(hes));
                hm->args.push_back(std::move(hblk));
                base = std::move(hm);
                continue;
            }
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
                size_t dimAt = pos_;
                auto ix = std::make_unique<Index>();
                ix->base = std::make_unique<VarExpr>("$_");
                ix->index = parseExpression();
                rejectNegativeIndex(dimAt);
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
        // dot-form hyper SUBSCRIPT: @a.»<key> / @a.»[0] / @a.»{k} — the same drop
        // of the dot, so the »<…> branch above reads it. `.».Str` was handled
        // (a `.` after the marker) but a tight subscript opener was not, and
        // Color::Names' `…».value.»<name>` died "expected method name after '.'".
        if (isOp(".") && peek().kind == Tok::Op && (peek().text == "»" || peek().text == ">>") &&
            (peek(2).kind == Tok::LBracket ||
             ((peek(2).kind == Tok::LBrace ||
               (peek(2).kind == Tok::Op && peek(2).text == "<")) && !peek(2).spaceBefore))) {
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
                    zi->zen = true;
                    base = std::move(zi);
                }
                else base = zenDecont(std::move(base));
                continue;
            }
            size_t dimAt = pos_;
            auto idx = std::make_unique<Index>();
            idx->base = std::move(base);
            idx->index = parseExpression();
            rejectNegativeIndex(dimAt);
            idx->isHash = false;
            // multidim subscript @a[X;Y]: a real multislice — each dim selects at
            // its level (Whatever = all), results flattened (`@aoa[*;1]` = column 1)
            if (isKind(Tok::Semicolon)) {
                auto dims = std::make_unique<ListExpr>();
                dims->items.push_back(std::move(idx->index));
                while (matchKind(Tok::Semicolon)) {
                    if (isKind(Tok::RBracket)) break;
                    dimAt = pos_;
                    dims->items.push_back(parseExpression());
                    rejectNegativeIndex(dimAt);
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
                    zi->zen = true;
                    base = std::move(zi);
                }
                else base = zenDecont(std::move(base));
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
        } else if (isKind(Tok::QwList) && !cur().spaceBefore && cur().text.find('\n') == std::string::npos) {
            // `config<name>` — the lexer read the angles as a word list because a
            // name puts it in term position; tight against the name it is the
            // subscript `config()<name>`, not the argument `config("name")`.
            std::vector<std::string> words;
            { std::string cur_w;
              for (char c : advance().text) {
                  if (c == ' ' || c == '\t' || c == '\r') { if (!cur_w.empty()) words.push_back(cur_w); cur_w.clear(); }
                  else cur_w += c;
              }
              if (!cur_w.empty()) words.push_back(cur_w); }
            auto idx = std::make_unique<Index>();
            idx->base = std::move(base);
            idx->isHash = true;
            if (words.size() == 1) idx->index = std::make_unique<StrLit>(words[0]);
            else {
                auto al = std::make_unique<ArrayLit>();
                for (auto& w : words) al->items.push_back(std::make_unique<StrLit>(w));
                idx->index = std::move(al);
            }
            base = std::move(idx);
            continue;
        } else if (!cur().spaceBefore &&
                   (isOp("\xC2\xAB") || isOp("<<") || fusedQqww(cur()))) {
            // the interpolating word-key subscript `%h«a $x»` / `%h<<a $x>>`:
            // `%h{«…»}`. Tight on a term it is always the subscript, never a
            // hyper infix — `@a«+»@b` is "Two terms in a row" in Rakudo.
            std::vector<std::string> words;
            takeQqwwWords(words);
            if (words.empty()) { base = zenDecont(std::move(base)); continue; }
            auto idx = std::make_unique<Index>();
            idx->base = std::move(base);
            idx->isHash = true;
            idx->index = qqwwList(words);
            base = std::move(idx);
            continue;
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
            if (words.empty()) {
                // `$x<>` / `@x<>`: zen-slice / decontainerize — the value itself.
                // But an ADVERBED zen slice keeps an Index to hang the adverb on,
                // exactly as `%h{}:k` does; without it `%h<>:k` dropped the `:k`.
                if (isOp(":") && (peek().kind == Tok::Ident || peek().kind == Tok::Var ||
                                  (peek().kind == Tok::Op && peek().text == "!"))) {
                    auto zi = std::make_unique<Index>();
                    zi->base = std::move(base);
                    zi->index = std::make_unique<WhateverExpr>();
                    zi->isHash = true;
                    zi->zen = true;
                    zi->angleKey = true;   // `%h<>:k`, the ANGLE zen slice
                    base = std::move(zi);
                }
                else base = zenDecont(std::move(base));
                continue;
            }
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
        } else if ((isOp("\xC2\xBB") || isOp(">>")) && !cur().spaceBefore &&
                   peek().kind == Tok::Op && peek().text == "!" && !peek().spaceBefore &&
                   peek(2).kind == Tok::Ident && !peek(2).spaceBefore) {
            // hyper PRIVATE call: `@o»!Foo::priv` calls it on every element
            advance(); advance(); // » !
            auto mc = std::make_unique<MethodCall>();
            mc->inv = std::move(base);
            mc->method = advance().text;
            mc->bang = true;
            mc->hyper = true;
            if (isKind(Tok::LParen) && !cur().spaceBefore) { advance(); mc->args = parseCallArgs(); }
            base = std::move(mc);
            continue;
        } else if (isOp("!") && !cur().spaceBefore && !peek().spaceBefore &&
                   (peek().kind == Tok::Ident || peek().kind == Tok::StrLit || peek().kind == Tok::StrInterp)) {
            // private method call: self!method / self!"method"() / $obj!method
            advance(); // !
            auto mc = std::make_unique<MethodCall>();
            mc->inv = std::move(base);
            // `self!"$name"()` names the private method at RUN time, exactly as
            // `."$name"()` does for a public one — and the interpolation must be
            // parsed as an expression, not taken as the token's literal text
            // (which dispatched to a method spelled `!$name`, issue #43).
            bool quotedName = cur().kind == Tok::StrLit || cur().kind == Tok::StrInterp;
            if (cur().kind == Tok::StrInterp) mc->methodExpr = parsePrimary();
            else mc->method = advance().text;
            mc->bang = true; // private call: only valid with a self in scope
            // Same rule the public `."name"()` path keeps (S12): a quoted name
            // must be immediately called. Bare `self!"$m"` used to parse as a
            // no-argument call, so `self!"$m" = 5` wrote into nothing.
            if (quotedName && !isKind(Tok::LParen))
                error("indirect method call requires parentheses: $obj!'name'()");
            if (isKind(Tok::LParen)) { advance(); mc->args = parseCallArgs(); }
            else if (isOp(":") && (startsTermToken(peek()) ||
                     (peek().kind == Tok::Ident && (peek().text == "my" || peek().text == "our" || peek().text == "state")))) {
                // colon listop args on a PRIVATE call too:  self!client-setup: { … }, :$enc
                // (IO::Socket::Async::SSL) — mirrors the public `.method: args` form
                advance(); // :
                // A colon-arg list is parsed as ONE expression down past the
                // list infixes (Z/X, looser than comma), then a top-level comma
                // ListExpr is splatted into the positional args. This captures
                // `blob32.new: $H Z+ $M` (a single zip whose result is the args)
                // while `.content: 'text/plain', $body` still splits on comma.
                {
                    ExprPtr argExpr = parseExpr(BP_ZIP - 1);
                    // …but a PARENTHESISED list is one argument, not an argument
                    // list: `@q.push: ($item, $depth, $base, $result)` pushes ONE
                    // four-element list, the way `@q.push(($item, …))` does.
                    // Splatting it made Path::Finder's work queue four times too
                    // long and handed the next iteration an Int where it wanted an
                    // IO::Path. Two groups (`push: (1,2), (3,4)`) already worked —
                    // the outer comma is the argument separator there.
                    if (argExpr && argExpr->kind == NK::ListExpr &&
                        !static_cast<ListExpr*>(argExpr.get())->parenned) {
                        for (auto& it : static_cast<ListExpr*>(argExpr.get())->items)
                            mc->args.push_back(std::move(it));
                    } else if (argExpr) {
                        mc->args.push_back(std::move(argExpr));
                    }
                }
            }
            base = std::move(mc);
            continue;
        } else if (isOp(".")) {
            // A dot GLUED to an integer literal is first read as a decimal point,
            // and a decimal point must be followed by a digit. `42.abs` is still a
            // method call — an identifier right after the dot wins — but `42. abs`
            // and `42.,` are the malformed number, which is what Rakudo says about
            // them (`1.5. abs` and `1e3. abs` are fine: those literals already
            // spent their dot). The set of characters that make the dot a decimal
            // point is Rakudo's own: whitespace, `,`, `=`, `:`, a terminator, or
            // end of input.
            if (pos_ > 0 && toks_[pos_ - 1].kind == Tok::IntLit && !cur().spaceBefore) {
                // Read off the TOKEN after the dot rather than the source byte:
                // a token's own `off` is not dependable here (it drifts on some
                // files, and `42.chr` in roast's S02-names-vars/perl.t read a
                // space out of a later string literal). The one case the two
                // spellings disagree on is a `#`(…)` comment wedged between the
                // dot and the name, which Rakudo accepts and this rejects.
                const Token& nx = peek();
                const bool decimalish =
                    nx.spaceBefore || nx.kind == Tok::Comma || nx.kind == Tok::Semicolon ||
                    nx.kind == Tok::RParen || nx.kind == Tok::RBracket ||
                    nx.kind == Tok::RBrace || nx.kind == Tok::End ||
                    (nx.kind == Tok::Op && (nx.text == "=" || nx.text == ":"));
                // …but `42.:<->` is the PREFIX operator called as a postfix
                // (`.:<op>`), and the dot is that call's
                const bool opCall = prefixOpCallAt(pos_ + 1);
                if (decimalish && !opCall) {
                    // Rakudo reports this and keeps parsing, so the number of
                    // diagnostics it ends up with is what decides the exception:
                    // one alone is the IllegalDecimal itself (`42. abs`, `42.:all`,
                    // `42.=abs` — the rest of the postfix still reads), and a
                    // second error makes it a group (`42.,`, `42.:`, `42. +1`).
                    size_t at = pos_ + 1;                       // the token after the dot
                    if (toks_[at].kind == Tok::Op &&
                        (toks_[at].text == "=" || toks_[at].text == ":")) at++;
                    const bool lone = toks_[at].kind == Tok::Ident;
                    if (lone)
                        throw ParseError("Decimal point must be followed by digit", cur().line,
                                         "X::Syntax::Number::IllegalDecimal", {});
                    throw ParseError("Decimal point must be followed by digit", cur().line,
                                     "X::Comp::Group",
                                     {{"sorrow", "X::Syntax::Number::IllegalDecimal"}});
                }
            }
            // `a . b` — a dot with SPACE ON BOTH SIDES is Perl 5's string
            // concatenation, and saying so beats whatever we make of it next
            // ("Cannot invoke non-Callable value of type Int", from reading
            // `. (0)` as a CALL-ME). Spacing is what decides: `$c.(1)` glued is
            // a real CALL-ME, and `$obj .method` with space before only is an
            // ordinary method call.
            //
            // What FOLLOWS the space decides too: Raku allows whitespace on both
            // sides of a method-call dot, so `DB::Pg . new` and `$x . uc` are
            // calls, and only a non-identifier right side (`$x . $y`, `1 . 2`,
            // `$x . ($x)`) is the Perl 5 obsolescence. Erroring on the spelling
            // alone rejected DB::Pg's suite at its `my $pg = DB::Pg . new`.
            //
            // The space AFTER the dot is the whole test — space before it is not
            // required. Rakudo reads `$o. ++`, `$o. 5`, `@a. [0]` and `$t. "uc"()`
            // as the obsolete concatenation just as it reads `$o . ++`; only a
            // METHOD NAME may follow the gap. Demanding both sides let `$o. ++`
            // through to "expected method name after '.'" (roast
            // S02-lexical-conventions/minimal-whitespace.t wants X::Obsolete).
            if (peek().spaceBefore && peek().kind != Tok::Ident)
                throw ParseError("Unsupported use of . to concatenate strings. In Raku please use: ~",
                                 cur().line, "X::Obsolete", {{"old", "."}, {"replacement", "~"}});
            advance();
            bool mutate = false;
            if (isOp("=")) { advance(); mutate = true; } // .= mutating method call
            // .«key» / .<<key>> postcircumfix:  %h.«a»  ==  %h«a»
            if (!mutate && !hyperNext && !cur().spaceBefore &&
                (isOp("\xC2\xAB") || isOp("<<") || fusedQqww(cur()))) {
                std::vector<std::string> words;
                takeQqwwWords(words);
                if (words.empty()) { base = zenDecont(std::move(base)); continue; }
                auto idx = std::make_unique<Index>();
                idx->base = std::move(base);
                idx->isHash = true;
                idx->index = qqwwList(words);
                base = std::move(idx);
                continue;
            }
            // .<key> postcircumfix:  $_.<key>  ==  $_<key>
            if (isOp("<") && !cur().spaceBefore) {
                advance();
                std::vector<std::string> words = readAngleWords(">");
                if (words.empty()) { // `.<>` — the dotted zen slice, same as `<>`
                    hyperNext = false;
                    base = zenDecont(std::move(base));
                    continue;
                }
                auto keyIndex = [&](ExprPtr b) {
                    auto idx = std::make_unique<Index>();
                    idx->base = std::move(b); idx->isHash = true;
                    idx->angleKey = true;   // `%h<a>`, not `%h{'a'}` — see Index::angleKey
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
            // `@a».[0]` / `@a».{$k}` subscript EVERY element, the same way
            // `».<key>` above does — spelling the hyper with the dot went through
            // the hyper-method-call branch, which left the subscript to be read as
            // an ordinary one on the list itself. ML::TriesWithFrequencies's own
            // suite matches on `$m.values>>.[0]>>.Str`.
            auto hyperSubscript = [&](std::unique_ptr<Index> idx, ExprPtr inv) {
                idx->base = std::make_unique<VarExpr>("$_");
                auto mc2 = std::make_unique<MethodCall>();
                mc2->inv = std::move(inv);
                mc2->method = "map";
                auto blk = std::make_unique<BlockExpr>();
                auto es = std::make_unique<ExprStmt>();
                es->e = std::move(idx);
                blk->body.push_back(std::move(es));
                mc2->args.push_back(std::move(blk));
                return mc2;
            };
            if (isKind(Tok::LBrace)) {
                advance();
                if (isKind(Tok::RBrace)) { // .{} zen slice
                    advance(); hyperNext = false;
                    base = zenDecont(std::move(base));
                    continue;
                }
                auto idx = std::make_unique<Index>();
                idx->isHash = true;
                idx->index = parseExpression();
                expectKind(Tok::RBrace, "}");
                if (hyperNext) { hyperNext = false; base = hyperSubscript(std::move(idx), std::move(base)); continue; }
                idx->base = std::move(base);
                base = std::move(idx);
                continue;
            }
            if (isKind(Tok::LBracket)) {
                advance();
                if (isKind(Tok::RBracket)) { // .[] zen slice
                    advance(); hyperNext = false;
                    base = zenDecont(std::move(base));
                    continue;
                }
                size_t dimAt = pos_;
                auto idx = std::make_unique<Index>();
                idx->isHash = false;
                idx->index = parseExpression();
                rejectNegativeIndex(dimAt);
                if (isKind(Tok::Semicolon)) { // .[X;Y] multislice, same as @a[X;Y]
                    auto dims = std::make_unique<ListExpr>();
                    dims->items.push_back(std::move(idx->index));
                    while (matchKind(Tok::Semicolon)) {
                        if (isKind(Tok::RBracket)) break;
                        dimAt = pos_;
                        dims->items.push_back(parseExpression());
                        rejectNegativeIndex(dimAt);
                    }
                    idx->index = std::move(dims);
                    idx->multiDim = true;
                }
                expectKind(Tok::RBracket, "]");
                if (hyperNext) { hyperNext = false; base = hyperSubscript(std::move(idx), std::move(base)); continue; }
                idx->base = std::move(base);
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
                takeTrailingAdverbs(c->args);
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
            // `.:<->` / `.:<~>` / `.:«!»` — a PREFIX operator called as a
            // postfix: `42.:<->` is `-42`
            if (prefixOpCallAt(pos_)) {
                advance(); // :
                std::vector<std::string> w;
                if (isKind(Tok::LBracket)) {                   // `.:["~"]`
                    advance();
                    if (isKind(Tok::StrLit) || isKind(Tok::StrInterp)) w.push_back(advance().text);
                    expectKind(Tok::RBracket, "]");
                }
                else if (isKind(Tok::QwList)) w.push_back(advance().text);   // `.:«~»`
                else if (isOp("<<")) { advance(); w = readAngleWords(">>"); }
                else if (isOp("<")) { advance(); w = readAngleWords(">"); }
                else if (isOp("\xC2\xAB")) { advance(); w = readAngleWords("\xC2\xBB"); }
                else if (cur().text.size() > 4 && cur().text.compare(0, 2, "<<") == 0 &&
                         cur().text.compare(cur().text.size() - 2, 2, ">>") == 0) {
                    std::string tk = advance().text;          // fused `<<~>>`
                    w.push_back(tk.substr(2, tk.size() - 4));
                }
                else if (cur().text.size() > 2 && cur().text.back() == '>') {
                    std::string tk = advance().text;          // fused `<->`
                    w.push_back(tk.substr(1, tk.size() - 2));
                }
                else { toks_[pos_].text = cur().text.substr(1); w = readAngleWords(">"); }
                if (!w.empty()) {                              // `<<'~'>>`: the quotes go
                    std::string& n = w[0];
                    size_t a = n.find_first_not_of(" \t"), z = n.find_last_not_of(" \t");
                    n = a == std::string::npos ? "" : n.substr(a, z - a + 1);
                    if (n.size() >= 2 && (n[0] == '\'' || n[0] == '"') && n.back() == n[0])
                        n = n.substr(1, n.size() - 2);
                }
                if (!w.empty() && !w[0].empty()) {
                    auto u = std::make_unique<Unary>();
                    u->op = w[0];
                    u->operand = std::move(base);
                    base = std::move(u);
                    continue;
                }
                error("expected an operator name in '.:<…>'");
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
            char allMode = 0;
            if (isOp("*") || isOp("+")) allMode = advance().text[0];   // .*all / .+all
            else if (isOp("&")) advance(); // .&fn — best effort
            auto mc = std::make_unique<MethodCall>();
            mc->inv = std::move(base);
            mc->maybe = maybe;
            mc->allMode = allMode;
            mc->meta = metaCall;
            mc->mutate = mutate;
            mc->hyper = hyperNext; hyperNext = false;
            bool indirectName = false;
            if ((cur().kind == Tok::Var || cur().kind == Tok::Op) && cur().text == "$" &&
                peek().kind == Tok::LParen && !peek().spaceBefore) {
                // `.$( * + 42 )` — the callable is an EXPRESSION (`.&( … )` is above)
                advance(); advance(); // sigil (
                mc->methodExpr = parseExpression();
                expectKind(Tok::RParen, ")");
            } else if (cur().kind == Tok::Var) {
                // `.$var` — the var holds a Callable (or a name); computed at runtime
                mc->methodExpr = std::make_unique<VarExpr>(advance().text);
            } else if (isOp("::") && peek().kind == Tok::Ident && !peek().spaceBefore &&
                       peek().text.find("::") != std::string::npos) {
                // `.::Int::abs` — the qualified name, written with its leading `::`
                advance();
                mc->method = advance().text;
                auto q = mc->method.rfind("::");
                mc->methodQual = mc->method.substr(0, q);
                mc->method = mc->method.substr(q + 2);
            } else if (cur().kind == Tok::Ident) {
                mc->method = advance().text;
                // qualified `$obj.Class::method` — dispatch to Class's method (a
                // deliberate reach past the invocant's own override, e.g.
                // `self.Parent::meth` from within an override). Keep the class part
                // so the lookup targets it; the bare last segment is the method name.
                auto q = mc->method.rfind("::");
                if (q != std::string::npos && q + 2 < mc->method.size()) {
                    mc->methodQual = mc->method.substr(0, q);
                    mc->method = mc->method.substr(q + 2);
                }
            } else if (cur().kind == Tok::StrLit) {
                mc->method = advance().text; indirectName = true;   // ."literal-name"()
            } else if (cur().kind == Tok::StrInterp) {
                mc->methodExpr = parsePrimary(); indirectName = true; // ."$name"() — computed at runtime
            } else {
                error("expected method name after '.'");
            }
            // `$.numeric:sym<frac>($/)` — a proto-regex candidate's `:sym<…>` is
            // part of its NAME on the call as much as on the declaration, and
            // PDF::Grammar's action for `numeric:sym<real>` hands off to the
            // `frac` candidate exactly this way. Read as an adverb, the `sym`
            // became a call to an undefined routine. Tight only: a SPACED
            // `:name` after a method is the detached adverb handled below.
            if (!indirectName && isOp(":") && !cur().spaceBefore &&
                peek().kind == Tok::Ident && peek().text == "sym" &&
                peek(2).kind == Tok::Op &&
                (peek(2).text == "<" || peek(2).text == "\xC2\xAB")) {
                advance(); advance(); // : sym
                std::vector<std::string> w;
                if (isOp("<")) { advance(); w = readAngleWords(">"); }
                else { advance(); w = readAngleWords("\xC2\xBB"); }
                mc->method += ":sym<" + (w.empty() ? std::string() : w[0]) + ">";
            }
            // An indirect (quoted/computed) method name must be immediately called:
            // `$x.'foo'()` is legal, bare `$x.'foo'` is not (S12).
            if (indirectName && !isKind(Tok::LParen))
                error("indirect method call requires parentheses: $obj.'name'()");
            if (isKind(Tok::LParen) && (!cur().spaceBefore || (slang_ && slang_->spacedMethodop))) { advance(); mc->args = parseCallArgs(); takeTrailingAdverbs(mc->args); } // .method(args) — tight only; `.doit ()` is Confused (use unspace) — unless Slang::Tuxic's methodop is in force
            // a DETACHED adverb — `$sth.row :hash` — the colonpair (ident TIGHT
            // after the colon) is the call's named argument. It must be decided
            // BEFORE the colon-args form below, which was swallowing
            // `:hash, hash(...), 'msg'` whole as positional args (DBIish).
            else if (isOp(":") &&
                     (peek().kind == Tok::Ident || peek().kind == Tok::IntLit ||
                      (peek().kind == Tok::Op && peek().text == "!" && peek(2).kind == Tok::Ident)) &&
                     !peek().spaceBefore) {
                while (isOp(":") &&
                       (peek().kind == Tok::Ident || peek().kind == Tok::IntLit ||
                        (peek().kind == Tok::Op && peek().text == "!" && peek(2).kind == Tok::Ident)) &&
                       !peek().spaceBefore)
                    mc->args.push_back(parseColonPair());
            }
            else if (isOp(":") && (startsTermToken(peek()) ||
                     (peek().kind == Tok::Ident && (peek().text == "my" || peek().text == "our" || peek().text == "state")))) {
                // colon method-args:  @x.sort: -*.value   ==  @x.sort(-*.value)
                // also blocks / pointy blocks:  @x.map: { ... } / @x.map: -> $a { ... }
                // and declarator args:  @a.push: my \p = ...
                advance(); // :
                // A colon-arg list is parsed as ONE expression down past the
                // list infixes (Z/X, looser than comma), then a top-level comma
                // ListExpr is splatted into the positional args. This captures
                // `blob32.new: $H Z+ $M` (a single zip whose result is the args)
                // while `.content: 'text/plain', $body` still splits on comma.
                {
                    ExprPtr argExpr = parseExpr(BP_ZIP - 1);
                    // …but a PARENTHESISED list is one argument, not an argument
                    // list: `@q.push: ($item, $depth, $base, $result)` pushes ONE
                    // four-element list, the way `@q.push(($item, …))` does.
                    // Splatting it made Path::Finder's work queue four times too
                    // long and handed the next iteration an Int where it wanted an
                    // IO::Path. Two groups (`push: (1,2), (3,4)`) already worked —
                    // the outer comma is the argument separator there.
                    if (argExpr && argExpr->kind == NK::ListExpr &&
                        !static_cast<ListExpr*>(argExpr.get())->parenned) {
                        for (auto& it : static_cast<ListExpr*>(argExpr.get())->items)
                            mc->args.push_back(std::move(it));
                    } else if (argExpr) {
                        mc->args.push_back(std::move(argExpr));
                    }
                }
            }
            base = std::move(mc);
        } else if ((isOp("\xE2\x9A\x9B++") || isOp("\xE2\x9A\x9B--")) && !cur().spaceBefore) {
            // $x⚛++ / $x⚛-- — the atomic postfix forms return the OLD value
            std::string aop = advance().text;
            auto call = std::make_unique<Call>();
            call->name = aop == "\xE2\x9A\x9B++" ? "atomic-fetch-inc" : "atomic-fetch-dec";
            call->args.push_back(std::move(base));
            base = std::move(call);
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
        } else if (cur().kind == Tok::Ident && !cur().spaceBefore && !userPostfix_.empty() &&
                   !userPostfix_.count(cur().text) && splitPostfixRun()) {
            continue;   // the run was split into its declared postfixes; take the first
        } else if ((cur().kind == Tok::Op || cur().kind == Tok::Ident) && !cur().spaceBefore &&
                   userPostfix_.count(cur().text)) {
            // user-defined postfix operator:  5!  ==  postfix:<!>(5) — and it must
            // touch its operand: `3 !< 2` with a postfix:<!> in scope is `!<`
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
            takeTrailingAdverbs(c->args);
            base = std::move(c);
        } else {
            break;
        }
    }
    return base;
}

// skip `is trait` / `does Role` / `where ...` clauses up to '=' , ; or end
void Parser::skipTraits(bool onVarDecl, ExprPtr* defaultOut) {
    while (isIdent("is") || isIdent("does") || isIdent("returns") || isIdent("of") ||
           (isIdent("will") && peek().kind == Tok::Ident)) {
        bool wasIs = isIdent("is");
        // `my $a is default(…) of Int` — the type as a trait, in any position
        if (isIdent("of") && peek().kind == Tok::Ident && ascii::isupper((unsigned char)peek().text[0])) {
            advance();
            lastOfType_ = advance().text;
            continue;
        }
        // `will leave {…}` / `will undo {…}` / `will keep {…}` on a variable
        // declaration: capture the phaser + block; the declarator emits them as
        // a synthetic phaser statement over this variable (Log::Async's suite
        // holds a tempfile in `my $output will leave { .unlink } = tempfile`)
        if (isIdent("will")) {
            advance();
            std::string ph = isKind(Tok::Ident) ? advance().text : "";
            if (isKind(Tok::LBrace)) {
                auto blk = parseBlock();
                if (onVarDecl && (ph == "leave" || ph == "keep" || ph == "undo")) {
                    auto be = std::make_unique<BlockExpr>();
                    be->body = std::move(blk->stmts);
                    lastWillBlock_ = std::move(be);
                    lastWillPhaser_.assign(1, ascii::toupper((unsigned char)ph[0]));
                    for (size_t k = 1; k < ph.size(); k++)
                        lastWillPhaser_ += ascii::toupper((unsigned char)ph[k]);
                }
            }
            continue;
        }
        advance();
        // `is readonly` is a PARAMETER trait; on a variable declaration it's a
        // compile error (X::Comp::Trait::Unknown) — roast S03-binding/ro.t.
        if (onVarDecl && wasIs && isIdent("readonly"))
            throw ParseError("Can't use unknown trait 'is' -> 'readonly' in a variable declaration.",
                             cur().line, "X::Comp::Trait::Unknown",
                             {{"type", "is"}, {"subtype", "readonly"}, {"declaring", "variable"}});
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
            // `is Set` / `is List` — and any other TYPE name, because
            // `has %.Converter is DBDish::TypeConverter` makes the attribute an
            // instance of that type rather than a plain Hash. Recording a name
            // here is harmless on its own: the interpreter acts only on a `%`
            // sigil whose trait names the QuantHash family or a class it knows,
            // so an ordinary uppercase trait (is DEPRECATED) still does nothing.
            bool wasContainer = wasIs && containers.count(cur().text);
            bool wasTypeName = wasIs && !cur().text.empty() &&
                               ascii::isupper((unsigned char)cur().text[0]);
            if (wasContainer || wasTypeName) lastContainerIs_ = cur().text;
            if (wasIs && cur().text == "dynamic") lastIsDynamic_ = true; // my $x is dynamic
            if (wasIs && cur().text == "export") lastIsExport_ = true;   // our %x is export
            advance(); // trait name / type
            // a QUALIFIED type trait: `has %.Converter is DBDish::TypeConverter`
            // — keep consuming `::name` segments into the captured name, or the
            // container type is just its first segment (DBIish's 06-types)
            if (wasTypeName)
                while (isOp("::") && peek().kind == Tok::Ident) {
                    advance(); // ::
                    lastContainerIs_ += "::" + advance().text;
                }
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

static void rejectPackagedDynamic(const std::string& name, int line); // see below

// An EXTENDED NAME — `$today:foo<a b>:bar:baz<x>` — is one symbol whose name
// carries the adverbs. Raku lets the values be written four ways and they all
// name the SAME variable, so the name is CANONICALISED here to the angle
// spelling Rakudo stores: `:foo<a b>` however it was typed. A valueless adverb
// (`:bar`) stays bare.
//
// The suffix is recognised only where Raku recognises one: the `:` glued to the
// name and the key glued to the `:`. That space is what separates it from the
// invocant colon (`say $obj: foo` passes `foo` to `$obj`), which is spelled
// with the space after the colon, and from an adverb written apart from its
// term. Nothing is consumed when the shape does not match.
std::string Parser::readExtendedNameSuffix() {
    auto splitWords = [](const std::string& t, std::vector<std::string>& out) {
        for (size_t i = 0; i < t.size();) {
            while (i < t.size() && ascii::isspace((unsigned char)t[i])) i++;
            size_t b = i;
            while (i < t.size() && !ascii::isspace((unsigned char)t[i])) i++;
            if (i > b) out.push_back(t.substr(b, i - b));
        }
    };
    std::string out;
    while (isOp(":") && !cur().spaceBefore &&
           peek().kind == Tok::Ident && !peek().spaceBefore && !peek().text.empty()) {
        size_t save = pos_;
        advance();                                  // :
        std::string key = advance().text;
        // `my $fo:o::b:ar` — an adverb key is an IDENTIFIER, and a package path
        // is not one. The lexer folds the `::` into the identifier, so the
        // double colon arrives here as part of the key; Rakudo is Confused by
        // it and so are we (roast S02-names-vars/varnames.t).
        if (key.find("::") != std::string::npos)
            throw ParseError("Confused", cur().line, "X::Syntax::Confused", {});
        std::vector<std::string> words;
        bool haveWords = false;
        if (!cur().spaceBefore) {
            if (isKind(Tok::QwList)) { splitWords(advance().text, words); haveWords = true; }
            else if (isOp("<")) { advance(); words = readAngleWords(">"); haveWords = true; }
            // `«a b»` — the opener is its own op token and the words follow
            else if (isOp("\xC2\xAB")) { advance(); words = readAngleWords("\xC2\xBB"); haveWords = true; }
            // …but a ONE-WORD guillemet list lexes FUSED, and in the ASCII
            // spelling: `«x»` arrives as the single op token `<<x>>`
            else if (isKind(Tok::Op) && cur().text.size() >= 4 &&
                     cur().text.compare(0, 2, "<<") == 0 &&
                     cur().text.compare(cur().text.size() - 2, 2, ">>") == 0) {
                std::string t = advance().text;
                splitWords(t.substr(2, t.size() - 4), words);
                haveWords = true;
            }
            else if (isKind(Tok::LBracket) || isKind(Tok::LParen)) {
                // `:foo['a','b']` / `:foo('a','b')` — a comma list of LITERALS.
                // Anything else in there is not a name, so the whole suffix is
                // handed back to the ordinary parse.
                Tok close = isKind(Tok::LBracket) ? Tok::RBracket : Tok::RParen;
                advance();
                bool ok = true;
                while (!isKind(close) && !isKind(Tok::End)) {
                    if (isKind(Tok::StrLit) || isKind(Tok::IntLit)) words.push_back(advance().text);
                    else { ok = false; break; }
                    if (isKind(Tok::Comma)) { advance(); continue; }
                    break;
                }
                if (!ok || !isKind(close)) { pos_ = save; return out; }
                advance();                          // ] / )
                haveWords = true;
            }
        }
        out += ":" + key;
        if (haveWords) {
            out += "<";
            for (size_t i = 0; i < words.size(); i++) { if (i) out += " "; out += words[i]; }
            out += ">";
        }
    }
    return out;
}

// `my \term:<ℵ₀> = Inf` / `constant \term:<ℵ₀>` — a sigilless name spelled as
// a TERM: the name is what the angles hold, and it is written bare after.
std::string Parser::sigillessTermName(const std::string& nm) {
    if (nm != "term" || !isOp(":") || cur().spaceBefore) return nm;
    if (peek().kind == Tok::Op && (peek().text == "<" || peek().text == "\xC2\xAB") && !peek().spaceBefore) {
        advance();                                   // :
        bool guil = cur().text != "<";
        advance();                                   // < or «
        std::vector<std::string> w = readAngleWords(guil ? "\xC2\xBB" : ">");
        if (!w.empty() && !w[0].empty()) return w[0];
    }
    return nm;
}

ExprPtr Parser::parseDeclarator(const std::string& scope) {
    if (matchOp("\\")) {
        // sigilless: my \x = ...
        std::string nm = (isKind(Tok::Ident) || isKind(Tok::Var)) ? advance().text : "";
        nm = sigillessTermName(nm);
        if (!nm.empty()) sigilless_.insert(nm);
        auto ve = std::make_unique<VarExpr>(nm);
        ve->declare = true; ve->declScope = scope;
        // …traits included: `my constant \COLORS is export(:colors) = %( … )`
        // (Color::Names). Left in the token stream, the trait made the `=` look
        // like an assignment TO it — "Target is not assignable".
        lastIsExport_ = false;
        skipTraits(scope != "has", &ve->declDefault);
        // …and `is export` on the constant is a fact the interpreter needs: a
        // `unit class` body publishes nothing on its own (Color::Names::X11)
        if (lastIsExport_) { ve->declExport = true; lastIsExport_ = false; }
        return ve;
    }
    // `my multi sub NAME(…) {…}` / `my proto sub NAME(|) {*}` in expression
    // position — the routine-declaration term (parsePrefix) declares it and
    // yields the routine; the `my` adds nothing a do-block does not already give.
    if (isKind(Tok::Ident) && (cur().text == "multi" || cur().text == "proto") &&
        peek().kind == Tok::Ident && (peek().text == "sub" || peek().text == "method") &&
        peek(2).kind == Tok::Ident)
        return parsePrefix();
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
    std::string type, coerceTo;
    char declSmiley = 0;            // `Int:D` — recorded on the variable below
    ExprPtr typeExpr;               // a parameterized declared type, as `Type[args]`
    bool indirectType = false;
    // type-capture declaration:  my ::T $x  (binds T to the type of $x; we just parse it)
    if (isOp("::") && peek().kind == Tok::Ident) { advance(); advance(); indirectType = true; }
    // compile-time enclosing type as the declared type: `my ::?CLASS:U $c = …`
    // (Font::AFM). Signatures already accepted `::?CLASS`; declarations did not,
    // and stopped at the `::` with "expected variable after declarator".
    else if (isOp("::") && peek().kind == Tok::Op && peek().text == "?" &&
             peek(2).kind == Tok::Ident) {
        advance(); advance(); advance(); // :: ? CLASS
        type = typeStack_.empty() ? "Mu" : typeStack_.back();
        indirectType = true;
    }
    // the `:D` / `:U` / `:_` smiley that may follow either form
    if (indirectType && isOp(":") && peek().kind == Tok::Ident &&
        (peek().text == "D" || peek().text == "U" || peek().text == "_"))
    { advance(); advance(); }
    // SLANG-PLAN §B: `my 👍 = 42` (Slang::Emoji), `my a = 42` (Slang::Nogil) — the
    // slang's `sigilless-variable` claims the bare identifier, exactly as `my \x`
    // would. Not when what follows makes it a TYPE (`my Int $x`).
    if (slang_ && slang_->sigilless && isKind(Tok::Ident) &&
        !(peek().kind == Tok::Var || peek().kind == Tok::Ident || (peek().kind == Tok::Op && peek().text == "\\")) &&
        slangSigillessHere()) {
        std::string nm = advance().text;
        sigilless_.insert(nm);
        auto ve = std::make_unique<VarExpr>(nm);
        ve->declare = true; ve->declScope = scope;
        lastIsExport_ = false;
        skipTraits(scope != "has", &ve->declDefault);
        if (lastIsExport_) { ve->declExport = true; lastIsExport_ = false; }
        return ve;
    }
    if (isKind(Tok::Ident)) {
        bool looksType = peek().kind == Tok::Var || peek().kind == Tok::LParen ||
                         peek().kind == Tok::LBracket ||
                         (peek().kind == Tok::Ident && peek().text == "of") || // `my Array of Int @box`
                         (peek().kind == Tok::Op &&
                          (peek().text == ":" || peek().text == "\\" ||
                           (peek().text.size() == 1 && std::strchr("$@%&", peek().text[0])))); // typed anon `my Int % = …`
        if (looksType) {
            type = advance().text;
            if (isOp(":") && peek().kind == Tok::Ident && !peek().spaceBefore) {  // :D / :U / :_ smiley
                advance();
                const std::string sm = advance().text;
                if (sm != "D" && sm != "U" && sm != "_")
                    throw ParseError("Invalid type smiley ':" + sm + "' used, only ':D', ':U' and ':_' are allowed",
                                     cur().line, "X::InvalidTypeSmiley", {{"name", sm}});
                declSmiley = sm[0];
            }
            if (isKind(Tok::LBracket)) {
                // the [T] parameter group is part of the TYPE: `my CArray[uint8]
                // $hash .= new` must build a uint8 array, and dropping the group
                // left an element-typeless CArray whose every stride was 8 —
                // Digest::SHA256::Native read its digest back as int64 garbage
                const std::string baseType = type;
                const size_t brStart = pos_;
                int d = 0;
                bool plainTypeList = true;   // only names and commas inside?
                std::string ptxt;
                do {
                    if (isKind(Tok::LBracket)) d++;
                    else if (isKind(Tok::RBracket)) d--;
                    else if (!(isKind(Tok::Ident) || isKind(Tok::Comma) ||
                               (isKind(Tok::Op) && cur().text == "::")))
                        plainTypeList = false;
                    ptxt += cur().text;
                    advance();
                } while (d > 0 && !isKind(Tok::End));
                type += ptxt;
                // …but the group is only a NAME the runtime can resolve when it
                // spells types. `my BinaryHeap::MinHeap[{ $^a.tail <=> $^b.tail }] $h`
                // (Graph's priority queues) made the fiction
                // "BinaryHeap::MinHeap[{$^a.tail<=>$^b.tail}]", which matches no
                // registered class — `.new` on it threw and every method fell
                // through to a built-in. Keep the text for the type-list forms and
                // ALSO record `Type[args]` as the expression it is, so the
                // declaration can evaluate the real parameterization.
                if (!plainTypeList) {
                    const size_t brEnd = pos_;
                    pos_ = brStart;
                    advance();                       // '['
                    auto ix = std::make_unique<Index>();
                    ix->base = std::make_unique<NameTerm>(baseType);
                    ix->index = parseExpression();
                    ix->isHash = false;
                    typeExpr = std::move(ix);
                    pos_ = brEnd;                    // the text scan is authoritative
                }
            }
            // `my Array of Int @box` — of-chained element type before the variable
            while (isIdent("of") && peek().kind == Tok::Ident) { advance(); type = advance().text; }
            // coercion type `Int(Str)`: assigned values are coerced to `type`
            if (isKind(Tok::LParen) && peek().kind == Tok::Ident && peek(2).kind == Tok::RParen) {
                advance(); advance(); advance(); // ( SourceType )
                coerceTo = type;
            }
            // …with a coercion type for its SOURCE, `Str(Num(Int))`: the value
            // still lands as the outermost target
            else if (isKind(Tok::LParen) && peek().kind == Tok::Ident && peek(2).kind == Tok::LParen) {
                size_t j = pos_; int d = 0; bool ok = true;
                do {
                    const Token& tk = toks_[j];
                    if (tk.kind == Tok::LParen) d++;
                    else if (tk.kind == Tok::RParen) d--;
                    else if (tk.kind != Tok::Ident && !(tk.kind == Tok::Op && (tk.text == ":" || tk.text == "::"))) { ok = false; break; }
                    j++;
                } while (d > 0 && j < toks_.size());
                if (ok && d == 0) { pos_ = j; coerceTo = type; }
            }
            // …and the EMPTY form `Hash()` — the `(Any)` shorthand. It used to
            // fall through, leaving `()` behind as a sink expression and the
            // whole declaration in pieces: `my Hash() %options` lost %options
            // entirely (Text::Table::Simple's option builder; issue #37's
            // zef-install leg found it).
            else if (isKind(Tok::LParen) && peek().kind == Tok::RParen) {
                advance(); advance(); // ( )
                coerceTo = type;
            }
        }
    }
    // sigilless after an optional type:  my Mu \x = …   (bare `my \x` handled above)
    if (matchOp("\\")) {
        std::string nm = (isKind(Tok::Ident) || isKind(Tok::Var)) ? advance().text : "";
        nm = sigillessTermName(nm);
        if (!nm.empty()) sigilless_.insert(nm);
        auto ve = std::make_unique<VarExpr>(nm);
        ve->declare = true; ve->declScope = scope; ve->declType = type;
        // …and it can carry traits like any other declarator: Color::Names writes
        // `my constant \COLORS is export(:colors) = %( … )`. Without this the
        // trait stayed in the token stream and the `=` looked like an assignment
        // to `is export(:colors)`, which is "Target is not assignable".
        lastIsExport_ = false;
        skipTraits(scope != "has", &ve->declDefault);
        if (lastIsExport_) { ve->declExport = true; lastIsExport_ = false; }
        return ve;
    }
    // `my :($sigil, $type) := do given …` — a SIGNATURE literal standing where a
    // parenthesised declaration list would. For the positional-only shape the two
    // spellings bind the same way, so drop the `:` and let the list branch below
    // declare the variables; a signature that says something a list cannot (a
    // named or slurpy parameter) is left to the error at the end of this
    // function rather than bound to the wrong thing. PDF::COS::Tie's `method
    // raku` is written this way, and the parse error stopped all of PDF (#79).
    if (isOp(":") && peek().kind == Tok::LParen) advance();
    if (isKind(Tok::LParen)) {
        advance();
        auto list = std::make_unique<ListExpr>();
        while (!isKind(Tok::RParen) && !isKind(Tok::End)) {
            if (isKind(Tok::LParen)) { // nested destructure:  my (\a, (\b, \c)) = …
                list->items.push_back(parseDeclarator(scope));
                if (!matchKind(Tok::Comma)) break;
                continue;
            }
            // …and a sigilless item may carry a TYPE: PDF::IO::Writer unpacks an
            // indirect object as `my (UInt \obj-num, UInt \gen-num, \object) = @_`.
            // The per-item type branch below only knows `Type $x`, so the `\` was
            // reached with the type still in front of it — "expected variable in
            // declaration". A sigilless binding takes the value as it comes, so
            // the type is parsed and dropped, exactly as `my Mu \x` does.
            if (isKind(Tok::Ident) && peek().kind == Tok::Op && peek().text == "\\" &&
                (peek(2).kind == Tok::Ident || peek(2).kind == Tok::Var))
                advance();
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
            // named slurpy `my ($key, *@path) = $s.split('::')` — in a
            // DECLARATION list the star marks what a trailing @ or % already
            // does (it takes the rest of the values), so consume the marker and
            // let the variable declare itself the ordinary way below.
            if (isOp("*") && peek().kind == Tok::Var && peek().text.size() > 1 &&
                (peek().text[0] == '@' || peek().text[0] == '%')) {
                advance();
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
            std::string t2, coerce2;
            // A COERCION type on one slot — `my ($before, Int() $linenr) = …`
            // (Backtrace::Files reads a line number out of a backtrace this
            // way). The `Type()` / `Type(Src)` spelling is Ident-then-LParen,
            // which this branch took for "not a type", and the declaration died
            // "expected variable in declaration".
            if (isKind(Tok::Ident) &&
                (peek().kind == Tok::Var ||
                 peek().kind == Tok::LBracket || // parameterized: Array[UInt] $x
                 (peek().kind == Tok::LParen &&
                  (peek(2).kind == Tok::RParen ||
                   (peek(2).kind == Tok::Ident && peek(3).kind == Tok::RParen))) ||
                 (peek().kind == Tok::Op && peek().text == ":" && peek(2).kind == Tok::Ident &&
                  peek(3).kind == Tok::Var))) {
                t2 = advance().text;
                if (isKind(Tok::LBracket)) { // the [T] parameter group rides with the type
                    int d = 0;
                    std::string ptxt;
                    do {
                        if (isKind(Tok::LBracket)) d++;
                        else if (isKind(Tok::RBracket)) d--;
                        ptxt += cur().text;
                        advance();
                    } while (d > 0 && !isKind(Tok::End));
                    t2 += ptxt;
                }
                if (isOp(":") && peek().kind == Tok::Ident) { advance(); advance(); } // :D/:U smiley
                if (isKind(Tok::LParen) && peek().kind == Tok::RParen) {
                    advance(); advance();                        // ( )
                    coerce2 = t2;
                }
                else if (isKind(Tok::LParen) && peek().kind == Tok::Ident && peek(2).kind == Tok::RParen) {
                    advance(); advance(); advance();             // ( SourceType )
                    coerce2 = t2;
                }
            }
            // named destructuring element `:@positional` / `:$x` / `:%h` — binds
            // the RHS hash's value under the bare key name (Cro::HTTP::Router:
            // `my (:@positional, :@named) := $sig.params.classify: {…}`)
            if (isOp(":") && peek().kind == Tok::Var) {
                advance();
                auto ve = std::make_unique<VarExpr>(advance().text);
                ve->declare = true; ve->declScope = scope; ve->namedBind = true;
                list->items.push_back(std::move(ve));
                if (!matchKind(Tok::Comma)) break;
                continue;
            }
            // a bare TYPE in a destructuring declaration (`my ($q1, Any, $q2)`)
            // is an anonymous slot too: the position is skipped, nothing is
            // declared for it. Stats reads the first and third quartile exactly
            // so. A type NAMING a variable was handled above, so what is left
            // here is an identifier standing on its own.
            if (isKind(Tok::Ident) &&
                (peek().kind == Tok::Comma || peek().kind == Tok::RParen)) {
                advance();
                auto anon = std::make_unique<VarExpr>(std::string("$") + kAnonSlot);
                anon->declare = true; anon->declScope = scope;
                list->items.push_back(std::move(anon));
                if (!matchKind(Tok::Comma)) break;
                continue;
            }
            // a literal element in a destructuring declaration (`my ($a, "foo")`)
            // binds nothing — an anonymous slot stands in
            if (isKind(Tok::StrLit) || isKind(Tok::StrInterp) || isKind(Tok::IntLit) || isKind(Tok::NumLit)) {
                advance();
                auto anon = std::make_unique<VarExpr>(std::string("$") + kAnonSlot);
                anon->declare = true; anon->declScope = scope;
                list->items.push_back(std::move(anon));
                if (!matchKind(Tok::Comma)) break;
                continue;
            }
            if (!isKind(Tok::Var)) error("expected variable in declaration");
            std::string dnm = advance().text;
            if (dnm.size() == 1 && std::strchr("$@%&", dnm[0])) dnm += kAnonSlot; // `my ($, $b)`: unnameable, as above
            auto ve = std::make_unique<VarExpr>(dnm);
            ve->declare = true; ve->declScope = scope;
            // `my ($x?) := ()` — the signature markers. A declaration list binds
            // POSITIONALLY, and a position the right-hand side does not reach
            // already yields an undefined value, which is exactly what optional
            // means here; the marker only has to stop being a parse error
            // ("expected )"), which took all fifteen tests of
            // S02-names-vars/signature.t with it.
            if (isOp("?") || isOp("!")) advance();
            // A type written BEFORE the parenthesis applies to every variable in
            // the list — `my Int ($a, $b)` declares two Int, and `my uint32
            // ($a, $b)` two 32-bit natives that wrap on assignment. Only the
            // per-item type was being kept, so the list form silently declared
            // untyped scalars: Digest::SHA2's `my uint32 ($T1, $T2) = …` never
            // truncated, and every SHA-256 digest came out wrong.
            // `my Int (Str $x)` — a type outside AND inside the list conflict
            if (!type.empty() && !t2.empty())
                throw ParseError("Variable " + dnm + " has conflicting types " + type + " and " + t2,
                                 cur().line, "X::Syntax::Variable::ConflictingTypes",
                                 {{"outer", type}, {"inner", t2}});
            ve->declType = t2.empty() ? type : t2;
            ve->declCoerce = coerce2.empty() ? coerceTo : coerce2;  // `my Int() ($a, $b)` / `my ($a, Int() $b)`
            if (t2.empty() && declSmiley && declSmiley != '_') ve->declSmiley = declSmiley;   // `my Int:D ($x = 5)`
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
        // `my ($a, $b) is default(42)` / `is dynamic` — a trait after the list
        // applies to EVERY variable in it. Each one needs its own default
        // expression, so the traits are re-parsed once per variable.
        if (isIdent("is") || isIdent("will")) {
            size_t traitsAt = pos_, traitsEnd = pos_;
            for (auto& it : list->items) {
                VarExpr* v = dynamic_cast<VarExpr*>(it.get());
                if (!v)
                    if (auto* as = dynamic_cast<Assign*>(it.get())) v = dynamic_cast<VarExpr*>(as->target.get());
                if (!v) continue;
                pos_ = traitsAt;
                lastIsDynamic_ = false;
                skipTraits(scope != "has", &v->declDefault);
                if (lastIsDynamic_) { v->declDynamic = true; lastIsDynamic_ = false; }
                traitsEnd = pos_;
            }
            if (traitsEnd == traitsAt) skipTraits(scope != "has");
            else pos_ = traitsEnd;
            lastContainerIs_.clear(); lastContainerOf_.clear(); lastIsExport_ = false;
            lastWillPhaser_.clear(); lastWillBlock_.reset();
        }
        return list;
    }
    if (isKind(Tok::Var)) {
        {   // `my $0` — numeric names are reserved for captures; `my $!x`/`my $?X`
            // — those twigils cannot take a `my`-style scope
            const std::string& vn = cur().text;
            if (vn.size() > 1 && ascii::isdigit((unsigned char)vn[1]))
                throw ParseError("Cannot declare a numeric variable " + vn, cur().line,
                                 "X::Syntax::Variable::Numeric", {});
            // `constant $?FILE = …` — a compile-time `?` constant is not yet implemented
            if (vn.size() > 2 && vn[1] == '?' && scope == "constant")
                throw ParseError("Constants with a '?' twigil not yet implemented. Sorry.", cur().line,
                                 "X::Comp::NYI", {{"feature", "Constants with a '?' twigil"}});
            // `my $::("foo")` — an indirect name cannot be DECLARED
            if (vn.size() == 1 && peek().kind == Tok::Op && peek().text == "::")
                throw ParseError("Cannot declare a variable by indirect name (use a hash instead?)",
                                 cur().line, "X::Syntax::Variable::IndirectDeclaration", {});
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
        std::string vname = advance().text;
        // `my $x :a` — an adverb cannot hang off a declaration
        if (isOp(":") && cur().spaceBefore && peek().kind == Tok::Ident && !peek().spaceBefore &&
            (peek(2).kind == Tok::Semicolon || peek(2).kind == Tok::End))
            throw ParseError("You can't adverb " + vname, cur().line, "X::Syntax::Adverb",
                             {{"what", vname}});
        // `my @a()` / `my &a()` — the ()-shape syntax is reserved
        if (isKind(Tok::LParen) && !cur().spaceBefore && vname.size() > 1 &&
            (vname[0] == '@' || vname[0] == '&')) {
            if (vname[0] == '@')
                throw ParseError("The ()-shape syntax in array declarations is reserved", cur().line,
                                 "X::Syntax::Reserved", {{"reserved", "()-shape syntax in array declarations"},
                                                         {"instead", "[]"}});
            throw ParseError("The ()-shape syntax in routine declarations is reserved (maybe use :() to declare a longname?)",
                             cur().line, "X::Syntax::Reserved",
                             {{"reserved", "()-shape syntax in routine declarations"}, {"instead", " (maybe use :() to declare a longname?)"}});
        }
        rejectPackagedDynamic(vname, cur().line);   // `my $*FOO::BAR` — see the helper
        vname += readExtendedNameSuffix();          // `my $today:foo<a b>` — one symbol
        // `my &infix:<plus> = sub ($a, $b) {…}` — an OPERATOR declared by
        // assigning to a code variable. It is the spelling a module reaches for
        // when it builds its operators inside `sub EXPORT` (every one of the
        // natural-language modules does: `my &infix:<加>`, `&infix:<plus>`,
        // `&infix:<más>`), and it has to reach the operator table exactly as
        // `sub infix:<plus>` does — the name is known HERE, at the declaration,
        // even though the routine behind it only exists at run time.
        //
        // Without it the operator was never registered, so `2 plus 3` parsed as
        // two statements — `2`, then a call to `plus(3)` — and the expression
        // silently answered its LEFT operand instead of failing.
        registerOperatorVarName(vname);
        // A BARE sigil lands here too when the lexer Var-lexed it (`my $`, `my @`,
        // and `my %`/`my &` where a `=` follows tight). Named by the sigil alone
        // the anonymous variable was NAMEABLE: after `my @ = 1, 2` a later bare
        // `@` term found it and answered [1, 2], where Rakudo's bare `@` is a
        // fresh empty Array every time. Same kAnonSlot sentinel as the
        // bare-sigil branch below, and the shape/key-type/trait machinery of
        // this branch keeps working — name[0] is still the sigil.
        if (vname.size() == 1 && std::strchr("$@%&", vname[0])) vname += kAnonSlot;
        auto ve = std::make_unique<VarExpr>(vname);
        ve->declare = true; ve->declScope = scope; ve->declType = type; ve->declCoerce = coerceTo;
        ve->declTypeExpr = std::move(typeExpr);
        // the smiley: written on the type, else the `use variables` default
        // (which applies to a TYPED declaration only)
        if (declSmiley) ve->declSmiley = declSmiley;
        else if (varsPragma_ && !type.empty() && (scope == "my" || scope == "our" || scope == "state")) {
            ve->declSmiley = varsPragma_; ve->declSmileyImplicit = true;
        }
        if (ve->declSmiley == '_') ve->declSmiley = 0;
        // shaped array `my @a[3]` / `my @a[2;2]`: the `[...]` right after the sigil
        // (no space) is a dimension list, not a subscript. Semicolons separate dims.
        if (ve->name[0] == '@' && isKind(Tok::LBracket) && !cur().spaceBefore) {
            advance(); // '['
            auto dims = std::make_unique<ListExpr>();
            dims->semicolon = true;
            while (!isKind(Tok::RBracket) && !isKind(Tok::End)) {
                // `my @a[;]` — an empty dimension: Rakudo declares that without a
                // shape rather than looping on it
                if (isKind(Tok::Semicolon)) { advance(); continue; }
                // `my @arr[*]` — a dimension of ANY size: that is an ordinary,
                // auto-extending array, so it declares no shape at all
                if (isOp("*") && (peek().kind == Tok::RBracket)) { advance(); continue; }
                dims->items.push_back(parseExpr(BP_COMMA + 1));
                if (isKind(Tok::Semicolon) || isKind(Tok::Comma)) { advance(); continue; } // `[3;3]` or `[3,3]`
                break;
            }
            expectKind(Tok::RBracket, "]");
            if (!dims->items.empty())
                ve->declShape = dims->items.size() == 1 ? std::move(dims->items[0]) : std::move(dims);
        }
        // `%a{Str}` — hash key-type shape declaration; the key type may carry a
        // smiley: `state %converter-for-type{Any:U}` (Getopt::Long). Unconsumed,
        // the `{Any:U}` became a SUBSCRIPT on the fresh variable and the whole
        // initializer landed under one key.
        std::string keyType;
        if (isKind(Tok::LBrace) && peek().kind == Tok::Ident &&
            (peek(2).kind == Tok::RBrace ||
             (peek(2).kind == Tok::Op && peek(2).text == ":" && peek(3).kind == Tok::Ident &&
              peek(4).kind == Tok::RBrace))) {
            advance(); keyType = advance().text;
            if (isOp(":")) { advance(); advance(); } // :U / :D / :_ on the key type
            advance(); // }
        }
        // `of Type` postfix trait sets the value/element type
        if (isIdent("of") && peek().kind == Tok::Ident) { advance(); ve->declType = advance().text; }
        // Hash[valueType,keyType] — an object hash with no explicit value type is
        // Hash[Any, KeyType], so a missing key answers Any (Rakudo)
        // An object hash with no declared VALUE type takes Mu from 6.e and Any
        // before it — `my %h{Str}; %h<nope>.WHAT` is Mu there, Any here.
        if (!keyType.empty())
            ve->declType = (ve->declType.empty() ? (langRev_ >= 2 ? "Mu" : "Any") : ve->declType) + "," + keyType;
        lastContainerIs_.clear(); lastContainerOf_.clear(); lastIsDynamic_ = false; lastIsExport_ = false;
        lastWillPhaser_.clear(); lastWillBlock_.reset(); lastOfType_.clear();
        skipTraits(scope != "has", &ve->declDefault);
        if (!lastOfType_.empty()) {
            if (ve->declType.empty() || ve->declType.find(',') == std::string::npos) ve->declType = lastOfType_;
            lastOfType_.clear();
        }
        if (!lastContainerIs_.empty()) { ve->containerIs = lastContainerIs_; lastContainerIs_.clear(); }
        if (lastIsDynamic_) { ve->declDynamic = true; lastIsDynamic_ = false; }
        if (lastIsExport_) { ve->declExport = true; lastIsExport_ = false; }
        if (!lastContainerOf_.empty()) { ve->containerOf = lastContainerOf_; lastContainerOf_.clear(); }
        if (lastWillBlock_) {
            // desugar `my $x will leave {…}` into `LEAVE {…}($x)` right after
            // this statement — the block loops flush pendingStmts_; calling the
            // block with the variable binds it as the phaser body's topic
            auto call = std::make_unique<Call>();
            call->callee = std::move(lastWillBlock_);
            call->args.push_back(std::make_unique<VarExpr>(ve->name));
            auto ph = std::make_unique<Block>();
            ph->phaser = lastWillPhaser_;
            auto es = std::make_unique<ExprStmt>(); es->e = std::move(call);
            ph->stmts.push_back(std::move(es));
            pendingStmts_.push_back(std::move(ph));
            lastWillPhaser_.clear(); lastWillBlock_.reset();
        }
        // `my $a is default(42) where * == 42` — constraint parsed, not yet enforced
        if (isIdent("where")) { advance(); parseExpr(BP_ASSIGN + 1); ve->declHasWhere = true; }
        return ve;
    }
    if (scope == "constant" && isKind(Tok::Ident)) {
        std::string cname = advance().text;
        // A sigilless constant is a TERM, so `CSI ~ $s` is an infix concat —
        // without this the name reads as a listop and swallows the `~` as a
        // prefix, giving "Undefined routine 'CSI'" (Terminal::ANSI does this).
        sigilless_.insert(cname);
        auto ve = std::make_unique<VarExpr>(cname);
        ve->declare = true; ve->declScope = scope;
        lastIsExport_ = false;
        skipTraits();
        // `constant \COLORS is export(:colors) = …` — the trait was skipped and
        // its flag left set for the NEXT declaration to pick up; a constant in
        // a `unit class` body is published nowhere else (Color::Names::X11)
        if (lastIsExport_) { ve->declExport = true; lastIsExport_ = false; }
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
        auto ve = std::make_unique<VarExpr>(sig + kAnonSlot);
        ve->declare = true; ve->declScope = scope; ve->declType = type;
        // anonymous object hash `my %{Str:D} is default(…)` — the braced key type
        if (sig == "%" && isKind(Tok::LBrace) && peek().kind == Tok::Ident) {
            advance();
            std::string keyType = advance().text;
            if (isOp(":") && peek().kind == Tok::Ident) { advance(); advance(); } // :D/:U smiley
            matchKind(Tok::RBrace);
            ve->declType = (ve->declType.empty() ? (langRev_ >= 2 ? "Mu" : "Any") : ve->declType) + "," + keyType;
        }
        lastContainerIs_.clear(); lastContainerOf_.clear(); lastIsDynamic_ = false;
        skipTraits(scope != "has", &ve->declDefault);
        if (lastIsDynamic_) { ve->declDynamic = true; lastIsDynamic_ = false; }
        if (!lastContainerOf_.empty()) { ve->containerOf = lastContainerOf_; lastContainerOf_.clear(); }
        // `my % is Hash::Ordered = …` — an ANONYMOUS variable takes the container
        // trait exactly as a named one does; dropping it left a plain Hash, and the
        // assignment that follows never reached the container type's STORE.
        if (!lastContainerIs_.empty()) { ve->containerIs = lastContainerIs_; lastContainerIs_.clear(); }
        return ve;
    }
    // A BAREWORD where the variable should be is the sigilless slip — `my Int a`
    // — and Rakudo names the two ways to write what was meant. The generic
    // "expected variable after declarator" said none of that and came out as
    // X::Syntax::Confused, which is not the type the spec tests ask for
    // (S02-names-vars/varnames.t matches the word "sigilless").
    if (isKind(Tok::Ident)) {
        std::string nm = cur().text;                 // `my Int a`: the LAST bareword
        for (int k = 1; peek(k).kind == Tok::Ident; k++) nm = peek(k).text;
        const std::string what = scope + " (did you mean to declare a sigilless \\" +
                                 nm + " or $" + nm + "?)";
        throw ParseError("Malformed " + what, cur().line,
                         "X::Syntax::Malformed", {{"what", what}});
    }
    // `constant * = 3` — a constant needs a NAME
    if (scope == "constant")
        throw ParseError("Missing constant name", cur().line, "X::Syntax::Missing", {{"what", "constant name"}});
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
// A CHAIN of pseudo-packages names the same scope-chain lookup as its last
// component: `OUTER::MY::<$x>` is a lexical, `CALLER::DYNAMIC::<$*x>` a dynamic.
// The P5 family's export tests ask `OUTER::MY::<<$name>>:exists` of every symbol
// a module claims to export, so the chain form is not exotic.
static bool isPseudoPkgPath(const std::string& p, std::string& effective) {
    if (p.empty()) return false;
    // `::CALLERS::<&x>` — a leading `::` names the same root (Red writes it so)
    size_t start = p.size() > 2 && p.compare(0, 2, "::") == 0 ? 2 : 0;
    while (true) {
        size_t sep = p.find("::", start);
        std::string comp = sep == std::string::npos ? p.substr(start)
                                                    : p.substr(start, sep - start);
        if (!isPseudoPkg(comp)) return false;
        effective = comp;
        if (sep == std::string::npos) return true;
        start = sep + 2;
    }
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
// A `*`-twigil name may not be package-qualified: Rakudo refuses every
// `$*FOO::BAR`, and refuses it for the pseudo-packages too (`$*PROCESS::IN`,
// `$*CALLER::x`) — a dynamic is looked up along the CALL chain, which is not
// where a package name points. Both the declaration and the use are errors
// (roast S02-names-vars/contextual.t asks for each).
static void rejectPackagedDynamic(const std::string& name, int line) {
    if (name.size() < 3 || !std::strchr("$@%&", name[0]) || name[1] != '*') return;
    if (name.find("::", 2) == std::string::npos) return;
    throw ParseError("Dynamic variables cannot have package-like names (with '::'), so\n'" +
                     name + "' is not allowed.", line, "X::Dynamic::Package",
                     {{"symbol", name}});
}

// A pseudo-package angle access `MY::<$x>` / `CORE::<&not>` — symbol via scope chain.
static std::string pseudoAngleSymbol(const std::string& pkg, const std::string& sym) {
    if (sym.empty()) return "$_";
    std::string s = sym;
    if (pkg == "PROCESS" && s.size() > 1 &&
        std::strchr("$@%&", s[0]) && !std::strchr("*!?.^", s[1])) s = s.substr(0, 1) + "*" + s.substr(1);
    // GLOBAL::<$x> is the PACKAGE variable `$GLOBAL::x` — assignable before
    // anything declares it, as `$GLOBAL::x = …` already is — not a lexical `$x`
    else if (pkg == "GLOBAL" && s.size() > 1 && s[0] == '$' && !std::strchr("*!?.^:", s[1]))
        s = "$GLOBAL::" + s.substr(1);
    return s;
}

static ExprPtr angleWordNumeric(const std::string& w); // defined below

// A colon-pair written directly after a call's closing paren is one of that
// call's ARGUMENTS: `blob-from-carray($au):12size` passes `size => 12`
// (NativeHelpers::Blob). It has to be tight — a spaced `:foo` starts something
// else — and stacking is allowed (`f($x):a:b`).
void Parser::takeTrailingAdverbs(std::vector<ExprPtr>& args) {
    while (isOp(":") && !cur().spaceBefore &&
           (peek().kind == Tok::Ident || peek().kind == Tok::IntLit ||
            (peek().kind == Tok::Op && peek().text == "!" && peek(2).kind == Tok::Ident)) &&
           !peek().spaceBefore) {
        args.push_back(parseColonPair());
    }
}

ExprPtr Parser::parseColonPair() {
    // ':' already current
    advance();
    // object-hash literal `:{ :42a, ... }` — the brace content is a hash
    // composer, but the result is an OBJECT hash: its keys keep their own type,
    // so `:{ 1 => "a" }.keys[0]` is the Int 1 and not "1" (Nil-Any sheet NA-33,
    // which needs `classify`'s result to compare equal to one of these).
    if (isKind(Tok::LBrace) && !cur().spaceBefore) {
        advance();
        auto u = std::make_unique<Unary>();
        u->op = "ctx%{}";
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
        // …and the NUMBER is the whole value: `:69th($_)` has nowhere to put the
        // parenthesised one, so Raku refuses it rather than calling the 69.
        if (isKind(Tok::LParen) && !cur().spaceBefore)
            throw ParseError("Extra argument not allowed with a numeric adverb",
                             cur().line, "X::Comp::AdHoc", {});
        return pair;
    }
    if (isKind(Tok::Var)) {
        // :$x -> x => $x ; a twigil is dropped from the KEY (`:$^radius` is
        // `radius => $^radius`, `:$*foo` is `foo => $*foo`) but kept in the value.
        std::string vn = cur().text;
        advance();
        // The `<` twigil — a MATCH CAPTURE as an adverb: `:$<fee>` is
        // `fee => $<fee>`, and `:@<fie>` / `:%<foe>` take that capture's list
        // / hash. The lexer leaves the bare sigil as its own Var and opens the
        // subscript separately, so the capture NAME is the key and the whole
        // subscript the value.
        if ((vn == "$" || vn == "@" || vn == "%") && isOp("<") && !cur().spaceBefore &&
            peek().kind == Tok::Ident && peek(2).kind == Tok::Op && peek(2).text == ">") {
            pair->key = peek().text;
            pair->value = parsePostfix(std::make_unique<VarExpr>(vn), false);
            return pair;
        }
        std::string key = vn.size() > 1 ? vn.substr(1) : vn;
        if (!key.empty() && std::strchr("^.!*?:=~", key[0])) key = key.substr(1);
        pair->key = key;
        pair->value = std::make_unique<VarExpr>(vn);
        return pair;
    }
    // radix literal: :16<FF> / :2<1010> / :8<777>  (a number written in the given base)
    // …and in guillemets, `:10«42»` / `:16<<2a>>`, which roast's val.t reads as
    // the same number (and which the «…» SUBSCRIPT would otherwise take).
    if (isKind(Tok::IntLit) && peek().kind == Tok::Op && !peek().spaceBefore &&
        (peek().text == "<" || peek().text == "\xC2\xAB" || peek().text == "<<" || fusedQqww(peek()))) {
        int base = std::atoi(cur().text.c_str());
        if (base < 2 || base > 36)
            throw ParseError("Radix " + std::to_string(base) +
                             " out of range (allowed: 2..36)", cur().line,
                             "X::Syntax::Number::RadixOutOfRange",
                             {{"radix", std::to_string(base)}});
        advance(); // radix
        std::vector<std::string> words;
        if (isOp("<")) { advance(); words = readAngleWords(">"); }
        else takeQqwwWords(words);
        std::string digits = words.empty() ? "" : words[0];
        // exponent form `:16<dead_beef*16**8>` / `:2<1.1*10**10>`: the part after
        // `*` is a base**exp multiplier, applied as a runtime multiplication so
        // BigInt-range results work
        std::string expPart;
        size_t star = digits.find('*');
        if (star != std::string::npos) { expPart = digits.substr(star + 1); digits = digits.substr(0, star); }
        long long val = 0, frac = 0, fdiv = 1; bool infrac = false;
        bool overflowed = false; std::vector<int> intDigits; // exact recompute on long-long overflow
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
                if (uniDigitValue(cp) >= 0) d = uniDigitValue(cp); // never-cut digit table
            }
            if (d < 0 || d >= base)
                throw ParseError("Malformed radix number", cur().line,
                                 "X::Syntax::Malformed", {{"what", "radix number"}});
            if (infrac) { frac = frac * base + d; fdiv *= base; }
            else {
                long long nv;
                if (rakupp::mul_ovf(val, (long long)base, &nv) || rakupp::add_ovf(nv, d, &nv))
                    overflowed = true; // recomputed exactly below (like 0x… literals)
                else val = nv;
                intDigits.push_back((int)d);
            }
        }
        ExprPtr numNode;
        if (infrac) { // fractional radix → a Rat: (val*fdiv + frac) / fdiv
            auto nl = std::make_unique<NumLit>((double)(val * fdiv + frac) / (double)fdiv);
            nl->isRat = true; nl->ratNum = val * fdiv + frac; nl->ratDen = fdiv;
            numNode = std::move(nl);
        }
        else if (overflowed) {
            // wider than long long: recompute exactly in decimal, the same way an
            // over-wide 0x…/0b… literal is handled (`:16<FFFF_FFFF_FFFF_FFFF>`)
            std::string dec = "0";
            auto mulAdd = [](const std::string& n, int m, int add) { // decimal long multiply
                std::string r; int carry = add;
                for (int i = (int)n.size() - 1; i >= 0; i--) {
                    int p = (n[i] - '0') * m + carry;
                    r += (char)('0' + p % 10); carry = p / 10;
                }
                while (carry) { r += (char)('0' + carry % 10); carry /= 10; }
                while (r.size() > 1 && r.back() == '0') r.pop_back();
                std::reverse(r.begin(), r.end());
                return r.empty() ? std::string("0") : r;
            };
            for (int d : intDigits) dec = mulAdd(dec, base, d);
            auto il = std::make_unique<IntLit>(0);
            il->big = dec;
            numNode = std::move(il);
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
    // radix with a digit LIST: :256[a, b, c] is ((a·256)+b)·256+c — place
    // values in the given base (`:256[|@^a]` packs bytes into a word in
    // Digest::SHA1)
    if (isKind(Tok::IntLit) && peek().kind == Tok::LBracket && !peek().spaceBefore) {
        // the base may be past 64 bits (`:18446744073709551616[1,1]`): it then
        // travels as its digit string, and the runtime works in BigInt
        const std::string baseText = cur().text;
        const bool bigBase = baseText.size() > 18;
        long long base = bigBase ? 0 : std::strtoll(baseText.c_str(), nullptr, 10);
        if (!bigBase && base < 2)
            throw ParseError("Radix " + std::to_string(base) + " out of range",
                             cur().line, "X::Syntax::Number::RadixOutOfRange",
                             {{"radix", std::to_string(base)}});
        advance(); advance(); // radix and '['
        auto c = std::make_unique<Call>();
        c->name = "__radix-list";
        if (bigBase) c->args.push_back(std::make_unique<StrLit>(baseText));
        else c->args.push_back(std::make_unique<IntLit>(base));
        if (!isKind(Tok::RBracket))
            for (;;) {
                c->args.push_back(parseExpr(BP_ASSIGN));
                if (isKind(Tok::Comma)) { advance(); continue; }
                break;
            }
        expectKind(Tok::RBracket, "]");
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
            // a STATEMENT MODIFIER inside the value parens — `:title(S/…// given $s)`,
            // `:t(5 if $ok)`. The plain-paren path has always taken the whole
            // chain; a colonpair's value stopped at the modifier keyword and
            // reported "expected )". Data::Dump::Tree writes its titles this way.
            ExprPtr v = applyExprModifiers(parseExpression());
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
        // The lexer can FUSE a whole angle value into one operator token —
        // `:replacement<->` arrives as the rw-pointy arrow `<->`, `:x<=>` as
        // the spaceship, `:x<+>` as a set op. Right after a colonpair NAME
        // with no space, such a token is the angle-quoted VALUE and its middle
        // is the word (fez calls `.decode('ascii', :replacement<->)`; issue #37).
        if (cur().kind == Tok::Op && !cur().spaceBefore && cur().text.size() > 2 &&
            cur().text.front() == '<' && cur().text.back() == '>') {
            std::string middle = cur().text.substr(1, cur().text.size() - 2);
            advance();
            auto al = angleWordNumeric(middle);
            if (al) {
                auto allo = std::make_unique<AllomorphLit>();
                allo->num = std::move(al); allo->str = middle;
                pair->value = std::move(allo);
            }
            else pair->value = std::make_unique<StrLit>(middle);
            return pair;
        }
        // …and the DOUBLE-angle form beside it: `:author<<Richard Hainsworth,
        // aka finanalyst>>` is a pair whose value is the word quote, and only
        // the single-angle spelling was accepted, so RakuDoc's Hilite plugin
        // died on its own `=begin pod` metadata.
        if ((isOp("<") || isOp("<<") || isOp("\xC2\xAB")) && !cur().spaceBefore) {
            // `:person«$user»` is the guillemet spelling of `<<…>>`
            const bool guil = cur().text == "\xC2\xAB";
            const bool dbl = cur().text == "<<" || guil;
            const char* closer = guil ? "\xC2\xBB" : dbl ? ">>" : ">";
            advance();
            std::vector<std::string> words = readAngleWords(closer);
            // a single numeric word is an allomorph, same as the term form:
            // :chmod<0o777> passes IntStr 511, not the string "0o777"
            // …and the DOUBLE-angle form INTERPOLATES (it is the `qqw` quote),
            // which the single-angle one does not — so a word carrying a sigil
            // goes through the string scanner rather than in as a literal.
            auto mkWord = [&](const std::string& w) -> ExprPtr {
                if (dbl && w.find_first_of("$@%&") != std::string::npos)
                    return parseInterpString(w);
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
            else { auto al = std::make_unique<ArrayLit>(); al->isList = true; for (auto& w : words) al->items.push_back(mkWord(w)); pair->value = std::move(al); }
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
        double re = cnum::strtod(s, &end);
        if (end != s && end < s + w.size() - 1 && (*end == '+' || *end == '-')) {
            char* end2 = nullptr;
            double im = cnum::strtod(end, &end2);
            if (end2 == s + w.size() - 1) {
                auto nl = std::make_unique<NumLit>(re);
                auto ni = std::make_unique<NumLit>(im); ni->imaginary = true;
                ni->raw = "<" + w + ">"; // marks the literal; see checkLiteralDeclType
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
    while (i < w.size() && ascii::isdigit((unsigned char)w[i])) i++;
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
        while (i < w.size() && ascii::isdigit((unsigned char)w[i])) i++;
        if (i == j || i != w.size()) return nullptr;
        std::string denPart = w.substr(j, i - j);
        return mkRat(intPart, denPart,
                     (neg ? -1.0 : 1.0) * cnum::strtod(intPart.c_str(), nullptr) /
                     std::max(1.0, cnum::strtod(denPart.c_str(), nullptr)));
    }
    if (i < w.size() && w[i] == '.') { // decimal  <4.25> / <.5>
        size_t j = ++i;
        while (i < w.size() && ascii::isdigit((unsigned char)w[i])) i++;
        if (i == j) return nullptr;
        if (i == w.size()) {
            std::string frac = w.substr(j, i - j);
            return mkRat(intPart + frac, "1" + std::string(frac.size(), '0'),
                         (neg ? -1.0 : 1.0) * cnum::strtod((intPart + "." + frac).c_str(), nullptr));
        }
    }
    if (i < w.size() && (w[i] == 'e' || w[i] == 'E')) { // e-notation  <1e5> <4.5e-2>
        errno = 0; char* end = nullptr;
        double v = cnum::strtod(w.c_str(), &end);
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
        // …counting NESTED brackets: `&[R[~~]]` is the R metaop over `[~~]`, and
        // stopping at the first `]` left a stray one behind. Test::Run flips its
        // comparison operator that way.
        int inner = 0;
        // `&[doesntexist]` — a lone word that names no infix
        if (isKind(Tok::Ident) && peek().kind == Tok::RBracket) {
            Token probe = cur();
            if (!classifyInfix(probe).valid && !userInfix_.count(probe.text) && !wordInfixSubs_.count(probe.text) &&
                !userDeclarators_.count(probe.text))
                throw ParseError("Missing infix inside []", cur().line, "X::Syntax::Missing",
                                 {{"what", "infix inside []"}});
        }
        while (!isKind(Tok::End)) {
            if (isKind(Tok::RBracket)) { if (!inner) break; inner--; }
            else if (isKind(Tok::LBracket)) inner++;
            // `&[«+»]`: the lexer made «+» a qw-list — restore its markers
            if (isKind(Tok::QwList)) op += "\xC2\xAB" + advance().text + "\xC2\xBB";
            else op += advance().text;
        }
        expectKind(Tok::RBracket, "]");
        // the nested metaop spelling normalises to the flat name this engine
        // uses: `R[~~]` is `R~~`, the same operator either way
        if (op.size() > 3 && op[1] == '[' && op.back() == ']' &&
            (op[0] == 'R' || op[0] == 'X' || op[0] == 'Z' || op[0] == 'S'))
            op = op[0] + op.substr(2, op.size() - 3);
        return std::make_unique<VarExpr>("&infix:<" + hyperMarkersToUni(op) + ">");
    }
    // user circumfix operator: `⟦ … ⟧`  ==  circumfix:<⟦ ⟧>( … )
    if ((cur().kind == Tok::Op || cur().kind == Tok::Ident) && userCircumfix_.count(cur().text)) {
        std::string open = advance().text, close = userCircumfix_[open];
        auto call = std::make_unique<Call>();
        call->name = "circumfix:<" + open + " " + close + ">";
        if (cur().text != close) call->args.push_back(circumfixOperand(parseExpression()));
        else {
            // empty brackets still pass ONE argument — the empty list the term
            // between them evaluates to (`⦃ ⦄` is `\(())`, not `\()`)
            auto empty = std::make_unique<ListExpr>();
            empty->parenned = true;
            call->args.push_back(std::move(empty));
        }
        if (cur().text == close) advance();
        else {
            const std::string dba = "circumfix:sym<" + open + " " + close + ">";
            throw ParseError("Unable to parse expression in " + dba + "; couldn't find final '" + close + "'",
                             cur().line, "X::Comp::FailGoal", {{"dba", dba}, {"goal", "'" + close + "'"}});
        }
        return call;
    }
    // ::?CLASS / ::?ROLE / ::?PACKAGE — the lexically-enclosing type (compile-time)
    if (isOp("::") && peek().text == "?" && peek(2).kind == Tok::Ident) {
        advance(); advance(); // :: ?
        std::string which = advance().text; // CLASS / ROLE / PACKAGE
        std::string nm = typeStack_.empty() ? "" : typeStack_.back();
        // ::?CLASS inside a ROLE is GENERIC — it means the CONSUMING class, known
        // only per invocant. Emit a runtime marker the interpreter resolves from
        // `self` (DBDish::Connection's `method new` does `::?CLASS.bless`, and
        // baking the role's own name built role-punned connections).
        if (which == "CLASS" && !typeIsRole_.empty() && typeIsRole_.back())
            // …with the ROLE's own name carried alongside, for the one place the
            // invocant cannot answer: the role BODY itself, which runs before any
            // class consumes it. `my &AT-KEY := ::?CLASS.^find_method('AT-KEY')`
            // at role-body level resolved to Mu and found nothing, so
            // WriteOnceHash called something that answered the hash rather than
            // the element. (Inside a METHOD `self` still decides, as before.)
            return std::make_unique<NameTerm>(nm.empty() ? std::string("::?CLASS")
                                                         : "::?CLASS\x01" + nm);
        return std::make_unique<NameTerm>(nm.empty() ? "Mu" : nm);
    }
    // root symbol access: `::<$x>` — the symbol looked up through the scope chain
    if (isOp("::") && peek().kind == Tok::Op && peek().text == "<" && !peek().spaceBefore) {
        advance(); advance(); // :: <
        std::vector<std::string> words = readAngleWords(">");
        std::string symName = words.empty() ? "$_" : words[0];
        // `::<EXPORT>:exists` / `::<$x>:!exists` — a namespace EXISTENCE probe,
        // not a lookup (Test::Async guards its EXPORT re-export machinery with
        // exactly this; unparsed, the ':' died as "expected ) (got ':')")
        if (isOp(":") && !cur().spaceBefore &&
            ((peek().kind == Tok::Ident && peek().text == "exists") ||
             (peek().kind == Tok::Op && peek().text == "!" &&
              peek(2).kind == Tok::Ident && peek(2).text == "exists"))) {
            advance(); // :
            bool neg = isOp("!");
            if (neg) advance();
            advance(); // exists
            auto u = std::make_unique<Unary>();
            u->op = neg ? "sym!exists" : "symexists";
            u->operand = std::make_unique<StrLit>(symName);
            return u;
        }
        return std::make_unique<VarExpr>(symName);
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
            sigRetType_.clear();
            be->params = parseSignature();
            // `:(Int --> Str)` — the return type belongs to the SIGNATURE here,
            // and it was parsed and then dropped, so `.returns` answered Mu and
            // `.gist` left the `-->` out. A `sub (--> Int) {…}` two hundred lines
            // below already read it back the same way.
            be->retType = sigRetType_;
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
    // `$::Foo::Bar::($name)` — a sigil, then `::`, then a PACKAGE PATH, then the
    // runtime name. The lexer splits that into `$` `::` `Foo::Bar::` `(`, so the
    // rule above (one Var token carrying the whole qualifier) never matched it and
    // the package path was parsed as a term that the parens then tried to CALL.
    // Date::Names walks its own tables with `$::Date::Names::en::($n)`.
    if (cur().kind == Tok::Var && cur().text.size() == 1 &&
        std::strchr("$@%&", cur().text[0]) &&
        peek().kind == Tok::Op && peek().text == "::" &&
        peek(2).kind == Tok::Ident && peek(3).kind == Tok::LParen &&
        peek(2).text.size() > 2 &&
        peek(2).text.compare(peek(2).text.size() - 2, 2, "::") == 0) {
        auto sr = std::make_unique<SymbolicRef>();
        sr->sigil = advance().text;   // $ / @ / % / &
        advance();                    // ::
        sr->pkg = advance().text;     // Foo::Bar::
        while (!sr->pkg.empty() && sr->pkg.back() == ':') sr->pkg.pop_back();
        advance();                    // (
        if (cur().kind != Tok::RParen) sr->nameExpr = parseExpression();
        expectKind(Tok::RParen, "expected ')' after sigil::(");
        parseSymSegs(sr.get());
        return sr;
    }
    // symbolic name reference in term position: `::Foo::Bar` → the named type/package
    // `::CALLERS::<&x>` is `CALLERS::<&x>`: a leading `::` before a
    // pseudo-package is the root it already names (Red's ResultSeq asks
    // `::CALLERS::<&__RED_OPERATOR_LOADED__>`). Drop it and parse the rest.
    if (isOp("::") && peek().kind == Tok::Ident) {
        std::string pt = peek().text, eff;
        if (pt.size() > 2 && pt.compare(pt.size() - 2, 2, "::") == 0) pt.resize(pt.size() - 2);
        if (isPseudoPkgPath(pt, eff)) advance();
    }
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
            // A RADIX literal wide enough to overflow a long long: the lexer's
            // strtoll saturated it, so `0xFFFF_FFFF_FFFF_FFFF` came out as
            // 9223372036854775807. Recompute it exactly, in decimal, the way an
            // over-long decimal literal is already handled just above — SHA-512's
            // constants are 64-bit and every one of them was being clamped.
            else if (bare.size() > 2 && bare[0] == '0' &&
                     (bare[1] == 'x' || bare[1] == 'X' || bare[1] == 'b' || bare[1] == 'B' ||
                      bare[1] == 'o' || bare[1] == 'O' || bare[1] == 'd' || bare[1] == 'D')) {
                int b = (bare[1] == 'x' || bare[1] == 'X') ? 16
                      : (bare[1] == 'b' || bare[1] == 'B') ? 2
                      : (bare[1] == 'o' || bare[1] == 'O') ? 8 : 10;
                std::string dec = "0";
                auto mulAdd = [](const std::string& n, int m, int add) { // decimal long multiply
                    std::string r; int carry = add;
                    for (int i = (int)n.size() - 1; i >= 0; i--) {
                        int p = (n[i] - '0') * m + carry;
                        r += (char)('0' + p % 10); carry = p / 10;
                    }
                    while (carry) { r += (char)('0' + carry % 10); carry /= 10; }
                    while (r.size() > 1 && r.back() == '0') r.pop_back();
                    std::reverse(r.begin(), r.end());
                    return r.empty() ? std::string("0") : r;
                };
                bool ok = true;
                for (size_t i = 2; i < bare.size() && ok; i++) {
                    int d = ascii::isdigit((unsigned char)bare[i]) ? bare[i] - '0'
                          : ascii::isalpha((unsigned char)bare[i])
                              ? ascii::tolower((unsigned char)bare[i]) - 'a' + 10 : -1;
                    if (d < 0 || d >= b) ok = false; else dec = mulAdd(dec, b, d);
                }
                if (ok) {
                    try { e->v = std::stoll(dec); }
                    catch (...) { e->big = dec; }
                }
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
            if (fmt) {
                // `q:o/…/` builds a Format, which 6.e introduced; before that the
                // adverb is simply not a thing, and Rakudo says so at compile time.
                if (langRev_ < 2) throw ParseError("Unrecognized adverb ':o' (a Format literal needs 6.e; `use v6.e.PREVIEW`)", cur().line);
                auto c = std::make_unique<Call>(); c->name = "__format__"; c->args.push_back(std::move(e)); return c;
            }
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
            if (fmt) {
                // `q:o/…/` builds a Format, which 6.e introduced; before that the
                // adverb is simply not a thing, and Rakudo says so at compile time.
                if (langRev_ < 2) throw ParseError("Unrecognized adverb ':o' (a Format literal needs 6.e; `use v6.e.PREVIEW`)", cur().line);
                auto c = std::make_unique<Call>(); c->name = "__format__"; c->args.push_back(std::move(e)); return c;
            }
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
            checkRegexBoundaries(tk.text, tk.line);
            auto e = std::make_unique<RegexLit>(tk.text);
            e->isRx = tk.flag;
            e->isM = (tk.ival == 1);
            return e;
        }
        case Tok::SubstLit: {
            const Token& t = advance();
            checkNullRegex(t.text, t.line, /*branches=*/false);
            // `s/a/b/i` — Perl 5's trailing modifiers; Raku writes them as adverbs
            if (isKind(Tok::Ident) && !cur().spaceBefore && !cur().text.empty() &&
                cur().text.find_first_not_of("igmsxe") == std::string::npos)
                throw ParseError("Unsupported use of /" + cur().text + "; in Raku please use :" +
                                 cur().text, cur().line, "X::Obsolete",
                                 {{"old", "/" + cur().text}, {"replacement", ":" + cur().text}});
            return std::make_unique<SubstLit>(t.text, t.text2, t.flag);
        }
        case Tok::QwList: { // qw<...> : split raw content on whitespace into a list of strings
            // the lexer records the form in text2: quote protection for
            // qww/qqww ('…'/"…" spans group into one word, quotes dropped),
            // interpolation+escapes for the qq forms ("\n" is a newline,
            // "$x" the variable's value). Empty text2 (other producers of
            // QwList tokens) keeps the plain whitespace split.
            std::string form = cur().text2;
            // An explicit `:v`/`:val` adverb rides on the form as a `:v` suffix
            // (see the lexer): it re-enables the allomorphing the q-family drops.
            bool valAdverb = form.size() > 2 && form.compare(form.size() - 2, 2, ":v") == 0;
            if (valAdverb) form.resize(form.size() - 2);
            const bool protect = form == "qww" || form == "qqww";
            const bool interp  = form == "qqw" || form == "qqww";
            // Only the ANGLE forms val() their words into allomorphs. `<8 9>`,
            // `«8 9»` and `<<8 9>>` give IntStr; every q-family spelling —
            // qw, qww, qqw, qqww and any `q:w`/`Q:w`/`:words` adverb — gives
            // plain Str, and Rakudo is firm about it: `qw<1.5>` is a Str where
            // `<1.5>` is a RatStr. rakupp allomorphed them all, and since
            // `IntStr ~~ Int` is True that silently changed DISPATCH:
            // Crane's `in` reads a step as a positional index when it is an
            // Int, so `Crane.in(%j, …, qw<8 9 10>, …)` built a 10-element
            // ARRAY where Rakudo nests three hash keys (issue #69).
            // The lexer sets text2 for the q-family only; the bare-angle
            // producers of this token leave it empty and keep the val().
            // An explicit `:v`/`:val` is the one q-family spelling that DOES
            // allomorph — Rakudo honours it, and S02-literals/allomorphic.t
            // asserts it element for element on `qw:v[1 2/3 4.5 6e7 8+9i]`.
            const bool allomorph = form.empty() || valAdverb;
            std::string raw = advance().text;
            auto arr = std::make_unique<ArrayLit>();
            arr->isList = true;
            // qqww shares «…»'s word rules (it IS «…» under :v): an interpolated
            // value splits into words, and a bare word splits at its
            // interpolations — qqww{$y} with "a b" is ("a", "b")
            const bool qq = protect && interp;
            std::string qqFlags;
            size_t i = 0, n = raw.size();
            while (i < n) {
                for (int w; i < n && (w = uniWsLen(raw, i, true)); ) i += w;
                if (i >= n) break;
                if (protect && (raw[i] == '\'' || raw[i] == '"')) {
                    // quoted span: one word up to the matching quote
                    char q = raw[i++];
                    std::string seg; bool closed = false;
                    while (i < n) {
                        if (raw[i] == '\\' && i + 1 < n) { seg += raw[i]; seg += raw[i + 1]; i += 2; continue; }
                        if (raw[i] == q) { closed = true; i++; break; }
                        seg += raw[i++];
                    }
                    (void)closed;
                    if (qq) {
                        std::string lit = seg;
                        if (q == '\'') {
                            lit.clear();
                            for (size_t k = 0; k < seg.size(); k++) {
                                if (seg[k] == '\\' && k + 1 < seg.size() &&
                                    (seg[k + 1] == q || seg[k + 1] == '\\')) { lit += seg[++k]; continue; }
                                lit += seg[k];
                            }
                        }
                        qqwwAddWord(*arr, qqFlags, std::string(1, q) + lit + q, allomorph);
                    }
                    else if (q == '"' && interp)
                        arr->items.push_back(parseInterpString(seg));
                    else {
                        // single-quote semantics: only \' and \\ unescape
                        std::string lit;
                        for (size_t k = 0; k < seg.size(); k++) {
                            if (seg[k] == '\\' && k + 1 < seg.size() &&
                                (seg[k + 1] == q || seg[k + 1] == '\\')) { lit += seg[++k]; continue; }
                            lit += seg[k];
                        }
                        arr->items.push_back(std::make_unique<StrLit>(lit));
                    }
                    continue;
                }
                size_t start = i;
                while (i < n && !uniWsLen(raw, i, true) &&
                       !(protect && (raw[i] == '\'' || raw[i] == '"'))) i++;
                if (i > start) {
                    std::string word = raw.substr(start, i - start);
                    // a numeric word is an allomorph (<42> IntStr, <1/3> RatStr, …) —
                    // in a multi-word list too: <1 2 3>[0].WHAT is IntStr
                    ExprPtr cp, num;
                    if (qq) qqwwAddWord(*arr, qqFlags, word, allomorph);
                    else if (protect && (cp = angleColonPair(word))) // `:name(…)` word → Pair
                        arr->items.push_back(std::move(cp));
                    else if (allomorph && (num = angleWordNumeric(word))) {
                        auto al = std::make_unique<AllomorphLit>();
                        al->num = std::move(num); al->str = word;
                        arr->items.push_back(std::move(al));
                    } else if (interp && word.find_first_of("$@{\\") != std::string::npos)
                        arr->items.push_back(parseInterpString(word));
                    else
                        arr->items.push_back(std::make_unique<StrLit>(word));
                }
            }
            if (qq) return qqwwFinish(std::move(arr), qqFlags, allomorph);
            // a single `<word>` is that element itself, not a one-item list
            if (arr->items.size() == 1) return std::move(arr->items[0]);
            return arr;
        }
        case Tok::Var: {
            if (cur().text.rfind("&?ROUTINE", 0) == 0 && routineDepth_ == 0)
                throw ParseError("Undeclared name:\n    &?ROUTINE used at line " +
                                 std::to_string(cur().line), cur().line,
                                 "X::Undeclared::Symbols", {});
            // `$?CLASS` / `$?ROLE` / `$?PACKAGE` — the SIGILLED spelling of the
            // compile-time enclosing type, and the same constant as `::?CLASS`
            // (handled in parsePrimary's `::` arm). Only the `::` form resolved,
            // so inside a `unit class` body `$?CLASS` read as an undeclared
            // variable and answered Any — which is what a class-level registry
            // keyed on `$?CLASS.^name` gets instead of its own name, and why
            // `Log.get` / `Logger.get` were dead here while working on Rakudo.
            if ((cur().text == "$?CLASS" || cur().text == "$?ROLE" ||
                 cur().text == "$?PACKAGE") && !typeStack_.empty()) {
                bool klass = cur().text == "$?CLASS";
                advance();
                // In a ROLE, `$?CLASS` is the CONSUMING class — resolved per
                // invocant at run time, exactly as the `::?CLASS` arm does.
                if (klass && !typeIsRole_.empty() && typeIsRole_.back())
                    return std::make_unique<NameTerm>("::?CLASS");
                return std::make_unique<NameTerm>(typeStack_.back());
            }
            // `@{$x}` / `%{$x}` — the Perl 5 hard DEREFERENCE forms, which Raku
            // spells `@($x)` / `%($x)`. (`%{...}` with anything else inside is a
            // hash composer and stays; only a lone variable is the P5 spelling.)
            if (cur().text.size() == 1 && (cur().text[0] == '@' || cur().text[0] == '%') &&
                peek().kind == Tok::LBrace && !peek().spaceBefore &&
                peek(2).kind == Tok::Var && peek(3).kind == Tok::RBrace) {
                std::string sig = cur().text;
                throw ParseError("Unsupported use of " + sig + "{" + peek(2).text +
                                 "} as " + sig + " dereference. In Raku please use: " +
                                 sig + "(" + peek(2).text + ").",
                                 cur().line, "X::Obsolete",
                                 {{"old", sig + "{" + peek(2).text + "}"},
                                  {"replacement", sig + "(" + peek(2).text + ")"}});
            }
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
                // …and `${$x}`, the Perl 5 hard DEREFERENCE, which Raku spells `$($x)`
                if (peek(2).kind == Tok::Var && peek(3).kind == Tok::RBrace)
                    throw ParseError("Unsupported use of ${" + peek(2).text + "}. In Raku please use: $(" +
                                     peek(2).text + ") for hard ref or $::(" + peek(2).text + ") for symbolic ref.",
                                     cur().line, "X::Obsolete",
                                     {{"old", "${" + peek(2).text + "}"},
                                      {"replacement", "$(" + peek(2).text + ")"}});
                advance();
                auto u = std::make_unique<Unary>();
                u->op = "ctx$"; u->operand = parsePrimary();
                return u;
            }
            // sink-assignment to an anonymous container, VAR-lexed spelling: the
            // lexer hands `% = …` (and tight `%=%h`) over as a Var token where the
            // operator-position one arrives as an Op and takes the branch in
            // parsePrimary's Op case. Falling through named the target `@`/`%`,
            // which made the anonymous variable nameable — `@ = (1,2); (@).raku`
            // answered [1, 2] where Rakudo's bare `@` term is a fresh Array.
            if (cur().text.size() == 1 &&
                (cur().text[0] == '@' || cur().text[0] == '%') &&
                peek().kind == Tok::Op && peek().text == "=") {
                auto ave = std::make_unique<VarExpr>(advance().text + kAnonSlot);
                ave->declare = true; ave->declScope = "my";
                return ave;
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
            int ln = cur().line;
            std::string raw = advance().text;
            rejectPackagedDynamic(raw, ln);
            // `$today:foo<a b>` — the adverbs are part of the NAME (the four
            // value spellings canonicalise to one), not a pair after the term
            raw += readExtendedNameSuffix();
            // `$.name(ARGS)` is `self.name(ARGS)` — a method call that TAKES those
            // arguments. It used to parse as the no-argument accessor `$.name`
            // followed by a postfix call on whatever that returned, so
            // `$.to-string(|$args)` ran to-string with nothing and then tried to
            // invoke its Str result ("Cannot invoke non-Callable value of type
            // Str"). `$!name(…)` cannot be a call at all — a private attribute is
            // not a method — so only the `.` twigil takes this path.
            // …and the colon spelling of the same call, `$.a: 40, 2`, which is
            // the listop form every other method call already accepts.
            // …and the `:sym<…>` that NAMES a proto-regex candidate, which is part
            // of the method name and not a listop argument: PDF::Grammar's action
            // for `numeric:sym<real>` hands off with `$.numeric:sym<frac>($/)`,
            // and the colon form below took `sym<frac>($/)` for its argument — a
            // call to an undefined routine `sym`.
            std::string symSuffix;
            if (raw.size() > 2 && raw[0] == '$' && raw[1] == '.' &&
                isOp(":") && !cur().spaceBefore && peek().kind == Tok::Ident &&
                peek().text == "sym" && peek(2).kind == Tok::Op &&
                (peek(2).text == "<" || peek(2).text == "\xC2\xAB")) {
                advance(); advance(); // : sym
                std::vector<std::string> w;
                if (isOp("<")) { advance(); w = readAngleWords(">"); }
                else { advance(); w = readAngleWords("\xC2\xBB"); }
                symSuffix = ":sym<" + (w.empty() ? std::string() : w[0]) + ">";
            }
            bool dotColonCall = symSuffix.empty() &&
                                raw.size() > 2 && raw[0] == '$' && raw[1] == '.' &&
                                isOp(":") && !cur().spaceBefore &&
                                peek().kind != Tok::RParen && peek().kind != Tok::Semicolon &&
                                !(peek().kind == Tok::Op && peek().text == "=");
            // …and `&.name(ARGS)` is the same call: the `&` sigil says the result
            // is wanted as a Callable, not that the name IS one. PDF::COS::Tie
            // coerces an assigned value with `&.coerce($_, :$.reader)`, which
            // read as a variable lookup followed by an invocation of whatever it
            // answered ("Cannot invoke non-Callable value of type Str").
            if (raw.size() > 2 && (raw[0] == '$' || raw[0] == '&') && raw[1] == '.' &&
                (!symSuffix.empty() || (isKind(Tok::LParen) && !cur().spaceBefore) || dotColonCall)) {
                bool paren = isKind(Tok::LParen) && !cur().spaceBefore;
                if (paren || dotColonCall) advance(); // ( or :
                auto mc = std::make_unique<MethodCall>();
                mc->line = ln;
                mc->inv = std::make_unique<SelfTerm>();
                mc->method = raw.substr(2) + symSuffix;
                if (paren ? !isKind(Tok::RParen) : dotColonCall)
                    for (;;) {
                        // BP_COMMA + 1: stop AT the comma so each argument is its
                        // own. At BP_COMMA the comma is swallowed and `$.a(2, 3)`
                        // arrives as one list argument, which numifies to its
                        // element count — `2`, quietly, instead of 5.
                        mc->args.push_back(parseExpr(BP_COMMA + 1));
                        if (isKind(Tok::Comma)) { advance(); continue; }
                        break;
                    }
                if (paren) expectKind(Tok::RParen, ")");
                return mc;
            }
            if (raw.size() > 1 && std::strchr("$@%&", raw[0]) &&
                (raw.find("CALLER::") == 1 || raw.find("CALLER::") == 2)) { // `$CALLER::y` / `$*CALLER::y`: the caller's, see the angle form
                auto sr = std::make_unique<SymbolicRef>();
                sr->pkg = "CALLER";
                sr->nameExpr = std::make_unique<StrLit>(stripPseudoPkg(raw));
                sr->line = ln; return sr;
            }
            // `$CALLERS::x` — the nearest caller that has one: every frame up
            if (raw.size() > 1 && std::strchr("$@%&", raw[0]) &&
                (raw.find("CALLERS::") == 1 || raw.find("CALLERS::") == 2)) {
                auto sr = std::make_unique<SymbolicRef>();
                sr->pkg = "CALLERS";
                sr->nameExpr = std::make_unique<StrLit>(stripPseudoPkg(raw));
                sr->line = ln; return sr;
            }
            // `$OUTER::a` is the lookup `OUTER::<$a>` already performs: each
            // OUTER steps one scope OUT before the name is resolved. Merely
            // stripping the qualifier — what the other pseudo-packages below
            // do, and rightly — found our own `$a` again, so the two spellings
            // disagreed and a shadowed outer variable had no way to be named
            // (roast S02-names-vars/variables-and-packages.t reads three
            // nested `$a` this way). A chain hops once per OUTER, and whatever
            // qualifier is left after them is stripped as before.
            if (raw.size() > 1 && std::strchr("$@%&", raw[0])) {
                size_t head = 1;
                if (head < raw.size() && std::strchr("*!?.^:", raw[head])) head++;
                size_t p = head;
                long long hops = 0;
                while (p + 7 <= raw.size() && raw.compare(p, 7, "OUTER::") == 0) { hops++; p += 7; }
                if (hops) {
                    auto c = std::make_unique<Call>();
                    c->name = "__sym-lookup";
                    c->line = ln;
                    c->args.push_back(std::make_unique<StrLit>(
                        stripPseudoPkg(raw.substr(0, head) + raw.substr(p))));
                    c->args.push_back(std::make_unique<IntLit>(hops));
                    return c;
                }
            }
            auto e = std::make_unique<VarExpr>(stripPseudoPkg(raw));
            e->processScoped = raw.find("PROCESS::") != std::string::npos;
            // `%GLOBAL::X` names a slot in the ROOT package. The strip above is
            // right — a file-scope `our $x` lives there, so `$GLOBAL::x` has to
            // find it (roast S02-names/our.t) — but when NOTHING has been put
            // there it is still a symbol-table slot: empty on read, created on
            // write, never "Variable '%X' is not declared". Red keeps its whole
            // connection registry in one, declared nowhere.
            if (raw.find("GLOBAL::") == 1 || raw.find("GLOBAL::") == 2) e->pkgSymbol = true;
            // `&CORE::chdir` names the BUILT-IN, and stripping the qualifier
            // loses exactly that: the bare `&chdir` then finds whatever the
            // program has put in scope — which, since the form is only written
            // when something IS shadowing, is the shadow itself. Marked the
            // same way the angle spelling `CORE::<&chdir>` already is, so the
            // interpreter can answer the builtin. (lizmat's Perl-builtin ports
            // are all this shape and each one called itself forever.)
            if (raw.size() > 1 && raw[0] == '&' &&
                (raw.compare(1, 6, "CORE::") == 0 || raw.compare(1, 9, "SETTING::") == 0)) {
                e->viaPseudoPkg = true;
                e->pseudoPkg    = "CORE";
            }
            // `$OUR::x` names the CURRENT package's `our $x` — which an inner
            // block may have declared, out of lexical reach (roast pseudo-6*.t)
            if (raw.size() > 6 && raw[0] == '$' && raw.compare(1, 5, "OUR::") == 0 &&
                raw.find("::", 6) == std::string::npos) {
                e->viaPseudoPkg = true;
                e->pseudoPkg    = "OUR";
            }
            e->line = ln; return e;
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
            // …and the TOPICALIZERS. They reach here as statements only when
            // they come FIRST — `(EXPR with X)` is the modifier form and is
            // handled below, after the expression — so position tells the two
            // apart with nothing to guess. RakuDoc::Render builds a string out
            // of `'[' ~ ( given %prm<type> { when 'internal' {…} } ) ~ …`, and
            // `if` in the same position already worked, which is what made the
            // gap look like a syntax error rather than a missing case.
            if ((isIdent("given") || isIdent("with") || isIdent("without")) &&
                peek().kind != Tok::FatArrow && peek().kind != Tok::Comma) {
                auto st = parseStatement();
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
            // `(EXPR when X)` — EXPR if the topic smartmatches X, else nothing
            if (isIdent("when")) {
                advance();
                auto sm = std::make_unique<Binary>();
                sm->op = "~~";
                sm->lhs = std::make_unique<VarExpr>("$_");
                sm->rhs = parseExpression();
                auto tern = std::make_unique<Ternary>();
                tern->cond = std::move(sm);
                tern->then = std::move(e);
                tern->els = std::make_unique<NameTerm>("Empty");
                e = std::move(tern);
                continue;
            }
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
                // per item ($_ bound). Desugar to map({ EXPR }, LIST).List.
                //
                // The `.List` is the difference between this and a bare map: a
                // `for` used as an expression evaluates to a LIST, and `map`
                // gives a Seq. The block spelling `(for LIST { EXPR })` already
                // returned a List (ForStmt's asExpr path builds one), so without
                // this the two spellings of the same loop disagreed about their
                // own type — and `($_ for ^1) eqv (0,)` was False where Rakudo
                // says True. Nothing was lost by being a Seq here, because this
                // desugaring is eager either way; it was only ever the name.
                advance();
                ExprPtr list = parseExpression();
                auto blk = std::make_unique<BlockExpr>();
                auto es = std::make_unique<ExprStmt>(); es->e = std::move(e);
                blk->body.push_back(std::move(es));
                auto call = std::make_unique<Call>(); call->name = "map";
                call->args.push_back(std::move(blk));
                call->args.push_back(std::move(list));
                auto toList = std::make_unique<MethodCall>();
                toList->inv = std::move(call);
                toList->method = "List";
                e = std::move(toList);
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
                // a lone segment with a TRAILING `;` (`( expr; )`) is just the
                // grouped value, not a 1-element list — Rakudo: `(5;)` is 5
                if (lst->items.size() == 1) return std::move(lst->items[0]);
                return lst;
            }
            expectKind(Tok::RParen, ")");
            // Remember that THIS node came out of parentheses. Only two readings
            // need it — the range operator's non-associativity and its
            // precedence worry — and both have to tell `(|4) .. 5` from `|4 .. 5`
            // when the parser hands back the inner node unwrapped.
            if (e) parenned_.insert(e.get());
            if (e && e->kind == NK::ListExpr) static_cast<ListExpr*>(e.get())->parenned = true;
            // A pair in parens is a positional Pair VALUE, not a named argument:
            // `f((:$x))` passes one, `f(:$x)` names one. DBDish::Pg's connect
            // adds its database name with `%params.push((:$dbname))`, and read as
            // a named argument that push did nothing — every connection went to
            // the server's default database instead.
            if (e && e->kind == NK::Pair) static_cast<PairExpr*>(e.get())->parenned = true;
            return e;
        }
        case Tok::LBracket: {
            // reduction metaoperator: [+] [*] [~] [max] [<] [Z] [X] ...
            // A named-operator reduce names a WORD INFIX (max/min/gcd/…) or a meta
            // base (Z/X, alone or fused). Any other lowercase identifier is a TERM:
            // `[ v ]` over a sigilless `\v` is a one-element array, and reading it
            // as a reduce over an operator named `v` made DBDish::Pg's
            // `my @data := arr ~~ Array ?? arr !! [ arr ]` build an EMPTY array —
            // so `pg-array-str` recursed on itself until the stack ran out.
            // A capitalized type name — `[Any]`, `[Int]`, `[Foo]` — was never a reduce.
            static const std::set<std::string> kReduceWordOps = {
                "eq", "ne", "lt", "gt", "le", "ge", "cmp", "leg", "eqv", "before", "after",
                "unicmp", "coll", "x", "xx", "and", "or", "xor", "andthen", "orelse", "notandthen",
                "div", "mod", "gcd", "lcm", "min", "max", "minmax", "but", "does", "o",
            };
            bool identReduce = peek(1).kind == Tok::Ident && !peek(1).text.empty() &&
                (peek(1).text == "Z" || peek(1).text == "X" ||
                 kReduceWordOps.count(peek(1).text) ||
                 userInfix_.count(peek(1).text) ||
                 // Z/X/R fused with a word op lex as one ident: [Zand] [Xor] [Rmax]
                 ((peek(1).text[0] == 'Z' || peek(1).text[0] == 'X' || peek(1).text[0] == 'R') &&
                  peek(1).text.size() > 1 &&
                  std::all_of(peek(1).text.begin() + 1, peek(1).text.end(),
                              [](unsigned char c){ return ascii::islower(c); })));
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
            // `∞` is lexed as an Op but is a TERM, so `[∞]` is a one-element array
            // literal rather than a reduction over an operator named `∞`.
            static const std::string kInf = "\xE2\x88\x9E";
            if (((peek(1).kind == Tok::Op && peek(1).text != "\\" && peek(1).text != kInf) || identReduce) &&
                peek(1).text != "]") {
                // the op may span several TIGHT tokens ([!=:=] lexes as `!=` + `:=`),
                // and the `!` negation metaop can be glued to a WORD infix ([!after]).
                // ONLY as the run's FIRST token: `[-x]` is an array literal and `[:!a]` a pair.
                int j = 2;
                while (peek(j).text != "]" && !peek(j).spaceBefore &&
                       (peek(j).kind == Tok::Op ||
                        (peek(j).kind == Tok::Ident && !peek(j).text.empty() &&
                         ascii::islower((unsigned char)peek(j).text[0]) &&
                         j == 2 && peek(1).kind == Tok::Op && peek(1).text == "!"))) j++;
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
                        else u->operand = parseParenSemiList();
                    }
                    else u->operand = parseExpr(BP_ZIP);  // reduce is a list-prefix: looser than Z/X and comma
                    return u;
                }
            }
            // `[[+]] 1, 2, 3` — a reduction over a BRACKETED operator. The
            // metaops nest freely and the brackets are punctuation, so the
            // reduction is over `+` itself.
            {
                int j = 1; std::string pre;    // an optional metaop run: `[R[+]]`, `[Z[cmp]]`
                while (j < 4 && (peek(j).kind == Tok::Ident || peek(j).kind == Tok::Op) &&
                       (j == 1 || !peek(j).spaceBefore)) pre += peek(j++).text;
                int m = j + 1; std::string inner;
                if (peek(j).kind == Tok::LBracket && (j == 1 || !peek(j).spaceBefore)) {
                    // `[[&foo]]` — a reduction over a bracketed CALLABLE. The
                    // name rides in the operator string and applyBinOp resolves
                    // it, exactly as the binary `A [&foo] B` form does.
                    if (peek(m).kind == Tok::Var && peek(m).text.size() > 1 && peek(m).text[0] == '&')
                        inner = peek(m++).text;
                    else
                        while (peek(m).kind == Tok::Op || peek(m).kind == Tok::Ident) inner += peek(m++).text;
                }
                // …but only when the brackets really hold an OPERATOR: `[[-x]]` is
                // an array of an array, not a reduction over an infix `-x`.
                bool okInner = false;
                if (!inner.empty() && inner[0] == '&') okInner = true;
                else if (!inner.empty()) {
                    Token t2 = cur(); t2.text = pre + inner;
                    t2.kind = ascii::isalpha((unsigned char)t2.text[0]) ? Tok::Ident : Tok::Op;
                    okInner = classifyInfix(t2).valid || userInfix_.count(t2.text) != 0;
                }
                if (okInner && peek(m).kind == Tok::RBracket &&
                    peek(m + 1).kind == Tok::RBracket) {
                    for (int k2 = 0; k2 <= m + 1; k2++) advance();
                    auto u = std::make_unique<Unary>();
                    u->op = "[" + pre + inner + "]";
                    if (isKind(Tok::Comma)) advance();
                    if (isKind(Tok::RParen) || isKind(Tok::Semicolon) || isKind(Tok::End))
                        u->operand = std::make_unique<ListExpr>();
                    else if (isKind(Tok::LParen) && !cur().spaceBefore) {
                        advance();
                        if (isKind(Tok::RParen)) { advance(); u->operand = std::make_unique<ListExpr>(); }
                        else { u->operand = parseParenSemiList(); }
                    }
                    else u->operand = parseExpr(BP_ZIP);
                    return u;
                }
            }
            // A reduction metaop's operand: the function-call form `[\+](…)` is
            // bounded by the parens (a comma AFTER the `)` belongs to the enclosing
            // list); otherwise it is a list-prefix, looser than Z/X and comma.
            auto reduceOperand = [&]() -> ExprPtr {
                if (isKind(Tok::LParen) && !cur().spaceBefore) {
                    advance();
                    if (isKind(Tok::RParen)) { advance(); return std::make_unique<ListExpr>(); }
                    return parseParenSemiList();
                }
                return parseExpr(BP_ZIP);
            };
            // triangular / scan reduce: [\+] [\~] [\*] — yields the list of running
            // partial reductions (1, 1+2, 1+2+3, …) rather than the final value.
            if (peek(1).kind == Tok::Op && peek(1).text == "\\" &&
                peek(2).kind == Tok::Comma && peek(3).kind == Tok::RBracket) {
                advance(); advance(); advance(); advance(); // [ \ , ]
                auto u = std::make_unique<Unary>();
                u->op = "[\\,]";
                u->operand = reduceOperand();
                return u;
            }
            if (peek(1).kind == Tok::Op && peek(1).text == "\\" &&
                (peek(2).kind == Tok::Op || (peek(2).kind == Tok::Ident && !peek(2).text.empty() &&
                    (ascii::isupper((unsigned char)peek(2).text[0]) ? // Z/X(+word) meta bases
                         (peek(2).text[0] == 'Z' || peek(2).text[0] == 'X' || peek(2).text[0] == 'R')
                       : ascii::islower((unsigned char)peek(2).text[0])))) &&
                !peek(2).spaceBefore && peek(2).text != "]") {
                // multi-token base ops too: [\X~] lexes as `\` `X` `~`, and the `!`
                // negation metaop can be glued to a WORD infix ([\!eq]) — only there
                int j = 3;
                while (peek(j).text != "]" && !peek(j).spaceBefore &&
                       (peek(j).kind == Tok::Op ||
                        (peek(j).kind == Tok::Ident && !peek(j).text.empty() &&
                         ascii::islower((unsigned char)peek(j).text[0]) &&
                         j == 3 && peek(2).kind == Tok::Op && peek(2).text == "!"))) j++;
                if (peek(j).kind == Tok::RBracket) {
                    advance(); // [
                    advance(); // '\'
                    std::string innerOp;
                    for (int k = 2; k < j; k++) innerOp += advance().text;
                    advance(); // ]
                    auto u = std::make_unique<Unary>();
                    u->op = "[\\" + innerOp + "]";
                    u->operand = reduceOperand();
                    return u;
                }
            }
            // reverse-metaop reduce over the COMMA — `[R,]` reverses a list, and
            // `[\R,]` gives the growing reversed prefixes. The comma arrives as
            // its own token kind, so the general R branch below never saw it.
            if (((peek(1).kind == Tok::Ident && peek(1).text == "R" &&
                  peek(2).kind == Tok::Comma && peek(3).kind == Tok::RBracket)) ||
                (peek(1).kind == Tok::Op && peek(1).text == "\\" &&
                 peek(2).kind == Tok::Ident && peek(2).text == "R" &&
                 peek(3).kind == Tok::Comma && peek(4).kind == Tok::RBracket)) {
                bool tri = peek(1).kind == Tok::Op;
                advance();               // [
                if (tri) advance();      // '\'
                advance(); advance();    // R ,
                advance();               // ]
                auto u = std::make_unique<Unary>();
                u->op = tri ? "[\\R,]" : "[R,]";
                u->operand = reduceOperand();
                return u;
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
                u->operand = reduceOperand();
                return u;
            }
            advance();
            auto arr = std::make_unique<ArrayLit>();
            if (!isKind(Tok::RBracket)) {
                ExprPtr e = parseExpression();
                // `[EXPR for LIST]` — a statement modifier inside the composer,
                // exactly as `(EXPR for LIST)` already allowed. The bracket form
                // was the only one that refused it, and it is the one people
                // reach for when the result should be an Array:
                // Terminal::Table builds its `.lines` that way. `if`/`unless`/
                // `with`/`while`/`given` come along, since it is the same chain.
                e = applyExprModifiers(std::move(e));
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
            bool isHash = braceLooksHash(/*emptyIsHash=*/true);
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
                // a TRAILING separator is allowed inside a composer, as it is in
                // any statement list: PDF::API6 ends the dictionary it coerces
                // with `:$destination;` before the closing brace. A `;` with more
                // after it makes the braces a block, which braceLooksHash settles.
                while (matchKind(Tok::Semicolon)) {}
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
                auto c = std::make_unique<Call>(); c->name = advance().text;
                // `!!! 42` / `... "not yet"` — the stub's message follows it
                switch (cur().kind) {
                    case Tok::IntLit: case Tok::NumLit: case Tok::StrLit: case Tok::StrInterp: case Tok::Var:
                        if (cur().line == t.line) c->args.push_back(parseExpr(BP_COMMA + 1));
                        break;
                    default: break;
                }
                return c;
            }
            if (t.text == ":") return parseColonPair();
            // sink-assignment to an anonymous container: `@ = (…)` / `$ = …`
            if ((t.text == "@" || t.text == "%" || t.text == "$") &&
                peek().kind == Tok::Op && peek().text == "=" && peek().spaceBefore) {
                advance();
                auto ve = std::make_unique<VarExpr>(t.text + kAnonSlot);
                ve->declare = true; ve->declScope = "my";
                return ve;
            }
            // `&%dispatch{$key}` / `&@handlers[$i]` — the CODE sigil on a hash/array
            // ELEMENT: the element is the callable, so the subscript belongs to the
            // variable and the `&` itself adds nothing at runtime (Rakudo binds the
            // value as a Callable). Date::Calendar::Strftime picks its formatters
            // out of a dispatch hash exactly this way.
            if (t.text == "&" && peek().kind == Tok::Var && !peek().spaceBefore &&
                peek().text.size() > 1 &&
                (peek().text[0] == '%' || peek().text[0] == '@' || peek().text[0] == '$')) {
                advance(); // &
                return parsePostfix(parsePrimary());
            }
            // `&(EXPR)` — the parenthesised Callable contextualizer: the inner
            // expression IS the callable and the `&` adds nothing at runtime
            // (Encode dispatches `&(%encodings{$encoding})($buf)` this way).
            // parsePostfix attaches the call/subscript that follows.
            if (t.text == "&" && peek().kind == Tok::LParen && !peek().spaceBefore) {
                advance(); // &
                advance(); // (
                auto inner = parseExpression();
                expectKind(Tok::RParen, ")");
                return parsePostfix(std::move(inner));
            }
            if (t.text == "*") { advance(); return std::make_unique<WhateverExpr>(); }
            if (t.text == "**") { advance(); auto w = std::make_unique<WhateverExpr>(); w->hyper = true; return w; } // HyperWhatever (e.g. %h{**})
            // slip-subscript `@a[|| @dims]` / `%h{|| @keys}`: navigate by a list
            // of indices/keys — but NOT `||(EXPR)` glued to a paren, which is the
            // orphaned-infix term handled further down (TOML::NQP).
            if (t.text == "||" && !(peek().kind == Tok::LParen && !peek().spaceBefore)) {
                // BP_PREFIX, not BP_COMMA: `|| 1 == 2 || 3 == 3` is
                // `(||1) == 2 || 3 == 3` upstream, so the prefix takes ONE term.
                // Binding loosely swallowed the rest of the chain and the
                // leading-`||` idiom answered a Slip where Rakudo answers a Bool.
                advance(); auto u = std::make_unique<Unary>(); u->op = "dimslip"; u->operand = parseExpr(BP_PREFIX); return u;
            }
            if (t.text == "\xE2\x88\x9E") { advance(); auto inf = std::make_unique<NumLit>(std::numeric_limits<double>::infinity()); inf->raw = "\xE2\x88\x9E"; return inf; } // ∞
            if (t.text == ".") {   // .method => $_.method
                // Flagged as SYNTHESIZED: Rakudo spells a bare `.method`
                // `Term::TopicCall` and a written-out `$_.method` an
                // `ApplyPostfix`, and the two deparse to the same text — so
                // without the bit `.AST` would answer a different tree for the
                // same program depending on which spelling it was written in.
                auto topic = std::make_unique<VarExpr>("$_");
                topic->synthTopic = true;
                return topic;
            }
            if (t.text == "\\") { // capture: \(…) builds a Capture (assoc-indexable); bare \x itemizes
                advance();
                if (isKind(Tok::LParen) && !cur().spaceBefore) {
                    // the capture is exactly the PARENTHESISED group — parsing a
                    // full prefix here would swallow the postfix too, making
                    // `\(1,2).list` a capture OF `(1,2).list`
                    advance(); // (
                    auto u = std::make_unique<Unary>();
                    u->op = "capture";
                    if (isKind(Tok::RParen)) u->operand = std::make_unique<ListExpr>();
                    else u->operand = parseExpression();
                    expectKind(Tok::RParen, ")");
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
            // guillemet word list « a b c », and its ASCII spelling `<<0 +4 'a b'>>`
            // (one word of which the lexer fuses: `<<ab>>`) — qq:ww:v, same
            // ladder as qqww
            if (t.text == "\xC2\xAB" || t.text == "<<" || fusedQqww(t)) {
                std::vector<std::string> words;
                takeQqwwWords(words);
                if (words.empty()) { auto arr = std::make_unique<ArrayLit>(); arr->isList = true; return arr; }
                return qqwwList(words);
            }
            if (t.text == "->" || t.text == "<->") {
                // Perl 5's `Foo->new`: an arrow TIGHT on both sides is the old
                // method arrow, never a pointy block — a block's arrow is always
                // delimited by whitespace. Raku names it instead of failing on
                // the missing `{`.
                if (t.text == "->" && !t.spaceBefore && pos_ > 0 &&
                    peek().kind == Tok::Ident && !peek().spaceBefore &&
                    termEndsHere(toks_[pos_ - 1]))
                    throw ParseError("Unsupported use of -> as postfix. In Raku please use: "
                                     "either . to call a method, or whitespace to delimit a pointy block.",
                                     t.line, "X::Obsolete",
                                     {{"old", "-> as postfix"},
                                      {"replacement", "either . to call a method, or whitespace "
                                                      "to delimit a pointy block"}});
                bool doubly = (t.text == "<->");   // `<->` binds its params `is rw`
                advance();
                auto be = std::make_unique<BlockExpr>();
                be->isPointy = true;   // even `-> {…}` has a written (empty) signature
                sigRetType_.clear();
                be->params = parsePointyParams();
                if (doubly) for (auto& p : be->params) p.isRw = true;
                be->retType = sigRetType_;   // `-> $x --> Int { … }`
                auto blk = parseBlock();
                be->body = std::move(blk->stmts);
                return be;
            }
            // `&[+]` / `&[!~~]` / `&[max]` — the infix operator as a value == `&infix:<OP>`.
            if (t.text == "&" && peek().kind == Tok::LBracket) {
                advance(); advance(); // & [
                std::string op;
                // …counting NESTED brackets: `&[R[~~]]` is the R metaop over `[~~]`,
                // and stopping at the first `]` left a stray one behind ("expected )").
                // Test::Run flips its comparison operator that way.
                int inner = 0;
                while (!isKind(Tok::End)) {
                    if (isKind(Tok::RBracket)) { if (!inner) break; inner--; }
                    else if (isKind(Tok::LBracket)) inner++;
                    // `&[«+»]`: the lexer made «+» a qw-list — restore its markers
                    if (isKind(Tok::QwList)) op += "\xC2\xAB" + advance().text + "\xC2\xBB";
                    else op += advance().text;
                }
                matchKind(Tok::RBracket);
                // A metaop written with the nested spelling normalises to the flat
                // one this engine names it by: `R[~~]` is `R~~`. Rakudo prints the
                // nested form back as the name; the behaviour is the same operator.
                if (op.size() > 3 && op[1] == '[' && op.back() == ']' &&
                    (op[0] == 'R' || op[0] == 'X' || op[0] == 'Z' || op[0] == 'S'))
                    op = op[0] + op.substr(2, op.size() - 3);
                return std::make_unique<VarExpr>("&infix:<" + hyperMarkersToUni(op) + ">");
            }
            // `&&(EXPR)` / `||(EXPR)` glued to `(` in TERM position is the value
            // of that parenthesised expression, the leading operator dropped,
            // exactly as Rakudo reads it (with a SPACE, `&& (EXPR)`, Rakudo errors
            // too, and leading `//` is a NULL REGEX, not this). Malformed but
            // real: TOML::NQP:255 writes `return @part if COND;` then a `&&(more);`
            // line, and Rakudo runs it (TOML → LLM::DWIM depend on it).
            if ((t.text == "&&" || t.text == "||") &&
                peek().kind == Tok::LParen && !peek().spaceBefore) {
                advance();                       // drop the orphaned operator
                return parsePrimary();           // the `(EXPR)` that follows is the term
            }
            error("unexpected operator in term position");
        }
        case Tok::Ident: {
            std::string name = t.text;
            // a type used before this unit declares it (`GrammarUserClass`'s
            // method naming a grammar declared below) — judged at the end
            if (!t.flag && !name.empty() && ascii::isupper((unsigned char)name[0]) &&
                name.find("::") == std::string::npos && peek().kind != Tok::FatArrow &&
                !declClassDecls_.count(name) && !earlyTypeUse_.count(name))
                earlyTypeUse_[name] = t.line;
            // A slang's sigilless variable (Slang::Emoji's 👍, flagged by the lexer's
            // seam): a TERM — never a call, and never auto-quoted before `=>`.
            if (t.flag) { advance(); auto nt = std::make_unique<NameTerm>(name); nt->noAutoQuote = true; return nt; }
            // A fat arrow AUTO-QUOTES the identifier on its left, so EVERY identifier
            // is a valid key — keywords and term-words included. This has to come
            // before all of them: without it the parser committed to `method`, `sub`,
            // `for`, `while` and `do` as the start of a declaration and died, and the
            // error was reported at the NEXT top-level construct's line rather than
            // the offending one, which made it expensive to find. It also settles
            // `True => 1` / `False => 1`, whose keys are the strings "True"/"False"
            // in Rakudo, not Bools. The `=>` infix turns a NameTerm into the key.
            // (…but not a slang's sigilless variable — `👍 => 666` under Slang::Emoji keys on its VALUE)
            // Only a plain IDENTIFIER is autoquoted: a package-qualified name
            // (`Red::AST => …`) is the TYPE, which is how Red keys the
            // unconditional arm of a filter (`:{ Red::AST => $response }`).
            if (peek().kind == Tok::FatArrow && !t.flag && name.find("::") != std::string::npos &&
                name.compare(name.size() - 2, 2, "::") != 0) {
                advance();
                auto nt = std::make_unique<NameTerm>(name); nt->noAutoQuote = true; return nt;
            }
            // `name => value` is a TERM, as in Rakudo: the pair is built here,
            // so it binds even where an infix at `=>`'s precedence would not —
            // `%h ~~ b => 'foo'` smartmatches against the pair, not `(%h ~~ b) => 'foo'`
            if (peek().kind == Tok::FatArrow && !t.flag) {
                advance();   // name
                advance();   // =>
                auto p = std::make_unique<PairExpr>();
                p->key = name;
                p->value = parseExpr(BP_ASSIGN);
                return p;
            }
            if (name == "True") { advance(); return std::make_unique<BoolLit>(true); }
            if (name == "False") { advance(); return std::make_unique<BoolLit>(false); }
            // `Nil` is a TERM, never a routine. Falling through to the general
            // identifier path let it be read as a listop whenever what followed
            // could start one: `Nil ~ 1` parsed as `Nil(~1)` and died with
            // "No such method 'Nil'", and `Nil ff 1` as `Nil(ff 1)`. Rakudo
            // answers those as ordinary infixes on the Nil type object.
            // A postfix still applies — `Nil.gist` parses the same as `True.Str`.
            if (name == "Nil" && peek(1).kind != Tok::LParen) {
                advance(); return std::make_unique<NameTerm>("Nil");
            }
            // anonymous `regex {…}` / `token {…}` / `rule {…}` in term position:
            // a first-class Regex value that closes over the current scope
            // (Cro's route matcher is `EVAL 'regex { … }'`)
            if ((name == "regex" || name == "token" || name == "rule") &&
                (peek(1).kind == Tok::RegexLit ||
                 (peek(1).kind == Tok::Ident && peek(1).text.empty() &&
                  peek(2).kind == Tok::RegexLit))) {
                advance();                                        // the declarator keyword
                if (isKind(Tok::Ident) && cur().text.empty()) advance(); // lexer's empty name slot
                auto rl = std::make_unique<RegexLit>(advance().text);
                rl->declKind = name;
                rl->isRx = true;
                return rl;
            }
            // A term, but only where a keyword may END: a TIGHT `self(` is a
            // call of a sub named `self`, exactly as `pi()` below is. Rakudo
            // spells the rule `<.end_keyword>` and roast declares `sub self
            // { 4 }` and calls it (S02-names-vars/names.t) — we answered
            // "'self' used where no object is available" and took the
            // remaining 135 tests of that file down with us. A SPACE before
            // the paren keeps the term, as it does there.
            // `qr/…/` — Perl 5's regex quote
            if (name == "qr" && peek().kind == Tok::Op && !peek().spaceBefore &&
                (peek().text == "/" || peek().text == "{"))
                throw ParseError("Unsupported use of qr for regex quoting; in Raku please use rx//", t.line,
                                 "X::Obsolete", {{"old", "qr for regex quoting"}, {"replacement", "rx//"}});
            if (name == "self" && peek().kind != Tok::FatArrow &&
                !(peek().kind == Tok::LParen && !peek().spaceBefore)) {
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
            // an ANONYMOUS enum in expression position, `my %e = enum :: <a b>` /
            // `enum <a b>`: the declaration, valued as its type
            if (name == "enum" &&
                ((peek().kind == Tok::Op && (peek().text == "::" || peek().text == "<" ||
                                             peek().text == "<<" || peek().text == "\xC2\xAB")) ||
                 peek().kind == Tok::QwList || (peek().kind == Tok::LParen && peek().spaceBefore))) {
                auto u = std::make_unique<Unary>(); u->op = "do";
                auto be = std::make_unique<BlockExpr>();
                be->body.push_back(parseStatement());
                u->operand = std::move(be);
                return u;
            }
            // `Foo::term:<ℵ₀>` — a package's TERM by its qualified name
            if (name.size() > 6 && name.compare(name.size() - 6, 6, "::term") == 0 &&
                peek().kind == Tok::Op && peek().text == ":" && !peek().spaceBefore &&
                peek(2).kind == Tok::Op && peek(2).text == "<" && !peek(2).spaceBefore) {
                advance(); advance(); advance();  // Foo::term : <
                std::vector<std::string> w = readAngleWords(">");
                std::string full = name.substr(0, name.size() - 4) + (w.empty() ? "" : w[0]);
                return std::make_unique<NameTerm>(full);
            }
            // `𝑒` (U+1D452 MATHEMATICAL ITALIC SMALL E) is Euler's number, as `e` is
            if (name == "\xF0\x9D\x91\x92" && !(peek().kind == Tok::LParen && !peek().spaceBefore))
                { advance(); return std::make_unique<NameTerm>("e"); }
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
                // `next()` / `last()` / `redo()` — the call form IS the control op;
                // consume the empty parens so they don't become a postfix call on
                // the op's (cooperative) result (zef: `next() R, DEBUG(...)`).
                if (name != "return" && name != "return-rw" &&
                    isKind(Tok::LParen) && peek().kind == Tok::RParen) { advance(); advance(); }
                bool retTerm = startsTermToken(cur()) && !kBlockKeywords.count(cur().text);
                // sub/method/do/start blocks are valid expression operands too, as is
                // an anonymous type literal (`return role {…}`)
                if (isIdent("sub") || isIdent("method") || isIdent("do") || isIdent("start")) retTerm = true;
                if ((isIdent("role") || isIdent("class") || isIdent("grammar")) &&
                    (peek().kind == Tok::LBrace || (peek().kind == Tok::Op && peek().text == "::"))) retTerm = true;
                // `return` is a LISTOP: its operand is the whole comma list, not
                // just the first element. At BP_ASSIGN `(return (1,2), 9)` dropped
                // the `9` and returned only `(1,2)`, while the statement-level
                // `return (1,2), 9` (which parses a full expression) kept it.
                if ((name == "return" || name == "return-rw") && retTerm &&
                    !isKind(Tok::RParen) && !isKind(Tok::Semicolon) && !isKind(Tok::RBrace))
                    u->operand = parseExpr(BP_COMMA);
                return u;
            }
            if (name == "INIT" && peek().kind != Tok::LBrace &&
                peek().kind != Tok::Semicolon && peek().kind != Tok::End) { // INIT <expr> — a value phaser
                advance(); // consume INIT
                // …and the operand may be a whole STATEMENT rather than an
                // expression, exactly as `do`'s is: `state $ = INIT given try
                // {require ::('PDF::Native::Filter::Predictors')} { … }` is how
                // PDF decides once whether a native predictor module is there.
                // Parsed as an expression, `given` read as a call to an
                // undefined routine of that name.
                if (isIdent("given") || isIdent("with") || isIdent("without") ||
                    isIdent("if") || isIdent("unless") || isIdent("for") ||
                    isIdent("while") || isIdent("until") || isIdent("loop") ||
                    isIdent("repeat")) {
                    auto u = std::make_unique<Unary>(); u->op = "do";
                    auto be = std::make_unique<BlockExpr>();
                    auto st = parseStatement();
                    markLoopAsExpr(st.get());
                    be->body.push_back(std::move(st));
                    u->operand = std::move(be);
                    return u;
                }
                return parseExpr(BP_ASSIGN);
            }
            // `proto sub NAME(|) {*}` / `multi sub NAME(…) {…}` as a TERM: the
            // declaration runs in a do-block of its own, and the block's value is
            // `&NAME` there — the routine, or the one-candidate dispatch group.
            // CLI::Version's suite hands `proto sub MAIN(|) {*}` to `use`, and its
            // EXPORT gives `&proto.add_dispatchee` a `my multi sub MAIN(…) {…}`;
            // both used to be "expected variable after declarator".
            if ((name == "proto" || name == "multi") && peek().kind == Tok::Ident &&
                (peek().text == "sub" || peek().text == "method") && peek(2).kind == Tok::Ident) {
                advance();                                   // proto / multi
                bool isM = isIdent("method");
                advance();                                   // sub / method
                auto decl = parseSub(true, name == "proto", isM);
                std::string rn = static_cast<SubDecl*>(decl.get())->name;
                auto be = std::make_unique<BlockExpr>();
                be->body.push_back(std::move(decl));
                auto es = std::make_unique<ExprStmt>();
                ExprPtr val = std::make_unique<VarExpr>("&" + rn);
                if (name == "multi") {
                    // the CANDIDATE just declared, not the group it joined — a
                    // multi here may attach to an outer proto of the same name,
                    // and the group is what `&NAME` would answer
                    auto cands = std::make_unique<MethodCall>();
                    cands->inv = std::move(val); cands->method = "candidates";
                    auto tail = std::make_unique<MethodCall>();
                    tail->inv = std::move(cands); tail->method = "tail";
                    val = std::move(tail);
                }
                es->e = std::move(val);
                be->body.push_back(std::move(es));
                auto u = std::make_unique<Unary>(); u->op = "do"; u->operand = std::move(be);
                return u;
            }
            if (name == "sub" || name == "method") {
                advance();
                auto be = std::make_unique<BlockExpr>();
                be->isSub = true; // `sub {…}` as a term is a Sub, not a bare Block
                be->isMethodTerm = name == "method"; // …and a method binds `self`
                if (isKind(Tok::Ident)) advance(); // optional name (anon use)
                if (isKind(Tok::LParen)) {
                    advance(); sigRetType_.clear();
                    be->params = parseSignature();
                    be->retType = sigRetType_;   // `sub (--> Int) { … }`
                    expectKind(Tok::RParen, ")");
                }
                // The traits between the signature and the block are skipped —
                // but `is rw` / `is raw` says what the ROUTINE RETURNS, and
                // losing it made every anonymous `sub (…) is rw {…}` unassignable
                // ("Target is not assignable"). PDF::COS::Tie generates one per
                // PDF dictionary entry and adds it as the accessor.
                while (!isKind(Tok::LBrace) && !isKind(Tok::End) && !isKind(Tok::Semicolon)) {
                    if (isIdent("is") && peek().kind == Tok::Ident &&
                        (peek().text == "rw" || peek().text == "raw"))
                        be->retRw = true;
                    advance();
                }
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
                if (!inReactBlock_)
                    throw ParseError("Cannot have a 'whenever' block outside the scope of a "
                                     "'supply' or 'react' block", cur().line,
                                     "X::Comp::WheneverOutOfScope", {});
                advance();
                auto call = std::make_unique<Call>(); call->name = "whenever";
                call->args.push_back(parseExpr(BP_COMMA + 1)); // the supply/promise expression
                auto be = std::make_unique<BlockExpr>();
                if (isOp("->") || isOp("<->")) { // whenever $s -> $v { } / <-> $v { }
                    bool doubly = isOp("<->");
                    advance(); be->params = parsePointyParams();
                    if (doubly) for (auto& p : be->params) p.isRw = true;
                }
                if (isKind(Tok::LBrace)) { auto blk = parseBlock(); be->body = std::move(blk->stmts); }
                call->args.push_back(std::move(be));
                return call;
            }
            // `hyper for … { }` / `race for …` / `eager for …` / `lazy for …`
            // are statement prefixes too, exactly like `do for`. Without this
            // `my @a = hyper for 1..4 { … }` parsed as `for 1..4 { my @a = hyper }`
            // and died on an undefined routine `hyper` — which is how
            // mandelbrot.raku got as far as printing its PGM header and stopped.
            //
            // ONLY directly before a loop keyword: `eager @a` and `lazy @a` are
            // list operators that already work, and must keep parsing that way.
            // (`sink` is deliberately not here: it DISCARDS, so folding it into
            // `do` — which collects — would be a different statement.)
            bool loopPrefix = (name == "hyper" || name == "race" || name == "eager" ||
                               name == "lazy") &&
                              peek().kind == Tok::Ident &&
                              (peek().text == "for" || peek().text == "while" ||
                               peek().text == "until" || peek().text == "loop" ||
                               peek().text == "repeat");
            if (name == "do" || name == "try" || name == "gather" || name == "quietly" ||
                name == "once" || loopPrefix ||
                name == "BEGIN" || name == "ENTER") {
                advance();
                auto u = std::make_unique<Unary>();
                // BEGIN/ENTER in value position evaluate their block/expr and yield it
                // (a tree-walker has no separate compile phase, so `do` semantics suffice).
                // hyper/race/eager/lazy collect the loop's values like `do` does.
                // The values are what a program reads; the parallelism hyper/race
                // additionally promise is not modelled here.
                // BEGIN keeps its own op: it evaluates ONCE (per node, the
                // tree-walker's compile-once) — Digest::SHA2 indexes
                // `(BEGIN blob64.new: map { frac 3√$_, 64 }, @primes[^80])[$t]`
                // inside its round loop, and do-semantics recomputed the whole
                // 80-root table per round: 6,400 FatRat square roots per block.
                u->op = (name == "ENTER" || loopPrefix) ? "do" : name;
                if (isKind(Tok::LBrace)) {
                    auto blk = parseBlock();
                    auto be = std::make_unique<BlockExpr>();
                    be->body = std::move(blk->stmts);
                    u->operand = std::move(be);
                } else if (isIdent("for") || isIdent("if") || isIdent("unless") || isIdent("while") ||
                           isIdent("until") || isIdent("loop") || isIdent("repeat") || isIdent("given") ||
                           isIdent("when") || isIdent("with") || isIdent("without") ||
                           // labeled loop: `gather LABEL: for … { next LABEL }` (zef's DEPSPEC:)
                           (isKind(Tok::Ident) && peek().kind == Tok::Op && peek().text == ":" &&
                            !peek().spaceBefore && peek(2).kind == Tok::Ident &&
                            (peek(2).text == "for" || peek(2).text == "while" ||
                             peek(2).text == "until" || peek(2).text == "loop" || peek(2).text == "repeat"))) {
                    // `gather for … {}` / `do if … {}` — the operand is a whole statement
                    auto be = std::make_unique<BlockExpr>();
                    auto st = parseStatement();
                    markLoopAsExpr(st.get()); // `do for …` collects the loop's values
                    be->body.push_back(std::move(st));
                    u->operand = std::move(be);
                } else {
                    // A statement prefix takes the WHOLE remaining expression —
                    // through `=`, through the comma list, and through the loose
                    // `and`/`or`. Measured against Rakudo: `do 1, 2` is (1, 2),
                    // `do 0 or 5` is 5, and `[try bad(), 2]` has ONE element.
                    // Stopping at BP_ASSIGN made `try EXPR or die MSG` parse as
                    // `(try EXPR) or die MSG`, so the die escaped the try it was
                    // written inside — HTTP::Tiny validates an absent proxy with
                    // exactly that idiom and died on every construction.
                    u->operand = parseExpr(0);
                    // …and a trailing statement MODIFIER belongs to that expression:
                    // `do EXPR for LIST` collects one value per iteration, which is how
                    // JSON::Fast's test builds a list (`List.new(|do … for 10 ... 1)`).
                    // Without this the `for` ended the argument and the parse died.
                    u->operand = applyExprModifiers(std::move(u->operand));
                }
                return u;
            }
            if ((name == "for" || name == "while" || name == "until" ||
                 name == "loop" || name == "repeat") &&
                // …but `sub while {}; while();` is that sub's call. Delegating
                // to parseStatement here would bounce straight back off its own
                // end_keyword guard and recurse until the stack gave out.
                !kwCallHere(name)) {
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
            if (name == "require" && !(peek().kind == Tok::Op &&
                (peek().text == "=>" || peek().text == "," ))) {
                // expression-position `require ::($name)` / `require Foo::Bar` —
                // runtime load yielding the loaded type (`(try require ::($m)) ~~ Nil`
                // is zef's plugin probe). A bare `require => v` pair stays a pair.
                advance();
                auto u = std::make_unique<Unary>();
                u->op = "require";
                // A bare module NAME is a name, not an expression: `(require Test
                // <&plan &is>)` read `Test <…>` as a call of a routine `Test`
                if (isKind(Tok::Ident) && !(peek().kind == Tok::LParen && !peek().spaceBefore))
                    u->operand = std::make_unique<StrLit>(advance().text);
                // TIGHTER than a comparison, for the reason the statement form
                // records below.
                else u->operand = parseExpr(BP_COMPARE + 1);
                // The optional IMPORT LIST, as the statement form takes it.
                // `try require ::('Data::Dump::Tree') <&ddt>` is how a module
                // makes a dependency optional, and `try` is what brings it
                // here rather than to the statement arm — so the `<…>` had to
                // be skipped in both places or neither (RakuDoc::Templates).
                {
                    int depth = 0;
                    while (!isKind(Tok::End)) {
                        if (depth == 0 && (isKind(Tok::Semicolon) || isKind(Tok::RBrace) ||
                                           isKind(Tok::Comma) || isKind(Tok::RParen))) break;
                        if (isKind(Tok::LParen) || isKind(Tok::LBracket)) depth++;
                        else if (isKind(Tok::RParen) || isKind(Tok::RBracket)) depth--;
                        advance();
                    }
                }
                return u;
            }
            // …unless the file declared `start` as a sigilless TERM, in which case
            // it is that term: PDF::Content::Image binds the offset of a data
            // URI's payload to `my Numeric \start` and then calls
            // `substr($data-uri, start)`.
            if (name == "start" && peek().kind != Tok::FatArrow && !sigilless_.count("start")) {
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
            if ((name == "my" || name == "our" || name == "state" ||
                 name == "has" || name == "constant") &&
                // `state => header-init` is a PAIR with key "state" (Cro::HTTP2),
                // not a declaration — a `=>` after the keyword means pair
                peek().kind != Tok::FatArrow &&
                // …and `sub has {}; has();` is that sub's call, not a
                // declaration of nothing: the keyword ended at the paren
                !kwCallHere(name)) {
                advance();
                // `my class Foo {…}` / `my role …` as an expression — evaluates to the type.
                // …but only when a DECLARATION follows: `constant class = Foo` names
                // the constant `class`, which is a perfectly good identifier
                // (InterceptAllMethods opens with exactly that line). The
                // declarator needs a name, a body or a qualified name after it.
                if ((isIdent("class") || isIdent("role") || isIdent("grammar") ||
                     isIdent("enum") || isIdent("subset")) &&
                    (peek().kind == Tok::Ident || peek().kind == Tok::LBrace ||
                     (peek().kind == Tok::Op && peek().text == "::"))) {
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
                    noteScalarDecls(decl.get());
                    if (isKind(Tok::Op) && (cur().text == "=" || cur().text == ":=")) {
                        bool listTarget = decl->kind == NK::ListExpr;
                        if (decl->kind == NK::VarExpr) {
                            const std::string& nm = static_cast<VarExpr*>(decl.get())->name;
                            listTarget = !nm.empty() && (nm[0] == '@' || nm[0] == '%');
                            // `constant C = 1, 2, 3` takes the whole list, whatever the sigil
                            if (static_cast<VarExpr*>(decl.get())->declScope == "constant") listTarget = true;
                        }
                        auto as = std::make_unique<Assign>();
                        as->op = advance().text;
                        // binding is a LIST operation whatever the sigil (`my $b :=
                        // 1, 2` binds the List), so it takes the comma list and the
                        // zip/cross infixes exactly as an `@`/`%` declaration does
                        if (as->op == ":=" || as->op == "::=") listTarget = true;
                        as->target = std::move(decl);
                        as->value = parseExpr(listTarget ? BP_ZIP : BP_ASSIGN); // list decls include Z/X
                        if (as->op == "=")
                            checkLiteralDeclType(as->target.get(), as->value.get(), cur().line);
                        return as;
                    }
                    return decl;
                }
            }
            advance(); // consume the name
            // operator-name call: infix:<+>(1,2) / postfix:<i>($x) — canonical op name.
            // `trait_mod` is spelled the same way and is CALLED that way too: a user
            // `trait_mod:<is>` commonly opens by delegating to `trait_mod:<of>($r, T)`.
            // Without it the `:<of>` read as an adverb and the call fell apart.
            if ((name == "infix" || name == "prefix" || name == "postfix" || name == "trait_mod") &&
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
            else if ((name == "infix" || name == "prefix" || name == "postfix" || name == "trait_mod") &&
                isOp(":") && !cur().spaceBefore &&
                peek().kind == Tok::Op && peek().text == "<" && !peek().spaceBefore) {
                advance(); advance(); // : <
                auto w = readAngleWords(">");
                name += ":<" + (w.empty() ? std::string() : w[0]) + ">";
            }
            else if ((name == "infix" || name == "prefix" || name == "postfix" || name == "trait_mod") &&
                     isOp(":") && !cur().spaceBefore && peek().kind == Tok::QwList &&
                     !peek().spaceBefore) {
                // the lexer took `<»+«>` as a word list — its text is the op name
                advance(); // :
                name += ":<" + advance().text + ">";
            }
            else if ((name == "infix" || name == "prefix" || name == "postfix" || name == "trait_mod") &&
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
            // pseudo-package access: `MY::<$x>` / `CORE::<&not>` / `PROCESS::<$IN>`
            // (the lexer folds the trailing `::` into the identifier) — the symbol is
            // resolved through the ordinary scope chain. The key does not have to be
            // written out: `MY::{$name}` computes it and `MY::<<$name>>` interpolates
            // it, which is how a suite asks whether each of its imports arrived.
            std::string pseudoPkg;
            if (name.size() > 2 && name.compare(name.size() - 2, 2, "::") == 0 &&
                isPseudoPkgPath(name.substr(0, name.size() - 2), pseudoPkg) &&
                !cur().spaceBefore &&
                // PROCESS keeps its own computed-key path: its symbols are the
                // `$*NAME` dynamics, not Env slots, and evalIndex reads them there
                (isOp("<") ||
                 ((isKind(Tok::LBrace) ||
                   (cur().kind == Tok::Op &&
                    (cur().text.compare(0, 2, "<<") == 0 || cur().text == "\xc2\xab"))) &&
                  pseudoPkg != "PROCESS"))) {
                std::string sym;      // the written-out key, when there is one
                ExprPtr keyExpr;      // else the expression that names the symbol
                if (isOp("<")) {
                    advance(); // <
                    std::vector<std::string> words = readAngleWords(">");
                    sym = pseudoAngleSymbol(pseudoPkg, words.empty() ? "" : words[0]);
                }
                else if (isKind(Tok::LBrace)) {
                    advance(); // {
                    keyExpr = parseExpression();
                    expectKind(Tok::RBrace, "}");
                }
                else {
                    // `<<…>>` interpolates. A one-word list lexes as a single op
                    // token (`<<$name>>`); anything with a space in it comes through
                    // as opener, words, closer, and only the one-word form of that
                    // names a single symbol — leave the rest to the ordinary parse.
                    const std::string& t = cur().text;
                    if (t.size() >= 4 && t.compare(0, 2, "<<") == 0 &&
                        t.compare(t.size() - 2, 2, ">>") == 0) {
                        keyExpr = parseInterpString(t.substr(2, t.size() - 4));
                        advance();
                    }
                    else {
                        const char* close = t == "<<" ? ">>" : "\xc2\xbb";
                        if (peek(2).kind == Tok::Op && peek(2).text == close) {
                            advance();                       // << / «
                            keyExpr = parseInterpString(advance().text);
                            advance();                       // >> / »
                        }
                    }
                }
                if (!sym.empty() || keyExpr) {
                    // `MY::<&foo>:exists` asks whether the symbol is DECLARED, which
                    // is how a module's export list is usually tested (Test::Output's
                    // suite, and every one of the P5 family's). Lexical lookups only —
                    // `SETTING::<$x>` asks about the SETTING, where a program's own
                    // lexical is absent however visible it is here.
                    // UNIT:: answers here too, and `:p` alongside `:exists`. Only
                    // MY::/LEXICAL:: and only `:exists` used to, so
                    // `UNIT::{"&$_"}:exists` fell out of this branch and the
                    // adverb was read as a routine call — "Undefined routine
                    // 'exists'" — while the same thing in an `if` condition or in
                    // parens was an outright parse error. Test::Output's EXPORT is
                    // written that way and went 2/2 -> 0/2 on it.
                    // …and the NEGATED spelling, `:!exists`, which asks the same
                    // question the other way round. Identity::Utils checks its
                    // export list with `OUTER::MY::{"&$_"}:!exists` — without
                    // this the `:` ended the term and `!exists` was read as a
                    // routine call ("Undefined routine 'exists'"), or, inside
                    // parentheses, was an outright parse error.
                    bool negAdv = isOp(":") && !cur().spaceBefore &&
                                  peek().kind == Tok::Op && peek().text == "!" &&
                                  peek(2).kind == Tok::Ident &&
                                  (peek(2).text == "exists" || peek(2).text == "p");
                    if ((pseudoPkg == "MY" || pseudoPkg == "LEXICAL" || pseudoPkg == "UNIT") &&
                        isOp(":") && !cur().spaceBefore &&
                        (negAdv || (peek().kind == Tok::Ident &&
                                    (peek().text == "exists" || peek().text == "p")))) {
                        advance();                              // ':'
                        if (negAdv) advance();                  // '!'
                        const std::string adv = advance().text; // exists | p
                        auto c = std::make_unique<Call>();
                        c->name = adv == "p" ? "__sym-pair" : "__sym-exists";
                        if (keyExpr) c->args.push_back(std::move(keyExpr));
                        else         c->args.push_back(std::make_unique<StrLit>(sym));
                        // each OUTER in the chain steps one scope out before looking
                        long long hops = 0;
                        for (size_t k = 0; k + 7 <= name.size(); k++)
                            if (name.compare(k, 7, "OUTER::") == 0) hops++;
                        c->args.push_back(std::make_unique<IntLit>(hops));
                        c->args.push_back(std::make_unique<StrLit>(pseudoPkg));
                        if (negAdv) {
                            auto no = std::make_unique<Unary>();
                            no->op = "!";
                            no->operand = std::move(c);
                            return no;
                        }
                        return c;
                    }
                    long long outerHops = 0;
                    for (size_t k = 0; k + 7 <= name.size(); k++)
                        if (name.compare(k, 7, "OUTER::") == 0) outerHops++;
                    if (outerHops) {
                        auto c = std::make_unique<Call>();
                        c->name = "__sym-lookup";
                        if (keyExpr) c->args.push_back(std::move(keyExpr));
                        else         c->args.push_back(std::make_unique<StrLit>(sym));
                        c->args.push_back(std::make_unique<IntLit>(outerHops));
                        return c;
                    }
                    // `CALLER::<$y>` names the CALLER's `$y`, not ours: keep the head
                    // on a symbolic ref, which the evaluator resolves through the
                    // dynamic chain (the plain-name approximation read our own $y)
                    if (pseudoPkg == "CALLER" || pseudoPkg == "CALLERS") {
                        auto sr = std::make_unique<SymbolicRef>();
                        sr->pkg = pseudoPkg;   // CALLERS walks every caller, CALLER one
                        if (keyExpr) sr->nameExpr = std::move(keyExpr);
                        else sr->nameExpr = std::make_unique<StrLit>(sym);
                        return sr;
                    }
                    if (keyExpr) {
                        auto sr = std::make_unique<SymbolicRef>();
                        sr->nameExpr = std::move(keyExpr);
                        return sr;
                    }
                    auto pv = std::make_unique<VarExpr>(sym);
                    pv->processScoped = (pseudoPkg == "PROCESS");
                    pv->viaPseudoPkg  = true;
                    pv->pseudoPkg     = pseudoPkg;
                    return pv;
                }
            }
            // `Foo::<bar>` — a slot in a REAL package's symbol table, the same
            // syntax the pseudo-packages above use. Read and WRITTEN: roast's
            // TestHOW does `EXPORTHOW::<class> = TestHOW`, and without this the
            // whole thing parsed as a call to a routine named `EXPORTHOW::`, so
            // the module would not load. The slot is just the qualified global
            // `Foo::bar`, which is where `our`-scoped symbols already live.
            if (name.size() > 2 && name.compare(name.size() - 2, 2, "::") == 0 &&
                !isPseudoPkgPath(name.substr(0, name.size() - 2), pseudoPkg) &&
                isOp("<") && !cur().spaceBefore) {
                advance(); // <
                std::vector<std::string> words = readAngleWords(">");
                std::string pkg = name.substr(0, name.size() - 2);
                std::string key = words.empty() ? "" : words[0];
                // A SIGILLED key names the very variable its long name does:
                // `A::<&foo>` and `&A::foo` are one slot, `A::<$bar>` and
                // `$A::bar` another. Spell it the way `our` publishes it —
                // sigil first — or the two syntaxes get a slot each, which is
                // how `our sub foo` stayed invisible to `A::<&foo>` and how
                // `A::<$bar> = 99` left the module's own `$bar` at 42.
                bool sigilled = !key.empty() &&
                    (key[0] == '$' || key[0] == '@' || key[0] == '%' || key[0] == '&');
                auto ve = std::make_unique<VarExpr>(
                    sigilled ? key.substr(0, 1) + pkg + "::" + key.substr(1)
                             : pkg + "::" + key);
                ve->pkgSymbol = true; // assignment autovivifies the slot
                return ve;
            }
            // A bare pseudo-package used as a VALUE — `UNIT::.grep: {…}`,
            // `MY::.keys` — is that scope's symbol table. String::Utils builds
            // its whole export list from `UNIT::.grep: { .key.starts-with('&') }`,
            // and the name fell through to an empty Stash, so nothing was
            // exported and every call landed on a built-in of the same name
            // (`after` answered the infix's True). The call answers a Hash of the
            // scope's symbols; the `:exists`/`:p` subscripts above keep their own path.
            if (name.size() > 2 && name.compare(name.size() - 2, 2, "::") == 0 &&
                isPseudoPkgPath(name.substr(0, name.size() - 2), pseudoPkg) &&
                (pseudoPkg == "UNIT" || pseudoPkg == "MY" || pseudoPkg == "LEXICAL" ||
                 pseudoPkg == "OUTER") &&
                !isOp("<") && !isKind(Tok::LBrace) && !isKind(Tok::LParen)) {
                auto c = std::make_unique<Call>();
                c->name = "__sym-stash";
                long long hops = 0;
                for (size_t k = 0; k + 7 <= name.size(); k++)
                    if (name.compare(k, 7, "OUTER::") == 0) hops++;
                c->args.push_back(std::make_unique<IntLit>(hops));
                c->args.push_back(std::make_unique<StrLit>(pseudoPkg));
                return c;
            }
            // `Foo::{EXPR}` — the same package-stash slot with a RUNTIME key.
            // Sparrow6 builds its export list this way:
            //     BEGIN for <&config &bash …> { EXPORT::DEFAULT::{$_} = ::($_) }
            // Desugars to `Foo.WHO.{EXPR}`: WHO answers the persistent per-package
            // stash (pkgStashes_), which is assignable through the lvalue path and
            // coherent with the `Foo::<bar>` form (angle writes land in qualified
            // globals, which WHO re-syncs in; stash writes are the angle READ
            // path's fallback).
            if (name.size() > 2 && name.compare(name.size() - 2, 2, "::") == 0 &&
                !isPseudoPkgPath(name.substr(0, name.size() - 2), pseudoPkg) &&
                isKind(Tok::LBrace) && !cur().spaceBefore) {
                advance(); // {
                auto who = std::make_unique<MethodCall>();
                who->inv = std::make_unique<NameTerm>(name.substr(0, name.size() - 2));
                who->method = "WHO";
                auto ix = std::make_unique<Index>();
                ix->base = std::move(who);
                ix->index = parseExpression();
                ix->isHash = true;
                expectKind(Tok::RBrace, "}");
                return ix;
            }
            // parameterized type: `Array[Int]`, `Hash[Int,Str]`, `Foo[Bar]` — a capitalized
            // runtime type parameterization with a variable: array[$T].new / Blob[$T]
            if (!name.empty() && (ascii::isupper((unsigned char)name[0]) || name == "array") &&
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
            if (!name.empty() && (ascii::isupper((unsigned char)name[0]) || name == "array") &&
                isKind(Tok::LBracket) && !cur().spaceBefore &&
                peek().kind == Tok::Ident && !peek().text.empty() &&
                (ascii::isupper((unsigned char)peek().text[0]) ||
                 [&]{ static const std::set<std::string> nat = {
                          "int","int8","int16","int32","int64","uint","uint8","uint16",
                          "uint32","uint64","byte","atomicint","num","num32","num64","str"};
                      return nat.count(peek().text) > 0; }())) {
                advance(); // [
                std::string params;
                bool atArgStart = true;   // a colonpair is a NAMED argument only here
                int tpd = 1; // nesting: Baz[Foo[Int], Bar[Int]]
                while (tpd > 0 && !isKind(Tok::End)) {
                    if (isKind(Tok::LBracket)) { tpd++; params += "["; advance(); continue; }
                    if (isKind(Tok::RBracket)) {
                        tpd--;
                        if (tpd == 0) break;
                        params += "]"; advance(); continue;
                    }
                    // A NAMED argument — `R[Type, :prefix<P_>]` / `R[Type, prefix
                    // => 'P_']`. Only identifiers were kept here, so the value was
                    // dropped and the colonpair's KEY joined the positional list:
                    // `BitEnum[MyBits, :prefix<BIT_>]` bound no prefix at all and
                    // every lookup through it failed. Kept in the `:name<value>`
                    // spelling the pun builder reads back.
                    if (isKind(Tok::Comma)) { advance(); atArgStart = true; continue; }
                    // …and ONLY at the start of an argument: `Array[Str:D]` is a
                    // type with a SMILEY, not a type plus a named `:D`, and reading
                    // it as one broke `Array[Str:D] ~~ Positional[Str]`.
                    if ((atArgStart && isOp(":") && peek().kind == Tok::Ident) ||
                        (isKind(Tok::Ident) && peek().kind == Tok::FatArrow)) {
                        std::string cp;
                        if (isOp(":")) { advance(); cp = ":" + advance().text; }
                        else { cp = ":" + advance().text; advance(); }   // name =>
                        if (isKind(Tok::QwList) && !cur().spaceBefore) cp += "<" + advance().text + ">";
                        else if (isOp("<") && !cur().spaceBefore) {
                            advance();
                            std::vector<std::string> ws = readAngleWords(">");
                            std::string joined;
                            for (auto& w : ws) { if (!joined.empty()) joined += " "; joined += w; }
                            cp += "<" + joined + ">";
                        }
                        else if (isKind(Tok::StrLit) || isKind(Tok::StrInterp) ||
                                 isKind(Tok::IntLit) || isKind(Tok::NumLit))
                            cp += "<" + advance().text + ">";
                        else if (isKind(Tok::LParen)) {
                            advance();
                            std::string inner;
                            while (!isKind(Tok::RParen) && !isKind(Tok::End)) inner += advance().text;
                            matchKind(Tok::RParen);
                            cp += "<" + inner + ">";
                        }
                        if (!params.empty()) params += ",";
                        params += cp;
                        atArgStart = false;
                        continue;
                    }
                    if (isKind(Tok::Ident)) {
                        if (!params.empty() &&
                            (ascii::isalnum((unsigned char)params.back()) || params.back() == ']'))
                            params += ",";
                        params += advance().text;
                        // package-qualified segment: Foo::Bar
                        while (isOp("::") && peek().kind == Tok::Ident) {
                            advance(); params += "::" + advance().text;
                        }
                        atArgStart = false;
                        continue;
                    }
                    advance(); // commas / smileys — the comma is re-added implicitly
                }
                expectKind(Tok::RBracket, "]");
                auto nt = std::make_unique<NameTerm>(name);
                nt->ofType = params;
                return nt;
            }
            // type smiley on a type name: `Foo:D` / `Channel:U` / `Bar:_` — a
            // DEFINITENESS-constrained type, which the value carries so that
            // `.^name`, smartmatch and `.^base_type` all see it
            if (!name.empty() && ascii::isupper((unsigned char)name[0]) &&
                isOp(":") && !cur().spaceBefore && peek().kind == Tok::Ident &&
                (peek().text == "U" || peek().text == "D" || peek().text == "_")) {
                advance(); std::string sm = advance().text; // : and the smiley letter
                auto nt = std::make_unique<NameTerm>(name);
                nt->defConstraint = sm == "D" ? 1 : sm == "U" ? 2 : 0;
                return nt;
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
            if (isKind(Tok::LParen) && (!cur().spaceBefore || slangSpacedCall(name))) {
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
                c->parenned = true;   // `f(…)`, against the listop form just below
                c->args = std::move(callArgs);
                takeTrailingAdverbs(c->args); // `f($x):12size`
                return c;
            }
            // list-op style call without parens
            // but a capitalized bareword followed by a block is a type + block body, e.g. `if Mu { }`
            // — EXCEPT the all-caps introspection subs, which are routines rather than
            // types, so `WHAT {3 => 4}` asks what the hash is instead of parsing as the
            // bareword `WHAT` followed by an unrelated block. EVAL is in the set for
            // the same reason and one more: `EVAL { … }` is the Perl 5 block eval,
            // and Raku's answer is a runtime error naming `try` — which it can only
            // give if the block arrives as an ARGUMENT rather than as a stray block
            // after a bare name (roast S29-context/eval.t, "block EVAL is gone").
            static const std::set<std::string> capsSubs = {
                "WHAT", "WHO", "HOW", "VAR", "WHICH", "WHY", "DEFINITE", "EVAL"};
            if (isKind(Tok::LBrace) && !name.empty() && ascii::isupper((unsigned char)name[0]) &&
                !capsSubs.count(name))
                return std::make_unique<NameTerm>(name);
            // A capitalized bareword (a type) followed by whitespace then `.method` is
            // a postfix method call on the type — `Thing .new` is `Thing.new`, NOT a
            // listop call `Thing(.new)`. (Whitespace before a postfix `.` is allowed.)
            // Require an identifier after the dot so `.method` is meant, not `.=`/`.(`.
            if (!name.empty() && ascii::isupper((unsigned char)name[0]) &&
                cur().kind == Tok::Op && cur().text == "." && cur().spaceBefore &&
                peek().kind == Tok::Ident)
                return std::make_unique<NameTerm>(name);
            // A name declared sigilless (`my \x`, `\a` param, `-> \d`) is a TERM, not a
            // listop: `x2 < 0 || 1 > 7` is two comparisons, never `x2(< 0 || 1 >) 7`.
            // (A tight `name(...)` call was already handled above, so invoking a
            // Callable held in a sigilless var still works.)
            // `sub term:<no-commas?>` — a declared term may end in `?` or `!`,
            // which the lexer cannot know about: it hands back the identifier and
            // the punctuation separately. Join them when the joined spelling is a
            // name something declared (Lingua::EN::Numbers queries its flag with
            // `no-commas?`, and the bare `no-commas` beside it is a different sub).
            if (!cur().spaceBefore && cur().kind == Tok::Op &&
                (cur().text == "?" || cur().text == "!") &&
                sigilless_.count(name + cur().text)) {
                std::string full = name + advance().text;
                auto nt = std::make_unique<NameTerm>(full); nt->noAutoQuote = t.flag; return nt;
            }
            if (sigilless_.count(name)) { auto nt = std::make_unique<NameTerm>(name); nt->noAutoQuote = t.flag; return nt; }
            // For +/-/? the prefix reading is only valid when the operand is
            // tight against the operator (`f -5` => f(-5), but `f - 5` => f() - 5).
            bool listopOk = startsListopArg(cur(), name);
            // Higher-order list builtins accept a pointy-block first arg without parens:
            // `map -> $v {…}, @list` (a bare `{…}` already works via startsListopArg). Gated
            // to these names so a general `bareword ->` isn't misread as a listop call.
            if (!listopOk && (cur().text == "->" || cur().text == "<->")) {
                static const std::set<std::string> blockListops = {
                    "map", "grep", "first", "sort", "reduce", "produce",
                    "classify", "categorize", "grep-index", "first-index",
                    "deepmap", "duckmap", "nodemap",
                };
                if (blockListops.count(name)) listopOk = true;
            }
            // callsame/nextsame are nullary redispatchers — they reuse the current
            // args and take none of their own, so `"[" ~ callsame ~ "]"` must not read
            // the trailing `~ "]"` (prefix ~) as an argument. (callwith/nextwith/
            // samewith DO take args and are left alone.)
            if (name == "callsame" || name == "nextsame" || name == "nextcallee") listopOk = false;
            // `so *` / `not *` — a bare Whatever curries through the boolish prefix
            // (a general `name *` stays multiplication)
            if (!listopOk && cur().kind == Tok::Op && cur().text == "*" &&
                (name == "so" || name == "not")) listopOk = true;
            // `map *², @a` (lexed `* ** 2`) / `grep * > 5, @a` — a leading
            // Whatever curries through ANY infix for the higher-order list
            // builtins; gated by name so a general `name * 2` stays
            // multiplication on the call's result
            if (!listopOk && cur().kind == Tok::Op && cur().text == "*" &&
                peek().kind == Tok::Op) {
                static const std::set<std::string> whateverListops = {
                    "map", "grep", "first", "sort", "reduce", "produce",
                    "min", "max", "sum", "classify", "categorize",
                    "grep-index", "first-index",
                    "deepmap", "duckmap", "nodemap",
                };
                if (whateverListops.count(name)) listopOk = true;
            }
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
            // `so` and `not` are LOOSE UNARY prefixes, not listops: they take one
            // argument and stop at the comma, so `f(so ($x), 2)` passes two arguments
            // rather than one. (`so 1, 2` is `(so 1), 2`.)
            if (listopOk && (name == "so" || name == "not")) {
                auto c = std::make_unique<Call>();
                c->name = name;
                c->args.push_back(parseExpr(BP_COMMA + 1));
                // A trailing `:adverb` belongs to whatever the operand was
                // (`not %h<k>:exists`); it never modifies `so`/`not` themselves, and
                // leaving it unconsumed would read as a second term.
                while (isOp(":") && !cur().spaceBefore && peek().kind == Tok::Ident) {
                    advance(); advance();
                    if (isKind(Tok::LParen)) { int d = 0;
                        do { if (isKind(Tok::LParen)) d++; else if (isKind(Tok::RParen)) d--; advance(); }
                        while (d > 0 && !isKind(Tok::End)); }
                }
                return c;
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
                // the tight ':' after the invocant expression is the marker —
                // and so is a ':' with space on BOTH sides (`xyzzy { … } : 1, 2`),
                // which cannot be an adverb, since an adverb's name is tight.
                if (isOp(":") && (!cur().spaceBefore || peek().spaceBefore) &&
                    !(!peek().spaceBefore && (peek().kind == Tok::Ident || peek().kind == Tok::IntLit))) {
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
                // `foo 5 :adv` — a spaced adverb after a listop's argument is the
                // listop's own named argument
                while (spacedAdverbAhead()) c->args.push_back(parseColonPair());
                while (matchKind(Tok::Comma) && startsTermToken(cur())) {
                    c->args.push_back(parseExpr(BP_ASSIGN));
                    while (spacedAdverbAhead()) c->args.push_back(parseColonPair());
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
            // (Rakudo: at the very end of the source it is a group whose panic is
            // "seems to be malformed"; `put` has a message of its own)
            if ((name == "put" || name == "say" || name == "print") &&
                (isKind(Tok::Semicolon) || isKind(Tok::End) || isKind(Tok::RBrace))) {
                const std::string obs = "Unsupported use of bare \"" + name + "\". In Raku please use: ." + name +
                    " if you meant to call it as a method on $_, or use an explicit invocant or argument, "
                    "or use &" + name + " to refer to the function as a noun.";
                if (isKind(Tok::End) && name != "put")
                    throw ParseError("Argument to \"" + name + "\" seems to be malformed", cur().line, "X::Comp::Group",
                                     {{"panic", "X::Comp::AdHoc"}, {"worry", obs}});
                if (name == "put")
                    throw ParseError("Function \"put\" may not be called without arguments (please use () or whitespace "
                                     "to denote arguments, or &put to refer to the function as a noun, or use .put if "
                                     "you meant to call it as a method on $_)", cur().line, "X::Comp::AdHoc", {});
                throw ParseError(obs, cur().line, "X::Obsolete",
                                 {{"old", "bare \"" + name + "\""},
                                  {"replacement", "." + name + " if you meant to call it as a method on $_, or use an "
                                   "explicit invocant or argument, or use &" + name + " to refer to the function as a noun"}});
            }
            return std::make_unique<NameTerm>(name);
        }
        default:
            // `42 +` (input ran out) or `$( 42 * )` (something that is not a
            // term is next, right where the operand belongs) — an infix with no
            // right-hand side. Rakudo's exact wording and type, because a REPL
            // shows this diagnostic to a human, and a document weaver puts it
            // in the document: `error()`'s "(got '')" describes an
            // end-of-input token that isn't there to see. atEof still stands
            // for the ran-out case, so rakupp's own REPL asks for a
            // continuation line rather than refusing the input.
            //
            // Only where an infix's operand was actually required: running out
            // of input has many other causes (`say $\` is Rakudo's "Confused",
            // and roast pins it), and this message used to be given to all of
            // them with "Confused:" glued on the front to cover both readings.
            if (pos_ == infixRhsPos_) {
                ParseError pe("Missing required term after infix", cur().line, isKind(Tok::End));
                pe.exType = "X::Comp::AdHoc";
                throw pe;
            }
            error("Confused"); // Rakudo's generic "confused parse" message
    }
}

// A word like `:name`, `:!name`, `:name(expr)` or `:42name` inside a
// «…»/<<…>>/qww word list is a COLONPAIR, as in Rakudo — `enum Loglevels
// <<:TRACE(1) DEBUG …>>` (Log::Async) relies on the pair reading to number
// its values. Returns null when the word is not pair-shaped.
ExprPtr Parser::angleColonPair(const std::string& w) {
    if (w.size() < 2 || w[0] != ':') return nullptr;
    size_t i = 1; bool neg = false;
    if (w[i] == '!') { neg = true; i++; if (i >= w.size()) return nullptr; }
    if (ascii::isdigit((unsigned char)w[i]) && !neg) { // :42name — value-first pair
        size_t d = i; while (d < w.size() && ascii::isdigit((unsigned char)w[d])) d++;
        if (d < w.size() && (ascii::isalpha((unsigned char)w[d]) || w[d] == '_' ||
                             (unsigned char)w[d] >= 0x80)) {
            std::string ds = w.substr(i, d - i);
            if (ds.size() > 18) return nullptr; // past long long: not a value-first pair
            auto pe = std::make_unique<PairExpr>();
            pe->colonForm = true;
            pe->key = w.substr(d);
            pe->value = std::make_unique<IntLit>(std::stoll(ds));
            return pe;
        }
        return nullptr;
    }
    // A colonpair key is an IDENTIFIER, and a Raku identifier may be any Unicode
    // letter — so the non-ASCII bytes of a UTF-8 letter continue it. Requiring
    // ASCII here made `enum Weekday <<:måndag(1) tisdag onsdag>>` read the whole
    // token `:måndag(1)` as a literal enum KEY at ordinal 0 and shift every
    // following name down by one: Swedish::TextDates_sv answered onsdag for
    // Thursday and trettonde for twelve, silently and only under rakupp.
    auto idStart = [](unsigned char c) { return ascii::isalpha(c) || c == '_' || c >= 0x80; };
    auto idCont  = [](unsigned char c) { return ascii::isalnum(c) || c == '_' || c >= 0x80; };
    if (!idStart((unsigned char)w[i])) return nullptr;
    size_t j = i;
    while (j < w.size() && (idCont((unsigned char)w[j]) ||
           ((w[j] == '-' || w[j] == '\'') && j + 1 < w.size() &&
            idStart((unsigned char)w[j + 1])))) j++;
    auto pe = std::make_unique<PairExpr>();
    pe->colonForm = true;
    pe->key = w.substr(i, j - i);
    if (j == w.size()) { pe->value = std::make_unique<BoolLit>(!neg); return pe; }
    if (neg) return nullptr;
    if (w[j] == '(' && w.back() == ')' && j + 1 < w.size())
        { pe->value = parseEmbeddedExpr(w.substr(j + 1, w.size() - j - 2)); return pe; }
    if (w[j] == '<' && w.back() == '>' && j + 1 < w.size())
        { pe->value = std::make_unique<StrLit>(w.substr(j + 1, w.size() - j - 2)); return pe; }
    return nullptr;
}

// A qq-word-list word that needs no run-time work: an allomorph when it spells
// a number and the list val()s its words (`«42»`, `qqww:v[42]`), else a Str.
static ExprPtr qqwwStaticWord(const std::string& w, bool val) {
    if (val)
        if (ExprPtr num = angleWordNumeric(w)) {
            auto al = std::make_unique<AllomorphLit>();
            al->num = std::move(num); al->str = w;
            return al;
        }
    return std::make_unique<StrLit>(w);
}

// One word of a «…»/<<…>>/qqww list, appended to `arr` as one or more items
// with their `__qqww` spelling in `flags`: `l` an item as it stands, `s` an
// interpolated value to split on whitespace and val(), `w` to split only, `v`
// a quoted interpolation to val() whole. Rakudo's qq:ww rules:
//   colonpair → Pair; quoted span → one word (double quotes interpolate), and
//   val()'d under :v — `«"4.5"»` is a RatStr; a bare word that interpolates
//   is one word PER literal run and PER interpolation (`«a$x»` is ("a", "a")),
//   and an interpolated value splits on whitespace (an empty one is no word).
// «…» IS qqww:v, and roast compares the two element for element
// (S02-literals/allomorphic.t), so both come through here.
void Parser::qqwwAddWord(ArrayLit& arr, std::string& flags, const std::string& w, bool val) {
    if (w.size() > 1 && w[0] == ':')
        if (ExprPtr cp = angleColonPair(w)) { arr.items.push_back(std::move(cp)); flags += 'l'; return; }
    auto stringified = [](ExprPtr e) {
        // stringified HERE, so the list cannot flatten `@a[]` into several items
        // that the flags no longer line up with
        auto mc = std::make_unique<MethodCall>();
        mc->inv = std::move(e);
        mc->method = "Str";
        return mc;
    };
    // the text of an interpolation with nothing to interpolate, if that is what it is
    auto staticText = [](const Expr* e, std::string& out) {
        if (e->kind == NK::StrLit) { out = static_cast<const StrLit*>(e)->v; return true; }
        if (e->kind != NK::InterpStr) return false;
        out.clear();
        for (auto& p : static_cast<const InterpStr*>(e)->parts) {
            if (p->kind != NK::StrLit) return false;
            out += static_cast<const StrLit*>(p.get())->v;
        }
        return true;
    };
    std::string text;
    if (w.size() >= 2 && (w.front() == '\'' || w.front() == '"') && w.back() == w.front()) {
        std::string inner = w.substr(1, w.size() - 2);
        if (w.front() == '\'') { arr.items.push_back(qqwwStaticWord(inner, val)); flags += 'l'; return; }
        ExprPtr is = parseInterpString(inner);
        if (staticText(is.get(), text)) { arr.items.push_back(qqwwStaticWord(text, val)); flags += 'l'; return; }
        if (val) { arr.items.push_back(stringified(std::move(is))); flags += 'v'; }
        else { arr.items.push_back(std::move(is)); flags += 'l'; }
        return;
    }
    if (w.find_first_of("$@{\\") == std::string::npos) {
        arr.items.push_back(qqwwStaticWord(w, val)); flags += 'l'; return;
    }
    ExprPtr is = parseInterpString(w);
    if (staticText(is.get(), text)) { arr.items.push_back(qqwwStaticWord(text, val)); flags += 'l'; return; }
    for (auto& part : static_cast<InterpStr*>(is.get())->parts) {
        if (part->kind == NK::StrLit) {
            // a literal run (escapes already applied) is a word as it stands
            arr.items.push_back(qqwwStaticWord(static_cast<StrLit*>(part.get())->v, val));
            flags += 'l';
        }
        else { arr.items.push_back(stringified(std::move(part))); flags += val ? 's' : 'w'; }
    }
}

// The finished list. Without run-time work, one word is that word, as `<a>` is
// (`«a»` is "a"). With it, the list goes through `__qqww`; under val() a lone
// interpolation that comes to exactly one word is that word (`«$x»`,
// `qqww:v{$n}`), and otherwise — `qqww{$x}` — it stays a List.
ExprPtr Parser::qqwwFinish(std::unique_ptr<ArrayLit> arr, const std::string& flags, bool val) {
    if (flags.find_first_not_of('l') == std::string::npos) {
        if (arr->items.size() == 1) {
            // a lone colonpair is a Pair VALUE, not a named argument: `f(«:a(1)»)`
            if (arr->items[0]->kind == NK::Pair)
                static_cast<PairExpr*>(arr->items[0].get())->parenned = true;
            return std::move(arr->items[0]);
        }
        return arr;
    }
    auto c = std::make_unique<Call>();
    c->name = "__qqww";
    c->args.push_back(std::make_unique<StrLit>((val && flags.size() == 1 ? "c" : "n") + flags));
    c->args.push_back(std::move(arr));
    return c;
}

// A `<<X>>` the lexer fused into one hyper-metaop token, in a place where it
// can only be a one-word `«X»` list: a term, or a subscript tight on a term.
static bool fusedQqww(const Token& t) {
    const std::string& s = t.text;
    return t.kind == Tok::Op && s.size() > 4 && s.compare(0, 2, "<<") == 0 &&
           s.compare(s.size() - 2, 2, ">>") == 0 &&
           s.find_first_of("<>", 2) == s.size() - 2;
}

bool Parser::takeQqwwWords(std::vector<std::string>& words) {
    if (isOp("\xC2\xAB")) { advance(); words = readAngleWords("\xC2\xBB"); return true; }
    if (isOp("<<")) { advance(); words = readAngleWords(">>"); return true; }
    if (fusedQqww(cur())) {
        const std::string t = advance().text;
        words = {t.substr(2, t.size() - 4)};
        return true;
    }
    return false;
}

// The whole `«…»` / `<<…>>` list: qq:ww:v.
ExprPtr Parser::qqwwList(const std::vector<std::string>& words) {
    auto arr = std::make_unique<ArrayLit>();
    arr->isList = true;
    std::string flags;
    for (auto& w : words) qqwwAddWord(*arr, flags, w, true);
    return qqwwFinish(std::move(arr), flags, true);
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
    p.userDeclarators_ = userDeclarators_;
    p.wordInfixSubs_ = wordInfixSubs_;   // `"{ div 3 }"` sees this unit's `sub div`
    p.kwNamedSubs_ = kwNamedSubs_;       // …and its `sub if`, so `"{ if() }"` calls it
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
    const bool qqww = close == "\xC2\xBB" || close == ">>"; // «…» / <<…>> (quotes are syntax)
    int braces = 0;          // inside a `{…}` in a qqww word, quotes are code
    bool afterQuote = false; // the token before was a quoted span: start a new word
    while (!(isOp(close) && depth == 0) && !isKind(Tok::End)) {
        // `<Zm8=>` — the lexer fused the word's trailing `=` with the closing angle
        // into a FAT ARROW. Inside a word list it is just those two characters, so
        // demote it to an operator token and let the end-glue rule below split it.
        if (close == ">" && cur().kind == Tok::FatArrow && cur().text == "=>")
            toks_[pos_].kind = Tok::Op;
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
            // …and re-lex what the close leaves behind together with the operator
            // tokens glued after it: `%h<<a b>>==1` lexes `>>=` + `=`, and the split
            // alone leaves two assignments where the source has one `==`.
            else if (toks_[pos_].text[0] != '>') {
                std::string glued = toks_[pos_].text;
                size_t j = pos_ + 1;
                while (j < toks_.size() && !toks_[j].spaceBefore &&
                       (toks_[j].kind == Tok::Op || toks_[j].kind == Tok::FatArrow))
                    glued += toks_[j++].text;
                if (j > pos_ + 1) {
                    std::vector<Token> nt = Lexer(glued, false).tokenize();
                    if (!nt.empty() && nt.back().kind == Tok::End) nt.pop_back();
                    bool allOps = true;
                    for (auto& k : nt) allOps = allOps && (k.kind == Tok::Op || k.kind == Tok::FatArrow);
                    if (allOps && !nt.empty() && nt.size() < j - pos_) {
                        for (auto& k : nt) { k.line = toks_[pos_].line; k.spaceBefore = false; }
                        toks_.erase(toks_.begin() + pos_, toks_.begin() + j);
                        toks_.insert(toks_.begin() + pos_, nt.begin(), nt.end());
                    }
                }
            }
            return words;
        }
        // `<<… -b-c->>`: the lexer took `->` and left the other `>` alone, so the
        // closing `>>` is split across two tokens. Put it back together; the
        // end-glue rule below then peels the word off the front.
        if (close == ">>" && depth == 0 && cur().kind == Tok::Op && cur().text.size() >= 2 &&
            cur().text.back() == '>' && peek().kind == Tok::Op && peek().text == ">" &&
            !peek().spaceBefore) {
            toks_[pos_].text += ">";
            toks_.erase(toks_.begin() + pos_ + 1);
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
        // a VersionLit token dropped its `v` at the lexer — restore it, or the
        // word list <v8.OMG vfe.xxx> loses the prefix on the v+digit entries
        // (Cro::Uri's IPvFuture test hosts)
        std::string wt = t.kind == Tok::VersionLit ? "v" + t.text : t.text;
        bool sep = words.empty() || t.spaceBefore;
        const bool quoted = qqww && (t.kind == Tok::StrLit || t.kind == Tok::StrInterp);
        if (quoted) {
            // the lexer took the quotes off; qqwwAddWord needs them back to keep
            // `«"$y"»` one word, and so does code in a word (`«{"a b"}»`). Outside
            // a block a quoted span is a word of its own, glued or not:
            // `«x"a b"»` is ("x", "a b").
            const char q = t.kind == Tok::StrInterp ? '"' : '\'';
            wt = q + wt + q;
        }
        if ((quoted || afterQuote) && braces == 0) sep = true;
        afterQuote = quoted && braces == 0;
        if (qqww && t.kind == Tok::LBrace) braces++;
        else if (qqww && t.kind == Tok::RBrace && braces > 0) braces--;
        if (sep) words.push_back(wt);
        else words.back() += wt;
        advance();
    }
    // `%x<` at end of input (or `%x<a;` mid-file, where the rest of the statement
    // was read as words) — a silent empty slice used to swallow the statement
    if (isKind(Tok::End)) error("Unable to parse quote-words subscript; couldn't find '" + close + "'");
    matchOp(close);
    return words;
}
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
static std::string scanInterpBlock(const std::string& raw, size_t& j) {
    size_t n = raw.size();
    int depth = 1; std::string inner;
    char q = 0;                    // active quote char inside the block, if any
    while (j < n && depth > 0) {
        char c = raw[j];
        if (q) {
            if (c == '\\' && q == '"' && j + 1 < n) { inner += c; inner += raw[j+1]; j += 2; continue; }
            if (c == q) q = 0;
        }
        else if (c == '\'' || c == '"') q = c;
        else if (c == '{') depth++;
        else if (c == '}') { depth--; if (depth == 0) break; }
        inner += c; j++;
    }
    return inner;
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
        return uniBinaryProp(cp, "ID_Continue") == 1;
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
                    // (\x \o \c with digits or brackets were handled above; a
                    // bare `\c` with nothing usable after it is reserved too)
                    if (ascii::isalpha((unsigned char)e))
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
            // balanced code block (quote-aware: a brace inside '…'/"…" is text)
            size_t j = i + 1;
            std::string inner = scanInterpBlock(raw, j);
            flush();
            try { result->parts.push_back(parseEmbeddedExpr(inner)); } catch (...) { rethrowIfObsolete(); }
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
                while (j < n && d > 0) { if (raw[j]=='(') d++; else if (raw[j]==')') d--; var += raw[j++]; }
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
            } else if (j + 1 < n && raw[j] == '.' && (raw[j+1] == '[' || raw[j+1] == '{' || raw[j+1] == '(')) {
                var += raw[j++]; // .
                char open = raw[j], close = open == '[' ? ']' : open == '{' ? '}' : ')';
                int d = 1; var += raw[j++];
                while (j < n && d > 0) { if (raw[j]==open) d++; else if (raw[j]==close) d--; var += raw[j++]; }
                commit();
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
                    while (j < n && d > 0) { if (raw[j]=='(') d++; else if (raw[j]==')') d--; var += raw[j++]; }
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
            else try { result->parts.push_back(parseEmbeddedExpr(var)); } catch (...) { rethrowIfObsolete(); lit += var; }
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
            try { result->parts.push_back(parseEmbeddedExpr(var)); } catch (...) { rethrowIfObsolete(); lit += var; }
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
                try { result->parts.push_back(parseEmbeddedExpr("&" + fname + "(" + argsrc + ")")); }
                catch (...) { rethrowIfObsolete(); }
                i = k2 + 1;
                continue;
            }
            // bare &name without parens: stays literal text
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
                // …but `"${$x}"` and `"@{$x}"` are the Perl 5 DEREFERENCES, which
                // Raku refuses by name wherever they are written — inside a string
                // as much as outside one.
                {
                    std::string t = inner;
                    while (!t.empty() && (t.front() == ' ' || t.front() == '\t')) t.erase(t.begin());
                    while (!t.empty() && (t.back() == ' ' || t.back() == '\t')) t.pop_back();
                    bool bareVar = t.size() > 1 && (t[0] == '$' || t[0] == '@' || t[0] == '%') &&
                                   t.find_first_of(" \t()[]{}.<>+-*/~,;") == std::string::npos;
                    if (bareVar)
                        throw ParseError("Unsupported use of " + std::string(1, sig) + "{" + t +
                                         "}. In Raku please use: " + std::string(1, sig) + "(" + t + ").",
                                         0, "X::Obsolete",
                                         {{"old", std::string(1, sig) + "{" + t + "}"},
                                          {"replacement", std::string(1, sig) + "(" + t + ")"}});
                }
                flush();
                try { result->parts.push_back(parseEmbeddedExpr(std::string(1, sig) + "(" + inner + ")")); }
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
            flush();
            try {
                ExprPtr part = parseEmbeddedExpr(var);
                // a compile-time magical reads its NODE's line — stamp the
                // string's own position, not the sub-parser's line 1
                // ("line $?LINE" — Log::Async's context test)
                if (part && part->kind == NK::VarExpr && var.rfind("$?", 0) == 0) {
                    int base = pos_ > 0 ? toks_[pos_ - 1].line : cur().line;
                    for (size_t nl = 0; nl < i; nl++) if (raw[nl] == '\n') base++;
                    static_cast<VarExpr*>(part.get())->line = base;
                }
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

// Record (or, for an untyped redeclaration, clear) the declared type of each
// `$` variable a declarator introduces — see scalarDeclTypes_.
void Parser::noteScalarDecls(const Expr* e) {
    if (!e) return;
    if (e->kind == NK::ListExpr) {
        for (auto& it : static_cast<const ListExpr*>(e)->items) noteScalarDecls(it.get());
        return;
    }
    if (e->kind != NK::VarExpr) return;
    auto* ve = static_cast<const VarExpr*>(e);
    if (!ve->declare || ve->name.size() < 2 || ve->name[0] != '$') return;
    scalarDeclTypes_.back()[ve->name] = ve->declType;
}

// ---------------- statements ----------------
std::unique_ptr<Block> Parser::parseBlock() {
    expectKind(Tok::LBrace, "{");
    size_t opMark = opUndo_.size(); // user operators are lexically scoped
    monkeyScopes_.push_back(0);
    scalarDeclTypes_.emplace_back();
    const char savedVarsPragma = varsPragma_;   // `use variables` is block-scoped
    auto blk = std::make_unique<Block>();
    while (!isKind(Tok::RBrace) && !isKind(Tok::End)) {
        if (matchKind(Tok::Semicolon)) continue;
        blk->stmts.push_back(parseStatement());
        for (auto& ps : pendingStmts_) blk->stmts.push_back(std::move(ps)); // `will leave` desugars
        pendingStmts_.clear();
        if (!isKind(Tok::Semicolon)) enforceStmtSep();
    }
    checkRedeclarations(blk->stmts);
    monkeyScopes_.pop_back();
    scalarDeclTypes_.pop_back();
    varsPragma_ = savedVarsPragma;
    lastBlockClose_ = pos_; // this `}` closes a BLOCK — see the note on the field
    expectKind(Tok::RBrace, "}");
    opRollback(opMark);
    return blk;
}

// The tail of a sigilless capture parameter — `\p`, typed or not: an optional
// paren sub-signature (`-> \p (:key($k) is raw, …)` destructures while p stays
// bound to the whole, which is how JSON::Class's ClassHOW iterates its role
// registry) and the `is`/`where` traits (`\key is raw` is how the same module
// writes every DELETE-KEY/EXISTS-KEY signature). Both places that parse a
// sigilless parameter — bare and typed — need exactly this, and each used to
// carry its own copy.
void Parser::parseSigillessTail(Param& p) {
            // `-> \p (:key($k) is raw, :value(@v) is raw)` destructures the Pair
            // while p stays bound to the whole (JSON::Class's ClassHOW iterates
            // its role registry exactly this way)
            if (isKind(Tok::LParen) && cur().spaceBefore) {
                advance(); // '('
                p.subSig = std::make_shared<std::vector<Param>>(parseSignature(Tok::RParen));
                if (!matchKind(Tok::RParen)) error("expected ')' in sub-signature");
            }
            // traits on the sigilless param — `\key is raw` is how JSON::Class
            // writes every DELETE-KEY/EXISTS-KEY signature; without this the `is`
            // fell out of the parameter list ("expected ) (got 'is')")
            while (isIdent("is") || isIdent("where")) {
                std::string trait = advance().text;
                if (trait == "where") { p.whereExpr = parseExpr(BP_ASSIGN + 1); continue; }
                if (isIdent("rw") || isIdent("copy") || isIdent("raw")) {
                    p.isRw   = cur().text == "rw";
                    p.isCopy = cur().text == "copy";
                    p.isRaw  = cur().text == "raw";
                }
                if (isIdent("required")) p.required = true;
                if (isKind(Tok::Ident)) advance();
                if (isKind(Tok::LParen)) {
                    int d = 0;
                    do { if (isKind(Tok::LParen)) d++; else if (isKind(Tok::RParen)) d--; advance(); } while (d > 0 && !isKind(Tok::End));
                }
            }
}

// `:a(:$!x)` binds the ATTRIBUTE $!x directly, and the second name it answers
// to is the attribute's own — `x`. Every consumer derives that name as
// p.name.substr(1), which here yields the twigil-carrying `!x` and matches
// nothing, so record the bare name as an alias key instead (Statistics::
// Distributions' `submethod BUILD(:a(:$!shape) = 1, …)` needs `:shape` to bind).
// A bare sigil where a parameter variable is expected is an ANONYMOUS
// parameter: `sub f($, @)` positionally, and `:filter(&)` / `:h(:help($))`
// under an external name, which accepts the key and binds its value to
// nothing. Red declares an unused callable option exactly that way.
static bool anonSigilTok(const Token& t) {
    return t.kind == Tok::Op && t.text.size() == 1 &&
           (t.text[0] == '$' || t.text[0] == '@' || t.text[0] == '%' || t.text[0] == '&');
}

static void aliasAttrName(Param& p) {
    if (!p.aliasBoth || p.name.size() <= 2) return;
    if (p.name[1] != '!' && p.name[1] != '.') return;
    std::string bare = p.name.substr(2);
    if (bare != p.namedKey) p.aliasKeys.push_back(bare);  // `:x(:$!x)` names one key, not two
}

// `is NAME` / `is NAME(EXPR)` on a parameter, where NAME is none of the built-in
// flags (rw/copy/raw/required): a USER trait. Its argument is parsed as a real
// expression rather than skipped, because that is where the payload lives —
// `:$foo is option("=s%")` is how Getopt::Long declares an option's spec, and
// the interpreter hands both to a `trait_mod:<is>(Parameter, :option(…))` when
// the routine is declared. Mirrors the attribute-trait parse exactly.
void Parser::recordParamTrait(Param& p, const std::string& name) {
    if (isKind(Tok::LParen) && !cur().spaceBefore) {
        advance();
        ExprPtr arg = isKind(Tok::RParen) ? nullptr : parseExpression();
        while (!isKind(Tok::RParen) && !isKind(Tok::End)) advance();
        if (isKind(Tok::RParen)) advance();
        p.userTraits.emplace_back(name, std::move(arg));
        return;
    }
    p.userTraits.emplace_back(name, nullptr); // bare `is getopt`
}

// Is the `{` at the current token a HASH COMPOSER or a BLOCK? One rule for the
// term position (parsePrimary) and the statement position (parseStatementImpl):
// the statement copy used to lack the computed-key chain and the topic veto,
// so `sub f { { $d.uc => 1 } }` returned a Pair where a Hash was meant.
// Does an interpolating string interpolate the TOPIC? `{ "a$_" => 1 }` is a
// BLOCK on Rakudo, not a one-key Hash, and the token scan below never looked
// inside a string — so `.map({ "&$_" => %impl{$_} })`, which is what building
// an export map looks like, composed a Hash, mapped with it, and produced an
// EMPTY Seq. Silently: the module compiled and exported nothing.
//
// An embedded `{ … }` code block owns its own topic, so it is skipped whole
// rather than descended into: `{ "a{ @y.map({ $_ }) }" => 1 }` really is a
// Hash on Rakudo. Both halves measured against Rakudo v2026.08.
static bool strInterpUsesTopic(const std::string& s) {
    size_t i = 0;
    // a `\x02feats\x02` prefix records the quote's adverbs — not string body
    if (!s.empty() && s[0] == '\x02') {
        size_t e = s.find('\x02', 1);
        i = (e == std::string::npos) ? s.size() : e + 1;
    }
    auto identChar = [](unsigned char c) { return std::isalnum(c) || c == '_'; };
    for (; i < s.size(); i++) {
        const char c = s[i];
        if (c == '\\') { i++; continue; }   // `"\$_"` interpolates nothing
        if (c == '{') {                     // embedded code: its own topic
            int d = 1;
            while (++i < s.size() && d) {
                if (s[i] == '\\') { i++; continue; }
                if (s[i] == '{') d++;
                else if (s[i] == '}') d--;
            }
            i--;
            continue;
        }
        if (c != '$' && c != '@' && c != '%') continue;
        if (i + 1 < s.size() && s[i + 1] == '^') { // `$^a` placeholder
            if (i + 2 < s.size() && (std::isalpha((unsigned char)s[i + 2]) || s[i + 2] == '_'))
                return true;
            continue;
        }
        // `$_`, `@_`, `%_` — but `$_b` is a variable in its own right, so the
        // character after the underscore must not continue the identifier
        if (i + 1 < s.size() && s[i + 1] == '_' &&
            !(i + 2 < s.size() && identChar((unsigned char)s[i + 2])))
            return true;
    }
    return false;
}

bool Parser::braceLooksHash(bool emptyIsHash) {
    bool isHash = false;
    const Token& a = peek(1), &b = peek(2);
    if (a.kind == Tok::RBrace) isHash = emptyIsHash; // {}
    else if ((a.kind == Tok::Ident || a.kind == Tok::StrLit ||
              a.kind == Tok::StrInterp || a.kind == Tok::IntLit) &&
             b.kind == Tok::FatArrow)
        isHash = true;
    else if (a.kind == Tok::Op && a.text == ":" &&
             (b.kind == Tok::Ident || b.kind == Tok::IntLit || b.kind == Tok::Var ||
              (b.kind == Tok::Op && b.text == "!")) &&
             // `:16(...)` / `:16<...>` / `:256[...]` is a RADIX literal,
             // not a colon-pair — so `{ :16($_) }` / `{ :256[|@^a] }` is
             // a CODE block, not a hash
             !(b.kind == Tok::IntLit &&
               (peek(3).kind == Tok::LParen || peek(3).kind == Tok::LBracket ||
                (peek(3).kind == Tok::Op && peek(3).text == "<"))))
        isHash = true; // starts with a colon-pair: :name / :1n / :$v / :!flag
    else if (a.kind == Tok::Var && !a.text.empty() && a.text[0] == '%' &&
             (b.kind == Tok::RBrace || b.kind == Tok::Comma))
        isHash = true;
    // A key that is a COMPUTED term still composes a hash: `{ $d.name => 1 }`,
    // `{ %h<k> => 1 }`. Only a plain postfix chain counts — a term followed by
    // `.name`, a subscript or a call, and then the fat arrow. Anything else
    // (a listop, an operator, a second statement) leaves it a block.
    else if (a.kind == Tok::Var || a.kind == Tok::Ident) {
        // Strictly a POSTFIX chain: `.name`, `!name`, or a bracket group,
        // repeated, and then the arrow. Two terms in a row would be a
        // listop call instead — `{ dt month => 0 }` is a block calling
        // `dt`, not a hash — so the chain stops there.
        size_t li = pos_;
        size_t k = li + 2;                    // past `{` and the first term
        while (k < toks_.size()) {
            const Token& tk = toks_[k];
            if (tk.kind == Tok::FatArrow) { isHash = true; break; }
            if (tk.kind == Tok::Op && (tk.text == "." || tk.text == "!")) {
                if (k + 1 >= toks_.size()) break;
                Tok nk = toks_[k + 1].kind;
                if (nk != Tok::Ident && nk != Tok::Var) break;
                k += 2;
                continue;
            }
            if (tk.kind == Tok::LParen || tk.kind == Tok::LBracket ||
                (tk.kind == Tok::Op && tk.text == "<")) {
                int d2 = 0;
                size_t j = k;
                for (; j < toks_.size(); j++) {
                    Tok kk = toks_[j].kind;
                    bool open = kk == Tok::LParen || kk == Tok::LBracket ||
                                (kk == Tok::Op && toks_[j].text == "<");
                    bool close = kk == Tok::RParen || kk == Tok::RBracket ||
                                 (kk == Tok::Op && toks_[j].text == ">");
                    if (kk == Tok::End) break;
                    if (open) d2++;
                    else if (close && --d2 == 0) { j++; break; }
                }
                if (j <= k) break;
                k = j;
                continue;
            }
            break;
        }
    }
    // …but a composer that USES THE TOPIC is a block after all: `{3 => 4, :b}`
    // is a Hash while `{3 => 4, :b($_)}` and `{3 => 4, :b(.Num)}` are Blocks.
    // Anything that reads `$_`, a placeholder parameter, or calls a method on
    // the topic (a `.` in TERM position) can only be code.
    if (isHash) {
        size_t li = pos_;
        int depth = 0;
        for (size_t k = li; k < toks_.size(); k++) {
            const Token& tk = toks_[k];
            if (tk.kind == Tok::LBrace) { depth++; continue; }
            if (tk.kind == Tok::RBrace) { if (--depth == 0) break; continue; }
            if (tk.kind == Tok::End) break;
            // only THIS composer's own level: a nested block owns its topic,
            // so `{ :out{ .contains: … } }` is still a Hash of one Block
            if (depth > 1) continue;
            // A `;` with something other than the closing brace after it is a
            // second STATEMENT, and statements are code: Rakudo composes a Hash
            // from `{ :a(1), :b(2); }` and a Block from `{ :a(1); :b(2) }`.
            if (tk.kind == Tok::Semicolon) {
                size_t n = k + 1;
                while (n < toks_.size() && toks_[n].kind == Tok::Semicolon) n++;
                if (n < toks_.size() && toks_[n].kind != Tok::RBrace) { isHash = false; break; }
            }
            // `@_`/`%_` are implicit parameters exactly as `$_` is, so a
            // composer mentioning one is a block too: `.map: { @_[0] =>
            // @_[1] }` builds a Pair per element, and reading it as a Hash
            // literal made the whole map produce nothing.
            // …and the `:` twigil is a placeholder exactly as `^` is — it names
            // the same parameter, only as a NAMED one: `{ :$:f }` is the block
            // `-> :$f { f => $f }`, not a Hash. A NAME has to follow the twigil:
            // the `:` in `&:<+>` opens an operator name instead.
            if (tk.kind == Tok::Var &&
                (tk.text == "$_" || tk.text == "@_" || tk.text == "%_" ||
                 (tk.text.size() > 2 &&
                  (tk.text[1] == '^' ||
                   (tk.text[1] == ':' &&
                    (ascii::isalpha((unsigned char)tk.text[2]) || tk.text[2] == '_')))))) { isHash = false; break; }
            // …and the same variables reached through a STRING INTERPOLATION,
            // which is a token the scan cannot see into from the outside
            if (tk.kind == Tok::StrInterp && strInterpUsesTopic(tk.text)) { isHash = false; break; }
            // `.method` with no invocant before it — the previous token cannot
            // end a term, so the dot's invocant is the topic
            if (tk.kind == Tok::Op && tk.text == "." && k > li) {
                const Token& pv = toks_[k - 1];
                bool termBefore = pv.kind == Tok::Var || pv.kind == Tok::Ident ||
                                  pv.kind == Tok::IntLit || pv.kind == Tok::NumLit ||
                                  pv.kind == Tok::StrLit || pv.kind == Tok::StrInterp ||
                                  pv.kind == Tok::RParen || pv.kind == Tok::RBracket ||
                                  pv.kind == Tok::RBrace ||
                                  // …and the `>` closing an ANGLE SUBSCRIPT, which ends a
                                  // term just as `]` does: `%h<k>.Int` and `$<cap>.ast` are
                                  // method calls on the subscript, not on the topic. Missing
                                  // it made a hash composer containing one parse as a BLOCK
                                  // — Cro::Uri's `{ authority => …, $<host>.ast }` came back
                                  // a Block, so `.<host>` on it failed and no URI parsed.
                                  (pv.kind == Tok::Op && pv.text == ">") ||
                                  // …and a HYPER marker: the dot in `>>.trim` calls on the
                                  // term before the marker, not on the topic — HTTP::Header's
                                  // `make { …, content => $x.split(',')>>.trim }` composer
                                  (pv.kind == Tok::Op &&
                                   (pv.text == ">>" || pv.text == "<<" ||
                                    pv.text == "\xC2\xBB" || pv.text == "\xC2\xAB")) ||
                                  // …and a WHATEVER glued to the dot: `:status(*.so)` is a
                                  // WhateverCode, not a call on the topic
                                  (pv.kind == Tok::Op && pv.text == "*" && !tk.spaceBefore &&
                                   k >= 2 && (toks_[k - 2].kind == Tok::LParen || toks_[k - 2].kind == Tok::Comma ||
                                              toks_[k - 2].kind == Tok::FatArrow));
                if (!termBefore) { isHash = false; break; }
            }
        }
    }
    return isHash;
}

std::vector<Param> Parser::parseSignature(Tok closeTok) {
    std::vector<Param> params;
    std::vector<std::pair<size_t, int>> podClaims; // (param index, `#=` line) — resolved at close
    // The trait ladder after a parameter — `where … is rw is copy is raw is required
    // is encoded('utf8') returns … of …`. One copy: the named-alias path
    // (`:name($n) is required`) used to carry its own, without `is required`.
    auto parseParamTraits = [&](Param& p) {
        while (isIdent("where") || isIdent("is") || isIdent("returns") || isIdent("of")) {
            std::string trait = advance().text;
            if (trait == "where") p.whereExpr = parseExpr(BP_ASSIGN + 1); // stop before the `= default`
            else if (!isKind(Tok::Comma) && !isKind(Tok::RParen) && !isKind(Tok::End) && !isOp("=")) {
                bool builtinTrait = false;
                if (trait == "is" && (isIdent("rw") || isIdent("copy") || isIdent("raw"))) {
                    p.isRw   = cur().text == "rw";
                    p.isCopy = cur().text == "copy";
                    p.isRaw  = cur().text == "raw";
                    builtinTrait = true;
                }
                // `is required` — same meaning as the `!` marker ($*USAGE
                // prints such a named param without brackets; issue #17)
                if (trait == "is" && isIdent("required")) { p.required = true; builtinTrait = true; }
                std::string traitName = isKind(Tok::Ident) ? cur().text : std::string();
                advance(); // the trait word (rw/copy/encoded/…)
                if (trait == "is" && !builtinTrait && !traitName.empty())
                    recordParamTrait(p, traitName);
                // a parenthesised trait argument: `is encoded('utf8')` — skip it
                else if (isKind(Tok::LParen)) {
                    int d = 0;
                    do { if (isKind(Tok::LParen)) d++; else if (isKind(Tok::RParen)) d--; advance(); } while (d > 0 && !isKind(Tok::End));
                }
            }
        }
    };
    bool pastDoubleSemi = false;   // `($a;; $b)` — what follows takes no part in multi dispatch
    while (!isKind(closeTok) && !isKind(Tok::End)) {
        // a `;` HERE is the second half of `;;` — the parameter before it
        // already consumed the first as its separator
        if (isKind(Tok::Semicolon)) {
            advance();
            pastDoubleSemi = true;
            continue;
        }
        Param p;
        p.pastDoubleSemi = pastDoubleSemi;
        const int paramLine = cur().line; // where THIS parameter starts — its `#|` sits above it
        // return-type constraint `--> Type` — always last; discarded. Skip to the
        // end of the signature so smileys (IO::Path:D) and parametrised types
        // (Positional[Int], (Int, Str)) don't trip the `)`-expectation.
        if (matchOp("-->")) {
            if (isKind(Tok::Ident) && (cur().text == "True" || cur().text == "False" || cur().text == "Nil" ||
                                          cur().text == "Empty"))
                sigRetLiteral_ = parsePrimary(); // `--> True` : a literal Bool/Nil return value
            else if (isKind(Tok::Ident)) {
                sigRetType_ = cur().text + nativeRetParam(pos_) + retCoercionMark(peek()); // remember the return type
                // `--> Int Str` — a second type after the return constraint
                if (peek().kind == Tok::Ident && knownTypeName(peek().text))
                    throw ParseError("Malformed return value (return constraints only allowed "
                                     "at the end of the signature)", cur().line,
                                     "X::Syntax::Malformed", {{"what", "return value"}});
            }
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
        // …and a NEGATIVE one, `multi m(-1)`: the sign and the number together
        if ((isOp("-") || isOp("+") || isOp("\xE2\x88\x92")) &&
            (peek().kind == Tok::IntLit || peek().kind == Tok::NumLit) && !peek().spaceBefore) {
            p.litVal = parsePrefix(true);
            params.push_back(std::move(p));
            if (!matchKind(Tok::Comma) && !matchKind(Tok::Semicolon)) break;
            continue;
        }
        if (isKind(Tok::StrLit) || isKind(Tok::StrInterp) || isKind(Tok::IntLit) || isKind(Tok::NumLit)) {
            p.litVal = parsePrimary();
            params.push_back(std::move(p));
            if (!matchKind(Tok::Comma) && !matchKind(Tok::Semicolon)) break;   // `(0;; uint32)`
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
            if ((isKind(Tok::Ident) && !isIdent("where") && !isIdent("is") && !isIdent("of")) || isKind(Tok::Var)) {
                p.name = advance().text;
                // the capture name is a sigilless TERM in the body: `rest<key>`
                // must subscript it, not become a listop call (HTTP::Tiny's
                // `|rest` + `rest<data-callback>:exists`)
                if (!p.name.empty() && p.name[0] != '$' && p.name[0] != '@' && p.name[0] != '%')
                    sigilless_.insert(p.name);
            }
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
            // paren sub-signature, same as on a sigilled param below:
            parseSigillessTail(p);
            // `method finalize(\SELF:)` — the bare colon marks a SIGILLESS
            // invocant, the same marker a sigilled `$self:` carries. FINALIZER
            // declares its whole API this way and the signature would not parse.
            if (isOp(":")) {
                advance();
                p.invocant = true;
                params.push_back(std::move(p));
                continue;
            }
            if (matchOp("=")) p.defaultVal = parseExpr(BP_ASSIGN);
            params.push_back(std::move(p));
            if (!matchKind(Tok::Comma)) break;
            continue;
        }
        bool named = matchOp(":");
        // `-> :($a, $b) { … }` — a signature LITERAL in parameter position, and
        // the colon has just been eaten by the line above, which is why this
        // has to sit here rather than beside the other parameter shapes.
        // Rakudo reads it as `:(:$ ($a, $b))`: a NAMED parameter with no name
        // whose target destructures with that signature. What makes it matter
        // beyond parsing is that the sub-signature's variables are the ones the
        // BODY refers to — PrettyDump writes every handler this way, and
        // RakuDoc::Render reaches it through RakuDoc::Processed.
        if (named && isKind(Tok::LParen) && !cur().spaceBefore) {
            advance();                  // '('
            p.subSig = std::make_shared<std::vector<Param>>(parseSignature(Tok::RParen));
            if (!matchKind(Tok::RParen)) error("expected ')' in signature-literal parameter");
            p.name = ""; p.sigil = '$'; p.named = true;
            if (matchOp("=")) p.defaultVal = parseExpr(BP_ASSIGN);
            params.push_back(std::move(p));
            if (!matchKind(Tok::Comma) && !matchKind(Tok::Semicolon)) break;
            continue;
        }
        // named alias:  :name($var)  — external key `name`, binds `$var` (optional inner type)
        if (named && isKind(Tok::Ident) && peek().kind == Tok::LParen) {
            p.namedKey = advance().text;
            advance(); // (
            // nested sub-signature:  :value((Str :key($d), …))  — and the SQUARE
            // spelling, which destructures a Positional the same way:
            // `:out([$out?, :pass($p) = True])` is how Test::Run declares a
            // three-part named argument. Only the paren form was accepted, so
            // the bracket one died at "expected variable in named-parameter
            // alias".
            if (isKind(Tok::LParen) || isKind(Tok::LBracket)) {
                Tok close = isKind(Tok::LBracket) ? Tok::RBracket : Tok::RParen;
                advance();
                p.subSig = std::make_shared<std::vector<Param>>(parseSignature(close));
                if (!matchKind(close)) error("expected closing bracket in nested sub-signature");
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
            else if (anonSigilTok(cur())) { p.sigil = cur().text[0]; p.name = ""; advance(); }
            else error("expected variable in named-parameter alias");
            aliasAttrName(p);
            // `:in(:$in)` — the alias and the variable name collide
            if (p.aliasBoth && p.name.size() > 1 && p.namedKey == p.name.substr(1))
                throw ParseError("Name " + p.namedKey + " used for more than one named parameter",
                                 cur().line, "X::Signature::NameClash", {{"name", p.namedKey}});
            p.named = true;
            for (; aliasDepth > 0; aliasDepth--)
                if (!matchKind(Tok::RParen)) error("expected ')' in named-parameter alias");
            if (!matchKind(Tok::RParen)) error("expected ')' in named-parameter alias");
            // `List :size($ss)(Int $sw, Int $sh)` — a SUB-SIGNATURE written
            // directly against the alias's closing paren. The spaced spelling
            // (`:size($ss) ($sw, $sh)`) already parsed; this one did not, and the
            // error pointed at the second `(` with "expected )". Imlib2 destructures
            // every one of its geometry arguments this way.
            if (isKind(Tok::LParen)) {
                advance();
                p.subSig = std::make_shared<std::vector<Param>>(parseSignature(Tok::RParen));
                if (!matchKind(Tok::RParen)) error("expected ')' in named-parameter sub-signature");
            }
            if (matchOp("?")) p.optional = true;
            else if (matchOp("!")) p.required = true;
            // `:size($ss)!(Int $sw, Int $sh)` — the marker may sit between the
            // alias and its sub-signature, so look again once it is consumed
            if (!p.subSig && isKind(Tok::LParen)) {
                advance();
                p.subSig = std::make_shared<std::vector<Param>>(parseSignature(Tok::RParen));
                if (!matchKind(Tok::RParen)) error("expected ')' in named-parameter sub-signature");
            }
            parseParamTraits(p); // the same ladder as a plain parameter's (`is required`, `is encoded(…)`)
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
            // colon ends it. The param IS pushed — dropping it here threw away the
            // :D/:U smiley just parsed into it, so `multi method g(::?CLASS:U:)`
            // and `(::?CLASS:D:)` were indistinguishable and the first declared
            // candidate won for every invocant (JSON::Class splits its type-object
            // and instance behaviour on exactly this).
            //
            // …but a colon GLUED to a variable opens a NAMED parameter of this
            // type — `::?CLASS :$copy` — and taking that for the invocant marker
            // made `$copy` a required POSITIONAL: CSS::Properties' TWEAK then
            // refused every `.new`, and with another named ahead of it the
            // signature would not even parse. The invocant colon never has a
            // variable tight behind it.
            if (isOp(":") && !(peek().kind == Tok::Var && !peek().spaceBefore)) {
                advance();
                p.invocant = true; p.type = "Mu"; p.name = ""; p.sigil = '$';
                params.push_back(std::move(p));
                continue;
            }
            // no invocant colon: `(::?CLASS:U)` is an anonymous POSITIONAL of the
            // enclosing type (verified against Rakudo — its signature reads
            // `(D $:: D:U, *%_)`), so it is CONSTRAINED to that type. Left as Mu
            // it bound anything, and `multi method COERCE(::?CLASS:D $_) { $_ }`
            // — the "already one of these" arm every PDF::COS class opens with —
            // swallowed the Str its sibling was written to convert.
            // Inside a ROLE the name means the CONSUMING class, which is not
            // known here; that case keeps binding anything, as before.
            p.type = (typeStack_.empty() ||
                      (!typeIsRole_.empty() && typeIsRole_.back()))
                   ? std::string("Mu") : typeStack_.back();
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
            // A SMILEY decides which of the two this is, and the difference is
            // visible in the body. `::T $x` is a type CAPTURE: it binds T to
            // whatever came in, and that binding shadows an outer T. `::T:U $x`
            // is an ordinary type CONSTRAINT wearing a definedness smiley — it
            // captures nothing, so an outer T stays visible. Rakudo draws the
            // line here too, and rejects `::T:U` outright when no T exists.
            // YAMLish leans on it: `load-yaml(::Grammar:U :$schema)` goes on to
            // call `Grammar.parse`, meaning the module's own grammar. Capture
            // that name and the call reaches $schema's type instead, no actions
            // are attached, and every document parses to its own source text.
            if (isOp(":") && peek().kind == Tok::Ident &&
                (peek().text == "D" || peek().text == "U" || peek().text == "_")) {
                advance(); std::string sm = advance().text;
                if (sm == "D") p.defConstraint = 1; else if (sm == "U") p.defConstraint = 2;
            }
            else {
                p.typeCapture = true;
                p.captureName = p.type;   // a following constraint overwrites `type`, never this
                // a type capture DECLARES its name for the unit: `::T $x` makes a
                // later bare `T` a legitimate (captured) type, not an undeclared one
                declTypeNames_.insert(p.type);
            }
        }
        // optional type constraint: a bare Ident (possibly Foo::Bar, with :D/:U smiley, [..])
        if (isKind(Tok::Ident)) {
            p.type = advance().text; // type name (used for multi-dispatch)
            // …and the smiley is GLUED to it. Spaced, the colon opens a named
            // parameter instead, which is how PDF::IO::Crypt asks for the PDF
            // encryption dictionary's one-letter keys: `Str :U($user-pass)!`
            // was read as `Str:U` plus a positional and bound nothing at all.
            if (isOp(":") && !cur().spaceBefore && peek().kind == Tok::Ident &&
                (peek().text == "D" || peek().text == "U" || peek().text == "_")) { // :D/:U/:_ smiley
                advance();
                std::string sm = advance().text;
                if (sm == "D") p.defConstraint = 1;
                else if (sm == "U") p.defConstraint = 2;
            }
            if (isKind(Tok::LBracket) && !cur().spaceBefore) {
                // A NativeCall type keeps its parameter: `CArray[uint8]` names an
                // element width the marshalling needs — the callback signature
                // `&cb (CArray[uint8], uint8 …)` of IO::Socket::Async::SSL's ALPN
                // selector read its bytes eight at a time without it. Other
                // parameterised types stay bare here, as they always were.
                bool keep = p.type == "CArray" || p.type == "Pointer";
                const bool natArr = p.type == "array";
                std::string inner;
                int depth = 0;
                do {
                    if (isKind(Tok::LBracket)) depth++; else if (isKind(Tok::RBracket)) depth--;
                    if (keep || natArr) inner += cur().text;
                    advance();
                }
                while (depth > 0 && !isKind(Tok::End));
                if (keep) p.type += inner;
                // `array[Int]` — a native array takes a native element type only;
                // Rakudo finds out composing the type, at BEGIN time
                if (natArr && inner.size() > 2 && ascii::isupper((unsigned char)inner[1]))
                    throw ParseError("An exception occurred while parameterizing array: Can only parameterize array "
                                     "with a native type, not " + inner.substr(1, inner.size() - 2),
                                     cur().line, "X::Comp::BeginTime", {{"use-case", "parameterizing array"}});
            }
            // `R1 of Int $x` — the OF form of a parameterised type: the outer
            // type is what the parameter is checked against here
            while (isIdent("of") && peek().kind == Tok::Ident &&
                   (peek(2).kind == Tok::Var || peek(2).kind == Tok::Ident ||
                    peek(2).kind == Tok::RParen || peek(2).kind == Tok::Comma ||
                    (peek(2).kind == Tok::Op && peek(2).text == "\\"))) {
                // `P of Int` where P is a package, module or plain class: it
                // takes no type parameter
                auto dit = declClassDecls_.find(p.type);
                if (dit != declClassDecls_.end() && dit->second) {
                    const ClassDecl* pc = dit->second;
                    bool parametric = pc->isRole || pc->isGrammar || !pc->parent.empty() ||
                                      !pc->extraParents.empty() || !pc->roles.empty();
                    for (auto& m : pc->methods) if (m && m->name == "^parameterize") parametric = true;
                    if (pc->isPackage || !parametric)
                        throw ParseError(p.type + " cannot be parameterized", cur().line,
                                         "X::NotParametric", {{"type", p.type}});
                }
                advance(); advance();
            }
            // coercion type Str(Cool)  OR  destructuring sub-signature  Pair ( :key($k), … )
            // Coercion is the tight single-ident form; anything else is a sub-signature.
            if (isKind(Tok::LParen)) {
                bool coercion = !cur().spaceBefore &&
                                ((peek().kind == Tok::Ident && peek(2).kind == Tok::RParen) ||
                                 peek().kind == Tok::RParen); // `Foo()` — coerce from Any
                if (coercion) {
                    p.coerce = true;
                    advance(); // (
                    if (isKind(Tok::Ident)) p.coerceFrom = advance().text; // the from-type; "" for `Foo()`
                    advance(); // )
                }
                else {
                    advance(); // (
                    p.subSig = std::make_shared<std::vector<Param>>(parseSignature(Tok::RParen));
                    if (!matchKind(Tok::RParen)) error("expected ')' in sub-signature");
                }
            }
            // `(Int Str $x)` — a second prefix type is refused, not read as a
            // stray term that then died "expected )"
            if (isKind(Tok::Ident) && knownTypeName(cur().text) &&
                (peek().kind == Tok::Var || (peek().kind == Tok::Op && peek().text == "\\")))
                throw ParseError("A parameter may only have one prefix type constraint",
                                 cur().line, "X::Parameter::MultipleTypeConstraints",
                                 {{"parameter", peek().text}});
            // …and a type capture AFTER the constraint: `Response ::RESPONSE = …`
            // constrains the parameter AND names whatever type actually arrived.
            // The capture branch above only fires when `::` OPENS the parameter,
            // so the constrained spelling died at "expected ) (got '::')". Net::HTTP
            // declares both GET and POST that way. typeCapture stays FALSE here —
            // it means "this parameter has no real type", and this one does: Rakudo
            // refuses `f("s")` for `f(Int ::T $x)` at compile time.
            if (isOp("::") && peek().kind == Tok::Ident) {
                advance();
                p.captureName = advance().text;
                declTypeNames_.insert(p.captureName);
                if (isOp(":") && !cur().spaceBefore && peek().kind == Tok::Ident &&
                    (peek().text == "D" || peek().text == "U" || peek().text == "_")) {
                    advance(); std::string sm = advance().text;
                    if (sm == "D") p.defConstraint = 1; else if (sm == "U") p.defConstraint = 2;
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
                // nested alias layers after a type: `Bool :h(:help($))` — every key
                // answers (zef's MAIN uses this). Mirrors the untyped path above.
                int aliasDepth = 0;
                while (isOp(":") && peek().kind == Tok::Ident && peek(2).kind == Tok::LParen) {
                    advance(); // :
                    p.aliasKeys.push_back(advance().text);
                    advance(); // (
                    aliasDepth++;
                }
                p.aliasBoth = matchOp(":"); // :name(:$var) answers BOTH names
                if (isKind(Tok::Var)) { p.name = cur().text; p.sigil = cur().text[0]; advance(); }
                else if (anonSigilTok(cur())) { p.sigil = cur().text[0]; p.name = ""; advance(); }
                else error("expected variable in named-parameter alias");
                aliasAttrName(p);
                for (; aliasDepth > 0; aliasDepth--)
                    if (!matchKind(Tok::RParen)) error("expected ')' in named-parameter alias");
            }
            if (!matchKind(Tok::RParen)) error("expected ')' in named-parameter alias");
            // …and the SUB-SIGNATURE that may follow it with no space between,
            // which is the typed half of the same shape handled on the untyped
            // path above: `List :size($ss)(Int $sw, Int $sh)`. Imlib2 writes
            // every geometry argument that way and could not be compiled at all.
            if (!p.subSig && isKind(Tok::LParen)) {
                advance();
                p.subSig = std::make_shared<std::vector<Param>>(parseSignature(Tok::RParen));
                if (!matchKind(Tok::RParen)) error("expected ')' in named-parameter sub-signature");
            }
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
            // paren sub-signature, same as on a sigilled param below:
            parseSigillessTail(p);
        } else if (isKind(Tok::Var)) {
            // `sub f($0)` — numeric names can't be parameters either
            if (cur().text.size() > 1 && ascii::isdigit((unsigned char)cur().text[1]))
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
            // `($bar :D)` — a smiley belongs on the TYPE, never after the name
            if (isOp(":") && peek().kind == Tok::Ident &&
                (peek().text == "D" || peek().text == "U" || peek().text == "_") &&
                (peek(2).kind == Tok::RParen || peek(2).kind == Tok::Comma))
                throw ParseError("Invalid typename '" + peek().text + "' in parameter declaration.",
                                 cur().line, "X::Parameter::InvalidType",
                                 {{"typename", peek().text}});
        } else if (anonSigilTok(cur())) {
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
        //
        // The space is what tells a sub-signature from a coercion or a callable's
        // own signature, and a BARE named variable keeps that rule: Rakudo
        // rejects `List :$c($x, $y)` outright. It is only once a `?`/`!` marker
        // has settled the parameter that the tight spelling becomes unambiguous
        // — see the second check, below the markers.
        if (!p.subSig && isKind(Tok::LParen) && cur().spaceBefore) {
            advance(); // '('
            p.subSig = std::make_shared<std::vector<Param>>(parseSignature(Tok::RParen));
            if (!matchKind(Tok::RParen)) error("expected ')' in sub-signature");
        }
        // shaped-array parameter:  @a[3] / @a[4,*] / @a[3;3] — the argument must
        // be declared with exactly that shape (`*` takes any size of its dimension)
        if (isKind(Tok::LBracket) && !cur().spaceBefore) {
            advance(); // [
            while (!isKind(Tok::RBracket) && !isKind(Tok::End)) {
                if (isOp("*")) { advance(); p.shapeDims.push_back(-1); }
                else if (isKind(Tok::IntLit) &&
                         (peek().kind == Tok::Comma || peek().kind == Tok::Semicolon ||
                          peek().kind == Tok::RBracket)) {
                    p.shapeDims.push_back(std::stoll(advance().text));
                }
                else {   // an expression dimension: parsed, not checked
                    int depth = 0;
                    while (!isKind(Tok::End) && !(depth == 0 && (isKind(Tok::Comma) || isKind(Tok::Semicolon) ||
                                                                 isKind(Tok::RBracket)))) {
                        if (isKind(Tok::LBracket) || isKind(Tok::LParen)) depth++;
                        else if (isKind(Tok::RBracket) || isKind(Tok::RParen)) depth--;
                        advance();
                    }
                    p.shapeDims.push_back(-2);
                }
                if (!matchKind(Tok::Comma)) matchKind(Tok::Semicolon);
            }
            matchKind(Tok::RBracket);
            if (p.name.empty() && p.sigil == '@') p.name = "@";   // anonymous: still bound, so it can be checked
        }
        if (matchOp("?")) p.optional = true;
        else if (matchOp("!")) p.required = true;
        // …and the sub-signature may sit AFTER the `?`/`!` marker:
        // `:@specification! (Optionality $o, Version $v)` — the check above runs
        // before the marker is consumed, so a required named parameter with one
        // never matched and the `(` fell through as a syntax error. META6 declares
        // its trait_mod that way, which took Test::META (and any suite that uses
        // it) down at parse time.
        // …and once a marker has been consumed the paren is unambiguous, so the
        // TIGHT spelling counts here: `List :$source!($x, $y)` is what Imlib2
        // writes, and Rakudo takes `:$c!(…)` and `:$c?(…)` while rejecting the
        // unmarked `:$c(…)`. `&` keeps the space rule either way — `&cb:(Int)`
        // is the callable's own signature, a different thing.
        if (!p.subSig && isKind(Tok::LParen) &&
            (cur().spaceBefore || ((p.optional || p.required) && p.named && p.sigil != '&'))) {
            advance(); // '('
            p.subSig = std::make_shared<std::vector<Param>>(parseSignature(Tok::RParen));
            if (!matchKind(Tok::RParen)) error("expected ')' in sub-signature");
        }
        // invocant marker:  method m ($self: $arg)  — ':' separates invocant from rest
        if (isOp(":")) {
            // the invocant marker belongs to the FIRST parameter only
            if (!params.empty())
                throw ParseError("Can only use : as invocant marker in a signature after the first parameter",
                                 cur().line, "X::Syntax::Signature::InvocantMarker", {});
            advance(); p.invocant = true; params.push_back(std::move(p)); continue;
        }
        parseParamTraits(p); // where / is / returns / of trait clauses
        // …and a destructuring sub-signature may sit after them too:
        // `@metas is copy [$, *@] = $.package-list` (App::ecogen). The check
        // above runs before the trait ladder, so the trait-first spelling died
        // at "expected ) (got '['".
        if (!p.subSig && isKind(Tok::LBracket) && cur().spaceBefore) {
            advance();
            p.subSig = std::make_shared<std::vector<Param>>(parseSignature(Tok::RBracket));
            if (!matchKind(Tok::RBracket)) error("expected ']' in sub-signature");
        }
        // …the invocant marker may also sit AFTER the traits:
        // `(::?CLASS:U $_ is rw: **@values)` — BinaryHeap's writable
        // class-invocant form. The check above runs before the trait loop, so
        // this spelling used to die "expected ) (got ':')".
        if (isOp(":")) { advance(); p.invocant = true; params.push_back(std::move(p)); continue; }
        if (matchOp("=")) {
            p.defaultVal = parseExpr(BP_ASSIGN);
            // a trait or constraint AFTER the default is out of place
            if (isIdent("is") || isIdent("where"))
                throw ParseError("Cannot put " + std::string(isIdent("is") ? "trait" : "post constraint") +
                                 " on parameter " + p.name + " after its default value", cur().line,
                                 "X::Parameter::AfterDefault",
                                 {{"type", isIdent("is") ? "trait" : "post constraint"},
                                  {"modifier", cur().text}});
        }
        // `is rw` cannot combine with a default value (X::Trait::Invalid):
        // an rw param must bind a writable container, a default is a fresh value
        if (p.isRw && p.defaultVal)
            error("Cannot use trait 'is rw' on a parameter with a default value");
        { // trailing `#= description` on this param's line — DEFERRED. Whether
          // the doc belongs to the param or to the ROUTINE depends on where the
          // signature closes: a comment runs to end of line, so a `#=` sharing
          // a line with the closing `)` necessarily sits after it, and Rakudo
          // gives that one to the routine (`sub MAIN(Int $x) {}  #= doc` shows
          // `-- doc` on the usage line and NO option entry). Recorded here,
          // resolved below once the closing line is known (issue #17).
            auto di = declPod_.find(cur().line);
            if (di == declPod_.end() && pos_ > 0) di = declPod_.find(toks_[pos_ - 1].line);
            if (di != declPod_.end()) podClaims.push_back({params.size(), di->first});
            // A `#|` above the param — but not the ROUTINE's own, which on a
            // one-line signature is "above" the parameters too. (A resolved
            // `#=` claim overrides this below — the old precedence.)
            // Asked of the parameter's OWN line, not of wherever the parse has
            // reached: after the LAST parameter that is the closing `)`, a line
            // below its doc, so the last one never found its `#|`.
            if (paramLine > sigOwnerLine_) p.pod = leadingPodFor(paramLine);
        }
        params.push_back(std::move(p));
        if (matchOp("-->")) { // return type — remember the name; skip the rest to end of signature
            if (isKind(Tok::Ident) && (cur().text == "True" || cur().text == "False" || cur().text == "Nil" ||
                                          cur().text == "Empty"))
                sigRetLiteral_ = parsePrimary(); // `--> True` : a literal Bool/Nil return value
            else if (isKind(Tok::Ident)) sigRetType_ = cur().text + nativeRetParam(pos_) + retCoercionMark(peek());
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
        if (isKind(Tok::Ident) && (cur().text == "True" || cur().text == "False" || cur().text == "Nil" ||
                                          cur().text == "Empty"))
                sigRetLiteral_ = parsePrimary(); // `--> True` : a literal Bool/Nil return value
            else if (isKind(Tok::Ident)) sigRetType_ = cur().text + nativeRetParam(pos_) + retCoercionMark(peek());
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
    { // resolve the deferred `#=` claims: a doc line strictly INSIDE the
      // signature belongs to its parameter (and is marked claimed, so the
      // routine's trailing-doc probe skips it); one on the closing line is
      // left for the routine to pick up.
        int closeLine = isKind(Tok::End) ? 0x7fffffff : cur().line;
        for (auto& cl : podClaims)
            if (cl.second < closeLine) {
                params[cl.first].pod = declPod_.at(cl.second);
                claimedPodLines_.insert(cl.second);
            }
    }
    return params;
}

std::vector<Param> Parser::parsePointyParams() {
    // pointy blocks share the full signature grammar (types, coercions, slurpies,
    // named params, destructuring, traits) — the body brace ends the signature
    return parseSignature(Tok::LBrace);
}

StmtPtr Parser::parseSub(bool isMulti, bool isProto, bool asMethod) {
    // 'sub' already consumed by caller
    auto s = std::make_unique<SubDecl>();
    int subDeclLine = pos_ > 0 ? toks_[pos_ - 1].line : cur().line;
    s->pod = leadingPodFor(subDeclLine); // `#|` above the decl
    s->isMulti = isMulti;
    s->isProto = isProto;
    std::string declInfix; // set when this is an `infix:<…>` declaration (for precedence traits)
    if (isOp("!")) { advance(); s->isPrivate = true; } // private method `method !name` — self!name only
    // `method ^parameterize(…)` — a META-METHOD: it lives on the type's HOW and is
    // reached as `T.^parameterize`, which is also what `T[…]` calls. The caret was
    // dropped, so the declaration became an anonymous method and `Ultimate[42]`
    // silently took the builtin parameterization (Parameterizable).
    if (isOp("^") && !peek().spaceBefore && peek().kind == Tok::Ident) {
        advance(); s->name = "^" + advance().text;
    }
    else if (isKind(Tok::Ident)) {
        s->name = advance().text;
        // `sub div` / `sub min` in THIS file: the name is now a routine, so at
        // term position it is a call and not the operator (startsListopArg).
        if (!asMethod && isWordInfixName(s->name)) wordInfixSubs_.insert(s->name);
        // …and `sub if` / `sub has` / `sub loop`: the name is now a routine too,
        // so a paren glued to it is that routine's call rather than the keyword.
        if (!asMethod && kNeedsEndKeyword.count(s->name)) kwNamedSubs_.insert(s->name);
    }
    else if (isKind(Tok::Var)) s->name = advance().text; // &-name
    else if (isOp("::") && peek().kind == Tok::LParen) {
        // INDIRECT name: `sub ::(EXPR) (…) {…}` / `method ::('name') {…}` —
        // the name is computed when the declaration RUNS (declarations are
        // evaluated at run time here, so this is an expression slot, not a
        // compile-time contortion)
        advance(); advance();               // :: (
        s->nameExpr = parseExpression();
        expectKind(Tok::RParen, "expected ')' after ::( name expression");
    }
    // trait handler declaration: sub trait_mod:<is>(…) — name keeps the angle form
    // an unknown extension category (`sub twigil:<@>`) cannot be added
    static const std::set<std::string> kCats = {
        "infix", "prefix", "postfix", "circumfix", "postcircumfix", "trait_mod", "term"};
    // A METHOD may carry a CATEGORICAL name — `method dispatch:<.?>` (the
    // metamodel's dispatch hooks) and, the general case, the action method for
    // a `proto rule mod {*}` alternative: `multi rule mod:<null>` in the
    // grammar is answered by `method mod:<null>` in the actions class, which is
    // how Red reads a CREATE TABLE statement. Rakudo refuses an unknown
    // category on a SUB and accepts it on a method, so keep the name whole.
    if (asMethod && !s->name.empty() && !kCats.count(s->name) &&
        isOp(":") && !peek().spaceBefore &&
        peek().kind == Tok::Op && peek().text == "<") {
        advance(); advance(); // : <
        std::vector<std::string> w = readAngleWords(">");
        s->name += ":<" + (w.empty() ? std::string() : w[0]) + ">";
    }
    else if (!s->name.empty() && !kCats.count(s->name) && isOp(":") &&
        !peek().spaceBefore &&
        peek().kind == Tok::Op && (peek().text == "<" || peek().text == "<<"))
        throw ParseError("Cannot add tokens of category '" + s->name + "'",
                         cur().line, "X::Syntax::Extension::Category",
                         {{"category", s->name}});
    // `sub term:<name>` DEFINES A TERM: `name` is then written bare, with no
    // parens and no arguments. That is exactly a 0-ary sub called by name, so it
    // becomes one — the category is syntax, not a distinct kind of routine.
    // Without this the `:<name>` part was dropped and every such declaration was
    // a routine called `term`, so a module defining more than one (XDG::
    // BaseDirectory has seven) died "Redeclaration of routine 'term'".
    if (s->name == "term" && isOp(":") && !peek().spaceBefore &&
        peek().kind == Tok::Op && (peek().text == "<" || peek().text == "<<")) {
        advance();                       // :
        bool dbl = cur().text == "<<";
        advance();                       // < or <<
        std::vector<std::string> w = readAngleWords(dbl ? ">>" : ">");
        if (!w.empty() && !w[0].empty()) {
            s->name = w[0];
            sigilless_.insert(w[0]);     // so it parses as a TERM, not a listop
        }
    }
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
        else if (isKind(Tok::LBracket) && !cur().spaceBefore) {
            // `sub prefix:["∓"]` / `sub infix:[$sym]` — the name as an
            // EXPRESSION. A literal is its own spelling; a variable is read off
            // the `constant` that declares it, which is the only kind of
            // variable whose value exists while the file is parsed.
            advance(); // [
            std::string nm;
            if (isKind(Tok::StrLit)) nm = advance().text;
            else if (isKind(Tok::StrInterp)) {
                // the one escape a symbol is written with: `"\x[2213]"`
                const std::string raw = advance().text;
                for (size_t i = 0; i < raw.size(); i++) {
                    size_t close;
                    if (raw.compare(i, 3, "\\x[") == 0 && (close = raw.find(']', i)) != std::string::npos) {
                        uint32_t cp = (uint32_t)std::strtoul(raw.substr(i + 3, close - i - 3).c_str(), nullptr, 16);
                        nm += cpToUtf8(cp);
                        i = close;
                    }
                    else if (raw.compare(i, 3, "\\c[") == 0 && (close = raw.find(']', i)) != std::string::npos) {
                        int32_t cp = uniCharByName(raw.substr(i + 3, close - i - 3));
                        if (cp >= 0) nm += cpToUtf8((uint32_t)cp);
                        i = close;
                    }
                    else nm += raw[i];
                }
            }
            else if (isKind(Tok::Var) && src_) {
                nm = constantStringFor(*src_, cur().text);
                advance();
            }
            if (isKind(Tok::RBracket) && !nm.empty()) { advance(); w.push_back(nm); }
        }
        std::string opname = w.empty() ? "" : w[0];
        // the SPECIAL FORMS the compiler handles itself cannot be overridden
        {
            static const std::set<std::string> specialInfix = {"=", ":=", "::=", "~~", ".", ".="};
            if ((cat == "infix" && specialInfix.count(opname)) || (cat == "prefix" && opname == "|"))
                throw ParseError("Cannot override " + cat + " operator '" + opname +
                                 "', as it is a special form handled directly by the compiler",
                                 cur().line, "X::Syntax::Extension::SpecialForm",
                                 {{"category", cat}, {"opname", opname}});
        }
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
        sigRetType_.clear(); sigRetLiteral_.reset(); advance();
        // The routine's own `#|` sits above the DECLARATION line. On a one-line
        // signature that is also "above" every parameter on it, so the first one
        // adopted the routine's description: `#| Another way.` on
        // `multi MAIN('go', Int $n)` came out as the help text for `<n>`.
        sigOwnerLine_ = pos_ > 0 ? toks_[pos_ - 1].line : cur().line;
        s->params = parseSignature();
        sigOwnerLine_ = 0;
        // a `--> T` that follows a parameter (not comma-separated) is left for us
        if (isOp("-->")) { advance();
                           if (isKind(Tok::Ident) && (cur().text == "True" || cur().text == "False" || cur().text == "Nil" ||
                                          cur().text == "Empty"))
                sigRetLiteral_ = parsePrimary(); // `--> True` : a literal Bool/Nil return value
            else if (isKind(Tok::Ident)) sigRetType_ = cur().text + nativeRetParam(pos_) + retCoercionMark(peek());
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
    // trailing `#=` run on/below the decl line — resolved only NOW, after the
    // signature: a multi-line signature puts each parameter's `#=` on the lines
    // just below the declaration, and those are the params' docs, not the
    // routine's (parseSignature marked them claimed; issue #17)
    {   // …the same for a routine: leading and trailing docs are joined, not
        // one-or-the-other (a parameter's own `#=` was claimed in the signature)
        std::string trail = trailingPodFor(subDeclLine);
        s->podTrail = trail;
        if (!trail.empty()) s->pod = s->pod.empty() ? trail : s->pod + "\n" + trail;
    }
    // optional return type / traits up to block: skip until '{'
    // (note whether an `is export` trait is present — governs module visibility;
    //  capture `of T` / `returns T` / `--> T` as the return type)
    while (!isKind(Tok::LBrace) && !isKind(Tok::End) && !isKind(Tok::Semicolon)) {
        // `method loader is rw handles <load-delegate>` — a ROUTINE may delegate
        // just as an attribute may: the names it lists answer on the class's
        // behalf, asked of what the routine returns. PDF::COS routes its whole
        // loader API through one such method, and the trait was skipped with the
        // rest of the ladder, so `$.load-delegate` had nowhere to go.
        if (isIdent("handles")) {
            advance();
            bool paren = isKind(Tok::LParen);
            if (paren) advance();
            auto take = [&](const std::string& w) { if (!w.empty()) s->handles.push_back(w); };
            for (;;) {
                if (isOp("<")) { advance(); for (auto& w : readAngleWords(">")) take(w); }
                else if (isKind(Tok::QwList)) {
                    std::istringstream ws(advance().text);
                    std::string w; while (ws >> w) take(w);
                }
                else if (isKind(Tok::StrLit) || isKind(Tok::StrInterp) || isKind(Tok::Ident)) take(advance().text);
                else break;
                if (!matchKind(Tok::Comma)) break;
            }
            if (paren && isKind(Tok::RParen)) advance();
            continue;
        }
        if (isIdent("export")) {
            s->isExport = true;
            // `is export(:foo :bar)` — capture the tag names. A tag that is not
            // DEFAULT/MANDATORY is published only when the importer asks for it
            // (`use Mod :foo`), as in Rakudo; a plain `is export` has none and is
            // a default export. (Prompt's `sub prompt is export(:prompt)`.)
            if (peek().kind == Tok::LParen) {
                advance(); advance(); // `export` `(`
                int d = 1;
                while (d > 0 && !isKind(Tok::End)) {
                    if (isKind(Tok::LParen)) { d++; advance(); continue; }
                    if (isKind(Tok::RParen)) { d--; advance(); continue; }
                    // a colonpair tag `:foo` / `:!foo` / `:foo<v>`
                    if (isOp(":") && (peek().kind == Tok::Ident)) {
                        advance(); s->exportTags.push_back(advance().text);
                        // skip an optional value adverb `<…>` / `(…)`
                        continue;
                    }
                    // `is export(WTF)` — a bare word names nothing declared
                    if (d == 1 && isKind(Tok::Ident) && !importsModules_ && !knownTypeName(cur().text) &&
                        (peek().kind == Tok::RParen || peek().kind == Tok::Comma))
                        throw ParseError("Undeclared name:\n    " + cur().text + " used at line " +
                                         std::to_string(cur().line), cur().line, "X::Undeclared::Symbols", {});
                    advance();
                }
                continue; // the `export` ident was not advanced past by the loop head
            }
        }
        // NativeCall traits: `is native` / `is native('lib')` / `is symbol('name')`
        if (isIdent("native")) {
            s->isNative = true;
            if (peek().kind == Tok::LParen) {
                advance(); advance(); // native (
                if (isKind(Tok::StrLit) || isKind(Tok::StrInterp)) s->nativeLib = cur().text;
                else if (cur().text == "&" || (!cur().text.empty() && cur().text[0] == '&')) {
                    // `is native(&gen-lib)`: a code ref whose call yields the lib
                    // path (OpenSSL, many NativeCall modules). Capture the sub name.
                    std::string ref = cur().text;
                    if (ref == "&" && peek().kind == Tok::Ident) ref = peek().text; // `&` `name`
                    else if (!ref.empty() && ref[0] == '&') ref = ref.substr(1);    // `&name` one token
                    if (!ref.empty() && (ascii::isalpha((unsigned char)ref[0]) || ref[0] == '_'))
                        s->nativeLibSub = ref;
                }
                else {
                    // ANY other argument — `constant SHA1 = %?RESOURCES<libraries/sha1>`
                    // then `is native(SHA1)` (Digest::SHA1::Native, issue #13), or
                    // `is native(%?RESOURCES<libraries/x>)` inline. Parse the whole
                    // expression; it is evaluated when the sub DECLARATION executes,
                    // in the module's own scope — a call-time eval could not see a
                    // module-private constant from the caller's env. (A bare type
                    // object like `is native(Str)` evaluates to undefined ⇒ the
                    // default dlsym namespace, same as before.)
                    s->nativeLibExpr = parseExpression();
                }
                int d = 1; while (d > 0 && !isKind(Tok::End)) { if (isKind(Tok::LParen)) d++; else if (isKind(Tok::RParen)) d--; advance(); }
                continue;
            }
            // the ANGLE spelling `is native<sqlite3>` — one word, the library
            if (!peek().spaceBefore && peek().kind == Tok::QwList) {
                advance();
                std::string w = advance().text;
                s->nativeLib = w.substr(0, w.find(' '));
                continue;
            }
            if (!peek().spaceBefore && peek().kind == Tok::Op && peek().text == "<") {
                advance(); advance();
                auto ws = readAngleWords(">");
                if (!ws.empty()) s->nativeLib = ws[0];
                continue;
            }
        }
        if (isIdent("symbol") && peek().kind == Tok::LParen) {
            advance(); advance(); // symbol (
            if ((isKind(Tok::StrLit) || isKind(Tok::StrInterp)) && peek().kind == Tok::RParen)
                s->nativeSym = cur().text;
            else
                s->nativeSymExpr = parseExpression(); // computed — see nativeSymExpr in Ast.h
            int d = 1; while (d > 0 && !isKind(Tok::End)) { if (isKind(Tok::LParen)) d++; else if (isKind(Tok::RParen)) d--; advance(); }
            continue;
        }
        // the ANGLE spelling `is symbol<getpwuid>` (lizmat's P5* family uses it
        // on every native sub; the dropped value made dlsym look up the RAKU
        // name — `_getpwuid` — and fail)
        if (isIdent("symbol") && !peek().spaceBefore &&
            (peek().kind == Tok::QwList || (peek().kind == Tok::Op && peek().text == "<"))) {
            advance(); // symbol
            if (isKind(Tok::QwList)) {
                std::string w = advance().text;
                s->nativeSym = w.substr(0, w.find(' '));
            }
            else {
                advance(); // <
                auto ws = readAngleWords(">");
                if (!ws.empty()) s->nativeSym = ws[0];
            }
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
                if (kind == "right") { regSet('r', userInfixRight_, declInfix); s->assocRight = true; }
                else userInfixRight_.erase(declInfix);
                continue;
            }
        }
        // `is rw` / `is raw` on the ROUTINE ITSELF: its result is the container its
        // final expression names, so an assignment to the call writes through it.
        // `method AT-KEY($k) is raw { %!h.AT-KEY($k) }` is the shape Hash::Agnostic
        // builds its whole ASSIGN-KEY on (`self.AT-KEY($key) = value`).
        if (isIdent("is") && peek().kind == Tok::Ident &&
            (peek().text == "rw" || peek().text == "raw"))
            s->retRw = true;
        // `is DEPRECATED` / `is DEPRECATED("use X")` — calls get reported by
        // `Deprecation.report`, naming what to use instead
        if (isIdent("is") && peek().kind == Tok::Ident && peek().text == "DEPRECATED") {
            advance(); advance();
            s->deprecated = true;
            if (isKind(Tok::LParen) && !cur().spaceBefore) {
                advance();
                if (!isKind(Tok::RParen)) s->deprecatedWith = parseExpression();
                expectKind(Tok::RParen, ")");
            }
            continue;
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
                else if (isOp("<") && !cur().spaceBefore) {
                    // `is also<mag>` — the angle word list is the trait's argument
                    // (one word → Str, several → a List, as trait_mod:<is> receives)
                    advance();
                    std::vector<std::string> ws = readAngleWords(">");
                    if (ws.size() == 1) st.arg = std::make_unique<StrLit>(ws[0]);
                    else {
                        auto lst = std::make_unique<ListExpr>();
                        for (auto& w : ws) lst->items.push_back(std::make_unique<StrLit>(w));
                        st.arg = std::move(lst);
                    }
                }
                s->traits.push_back(std::move(st));
                continue;
            }
            // a BUILT-IN trait's argument is skipped whole: walking it token by
            // token took the `{` of `is DEPRECATED((sub { "a" })())` for the body
            // (only those whose argument nothing below reads: `export(:tag)`,
            // `tighter(…)`, `native(…)` and the like are taken further on)
            if (peek(2).kind == Tok::LParen && !peek(2).spaceBefore &&
                (peek().text == "DEPRECATED" || peek().text == "pure" || peek().text == "nodal" ||
                 peek().text == "implementation-detail" || peek().text == "hidden-from-backtrace" ||
                 peek().text == "test-assertion")) {
                advance(); advance(); // is NAME
                int d = 0;
                do { if (isKind(Tok::LParen)) d++; else if (isKind(Tok::RParen)) d--; advance(); }
                while (d > 0 && !isKind(Tok::End));
                continue;
            }
        }
        if ((isIdent("of") || isIdent("returns")) && peek().kind == Tok::Ident) {
            // `returns Positional of Numeric`: the `of` after a `returns` names the
            // ELEMENT type, not the return type — the last word used to win, so the
            // return type of that spelling was recorded as `Numeric`
            const bool elemOf = isIdent("of") && !s->retType.empty();
            // `sub f(--> List) returns Str` — the return type, said twice
            if (isIdent("returns") && !s->retType.empty())
                throw ParseError("Redeclaration of return type for '" + s->name + "' (previous return type was " +
                                 s->retType + ")", cur().line, "X::Redeclaration",
                                 {{"symbol", s->name}, {"what", "return type for"}});
            const bool viaReturns = isIdent("returns");
            advance();
            // `returns CArray[Str]` keeps its element parameter, as `--> …` does
            if (!elemOf) { s->retType = cur().text + nativeRetParam(pos_); s->retViaReturns = viaReturns; }
        } else if (isIdent("returns") && peek().kind != Tok::Ident && peek().kind != Tok::Var &&
                   peek().kind != Tok::LBrace) {
            // `returns !!!wtf???` — no type after the trait
            throw ParseError("Malformed trait", cur().line, "X::Syntax::Malformed", {{"what", "trait"}});
        } else if (isOp("-->") && peek().kind == Tok::Ident) {
            advance(); s->retType = cur().text + nativeRetParam(pos_);
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
        // A `whenever` inside a nested sub is legal iff the sub is itself
        // lexically inside a react/supply block — Rakudo's rule is purely
        // lexical, so propagate inReactBlock_ into the body (a top-level sub
        // still has it false and so still rejects a stray `whenever`).
        routineDepth_++; // &?ROUTINE is legal inside
        bool savedRw = sawReturnRw_; sawReturnRw_ = false;
        auto blk = parseBlock();
        // `return-rw` makes the routine container-returning even with no `is rw`
        // trait, which is what Rakudo does (`method m($i) { return-rw @!a[$i] }`).
        if (sawReturnRw_) s->retRw = true;
        sawReturnRw_ = savedRw;
        routineDepth_--;
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
        routineDepth_++;   // it IS the routine's body: `&?ROUTINE` is MAIN there
        while (!isKind(Tok::End)) {
            if (matchKind(Tok::Semicolon)) continue;
            s->body.push_back(parseStatement());
        }
        routineDepth_--;
    }
    // A bodyless named `sub foo;` is only legal as the unit-scoped `unit sub foo;`
    // (whose body is the rest of the file). Any other bodyless declaration is a
    // compile error — you probably meant `unit sub` or forgot the block.
    if (!hadBlock && s->body.empty() && !s->name.empty() && s->name != "MAIN" &&
        !unitDecl_ && !s->isMulti && (isKind(Tok::Semicolon) || isKind(Tok::End)))
        throw ParseError("A unit-scoped sub definition is not allowed except on a MAIN sub;\n"
                         "Please use the block form. If you did not mean to declare a unit-scoped\n"
                         "sub, perhaps you accidentally placed a semicolon after routine's definition?",
                         cur().line, "X::UnitScope::Invalid", {{"what", "sub"}});
    // `unit sub foo;` for anything but MAIN is 6.e syntax. Before 6.e only
    // `unit sub MAIN;` was allowed, so accepting the rest under 6.d let a
    // program compile here that Rakudo refuses — the revision has to say no.
    if (!hadBlock && s->body.empty() && !s->name.empty() && s->name != "MAIN" &&
        unitDecl_ && langRev_ < 2 && (isKind(Tok::Semicolon) || isKind(Tok::End)))
        throw ParseError("A unit-scoped sub is only allowed for MAIN before 6.e; "
                         "use the block form, or `use v6.e.PREVIEW`.",
                         cur().line, "X::UnitScope::Invalid", {{"what", "sub"}});
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
    if (!sd->name.empty()) declTypeNames_.insert(sd->name);
    // `of` and traits come in EITHER order — Cro writes `of Str is export`,
    // JSON::Class writes `is export of Mu` — and a trait may carry a tag list
    // (`is export(:TAG)`), which must be consumed with it or it desyncs the
    // stream. Loop until neither matches.
    for (;;) {
        if (isIdent("of")) {
            advance();
            if (isKind(Tok::Ident)) sd->baseType = advance().text;
            // …and the base type's SMILEY: `subset LatinStr of Str:D` is a
            // subset of the DEFINITE Str. Unconsumed, the `:D` fell out of the
            // declaration and became a statement of its own ("Useless use of
            // :D(True) in sink context"), taking any following `is export(…)`
            // with it — which then parsed as a call to a routine named
            // `export`. That is what stopped PDF (#79) and everything under it.
            if (isOp(":") && peek().kind == Tok::Ident &&
                (peek().text == "D" || peek().text == "U" || peek().text == "_")) {
                advance();
                std::string sm = advance().text;
                sd->defConstraint = sm == "D" ? 1 : sm == "U" ? 2 : 0;
            }
            // …and a COERCION: `subset CreditCard of Str() where …` refines Str
            // and says what may be coerced into it. Unconsumed, the `()` parsed
            // as a CALL to the type and the `where` after it became a routine
            // of its own ("Undefined routine 'where'").
            if (isKind(Tok::LParen) && !cur().spaceBefore) {
                sd->coerceBase = true;
                int d = 0;
                do { if (isKind(Tok::LParen)) d++; else if (isKind(Tok::RParen)) d--; advance(); }
                while (d > 0 && !isKind(Tok::End));
            }
            continue;
        }
        // `will complain { … }` (use experimental :will-complain, 6.e): a custom
        // type-check MESSAGE. Parse-only — the failure still throws, it just
        // carries the stock text. JSON::Class hangs one on its AssocOrPos subset.
        if (isIdent("will")) {
            advance();
            if (isKind(Tok::Ident)) advance();      // complain
            if (isKind(Tok::LBrace)) { auto blk = parseBlock(); (void)blk; }
            continue;
        }
        if (isIdent("is")) {
            advance();
            if (isKind(Tok::Ident)) advance();
            if (isKind(Tok::LParen) && !cur().spaceBefore) {
                int d = 0;
                do { if (isKind(Tok::LParen)) d++; else if (isKind(Tok::RParen)) d--; advance(); }
                while (d > 0 && !isKind(Tok::End));
            }
            continue;
        }
        break;
    }
    if (isIdent("where")) { advance(); sd->where = parseExpr(BP_ASSIGN); }
    return sd;
}

StmtPtr Parser::parseEnum() {
    // 'enum' already consumed:  enum [NAME] [of TYPE] ( <words> | (pairs) | «words» )
    auto ed = std::make_unique<EnumDecl>();
    if (isKind(Tok::Ident)) ed->name = advance().text;
    else if (isOp("::")) advance();   // `enum :: <un>` — anonymous, spelled out
    if (isIdent("of")) { advance(); if (isKind(Tok::Ident)) advance(); }
    while (isIdent("is")) {
        advance();
        if (isKind(Tok::Ident) || isKind(Tok::Var)) {
            if (cur().text == "export") ed->isExport = true;
            advance();
            // trait argument: `is export(:sort-list)` (Text::Utils)
            if (isKind(Tok::LParen) && !cur().spaceBefore) {
                int d = 0;
                do { if (isKind(Tok::LParen)) d++; else if (isKind(Tok::RParen)) d--; advance(); }
                while (d > 0 && !isKind(Tok::End));
            }
        }
    }
    if (!isKind(Tok::Semicolon) && !isKind(Tok::End) && !isKind(Tok::RBrace))
        // TIGHTER than an ordinary expression: the value list is one TERM, and a
        // trait may follow it. `enum Level <Off Fatal Error> does role { … }`
        // parsed the whole thing as `<…> does role{…}`, an infix expression whose
        // value is a single string — so the enum had ONE member named
        // "Off Fatal Error" and every use of `Error` was an undeclared name.
        // Lumberjack declares its levels exactly that way.
        ed->values = parseExpr(BP_MUL + 1);
    // …and the traits that follow the value list. `does role { … }` composes a
    // role into the enum's values in Rakudo; the members are what matters here,
    // and the role is consumed rather than composed (noted as a gap).
    while (isIdent("is") || isIdent("does")) {
        advance();
        if (isIdent("role") && peek().kind == Tok::LBrace) {   // `does role { … }`
            advance();                                        // role
            int d = 0;
            do { if (isKind(Tok::LBrace)) d++; else if (isKind(Tok::RBrace)) d--; advance(); }
            while (d > 0 && !isKind(Tok::End));
            continue;
        }
        if (isKind(Tok::Ident) || isKind(Tok::Var)) {
            if (cur().text == "export") ed->isExport = true;
            advance();
            if (isKind(Tok::LParen) && !cur().spaceBefore) {
                int d = 0;
                do { if (isKind(Tok::LParen)) d++; else if (isKind(Tok::RParen)) d--; advance(); }
                while (d > 0 && !isKind(Tok::End));
            }
        }
    }
    if (!ed->name.empty()) declTypeNames_.insert(ed->name);
    // enum MEMBERS are bare-name terms too. A word-list (`<Red Green>`) is
    // statically visible; anything computed makes the whole unit opaque —
    // over-lenient beats a false "Undeclared name".
    if (ed->values && ed->values->kind == NK::ArrayLit) {
        for (auto& it : static_cast<ArrayLit*>(ed->values.get())->items) {
            if (it->kind == NK::StrLit)
                declTypeNames_.insert(static_cast<StrLit*>(it.get())->v);
            else { declTypesOpaque_ = true; break; }
        }
    }
    // `enum E (A => 1, 'B', C)`: quoted and pair keys are declared; a BARE
    // name (`C`) is a reference to something declared elsewhere, so an
    // undeclared one is refused like any other (`enum Animal (Cat, Dog)`)
    else if (ed->values && ed->values->kind == NK::ListExpr) {
        for (auto& it : static_cast<ListExpr*>(ed->values.get())->items) {
            if (!it) continue;
            if (it->kind == NK::StrLit)
                declTypeNames_.insert(static_cast<StrLit*>(it.get())->v);
            else if (it->kind == NK::Pair && !static_cast<PairExpr*>(it.get())->keyExpr)
                declTypeNames_.insert(static_cast<PairExpr*>(it.get())->key);
            else if (it->kind == NK::NameTerm) continue;
            else { declTypesOpaque_ = true; break; }
        }
    }
    else if (ed->values)
        declTypesOpaque_ = true;
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
    if (!branches) return;
    // `/\ X/` — an unspace; `/m ** 1 ..2/` — a spaced bare range. Both are
    // compile-time refusals; quotes and character classes are left alone.
    char q = 0; int cls = 0;
    for (size_t i = 0; i < pat.size(); i++) {
        char c = pat[i];
        if (q) { if (c == '\\') i++; else if (c == q) q = 0; continue; }
        if (cls) { if (c == '\\') i++; else if (c == ']') cls--; continue; }
        if (c == '\'' || c == '"') { q = c; continue; }
        if (c == '<' && i + 1 < pat.size() && (pat[i + 1] == '[' || ((pat[i + 1] == '-' || pat[i + 1] == '+') && i + 2 < pat.size() && pat[i + 2] == '['))) { cls = 1; continue; }
        if (c == '#') { size_t nl = pat.find('\n', i); if (nl == std::string::npos) break; i = nl; continue; }
        if (c == '\\' && i + 1 < pat.size()) {
            if (pat[i + 1] == ' ')
                throw ParseError("No unspace allowed in regex; if you meant to match the literal character, "
                                 "please enclose in single quotes (' ') or use a backslashed form like \\x20",
                                 line, "X::Syntax::Regex::Unspace",
                                 {{"char", " "}, {"pre", "/" + pat.substr(0, i + 2)}, {"post", pat.substr(i + 2) + "/"}});
            i++;
            continue;
        }
        if (c == '*' && i + 1 < pat.size() && pat[i + 1] == '*') {
            size_t j = i + 2;
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
            i++;
        }
    }
}

void Parser::checkVirtualCallInDefault(size_t defStart) {
    // `has $.x = $.y` — a virtual call in an attribute default runs against a
    // partially constructed object; `has $.a = $^b` — a placeholder cannot
    // parameterize an attribute default. Rakudo rejects both at compile time.
    //
    // The two bans reach different depths. A virtual call stays illegal inside
    // a nested block (`has &.f = { $.y }` is an error in Rakudo too), but a
    // placeholder is CLAIMED by a block literal in the default — which is how
    // `has &.seqLeftSepForm = { ($^a, $^b) }` is written (issue #60,
    // FunctionalParsers). Only a block that runs where it stands (`do`, `try`,
    // `gather`, a control block) or one that already carries an explicit
    // signature (`-> $x { … }`) leaves the placeholder with nothing to bind to.
    static const std::set<std::string> runsInPlace = {
        "do", "try", "gather", "start", "quietly", "supply", "react", "whenever",
        "race", "hyper", "lazy", "eager", "once", "sink",
        "if", "elsif", "else", "unless", "with", "orwith", "without",
        "while", "until", "repeat", "loop", "for", "given", "when", "default",
    };
    std::vector<bool> claims; // per open `{`: can that block take a signature?
    for (size_t i = defStart; i < pos_ && i < toks_.size(); i++) {
        const Token& tk = toks_[i];
        if (tk.kind == Tok::LBrace) {
            const Token* prev = i > defStart ? &toks_[i - 1] : nullptr;
            bool claimsPH = true;
            if (prev) {
                // `do { … }` and friends run the block right here, with no
                // signature to hang a placeholder on
                if (prev->kind == Tok::Ident && runsInPlace.count(prev->text)) claimsPH = false;
                // `sub ($x) { … }`, `-> $x { … }`, `if (…) { … }`: the block
                // already has (or cannot have) a signature of its own
                else if (prev->kind == Tok::RParen || prev->kind == Tok::Var) claimsPH = false;
            }
            claims.push_back(claimsPH);
            continue;
        }
        if (tk.kind == Tok::RBrace) { if (!claims.empty()) claims.pop_back(); continue; }
        if (tk.kind != Tok::Var || tk.text.size() <= 2) continue;
        if (tk.text[1] == '.' &&
            (ascii::isalpha((unsigned char)tk.text[2]) || tk.text[2] == '_'))
            throw ParseError("Virtual call " + tk.text + " may not be used on "
                             "partially constructed object", tk.line,
                             "X::Syntax::VirtualCall", {{"call", tk.text}});
        if (tk.text[1] == '^' &&
            (ascii::isalpha((unsigned char)tk.text[2]) || tk.text[2] == '_')) {
            // the INNERMOST enclosing block decides: `{ do { $^a } }` is an
            // error in Rakudo even though the outer block could have taken it
            if (!claims.empty() && claims.back()) continue;
            throw ParseError("Placeholder variable " + tk.text + " may not be "
                             "used here because the surrounding block does not "
                             "take a signature", tk.line,
                             "X::Placeholder::Attribute", {{"placeholder", tk.text}});
        }
    }
}

// `my $.x` / `our @.y` / `state %.z` in a class or role body. Rakudo reads that
// as TWO declarations: a lexical the body's own code closes over, and a package
// accessor of the bare name — so `$.x` inside the type's methods is
// `self.x` like any other dotted form, and `Type.x` answers from outside too.
// ML::TriesWithFrequencies::Trieish publishes its two label strings exactly this
// way, and the whole distribution failed to install without it (issue #53).
//
// The lexical is renamed to a spelling no source can produce (`$ .x` — a space
// cannot appear in a variable name), which is what lets the generated accessor
// read it DIRECTLY instead of recursing back through `self`. The accessor is a
// real synthesized method, so role composition, type-object calls, `.^methods`
// and AST serialization all take it without a special case of their own.
namespace {
std::string dotDeclLexName(const std::string& n) { // `$.x` -> `$ .x`
    return std::string(1, n[0]) + " " + n.substr(1);
}
void addDotDeclAccessor(ClassDecl& cd, const std::string& lexName,
                        const std::string& bare, int line) {
    auto md = std::make_unique<SubDecl>();
    md->name = bare;
    md->isMethod = true;
    md->hadSig = true; // an explicit (empty) signature: nothing to scan for placeholders
    md->retRw = true;  // the accessor IS the lexical: `Type.x = v` writes it, as in Rakudo
    md->line = line;
    auto es = std::make_unique<ExprStmt>();
    es->e = std::make_unique<VarExpr>(lexName);
    es->e->line = line;
    es->line = line;
    md->body.push_back(std::move(es));
    cd.methods.push_back(std::move(md));
}
void desugarDotDecl(Expr* e, ClassDecl& cd, int line) {
    if (!e) return;
    if (e->kind == NK::Assign) { desugarDotDecl(static_cast<Assign*>(e)->target.get(), cd, line); return; }
    if (e->kind == NK::ListExpr) { // `my ($.a, $.b)`
        for (auto& it : static_cast<ListExpr*>(e)->items) desugarDotDecl(it.get(), cd, line);
        return;
    }
    if (e->kind != NK::VarExpr) return;
    auto* v = static_cast<VarExpr*>(e);
    if (!v->declare || v->name.size() < 3 || v->name[1] != '.') return;
    if (v->declScope != "my" && v->declScope != "our" && v->declScope != "state") return;
    std::string bare = v->name.substr(2);
    v->name = dotDeclLexName(v->name);
    v->syncAttrCache(); // the renamed node is a plain lexical, not an attribute
    addDotDeclAccessor(cd, v->name, bare, v->line ? v->line : line);
}
} // namespace

StmtPtr Parser::parseClass(bool isRole, bool isGrammar, bool isPackage, bool isUnit,
                           const std::string& kindKw) {
    // 'class'/'role'/'grammar'/'module'/'package' already consumed
    struct DepthGuard {
        int& d;
        DepthGuard(int& x) : d(x) { d++; }
        ~DepthGuard() { d--; }
    } classDepthGuard(classDepth_);
    auto cd = std::make_unique<ClassDecl>();
    {   // `#|` above the decl and `#=` below it are BOTH the declaration's doc,
        // and a declaration carrying the two answers them joined by a newline.
        int dl = pos_ > 0 ? toks_[pos_ - 1].line : cur().line;
        cd->pod = leadingPodFor(dl);
        std::string trail = trailingPodFor(dl);
        cd->podTrail = trail;
        if (!trail.empty()) cd->pod = cd->pod.empty() ? trail : cd->pod + "\n" + trail;
    }
    cd->isRole = isRole;
    cd->isGrammar = isGrammar;
    cd->isMonitor = kindKw == "monitor";
    cd->isModuleDecl = kindKw == "module";
    cd->isPackage = isPackage;
    if (isKind(Tok::Ident)) cd->name = advance().text;
    // A pseudo-package names a SCOPE to look in, so nothing may be declared
    // under one: `unit module MY;` is an error, and so is `module CALLER::Foo`.
    // Only the FIRST component counts — `module Foo::MY` is an ordinary name —
    // and `GLOBAL::` is the exception that proves it, being a real place to put
    // a declaration (`class GLOBAL::evo`, stripped just below). GLOBAL standing
    // ALONE is still nothing to declare, and Rakudo says so in its own words.
    if (!cd->name.empty()) {
        size_t sep = cd->name.find("::");
        std::string first = sep == std::string::npos ? cd->name : cd->name.substr(0, sep);
        if (first == "GLOBAL" && sep == std::string::npos)
            throw ParseError("Cannot declare pseudo-package GLOBAL", cur().line,
                             "X::AdHoc", {});
        if (first != "GLOBAL" && isPseudoPkg(first))
            throw ParseError("Cannot use pseudo package " + first + " in package name",
                             cur().line, "X::PseudoPackage::InDeclaration",
                             {{"pseudo-package", first}, {"action", "package name"}});
    }
    // `class GLOBAL::evo { … }` declares `evo` in the root package — the pseudo
    // package is where it goes, not part of its name (S12-class/magical-vars.t
    // EVALs such a class and then names it bare). Only GLOBAL:: is stripped:
    // OUR:: would need the current package's name, which this AST does not carry.
    if (cd->name.rfind("GLOBAL::", 0) == 0 && cd->name.size() > 8) cd->name = cd->name.substr(8);
    else if (isOp("::")) {
        advance(); // anonymous type: `class :: does R { … }` …
        if (isKind(Tok::LParen)) { // …or an INDIRECT name: `class ::(EXPR) { … }`
            advance();
            cd->nameExpr = parseExpression();
            expectKind(Tok::RParen, "expected ')' after ::( name expression");
        }
    }
    if (!cd->name.empty()) { declTypeNames_.insert(cd->name); declClassDecls_[cd->name] = cd.get(); }
    if (cd->nameExpr) declTypesOpaque_ = true; // the declared name is a run-time value
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
        ExprPtr valExpr;
        if (isKind(Tok::LParen)) {          // :adverb(EXPR) — evaluated at declaration
            advance();
            if (!isKind(Tok::RParen)) valExpr = parseExpression();
            expectKind(Tok::RParen, ")");
        }
        if (adv == "ver") { cd->ver = val; cd->verExpr = std::move(valExpr); }
        else if (adv == "auth") { cd->auth = val; cd->authExpr = std::move(valExpr); }
        else if (adv == "api") { cd->api = val; cd->apiExpr = std::move(valExpr); }
    }
    if (isRole && isKind(Tok::LBracket)) {
        // parameterized role: `role R[$x, Bool :$opt = False] { … $x … $opt … }`.
        // The params are bound when the role is composed (`does R[args]`) — see
        // the composition code, which stashes the values on the consuming class.
        advance();
        cd->roleParams = parseSignature(Tok::RBracket);
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
        cd->bracedBody = true;   // …even when nothing is between the braces
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
    // `hides Parent` inherits as `is Parent` does (S12); what it hides from
    // dispatch is not modelled
    while (isIdent("is") || isIdent("does") || (isIdent("hides") && peek().kind == Tok::Ident)) {
        bool isDoes = isIdent("does");
        if (isIdent("hides")) cd->hidesNames.push_back(peek().text);
        advance();
        // `is hidden` — the class keeps out of its children's `nextsame` chain;
        // a trait, not a parent (not modelled, like `hides`)
        if (!isDoes && isIdent("hidden")) { advance(); continue; }
        // `class A is DEPRECATED("…")` — a trait, not a parent (its report is not
        // modelled; a routine's is)
        if (!isDoes && isIdent("DEPRECATED")) {
            advance();
            if (isKind(Tok::LParen) && !cur().spaceBefore) {
                int d = 0;
                do { if (isKind(Tok::LParen)) d++; else if (isKind(Tok::RParen)) d--; advance(); }
                while (d > 0 && !isKind(Tok::End));
            }
            continue;
        }
        if (!isDoes && isIdent("export")) { // trait, not a parent class
            advance();
            // `is export(:TAG)` — consume the tag list too. Leaving it in the
            // stream desynced the parse by one statement: JSON::Class::Types'
            // `class NOT-SET is Nil is export(:NOT-SET) {…}` silently ate the
            // statement after the class, and the miss surfaced as "Confused"
            // several lines later. Tags gate selective import, which rakupp
            // does not implement — parse-only, like the sub-trait handler.
            if (isKind(Tok::LParen)) {
                int d = 0;
                do {
                    if (isKind(Tok::LParen)) d++;
                    else if (isKind(Tok::RParen)) d--;
                    advance();
                } while (d > 0 && !isKind(Tok::End));
            }
            continue;
        }
        // `class X is rw` — every public attribute is writable (Compress::Zstd
        // declares its buffer structs this way). A trait, not a parent.
        if (!isDoes && isIdent("rw")) {
            advance();
            cd->classRw = true;
            continue;
        }
        // `is repr("CStruct")` — a VM-representation trait, not a parent class.
        // Capture the name (drives NativeCall struct layout); most reprs our
        // values pick their own storage for and simply ignore.
        if (!isDoes && isIdent("repr")) {
            advance();
            if (isKind(Tok::LParen)) {
                advance();
                if (isKind(Tok::StrLit) || isKind(Tok::StrInterp) || isKind(Tok::Ident)) cd->repr = cur().text;
                int d = 1; while (d > 0 && !isKind(Tok::End)) { if (isKind(Tok::LParen)) d++; else if (isKind(Tok::RParen)) d--; advance(); }
            }
            // the ANGLE spelling `is repr<CStruct>` (lizmat's P5* family uses
            // it throughout). Unconsumed, the value derailed the whole class:
            // the trait loop ended on it, the `{` was no longer where the body
            // check looks, and the class fell into the forward-declaration
            // path — with its body left behind to parse as a bare block.
            else if (isKind(Tok::QwList) && !cur().spaceBefore) {
                std::string w = advance().text;         // first word is the repr name
                cd->repr = w.substr(0, w.find(' '));
            }
            else if (isOp("<") && !cur().spaceBefore) {
                advance();
                auto ws = readAngleWords(">");
                if (!ws.empty()) cd->repr = ws[0];
            }
            continue;
        }
        // `is <lowercase name>` WITH A TIGHT ARGUMENT is a USER TRAIT, not a
        // superclass: `unit model Foo is table<sqlite_master>` is how every Red
        // model names its table, and read as inheritance it died with "cannot
        // inherit from 'table'". A superclass never carries `<…>`/`{…}`, and the
        // parenthesised form is a trait too (`is repr("CStruct")` above is the
        // built-in of exactly that shape).
        //
        // A BARE `is name` stays a parent here whatever its case — fez's own
        // `class auth-response is api-response` is lowercase, and taking that
        // for a trait cost the child every inherited method. When the name turns
        // out to name no type, the DECLARATION offers it to `trait_mod:<is>`
        // before it complains (see the ClassDecl executor), which is where a
        // bare `is temp` lands.
        if (!isDoes && isKind(Tok::Ident) && !cur().text.empty() &&
            ascii::islower((unsigned char)cur().text[0]) &&
            !peek().spaceBefore &&
            (peek().kind == Tok::LParen || peek().kind == Tok::LBrace ||
             peek().kind == Tok::QwList ||
             (peek().kind == Tok::Op && peek().text == "<"))) {
            std::string utn = advance().text;
            cd->userTraits.emplace_back(utn, parsePrimary());
            continue;
        }
        if (isKind(Tok::Ident) || isKind(Tok::Var)) {
            std::string t = advance().text;
            if (cd->parent.empty()) { cd->parent = t; cd->parentIsDoes = isDoes; }
            else if (isDoes) cd->roles.push_back(t);      // extra `does Role` — composed in
            else cd->extraParents.push_back(t);            // extra `is Class` — multiple inheritance
            // parameterized composition `does R[args]`: capture the bracket args so
            // a role's value params (role R[$x]/[%h]/[Bool :$opt]) bind at compose
            // time and are visible in the role body. (Type args like `does R[Int]`
            // are captured too, bound to `::T` params.)
            if (isKind(Tok::LBracket)) {
                advance(); // [
                std::vector<ExprPtr> rargs;
                if (!isKind(Tok::RBracket)) {
                    ExprPtr e = parseExpression();
                    if (e && e->kind == NK::ListExpr && !static_cast<ListExpr*>(e.get())->parenned)
                        for (auto& it : static_cast<ListExpr*>(e.get())->items) rargs.push_back(std::move(it));
                    else if (e) rargs.push_back(std::move(e));
                }
                expectKind(Tok::RBracket, "]");
                if (!rargs.empty()) cd->roleArgs.push_back({t, std::move(rargs)});
            }
        }
        // skip any remaining type params / stray brackets
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
    typeIsRole_.push_back(cd->isRole);
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
        if (isIdent("has") || isIdent("HAS")) {
            if (isPackage)
                throw ParseError("A " + kindKw + " cannot have attributes",
                                 cur().line, "X::Attribute::Package",
                                 {{"package-kind", kindKw}});
            // `HAS` declares the member INLINE — C's `struct T t;` where `has`
            // means `struct T *t;`. It is a declarator in its own right, not a
            // trait, so it is read here and nowhere else.
            const bool attrInlined = isIdent("HAS");
            advance();
            // optional type before the attribute var: `has Int $.x`, `has Int:D $.x`,
            // `has Array[Int] $.x` — consume the type name, any :D/:U/:_ smiley, and [..] params.
            std::string attrType;
            int attrSmiley = 0; // :D=1, :U=2 (:_ / none = 0)
            if (isKind(Tok::Ident)) {
                attrType = advance().text; // type name
                if (isOp(":") && (peek().kind == Tok::Ident)) { // :D / :U / :_ smiley
                    advance(); std::string sm = advance().text;
                    if (sm == "D") attrSmiley = 1; else if (sm == "U") attrSmiley = 2;
                }
                if (isKind(Tok::LBracket)) {
                    // the [T] group is part of the TYPE, exactly as in `my`
                    // declarations above: `has CArray[Str] $.gr_mem` must keep
                    // its element type or the field reads back as int64
                    // pointers (P5getgrnam walked those "always defined"
                    // pointers past the NULL terminator and crashed)
                    int d = 0;
                    std::string ptxt;
                    do {
                        if (isKind(Tok::LBracket)) d++;
                        else if (isKind(Tok::RBracket)) d--;
                        ptxt += cur().text;
                        advance();
                    } while (d > 0 && !isKind(Tok::End));
                    attrType += ptxt;
                }
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
            // A parenthesized attribute list, with or WITHOUT a leading type:
            // `has ( $.this, $.that )` and `has UInt ($.width, $.height)`. The
            // typed form used to fall through every branch and declare NOTHING —
            // not one attribute, so the class had no accessors at all and the
            // first mention of `$!height` was an undeclared-attribute error.
            // (`has Int(Cool) $.n` is a coercion type, already consumed above:
            // that `(` is glued to the type name, this one is separated by
            // space and holds variables.)
            if (isKind(Tok::LParen)) {
                advance();
                size_t firstListAttr = cd->attrs.size();
                while (!isKind(Tok::RParen) && !isKind(Tok::End)) {
                    if (isKind(Tok::Var)) {
                        std::string vn = advance().text;
                        AttrDecl a;
                        a.type = attrType;              // the shared type applies to each
                        a.defConstraint = attrSmiley;   // …and so does its :D/:U smiley
                        a.coerce = attrCoerce;
                        a.inlined = attrInlined;
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
                // `has ($.x, $.y) is rw is default(42)` — the traits apply to
                // every attribute of the list; a default expression is parsed
                // once per attribute so each owns its own
                while (isIdent("is") && peek().kind == Tok::Ident) {
                    advance();
                    std::string tn = advance().text;
                    size_t argAt = pos_, argEnd = pos_;
                    for (size_t k = firstListAttr; k < cd->attrs.size(); k++) {
                        auto& la = cd->attrs[k];
                        if (tn == "rw") la.rw = true;
                        else if (tn == "required") la.required = true;
                        else if (tn == "built") la.built = true;
                        if (isKind(Tok::LParen)) {
                            advance();
                            ExprPtr arg = isKind(Tok::RParen) ? nullptr : parseExpression();
                            while (!isKind(Tok::RParen) && !isKind(Tok::End)) advance();
                            if (isKind(Tok::RParen)) advance();
                            if (tn == "default" && arg) la.defaultTrait = std::move(arg);
                            argEnd = pos_;
                            pos_ = argAt;
                        }
                    }
                    pos_ = argEnd;
                }
                matchKind(Tok::Semicolon);
                continue;
            }
            if (isKind(Tok::Var)) {
                std::string vn = advance().text;
                AttrDecl a;
                a.type = attrType;
                a.defConstraint = attrSmiley;
                a.coerce = attrCoerce;
                a.inlined = attrInlined;
                a.sigil = vn[0];
                size_t idx = 1;
                if (vn.size() > 1 && (vn[1] == '.' || vn[1] == '!')) { a.pub = (vn[1] == '.'); idx = 2; }
                a.name = vn.substr(idx);
                // an OBJECT-HASH key-type shape rides right on the name:
                // `has Callable %!Conversions{Mu:U} handles <AT-KEY EXISTS-KEY>`
                // (DBDish::TypeConverter). Left unconsumed it derailed the
                // trait loop, so `handles` was never captured.
                if (a.sigil == '%' && isKind(Tok::LBrace) && !cur().spaceBefore) {
                    a.objKeyed = true; // {Mu:U}-shaped: type-object keys stay distinct
                    // …and a plain `{Type}` shape is the KEY TYPE, recorded the
                    // way `my %h{Type}` records it ("valueType,keyType"), so
                    // `.keys` hands back the objects themselves. Red's
                    // `has %!relationships{Attribute}` answered Str keys and
                    // every `$rel.build-relationship` died on a string.
                    if (peek().kind == Tok::Ident && peek(2).kind == Tok::RBrace && a.type.find(',') == std::string::npos)
                        a.type = (a.type.empty() ? (langRev_ >= 2 ? "Mu" : "Any") : a.type) + "," + peek().text;
                    int d = 0;
                    do { if (isKind(Tok::LBrace)) d++; else if (isKind(Tok::RBrace)) d--; advance(); }
                    while (d > 0 && !isKind(Tok::End));
                }
                // traits before the default: is rw / is readonly / of Type / does Role / where EXPR / handles <...>
                while (isIdent("is") || isIdent("of") || isIdent("does") || isIdent("where") || isIdent("handles")) {
                    std::string tr = advance().text;
                    // BP_ASSIGN + 1: the `=` that follows a where clause is the
                    // attribute's DEFAULT, not part of the constraint.
                    // `has Str $.locale is rw where { … } = 'en'` was parsing the
                    // whole `{ … } = 'en'` as the constraint, which lost the
                    // default and left the attribute unwritable
                    // (Date::Calendar::Gregorian declares two of them).
                    if (tr == "where") { a.whereExpr = parseExpr(BP_ASSIGN + 1); continue; }
                    // `has $.a of Int` — the type, spelled as a trait
                    if (tr == "of" && isKind(Tok::Ident) && ascii::isupper((unsigned char)cur().text[0])) {
                        a.type = advance().text;
                        continue;
                    }
                    if (tr == "handles") { // handles <m1 m2> / handles "m" / handles 'm'
                        // …and the RENAMING forms, `handles(:local<remote>, …)` /
                        // `handles(local => 'remote')`: the class exposes the KEY and
                        // calls the VALUE on the attribute. TAP's Output::Handle is
                        // `has IO::Handle $.handle handles(:print<print>, :terminal<t>)`
                        // — `.terminal` is IO::Handle's `.t` (issue #34).
                        auto add = [&a](const std::string& local, const std::string& remote) {
                            a.handles.push_back(local);
                            a.handlesTo.push_back(local == remote ? "" : remote);
                        };
                        auto angleWord = [&, this](std::string& into) {
                            if (isOp("<")) { advance();
                                auto w = readAngleWords(">"); if (!w.empty()) into = w[0]; return true; }
                            if (isKind(Tok::StrLit) || isKind(Tok::StrInterp) || isKind(Tok::Ident)) {
                                into = advance().text; return true; }
                            if (isKind(Tok::QwList)) {
                                std::istringstream ws(advance().text);
                                std::string w; if (ws >> w) into = w; return true; }
                            return false;
                        };
                        // one delegation item; false = a form we don't model
                        auto item = [&, this]() -> bool {
                            if (isOp(":") && peek().kind == Tok::Ident) { // :local<remote> / :local('remote')
                                advance();
                                std::string local = advance().text, remote = local;
                                if (isKind(Tok::LParen)) { advance(); angleWord(remote); matchKind(Tok::RParen); }
                                else angleWord(remote);
                                add(local, remote);
                                return true;
                            }
                            if ((isKind(Tok::Ident) || isKind(Tok::StrLit) || isKind(Tok::StrInterp)) &&
                                peek().kind == Tok::FatArrow) {                    // local => 'remote'
                                std::string local = advance().text, remote = local;
                                advance();                                          // =>
                                angleWord(remote);
                                add(local, remote);
                                return true;
                            }
                            if (isOp("<")) { advance();
                                for (auto& w : readAngleWords(">")) add(w, w); return true; }
                            if (isKind(Tok::QwList)) {
                                std::istringstream ws(advance().text);
                                for (std::string w; ws >> w; ) add(w, w);
                                return true;
                            }
                            if (isKind(Tok::StrLit) || isKind(Tok::StrInterp) || isKind(Tok::Ident)) {
                                std::string n = advance().text; add(n, n); return true;
                            }
                            if (isOp("*")) { advance(); add("*", "*"); return true; } // any unknown method
                            return false;
                        };
                        if (isKind(Tok::LParen)) {
                            advance();
                            while (!isKind(Tok::RParen) && !isKind(Tok::End)) {
                                if (matchKind(Tok::Comma)) continue;
                                if (!item()) advance(); // a form we don't model (regex, Whatever): skip it
                            }
                            matchKind(Tok::RParen);
                        }
                        else item();
                        continue;
                    }
                    if (tr == "is" && isIdent("rw")) a.rw = true;
                    if (tr == "is" && isIdent("required")) a.required = true;
                    // `is built` / `is built(:bind)` — a private attr the default
                    // constructor may set by name (JSON::Class's $!declarant); the
                    // generic skip below consumes any (:bind)-style argument
                    if (tr == "is" && isIdent("built")) a.built = true;
                    // `is required("reason")` — the reason goes into the exception
                    // message. The generic trait-argument skip below used to eat it.
                    if (tr == "is" && isIdent("required") && peek().kind == Tok::LParen) {
                        size_t save = pos_;
                        advance();                                    // `required`
                        advance();                                    // `(`
                        if ((isKind(Tok::StrLit) || isKind(Tok::StrInterp)) &&
                            peek().kind == Tok::RParen) {
                            a.requiredWhy = advance().text;
                            advance();                                // `)`
                            continue;
                        }
                        pos_ = save;                                  // not a literal: fall through
                    }
                    if (tr == "is" && isKind(Tok::Ident)) {
                        // `has %.a is Set` — the QuantHash family, and also any
                        // user type: `has %.Converter is DBDish::TypeConverter`
                        // makes the attribute an instance of that role, not a
                        // plain Hash. Only a capitalised name, so `is rw` and the
                        // other lowercase traits are untouched.
                        const std::string& tn = cur().text;
                        if (!tn.empty() && ascii::isupper((unsigned char)tn[0])) {
                            a.containerIs = tn;
                            advance();
                            // QUALIFIED names keep their `::segment`s — the
                            // captured type was just "DBDish" (DBIish 06-types)
                            while (isOp("::") && peek().kind == Tok::Ident) {
                                advance();
                                a.containerIs += "::" + advance().text;
                            }
                            if (isKind(Tok::LParen)) { int d = 0; do { if (isKind(Tok::LParen)) d++; else if (isKind(Tok::RParen)) d--; advance(); } while (d > 0 && !isKind(Tok::End)); }
                            continue;
                        }
                    }
                    // any OTHER lowercase `is trait` is a USER trait — keep the
                    // name and parse its parenthesised argument as a real
                    // expression instead of skipping it, because the JSON
                    // ecosystem's attribute traits carry their payload there:
                    // `is json-name('licenseId')`, `is unmarshalled-by(-> $d {…})`.
                    if (tr == "is" && isKind(Tok::Ident)) {
                        std::string utn = advance().text;
                        static const std::set<std::string> builtinAttrTraits =
                            {"rw", "readonly", "required", "built", "default", "DEPRECATED"};
                        bool known = builtinAttrTraits.count(utn) != 0;
                        // A trait argument does not have to be parenthesised:
                        // `is column{ :name<x>, :!nullable }` hands a HASH and
                        // `is column<rootpage>` a word — the two spellings Red
                        // writes every model with. Only `(…)` was read, so the
                        // brace was left in the stream and the trait arrived as
                        // a bare True, matching no candidate and silently doing
                        // nothing: every Red model came out with no columns.
                        if (!known && !cur().spaceBefore &&
                            (isKind(Tok::LBrace) || isKind(Tok::QwList) ||
                             (isKind(Tok::Op) && cur().text == "<"))) {
                            a.userTraits.emplace_back(utn, parsePrimary());
                            continue;
                        }
                        if (isKind(Tok::LParen) && !cur().spaceBefore) {
                            advance();
                            ExprPtr arg = isKind(Tok::RParen) ? nullptr : parseExpression();
                            while (!isKind(Tok::RParen) && !isKind(Tok::End)) advance();
                            if (isKind(Tok::RParen)) advance();
                            // `is default(V)` on an attribute: the value it reads
                            // when nothing was assigned. Its argument was parsed
                            // and thrown away with every other known trait's, so
                            // `has $.a is default(42)` read as (Any). An explicit
                            // initialiser still wins — `is default(42) = 768` is
                            // 768 — but `.VAR.default` answers 42 either way.
                            if (known && utn == "default" && arg)
                                a.defaultTrait = std::move(arg);
                            else if (!known) a.userTraits.emplace_back(utn, std::move(arg));
                        }
                        else if (!known) a.userTraits.emplace_back(utn, nullptr);
                        continue;
                    }
                    if (isKind(Tok::Ident) || isKind(Tok::Var)) advance();
                    if (isKind(Tok::LParen)) { int d = 0; do { if (isKind(Tok::LParen)) d++; else if (isKind(Tok::RParen)) d--; advance(); } while (d > 0 && !isKind(Tok::End)); }
                }
                bool dotEq = false, hasDefault = true;
                if (matchOp("=")) { }
                else if (matchOp(".=")) dotEq = true;
                else if (isOp(".") && peek().kind == Tok::Op && peek().text == "=") { advance(); advance(); dotEq = true; }
                else hasDefault = false;
                if (hasDefault) {
                    size_t defStart = pos_;
                    if (dotEq) {
                        // `has T $x .= meth(args)` desugars to a default of
                        // `$!x.meth(args)` — the attr is seeded to T's type object at
                        // construction, and .meth (usually .new) coerces it. Build the
                        // method call so it actually runs (was dropped before).
                        auto mc = std::make_unique<MethodCall>();
                        mc->inv = std::make_unique<VarExpr>("$!" + a.name);
                        if (isKind(Tok::Ident) || isKind(Tok::Var)) mc->method = advance().text;
                        if (isKind(Tok::LParen) && !cur().spaceBefore) { advance(); mc->args = parseCallArgs(); }
                        a.def = std::move(mc);
                    } else {
                        // A `@`/`%` attribute's default is a LIST assignment, so
                        // it takes the whole comma list exactly as `my @a = 1,2,3`
                        // does (the infix path decides this from the sigil; this
                        // one consumed the `=` itself and stopped at the first
                        // comma, so `has @.things = 1,2,3` kept only the 1).
                        a.def = parseExpr((a.sigil == '@' || a.sigil == '%') ? BP_ZIP : BP_ASSIGN);
                        checkVirtualCallInDefault(defStart);
                    }
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
            std::string tn;
            while (isKind(Tok::Ident) || isOp("::")) tn += advance().text;
            if (!tn.empty()) cd->trustsNames.push_back(tn);
            matchKind(Tok::Semicolon);
            continue;
        }
        // `only method new(...)` — the explicit non-multi declarator (the default);
        // consume it and parse the routine as usual (Cro::HTTP::Server)
        if (isIdent("only") && (peek().text == "method" || peek().text == "submethod" || peek().text == "sub"))
            advance();
        if (isIdent("method") || isIdent("submethod")) {
            bool sub = isIdent("submethod");
            int ln = cur().line;
            advance();
            auto s = parseSub(false, false, true);
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
            // isProto must travel: a bare `{*}` proto METHOD is the group
            // definition, not a candidate — without the flag it entered dispatch
            // with its (|) slurpy and beat every invocant-constrained candidate
            auto s = parseSub(true, multiness == "proto", isM);
            if (static_cast<SubDecl*>(s.get())->name.empty())
                throw ParseError("Cannot put " + multiness + " on anonymous routine",
                                 cur().line, "X::Anon::Multi",
                                 {{"multiness", multiness},
                                  {"routine-type", isM ? "method" : "sub"}});
            static_cast<SubDecl*>(s.get())->isMethod = isM;
            static_cast<SubDecl*>(s.get())->isSubmethod = isSub;
            // Only `multi method` / `submethod` declares a method. A bare
            // `proto`/`multi` in a class body is a SUB, as it is anywhere else —
            // it belongs in the body scope, so `proto glob(|) is export {*}` in
            // `unit class IO::Glob` reaches the importer instead of becoming an
            // unreachable method.
            if (!isM) { cd->body.push_back(std::move(s)); continue; }
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
                    // split a captured signature `NAME(Str $indent, $*STOPPER = '"', …)` →
                    // name + param entries. Each entry is the var name (twigils kept:
                    // `$*STOPPER` is a dynamic var) plus, after a \x1f separator, the
                    // default-value expression text when the signature declares one.
                    std::vector<std::string> params;
                    // `multi rule expr(0)` / `multi token pred(3)`: a positional
                    // slot given as a LITERAL VALUE rather than a variable is a
                    // dispatch constraint, not a binding — one entry per slot,
                    // empty where the slot names a variable. 99problems P47
                    // builds its precedence climber out of exactly this
                    // (`expr(0)` is the base case that stops `<expr($p-1)>`).
                    std::vector<std::string> lits;
                    auto lp = nm.find('(');
                    if (lp != std::string::npos) {
                        std::string sig = nm.substr(lp);
                        nm = nm.substr(0, lp);
                        {   // split the signature on top-level commas and keep the
                            // literal slots; a slot that starts with a sigil is a
                            // parameter and is collected by the scan below.
                            std::string inner = sig;
                            if (!inner.empty() && inner[0] == '(') inner = inner.substr(1);
                            if (!inner.empty() && inner.back() == ')') inner.pop_back();
                            std::string cur2; int d2 = 0; char q2 = 0;
                            std::vector<std::string> slots;
                            for (size_t k = 0; k <= inner.size(); k++) {
                                char c2 = k < inner.size() ? inner[k] : ',';
                                if (q2) { cur2 += c2; if (c2 == q2) q2 = 0; continue; }
                                if (c2 == '\'' || c2 == '"') { q2 = c2; cur2 += c2; continue; }
                                if (c2 == '(' || c2 == '[' || c2 == '{') d2++;
                                if (c2 == ')' || c2 == ']' || c2 == '}') d2--;
                                if (c2 == ',' && d2 == 0) { slots.push_back(cur2); cur2.clear(); continue; }
                                cur2 += c2;
                            }
                            bool any = false;
                            for (auto& sl : slots) {
                                size_t a2 = sl.find_first_not_of(" \t");
                                if (a2 == std::string::npos) { lits.push_back(""); continue; }
                                size_t b3 = sl.find_last_not_of(" \t");
                                std::string t2 = sl.substr(a2, b3 - a2 + 1);
                                // A slot is a literal VALUE only when it looks
                                // like one: a number, a quoted string, or a Bool.
                                // A bare name is a TYPE constraint on an
                                // anonymous parameter (`token f(Int)`), which
                                // this does not dispatch on — reading it as the
                                // literal "Int" would refuse every call.
                                char c3 = t2[0];
                                bool isLit = ascii::isdigit((unsigned char)c3) || c3 == '\'' || c3 == '"' ||
                                             ((c3 == '-' || c3 == '+') && t2.size() > 1 &&
                                              ascii::isdigit((unsigned char)t2[1])) ||
                                             t2 == "True" || t2 == "False";
                                if (!isLit) { lits.push_back(""); continue; }
                                lits.push_back(t2); any = true;
                            }
                            if (!any) lits.clear();
                        }
                        for (size_t i = 0; i < sig.size(); i++) {
                            if (sig[i] != '$' && sig[i] != '@' && sig[i] != '%') continue;
                            std::string v(1, sig[i]);
                            size_t j = i + 1;
                            if (j < sig.size() && sig[j] == '*') v += sig[j++]; // dynamic-var twigil
                            for (; j < sig.size() && (ascii::isalnum((unsigned char)sig[j]) || sig[j] == '_' || sig[j] == '-'); j++) v += sig[j];
                            if (v.size() == 1 || (v.size() == 2 && v[1] == '*')) continue; // bare sigil
                            i = j - 1;
                            // an optional `= EXPR` default, up to a top-level ',' or ')'
                            while (j < sig.size() && ascii::isspace((unsigned char)sig[j])) j++;
                            if (j < sig.size() && sig[j] == '=' && (j + 1 >= sig.size() || sig[j + 1] != '=')) {
                                j++;
                                int depth = 0; char q = 0; std::string dflt;
                                for (; j < sig.size(); j++) {
                                    char sc2 = sig[j];
                                    if (q) { dflt += sc2; if (sc2 == q) q = 0; continue; }
                                    if (sc2 == '\'' || sc2 == '"') { q = sc2; dflt += sc2; continue; }
                                    if (sc2 == '(' || sc2 == '[' || sc2 == '{') depth++;
                                    if (sc2 == ')' || sc2 == ']' || sc2 == '}') { if (depth == 0) break; depth--; }
                                    if (sc2 == ',' && depth == 0) break;
                                    dflt += sc2;
                                }
                                size_t a = dflt.find_first_not_of(" \t"), b2 = dflt.find_last_not_of(" \t");
                                if (a != std::string::npos) v += '\x1f' + dflt.substr(a, b2 - a + 1);
                                i = j - 1;
                            }
                            params.push_back(v);
                        }
                    }
                    if (!nm.empty()) cd->rules.push_back({nm, pat, kind, params, lits});
                    continue;
                }
            }
        }
        // anything else in class body: nested classes/enums get registered globally;
        // `my` lexicals and plain expressions run in the body scope the methods
        // close over (`my $lex = ...; method m { $lex }`); the rest is discarded.
        auto st = parseStatement();
        // `my $.x = …`: split into the lexical the body closes over and the
        // accessor method the name promises (see desugarDotDecl above). A
        // module/package body has no method table, so it keeps the plain form.
        if (st && !isPackage && st->kind == NK::ExprStmt)
            desugarDotDecl(static_cast<ExprStmt*>(st.get())->e.get(), *cd, st->line);
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
             // a `subset` declared in the body is a real type the class's own
             // signatures use (`subset ValidRGB of Real where 0 <= $_ <= 255` in
             // Color, then `multi method alpha(ValidRGB $a)`); dropping it left
             // every such signature unmatchable
             st->kind == NK::SubsetDecl ||
             // a lexical `my regex/token/rule NAME { … }` in the body is callable
             // as `<NAME>` from the class's own methods — URI declares
             // `my regex path-authority { … }` after `unit class URI` and matches
             // against it in !check-path. Dropping it here left `<path-authority>`
             // unresolved, which the engine treats as a lenient zero-width match,
             // so every path validation failed with "Could not parse path".
             st->kind == NK::NamedRegexDecl ||
             // a compile-time phaser in the body is how a class DECLARES ITSELF:
             //     BEGIN { for <get head put post delete> -> $m {
             //                 ::?CLASS.^add_method: $m, method (…) {…} } }
             // is the whole public API of HTTP::Tinyish::Base. Dropped here, the
             // class simply had no `get` — and nothing said so, because a body
             // statement this loop does not recognise vanishes without a word.
             // (END is deliberately still out: running it at declaration time
             // would be worse than not running it.)
             (st->kind == NK::Block && (static_cast<Block*>(st.get())->phaser == "BEGIN" ||
                                        static_cast<Block*>(st.get())->phaser == "CHECK" ||
                                        static_cast<Block*>(st.get())->phaser == "INIT")) ||
             st->kind == NK::UseStmt)) // `use X` inside a class body loads at declaration (URI does this after `unit class URI`)
            cd->body.push_back(std::move(st));
    }
    if (braced) expectKind(Tok::RBrace, "}");
    typeStack_.pop_back();
    typeIsRole_.pop_back();
    return cd;
}

StmtPtr Parser::parseIf(bool isUnless) {
    auto s = std::make_unique<IfStmt>();
    s->isUnless = isUnless;
    ExprPtr cond;
    { bool sv = stmtCond_; stmtCond_ = true; cond = parseExpression(); stmtCond_ = sv; }
    // The binder after `->` is the FULL pointy-signature grammar, the same one a
    // block's signature uses — a $/@/% variable, a SIGILLESS `\name`
    // (`if self.elems -> \elems { }`, the Agnostic family's shape), a type or an
    // `is copy` trait (both consumed and ignored: the binding is a plain writable
    // copy already), and DESTRUCTURING: `if $r.errors -> @ ($head, *@tail) { }`,
    // the shape TAP unpacks a result's parse errors with (issue #34). Only a
    // sub-signature needs real binding; a lone name keeps the string fast path.
    auto arrowBind = [this](std::string& into, std::vector<Param>& intoParams) {
        std::vector<Param> ps = parsePointyParams();
        bool anySub = false;
        for (auto& p : ps) anySub = anySub || (bool)p.subSig;
        if (anySub) intoParams = std::move(ps);
        else if (!ps.empty() && !ps[0].name.empty())
            into = (ps[0].slurpy ? "*" : "") + ps[0].name;
    };
    std::vector<Param> thenParams;
    if (matchOp("->")) arrowBind(s->thenVar, thenParams);
    auto blk = parseBlock();
    s->branches.emplace_back(std::move(cond), std::move(blk));
    s->branchVars.push_back(s->thenVar);
    s->branchParams.push_back(std::move(thenParams));
    if (isUnless && (isIdent("else") || isIdent("elsif") ||
                     isIdent("orwith") || isIdent("orwithout")))
        throw ParseError("\"unless\" does not take \"" + cur().text +
                         "\", please rewrite using \"if\"",
                         cur().line, "X::Syntax::UnlessElse", {{"keyword", cur().text}});
    while (isIdent("elsif")) {
        advance();
        ExprPtr c;
        { bool sv = stmtCond_; stmtCond_ = true; c = parseExpression(); stmtCond_ = sv; }
        std::string bv;
        std::vector<Param> bps;
        if (matchOp("->")) arrowBind(bv, bps); // elsif EXPR -> $x is copy / -> *@x / -> \x / -> ($a,$b)
        auto b = parseBlock();
        s->branches.emplace_back(std::move(c), std::move(b));
        s->branchVars.push_back(bv);
        s->branchParams.push_back(std::move(bps));
    }
    // `if A {…} orwith $x {…}` — with/without join the if-chain from EITHER end.
    // The `with A {…} orwith B {…}` direction was chained (parseStatement's
    // given/with branch); a chain that STARTS with if/elsif fell through here and
    // left the `orwith` standing as a separate statement, so it ran even when an
    // earlier branch had already been taken. That is how a TLS client re-ran its
    // handshake branch after every read: IO::Socket::Async::SSL dispatches on
    // `if !$!ssl {} elsif … {} orwith $!connected-promise {}`, and the second
    // `.keep` on an already-kept Promise threw X::Promise::Vowed.
    // Rewritten as `else { with B {…} }`, the same shape the other direction uses.
    if (isIdent("orwith") || isIdent("orwithout")) {
        auto blk = std::make_unique<Block>();
        blk->stmts.push_back(parseStatement());
        s->elseBlock = std::move(blk);
        return s;
    }
    if (isIdent("else")) {
        advance();
        if (isIdent("if")) // C-style `else if` — Raku spells it elsif
            throw ParseError("Please use 'elsif' instead of 'else if'",
                             cur().line, "X::Syntax::Malformed::Elsif", {});
        if (matchOp("->")) arrowBind(s->elseVar, s->elseParams); // else -> $x is copy / -> *@x / -> \x / -> ($a,$b)
        s->elseBlock = parseBlock();
    }
    return s;
}

StmtPtr Parser::parseWhile(bool isUntil) {
    auto s = std::make_unique<WhileStmt>();
    s->isUntil = isUntil;
    { bool sv = stmtCond_; stmtCond_ = true; s->cond = parseExpression(); stmtCond_ = sv; }
    if (matchOp("->")) {
        // The binder is the full pointy-signature grammar, as on a block: a plain
        // name, a sigilless `\r` (DBIish's StatementHandle is written that way), or
        // a DESTRUCTURING signature — `while $c.receive -> (:key($i), :value($r))`,
        // `-> @ ($head, *@tail)`. Only a sub-signature needs real binding.
        std::vector<Param> ps = parsePointyParams();
        bool anySub = false;
        for (auto& p : ps) anySub = anySub || (bool)p.subSig;
        if (anySub) s->params = std::move(ps);
        else if (!ps.empty() && !ps[0].name.empty()) s->var = ps[0].name;
    }
    s->body = parseBlock();
    return s;
}

// A pointy parameter carrying anything the plain-name path would DROP needs
// real signature binding. That path keeps only the NAME, so a type, a coercion,
// a `:D`/`:U` smiley or a `where` silently stopped existing: `for @f -> IO() $x`
// bound a Str and `.e` was then "no such method", and `for ("x",) -> Int $n`
// bound it without a murmur where Rakudo fails the type check. `is copy` was
// already fixed here once, one trait at a time; this asks the question once.
static bool pointyParamNeedsBinding(const Param& p) {
    return p.subSig || p.isCopy || p.coerce || !p.type.empty() ||
           p.defConstraint != 0 || p.whereExpr || p.hadWhere;
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
        bool needsBinding = false;
        for (auto& p : ps) { needsBinding = needsBinding || pointyParamNeedsBinding(p);
                             if (p.isRw) s->rwVars = true; }
        // The plain-name path keeps only the name, so anything else a parameter
        // carries has to send it to real binding — see pointyParamNeedsBinding.
        if (needsBinding) s->params = std::move(ps);
        else for (auto& p : ps) {
            s->vars.push_back(p.name);
            // `<->` makes EVERY parameter rw, and says so nowhere on the Params
            unsigned char tr = 0;
            if (p.isRw || doubly) tr |= ForStmt::VT_RW;
            if (p.isRaw)          tr |= ForStmt::VT_RAW;
            s->varTraits.push_back(tr);
        }
    }
    s->body = parseBlock();
    return s;
}

static std::unique_ptr<Block> wrapStmt(StmtPtr s) {
    auto b = std::make_unique<Block>();
    b->stmts.push_back(std::move(s));
    return b;
}

// Trailing statement modifiers on an EXPRESSION (inside `@(…)` / `$(…)` etc.):
// `@(EXPR for LIST)`, `@(EXPR if COND)`, … — chains, wrapping the value so far.
// Mirrors the desugars the plain-paren path uses (list-comprehension semantics).
ExprPtr Parser::applyExprModifiers(ExprPtr e) {
    for (;;) {
        // `(EXPR when X)` — EXPR if the topic smartmatches X, else nothing
        if (isIdent("when")) {
            advance();
            auto sm = std::make_unique<Binary>();
            sm->op = "~~";
            sm->lhs = std::make_unique<VarExpr>("$_");
            sm->rhs = parseExpression();
            auto tern = std::make_unique<Ternary>();
            tern->cond = std::move(sm);
            tern->then = std::move(e);
            tern->els = std::make_unique<NameTerm>("Empty");
            e = std::move(tern);
            continue;
        }
        if (isIdent("if") || isIdent("unless")) {
            bool neg = cur().text == "unless"; advance();
            ExprPtr cond = parseExpression();
            auto tern = std::make_unique<Ternary>();
            tern->cond = std::move(cond);
            ExprPtr empty = std::make_unique<NameTerm>("Empty"); // vanishes in list context
            if (neg) { tern->then = std::move(empty); tern->els = std::move(e); }
            else     { tern->then = std::move(e); tern->els = std::move(empty); }
            e = std::move(tern);
            continue;
        }
        // `with(EXPR)` GLUED to its paren is a CALL of a sub named `with` — Crane
        // takes `:&with!` and applies it so — never the topicalizer; Rakudo
        // says as much ("interpreted as a 'with()' function call")
        if ((isIdent("with") || isIdent("without")) &&
            !(peek().kind == Tok::LParen && !peek().spaceBefore)) {
            // `$(EXPR with X)` — a topicalizer, like the plain-paren path:
            // desugar to  do { with X { EXPR } }.
            std::string mod = advance().text;
            auto gs = std::make_unique<GivenStmt>();
            gs->topic = parseExpression();
            gs->defGuard = (mod == "with") ? 1 : 2;
            auto es = std::make_unique<ExprStmt>(); es->e = std::move(e);
            gs->body = std::make_unique<Block>();
            gs->body->stmts.push_back(std::move(es));
            auto be = std::make_unique<BlockExpr>(); be->body.push_back(std::move(gs));
            auto u = std::make_unique<Unary>(); u->op = "do"; u->operand = std::move(be);
            e = std::move(u);
            continue;
        }
        if (isIdent("for")) { // (EXPR for LIST) → map({ EXPR }, LIST)
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
        if (isIdent("while") || isIdent("until")) {
            bool untl = cur().text == "until"; advance();
            auto ws = std::make_unique<WhileStmt>();
            ws->cond = parseExpression(); ws->isUntil = untl; ws->asExpr = true;
            ws->body = std::make_unique<Block>();
            auto es = std::make_unique<ExprStmt>(); es->e = std::move(e);
            ws->body->stmts.push_back(std::move(es));
            auto be = std::make_unique<BlockExpr>(); be->body.push_back(std::move(ws));
            auto u = std::make_unique<Unary>(); u->op = "do"; u->operand = std::move(be);
            e = std::move(u);
            continue;
        }
        if (isIdent("given") && peek().kind != Tok::FatArrow && peek().kind != Tok::Comma) {
            advance();
            auto gs = std::make_unique<GivenStmt>();
            gs->topic = parseExpression();
            auto es = std::make_unique<ExprStmt>(); es->e = std::move(e);
            gs->body = std::make_unique<Block>();
            gs->body->stmts.push_back(std::move(es));
            auto be = std::make_unique<BlockExpr>(); be->body.push_back(std::move(gs));
            auto u = std::make_unique<Unary>(); u->op = "do"; u->operand = std::move(be);
            e = std::move(u);
            continue;
        }
        break;
    }
    return e;
}

// `do BLOCK` with a LOOP modifier is Perl 5's do-while, which Raku disallows
// (S04: "applying a statement modifier to a `do` block is specifically
// disallowed"). The trap is silent rather than cosmetic: a modifier `while` is
// a PRE-test, so `my $i = 5; do {...} while $i < 3` runs the body ZERO times
// where Perl 5 runs it once — the one author who reaches for this form is the
// one it burns. Rakudo raises X::Obsolete from the `statement modifier loop`
// branch of its grammar and only there, so a CONDITIONAL modifier in between
// (`do {...} if $a for @b`) parses the other branch and stays legal — hence
// the test that the statement is still a bare `do` ExprStmt, unwrapped. A
// blockless `do EXPR while …` is legal in both: its modifier binds INSIDE the
// `do`, so the loop never reaches here. A BARE block is what ends the inner
// statement and strands the modifier outside; a block that is really a TERM —
// `do -> $x {…} for @a`, `do <-> {…}`, `do sub {…}` — is an EXPR again, takes
// the modifier with it, and stays legal in Rakudo (it dies later, passing the
// topic to a block that did not ask for one, which is a runtime matter).
static bool isDoBlockExprStmt(const Stmt* s) {
    if (!s || s->kind != NK::ExprStmt) return false;
    const auto* es = static_cast<const ExprStmt*>(s);
    if (!es->e || es->e->kind != NK::Unary) return false;
    const auto* u = static_cast<const Unary*>(es->e.get());
    if (u->op != "do" || !u->operand || u->operand->kind != NK::BlockExpr) return false;
    const auto* be = static_cast<const BlockExpr*>(u->operand.get());
    return !be->isPointy && !be->isSub && !be->isMethodTerm;
}

StmtPtr Parser::applyModifiers(StmtPtr s) {
    // a BLOCK's `}` at end-of-line TERMINATES the statement (Rakudo's rule) — so
    // `x => {…}\n if COND {…}` starts a NEW if statement, while a modifier on
    // a continuation line after a non-brace token still attaches. Any RBrace
    // used to count, and a SUBSCRIPT's brace is not a block's: `return %h{$k}`
    // with its `if %h{$k}:exists` on the next line lost the modifier and the
    // `if` was read as a fresh block-if ("expected { (got ';')"). That one
    // line is CSS::Writer's, and it stopped four dists in the battery.
    // lastBlockClose_ is the same test the infix and method-call continuation
    // rules use; the statement being parsed must own the brace.
    // An unspace joins the lines: see the note in parseExpr.
    if (pos_ > 0 && pos_ - 1 == lastBlockClose_ && lastBlockClose_ >= stmtStart_ &&
        cur().line != toks_[pos_ - 1].line && cur().spaceBefore) return s;
    if (cur().kind == Tok::Ident) {
        const std::string& kw = cur().text;
        // A keyword is only a keyword when whitespace follows it (Rakudo's `kok`).
        // `say "OK" if+1` is a missing space, not a modifier, and Rakudo says so
        // rather than reading `+1` as the condition. The glued `(` is left alone:
        // Rakudo rejects `say "OK" if(1)` too — as a call to a routine named `if`
        // — but accepting it is the older, wider reading and plenty of code in
        // the wild is written that way.
        if (kStmtModifiers.count(kw) && !peek().spaceBefore &&
            peek().kind != Tok::LParen && startsTermToken(peek()))
            throw ParseError("Whitespace required after keyword '" + kw + "'",
                             cur().line, "X::Comp::AdHoc", {});
        // see isDoBlockExprStmt: `do {...} while/until/for/given` is Rakudo's
        // one obsolete-syntax error in this function; the loop modifiers are
        // exactly the four its grammar lists.
        if ((kw == "while" || kw == "until" || kw == "for" || kw == "given") &&
            stmtStart_ < toks_.size() && toks_[stmtStart_].kind == Tok::Ident &&
            toks_[stmtStart_].text == "do" && isDoBlockExprStmt(s.get())) {
            std::string old = "do..." + kw;
            std::string repl = "repeat...while or repeat...until";
            throw ParseError("Unsupported use of " + old + ". In Raku please use: " + repl,
                             cur().line, "X::Obsolete", {{"old", old}, {"replacement", repl}});
        }
        if (kw == "if" || kw == "unless") {
            advance();
            auto is = std::make_unique<IfStmt>();
            is->isUnless = (kw == "unless");
            is->modifier = true; // postfix form: a `my` in the modified STMT leaks to the enclosing scope
            ExprPtr cond = parseExpression();
            // `say 1 if 2 if 3 { … }` — only a LOOP modifier may follow a
            // conditional one; a second condition is a missing semicolon
            if (isKind(Tok::Ident) && (isIdent("if") || isIdent("unless") || isIdent("with") ||
                                       isIdent("without") || isIdent("when"))) {
                std::string pre, post;
                if (src_ && stmtStart_ < toks_.size()) {
                    const Token& st = toks_[stmtStart_];
                    const Token& ct = cur();
                    size_t a = st.off - st.text.size(), b = ct.off - ct.text.size();
                    if (a <= b && b <= src_->size()) {
                        pre = src_->substr(a, b - a);
                        size_t e = src_->find('\n', b);
                        post = src_->substr(b, (e == std::string::npos ? src_->size() : e) - b);
                    }
                }
                throw ParseError("Missing semicolon", cur().line, "X::Syntax::Confused",
                                 {{"reason", "Missing semicolon"}, {"pre", pre}, {"post", post}});
            }
            is->branches.emplace_back(std::move(cond), wrapStmt(std::move(s)));
            return applyModifiers(std::move(is)); // chained modifiers: `X if A for B`
        }
        if (kw == "while" || kw == "until") {
            advance();
            auto ws = std::make_unique<WhileStmt>();
            ws->isUntil = (kw == "until");
            ws->modifier = true; // postfix form: a `my` in STMT leaks to the enclosing scope
            ws->cond = parseExpression();
            ws->body = wrapStmt(std::move(s));
            return applyModifiers(std::move(ws));
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
                        bool anySub = false, needsBinding = false;
                        for (auto& p : be->params) { anySub = anySub || (bool)p.subSig;
                                                     needsBinding = needsBinding || pointyParamNeedsBinding(p); }
                        if (anySub) { fs->destructure = true; fs->params = std::move(be->params); }
                        else if (needsBinding) fs->params = std::move(be->params);
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
            return applyModifiers(std::move(g));
        }
        if (kw == "when") { // STMT when X  ==  if $_ ~~ X { STMT }
            advance();
            auto bin = std::make_unique<Binary>();
            bin->op = "~~"; bin->lhs = std::make_unique<VarExpr>("$_"); bin->rhs = parseExpression();
            auto is = std::make_unique<IfStmt>();
            is->modifier = true; // postfix form: a `my` in STMT leaks to the enclosing scope
            is->branches.emplace_back(std::move(bin), wrapStmt(std::move(s)));
            return applyModifiers(std::move(is)); // chained: `X when A for B`
        }
        if (kw == "with" || kw == "without") {
            // STMT with X : bind $_ = X, run only if X is defined (without: if undefined)
            advance();
            auto g = std::make_unique<GivenStmt>();
            g->modifier = true; // no implicit block: a `my` in STMT leaks out
            g->defGuard = (kw == "with") ? 1 : 2;
            g->topic = parseExpression();
            g->body = wrapStmt(std::move(s));
            return applyModifiers(std::move(g));
        }
    }
    return s;
}

// A statement that is ONLY the declaration of a `:D` variable leaves it with
// nothing to hold — `my Int:D $a;` is X::Syntax::Variable::MissingInitializer
// (an `is default(…)` supplies one).
StmtPtr Parser::parseStatement() {
    const int line = cur().line;
    StmtPtr st = parseStatementInner();
    if (st && st->kind == NK::ExprStmt) {
        Expr* e = static_cast<ExprStmt*>(st.get())->e.get();
        if (e && e->kind == NK::VarExpr) {
            auto* ve = static_cast<VarExpr*>(e);
            // `my \foo;` — a sigilless name is BOUND at its declaration, so
            // it needs something to bind
            if (ve->declare && !ve->name.empty() && !std::strchr("$@%&", ve->name[0]) &&
                (ve->declScope == "my" || ve->declScope == "our" || ve->declScope == "state"))
                throw ParseError("Term definition requires an initializer", line,
                                 "X::Syntax::Term::MissingInitializer", {});
            if (ve->declare && ve->declSmiley == 'D' && !ve->declDefault && !ve->declHasWhere && !ve->name.empty() &&
                ve->name[0] == '$') {
                const std::string ty = ve->declType + ":D";
                std::vector<std::pair<std::string, std::string>> at = {{"type", ty}};
                if (ve->declSmileyImplicit) at.push_back({"implicit", ":D by pragma"});
                throw ParseError("Variable definition of type " + ty +
                                 (ve->declSmileyImplicit ? " (implicit :D by pragma)" : "") +
                                 " requires an initializer", line,
                                 "X::Syntax::Variable::MissingInitializer", at);
            }
        }
    }
    return st;
}

StmtPtr Parser::parseStatementInner() {
    while (matchKind(Tok::Semicolon)) {}
    int stmtLine = cur().line;
    size_t savedStart = stmtStart_;
    stmtStart_ = pos_; // where the end-of-line `}` rule starts counting
    StmtPtr st = parseStatementImpl();
    stmtStart_ = savedStart;
    if (st && st->line == 0) st->line = stmtLine; // stamp the source line for diagnostics
    return st;
}

StmtPtr Parser::parseStatementImpl() {
    while (matchKind(Tok::Semicolon)) {}
    // `infix:(&)` — a colonpair on an operator category is an adverb on
    // nothing (and no label either)
    if (cur().kind == Tok::Ident &&
        (cur().text == "infix" || cur().text == "prefix" || cur().text == "postfix" ||
         cur().text == "circumfix" || cur().text == "postcircumfix" || cur().text == "term") &&
        peek().kind == Tok::Op && peek().text == ":" && !peek().spaceBefore &&
        !peek(2).spaceBefore && peek(2).kind == Tok::Op && peek(2).text.size() > 1 && peek(2).text[0] == '(') {
        std::string w = ":" + peek(2).text;
        throw ParseError("You can't adverb " + w, cur().line, "X::Syntax::Adverb", {{"what", w}});
    }
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
        // A keyword ENDS at a paren glued to it, so once this unit declares a
        // routine of that name the call is what is written: `sub loop {}; loop();`
        // is a call, not a malformed loop spec. Every branch below reads the
        // keyword, so the question has to be asked ahead of all of them.
        if (kwCallHere(kw)) {
            auto es = std::make_unique<ExprStmt>();
            es->e = parseExpression();
            return applyModifiers(std::move(es));
        }
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
                    throw ParseError("Cannot put adverbs on a typename when augmenting: found '" +
                                     adv + "'", cur().line, "X::Syntax::Augment::Adverb", {});
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
        // `unit class/role/grammar/monitor Foo;` — the rest of the file is the body.
        // `monitor` (OO::Monitors) parsed only in its BLOCK form, so a module
        // written as `unit monitor Foo;` — Terminal::ANSI::Virtual, and Cro's
        // internals — reached the attribute declarations with no package open
        // and died "You cannot declare an attribute here".
        if (kw == "unit" && peek().kind == Tok::Ident &&
            (peek().text == "class" || peek().text == "role" ||
             peek().text == "grammar" || peek().text == "monitor")) {
            advance(); // unit
            std::string what = advance().text; // class/role/grammar/monitor
            return parseClass(what == "role", what == "grammar", false, /*isUnit=*/true, what);
        }
        // …and the same for a module-supplied declarator: `unit model Foo;` is
        // how every one of Red's own models is written.
        if (kw == "unit" && peek().kind == Tok::Ident && userDeclarators_.count(peek().text)) {
            advance();                                  // unit
            std::string what = advance().text;          // the declarator
            StmtPtr decl = parseClass(false, false, false, /*isUnit=*/true, "class");
            static_cast<ClassDecl*>(decl.get())->howName = userDeclarators_[what];
            return decl;
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
                bool wasMy = (kw == "my");
                advance(); // strip scope/unit; re-dispatch on the declaration keyword
                bool savedUnit = unitDecl_; unitDecl_ = wasUnit;
                StmtPtr st = parseStatement();
                unitDecl_ = savedUnit;
                // `my class`/`my grammar` is lexically scoped — the runtime
                // redeclaration guard exempts it (a later EVAL may declare the
                // same name again, as Rakudo allows for my-scoped types)
                if (wasMy && st && st->kind == NK::ClassDecl)
                    static_cast<ClassDecl*>(st.get())->isMy = true;
                // `my constant X = …` stays lexical (a bare one is `our`)
                if (wasMy && st && st->kind == NK::ExprStmt) {
                    Expr* e = static_cast<ExprStmt*>(st.get())->e.get();
                    if (e && e->kind == NK::Assign) e = static_cast<Assign*>(e)->target.get();
                    if (e && e->kind == NK::VarExpr && static_cast<VarExpr*>(e)->declScope == "constant")
                        static_cast<VarExpr*>(e)->declMyConstant = true;
                }
                // `anon class C {…}` keeps the NAME C — `.^name` answers it —
                // but installs the symbol nowhere, so `C` afterwards is
                // undeclared, and so is a `C` inside the body.
                if (kw == "anon" && st && st->kind == NK::ClassDecl)
                    static_cast<ClassDecl*>(st.get())->isAnonDecl = true;
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
            // (`my T constant X` is lexical, as `my constant X` is)
            auto markMyConstant = [&](StmtPtr& st) {
                if (kw != "my" || !st || st->kind != NK::ExprStmt) return;
                Expr* e = static_cast<ExprStmt*>(st.get())->e.get();
                if (e && e->kind == NK::Assign) e = static_cast<Assign*>(e)->target.get();
                if (e && e->kind == NK::VarExpr && static_cast<VarExpr*>(e)->declScope == "constant")
                    static_cast<VarExpr*>(e)->declMyConstant = true;
            };
            if (peek().kind == Tok::Ident && peek(2).kind == Tok::Ident && declKw.count(peek(2).text)) {
                advance(); // scope
                std::string prefixType = advance().text;
                StmtPtr st = parseStatement();
                // `my Bool sub f` — the prefix type IS the return type, checked
                // like `--> Bool`; it used to be stripped and forgotten
                if (st && st->kind == NK::SubDecl) {
                    auto* sd = static_cast<SubDecl*>(st.get());
                    if (sd->retType.empty()) sd->retType = prefixType;
                    // `my Int sub f(--> Str)` — a second, different return type
                    else if (sd->retType != prefixType)
                        throw ParseError("Redeclaration of return type for '" + sd->name +
                                         "' (previous return type was " + prefixType + ")", t.line,
                                         "X::Redeclaration", {{"symbol", sd->name}, {"what", "return type for"}});
                }
                // `my RT114506 constant Ticket .= new(…)` — the prefix type is the
                // CONSTANT's type, which `.=` needs for its invocant
                else if (st && st->kind == NK::ExprStmt) {
                    Expr* e = static_cast<ExprStmt*>(st.get())->e.get();
                    if (e && e->kind == NK::MethodCall && static_cast<MethodCall*>(e)->mutate)
                        e = static_cast<MethodCall*>(e)->inv.get();
                    else if (e && e->kind == NK::Assign)
                        e = static_cast<Assign*>(e)->target.get();
                    if (e && e->kind == NK::VarExpr) {
                        auto* ve = static_cast<VarExpr*>(e);
                        if (ve->declare && ve->declScope == "constant" && ve->declType.empty())
                            ve->declType = prefixType;
                    }
                }
                markMyConstant(st);
                return st;
            }
            // `my Int Str $x` / `our Int Str sub f` — Rakudo parses a second
            // prefix type and refuses it as not yet implemented
            if (peek().kind == Tok::Ident && peek(2).kind == Tok::Ident &&
                knownTypeName(peek().text) && knownTypeName(peek(2).text) &&
                (peek(3).kind == Tok::Var || (peek(3).kind == Tok::Ident && declKw.count(peek(3).text))))
                throw ParseError("Multiple prefix constraints not yet implemented. Sorry.",
                                 t.line, "X::Comp::NYI",
                                 {{"feature", "Multiple prefix constraints"}});
            // typed scoped decl with a parameterized type: `my Foo::Bar[Ber::Meow] constant …`
            if (peek().kind == Tok::Ident && peek(2).kind == Tok::LBracket) {
                size_t save = pos_;
                advance(); // scope
                std::string prefixType = advance().text;   // `Foo::Bar[Ber::Meow]`, spelled out
                int d = 0;
                do {
                    if (isKind(Tok::LBracket)) d++; else if (isKind(Tok::RBracket)) d--;
                    prefixType += cur().text;
                    advance();
                } while (d > 0 && !isKind(Tok::End));
                if (isKind(Tok::Ident) && declKw.count(cur().text)) {
                    StmtPtr st = parseStatement();
                    if (st && st->kind == NK::ExprStmt) {
                        Expr* e = static_cast<ExprStmt*>(st.get())->e.get();
                        if (e && e->kind == NK::Assign) e = static_cast<Assign*>(e)->target.get();
                        if (e && e->kind == NK::VarExpr) {
                            auto* ve = static_cast<VarExpr*>(e);
                            if (ve->declare && ve->declScope == "constant" && ve->declType.empty())
                                ve->declType = prefixType;
                        }
                    }
                    markMyConstant(st);
                    return st;
                }
                pos_ = save; // not a typed scoped decl — restore
            }
        }
        if (kw == "method" || kw == "submethod") {
            bool sub = (kw == "submethod");
            advance();
            auto st = parseSub(false);
            // a statement-level `my method m(Int:D: $x) {…}` is still a METHOD: it
            // binds `self` from the invocant and answers Method to `.^name`. The
            // flag was only ever set on class-body declarations.
            if (st && st->kind == NK::SubDecl) {
                static_cast<SubDecl*>(st.get())->isMethod = true;
                static_cast<SubDecl*>(st.get())->isSubmethod = sub;
            }
            return st;
        }
        // `token`/`rule`/`regex` introduce a named regex only when a NAME or a
        // body follows. `rule('a')` and `token + 1` are ordinary code calling a
        // sub of that name — and were being swallowed as malformed declarations,
        // silently, with the call never happening.
        if ((kw == "token" || kw == "rule" || kw == "regex") &&
            (peek().kind == Tok::Ident || peek().kind == Tok::Var ||
             peek().kind == Tok::RegexLit || peek().kind == Tok::LBrace)) {
            std::string knd = advance().text;
            auto nr = std::make_unique<NamedRegexDecl>();
            nr->kind = knd;
            if (isKind(Tok::Ident) || isKind(Tok::Var)) nr->name = advance().text; // name
            if (nr->name.empty() && isKind(Tok::RegexLit)) {
                // anonymous `regex {…}` at statement level (e.g. as an EVAL's
                // last statement): a first-class Regex VALUE, not a declaration
                auto rl = std::make_unique<RegexLit>(advance().text);
                rl->declKind = knd;
                rl->isRx = true;
                auto es = std::make_unique<ExprStmt>();
                es->e = std::move(rl);
                return es;
            }
            // traits between the name and the body — `my token stamp is export
            // {…}`. Unconsumed, the RegexLit check below failed and the
            // declaration fell to the brace-skipping fallback with an EMPTY
            // pattern: the name registered, every `<stamp>` resolved to it, and
            // matched the empty string — inside the declaring module as much as
            // in the importer. Apache::LogFormat's test library exports its
            // `<timefmt>` exactly this way.
            while (isIdent("is") && (peek().kind == Tok::Ident || peek().kind == Tok::Var)) {
                advance(); advance();                      // is NAME
                if (isKind(Tok::LParen)) {                 // is foo(...)
                    int d = 0;
                    do { if (isKind(Tok::LParen)) d++; else if (isKind(Tok::RParen)) d--; advance(); }
                    while (d > 0 && !isKind(Tok::End));
                }
                else if (isKind(Tok::QwList) && !cur().spaceBefore) advance();   // is foo<a b>
                else if (isOp("<") && !cur().spaceBefore) { advance(); readAngleWords(">"); }
            }
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
            if (isKind(Tok::Ident)) {
                auto u = std::make_unique<UseStmt>();
                u->isRequire = true;
                u->module = advance().text;
                // Skip the name adverbs (`:ver(v0.3.3+)`, `:auth<…>`) and any
                // `<…>` import list — both accepted and ignored, since
                // loadModule publishes the module's subs globally. STOP at a
                // block close or comma, not just `;`/End: `lives-ok { require
                // CSS::Grammar:ver(v0.3.3+) }, "…"` has no semicolon, and eating
                // to End swallowed the block's `}` (the CSS cluster: CSS::Module,
                // ::CSS3::Selectors, ::Specification). Balanced (…) / […] keep
                // an inner `}`/`,` (a version range's parens) from ending it.
                // `require Stub:file($path)` — the one adverb that is not ignored
                if (isOp(":") && !cur().spaceBefore && peek().kind == Tok::Ident &&
                    peek().text == "file" && peek(2).kind == Tok::LParen) {
                    advance(); advance(); advance(); // : file (
                    u->fileExpr = parseExpression();
                    expectKind(Tok::RParen, ")");
                }
                int depth = 0;
                while (!isKind(Tok::End)) {
                    if (depth == 0 && (isKind(Tok::Semicolon) || isKind(Tok::RBrace) ||
                                       isKind(Tok::Comma) || isKind(Tok::RParen))) break;
                    if (isKind(Tok::LParen) || isKind(Tok::LBracket)) depth++;
                    else if (isKind(Tok::RParen) || isKind(Tok::RBracket)) depth--;
                    advance();
                }
                matchKind(Tok::Semicolon);
                return u;
            }
            // dynamic name: `require ::($name)` — evaluate at runtime, load, and
            // yield the loaded type (zef's plugin loader). Expression form.
            auto u = std::make_unique<Unary>();
            u->op = "require";
            // TIGHTER than a comparison: what follows `require` is a NAME, and
            // an import list written with a space before it — `require
            // ::('Data::Dump::Tree') <&ddt>` — otherwise reads as `<` the
            // less-than operator and swallows the rest of the statement. The
            // named form never met this because it never parses an expression.
            u->operand = parseExpr(BP_COMPARE + 1);
            // …and the SAME optional import list the named form above accepts:
            // `try require ::('Data::Dump::Tree') <&ddt>` is how a module makes
            // a dependency optional, and only the named form took it, so the
            // symbolic one died on the `<…>` (RakuDoc::Templates).
            // Syntactically accepted and ignored, exactly as above — loadModule
            // publishes the module's subs globally either way.
            {
                int depth = 0;
                while (!isKind(Tok::End)) {
                    if (depth == 0 && (isKind(Tok::Semicolon) || isKind(Tok::RBrace) ||
                                       isKind(Tok::Comma) || isKind(Tok::RParen))) break;
                    if (isKind(Tok::LParen) || isKind(Tok::LBracket)) depth++;
                    else if (isKind(Tok::RParen) || isKind(Tok::RBracket)) depth--;
                    advance();
                }
            }
            auto es = std::make_unique<ExprStmt>();
            es->e = std::move(u);
            matchKind(Tok::Semicolon);
            return es;
        }
        // `import Foo;` — the package is already declared (or already loaded);
        // this only brings its exported routines into THIS scope. Rakudo
        // refuses the bare name without it, so it is not decoration:
        // L10N::ZH's `11-use-import` is the one test in the corpus that writes
        // it, and Rakudo runs it.
        if (kw == "import" && peek().kind == Tok::Ident) {
            advance();
            auto u = std::make_unique<UseStmt>();
            u->isImport = true;
            u->module = advance().text;
            while (isKind(Tok::Op) && cur().text == "::") { advance(); u->module += "::" + advance().text; }
            // …and the import list, which for `import` is the whole point of the
            // statement: `need Math::Trig; import Math::Trig :radial;` is how a
            // module's SELECTIVE exports are asked for after a load that took
            // none. The list used to be skipped, so the tag never reached the
            // module and six subs stayed unimported.
            while (!isKind(Tok::End) && !isKind(Tok::Semicolon) && !isKind(Tok::RBrace)) {
                if (isOp(":") && peek().kind == Tok::Var && peek().text.size() > 1 &&
                    std::strchr("&$@%", peek().text[0])) {      // :&name
                    advance();
                    u->importArgs.push_back(advance().text);
                    continue;
                }
                if (isOp(":") && peek().kind == Tok::Ident) {   // :tag
                    advance();
                    std::string tag = advance().text;
                    bool valued = (isOp("<") && !cur().spaceBefore) ||
                                  (isKind(Tok::QwList) && !cur().spaceBefore) ||
                                  (isKind(Tok::LParen) && !cur().spaceBefore);
                    if (!valued) u->importArgs.push_back(tag);
                    continue;
                }
                if (isKind(Tok::QwList)) {                      // <a b>
                    std::string w;
                    for (char c : cur().text) {
                        if (c == ' ' || c == '\t') { if (!w.empty()) u->importArgs.push_back(w); w.clear(); }
                        else w += c;
                    }
                    if (!w.empty()) u->importArgs.push_back(w);
                    advance();
                    continue;
                }
                advance();
            }
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
            // `use isms <Perl5>` lifts the Perl 5 brainos for the rest of the unit,
            // so `$a =~ $a` there is the `= ~` it reads as in Perl 5 rather than a
            // "please use ~~" error. (Rakudo scopes the pragma to its block; the
            // pragma is rare enough that unit scope is the same thing in practice.)
            if (!u->isNo && isKind(Tok::Ident) && cur().text == "isms") ismsPerl5_ = true;
            if (isKind(Tok::VersionLit)) { // `use v6;` / `use v6.d;` / `use v6.e.PREVIEW;`
                { std::string ver = advance().text; // VersionLit text is like "6.e" (no leading v)
                  // swallow any dotted tail the version lexer didn't take (.PREVIEW)
                  while (!isKind(Tok::Semicolon) && !isKind(Tok::End)) ver += advance().text;
                  u->module = (ver.empty() || ver[0] != 'v') ? "v" + ver : ver; // exec() reads langRev from this
                  // …and the parser reads it now, for syntax that is 6.e-only.
                  if (u->module.find("6.c") != std::string::npos) langRev_ = 0;
                  else if (u->module.find("6.d") != std::string::npos) langRev_ = 1;
                  else if (u->module.find('.') == std::string::npos) langRev_ = 1; // bare `v6`
                  else langRev_ = 2; }
                matchKind(Tok::Semicolon);
                return u; // a version pragma loads no module — exec() only reads langRev from u->module
            }
            if (!isKind(Tok::Semicolon) && !isKind(Tok::End)) u->module = advance().text;
            // `use variables :D` / `:U` / `:_` — the default type smiley for
            // typed declarations in the rest of the enclosing block
            if (u->module == "variables") {
                const int ln = cur().line;
                if (u->isNo)
                    throw ParseError("Cannot use 'no' with pragma 'variables'", ln,
                                     "X::Pragma::CannotWhat", {{"what", "no"}, {"name", "variables"}});
                std::vector<std::string> smileys;
                while (!isKind(Tok::Semicolon) && !isKind(Tok::End) && !isKind(Tok::RBrace)) {
                    if (isOp(":") && peek().kind == Tok::Ident) {
                        advance();
                        const std::string sm = advance().text;
                        if (sm != "D" && sm != "U" && sm != "_")
                            throw ParseError("Invalid type smiley ':" + sm + "' used, only ':D', ':U' and ':_' are allowed",
                                             ln, "X::InvalidTypeSmiley", {{"name", sm}});
                        smileys.push_back(sm);
                    }
                    else if (isKind(Tok::StrLit) || isKind(Tok::StrInterp) || isKind(Tok::Ident))
                        throw ParseError("Don't know how to handle '" + cur().text + "' with pragma 'variables'", ln,
                                         "X::Pragma::UnknownArg", {{"name", "variables"}, {"arg", cur().text}});
                    else advance();
                }
                if (smileys.empty())
                    throw ParseError("'variables' pragma expects one parameter out of :D, :U, :_", ln,
                                     "X::Pragma::MustOneOf", {{"name", "variables"}});
                if (smileys.size() > 1)
                    throw ParseError("The 'variables' pragma takes only one argument", ln,
                                     "X::Pragma::OnlyOne", {{"name", "variables"}});
                varsPragma_ = smileys[0][0];
                matchKind(Tok::Semicolon);
                return u;
            }
            // An import brings type names this unit never spells; the
            // declaration-type check stands down for such a unit (Program::
            // importsModules).
            if (!u->isNo && !u->module.empty() && !isPragmaName(u->module))
                importsModules_ = true;
            // name adverbs on the USE: `use JSON::Class:ver<0.0.14+>` — capture
            // the version constraint so the loader can SKIP too-old candidates
            // (License::SPDX needs 0.0.14+; the vendored battery copy is 0.0.6,
            // and taking it broke the whole Test::META chain). :auth/:api are
            // read and dropped, as the module-decl parser does.
            while (isOp(":") && !cur().spaceBefore && peek().kind == Tok::Ident) {
                advance();                          // :
                std::string adv = advance().text;   // ver / auth / api / if
                // `use Foo:if(EXPR)` — the ecosystem `if` dist's conditional
                // load. The condition is a real EXPRESSION (typically probing
                // $*RAKU.version or $*KERNEL), so it cannot ride the textual
                // value capture below; exec() evaluates it and skips the load
                // outright when it is false.
                if (adv == "if" && isKind(Tok::LParen) && !cur().spaceBefore) {
                    advance();
                    u->ifCond = parseExpression();
                    if (isKind(Tok::RParen)) advance();
                    continue;
                }
                std::string val;
                if (isKind(Tok::QwList) && !cur().spaceBefore) val = advance().text;
                else if (isOp("<") && !cur().spaceBefore) {
                    advance();
                    auto ws = readAngleWords(">");
                    val = ws.empty() ? std::string() : ws[0];
                }
                else if (isKind(Tok::LParen) && !cur().spaceBefore) {
                    // the PAREN spelling: `use JSON::Class:ver(v0.0.20+);` — a
                    // version literal or string, not an angle list (META6 uses
                    // this form; missing it silently loaded ANY version)
                    advance();
                    while (!isKind(Tok::RParen) && !isKind(Tok::End)) val += advance().text;
                    if (isKind(Tok::RParen)) advance();
                    if (!val.empty() && val[0] == 'v') val = val.substr(1);
                    if (val.size() > 1 && (val[0] == '"' || val[0] == '\'')) val = val.substr(1, val.size() - 2);
                    // an EXPRESSION in the parens — zef pins every sibling with
                    // `:ver($?DISTRIBUTION.meta<version> // '*')` — cannot be
                    // matched textually: the raw source text used to become the
                    // requirement and reject every candidate ("1.1.3 !~
                    // $?DISTRIBUTION.meta<version>//…", issue #37). Treat a
                    // non-literal as UNCONSTRAINED, the same behaviour every
                    // non-literal spelling had before the paren form existed.
                    bool literalish = !val.empty();
                    for (char c : val)
                        if (!(ascii::isalnum((unsigned char)c) || c == '.' || c == '*' ||
                              c == '+' || c == '-')) { literalish = false; break; }
                    if (!literalish) val.clear();
                }
                if (adv == "ver") u->verReq = val;
                else if (adv == "from") u->fromLang = val;   // `use NQPHLL:from<NQP>`: a foreign load, never searched for here
                // `use experimental:rakuast` — the TIGHT spelling of a pragma
                // argument lands in THIS loop (the spaced `use experimental
                // :rakuast` is caught by the `:tag` capture further down), and
                // only `ver` was ever kept, so the argument was dropped
                // silently. Valued forms (`:x<1>`) are not a plain request, the
                // same rule the spaced capture applies.
                else if (u->module == "experimental" && val.empty())
                    u->importArgs.push_back(adv);
            }
            // `use Mod ()` — an explicit EMPTY import list loads the module and
            // imports NOTHING. The parens used to fall through to the sub-EXPORT
            // expression branch below, which parsed them as an empty list and
            // left the default import running: `use P5index ()` still installed
            // &index over the built-in one.
            if (!u->isNo && isKind(Tok::LParen) && peek().kind == Tok::RParen) {
                advance(); advance();
                u->emptyImport = true;
            }
            if (!u->isNo && u->fromLang.empty()) scanModuleOps(u->module); // its operators must parse HERE
            if (!u->isNo && u->module.compare(0, 6, "MONKEY") == 0)
                monkeyScopes_.back() = 1; // use MONKEY-TYPING / use MONKEY (lexical)
            // `use lib` takes an expression unless it is the plain one-string form,
            // whose path is kept as text (the native backends read it there). A
            // COMMA LIST is not that form: `use lib 'lib', 't/lib'` kept the first
            // path and silently dropped the rest.
            if (u->module == "lib" && !isKind(Tok::Semicolon) && !isKind(Tok::End) &&
                (!(isKind(Tok::StrLit) || isKind(Tok::StrInterp)) ||
                 peek().kind == Tok::Comma)) {
                u->argExpr = parseExpression(); // `use lib $?FILE.IO.parent`
            } else {
                // capture first string argument, e.g. `use lib 'lib'`
                while (!isKind(Tok::Semicolon) && !isKind(Tok::End)) {
                    // `use Mod <a b>` — a bare angle list is lexed as words (see
                    // angleTermContext), so read it here as the EXPORT arguments
                    if (isKind(Tok::Op) && cur().text == "<") {
                        advance();
                        for (auto& w : readAngleWords(">")) u->importArgs.push_back(w);
                        continue;
                    }
                    if (isKind(Tok::QwList)) { // `use Mod <immutable !pretty>` — EXPORT args
                        std::string ws = cur().text, w;
                        for (char c : ws) {
                            if (c == ' ' || c == '\t') { if (!w.empty()) u->importArgs.push_back(w); w.clear(); }
                            else w += c;
                        }
                        if (!w.empty()) u->importArgs.push_back(w);
                    }
                    // `use Mod :tag` — a colonpair selects an export tag; capture the
                    // NAME as an import argument (`use Prompt :prompt`). `:!tag` and a
                    // valued `:tag<v>` are consumed but not treated as a plain request.
                    // …and the SYMBOL spelling `:&name` / `:$name`, where the sigil
                    // is part of the request rather than of the tag. PDF::COS::Dict
                    // imports its AST helpers as `use PDF::COS::Util :&from-ast,
                    // :&ast-coerce` — neither looked like a tag here, so both fell
                    // to the expression branch and never arrived.
                    if (isOp(":") && peek().kind == Tok::Var && peek().text.size() > 1 &&
                        std::strchr("&$@%", peek().text[0])) {
                        advance();                       // :
                        u->importArgs.push_back(advance().text);   // &name
                        continue;
                    }
                    if (isOp(":") && peek().kind == Tok::Ident) {
                        advance();                       // :
                        std::string tag = advance().text;
                        bool valued = (isOp("<") && !cur().spaceBefore) ||
                                      (isKind(Tok::QwList) && !cur().spaceBefore) ||
                                      (isKind(Tok::LParen) && !cur().spaceBefore);
                        if (!valued) u->importArgs.push_back(tag);
                        continue;                        // already advanced past the pair
                    }
                    // Anything else is an EXPRESSION list for sub EXPORT:
                    // `use META::constants $?DISTRIBUTION`, `use CLI::Version
                    // $?DISTRIBUTION, &MAIN, 'long'`, and the suite's
                    // `use CLI::Version Distribution, proto sub MAIN(|) {*}`. The
                    // tokens used to be skipped one by one, so EXPORT ran with no
                    // arguments — and a `use` that is the last statement of a
                    // block lost the block's closing brace to the skip.
                    if (!(isKind(Tok::StrLit) || isKind(Tok::StrInterp) || isKind(Tok::QwList)) &&
                        !(isOp(":") && peek().kind == Tok::Ident) && !isOp("<") &&
                        startsTermToken(cur())) {
                        u->argExpr = parseExpression();
                        break;
                    }
                    if (isKind(Tok::RBrace)) break;   // `{ use Mod args }` — the block's own brace
                    if ((isKind(Tok::StrLit) || isKind(Tok::StrInterp)) && u->arg.empty()) u->arg = cur().text;
                    // `use Mod "use-args"` — a STRING argument reaches sub EXPORT
                    // exactly like the angle form (`use lib 'x'` keeps u->arg only)
                    if ((isKind(Tok::StrLit) || isKind(Tok::StrInterp)) && u->module != "lib")
                        u->importArgs.push_back(cur().text);
                    advance();
                }
            }
            // The path this statement adds has to be on the search path for the
            // REST of this parse: a `use` below it names a module that lives there,
            // and its operators are harvested now (see staticUsePath).
            if (u->module == "lib" && !u->isNo) {
                std::vector<std::string> paths;
                if (!u->arg.empty()) paths.push_back(u->arg);
                else if (u->argExpr) staticUsePaths(u->argExpr.get(), srcFile_, paths);
                for (auto& path : paths)
                    if (!path.empty()) libPaths_.insert(libPaths_.begin(), path);
            }
            // `use experimental :rakuast` — both spellings land in importArgs by
            // now (the tight one above, the spaced one in the :tag capture), and
            // the flag has to be on the PROGRAM: a module's subs are hoisted
            // before its mainline runs, so the pragma statement would execute
            // too late to govern the routines it is written for.
            if (u->module == "experimental" && !u->isNo)
                for (auto& tag : u->importArgs)
                    if (tag == "rakuast") usesRakuAst_ = true;
            // SLANG-PLAN §A: the module registers a slang. Run it now, and read the
            // rest of this unit through what it registered. (`need` and `require`
            // run no EXPORT, so they activate nothing — as in Rakudo.)
            if (lastScanSlang_ && !u->isNo && !u->isNeed && !u->isRequire && !u->isImport) {
                matchKind(Tok::Semicolon);
                activateSlang(u->module);
                return u;
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
        if (kw == "sub") {
            // `sub` with nothing after it — no name, no signature, no block
            if (peek().kind == Tok::RBrace || peek().kind == Tok::Semicolon || peek().kind == Tok::End)
                throw ParseError("Missing block", cur().line, "X::Syntax::Missing", {{"what", "block"}});
            advance();
            StmtPtr d = parseSub(false);
            // A routine declaration is a TERM in Raku, so a comma strings several
            // into one statement — PDF::Content::PageTree declares the FETCH and
            // STORE of a Proxy as `sub FETCH($) {…}, sub STORE($, $_) {…}`. The
            // list's value is discarded; what matters is that both names are
            // declared, so the rest ride along with this statement.
            while (isKind(Tok::Comma) && peek().kind == Tok::Ident && peek().text == "sub") {
                advance(); advance(); // the comma and `sub`
                pendingStmts_.push_back(parseSub(false));
            }
            return d;
        }
        // …and the same declarator at STATEMENT level: `anon sub foo {…}` is a
        // routine nothing can name afterwards. Recognised only where a
        // declaration can follow, so a sub or variable called `anon` still
        // parses as a call.
        if (kw == "anon" && peek().kind == Tok::Ident &&
            (peek().text == "sub" || peek().text == "method" || peek().text == "submethod" ||
             peek().text == "class" || peek().text == "role" || peek().text == "grammar" ||
             peek().text == "package" || peek().text == "module" ||
             peek().text == "token" || peek().text == "rule" || peek().text == "regex" ||
             peek().text == "multi" || peek().text == "state" || peek().text == "my")) {
            advance();
            auto es = std::make_unique<ExprStmt>();
            es->e = parseExpression();
            markAnonDecl(es->e.get());
            return applyModifiers(std::move(es));
        }
        // `only` is the third multiness declarator — it says this routine has
        // exactly one candidate, which is what a plain `sub` already is here.
        // Recognised only where a routine can actually follow, so a sub or
        // variable NAMED `only` still parses as a call.
        if (kw == "only" &&
            (peek().text == "sub" || peek().text == "method" || peek().text == "submethod" ||
             (peek(1).kind == Tok::Ident &&
              (peek(2).kind == Tok::LParen || peek(2).kind == Tok::LBrace)))) {
            advance();
            bool isM = false;
            if (isIdent("method") || isIdent("submethod")) { isM = true; advance(); }
            else if (isIdent("sub")) advance();
            auto st = parseSub(false, false, isM);
            if (st && st->kind == NK::SubDecl) {
                // `only sub {}` — a multiness declarator needs something to name
                if (static_cast<SubDecl*>(st.get())->name.empty())
                    throw ParseError("Cannot put only on anonymous routine", t.line,
                                     "X::Anon::Multi",
                                     {{"multiness", "only"},
                                      {"routine-type", isM ? "method" : "sub"}});
                static_cast<SubDecl*>(st.get())->isMethod = isM;
            }
            return st;
        }
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
                // `loop (a, b, c)` / `loop (a; b, c)`: fewer than three parts
                auto partsGot = [&](const char* n) {
                    std::string w = std::string("loop spec (expected 3 parts separated by semicolon, got ") + n + ")";
                    throw ParseError("Malformed " + w, cur().line, "X::Syntax::Malformed", {{"what", w}});
                };
                if (!isKind(Tok::Semicolon)) ls->init = parseExpression();
                if (isKind(Tok::RParen)) partsGot("1");
                expectKind(Tok::Semicolon, ";");
                if (!isKind(Tok::Semicolon)) ls->cond = parseExpression();
                if (isKind(Tok::RParen)) partsGot("2");
                expectKind(Tok::Semicolon, ";");
                if (!isKind(Tok::RParen)) ls->incr = parseExpression();
                if (isKind(Tok::Semicolon) && peek().kind != Tok::RParen) partsGot("more");
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
        if ((kw == "given" || kw == "with" || kw == "without" || kw == "orwith" || kw == "orwithout") &&
            // `with(EXPR)` glued to its paren is a CALL of a sub named `with`
            // (Crane applies its `:&with!` parameter so); the statement form
            // always has whitespace — Rakudo insists on it
            !((kw == "with" || kw == "without") && peek().kind == Tok::LParen && !peek().spaceBefore)) {
            bool isWith = (kw == "with" || kw == "orwith");
            bool isWithout = (kw == "without" || kw == "orwithout");
            advance();
            auto g = std::make_unique<GivenStmt>();
            g->defGuard = isWith ? 1 : isWithout ? 2 : 0;
            { bool sv = stmtCond_; stmtCond_ = true; g->topic = parseExpression(); stmtCond_ = sv; }
            if (isOp("->") || isOp("<->")) { // `given X -> $y is copy { }` / `-> ($a, $b)`
                advance();
                auto ps = parsePointyParams();
                bool anySub = false;
                for (auto& p : ps) anySub = anySub || (bool)p.subSig;
                if (anySub) g->params = std::move(ps); // destructuring binder — bindParams
                else if (!ps.empty() && !ps[0].name.empty()) g->var = ps[0].name; // "$_" too: marks an explicit binder
            }
            g->body = parseBlock();
            // orwith/orwithout chain:  with A {} orwith B {}  ==  with A {} else { with B {} }
            if (g->defGuard && (isIdent("orwith") || isIdent("orwithout"))) {
                auto blk = std::make_unique<Block>();
                blk->stmts.push_back(parseStatement());
                g->hasElse = true; g->elseBody = std::move(blk);
                return g;
            }
            // `with A {…} elsif B {…} else {…}` — with/without take part in the
            // if-chain, so an elsif after the body continues it. Rewritten as
            // `else { if B {…} else {…} }`, the same shape as the orwith chain
            // above. IO::Glob picks its grammar with exactly this.
            if (g->defGuard && isIdent("elsif")) {
                advance();
                auto blk = std::make_unique<Block>();
                blk->stmts.push_back(parseIf(false));
                g->hasElse = true; g->elseBody = std::move(blk);
                return g;
            }
            g->hasElse = (g->defGuard && isIdent("else"));
            if (g->hasElse) {
                advance();
                if (isOp("->") || isOp("<->")) { // `else -> $pos { }` binds the topic
                    advance();
                    auto ps = parsePointyParams();
                    bool anySub = false;
                    for (auto& p : ps) anySub = anySub || (bool)p.subSig;
                    if (anySub) g->elseParams = std::move(ps);
                    else if (!ps.empty() && !ps[0].name.empty()) g->elseVar = ps[0].name;
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
            if (r->isRw) sawReturnRw_ = true; // …and the routine reading this body returns a container
            if (!isKind(Tok::Semicolon) && !isKind(Tok::End) && !isKind(Tok::RBrace) &&
                cur().kind != Tok::Ident) {
                r->value = parseExpression();
            // `return without $path;` — a bare return under a MODIFIER, not a
            // return whose value is a call to `without`. `with`/`without` start
            // a term elsewhere, so they are deliberately absent from
            // kBlockKeywords and need the modifier list here, exactly as the
            // loop controls already do for `next without $x`.
            } else if ((startsTermToken(cur()) && !kBlockKeywords.count(cur().text) &&
                        !kStmtModifiers.count(cur().text)) ||
                       isIdent("sub") || isIdent("method") || isIdent("do") || isIdent("start") ||
                       ((isIdent("role") || isIdent("class") || isIdent("grammar")) &&
                        (peek().kind == Tok::LBrace || (peek().kind == Tok::Op && peek().text == "::")))) {
                r->value = parseExpression();
            }
            return applyModifiers(std::move(r));
        }
        if ((kw == "last" || kw == "next" || kw == "redo") &&
            peek().kind != Tok::LParen) {
            // `next()` (call form) falls through to EXPRESSION parsing instead:
            // it composes into a larger expression whose modifiers must gate it —
            // zef's `next() R, DEBUG(...) if COND` was swallowing everything after
            // `next` here and skipping unconditionally.
            advance();
            // optional loop label:  `last OUTER`
            std::string tgt;
            if (cur().kind == Tok::Ident && !kBlockKeywords.count(cur().text) &&
                !kStmtModifiers.count(cur().text) &&
                peek().kind != Tok::Op) { tgt = cur().text; advance(); }
            StmtPtr cs;
            if (kw == "last") { auto c = std::make_unique<LastStmt>(); c->target = tgt; cs = std::move(c); }
            else if (kw == "next") { auto c = std::make_unique<NextStmt>(); c->target = tgt; cs = std::move(c); }
            else { auto c = std::make_unique<RedoStmt>(); c->target = tgt; cs = std::move(c); }
            return applyModifiers(std::move(cs));
        }
        // A module-supplied package declarator (`model Foo { … }`) declares a
        // class like any other; only its metaobject differs. Guarded to a shape
        // that can only be a declaration — a NAME, an anonymous `::`, or a bare
        // block — so a sub or a method of the same name is untouched.
        if (userDeclarators_.count(kw) &&
            (peek().kind == Tok::Ident || peek().kind == Tok::LBrace ||
             (peek().kind == Tok::Op && peek().text == "::"))) {
            advance();
            StmtPtr decl = parseClass(false, false, false, false, "class");
            static_cast<ClassDecl*>(decl.get())->howName = userDeclarators_[kw];
            return decl;
        }
        // …and the built-in package declarators, under the same guard: a
        // declaration needs a NAME, an anonymous `::`, or a bare block after the
        // keyword. Unguarded, a sigilless TERM of that name was read as a
        // declaration of an anonymous package and its `.method(…)` applied to
        // THAT — `method compose(Mu \package) { package.^add_method(…) }`
        // (PDF::Content::Ops' graphics-attribute trait) added the method to a
        // package nothing else could see, and said nothing.
        if ((kw == "class" || kw == "role" || kw == "monitor" ||
             kw == "grammar" || kw == "module" || kw == "package") &&
            (peek().kind == Tok::Ident || peek().kind == Tok::LBrace ||
             (peek().kind == Tok::Op && peek().text == "::"))) {
            advance();
            StmtPtr decl = parseClass(kw == "role", kw == "grammar",
                                      kw == "module" || kw == "package",
                                      false, kw);
            // `class Foo {…}.new(…)` as a statement (zef's config object is exactly
            // this as a sub's last statement): the type is a TERM with a postfix.
            // Wrap the decl in `do { … }` (evals to the type object) and apply it.
            if (isOp(".") && !cur().spaceBefore) {
                auto be = std::make_unique<BlockExpr>();
                be->body.push_back(std::move(decl));
                auto u = std::make_unique<Unary>(); u->op = "do"; u->operand = std::move(be);
                auto es = std::make_unique<ExprStmt>();
                es->e = parsePostfix(std::move(u), true);
                return es;
            }
            return decl;
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
        if ((kw == "BEGIN" || kw == "END" || kw == "INIT" || kw == "CHECK" ||
             kw == "ENTER" || kw == "LEAVE" || kw == "FIRST" || kw == "NEXT" ||
             kw == "LAST" || kw == "KEEP" || kw == "UNDO" || kw == "PRE" ||
             kw == "POST" || kw == "DOC" || kw == "QUIT" || kw == "CLOSE") &&
            // a TIGHT paren is CALL syntax, not a phaser body:
            // `POST($uri, content => %form)` inside HTTP::Request::Common is a
            // recursive call to the user's `sub POST` — it parsed as the POST
            // phaser over a parenthesized list, which then ECHOED the arguments
            // as the candidate's return value
            !(peek().kind == Tok::LParen && !peek().spaceBefore)) {
            advance();
            auto b = std::make_unique<Block>();
            b->phaser = kw; // run-timing handled by the interpreter
            b->srcPos = (int)pos_;                 // source order, finer than the line
            if (kw == "END") sawEndPhaser_ = true; // the unit needs the END walk
            if (isKind(Tok::LBrace)) { auto blk = parseBlock(); b->stmts = std::move(blk->stmts); }
            else { b->stmts.push_back(parseStatement()); b->stmtForm = true; } // PHASER statement; — declarations belong to the enclosing scope
            return b;
        }
    }

    if (t.kind == Tok::LBrace) {
        // A statement-leading {...} is a hash literal if it starts with a Pair
        // (key=>/:key) or a %-var; otherwise it's a block. (Empty {} stays a block.)
        bool looksHash = braceLooksHash(/*emptyIsHash=*/false);
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
                        // `{ 1,2 },Nil` — a comma on the same line makes the
                        // block one element of a list
                        if (nx.kind == Tok::Comma && nx.line == toks_[i].line) exprBlock = true;
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
void Parser::checkRedeclarations(const std::vector<StmtPtr>& stmts, bool unitScope) {
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
                // …but a `constant &infix:<☄> := &infix:<☃>` ALIASES a routine
                // that may still take candidates: `multi infix:<☄>` after it is fine
                if (ve->declare && ve->name.size() > 1 && ve->name[0] == '&' &&
                    ve->declScope != "constant")
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
                throw ParseError("Redeclaration of routine '" + sd->name +
                                 "'. Did you mean to declare a multi-sub?", sd->line,
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
            completedPkgs_.insert(cd->name);  // …wherever the body stands (see the unit check)
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
    // A stub is a promise to the COMPILATION UNIT, not to the block it stands in:
    // `class Gen::Tab { ... }` written inside a method is completed by the
    // file-scope `class Gen::Tab` further down, which is how Terminal::Table
    // forward-declares the class its generator returns. Checking it at the end of
    // every block rejected that at the closing brace of the method.
    if (!stubbed.empty() && unitScope) {
        // a stub naming a `use`d module is a redeclaration hint, not a promise
        std::set<std::string> used;
        for (auto& s : stmts)
            if (s && s->kind == NK::UseStmt)
                used.insert(static_cast<const UseStmt*>(s.get())->module);
        std::string names;
        // …and the body may stand in a NESTED block: a package declaration is
        // `our`-scoped wherever it is written, so `class Rak { … }` inside the
        // `rak` sub completes the file-scope `our class Rak { ... }` (that is how
        // rak keeps its result class beside the code that builds it). Nested
        // blocks are checked before the unit, so the set is complete here.
        for (auto& n : stubbed)
            if (!used.count(n) && !completedPkgs_.count(n)) { if (!names.empty()) names += " "; names += n; }
        if (!names.empty())
            throw ParseError("The following packages were stubbed but not defined: " +
                             names, stmts.empty() ? 0 : stmts.back()->line,
                             "X::Package::Stubbed", {{"packages", names}});
    }
}

Program Parser::parseProgram() {
    Program prog;
    try {
        while (!isKind(Tok::End)) {
            if (matchKind(Tok::Semicolon)) continue;
            prog.stmts.push_back(parseStatement());
            for (auto& ps : pendingStmts_) prog.stmts.push_back(std::move(ps)); // `will leave` desugars
            pendingStmts_.clear();
            if (!matchKind(Tok::Semicolon)) enforceStmtSep();
        }
    } catch (ParseError&) {
        // A tolerant lex (Lexer::tokenize) stopped at a construct it could not
        // read; whatever the parser then tripped over AT that end is a symptom
        // (`say "abc` reads as a bare `say` before End). The lex error is the
        // unit's. An error raised BEFORE the end is the parser's own.
        if (isKind(Tok::End) && cur().flag) throw Lexer::storedLexError((size_t)cur().ival);
        throw;
    }
    // the lex stopped early (Lexer::tokenize, tolerant): no slang re-read the
    // rest, so the error it stopped on is the unit's
    if (isKind(Tok::End) && cur().flag) throw Lexer::storedLexError((size_t)cur().ival);
    checkRedeclarations(prog.stmts, /*unitScope=*/true);
    // Illegal post-declaration: a class/role/grammar named as a term BEFORE the
    // unit declares it is Rakudo's X::Undeclared::Symbols (with `post_types`)
    if (!importsModules_ && !declTypesOpaque_)
        for (auto& [n, ln] : earlyTypeUse_)
            if (declClassDecls_.count(n) && !isKnownTypeName(n))
                throw ParseError("Undeclared name:\n    " + n + " used at line " + std::to_string(ln) +
                                 " (illegal post-declaration)", ln, "X::Undeclared::Symbols", {{"post_types", n}});
    prog.declaredTypeNames = std::move(declTypeNames_);
    prog.typeNamesOpaque = declTypesOpaque_;
    prog.importsModules = importsModules_;
    prog.mayHaveEnd = sawEndPhaser_;
    prog.langRev = langRev_;
    prog.usesRakuAst = usesRakuAst_;
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
    // …and when what SPLIT them is a tight `->`, name it: that is Perl 5's
    // method arrow, which the pointy-block branch only sees after a bareword.
    const bool arrowAfter = pos_ > 1 && pv.kind == Tok::Op && pv.text == "->" &&
                            !pv.spaceBefore && !cur().spaceBefore && termEndsHere(toks_[pos_ - 2]);
    const bool arrowHere  = isKind(Tok::Op) && cur().text == "->" && !cur().spaceBefore &&
                            termEndsHere(pv) && peek().kind == Tok::Ident && !peek().spaceBefore;
    if (arrowAfter || arrowHere)
        throw ParseError("Unsupported use of -> as postfix. In Raku please use: "
                         "either . to call a method, or whitespace to delimit a pointy block.",
                         cur().line, "X::Obsolete",
                         {{"old", "-> as postfix"},
                          {"replacement", "either . to call a method, or whitespace "
                                          "to delimit a pointy block"}});
    if (pv.kind != Tok::RBrace && pv.kind != Tok::Semicolon && cur().line == pv.line) {
        // A `[` here was read as a bracketed INFIX whose brackets do not spell an
        // operator — `@a [0]` — and naming that is more use than "two terms in a
        // row", which is also what Rakudo says about it. (parseExpr cannot raise
        // it where it gives up on the brackets: the same `[` is legitimately
        // retried there at a looser precedence, which is how `2 [&f] 3 [&f] 4`
        // and `1,2 [Z*] 3,4` get parsed at all.)
        if (isKind(Tok::LBracket))
            throw ParseError("Missing infix inside []", cur().line,
                             "X::Syntax::Missing", {{"what", "infix inside []"}});
        throw ParseError("Two terms in a row (missing semicolon?)", cur().line);
    }
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
        // buffer read/write flags: low 2 bits = endian (Endian enum:
        // Native 0 / Little 1 / Big 2), bits 2+ = size code (1<<(flag>>2) bytes).
        {"BINARY_SIZE_8_BIT", 0},  {"BINARY_SIZE_16_BIT", 4},
        {"BINARY_SIZE_32_BIT", 8}, {"BINARY_SIZE_64_BIT", 12},
        // …and the endian half of the same flag word (path-utils ORs
        // BINARY_ENDIAN_LITTLE into every size it reads)
        {"BINARY_ENDIAN_NATIVE", 0}, {"BINARY_ENDIAN_LITTLE", 1}, {"BINARY_ENDIAN_BIG", 2},
        // `nqp::stat`/`nqp::lstat` field selectors, as MoarVM numbers them.
        // Path::Finder asks for the inode to detect directory loops.
        {"STAT_EXISTS", 0}, {"STAT_FILESIZE", 1}, {"STAT_ISDIR", 2},
        {"STAT_ISREG", 3}, {"STAT_ISDEV", 4}, {"STAT_CREATETIME", 5},
        {"STAT_ACCESSTIME", 6}, {"STAT_MODIFYTIME", 7}, {"STAT_CHANGETIME", 8},
        {"STAT_BACKUPTIME", 9}, {"STAT_UID", 10}, {"STAT_GID", 11},
        {"STAT_ISLNK", 12},
        // the PLATFORM_ selectors are NEGATIVE — read off Rakudo rather than
        // guessed, which is how the first draft got 13/14 where it wanted -1/-2
        {"STAT_PLATFORM_DEV", -1}, {"STAT_PLATFORM_INODE", -2},
        {"STAT_PLATFORM_MODE", -3}, {"STAT_PLATFORM_NLINKS", -4},
        {"STAT_PLATFORM_DEVTYPE", -5}, {"STAT_PLATFORM_BLOCKSIZE", -6},
        {"STAT_PLATFORM_BLOCKS", -7},
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
        {"bitor_i", NqpOpc::BitorI}, {"bitxor_i", NqpOpc::BitxorI},
        {"bitshiftl_i", NqpOpc::BitshiftlI}, {"bitshiftr_i", NqpOpc::BitshiftrI},
        {"iseq_n", NqpOpc::IseqN}, {"isne_n", NqpOpc::IsneN},
        {"atpos_n", NqpOpc::AtposN}, {"bindpos_n", NqpOpc::BindposN},
        {"readuint", NqpOpc::ReadUInt}, {"readint", NqpOpc::ReadInt},
        {"readnum", NqpOpc::ReadNum}, {"writeuint", NqpOpc::WriteUInt},
        {"writeint", NqpOpc::WriteInt}, {"writenum", NqpOpc::WriteNum},
        {"slice", NqpOpc::Slice}, {"decode", NqpOpc::Decode},
        {"setelems", NqpOpc::SetElems}, {"add_I", NqpOpc::AddBigI},
        {"decont", NqpOpc::Decont}, {"p6box_s", NqpOpc::P6BoxS},
        {"ordat", NqpOpc::Ordat},  {"eqat", NqpOpc::Eqat},
        {"substr", NqpOpc::Substr},{"chars", NqpOpc::Chars},
        {"concat", NqpOpc::Concat},{"join", NqpOpc::Join},
        {"index", NqpOpc::Index},  {"chr", NqpOpc::Chr},
        // case-insensitive index, and the ignore-mark variant beside it
        {"indexic", NqpOpc::Indexic}, {"indexicim", NqpOpc::Indexicim},
        {"indexim", NqpOpc::Indexim},
        {"strfromcodes", NqpOpc::StrFromCodes}, {"strtocodes", NqpOpc::StrToCodes},
        {"findnotcclass", NqpOpc::FindNotCClass}, {"iscclass", NqpOpc::IsCClass},
        {"list", NqpOpc::List}, {"list_i", NqpOpc::ListI}, {"list_s", NqpOpc::ListS},
        {"elems", NqpOpc::Elems}, {"atpos", NqpOpc::Atpos}, {"atpos_i", NqpOpc::AtposI},
        {"atpos_u", NqpOpc::AtposI}, {"hllbool", NqpOpc::HllBool},
        {"bindpos", NqpOpc::Bindpos}, {"bindpos_i", NqpOpc::BindposI},
        {"push", NqpOpc::Push}, {"push_i", NqpOpc::PushI}, {"push_s", NqpOpc::PushS},
        {"pop_s", NqpOpc::PopS}, {"shift_i", NqpOpc::ShiftI}, {"splice", NqpOpc::Splice},
        {"hash", NqpOpc::Hash}, {"bindkey", NqpOpc::Bindkey},
        // the read side of the same surface — HTML::Entity::Fast reads its table
        // with nqp::atkey, and `unbox_s` is how Path::Finder gets at a Str's guts
        {"atkey", NqpOpc::Atkey}, {"existskey", NqpOpc::ExistsKey},
        {"deletekey", NqpOpc::DeleteKey},
        {"unbox_s", NqpOpc::Decont}, {"unbox_i", NqpOpc::Decont},
        {"unbox_n", NqpOpc::Decont}, {"decont_s", NqpOpc::Decont},
        {"create", NqpOpc::Create}, {"istype", NqpOpc::Istype},
        {"istype_nd", NqpOpc::Istype}, // no-decont variant: same for us
        {"getattr", NqpOpc::Getattr}, {"bindattr", NqpOpc::Bindattr},
        {"getattr_i", NqpOpc::Getattr},
        {"p6bindattrinvres", NqpOpc::P6BindAttrInvRes},
        {"p6scalarwithvalue", NqpOpc::P6ScalarWithValue},
        {"null", NqpOpc::Null}, {"isnanorinf", NqpOpc::IsNanOrInf},
        {"isnull", NqpOpc::IsNull}, {"isnull_s", NqpOpc::IsNullS},
        {"eqaddr", NqpOpc::Eqaddr}, {"objprimspec", NqpOpc::ObjPrimSpec},
        {"unipropcode", NqpOpc::UniPropCode}, {"getuniprop_str", NqpOpc::GetUniPropStr},
        {"getuniprop_bool", NqpOpc::GetUniPropBool}, {"getuniprop_int", NqpOpc::GetUniPropInt},
        // the AttrX::Mooish surface
        {"hllize", NqpOpc::Decont}, {"box_s", NqpOpc::P6BoxS},
        {"what", NqpOpc::What}, {"islist", NqpOpc::IsList},
        {"iscont", NqpOpc::IsCont}, {"istrue", NqpOpc::IsTrue},
        {"isconcrete", NqpOpc::IsConcrete}, {"isconcrete_nd", NqpOpc::IsConcrete},
        // Rakudo's own op, not NQP's: same question as `isconcrete`, but the
        // answer is a Raku Bool rather than an nqp int. Compress::Zlib's line
        // reader loops on it (`while nqp::p6definite(my $line = self.get)`).
        {"p6definite", NqpOpc::P6Definite},
        // `nqp::can($type.HOW, "roles")` — RakuAST::Utils asks a META-OBJECT
        // whether it answers a method before calling it.
        {"can", NqpOpc::Can},
        {"clone", NqpOpc::CloneOp}, {"clone_nd", NqpOpc::CloneOp},
        {"shift", NqpOpc::Shift},
        {"lock", NqpOpc::LockOp}, {"unlock", NqpOpc::UnlockOp},
        {"open", NqpOpc::OpenFh}, {"readfh", NqpOpc::ReadFh}, {"closefh", NqpOpc::CloseFh},
        // `nqp::stat($path, nqp::const::STAT_…)` — the const names were already
        // known; this is the op that reads them. Path::Finder matches on inode,
        // device, uid, gid, nlinks, blocks, blocksize, devtype and is-dev, and
        // keys its symlink-loop guard on inode+device.
        {"stat", NqpOpc::Stat}, {"stat_time", NqpOpc::StatTime},     // the _time forms answer a Num
        {"lstat", NqpOpc::Lstat}, {"lstat_time", NqpOpc::LstatTime}, // (path-utils' isa-ok Num)
        // the running compiler, for the REPL-sandbox pattern (see REPL below)
        {"getcomp", NqpOpc::GetComp},
        {"sha1", NqpOpc::Sha1},
        // the directory walk `paths` is written against, and the file tests
        // `path-utils` asks beside it
        {"opendir", NqpOpc::OpenDir}, {"nextfiledir", NqpOpc::NextFileDir},
        {"closedir", NqpOpc::CloseDir},
        {"filereadable", NqpOpc::FileReadable}, {"filewritable", NqpOpc::FileWritable},
        {"fileexecutable", NqpOpc::FileExecutable}, {"fileislink", NqpOpc::FileIsLink},
        {"handle", NqpOpc::Handle},
        // small leaves: String::Utils spells `ne`, `!`, `%` and the positive
        // character-class scan this way, and the `_s` spellings of the list and
        // attribute ops are the same ops on strings
        {"isne_s", NqpOpc::IsneS}, {"iseq_s", NqpOpc::IseqS},
        // the rest of the string comparison family — Array::Sorted::Util
        // walks its keys with `iseq_s`/`islt_s` and has eleven dists behind it
        {"islt_s", NqpOpc::IsltS}, {"isle_s", NqpOpc::IsleS},
        {"isgt_s", NqpOpc::IsgtS}, {"isge_s", NqpOpc::IsgeS}, {"not_i", NqpOpc::NotI}, {"mod_i", NqpOpc::ModI},
        {"findcclass", NqpOpc::FindCClass},
        {"null_s", NqpOpc::Null}, {"atpos_s", NqpOpc::Atpos}, {"bindpos_s", NqpOpc::Bindpos},
        {"bindattr_i", NqpOpc::Bindattr}, {"bindattr_s", NqpOpc::Bindattr},
        {"rindex", NqpOpc::Rindex}, {"flip", NqpOpc::Flip}, {"split", NqpOpc::Split},
        {"x", NqpOpc::X},
        // The integer/list/system leaves lizmat's modules reach for. `div_i` is
        // the binary-search midpoint in Array::Sorted::Util and the eleven
        // dists around it; the `_I` pair is its bignum spelling.
        {"div_i", NqpOpc::DivI}, {"div_I", NqpOpc::DivBigI},
        {"isne_I", NqpOpc::IsneBigI}, {"isfalse", NqpOpc::IsFalse},
        {"pop", NqpOpc::Pop}, {"pop_i", NqpOpc::Pop}, {"pop_n", NqpOpc::Pop},
        {"print", NqpOpc::Print}, {"say", NqpOpc::SayOp},
        {"time", NqpOpc::TimeOp},
        {"readlink", NqpOpc::ReadLink},
        {"repeat_while", NqpOpc::RepeatWhile}, {"repeat_until", NqpOpc::RepeatUntil},
        // the rest of the bignum `_I` family, and the boxing leaves beside it
        {"box_i", NqpOpc::BoxI}, {"box_n", NqpOpc::BoxN},
        {"iseq_I", NqpOpc::IseqBigI}, {"islt_I", NqpOpc::IsltBigI},
        {"isle_I", NqpOpc::IsleBigI}, {"isge_I", NqpOpc::IsgeBigI},
        {"isgt_I", NqpOpc::IsgtBigI}, {"cmp_I", NqpOpc::CmpBigI},
        // the NATIVE three-way compares beside the bignum one: Array::Sorted::Util
        // orders its keys with `nqp::cmp_s` and String::Color sorts through it
        {"cmp_s", NqpOpc::CmpS}, {"cmp_i", NqpOpc::CmpI}, {"cmp_n", NqpOpc::CmpI},
        {"mul_I", NqpOpc::MulBigI},   {"sub_I", NqpOpc::SubBigI},
        {"mod_I", NqpOpc::ModBigI},   {"neg_I", NqpOpc::NegBigI},
        {"abs_I", NqpOpc::AbsBigI},   {"pow_I", NqpOpc::PowBigI},
        {"gcd_I", NqpOpc::GcdBigI},   {"lcm_I", NqpOpc::LcmBigI},
        {"bitand_I", NqpOpc::BitandBigI}, {"bitor_I", NqpOpc::BitorBigI},
        {"bitxor_I", NqpOpc::BitxorBigI},
        {"bitshiftl_I", NqpOpc::BitshiftlBigI}, {"bitshiftr_I", NqpOpc::BitshiftrBigI},
        {"isbig_I", NqpOpc::IsBigI}, {"tostr_I", NqpOpc::ToStrBigI},
        {"fromstr_I", NqpOpc::FromStrBigI}, {"sqrt_n", NqpOpc::SqrtN},
    };
    auto it = k.find(op);
    if (it == k.end()) return nullptr;
    auto n = std::make_unique<NqpOp>(it->second);
    n->args = std::move(args);
    return n;
}

// ---- SLANG-PLAN §A: `use Slang::X` --------------------------------------
// scanModuleOps read the module's source for its operators and saw that it
// registers a slang. The module runs verbatim in a scratch Interpreter
// (rakuppActivateSlang), and what it registered comes back as seams the lexer
// runs and modes the parser honours. Then the REST of this unit is lexed
// again with those armed — from the byte just past the `use` statement, so
// the pragma's own words are never read through the slang. Unit-scoped, as
// L10N is here and as mutsu is: a `use` inside a block governs to the end
// of the file, where Rakudo would stop at the block's brace.
// An imported module declares `sub tr` / `sub q`: the quote form of that name has
// to stop being one for the rest of THIS unit. The lexer already ran, so the tail
// of the token stream is replaced by a fresh lex that knows the names — the same
// splice activateSlang performs, and subject to the same limit: it needs the
// unit's source, so a `use` inside an EVAL of a string this parser never saw
// keeps the quote reading.
void Parser::relexForQuoteWords() {
    if (!src_ || quoteWordSubs_.empty()) return;
    const size_t from = pos_ > 0 ? toks_[pos_ - 1].off : 0;   // just past the `use`
    Lexer lx(*src_);
    lx.slang_ = slang_;
    lx.slangFrom_ = slang_ ? 0 : 0;
    // imported names shadow the quote form everywhere in this unit; the unit's
    // OWN declarations are re-scanned by the lexer, with their real scopes
    for (auto& n : quoteWordSubs_) lx.notQuoteWords_[n].push_back({0, SIZE_MAX});
    std::vector<Token> nt = lx.tokenize();
    size_t i = 0;
    while (i < nt.size() && nt[i].kind != Tok::End && nt[i].off <= from) i++;
    toks_.erase(toks_.begin() + pos_, toks_.end());
    toks_.insert(toks_.end(), nt.begin() + i, nt.end());
}

void Parser::activateSlang(const std::string& module) {
    if (!src_) error("`use " + module + "`: a slang cannot be applied here (the source is not available to re-read)");
    std::string err;
    std::shared_ptr<SlangSeams> seams = rakuppActivateSlang(module, libPaths_, err);
    if (!seams) error(err);
    if (slang_) {
        // a second slang in the same unit: both sets of seams, the newer tried first
        std::shared_ptr<SlangSeams> older = slang_;
        auto merged = std::make_shared<SlangSeams>(*seams);
        merged->number |= older->number;         merged->value |= older->value;
        merged->identifier |= older->identifier; merged->sigilless |= older->sigilless;
        merged->pointy |= older->pointy;         merged->declarator |= older->declarator;
        merged->spacedCall |= older->spacedCall; merged->spacedMethodop |= older->spacedMethodop;
        auto tryNew = seams->tryMatch, tryOld = older->tryMatch;
        merged->tryMatch = [tryNew, tryOld, seams, older](const std::string& which, const std::string& src, size_t pos,
                                                          size_t& end, std::string& repl) -> bool {
            return (tryNew && tryNew(which, src, pos, end, repl)) || (tryOld && tryOld(which, src, pos, end, repl));
        };
        slang_ = merged;
    }
    else slang_ = seams;
    const size_t from = pos_ > 0 ? toks_[pos_ - 1].off : 0;   // just past the `use` statement
    Lexer lx(*src_);
    lx.slang_ = slang_;
    lx.slangFrom_ = from;
    std::vector<Token> nt = lx.tokenize();
    size_t i = 0;
    while (i < nt.size() && nt[i].kind != Tok::End && nt[i].off <= from) i++;
    toks_.erase(toks_.begin() + pos_, toks_.end());
    toks_.insert(toks_.end(), nt.begin() + i, nt.end());
    // declarator pod after the pragma comes from the second lexer — same text, same lines
    for (auto& kv : lx.declPod_) declPod_[kv.first] = kv.second;
    for (auto& kv : lx.leadPod_) leadPod_[kv.first] = kv.second;
}

// Tuxic's `term:sym<identifier>`: `name (args)` is a call with those args —
// except for the control keywords its own token excludes, and a type name.
// This is a MODE, not the token run: the token's body calls Rakudo's <args>
// and $*R, which no seam can supply (SLANG-PLAN tier 3).
bool Parser::slangSpacedCall(const std::string& name) const {
    if (!slang_ || !slang_->spacedCall) return false;
    static const std::set<std::string> kw = {"sub", "if", "elsif", "while", "until", "for"};
    if (kw.count(name)) return false;
    if (!name.empty() && ascii::isupper((unsigned char)name[0]) && knownTypeName(name)) return false;
    return true;
}

// The identifier at cur() is a sigilless variable under the armed slang: the
// lexer already said so (a codepoint no bareword covers, Slang::Emoji's 👍),
// or the slang's token claims the whole bareword (Slang::Nogil's `my a`).
bool Parser::slangSigillessHere() {
    if (cur().flag) return true;
    if (!src_ || !slang_->tryMatch) return false;
    const Token& t = cur();
    if (t.off < t.text.size()) return false;
    const size_t start = t.off - t.text.size();
    size_t end = 0;
    std::string repl;
    try {
        if (!slang_->tryMatch("sigilless-variable", *src_, start, end, repl)) return false;
    } catch (ParseError& e) {
        if (e.line == 0) throw ParseError(e.what(), t.line);
        throw;
    }
    return end == t.off;
}


// A name that is a TYPE here: one of the core types, or a class/role/grammar
// this unit declared. Tuxic's spaced-call exclusion list is its only user.
bool Parser::knownTypeName(const std::string& name) const {
    static const std::set<std::string> core = {
        "Int", "Str", "Num", "Rat", "FatRat", "Complex", "Bool", "Array", "Hash", "List", "Map", "Any", "Mu",
        "Cool", "Numeric", "Real", "Positional", "Associative", "Callable", "Iterable", "Date", "DateTime",
        "Instant", "Duration", "IO", "Buf", "Blob", "Pair", "Set", "Bag", "Mix", "SetHash", "BagHash", "MixHash",
        "Range", "Seq", "Supply", "Promise", "Channel", "Proc", "Lock", "Thread", "Nil", "Version", "Junction",
        "Code", "Sub", "Method", "Block", "Routine", "Regex", "Grammar", "Match", "Capture", "Signature",
        "Parameter", "Attribute", "Exception", "Failure", "Order", "Whatever", "Slip", "Scalar", "Stash",
        "Enumeration", "UInt", "Nat", "IntStr", "NumStr", "RatStr", "Uni", "NFC", "NFD", "Encoding",
    };
    return core.count(name) > 0 || declTypeNames_.count(name) > 0;
}

} // namespace rakupp
