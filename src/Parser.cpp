#include "Parser.h"
#include "Lexer.h"
#include "Unicode.h"
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <set>
#include <unordered_set>

namespace rakupp {

// binding powers (higher = tighter)
// Raku orders these (tightest→loosest): additive `+ -` > replication `x xx` >
// concatenation `~` > range. So `"-" x $n + 2` == `"-" x ($n + 2)` and
// `"x" x 2 ~ "y"` == `("x" x 2) ~ "y"`. Each gets a distinct level.
enum {
    BP_OR = 1, BP_AND = 2, BP_COMMA = 3, BP_ASSIGN = 4, BP_TERNARY = 5,
    BP_OROR = 6, BP_ANDAND = 7, BP_COMPARE = 8, BP_RANGE = 9,
    BP_CONCAT = 10, BP_REPLICATE = 11, BP_ADD = 12, BP_MUL = 13, BP_POW = 14, BP_PREFIX = 15
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
        if (o == "+" || o == "-") { in.valid = true; in.lbp = BP_ADD; return in; }
        if (o == "~") { in.valid = true; in.lbp = BP_CONCAT; return in; } // concatenation: looser than x/xx
        if (o == ".." || o == "..^" || o == "^.." || o == "^..^") { in.valid = true; in.lbp = BP_RANGE; in.isRange = true; return in; }
        if (o == "..." || o == "...^") { in.valid = true; in.lbp = BP_COMMA; return in; } // sequence op: looser than comma, so `1,3 ... 19` seeds with (1,3)
        if (o == "==>" || o == "<==") { in.valid = true; in.lbp = BP_OR; return in; } // feed operators (very loose, left-assoc)
        if (o == "==" || o == "!=" || o == "<" || o == "<=" || o == ">" || o == ">=" ||
            o == "<=>" || o == "~~" || o == "!~~" || o == "=:=" || o == "===" || o == "!==" ||
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
        return in;
    }
    if (t.kind == Tok::Ident) {
        const std::string& o = t.text;
        in.op = o;
        if (o == "eq" || o == "ne" || o == "lt" || o == "gt" || o == "le" || o == "ge" ||
            o == "cmp" || o == "leg" || o == "eqv" || o == "before" || o == "after") { in.valid = true; in.lbp = BP_COMPARE; return in; }
        if (o == "x" || o == "xx") { in.valid = true; in.lbp = BP_REPLICATE; return in; }
        if (o == "div" || o == "mod" || o == "gcd" || o == "lcm") {
            in.valid = true; in.lbp = BP_MUL; return in;
        }
        if (o == "does" || o == "but") { in.valid = true; in.lbp = BP_MUL; return in; }
        if (o == "Z" || o == "X") { in.valid = true; in.lbp = BP_ADD; return in; } // zip / cross
        if (o == "and" || o == "andthen") { in.valid = true; in.lbp = BP_AND; return in; }
        if (o == "or" || o == "xor" || o == "orelse") { in.valid = true; in.lbp = BP_OR; return in; }
        return in;
    }
    return in;
}

static bool startsTermToken(const Token& t) {
    switch (t.kind) {
        case Tok::IntLit: case Tok::NumLit: case Tok::StrLit: case Tok::StrInterp: case Tok::RegexLit: case Tok::SubstLit:
        case Tok::QwList:
        case Tok::Var: case Tok::LParen: case Tok::LBracket: case Tok::LBrace:
            return true;
        case Tok::Op:
            return t.text == "!" || t.text == "~" || t.text == "\\" || t.text == "<" ||
                   t.text == "+" || t.text == "-" || t.text == "?" || t.text == ":" ||
                   t.text == "*" || t.text == "->" || t.text == "<->" || t.text == "|" ||
                   t.text == "." || // leading `.method` => $_.method (e.g. `1, .uc`)
                   t.text == "\xE2\x88\x9E" || t.text == "\xC2\xAB" || // ∞  and  «qw»
                   t.text == "$" || t.text == "@" || t.text == "%"; // contextualizers $( $[ @( %(
        case Tok::Ident:
            // sub/method/do/start begin an expression (anonymous routine / do-block) even though block keywords
            return !kBlockKeywords.count(t.text) ||
                   t.text == "sub" || t.text == "method" || t.text == "do" || t.text == "start";
        default:
            return false;
    }
}
// what may begin a list-op argument (no parens; conservative on leading symbols)
static bool startsListopArg(const Token& t) {
    switch (t.kind) {
        case Tok::IntLit: case Tok::NumLit: case Tok::StrLit: case Tok::StrInterp: case Tok::RegexLit: case Tok::SubstLit:
        case Tok::QwList:
        case Tok::Var: case Tok::LBracket: case Tok::LParen: case Tok::LBrace:
            return true;
        case Tok::Op:
            return t.text == "!" || t.text == "~" || t.text == "\\" || t.text == "<" ||
                   t.text == ":" || t.text == "+" || t.text == "-" || t.text == "?" ||
                   t.text == "^" || // prefix `^N` (upto) as a listop arg: `flat ^15, 49`
                   (t.text == "|" && t.spaceBefore) || // slip first arg `run |@cmd` (space before |) — NOT infix junction `Any|Blob`
                   t.text == "!!" || // prefix boolify `say !!$x` (`!!` never starts a bare term otherwise)
                   (t.text == "." && t.spaceBefore) || // leading `.method` => $_.method (only after a space: `say .uc`)
                   t.text == "\xE2\x88\x9E" || t.text == "\xC2\xAB"; // ∞  and  «qw»
        case Tok::Ident: {
            // A word-infix operator right after a bareword term is an INFIX, not the
            // start of a listop argument: `Seq eqv Seq`, `Int eq Int`, `$x div $y`.
            static const std::set<std::string> wordInfix = {
                "eq", "ne", "lt", "gt", "le", "ge", "cmp", "leg", "eqv", "before", "after",
                "x", "xx", "and", "or", "andthen", "orelse", "div", "mod", "gcd", "lcm",
            };
            if (wordInfix.count(t.text)) return false;
            // sub/method/do/start begin an expression (anonymous routine / do-block) even though block keywords
            return !kBlockKeywords.count(t.text) ||
                   t.text == "sub" || t.text == "method" || t.text == "do" || t.text == "start";
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
            if (BP_ADD < minbp) break; // default (additive) precedence, left-assoc
            std::string opname = advance().text;
            auto call = std::make_unique<Call>();
            call->name = "infix:<" + opname + ">";
            call->args.push_back(std::move(lhs));
            call->args.push_back(parseExpr(BP_ADD + 1));
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

        advance(); // consume infix op

        // list assignment: `@a = 1,2,3` / `my ($a,$b) = ...` grabs the whole comma list
        bool listAssign = false;
        if (in.isAssign && in.op == "=") {
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

ExprPtr Parser::parsePrefix() {
    if (cur().kind == Tok::Op) {
        const std::string& o = cur().text;
        if (o == "!" || o == "-" || o == "+" || o == "~" || o == "?" ||
            o == "++" || o == "--" || o == "^" || o == "|") {
            advance();
            auto u = std::make_unique<Unary>();
            u->op = o; u->operand = parseExpr(BP_PREFIX);
            return u;
        }
        if (o == "!!") { // prefix double-negation == boolify
            advance();
            auto u = std::make_unique<Unary>();
            u->op = "?"; u->operand = parseExpr(BP_PREFIX);
            return u;
        }
        // contextualizer: $(...) @(...) %(...) $@foo etc.
        if (o == "$" || o == "@" || o == "%") {
            advance();
            auto u = std::make_unique<Unary>();
            // parsePrefix (not just primary) so nested contextualizers like $%( ... ) work
            u->op = "ctx" + o; u->operand = parsePrefix();
            return u;
        }
    }
    return parsePostfix(parsePrimary());
}

std::vector<ExprPtr> Parser::parseCallArgs() {
    std::vector<ExprPtr> args;
    if (isKind(Tok::RParen)) { advance(); return args; }
    ExprPtr e = parseExpression();
    if (e->kind == NK::ListExpr) {
        auto* l = static_cast<ListExpr*>(e.get());
        for (auto& it : l->items) args.push_back(std::move(it));
    } else {
        args.push_back(std::move(e));
    }
    expectKind(Tok::RParen, ")");
    return args;
}

ExprPtr Parser::parsePostfix(ExprPtr base) {
    bool hyperNext = false;
    for (;;) {
        // hyper method call: @a>>.method / @a<<.method (»« multibyte too)
        if ((isOp(">>") || isOp("<<") || isOp("»") || isOp("«")) && peek().kind == Tok::Op && peek().text == ".") {
            advance(); hyperNext = true; continue;
        }
        // subscript adverb: %h{k}:exists / :delete / :!exists / :kv / :k / :v / :p
        if (isOp(":") && base->kind == NK::Index &&
            static_cast<Index*>(base.get())->adverb.empty() &&
            (peek().kind == Tok::Ident || (peek().kind == Tok::Op && peek().text == "!"))) {
            advance(); // :
            std::string adv;
            if (isOp("!")) { advance(); adv = "!"; }
            if (isKind(Tok::Ident)) adv += advance().text;
            static_cast<Index*>(base.get())->adverb = adv;
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
            if (cur().kind == Tok::Ident || cur().kind == Tok::Var) {
                mc->method = advance().text;
            } else if (cur().kind == Tok::StrLit) {
                mc->method = advance().text;           // ."literal-name"()
            } else if (cur().kind == Tok::StrInterp) {
                mc->methodExpr = parsePrimary();       // ."$name"() — computed at runtime
            } else {
                error("expected method name after '.'");
            }
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
        } else if (isOp("++") || isOp("--")) {
            auto u = std::make_unique<Unary>();
            u->op = advance().text; u->postfix = true; u->operand = std::move(base);
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
void Parser::skipTraits() {
    while (isIdent("is") || isIdent("does") || isIdent("returns") || isIdent("of")) {
        advance();
        if (isKind(Tok::Ident) || isKind(Tok::Var)) advance(); // trait name / type
        if (isKind(Tok::LParen)) { int d = 0; do { if (isKind(Tok::LParen)) d++; else if (isKind(Tok::RParen)) d--; advance(); } while (d > 0 && !isKind(Tok::End)); }
        if (isKind(Tok::LBracket)) { int d = 0; do { if (isKind(Tok::LBracket)) d++; else if (isKind(Tok::RBracket)) d--; advance(); } while (d > 0 && !isKind(Tok::End)); }
    }
}

ExprPtr Parser::parseDeclarator(const std::string& scope) {
    if (matchOp("\\")) {
        // sigilless: my \x = ...
        std::string nm = (isKind(Tok::Ident) || isKind(Tok::Var)) ? advance().text : "";
        auto ve = std::make_unique<VarExpr>(nm);
        ve->declare = true; ve->declScope = scope;
        return ve;
    }
    // type-capture declaration:  my ::T $x  (binds T to the type of $x; we just parse it)
    if (isOp("::") && peek().kind == Tok::Ident) { advance(); advance(); }
    std::string type;
    if (isKind(Tok::Ident)) {
        bool looksType = peek().kind == Tok::Var || peek().kind == Tok::LParen ||
                         peek().kind == Tok::LBracket ||
                         (peek().kind == Tok::Op && (peek().text == ":" || peek().text == "\\"));
        if (looksType) {
            type = advance().text;
            if (isOp(":") && peek().kind == Tok::Ident) { advance(); advance(); } // :D / :U / :_ smiley
            if (isKind(Tok::LBracket)) { int d = 0; do { if (isKind(Tok::LBracket)) d++; else if (isKind(Tok::RBracket)) d--; advance(); } while (d > 0 && !isKind(Tok::End)); }
        }
    }
    // sigilless after an optional type:  my Mu \x = …   (bare `my \x` handled above)
    if (matchOp("\\")) {
        std::string nm = (isKind(Tok::Ident) || isKind(Tok::Var)) ? advance().text : "";
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
        ve->declare = true; ve->declScope = scope; ve->declType = type;
        skipTraits();
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
        skipTraits();
        return ve;
    }
    error("expected variable after declarator");
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
        long long val = 0;
        for (char c : digits) {
            if (c == '_') continue;
            long long d = (c >= '0' && c <= '9') ? c - '0'
                        : (c >= 'a' && c <= 'z') ? c - 'a' + 10
                        : (c >= 'A' && c <= 'Z') ? c - 'A' + 10 : -1;
            if (d < 0 || d >= base) break; // stop at a fractional '.' or invalid digit
            val = val * base + d;
        }
        return std::make_unique<IntLit>(val);
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

ExprPtr Parser::parsePrimary() {
    // symbolic name reference in term position: `::Foo::Bar` → the named type/package
    if (isOp("::") && peek().kind == Tok::Ident) {
        advance(); // ::
        std::string name = advance().text;
        while (isOp("::") && peek().kind == Tok::Ident) { advance(); name += "::" + advance().text; }
        return std::make_unique<NameTerm>(name);
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
            if (isRat) {
                // "15.8972" -> numerator 158972, denominator 10^(fractional digits)
                size_t dot = txt.find('.');
                std::string digits = txt.substr(0, dot) + txt.substr(dot + 1);
                long long fracLen = (long long)(txt.size() - dot - 1);
                long long den = 1; for (long long k = 0; k < fracLen && den < 1000000000000000LL; k++) den *= 10;
                e->isRat = true;
                e->ratNum = std::strtoll(digits.c_str(), nullptr, 10);
                e->ratDen = den;
            }
            return e;
        }
        case Tok::StrLit: {
            bool fmt = cur().flag; auto e = std::make_unique<StrLit>(advance().text);
            if (fmt) { auto c = std::make_unique<Call>(); c->name = "__format__"; c->args.push_back(std::move(e)); return c; }
            return e;
        }
        case Tok::StrInterp: {
            bool fmt = cur().flag; std::string raw = advance().text; auto e = parseInterpString(raw);
            if (fmt) { auto c = std::make_unique<Call>(); c->name = "__format__"; c->args.push_back(std::move(e)); return c; }
            return e;
        }
        case Tok::RegexLit: { auto e = std::make_unique<RegexLit>(advance().text); return e; }
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
            return arr;
        }
        case Tok::Var: { int ln = cur().line; auto e = std::make_unique<VarExpr>(advance().text); e->line = ln; return e; }
        case Tok::LParen: {
            advance();
            if (isKind(Tok::RParen)) { advance(); return std::make_unique<ListExpr>(); }
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
            // reduction metaoperator: [+] [*] [~] [max] [<] ...
            if ((peek(1).kind == Tok::Op || peek(1).kind == Tok::Ident) &&
                peek(2).kind == Tok::RBracket && peek(1).text != "]") {
                advance(); // [
                std::string innerOp = advance().text; // operator
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
                auto arr = std::make_unique<ArrayLit>();
                for (auto& w : readAngleWords(">")) arr->items.push_back(std::make_unique<StrLit>(w));
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
            if (name == "return" || name == "last" || name == "next" || name == "redo") {
                advance();
                auto u = std::make_unique<Unary>();
                u->op = name;
                bool retTerm = startsTermToken(cur()) && !kBlockKeywords.count(cur().text);
                // sub/method/do/start blocks are valid expression operands too
                if (isIdent("sub") || isIdent("method") || isIdent("do") || isIdent("start")) retTerm = true;
                if (name == "return" && retTerm &&
                    !isKind(Tok::RParen) && !isKind(Tok::Semicolon) && !isKind(Tok::RBrace))
                    u->operand = parseExpr(BP_ASSIGN);
                return u;
            }
            if (name == "sub" || name == "method") {
                advance();
                auto be = std::make_unique<BlockExpr>();
                if (isKind(Tok::Ident)) advance(); // optional name (anon use)
                if (isKind(Tok::LParen)) { advance(); be->params = parseSignature(); expectKind(Tok::RParen, ")"); }
                while (!isKind(Tok::LBrace) && !isKind(Tok::End) && !isKind(Tok::Semicolon)) advance();
                if (isKind(Tok::LBrace)) { auto blk = parseBlock(); be->body = std::move(blk->stmts); }
                return be;
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
            if (name == "do" || name == "try" || name == "gather" || name == "quietly") {
                advance();
                auto u = std::make_unique<Unary>();
                u->op = name;
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
                    be->body.push_back(parseStatement());
                    u->operand = std::move(be);
                } else {
                    u->operand = parseExpr(BP_PREFIX);
                }
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
                    be->body.push_back(parseStatement());
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
                return parseDeclarator(name);
            }
            advance(); // consume the name
            // type smiley on a type name: Channel:U / Foo:D / Bar:_  (definedness ignored)
            if (!name.empty() && std::isupper((unsigned char)name[0]) &&
                isOp(":") && !cur().spaceBefore && peek().kind == Tok::Ident &&
                (peek().text == "U" || peek().text == "D" || peek().text == "_")) {
                advance(); advance(); // : and the smiley letter
                return std::make_unique<NameTerm>(name);
            }
            if (isKind(Tok::LParen) && !cur().spaceBefore) {
                advance();
                auto c = std::make_unique<Call>();
                c->name = name;
                c->args = parseCallArgs();
                return c;
            }
            // list-op style call without parens
            // but a capitalized bareword followed by a block is a type + block body, e.g. `if Mu { }`
            if (isKind(Tok::LBrace) && !name.empty() && std::isupper((unsigned char)name[0]))
                return std::make_unique<NameTerm>(name);
            // For +/-/? the prefix reading is only valid when the operand is
            // tight against the operator (`f -5` => f(-5), but `f - 5` => f() - 5).
            bool listopOk = startsListopArg(cur());
            if (listopOk && cur().kind == Tok::Op &&
                (cur().text == "+" || cur().text == "-" || cur().text == "?" || cur().text == "|" || cur().text == "!!") &&
                peek(1).spaceBefore)
                listopOk = false; // `f -5` => f(-5) but `f - 5` => f() - 5; likewise `run |@x` slip;
                                   // and `Nil !! Any` (space after !!) is a ternary else-marker, not `Nil(!!Any)`
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
static ExprPtr parseEmbeddedExpr(const std::string& src) {
    Lexer lx(src);
    Parser p(lx.tokenize());
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
                            raw[i + 1] == '.' || (unsigned char)raw[i + 1] >= 0x80)) {
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
            // twigil
            if (raw[j] == '*' || raw[j] == '!' || raw[j] == '.') var += raw[j++];
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
                    if (trait == "is" && (isIdent("rw") || isIdent("copy"))) p.isRw = (cur().text == "rw");
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
            if (isOp(":") && (peek().kind == Tok::Ident)) { // :D / :U / :_ smiley
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
            if (isKind(Tok::LParen)) {
                int depth = 0;
                do { if (isKind(Tok::LParen)) depth++; else if (isKind(Tok::RParen)) depth--; advance(); }
                while (depth > 0 && !isKind(Tok::End));
            }
        }
        if (!named && isOp(":") && peek().kind == Tok::Var) { advance(); named = true; } // Type :$named
        if (matchOp("\\")) { // typed sigilless capture:  Iterator:D \iter
            if (isKind(Tok::Ident) || isKind(Tok::Var)) p.name = advance().text;
            p.sigil = '\\';
        } else if (isKind(Tok::Var)) {
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
                advance();
            }
        }
        if (matchOp("=")) p.defaultVal = parseExpr(BP_ASSIGN);
        params.push_back(std::move(p));
        if (matchOp("-->")) { // return type — discarded; skip to end of signature (smileys, params)
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
    std::vector<Param> ps;
    while (!isKind(Tok::LBrace) && !isKind(Tok::End)) {
        if (matchOp("-->")) { if (isKind(Tok::Ident) || isKind(Tok::Var)) advance(); break; } // return type
        Param p;
        if (isKind(Tok::LParen)) { // destructuring: -> ($a, $b) {} / -> (:key($k), :value($v)) {}
            advance();
            while (!isKind(Tok::RParen) && !isKind(Tok::End)) {
                if (isOp(":")) { // :name($var) — bind $var
                    advance();
                    if (isKind(Tok::Ident)) advance(); // name
                    if (isKind(Tok::LParen)) {
                        advance();
                        if (isKind(Tok::Var)) { Param q; q.name = cur().text; q.sigil = cur().text[0]; advance(); ps.push_back(std::move(q)); }
                        while (!isKind(Tok::RParen) && !isKind(Tok::End)) advance();
                        matchKind(Tok::RParen);
                    }
                } else {
                    if (isKind(Tok::Ident) && peek().kind == Tok::Var) advance(); // type
                    if (isKind(Tok::Var)) { Param q; q.name = cur().text; q.sigil = cur().text[0]; advance(); ps.push_back(std::move(q)); }
                    else advance();
                }
                if (!matchKind(Tok::Comma)) break;
            }
            expectKind(Tok::RParen, ")");
            if (!matchKind(Tok::Comma)) break;
            continue;
        }
        if (matchOp("\\")) {
            if (isKind(Tok::Var) || isKind(Tok::Ident)) p.name = advance().text;
            p.sigil = '\\';
        } else {
            if (isKind(Tok::Ident) && peek().kind == Tok::Var) advance(); // type
            if (isKind(Tok::Var)) { p.name = cur().text; p.sigil = cur().text[0]; advance(); }
            else break;
        }
        if (matchOp("?")) p.optional = true;
        else if (matchOp("!")) {}
        while (isIdent("is") || isIdent("where") || isIdent("returns") || isIdent("of")) {
            std::string trait = advance().text;
            if (trait == "where") p.whereExpr = parseExpr(BP_COMMA + 1);
            else if (!isKind(Tok::LBrace) && !isKind(Tok::Comma) && !isKind(Tok::End) && !isOp("=")) {
                if (trait == "is" && (isIdent("rw") || isIdent("copy"))) p.isRw = (cur().text == "rw");
                advance();
            }
        }
        if (matchOp("=")) p.defaultVal = parseExpr(BP_ASSIGN);
        ps.push_back(std::move(p));
        if (matchOp("-->")) { if (isKind(Tok::Ident) || isKind(Tok::Var)) advance(); break; } // return type
        if (!matchKind(Tok::Comma)) break;
    }
    return ps;
}

StmtPtr Parser::parseSub(bool isMulti) {
    // 'sub' already consumed by caller
    auto s = std::make_unique<SubDecl>();
    s->isMulti = isMulti;
    if (isOp("!")) advance(); // private method `method !name` — stored under its bare name
    if (isKind(Tok::Ident)) s->name = advance().text;
    else if (isKind(Tok::Var)) s->name = advance().text; // &-name
    // operator declaration: sub infix:<avg> / prefix:<§> / postfix:<²>
    if ((s->name == "infix" || s->name == "prefix" || s->name == "postfix" ||
         s->name == "circumfix" || s->name == "postcircumfix") && isOp(":")) {
        std::string cat = s->name;
        advance(); // :
        std::string opname;
        if (isOp("<")) { advance(); auto w = readAngleWords(">"); opname = w.empty() ? "" : w[0]; }
        else if (isOp("\xC2\xAB")) { advance(); auto w = readAngleWords("\xC2\xBB"); opname = w.empty() ? "" : w[0]; }
        if (!opname.empty()) {
            s->name = cat + ":<" + opname + ">";
            if (cat == "infix") userInfix_.insert(opname);
            else if (cat == "prefix") userPrefix_.insert(opname);
            else if (cat == "postfix") userPostfix_.insert(opname);
        }
    }
    if (isKind(Tok::LParen)) { advance(); s->params = parseSignature(); expectKind(Tok::RParen, ")"); }
    // optional return type / traits up to block: skip until '{'
    while (!isKind(Tok::LBrace) && !isKind(Tok::End) && !isKind(Tok::Semicolon)) advance();
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

StmtPtr Parser::parseClass(bool isRole, bool isGrammar, bool isPackage) {
    // 'class'/'role'/'grammar'/'module'/'package' already consumed
    auto cd = std::make_unique<ClassDecl>();
    cd->isRole = isRole;
    cd->isGrammar = isGrammar;
    cd->isPackage = isPackage;
    if (isKind(Tok::Ident)) cd->name = advance().text;
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
        if (isKind(Tok::Ident) || isKind(Tok::Var)) {
            std::string t = advance().text;
            if (cd->parent.empty()) { cd->parent = t; cd->parentIsDoes = isDoes; }
            else if (isDoes) cd->roles.push_back(t);      // extra `does Role` — composed in
            else cd->extraParents.push_back(t);            // extra `is Class` — multiple inheritance
        }
        // skip type params / extra
        while (isKind(Tok::LBracket)) { int d = 0; do { if (isKind(Tok::LBracket)) d++; else if (isKind(Tok::RBracket)) d--; advance(); } while (d > 0 && !isKind(Tok::End)); }
    }
    if (!isKind(Tok::LBrace)) {
        while (!isKind(Tok::Semicolon) && !isKind(Tok::End)) advance();
        return cd;
    }
    advance(); // {
    while (!isKind(Tok::RBrace) && !isKind(Tok::End)) {
        if (matchKind(Tok::Semicolon)) continue;
        if (isIdent("has")) {
            advance();
            // optional type before the attribute var: `has Int $.x`, `has Int:D $.x`,
            // `has Array[Int] $.x` — consume the type name, any :D/:U/:_ smiley, and [..] params.
            if (isKind(Tok::Ident)) {
                advance(); // type name
                if (isOp(":") && (peek().kind == Tok::Ident)) { advance(); advance(); } // :D / :U / :_ smiley
                if (isKind(Tok::LBracket)) { int d = 0; do { if (isKind(Tok::LBracket)) d++; else if (isKind(Tok::RBracket)) d--; advance(); } while (d > 0 && !isKind(Tok::End)); }
            }
            if (isKind(Tok::Var)) {
                std::string vn = advance().text;
                AttrDecl a;
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
    expectKind(Tok::RBrace, "}");
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
        for (auto& p : parsePointyParams()) s->vars.push_back(p.name);
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
        if (kw == "my" || kw == "our" || kw == "state" || kw == "has" ||
            kw == "anon" || kw == "unit" || kw == "augment") {
            if (peek().kind == Tok::Ident && declKw.count(peek().text)) {
                advance(); // strip scope/unit; re-dispatch on the declaration keyword
                return parseStatement();
            }
            // typed scoped decl:  my Int sub / my Num constant / our Str sub
            if (peek().kind == Tok::Ident && peek(2).kind == Tok::Ident && declKw.count(peek(2).text)) {
                advance(); advance(); // strip scope and type
                return parseStatement();
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
        if (kw == "return") {
            advance();
            auto r = std::make_unique<ReturnStmt>();
            if (!isKind(Tok::Semicolon) && !isKind(Tok::End) && !isKind(Tok::RBrace) &&
                cur().kind != Tok::Ident) {
                r->value = parseExpression();
            } else if ((startsTermToken(cur()) && !kBlockKeywords.count(cur().text)) ||
                       isIdent("sub") || isIdent("method") || isIdent("do") || isIdent("start")) {
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
            (a.kind == Tok::Op && a.text == ":" && b.kind == Tok::Ident) ||
            (a.kind == Tok::Var && !a.text.empty() && a.text[0] == '%' &&
             (b.kind == Tok::RBrace || b.kind == Tok::Comma));
        if (!looksHash) {
            auto blk = parseBlock();
            return applyModifiers(std::move(blk));
        }
        // else fall through: parseExpression -> parsePrimary builds a HashLit
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
