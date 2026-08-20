#include "AsciiCtype.h"
#include "Regex.h"
#include "LtmNfa.h"
#include "Interpreter.h"   // RakuError: feature-stub throws must escape the ctor

namespace rakupp { bool ltmModeOn(); }
bool rakupp::ltmModeOn() {
    // TRUE LTM IS THE DEFAULT (v3, LTM-PLAN phase 4): `|` alternations and
    // proto dispatch rank by declarative prefix via the NFA. RAKUPP_LTM=0
    // selects the legacy greedy-probe ranker — kept for one release as the
    // escape hatch and bisection tool, then the probe path retires.
    static const bool on = [] {
        const char* e = std::getenv("RAKUPP_LTM");
        return !(e && *e && std::string(e) == "0");
    }();
    return on;
}
#include <iostream>
#include <mutex>
#include <map>
#include <cstdint>
#include <memory>
#include <cstring>
#include <cstdlib>
#include "Unicode.h"
#include <algorithm>
#include <cctype>
#include <set>
#include <thread>

// Full case folding for :i matching (CaseFolding.txt F-entries, the common
// set): one codepoint may fold to SEVERAL — `ß` folds to "ss", so
// `"Weiß" ~~ m:i/WEISS/` matches. Everything else takes the simple fold
// (lowercase mapping; ς normalises to σ so both sigmas compare equal).
static void foldCpPush(uint32_t cp, std::vector<uint32_t>& out) {
    switch (cp) {
        case 0x00DF: case 0x1E9E: out.push_back('s'); out.push_back('s'); return;   // ß ẞ
        case 0xFB00: out.push_back('f'); out.push_back('f'); return;                // ff-ligature
        case 0xFB01: out.push_back('f'); out.push_back('i'); return;
        case 0xFB02: out.push_back('f'); out.push_back('l'); return;
        case 0xFB03: out.push_back('f'); out.push_back('f'); out.push_back('i'); return;
        case 0xFB04: out.push_back('f'); out.push_back('f'); out.push_back('l'); return;
        case 0xFB05: case 0xFB06: out.push_back('s'); out.push_back('t'); return;
        case 0x0149: out.push_back(0x02BC); out.push_back('n'); return;             // ŉ
        case 0x0130: out.push_back('i'); out.push_back(0x0307); return;             // İ
        case 0x0390: out.push_back(0x03B9); out.push_back(0x0308); out.push_back(0x0301); return; // ΐ
        case 0x03B0: out.push_back(0x03C5); out.push_back(0x0308); out.push_back(0x0301); return; // ΰ
        case 0x03C2: out.push_back(0x03C3); return;                                 // final sigma folds to σ
        default: break;
    }
    for (uint32_t c : rakupp::uniCaseMap(cp, 0)) out.push_back(c);
}

namespace rakupp {

static int32_t namedCp(const std::string& nm); // \c[NAME] resolver (defined below)

Regex::Regex(const std::string& pattern, const std::string& flags) : pat_(pattern) {
    for (char f : flags) {
        if (f == 'i') icase_ = true;
        if (f == 's') sigspace_ = true;
        if (f == 'r') ratchet_ = true;
        if (f == 'm') curImark_ = true; // external :ignoremark adverb
        if (f == '5') p5_ = true;       // Perl 5 pattern syntax (:P5/:Perl5)
    }
    // The lexer bakes `m:P5:i/…/` adverbs into the pattern as leading `:name `
    // tokens. A `:P5`/`:Perl5` among them switches the whole pattern to Perl 5
    // syntax, where `:` is a literal — so the adverb run must be consumed HERE,
    // not by skipWs(). Scan without committing; commit only when :P5 was seen
    // (Raku-syntax patterns keep the normal scoped-adverb handling).
    if (!p5_ && !pat_.empty() && pat_[0] == ':') {
        size_t p = 0; bool sawP5 = false, sawI = false;
        while (p < pat_.size() && pat_[p] == ':') {
            size_t j = p + 1;
            if (j < pat_.size() && pat_[j] == '!') j++;
            size_t ns = j;
            while (j < pat_.size() && ascii::isalnum((unsigned char)pat_[j])) j++;
            if (j == ns) break;
            std::string name = pat_.substr(ns, j - ns);
            if (j < pat_.size() && pat_[j] == '(') { // :nth(3)-style argument
                int d = 0;
                while (j < pat_.size()) { char c = pat_[j++]; if (c == '(') d++; else if (c == ')' && --d == 0) break; }
            }
            if (j < pat_.size() && pat_[j] == ' ') j++;
            else if (j < pat_.size()) break; // not the lexer's `:adv ` shape — a pattern colon
            if (name == "P5" || name == "Perl5") sawP5 = true;
            else if (name == "i" || name == "ignorecase") sawI = true;
            p = j;
        }
        if (sawP5) { p5_ = true; pos_ = p; if (sawI) icase_ = true; }
    }
    curIcase_ = icase_;
    try {
        if (p5_) {
            root_ = p5Alt();
            if (!eof()) throw P5BadPattern{}; // e.g. an unmatched `)`
        } else {
            root_ = parseAlt();
            if (!eof()) ok_ = false; // trailing garbage (e.g. unbalanced)
        }
    } catch (ObsoleteEscape& oe) {
        ok_ = false; obsolete_ = oe.seq;
    } catch (FeatureNotBuilt&) {
        // X::Feature::NotBuilt from a SLIM stub (a \c[NAME] needing the cut
        // name table, a <:Script<…>> needing the cut props table). Folding
        // that into ok_=false would turn "this binary lacks the feature" into
        // a silent no-match — the exact failure mode SLIM-PLAN bans. Every
        // OTHER throw (a bad \c name, ordinary syntax trouble) keeps the
        // established lenient behaviour below.
        throw;
    } catch (...) {
        ok_ = false;
    }
}

// =====================  Perl 5 pattern syntax (:P5)  =====================
// A second front-end producing the same Node AST the matcher runs. Ported from
// the working parser in showcase/perl/perl.raku, widened to the re_tests
// surface: lookaround, named groups, backrefs, inline (?imsx) modifiers.
// P5 text is char-exact — no skipWs(), no Raku adverbs, `:` is a literal.

static std::string p5Utf8(uint32_t cp) {
    std::string o;
    if (cp < 0x80) o += (char)cp;
    else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
    else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
    return o;
}

uint32_t Regex::p5Codepoint() {
    unsigned char c0 = (unsigned char)pat_[pos_];
    int clen = c0 < 0x80 ? 1 : (c0 >> 5) == 0x6 ? 2 : (c0 >> 4) == 0xe ? 3 : (c0 >> 3) == 0x1e ? 4 : 1;
    uint32_t cp = clen == 1 ? c0 : (uint32_t)(c0 & (0xFF >> (clen + 1)));
    for (int i = 1; i < clen && pos_ + i < pat_.size(); i++) cp = (cp << 6) | ((unsigned char)pat_[pos_ + i] & 0x3F);
    pos_ += clen;
    return cp;
}

void Regex::p5SkipX() {
    if (!p5Ext_) return;
    for (;;) {
        while (!eof() && ascii::isspace((unsigned char)peek())) pos_++;
        if (peek() == '#') { while (!eof() && peek() != '\n') pos_++; continue; }
        break;
    }
}

Regex::NodePtr Regex::p5Alt() {
    auto first = p5Seq();
    if (peek() != '|') return first;
    auto alt = std::make_unique<Node>();
    alt->k = K::Alt;
    alt->firstMatch = true; // P5 `|` is leftmost-first, never LTM
    // empty branches stay: /a|/ legitimately matches the empty string
    alt->kids.push_back(std::move(first));
    while (peek() == '|') { pos_++; alt->kids.push_back(p5Seq()); }
    return alt;
}

Regex::NodePtr Regex::p5Seq() {
    auto seq = std::make_unique<Node>();
    seq->k = K::Seq;
    for (;;) {
        p5SkipX();
        if (eof() || peek() == '|') break;
        if (peek() == ')') {
            if (p5Depth_ == 0) throw P5BadPattern{}; // unmatched `)`
            break;
        }
        auto atom = p5Atom();
        if (!atom) continue;                          // (?i) / (?#…) — pure state, no node
        seq->kids.push_back(p5Quant(std::move(atom)));
    }
    if (seq->kids.size() == 1) return std::move(seq->kids[0]);
    return seq;
}

Regex::NodePtr Regex::p5Quant(NodePtr atom) {
    // (?#comments) — and (?x)-mode whitespace — are invisible, so a quantifier
    // BEYOND them still binds this atom: `a(?#xxx){3}` repeats the `a`
    for (;;) {
        p5SkipX();
        if (peek() == '(' && peek(1) == '?' && peek(2) == '#') {
            while (!eof() && peek() != ')') pos_++;
            if (eof()) throw P5BadPattern{};
            pos_++;
            continue;
        }
        break;
    }
    char c = peek();
    long mn = 0, mx = -1;
    if (c == '*') { pos_++; }
    else if (c == '+') { pos_++; mn = 1; }
    else if (c == '?') { pos_++; mx = 1; }
    else if (c == '{') {
        // {n} / {n,} / {n,m} — anything else is a literal `{`, left for the next atom
        size_t j = pos_ + 1; std::string lo, hi; bool comma = false;
        while (j < pat_.size() && ascii::isdigit((unsigned char)pat_[j])) lo += pat_[j++];
        if (j < pat_.size() && pat_[j] == ',') { comma = true; j++; }
        while (j < pat_.size() && ascii::isdigit((unsigned char)pat_[j])) hi += pat_[j++];
        if (j >= pat_.size() || pat_[j] != '}' || lo.empty()) return atom;
        pos_ = j + 1;
        mn = std::stol(lo);
        mx = comma ? (hi.empty() ? -1 : std::stol(hi)) : mn;
    }
    else return atom;
    auto rep = std::make_unique<Node>();
    rep->k = K::Rep; rep->min = mn; rep->max = mx;
    if (peek() == '?') { pos_++; rep->greedy = false; }       // lazy
    else if (peek() == '+') { pos_++; rep->possessive = true; } // possessive (5.10+)
    // NOTE: no listCap — a quantified P5 capture keeps its LAST occurrence,
    // Perl-style, not a Raku list of every occurrence.
    rep->kids.push_back(std::move(atom));
    return rep;
}

// A spliced sub-pattern (see Regex::spliceOf): compile the source with ITS OWN
// front-end and graft the tree in. Pasting the text in instead would reread it
// in the host's syntax — `rx:P5/[a-z]+/` interpolated into a Raku `s///` became
// a group over `a`, `-`, `z` and quietly matched nothing (HTTP::Tinyish builds
// its header-name token that way). Captures are renumbered onto the end of the
// host's, so `$0` keeps meaning what the host wrote.
Regex::NodePtr Regex::parseSplice() {
    size_t span = spliceSpan(pat_, pos_);
    bool p5 = pat_[pos_ + 1] == 'P';
    size_t hdr = pat_.find('\x01', pos_ + 1) + 1;
    std::string src = pat_.substr(hdr, pos_ + span - hdr);
    pos_ += span;
    Regex sub(src, p5 ? "5" : "");
    if (!sub.ok() || !sub.root_) { auto n = std::make_unique<Node>(); n->k = K::Nop; return n; }
    if (sub.ncaps_ > 0) {
        int base = ncaps_;
        std::function<void(Node*)> shift = [&](Node* n) {
            if (n->capIndex >= 0) n->capIndex += base;
            // a numeric backreference names a capture by index too
            if (n->k == K::VarMatch && !n->lit.empty() &&
                n->lit.find_first_not_of("0123456789") == std::string::npos)
                n->lit = std::to_string(std::stoi(n->lit) + base);
            for (auto& k : n->kids) shift(k.get());
            if (n->sep) shift(n->sep.get());
        };
        shift(sub.root_.get());
        for (int i : sub.listCaps_) listCaps_.insert(i + base);
        ncaps_ += sub.ncaps_;
    }
    if (sub.listNames_) {
        if (!listNames_) listNames_ = std::make_shared<std::set<std::string>>();
        listNames_->insert(sub.listNames_->begin(), sub.listNames_->end());
    }
    return std::move(sub.root_);
}

Regex::NodePtr Regex::p5Atom() {
    char c = peek();
    if (c == '\x01' && spliceSpan(pat_, pos_)) return parseSplice();
    if (c == '(') return p5Group();
    if (c == '[') return p5Class();
    if (c == '\\') return p5Escape();
    if (c == '.') {
        pos_++;
        if (p5DotAll_) { auto n = std::make_unique<Node>(); n->k = K::Any; return n; }
        auto n = std::make_unique<Node>();
        n->k = K::Class; n->negate = true; n->ranges.push_back({'\n', '\n'});
        return n;
    }
    if (c == '^') {
        pos_++;
        auto n = std::make_unique<Node>(); n->k = K::AnchorStart;
        n->multiline = p5Multi_;
        n->p5Line = p5Multi_; // P5 (?m)^: line starts only — NOT the void after a final \n
        return n;
    }
    if (c == '$') {
        // `$` is the end anchor unless what follows could only be an (upstream,
        // already-attempted) variable interpolation — a name char. `2(]*)?$\1`
        // anchors mid-pattern (re_tests 1327).
        size_t j = pos_ + 1;
        if (p5Ext_) while (j < pat_.size() && ascii::isspace((unsigned char)pat_[j])) j++;
        if (j >= pat_.size() || !(ascii::isalnum((unsigned char)pat_[j]) || pat_[j] == '_')) {
            pos_++;
            auto n = std::make_unique<Node>(); n->k = K::AnchorEnd; n->multiline = p5Multi_;
            return n;
        }
        pos_++;
        auto n = std::make_unique<Node>(); n->k = K::Lit; n->icase = curIcase_; n->lit = "$";
        return n;
    }
    if (c == '*' || c == '+' || c == '?') throw P5BadPattern{}; // quantifier follows nothing
    // plain literal — one whole codepoint
    auto n = std::make_unique<Node>();
    n->k = K::Lit; n->icase = curIcase_;
    n->lit = p5Utf8(p5Codepoint());
    return n;
}

Regex::NodePtr Regex::p5Group() {
    pos_++; // '('
    bool savedI = curIcase_, savedM = p5Multi_, savedS = p5DotAll_, savedX = p5Ext_;
    auto body = [&](int capIdx, const std::string& capName) -> NodePtr {
        p5Depth_++;
        auto inner = p5Alt();
        p5Depth_--;
        if (peek() != ')') throw P5BadPattern{};
        pos_++;
        curIcase_ = savedI; p5Multi_ = savedM; p5DotAll_ = savedS; p5Ext_ = savedX;
        auto g = std::make_unique<Node>();
        g->k = K::Group; g->capIndex = capIdx; g->capName = capName;
        g->kids.push_back(std::move(inner));
        return g;
    };
    if (peek() != '?') return body(ncaps_++, "");
    pos_++; // '?'
    char d = peek();
    if (d == ':') { pos_++; return body(-1, ""); }
    if (d == '#') { // comment
        while (!eof() && peek() != ')') pos_++;
        if (eof()) throw P5BadPattern{};
        pos_++;
        return nullptr;
    }
    if (d == '{' || (d == '?' && peek(1) == '{')) { // (?{ code }) / (??{ code })
        bool postponed = d == '?';
        pos_ += postponed ? 2 : 1;
        // balanced-brace scan, honouring \-escapes and one nesting level per brace
        std::string code;
        int depth = 1;
        while (!eof()) {
            char c2 = pat_[pos_];
            if (c2 == '\\' && pos_ + 1 < pat_.size()) { code += c2; code += pat_[pos_ + 1]; pos_ += 2; continue; }
            if (c2 == '{') depth++;
            if (c2 == '}' && --depth == 0) break;
            code += c2;
            pos_++;
        }
        if (eof()) throw P5BadPattern{};
        pos_++; // '}'
        if (peek() != ')') throw P5BadPattern{};
        pos_++;
        if (!postponed) return nullptr; // (?{…}): side effects only — a no-op here
        // (??{ … }): the code's result is matched as a pattern. A CONSTANT string
        // (the only shape Roast uses: (??{"(?!)"})) compiles right here; anything
        // dynamic is out of scope.
        size_t a = code.find_first_not_of(" \t"), b = code.find_last_not_of(" \t");
        std::string t = a == std::string::npos ? "" : code.substr(a, b - a + 1);
        if (t.size() >= 2 && (t.front() == '"' || t.front() == '\'') && t.back() == t.front()) {
            Regex sub(t.substr(1, t.size() - 2), "5");
            if (!sub.ok() || sub.ncaps_ > 0) throw P5BadPattern{};
            return std::move(sub.root_);
        }
        throw P5BadPattern{};
    }
    if (d == '(') { // (?(COND)yes|no) — conditional: group N, lookaround, or (?{const})
        pos_++;
        long condGroup = -1;
        NodePtr condPos, condNeg; // assertion conditions: the test, and its negation
        if (ascii::isdigit((unsigned char)peek())) {
            std::string num;
            while (ascii::isdigit((unsigned char)peek())) num += pat_[pos_++];
            if (peek() != ')') throw P5BadPattern{};
            pos_++;
            condGroup = std::stol(num);
        } else if (peek() == '?') {
            // The condition parses TWICE — once straight, once negated — because
            // Node trees have no clone and the desugared form needs both senses.
            size_t condStart = pos_;
            auto parseCond = [&](bool flip) -> NodePtr {
                pos_ = condStart + 1; // past '?'
                char e = peek();
                if (e == '{') { // (?{ const }) — 0 is false, any other constant true
                    pos_++;
                    std::string code;
                    int depth = 1;
                    while (!eof()) {
                        char c2 = pat_[pos_];
                        if (c2 == '\\' && pos_ + 1 < pat_.size()) { code += c2; code += pat_[pos_ + 1]; pos_ += 2; continue; }
                        if (c2 == '{') depth++;
                        if (c2 == '}' && --depth == 0) break;
                        code += c2;
                        pos_++;
                    }
                    if (eof()) throw P5BadPattern{};
                    pos_++;
                    size_t a2 = code.find_first_not_of(" \t");
                    size_t b2 = code.find_last_not_of(" \t");
                    std::string t = a2 == std::string::npos ? "" : code.substr(a2, b2 - a2 + 1);
                    if (t.find_first_not_of("0123456789") != std::string::npos) throw P5BadPattern{}; // dynamic code: out of scope
                    bool truth = !t.empty() && t.find_first_not_of("0") != std::string::npos;
                    if (flip) truth = !truth;
                    auto n = std::make_unique<Node>();
                    if (truth) { n->k = K::Nop; }
                    else { // always-fail: negated lookahead of the empty pattern
                        n->k = K::Look; n->negate = true;
                        auto empty = std::make_unique<Node>(); empty->k = K::Seq;
                        n->kids.push_back(std::move(empty));
                    }
                    if (peek() != ')') throw P5BadPattern{};
                    pos_++;
                    return n;
                }
                bool behind = false, neg = false;
                if (e == '=') pos_++;
                else if (e == '!') { neg = true; pos_++; }
                else if (e == '<' && peek(1) == '=') { behind = true; pos_ += 2; }
                else if (e == '<' && peek(1) == '!') { behind = true; neg = true; pos_ += 2; }
                else throw P5BadPattern{};
                p5Depth_++;
                auto inner = p5Alt();
                p5Depth_--;
                if (peek() != ')') throw P5BadPattern{};
                pos_++;
                auto look = std::make_unique<Node>();
                look->k = K::Look; look->negate = flip ? !neg : neg; look->behind = behind;
                look->kids.push_back(std::move(inner));
                return look;
            };
            condPos = parseCond(false);
            condNeg = parseCond(true); // leaves pos_ right after the condition
        } else throw P5BadPattern{};
        p5Depth_++;
        auto yes = p5Seq();
        NodePtr no;
        if (peek() == '|') { pos_++; no = p5Seq(); }
        p5Depth_--;
        if (peek() != ')') throw P5BadPattern{}; // includes >2 branches
        pos_++;
        curIcase_ = savedI; p5Multi_ = savedM; p5DotAll_ = savedS; p5Ext_ = savedX;
        if (condGroup >= 0) {
            auto cond = std::make_unique<Node>();
            cond->k = K::CondRef; cond->min = condGroup;
            cond->kids.push_back(std::move(yes));
            if (no) cond->kids.push_back(std::move(no));
            return cond;
        }
        // assertion condition — desugar with what the engine has:
        //   Alt||( Seq(COND, yes), Seq(¬COND, no) )
        auto mkSeq = [](NodePtr a, NodePtr b2) {
            auto s = std::make_unique<Node>();
            s->k = K::Seq;
            s->kids.push_back(std::move(a));
            if (b2) s->kids.push_back(std::move(b2));
            return s;
        };
        auto alt = std::make_unique<Node>();
        alt->k = K::Alt; alt->firstMatch = true;
        alt->kids.push_back(mkSeq(std::move(condPos), std::move(yes)));
        alt->kids.push_back(mkSeq(std::move(condNeg), std::move(no)));
        return alt;
    }
    if (d == '=' || d == '!') { // lookahead
        pos_++;
        p5Depth_++;
        auto inner = p5Alt();
        p5Depth_--;
        if (peek() != ')') throw P5BadPattern{};
        pos_++;
        curIcase_ = savedI; p5Multi_ = savedM; p5DotAll_ = savedS; p5Ext_ = savedX;
        auto look = std::make_unique<Node>();
        look->k = K::Look; look->negate = (d == '!'); look->behind = false;
        look->kids.push_back(std::move(inner));
        return look;
    }
    if (d == '<' && (peek(1) == '=' || peek(1) == '!')) { // lookbehind
        char e = peek(1);
        pos_ += 2;
        p5Depth_++;
        auto inner = p5Alt();
        p5Depth_--;
        if (peek() != ')') throw P5BadPattern{};
        pos_++;
        curIcase_ = savedI; p5Multi_ = savedM; p5DotAll_ = savedS; p5Ext_ = savedX;
        auto look = std::make_unique<Node>();
        look->k = K::Look; look->negate = (e == '!'); look->behind = true;
        look->kids.push_back(std::move(inner));
        return look;
    }
    if (d == '<' || d == '\'' || d == 'P') { // named capture: (?<name>…) / (?'name'…) / (?P<name>…)
        char open = d == 'P' ? (pos_++, peek()) : d;
        if (open != '<' && open != '\'') throw P5BadPattern{};
        char close = open == '<' ? '>' : '\'';
        pos_++;
        std::string name;
        while (!eof() && peek() != close) name += pat_[pos_++];
        if (eof() || name.empty()) throw P5BadPattern{}; // (?<>) is a syntax error
        pos_++;
        // Rakudo's m:P5 keeps named groups OUT of the positional numbering —
        // $/[0] is the first UNNAMED group (S05-modifier/Perl_10.t), unlike Perl
        // itself where $1 would be this group.
        return body(-1, name);
    }
    if (d == 'i' || d == 'm' || d == 's' || d == 'x' || d == '-') { // (?imsx-imsx) / (?imsx:…)
        bool on = true;
        while (!eof()) {
            char f = peek();
            if (f == '-') { on = false; pos_++; continue; }
            if (f == 'i') curIcase_ = on;
            else if (f == 'm') p5Multi_ = on;
            else if (f == 's') p5DotAll_ = on;
            else if (f == 'x') p5Ext_ = on;
            else break;
            pos_++;
        }
        if (peek() == ':') { pos_++; return body(-1, ""); } // scoped: (?i:…)
        if (peek() != ')') throw P5BadPattern{};
        pos_++;
        // bare (?i): applies to the END of the enclosing group — undo the
        // body()-style restore by re-saving nothing: state simply stays set.
        return nullptr;
    }
    throw P5BadPattern{};
}

Regex::NodePtr Regex::p5Escape() {
    pos_++; // backslash
    if (eof()) throw P5BadPattern{};
    char e = pat_[pos_++];
    auto mkClass = [&](char flag, bool neg) {
        auto n = std::make_unique<Node>();
        n->k = K::Class; n->icase = curIcase_; n->classFlags = std::string(1, flag); n->negate = neg;
        return n;
    };
    auto mkLit = [&](const std::string& s) {
        auto n = std::make_unique<Node>();
        n->k = K::Lit; n->icase = curIcase_; n->lit = s;
        return n;
    };
    switch (e) {
        case 'd': return mkClass('d', false);
        case 'D': return mkClass('d', true);
        case 'w': return mkClass('w', false);
        case 'W': return mkClass('w', true);
        case 's': return mkClass('s', false);
        case 'S': return mkClass('s', true);
        case 'b': case 'B': { // word boundary — either edge (WBLeft | WBRight)
            auto alt = std::make_unique<Node>();
            alt->k = K::Alt; alt->firstMatch = true;
            auto l = std::make_unique<Node>(); l->k = K::WBLeft;
            auto r = std::make_unique<Node>(); r->k = K::WBRight;
            alt->kids.push_back(std::move(l)); alt->kids.push_back(std::move(r));
            if (e == 'b') return alt;
            auto look = std::make_unique<Node>(); // \B: NOT a boundary — negated zero-width
            look->k = K::Look; look->negate = true; look->behind = false;
            look->kids.push_back(std::move(alt));
            return look;
        }
        case 'A': { auto n = std::make_unique<Node>(); n->k = K::AnchorStart; return n; }
        case 'Z': { auto n = std::make_unique<Node>(); n->k = K::AnchorEnd; return n; } // end or before final \n
        case 'z': { auto n = std::make_unique<Node>(); n->k = K::AnchorEnd; n->absEnd = true; return n; }
        case 'G': { auto n = std::make_unique<Node>(); n->k = K::AnchorStart; return n; } // ≈ \A: anchored at search start
        case 'n': return mkLit("\n");
        case 't': return mkLit("\t");
        case 'r': return mkLit("\r");
        case 'f': return mkLit("\f");
        case 'e': return mkLit("\x1b");
        case 'a': return mkLit("\x07");
        case 'x': { // \xHH / \x{HHHH}
            uint32_t cp = 0;
            if (peek() == '{') {
                pos_++;
                while (!eof() && peek() != '}') { cp = cp * 16 + (ascii::isdigit((unsigned char)peek()) ? peek() - '0' : (ascii::tolower((unsigned char)peek()) - 'a' + 10)); pos_++; }
                if (eof()) throw P5BadPattern{};
                pos_++;
            } else {
                for (int i = 0; i < 2 && ascii::isxdigit((unsigned char)peek()); i++) {
                    cp = cp * 16 + (ascii::isdigit((unsigned char)peek()) ? peek() - '0' : (ascii::tolower((unsigned char)peek()) - 'a' + 10));
                    pos_++;
                }
            }
            return mkLit(p5Utf8(cp));
        }
        case 'c': { // \cX control char
            if (eof()) throw P5BadPattern{};
            char x = pat_[pos_++];
            return mkLit(std::string(1, (char)(ascii::toupper((unsigned char)x) ^ 64)));
        }
        default: break;
    }
    if (e == '0' || (e >= '1' && e <= '9')) {
        if (e == '0') { // octal: \0, \0NN
            uint32_t v = 0; int nd = 0;
            while (nd < 2 && peek() >= '0' && peek() <= '7') { v = v * 8 + (peek() - '0'); pos_++; nd++; }
            return mkLit(v ? p5Utf8(v) : std::string(1, '\0'));
        }
        std::string num(1, e); // backref: \1 → the engine's in-flight $0
        while (ascii::isdigit((unsigned char)peek())) num += pat_[pos_++];
        auto vm = std::make_unique<Node>();
        vm->k = K::VarMatch; vm->icase = curIcase_;
        vm->lit = "$" + std::to_string(std::stol(num) - 1);
        // NOTE: a backref to an unmatched group FAILS (engine default) — modern
        // Perl semantics, and what re_tests 349 (`(a)|\1` vs "x") demands
        return vm;
    }
    // identity escape: \/ \. \\ \+ … — the char itself, one whole codepoint
    pos_--;
    return mkLit(p5Utf8(p5Codepoint()));
}

Regex::NodePtr Regex::p5Class() {
    pos_++; // '['
    bool neg = false;
    if (peek() == '^') { neg = true; pos_++; }
    auto cls = std::make_unique<Node>();
    cls->k = K::Class; cls->icase = curIcase_;
    std::vector<NodePtr> negMembers; // \D-style members: complement classes
    bool first = true;
    auto addCp = [&](uint32_t cp) {
        if (cp < 0x80) cls->ranges.push_back({(unsigned char)cp, (unsigned char)cp});
        else cls->cpRanges.push_back({cp, cp});
    };
    // a class-member escape resolves to a codepoint (-1: it was a flag/complement)
    auto escMember = [&]() -> int32_t {
        char e = pat_[pos_++];
        switch (e) {
            case 'd': cls->classFlags += 'd'; return -1;
            case 'w': cls->classFlags += 'w'; return -1;
            case 's': cls->classFlags += 's'; return -1;
            case 'D': case 'W': case 'S': {
                auto m = std::make_unique<Node>();
                m->k = K::Class; m->icase = curIcase_; m->classFlags = std::string(1, (char)ascii::tolower((unsigned char)e));
                negMembers.push_back(std::move(m));
                return -1;
            }
            case 'n': return '\n';
            case 't': return '\t';
            case 'r': return '\r';
            case 'f': return '\f';
            case 'e': return 0x1b;
            case 'a': return 0x07;
            case 'b': return 0x08; // inside a class, \b is BACKSPACE
            case 'x': {
                uint32_t cp = 0;
                if (peek() == '{') {
                    pos_++;
                    while (!eof() && peek() != '}') { cp = cp * 16 + (ascii::isdigit((unsigned char)peek()) ? peek() - '0' : (ascii::tolower((unsigned char)peek()) - 'a' + 10)); pos_++; }
                    if (eof()) throw P5BadPattern{};
                    pos_++;
                } else {
                    for (int i = 0; i < 2 && ascii::isxdigit((unsigned char)peek()); i++) {
                        cp = cp * 16 + (ascii::isdigit((unsigned char)peek()) ? peek() - '0' : (ascii::tolower((unsigned char)peek()) - 'a' + 10));
                        pos_++;
                    }
                }
                return (int32_t)cp;
            }
            case 'c': { if (eof()) throw P5BadPattern{}; char x = pat_[pos_++]; return (int32_t)(ascii::toupper((unsigned char)x) ^ 64); }
            case '0': { uint32_t v = 0; int nd = 0; while (nd < 2 && peek() >= '0' && peek() <= '7') { v = v * 8 + (peek() - '0'); pos_++; nd++; } return (int32_t)v; }
            default: pos_--; return (int32_t)p5Codepoint();
        }
    };
    while (!eof() && (peek() != ']' || first)) {
        first = false;
        // POSIX class [:alpha:] — expressed as plain ASCII ranges
        if (peek() == '[' && peek(1) == ':') {
            size_t j = pos_ + 2; bool pneg = false;
            if (j < pat_.size() && pat_[j] == '^') { pneg = true; j++; }
            std::string nm;
            while (j < pat_.size() && ascii::isalpha((unsigned char)pat_[j])) nm += pat_[j++];
            if (j + 1 < pat_.size() && pat_[j] == ':' && pat_[j + 1] == ']') {
                pos_ = j + 2;
                auto tgt = std::make_unique<Node>();
                tgt->k = K::Class; tgt->icase = curIcase_;
                auto& R = tgt->ranges;
                if (nm == "alpha") { R.push_back({'A','Z'}); R.push_back({'a','z'}); }
                else if (nm == "digit") R.push_back({'0','9'});
                else if (nm == "alnum") { R.push_back({'0','9'}); R.push_back({'A','Z'}); R.push_back({'a','z'}); }
                else if (nm == "upper") R.push_back({'A','Z'});
                else if (nm == "lower") R.push_back({'a','z'});
                else if (nm == "space") { R.push_back({'\t','\r'}); R.push_back({' ',' '}); }
                else if (nm == "blank") { R.push_back({'\t','\t'}); R.push_back({' ',' '}); }
                else if (nm == "word") { R.push_back({'0','9'}); R.push_back({'A','Z'}); R.push_back({'a','z'}); R.push_back({'_','_'}); }
                else if (nm == "xdigit") { R.push_back({'0','9'}); R.push_back({'A','F'}); R.push_back({'a','f'}); }
                else if (nm == "punct") { R.push_back({'!','/'}); R.push_back({':','@'}); R.push_back({'[','`'}); R.push_back({'{','~'}); }
                else if (nm == "cntrl") { R.push_back({0,31}); R.push_back({127,127}); }
                else if (nm == "graph") R.push_back({'!','~'});
                else if (nm == "print") R.push_back({' ','~'});
                else if (nm == "ascii") R.push_back({0,127});
                else throw P5BadPattern{};
                if (pneg) { tgt->negate = true; negMembers.push_back(std::move(tgt)); }
                else { for (auto& r : tgt->ranges) cls->ranges.push_back(r); }
                continue;
            }
        }
        int32_t lo;
        if (peek() == '\\') { pos_++; if (eof()) throw P5BadPattern{}; lo = escMember(); }
        else lo = (int32_t)p5Codepoint();
        if (lo < 0) continue; // flag / complement member — no range endpoint
        // range? `a-z`; a trailing `-` (or `-]`) is a literal dash
        if (peek() == '-' && peek(1) != ']' && pos_ + 1 < pat_.size()) {
            pos_++;
            int32_t hi;
            if (peek() == '\\') { pos_++; if (eof()) throw P5BadPattern{}; hi = escMember(); }
            else hi = (int32_t)p5Codepoint();
            if (hi < 0) throw P5BadPattern{}; // [a-\d] is a syntax error
            if (lo < 0x80 && hi < 0x80) cls->ranges.push_back({(unsigned char)lo, (unsigned char)hi});
            else cls->cpRanges.push_back({(uint32_t)lo, (uint32_t)hi});
        }
        else addCp((uint32_t)lo);
    }
    if (eof()) throw P5BadPattern{}; // unterminated class
    pos_++; // ']'
    bool clsEmpty = cls->ranges.empty() && cls->cpRanges.empty() && cls->classFlags.empty();
    if (negMembers.empty()) {
        if (clsEmpty) throw P5BadPattern{};
        cls->negate = neg;
        return cls;
    }
    if (!neg) {
        // [a\D] — union: a | ¬d. Complement members become negated classes; the
        // classCombo Alt is the engine's own composed-class shape.
        for (auto& m : negMembers) m->negate = true;
        if (clsEmpty && negMembers.size() == 1) return std::move(negMembers[0]);
        auto alt = std::make_unique<Node>();
        alt->k = K::Alt; alt->firstMatch = true; alt->classCombo = true;
        if (!clsEmpty) alt->kids.push_back(std::move(cls));
        for (auto& m : negMembers) alt->kids.push_back(std::move(m));
        return alt;
    }
    // [^a\D] — De Morgan: ¬(a ∪ ¬d) = ¬a ∩ d. Conj: all match at the same
    // position, the last one consumes.
    if (clsEmpty && negMembers.size() == 1) return std::move(negMembers[0]); // [^\D] = \d
    auto conj = std::make_unique<Node>();
    conj->k = K::Conj;
    if (!clsEmpty) { cls->negate = true; conj->kids.push_back(std::move(cls)); }
    for (auto& m : negMembers) conj->kids.push_back(std::move(m));
    return conj;
}

void Regex::skipWs() {
    for (;;) {
        while (!eof() && ascii::isspace((unsigned char)peek())) pos_++;
        if (peek() == '#') { while (!eof() && peek() != '\n') pos_++; continue; }
        // inline adverb :i :s :ignorecase — with an optional value: :!i, :0i/:1i, :i(0)/:i(1)
        if (peek() == ':' && (ascii::isalpha((unsigned char)peek(1)) || ascii::isdigit((unsigned char)peek(1)) ||
                              (peek(1) == '!' && ascii::isalpha((unsigned char)peek(2))))) {
            size_t save = pos_;
            pos_++;
            long num = -1; // -1 = no value given (plain :i means on)
            while (ascii::isdigit((unsigned char)peek())) { num = (num < 0 ? 0 : num) * 10 + (peek() - '0'); pos_++; }
            bool neg = peek() == '!';
            if (neg) pos_++;
            std::string adv;
            while (ascii::isalnum((unsigned char)peek())) adv += pat_[pos_++];
            if (peek() == '(') { // :i(0) / :i(1) argument form
                size_t p = pos_ + 1; std::string arg;
                while (p < pat_.size() && pat_[p] != ')') arg += pat_[p++];
                if (p < pat_.size()) {
                    size_t q = arg.find_first_not_of(" \t");
                    num = (q != std::string::npos && arg.find_first_not_of(" \t0") == std::string::npos) ? 0 : 1;
                    pos_ = p + 1;
                }
            }
            bool on = !neg && num != 0;
            // scoped: applies from here to the end of the enclosing group
            if (adv == "i" || adv == "ignorecase") curIcase_ = on;
            else if (adv == "s" || adv == "sigspace") sigspace_ = on;
            else if (adv == "m" || adv == "ignoremark" || adv == "mm" || adv == "samemark") curImark_ = on;
            else if (adv == "g" || adv == "ratchet") {}
            else { pos_ = save; break; } // not an adverb we consume; leave it
            continue;
        }
        break;
    }
}

Regex::NodePtr Regex::parseAlt() {
    // A leading/empty branch (`[ | A | B ]` — cosmetic in Raku) must NOT become a
    // zero-width alternative that always wins; drop empty branches.
    auto isEmpty = [](const NodePtr& n) { return n->k == K::Seq && n->kids.empty(); };
    auto first = parseConj();
    if (peek() != '|') return first;
    auto alt = std::make_unique<Node>();
    alt->k = K::Alt;
    if (!isEmpty(first)) alt->kids.push_back(std::move(first));
    bool sawDouble = false;
    while (peek() == '|') {
        pos_++;
        if (peek() == '|') { pos_++; sawDouble = true; } // `||` = sequential first-match
        auto branch = parseConj();
        if (!isEmpty(branch)) alt->kids.push_back(std::move(branch));
    }
    // pure `|` uses LTM (longest-token wins); any `||` present → first-match (conservative)
    alt->firstMatch = sawDouble;
    if (alt->kids.size() == 1) return std::move(alt->kids[0]);
    return alt;
}

// Conjunction: `A & B` (LTM) / `A && B` (sequential) — every term must match at
// the same start position; the whole match is as long as the LAST term. Binds
// tighter than alternation `|`, looser than concatenation.
Regex::NodePtr Regex::parseConj() {
    auto first = parseSeq();
    { size_t p = pos_; skipWs(); if (peek() != '&') { pos_ = p; return first; } pos_ = p; }
    auto conj = std::make_unique<Node>();
    conj->k = K::Conj;
    conj->kids.push_back(std::move(first));
    for (;;) {
        skipWs();
        if (peek() != '&') break;
        pos_++;
        if (peek() == '&') pos_++; // `&&` matches like `&` for our purposes
        conj->kids.push_back(parseSeq());
    }
    if (conj->kids.size() == 1) return std::move(conj->kids[0]);
    return conj;
}

Regex::NodePtr Regex::parseSeq() {
    auto seq = std::make_unique<Node>();
    seq->k = K::Seq;
    NodePtr goalClose;      // `OPEN ~ CLOSE body` — the goal, appended after the body
    bool goalAfterNext = false; // …which is the ONE atom that follows it
    for (;;) {
        size_t before = pos_;
        skipWs();
        bool hadSpace = pos_ > before;
        char c = peek();
        if (eof() || c == '|' || c == '&' || c == ']' ||
            (c == ')' && peek(1) != '>') ||
            (assertDepth_ > 0 && c == '>')) {
            // sigspace: TRAILING whitespace in a rule also matches <.ws> —
            // `rule TOP { \w+ '=' \N* }` accepts the line's trailing newline
            if (sigspace_ && hadSpace && !seq->kids.empty()) {
                auto ws = std::make_unique<Node>();
                ws->k = K::Subrule; ws->ruleName = "ws"; ws->ruleCapture = false;
                seq->kids.push_back(std::move(ws));
            }
            break;
        }
        // goal operator: `A ~ B C` matches `A C B` — bracket-matching sugar whose
        // BODY is the single atom after the goal, NOT the rest of the sequence.
        // `'[' ~ ']' <-[\]\n]>+ <.eol>+` is Config::INI's header: the `]` closes
        // right after the name, and the newline follows the bracket. Deferring
        // the goal to the END of the sequence read it as `[ name \n ]` — the two
        // engines came out exactly inverted, ours matching `[core\n]` and
        // rejecting `[core]\n`.
        if (c == '~' && !seq->kids.empty()) {
            pos_++; skipWs();
            goalClose = parseQuant();       // the goal itself
            goalAfterNext = true;           // …to be appended after ONE more atom
            continue;
        }
        // sigspace (a `rule`): whitespace between atoms matches <.ws> — \s* that
        // may be zero-width only OFF a word-word boundary ('foobar' !~~ /:s foo bar/)
        if (sigspace_ && !seq->kids.empty() && hadSpace) {
            auto ws = std::make_unique<Node>();
            ws->k = K::Subrule; ws->ruleName = "ws"; ws->ruleCapture = false;
            seq->kids.push_back(std::move(ws));
        }
        seq->kids.push_back(parseQuant());
        // The goal closes right after its body — the single atom that follows it,
        // not the rest of the sequence. `'[' ~ ']' <-[\]\n]>+ <.eol>+` (Config::
        // INI's header) is `[ name ] eol+`; closing at the end of the sequence
        // read it as `[ name eol+ ]`, and the two engines came out exactly
        // inverted — ours matching "[core\n]" and rejecting "[core]\n". Appending
        // HERE rather than parsing the body inline keeps the loop's sigspace
        // handling, which a `rule` needs around the body (JSON::Tiny::Grammar's
        // `rule object { '{' ~ '}' <pairlist> }`).
        if (goalAfterNext) { seq->kids.push_back(std::move(goalClose)); goalAfterNext = false; }
    }
    if (goalClose) seq->kids.push_back(std::move(goalClose)); // a goal with no body at all
        // Peephole: adjacent single-char literals with identical flags merge into
    // one Lit node. The fold-aware :i matcher must see a one-to-many case fold
    // (text ß vs pattern SS) inside ONE node — per-char nodes can never match
    // it — and byte-run literal matching is faster besides.
    {
        std::vector<NodePtr> merged;
        for (auto& kd : seq->kids) {
            if (!merged.empty() && kd->k == K::Lit && merged.back()->k == K::Lit &&
                merged.back()->icase == kd->icase && merged.back()->imark == kd->imark &&
                !kd->negate && !merged.back()->negate)
                merged.back()->lit += kd->lit;
            else
                merged.push_back(std::move(kd));
        }
        seq->kids = std::move(merged);
    }
    if (seq->kids.size() == 1) return std::move(seq->kids[0]);
    return seq;
}

// Gather every capturing <subrule> key inside a quantified atom: those captures
// are list-valued in Rakudo even with 0 or 1 occurrences (`<pair>*` gives an
// Array). Zero-width assertions (<?before …>) don't record captures — skip them.
void Regex::collectListNames(const Node* n) {
    if (!n || n->k == K::Look) return;
    if (n->k == K::Subrule && n->ruleCapture && !n->ruleName.empty()) {
        if (!listNames_) listNames_ = std::make_shared<std::set<std::string>>();
        listNames_->insert(n->ruleAlias.empty() ? n->ruleName : n->ruleAlias);
    }
    // a NAMED group alias is list-valued under a quantifier too:
    // `[\%$<bit>=[..]]+` gives $<bit> = [Match, Match, …] (URI::Encode's decoder)
    if (n->k == K::Group && !n->capName.empty()) {
        if (!listNames_) listNames_ = std::make_shared<std::set<std::string>>();
        listNames_->insert(n->capName);
    }
    // …and so is a POSITIONAL capture nested inside the quantified atom:
    // `[ (x) ]+` gives $0 = [Match, Match, …], exactly as `(x)+` does. Only the
    // quantified atom ITSELF was being marked, so a capture one level in stayed
    // a lone Match — URI::Escape's decoder reads `$0.flatmap(…)` over
    // `[ '%' (<.xdigit> ** 2) ]+` and got a Match instead of the list, so
    // uri-unescape returned the empty string for every input.
    if (n->k == K::Group && n->capIndex >= 0) {
        const_cast<Node*>(n)->listCap = true;
        listCaps_.insert(n->capIndex);
    }
    for (auto& kd : n->kids) collectListNames(kd.get());
}

// sigspace helper: `inner` followed by a non-capturing <.ws> call (the grammar's
// own ws rule, so an overridden ws is honoured just like parseSeq's insertions)
Regex::NodePtr Regex::wsWrap(NodePtr inner) {
    auto seq = std::make_unique<Node>(); seq->k = K::Seq;
    seq->kids.push_back(std::move(inner));
    auto ws = std::make_unique<Node>(); ws->k = K::Subrule; ws->ruleName = "ws"; ws->ruleCapture = false;
    seq->kids.push_back(std::move(ws));
    return seq;
}

Regex::NodePtr Regex::parseQuant() {
    auto atom = parseAtom();
    // In sigspace, whitespace between the atom and its quantifier is Rakudo's cue to
    // apply <.ws> INSIDE the repetition (`<num> +` matches "1 2"); other whitespace is
    // kept so parseSeq can insert the inter-atom <.ws>.
    bool wsBeforeQuant = false;
    if (!sigspace_) skipWs();
    else {
        size_t qsave = pos_;
        skipWs();
        char q = peek();
        if (pos_ > qsave && (q == '*' || q == '+' || q == '?')) wsBeforeQuant = true;
        else if (pos_ > qsave) pos_ = qsave;
    }
    char c = peek();
    long mn = -2, mx = -2;
    // A repetition quantifier (*/+/**) makes a wrapped capture list-valued ($n is
    // an Array of every occurrence); `?` (optional) does not.
    auto markListCap = [&]() {
        if (atom && atom->k == K::Group && atom->capIndex >= 0) {
            atom->listCap = true;
            listCaps_.insert(atom->capIndex);
        }
        collectListNames(atom.get());
    };
    if (c == '*' && peek(1) != '*') { pos_++; mn = 0; mx = -1; markListCap(); }
    else if (c == '+') { pos_++; mn = 1; mx = -1; markListCap(); }
    else if (c == '?') { pos_++; mn = 0; mx = 1; }
    bool ngMod = false; // `**?` / `**:?` — non-greedy bounds ( `!` / `:!` = explicit greed, the default)
    if (mn == -2 && peek() == '*' && peek(1) == '*') {
        markListCap();
        pos_ += 2; skipWs();
        {   // optional greed/ratchet modifier between `**` and the bounds
            size_t msave = pos_;
            if (peek() == ':') pos_++;
            if (peek() == '?') { ngMod = true; pos_++; }
            else if (peek() == '!') pos_++;
            else if (pos_ != msave && peek() != '{' && !ascii::isdigit((unsigned char)peek())) pos_ = msave; // lone ':' before something else
            skipWs();
        }
        if (peek() == '{') { // `** { … }` — runtime bounds evaluated at match time
            int depth = 1; pos_++;
            std::string code;
            while (!eof() && depth > 0) { char d = pat_[pos_++]; if (d == '{') depth++; else if (d == '}') { depth--; if (!depth) break; } code += d; }
            auto rep = std::make_unique<Node>();
            rep->k = K::Rep; rep->min = 0; rep->max = -1; rep->greedy = !ngMod; rep->repCode = code;
            rep->kids.push_back(wsBeforeQuant ? wsWrap(std::move(atom)) : std::move(atom));
            return rep;
        }
        long lo = 0; bool haveLo = false;
        while (ascii::isdigit((unsigned char)peek())) { lo = lo * 10 + (pat_[pos_++] - '0'); haveLo = true; }
        mn = haveLo ? lo : 0;
        if (peek() == '.' && peek(1) == '.') {
            pos_ += 2;
            if (peek() == '*' || peek() == 'I') { pos_++; mx = -1; }
            else { long hi = 0; while (ascii::isdigit((unsigned char)peek())) hi = hi * 10 + (pat_[pos_++] - '0'); mx = hi; }
        } else {
            mx = mn;
        }
    }
    if (mn == -2) return atom; // no quantifier
    auto rep = std::make_unique<Node>();
    rep->k = K::Rep; rep->min = mn; rep->max = mx; rep->greedy = !ngMod;
    // Quantifier modifier: `?` frugal, `!` greedy, `:` ratchet (possessive). Each may
    // also be spelled with a leading colon — `a*:?` is frugal, `a*:!` greedy, and a
    // bare `a*:` is the ratchet. Consuming the `:` without looking at what follows
    // turned `xa*:!` into a possessive `a*` followed by a literal `!`.
    if (peek() == '?') { rep->greedy = false; pos_++; }
    else if (peek() == '+' || peek() == '!') { pos_++; }   // explicit greedy
    else if (peek() == ':') {
        pos_++;
        if (peek() == '?') { rep->greedy = false; pos_++; }
        else if (peek() == '!') { pos_++; }
        else rep->possessive = true;                       // `a*:` — no backtracking into it
    }
    // Sigspace with whitespace before the quantifier: <.ws> joins each iteration —
    // `rule { <num> + }` matches "1 2" (Rakudo: the space distributes into the repetition).
    rep->kids.push_back(wsBeforeQuant ? wsWrap(std::move(atom)) : std::move(atom));
    // separator quantifier:  X+ % Y  (Y between items)  /  X+ %% Y  (trailing Y allowed)
    size_t sepSave = pos_;
    skipWs();
    // in a `rule`, whitespace after a quantifier is significant unless a `%` separator
    // follows — restore it so parseSeq can insert the inter-atom `\s*`.
    if (sigspace_ && peek() != '%') pos_ = sepSave;
    if (peek() == '%') {
        pos_++;
        if (peek() == '%') { pos_++; rep->sepTrail = true; } // %%: trailing separator allowed
        skipWs();
        NodePtr sep = parseAtom();
        // the separator may itself be quantified: `X* %% ['\\' . ]+` (zef's
        // identity value-regex). One level, no nested separators.
        if (peek() == '+' || peek() == '*' || peek() == '?') {
            auto srep = std::make_unique<Node>();
            srep->k = K::Rep;
            srep->min = peek() == '+' ? 1 : 0;
            srep->max = peek() == '?' ? 1 : -1;
            srep->greedy = true;
            pos_++;
            if (peek() == '?') { srep->greedy = false; pos_++; }
            srep->kids.push_back(std::move(sep));
            sep = std::move(srep);
        }
        if (sigspace_) {
            // In a `rule`, <.ws> follows the separator (`<x>* %% ','` matches "a, b").
            // Whitespace BEFORE the separator is allowed only via the iteration unit's
            // trailing <.ws>, i.e. when the quantifier had a leading space — Rakudo
            // matches "1 , 2" with `<num> * % \,` but not with `<num>* % \,`.
            sep = wsWrap(std::move(sep));
        }
        rep->sep = std::move(sep);
    }
    return rep;
}

static std::string ruleFlag(const std::string& nm); // built-in rule → classMatch flags
bool charClassMatch(char flag, uint32_t cp);        // the POSIX-named classes, over a codepoint

Regex::NodePtr Regex::parseAtom() {
    skipWs();
    char c = peek();
    if (c == '\x01' && spliceSpan(pat_, pos_)) return parseSplice(); // an interpolated regex VALUE
    if (c == ')' && peek(1) == '>') { // `)>` — match-capture end (pairs with `<(`)
        pos_ += 2;
        auto n = std::make_unique<Node>(); n->k = K::CapEnd; return n;
    }
    if (c == '{') { // bare code block { … } — execute for side effects, zero-width
        int depth = 1; pos_++;
        std::string code;
        while (!eof() && depth > 0) { char d = pat_[pos_++]; if (d == '{') depth++; else if (d == '}') { depth--; if (!depth) break; } code += d; }
        auto cn = std::make_unique<Node>(); cn->k = K::Code; cn->lit = code; cn->runOnly = true; cn->ltmStop = true; return cn;
    }
    if (c == ':' && pat_.compare(pos_, 3, ":my") == 0) { // :my $x [= …]; — a declaration statement
        pos_++; // ':'
        std::string code;
        while (!eof() && peek() != ';' && peek() != '}' && peek() != '>') code += pat_[pos_++];
        if (peek() == ';') pos_++;
        auto cn = std::make_unique<Node>(); cn->k = K::Code; cn->lit = code; cn->runOnly = true; return cn;
    }
    if (c == ':') { // ratchet / backtrack-control marker (`:`, `::`, `:!`) — rakupp is greedy: no-op
        pos_++; if (peek() == ':') pos_++; else if (peek() == '!') pos_++;
        auto nop = std::make_unique<Node>(); nop->k = K::Nop; return nop;
    }
    // word-boundary anchors (zero-width): `<<` or `«` = left edge of a word,
    // `>>` or `»` = right edge.  « = U+00AB (C2 AB), » = U+00BB (C2 BB).
    if ((c == '<' && peek(1) == '<') || ((unsigned char)c == 0xC2 && (unsigned char)peek(1) == 0xAB)) {
        pos_ += 2; auto n = std::make_unique<Node>(); n->k = K::WBLeft; return n;
    }
    if ((c == '>' && peek(1) == '>') || ((unsigned char)c == 0xC2 && (unsigned char)peek(1) == 0xBB)) {
        pos_ += 2; auto n = std::make_unique<Node>(); n->k = K::WBRight; return n;
    }
    if ((c == '$' || c == '@' || c == '%') && peek(1) == '<') {
        // named capture: $<name>=(...) / $<name>=[...]; `@<name>=…` is list-valued
        // (occurrences → Array); `%<name>=…` is hash-valued (occurrences → Hash keys).
        bool listCap = (c == '@');
        bool hashCap = (c == '%');
        size_t save = pos_;
        pos_ += 2;
        std::string name;
        while (pos_ < pat_.size() && peek() != '>') name += pat_[pos_++];
        if (peek() == '>') pos_++;
        skipWs(); // allow `$<value> = [ … ]` with spaces around `=`
        if (peek() == '=') {
            pos_++;
            skipWs();
            auto child = parseQuant(); // bind the whole quantified atom: `$<v>=.*` = `$<v>=[.*]`
            if (listCap || hashCap) { // force the named capture to be list/hash-valued
                if (listCap) {
                    if (!listNames_) listNames_ = std::make_shared<std::set<std::string>>();
                    listNames_->insert(name);
                } else {
                    if (!hashNames_) hashNames_ = std::make_shared<std::set<std::string>>();
                    hashNames_->insert(name);
                }
                // `@<name>=( … )+` / `%<name>=( … )+` collects EACH iteration: name the
                // inner repeated group so every occurrence collates under the name.
                Node* inner = child.get();
                if (inner->k == K::Rep && !inner->kids.empty() && inner->kids[0]->k == K::Group)
                    inner = inner->kids[0].get();
                if (inner->k == K::Group) { inner->capName = name; return child; }
            }
            auto g = std::make_unique<Node>();
            g->k = K::Group; g->capIndex = -1; g->capName = name;
            g->kids.push_back(std::move(child));
            return g;
        }
        // `$<name>` with no `=` is a BACKREFERENCE to the named capture: match its
        // already-captured text literally, WITHOUT creating a new capture. XML close
        // tags rely on this — `token element { '<' <name> … '</' $<name> '>' }` — and
        // without it `<name>` would be captured twice (the tag name doubled).
        if (c == '$' && !name.empty()) {
            auto vm = std::make_unique<Node>();
            vm->k = K::VarMatch; vm->lit = "$<" + name + ">";
            return vm;
        }
        pos_ = save; // not a named capture (e.g. @<name>/%<name> without =)
    }
    if (c == '(') {
        pos_++;
        int idx = ncaps_++;
        bool savedI = curIcase_, savedS = sigspace_, savedM = curImark_;
        auto child = parseAlt();
        curIcase_ = savedI; sigspace_ = savedS; curImark_ = savedM;
        if (peek() == ')') pos_++;
        auto g = std::make_unique<Node>();
        g->k = K::Group; g->capIndex = idx;
        g->kids.push_back(std::move(child));
        return g;
    }
    if (c == '[') {
        pos_++;
        bool savedI = curIcase_, savedS = sigspace_, savedM = curImark_;
        auto child = parseAlt();
        curIcase_ = savedI; sigspace_ = savedS; curImark_ = savedM;
        if (peek() == ']') pos_++;
        auto g = std::make_unique<Node>();
        g->k = K::Group; g->capIndex = -1;
        g->kids.push_back(std::move(child));
        return g;
    }
    if (c == '<') {
        pos_++;
        if (peek() == '(') { pos_++; auto n = std::make_unique<Node>(); n->k = K::CapStart; return n; } // <( match-capture start
        // `<|w>` — a zero-width WORD boundary: either edge, i.e. `<<` or `>>`.
        // (`<?|w>`/`<!|w>` route through the assertion reader below.)
        if (peek() == '|' && peek(1) == 'w' && peek(2) == '>') {
            pos_ += 3;
            auto alt = std::make_unique<Node>(); alt->k = K::Alt; alt->firstMatch = true;
            auto l = std::make_unique<Node>(); l->k = K::WBLeft;
            auto r = std::make_unique<Node>(); r->k = K::WBRight;
            alt->kids.push_back(std::move(l)); alt->kids.push_back(std::move(r));
            return alt;
        }
        // Enumerated string alternation: `< + - >` / `< foo bar >` (a LEADING space after
        // `<` signals the quoted-word-list form) matches any of the literal words, longest first.
        if (peek() == ' ' || peek() == '\t') {
            std::vector<std::string> words;
            while (!eof() && peek() != '>') {
                while (peek() == ' ' || peek() == '\t') pos_++;
                if (eof() || peek() == '>') break;
                std::string w; while (!eof() && peek() != ' ' && peek() != '\t' && peek() != '>') w += pat_[pos_++];
                if (!w.empty()) words.push_back(w);
            }
            if (peek() == '>') pos_++;
            std::stable_sort(words.begin(), words.end(), [](const std::string& a, const std::string& b) { return a.size() > b.size(); });
            auto alt = std::make_unique<Node>(); alt->k = K::Alt;
            for (auto& w : words) { // each word is ONE Lit: the :i fold matcher needs whole spans
                auto n = std::make_unique<Node>(); n->k = K::Lit; n->icase = curIcase_; n->imark = curImark_;
                n->lit = w; alt->kids.push_back(std::move(n));
            }
            return alt;
        }
        auto node = std::make_unique<Node>();
        node->k = K::Class; node->icase = curIcase_;
        // char class, possibly composed: `[..]`, `-[..]`, `+[..]`, `<+alpha>`, `<+[A]+alpha>`.
        // (A bare `<-name>` is the negated-subrule branch further down, not this.)
        // `<-space-[\"]>`: a leading NEGATED builtin class that goes on to compose
        // with a bracket. The negated-subrule branch below can chain `-name`/`+name`
        // but has no bracket member, so it would drop the `-[\"]` silently; route it
        // here instead, where a leading `-` sets `negate` and the bracket subtracts.
        // A bare `<-space>` (no bracket to follow) keeps the old path.
        auto negFlagComposes = [&]() -> bool {
            if (peek() != '-' || !(ascii::isalpha((unsigned char)peek(1)) || peek(1) == '_')) return false;
            size_t q = pos_ + 1;
            std::string nm;
            while (q < pat_.size() && (ascii::isalnum((unsigned char)pat_[q]) || pat_[q] == '_' ||
                                       (pat_[q] == '-' && q + 1 < pat_.size() &&
                                        (ascii::isalnum((unsigned char)pat_[q + 1]) || pat_[q + 1] == '_'))))
                nm += pat_[q++];
            if (ruleFlag(nm).empty()) return false;
            while (q < pat_.size() && (pat_[q] == ' ' || pat_[q] == '\t')) q++;
            if (q < pat_.size() && pat_[q] == '[') return true;
            if (q + 1 < pat_.size() && (pat_[q] == '+' || pat_[q] == '-')) {
                size_t r = q + 1;
                while (r < pat_.size() && (pat_[r] == ' ' || pat_[r] == '\t')) r++;
                return r < pat_.size() && pat_[r] == '[';
            }
            return false;
        };
        // `<- [a..z]>` — whitespace is insignificant in a regex, so a blank may sit
        // between the sign and the bracket. Requiring them adjacent meant `<- [x]>`
        // was not recognised as a character class at all and fell through to the
        // assertion branches, where it matched empty at every BYTE position
        // (URI::Escape writes `<- [\-._~A..Za..z0..9]>`, so nothing was escaped).
        auto signThenBracket = [&]() -> bool {
            if (peek() != '+' && peek() != '-') return false;
            size_t q = pos_ + 1;
            while (q < pat_.size() && (pat_[q] == ' ' || pat_[q] == '\t')) q++;
            return q < pat_.size() && pat_[q] == '[';
        };
        if (peek() == '[' || signThenBracket() ||
            (peek() == '+' && (ascii::isalpha((unsigned char)peek(1)) || peek(1) == '_' || peek(1) == '.' || peek(1) == ':')) ||
            negFlagComposes()) {
            node->negate = false;
            bool first = true;
            // USER-token parts (`<[\-+.] +uri-alpha +digit>` in the RFC 3986 grammar)
            // can't fold into class flags — collect them and build a composite:
            //   [ <!minus1> <!-subtracted-bracket> [ base-class | <plus1> | <plus2> ] ]
            std::vector<std::string> plusSubs, minusSubs;
            // `+:Prop` / `-:Prop` members, kept as Unicode properties
            std::vector<std::string> plusProps, minusProps;
            std::unique_ptr<Node> negBracket; // `- [..]` after the first member: a subtraction
            // `<-[ab]+[b]>` re-admits `b`: under a NEGATED base a `+member` unions with
            // the complement, it does not join the set being complemented. Collect those
            // members separately and offer them as an alternative.
            std::unique_ptr<Node> posExtra;
            auto posExtraNode = [&]() {
                if (!posExtra) { posExtra = std::make_unique<Node>(); posExtra->k = K::Class; posExtra->icase = curIcase_; }
                return posExtra.get();
            };
            for (;;) {
                while (peek() == ' ' || peek() == '\t') pos_++; // blanks between members / before '>'
                if (!(peek() == '[' || peek() == '+' || peek() == '-')) break;
                char op = '+';
                if (peek() == '+') { pos_++; op = '+'; }
                else if (peek() == '-') { pos_++; op = '-'; }
                while (peek() == ' ' || peek() == '\t') pos_++; // `<+tok - [x]>` — blanks after the op
                if (peek() == '[') {
                    pos_++;
                    if (op == '-' && first) { node->negate = true; parseClassBodyMember(node.get()); }
                    else if (op == '-') { // `<+unenc-pchar - [:]>` — subtracted bracket
                        if (!negBracket) { negBracket = std::make_unique<Node>(); negBracket->k = K::Class; negBracket->icase = curIcase_; }
                        parseClassBodyMember(negBracket.get());
                    }
                    else if (node->negate) parseClassBodyMember(posExtraNode());
                    else parseClassBodyMember(node.get());
                }
                else { // +rule / -rule member (builtin, unicode property, or USER token)
                    if (peek() == '.') pos_++;
                    // a '-' directly between ident chars is part of a KEBAB-CASE name
                    // (`+uri-alpha`); a standalone '-' (spaced, or before '[') is the
                    // subtraction operator
                    std::string nm;
                    while (!eof()) {
                        char pc = peek();
                        if (pc == '>' || pc == '+' || pc == '[') break;
                        if (pc == '-') {
                            char nx = peek(1);
                            bool prevIdent = !nm.empty() &&
                                (ascii::isalnum((unsigned char)nm.back()) || nm.back() == '_');
                            if (!prevIdent || !(ascii::isalnum((unsigned char)nx) || nx == '_')) break;
                        }
                        nm += pat_[pos_++];
                    }
                    size_t a = nm.find_first_not_of(" \t"), b = nm.find_last_not_of(" \t");
                    if (a != std::string::npos) nm = nm.substr(a, b - a + 1);
                    std::string fl = ruleFlag(nm);
                    // unicode-property part (`+:N +:S` in uri-alphanum): approximate
                    // the common ones; unsupported ones drop (ASCII-liberal is fine)
                    // A `+:Prop` member is a real Unicode property, matched
                    // codepoint-wise by the same machinery a standalone `<:Prop>`
                    // uses. Approximating :N as "digit" and :L as "alpha" and
                    // DROPPING every other property silently is what made
                    // `<+uri-alpha +:N +:S>` reject a snowman: :S was thrown away,
                    // so URI could not parse `http://host/echo2/☃`.
                    std::string uprop;
                    if (fl.empty() && !nm.empty() && nm[0] == ':' && nm.size() > 1)
                        uprop = nm.substr(1);
                    if (!uprop.empty()) {
                        if (op == '+') plusProps.push_back(uprop);
                        else minusProps.push_back(uprop);
                    }
                    else if (!fl.empty()) {
                        if (op == '-' && first) { node->negate = true; node->classFlags += fl; }
                        else if (op == '-') node->negClassFlags += fl;
                        else if (node->negate) posExtraNode()->classFlags += fl;
                        else node->classFlags += fl;
                    }
                    else if (!nm.empty() && nm[0] != ':')
                        (op == '+' ? plusSubs : minusSubs).push_back(nm); // user-defined token part
                }
                first = false;
                if (eof()) break;
            }
            if (peek() == '>') pos_++;
            if (plusSubs.empty() && minusSubs.empty() && plusProps.empty() && minusProps.empty() && !negBracket && !posExtra) return node; // plain class (fast path)
            auto mkSub = [&](const std::string& rn) {
                auto s = std::make_unique<Node>();
                s->k = K::Subrule; s->ruleName = rn; s->ruleCapture = false;
                return s;
            };
            auto mkNegLook = [&](std::unique_ptr<Node> inner) {
                auto look = std::make_unique<Node>();
                look->k = K::Look; look->negate = true; look->behind = false;
                look->kids.push_back(std::move(inner));
                return look;
            };
            auto mkProp = [&](const std::string& pr) {
                auto n = std::make_unique<Node>();
                n->k = K::Class; n->icase = curIcase_; n->uprop = pr;
                return n;
            };
            auto seq = std::make_unique<Node>(); seq->k = K::Seq;
            for (auto& ms : minusSubs) seq->kids.push_back(mkNegLook(mkSub(ms)));
            for (auto& mp : minusProps) seq->kids.push_back(mkNegLook(mkProp(mp)));
            if (negBracket) seq->kids.push_back(mkNegLook(std::move(negBracket)));
            bool haveBase = node->negate || !node->ranges.empty() || !node->cpRanges.empty() || !node->classFlags.empty();
            if (plusSubs.empty() && plusProps.empty() && !posExtra) seq->kids.push_back(std::move(node));
            else {
                auto altN = std::make_unique<Node>(); altN->k = K::Alt; altN->firstMatch = true; altN->classCombo = true;
                if (posExtra) altN->kids.push_back(std::move(posExtra)); // union members win over the complement
                if (haveBase) altN->kids.push_back(std::move(node));
                for (auto& ps : plusSubs) altN->kids.push_back(mkSub(ps));
                for (auto& pp : plusProps) altN->kids.push_back(mkProp(pp));
                seq->kids.push_back(std::move(altN));
            }
            return seq;
        }
        else if (peek() == '-' && (ascii::isalpha((unsigned char)peek(1)) || peek(1) == '.' || peek(1) == '_')) {
            // <-name> — negated subrule char class: one char NOT matched by rule `name`.
            // Equivalent to `[ <!name> . ]`. COMPOSABLE with further set terms:
            // `<-restricted +name-sep>` (zef's identity grammar) is
            //   [ <name-sep> | <!restricted> . ]
            // — each `+rule` adds a positive alternative, each `-rule` another
            // negative lookahead on the fallback any-char branch.
            pos_++;
            if (peek() == '.') pos_++;
            auto readName = [&]() {
                std::string nm;
                while (!eof() && (ascii::isalnum((unsigned char)peek()) || peek() == '-' || peek() == '_' || peek() == ':' || peek() == '.')) {
                    // a '-' is part of a kebab-case name only between ident chars
                    if (peek() == '-' && !(ascii::isalnum((unsigned char)peek(1)) || peek(1) == '_')) break;
                    nm += pat_[pos_++];
                }
                return nm;
            };
            std::vector<std::string> negs{readName()};
            std::vector<std::string> poss;
            std::string args;
            if (peek() == '(') { int d = 1; pos_++; while (!eof() && d > 0) { char x = pat_[pos_++]; if (x == '(') d++; else if (x == ')') { d--; if (!d) break; } args += x; } }
            for (;;) {
                while (!eof() && (peek() == ' ' || peek() == '\t')) pos_++;
                if (peek() == '+' && (ascii::isalpha((unsigned char)peek(1)) || peek(1) == '_')) { pos_++; poss.push_back(readName()); }
                else if (peek() == '-' && (ascii::isalpha((unsigned char)peek(1)) || peek(1) == '_')) { pos_++; negs.push_back(readName()); }
                else break;
            }
            if (peek() == '>') pos_++;
            auto mkSubN = [&](const std::string& n, bool cap) {
                auto s = std::make_unique<Node>();
                s->k = K::Subrule; s->ruleName = n; s->ruleCapture = cap;
                if (n == negs[0]) s->ruleArgs = args;
                return s;
            };
            auto fall = std::make_unique<Node>(); fall->k = K::Seq; // <!neg1> <!neg2> … .
            for (auto& n : negs) {
                auto look = std::make_unique<Node>();
                look->k = K::Look; look->negate = true; look->behind = false;
                look->kids.push_back(mkSubN(n, false));
                fall->kids.push_back(std::move(look));
            }
            auto any = std::make_unique<Node>(); any->k = K::Any;
            fall->kids.push_back(std::move(any));
            if (poss.empty()) return fall;
            auto altN = std::make_unique<Node>(); altN->k = K::Alt; altN->firstMatch = true; altN->classCombo = true;
            for (auto& p : poss) altN->kids.push_back(mkSubN(p, false)); // positives first: '::' beats not-':'
            altN->kids.push_back(std::move(fall));
            return altN;
        }
        else if (peek() == '?' || peek() == '!') {
            // zero-width assertion: <?before P> / <!before P> / <?after P> / <!after P>
            // / <?rule> / <![...]> ; a code assertion <?{ … }> stays lenient (no-op).
            bool neg = (peek() == '!');
            pos_++; skipWs();
            if (peek() == '{') { // code assertion <?{ code }> / <!{ code }> — evaluated via the interpreter hook
                int depth = 1; pos_++;
                std::string code;
                while (!eof() && depth > 0) {
                    char d = pat_[pos_++];
                    if (d == '{') depth++;
                    else if (d == '}') { depth--; if (depth == 0) break; }
                    code += d;
                }
                if (peek() == '>') pos_++;
                auto cn = std::make_unique<Node>(); cn->k = K::Code; cn->lit = code; cn->negate = neg;
                return cn;
            }
            bool behind = false;
            bool lookKw = false; // an explicit `before`/`after` keyword was consumed
            bool savedAdvI = curIcase_, savedAdvS = sigspace_, savedAdvM = curImark_; // adverbs inside an assertion are scoped to it
            // `before`/`after` may be followed by any whitespace (space, newline, tab),
            // not just a literal space — the inner pattern can start on the next line.
            auto kw = [&](const char* w, size_t n) {
                if (pat_.compare(pos_, n, w) != 0) return false;
                char after = pos_ + n < pat_.size() ? pat_[pos_ + n] : '\0';
                return after == ' ' || after == '\n' || after == '\t' || after == '\r';
            };
            if (kw("before", 6)) { pos_ += 6; skipWs(); lookKw = true; }
            else if (kw("after", 5)) { pos_ += 5; behind = true; skipWs(); lookKw = true; }
            else if (ascii::isalpha((unsigned char)peek()) || peek() == '_' || peek() == '.') {
                // <?name> / <!name(args)> — zero-width subrule assertion (not a pattern lookahead)
                if (peek() == '.') pos_++;
                std::string nm; while (!eof() && peek() != '>' && peek() != '(') nm += pat_[pos_++];
                std::string args;
                if (peek() == '(') { int d = 1; pos_++; while (!eof() && d > 0) { char x = pat_[pos_++]; if (x == '(') d++; else if (x == ')') { d--; if (!d) break; } args += x; } }
                if (peek() == '>') pos_++;
                auto sub = std::make_unique<Node>();
                sub->k = K::Subrule; sub->ruleName = nm; sub->ruleArgs = args; sub->ruleCapture = false;
                auto look = std::make_unique<Node>();
                look->k = K::Look; look->negate = neg; look->behind = false;
                look->kids.push_back(std::move(sub));
                return look;
            }
            else if (peek() == ':') {
                // <?:prop> / <!:prop> — zero-width Unicode-property assertion (balance
                // an inner value like <!:bc<L>>). Wrap a Class{uprop} in a lookahead.
                pos_++; // ':'
                std::string nm; int adepth = 0;
                while (!eof() && (peek() != '>' || adepth > 0)) {
                    char ch = peek(); if (ch == '<') adepth++; else if (ch == '>') adepth--;
                    nm += pat_[pos_++];
                }
                if (peek() == '>') pos_++;
                auto cls = std::make_unique<Node>(); cls->k = K::Class; cls->icase = curIcase_; cls->uprop = nm;
                auto look = std::make_unique<Node>();
                look->k = K::Look; look->negate = neg; look->behind = false;
                look->kids.push_back(std::move(cls));
                return look;
            }
            // <![...]> / <?[...]> — a class assertion: the inner is a CHARACTER CLASS,
            // not a group (a quote member like <!["]> must not open a string literal).
            // Only the keyword-less form is a class: after an explicit `before`/`after`
            // the bracket is an ordinary non-capturing group (`<?before [ 'a' ]* 'b'>`).
            if (!behind && !lookKw && (peek() == '[' || ((peek() == '-' || peek() == '+') && peek(1) == '['))) {
                auto cls = std::make_unique<Node>();
                cls->k = K::Class; cls->icase = curIcase_;
                if (peek() == '-') { pos_++; cls->negate = true; }
                else if (peek() == '+') pos_++;
                pos_++; // '['
                parseClassBodyMember(cls.get());
                if (peek() == '>') pos_++;
                auto look = std::make_unique<Node>();
                look->k = K::Look; look->negate = neg; look->behind = false;
                look->kids.push_back(std::move(cls));
                return look;
            }
            assertDepth_++;
            auto child = parseAlt();
            curIcase_ = savedAdvI; sigspace_ = savedAdvS; curImark_ = savedAdvM;
            assertDepth_--;
            skipWs();
            if (peek() == '>') pos_++;
            auto look = std::make_unique<Node>();
            look->k = K::Look; look->negate = neg; look->behind = behind;
            look->kids.push_back(std::move(child));
            return look;
        }
        else if (peek() == '&' || peek() == '{') {
            // <&code> dynamic subrule / <{ code }> — unsupported: zero-width no-op
            int depth = 1; pos_++;
            while (!eof() && depth > 0) { char d = pat_[pos_++]; if (d == '<') depth++; else if (d == '>') depth--; }
            auto nop = std::make_unique<Node>(); nop->k = K::Nop; return nop;
        } else {
            // named char class or subrule; balance inner <…> so a property value
            // like `<:bc<L>>` (property `bc`, value `L`) reads as one unit.
            std::string name;
            int adepth = 0;
            while (!eof() && (peek() != '>' || adepth > 0)) {
                char ch = peek();
                if (ch == '<') adepth++;
                else if (ch == '>') adepth--;
                name += pat_[pos_++];
            }
            if (peek() == '>') pos_++;
            // a leading `.` (<.space>) means a non-capturing call; strip it for the
            // built-in class check so `<.space>` also resolves to `\s`.
            bool dotless = !name.empty() && name[0] == '.';
            if (dotless) name = name.substr(1);
            // `<-:Prop>` — inverted Unicode property (a char NOT having Prop).
            if (name.size() > 1 && name[0] == '-' && name[1] == ':') { node->negate = true; name = name.substr(1); }
            // Unicode property class: <:Nd> <:L> <:Alpha> <:!Upper> (codepoint-aware)
            if (!name.empty() && (name[0] == ':' || (name[0] == '!' && name.size() > 1 && name[1] == ':'))) {
                std::string p = name;
                if (p[0] == '!') { node->negate = true; p = p.substr(1); }
                p = p.substr(1); // drop ':'
                if (!p.empty() && p[0] == '!') { node->negate = !node->negate; p = p.substr(1); }
                node->uprop = p;
                return node;
            }
            // Built-in char-class names (<digit>, <alpha>, <blank>, …) are NOT
            // decided here. They used to compile straight to a Class node, which
            // settled the question before any grammar existed — so a grammar's own
            // `token blank { \h* \n }` was silently ignored and the built-in
            // horizontal-whitespace class answered instead. Rakudo resolves the
            // other way: the grammar's definition wins and the built-in is only a
            // fallback for names nothing defines.
            //
            // So every one of them takes the subrule path below, exactly as the
            // dotted form <.blank> already did, and the decision moves to where the
            // grammar is actually in scope:
            //   - in a grammar   nameMeta() sets builtinClass only `if (!rule)`
            //                    (the same reasoning <ws> has always had), and
            //                    matchSubMeta keeps the one-byte class test
            //   - a plain regex  has no grammar, so builtinRuleMatch answers —
            //                    `/<alpha>/` keeps working, and a lexical
            //                    `my regex digit {…}` now shadows it, which the
            //                    compile-time path could not see either
            // Capture is unchanged: the undotted form still fills $<digit>, now via
            // the subrule's own capKey rather than a named Group wrapped round a
            // Class node.
            {
                // subrule call <name> / <.name> / <name=other>
                auto sr = std::make_unique<Node>();
                sr->k = K::Subrule;
                sr->ruleCapture = !dotless; // <.name> is a non-capturing call
                std::string nm = name;
                auto eq = nm.find('=');
                if (eq != std::string::npos) {
                    sr->ruleAlias = nm.substr(0, eq); nm = nm.substr(eq + 1); // <alias=rule>
                    // <alias=.rule> — the dot only suppresses the RULE-NAME capture;
                    // the alias still captures (Cro::MediaType: `<attribute=.token>`)
                    if (!nm.empty() && nm[0] == '.') { nm = nm.substr(1); sr->aliasDotted = true; }
                }
                // parameterised call <name($x, '')> — peel off the argument list
                auto lp = nm.find('(');
                if (lp != std::string::npos && nm.back() == ')') {
                    sr->ruleArgs = nm.substr(lp + 1, nm.size() - lp - 2);
                    nm = nm.substr(0, lp);
                }
                sr->ruleName = nm;
                return sr;
            }
        }
        // (unreachable: every `<...>` branch above returns after eating its own `>`)
    }
    if (c == '\'' || c == '"') {
        char q = c;
        pos_++;
        auto seq = std::make_unique<Node>(); seq->k = K::Seq;
        std::string lit;
        // a quoted multi-char literal is ONE Lit node: the fold-aware :i matcher
        // must see one-to-many case folds (text ß vs pattern SS) inside a single
        // node — per-char Lits could never match them (m:i/'WEISS'/ vs Weiß).
        // Quantifiers are unaffected: they bind the returned atom whole.
        auto flush = [&]() {
            if (lit.empty()) return;
            auto n = std::make_unique<Node>(); n->k = K::Lit; n->icase = curIcase_; n->imark = curImark_;
            n->lit = lit; seq->kids.push_back(std::move(n));
            lit.clear();
        };
        while (!eof() && peek() != q) {
            if (peek() == '\\' && pos_ + 1 < pat_.size()) {
                pos_++; char e = pat_[pos_++];
                // A SINGLE-quoted literal has exactly two escapes, `\\` and `\'`;
                // everything else keeps its backslash, so `'\n'` is backslash-then-n
                // and `'\u'` is backslash-then-u. Applying the double-quoted rules to
                // both spellings silently ATE the backslash — which is why
                // JSON::Tiny's `<utf16_codepoint>+ % '\u'` never saw its separator
                // and decoded each half of a surrogate pair on its own.
                if (q == '\'') {
                    if (e == '\\' || e == '\'') lit += e;
                    else { lit += '\\'; lit += e; }
                }
                else if ((e == 'x' || e == 'o') &&
                         (peek() == '[' || ascii::isalnum((unsigned char)peek()))) {
                    // "\x41" / "\x[263a]" / "\o[17]" inside a DOUBLE-quoted span
                    // decode exactly as they do in a qq string — HTTP::MediaType's
                    // grammar writes its SP/HTAB/DQUOTE rules as `"\x20"` literals
                    int base = e == 'x' ? 16 : 8;
                    auto emitCp = [&](long cp) {
                        if (cp < 0) return;
                        uint32_t u = (uint32_t)cp;
                        if (u < 0x80) lit += (char)u;
                        else if (u < 0x800) { lit += (char)(0xC0 | (u >> 6)); lit += (char)(0x80 | (u & 0x3F)); }
                        else if (u < 0x10000) { lit += (char)(0xE0 | (u >> 12)); lit += (char)(0x80 | ((u >> 6) & 0x3F)); lit += (char)(0x80 | (u & 0x3F)); }
                        else { lit += (char)(0xF0 | (u >> 18)); lit += (char)(0x80 | ((u >> 12) & 0x3F)); lit += (char)(0x80 | ((u >> 6) & 0x3F)); lit += (char)(0x80 | (u & 0x3F)); }
                    };
                    if (peek() == '[') {
                        pos_++;
                        std::string body;
                        while (!eof() && peek() != ']') body += pat_[pos_++];
                        if (peek() == ']') pos_++;
                        size_t p = 0;
                        while (p <= body.size()) {
                            size_t comma = body.find(',', p);
                            std::string tok = body.substr(p, comma == std::string::npos ? std::string::npos : comma - p);
                            size_t a2 = tok.find_first_not_of(" \t"), b2 = tok.find_last_not_of(" \t");
                            if (a2 != std::string::npos)
                                emitCp(std::strtol(tok.substr(a2, b2 - a2 + 1).c_str(), nullptr, base));
                            if (comma == std::string::npos) break;
                            p = comma + 1;
                        }
                    } else {
                        std::string digits;
                        auto isd = [&](char ch) { return base == 16 ? ascii::isxdigit((unsigned char)ch) != 0 : (ch >= '0' && ch <= '7'); };
                        while (!eof() && isd(peek())) digits += pat_[pos_++];
                        emitCp(std::strtol(digits.c_str(), nullptr, base));
                    }
                }
                else switch (e) { case 'n': lit += '\n'; break; case 't': lit += '\t'; break;
                             case 'r': lit += '\r'; break; case '0': lit += '\0'; break; default: lit += e; }
            } else if (q == '"' && peek() == '{') {
                // "…{EXPR}…" inside a double-quoted regex literal evaluates at
                // match time and matches the result's Str literally, exactly as
                // a qq string interpolates — Test::Output writes
                // /^ "warning!{$nl}" $/ and Rakudo matches it
                pos_++;
                std::string expr;
                int depth = 1;
                while (!eof()) {
                    char p = pat_[pos_++];
                    if (p == '{') depth++;
                    else if (p == '}' && --depth == 0) break;
                    expr += p;
                }
                flush();
                auto vm = std::make_unique<Node>(); vm->k = K::VarMatch; vm->lit = expr;
                seq->kids.push_back(std::move(vm));
            } else if (q == '"' && peek() == '$' &&
                       (ascii::isalnum((unsigned char)peek(1)) || peek(1) == '_')) {
                // "$0" / "$var" inside a double-quoted regex literal matches the
                // value at match time (in-flight capture or scope variable)
                pos_++;
                std::string var = "$";
                while (!eof()) {
                    char p = peek();
                    if (ascii::isalnum((unsigned char)p) || p == '_') { var += p; pos_++; }
                    else if (p == '-' && (ascii::isalnum((unsigned char)peek(1)) || peek(1) == '_')) { var += p; pos_++; }
                    else break;
                }
                flush();
                auto vm = std::make_unique<Node>(); vm->k = K::VarMatch; vm->lit = var; seq->kids.push_back(std::move(vm));
            } else lit += pat_[pos_++];
        }
        if (peek() == q) pos_++;
        // An EXPLICIT empty literal `''` is a real zero-width atom, not "nothing
        // parsed". Returning an empty Seq made it indistinguishable from a
        // cosmetic empty branch, and parseAlt drops those — so `[ '' || 'zz' ]`
        // lost its first alternative and never matched the empty string. URI
        // spells its Scheme subset `/^ [ '' || <…::scheme> ] $/`.
        if (seq->kids.empty() && lit.empty()) {
            auto n = std::make_unique<Node>(); n->k = K::Lit;
            n->icase = curIcase_; n->imark = curImark_; n->lit = "";
            return n;
        }
        if (seq->kids.empty() && lit.size() == 1) { auto n = std::make_unique<Node>(); n->k = K::Lit; n->icase = curIcase_; n->imark = curImark_; n->lit = lit; return n; }
        flush();
        if (seq->kids.size() == 1) return std::move(seq->kids[0]);
        return seq;
    }
    if (c == '.') { pos_++; auto n = std::make_unique<Node>(); n->k = K::Any; return n; }
    if (c == '^') { pos_++; auto n = std::make_unique<Node>(); n->k = K::AnchorStart; if (peek() == '^') { pos_++; n->multiline = true; } return n; }
    if (c == '$') {
        // Numbered capture alias `$N=(...)`: the group captures into position N,
        // and auto-numbering continues from N+1 (`(.)(.)$7=(.)(.)` → $0 $1 $7 $8).
        {
            int j = 1; std::string num;
            while (ascii::isdigit((unsigned char)peek(j))) { num += peek(j); j++; }
            if (!num.empty() && peek(j) == '=' && peek(j + 1) == '(') {
                int idx = std::atoi(num.c_str());
                for (int t = 0; t < j + 2; t++) pos_++; // consume `$N=(`
                bool savedI = curIcase_, savedS = sigspace_, savedM = curImark_;
                auto child = parseAlt();
                curIcase_ = savedI; sigspace_ = savedS; curImark_ = savedM;
                if (peek() == ')') pos_++;
                auto g = std::make_unique<Node>();
                g->k = K::Group; g->capIndex = idx;
                g->kids.push_back(std::move(child));
                if (idx + 1 > ncaps_) ncaps_ = idx + 1; // auto-numbering resumes after N
                return g;
            }
        }
        // end anchor only when not an interpolation/backref. A following '$'
        // means the `$$` end-of-line anchor (its own second char isn't a var).
        char nx = peek(1);
        // `>` closes an assertion, so `<?before $>` is the END ANCHOR inside a
        // lookahead — not a variable named `$>`. `}` closes an embedded block the
        // same way.
        if (nx == '\0' || nx == ')' || nx == ']' || nx == '|' || nx == '$' || nx == '>' ||
            nx == '}' || ascii::isspace((unsigned char)nx)) {
            pos_++;
            auto n = std::make_unique<Node>(); n->k = K::AnchorEnd;
            // `$$` = end of any LINE. Raku's `$` is the ABSOLUTE end of the
            // string — unlike Perl 5's, it does not also match before a final
            // newline (`"abc\ndef\n" ~~ /def$/` is False).
            if (peek() == '$') { pos_++; n->multiline = true; }
            else n->absEnd = true;
            return n;
        }
        // `$( … )` — an arbitrary expression, matched as its Str. The identifier
        // scan below takes letters only, so `$(` left the atom as a bare "$" and
        // the parenthesis went on to parse as a GROUP: the pattern silently
        // matched something else rather than failing, which is the worst way for
        // this to go wrong. `$( … )` is valid Raku on its own, so the whole text
        // is handed to the same hook that evaluates `$var`.
        if (nx == '(') {
            std::string expr = "$";
            pos_++;                                   // the '$'
            int depth = 0;
            while (!eof()) {
                char p = pat_[pos_++];
                expr += p;
                if (p == '(') depth++;
                else if (p == ')' && --depth == 0) break;
            }
            auto vm = std::make_unique<Node>(); vm->k = K::VarMatch; vm->lit = expr;
            return vm;
        }
        // $var — match the variable's current Str value literally at match time
        pos_++;
        std::string var = "$";
        while (!eof()) {
            char p = peek();
            if (ascii::isalnum((unsigned char)p) || p == '_') { var += p; pos_++; }
            else if (p == '-' && (ascii::isalnum((unsigned char)peek(1)) || peek(1) == '_')) { var += p; pos_++; }
            else break;
        }
        auto vm = std::make_unique<Node>(); vm->k = K::VarMatch; vm->lit = var; return vm;
    }
    if (c == '\\') {
        pos_++;
        char e = peek(); pos_++;
        auto n = std::make_unique<Node>();
        if (e == 'd' || e == 'w' || e == 's') { n->k = K::Class; n->icase = curIcase_; n->classFlags = std::string(1, e); return n; }
        if (e == 'D' || e == 'W' || e == 'S') { n->k = K::Class; n->icase = curIcase_; n->classFlags = std::string(1, (char)ascii::tolower(e)); n->negate = true; return n; }
        // \N — any char except a logical newline (\n, \r). \h/\v — horizontal/vertical
        // whitespace (and \H/\V their negations).
        if (e == 'N') { n->k = K::Class; n->icase = curIcase_; n->negate = true; n->ranges.push_back({'\n','\n'}); n->ranges.push_back({'\r','\r'}); return n; }
        if (e == 'h' || e == 'H') { n->k = K::Class; n->icase = curIcase_; n->negate = (e=='H'); n->ranges.push_back({' ',' '}); n->ranges.push_back({'\t','\t'}); return n; }
        // \T \R \F \E — one char that is NOT a tab / return / formfeed / escape char
        if (e == 'T' || e == 'R' || e == 'F' || e == 'E') {
            char lc = e == 'T' ? '\t' : e == 'R' ? '\r' : e == 'F' ? '\f' : '\x1b';
            n->k = K::Class; n->icase = curIcase_; n->negate = true;
            n->ranges.push_back({(unsigned char)lc, (unsigned char)lc}); return n;
        }
        // retired Perl 5 metachars die at compile time (Rakudo: X::Obsolete)
        if (e == 'A' || e == 'Z' || e == 'z' || e == 'G' || e == 'p' || e == 'P' ||
            e == 'L' || e == 'U' || e == 'Q' || (e >= '1' && e <= '9'))
            throw ObsoleteEscape{std::string("\\") + e};
        if (e == 'v' || e == 'V') { n->k = K::Class; n->icase = curIcase_; n->negate = (e=='V'); n->ranges.push_back({'\n','\n'}); n->ranges.push_back({'\r','\r'}); n->ranges.push_back({'\f','\f'}); n->ranges.push_back({'\v','\v'}); return n; }
        // \X[HH] / \O[OO] / \C[NAME] — match ONE codepoint that is NOT the given one(s).
        if ((e == 'X' || e == 'O' || e == 'C') && (peek() == '[' || (e != 'C' && ascii::isalnum((unsigned char)peek())))) {
            char le = (char)ascii::tolower((unsigned char)e);
            auto cpOf = [&](std::string t) -> int32_t {
                size_t a = t.find_first_not_of(" \t"), b = t.find_last_not_of(" \t");
                if (a == std::string::npos) return -1; t = t.substr(a, b - a + 1);
                if (le == 'x') return (int32_t)std::strtol(t.c_str(), nullptr, 16);
                if (le == 'o') return (int32_t)std::strtol(t.c_str(), nullptr, 8);
                return namedCp(t);
            };
            n->k = K::Class; n->icase = curIcase_; n->negate = true;
            auto addCp = [&](const std::string& t) { int32_t cp = cpOf(t); if (cp >= 0) n->cpRanges.push_back({(uint32_t)cp, (uint32_t)cp}); };
            if (peek() == '[') {
                pos_++; std::string body; while (!eof() && peek() != ']') body += pat_[pos_++]; if (peek() == ']') pos_++;
                for (size_t s = 0; s <= body.size(); ) { size_t cm = body.find(',', s); addCp(body.substr(s, cm == std::string::npos ? std::string::npos : cm - s)); if (cm == std::string::npos) break; s = cm + 1; }
            } else { std::string d; while (!eof() && ascii::isalnum((unsigned char)peek())) d += pat_[pos_++]; addCp(d); }
            return n;
        }
        if ((e == 'x' || e == 'o' || e == 'c') && (peek() == '[' || (e != 'c' && ascii::isalnum((unsigned char)peek()))
                                                                 || (e == 'c' && ascii::isdigit((unsigned char)peek())))) {
            // \xHH / \x[HH] / \o[OO] / \c[NAME] / \c[A, B] / \cDDD (decimal) — codepoint literal(s)
            auto encode = [](uint32_t cp) -> std::string { // minimal UTF-8 encoder
                std::string o;
                if (cp < 0x80) o += (char)cp;
                else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
                else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
                else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
                return o;
            };
            auto cpOf = [&](std::string t) -> int32_t {
                size_t a = t.find_first_not_of(" \t"), b = t.find_last_not_of(" \t");
                if (a == std::string::npos) return -1; t = t.substr(a, b - a + 1);
                if (e == 'x') return (int32_t)std::strtol(t.c_str(), nullptr, 16);
                if (e == 'o') return (int32_t)std::strtol(t.c_str(), nullptr, 8);
                return namedCp(t);
            };
            auto seq = std::make_unique<Node>(); seq->k = K::Seq;
            auto addCp = [&](const std::string& t) { int32_t cp = cpOf(t); if (cp >= 0) { auto lit = std::make_unique<Node>(); lit->k = K::Lit; lit->icase = curIcase_; lit->imark = curImark_; lit->lit = encode((uint32_t)cp); seq->kids.push_back(std::move(lit)); } };
            if (peek() == '[') {
                pos_++; std::string body; while (!eof() && peek() != ']') body += pat_[pos_++]; if (peek() == ']') pos_++;
                for (size_t s = 0; s <= body.size(); ) { size_t cm = body.find(',', s); addCp(body.substr(s, cm == std::string::npos ? std::string::npos : cm - s)); if (cm == std::string::npos) break; s = cm + 1; }
            } else if (e == 'c') { std::string d; while (!eof() && ascii::isdigit((unsigned char)peek())) d += pat_[pos_++]; addCp(d); } // \c65 — decimal only
            else { std::string d; while (!eof() && ascii::isalnum((unsigned char)peek())) d += pat_[pos_++]; addCp(d); }
            if (seq->kids.empty()) { seq->k = K::Nop; return seq; }
            if (seq->kids.size() == 1) return std::move(seq->kids[0]);
            return seq;
        }
        n->k = K::Lit; n->icase = curIcase_; n->imark = curImark_;
        switch (e) {
            case 'n': n->lit = "\n"; break;
            case 't': n->lit = "\t"; break;
            case 'r': n->lit = "\r"; break;
            case 'e': n->lit = "\x1b"; break;
            case 'f': n->lit = "\f"; break;
            case '0': n->lit = std::string(1, '\0'); break;
            default: n->lit = std::string(1, e); break;
        }
        return n;
    }
    // plain literal char
    auto n = std::make_unique<Node>();
    n->k = K::Lit; n->icase = curIcase_; n->imark = curImark_;
    n->lit = std::string(1, c);
    pos_++;
    return n;
}

// member: parse "<[ ... ]>" inner content (after the '[') into ranges/flags
void Regex::parseClassBodyMember(Node* node) {
    while (!eof() && peek() != ']') {
        if (ascii::isspace((unsigned char)peek())) { pos_++; continue; }
        if (peek() == '\\') {
            pos_++; char e = peek(); pos_++;
            if (e == 'd' || e == 'w' || e == 's') node->classFlags += e;
            else if (e == 'n') node->ranges.push_back({'\n', '\n'});
            else if (e == 't') node->ranges.push_back({'\t', '\t'});
            else if (e == 'r') node->ranges.push_back({'\r', '\r'});
            else if (e == 'x' || e == 'X' || e == 'o' || e == 'O' || e == 'c' || e == 'C') {
                // codepoint escapes in a class: \x[HH]/\xHH, \o[OO], \c[NAME,…]; uppercase
                // (\X/\O/\C) negate the whole class. Codepoints go to cpRanges (any size).
                bool neg = (e == 'X' || e == 'O' || e == 'C');
                char le = (char)ascii::tolower((unsigned char)e);
                std::vector<std::string> toks;
                if (peek() == '[') {
                    pos_++; std::string body; while (!eof() && peek() != ']') body += pat_[pos_++]; if (peek() == ']') pos_++;
                    for (size_t s = 0; s <= body.size(); ) { size_t cm = body.find(',', s); toks.push_back(body.substr(s, cm == std::string::npos ? std::string::npos : cm - s)); if (cm == std::string::npos) break; s = cm + 1; }
                } else if (le != 'c') {
                    std::string d; while (!eof() && ascii::isalnum((unsigned char)peek())) d += pat_[pos_++]; toks.push_back(d);
                } else if (ascii::isdigit((unsigned char)peek())) { // \c32 — bare decimal codepoint
                    std::string d; while (!eof() && ascii::isdigit((unsigned char)peek())) d += pat_[pos_++]; toks.push_back(d);
                }
                // an escaped RANGE endpoint: `\x21..\xFF` (Cro::HTTP header
                // field-content) / `\c32..\c126` (JSON::Tiny) / `\x21..z` —
                // consume `..` and the second endpoint
                int32_t loCp = -1; // single token: candidate low endpoint of a range
                if (toks.size() == 1 && !toks[0].empty()) {
                    size_t la = toks[0].find_first_not_of(" \t"), lb = toks[0].find_last_not_of(" \t");
                    std::string lt = la == std::string::npos ? "" : toks[0].substr(la, lb - la + 1);
                    if (le != 'c') loCp = (int32_t)std::strtol(lt.c_str(), nullptr, le == 'x' ? 16 : 8);
                    else if (!lt.empty() && ascii::isdigit((unsigned char)lt[0])) loCp = (int32_t)std::strtol(lt.c_str(), nullptr, 10);
                    else if (!lt.empty()) loCp = namedCp(lt); // `\c[LATIN…A]..\c[LATIN…Z]` — named endpoints range too
                }
                if (loCp >= 0) { // …the same for an escaped endpoint (`\x41 .. \x5A`)
                    size_t afterEsc = pos_;
                    while (ascii::isspace((unsigned char)peek())) pos_++;
                    if (!(peek() == '.' && peek(1) == '.')) pos_ = afterEsc;
                }
                if (loCp >= 0 && peek() == '.' && peek(1) == '.') {
                    uint32_t lo = (uint32_t)loCp;
                    pos_ += 2;
                    while (ascii::isspace((unsigned char)peek())) pos_++;
                    int32_t hi = -1;
                    if (peek() == '\\') {
                        pos_++; char e2 = (char)ascii::tolower((unsigned char)peek()); pos_++;
                        if (e2 == 'x' || e2 == 'o' || e2 == 'c') {
                            std::string d;
                            if (peek() == '[') { pos_++; while (!eof() && peek() != ']') d += pat_[pos_++]; if (peek() == ']') pos_++; }
                            else while (!eof() && (e2 == 'c' ? ascii::isdigit((unsigned char)peek()) : ascii::isalnum((unsigned char)peek()))) d += pat_[pos_++];
                            hi = e2 == 'c' ? namedCp(d) : (int32_t)std::strtol(d.c_str(), nullptr, e2 == 'x' ? 16 : 8);
                        }
                    } else if (!eof()) hi = (int32_t)(unsigned char)pat_[pos_++];
                    if (hi >= 0) {
                        if (lo < 0x80 && hi < 0x80) node->ranges.push_back({(unsigned char)lo, (unsigned char)hi});
                        else node->cpRanges.push_back({lo, (uint32_t)hi});
                        if (neg) node->negate = !node->negate;
                        continue;
                    }
                }
                // Collect the escape's codepoints, then split into GRAPHEMES: a single
                // `\c[A, COMBINING…]` names one multi-codepoint grapheme (one class member),
                // whereas `\c[FF, LF]` names two separate graphemes (two members).
                std::vector<uint32_t> cps;
                for (auto& tk : toks) {
                    size_t a = tk.find_first_not_of(" \t"), b = tk.find_last_not_of(" \t");
                    if (a == std::string::npos) continue;
                    std::string t = tk.substr(a, b - a + 1);
                    int32_t cp = le == 'x' ? (int32_t)std::strtol(t.c_str(), nullptr, 16)
                               : le == 'o' ? (int32_t)std::strtol(t.c_str(), nullptr, 8) : namedCp(t);
                    if (cp >= 0) cps.push_back((uint32_t)cp);
                }
                auto enc1 = [](uint32_t cp) -> std::string {
                    std::string o;
                    if (cp < 0x80) o += (char)cp;
                    else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
                    else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
                    else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
                    return o;
                };
                auto starts = cps.empty() ? std::vector<size_t>{} : uniGraphemeStarts(cps);
                for (size_t gi = 0; gi < starts.size(); gi++) {
                    size_t gb = starts[gi], ge = (gi + 1 < starts.size()) ? starts[gi + 1] : cps.size();
                    if (ge - gb == 1) node->cpRanges.push_back({cps[gb], cps[gb]}); // single-cp grapheme
                    else { std::string mem; for (size_t j = gb; j < ge; j++) mem += enc1(cps[j]); node->clusterMembers.push_back(mem); }
                }
                if (neg) node->negate = !node->negate;
            }
            else node->ranges.push_back({(unsigned char)e, (unsigned char)e}); // \: \# \- etc → literal
            continue;
        }
        // A literal member may be a multibyte UTF-8 codepoint (e.g. <[é]>);
        // read the whole codepoint so it isn't stored as two stray lead/cont bytes.
        auto readCp = [&]() -> uint32_t {
            unsigned char c0 = (unsigned char)pat_[pos_++];
            if (c0 < 0x80) return c0;
            int clen = (c0 >> 5) == 0x6 ? 2 : (c0 >> 4) == 0xe ? 3 : (c0 >> 3) == 0x1e ? 4 : 1;
            uint32_t cp = (uint32_t)(c0 & (0xFF >> (clen + 1)));
            for (int i = 1; i < clen && !eof(); i++) cp = (cp << 6) | ((unsigned char)pat_[pos_++] & 0x3F);
            return cp;
        };
        uint32_t lo = readCp();
        // Whitespace inside a character class is insignificant, RANGES included:
        // `<[ a .. z ]>` is `<[a..z]>`. Looking for the `..` without skipping it
        // made the spaced-out spelling three literal members (a, ., z).
        size_t afterLo = pos_;
        while (ascii::isspace((unsigned char)peek())) pos_++;
        if (!(peek() == '.' && peek(1) == '.')) pos_ = afterLo;
        if (peek() == '.' && peek(1) == '.') {
            pos_ += 2;
            while (ascii::isspace((unsigned char)peek())) pos_++;
            uint32_t hi = readCp();
            if (lo < 0x80 && hi < 0x80) node->ranges.push_back({(unsigned char)lo, (unsigned char)hi});
            else node->cpRanges.push_back({lo, hi}); // any endpoint ≥ 0x80 → codepoint range
        } else if (lo < 0x80) {
            node->ranges.push_back({(unsigned char)lo, (unsigned char)lo});
        } else {
            node->cpRanges.push_back({lo, lo}); // non-ASCII literal → codepoint, not raw bytes
        }
    }
    if (peek() == ']') pos_++;
}

// POSIX-ish built-in regex rules (<alpha>, <digit>, <ident>, <ws>, …) as char
// matchers. Returns the new position after matching, -1 if it IS a built-in but
// doesn't match here, or -2 if `nm` is not a built-in rule at all.
static long builtinRuleMatch(const std::string& nm, const std::string& s, long pos, long len) {
    if (nm == "ws") { // <!ww> \s* — zero-width only OFF a word-word boundary
        long p = pos;
        while (p < len && ascii::isspace((unsigned char)s[p])) p++;
        auto wordAt = [&](long i) { return i >= 0 && i < len && (ascii::isalnum((unsigned char)s[i]) || s[i] == '_'); };
        if (p == pos && wordAt(pos - 1) && wordAt(pos)) return -1; // between two word chars: needs real space
        return p;
    }
    // One codepoint of `s` at `p`, and where it ends. ASCII stays one byte.
    auto cpAt = [&](long p, long* end) -> uint32_t {
        unsigned char c0 = (unsigned char)s[p];
        if (c0 < 0x80) { *end = p + 1; return c0; }
        if (c0 < 0xC0) { *end = p + 1; return c0; } // stray continuation byte: never a class member
        int clen = (c0 >> 5) == 0x6 ? 2 : (c0 >> 4) == 0xe ? 3 : (c0 >> 3) == 0x1e ? 4 : 1;
        uint32_t cp = (uint32_t)(c0 & (0xFF >> (clen + 1)));
        for (int i = 1; i < clen && p + i < len; i++) cp = (cp << 6) | ((unsigned char)s[p + i] & 0x3F);
        *end = (long)uniClusterEndUtf8(s, p, len); // NFG: consume the whole grapheme
        return cp;
    };
    // `<ident>`: one alpha (which includes '_') then alnum — Unicode throughout,
    // so `aö1_` is a single identifier the way Rakudo reads it.
    if (nm == "ident") {
        if (pos >= len) return -1;
        long e = pos;
        if (!charClassMatch('a', cpAt(pos, &e))) return -1;
        long p = e;
        while (p < len) {
            long e2 = p;
            uint32_t cp = cpAt(p, &e2);
            if (!(charClassMatch('a', cp) || charClassMatch('d', cp))) break;
            p = e2;
        }
        return p;
    }
    // The two zero-width word assertions. Without them `<wb>`/`<ww>` fell through
    // to "unknown rule" and matched the empty string EVERYWHERE, which is silently
    // wrong rather than loudly missing.
    if (nm == "wb" || nm == "ww") {
        auto word = [&](long i) { // i starts a codepoint
            if (i < 0 || i >= len) return false;
            long e = i;
            return charClassMatch('w', cpAt(i, &e));
        };
        long prev = pos - 1; // step back over continuation bytes to the lead byte
        while (prev > 0 && ((unsigned char)s[prev] & 0xC0) == 0x80) prev--;
        bool before = pos > 0 && word(prev), after = word(pos);
        if (nm == "ww") return (before && after) ? pos : -1;
        return (before != after) ? pos : -1;
    }
    std::string flags = ruleFlag(nm);
    if (flags.empty() || nm == "word" || nm == "ws") return -2;
    if (pos >= len) return -1;
    long end = pos;
    uint32_t cp = cpAt(pos, &end);
    for (char f : flags) if (charClassMatch(f, cp)) return end; // `alnum` is alpha ∪ digit
    return -1;
}

// Built-in rule name → classMatch flag(s), for `<+alpha>` charset composition ("" = none).
static std::string ruleFlag(const std::string& nm) {
    if (nm == "alpha") return "a"; if (nm == "digit") return "d";
    if (nm == "space") return "s"; if (nm == "blank") return "b";
    if (nm == "alnum") return "ad"; if (nm == "upper") return "u";
    if (nm == "lower") return "l"; if (nm == "xdigit") return "x";
    if (nm == "word") return "w"; if (nm == "punct") return "p";
    if (nm == "cntrl") return "k"; if (nm == "graph") return "g";
    if (nm == "print") return "r";
    return "";
}

// Word char for the `<<`/`>>` boundary anchors — matches \w (ASCII alnum + _),
// plus any multibyte lead/continuation byte so boundaries land around Unicode words.
static bool isWordChar(const std::string& s, long i) {
    if (i < 0 || i >= (long)s.size()) return false;
    unsigned char c = (unsigned char)s[i];
    return ascii::isalnum(c) || c == '_' || c >= 0x80;
}

// Unicode whitespace codepoints beyond ASCII (for \s / \S on multibyte input).
static bool isUnicodeSpace(uint32_t cp) {
    switch (cp) {
        case 0x85: case 0xA0: case 0x1680:
        case 0x2000: case 0x2001: case 0x2002: case 0x2003: case 0x2004:
        case 0x2005: case 0x2006: case 0x2007: case 0x2008: case 0x2009: case 0x200A:
        case 0x2028: case 0x2029: case 0x202F: case 0x205F: case 0x3000:
            return true;
        default: return cp <= 0x20 && (cp == 0x20 || (cp >= 0x09 && cp <= 0x0D));
    }
}

// The POSIX-NAMED character classes — `<alpha>`, `<+digit>`, `\d` — ONE definition
// over a CODEPOINT, shared by the three places that used to spell them out
// separately: the rule form (`<alpha>`), the ASCII byteset a class node compiles
// to, and the multibyte arm of a class match. They are Unicode, and they are NOT
// <ctype.h>'s: Rakudo's `<alpha>` is :L plus '_', `<punct>` is :P alone (so '+',
// '$' and '|' are OUT — they are symbols), `<graph>` is alnum ∪ punct, and
// `<print>` is everything that is not :Cc. Derived character by character from
// Rakudo; t/regression/builtin-char-class-rules.raku holds the table.
//
// They were byte tests, so any grammar reading non-ASCII text stopped at the
// first accented letter: URI's RFC-3986 grammar routes every path character
// through `<alpha>`, and `URI.new("http://test.de/ö")` threw "Could not parse".
// The flag letters are ruleFlag()'s, below.
static bool charClassCp(char flag, uint32_t cp) {
    switch (flag) {
        case 'a': return cp == '_' || uniMatchesProp(cp, "L");
        case 'd': return uniMatchesProp(cp, "Nd");
        // \w is <alnum>, which is <alpha> ∪ <digit> — :Nd only. `²` (No) and `Ⅰ`
        // (Nl) are NOT word characters, though both are :N.
        case 'w': return cp == '_' || uniMatchesProp(cp, "L") || uniMatchesProp(cp, "Nd");
        case 's': return isUnicodeSpace(cp);
        case 'u': return uniMatchesProp(cp, "Lu");
        case 'l': return uniMatchesProp(cp, "Ll");
        case 'p': return uniMatchesProp(cp, "P");
        case 'k': return uniMatchesProp(cp, "Cc");
        case 'b': return cp == '\t' || uniMatchesProp(cp, "Zs");
        case 'x': return cp < 0x80 && ascii::isxdigit((unsigned char)cp);
        case 'g': return charClassCp('w', cp) || uniMatchesProp(cp, "P"); // alnum ∪ punct
        case 'r': return !uniMatchesProp(cp, "Cc");
    }
    return false;
}

// Same predicate, with ASCII answered from a table built once — uniMatchesProp
// walks a name chain and a category lookup, and this runs per input position in
// a grammar's inner loop.
static const char CC_FLAGS[] = "adwsulpkbxgr";
bool charClassMatch(char flag, uint32_t cp) {
    if (cp < 128) {
        static const struct AsciiCC {
            uint16_t bits[128] = {};
            signed char slot[256];
            AsciiCC() {
                for (int i = 0; i < 256; i++) slot[i] = -1;
                for (int i = 0; CC_FLAGS[i]; i++) slot[(unsigned char)CC_FLAGS[i]] = (signed char)i;
                for (uint32_t c = 0; c < 128; c++)
                    for (int i = 0; CC_FLAGS[i]; i++)
                        if (charClassCp(CC_FLAGS[i], c)) bits[c] |= (uint16_t)(1u << i);
            }
        } T;
        signed char b = T.slot[(unsigned char)flag];
        return b >= 0 && ((T.bits[cp] >> b) & 1);
    }
    return charClassCp(flag, cp);
}

bool Regex::classMatch(const Node* n, char ch) const {
    // The per-byte result (ranges + flags + icase + negate) is pure per node —
    // build a 256-bit table on first use, then every test is one bit probe.
    if (!n->bytesetReady.load(std::memory_order_acquire)) {
        // BYTES, not codepoints: 0x80–0xFF here are UTF-8 lead/continuation bytes,
        // never characters — a multibyte codepoint is decoded and tested whole in
        // the K::Class arm below, so the table must leave those bits clear or a
        // class would match half a character.
        auto flagHit = [](char f, unsigned char c) -> bool {
            return c < 0x80 && charClassMatch(f, c);
        };
        auto test = [&](unsigned char c) -> bool {
            bool pos = false;
            for (auto& r : n->ranges) if (c >= r.first && c <= r.second) { pos = true; break; }
            // ASCII-range codepoint entries (\c[LF], \x0A, …) participate too
            if (!pos) for (auto& r : n->cpRanges) if (c >= r.first && c <= r.second) { pos = true; break; }
            if (!pos) for (char f : n->classFlags) if (flagHit(f, c)) { pos = true; break; }
            return pos;
        };
        auto subtracted = [&](unsigned char c) -> bool {
            for (char f : n->negClassFlags) if (flagHit(f, c)) return true;
            return false;
        };
        for (int i = 0; i < 8; i++) n->byteset[i] = 0;
        for (int v = 0; v < 256; v++) {
            unsigned char c = (unsigned char)v;
            bool in = test(c);
            if (!in && n->icase) in = test((unsigned char)ascii::tolower(c)) || test((unsigned char)ascii::toupper(c));
            // The class is (base, negated if `<-…>`) MINUS every `-member`: the
            // subtraction applies to the FINAL set, not to the base. Subtracting
            // first made `<-[\"]-space>` match a space — it is not in the base, so
            // the negation let it back in.
            if (n->negate) in = !in;
            if (in && subtracted(c)) in = false;
            if (in) n->byteset[v >> 5] |= (1u << (v & 31));
        }
        // release-publish AFTER the words are written: an acquire reader that
        // sees true also sees the finished table. Two threads may build it
        // concurrently — the words are idempotent, |= of the same bits.
        n->bytesetReady.store(true, std::memory_order_release);
    }
    unsigned char c = (unsigned char)ch;
    return (n->byteset[c >> 5] >> (c & 31)) & 1;
}


bool Regex::rootIsSingleChar() const {
    if (!ok_ || !root_) return false;
    const Node* n = root_.get();
    if (n->k == K::Class) {
        // a codepoint entry past 0xFF can match a MULTI-BYTE char — the one-byte
        // fast path can't represent that (nor can a negated class, which must
        // accept arbitrary non-members)
        for (auto& r : n->cpRanges) if (r.second > 0xFF) return false;
        if (n->negate && !n->cpRanges.empty()) return false;
        return n->uprop.empty();
    }
    if (n->k == K::Lit) return !n->icase && !n->imark && n->lit.size() == 1;
    return n->k == K::Any;
}

long Regex::trySingleChar(const std::string& s, long pos) const {
    if (pos >= (long)s.size()) return -1;
    // MULTIBYTE input: this path answers from a one-BYTE table, and a Unicode
    // class member (`<+alnum>`) or a negated class spans the whole character —
    // -2 tells the caller to run the real matcher instead. Deciding it here from
    // the byte alone matched one third of an `ö` and left the grammar mid-character.
    if ((unsigned char)s[pos] >= 0x80) return -2;
    const Node* n = root_.get();
    if (n->k == K::Any) return s[pos] == '\n' ? -1 : pos + 1;
    if (n->k == K::Lit) return s[pos] == n->lit[0] ? pos + 1 : -1;
    return classMatch(n, s[pos]) ? pos + 1 : -1; // Class
}

std::pair<long, long> Regex::nodeWidth(const Node* n, MState& st) const {
    const long UNB = -1, CAP = 1000000000;
    switch (n->k) {
        case K::Lit: return {(long)n->lit.size(), n->imark ? UNB : (long)n->lit.size()}; // :ignoremark may consume trailing marks
        case K::Any: return {1, 4}; // `.` consumes a whole grapheme — up to 4 bytes per codepoint
        case K::Class:
            // anything that can match beyond ASCII decodes whole codepoints (up to
            // 4 bytes): uprop, cp ranges, cluster members, negation, and the
            // codepoint-capable flags. The width is only a lookbehind scan bound,
            // so widening is safe; understating it made `<?after \w>` fail after
            // a multibyte char (the scan probed only pos-1, a continuation byte).
            if (!n->uprop.empty() || !n->cpRanges.empty() || !n->clusterMembers.empty() ||
                n->negate || !n->negClassFlags.empty() ||
                n->classFlags.find_first_of("swadulp") != std::string::npos) return {1, 4};
            return {1, 1};
        case K::Seq: {
            long lo = 0, hi = 0;
            for (auto& kd : n->kids) {
                auto w = nodeWidth(kd.get(), st);
                lo += w.first;
                if (hi >= 0) hi = (w.second < 0 || hi + w.second > CAP) ? UNB : hi + w.second;
            }
            return {lo, hi};
        }
        case K::Alt: {
            if (n->kids.empty()) return {0, 0};
            long lo = -1, hi = 0;
            for (auto& kd : n->kids) {
                auto w = nodeWidth(kd.get(), st);
                lo = lo < 0 ? w.first : std::min(lo, w.first);
                if (hi >= 0) hi = w.second < 0 ? UNB : std::max(hi, w.second);
            }
            return {lo, hi};
        }
        case K::Rep: {
            auto w = nodeWidth(n->kids[0].get(), st);
            long mn = n->min > 0 ? n->min : 0;
            long lo = w.first * mn;
            long hi = (n->repCode.empty() && n->max >= 0 && w.second >= 0 && w.second * n->max <= CAP)
                    ? w.second * n->max : UNB;
            if (n->sep) {
                auto ws = nodeWidth(n->sep.get(), st);
                if (mn > 1) lo += ws.first * (mn - 1);
                if (hi >= 0) hi = (n->max > 1 && (ws.second < 0 || hi + ws.second * (n->max - 1) > CAP)) ? UNB
                                : (n->max > 1 ? hi + ws.second * (n->max - 1) : hi);
            }
            return {lo, hi};
        }
        case K::Conj: // the match is as long as the last term (all share the start)
            return n->kids.empty() ? std::make_pair(0L, 0L) : nodeWidth(n->kids.back().get(), st);
        case K::CondRef: { // either branch may run (a missing `no` branch is zero-width)
            auto wy = nodeWidth(n->kids[0].get(), st);
            auto wn = n->kids.size() > 1 ? nodeWidth(n->kids[1].get(), st) : std::make_pair(0L, 0L);
            return {std::min(wy.first, wn.first),
                    (wy.second < 0 || wn.second < 0) ? UNB : std::max(wy.second, wn.second)};
        }
        case K::Group: return nodeWidth(n->kids[0].get(), st);
        case K::AnchorStart: case K::AnchorEnd: case K::WBLeft: case K::WBRight:
        case K::Nop: case K::Code: case K::Look: case K::CapStart: case K::CapEnd:
            return {0, 0};
        case K::Subrule:
            if (st.grammar) {
                if (!n->metaCache) n->metaCache = &st.grammar->nameMeta(n->ruleName);
                // one CHARACTER, whose UTF-8 encoding is 1..n bytes — this is only
                // used to bound a lookbehind window, so an open upper bound is a
                // wider scan, never a wrong answer
                if (n->metaCache->singleChar || !n->metaCache->builtinClass.empty()) return {1, UNB};
            }
            return {0, UNB};
        case K::VarMatch: return {0, UNB};
    }
    return {0, UNB};
}

static const GrammarHooks::ParamMap kNoParams; // shared empty map for hook calls

bool Regex::matchNode(const Node* n, MState& st, long pos, const FnRef& k) const {
    // Step budget: bounds catastrophic backtracking and unbounded CPS recursion.
    // ~8M steps is far beyond any real match yet trips in well under a second on
    // patterns like /[a*]* b/ against a long non-matching string.
    if (++st.steps > 8000000) throw StepLimitExceeded{};
    long len = (long)st.s.size();
    switch (n->k) {
        case K::Nop: return k(pos);
        case K::CapStart: { st.capFrom = pos; return k(pos); } // `<(`: zero-width, marks the .Str start
        case K::CapEnd:   { st.capTo   = pos; return k(pos); } // `)>`: zero-width, marks the .Str end
        case K::Look: {
            // zero-width assertion — match the inner in an isolated capture state
            const Node* child = n->kids.empty() ? nullptr : n->kids[0].get();
            if (!child) return k(pos);
            bool m = false;
            if (!n->behind) {
                MState sub{st.s, std::vector<std::pair<long, long>>(ncaps_, {-1, -1}), {}, {}, st.resolver, st.grammar};
                sub.hooks = st.hooks; sub.startPos = pos; // propagate hooks so embedded {code}/$var work
                m = matchNode(child, sub, pos, [](long) { return true; });
            } else {
                // A lookbehind inner can only match ending at `pos` if it starts within
                // its own width of it — bound the scan window (O(width), not O(pos)).
                if (!n->lookWidthReady) {
                    auto w = nodeWidth(child, st);
                    n->lookMin = w.first; n->lookMax = w.second; n->lookWidthReady = true;
                }
                long hi = pos - n->lookMin;
                long lo = n->lookMax < 0 ? 0 : pos - n->lookMax;
                if (lo < 0) lo = 0;
                for (long j = hi; j >= lo && !m; j--) {
                    MState sub{st.s, std::vector<std::pair<long, long>>(ncaps_, {-1, -1}), {}, {}, st.resolver, st.grammar};
                    sub.hooks = st.hooks; sub.startPos = j;
                    if (matchNode(child, sub, j, [&](long e) { return e == pos; })) m = true;
                }
            }
            return (m != n->negate) ? k(pos) : false; // zero-width; negate flips
        }
        case K::Subrule: {
            // Grammar path: backtrackable — thread `k` through the callee. The name→meta
            // resolution is cached on the node (compiled Regexes live in the matcher's cache,
            // so node and matcher share a lifetime).
            if (st.grammar) {
                if (!n->metaCache) n->metaCache = &st.grammar->nameMeta(n->ruleName);
                return st.grammar->matchSubMeta(*n->metaCache, n->ruleName, n->ruleArgs,
                                            n->ruleCapture ? (n->ruleAlias.empty() ? n->ruleName : n->ruleAlias) : std::string(),
                                            st, pos, k,
                                            /*alsoBareName=*/n->ruleCapture && !n->ruleAlias.empty() && !n->aliasDotted);
            }
            // <at(N)> — zero-width position assertion: current offset must equal N.
            if (n->ruleName == "at") {
                long target = std::strtol(n->ruleArgs.c_str(), nullptr, 10);
                return (pos == target) ? k(pos) : false;
            }
            // built-in char-class rules (<.alpha>, <ident>, <ws>, …) resolve here and
            // take precedence over an interpreter resolver that doesn't define them —
            // UNLESS a lexical `my regex NAME {…}` shadows the built-in.
            if (!(st.lexNames && st.lexNames->count(n->ruleName))) {
                long e = builtinRuleMatch(n->ruleName, st.s, pos, len);
                if (e >= 0) {
                    if (n->ruleCapture && !n->ruleName.empty()) { // record $<name> for a capturing built-in
                        const std::string& cn = n->ruleName;
                        auto saved = st.named.count(cn) ? st.named[cn] : std::pair<long, long>{-1, -1};
                        bool had = st.named.count(cn);
                        st.named[cn] = {pos, e};
                        ParseNode leaf; leaf.name = cn; leaf.from = pos; leaf.to = e;
                        st.children[cn].push_back(std::move(leaf)); // <alpha>+ collates into a list
                        if (k(e)) return true;
                        st.children[cn].pop_back(); if (st.children[cn].empty()) st.children.erase(cn);
                        if (had) st.named[cn] = saved; else st.named.erase(cn);
                        return false;
                    }
                    return k(e);
                }
                if (e == -1) return false;
            }
            if (!st.resolver) return k(pos); // unknown subrule, no resolver: lenient zero-width
            RxMatch sub;
            // pass a parameterised call's args to the resolver, encoded after \x1f
            std::string call = n->ruleArgs.empty() ? n->ruleName : (n->ruleName + "\x1f" + n->ruleArgs);
            if (!(*st.resolver)(call, st.s, pos, sub)) return false;
            if (n->ruleCapture && !n->ruleName.empty()) {
                const std::string& cn = n->ruleName;
                auto savedN = st.named.count(cn) ? st.named[cn] : std::pair<long,long>{-1,-1};
                bool had = st.named.count(cn);
                st.named[cn] = {sub.from, sub.to};
                ParseNode leaf; leaf.name = cn; leaf.from = sub.from; leaf.to = sub.to;
                for (auto& kv : sub.named) leaf.named[kv.first] = kv.second;
                leaf.caps = sub.caps;
                // …and the sub-match TREE. A `my regex` whose body captures through
                // subrules records children, not just named spans; dropping them left
                // `$<ps>.hash` empty, so URI::Path could not find which of
                // path-absolute/path-rootless/path-empty had matched and every
                // mutated path came back as the empty string.
                if (!sub.children.empty())
                    leaf.kids = std::make_shared<const ChildMap>(sub.children);
                if (sub.listNames) leaf.listNames = sub.listNames;
                if (!sub.listCaps.empty())
                    leaf.listCaps = std::make_shared<const std::set<int>>(sub.listCaps);
                st.children[cn].push_back(std::move(leaf)); // collates repeated <cn> into a list
                if (k(sub.to)) return true;
                st.children[cn].pop_back(); if (st.children[cn].empty()) st.children.erase(cn);
                if (had) st.named[cn] = savedN; else st.named.erase(cn);
                return false;
            }
            return k(sub.to);
        }
        case K::Lit: {
            // :ignoremark — each literal codepoint matches an input grapheme whose BASE
            // codepoint is the same (combining marks and NFC composition ignored: `a`
            // matches both "a\x[308]" and precomposed "ä"), consuming the input grapheme
            // whole. A literal holding byte FRAGMENTS of a codepoint keeps exact matching.
            if (n->imark && !n->lit.empty()) {
                auto declen = [](unsigned char c) -> int {
                    return c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xe ? 3 : (c >> 3) == 0x1e ? 4 : 1;
                };
                bool wholeCps = (unsigned char)n->lit[0] < 0x80 || (unsigned char)n->lit[0] >= 0xC0;
                for (size_t i = 0; wholeCps && i < n->lit.size();) {
                    unsigned char c = (unsigned char)n->lit[i];
                    if (c >= 0x80 && c < 0xC0) { wholeCps = false; break; }
                    size_t cl = (size_t)declen(c);
                    if (i + cl > n->lit.size()) { wholeCps = false; break; }
                    i += cl;
                }
                if (wholeCps) {
                    auto decode = [&](const std::string& s, long p, int& cl) -> uint32_t {
                        unsigned char c0 = (unsigned char)s[p];
                        cl = declen(c0);
                        uint32_t cp = c0 < 0x80 ? c0 : (uint32_t)(c0 & (0xFF >> (cl + 1)));
                        for (int i = 1; i < cl && p + i < (long)s.size(); i++) cp = (cp << 6) | ((unsigned char)s[p + i] & 0x3F);
                        return cp;
                    };
                    auto baseCp = [](uint32_t cp) -> uint32_t {
                        if (cp < 0x80) return cp;
                        for (uint32_t c : uniNormalize({cp}, 0)) // NFD, first starter = the base
                            if (uniCombiningClass(c) == 0) return c;
                        return cp;
                    };
                    long ip = pos;
                    for (size_t i = 0; i < n->lit.size();) {
                        if (ip >= len) return false;
                        unsigned char c0 = (unsigned char)st.s[ip];
                        if (c0 >= 0x80 && c0 < 0xC0) return false; // mid-codepoint
                        int lcl = 0, scl = 0;
                        uint32_t lb = baseCp(decode(n->lit, (long)i, lcl));
                        uint32_t sb = baseCp(decode(st.s, ip, scl));
                        if (lb != sb && !(n->icase && lb < 0x80 && sb < 0x80 &&
                                          ascii::tolower((int)lb) == ascii::tolower((int)sb))) return false;
                        i += (size_t)lcl;
                        ip = (long)uniClusterEndUtf8(st.s, ip, len);
                    }
                    // literal-prefix bookkeeping (LTM tie-break), same as the other Lit exits
                    if (st.litPrefix < 0) st.litPrefix = st.startPos;
                    if (st.litPrefix == pos) st.litPrefix = ip;
                    return k(ip);
                }
            }
            // :i with non-ASCII on either side takes the FOLD-AWARE path: both
            // streams expand through full case folding, so a one-to-many fold
            // (ß → ss) matches across codepoint boundaries. A fold that would
            // end mid-expansion fails (a match cannot split ß in half).
            if (n->icase) {
                bool needFold = false;
                for (unsigned char c : n->lit) if (c >= 0x80) { needFold = true; break; }
                if (!needFold)
                    for (long j = pos; j < len && j < pos + (long)n->lit.size() + 4; j++)
                        if ((unsigned char)st.s[j] >= 0x80) { needFold = true; break; }
                if (needFold) {
                    auto dec = [](const std::string& str, long p, int& cl) -> uint32_t {
                        unsigned char c0 = (unsigned char)str[p];
                        if (c0 < 0x80) { cl = 1; return c0; }
                        cl = (c0 >> 5) == 0x6 ? 2 : (c0 >> 4) == 0xE ? 3 : 4;
                        uint32_t cp = c0 & (0xFF >> (cl + 1));
                        for (int i2 = 1; i2 < cl && p + i2 < (long)str.size(); i2++) cp = (cp << 6) | ((unsigned char)str[p + i2] & 0x3F);
                        return cp;
                    };
                    std::vector<uint32_t> la, sa;
                    size_t li = 0, si = 0, i = 0;
                    long ip = pos;
                    for (;;) {
                        if (li == la.size()) {
                            if (i >= n->lit.size()) break;
                            la.clear(); li = 0; int cl;
                            foldCpPush(dec(n->lit, (long)i, cl), la);
                            i += (size_t)cl;
                        }
                        if (si == sa.size()) {
                            if (ip >= len) return false;
                            unsigned char c0 = (unsigned char)st.s[ip];
                            if (c0 >= 0x80 && c0 < 0xC0) return false; // mid-codepoint
                            sa.clear(); si = 0; int cl;
                            foldCpPush(dec(st.s, ip, cl), sa);
                            ip += cl;
                        }
                        if (la[li] != sa[si]) return false;
                        ++li; ++si;
                    }
                    if (si != sa.size()) return false; // literal ended inside a fold
                    if (st.litPrefix < 0) st.litPrefix = st.startPos;
                    if (st.litPrefix == pos) st.litPrefix = ip;
                    return k(ip);
                }
            }
            long m = (long)n->lit.size();
            if (pos + m > len) return false;
            for (long j = 0; j < m; j++) {
                char a = st.s[pos + j], b = n->lit[j];
                if (a != b && !(n->icase && ascii::tolower((unsigned char)a) == ascii::tolower((unsigned char)b))) return false;
            }
            // extend the leading literal run (LTM specificity) while still contiguous from startPos
            if (st.litPrefix < 0) st.litPrefix = st.startPos;
            if (st.litPrefix == pos) st.litPrefix = pos + m;
            return k(pos + m);
        }
        case K::Any: {
            if (pos >= len) return false;
            unsigned char c0 = (unsigned char)st.s[pos]; // `.` matches one whole GRAPHEME (NFG), not one codepoint
            if (c0 >= 0x80 && c0 < 0xC0) return false;   // mid-codepoint continuation byte
            return k((long)uniClusterEndUtf8(st.s, pos, len));
        }
        case K::Class:
            if (pos >= len) return false;
            if (!n->cpRanges.empty() || !n->clusterMembers.empty()) { // codepoint / grapheme char class
                unsigned char c0 = (unsigned char)st.s[pos];
                if (c0 >= 0x80 && c0 < 0xC0) return false;
                int clen = c0 < 0x80 ? 1 : (c0 >> 5) == 0x6 ? 2 : (c0 >> 4) == 0xe ? 3 : (c0 >> 3) == 0x1e ? 4 : 1;
                uint32_t cp = (c0 < 0x80 || clen == 1) ? c0 : (uint32_t)(c0 & (0xFF >> (clen + 1)));
                for (int i = 1; i < clen && pos + i < (long)len; i++) cp = (cp << 6) | ((unsigned char)st.s[pos + i] & 0x3F);
                // NFG: a class member is a whole grapheme. A multi-codepoint input grapheme
                // matches only a multi-cp member with the exact same codepoints; a single-cp
                // grapheme matches the codepoint ranges.
                long gEnd = (long)uniClusterEndUtf8(st.s, pos, len);
                bool single = (gEnd == pos + clen);
                bool in = false;
                for (auto& mem : n->clusterMembers)                 // whole-grapheme members
                    if ((long)mem.size() == gEnd - pos && st.s.compare(pos, mem.size(), mem) == 0) { in = true; break; }
                if (!in && single) {
                    for (auto& r : n->cpRanges) if (cp >= r.first && cp <= r.second) { in = true; break; }
                    if (!in) for (auto& r : n->ranges) if (cp >= r.first && cp <= r.second) { in = true; break; } // mixed class
                }
                if (n->negate) in = !in;
                if (!in) return false;
                return k(gEnd);
            }
            if (!n->uprop.empty()) { // Unicode property class: decode one codepoint
                unsigned char c0 = (unsigned char)st.s[pos];
                if (c0 >= 0x80 && c0 < 0xC0) return false; // mid-codepoint continuation byte: no match here
                int clen = c0 < 0x80 ? 1 : (c0 >> 5) == 0x6 ? 2 : (c0 >> 4) == 0xe ? 3 : (c0 >> 3) == 0x1e ? 4 : 1;
                uint32_t cp = (c0 < 0x80 || clen == 1) ? c0 : (uint32_t)(c0 & (0xFF >> (clen + 1)));
                for (int i = 1; i < clen && pos + i < (long)len; i++) cp = (cp << 6) | ((unsigned char)st.s[pos + i] & 0x3F);
                bool m = uniMatchesProp(cp, n->uprop);
                if (n->negate) m = !m;
                if (!m) return false;
                return k((long)uniClusterEndUtf8(st.s, pos, len)); // NFG: consume the whole grapheme
            }
            // The byteset table covers bytes 0x00–0xFF only; a multibyte codepoint
            // must be tested and consumed whole, or a negated class like <-[x]>
            // matches a lone UTF-8 lead byte and splits the codepoint. Decode the
            // full codepoint and test class membership at codepoint granularity.
            {
                unsigned char c0 = (unsigned char)st.s[pos];
                if (c0 >= 0x80) {
                    if (c0 < 0xC0) return false; // bare continuation byte: not a codepoint start
                    int clen = (c0 >> 5) == 0x6 ? 2 : (c0 >> 4) == 0xe ? 3 : (c0 >> 3) == 0x1e ? 4 : 1;
                    uint32_t cp = (uint32_t)(c0 & (0xFF >> (clen + 1)));
                    for (int i = 1; i < clen && pos + i < (long)len; i++) cp = (cp << 6) | ((unsigned char)st.s[pos + i] & 0x3F);
                    auto flagHitCp = [](char f, uint32_t c) -> bool { return charClassMatch(f, c); };
                    bool in = false;
                    for (auto& r : n->ranges)   if (cp >= r.first && cp <= r.second) { in = true; break; }
                    if (!in) for (auto& r : n->cpRanges) if (cp >= r.first && cp <= r.second) { in = true; break; }
                    if (!in) for (char f : n->classFlags)    if (flagHitCp(f, cp)) { in = true; break; }
                    bool subtractedCp = false;
                    for (char f : n->negClassFlags) if (flagHitCp(f, cp)) { subtractedCp = true; break; }
                    // enumerated (range) members are whole-grapheme; property/flag members
                    // test the base and consume the cluster. A multi-codepoint grapheme can
                    // only satisfy a range member when negated.
                    long gEnd = (long)uniClusterEndUtf8(st.s, pos, len);
                    bool hasFlag = !n->classFlags.empty() || !n->negClassFlags.empty();
                    if (!hasFlag && gEnd != pos + clen) in = false; // multi-cp grapheme vs a pure range class
                    if (n->negate) in = !in;
                    if (subtractedCp) in = false;   // `-member` subtracts from the FINAL set
                    if (!in) return false;
                    return k(gEnd);
                }
            }
            if (!classMatch(n, st.s[pos])) return false;
            return k(pos + 1);
        case K::AnchorStart:
            // `^^` (multiline): start of any line. `^`: start of the string only.
            // p5Line — P5's (?m)^ — is `^^` minus the position after a FINAL newline.
            if (n->p5Line) return (pos == 0 || (pos < len && st.s[pos - 1] == '\n')) ? k(pos) : false;
            if (n->multiline ? (pos == 0 || st.s[pos - 1] == '\n') : (pos == 0)) return k(pos);
            return false;
        case K::AnchorEnd:
            // `$$` (multiline): end of any line. `$`: end of string (or just before a final
            // newline). P5 `\z` (absEnd): the absolute end, a trailing newline doesn't count.
            if (n->absEnd) return pos == len ? k(pos) : false;
            if (n->multiline ? (pos == len || st.s[pos] == '\n')
                             : (pos == len || (pos + 1 == len && st.s[pos] == '\n'))) return k(pos);
            return false;
        case K::WBLeft: {
            // `<<` / `«` — left word boundary: non-word (or start) on the left, word char on the right.
            bool prevW = pos > 0 && isWordChar(st.s, pos - 1);
            bool curW  = pos < (long)len && isWordChar(st.s, pos);
            if (!prevW && curW) return k(pos);
            return false;
        }
        case K::WBRight: {
            // `>>` / `»` — right word boundary: word char on the left, non-word (or end) on the right.
            bool prevW = pos > 0 && isWordChar(st.s, pos - 1);
            bool curW  = pos < (long)len && isWordChar(st.s, pos);
            if (prevW && !curW) return k(pos);
            return false;
        }
        case K::Seq: {
            auto go = [&](auto&& self, size_t i, long p) -> bool {
                if (i == n->kids.size()) return k(p);
                auto cont = [&, i](long np) { return self(self, i + 1, np); };
                return matchNode(n->kids[i].get(), st, p, cont);
            };
            return go(go, 0, pos);
        }
        case K::Conj: {
            // Every term must match at the SAME start `pos`. Earlier terms need only
            // succeed (any end); the LAST term carries the real continuation, so the
            // overall match ends where it ends. Backtracks across all terms.
            auto go = [&](auto&& self, size_t i) -> bool {
                if (i + 1 == n->kids.size())
                    return matchNode(n->kids[i].get(), st, pos, k);
                auto cont = [&, i](long) { return self(self, i + 1); };
                return matchNode(n->kids[i].get(), st, pos, cont);
            };
            return n->kids.empty() ? k(pos) : go(go, 0);
        }
        case K::Code: { // <?{…}>/<!{…}> assertion, or `:my …`/bare `{…}` side-effect (runOnly)
            const auto& params = st.grammar ? st.grammar->currentParams() : kNoParams;
            if (n->runOnly) { // execute for side effects, zero-width, always pass
                if (n->ltmStop && st.firstCode < 0) st.firstCode = pos; // a bare code block ends the LTM declarative prefix
                if (st.hooks && st.hooks->runCaps) st.hooks->runCaps(n->lit, st.startPos, pos, st.named, st.caps, params);
                else if (st.hooks && st.hooks->run) st.hooks->run(n->lit, st.startPos, pos, st.named, params);
                return k(pos);
            }
            bool ok = (st.hooks && st.hooks->assertPassCaps)
                          ? st.hooks->assertPassCaps(n->lit, st.startPos, pos, st.named, st.caps, params)
                    : (st.hooks && st.hooks->assertPass)
                          ? st.hooks->assertPass(n->lit, st.startPos, pos, st.named, params) : true;
            if (n->negate) ok = !ok;
            return ok ? k(pos) : false;
        }
        case K::CondRef: { // P5 (?(N)yes|no): which branch depends on group N having matched
            long ci = n->min - 1;
            bool set = ci >= 0 && ci < (long)st.caps.size() && st.caps[ci].first >= 0;
            const Node* br = set ? n->kids[0].get() : (n->kids.size() > 1 ? n->kids[1].get() : nullptr);
            return br ? matchNode(br, st, pos, k) : k(pos);
        }
        case K::VarMatch: { // `$var` in a pattern — match the variable's current Str value literally
            // `$<name>` named backreference: match the in-flight named capture's text
            // literally (no new capture). Used by XML close-tag matching.
            if (n->lit.size() > 3 && n->lit[1] == '<' && n->lit.back() == '>') {
                std::string nm = n->lit.substr(2, n->lit.size() - 3);
                auto it = st.named.find(nm);
                if (it == st.named.end() || it->second.first < 0) return false;
                long cb = it->second.first, ce = it->second.second, clen = ce - cb;
                if (clen < 0 || pos + clen > (long)st.s.size()) return false;
                if (st.s.compare(pos, clen, st.s, cb, clen) != 0) return false;
                return k(pos + clen);
            }
            // `$0`/`$1` backreference: the IN-FLIGHT capture of this same match
            // (`(.) $0*` matches a run of the captured character)
            if (n->lit.size() > 1 && ascii::isdigit((unsigned char)n->lit[1])) {
                long ci = std::stol(n->lit.substr(1));
                if (ci >= 0 && ci < (long)st.caps.size() && st.caps[ci].first >= 0) {
                    long cb = st.caps[ci].first, ce = st.caps[ci].second;
                    long clen = ce - cb;
                    if (clen < 0) return false;
                    if (pos + clen > (long)st.s.size()) return false;
                    if (n->icase) { // (?i) backref (P5): ASCII case-blind comparison
                        for (long i = 0; i < clen; i++)
                            if (ascii::tolower((unsigned char)st.s[pos + i]) != ascii::tolower((unsigned char)st.s[cb + i]))
                                return false;
                    }
                    else if (st.s.compare(pos, clen, st.s, cb, clen) != 0) return false;
                    return k(pos + clen);
                }
                return false; // that group hasn't captured yet
            }
            if (!st.hooks || !st.hooks->str) return false;
            const auto& params = st.grammar ? st.grammar->currentParams() : kNoParams;
            std::string v = st.hooks->str(n->lit, st.named, params);
            if (pos + (long)v.size() > (long)st.s.size()) return false;
            if (st.s.compare(pos, v.size(), v) != 0) return false;
            return k(pos + (long)v.size());
        }
        case K::Alt: {
            if (n->firstMatch) { // `||` — try branches in order, first that satisfies k wins
                long cf0 = st.capFrom, ct0 = st.capTo, fc0 = st.firstCode, lp0 = st.litPrefix;
                for (auto& kid : n->kids) {
                    if (matchNode(kid.get(), st, pos, k)) return true;
                    st.capFrom = cf0; st.capTo = ct0; st.firstCode = fc0; // roll back a failed branch's <( )> / firstCode
                    st.litPrefix = lp0; // …and its literal-prefix bookkeeping (LTM tie-break)
                }
                return false;
            }
            // `|` — longest-token match, two selectable rankers during the v3
            // rollout (LTM-PLAN.md phase 2):
            //
            // RAKUPP_LTM=1 — TRUE LTM: rank by each branch's DECLARATIVE
            // PREFIX via the LtmNfa (one linear scan, no user code, no
            // backtracking), then commit branches in that order with the real
            // engine. Branches whose prefix cannot match the input are not
            // candidates at all; empty-prefix branches rank last but are
            // tried, per S05.
            static const bool ltmMode = ltmModeOn();
            if (ltmMode) {
                static std::mutex ltmBuildM2;
                LtmNfa* nfa = nullptr;
                LtmExpand ectx;
                ectx.hooks = st.hooks;
                if (st.grammar)
                    ectx.grammar = [&st](const std::string& nm, const void*& reOut, char& fl) {
                        return st.grammar->ltmResolve(nm, reOut, fl);
                    };
                {
                    std::lock_guard<std::mutex> lk(ltmBuildM2);
                    if (n->ltmNfa && !n->ltmNfa->stillValid(st.hooks))
                        n->ltmNfa.reset(); // a named rule was re-declared: rebuild
                    if (!n->ltmNfa) n->ltmNfa = LtmNfa::buildForAlt(*this, n, &ectx);
                    nfa = n->ltmNfa.get();
                }
                if (nfa && !nfa->anyModelGap()) {
                    auto ranked = nfa->rank(st.s, pos);
                    static const bool rankDump = std::getenv("RAKUPP_LTM_RANKDUMP") != nullptr;
                    if (rankDump) {
                        fprintf(stderr, "[rank] pos=%ld kids=%zu:", pos, n->kids.size());
                        for (auto& r : ranked) fprintf(stderr, " b%d end=%ld lit=%ld", r.branch, r.prefixEnd, r.litPrefix);
                        fprintf(stderr, "\n");
                    }
                    for (auto& r : ranked)
                        if (matchNode(n->kids[r.branch].get(), st, pos, k)) return true;
                    return false;
                }
                // fall through to the probe when the NFA could not build OR
                // some branch's prefix ended at a construct the model does
                // not cover yet (subrule expansion is phase 3) — a hybrid,
                // so RAKUPP_LTM=1 is never LESS correct than the probe
            }
            // Default (probe) path: rank by each branch's greedy full-match
            // end. True LTM needs each branch's set of reachable ends;
            // enumerating them (a trivial `return false` continuation) explodes
            // combinatorially on recursive grammars. So probe each branch ONCE for its
            // greedy end (cheap, single descent), snapshotting interpreter side-effects,
            // then commit branches in longest-end-first order with the real continuation.
            // The commit re-runs the winning branch so its captures are singular/live.
            std::shared_ptr<void> snap = (st.hooks && st.hooks->saveState) ? st.hooks->saveState() : nullptr;
            // The probe's `return true` continuation makes capture-setting nodes KEEP their
            // captures; snapshot the match-state containers so probing doesn't leak captures
            // into the commit (which would duplicate them into Arrays).
            auto savedCaps = st.caps; auto savedNamed = st.named;
            auto savedChildren = st.children;
            auto savedReps = st.capReps;
            long savedCapFrom = st.capFrom, savedCapTo = st.capTo, savedFirstCode = st.firstCode, savedLitPrefix = st.litPrefix;
            std::vector<std::pair<long, size_t>> order; // (greedy end, branch index)
            for (size_t i = 0; i < n->kids.size(); i++) {
                long e0 = -1;
                matchNode(n->kids[i].get(), st, pos, [&](long e) { e0 = e; return true; });
                if (e0 >= 0) order.push_back({e0, i});
            }
            st.caps = std::move(savedCaps); st.named = std::move(savedNamed);
            st.children = std::move(savedChildren);
            st.capReps = std::move(savedReps);
            st.capFrom = savedCapFrom; st.capTo = savedCapTo; st.firstCode = savedFirstCode; st.litPrefix = savedLitPrefix;
            if (snap && st.hooks->restoreState) st.hooks->restoreState(snap);
            std::stable_sort(order.begin(), order.end(),
                             [](const auto& a, const auto& b) { return a.first > b.first; });
            // ---- LTM phase-1 harness (RAKUPP_LTM_DEBUG=1): rank the same
            // branches with the declarative-prefix NFA and report ORDER
            // disagreements with the probe. The probe's answer still decides —
            // this block changes no behavior; it exists to classify every
            // disagreement against the Rakudo oracle before phase 2 wires the
            // NFA in for real (LTM-PLAN.md).
            static const bool ltmDebug = std::getenv("RAKUPP_LTM_DEBUG") != nullptr;
            if (ltmDebug) {
                static std::mutex ltmBuildM;
                LtmNfa* nfa = nullptr;
                LtmExpand ectx;
                ectx.hooks = st.hooks;
                if (st.grammar)
                    ectx.grammar = [&st](const std::string& nm, const void*& reOut, char& fl) {
                        return st.grammar->ltmResolve(nm, reOut, fl);
                    };
                {
                    std::lock_guard<std::mutex> lk(ltmBuildM);
                    if (n->ltmNfa && !n->ltmNfa->stillValid(st.hooks))
                        n->ltmNfa.reset();
                    if (!n->ltmNfa) n->ltmNfa = LtmNfa::buildForAlt(*this, n, &ectx);
                    nfa = n->ltmNfa.get();
                }
                if (nfa) {
                    auto ranked = nfa->rank(st.s, pos);
                    bool differ = ranked.size() != order.size();
                    if (!differ)
                        for (size_t i = 0; i < ranked.size(); i++)
                            if (ranked[i].branch != (int)order[i].second) { differ = true; break; }
                    if (differ) {
                        std::string a, b;
                        for (auto& o : order)  a += std::to_string(o.second) + "@" + std::to_string(o.first) + " ";
                        for (auto& r : ranked) b += std::to_string(r.branch) + "@" + std::to_string(r.prefixEnd) + " ";
                        std::string pv = pat_.size() > 60 ? pat_.substr(0, 60) + "…" : pat_;
                        std::string sv = st.s.size() > 40 ? st.s.substr(0, 40) + "…" : st.s;
                        std::cerr << "[LTM] /" << pv << "/ on \"" << sv << "\" pos " << pos
                                  << "  probe: " << a << " nfa: " << b << "\n";
                    }
                }
            }
            for (auto& pr : order) {
                if (matchNode(n->kids[pr.second].get(), st, pos, k)) return true;
            }
            return false;
        }
        case K::Rep: {
            const Node* child = n->kids[0].get();
            const Node* sep = n->sep.get(); // separator for `X+ % Y` (null if none)
            long mn = n->min, mx = n->max; bool greedy = n->greedy;
            if (!n->repCode.empty() && st.hooks && st.hooks->range) { // `** { … }` runtime bounds
                const auto& params = st.grammar ? st.grammar->currentParams() : kNoParams;
                auto rng = st.hooks->range(n->repCode, st.named, params); mn = rng.first; mx = rng.second;
            }
            auto rep = [&](auto&& self, long count, long p) -> bool {
                // match one more `child`, preceded by `sep` on all but the first iteration
                auto matchOne = [&](long q, const FnRef& kk) -> bool {
                    if (count > 0 && sep) {
                        auto viaSep = [&](long sp) { return matchNode(child, st, sp, kk); };
                        return matchNode(sep, st, q, viaSep);
                    }
                    return matchNode(child, st, q, kk);
                };
                // `%%`: after the last item, an optional trailing separator may be
                // consumed — try the continuation with it first (greedy), then without
                auto finish = [&](long q, long itemCount) -> bool {
                    if (sep && n->sepTrail && itemCount > 0) {
                        auto viaK = [&](long sp) { return k(sp); };
                        if (matchNode(sep, st, q, viaK)) return true;
                    }
                    return k(q);
                };
                if (greedy && (ratchet_ || n->possessive)) {
                    // possessive: grab as many children as possible (each at its greedy
                    // longest), then commit — never give any back. This is `:ratchet`
                    // token semantics, and it kills exponential partition backtracking.
                    // Apply the separator on every iteration after the first (matchOne's
                    // own `count` check can't be used — it stays fixed at the outer count).
                    long cnt = count, q = p;
                    while (mx < 0 || cnt < mx) {
                        long np = -1;
                        auto grab = [&](long r) { np = r; return true; };
                        if (cnt > 0 && sep) {
                            // commit the separator at its first (greedy) end even if the
                            // child then fails — matches the pre-FnRef behavior exactly
                            auto viaSep = [&](long sp) { matchNode(child, st, sp, grab); return true; };
                            matchNode(sep, st, q, viaSep);
                        } else
                            matchNode(child, st, q, grab);
                        if (np < 0 || np == q) break;
                        q = np; cnt++;
                    }
                    if (cnt >= mn) return finish(q, cnt);
                    return false;
                }
                // Iterative greedy for a deterministic single-atom child
                // (Lit/Any/Class, no separator): grab as far as possible collecting
                // stop positions, then try the continuation longest-first. The
                // recursive form below spends one C++ stack frame per repetition,
                // which overflowed the stack on long runs — `/\d+/` over a few
                // million chars crashed with SIGBUS. An atom matches exactly one way
                // per position, so iterating is behaviourally identical.
                if (greedy && !sep &&
                    (child->k == K::Lit || child->k == K::Any || child->k == K::Class)) {
                    // Positions after 0,1,2,… atoms. A stack buffer holds the common
                    // short match with ZERO heap allocation (the regex hot path is
                    // many tiny matches); only a genuinely long run spills to the
                    // heap — and that run was previously a stack-overflow crash, so
                    // the one allocation is well spent.
                    constexpr int INLINE = 512;
                    long inlineStops[INLINE];
                    int ni = 0;
                    std::vector<long> spill;
                    inlineStops[ni++] = p;
                    long q = p, c = count;
                    while (mx < 0 || c < mx) {
                        long np = -1;
                        matchOne(q, [&](long r) { np = r; return true; });
                        if (np < 0 || np == q) break; // no more / zero-width
                        q = np; c++;
                        if (ni < INLINE) inlineStops[ni++] = q; else spill.push_back(q);
                    }
                    long nStops = ni + (long)spill.size();
                    for (long i = nStops - 1; i >= 0; i--) { // greedy: longest first
                        long cnt = count + i;
                        long posAt = i < ni ? inlineStops[i] : spill[i - ni];
                        if (cnt >= mn && finish(posAt, cnt)) return true;
                    }
                    return false;
                }
                if (greedy) {
                    if (mx < 0 || count < mx) {
                        auto more = [&](long np) {
                            if (np != p) return self(self, count + 1, np);
                            // zero-width child match: Perl admits it as ONE final
                            // iteration — `(]*)?\1` participates with an empty
                            // capture (re_tests 1327). Raku rejects it outright.
                            return p5_ && count + 1 >= mn && finish(np, count + 1);
                        };
                        if (matchOne(p, more)) return true;
                    }
                    if (count >= mn) return finish(p, count);
                    return false;
                } else {
                    if (count >= mn && finish(p, count)) return true;
                    if (mx < 0 || count < mx) {
                        auto more = [&](long np) {
                            if (np != p) return self(self, count + 1, np);
                            return p5_ && count + 1 >= mn && finish(np, count + 1);
                        };
                        return matchOne(p, more);
                    }
                    return false;
                }
            };
            return rep(rep, 0, pos);
        }
        case K::Group: {
            const Node* child = n->kids[0].get();
            int ci = n->capIndex;
            const std::string& cn = n->capName;
            return matchNode(child, st, pos, [&](long np) -> bool {
                std::pair<long,long> savedC{-1,-1}, savedN{-1,-1}; bool hadN = false;
                if (ci >= 0 && ci < (long)st.caps.size()) { savedC = st.caps[ci]; st.caps[ci] = {pos, np}; }
                // a capture under a repetition quantifier collates every occurrence
                // into a list (`(\d)+` → $0 is an Array), matching Rakudo
                if (n->listCap && ci >= 0) st.capReps[ci].push_back({pos, np});
                if (!cn.empty()) {
                    hadN = st.named.count(cn); if (hadN) savedN = st.named[cn]; st.named[cn] = {pos, np};
                    // also collate the occurrence (empty name = plain capture, not a rule),
                    // so a capture repeated under a quantifier yields a list like Rakudo's
                    ParseNode leaf; leaf.from = pos; leaf.to = np;
                    // `$<value>=<value-sq>` — a named group wrapping a SUBRULE keeps that
                    // subrule's own tree, so `$<value><val>` still reaches inside. A bare
                    // span would drop it: XML's attribute rule captures single-quoted
                    // values exactly this way.
                    // The wrapped subrule recorded its own node over exactly this span;
                    // find it by span rather than by walking the AST, which the sigspace
                    // forms wrap in a Seq.
                    for (auto& ce : st.children) {
                        if (ce.first == cn || ce.second.empty()) continue;
                        const ParseNode& sub = ce.second.back();
                        if (sub.from != pos || sub.to != np) continue;
                        if (!sub.kids && sub.named.empty() && sub.caps.empty()) continue;
                        leaf.name = cn;   // it stands for a rule now, not a bare span
                        leaf.kids = sub.kids;
                        leaf.named = sub.named;
                        leaf.caps = sub.caps;
                        leaf.listNames = sub.listNames;
                        break;
                    }
                    st.children[cn].push_back(std::move(leaf));
                }
                if (k(np)) return true;
                if (ci >= 0 && ci < (long)st.caps.size()) st.caps[ci] = savedC;
                if (n->listCap && ci >= 0) {
                    auto it = st.capReps.find(ci);
                    if (it != st.capReps.end()) { it->second.pop_back(); if (it->second.empty()) st.capReps.erase(it); }
                }
                if (!cn.empty()) {
                    if (hadN) st.named[cn] = savedN; else st.named.erase(cn);
                    st.children[cn].pop_back();
                    if (st.children[cn].empty()) st.children.erase(cn);
                }
                return false;
            });
        }
    }
    return false;
}

bool Regex::search(const std::string& subject, long startPos, RxMatch& out) const {
    return search(subject, startPos, out, nullptr);
}

bool Regex::search(const std::string& subject, long startPos, RxMatch& out, const SubResolver& r,
                   const std::set<std::string>* lexNames) const {
    if (!ok_ || !root_) return false;
    long budget = 0; // shared across start positions: a whole search is bounded, not each attempt
    for (long start = startPos; start <= (long)subject.size(); start++) {
        MState st{subject, std::vector<std::pair<long, long>>(ncaps_, {-1, -1}), {}, {}, r ? &r : nullptr, nullptr};
        st.lexNames = lexNames;
        st.hooks = runHooks; // standalone matches may still run {…} blocks
        st.startPos = start;  // where THIS attempt began — the `$/` a `{…}` block sees
        st.steps = budget;
        long endPos = -1;
        try {
            if (matchNode(root_.get(), st, start, [&](long e) { endPos = e; return true; })) {
                out.matched = true; out.from = st.capFrom >= 0 ? st.capFrom : start; out.to = st.capTo >= 0 ? st.capTo : endPos;
                out.caps = st.caps; out.named = st.named;
                out.children = st.children; out.capReps = st.capReps; out.listCaps = listCaps_; out.listNames = listNames_; out.hashNames = hashNames_;
                return true;
            }
        } catch (const StepLimitExceeded&) { return false; } // pathological pattern: give up (no match)
        budget = st.steps;
    }
    return false;
}

std::vector<RxMatch> Regex::searchExhaustive(const std::string& subject, const SubResolver& r,
                                             const std::set<std::string>* lexNames) const {
    std::vector<RxMatch> results;
    if (!ok_ || !root_) return results;
    long budget = 0;
    for (long start = 0; start <= (long)subject.size(); start++) {
        MState st{subject, std::vector<std::pair<long, long>>(ncaps_, {-1, -1}), {}, {}, r ? &r : nullptr, nullptr};
        st.lexNames = lexNames;
        st.hooks = runHooks;
        st.startPos = start;
        st.steps = budget;
        try {
            // A `false`-returning continuation records the match then forces the
            // engine to keep backtracking, so EVERY reachable end is enumerated.
            matchNode(root_.get(), st, start, [&](long e) -> bool {
                RxMatch out;
                out.matched = true; out.from = st.capFrom >= 0 ? st.capFrom : start; out.to = st.capTo >= 0 ? st.capTo : e;
                out.caps = st.caps; out.named = st.named;
                out.children = st.children; out.capReps = st.capReps;
                out.listCaps = listCaps_; out.listNames = listNames_; out.hashNames = hashNames_;
                results.push_back(std::move(out));
                return false;
            });
        } catch (const StepLimitExceeded&) { break; }
        budget = st.steps;
    }
    return results;
}

bool Regex::matchAt(const std::string& subject, long pos, RxMatch& out, const SubResolver& r,
                    const std::set<std::string>* lexNames) const {
    if (!ok_ || !root_) return false;
    MState st{subject, std::vector<std::pair<long, long>>(ncaps_, {-1, -1}), {}, {}, r ? &r : nullptr, nullptr};
    st.lexNames = lexNames;
    st.hooks = runHooks; // a `my regex` subrule still runs its {…} blocks (same as search)
    st.startPos = pos;   // where this anchored attempt begins — the `$/` a block sees
    long endPos = -1;
    try {
        if (matchNode(root_.get(), st, pos, [&](long e) { endPos = e; return true; })) {
            out.matched = true; out.from = st.capFrom >= 0 ? st.capFrom : pos; out.to = st.capTo >= 0 ? st.capTo : endPos;
            out.caps = st.caps; out.named = st.named;
            out.children = st.children; out.capReps = st.capReps; out.listCaps = listCaps_; out.listNames = listNames_; out.hashNames = hashNames_;
            return true;
        }
    } catch (const StepLimitExceeded&) { return false; }
    return false;
}

// ---- GrammarMatcher: backtrackable grammar engine ------------------------------

// Resolve a `\c[…]` character name: full Unicode name, common control abbreviation, or decimal.
static int32_t namedCp(const std::string& nm) {
    int32_t cp = uniCharByName(nm);
    if (cp >= 0) return cp;
    static const std::map<std::string, int> ab = {
        {"NUL",0},{"SOH",1},{"STX",2},{"ETX",3},{"EOT",4},{"ENQ",5},{"ACK",6},{"BEL",7},
        {"BS",8},{"HT",9},{"TAB",9},{"LF",0x0A},{"VT",0x0B},{"FF",0x0C},{"CR",0x0D},
        {"SO",0x0E},{"SI",0x0F},{"ESC",0x1B},{"FS",0x1C},{"GS",0x1D},{"RS",0x1E},{"US",0x1F},
        {"SP",0x20},{"SPACE",0x20},{"DEL",0x7F},{"NEL",0x85},{"NBSP",0xA0},
    };
    auto it = ab.find(nm); if (it != ab.end()) return it->second;
    if (!nm.empty() && ascii::isdigit((unsigned char)nm[0])) return (int32_t)std::strtol(nm.c_str(), nullptr, 10);
    return -1;
}

static std::string gmQuoteMeta(const std::string& s) {
    std::string out;
    for (char c : s) {
        // whitespace is insignificant in a Raku regex — escape it so substituted
        // values (e.g. a space-valued indent) match literally
        if (c == ' ') { out += "\\ "; continue; }
        if (c == '\t') { out += "\\t"; continue; }
        if (c == '\n') { out += "\\n"; continue; }
        if (c == '\r') { out += "\\r"; continue; }
        if (std::strchr(".?*+^$()[]{}|\\<>-", c)) out += '\\';
        out += c;
    }
    return out;
}

std::string GrammarMatcher::evalArg(const std::string& e0) const {
    std::string e = e0;
    auto l = e.find_first_not_of(" \t"); if (l == std::string::npos) return "";
    auto r = e.find_last_not_of(" \t"); e = e.substr(l, r - l + 1);
    { bool inq = false; char q = 0;
      for (size_t i = 0; i < e.size(); i++) { char c = e[i];
          if (inq) { if (c == q) inq = false; }
          else if (c == '\'' || c == '"') { inq = true; q = c; }
          else if (c == '~') return evalArg(e.substr(0, i)) + evalArg(e.substr(i + 1)); } }
    if (e.empty()) return "";
    if (e[0] == '$') {
        if (!scope_.empty()) { auto it = scope_.back().find(e); if (it != scope_.back().end()) return it->second; }
        if (hooks.str) return hooks.str(e, {}, currentParams()); // a `:my`/runtime var — ask the interpreter
        return "";
    }
    if (e[0] == '\'' || e[0] == '"') {
        std::string o; for (size_t i = 1; i < e.size() && e[i] != e[0]; i++) {
            if (e[i] == '\\' && i + 1 < e.size()) { i++; o += (e[i] == 'n' ? '\n' : e[i] == 't' ? '\t' : e[i]); } else o += e[i]; }
        return o;
    }
    return e; // number / bareword
}

std::vector<std::string> GrammarMatcher::splitArgs(const std::string& s) const {
    std::vector<std::string> out; std::string cur; int depth = 0; bool inq = false; char q = 0;
    for (char c : s) {
        if (inq) { cur += c; if (c == q) inq = false; }
        else if (c == '\'' || c == '"') { inq = true; q = c; cur += c; }
        else if (c == '(' || c == '[') { depth++; cur += c; }
        else if (c == ')' || c == ']') { depth--; cur += c; }
        else if (c == ',' && depth == 0) { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty() || !out.empty()) out.push_back(cur);
    return out;
}

const std::map<std::string, std::string>& GrammarMatcher::currentParams() const {
    static const std::map<std::string, std::string> empty;
    if (scope_.empty()) return empty;
    // Dynamic-var params (`token value($*STOPPER = '"')`) are visible to every rule
    // the parameterised rule calls (XML's `char` reads $*STOPPER two frames down) —
    // merge them from outer frames; inner bindings shadow outer, and the current
    // frame's own (lexical) params always win.
    bool outerDyn = false;
    for (size_t i = 0; i + 1 < scope_.size() && !outerDyn; i++)
        for (auto& kv : scope_[i])
            if (kv.first.size() > 1 && kv.first[1] == '*') { outerDyn = true; break; }
    if (!outerDyn) return scope_.back();
    mergedParams_.clear();
    for (auto& frame : scope_)
        for (auto& kv : frame)
            if (&frame == &scope_.back() || (kv.first.size() > 1 && kv.first[1] == '*'))
                mergedParams_[kv.first] = kv.second;
    return mergedParams_;
}

std::string GrammarMatcher::interpParams(const std::string& pat, const std::map<std::string, std::string>& sc) const {
    if (sc.empty()) return pat;
    std::string out; int angle = 0, brace = 0;
    for (size_t i = 0; i < pat.size(); i++) {
        char c = pat[i];
        if (c == '\\' && i + 1 < pat.size()) { out += c; out += pat[i + 1]; i++; continue; }
        if (c == '{') { brace++; out += c; continue; }   // don't substitute inside {…} code blocks
        if (c == '}') { if (brace > 0) brace--; out += c; continue; }
        if (c == '<') { angle++; out += c; continue; }
        if (c == '>') { if (angle > 0) angle--; out += c; continue; }
        if (c == '$' && angle == 0 && brace == 0 && i + 1 < pat.size() && (ascii::isalpha((unsigned char)pat[i + 1]) || pat[i + 1] == '_')) {
            size_t j = i + 1; while (j < pat.size() && (ascii::isalnum((unsigned char)pat[j]) || pat[j] == '_' || pat[j] == '-')) j++;
            auto it = sc.find("$" + pat.substr(i + 1, j - i - 1));
            if (it != sc.end()) { out += gmQuoteMeta(it->second); i = j - 1; continue; }
        }
        out += c;
    }
    return out;
}

// Compile a rule for a given call (interpolating parameter values into the body),
// caching by name + evaluated arg VALUES (short) — never by the pattern text, so a
// cache hit costs no pattern-length copies or compares. Returns the bindings to push.
Regex* GrammarMatcher::compiled(const std::string& name, const std::string& argstr,
                                std::map<std::string, std::string>& boundOut) {
    auto it = rules.find(name);
    if (it == rules.end()) return nullptr;
    return compiledFor(it->second, name, argstr, boundOut);
}

Regex* GrammarMatcher::compiledFor(const Rule& rule, const std::string& name, const std::string& argstr,
                                   std::map<std::string, std::string>& boundOut) {
    std::string key = name;
    if (!rule.params.empty()) {
        auto args = splitArgs(argstr);
        for (size_t i = 0; i < rule.params.size(); i++) {
            // a param entry is "NAME" or "NAME\x1fDEFAULT-EXPR" (token value($*STOPPER = '"'))
            std::string pname = rule.params[i], dflt;
            auto dsep = pname.find('\x1f');
            if (dsep != std::string::npos) { dflt = pname.substr(dsep + 1); pname = pname.substr(0, dsep); }
            std::string v = i < args.size() ? evalArg(args[i])
                          : !dflt.empty()  ? evalArg(dflt) : std::string();
            key += '\x1f'; key += v;
            boundOut[pname] = std::move(v);
        }
    }
    auto cit = cache_.find(key);
    if (cit == cache_.end()) {
        const std::string& ipat = rule.params.empty() ? rule.pattern : interpParams(rule.pattern, boundOut);
        cit = cache_.emplace(std::move(key), std::make_unique<Regex>(ipat,
            rule.kind == "rule" ? "sr" : rule.kind == "regex" ? "" : "r")).first; // token/rule ratchet; regex does not
    }
    return cit->second.get();
}

// Match subrule <name(args)> at `pos`, threading `k` through the callee so the
// outer pattern can backtrack into it. `capKey` (empty for <.name>) is where the
// sub-match is recorded in the parent frame.
const GrammarMatcher::NameMeta& GrammarMatcher::nameMeta(const std::string& name) {
    auto it = nameMeta_.find(name);
    if (it != nameMeta_.end()) return it->second;
    NameMeta m;
    auto rit = rules.find(name);
    const Rule* rule = rit != rules.end() ? &rit->second : nullptr;
    m.rule = rule;
    m.ratchet = rule && rule->kind != "regex";
    // A body that declares `:my` gets a fresh dynamic scope per invocation: its vars must be
    // rolled back when the rule exits (dynamic scoping). And any body that touches a dynamic
    // var — declaring `:my` or reading `$*`/`@*`/`%*` (e.g. an indentation `<?{ … == $*IND }>`)
    // — can match differently depending on caller state the packrat key doesn't capture, so
    // it must not be memoised.
    if (rule) {
        const std::string& p = rule->pattern;
        m.scoped = p.find(":my") != std::string::npos;
        m.dynDep = m.scoped || p.find("$*") != std::string::npos
                            || p.find("@*") != std::string::npos
                            || p.find("%*") != std::string::npos;
    }
    m.id = (int)nameMeta_.size();
    // Rules with no params whose whole body is a single-character class (space, break,
    // plainfirst-ish …) get inlined at call sites — they dominate call volume.
    // Every parameterless rule keeps its compiled body in `noArg` so the per-call
    // path never touches the key/cache machinery.
    if (rule && rule->params.empty()) {
        std::map<std::string, std::string> b;
        m.noArg = compiled(name, "", b);
        if (m.ratchet && m.noArg && m.noArg->rootIsSingleChar()) m.singleChar = m.noArg;
    }
    auto pit = protos.find(name);
    if (pit != protos.end()) m.proto = &pit->second;
    // Only fall back to the built-in <ws> when the grammar hasn't defined its own —
    // a user `token ws { … }` (e.g. to skip comments) must win over the builtin.
    m.isWs = (name == "ws" && !rule);
    if (!rule) { // built-in char-class fallbacks for names the grammar doesn't define
        if (name == "digit") m.builtinClass = "d"; else if (name == "alpha") m.builtinClass = "a";
        else if (name == "alnum" || name == "ident") m.builtinClass = "ad";
        else if (name == "space") m.builtinClass = "s";
        else if (name == "blank") m.builtinClass = "b"; // horizontal ws only
        else if (name == "upper") m.builtinClass = "u"; else if (name == "lower") m.builtinClass = "l";
        else if (name == "xdigit") m.builtinClass = "x";
    }
    return nameMeta_.emplace(name, std::move(m)).first->second;
}

bool GrammarMatcher::matchSub(const std::string& name, const std::string& args, const std::string& capKey,
                              Regex::MState& st, long pos, const FnRef& k) {
    return matchSubMeta(nameMeta(name), name, args, capKey, st, pos, k);
}

int GrammarMatcher::ltmResolve(const std::string& name, const void*& regexOut, char& flagOut) {
    const NameMeta& m = nameMeta(name);
    if (m.isWs) return 2;
    if (m.proto) return 0;             // nested proto: not unioned here (yet)
    if (m.dynDep) return 0;            // caller-state-dependent body
    if (m.rule) {
        auto* rl = static_cast<const Rule*>(m.rule);
        if (!rl->params.empty()) return 0;
        Regex* body = m.noArg;
        if (!body) {                   // compile-and-cache, same as a first call would
            std::map<std::string, std::string> bound;
            body = compiledFor(*rl, name, "", bound);
        }
        if (!body || !body->ok()) return 0;
        regexOut = body;
        return 1;
    }
    if (m.builtinClass.size() == 1) { flagOut = m.builtinClass[0]; return 3; }
    return 0;
}

bool GrammarMatcher::matchSubMeta(const GrammarRuleMeta& meta, const std::string& name,
                                  const std::string& args, const std::string& capKey,
                                  Regex::MState& st, long pos, const FnRef& k,
                                  bool alsoBareName) {
    // `<sym>` inside a proto candidate (`token alt:sym<foo> { <sym> }`) matches that
    // candidate's sym literal ("foo"), threaded in via st.curSym.
    if (name == "sym" && args.empty()) {
        if (st.curSym && !st.curSym->empty()) {
            const std::string& sv = *st.curSym;
            if (pos + (long)sv.size() <= (long)st.s.size() && st.s.compare(pos, sv.size(), sv) == 0) {
                long np = pos + (long)sv.size();
                if (capKey.empty()) return k(np);
                ParseNode pn; pn.name = name; pn.from = pos; pn.to = np;
                bool hadSpan = st.named.count(capKey); auto savedSpan = hadSpan ? st.named[capKey] : std::pair<long,long>{-1,-1};
                st.named[capKey] = {pos, np};
                st.children[capKey].push_back(std::move(pn));
                if (k(np)) return true;
                st.children[capKey].pop_back();
                if (st.children[capKey].empty()) st.children.erase(capKey);
                if (hadSpan) st.named[capKey] = savedSpan; else st.named.erase(capKey);
                return false;
            }
        }
        noteFail(pos, name);
        return false;
    }
    // If this call is a proto candidate `X:sym<VALUE>`, its body's `<sym>` matches VALUE;
    // otherwise inherit the enclosing candidate's sym (if any).
    std::string candSym; const std::string* symPtr = st.curSym;
    { auto sp = name.find(":sym<");
      if (sp != std::string::npos) { auto e = name.find('>', sp + 5); if (e != std::string::npos) { candSym = name.substr(sp + 5, e - (sp + 5)); symPtr = &candSym; } }
      else { sp = name.find(":sym\xC2\xAB"); // :sym«…»
             if (sp != std::string::npos) { auto e = name.find("\xC2\xBB", sp + 6); if (e != std::string::npos) { candSym = name.substr(sp + 6, e - (sp + 6)); symPtr = &candSym; } } } }
    // protoregex: `<element>` dispatches to its `element:<sym>` candidates, longest wins (LTM)
    if (meta.proto) {
        const auto& cands = *meta.proto;
        // RAKUPP_LTM=1: rank candidates with the union NFA over their
        // declarative prefixes — one linear scan, no probe descents, no user
        // code. <sym> inlines as each candidate's literal. Any model gap in
        // any candidate falls the whole proto back to the probe (the same
        // never-less-correct hybrid the Alt site uses). The matcher is
        // per-parse, so the meta-cached NFA cannot go stale.
        if (rakupp::ltmModeOn() && args.empty()) {
            if (!meta.protoNfaTried) {
                meta.protoNfaTried = true;
                std::vector<const void*> bodies;
                std::vector<std::string> syms;
                bool ok = true;
                for (auto& cand : cands) {
                    const void* reOut = nullptr; char fl = 0;
                    if (ltmResolve(cand, reOut, fl) != 1) { ok = false; break; }
                    bodies.push_back(reOut);
                    std::string sym;
                    auto sp = cand.find(":sym<");
                    if (sp != std::string::npos) {
                        auto e = cand.find('>', sp + 5);
                        if (e != std::string::npos) sym = cand.substr(sp + 5, e - (sp + 5));
                    }
                    else {
                        sp = cand.find(":sym\xC2\xAB");
                        if (sp != std::string::npos) {
                            auto e = cand.find("\xC2\xBB", sp + 6);
                            if (e != std::string::npos) sym = cand.substr(sp + 6, e - (sp + 6));
                        }
                    }
                    syms.push_back(std::move(sym));
                }
                if (ok) {
                    LtmExpand ectx;
                    ectx.hooks = st.hooks;
                    ectx.grammar = [this](const std::string& nm, const void*& reOut2, char& fl2) {
                        return ltmResolve(nm, reOut2, fl2);
                    };
                    auto built = LtmNfa::buildForBranches(bodies, syms, &ectx);
                    if (built && !built->anyModelGap())
                        meta.protoNfa = std::move(built);
                }
            }
            if (meta.protoNfa) {
                auto ranked = meta.protoNfa->rank(st.s, pos);
                for (auto& r : ranked)
                    if (matchSub(cands[r.branch], args, capKey, st, pos, k)) return true;
                return false;
            }
        }
        // Longest-token-match ranks candidates by their DECLARATIVE-prefix length — the span
        // matched before the first bare `{…}` code block (which ends the declarative part);
        // a candidate with no code block ranks by its full match. First pass: measure each.
        struct Ranked { long declEnd; long litPrefix; const std::string* cand; };
        std::vector<Ranked> ranked;
        for (auto& cand : cands) {
            candDeclEnd_ = -1; candLitPrefix_ = 0;
            if (matchSub(cand, args, "", st, pos, [&](long) { return true; }) && candDeclEnd_ >= 0)
                ranked.push_back({candDeclEnd_, candLitPrefix_, &cand});
        }
        // LTM ranking (S05): longest declarative match first; on a tie the more
        // specific candidate — the longer literal prefix (a literal outranks an open
        // char class / subrule) — wins; stable_sort leaves declaration order as the
        // final tiebreak for genuinely equal candidates.
        std::stable_sort(ranked.begin(), ranked.end(), [](const Ranked& a, const Ranked& b) {
            if (a.declEnd != b.declEnd) return a.declEnd > b.declEnd;
            return a.litPrefix > b.litPrefix;
        });
        for (auto& r : ranked)
            if (matchSub(*r.cand, args, capKey, st, pos, k)) return true;
        return false;
    }
    if (meta.isWs) { // built-in <ws>: \s* gated by <!ww> — same matcher as the plain-regex path
        long p = builtinRuleMatch("ws", st.s, pos, (long)st.s.size());
        return p < 0 ? false : k(p);
    }
    // built-in rules (<.alpha> …, <ident>) when the grammar doesn't redefine them
    if (!meta.rule) {
        // The SAME matcher the plain-regex path uses. This was a second, byte-wise
        // ASCII copy that also silently lacked punct/cntrl/graph/print — so a
        // grammar's `<alpha>` stopped at the first accented letter while the very
        // same `/<alpha>/` outside a grammar matched it.
        long pend = builtinRuleMatch(name, st.s, pos, (long)st.s.size());
        if (pend == -2) return false; // unknown subrule
        if (pend < 0) { noteFail(pos, name); return false; }
        if (capKey.empty()) return k(pend);
        // A capturing built-in rule inside a GRAMMAR records $<name> too — the
        // plain-regex path already did, this one dropped it, so YAMLish's
        // `x <xdigit>**2` saw an empty `$<xdigit>` and never decoded \xNN.
        ParseNode pn; pn.name = name; pn.from = pos; pn.to = pend;
        bool hadSpan = st.named.count(capKey);
        auto savedSpan = hadSpan ? st.named[capKey] : std::pair<long,long>{-1,-1};
        st.named[capKey] = {pos, pend};
        st.children[capKey].push_back(std::move(pn));
        if (k(pend)) return true;
        st.children[capKey].pop_back();
        if (st.children[capKey].empty()) st.children.erase(capKey);
        if (hadSpan) st.named[capKey] = savedSpan; else st.named.erase(capKey);
        return false;
    }
    // `<alias=rule>` captures under BOTH names in Rakudo: `<tags=tag-directive>`
    // fills `$<tags>` AND `$<tag-directive>`, which is how YAMLish's directives
    // action (`@<tag-directive>».ast`) reads the very captures the grammar aliased.
    // The DOTTED form `<alias=.rule>` is the opt-out — YAMLish's map-entry writes
    // `<key=.element(…)>` next to a real `<element(…)>`, and doubling that up would
    // turn `$<element>` into a two-element list.
    // "\x01…" keys are internal sentinels and never double up.
    const bool alsoRuleName = alsoBareName && !capKey.empty() && capKey != name && capKey[0] != '\x01';

    // Inline single-character rules (space, break, …): a bare char test, no memo/record
    // machinery. These are the overwhelming majority of subrule calls.
    // -3: not a single-char rule at all. -2: a multibyte character is sitting at
    // `pos`, which only the real matcher can decide — fall through to it.
    const long scNp = (meta.singleChar && args.empty()) ? meta.singleChar->trySingleChar(st.s, pos) : -3;
    if (scNp != -3 && scNp != -2) {
        long np = scNp;
        if (np < 0) { noteFail(pos, name); return false; }
        if (capKey.empty()) return k(np);
        // capturing <name>: record a leaf node spanning the one char, then continue
        ParseNode pn; pn.name = name; pn.from = pos; pn.to = np;
        bool hadSpan = st.named.count(capKey); auto savedSpan = hadSpan ? st.named[capKey] : std::pair<long,long>{-1,-1};
        bool hadSpan2 = alsoRuleName && st.named.count(name);
        auto savedSpan2 = hadSpan2 ? st.named[name] : std::pair<long,long>{-1,-1};
        st.named[capKey] = {pos, np};
        if (alsoRuleName) { st.named[name] = {pos, np}; st.children[name].push_back(pn); }
        st.children[capKey].push_back(std::move(pn));
        if (k(np)) return true;
        st.children[capKey].pop_back();
        if (st.children[capKey].empty()) st.children.erase(capKey);
        if (hadSpan) st.named[capKey] = savedSpan; else st.named.erase(capKey);
        if (alsoRuleName) {
            st.children[name].pop_back();
            if (st.children[name].empty()) st.children.erase(name);
            if (hadSpan2) st.named[name] = savedSpan2; else st.named.erase(name);
        }
        return false;
    }
    bool ratchet = meta.ratchet; // token/rule commit + memoize

    // Record a completed sub-match (span + subtree) under `capKey` in the caller frame,
    // then run the caller's continuation `k`; on failure, roll the recording back.
    // `kids` is a frozen subtree — sharing it is O(1), which is what makes memo replays cheap.
    auto record = [&](long end, const std::vector<std::pair<long,long>>& caps,
                      const GrammarHooks::NamedMap& named,
                      std::shared_ptr<const ChildMap> kids,
                      std::shared_ptr<const std::set<std::string>> listNames,
                      std::shared_ptr<const std::set<int>> listCaps = nullptr,
                      std::shared_ptr<const std::map<int, std::vector<std::pair<long,long>>>> capReps = nullptr,
                      long capFrom = -1, long capTo = -1) -> bool {
        if (capKey.empty()) return k(end);
        // `<( … )>` in the rule body trims what the CAPTURE reports; the parse
        // still continues at the real end.
        long cf = capFrom >= 0 ? capFrom : pos, ct = capTo >= 0 ? capTo : end;
        ParseNode pn; pn.name = name; pn.from = cf; pn.to = ct;
        pn.caps = caps; pn.named = named; pn.kids = std::move(kids);
        pn.listNames = std::move(listNames);
        pn.listCaps = std::move(listCaps); pn.capReps = std::move(capReps);
        bool hadSpan = st.named.count(capKey); auto savedSpan = hadSpan ? st.named[capKey] : std::pair<long, long>{-1, -1};
        bool hadSpan2 = alsoRuleName && st.named.count(name);
        auto savedSpan2 = hadSpan2 ? st.named[name] : std::pair<long, long>{-1, -1};
        st.named[capKey] = {cf, ct};
        if (alsoRuleName) { st.named[name] = {cf, ct}; st.children[name].push_back(pn); } // `<alias=rule>` answers to both
        st.children[capKey].push_back(std::move(pn)); // collate repeated captures into a list
        if (k(end)) return true;
        st.children[capKey].pop_back();               // backtrack: drop this occurrence
        if (st.children[capKey].empty()) st.children.erase(capKey);
        if (hadSpan) st.named[capKey] = savedSpan; else st.named.erase(capKey);
        if (alsoRuleName) {
            st.children[name].pop_back();
            if (st.children[name].empty()) st.children.erase(name);
            if (hadSpan2) st.named[name] = savedSpan2; else st.named.erase(name);
        }
        return false;
    };

    if (ratchet) {
        // Packrat: a ratchet token's match at (rule, params, pos) is deterministic. Serve
        // it from the memo when seen before; otherwise run once, committing to the first
        // (greedy/longest) complete match, and cache it. This is what makes recursive LTM
        // grammars run in polynomial rather than exponential time. Build an INTEGER key
        // and probe the memo BEFORE compiling/interpolating — the hot no-arg tokens
        // (space, alnum…) hit here and skip all string work.
        // Dynamic-var-dependent rules skip the memo: their match can depend on caller state
        // ($*IND, …) not in the key, and any `:my` side effects are rolled back on exit.
        bool memoise = !meta.dynDep;
        uint64_t mkey = (uint64_t)meta.id * 1099511628211ULL + (uint64_t)pos * 131ULL;
        if (memoise) {
            if (!args.empty()) // fold param VALUES (not text) into the key — they vary by caller scope
                for (auto& a : splitArgs(args)) { for (char c : evalArg(a)) mkey = mkey * 131 + (unsigned char)c; mkey = mkey * 131 + 1; }
            auto mit = memo_.find(mkey);
            if (mit != memo_.end()) {
                if (!mit->second.matched) return false;
                const MemoEntry& me = mit->second;
                candDeclEnd_ = me.declEnd;
                candLitPrefix_ = me.litPrefix;
                return record(me.end, me.caps, me.named, me.kids, me.listNames, me.listCaps, me.capReps,
                              me.capFrom, me.capTo);
            }
        }
        std::map<std::string, std::string> bound;
        Regex* re = (args.empty() && meta.noArg) ? meta.noArg
                  : compiledFor(*static_cast<const Rule*>(meta.rule), name, args, bound);
        if (!re || !re->ok()) { if (memoise) memo_[mkey].matched = false; return false; }
        Regex::MState sub{st.s, std::vector<std::pair<long, long>>(re->ncaps(), {-1, -1}), {}, {}, nullptr, this};
        sub.startPos = pos; sub.hooks = st.hooks; sub.curSym = symPtr;
        scope_.push_back(std::move(bound));
        // A `:my` rule opens a fresh dynamic scope: snapshot the interpreter's `:my` vars so
        // the rule's declarations (and shadows) are rolled back when it exits, restoring the
        // caller's bindings — this is what makes indentation-style dedent work.
        std::shared_ptr<void> savedScope = (meta.scoped && st.hooks && st.hooks->saveState)
                                         ? st.hooks->saveState() : nullptr;
        MemoEntry me;
        me.listNames = re->listNamesPtr();
        me.listCaps = re->listCapsPtr();
        re->matchNode(re->root(), sub, pos, [&](long end) -> bool {
            me.matched = true; me.end = end;
            me.capFrom = sub.capFrom; me.capTo = sub.capTo; // rule-body `<( … )>`
            me.declEnd = (sub.firstCode >= 0 ? sub.firstCode : end);
            me.litPrefix = (sub.litPrefix >= 0 ? sub.litPrefix - pos : 0);
            me.caps = sub.caps; me.named = sub.named;
            if (!sub.capReps.empty())
                me.capReps = std::make_shared<const std::map<int, std::vector<std::pair<long,long>>>>(std::move(sub.capReps));
            // the frame is committed (ratchet) — freeze its subtree without copying
            me.kids = sub.children.empty() ? nullptr
                    : std::make_shared<const ChildMap>(std::move(sub.children));
            return true; // commit to the first complete match — ratchet never backtracks in
        });
        if (savedScope) st.hooks->restoreState(savedScope); // rule exited: restore caller's dynamic scope
        scope_.pop_back();
        if (!me.matched) noteFail(pos, name); // the RULE failed here (not a continuation)
        // a FRESH completion fires its action method now (memo replays reuse it) —
        // Rakudo fires actions during the match, and a later failure keeps them
        if (me.matched && st.hooks && st.hooks->onRule && st.hooks->hasAction &&
            st.hooks->hasAction(name)) {
            ParseNode fp; fp.name = name;
            fp.from = me.capFrom >= 0 ? me.capFrom : pos;
            fp.to = me.capFrom >= 0 ? me.capTo : me.end;
            fp.caps = me.caps; fp.named = me.named; fp.kids = me.kids;
            fp.listNames = me.listNames; fp.listCaps = me.listCaps; fp.capReps = me.capReps;
            st.hooks->onRule(std::move(fp));
        }
        if (!memoise) {
            if (!me.matched) return false;
            candDeclEnd_ = me.declEnd;
            candLitPrefix_ = me.litPrefix;
            return record(me.end, me.caps, me.named, me.kids, me.listNames, me.listCaps, me.capReps,
                              me.capFrom, me.capTo);
        }
        auto& slot = (memo_[mkey] = std::move(me));
        if (!slot.matched) return false;
        candDeclEnd_ = slot.declEnd;
        candLitPrefix_ = slot.litPrefix;
        return record(slot.end, slot.caps, slot.named, slot.kids, slot.listNames, slot.listCaps, slot.capReps,
                      slot.capFrom, slot.capTo);
    }

    // non-ratchet `regex`: thread `k` through so the caller can backtrack into the callee
    std::map<std::string, std::string> bound;
    Regex* re = (args.empty() && meta.noArg) ? meta.noArg
              : compiledFor(*static_cast<const Rule*>(meta.rule), name, args, bound);
    if (!re || !re->ok()) return false;
    Regex::MState sub{st.s, std::vector<std::pair<long, long>>(re->ncaps(), {-1, -1}), {}, {}, nullptr, this};
    sub.startPos = pos; sub.hooks = st.hooks; sub.curSym = symPtr; // propagate hooks + candidate sym
    scope_.push_back(std::move(bound));
    std::shared_ptr<void> savedScope = (meta.scoped && st.hooks && st.hooks->saveState)
                                     ? st.hooks->saveState() : nullptr;   // fresh dynamic scope for `:my`
    bool calleeMatched = false; // vs. failing in the CALLER's continuation (G1 highwater)
    bool ok = re->matchNode(re->root(), sub, pos, [&](long end) -> bool {
        calleeMatched = true;
        // The callee has matched; the continuation `k` belongs to the CALLER, so its
        // code blocks must see the caller's params — pop the callee's param frame for
        // the duration of `k` (restore it so backtracking into the callee still works).
        auto calleeScope = std::move(scope_.back()); scope_.pop_back();
        auto finish = [&](bool r) { scope_.push_back(std::move(calleeScope)); return r; };
        candDeclEnd_ = (sub.firstCode >= 0 ? sub.firstCode : end);
        candLitPrefix_ = (sub.litPrefix >= 0 ? sub.litPrefix - pos : 0);
        // non-ratchet: the callee may complete again after backtracking, so its frame
        // stays live — freeze a COPY of the subtree for this completion
        auto kidsCopy = sub.children.empty() ? nullptr : std::make_shared<const ChildMap>(sub.children);
        // fresh completion: fire the action (each re-completion after a backtrack
        // fires again, as Rakudo's non-ratchet regexes do)
        if (st.hooks && st.hooks->onRule && st.hooks->hasAction && st.hooks->hasAction(name)) {
            ParseNode fp; fp.name = name;
            fp.from = sub.capFrom >= 0 ? sub.capFrom : pos;
            fp.to = sub.capFrom >= 0 ? sub.capTo : end;
            fp.caps = sub.caps; fp.named = sub.named; fp.kids = kidsCopy;
            fp.listNames = re->listNamesPtr(); fp.listCaps = re->listCapsPtr();
            if (!sub.capReps.empty())
                fp.capReps = std::make_shared<const std::map<int, std::vector<std::pair<long,long>>>>(sub.capReps);
            st.hooks->onRule(std::move(fp));
        }
        return finish(record(end, sub.caps, sub.named,
                             kidsCopy,
                             re->listNamesPtr(), re->listCapsPtr(),
                             sub.capReps.empty() ? nullptr
                               : std::make_shared<const std::map<int, std::vector<std::pair<long,long>>>>(sub.capReps),
                             sub.capFrom, sub.capTo)); // rule-body `<( … )>` trims the capture
    });
    if (savedScope) st.hooks->restoreState(savedScope); // rule exited: restore caller's dynamic scope
    scope_.pop_back();
    if (!calleeMatched) noteFail(pos, name); // the rule itself never completed here
    return ok;
}

void GrammarMatcher::reapMemo() {
    if (memo_.empty()) return;
    // See the ~GrammarMatcher comment in Regex.h. Thresholds: a small memo's
    // walk costs less than a thread spawn; the in-flight cap keeps a parse
    // loop from outrunning the reaper (at the cap we destroy inline — the
    // pre-reaper behavior, so degradation is graceful, never unbounded RAM).
    // Destructor recursion depth on the reaper thread = parse-tree depth;
    // the default pthread stack comfortably holds thousands of frames.
    static std::atomic<int> inFlight{0};
    if (memo_.size() < 512 || inFlight.load(std::memory_order_relaxed) >= 4) {
        memo_.clear();
        return;
    }
    auto box = std::make_unique<std::unordered_map<uint64_t, MemoEntry>>(std::move(memo_));
    memo_.clear(); // moved-from state → guaranteed empty for the next parse
    inFlight.fetch_add(1, std::memory_order_relaxed);
    std::thread([b = std::move(box)]() mutable {
        b.reset();
        inFlight.fetch_sub(1, std::memory_order_relaxed);
    }).detach();
}

bool GrammarMatcher::parse(const std::string& input, const std::string& top, bool subparse,
                           ParseNode& out, long& endOut) {
    clearMemo(); // packrat memo is valid only within a single input parse
    hwPos = -1; hwRule.clear(); // fresh highwater per parse (G1 diagnostics)
    // A proto rule used as the entry point (`.parse(:rule('lit'))`) dispatches to its
    // candidates with LTM, exactly as a `<lit>` subrule call would.
    if (protos.count(top)) {
        Regex::MState st{input, {}, {}, {}, nullptr, this};
        st.startPos = 0; st.hooks = &hooks;
        long endPos = -1;
        scope_.push_back({});
        bool ok = matchSubMeta(nameMeta(top), top, "", "\x01proto", st, 0, [&](long e) {
            if (!subparse && e != (long)input.size()) return false;
            endPos = e; return true;
        });
        scope_.pop_back();
        auto it = st.children.find("\x01proto");
        if (!ok || it == st.children.end() || it->second.empty()) return false;
        out = it->second.back();
        out.actualRule = out.name; // preserve the winning candidate for actions/makes
        out.name = top; out.from = 0; out.to = endPos;
        endOut = endPos;
        return true;
    }
    std::map<std::string, std::string> bound;
    Regex* re = compiled(top, "", bound);
    if (!re || !re->ok()) return false;
    Regex::MState st{input, std::vector<std::pair<long, long>>(re->ncaps(), {-1, -1}), {}, {}, nullptr, this};
    st.startPos = 0; st.hooks = &hooks; // top-level match starts at 0; wire the interpreter hooks
    long endPos = -1;
    scope_.push_back(std::move(bound)); // entry rule's params (defaults included) visible to its code blocks
    bool ok = re->matchNode(re->root(), st, 0, [&](long e) {
        if (!subparse && e != (long)input.size()) return false; // require a full match
        endPos = e; return true;
    });
    scope_.pop_back();
    if (!ok) return false;
    out.name = top; out.from = 0; out.to = endPos;
    out.caps = st.caps; out.named = st.named;
    out.kids = st.children.empty() ? nullptr : std::make_shared<const ChildMap>(std::move(st.children));
    out.listNames = re->listNamesPtr();
    endOut = endPos;
    return true;
}

} // namespace rakupp
