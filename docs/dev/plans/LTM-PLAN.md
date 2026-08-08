# Plan: true Longest-Token Matching (LTM)

**Status: planned, not started.** One of the three **v3.0.0** pillars
([VERSIONS.md](VERSIONS.md); the others are [CLI-PLAN.md](CLI-PLAN.md) and
[PARALLEL-PLAN.md](PARALLEL-PLAN.md)), and the
one the pre-v3 discussion ranked as the tallest of the "real walls"
([100.md](100.md)). Goal: replace the current probe-and-rank approximation
of LTM with the real thing — a side-effect-free automaton over each
alternative's *declarative prefix* — so `|` alternation and protoregex
dispatch rank candidates the way the Raku spec defines, never execute user
code while ranking, and stay fast.

Measurements: 2026-08-07, `build-arm64/rakupp` at v2.0.0 vs Rakudo 2026.07
(the oracle).

## What LTM is

Most regex engines treat `a | b` *sequentially*: try `a`, and only if it
fails anywhere, try `b`. Raku has that operator too — it is spelled `||`.
But plain `|` means something different: **the alternative that matches the
longest token wins**, regardless of the order the alternatives were written
in. It is the same rule a tokenizer uses ("maximal munch"): when the input
is `++`, a lexer with tokens `+` and `++` must pick `++`, no matter which
was declared first.

Why the language is defined this way: **grammars compose**. A Raku grammar
can be extended from outside — add one more `token statement:sym<if>`
candidate to a protoregex and the dispatcher picks the right branch by what
the *input* looks like, not by declaration order. That is what makes slangs,
grammar inheritance, and Raku's own self-hosted grammar workable: nobody has
to hand-order thousands of alternatives. LTM applies in three places:

- `|` alternation inside a regex,
- protoregex dispatch (`<term>` trying every `term:sym<…>` candidate),
- implicitly, alternations produced by interpolating an array (`/@ops/`).

**The declarative prefix.** "Longest token" is deliberately not "longest
match". Each alternative is split into a *declarative* prefix — the leading
part built only of things a finite automaton can decide: literals, character
classes, quantifiers over them, and subrule calls whose own bodies are
declarative — and a *procedural* tail, which starts at the first construct
that needs the general engine or the interpreter. Per S05, the prefix ends
at: an embedded code block `{…}`, a code assertion `<?{…}>`, a back-reference,
a sequential alternation `||`, or a recursive subrule call. Ranking compares
**only the declarative prefixes**. Two consequences that surprise people:

1. `/ [ ab {} cd ] | abc /` on `"abcd"`: the first alternative can match
   four characters, but its declarative prefix is just `ab` (the `{}` ends
   it). `abc`'s prefix is three long. **`abc` wins** — Rakudo confirms.
2. Ranking must run **no user code**: the `{}` blocks of losing (or even
   winning) alternatives do not execute during candidate selection.

**Tie-breaking** (S05): longest declarative prefix first; on a tie, the
candidate with the longer *literal* prefix beats one that got there with
open character classes; then the more-derived grammar (a child grammar's
candidate beats its parent's); finally, textual declaration order.

**How Rakudo implements it**: each alternative's declarative prefix is
compiled once into an NFA; the NFAs of all alternatives are merged and run
*in parallel* over the input in a single scan — no backtracking, no
captures, no code — producing the list of viable candidates ranked by how
far each got. Then the full alternatives (procedural tails and all) are
tried in that order with the real engine. The NFA is a *ranking oracle*, not
the matcher.

## Where rakupp is today — honest, measured

The engine (src/Regex.cpp) fakes LTM two different ways:

- **`|` alternation** (`case K::Alt`, the non-`firstMatch` branch): probe
  each branch once for its *greedy full-match end* — executing the branch,
  code blocks included, under a snapshot/rollback of `:my` vars and deferred
  `make`s — then sort branches by that end and commit. Two divergences
  fall straight out of the design, both confirmed against the oracle today:
  - `"abcd" ~~ / [ ab {} cd ] | abc /` → rakupp `abcd`, Rakudo `abc`
    (we rank by full greedy end; LTM ranks by declarative-prefix end);
  - `my $n = 0; "x" ~~ / [ {$n++} y ] | x /` → rakupp leaves `$n == 1`,
    Rakudo `0` (our probe runs user code; the snapshot covers interpreter
    state, not user variables — and cannot cover I/O at all).
- **Protoregex dispatch** (Interpreter.cpp `matchSub`, `meta.proto`): closer
  to the truth — candidates are ranked by `candDeclEnd_` (the span matched
  before the first bare code block; the `ltmStop` flag on Code nodes exists
  precisely for this) with `candLitPrefix_` as the tie-break and declaration
  order as the stable fallback. But the measurement is made by *executing
  each candidate* as a probe, so it shares the run-user-code problem and
  costs a full descent per candidate per position (softened by the packrat
  memo for `token`/`rule`).

Roast standing: `S05-metasyntax/longest-alternative.t` **45/62**,
`S05-grammar/proto-token-ltm.t` **10/10**. (The raw longest-alternative file
does not compile under unfudged Rakudo either — it uses NYI `::` prefix
declarations — so the target is "everything fudged Rakudo passes", per the
usual counting rules.) The ROADMAP's deferred item "regex code blocks under
backtracking" is the same family: blocks re-run when the engine revisits a
position. This plan fixes the *ranking-time* executions; the
backtracking-time re-runs are adjacent work that the new machinery makes
easier (see Phases).

What must not be lost: the engine is currently *fast* — a 60-key JSON
grammar parsed 20 times takes 31 ms under rakupp vs 147 ms under Rakudo
(measured today) — largely thanks to the ratchet packrat memo and the
possessive-repetition path. LTM must land without giving that back.

## The design

### 1. `LtmNfa` — a declarative-prefix automaton per alternative

A new, small module (`src/LtmNfa.{h,cpp}`) that walks a compiled
`Regex::Node` tree and builds a Thompson NFA of its declarative prefix:

- **Transitions** are codepoint predicates, not alphabet sets: a transition
  carries the same match data a `Class` node already has (byte ranges,
  `cpRanges`, `uprop`, negation, `icase`/`imark` folding via the existing
  `foldCpPush` tables). No new Unicode machinery; the NFA *reuses* the
  class matcher's decision logic.
- **Construction** handles: `Lit` (a chain of codepoint edges, case-folded
  under `:i`), `Class`, `Any`, `Seq`, `Group`/captures (transparent —
  captures don't exist at ranking time), `Rep` (standard Thompson loops;
  `**{code}` runtime bounds end the prefix), nested `|` (union), `&`
  conjunction (ends the prefix — rare and not worth NFA intersection),
  anchors/word boundaries (zero-width predicate edges).
- **Prefix terminators** — where construction stops and marks an accept-
  with-continuation: `Code` blocks (the `ltmStop` flag already marks
  exactly these; `:my` declarations and run-only assertions do *not* stop
  the prefix, matching what the flag already encodes), `<?{…}>`/`<!{…}>`,
  back-references (`VarMatch`), `||` (only its first branch contributes,
  per S05), lookarounds (`Look` — conservatively a terminator at first;
  Rakudo is subtler, and the oracle will tell us if it matters), and
  parameterized subrule calls (args need evaluation).
- **Subrule expansion**: `<name>` inlines the callee's declarative prefix
  recursively — this is what makes proto dispatch rank correctly through
  helper tokens. A rule already on the expansion stack (recursion) ends the
  prefix there, as in Rakudo. Expansion depth is bounded; blowing the bound
  just ends the prefix early (safe: a shorter prefix only demotes, never
  breaks, correctness of the final match).
- Each NFA records, per accept state, the **literal-prefix length** reached
  purely by `Lit` edges — the existing tie-break, now computed statically.

### 2. The ranking runner

Standard multi-state NFA simulation, one linear scan from the match
position: a state set advances codepoint by codepoint; each alternative
remembers the furthest position at which one of *its* accept states was
live. No backtracking, no captures, no interpreter calls, no allocation on
the hot path (state sets are reusable bitsets sized to the NFA). Output: the
alternatives that are viable at this position, sorted by (prefix end desc,
literal-prefix desc, derivation depth, declaration order) — and everything
downstream keeps working exactly as now, because the *commit* loop (try full
branches in rank order with the real engine) already exists in both
integration points.

### 3. Integration points

- **`Alt` nodes** (Regex.cpp ~1816): build the union NFA lazily on first
  use and cache it on the node (the `mutable byteset` pattern already
  present). The probe loop is replaced by the NFA rank; branches whose
  prefix is empty or unbuildable rank last in declaration order. The
  snapshot/rollback hooks stay only for the *commit* phase's normal
  backtracking needs.
- **Protoregex dispatch** (Interpreter.cpp `meta.proto` block): one union
  NFA per proto, built from all candidates' prefixes, cached per grammar
  (`ClassInfo` — next to `ruleOrder`, which already stores the declaration
  order it needs). Invalidated when the rule set changes (`^add_method` /
  `augment` / role mixin — the same places that touch `ClassInfo.rules`).
  `candDeclEnd_`/`candLitPrefix_` and the probe pass disappear from the
  dispatch path; the packrat memo stays for the committed matches.
- **`/@arr/` interpolation** already lowers to a literal alternation
  (longest-first); it simply becomes an `Alt` and inherits the machinery.

### 4. Rollout switch

`RAKUPP_LTM=0` keeps the old probe path selectable during the campaign
(the `RAKUPP_PARALLEL` precedent) so any behavioral report can be bisected
to ranking-vs-engine in one rerun. It is removed once the gates hold.

## What changes user-visibly

- The two measured divergences flip to Rakudo's answers.
- Code blocks in alternations stop firing during candidate selection —
  programs that (accidentally) relied on probe-time side effects change
  behavior. The showcases (`showcase/js`, `showcase/perl`,
  `showcase/python` — all grammar-heavy) and the quirks memos get re-run;
  the `|`-vs-`||` workaround noted in the perl-showcase findings can be
  revisited.
- Ranking order changes for alternatives whose greedy ends and declarative
  prefixes disagree. This is the risk surface, and it is exactly what the
  oracle sweep is for.

## Performance budget

The NFA is built once per node/proto and cached; steady-state cost is one
linear scan per alternation entry, replacing one full probe descent *per
branch*. It should be a wash or a win on grammar-heavy code; the gates make
that a requirement, not a hope:

- `tools/bench/regex.raku` and the JSON-grammar bench above: within the
  perf-guard noise band of the pre-LTM binary (the 31 ms / 147 ms split is
  the number to protect).
- `perf-guard --check` against the recorded baseline, per the standing
  release rule.
- The tiny-alternation fast path (pure literal alternatives) must not get
  slower — it is the hottest shape in real grammars.

## Phases

**Phase 1: DELIVERED 2026-08-08 (commit 2de65f7).** `src/LtmNfa.{h,cpp}`
builds and ranks; `RAKUPP_LTM_DEBUG=1` diffs NFA-vs-probe order at the Alt
site with zero behavior change. Day-one results: both oracle divergences
reproduce with the NFA on Rakudo's side; the harness caught its own first
builder bug (shared entry states let a sibling's `a*` re-anchor other
branches — per-branch entries now); corpus: ZERO disagreements across the
whole 392-check suite and proto-token-ltm.t, 56 in longest-alternative.t
(the divergence being fixed), and a 6-item hand-classification worklist in
S05-mass/rx.t (one empty-rank shape included) — that worklist is phase 2's
entry ticket.

1. **NFA builder + offline harness.** `LtmNfa` with unit tests; a dump tool
   (`--ltm-dump` or a debug env var) that prints, for a given regex and
   input, the ranked order under probe vs NFA. Run it over a corpus
   harvested from Roast S05 + the three showcases' grammars; every
   disagreement is either a bug in the builder or a divergence we are
   *fixing* — classified by hand against the oracle.
2. **Wire `Alt`** behind `RAKUPP_LTM=1`. Gate: full Roast + battery +
   spec-site in both settings; divergence list from phase 1 resolved.
3. **Wire proto dispatch.** Same gates; `proto-token-ltm.t` must hold
   10/10, `longest-alternative.t` target: everything fudged Rakudo passes.
4. **Flip the default**, keep `RAKUPP_LTM=0` one release, then remove the
   probe path (or park it under `#ifdef` if the diff tool stays useful).
5. **Adjacent cleanup enabled by the new machinery**: the deferred
   "code blocks re-run under backtracking" item — with ranking now
   side-effect-free, the remaining re-runs are only in the committed
   engine, and deferring block execution to the accepted path becomes a
   contained change instead of a rewrite. Also the review-noted
   consolidation of the engine's seven copies of the builtin-class tables,
   which the NFA transition guards will want anyway.
6. **Docs**: a "How LTM works here" section in
   [internals/PARSING.md](../../internals/PARSING.md)'s regex companion (or
   a new `internals/REGEX-LTM.md`): the concept explainer from this plan,
   plus the implementation shape and the measured before/after. The
   user-facing regex guide gets the `|`-vs-`||` semantics spelled out with
   the `[ ab {} cd ] | abc` example.

## Gates (summary)

- Zero Roast regressions (flap band rules apply), battery 50/59, spec-site
  952 byte-identical examples — all unchanged or better, in both `RAKUPP_LTM`
  settings until the flip.
- `longest-alternative.t`: from 45/62 to the fudged-Rakudo score;
  `proto-token-ltm.t` stays 10/10.
- The two oracle divergences above are regression-tested in
  `t/regression/` (a new `ltm-declarative-prefix.raku`, passing on both
  engines).
- Perf: perf-guard baseline + the grammar bench, no regression outside the
  noise band.

## Risks, named

- **Prefix-termination semantics drift** — the exact Rakudo rule for
  lookarounds and zero-width assertions inside prefixes is folklore-level
  documented; every choice here gets an oracle probe before it is frozen
  (the conformance campaign's method).
- **Ranking changes break something that accidentally worked** — that is
  what phase 1's corpus diff is for; found early, each case is a
  classification, not a firefight.
- **NFA size on pathological classes** — transitions are predicates, not
  expanded sets, so `<:L>` costs one edge; the realistic blowup is deep
  subrule inlining, capped by the expansion bound.
- **Cache invalidation on grammar mutation** — the invalidation points are
  the same ones that already touch `ClassInfo.rules`; a missed one shows up
  as a stale ranking, which the `RAKUPP_LTM=0` bisect switch isolates
  quickly.
