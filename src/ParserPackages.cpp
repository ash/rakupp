// The parser's side of PACKAGE SYMBOLS: `Foo::<$x>` with an adverb or a
// slice, `Foo::<$x>:exists`, and the tags of an `is export(:a :b)` trait. Split out of Parser.cpp, whose size it was; it
// rides the same `parse` archive (CMakeLists.txt).
#include "AsciiCtype.h"
#include "Parser.h"
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace rakupp {

// `Foo::<$x>:exists` / `:!exists` asks the package's table for `qual`, not the
// autovivifying read (O31: the adverb went to the enclosing call). Null: none.
ExprPtr Parser::pkgExistsAdverb(const std::string& qual) {
    const bool neg = isOp(":") && !cur().spaceBefore && peek().kind == Tok::Op && peek().text == "!" &&
                     peek(2).kind == Tok::Ident && peek(2).text == "exists";
    if (!(isOp(":") && !cur().spaceBefore && (neg || (peek().kind == Tok::Ident && peek().text == "exists"))))
        return nullptr;
    advance(); if (neg) advance(); advance();   // ':' '!' exists
    auto c = std::make_unique<Call>();
    c->name = "__sym-exists";
    c->args.push_back(std::make_unique<StrLit>(qual));
    c->args.push_back(std::make_unique<IntLit>(0));
    c->args.push_back(std::make_unique<StrLit>("PKG"));
    if (!neg) return c;
    auto no = std::make_unique<Unary>();
    no->op = "!";
    no->operand = std::move(c);
    return no;
}

// `Foo::<…>` as a SLICE, or with any adverb but `:exists` (`Foo::<$x>:p`,
// `:kv`, `:delete`): a subscript of the package's Stash, as `Foo::{…}` is,
// for the postfix that follows to put its adverb on. As one slot the `:p`
// was a Pair of its own after it (#202). Null: neither.
ExprPtr Parser::pkgStashSubscript(const std::string& pkg, const std::vector<std::string>& words) {
    const bool adverb = isOp(":") && !cur().spaceBefore &&
        ((peek().kind == Tok::Ident && peek().text != "exists") ||
         (peek().kind == Tok::Op && peek().text == "!" && peek(2).kind == Tok::Ident && peek(2).text != "exists"));
    if (words.size() < 2 && !adverb) return nullptr;
    auto who = std::make_unique<MethodCall>();
    who->inv = std::make_unique<NameTerm>(pkg);
    who->method = "WHO";
    auto ix = std::make_unique<Index>();
    ix->base = std::move(who);
    ix->isHash = true;
    ix->angleKey = true;
    if (words.size() == 1) ix->index = std::make_unique<StrLit>(words[0]);
    else {
        auto al = std::make_unique<ArrayLit>();
        for (auto& w : words) al->items.push_back(std::make_unique<StrLit>(w));
        ix->index = std::move(al);
    }
    return ix;
}

// `is export(:a, :b)` — the tags, read from the `(` through its `)`. A value
// on a tag (`:a<x>`) is skipped; the tag is its name. No `(`: no tags, which
// means DEFAULT.
std::vector<std::string> Parser::readExportTags() {
    std::vector<std::string> tags;
    if (!isKind(Tok::LParen) || cur().spaceBefore) return tags;
    int d = 0;
    do {
        if (isKind(Tok::LParen)) d++;
        else if (isKind(Tok::RParen)) d--;
        else if (d == 1 && isOp(":") && peek().kind == Tok::Ident) {
            advance();
            tags.push_back(advance().text);
            continue;
        }
        advance();
    } while (d > 0 && !isKind(Tok::End));
    return tags;
}
void Parser::takeExportTrait(VarExpr* ve) {
    if (lastIsExport_) { ve->dm().declExport = true; ve->dm().exportTags = std::move(lastExportTags_); }
    lastIsExport_ = false;
    lastExportTags_.clear();
}

// `::?CLASS` / `::?ROLE` at cur() as a TYPE (`my ::?CLASS:U $c`, `has
// ::?CLASS $.x`, `returns ::?CLASS`, `subset S of ::?ROLE:D`): consumed, and
// `out` is the innermost role for ROLE, the innermost type for CLASS, or
// `none` outside every type. False, with nothing consumed, when it is not here.
bool Parser::enclosingTypeHere(std::string& out, const char* none) {
    if (!(isOp("::") && peek().kind == Tok::Op && peek().text == "?" && peek(2).kind == Tok::Ident)) return false;
    advance(); advance();                                   // :: ?
    const bool role = advance().text == "ROLE";             // CLASS / ROLE
    out = role && !enclosingRoleName().empty() ? enclosingRoleName()
        : typeStack_.empty() ? std::string(none) : typeStack_.back();
    return true;
}

// `CALLERS::('$_')`, `M::('$q')`: a package name, then a parenthesized name
// to look up in it, at run time — `::("M::" ~ EXPR)`, which it called a sub
// `M::` (rakudo#2639). CALLER and CALLERS take the `<…>` forms' node, so a
// pseudo-package's head is resolved as `CALLERS::<$_>` resolves it.
ExprPtr Parser::parsePkgNameLookup() {
    const std::string pk = advance().text;   // NAME::
    advance();                               // (
    ExprPtr name = parseExpression();
    expectKind(Tok::RParen, "expected ')' after ::(");
    auto sr = std::make_unique<SymbolicRef>();
    if (pk == "CALLER::" || pk == "CALLERS::") {
        sr->pkg = pk.substr(0, pk.size() - 2);
        sr->nameExpr = std::move(name);
        return sr;
    }
    auto cat = std::make_unique<Binary>();
    cat->op = "~";
    cat->lhs = std::make_unique<StrLit>(pk);
    cat->rhs = std::move(name);
    sr->nameExpr = std::move(cat);
    return sr;
}

// `FIRST($_ * 10)`, `ENTER(1)`: a phaser's name with a TIGHT paren is a CALL,
// as in Rakudo, of a routine nothing declares (cur() is the name). It ran the
// phaser over the parenthesized expression.
void Parser::refusePhaserCall(const std::string& name) {
    static const std::set<std::string> kPhasers = {"INIT", "BEGIN", "CHECK", "ENTER", "FIRST", "END", "LEAVE",
        "NEXT", "LAST", "KEEP", "UNDO", "PRE", "POST", "CATCH", "CONTROL", "QUIT", "CLOSE"};
    if (!kPhasers.count(name) || peek().kind != Tok::LParen || peek().spaceBefore || kwCallHere(name) ||
        declaredSubNames_.count(name) || importsModules_) return;
    throw ParseError("Undeclared routine:\n    " + name + " used at line " + std::to_string(cur().line), cur().line,
                     "X::Undeclared::Symbols", {});
}

// `method z { $^a }` — a method with no signature of its own may not take
// placeholders, as in Rakudo (rakudo#2320); a block inside it (`{ $^a }`) has
// its own. Tokens from the body's `{` (bodyAt) to here.
void Parser::refuseMethodPlaceholders(size_t bodyAt, const std::string& name) {
    int depth = 0;
    for (size_t k = bodyAt; k < pos_ && k < toks_.size(); k++) {
        const Token& t = toks_[k];
        if (t.kind == Tok::LBrace) depth++;
        else if (t.kind == Tok::RBrace) depth--;
        else if (depth == 1 && t.kind == Tok::Var && t.text.size() > 2 && t.text[1] == '^' &&
                 (ascii::isalpha((unsigned char)t.text[2]) || t.text[2] == '_'))
            throw ParseError("Placeholder variables (eg. " + t.text + ") cannot be used in a method.\n"
                             "Please specify an explicit signature, like method " + name + " (" + t.text.substr(0, 1) +
                             t.text.substr(2) + ") { ... }", t.line, "X::AdHoc", {});
    }
}

// `use v6…`: the revisions and modifiers Rakudo 2026.09 has a compiler for.
// 6.c and 6.d stand alone (6.d may still say PREVIEW), and 6.e is a preview,
// so it needs `.PREVIEW` (or `.*`): a bare `use v6.e` is X::Language::ModRequired
// (rakudo#2783), where it was taken as 6.e. Any other revision, modifier or
// language is X::Language::Unsupported. `version` is spelled as written, `v6.e`.
void checkLangVersion(const std::string& version, int line) {
    auto unsupported = [&] {
        throw ParseError("No compiler available for Raku " + version, line, "X::Language::Unsupported",
                         {{"version", version}});
    };
    size_t i = 1;   // past the `v`
    while (i < version.size() && ascii::isdigit((unsigned char)version[i])) i++;
    if (version.substr(1, i - 1) != "6") unsupported();
    if (i < version.size() && version[i] == '.') i++;
    if (i >= version.size() || version[i] == '*') return;   // `v6`, `v6.*`
    const char rev = version[i++];
    std::vector<std::string> mods;
    while (i < version.size()) {
        if (version[i] == '.') { i++; continue; }
        size_t j = version.find('.', i);
        mods.push_back(version.substr(i, j == std::string::npos ? std::string::npos : j - i));
        i = j == std::string::npos ? version.size() : j;
    }
    if (rev != 'c' && rev != 'd' && rev != 'e') unsupported();
    if (mods.size() > 1 || (mods.size() == 1 && mods[0] != "*" && !(mods[0] == "PREVIEW" && rev != 'c'))) unsupported();
    if (rev == 'e' && mods.empty())
        throw ParseError("Raku " + version + " requires PREVIEW modifier", line, "X::Language::ModRequired",
                         {{"version", version}, {"modifier", "PREVIEW"}});
}

} // namespace rakupp
