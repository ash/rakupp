# Grand Review — plan

Started 2026-09-06 at `4183921` (v3.25.0 + 53 commits). Three phases, in
this order, each finished before the next starts:

1. **Source** — the whole hand-written `src/` surface.
2. **User-facing documentation** — README, LONGREAD, CHANGELOG, `docs/guide`
   (+ faq), `docs/status`, `docs/cookbook`.
3. **The Internals book** — `docs/book` (47 chapters) and `docs/internals`
   (12 notes), which overlap and get one decision.

## Why this order

The docs describe the code, and a source review changes the code: the
pre-2.0 review ([REVIEW-2.0.md](../findings/REVIEW-2.0.md)) rewrote code in
every subsystem and deleted whole dead dispatch arms. Anything reviewed
before that is reviewed twice. Phase 1 also produces the worklist for the
other two: every finding carries a doc-impact tag (`user` / `internals` /
`none`), so phases 2 and 3 start from a ledger. The book is last because it
is also the history and should end with the review itself. And only phase 1
competes for the machine — the gates must run alone.

The one honest argument for docs first — a doc review harvests claims the
code review should verify — is kept without the reordering: each source
lane's brief names the guide chapter and the book chapter for its subsystem,
read as the specification before the code. The docs are *read* in phase 1
and *corrected* in phases 2 and 3.

## Phase 1 — source

Sizes at the start: 116 hand-written files, 105,015 lines (the generated
Unicode tables, `unicode_names.cpp` and `JsRuntimeSrc.cpp` excluded).

**Method** — the same as REVIEW-2.0 and REVIEW-3.7: parallel read-only
reviewers, one per lane, each reading its slice in full plus the docs that
specify it; every finding verified on the running engine against the Rakudo
oracle (`/opt/homebrew/bin/raku`, arm64 2026.08) with a probe file kept;
findings ranked by user impact (silent wrong answer > crash > divergence >
twin drift > dead code > perf); then fixes applied in **gated batches**, the
rest on a deferred list. The shared brief is `rc-work/review-grand/BRIEF.md`;
raw per-lane findings land in `rc-work/review-grand/findings/`; the curated
result is `docs/dev/findings/REVIEW-GRAND.md` (this review's entry in the
REVIEW-* series).

**Lanes** (13):

| Lane | Files | Lines |
|---|---|---|
| L1-parse | Lexer, Parser, Token, Ast.h, AstDump/Emit, Pod, Highlight | ~14.4K |
| L2-interp-a | Interpreter.cpp 1–10500, Interpreter.h | ~13K |
| L3-interp-b | Interpreter.cpp 10500–21000 | ~10.5K |
| L4-interp-c | Interpreter.cpp 21000–end | ~10.6K |
| L5-builtins-a | Builtins.cpp 1–6800, BuiltinsShared.h | ~7K |
| L6-builtins-b | Builtins.cpp 6800–end | ~6.7K |
| L7-methods | MethodCallPart2/Part3/Tail, MethodCallSegment.h, MethodName.h | ~11.7K |
| L8-regex | Regex, LtmNfa, GrammarShim | ~5.5K |
| L9-codegen | Codegen, AstSerial, SlimScan, stubs | ~4.4K |
| L10-js | codegen/Js, js-rt/*.js | ~7.8K |
| L11-value | Value, ValueHash/Vec, IStr, BigInt, IntOps, CNumeric, Unicode, Runtime, Platform, IOSpec, JsonLite, Zlib, Digest | ~7.6K |
| L12-cli-ffi | main, BuildInfo, FeatureGate, Ffi, ExtApi/Ctx, EmbedApi, Data*, DeclCheck, Lint, Profiler | ~7.1K |
| L13-servers | Repl, Lsp, McpServer, JupyterKernel | ~3.3K |

**The known-open ledger is part of the scope.** Every lane carries the
items earlier reviews deferred (REVIEW-2.0 and REVIEW-3.7's deferred lists)
plus the open items in the project notes — the two duplication-audit
standouts (`.raku` dropping inherited attributes; objects as Set elements /
Hash keys), the invokeMethod/callCallableRaw twin, the module `use` leak,
the eager-sink bug, silently-ignored exported operators, in-flight captures
invisible to lookahead, the LSP server "needs review", and the rest. Each
gets closed or explicitly re-deferred with evidence; the review's deferred
list is the new ledger.

**Baselines** — measured on a clean arm64 build of the start commit in a
detached worktree (`rc-work/review-grand/base/`), so no peer session's
uncommitted edit leaks in. Roast (three runs, union list), `t/run.raku`, and
the perf guard's numbers are recorded in `rc-work/review-grand/gates/` before
the first batch. Rollback: branch `backup/pre-grand-review`.

**Batches** — grouped by risk, as before: dead code first (zero behaviour),
then parser/lexer, interpreter correctness, codegen/JS twins, regex,
runtime/CLI. Per batch: fix → the new `t/regression/` case on BOTH engines →
`t/run.raku` → full Roast with the per-file join (not the totals; a partial
file losing an assertion is invisible to the pass-list diff) → next batch.
Perf-sensitive batches add an interleaved A/B against the baseline binary.
Slim gate and `--exe` optbench identity at the end of any batch that
touches Codegen, Interpreter-across-file calls, or the runtime archive.
Nothing is committed by the review itself — the user batches commits — and
peers are told the file list before a batch touches the tree.

**Exit** — `docs/dev/findings/REVIEW-GRAND.md` written in the REVIEW-2.0
shape: baselines, the one-paragraph verdict, per-batch lists, final gates,
and the deferred ledger. Each entry tagged with its doc impact, which is
phase 2's and phase 3's input.

## Phase 2 — user docs

Every example run on the current engine through the conformance harness;
every count re-measured (the FEATURES / ROAST / README / REFERENCE numbers
must agree); every link resolved; tone per the house rule (raw numbers, no
boasting). CHANGELOG is a record: link and current-claim checks only. Output
includes the list of raku.online sections to republish, since several are
generated from these files.

## Phase 3 — the book

Per chapter: the code it cites still exists under that name and shape, the
decision it explains still holds, and the history runs through v3.25 and
the review itself. Chapter 8 already shows the pattern (the struct that
exists beside the one it was written against). `docs/internals` gets a
decision: merge into the book, or keep as the short form with cross-links.
The PDF is rebuilt at the end.
