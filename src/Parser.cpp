#include "Parser.h"
#include <cstdint>
#include <memory>
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

// binding powers (higher = tighter)
// Raku orders these (tightest→loosest): additive `+ -` > replication `x xx` >
// concatenation `~` > range. So `"-" x $n + 2` == `"-" x ($n + 2)` and
// `"x" x 2 ~ "y"` == `("x" x 2) ~ "y"`. Each gets a distinct level.
// Levels are spaced by 10 so a user-defined operator's `is tighter`/`looser`
// trait can slot a fresh precedence *between* two built-in levels (e.g. tighter
// than `+` but looser than `*`).
enum {
    BP_OR = 10, BP_AND = 20, BP_COMMA = 30, BP_ASSIGN = 40, BP_TERNARY = 50,
    BP_OROR = 60, BP_ANDAND = 70, BP_COMPARE = 80, BP_RANGE = 90,
    BP_CONCAT = 100, BP_REPLICATE = 110, BP_ADD = 120, BP_MUL = 130, BP_POW = 140, BP_PREFIX = 150
};

static const std::unordered_set<std::string> kAssignOps = {
    "=", "+=", "-=", "*=", "/=", "~=", "%=", "**=", "//=", "||=", "&&=", "x=", ":=",
    "div=", "mod=", "gcd=", "lcm=",
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
        if (o == "Z" || o == "X") { in.valid = true; in.lbp = BP_ADD; return in; } // zip / cross
        if (o == "min" || o == "max") { in.valid = true; in.lbp = BP_ADD; return in; } // infix min/max
        if (o == "and" || o == "andthen") { in.valid = true; in.lbp = BP_AND; return in; }
        if (o == "or" || o == "xor" || o == "orelse") { in.valid = true; in.lbp = BP_OR; return in; }
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

bool Parser::startsTermToken(const Token& t) const {
    switch (t.kind) {
        case Tok::IntLit: case Tok::NumLit: case Tok::StrLit: case Tok::VersionLit: case Tok::StrInterp: case Tok::RegexLit: case Tok::SubstLit:
        case Tok::QwList:
        case Tok::Var: case Tok::LParen: case Tok::LBracket: case Tok::LBrace:
            return true;
        case Tok::Op:
            return t.text == "!" || t.text == "~" || t.text == "\\" || t.text == "<" ||
                   t.text == "+" || t.text == "-" || t.text == "?" || t.text == ":" ||
                   t.text == "++" || t.text == "--" || // prefix incr/decr: `f 0, ++$x`
                   t.text == "*" || t.text == "->" || t.text == "<->" || t.text == "|" ||
                   t.text == "^" || // prefix `^N` (upto) after a comma: `1, ^10 .Seq` (infix ^ is impossible there)
                   t.text == "&" || // operator-as-value `&[+]` (bare `&` in term position is only `&[OP]`)
                   t.text == "." || // leading `.method` => $_.method (e.g. `1, .uc`)
                   t.text == "::" || // symbolic reference `::($name)` / `::Foo`
                   t.text == "\xE2\x88\x9E" || t.text == "\xC2\xAB" || // ∞  and  «qw»
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
            return !kBlockKeywords.count(t.text) ||
                   t.text == "sub" || t.text == "method" || t.text == "do" || t.text == "start" ||
                   t.text == "my" || t.text == "our" || t.text == "state" || t.text == "has" || t.text == "constant";
        default:
            return false;
    }
}
// what may begin a list-op argument (no parens; conservative on leading symbols)
bool Parser::startsListopArg(const Token& t) const {
    switch (t.kind) {
        case Tok::IntLit: case Tok::NumLit: case Tok::StrLit: case Tok::VersionLit: case Tok::StrInterp: case Tok::RegexLit: case Tok::SubstLit:
        case Tok::QwList:
        case Tok::Var: case Tok::LBracket: case Tok::LParen: case Tok::LBrace:
            return true;
        case Tok::Op:
            return t.text == "!" || t.text == "~" || t.text == "\\" || t.text == "<" ||
                   t.text == ":" || t.text == "+" || t.text == "-" || t.text == "?" ||
                   t.text == "++" || t.text == "--" || // prefix incr/decr: `say 0, ++$x`
                   t.text == "^" || // prefix `^N` (upto) as a listop arg: `flat ^15, 49`
                   (t.text == "|" && t.spaceBefore) || // slip first arg `run |@cmd` (space before |) — NOT infix junction `Any|Blob`
                   t.text == "!!" || // prefix boolify `say !!$x` (`!!` never starts a bare term otherwise)
                   (t.text == "." && t.spaceBefore) || // leading `.method` => $_.method (only after a space: `say .uc`)
                   t.text == "::" || // symbolic reference `say ::($name)` / `say ::Foo`
                   t.text == "$" || // item contextualizer `ok $%*ENV` / `say $(1,2)` (bare `$` is never an infix)
                   ((t.text == "%" || t.text == "@") && &t == &cur() &&
                    peek().kind == Tok::LParen && !peek().spaceBefore) || // hash/list contextualizer `is-deeply %(...)…` / `push @(...)…`
                   t.text == "\xE2\x88\x9E" || t.text == "\xC2\xAB" || // ∞  and  «qw»
                   userPrefix_.count(t.text) || userCircumfix_.count(t.text); // user prefix / circumfix-open
        case Tok::Ident: {
            // A word-infix operator right after a bareword term is an INFIX, not the
            // start of a listop argument: `Seq eqv Seq`, `Int eq Int`, `$x div $y`.
            static const std::set<std::string> wordInfix = {
                "eq", "ne", "lt", "gt", "le", "ge", "cmp", "leg", "eqv", "before", "after", "unicmp", "coll",
                "x", "xx", "and", "or", "andthen", "orelse", "div", "mod", "gcd", "lcm",
            };
            if (wordInfix.count(t.text)) return false;
            // sub/method/do/start begin an expression (anonymous routine / do-block); my/our/state/has/
            // constant begin a declaration expression that is a valid list-op argument (`ok my $x = 5, "d"`)
            return !kBlockKeywords.count(t.text) ||
                   t.text == "sub" || t.text == "method" || t.text == "do" || t.text == "start" ||
                   t.text == "my" || t.text == "our" || t.text == "state" || t.text == "has" || t.text == "constant";
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
        InfixInfo in = classifyInfix(cur());
        if (!in.valid || in.lbp < minbp) break;

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
            if (startsTermToken(cur())) {
                list->items.push_back(parseExpr(BP_COMMA + 1));
            }
            lhs = std::move(list);
            continue;
        }
        if (in.isFatArrow) {
            advance();
            auto p = std::make_unique<PairExpr>();
            if (lhs->kind == NK::NameTerm) p->key = static_cast<NameTerm*>(lhs.get())->name;
            else if (lhs->kind == NK::StrLit) p->key = static_cast<StrLit*>(lhs.get())->v;
            else p->keyExpr = std::move(lhs); // $var / "interp" / (expr) keys evaluated at runtime
            p->value = parseExpr(BP_ASSIGN);
            lhs = std::move(p);
            continue;
        }

        // zip/cross metaoperator with a trailing tight op: Z=> Z+ X* Zeq ...
        if (in.op == "Z" || in.op == "X") {
            std::string meta;
            if (peek().kind == Tok::FatArrow) meta = "=>";
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
        if (in.valid && cur().kind == Tok::Ident &&
            peek().kind == Tok::Op && peek().text == "=" && !peek().spaceBefore) {
            if (BP_ASSIGN < minbp) break;
            std::string opname = advance().text; advance(); // the word op, then '='
            auto a = std::make_unique<Assign>();
            a->target = std::move(lhs);
            a->op = opname + "=";
            a->value = parseExpr(BP_ASSIGN);
            lhs = std::move(a);
            continue;
        }

        advance(); // consume infix op

        // list assignment: `@a = 1,2,3` / `my ($a,$b) = ...` grabs the whole comma
        // list; binding does too (`my @r := &min, &max, &minmax` is a 3-element bind)
        bool listAssign = false;
        if (in.isAssign && (in.op == "=" || in.op == ":=")) {
            if (lhs->kind == NK::ListExpr) listAssign = true;
            else if (lhs->kind == NK::VarExpr) {
                const std::string& nm = static_cast<VarExpr*>(lhs.get())->name;
                if (!nm.empty() && (nm[0] == '@' || nm[0] == '%')) listAssign = true;
            }
        }

        int nextMin = listAssign ? BP_COMMA : (in.rightAssoc ? in.lbp : in.lbp + 1);
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
        if (o == "!" || o == "-" || o == "+" || o == "~" || o == "?" ||
            o == "++" || o == "--" || o == "^" || o == "|") {
            advance();
            auto u = std::make_unique<Unary>();
            // the operand parses "tight" (its own postfixes stop at a space-preceded
            // `.method`), so `^30 .map` is (^30).map while `^30.map` stays ^(30.map).
            u->op = o; u->operand = parsePrefix(true);
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
            auto* ix = static_cast<Index*>(base.get());
            // stacked adverbs (`:exists:kv:$delete`) accumulate ':'-joined
            ix->adverb += (ix->adverb.empty() ? "" : ":") + adv;
            continue;
        }
        if (isKind(Tok::LBracket) && !cur().spaceBefore) {
            advance();
            if (isKind(Tok::RBracket)) { advance(); continue; } // zen slice @a[] == @a
            auto idx = std::make_unique<Index>();
            idx->base = std::move(base);
            idx->index = parseExpression();
            idx->isHash = false;
            // multidim subscript @a[i;j] ≡ @a[i][j]
            while (matchKind(Tok::Semicolon)) {
                if (isKind(Tok::RBracket)) break;
                auto outer = std::make_unique<Index>();
                outer->base = std::move(idx);
                outer->isHash = false;
                outer->index = parseExpression();
                idx = std::move(outer);
            }
            expectKind(Tok::RBracket, "]");
            base = std::move(idx);
        } else if (isKind(Tok::LBrace) && !cur().spaceBefore) {
            advance();
            if (isKind(Tok::RBrace)) { advance(); continue; } // zen slice %h{} == %h
            auto idx = std::make_unique<Index>();
            idx->base = std::move(base);
            idx->index = parseExpression();
            idx->isHash = true;
            while (matchKind(Tok::Semicolon)) {
                if (isKind(Tok::RBrace)) break;
                auto outer = std::make_unique<Index>();
                outer->base = std::move(idx);
                outer->isHash = true;
                outer->index = parseExpression();
                idx = std::move(outer);
            }
            expectKind(Tok::RBrace, "}");
            base = std::move(idx);
        } else if (isOp("<") && !cur().spaceBefore) {
            // word-key hash subscript: %h<key>  (and $<name>/@<name>/%<name> capture sugar for $/<name>)
            // On a numeric literal (`1<2`) this can only be a mistyped comparison —
            // infix `<` requires whitespace before it (S03), so it's a parse error.
            if (base->kind == NK::IntLit || base->kind == NK::NumLit)
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
        } else if (isOp("!") && !cur().spaceBefore && peek().kind == Tok::Ident && !peek().spaceBefore) {
            // private method call: self!method / $obj!method (shares the method table)
            advance(); // !
            auto mc = std::make_unique<MethodCall>();
            mc->inv = std::move(base);
            mc->method = advance().text;
            if (isKind(Tok::LParen)) { advance(); mc->args = parseCallArgs(); }
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
                auto idx = std::make_unique<Index>();
                idx->base = std::move(base); idx->isHash = true;
                if (words.size() == 1) idx->index = std::make_unique<StrLit>(words[0]);
                else { auto al = std::make_unique<ArrayLit>(); for (auto& w : words) al->items.push_back(std::make_unique<StrLit>(w)); idx->index = std::move(al); }
                base = std::move(idx);
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
                expectKind(Tok::RBracket, "]");
                base = std::move(idx);
                continue;
            }
            if (isKind(Tok::LParen)) {
                advance();
                auto c = std::make_unique<Call>();
                c->callee = std::move(base);
                c->args = parseCallArgs();
                base = std::move(c);
                continue;
            }
            bool maybe = false;
            if (isOp("?")) { advance(); maybe = true; }
            bool metaCall = false;
            if (isOp("^")) { metaCall = true; advance(); } // .^meta
            else if (isOp("&") || isOp("*") || isOp("+")) advance(); // .&fn / .*all / .+all — best effort
            auto mc = std::make_unique<MethodCall>();
            mc->inv = std::move(base);
            mc->maybe = maybe;
            mc->meta = metaCall;
            mc->mutate = mutate;
            mc->hyper = hyperNext; hyperNext = false;
            bool indirectName = false;
            if (cur().kind == Tok::Ident || cur().kind == Tok::Var) {
                mc->method = advance().text;
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
            if (isKind(Tok::LParen)) { advance(); mc->args = parseCallArgs(); } // .method(args) or .method (args)
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
        } else if (cur().kind == Tok::Op && userPostfix_.count(cur().text)) {
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
void Parser::skipTraits(bool onVarDecl) {
    while (isIdent("is") || isIdent("does") || isIdent("returns") || isIdent("of")) {
        bool wasIs = isIdent("is");
        advance();
        // `is readonly` is a PARAMETER trait; on a variable declaration it's a
        // compile error (X::Comp::Trait::Unknown) — roast S03-binding/ro.t.
        if (onVarDecl && wasIs && isIdent("readonly"))
            error("Can't use unknown trait 'is readonly' in a variable declaration");
        if (isKind(Tok::Ident) || isKind(Tok::Var)) advance(); // trait name / type
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
    // type-capture declaration:  my ::T $x  (binds T to the type of $x; we just parse it)
    if (isOp("::") && peek().kind == Tok::Ident) { advance(); advance(); }
    std::string type, coerceTo;
    if (isKind(Tok::Ident)) {
        bool looksType = peek().kind == Tok::Var || peek().kind == Tok::LParen ||
                         peek().kind == Tok::LBracket ||
                         (peek().kind == Tok::Op && (peek().text == ":" || peek().text == "\\"));
        if (looksType) {
            type = advance().text;
            if (isOp(":") && peek().kind == Tok::Ident) { advance(); advance(); } // :D / :U / :_ smiley
            if (isKind(Tok::LBracket)) { int d = 0; do { if (isKind(Tok::LBracket)) d++; else if (isKind(Tok::RBracket)) d--; advance(); } while (d > 0 && !isKind(Tok::End)); }
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
                list->items.push_back(std::move(ve));
                if (!matchKind(Tok::Comma)) break;
                continue;
            }
            std::string t2;
            if (isKind(Tok::Ident) && peek().kind == Tok::Var) t2 = advance().text;
            if (!isKind(Tok::Var)) error("expected variable in declaration");
            auto ve = std::make_unique<VarExpr>(advance().text);
            ve->declare = true; ve->declScope = scope; ve->declType = t2;
            list->items.push_back(std::move(ve));
            if (!matchKind(Tok::Comma)) break;
        }
        expectKind(Tok::RParen, ")");
        return list;
    }
    if (isKind(Tok::Var)) {
        auto ve = std::make_unique<VarExpr>(advance().text);
        ve->declare = true; ve->declScope = scope; ve->declType = type; ve->declCoerce = coerceTo;
        // `%a{Str}` — hash key-type shape declaration
        std::string keyType;
        if (isKind(Tok::LBrace) && peek().kind == Tok::Ident && peek(2).kind == Tok::RBrace) {
            advance(); keyType = advance().text; advance();
        }
        // `of Type` postfix trait sets the value/element type
        if (isIdent("of") && peek().kind == Tok::Ident) { advance(); ve->declType = advance().text; }
        // Hash[valueType,keyType]
        if (!keyType.empty()) ve->declType = (ve->declType.empty() ? "Any" : ve->declType) + "," + keyType;
        skipTraits(scope != "has");
        return ve;
    }
    if (scope == "constant" && isKind(Tok::Ident)) {
        auto ve = std::make_unique<VarExpr>(advance().text);
        ve->declare = true; ve->declScope = scope;
        skipTraits();
        return ve;
    }
    // bare sigil = anonymous variable: `my $`, `my @`, `my %`, `my &`
    if (type.empty() && ((isKind(Tok::Op) && cur().text.size() == 1 && strchr("$@%&", cur().text[0])) ||
                         (isKind(Tok::Var) && cur().text.size() == 1))) {
        std::string sig = advance().text;
        auto ve = std::make_unique<VarExpr>(sig + "!anon");
        ve->declare = true; ve->declScope = scope;
        skipTraits(scope != "has");
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
    // PROCESS/GLOBAL hold the process-wide dynamics: `$PROCESS::IN` is `$*IN`
    if ((pkg == "PROCESS" || pkg == "GLOBAL") && !std::strchr("*!?.^", rest[0])) sig += "*";
    return sig + rest;
}
// A pseudo-package angle access `MY::<$x>` / `CORE::<&not>` — symbol via scope chain.
static std::string pseudoAngleSymbol(const std::string& pkg, const std::string& sym) {
    if (sym.empty()) return "$_";
    std::string s = sym;
    if ((pkg == "PROCESS" || pkg == "GLOBAL") && s.size() > 1 &&
        std::strchr("$@%&", s[0]) && !std::strchr("*!?.^", s[1])) s = s.substr(0, 1) + "*" + s.substr(1);
    return s;
}

ExprPtr Parser::parseColonPair() {
    // ':' already current
    advance();
    bool negate = false;
    if (isOp("!")) { advance(); negate = true; }
    auto pair = std::make_unique<PairExpr>();
    // numeric adverb shorthand: :3c  ==  c => 3   (also :2.5x)
    if ((isKind(Tok::IntLit) || isKind(Tok::NumLit)) && peek().kind == Tok::Ident) {
        ExprPtr num = parsePrimary();
        pair->key = advance().text;
        pair->value = std::move(num);
        return pair;
    }
    if (isKind(Tok::Var)) {
        // :$x  -> x => $x
        std::string vn = cur().text;
        advance();
        pair->key = vn.size() > 1 ? vn.substr(1) : vn;
        pair->value = std::make_unique<VarExpr>(vn);
        return pair;
    }
    // radix literal: :16<FF> / :2<1010> / :8<777>  (a number written in the given base)
    if (isKind(Tok::IntLit) && peek().kind == Tok::Op && peek().text == "<" && !peek().spaceBefore) {
        int base = std::atoi(cur().text.c_str());
        advance(); advance(); // radix and '<'
        std::vector<std::string> words = readAngleWords(">");
        std::string digits = words.empty() ? "" : words[0];
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
        if (infrac) { // fractional radix → a Rat: (val*fdiv + frac) / fdiv
            auto nl = std::make_unique<NumLit>((double)(val * fdiv + frac) / (double)fdiv);
            nl->isRat = true; nl->ratNum = val * fdiv + frac; nl->ratDen = fdiv;
            return nl;
        }
        return std::make_unique<IntLit>(val);
    }
    // radix conversion of a runtime value: :16("2e") / :2($bits) — the argument's
    // string form parsed in the given base (an Int).
    if (isKind(Tok::IntLit) && peek().kind == Tok::LParen && !peek().spaceBefore) {
        int base = std::atoi(cur().text.c_str());
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
        if (negate) { pair->value = std::make_unique<BoolLit>(false); return pair; }
        if (isKind(Tok::LParen) && !cur().spaceBefore) {
            advance();
            pair->value = isKind(Tok::RParen) ? std::make_unique<ListExpr>() : parseExpression();
            expectKind(Tok::RParen, ")");
            return pair;
        }
        if (isOp("<") && !cur().spaceBefore) {
            advance();
            std::vector<std::string> words = readAngleWords(">");
            if (words.size() == 1) pair->value = std::make_unique<StrLit>(words[0]);
            else { auto al = std::make_unique<ArrayLit>(); for (auto& w : words) al->items.push_back(std::make_unique<StrLit>(w)); pair->value = std::move(al); }
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
    // symbolic reference: `::($name)` — look up a symbol at runtime by string name
    if (isOp("::") && peek().kind == Tok::LParen) {
        advance(); advance(); // :: (
        auto sr = std::make_unique<SymbolicRef>();
        if (cur().kind != Tok::RParen) sr->nameExpr = parseExpression();
        expectKind(Tok::RParen, "expected ')' after ::(");
        return sr;
    }
    // sigil-prefixed symbolic deref: `$::($name)` / `@::(…)` / `%::(…)` / `&::(…)`
    // → look up the variable named SIGIL ~ $name at runtime.
    if (cur().kind == Tok::Var && cur().text.size() == 1 &&
        std::strchr("$@%&", cur().text[0]) &&
        peek().kind == Tok::Op && peek().text == "::" && peek(2).kind == Tok::LParen) {
        auto sr = std::make_unique<SymbolicRef>();
        sr->sigil = advance().text; // sigil
        advance(); advance();       // :: (
        if (cur().kind != Tok::RParen) sr->nameExpr = parseExpression();
        expectKind(Tok::RParen, "expected ')' after sigil::(");
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
        case Tok::IntLit: {
            const Token& tk = advance();
            auto e = std::make_unique<IntLit>(tk.ival);
            if (tk.text.size() > 18 && tk.text.find_first_not_of("0123456789") == std::string::npos) {
                try { (void)std::stoll(tk.text); } catch (...) { e->big = tk.text; }
            }
            return e;
        }
        case Tok::NumLit: {
            bool imag = !t.text.empty() && t.text.back() == 'i';
            bool isRat = t.flag && !imag; // decimal literal with no exponent -> Rat
            std::string txt = t.text;
            auto e = std::make_unique<NumLit>(advance().nval);
            e->imaginary = imag;
            if (isRat && txt.find('/') != std::string::npos) {
                // explicit numerator/denominator (a Unicode vulgar-fraction numeral)
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
            bool fmt = cur().flag; auto e = std::make_unique<StrLit>(advance().text);
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
            bool fmt = cur().flag; std::string raw = advance().text; auto e = parseInterpString(raw);
            if (fmt) { auto c = std::make_unique<Call>(); c->name = "__format__"; c->args.push_back(std::move(e)); return c; }
            return e;
        }
        case Tok::RegexLit: { Token tk = advance(); auto e = std::make_unique<RegexLit>(tk.text); e->isRx = tk.flag; return e; }
        case Tok::SubstLit: { const Token& t = advance(); return std::make_unique<SubstLit>(t.text, t.text2, t.flag); }
        case Tok::QwList: { // qw<...> : split raw content on whitespace into a list of strings
            std::string raw = advance().text;
            auto arr = std::make_unique<ArrayLit>();
            arr->isList = true;
            size_t i = 0, n = raw.size();
            while (i < n) {
                while (i < n && std::isspace((unsigned char)raw[i])) i++;
                size_t start = i;
                while (i < n && !std::isspace((unsigned char)raw[i])) i++;
                if (i > start) arr->items.push_back(std::make_unique<StrLit>(raw.substr(start, i - start)));
            }
            if (arr->items.size() == 1) // <42> / <1/3> / <1e5>: numeric allomorph
                if (ExprPtr num = angleWordNumeric(static_cast<StrLit*>(arr->items[0].get())->v)) return num;
            return arr;
        }
        case Tok::Var: {
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
                advance();
                auto u = std::make_unique<Unary>();
                u->op = "ctx$"; u->operand = parsePrimary();
                return u;
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
            ExprPtr e = parseExpression();
            // statement modifier inside parens:  (42 if $x)  (42 unless $x)  (42 with $y)
            if (isIdent("if") || isIdent("unless") || isIdent("with") || isIdent("without")) {
                std::string mod = advance().text;
                ExprPtr cond = parseExpression();
                ExprPtr test = std::move(cond);
                if (mod == "with" || mod == "without") {
                    auto c = std::make_unique<Call>(); c->name = "defined";
                    c->args.push_back(std::move(test)); test = std::move(c);
                }
                bool neg = (mod == "unless" || mod == "without");
                auto tern = std::make_unique<Ternary>();
                tern->cond = std::move(test);
                auto empty = std::make_unique<ArrayLit>();
                if (neg) { tern->then = std::move(empty); tern->els = std::move(e); }
                else { tern->then = std::move(e); tern->els = std::move(empty); }
                e = std::move(tern);
            }
            else if (isIdent("for")) {
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
                u->operand = parseExpr(BP_COMMA);
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
                    // listop-style forms: `([op], 42)` (comma before the args) and
                    // the zero-argument `([op])`
                    if (isKind(Tok::Comma)) advance();
                    if (isKind(Tok::RParen) || isKind(Tok::Semicolon) || isKind(Tok::End)) {
                        auto empty = std::make_unique<ListExpr>();
                        u->operand = std::move(empty);
                    }
                    else u->operand = parseExpr(BP_COMMA);
                    return u;
                }
            }
            // triangular / scan reduce: [\+] [\~] [\*] — yields the list of running
            // partial reductions (1, 1+2, 1+2+3, …) rather than the final value.
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
                    u->operand = parseExpr(BP_COMMA);
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
                u->operand = parseExpr(BP_COMMA);
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
                      (b.kind == Tok::Op && b.text == "!")))
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
            if (t.text == "*") { advance(); return std::make_unique<WhateverExpr>(); }
            if (t.text == "**") { advance(); auto w = std::make_unique<WhateverExpr>(); w->hyper = true; return w; } // HyperWhatever (e.g. %h{**})
            if (t.text == "||") { // slip-subscript `@a[|| @dims]` / `%h{|| @keys}`: navigate by a list of indices/keys
                advance(); auto u = std::make_unique<Unary>(); u->op = "dimslip"; u->operand = parseExpr(BP_COMMA + 1); return u;
            }
            if (t.text == "\xE2\x88\x9E") { advance(); return std::make_unique<NumLit>(std::numeric_limits<double>::infinity()); } // ∞
            if (t.text == ".") return std::make_unique<VarExpr>("$_"); // .method => $_.method
            if (t.text == "\\") { advance(); return parsePrefix(); } // capture/itemize: pass through
            if (t.text == "<") {
                // qw word list  < a b c >
                advance();
                auto words = readAngleWords(">");
                if (words.size() == 1)
                    if (ExprPtr num = angleWordNumeric(words[0])) return num;
                auto arr = std::make_unique<ArrayLit>();
                for (auto& w : words) arr->items.push_back(std::make_unique<StrLit>(w));
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
                while (!isKind(Tok::RBracket) && !isKind(Tok::End)) op += advance().text;
                matchKind(Tok::RBracket);
                return std::make_unique<VarExpr>("&infix:<" + op + ">");
            }
            error("unexpected operator in term position");
        }
        case Tok::Ident: {
            std::string name = t.text;
            if (name == "True") { advance(); return std::make_unique<BoolLit>(true); }
            if (name == "False") { advance(); return std::make_unique<BoolLit>(false); }
            if (name == "self") { advance(); return std::make_unique<SelfTerm>(); }
            // mathematical constants are TERMS, never listops: `e + 1`, `pi + 0`.
            // A tight `pi()` is left as a call so it dies as an undeclared routine.
            if ((name == "pi" || name == "tau" || name == "e" || name == "\xCF\x80" || name == "\xCF\x84")
                && !(peek().kind == Tok::LParen && !peek().spaceBefore))
                { advance(); return std::make_unique<NameTerm>(name); }
            // control-flow in expression position: `... or return X`, `... or last`
            if (name == "return" || name == "return-rw" || name == "last" || name == "next" || name == "redo") {
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
            if (name == "sub" || name == "method") {
                advance();
                auto be = std::make_unique<BlockExpr>();
                be->isSub = true; // `sub {…}` as a term is a Sub, not a bare Block
                if (isKind(Tok::Ident)) advance(); // optional name (anon use)
                if (isKind(Tok::LParen)) { advance(); be->params = parseSignature(); expectKind(Tok::RParen, ")"); }
                while (!isKind(Tok::LBrace) && !isKind(Tok::End) && !isKind(Tok::Semicolon)) advance();
                if (isKind(Tok::LBrace)) { auto blk = parseBlock(); be->body = std::move(blk->stmts); }
                return be;
            }
            // anonymous type as an expression term: `$x does role {…}`, `my $r = role {…}`,
            // the explicit form `class :: does R {…}`, and a NAMED inline class
            // in value position (`class Foo {}.new` as a list element)
            if ((name == "role" || name == "class" || name == "grammar") &&
                (peek().kind == Tok::LBrace ||
                 (peek().kind == Tok::Op && peek().text == "::") ||
                 (peek().kind == Tok::Ident && peek(2).kind == Tok::LBrace))) {
                advance(); // consume the keyword (parseClass expects it already consumed)
                auto decl = parseClass(name == "role", name == "grammar");
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
            if (name == "start") {
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
                        as->value = parseExpr(listTarget ? BP_COMMA : BP_ASSIGN);
                        return as;
                    }
                    return decl;
                }
            }
            advance(); // consume the name
            // operator-name call: infix:<+>(1,2) / postfix:<i>($x) — canonical op name
            if ((name == "infix" || name == "prefix" || name == "postfix") &&
                isOp(":") && !cur().spaceBefore &&
                peek().kind == Tok::Op && peek().text == "<" && !peek().spaceBefore) {
                advance(); advance(); // : <
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
                while (!isKind(Tok::RBracket) && !isKind(Tok::End)) {
                    if (isKind(Tok::Ident)) { if (!params.empty()) params += ","; params += advance().text; }
                    else advance(); // skip commas / smileys / whitespace tokens
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
                auto c = std::make_unique<Call>();
                c->name = name;
                c->args = std::move(callArgs);
                return c;
            }
            // list-op style call without parens
            // but a capitalized bareword followed by a block is a type + block body, e.g. `if Mu { }`
            if (isKind(Tok::LBrace) && !name.empty() && std::isupper((unsigned char)name[0]))
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
                ExprPtr firstArg = parseExpr(BP_ASSIGN);
                // Indirect-object (dative): `print $*OUT: 'ok'` == `$*OUT.print('ok')`.
                // The colon must be TIGHT against the invocant (no space before) — a
                // space before ':' is an adverb (`slurp $p :bin`), not an invocant.
                // Restricted to the IO writer verbs to keep colon parsing unambiguous.
                static const std::set<std::string> dativeVerbs = {
                    "print", "say", "put", "note", "printf", "write",
                };
                if (isOp(":") && !cur().spaceBefore && dativeVerbs.count(name)) {
                    advance(); // consume the invocant-marking ':'
                    auto mc = std::make_unique<MethodCall>();
                    mc->inv = std::move(firstArg);
                    mc->method = name;
                    if (startsTermToken(cur()))
                        do { mc->args.push_back(parseExpr(BP_ASSIGN)); } while (matchKind(Tok::Comma) && startsTermToken(cur()));
                    return mc;
                }
                c->args.push_back(std::move(firstArg));
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
                // `f (a, b)` (space then a parenthesised comma-list) == `f(a, b)`:
                // flatten the single list argument into the call's args.
                if (c->args.size() == 1 && c->args[0]->kind == NK::ListExpr) {
                    auto* l = static_cast<ListExpr*>(c->args[0].get());
                    std::vector<ExprPtr> items = std::move(l->items);
                    c->args = std::move(items);
                }
                return c;
            }
            return std::make_unique<NameTerm>(name);
        }
        default:
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
    while (!isOp(close) && !isKind(Tok::End)) {
        // The closing delimiter may be glued to a following operator by the lexer:
        // `%h<a>=9` lexes `>=` as one token, `%h<a>>` lexes `>>`. Split it: the leading
        // `>` closes the word list, the remainder stays as the next token. Only when the
        // token is glued to the preceding word (no space) — a spaced `< a >= b >` word
        // list legitimately contains `>=` as a word and must not be truncated.
        if (cur().kind == Tok::Op && !cur().spaceBefore &&
            cur().text.size() > close.size() &&
            cur().text.compare(0, close.size(), close) == 0) {
            toks_[pos_].text = cur().text.substr(close.size());
            toks_[pos_].spaceBefore = false;
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

ExprPtr Parser::parseInterpString(const std::string& raw) {
    auto result = std::make_unique<InterpStr>();
    std::string lit;
    auto flush = [&]() {
        if (!lit.empty()) { result->parts.push_back(std::make_unique<StrLit>(lit)); lit.clear(); }
    };
    auto isIdentCont = [](char c) { return std::isalnum((unsigned char)c) || c == '_'; };

    size_t i = 0, n = raw.size();
    while (i < n) {
        char c = raw[i];
        if (c == '\\' && i + 1 < n) {
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
                default: lit += e; break;
            }
            i += 2;
            continue;
        }
        if (c == '{') {
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
        if (c == '$' && (i + 1 < n) && (raw[i + 1] == '/' || raw[i + 1] == '!') &&
            !(i + 2 < n && (std::isalnum((unsigned char)raw[i + 2]) || raw[i + 2] == '_'))) {
            flush();
            result->parts.push_back(std::make_unique<VarExpr>(std::string("$") + raw[i + 1]));
            i += 2;
            continue;
        }
        // numbered regex captures `$0` `$1` … and named captures `$<name>`
        if (c == '$' && (i + 1 < n) && (std::isdigit((unsigned char)raw[i + 1]) || raw[i + 1] == '<')) {
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
        if ((c == '$' || c == '@' || c == '%') &&
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
    auto blk = std::make_unique<Block>();
    while (!isKind(Tok::RBrace) && !isKind(Tok::End)) {
        if (matchKind(Tok::Semicolon)) continue;
        blk->stmts.push_back(parseStatement());
    }
    expectKind(Tok::RBrace, "}");
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
            if (isKind(Tok::Ident)) sigRetType_ = cur().text; // remember the return type
            int depth = 0;
            while (!isKind(Tok::End)) {
                if (depth == 0 && (isKind(Tok::RParen) || isKind(Tok::Semicolon))) break;
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
                if (trait == "where") p.whereExpr = parseExpr(BP_COMMA + 1);
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
            matchOp(":"); // allow :name(:$var)
            if (isKind(Tok::Var)) { p.name = cur().text; p.sigil = cur().text[0]; advance(); }
            else error("expected variable in named-parameter alias");
            p.named = true;
            if (!matchKind(Tok::RParen)) error("expected ')' in named-parameter alias");
            if (matchOp("?")) p.optional = true;
            else if (matchOp("!")) p.required = true;
            while (isIdent("where") || isIdent("is") || isIdent("returns") || isIdent("of")) {
                std::string trait = advance().text;
                if (trait == "where") p.whereExpr = parseExpr(BP_COMMA + 1);
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
        if (!named && isOp(":") && peek().kind == Tok::Ident && peek(2).kind == Tok::LParen) {
            advance(); // :
            p.namedKey = advance().text;
            advance(); // (
            if (isKind(Tok::LParen)) { // nested sub-signature form
                advance();
                p.subSig = std::make_shared<std::vector<Param>>(parseSignature(Tok::RParen));
                if (!matchKind(Tok::RParen)) error("expected ')' in nested sub-signature");
                p.name = ""; p.sigil = '$';
            } else {
                matchOp(":");
                if (isKind(Tok::Var)) { p.name = cur().text; p.sigil = cur().text[0]; advance(); }
                else error("expected variable in named-parameter alias");
            }
            if (!matchKind(Tok::RParen)) error("expected ')' in named-parameter alias");
            p.named = true; named = true; aliasBound = true;
        }
        if (!named && isOp(":") && peek().kind == Tok::Var) { advance(); named = true; } // Type :$named
        if (aliasBound) { // name (or nested sub-sig) already bound by the alias above
        } else if (matchOp("\\")) { // typed sigilless capture:  Iterator:D \iter
            if (isKind(Tok::Ident) || isKind(Tok::Var)) p.name = advance().text;
            p.sigil = '\\';
            if (!p.name.empty()) sigilless_.insert(p.name);
        } else if (isKind(Tok::Var)) {
            // compile-time twigil vars ($?VERSION) can't be parameters —
            // dynamic ($*SCHEDULER) and accessor ($.x) parameters are legal
            if (cur().text.size() > 1 && cur().text[1] == '?')
                error("Cannot use a variable with twigil '?' as a parameter");
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
            if (trait == "where") p.whereExpr = parseExpr(BP_COMMA + 1);
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
        params.push_back(std::move(p));
        if (matchOp("-->")) { // return type — remember the name; skip the rest to end of signature
            if (isKind(Tok::Ident)) sigRetType_ = cur().text;
            int depth = 0;
            while (!isKind(Tok::End)) {
                if (depth == 0 && (isKind(Tok::RParen) || isKind(Tok::Semicolon))) break;
                if (isKind(Tok::LParen) || isKind(Tok::LBracket)) depth++;
                else if (isKind(Tok::RParen) || isKind(Tok::RBracket)) depth--;
                advance();
            }
            break;
        }
        // `,` or `;` (multi-frame separator, e.g. `$;$ = Str`) both separate params
        if (!matchKind(Tok::Comma) && !matchKind(Tok::Semicolon)) break;
    }
    return params;
}

std::vector<Param> Parser::parsePointyParams() {
    // pointy blocks share the full signature grammar (types, coercions, slurpies,
    // named params, destructuring, traits) — the body brace ends the signature
    return parseSignature(Tok::LBrace);
}

StmtPtr Parser::parseSub(bool isMulti) {
    // 'sub' already consumed by caller
    auto s = std::make_unique<SubDecl>();
    s->isMulti = isMulti;
    std::string declInfix; // set when this is an `infix:<…>` declaration (for precedence traits)
    if (isOp("!")) advance(); // private method `method !name` — stored under its bare name
    if (isKind(Tok::Ident)) s->name = advance().text;
    else if (isKind(Tok::Var)) s->name = advance().text; // &-name
    // operator declaration: sub infix:<avg> / prefix:<§> / postfix:<²>
    if ((s->name == "infix" || s->name == "prefix" || s->name == "postfix" ||
         s->name == "circumfix" || s->name == "postcircumfix") && isOp(":")) {
        std::string cat = s->name;
        advance(); // :
        std::vector<std::string> w;
        if (isOp("<")) { advance(); w = readAngleWords(">"); }
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
            if (cat == "circumfix") userCircumfix_[w[0]] = w[1];
            else userPostcircumfix_[w[0]] = w[1];
        } else if (!opname.empty()) {
            s->name = cat + ":<" + opname + ">";
            if (cat == "infix") { userInfix_[opname] = BP_ADD; declInfix = opname; } // default precedence; traits may adjust
            else if (cat == "prefix") userPrefix_.insert(opname);
            else if (cat == "postfix") userPostfix_.insert(opname);
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
        sigRetType_.clear(); advance(); s->params = parseSignature();
        // a `--> T` that follows a parameter (not comma-separated) is left for us
        if (isOp("-->")) { advance(); if (isKind(Tok::Ident)) sigRetType_ = cur().text;
                           while (!isKind(Tok::RParen) && !isKind(Tok::End)) advance(); }
        expectKind(Tok::RParen, ")");
        if (!sigRetType_.empty()) s->retType = sigRetType_; // `--> T` inside the signature
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
                userInfix_[declInfix] = trait == "equiv" ? refBp : trait == "tighter" ? refBp + 5 : refBp - 5;
                continue;
            }
            if (trait == "assoc") {
                advance(); advance(); // `is` `assoc`
                std::string kind;
                if (isOp("<")) { advance(); auto w = readAngleWords(">"); if (!w.empty()) kind = w[0]; }
                if (kind == "right") userInfixRight_.insert(declInfix);
                else userInfixRight_.erase(declInfix);
                continue;
            }
        }
        if ((isIdent("of") || isIdent("returns")) && peek().kind == Tok::Ident) {
            advance(); s->retType = cur().text;
        } else if (isOp("-->") && peek().kind == Tok::Ident) {
            advance(); s->retType = cur().text;
        }
        advance();
    }
    if (isKind(Tok::LBrace)) {
        bool saved = inReactBlock_; inReactBlock_ = false; // whenever in a nested sub is out of scope
        auto blk = parseBlock();
        inReactBlock_ = saved;
        s->body = std::move(blk->stmts);
    }
    return s;
}

StmtPtr Parser::parseSubset() {
    // 'subset' already consumed:  subset NAME [of TYPE] [where EXPR] ;
    if (isKind(Tok::Ident)) advance(); // name (becomes a typeObj on use)
    if (isIdent("of")) { advance(); if (isKind(Tok::Ident)) advance(); }
    if (isIdent("where")) { advance(); parseExpr(BP_ASSIGN); } // constraint not enforced
    return std::make_unique<Block>(); // no-op declaration
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

StmtPtr Parser::parseClass(bool isRole, bool isGrammar, bool isPackage, bool isUnit) {
    // 'class'/'role'/'grammar'/'module'/'package' already consumed
    auto cd = std::make_unique<ClassDecl>();
    cd->isRole = isRole;
    cd->isGrammar = isGrammar;
    cd->isPackage = isPackage;
    if (isKind(Tok::Ident)) cd->name = advance().text;
    else if (isOp("::")) advance(); // anonymous type: `class :: does R { … }`
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
            cd->body.push_back(parseStatement());
        }
        expectKind(Tok::RBrace, "}");
        return cd;
    }
    while (isIdent("is") || isIdent("does")) {
        bool isDoes = isIdent("does");
        advance();
        if (!isDoes && isIdent("export")) { advance(); continue; } // trait, not a parent class
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
            advance();
            // optional type before the attribute var: `has Int $.x`, `has Int:D $.x`,
            // `has Array[Int] $.x` — consume the type name, any :D/:U/:_ smiley, and [..] params.
            std::string attrType;
            if (isKind(Tok::Ident)) {
                attrType = advance().text; // type name
                if (isOp(":") && (peek().kind == Tok::Ident)) { advance(); advance(); } // :D / :U / :_ smiley
                if (isKind(Tok::LBracket)) { int d = 0; do { if (isKind(Tok::LBracket)) d++; else if (isKind(Tok::RBracket)) d--; advance(); } while (d > 0 && !isKind(Tok::End)); }
            }
            if (isKind(Tok::Var)) {
                std::string vn = advance().text;
                AttrDecl a;
                a.type = attrType;
                a.sigil = vn[0];
                size_t idx = 1;
                if (vn.size() > 1 && (vn[1] == '.' || vn[1] == '!')) { a.pub = (vn[1] == '.'); idx = 2; }
                a.name = vn.substr(idx);
                // traits before the default: is rw / is readonly / of Type / does Role / where EXPR
                while (isIdent("is") || isIdent("of") || isIdent("does") || isIdent("where")) {
                    std::string tr = advance().text;
                    if (tr == "where") { parseExpr(BP_ASSIGN); continue; }
                    if (tr == "is" && isIdent("rw")) a.rw = true;
                    if (isKind(Tok::Ident) || isKind(Tok::Var)) advance();
                    if (isKind(Tok::LParen)) { int d = 0; do { if (isKind(Tok::LParen)) d++; else if (isKind(Tok::RParen)) d--; advance(); } while (d > 0 && !isKind(Tok::End)); }
                }
                if (matchOp("=") || matchOp(".=")) a.def = parseExpr(BP_ASSIGN);
                skipToStatementEnd();
                cd->attrs.push_back(std::move(a));
            } else {
                skipToStatementEnd();
            }
            continue;
        }
        if (isIdent("method") || isIdent("submethod")) {
            bool sub = isIdent("submethod");
            advance();
            auto s = parseSub(false);
            static_cast<SubDecl*>(s.get())->isMethod = true;
            static_cast<SubDecl*>(s.get())->isSubmethod = sub;
            cd->methods.push_back(std::unique_ptr<SubDecl>(static_cast<SubDecl*>(s.release())));
            continue;
        }
        if ((isIdent("multi") || isIdent("proto")) &&
            !(peek(1).text == "token" || peek(1).text == "rule" || peek(1).text == "regex")) {
            advance();
            bool isM = false, isSub = false;
            if (isIdent("method") || isIdent("submethod")) { isSub = isIdent("submethod"); advance(); isM = true; }
            else if (isIdent("sub")) advance();
            auto s = parseSub(true);
            static_cast<SubDecl*>(s.get())->isMethod = isM;
            static_cast<SubDecl*>(s.get())->isSubmethod = isSub;
            cd->methods.push_back(std::unique_ptr<SubDecl>(static_cast<SubDecl*>(s.release())));
            continue;
        }
        if (isIdent("sub")) { advance(); parseSub(false); continue; } // static sub: parsed, not yet wired
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
        // other statements (constants, phasers) are parsed and discarded.
        auto st = parseStatement();
        if (st && (st->kind == NK::ClassDecl || st->kind == NK::EnumDecl || st->kind == NK::SubDecl))
            cd->body.push_back(std::move(st));
    }
    if (braced) expectKind(Tok::RBrace, "}");
    typeStack_.pop_back();
    return cd;
}

StmtPtr Parser::parseIf(bool isUnless) {
    auto s = std::make_unique<IfStmt>();
    s->isUnless = isUnless;
    ExprPtr cond = parseExpression();
    if (matchOp("->")) { if (isKind(Tok::Var)) s->thenVar = advance().text; }
    auto blk = parseBlock();
    s->branches.emplace_back(std::move(cond), std::move(blk));
    while (isIdent("elsif")) {
        advance();
        ExprPtr c = parseExpression();
        auto b = parseBlock();
        s->branches.emplace_back(std::move(c), std::move(b));
    }
    if (isIdent("else")) {
        advance();
        s->elseBlock = parseBlock();
    }
    return s;
}

StmtPtr Parser::parseWhile(bool isUntil) {
    auto s = std::make_unique<WhileStmt>();
    s->isUntil = isUntil;
    s->cond = parseExpression();
    if (matchOp("->")) { if (isKind(Tok::Var)) s->var = advance().text; }
    s->body = parseBlock();
    return s;
}

StmtPtr Parser::parseFor() {
    auto s = std::make_unique<ForStmt>();
    s->list = parseExpression();
    if (matchOp("->") || matchOp("<->")) {
        if (isKind(Tok::LParen)) s->destructure = true; // `-> ($a,$b)`: unpack each element
        for (auto& p : parsePointyParams()) {
            if (p.subSig && p.name.empty()) // signature-style destructure: inner names
                for (auto& q : *p.subSig) s->vars.push_back(q.name);
            else s->vars.push_back(p.name);
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

StmtPtr Parser::applyModifiers(StmtPtr s) {
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
                        if (be->params.size() == 1 && be->params[0].subSig) fs->destructure = true;
                        for (auto& p : be->params) {
                            if (p.subSig && p.name.empty()) // signature-style destructure
                                for (auto& q : *p.subSig) fs->vars.push_back(q.name);
                            else fs->vars.push_back(p.name);
                        }
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
        !peek().spaceBefore && !kBlockKeywords.count(cur().text)) {
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
            // A smiley (`:D`/`:U`) or `:auth`/`:ver`/`:api` adverb on an augment
            // target is illegal — you can only augment a bare type name (S12).
            if (cur().kind == Tok::Ident && peek().kind == Tok::Op && peek().text == ":" &&
                peek(2).kind == Tok::Ident) {
                const std::string& adv = peek(2).text;
                if (adv == "D" || adv == "U" || adv == "_" || adv == "auth" || adv == "ver" || adv == "api")
                    error("cannot augment a type with a '" + adv + "' adverb");
            }
            auto st = parseClass(what == "role", what == "grammar");
            if (st->kind == NK::ClassDecl) static_cast<ClassDecl*>(st.get())->isAugment = true;
            return st;
        }
        // `unit class/role/grammar Foo;` — the rest of the file is the body.
        if (kw == "unit" && peek().kind == Tok::Ident &&
            (peek().text == "class" || peek().text == "role" || peek().text == "grammar")) {
            advance(); // unit
            std::string what = advance().text; // class/role/grammar
            return parseClass(what == "role", what == "grammar", false, /*isUnit=*/true);
        }
        if (kw == "my" || kw == "our" || kw == "state" || kw == "has" ||
            kw == "anon" || kw == "unit" || kw == "augment") {
            if (peek().kind == Tok::Ident && declKw.count(peek().text)) {
                bool wasOur = (kw == "our");
                advance(); // strip scope/unit; re-dispatch on the declaration keyword
                StmtPtr st = parseStatement();
                // `our sub`/`our multi` — remember package scope so it installs globally.
                if (wasOur && st && st->kind == NK::SubDecl) static_cast<SubDecl*>(st.get())->isOur = true;
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
            if (isKind(Tok::VersionLit)) { // `use v6;` / `use v6.d;` / `use v6.e.PREVIEW;`
                { std::string ver = advance().text; // VersionLit text is like "6.e" (no leading v)
                  // swallow any dotted tail the version lexer didn't take (.PREVIEW)
                  while (!isKind(Tok::Semicolon) && !isKind(Tok::End)) ver += advance().text;
                  u->module = (ver.empty() || ver[0] != 'v') ? "v" + ver : ver; } // exec() reads langRev from this
                matchKind(Tok::Semicolon);
                return u; // a version pragma loads no module — exec() only reads langRev from u->module
            }
            if (!isKind(Tok::Semicolon) && !isKind(Tok::End)) u->module = advance().text;
            if (u->module == "lib" && !isKind(Tok::Semicolon) && !isKind(Tok::End) &&
                !isKind(Tok::StrLit) && !isKind(Tok::StrInterp)) {
                u->argExpr = parseExpression(); // `use lib $?FILE.IO.parent`
            } else {
                // capture first string argument, e.g. `use lib 'lib'`
                while (!isKind(Tok::Semicolon) && !isKind(Tok::End)) {
                    if ((isKind(Tok::StrLit) || isKind(Tok::StrInterp)) && u->arg.empty()) u->arg = cur().text;
                    advance();
                }
            }
            matchKind(Tok::Semicolon);
            return u;
        }
        if (kw == "sub") { advance(); return parseSub(false); }
        if (kw == "multi" || kw == "proto") {
            advance();
            if (isIdent("sub")) advance();
            return parseSub(true);
        }
        if (kw == "if") { advance(); return parseIf(false); }
        if (kw == "unless") { advance(); return parseIf(true); }
        if (kw == "while") { advance(); return parseWhile(false); }
        if (kw == "until") { advance(); return parseWhile(true); }
        if (kw == "for") { advance(); return parseFor(); }
        if (kw == "loop") {
            advance();
            if (isKind(Tok::LParen)) {
                advance();
                auto ls = std::make_unique<LoopStmt>();
                if (!isKind(Tok::Semicolon)) ls->init = parseExpression();
                expectKind(Tok::Semicolon, ";");
                if (!isKind(Tok::Semicolon)) ls->cond = parseExpression();
                expectKind(Tok::Semicolon, ";");
                if (!isKind(Tok::RParen)) ls->incr = parseExpression();
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
            g->topic = parseExpression();
            if (isOp("->") || isOp("<->")) { // `given X -> $y is copy { }`
                advance();
                auto ps = parsePointyParams();
                if (!ps.empty() && !ps[0].name.empty() && ps[0].name != "$_") g->var = ps[0].name;
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
            if (g->hasElse) { advance(); g->elseBody = parseBlock(); }
            return g;
        }
        if (kw == "when") {
            advance();
            auto w = std::make_unique<WhenStmt>();
            w->cond = parseExpression();
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
                                         kw == "module" || kw == "package");
        }
        if (kw == "subset") { advance(); return parseSubset(); }
        if (kw == "enum") { advance(); return parseEnum(); }
        if (kw == "CATCH" || kw == "CONTROL") {
            advance();
            auto blk = parseBlock();
            blk->isCatch = true;
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
              (b.kind == Tok::Op && b.text == "!"))) ||
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

Program Parser::parseProgram() {
    Program prog;
    while (!isKind(Tok::End)) {
        if (matchKind(Tok::Semicolon)) continue;
        prog.stmts.push_back(parseStatement());
        matchKind(Tok::Semicolon);
    }
    return prog;
}

} // namespace rakupp
