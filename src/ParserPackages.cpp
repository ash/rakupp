// The parser's side of PACKAGE SYMBOLS: `Foo::<$x>` with an adverb or a
// slice, `Foo::<$x>:exists`, and the tags of an `is export(:a :b)` trait. Split out of Parser.cpp, whose size it was; it
// rides the same `parse` archive (CMakeLists.txt).
#include "Parser.h"
#include <memory>
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

} // namespace rakupp
