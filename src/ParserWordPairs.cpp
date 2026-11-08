// The parser's side of COLONPAIRS IN WORD LISTS: `« :x[1, 2] a:b »`,
// `qww<:x<1 2>>`, and the code snippets those pairs (and string
// interpolations) are parsed from. Split out of Parser.cpp, whose size it was;
// it rides the same `parse` archive (CMakeLists.txt).
#include "AsciiCtype.h"
#include "Lexer.h"
#include "Parser.h"
#include "Unicode.h"
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace rakupp {

// Past the `(…)`/`[…]`/`{…}` group whose opener is at s[j]: brackets of its
// own kind nest, and a quoted span is the code's (`:x[1, "]"]`). npos: it
// never closes.
static size_t groupEnd(const std::string& s, size_t j, char open, char close) {
    const size_t n = s.size();
    for (int depth = 0; j < n; j++) {
        const char c = s[j];
        if (c == '\\') { j++; continue; }
        if (size_t e = uniQuoteSpanEnd(s, j)) { j = e - 1; continue; }
        // (an apostrophe inside a name, `don't`, opens no string)
        if (c == '"' || (c == '\'' && !(j > 0 && rakuIdentCont(s[j - 1]) && j + 1 < n && rakuIdentStart(s[j + 1])))) {
            size_t e = j + 1;
            while (e < n && s[e] != c) e += s[e] == '\\' ? 2 : 1;
            if (e >= n) return std::string::npos;
            j = e;
        }
        else if (c == open) depth++;
        else if (c == close && --depth == 0) return j + 1;
    }
    return std::string::npos;
}

// Past the `<…>`/`<<…>>`/`«…»` group at s[j]: angles nest by character, as a
// word list's do (`:x<a <b> c>` holds the word `<b>`).
static size_t angleGroupEnd(const std::string& s, size_t j, const char* open, const char* close) {
    const size_t n = s.size(), w = std::strlen(open);
    for (int depth = 0; j < n; j++) {
        if (s.compare(j, w, open) == 0) depth++;
        else if (s.compare(j, w, close) == 0 && --depth == 0) return j + w;
    }
    return std::string::npos;
}

// Where a colonpair that starts at s[i] (a `:`) ends, or npos when none
// starts there: `:30`, `::`, `:=` and the `:` of `http://` stay text. In a
// quote-protected word list (`«…»`, `<<…>>`, qww, qqww, Q:ww …) Rakudo tries
// its colonpair rule at every character of a word, so a pair may start
// mid-word (`a:b` is ("a", :b)) and the character after it starts a new word
// (`:x[1,2]y` is (:x([1, 2]), "y")). A value group glued to the key — (…) […]
// {…} <…> <<…>> «…» — is the pair's, blanks, quotes and nested brackets and
// all; a negated or numbered key takes one too, for parseColonPair to refuse
// as Rakudo does. `:$x` and its kin take none (`:$x(1)` is (:x(1), "(1)")).
// A group that never closes runs to the end, for the parse to report, and
// sets `open`.
static size_t colonPairExtent(const std::string& s, size_t i, bool& open) {
    const size_t n = s.size();
    auto group = [&](size_t e) { open = e == std::string::npos; return open ? n : e; };
    size_t j = i + 1;
    if (j >= n) return std::string::npos;
    bool valued = true;
    if (s[j] && std::strchr("$@%&", s[j])) {
        valued = false;
        if (++j < n && s[j] && std::strchr("*.!^?=~", s[j])) j++;   // a twigil
    }
    else if (s[j] == '!') j++;
    else while (j < n && ascii::isdigit((unsigned char)s[j])) j++;
    if (j >= n || !uniIdentCharAt(s, j, true)) return std::string::npos;
    j = uniIdentEnd(s, j);
    if (!valued || j >= n) return j;
    if (s[j] == '(') return group(groupEnd(s, j, '(', ')'));
    if (s[j] == '[') return group(groupEnd(s, j, '[', ']'));
    if (s[j] == '{') return group(groupEnd(s, j, '{', '}'));
    if (s[j] == '<') return group(angleGroupEnd(s, j, "<", ">"));
    if (s.compare(j, 2, "\xC2\xAB") == 0) return group(angleGroupEnd(s, j, "\xC2\xAB", "\xC2\xBB"));
    return j;
}
size_t colonPairEnd(const std::string& s, size_t i) {
    bool open = false;
    return colonPairExtent(s, i, open);
}

// A parser for a snippet of this unit's code, which sees the unit's own
// operators, keywords and declarators as the enclosing parse does.
void Parser::inheritSyntax(Parser& p) const {
    p.userInfix_ = userInfix_;
    p.userPrefix_ = userPrefix_;
    p.userPrefixBp_ = userPrefixBp_;
    p.postfixLooserThanPrefix_ = postfixLooserThanPrefix_;
    p.useNqp_ = useNqp_; // `"{ nqp::chr($o) }"` in a `use nqp` unit sees the subset
    p.userPostfix_ = userPostfix_;
    p.userCircumfix_ = userCircumfix_;
    p.userPostcircumfix_ = userPostcircumfix_;
    p.userDeclarators_ = userDeclarators_;
    p.wordInfixSubs_ = wordInfixSubs_;   // `"{ div 3 }"` sees this unit's `sub div`
    p.kwNamedSubs_ = kwNamedSubs_;       // …and its `sub if`, so `"{ if() }"` calls it
    p.capSubNames_ = capSubNames_;       // …and its `sub Foo`, so `"{ Foo { 1 } }"` passes the block
    p.inEmbedded_ = true;                // (`"$!name"` in a method reaches the attribute)
    p.routineDepth_ = routineDepth_;     // `"{&?ROUTINE.name}"` inside a sub names that sub
    p.selfDepth_ = selfDepth_;
}

ExprPtr Parser::parseEmbeddedExpr(const std::string& src, bool ownScope) {
    Lexer lx(src);
    std::vector<Token> toks = lx.tokenize();
    if (embedLineBase_ > 1) for (auto& t : toks) t.line += embedLineBase_ - 1;   // (the file's lines)
    Parser p(std::move(toks));
    // Inherit user-defined operators so `"{ 4! }"` sees this program's `postfix:<!>`
    // (and custom infix/prefix/circumfix) just as top-level code does.
    inheritSyntax(p);
    Program prog = p.parseProgram();
    if (p.sawEndPhaser_) sawEndPhaser_ = true;   // `"{ END { … } }"` needs the END walk too
    if (p.sawOur_) sawOur_ = true;
    // a single bare expression interpolates directly — unless it is a `{…}`
    // block that declares something: `"{my $n = 2}"` must not leak `$n` out
    if (prog.stmts.size() == 1 && prog.stmts[0]->kind == NK::ExprStmt && !ownScope)
        return std::move(static_cast<ExprStmt*>(prog.stmts[0].get())->e);
    // statements (with modifiers, e.g. `{ 'x' if $y }`) run as a do-block
    auto be = std::make_unique<BlockExpr>();
    be->body = std::move(prog.stmts);
    auto u = std::make_unique<Unary>();
    u->op = "do"; u->operand = std::move(be);
    return u;
}

// The colonpair `src` (colonPairEnd's extent of a qww/qqww word) as
// parseColonPair reads it in code: a word list's pair IS the ordinary one, its
// value (`:x{…}` a Block, `:x<1 2>` allomorphs) and its errors alike. `line`
// is where the pair stands, for those errors.
ExprPtr Parser::wordColonPair(const std::string& src, int line) {
    Lexer lx(src);
    Parser p(lx.tokenize());
    inheritSyntax(p);
    try {
        ExprPtr pe = p.parseColonPair();
        if (!p.isKind(Tok::End)) p.error("Confused");
        return pe;
    } catch (ParseError& e) { e.line += line - 1; throw; }
}

// A word that is one whole colonpair, `:name(…)`, as a word list split at its
// blanks hands it over (`:x[1,` is none). Null when it is not one.
ExprPtr Parser::angleColonPair(const std::string& w) {
    bool open = false;
    if (w.empty() || w[0] != ':' || colonPairExtent(w, 0, open) != w.size() || open) return nullptr;
    return wordColonPair(w, cur().line);
}

// A colonpair at the current token of a «…» list, which the lexer has read as
// code: parsed as code, by parseColonPair, into `pairs`, behind a "\x03N" word
// for qqwwList. A name the lexer read with `::` in it is split at its colons
// first, since each `:` may start a pair (Rakudo's `« Foo::Bar »` is
// ("Foo:", :Bar), `« ::x »` (":", :x)); `\:` is an escaped colon, and a word
// after `$v:` continues the variable's NAME (qqwwAddWord reports `$v:b`).
bool Parser::takeWordPair(std::vector<ExprPtr>& pairs, std::vector<std::string>& words) {
    auto splitName = [&](size_t k) {
        if (k < toks_.size() && toks_[k].kind == Tok::Op && toks_[k].text == "::") {
            toks_[k].text = ":";
            toks_.insert(toks_.begin() + k + 1, toks_[k]);
            toks_[k + 1].spaceBefore = false;
            return;
        }
        if (k >= toks_.size() || toks_[k].kind != Tok::Ident) return;
        const std::string t = toks_[k].text;
        if (t.find("::") == std::string::npos) return;
        std::vector<Token> parts;
        for (size_t a = 0; a < t.size(); ) {
            size_t b = t[a] == ':' ? a + 1 : t.find(':', a);
            if (b == std::string::npos) b = t.size();
            Token p = toks_[k];
            p.kind = t[a] == ':' ? Tok::Op : Tok::Ident;
            p.text = t.substr(a, b - a);
            if (p.kind == Tok::Ident && !uniIdentCharAt(p.text, 0, true)) return;   // not a plain name
            p.spaceBefore = a == 0 && toks_[k].spaceBefore;
            parts.push_back(p);
            a = b;
        }
        toks_.erase(toks_.begin() + k);
        toks_.insert(toks_.begin() + k, parts.begin(), parts.end());
    };
    splitName(pos_);
    if (!isOp(":")) return false;
    const Token* before = pos_ > 0 && !cur().spaceBefore ? &toks_[pos_ - 1] : nullptr;
    if (before && before->kind == Tok::Var && before->text[0] == '$' && uniIdentCharAt(before->text, 1, true))
        return false;
    splitName(pos_ + 1);
    const Token& h = peek();
    if (h.spaceBefore) return false;
    const bool named = h.kind == Tok::Ident ||
        (h.kind == Tok::Var && h.text.size() > 1 &&
         (uniIdentCharAt(h.text, 1, true) || (std::strchr("*.!^?=~", h.text[1]) && uniIdentCharAt(h.text, 2, true))));
    const bool keyNext = peek(2).kind == Tok::Ident && !peek(2).spaceBefore;
    if (!named && !(keyNext && (h.kind == Tok::IntLit || (h.kind == Tok::Op && h.text == "!")))) return false;
    int slashes = 0;
    for (size_t k = pos_; k > 0 && !toks_[k].spaceBefore && toks_[k - 1].kind == Tok::Op && toks_[k - 1].text == "\\"; k--)
        slashes++;
    if (slashes % 2) return false;
    pairs.push_back(parseColonPair());
    words.push_back("\x03" + std::to_string(pairs.size() - 1));
    return true;
}

// The column a parse error raised on `line` names, when it names none: where
// the token the parse stopped on starts, or, when that one is on a later line
// or is the end, just past the token before it (`say 1 +` ran out after the
// `+`). Read off the source by each token's end offset, and 0 when the token's
// text is not what the source holds there (a string, a fudged file).
int Parser::stopColumn(int line) const {
    auto colAt = [&](size_t i, bool after) -> int {
        const Token& t = toks_[i];
        if (!src_ || t.off == 0 || t.off > src_->size() || t.off < t.text.size() ||
            src_->compare(t.off - t.text.size(), t.text.size(), t.text) != 0) return 0;
        const size_t at = after ? t.off : t.off - t.text.size();
        const size_t nl = at == 0 ? std::string::npos : src_->rfind('\n', at - 1);
        return (int)(at - (nl == std::string::npos ? 0 : nl + 1)) + 1;
    };
    if (pos_ < toks_.size() && toks_[pos_].kind != Tok::End && toks_[pos_].line == line) return colAt(pos_, false);
    if (pos_ > 0 && pos_ - 1 < toks_.size() && toks_[pos_ - 1].line == line) return colAt(pos_ - 1, true);
    return 0;
}

// `use Test` imports its routines into the scope it stands in, so a plain `sub
// ok` or a `my &ok` beside it redeclares one, in either order, as on Rakudo (a
// multi joins them; `use Test ()` imports nothing). Test's routines are built
// in here, and no import ever met the local one.
bool Parser::isTestExport(const std::string& n) {
    static const std::set<std::string> names = {
        "bail-out", "can-ok", "cmp-ok", "diag", "dies-ok", "does-ok", "done-testing", "eval-dies-ok",
        "eval-lives-ok", "fails-like", "flunk", "is", "is-approx", "is-deeply", "isa-ok", "isnt", "like",
        "lives-ok", "nok", "ok", "pass", "plan", "skip", "skip-rest", "subtest", "throws-like", "todo",
        "unlike", "use-ok"};
    return names.count(n) > 0;
}
bool Parser::testImportedHere(const std::vector<StmtPtr>& stmts) {
    for (auto& s : stmts)
        if (s && s->kind == NK::UseStmt) {
            auto* u = static_cast<const UseStmt*>(s.get());
            if (u->module == "Test" && !u->isNo && !u->isNeed && !u->emptyImport && u->importArgs.empty()) return true;
        }
    return false;
}

// `prefix:<->(3)`, `postfix:<++>($x)`, `postcircumfix:<[ ]>(@a, 1)`: a built-in
// operator called by its name is that operator applied, as Rakudo's `&prefix:<->`
// is; a written one keeps a postfix's container (`postfix:<++>(my $y = 1)` steps
// $y). One the program declares stays a call to it, and so does anything with a
// named argument. Answers null when the call is not one of these.
ExprPtr Parser::builtinOpCall(const std::string& name, std::vector<ExprPtr>& args) {
    if (name.empty() || name.back() != '>') return nullptr;
    for (auto& a : args) if (a->kind == NK::Pair && !static_cast<PairExpr*>(a.get())->parenned) return nullptr;
    auto op = [&](size_t from) { return name.substr(from, name.size() - from - 1); };
    static const std::set<std::string> kPrefix = {"-", "+", "~", "?", "!", "so", "not", "+^", "~^", "?^", "^", "++", "--"};
    const bool pre = name.rfind("prefix:<", 0) == 0 && kPrefix.count(op(8)) && !userPrefix_.count(op(8));
    const bool post = name.rfind("postfix:<", 0) == 0 && (op(9) == "++" || op(9) == "--") && !userPostfix_.count(op(9));
    if ((pre || post) && args.size() == 1) {
        auto u = std::make_unique<Unary>();
        const std::string o = pre ? op(8) : op(9);   // (`so` and `not` are the tight `?` and `!` applied)
        u->line = args[0]->line; u->op = o == "so" ? "?" : o == "not" ? "!" : o; u->postfix = post; u->operand = std::move(args[0]);
        return u;
    }
    const bool brackets = name == "postcircumfix:<[ ]>" && !userPostcircumfix_.count("[");
    if ((brackets || (name == "postcircumfix:<{ }>" && !userPostcircumfix_.count("{"))) && args.size() == 2) {
        auto ix = std::make_unique<Index>();
        ix->line = args[0]->line; ix->isHash = !brackets; ix->base = std::move(args[0]); ix->index = std::move(args[1]);
        return ix;
    }
    return nullptr;
}

} // namespace rakupp
