# The Grand Review, phase 2 — the user-facing documentation — 2026-09-06

The second of three phases (source → user docs → the Internals book; the plan is
`docs/dev/plans/GRAND-REVIEW-PLAN.md`, phase 1's result is `REVIEW-GRAND.md`).
Nine read-only lanes read every user-facing page in full — roughly 16,000 lines
across 45 files — and verified each claim against the engine as it is today
(`build-arm64/rakupp` at `fa74f74`) and, where the page claims parity, against
Rakudo 2026.08. Every example was run on both engines; every number was measured
or counted from the repo; every link and anchor was resolved.

**This document records what the review FOUND. No documentation has been edited
yet** — the fix batches wait on three decisions, listed at the end. The working
ledger (every finding, its batch, every verification) is
`rc-work/review-grand/TRIAGE-2.md`; the lane reports are
`rc-work/review-grand/findings-2/D1..D9.md`, indexed one line per finding in
`findings-2/INDEX.md`.

## What the review found, in one paragraph

**204 findings**: 62 wrong claims, 41 stale numbers, 25 gaps where a page should
describe behaviour it does not mention, 19 places where two documents (or two
paragraphs) contradict each other, 9 broken references, and a handful of tone and
portability items. The dominant defect is not staleness but **a page contradicting
itself**: the async guide's prose and table state the concurrency model correctly
while the copy-pasteable shell block beneath them states its opposite; the
networking table says TLS verification is missing twenty-five lines under the prose
that demonstrates it working; a debugging page states the opposite of the buffering
page about the same call. The second pattern is **the promise a page cannot keep**:
the reference guide opens by promising every example shows its real output and is
wrong in twenty places — and `rakupp doc` serves those lines at the terminal, so
the drift is not merely on disk. The third is **the engine outrunning its
documentation**: eight of the feature page's listed gaps have closed, six reference
caveats warn readers off features that now work, and three pages still describe
process divergences that phase 1 fixed. Three findings were not documentation
problems at all but engine bugs, escalated below.

## Baselines (mechanical, before the lanes read)

- `tools/check-figures.raku`: the published figures AGREE with each other at
  **651** fully-passing Roast files. The engine measures **660** on main. That is
  one decision, not fifteen findings — see Decisions.
- Relative links: 562 checked across 45 files. The one "broken" link is a false
  positive (a `[+](…)` inside a code span); the checker needs a code-span skip.
- Both-engine example sweep of `docs/guide`, `docs/cookbook`, `docs/status`
  (`tools/doc-examples-diff.raku`): 223 blocks — 162 match, 35 are fragments that
  fail on both, 13 differ, 9 run here and fail on Rakudo, 1 fails only here.
- Phase 1's 133 doc-impact tags were extracted per page as each lane's worklist.

## Three engine bugs, escalated out of the phase

Each was verified by the coordinator independently of the lane that found it, and
each carries a reproducer.

1. **A native extension segfaults in a compiled binary.** A program that loads an
   extension and calls one of its subs prints its load and lookup lines, then exits
   139 (SIGSEGV) — interpreted, the same file completes. `--exe` reports that a
   symbolic reference is not natively compiled and bundles the interpreter instead;
   it is the bundled interpreter that dies. Confirmed on a clean arm64 build.
   `docs/guide/EXTENSIONS.md:497` says such a program "cannot be compiled into a
   standalone binary" — it compiles, and then crashes.
2. **`--exe` hoists a nested `my sub` above the declarations it closes over**, so
   the generated C++ does not compile (`use of undeclared identifier 'v_sscale'`).
   Three showcase programs (kvstore, fourier, orbits) cannot be compiled for this
   reason; of the 19 showcase entry points, 8 compile natively, 8 silently fall
   back to `--bundle`, 3 fail.
3. **A `LEAVE` phaser naming a variable whose `my` an early `return` skipped throws
   `X::Undeclared`**, where Rakudo runs the phaser with an undefined value. This is
   why `rakupp install --check` — documented twice in CLI.md — dies on any
   populated store while an empty store passes, and it still exits 0. Phase 1's C1
   batch worked on this phaser family and did not reach this case.

Two smaller code items: `rakupp doc :i` prints its own usage instead of the entry,
so every regex adverb is unlookuppable (`tools/doc.raku`'s MAIN treats a leading
colon as a named argument); and `libCandidates` prepends library directories to
*absolute* `.dylib` paths, which is why an arm64 box cannot recover from an
x86_64-pinned distribution on its own.

## The findings that matter most, by lane

- **D5, the concurrency guide — the highest-stakes error in the phase.**
  `ASYNC.md`'s heading, prose and table say parallel execution is the default (it
  has been since v3); the `sh` block ten lines below says the opposite, and the
  table's own row promises that unsynchronised shared mutation is "safe
  (serialised)" in the mode that block names. Measured: four workers × 50,000
  increments give **132,988** of 200,000 by default, and exactly 200,000 under
  `RAKUPP_GIL=1`. A reader who trusts the runnable half loses data silently. The
  same stale switch appears in nine places across five documents.
  Also: the networking table calls TLS certificate verification "not yet" while the
  prose above it demonstrates four refusals and the changelog records it landing in
  v1.8.0; and `JS.md`'s "byte for byte" claim is false in six measured ways, every
  one of them a question phase 1's JS lane had raised.
- **D2, the reference.** The opening promise is false in 20 places, and Rakudo
  prints identically in all 20 — drift, not divergence. `rakupp doc log10` shows
  `3` where both engines print `2.9999999999999996`. Six caveats warn readers off
  features that now work. The divergence register holds 3 bullets where 9 live
  divergences belong, two of them found by this lane.
- **D3, the command line.** 34 findings, up from 7 in the lane's first sitting over
  the same pages. "Flags are position-independent" is false for the one-liner
  family; "the completion script cannot drift" is false; the MAIN usage text is not
  byte-identical to Rakudo's; the recursion depth is given as two different wrong
  numbers on two pages; the caching page's whole timing session is 2.3–2.7× stale.
  `--lsp` has one subordinate clause in the entire documentation.
- **D7, the status pages.** The release runbook names a `deploy.sh` that does not
  exist, at the step that stamps the site's cache tag — a releaser who stalls there
  ships an engine returning visitors never load. The roadmap is two majors behind.
  The counting page's four-denominator arithmetic is sound, with one defect where a
  count and a percentage come from different runs.
- **D1, the front matter.** The features page's only runnable example is wrong on
  both engines, and eight of its listed gaps have closed and now match Rakudo
  exactly. Percentages disagree with the numbers printed beside them; one page
  contradicts itself thirty lines apart.
- **D4, the native surface.** Extension ABI 2 is documented; the header has been at
  3 since the bytes work, and the whole `rk_blob` family is missing from both
  tables — so a codec author sees only `rk_str`, which decodes UTF-8 and silently
  mangles raw bytes. The page's opening premise (a tight loop costs this engine ten
  times what it costs Rakudo) no longer measures.
- **D6, modules and recipes.** A recipe block that compiles on neither engine; five
  annotated outputs that neither engine prints; `:ver` documented as ignored when it
  is honoured; `zef install Data::Native` promised by two pages when the
  distribution is in neither ecosystem index.
- **D9, the FAQ.** Three closed process divergences still documented as open; a
  page stating the opposite of its sibling; two timing tables that no longer
  reproduce on the same Rakudo binary.
- **D8, the record and the site sources.** Four showcase projects are 404 on the
  site while the README promises each has a page — a republish, not an edit. Two
  example output blocks come from older, smaller versions of their programs.

## A gate this phase had to build

`tools/doc-examples-diff.raku` compares the two ENGINES with each other, so a block
whose `# →` annotation claims output neither engine ever printed is still reported
MATCH. **`tools/check-doc-annotations.raku`** (added with this commit) fills that
gap: it runs each block and compares its stdout, in order, against the same-line
annotations, skipping anything it cannot judge conservatively. First run over all
documentation: 62 blocks checked, 42 agree, **20 disagree** — including
`REFERENCE.md` documenting `True` where the engine prints `any(False, True,
False)`, `ABC` where it prints `AbC`, and `2A` where it prints `42`. Two lanes and
this tool found the same reference drift independently.

## Method notes worth keeping

- **`perl` on this box is x86_64**, so wrapping a build in `perl -e 'alarm N; exec
  @ARGV'` makes the spawned compiler emit x86_64 objects — which looks exactly like
  an `--exe` regression. Never wrap `--exe`/`--aot`/`--bundle` runs. The extension
  segfault was re-verified without the wrapper before it was believed.
- **A single reading under-reads a large page family.** Every lane in this phase
  received two sittings (the first round was cut short by a model limit, the second
  verified it). One lane went from 7 findings to 34 over the same six pages;
  another corrected a systematic line-number drift in seven of its own findings,
  which would have made line-addressed edits land in the wrong place.
- **`qx{}` does not interpolate in Raku; `qqx{}` does.** The new checker scanned
  nothing for three runs because of it.
- A lane's report must be written early and appended to. That rule is what made the
  interrupted first round cost nothing.

## Decisions needed before the fix batches

1. **The figure refresh.** The published set (651 files, 199,980 assertions, 637
   regression checks) agrees with itself and describes v3.25.0; main measures 660,
   200,172 and 773. The other figures in the same comparison table (documentation
   examples, ecosystem distributions, `--slim` size) were not re-measured here and
   would need their own sweeps. Options and the full table:
   `rc-work/review-grand/findings-2/FIGURE-REFRESH.md`. Recommendation: leave the
   published set until the next release unless one is close.
2. **`Data::Native`.** Two pages promise `zef install Data::Native` makes the
   examples run on Rakudo. The distribution is in neither ecosystem index (both
   caches refreshed during the review) and exists only unpublished at
   `~/raku-modules/Data-Native`, while sibling `zef:ash` distributions are
   published. Publish it, or change both pages.
3. **The second CI leg.** FFI.md states "the whole suite is run twice, once each
   way", and three book chapters repeat it; `release.yml` runs `t/run.raku` once.
   Add the leg, or reword five places — phase 3 inherits the same sentence.

## The fix plan

Ten batches by document family (cross-lane pins first, since six clusters must be
worded once: the concurrency switch in 9 places, the build-directory spelling in
13, the startup time in 4, the Roast run time in 3, the documentation-example count
in 3, one assertion pair in 3). Each batch: edits quoting their finding → the
page's blocks re-run on both engines → `check-doc-annotations` on the page → the
link check → `check-figures` if a figure moved → a `rakupp doc` spot-check if the
reference changed.

## What phase 3 inherits

The Internals book repeats three claims this phase found wrong (the second CI leg,
the extension ABI version, the concurrency default); the book needs a
`--target=js` chapter (phase 1's JS lane wrote the outline); and the two appendices
in the reference guide are generated and stale, which is the same generator
question the book's appendices raise.
