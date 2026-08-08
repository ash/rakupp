#pragma once
// LtmNfa — the declarative-prefix automaton for Longest-Token Matching
// (v3 LTM pillar, phase 1: builder + offline harness; LTM-PLAN.md).
//
// WHAT THIS IS. Raku's `|` alternation and protoregex dispatch pick their
// winner by LONGEST DECLARATIVE PREFIX — the part of each alternative made
// of literals, character classes and quantifiers, up to the first
// procedural construct (a code block, a back-reference, …). Ranking by
// running each branch (the probe approach this replaces) both ranks by the
// WRONG length (greedy full-match end, not declarative-prefix end) and
// RUNS USER CODE during candidate selection. An NFA cannot execute code —
// it can only answer "how far could this alternative's prefix reach?" —
// which makes it structurally the right ranking oracle.
//
// SHAPE. One Thompson NFA per alternation, built lazily from the compiled
// Regex::Node tree and cached. Transitions are codepoint predicates that
// REUSE the Class node's own match data (byte ranges, cpRanges, uprop,
// negation, folding) — no second Unicode implementation. Construction
// stops at prefix terminators (Code with ltmStop, lookarounds,
// back-references, `||` tails, parameterized subrules, `&` conjunction,
// unbounded runtime bounds) by marking an accept; ANY construct the
// builder does not handle also just ends the prefix — a shorter prefix
// can demote an alternative in the ranking but can never make the final
// match wrong, because the COMMIT phase still runs the real engine.
//
// Phase 1 is an ORACLE HARNESS ONLY: nothing consults the NFA's answer
// yet. RAKUPP_LTM_DEBUG=1 makes the Alt probe path rank BOTH ways and
// print disagreements for classification against Rakudo.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace rakupp {

class Regex;

class LtmNfa {
public:
    // Build the union NFA for an Alt node's branches. Every branch gets an
    // entry ε-edge from state 0 and its accepts tagged with its index.
    // Returns null only on allocation-level failure; an unbuildable branch
    // simply contributes an accept-at-entry (empty prefix, ranks last).
    static std::unique_ptr<LtmNfa> buildForAlt(const Regex& re, const void* altNode);

    struct Ranked {
        int branch;        // index into the Alt's kids
        long prefixEnd;    // furthest input position its declarative prefix reached
        long litPrefix;    // literal-only edge count along the best path (tie-break)
    };
    // One linear scan of `s` from `pos`; no code execution, no allocation
    // after warm-up (state sets are reused). Sorted by (prefixEnd desc,
    // litPrefix desc, declaration order) — the S05 tie-break chain.
    std::vector<Ranked> rank(const std::string& s, long pos) const;

    int states() const { return (int)preds_.size(); }
    // True when some branch's prefix ended at a construct phase 2 does not
    // MODEL (subrule call, :m literal, uprop/cluster class, non-ASCII :i,
    // lookaround, conjunction, a blown build bound) — as opposed to a
    // construct that IS the spec's prefix end (code block, <?{}>,
    // back-reference, || tail, runtime bounds). A gap means the ranking
    // may unfairly demote that branch, so the caller should fall back to
    // the probe for this alternation until expansion lands (phase 3).
    bool anyModelGap() const { return anyGap_; }

private:
    LtmNfa() = default;
    friend class Regex;

    // A transition predicate: matches exactly like the Class/Lit node it
    // was built from. `node` borrows the Regex's Node (the Regex outlives
    // its cached NFA — same lifetime the byteset cache already relies on).
    struct Pred {
        const void* node = nullptr;   // Regex::Node* — class-style predicate
        uint32_t lit = 0;             // or a single literal codepoint (node == null)
        bool isLit = false;
        bool icase = false;
    };
    struct State {
        std::vector<std::pair<int, int>> eps;   // ε-edges (to-state, unused)
        std::vector<std::pair<int, int>> edges; // (pred index, to-state)
        int acceptBranch = -1;                  // >=0: this state accepts for that branch
        int litDepth = 0;                       // literal-edge count from entry (for the tie-break)
    };
    std::vector<Pred> preds_;
    std::vector<State> states_;
    int nBranches_ = 0;
    bool anyGap_ = false;
    const Regex* owner_ = nullptr;

    int addState();
    int addPred(Pred p);
    static bool predMatch(const Pred& p, uint32_t c);
    static bool classMatch(const void* node, uint32_t c); // reads Regex::Node fields (friend)
    // Thompson construction over the declarative prefix of `n`; returns the
    // exit state, or -1 when `n` TERMINATES the prefix (caller marks accept).
    int buildNode(const void* n, int from, int branch, int litDepth, int depth);
};

} // namespace rakupp
