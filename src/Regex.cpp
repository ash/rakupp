#include "Regex.h"
#include "Unicode.h"
#include <cctype>

namespace rakupp {

Regex::Regex(const std::string& pattern, const std::string& flags) : pat_(pattern) {
    for (char f : flags) {
        if (f == 'i') icase_ = true;
        if (f == 's') sigspace_ = true;
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
    auto first = parseSeq();
    if (peek() != '|') return first;
    auto alt = std::make_unique<Node>();
    alt->k = K::Alt;
    alt->kids.push_back(std::move(first));
    while (peek() == '|') {
        pos_++;
        if (peek() == '|') pos_++; // treat || like |
        alt->kids.push_back(parseSeq());
    }
    return alt;
}

Regex::NodePtr Regex::parseSeq() {
    auto seq = std::make_unique<Node>();
    seq->k = K::Seq;
    for (;;) {
        skipWs();
        char c = peek();
        if (eof() || c == '|' || c == ')' || c == ']') break;
        seq->kids.push_back(parseQuant());
    }
    if (seq->kids.size() == 1) return std::move(seq->kids[0]);
    return seq;
}

Regex::NodePtr Regex::parseQuant() {
    auto atom = parseAtom();
    skipWs();
    char c = peek();
    long mn = -2, mx = -2;
    if (c == '*' && peek(1) != '*') { pos_++; mn = 0; mx = -1; }
    else if (c == '+') { pos_++; mn = 1; mx = -1; }
    else if (c == '?') { pos_++; mn = 0; mx = 1; }
    if (mn == -2 && peek() == '*' && peek(1) == '*') {
        pos_ += 2; skipWs();
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
    return rep;
}

Regex::NodePtr Regex::parseAtom() {
    skipWs();
    char c = peek();
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
        auto node = std::make_unique<Node>();
        node->k = K::Class;
        if (peek() == '[') { pos_++; node->negate = false; parseClassBodyMember(node.get()); }
        else if (peek() == '-' && peek(1) == '[') { pos_ += 2; node->negate = true; parseClassBodyMember(node.get()); }
        else if (peek() == '+' && peek(1) == '[') { pos_ += 2; node->negate = false; parseClassBodyMember(node.get()); }
        else if (peek() == '?' || peek() == '!' || peek() == '.' || peek() == '&' || peek() == '{') {
            // assertion / subrule / code — unsupported: zero-width no-op
            int depth = 1; pos_++;
            while (!eof() && depth > 0) { char d = pat_[pos_++]; if (d == '<') depth++; else if (d == '>') depth--; }
            auto nop = std::make_unique<Node>(); nop->k = K::Nop; return nop;
        } else {
            // named char class or subrule
            std::string name;
            while (!eof() && peek() != '>') name += pat_[pos_++];
            if (peek() == '>') pos_++;
            // Unicode property class: <:Nd> <:L> <:Alpha> <:!Upper> (codepoint-aware)
            if (!name.empty() && (name[0] == ':' || (name[0] == '!' && name.size() > 1 && name[1] == ':'))) {
                std::string p = name;
                if (p[0] == '!') { node->negate = true; p = p.substr(1); }
                p = p.substr(1); // drop ':'
                if (!p.empty() && p[0] == '!') { node->negate = !node->negate; p = p.substr(1); }
                node->uprop = p;
                return node;
            }
            std::string fl;
            if (name == "digit") fl = "d";
            else if (name == "alpha") fl = "a";
            else if (name == "alnum" || name == "ident") fl = "ad";
            else if (name == "space" || name == "blank") fl = "s";
            else if (name == "upper") fl = "u";
            else if (name == "lower") fl = "l";
            else if (name == "xdigit") fl = "x";
            else if (name == "word") fl = "w";
            else {
                // subrule call <name> / <.name> / <name=other>
                auto sr = std::make_unique<Node>();
                sr->k = K::Subrule;
                sr->ruleCapture = true;
                std::string nm = name;
                if (!nm.empty() && nm[0] == '.') { sr->ruleCapture = false; nm = nm.substr(1); }
                auto eq = nm.find('=');
                if (eq != std::string::npos) nm = nm.substr(eq + 1); // <alias=rule> -> call rule
                sr->ruleName = nm;
                return sr;
            }
            node->classFlags = fl;
            return node;
        }
        if (peek() == '>') pos_++;
        return node;
    }
    if (c == '\'' || c == '"') {
        char q = c;
        pos_++;
        std::string lit;
        while (!eof() && peek() != q) { if (peek() == '\\') pos_++; lit += pat_[pos_++]; }
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
        // $var / $0 interpolation: unsupported -> skip token, zero-width
        pos_++;
        while (!eof() && (std::isalnum((unsigned char)peek()) || peek() == '_' || peek() == '<' || peek() == '>')) pos_++;
        auto nop = std::make_unique<Node>(); nop->k = K::Nop; return nop;
    }
    if (c == '\\') {
        pos_++;
        char e = peek(); pos_++;
        auto n = std::make_unique<Node>();
        if (e == 'd' || e == 'w' || e == 's') { n->k = K::Class; n->classFlags = std::string(1, e); return n; }
        if (e == 'D' || e == 'W' || e == 'S') { n->k = K::Class; n->classFlags = std::string(1, (char)std::tolower(e)); n->negate = true; return n; }
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
            else node->ranges.push_back({(unsigned char)e, (unsigned char)e});
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

bool Regex::classMatch(const Node* n, char ch) const {
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
    unsigned char c = (unsigned char)ch;
    bool in = test(c);
    if (!in && icase_) in = test((unsigned char)std::tolower(c)) || test((unsigned char)std::toupper(c));
    return n->negate ? !in : in;
}

bool Regex::matchNode(const Node* n, MState& st, long pos, const std::function<bool(long)>& k) const {
    long len = (long)st.s.size();
    switch (n->k) {
        case K::Nop: return k(pos);
        case K::Subrule: {
            if (!st.resolver) return k(pos); // no grammar context: lenient zero-width
            RxMatch sub;
            if (!(*st.resolver)(n->ruleName, st.s, pos, sub)) return false;
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
            if (!classMatch(n, st.s[pos])) return false;
            return k(pos + 1);
        case K::AnchorStart:
            if (pos == 0 || (pos > 0 && st.s[pos - 1] == '\n')) return k(pos);
            return false;
        case K::AnchorEnd:
            if (pos == len || st.s[pos] == '\n') return k(pos);
            return false;
        case K::Seq: {
            std::function<bool(size_t, long)> go = [&](size_t i, long p) -> bool {
                if (i == n->kids.size()) return k(p);
                return matchNode(n->kids[i].get(), st, p, [&, i](long np) { return go(i + 1, np); });
            };
            return go(0, pos);
        }
        case K::Alt:
            for (auto& kid : n->kids)
                if (matchNode(kid.get(), st, pos, k)) return true;
            return false;
        case K::Rep: {
            const Node* child = n->kids[0].get();
            long mn = n->min, mx = n->max; bool greedy = n->greedy;
            std::function<bool(long, long)> rep = [&](long count, long p) -> bool {
                if (greedy) {
                    if (mx < 0 || count < mx)
                        if (matchNode(child, st, p, [&](long np) { return np != p && rep(count + 1, np); })) return true;
                    if (count >= mn) return k(p);
                    return false;
                } else {
                    if (count >= mn && k(p)) return true;
                    if (mx < 0 || count < mx)
                        return matchNode(child, st, p, [&](long np) { return np != p && rep(count + 1, np); });
                    return false;
                }
            };
            return rep(0, pos);
        }
        case K::Group: {
            const Node* child = n->kids[0].get();
            int ci = n->capIndex;
            return matchNode(child, st, pos, [&](long np) -> bool {
                if (ci >= 0 && ci < (long)st.caps.size()) {
                    auto saved = st.caps[ci];
                    st.caps[ci] = {pos, np};
                    if (k(np)) return true;
                    st.caps[ci] = saved;
                    return false;
                }
                return k(np);
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
        MState st{subject, std::vector<std::pair<long, long>>(ncaps_, {-1, -1}), {}, {}, r ? &r : nullptr};
        long endPos = -1;
        if (matchNode(root_.get(), st, start, [&](long e) { endPos = e; return true; })) {
            out.matched = true; out.from = start; out.to = endPos;
            out.caps = st.caps; out.named = st.named; out.subs = st.subs;
            return true;
        }
    }
    return false;
}

bool Regex::matchAt(const std::string& subject, long pos, RxMatch& out, const SubResolver& r) const {
    if (!ok_ || !root_) return false;
    MState st{subject, std::vector<std::pair<long, long>>(ncaps_, {-1, -1}), {}, {}, r ? &r : nullptr};
    long endPos = -1;
    if (matchNode(root_.get(), st, pos, [&](long e) { endPos = e; return true; })) {
        out.matched = true; out.from = pos; out.to = endPos;
        out.caps = st.caps; out.named = st.named; out.subs = st.subs;
        return true;
    }
    return false;
}

} // namespace rakupp
