# Longest-Token Matching

Raku's `|` alternation does not try its branches left to right. Each branch is
ranked by how far its **declarative prefix** can reach into the input — the
leading part of the pattern made of literals, character classes and quantifiers,
up to the first procedural construct. The branch with the longest reachable
prefix is committed first; ties break by more-literal-first, then by declaration
order. `||` is the opt-out: sequential first match, no ranking.

```raku
say ("abcd" ~~ / [ ab { } cd ] | abc /).Str;   # abc
```

The first branch *could* match four characters, but its declarative prefix ends
at the code block, at length 2. `abc`'s prefix is 3, so `abc` wins.

Two properties of that example matter and pull in opposite directions. The
ranking is by **prefix** length, not by greedy match length. And ranking must
run **no user code** — the `{ }` above must not fire for a branch that loses.

## Two rankers

**The probe** (the default) runs each branch once for its greedy full-match end,
snapshotting and rolling back side effects through the `saveState`/`restoreState`
hooks, and commits branches in longest-end-first order.

It is cheap and it is wrong twice: it ranks by the wrong length, and it descends
into user-code paths that true longest-token matching never visits.

**The NFA** (`RAKUPP_LTM=1`) builds a Thompson automaton per alternation, lazily,
from the compiled node tree, and answers "how far could each branch's
declarative prefix reach?" in one linear scan of the input. No code execution, no
backtracking. The commit phase then runs the real engine on the ranked branches,
so captures, assertions and side effects behave exactly as before.

An automaton cannot execute code. That is not a limitation here — it is
precisely what makes it the structurally correct ranking oracle.

## What ends a declarative prefix

Two very different reasons a prefix stops, and the distinction is the whole
correctness argument.

**Spec enders** — the prefix genuinely ends here, and ranking at this point is
*correct*: a bare `{…}` code block, a back-reference (`$0`, `$<name>`), a `||`
tail (the first branch's prefix continues, per the specification), runtime
quantifier bounds `** {$n}`, and `&` conjunction.

**Model gaps** — the *builder* could not model the construct, so ranking might
unfairly demote this branch: an unexpandable subrule, Unicode-property and
grapheme-cluster classes, `:m` on a character *class*, non-ASCII `:i` folding,
lookarounds, and a blown build bound (200 depth or 4,000 states).

Zero-width assertions are **transparent**: anchors, word boundaries, `:my`
declarations, and the code assertions `<?{…}>` / `<!{…}>` are epsilon
transitions for ranking purposes. Roast checks this — `a <?{ 1 }> .+ | aa`
against `"aaa"` matches `aaa`, so the assertion does not end the prefix.

Over-permissiveness is safe: the commit engine enforces assertions for real, and
a branch whose assertion fails simply fails at commit and hands over to the next
ranked branch.

## The gap-aware hybrid contract

`RAKUPP_LTM=1` must never be *less* correct than the default. So:

> The NFA decides an alternation only when **every** branch's prefix ended for a
> spec reason. If any branch hit a model gap, that alternation falls back to the
> probe.

```cpp
// src/LtmNfa.h
bool anyModelGap() const { return anyGap_; }
```

Each expansion route closed converts more alternations from probe fallback to
NFA ranking. A branch whose prefix cannot reach any accept state on the given
input is not a candidate at all — the commit never tries it, matching Rakudo's
own pruning.

## The automaton

```cpp
// src/LtmNfa.h
struct Pred {
    char kind = 'A';   // 'L' literal cp, 'C' class node, 'A' any,
                       // 'S' whitespace (<ws>), 'F' builtin flag class
    const void* node = nullptr;
    uint32_t lit = 0;
    bool icase = false;
};
struct State {
    std::vector<std::pair<int,int>> eps;
    std::vector<std::pair<int,int>> edges;
    int acceptBranch = -1;
    int litDepth = 0;
};
```

Transitions are codepoint predicates that **reuse the `Class` node's own match
data** — byte ranges, codepoint ranges, negation, folding. There is no second
Unicode implementation, which is both less code and structurally safer: the
ranker cannot disagree with the matcher about what a class contains.

The `Pred` borrows the `Regex`'s `Node`, relying on the regex outliving its
cached automaton — the same lifetime the byteset cache already assumes.

Every branch gets its **own** entry epsilon-edge from state 0. Sharing entry
states once let a sibling branch's `a*` loop re-anchor other branches, which
produced ranks that were not merely wrong but incoherent.

## The cache, and an address-reuse bug

The automaton is cached **on the `Alt` node**:

```cpp
// src/Regex.h — Node
mutable std::unique_ptr<LtmNfa> ltmNfa;
```

The first implementation used a side map keyed on `Node*`, which is the obvious
thing and was wrong. A process compiles many regexes, freed node addresses get
recycled by the allocator, and the map handed out an automaton built for a
completely different pattern. The symptom, found by the phase-1 harness on a
Roast alternation file, was empty or nonsense ranks.

That is the exact opposite of the flip-flop state map in Chapter 17, which *is*
keyed on a node address and *is* correct — because AST nodes are never freed
while regex nodes are. The rule to extract: **a pointer is only a valid key when
its target's lifetime is at least the map's.**

For expanded subrules there is a second staleness axis. Named regexes register
last-wins, so a re-declared token changes what the same compiled node resolves
to. The stored pattern text doubles as a staleness stamp:

```cpp
// src/LtmNfa.h
bool stillValid(const GrammarHooks* hooks) const;
std::map<std::string, std::string> expandStamps_;   // name → pattern text used
```

## Subrule expansion: three routes

A `<name>` inside a branch resolves through an expansion context:

```cpp
// src/LtmNfa.h
struct LtmExpand {
    const GrammarHooks* hooks = nullptr;
    std::function<int(const std::string& name, const void*& regexOut,
                      char& flagOut)> grammar;
};
```

**Lexical.** Interpreter-registered `my token/rule/regex` bodies, handed over as
text plus the match path's exact flag spelling (`rule` becomes `sr`, `token`
becomes `r`). The automaton compiles and **owns** the callee, inlining its
prefix recursively. Recursion is treated as a spec prefix end.

**Grammar.** Already-compiled rule bodies from the grammar's own table, so
nothing is recompiled and the automaton borrows the `Regex`'s nodes. The
resolver answers with a small integer: refuse, here is a compiled body, this is
`<ws>`, or this is a single-character built-in class. Protos, parameterised rules
and dynamically-dependent bodies refuse, which becomes a model gap and therefore
a probe fallback.

**`<sym>`** inlines as the current candidate's `:sym<…>` literal, for proto
dispatch.

`<ws>` is modelled as `\s*` for ranking. That is deliberately approximate — the
commit engine enforces the real `<!ww>` word-boundary gate — and it is
ranking-grade rather than semantics-grade, which is all the ranker needs.

### The composed character class

A class written `<[\-+.] +uri_alpha +digit>` is parsed into a synthesised
first-match `Alt`. Semantically it is a **one-character union**, so the builder
must union its members:

```cpp
// src/Regex.h — Node
bool classCombo = false;   // a SYNTHESIZED Alt for a composed char class
```

Without the tag those nodes were modelled like a user `||`, which takes only the
first member. That under-matched the prefix and pruned whole branches — the
symptom was a URI grammar failing on its `scheme` rule.

## Proto dispatch

Protoregex candidates are ranked by the same machinery:

```cpp
// src/LtmNfa.h
static std::unique_ptr<LtmNfa> buildForBranches(
    const std::vector<const void*>& regexes,
    const std::vector<std::string>& syms, const LtmExpand* ctx);
```

One union automaton over all candidates' bodies, one branch per candidate, with
`<sym>` inlined per branch. It is cached on the `GrammarRuleMeta`, and since the
matcher is per-parse, that cache dies with the parse and needs no staleness
management at all. Any model gap in any candidate discards the automaton and the
probe dispatch stands.

## The tie-break must be per-path

Equal prefix length breaks by "more literal wins", and that count must be
computed **per path during the scan**, not stored per state.

The builder originally kept a static literal depth on each state, max-merged at
join states. In:

```raku
token TOP { <foo> | <bar> }
token foo { \w\w }
token bar { aa | <foo> }
```

on input `"bb"`, `bar`'s dead `aa` path — two literals — leaked its count onto
the join state that `bar`'s live `<foo>` path — zero literals — also reaches. So
`bar` stole the tie from the earlier-declared `foo`.

`rank()` now carries, per live state, the length of the leading literal run
along the path that reached it, frozen at the first non-literal edge; accepts
record the *path's* value, not the state's.

```cpp
// src/LtmNfa.h
struct Ranked { int branch; long prefixEnd; long litPrefix; };
std::vector<Ranked> rank(const std::string& s, long pos) const;
```

sorted by prefix end descending, then literal prefix descending, then
declaration order — the specification's tie-break chain.

## Oracle notes

Three behaviours confirmed against Rakudo while building this, each of which
would otherwise have been guessed wrong:

- **`multi token` protos do not rank.** Rakudo ranks proto candidates by
  declarative prefix only when they are declared `token t:sym<x>`; `multi token
  t:sym<x>` candidates dispatch in declaration order, and so does a direct
  `.subparse(:rule<proto>)`. Both rankers here rank `multi token` candidates
  too — a pre-existing divergence, deferred rather than hidden.
- **`||` continues through its first branch.** `a || b`'s declarative prefix is
  `a`'s prefix: not empty, and not both.
- **Code assertions are transparent** to ranking, as above.

## Debugging and measured results

| Variable | Effect |
|---|---|
| `RAKUPP_LTM=1` | use the NFA ranker where it is gap-free |
| `RAKUPP_LTM_DEBUG=1` | rank both ways and print disagreements |
| `RAKUPP_LTM_RANKDUMP=1` | print each NFA-decided alternation's ranking |

The debug flag works *without* the feature flag, and it was the phase-1 harness:
before anything consulted the automaton's answer, the probe path computed both
rankings and printed every disagreement with its pattern context, for
classification against Rakudo. It is still the fastest way to classify a
suspected ranking bug.

Same binary, same machine, full Roast:

| setting | assertions | `longest-alternative.t` | `proto-token-ltm.t` |
|---|---|---|---|
| default (probe) | 197,116 | 45/62 | 10/10 |
| `RAKUPP_LTM=1` | 197,117 | 47/62 | 10/10 |

The flag's failure set on the alternation file is a strict subset of the
default's. A grammar benchmark — twenty compiles of a JSON grammar corpus — runs
in 28 to 34 milliseconds in both settings, so automaton construction is fully
amortised by the node cache. The twenty-eight grammar showcase runs are
byte-identical across settings.

The plan keeps the flag available for one release after the default changes,
which is the general policy for a switch that changes behaviour rather than
speed.
