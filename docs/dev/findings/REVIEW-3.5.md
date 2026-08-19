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

## Gates

| gate | before | batch 1 | batch 2 | batch 3 |
|---|---|---|---|---|
| Roast, all declared | 197,118 / 218,160 | 197,385 / 218,489 | 197,401 / 218,588 | **197,435 / 218,624** |
| files fully passing | 627 | 627 | 627 | **628** |
| `t/run.raku` | 471/471 | 471/471 | 471/471 | **472/472** |
| perf-guard | green | green | green | green |

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
