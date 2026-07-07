#include "Regex.h"
#include "Unicode.h"
#include <algorithm>
#include <cctype>
#include <set>

namespace rakupp {

static int32_t namedCp(const std::string& nm); // \c[NAME] resolver (defined below)

Regex::Regex(const std::string& pattern, const std::string& flags) : pat_(pattern) {
    for (char f : flags) {
        if (f == 'i') icase_ = true;
        if (f == 's') sigspace_ = true;
        if (f == 'r') ratchet_ = true;
    }
    try {
        root_ = parseAlt();
        if (!eof()) ok_ = false; // trailing garbage (e.g. unbalanced)
    } catch (...) {
        ok_ = false;
    }
}

void Regex::skipWs() {
    for (;;) {
        while (!eof() && std::isspace((unsigned char)peek())) pos_++;
        if (peek() == '#') { while (!eof() && peek() != '\n') pos_++; continue; }
        // inline adverb :i :s :ignorecase ...
        if (peek() == ':' && (std::isalpha((unsigned char)peek(1)))) {
            size_t save = pos_;
            pos_++;
            std::string adv;
            while (std::isalnum((unsigned char)peek())) adv += pat_[pos_++];
            if (adv == "i" || adv == "ignorecase") icase_ = true;
            else if (adv == "s" || adv == "sigspace") sigspace_ = true;
            else if (adv == "g" || adv == "ratchet" || adv == "m" || adv == "ratchet") {}
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
    auto first = parseSeq();
    if (peek() != '|') return first;
    auto alt = std::make_unique<Node>();
    alt->k = K::Alt;
    if (!isEmpty(first)) alt->kids.push_back(std::move(first));
    bool sawDouble = false;
    while (peek() == '|') {
        pos_++;
        if (peek() == '|') { pos_++; sawDouble = true; } // `||` = sequential first-match
        auto branch = parseSeq();
        if (!isEmpty(branch)) alt->kids.push_back(std::move(branch));
    }
    // pure `|` uses LTM (longest-token wins); any `||` present → first-match (conservative)
    alt->firstMatch = sawDouble;
    if (alt->kids.size() == 1) return std::move(alt->kids[0]);
    return alt;
}

Regex::NodePtr Regex::parseSeq() {
    auto seq = std::make_unique<Node>();
    seq->k = K::Seq;
    NodePtr goalClose; // `OPEN ~ CLOSE body…` — CLOSE is deferred to the end of the sequence
    for (;;) {
        size_t before = pos_;
        skipWs();
        bool hadSpace = pos_ > before;
        char c = peek();
        if (eof() || c == '|' || c == ')' || c == ']') break;
        if (assertDepth_ > 0 && c == '>') break; // end of an assertion inner
        // goal operator: `A ~ B  C D` matches `A C D B` (nice bracket-matching sugar)
        if (c == '~' && !seq->kids.empty()) { pos_++; skipWs(); goalClose = parseQuant(); continue; }
        // sigspace (a `rule`): whitespace between atoms matches optional whitespace `\s*`
        if (sigspace_ && !seq->kids.empty() && hadSpace) {
            auto cls = std::make_unique<Node>(); cls->k = K::Class; cls->classFlags = "s";
            auto rep = std::make_unique<Node>(); rep->k = K::Rep; rep->min = 0; rep->max = -1; rep->greedy = true;
            rep->kids.push_back(std::move(cls));
            seq->kids.push_back(std::move(rep));
        }
        seq->kids.push_back(parseQuant());
    }
    if (goalClose) seq->kids.push_back(std::move(goalClose)); // append the deferred CLOSE
    if (seq->kids.size() == 1) return std::move(seq->kids[0]);
    return seq;
}

Regex::NodePtr Regex::parseQuant() {
    auto atom = parseAtom();
    if (!sigspace_) skipWs(); // in sigspace, keep inter-atom whitespace so parseSeq can insert <.ws>
    char c = peek();
    long mn = -2, mx = -2;
    if (c == '*' && peek(1) != '*') { pos_++; mn = 0; mx = -1; }
    else if (c == '+') { pos_++; mn = 1; mx = -1; }
    else if (c == '?') { pos_++; mn = 0; mx = 1; }
    if (mn == -2 && peek() == '*' && peek(1) == '*') {
        pos_ += 2; skipWs();
        if (peek() == '{') { // `** { … }` — runtime bounds evaluated at match time
            int depth = 1; pos_++;
            std::string code;
            while (!eof() && depth > 0) { char d = pat_[pos_++]; if (d == '{') depth++; else if (d == '}') { depth--; if (!depth) break; } code += d; }
            auto rep = std::make_unique<Node>();
            rep->k = K::Rep; rep->min = 0; rep->max = -1; rep->greedy = true; rep->repCode = code;
            rep->kids.push_back(std::move(atom));
            return rep;
        }
        long lo = 0; bool haveLo = false;
        while (std::isdigit((unsigned char)peek())) { lo = lo * 10 + (pat_[pos_++] - '0'); haveLo = true; }
        mn = haveLo ? lo : 0;
        if (peek() == '.' && peek(1) == '.') {
            pos_ += 2;
            if (peek() == '*' || peek() == 'I') { pos_++; mx = -1; }
            else { long hi = 0; while (std::isdigit((unsigned char)peek())) hi = hi * 10 + (pat_[pos_++] - '0'); mx = hi; }
        } else {
            mx = mn;
        }
    }
    if (mn == -2) return atom; // no quantifier
    auto rep = std::make_unique<Node>();
    rep->k = K::Rep; rep->min = mn; rep->max = mx; rep->greedy = true;
    if (peek() == '?') { rep->greedy = false; pos_++; }
    else if (peek() == '+' || peek() == '!') { pos_++; } // ratchet/possessive: treat greedy
    rep->kids.push_back(std::move(atom));
    // separator quantifier:  X+ % Y  (Y between items)  /  X+ %% Y  (trailing Y allowed)
    skipWs();
    if (peek() == '%') {
        pos_++;
        if (peek() == '%') pos_++; // %% (trailing separator) — approximated as %
        skipWs();
        NodePtr sep = parseAtom();
        if (sigspace_) {
            // In a `rule`, whitespace around the separator is significant: `<x>* %% ','`
            // must match `a, b` (space after the comma). Wrap the separator in `\s*`.
            auto ws = [] { auto c = std::make_unique<Node>(); c->k = K::Class; c->classFlags = "s";
                           auto r = std::make_unique<Node>(); r->k = K::Rep; r->min = 0; r->max = -1; r->greedy = true;
                           r->kids.push_back(std::move(c)); return r; };
            auto seq = std::make_unique<Node>(); seq->k = K::Seq;
            seq->kids.push_back(ws()); seq->kids.push_back(std::move(sep)); seq->kids.push_back(ws());
            sep = std::move(seq);
        }
        rep->sep = std::move(sep);
    }
    return rep;
}

Regex::NodePtr Regex::parseAtom() {
    skipWs();
    char c = peek();
    if (c == '{') { // bare code block { … } — execute for side effects, zero-width
        int depth = 1; pos_++;
        std::string code;
        while (!eof() && depth > 0) { char d = pat_[pos_++]; if (d == '{') depth++; else if (d == '}') { depth--; if (!depth) break; } code += d; }
        auto cn = std::make_unique<Node>(); cn->k = K::Code; cn->lit = code; cn->runOnly = true; return cn;
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
    if (c == '$' && peek(1) == '<') {
        // named capture: $<name>=(...) / $<name>=[...]
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
            auto g = std::make_unique<Node>();
            g->k = K::Group; g->capIndex = -1; g->capName = name;
            g->kids.push_back(std::move(child));
            return g;
        }
        pos_ = save; // not a named capture
    }
    if (c == '(') {
        pos_++;
        int idx = ncaps_++;
        auto child = parseAlt();
        if (peek() == ')') pos_++;
        auto g = std::make_unique<Node>();
        g->k = K::Group; g->capIndex = idx;
        g->kids.push_back(std::move(child));
        return g;
    }
    if (c == '[') {
        pos_++;
        auto child = parseAlt();
        if (peek() == ']') pos_++;
        auto g = std::make_unique<Node>();
        g->k = K::Group; g->capIndex = -1;
        g->kids.push_back(std::move(child));
        return g;
    }
    if (c == '<') {
        pos_++;
        if (peek() == '(') { pos_++; auto n = std::make_unique<Node>(); n->k = K::CapStart; return n; } // <( match-capture start
        auto node = std::make_unique<Node>();
        node->k = K::Class;
        if (peek() == '[') { pos_++; node->negate = false; parseClassBodyMember(node.get()); }
        else if (peek() == '-' && peek(1) == '[') { pos_ += 2; node->negate = true; parseClassBodyMember(node.get()); }
        else if (peek() == '+' && peek(1) == '[') { pos_ += 2; node->negate = false; parseClassBodyMember(node.get()); }
        else if (peek() == '-' && (std::isalpha((unsigned char)peek(1)) || peek(1) == '.' || peek(1) == '_')) {
            // <-name> — negated subrule char class: one char NOT matched by rule `name`.
            // Equivalent to `[ <!name> . ]`.
            pos_++;
            if (peek() == '.') pos_++;
            std::string nm; while (!eof() && peek() != '>' && peek() != '(') nm += pat_[pos_++];
            std::string args;
            if (peek() == '(') { int d = 1; pos_++; while (!eof() && d > 0) { char x = pat_[pos_++]; if (x == '(') d++; else if (x == ')') { d--; if (!d) break; } args += x; } }
            if (peek() == '>') pos_++;
            auto sub = std::make_unique<Node>();
            sub->k = K::Subrule; sub->ruleName = nm; sub->ruleArgs = args; sub->ruleCapture = false;
            auto look = std::make_unique<Node>();
            look->k = K::Look; look->negate = true; look->behind = false;
            look->kids.push_back(std::move(sub));
            auto any = std::make_unique<Node>(); any->k = K::Any;
            auto seq = std::make_unique<Node>(); seq->k = K::Seq;
            seq->kids.push_back(std::move(look));
            seq->kids.push_back(std::move(any));
            return seq;
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
            // `before`/`after` may be followed by any whitespace (space, newline, tab),
            // not just a literal space — the inner pattern can start on the next line.
            auto kw = [&](const char* w, size_t n) {
                if (pat_.compare(pos_, n, w) != 0) return false;
                char after = pos_ + n < pat_.size() ? pat_[pos_ + n] : '\0';
                return after == ' ' || after == '\n' || after == '\t' || after == '\r';
            };
            if (kw("before", 6)) { pos_ += 6; skipWs(); }
            else if (kw("after", 5)) { pos_ += 5; behind = true; skipWs(); }
            else if (std::isalpha((unsigned char)peek()) || peek() == '_' || peek() == '.') {
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
            assertDepth_++;
            auto child = parseAlt();
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
            // named char class or subrule
            std::string name;
            while (!eof() && peek() != '>') name += pat_[pos_++];
            if (peek() == '>') pos_++;
            // a leading `.` (<.space>) means a non-capturing call; strip it for the
            // built-in class check so `<.space>` also resolves to `\s`.
            bool dotless = !name.empty() && name[0] == '.';
            if (dotless) name = name.substr(1);
            // Unicode property class: <:Nd> <:L> <:Alpha> <:!Upper> (codepoint-aware)
            if (!name.empty() && (name[0] == ':' || (name[0] == '!' && name.size() > 1 && name[1] == ':'))) {
                std::string p = name;
                if (p[0] == '!') { node->negate = true; p = p.substr(1); }
                p = p.substr(1); // drop ':'
                if (!p.empty() && p[0] == '!') { node->negate = !node->negate; p = p.substr(1); }
                node->uprop = p;
                return node;
            }
            // Undotted built-in char-class names compile to a class; a DOTTED call
            // (<.space>) is a rule call so a grammar's own `token space` wins (with a
            // built-in fallback in matchSub). `<word>` is never a built-in.
            std::string fl;
            if (!dotless) {
                if (name == "digit") fl = "d";
                else if (name == "alpha") fl = "a";
                else if (name == "alnum" || name == "ident") fl = "ad";
                else if (name == "space" || name == "blank") fl = "s";
                else if (name == "upper") fl = "u";
                else if (name == "lower") fl = "l";
                else if (name == "xdigit") fl = "x";
            }
            if (fl.empty()) {
                // subrule call <name> / <.name> / <name=other>
                auto sr = std::make_unique<Node>();
                sr->k = K::Subrule;
                sr->ruleCapture = !dotless; // <.name> is a non-capturing call
                std::string nm = name;
                auto eq = nm.find('=');
                if (eq != std::string::npos) { sr->ruleAlias = nm.substr(0, eq); nm = nm.substr(eq + 1); } // <alias=rule>
                // parameterised call <name($x, '')> — peel off the argument list
                auto lp = nm.find('(');
                if (lp != std::string::npos && nm.back() == ')') {
                    sr->ruleArgs = nm.substr(lp + 1, nm.size() - lp - 2);
                    nm = nm.substr(0, lp);
                }
                sr->ruleName = nm;
                return sr;
            }
            node->classFlags = fl;
            return node;
        }
        while (peek() == ' ' || peek() == '\t') pos_++; // allow `<[ … ] >` (space before close)
        if (peek() == '>') pos_++;
        return node;
    }
    if (c == '\'' || c == '"') {
        char q = c;
        pos_++;
        std::string lit;
        while (!eof() && peek() != q) {
            if (peek() == '\\' && pos_ + 1 < pat_.size()) {
                pos_++; char e = pat_[pos_++];
                switch (e) { case 'n': lit += '\n'; break; case 't': lit += '\t'; break;
                             case 'r': lit += '\r'; break; case '0': lit += '\0'; break; default: lit += e; }
            } else lit += pat_[pos_++];
        }
        if (peek() == q) pos_++;
        // a quoted multi-char literal: build a Seq of single-char Lits so quantifiers stay sane
        if (lit.size() == 1) { auto n = std::make_unique<Node>(); n->k = K::Lit; n->lit = lit; return n; }
        auto seq = std::make_unique<Node>(); seq->k = K::Seq;
        for (char ch : lit) { auto n = std::make_unique<Node>(); n->k = K::Lit; n->lit = std::string(1, ch); seq->kids.push_back(std::move(n)); }
        return seq;
    }
    if (c == '.') { pos_++; auto n = std::make_unique<Node>(); n->k = K::Any; return n; }
    if (c == '^') { pos_++; if (peek() == '^') pos_++; auto n = std::make_unique<Node>(); n->k = K::AnchorStart; return n; }
    if (c == '$') {
        // end anchor only when not an interpolation/backref
        char nx = peek(1);
        if (nx == '\0' || nx == ')' || nx == ']' || nx == '|' || std::isspace((unsigned char)nx)) {
            pos_++; if (peek() == '$') pos_++;
            auto n = std::make_unique<Node>(); n->k = K::AnchorEnd; return n;
        }
        // $var — match the variable's current Str value literally at match time
        pos_++;
        std::string var = "$";
        while (!eof()) {
            char p = peek();
            if (std::isalnum((unsigned char)p) || p == '_') { var += p; pos_++; }
            else if (p == '-' && (std::isalnum((unsigned char)peek(1)) || peek(1) == '_')) { var += p; pos_++; }
            else break;
        }
        auto vm = std::make_unique<Node>(); vm->k = K::VarMatch; vm->lit = var; return vm;
    }
    if (c == '\\') {
        pos_++;
        char e = peek(); pos_++;
        auto n = std::make_unique<Node>();
        if (e == 'd' || e == 'w' || e == 's') { n->k = K::Class; n->classFlags = std::string(1, e); return n; }
        if (e == 'D' || e == 'W' || e == 'S') { n->k = K::Class; n->classFlags = std::string(1, (char)std::tolower(e)); n->negate = true; return n; }
        // \N — any char except a logical newline (\n, \r). \h/\v — horizontal/vertical
        // whitespace (and \H/\V their negations).
        if (e == 'N') { n->k = K::Class; n->negate = true; n->ranges.push_back({'\n','\n'}); n->ranges.push_back({'\r','\r'}); return n; }
        if (e == 'h' || e == 'H') { n->k = K::Class; n->negate = (e=='H'); n->ranges.push_back({' ',' '}); n->ranges.push_back({'\t','\t'}); return n; }
        if (e == 'v' || e == 'V') { n->k = K::Class; n->negate = (e=='V'); n->ranges.push_back({'\n','\n'}); n->ranges.push_back({'\r','\r'}); n->ranges.push_back({'\f','\f'}); n->ranges.push_back({'\v','\v'}); return n; }
        if ((e == 'x' || e == 'o' || e == 'c') && (peek() == '[' || (e != 'c' && std::isalnum((unsigned char)peek())))) {
            // \xHH / \x[HH] / \o[OO] / \c[NAME] / \c[A, B] — codepoint literal(s)
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
            auto addCp = [&](const std::string& t) { int32_t cp = cpOf(t); if (cp >= 0) { auto lit = std::make_unique<Node>(); lit->k = K::Lit; lit->lit = encode((uint32_t)cp); seq->kids.push_back(std::move(lit)); } };
            if (peek() == '[') {
                pos_++; std::string body; while (!eof() && peek() != ']') body += pat_[pos_++]; if (peek() == ']') pos_++;
                for (size_t s = 0; s <= body.size(); ) { size_t cm = body.find(',', s); addCp(body.substr(s, cm == std::string::npos ? std::string::npos : cm - s)); if (cm == std::string::npos) break; s = cm + 1; }
            } else { std::string d; while (!eof() && std::isalnum((unsigned char)peek())) d += pat_[pos_++]; addCp(d); }
            if (seq->kids.empty()) { seq->k = K::Nop; return seq; }
            if (seq->kids.size() == 1) return std::move(seq->kids[0]);
            return seq;
        }
        n->k = K::Lit;
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
    n->k = K::Lit;
    n->lit = std::string(1, c);
    pos_++;
    return n;
}

// member: parse "<[ ... ]>" inner content (after the '[') into ranges/flags
void Regex::parseClassBodyMember(Node* node) {
    while (!eof() && peek() != ']') {
        if (std::isspace((unsigned char)peek())) { pos_++; continue; }
        if (peek() == '\\') {
            pos_++; char e = peek(); pos_++;
            if (e == 'd' || e == 'w' || e == 's') node->classFlags += e;
            else if (e == 'n') node->ranges.push_back({'\n', '\n'});
            else if (e == 't') node->ranges.push_back({'\t', '\t'});
            else if (e == 'r') node->ranges.push_back({'\r', '\r'});
            else if (e == 'x') { // \xHH or \x[HH] hex codepoint
                std::string hex;
                if (peek() == '[') { pos_++; while (!eof() && peek() != ']') hex += pat_[pos_++]; if (peek() == ']') pos_++; }
                else while (std::isxdigit((unsigned char)peek())) hex += pat_[pos_++];
                unsigned long cp = hex.empty() ? 0 : std::strtoul(hex.c_str(), nullptr, 16);
                node->ranges.push_back({(unsigned char)cp, (unsigned char)cp});
            }
            else if (e == 'c' && peek() == '[') { // \c[NAME] / \c[NAME1, NAME2] named char(s) (the ] is part of the escape)
                pos_++; std::string names; while (!eof() && peek() != ']') names += pat_[pos_++]; if (peek() == ']') pos_++;
                for (size_t s = 0; s <= names.size(); ) {
                    size_t comma = names.find(',', s);
                    std::string nm = names.substr(s, comma == std::string::npos ? std::string::npos : comma - s);
                    size_t a = nm.find_first_not_of(" \t"), b = nm.find_last_not_of(" \t");
                    nm = (a == std::string::npos) ? "" : nm.substr(a, b - a + 1);
                    if (!nm.empty()) {
                        int32_t cp = namedCp(nm);
                        if (cp >= 0) node->ranges.push_back({(unsigned char)cp, (unsigned char)cp});
                    }
                    if (comma == std::string::npos) break; s = comma + 1;
                }
            }
            else node->ranges.push_back({(unsigned char)e, (unsigned char)e}); // \: \# \- etc → literal
            continue;
        }
        unsigned char lo = (unsigned char)pat_[pos_++];
        if (peek() == '.' && peek(1) == '.') {
            pos_ += 2;
            while (std::isspace((unsigned char)peek())) pos_++;
            unsigned char hi = (unsigned char)pat_[pos_++];
            node->ranges.push_back({lo, hi});
        } else {
            node->ranges.push_back({lo, lo});
        }
    }
    if (peek() == ']') pos_++;
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

bool Regex::classMatch(const Node* n, char ch) const {
    // The per-byte result (ranges + flags + icase + negate) is pure per node —
    // build a 256-bit table on first use, then every test is one bit probe.
    if (!n->bytesetReady) {
        auto test = [&](unsigned char c) -> bool {
            for (auto& r : n->ranges) if (c >= r.first && c <= r.second) return true;
            for (char f : n->classFlags) {
                switch (f) {
                    case 'd': if (std::isdigit(c)) return true; break;
                    case 'w': if (std::isalnum(c) || c == '_') return true; break;
                    case 's': if (std::isspace(c)) return true; break;
                    case 'a': if (std::isalpha(c)) return true; break;
                    case 'u': if (std::isupper(c)) return true; break;
                    case 'l': if (std::islower(c)) return true; break;
                    case 'x': if (std::isxdigit(c)) return true; break;
                }
            }
            return false;
        };
        for (int i = 0; i < 8; i++) n->byteset[i] = 0;
        for (int v = 0; v < 256; v++) {
            unsigned char c = (unsigned char)v;
            bool in = test(c);
            if (!in && icase_) in = test((unsigned char)std::tolower(c)) || test((unsigned char)std::toupper(c));
            if (n->negate ? !in : in) n->byteset[v >> 5] |= (1u << (v & 31));
        }
        n->bytesetReady = true;
    }
    unsigned char c = (unsigned char)ch;
    return (n->byteset[c >> 5] >> (c & 31)) & 1;
}


bool Regex::rootIsSingleChar() const {
    if (!ok_ || !root_) return false;
    const Node* n = root_.get();
    return n->k == K::Class || n->k == K::Any || (n->k == K::Lit && n->lit.size() == 1);
}

long Regex::trySingleChar(const std::string& s, long pos) const {
    if (pos >= (long)s.size()) return -1;
    const Node* n = root_.get();
    if (n->k == K::Any) return s[pos] == '\n' ? -1 : pos + 1;
    if (n->k == K::Lit) return s[pos] == n->lit[0] ? pos + 1 : -1;
    return classMatch(n, s[pos]) ? pos + 1 : -1; // Class
}

std::pair<long, long> Regex::nodeWidth(const Node* n, MState& st) const {
    const long UNB = -1, CAP = 1000000000;
    switch (n->k) {
        case K::Lit: return {(long)n->lit.size(), (long)n->lit.size()};
        case K::Any: return {1, 1};
        case K::Class:
            // uprop and \s classes decode whole codepoints — up to 4 bytes
            if (!n->uprop.empty() || n->classFlags.find('s') != std::string::npos) return {1, 4};
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
        case K::Group: return nodeWidth(n->kids[0].get(), st);
        case K::AnchorStart: case K::AnchorEnd: case K::Nop: case K::Code: case K::Look: case K::CapStart:
            return {0, 0};
        case K::Subrule:
            if (st.grammar) {
                if (!n->metaCache) n->metaCache = &st.grammar->nameMeta(n->ruleName);
                if (n->metaCache->singleChar || !n->metaCache->builtinClass.empty()) return {1, 1};
            }
            return {0, UNB};
        case K::VarMatch: return {0, UNB};
    }
    return {0, UNB};
}

bool Regex::matchNode(const Node* n, MState& st, long pos, const FnRef& k) const {
    long len = (long)st.s.size();
    switch (n->k) {
        case K::Nop: return k(pos);
        case K::CapStart: { st.capFrom = pos; return k(pos); } // `<(`: zero-width, marks the .Str start
        case K::Look: {
            // zero-width assertion — match the inner in an isolated capture state
            const Node* child = n->kids.empty() ? nullptr : n->kids[0].get();
            if (!child) return k(pos);
            bool m = false;
            if (!n->behind) {
                MState sub{st.s, std::vector<std::pair<long, long>>(ncaps_, {-1, -1}), {}, {}, {}, st.resolver, st.grammar};
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
                    MState sub{st.s, std::vector<std::pair<long, long>>(ncaps_, {-1, -1}), {}, {}, {}, st.resolver, st.grammar};
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
                                            st, pos, k);
            }
            if (!st.resolver) {
                // built-in char-class fallback for <.alpha>/<.digit>/… with no grammar
                const std::string& nm = n->ruleName;
                std::string fl;
                if (nm == "alpha") fl = "a"; else if (nm == "digit") fl = "d";
                else if (nm == "alnum" || nm == "ident") fl = "ad";
                else if (nm == "space" || nm == "blank") fl = "s";
                else if (nm == "upper") fl = "u"; else if (nm == "lower") fl = "l";
                else if (nm == "xdigit") fl = "x"; else if (nm == "word") fl = "w";
                if (!fl.empty()) {
                    if (pos >= len) return false;
                    unsigned char c = st.s[pos]; bool ok = false;
                    for (char f : fl) ok = ok ||
                        (f == 'a' && std::isalpha(c)) || (f == 'd' && std::isdigit(c)) ||
                        (f == 's' && std::isspace(c)) || (f == 'u' && std::isupper(c)) ||
                        (f == 'l' && std::islower(c)) || (f == 'x' && std::isxdigit(c)) ||
                        (f == 'w' && (std::isalnum(c) || c == '_'));
                    return ok ? k(pos + 1) : false;
                }
                return k(pos); // unknown subrule: lenient zero-width
            }
            RxMatch sub;
            // pass a parameterised call's args to the resolver, encoded after \x1f
            std::string call = n->ruleArgs.empty() ? n->ruleName : (n->ruleName + "\x1f" + n->ruleArgs);
            if (!(*st.resolver)(call, st.s, pos, sub)) return false;
            if (n->ruleCapture && !n->ruleName.empty()) {
                auto savedN = st.named.count(n->ruleName) ? st.named[n->ruleName] : std::pair<long,long>{-1,-1};
                bool had = st.named.count(n->ruleName);
                st.named[n->ruleName] = {sub.from, sub.to};
                st.subs[n->ruleName] = {sub.from, sub.to};
                if (k(sub.to)) return true;
                if (had) st.named[n->ruleName] = savedN; else st.named.erase(n->ruleName);
                st.subs.erase(n->ruleName);
                return false;
            }
            return k(sub.to);
        }
        case K::Lit: {
            long m = (long)n->lit.size();
            if (pos + m > len) return false;
            for (long j = 0; j < m; j++) {
                char a = st.s[pos + j], b = n->lit[j];
                if (a != b && !(icase_ && std::tolower((unsigned char)a) == std::tolower((unsigned char)b))) return false;
            }
            return k(pos + m);
        }
        case K::Any:
            if (pos >= len) return false;
            return k(pos + 1);
        case K::Class:
            if (pos >= len) return false;
            if (!n->uprop.empty()) { // Unicode property class: decode one codepoint
                unsigned char c0 = (unsigned char)st.s[pos];
                if (c0 >= 0x80 && c0 < 0xC0) return false; // mid-codepoint continuation byte: no match here
                int clen = c0 < 0x80 ? 1 : (c0 >> 5) == 0x6 ? 2 : (c0 >> 4) == 0xe ? 3 : (c0 >> 3) == 0x1e ? 4 : 1;
                uint32_t cp = (c0 < 0x80 || clen == 1) ? c0 : (uint32_t)(c0 & (0xFF >> (clen + 1)));
                for (int i = 1; i < clen && pos + i < (long)len; i++) cp = (cp << 6) | ((unsigned char)st.s[pos + i] & 0x3F);
                bool m = uniMatchesProp(cp, n->uprop);
                if (n->negate) m = !m;
                if (!m) return false;
                return k(pos + clen);
            }
            // \s / \S against a multibyte codepoint (e.g. NBSP U+00A0): decode and
            // test Unicode whitespace, consuming the whole codepoint.
            {
                unsigned char c0 = (unsigned char)st.s[pos];
                if (c0 >= 0xC0 && n->ranges.empty() && n->classFlags == "s") {
                    int clen = (c0 >> 5) == 0x6 ? 2 : (c0 >> 4) == 0xe ? 3 : (c0 >> 3) == 0x1e ? 4 : 1;
                    uint32_t cp = (uint32_t)(c0 & (0xFF >> (clen + 1)));
                    for (int i = 1; i < clen && pos + i < (long)len; i++) cp = (cp << 6) | ((unsigned char)st.s[pos + i] & 0x3F);
                    bool m = isUnicodeSpace(cp);
                    if (n->negate) m = !m;
                    if (!m) return false;
                    return k(pos + clen);
                }
            }
            if (!classMatch(n, st.s[pos])) return false;
            return k(pos + 1);
        case K::AnchorStart:
            if (pos == 0 || (pos > 0 && st.s[pos - 1] == '\n')) return k(pos);
            return false;
        case K::AnchorEnd:
            if (pos == len || st.s[pos] == '\n') return k(pos);
            return false;
        case K::Seq: {
            auto go = [&](auto&& self, size_t i, long p) -> bool {
                if (i == n->kids.size()) return k(p);
                auto cont = [&, i](long np) { return self(self, i + 1, np); };
                return matchNode(n->kids[i].get(), st, p, cont);
            };
            return go(go, 0, pos);
        }
        case K::Code: { // <?{…}>/<!{…}> assertion, or `:my …`/bare `{…}` side-effect (runOnly)
            static const GrammarHooks::ParamMap noParams;
            const auto& params = st.grammar ? st.grammar->currentParams() : noParams;
            if (n->runOnly) { // execute for side effects, zero-width, always pass
                if (st.hooks && st.hooks->run) st.hooks->run(n->lit, st.startPos, pos, st.named, params);
                return k(pos);
            }
            bool ok = (st.hooks && st.hooks->assertPass) ? st.hooks->assertPass(n->lit, st.startPos, pos, st.named, params) : true;
            if (n->negate) ok = !ok;
            return ok ? k(pos) : false;
        }
        case K::VarMatch: { // `$var` in a pattern — match the variable's current Str value literally
            if (!st.hooks || !st.hooks->str) return false;
            static const GrammarHooks::ParamMap noParams;
            const auto& params = st.grammar ? st.grammar->currentParams() : noParams;
            std::string v = st.hooks->str(n->lit, st.named, params);
            if (pos + (long)v.size() > (long)st.s.size()) return false;
            if (st.s.compare(pos, v.size(), v) != 0) return false;
            return k(pos + (long)v.size());
        }
        case K::Alt: {
            if (n->firstMatch) { // `||` — try branches in order, first that satisfies k wins
                for (auto& kid : n->kids)
                    if (matchNode(kid.get(), st, pos, k)) return true;
                return false;
            }
            // `|` — longest-token match. True LTM needs each branch's set of reachable
            // ends; enumerating them (a trivial `return false` continuation) explodes
            // combinatorially on recursive grammars. So probe each branch ONCE for its
            // greedy end (cheap, single descent), snapshotting interpreter side-effects,
            // then commit branches in longest-end-first order with the real continuation.
            // The commit re-runs the winning branch so its captures are singular/live.
            std::shared_ptr<void> snap = (st.hooks && st.hooks->saveState) ? st.hooks->saveState() : nullptr;
            // The probe's `return true` continuation makes capture-setting nodes KEEP their
            // captures; snapshot the match-state containers so probing doesn't leak captures
            // into the commit (which would duplicate them into Arrays).
            auto savedCaps = st.caps; auto savedNamed = st.named;
            auto savedSubs = st.subs; auto savedChildren = st.children;
            std::vector<std::pair<long, size_t>> order; // (greedy end, branch index)
            for (size_t i = 0; i < n->kids.size(); i++) {
                long e0 = -1;
                matchNode(n->kids[i].get(), st, pos, [&](long e) { e0 = e; return true; });
                if (e0 >= 0) order.push_back({e0, i});
            }
            st.caps = std::move(savedCaps); st.named = std::move(savedNamed);
            st.subs = std::move(savedSubs); st.children = std::move(savedChildren);
            if (snap && st.hooks->restoreState) st.hooks->restoreState(snap);
            std::stable_sort(order.begin(), order.end(),
                             [](const auto& a, const auto& b) { return a.first > b.first; });
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
                static const GrammarHooks::ParamMap noParams;
                const auto& params = st.grammar ? st.grammar->currentParams() : noParams;
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
                if (greedy && ratchet_) {
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
                    if (cnt >= mn) return k(q);
                    return false;
                }
                if (greedy) {
                    if (mx < 0 || count < mx) {
                        auto more = [&](long np) { return np != p && self(self, count + 1, np); };
                        if (matchOne(p, more)) return true;
                    }
                    if (count >= mn) return k(p);
                    return false;
                } else {
                    if (count >= mn && k(p)) return true;
                    if (mx < 0 || count < mx) {
                        auto more = [&](long np) { return np != p && self(self, count + 1, np); };
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
                if (!cn.empty()) {
                    hadN = st.named.count(cn); if (hadN) savedN = st.named[cn]; st.named[cn] = {pos, np};
                    // also collate the occurrence (empty name = plain capture, not a rule),
                    // so a capture repeated under a quantifier yields a list like Rakudo's
                    ParseNode leaf; leaf.from = pos; leaf.to = np;
                    st.children[cn].push_back(std::move(leaf));
                }
                if (k(np)) return true;
                if (ci >= 0 && ci < (long)st.caps.size()) st.caps[ci] = savedC;
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

bool Regex::search(const std::string& subject, long startPos, RxMatch& out, const SubResolver& r) const {
    if (!ok_ || !root_) return false;
    for (long start = startPos; start <= (long)subject.size(); start++) {
        MState st{subject, std::vector<std::pair<long, long>>(ncaps_, {-1, -1}), {}, {}, {}, r ? &r : nullptr, nullptr};
        long endPos = -1;
        if (matchNode(root_.get(), st, start, [&](long e) { endPos = e; return true; })) {
            out.matched = true; out.from = st.capFrom >= 0 ? st.capFrom : start; out.to = endPos;
            out.caps = st.caps; out.named = st.named; out.subs = st.subs;
            out.children = st.children;
            return true;
        }
    }
    return false;
}

bool Regex::matchAt(const std::string& subject, long pos, RxMatch& out, const SubResolver& r) const {
    if (!ok_ || !root_) return false;
    MState st{subject, std::vector<std::pair<long, long>>(ncaps_, {-1, -1}), {}, {}, {}, r ? &r : nullptr, nullptr};
    long endPos = -1;
    if (matchNode(root_.get(), st, pos, [&](long e) { endPos = e; return true; })) {
        out.matched = true; out.from = st.capFrom >= 0 ? st.capFrom : pos; out.to = endPos;
        out.caps = st.caps; out.named = st.named; out.subs = st.subs;
        out.children = st.children;
        return true;
    }
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
    if (!nm.empty() && std::isdigit((unsigned char)nm[0])) return (int32_t)std::strtol(nm.c_str(), nullptr, 10);
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
    return scope_.empty() ? empty : scope_.back();
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
        if (c == '$' && angle == 0 && brace == 0 && i + 1 < pat.size() && (std::isalpha((unsigned char)pat[i + 1]) || pat[i + 1] == '_')) {
            size_t j = i + 1; while (j < pat.size() && (std::isalnum((unsigned char)pat[j]) || pat[j] == '_' || pat[j] == '-')) j++;
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
            std::string v = i < args.size() ? evalArg(args[i]) : std::string();
            key += '\x1f'; key += v;
            boundOut[rule.params[i]] = std::move(v);
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
    m.isWs = (name == "ws");
    if (!rule) { // built-in char-class fallbacks for names the grammar doesn't define
        if (name == "digit") m.builtinClass = "d"; else if (name == "alpha") m.builtinClass = "a";
        else if (name == "alnum" || name == "ident") m.builtinClass = "ad";
        else if (name == "space" || name == "blank") m.builtinClass = "s";
        else if (name == "upper") m.builtinClass = "u"; else if (name == "lower") m.builtinClass = "l";
        else if (name == "xdigit") m.builtinClass = "x";
    }
    return nameMeta_.emplace(name, std::move(m)).first->second;
}

bool GrammarMatcher::matchSub(const std::string& name, const std::string& args, const std::string& capKey,
                              Regex::MState& st, long pos, const FnRef& k) {
    return matchSubMeta(nameMeta(name), name, args, capKey, st, pos, k);
}

bool GrammarMatcher::matchSubMeta(const GrammarRuleMeta& meta, const std::string& name,
                                  const std::string& args, const std::string& capKey,
                                  Regex::MState& st, long pos, const FnRef& k) {
    // protoregex: `<element>` dispatches to its `element:<sym>` candidates, longest wins (LTM)
    if (meta.proto) {
        const auto& cands = *meta.proto;
        std::set<long, std::greater<long>> ends;
        for (auto& cand : cands) matchSub(cand, args, "", st, pos, [&](long e) { ends.insert(e); return false; });
        for (long e : ends)
            for (auto& cand : cands)
                if (matchSub(cand, args, capKey, st, pos, [&](long ee) { return ee == e && k(ee); })) return true;
        return false;
    }
    if (meta.isWs) { // built-in optional whitespace
        long p = pos; while (p < (long)st.s.size() && std::isspace((unsigned char)st.s[p])) p++;
        return k(p);
    }
    // built-in char-class rules (<.alpha> …) when the grammar doesn't redefine them
    if (!meta.rule) {
        const std::string& fl = meta.builtinClass;
        if (!fl.empty()) {
            if (pos >= (long)st.s.size()) return false;
            unsigned char c = (unsigned char)st.s[pos]; bool ok = false;
            for (char f : fl) switch (f) {
                case 'd': ok |= (bool)std::isdigit(c); break; case 'a': ok |= (bool)std::isalpha(c); break;
                case 's': ok |= (bool)std::isspace(c); break; case 'u': ok |= (bool)std::isupper(c); break;
                case 'l': ok |= (bool)std::islower(c); break; case 'x': ok |= (bool)std::isxdigit(c); break;
            }
            return ok ? k(pos + 1) : false;
        }
        return false; // unknown subrule
    }
    // Inline single-character rules (space, break, …): a bare char test, no memo/record
    // machinery. These are the overwhelming majority of subrule calls.
    if (meta.singleChar && args.empty()) {
        long np = meta.singleChar->trySingleChar(st.s, pos);
        if (np < 0) return false;
        if (capKey.empty()) return k(np);
        // capturing <name>: record a leaf node spanning the one char, then continue
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
    bool ratchet = meta.ratchet; // token/rule commit + memoize

    // Record a completed sub-match (span + subtree) under `capKey` in the caller frame,
    // then run the caller's continuation `k`; on failure, roll the recording back.
    // `kids` is a frozen subtree — sharing it is O(1), which is what makes memo replays cheap.
    auto record = [&](long end, const std::vector<std::pair<long,long>>& caps,
                      const std::map<std::string, std::pair<long,long>>& named,
                      std::shared_ptr<const ChildMap> kids) -> bool {
        if (capKey.empty()) return k(end);
        ParseNode pn; pn.name = name; pn.from = pos; pn.to = end;
        pn.caps = caps; pn.named = named; pn.kids = std::move(kids);
        bool hadSpan = st.named.count(capKey); auto savedSpan = hadSpan ? st.named[capKey] : std::pair<long, long>{-1, -1};
        st.named[capKey] = {pos, end};
        st.children[capKey].push_back(std::move(pn)); // collate repeated captures into a list
        if (k(end)) return true;
        st.children[capKey].pop_back();               // backtrack: drop this occurrence
        if (st.children[capKey].empty()) st.children.erase(capKey);
        if (hadSpan) st.named[capKey] = savedSpan; else st.named.erase(capKey);
        return false;
    };

    if (ratchet) {
        // Packrat: a ratchet token's match at (rule, params, pos) is deterministic. Serve
        // it from the memo when seen before; otherwise run once, committing to the first
        // (greedy/longest) complete match, and cache it. This is what makes recursive LTM
        // grammars run in polynomial rather than exponential time. Build an INTEGER key
        // and probe the memo BEFORE compiling/interpolating — the hot no-arg tokens
        // (space, alnum…) hit here and skip all string work.
        uint64_t mkey = (uint64_t)meta.id * 1099511628211ULL + (uint64_t)pos * 131ULL;
        if (!args.empty()) // fold param VALUES (not text) into the key — they vary by caller scope
            for (auto& a : splitArgs(args)) { for (char c : evalArg(a)) mkey = mkey * 131 + (unsigned char)c; mkey = mkey * 131 + 1; }
        auto mit = memo_.find(mkey);
        if (mit != memo_.end()) {
            if (!mit->second.matched) return false;
            const MemoEntry& me = mit->second;
            return record(me.end, me.caps, me.named, me.kids);
        }
        std::map<std::string, std::string> bound;
        Regex* re = (args.empty() && meta.noArg) ? meta.noArg
                  : compiledFor(*static_cast<const Rule*>(meta.rule), name, args, bound);
        if (!re || !re->ok()) { memo_[mkey].matched = false; return false; }
        Regex::MState sub{st.s, std::vector<std::pair<long, long>>(re->ncaps(), {-1, -1}), {}, {}, {}, nullptr, this};
        sub.startPos = pos; sub.hooks = st.hooks;
        scope_.push_back(std::move(bound));
        MemoEntry me;
        re->matchNode(re->root(), sub, pos, [&](long end) -> bool {
            me.matched = true; me.end = end;
            me.caps = sub.caps; me.named = sub.named;
            // the frame is committed (ratchet) — freeze its subtree without copying
            me.kids = sub.children.empty() ? nullptr
                    : std::make_shared<const ChildMap>(std::move(sub.children));
            return true; // commit to the first complete match — ratchet never backtracks in
        });
        scope_.pop_back();
        auto& slot = (memo_[mkey] = std::move(me));
        if (!slot.matched) return false;
        return record(slot.end, slot.caps, slot.named, slot.kids);
    }

    // non-ratchet `regex`: thread `k` through so the caller can backtrack into the callee
    std::map<std::string, std::string> bound;
    Regex* re = (args.empty() && meta.noArg) ? meta.noArg
              : compiledFor(*static_cast<const Rule*>(meta.rule), name, args, bound);
    if (!re || !re->ok()) return false;
    Regex::MState sub{st.s, std::vector<std::pair<long, long>>(re->ncaps(), {-1, -1}), {}, {}, {}, nullptr, this};
    sub.startPos = pos; sub.hooks = st.hooks; // $/ in the callee starts here; propagate hooks
    scope_.push_back(std::move(bound));
    bool ok = re->matchNode(re->root(), sub, pos, [&](long end) -> bool {
        // The callee has matched; the continuation `k` belongs to the CALLER, so its
        // code blocks must see the caller's params — pop the callee's param frame for
        // the duration of `k` (restore it so backtracking into the callee still works).
        auto calleeScope = std::move(scope_.back()); scope_.pop_back();
        auto finish = [&](bool r) { scope_.push_back(std::move(calleeScope)); return r; };
        // non-ratchet: the callee may complete again after backtracking, so its frame
        // stays live — freeze a COPY of the subtree for this completion
        return finish(record(end, sub.caps, sub.named,
                             sub.children.empty() ? nullptr : std::make_shared<const ChildMap>(sub.children)));
    });
    scope_.pop_back();
    return ok;
}

bool GrammarMatcher::parse(const std::string& input, const std::string& top, bool subparse,
                           ParseNode& out, long& endOut) {
    std::map<std::string, std::string> bound;
    Regex* re = compiled(top, "", bound);
    if (!re || !re->ok()) return false;
    clearMemo(); // packrat memo is valid only within a single input parse
    Regex::MState st{input, std::vector<std::pair<long, long>>(re->ncaps(), {-1, -1}), {}, {}, {}, nullptr, this};
    st.startPos = 0; st.hooks = &hooks; // top-level match starts at 0; wire the interpreter hooks
    long endPos = -1;
    scope_.push_back({});
    bool ok = re->matchNode(re->root(), st, 0, [&](long e) {
        if (!subparse && e != (long)input.size()) return false; // require a full match
        endPos = e; return true;
    });
    scope_.pop_back();
    if (!ok) return false;
    out.name = top; out.from = 0; out.to = endPos;
    out.caps = st.caps; out.named = st.named;
    out.kids = st.children.empty() ? nullptr : std::make_shared<const ChildMap>(std::move(st.children));
    endOut = endPos;
    return true;
}

} // namespace rakupp
