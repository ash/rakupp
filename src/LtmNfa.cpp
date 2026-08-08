// LtmNfa — declarative-prefix NFA builder + ranking runner (phase 1).
// See LtmNfa.h for the concept. The build walks Regex::Node (friend access);
// anything it does not understand conservatively ENDS the prefix, which can
// demote an alternative in the ranking but never break the final match —
// the commit phase still runs the real engine.
#include "LtmNfa.h"
#include "Regex.h"
#include <algorithm>
#include <cctype>

namespace rakupp {

// ---- UTF-8: decode one codepoint at s[pos]; returns its byte width -------
static int cpAt(const std::string& s, long pos, uint32_t& cp) {
    if (pos < 0 || pos >= (long)s.size()) return 0;
    unsigned char b = (unsigned char)s[pos];
    if (b < 0x80) { cp = b; return 1; }
    int w = (b >= 0xF0) ? 4 : (b >= 0xE0) ? 3 : (b >= 0xC0) ? 2 : 1;
    if (pos + w > (long)s.size()) { cp = b; return 1; }
    static const uint32_t mask[5] = {0, 0x7F, 0x1F, 0x0F, 0x07};
    cp = b & mask[w];
    for (int i = 1; i < w; i++) cp = (cp << 6) | ((unsigned char)s[pos + i] & 0x3F);
    return w;
}

int LtmNfa::addState() {
    states_.push_back({});
    return (int)states_.size() - 1;
}
int LtmNfa::addPred(Pred p) {
    preds_.push_back(p);
    return (int)preds_.size() - 1;
}

// ---- predicate evaluation -------------------------------------------------
// A Class-node predicate re-reads the node's OWN match data — byte ranges,
// codepoint ranges, d/w/s-style flags with uppercase negation, difference
// members, whole-class negation, ASCII :i. (uprop/cluster/imark classes are
// never built into predicates: the builder terminates the prefix on them.)
static bool flagHit(char f, uint32_t c) {
    switch (f) {
        case 'd': return c < 128 && std::isdigit((int)c);
        case 'w': return c < 128 && (std::isalnum((int)c) || c == '_');
        case 's': return c < 128 && std::isspace((int)c);
        case 'a': return c < 128 && std::isalpha((int)c);
        case 'u': return c < 128 && std::isupper((int)c);
        case 'l': return c < 128 && std::islower((int)c);
        case 'x': return c < 128 && std::isxdigit((int)c);
        case 'b': return c == ' ' || c == '\t';
        case 'n': return c == '\n';
        default:  return false;
    }
}
bool LtmNfa::classMatch(const void* nodeV, uint32_t c) {
    auto* n = static_cast<const Regex::Node*>(nodeV);
    uint32_t probe = c;
    if (n->icase && probe < 128) probe = (uint32_t)std::tolower((int)probe);
    bool in = false;
    for (auto& r : n->ranges) {
        uint32_t lo = r.first, hi = r.second;
        if (n->icase) { lo = lo < 128 ? (uint32_t)std::tolower((int)lo) : lo;
                        hi = hi < 128 ? (uint32_t)std::tolower((int)hi) : hi; }
        if (probe >= lo && probe <= hi) { in = true; break; }
        if (n->icase && c >= r.first && c <= r.second) { in = true; break; }
    }
    if (!in) for (auto& r : n->cpRanges)
        if (c >= r.first && c <= r.second) { in = true; break; }
    if (!in) for (char f : n->classFlags) {
        if (std::islower((unsigned char)f) ? flagHit(f, c) : !flagHit((char)std::tolower(f), c)) { in = true; break; }
    }
    for (char f : n->negClassFlags)
        if (flagHit((char)std::tolower(f), c)) { in = false; break; } // difference members subtract LAST
    return n->negate ? !in : in;
}
bool LtmNfa::predMatch(const Pred& p, uint32_t c) {
    if (p.isLit) {
        if (c == p.lit) return true;
        if (p.icase && c < 128 && p.lit < 128)
            return std::tolower((int)c) == std::tolower((int)p.lit);
        return false;
    }
    if (!p.node) return true; // `.` — Any matches every codepoint in Raku
    return classMatch(p.node, c);
}

// ---- construction ----------------------------------------------------------
// Returns the exit state, or -1 when the node TERMINATES the prefix — in
// which case an accept for `branch` has already been recorded at the point
// the prefix reached.
int LtmNfa::buildNode(const void* nv, int from, int branch, int litDepth, int depth) {
    using K = Regex::K;
    auto accept = [&](int st) { // the SPEC's prefix end — a fair ranking point
        if (states_[st].acceptBranch < 0) states_[st].acceptBranch = branch;
        return -1;
    };
    auto acceptGap = [&](int st) { // a MODEL gap — ranking may be unfair here
        anyGap_ = true;
        return accept(st);
    };
    if (depth > 200 || states_.size() > 4000) return acceptGap(from); // bound blown: unfair, not wrong
    if (!nv) return from;
    auto* n = static_cast<const Regex::Node*>(nv);
    switch (n->k) {
        case K::Nop:
        case K::CapStart:
        case K::CapEnd:
            return from;
        case K::AnchorStart: case K::AnchorEnd:
        case K::WBLeft: case K::WBRight:
            // zero-width: phase 1 treats them as ε — over-permissive for the
            // RANKING only; the commit engine enforces them for real
            return from;
        case K::Lit: {
            if (n->imark) return acceptGap(from);
            int cur = from;
            long i = 0;
            while (i < (long)n->lit.size()) {
                uint32_t cp; int w = cpAt(n->lit, i, cp);
                if (!w) break;
                if (n->icase && cp >= 128) return acceptGap(cur); // non-ASCII folding: not in phase 1
                int nxt = addState();
                states_[nxt].litDepth = ++litDepth;
                Pred p; p.isLit = true; p.lit = cp; p.icase = n->icase;
                states_[cur].edges.push_back({addPred(p), nxt});
                cur = nxt;
                i += w;
            }
            return cur;
        }
        case K::Any: case K::Class: {
            if (n->k == K::Class &&
                (!n->uprop.empty() || !n->clusterMembers.empty() || n->imark))
                return acceptGap(from); // needs machinery phase 1 does not model
            int nxt = addState();
            states_[nxt].litDepth = litDepth; // classes do not extend the LITERAL prefix
            Pred p; p.node = n->k == K::Class ? nv : nullptr;
            states_[from].edges.push_back({addPred(p), nxt});
            return nxt;
        }
        case K::Seq: {
            int cur = from;
            for (auto& kid : n->kids) {
                cur = buildNode(kid.get(), cur, branch, states_[cur].litDepth, depth + 1);
                if (cur < 0) return -1; // terminated inside: accept already placed
            }
            return cur;
        }
        case K::Group:
            return n->kids.empty() ? from
                 : buildNode(n->kids[0].get(), from, branch, litDepth, depth + 1);
        case K::Alt: {
            if (n->firstMatch) // `||`: only the FIRST branch contributes (S05)
                return n->kids.empty() ? from
                     : buildNode(n->kids[0].get(), from, branch, litDepth, depth + 1);
            int join = -1;
            for (auto& kid : n->kids) {
                int e = buildNode(kid.get(), from, branch, litDepth, depth + 1);
                if (e < 0) continue;              // that path's prefix ended inside
                if (join < 0) join = addState();
                states_[e].eps.push_back({join, 0});
                if (states_[join].litDepth < states_[e].litDepth)
                    states_[join].litDepth = states_[e].litDepth;
            }
            return join; // -1 if every path terminated (accepts are placed)
        }
        case K::Conj:
            return acceptGap(from); // `&` — not worth NFA intersection (plan)
        case K::Rep: {
            if (!n->repCode.empty()) return accept(from); // runtime bounds end the prefix
            const Regex::Node* body = n->kids.empty() ? nullptr : n->kids[0].get();
            if (!body) return from;
            long mn = n->min, mx = n->max;
            if (mn > 8) mn = 8;                    // cap the unroll; shorter prefix is safe
            int cur = from;
            for (long i = 0; i < mn; i++) {
                if (i > 0 && n->sep) {
                    cur = buildNode(n->sep.get(), cur, branch, states_[cur].litDepth, depth + 1);
                    if (cur < 0) return -1;
                }
                cur = buildNode(body, cur, branch, states_[cur].litDepth, depth + 1);
                if (cur < 0) return -1;
            }
            if (mx < 0) { // unbounded tail: one ε-looped copy (sep included)
                int loopIn = cur;
                int e;
                if (n->sep) {
                    e = buildNode(n->sep.get(), loopIn, branch, states_[loopIn].litDepth, depth + 1);
                    if (e >= 0) e = buildNode(body, e, branch, states_[e].litDepth, depth + 1);
                }
                else e = buildNode(body, loopIn, branch, states_[loopIn].litDepth, depth + 1);
                if (e >= 0) states_[e].eps.push_back({loopIn, 0});
            }
            else if (mx > n->min) { // bounded optional tail, capped like the unroll
                long extra = mx - n->min; if (extra > 8) extra = 8;
                int join = addState();
                states_[cur].eps.push_back({join, 0});
                int c2 = cur;
                for (long i = 0; i < extra; i++) {
                    if (n->sep) {
                        c2 = buildNode(n->sep.get(), c2, branch, states_[c2].litDepth, depth + 1);
                        if (c2 < 0) break;
                    }
                    c2 = buildNode(body, c2, branch, states_[c2].litDepth, depth + 1);
                    if (c2 < 0) break;
                    states_[c2].eps.push_back({join, 0});
                }
                cur = join;
            }
            return cur;
        }
        case K::Code:
            if (n->ltmStop) return accept(from);   // a bare {…} ends the prefix
            if (n->runOnly) return from;           // :my etc. — transparent to LTM
            return accept(from);                   // <?{…}>/<!{…}> — a spec prefix end
        case K::VarMatch: // a back-reference IS the spec's prefix end
            return accept(from);
        case K::Subrule: { // phase 3: inline the callee's declarative prefix
            // recursion IS the spec's prefix end (a rule already being
            // expanded ends the prefix there, as in Rakudo)
            for (auto& nm : expandStack_)
                if (nm == n->ruleName) return accept(from);
            if (!buildHooks_ || !buildHooks_->namedRule || !n->ruleArgs.empty())
                return acceptGap(from); // parameterized / no resolution: a gap
            std::string text, flags;
            if (!buildHooks_->namedRule(n->ruleName, text, flags))
                return acceptGap(from); // builtins, protos, qualified names, rules
            auto callee = std::make_unique<Regex>(text, flags);
            if (!callee->ok()) return acceptGap(from);
            expandStamps_[n->ruleName] = text;
            expandStack_.push_back(n->ruleName);
            int e = buildNode(callee->root_.get(), from, branch, litDepth, depth + 1); // friend access
            expandStack_.pop_back();
            owned_.push_back(std::move(callee)); // the NFA borrows its Nodes: keep it alive
            return e; // -1 propagates: the callee's own prefix end was recorded
        }
        case K::Look:     // conservative (Rakudo is subtler); a gap for now
        default:
            return acceptGap(from);
    }
}

std::unique_ptr<LtmNfa> LtmNfa::buildForAlt(const Regex& re, const void* altNode,
                                            const GrammarHooks* hooks) {
    auto* alt = static_cast<const Regex::Node*>(altNode);
    if (!alt || alt->k != Regex::K::Alt) return nullptr;
    std::unique_ptr<LtmNfa> nfa(new LtmNfa());
    nfa->owner_ = &re;
    nfa->buildHooks_ = hooks;
    nfa->nBranches_ = (int)alt->kids.size();
    nfa->addState(); // state 0 = entry
    for (int b = 0; b < (int)alt->kids.size(); b++) {
        // each branch gets its OWN entry state, ε-linked from state 0 — if
        // branches build directly from the shared entry, a sibling's Rep
        // loop-back through it re-anchors every other branch mid-string
        // (found by the harness on `a* | aa` over "aaa": branch 1 reported
        // a prefix end of 3)
        int entry = nfa->addState();
        nfa->states_[0].eps.push_back({entry, 0});
        int e = nfa->buildNode(alt->kids[b].get(), entry, b, 0, 0);
        if (e >= 0) nfa->states_[e].acceptBranch = b; // fully declarative: accept at the end
    }
    nfa->buildHooks_ = nullptr; // build-time only; rank() never resolves anything
    return nfa;
}

bool LtmNfa::stillValid(const GrammarHooks* hooks) const {
    if (expandStamps_.empty()) return true; // nothing expanded: always valid
    if (!hooks || !hooks->namedRule) return false;
    for (auto& kv : expandStamps_) {
        std::string text, flags;
        if (!hooks->namedRule(kv.first, text, flags) || text != kv.second)
            return false; // the name resolves differently now: rebuild
    }
    return true;
}

// ---- the ranking runner -----------------------------------------------------
std::vector<LtmNfa::Ranked> LtmNfa::rank(const std::string& s, long pos) const {
    const int N = (int)states_.size();
    std::vector<char> cur(N, 0), nxt(N, 0);
    std::vector<long> bestEnd(nBranches_, -1), bestLit(nBranches_, 0);
    // ε-closure of a set, recording accepts at position `at`
    auto close = [&](std::vector<char>& set, long at) {
        std::vector<int> work;
        for (int i = 0; i < N; i++) if (set[i]) work.push_back(i);
        while (!work.empty()) {
            int st = work.back(); work.pop_back();
            int b = states_[st].acceptBranch;
            if (b >= 0 && (at > bestEnd[b] ||
                           (at == bestEnd[b] && states_[st].litDepth > bestLit[b]))) {
                bestEnd[b] = at; bestLit[b] = states_[st].litDepth;
            }
            for (auto& e : states_[st].eps)
                if (!set[e.first]) { set[e.first] = 1; work.push_back(e.first); }
        }
    };
    cur[0] = 1;
    close(cur, pos);
    long p = pos;
    while (p < (long)s.size()) {
        uint32_t cp; int w = cpAt(s, p, cp);
        if (!w) break;
        std::fill(nxt.begin(), nxt.end(), 0);
        bool any = false;
        for (int i = 0; i < N; i++) {
            if (!cur[i]) continue;
            for (auto& e : states_[i].edges)
                if (!nxt[e.second] && predMatch(preds_[e.first], cp)) { nxt[e.second] = 1; any = true; }
        }
        if (!any) break;
        p += w;
        cur.swap(nxt);
        close(cur, p);
    }
    std::vector<Ranked> out;
    for (int b = 0; b < nBranches_; b++)
        if (bestEnd[b] >= 0) out.push_back({b, bestEnd[b], bestLit[b]});
    std::stable_sort(out.begin(), out.end(), [](const Ranked& a, const Ranked& b) {
        if (a.prefixEnd != b.prefixEnd) return a.prefixEnd > b.prefixEnd;
        if (a.litPrefix != b.litPrefix) return a.litPrefix > b.litPrefix;
        return a.branch < b.branch; // declaration order
    });
    return out;
}

} // namespace rakupp
