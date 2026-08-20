# Pre-3.5 review — 2026-08-19

A profile-first review of the hand-written `src/` surface, run after the 6.e
campaign and before tagging v3.5.0. The brief: **cleaner, and faster; no test
may move down**. Method: sample a dispatch-heavy workload, fix what the profile
names, and separately run a clone detector over the non-generated sources to
find the copy-then-diverge pairs the 2.0 review named as the dominant defect
class.

## Baselines (pre-review, at eb51736 + release prep)

- Roast **197,118 / 218,160** declared, **627** files fully passing
- `t/run.raku` 471/471 · `perf-guard --check` green
- profile of a method-dispatch loop (1.2M iterations, `.chars`/`.uc`/`.elems`/
  `.head`/`.substr`): 4.2–4.4 s

## Where the time actually went

| cost centre | share of active samples |
|---|---|
| `Value` copy + destroy | ~22% |
| the method-dispatch chain (Inner + Part2/3/Tail arm walking) | ~16% |
| malloc/free | ~14% |
| Unicode case mapping (`uc` on an ASCII string) | ~7.6% |

## Batch 1 — the profile's two answers

**ASCII case mapping.** `mapCase` decoded to codepoints, segmented graphemes,
allocated a case-map vector per character and re-normalised the result — for
`"hello world".uc`. None of that can change an ASCII answer: no multi-codepoint
graphemes, no Final_Sigma (Greek), no expansions (ß, ﬁ), nothing for NFC to
compose. An `allAscii` fast path (the helper already existed, three lines below,
for `cpCount`) makes it a byte map. **4.2 s → 3.0 s on the benchmark (-28%)**,
and the malloc traffic left the profile's top ten entirely.

**Two copies per method call.** The call site built the method name with
`mc->bang ? "!" + … : mc->meta ? "^" + … : mc->method` — a std::string copy on
*every* call for the common case where neither prefix applies — and passed the
argument vector as an lvalue into a by-value parameter, copying the vector and
every `Value` in it. The name is now bound by reference unless a prefix is
needed, and the arguments move. `strscan` -6.1%, `hash` -3.1% on perf-guard.

## Batch 2 — the clone detector

**UAX #29, twice.** The grapheme-break rule chain (GB3–GB999) and its carried
state (regional-indicator run, emoji-ZWJ flag, GB9c conjunct chain) was written
out once over a codepoint vector (`uniGraphemeStarts`) and once over UTF-8 bytes
(`uniClusterEndUtf8`). The copies agreed, which is the dangerous kind: the next
rule fix would have had to land in both, and only one of them is on the regex
engine's grapheme stride. Now one `GbState` + `gbBreakBefore` + `gbAdvance`.
Checked against the official conformance data: `S15-nfg/GraphemeBreakTest-*.t`
766/766.

**Multi-dimensional subscript expansion, twice.** `@a[1; *; 2..3]` names a set
of paths; the recursion that fans Whatever/list/range dimensions out into
concrete index tuples lived in both `evalIndex` (read) and `evalAssignInner`
(write), identical but for one comment. Extracted to `expandDimTuples`.

## Batch 3 — the rest of the clones, and the bug one of them was hiding

**Two hyper walks.** `hyperUnary` and `hyperPostfixApply` each carried the same
container recursion — descend arrays and ranges, keep hash keys and quanthash
flavour — differing only in the leaf, which is the entire point of the
operation. Now `hyperWalk(v, leaf)`.

**Two `.can` stub builders.** The class arm and the instance arm each carried
the universal-method list (`new bless gist Str raku perl so defined can isa does
WHAT WHICH WHERE clone`) and the stub-callable construction. Now
`builtinCanStub`.

**Two sigilless-parameter parsers.** The bare (`\p`) and typed (`Int:D \p`)
branches shared 26 identical lines — paren sub-signature, then the `is`/`where`
trait loop. Now `parseSigillessTail`.

**…and what that extraction surfaced.** `sub f(\i) { i }` returned the imaginary
unit, and `\e`, `\pi`, `\tau`, `\now` behaved the same way. The interpreter's
NameTerm path consulted the `pi`/`e`/`i`/`tau`/`now`/`time`/`rand` constants
*before* looking in the lexical scope; the codegen runtime's `rtNameTerm` had
always done it the other way round, with a comment saying why ("so a user's own
`sub now {…}` still wins"). Two implementations of one rule, and the wrong one
was in the interpreter. Order fixed to match; pinned by
`t/regression/sigilless-name-shadows-term.raku`. This was pre-existing, and
confirmed against a pre-review build before the fix.

## Batch 4 — a second profile, on a different shape

The first profile was dispatch-heavy. A string/regex workload names different
things: the matcher itself (`matchNode`, 773 samples — inherent), then `Value`
vector relocation (~500 between `emplace_back`, `__uninitialized_allocator_relocate`
and `~Value`), and — startling in a hot path — `std::operator>>(istream&, string&)`
at 84.

**`.words` was a `std::istringstream`.** It allocated a stream and a buffer per
call and split on the C locale's idea of whitespace, which is ASCII only:
`"a\c[NO-BREAK SPACE]b".words` was one word here and two in Rakudo. Replaced
with a direct scan over the bytes using a shared `uniIsSpaceCp` (category Z plus
the ASCII controls and NEL — the same set the `<:space>` property assertion
uses). ~3% on the mixed workload, and a correctness fix.

**Matches copied, then copied again.** The `:nth`-list path built a
`std::vector<Value>` and then copied every element into the result list; it
hands the vector over now. The `:g` loop reserves, so a long match list stops
relocating heavy `Value`s through the 1-2-4-8 doublings.

**…and what that path turned out to be hiding.** `("abc" ~~ m:g/z/).elems` was
**1** here and **0** in Rakudo. A multi-match form that matches nothing answers
with an *empty List*; the smartmatch was collapsing every falsy match result to
`Nil`, and `Nil.elems` is 1. Both are falsy, so only code that counts or
iterates the result could tell — which is exactly what a `for` over `m:g`
does. Fixed in both the smartmatch and `substSelect` (`s:g///` had it too),
pinned by `t/regression/empty-global-match-is-a-list.raku`. Pre-existing,
confirmed against a pre-review build.

## Gates

| gate | before | batch 1 | batch 2 | batch 3 | batch 4 |
|---|---|---|---|---|
| Roast, all declared | 197,118 / 218,160 | 197,385 / 218,489 | 197,401 / 218,588 | 197,435 / 218,624 | **197,430 / 218,614** |
| files fully passing | 627 | 627 | 627 | 628 | **629** |
| `t/run.raku` | 471/471 | 471/471 | 471/471 | 472/472 | **473/473** |
| perf-guard | green | green | green | green | green |

## Open, from the profile — not attempted here

- **`Value` copy cost (~22%, now the clear top).** 32 fields, twelve of them
  non-trivial; a copy is several string copies plus up to ten `shared_ptr`
  bumps. Shrinking it is [REPRESENTATION-PLAN](../plans/REPRESENTATION-PLAN.md)
  work, not a review batch.
- **The dispatch chain (~16%).** ~700 arms walked in order across four files.
  `MName` already makes each comparison cheap (length + first-eight-bytes as one
  integer); what is left is the number of them. A per-callsite inline cache is
  the obvious idea and is *not* obviously safe: arms are guarded by argument
  shape as well as name and invocant type, so caching "segment 3 served this"
  can skip an earlier arm that would have claimed a different argument list.
  Wants a measured design, not a patch.
- `_platform_memcmp` at ~4%: arm guards comparing `hashKind`/`enumName` against
  string literals. Interning those is again representation work.

## Issue #22 — a Junction argument stopped autothreading off the eval path

`'00011', { .subst(/01/, '10', :g) } ... *.contains: none '01'` never
terminated (reported by habere-et-dispertire). The endpoint is a curried
`*.contains(none '01')`, and a WhateverCode calls `methodCall()` directly rather
than going through the `MethodCall` eval arm — which is where junction
autothreading of arguments was written. So the predicate answered a plain
`False` where Rakudo answers `none(False)`, which is *true*, and the sequence
ran to the million-element cap.

The rule now lives in `methodCall()`, where every caller reaches it, and the
eval-arm copy is gone. The exemption list (matcher positions — `grep`, `first`,
`match`… — where a junction is a smartmatch target rather than a value to thread
over) was moved across verbatim: this was a *relocation*, not a redefinition,
and the four entries the first draft invented were removed before it landed.

The common path pays only an `isJunction` scan of the arguments, which is a type
tag and an enum-name check; the matcher-set lookup happens only once a junction
is actually found.

Roast: `S03-junctions/autothreading.t` 91/107 → **96/107**,
`S03-junctions/misc.t` 137/155 → **143/155**. Pinned by
`t/regression/junction-arg-autothreads-everywhere.raku`.

## Found while testing #22, not fixed

`q` as a routine name loses to the `q//` quote operator: with `my &q = …`
declared, `q("11000")` is the *string* `"11000"` here and the call in Rakudo.
Same family as the sigilless-name shadowing fixed in batch 3 — a declared name
that a built-in spelling wins against — but in the lexer rather than the name
resolver, so it needs its own look.
