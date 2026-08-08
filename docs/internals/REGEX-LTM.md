# Longest-Token Matching (LTM)

How the regex engine ranks `|` alternations and protoregex dispatch, what
the declarative prefix is, and how the `RAKUPP_LTM=1` NFA ranker works.
The campaign history and the phase-by-phase delivery log live in
[the LTM plan](../dev/plans/LTM-PLAN.md); this is the implementation
reference for the code that landed.

## Contents
- [What LTM is](#what-ltm-is)
- [Two rankers](#two-rankers)
- [The declarative prefix: what ends it](#the-declarative-prefix-what-ends-it)
- [The gap-aware hybrid contract](#the-gap-aware-hybrid-contract)
- [The NFA](#the-nfa)
- [Subrule expansion: three routes](#subrule-expansion-three-routes)
- [Proto dispatch](#proto-dispatch)
- [The tie-break is per-path](#the-tie-break-is-per-path)
- [Oracle notes](#oracle-notes)
- [Debugging](#debugging)
- [Measured](#measured)

## What LTM is

Raku's `|` alternation does not try branches left to right. Each branch is
ranked by how far its **declarative prefix** can reach into the input — the
leading part of the pattern made of literals, character classes and
quantifiers, up to the first procedural construct (a code block, a
back-reference, a `||`). The branch with the longest reachable prefix is
committed first; ties break by more-literal-first, then declaration order.
`||` is the opt-out: sequential first-match, no ranking.

```raku
say ("abcd" ~~ / [ ab { } cd ] | abc /).Str;   # abc
```

The first branch *could* match four characters, but its declarative prefix
ends at the code block — length 2. `abc`'s prefix is 3, so `abc` wins. Two
properties matter: the ranking is by *prefix* length, not greedy match
length, and ranking must run **no user code** — the `{ }` above must not
fire for a branch that loses.

## Two rankers

- **Default (probe)**: each branch is probed once for its greedy full-match
  end (side effects snapshotted and rolled back), branches commit in
  longest-end-first order. Cheap, but it ranks by the wrong length and the
  probe descends into user code paths that true LTM never visits.
- **`RAKUPP_LTM=1` (NFA)**: a Thompson NFA per alternation, built lazily
  from the compiled node tree and cached on the `Alt` node, answers "how
  far could each branch's declarative prefix reach?" in one linear scan of
  the input. No code execution, no backtracking. The commit phase then runs
  the real engine on the ranked branches, so captures, assertions and side
  effects behave exactly as in the default engine.

The flag is the phase-4 flip candidate: the plan keeps `RAKUPP_LTM=0`
available for one release after the default changes.

## The declarative prefix: what ends it

Two very different reasons a prefix stops, tracked separately:

**Spec enders** (the prefix genuinely ends here; ranking at this point is
*correct*): a bare `{…}` code block, a back-reference (`$0`, `$<name>`),
a `||` tail (the first branch's prefix continues, per S05), runtime
quantifier bounds (`** {$n}`), `&` conjunction.

**Model gaps** (the *builder* couldn't model the construct; ranking might
unfairly demote this branch): an unexpandable subrule, `:m`/`:ignoremark`
literals, Unicode-property and grapheme-cluster classes, non-ASCII `:i`
folding, lookarounds, a blown build bound (200 depth / 4000 states).

Zero-width assertions are **transparent**: anchors, word boundaries, `:my`
declarations, and the code assertions `<?{…}>`/`<!{…}>` are ε for the
ranking (roast: `a <?{ 1 }> .+ | aa` on `"aaa"` matches `aaa` — the
assertion does not end the prefix). Over-permissiveness is safe: the commit
engine enforces them for real, and a branch whose assertion fails simply
fails at commit and hands over to the next ranked branch.

## The gap-aware hybrid contract

`RAKUPP_LTM=1` must never be *less* correct than the default. The NFA
decides an alternation only when every branch's prefix ended for a spec
reason; if any branch hit a model gap, that alternation falls back to the
probe. Each expansion route closed (below) converts more alternations from
probe-fallback to NFA ranking. A branch whose prefix cannot reach any
accept state on the given input is not a candidate at all — the commit
never tries it (matching Rakudo's NFA pruning).

## The NFA

`src/LtmNfa.{h,cpp}`. States with ε-edges and predicate edges; predicates
reuse the `Class` node's own match data (byte ranges, codepoint ranges,
negation, folding) — there is no second Unicode implementation. Predicate
kinds: `L` literal codepoint, `C` class node, `A` any, `S` whitespace
(models `<ws>` as `\s*` for ranking; the commit engine enforces the real
`<!ww>` gate), `F` single-char builtin class by flag letter.

Every branch gets its own entry ε-edge from state 0 — shared entry states
once let a sibling's `a*` loop re-anchor other branches. The NFA is cached
on the `Alt` node (a `Node*`-keyed side map was poisoned by freed-address
reuse) and revalidated per use against the named-regex registry, which is
last-wins: a re-declared token changes what the same compiled node
resolves to, so the stored pattern text doubles as a staleness stamp.

## Subrule expansion: three routes

A `<name>` inside a branch resolves through `LtmExpand`:

1. **Lexical** (`GrammarHooks::namedRule`): interpreter-registered `my
   token/regex` bodies as text + flags; the NFA compiles and owns the
   callee, inlines its prefix recursively. Recursion is a spec prefix end.
   `rule`-kind bodies decline until `<ws>` modeling covers the lexical
   route too.
2. **Grammar** (`GrammarMatcher::ltmResolve`): already-compiled rule bodies
   from the grammar's own table — no recompile, the NFA borrows the
   `Regex`'s nodes. `<ws>` answers as the `\s*` predicate loop, single-char
   builtin classes as `F` predicates. Protos, parameterized rules and
   dyn-dependent bodies refuse (→ model gap → probe).
3. **`<sym>`** inlines as the current candidate's `:sym<…>` literal
   (proto dispatch only).

A composed character class (`<[\-+.] +uri_alpha +digit>`) is parsed into a
synthesized first-match `Alt` — semantically a one-character **union**, so
those nodes carry a `classCombo` tag and the builder unions them. Without
the tag they were modeled like user `||` (first member only), which
under-matched the prefix and pruned whole branches (the URI grammar's
`scheme`).

## Proto dispatch

`proto token t {*}` candidates are ranked by the same machinery:
`LtmNfa::buildForBranches` builds one union NFA over all candidates'
bodies (one branch per candidate, `<sym>` inlined per branch), cached on
the `GrammarRuleMeta`. The matcher is a per-parse stack object, so the
cache dies with the parse and needs no staleness management. Any model gap
in any candidate discards the NFA and the probe dispatch stands.

## The tie-break is per-path

Equal prefix length breaks by "more literal wins" — and that count must be
computed **per path during the scan**, not stored per state. The builder
originally kept a static literal depth on each state, max-merged at join
states; in

```raku
token TOP { <foo> | <bar> }
token foo { \w\w }
token bar { aa | <foo> }
```

on input `"bb"`, `bar`'s dead `aa` path (2 literals) leaked its count onto
the join state that `bar`'s live `<foo>` path (0 literals) also reaches, so
`bar` stole the tie from the earlier-declared `foo`. `rank()` now carries,
per live NFA state, the length of the leading literal run along the path
that reached it, frozen at the first non-literal edge; accepts record the
path's value, not the state's.

## Oracle notes

Divergences and behaviors confirmed against Rakudo while building this:

- **`multi token` protos don't rank.** Rakudo ranks proto candidates by
  declarative prefix only when they are declared `token t:sym<x>`; `multi
  token t:sym<x>` candidates dispatch in declaration order (plain multi
  dispatch), and so does a direct `G.subparse(:rule<proto>)` call. Both
  our rankers (probe and NFA) rank `multi token` candidates too — a
  pre-existing divergence, deferred; recording multi-ness on
  `GrammarRuleMeta` is the entry ticket.
- **`||` continues through its first branch.** `a || b`'s declarative
  prefix is `a`'s prefix (not empty, not both).
- **Code assertions are transparent** to ranking (see above), and a plain
  `~~` regex evaluates them for real via the same `assertPass` hook the
  wired `regex {…}` values use.

## Debugging

- `RAKUPP_LTM_DEBUG=1` (works without `RAKUPP_LTM`): the probe path ranks
  both ways and prints any disagreement with the pattern context — the
  phase-1 oracle harness, still the fastest way to classify a suspected
  ranking bug.
- `RAKUPP_LTM_RANKDUMP=1` (with `RAKUPP_LTM=1`): prints each NFA-decided
  alternation's ranked order (`branch`, `prefixEnd`, `litPrefix`).

## Measured

Same binary, same machine, full Roast:

| setting | assertions | longest-alternative.t | proto-token-ltm.t |
|---|---|---|---|
| default (probe) | 197,116 | 45/62 | 10/10 |
| `RAKUPP_LTM=1` | 197,117 | 47/62 | 10/10 |

The flag's fail set on longest-alternative.t is a strict subset of the
default's; the only other per-file movers are the known flappers
(lines.t, pick.t, advent2012-day13.t, stress.t). Grammar bench (20
compiles of the JSON grammar corpus): 28–34 ms in both settings — NFA
construction is amortized by the node cache. The 28 grammar showcase runs
(js, perl, python, forth, lisp, json, markdown) are byte-identical across
settings.
