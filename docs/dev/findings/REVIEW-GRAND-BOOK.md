# The Grand Review, phase 3 — the Internals book — 2026-09-07/08

The last of three phases (source → user docs → the book; the plan is
`docs/dev/plans/GRAND-REVIEW-PLAN.md`, phase 1's result is `REVIEW-GRAND.md`,
phase 2's is `REVIEW-GRAND-DOCS.md`). Ten read-only lanes read all 47 chapters
and all 12 `docs/internals` pages in full — 18,953 lines across 59 files — and
checked three things per chapter, in the order the plan sets: the code it cites
still exists under that name and shape; the decision it explains still holds;
and the history is current.

**247 findings**, nearly all confirmed by a run, a grep or a measurement. The
working ledger is `rc-work/review-grand/TRIAGE-3.md`; the lane reports are
`rc-work/review-grand/findings-3/B1..B10.md`.

## What the phase found, in one paragraph

The dominant defect is not staleness. It is that **the book and
`docs/internals` were frequently the same document**, and that two copies of one
explanation rot together and get fixed once. Chapters 4-7 and `PARSING.md`
shared ten of twelve code blocks, errors included. Chapter 23 and
`REGEX-LTM.md` shared the same examples and the same measured table byte for
byte, and neither named the other. Chapter 9 and `STRINGS.md` were the same
eleven sections. In every pair the two carried the *same* error, and in every
pair there was exactly one place they contradicted each other — where, twice,
the short form was the one that was right. One lane found seven cases where a
correction had been applied once and its twin missed.

The second pattern is a chapter that is wrong because it is honest: it quotes
the source faithfully, and the source contradicts itself. Chapter 11 called
BigInt division a per-limb binary search because `src/BigInt.cpp:286` still did,
twenty-three lines above the comment saying it is Knuth's algorithm D. Chapter
37 had the concurrency default backwards because the comment above
`parallelMode_` in `src/Interpreter.h` did.

## The five worst, in order

1. **Chapter 15 printed the bug that a commit fixed, as the fix.** The
   cooperative loop-control guard `curLoopFrame != 0 && …` is what 3941055
   removed: `0` was both "no frame armed" and the mainline's own frame, so every
   top-level loop and `given` threw instead — ~80 µs per iteration, in exactly
   the programs a beginner writes.
2. **`--aot` was explained as the design that was deleted for being lossy** — in
   two chapters and in `ARCHITECTURE.md`, found independently by two lanes. The
   per-node emitter silently dropped fields, so `my Int(Str) $n = "42"` compiled
   into a binary that threw a type error, and compiled diagnostics lost their
   line numbers.
3. **Chapter 21 documented a queue-and-drain mechanism that does not exist** and
   was rejected as "observably wrong". It was written from a comment in
   `Regex.h` with no member under it.
4. **Chapter 31's central code block quotes deleted code.** `rakupp_web.cpp` was
   rewritten onto the public C embedding API, and its header lists exactly the
   two things the chapter still teaches as current.
5. **Blocks a reader would paste and be harmed by.** `@a := @b` cleared
   `isList`, a line removed on purpose, reintroducing two fixed bugs;
   `strToNumOr0` recommended `std::strtod` where the engine calls
   `cnum::strtod` precisely because the C library follows the host locale;
   `nativeCifCache` was given the `void*` it had before it needed a copy
   constructor, reintroducing a double-free.

## Gates this phase produced

- **`tools/check-book-symbols.raku`** — every C++ name the book cites, looked
  for in the source it describes. It grew from one rule to four as the lanes
  showed which classes it was blind to: qualified names, member names by the
  trailing-underscore convention, exception types, and AST node kinds. Across
  both trees the added rules have no false positives, and they flag exactly the
  four errors the lanes found by reading — `atomDropEnd_` in two documents (the
  field is `unspaceEnd_`), and `X::Role::Unimplemented` in two more (the engine
  throws `X::Comp::AdHoc`). It now reports **0 missing of ~250 cited names**.
- **`tools/check-book-appendix.raku`** — Appendix B against `kFlagDocs`, the
  declarative table `--help` prints from. The appendix named 39 of 66 flags,
  including no `--lsp` and no `--target=js` group at all. The gate fails in both
  directions, so a removed flag cannot linger either.
- **`tools/check-doc-links.raku`** (from phase 2) covers both trees: 1,198 links
  and heading anchors resolve.

## The merge

Eight of the twelve `docs/internals` pages are now stubs pointing at the
chapters that absorbed them: `PARSING`, `REGEX-LTM`, `STRINGS`,
`NODE-SPECIALIZATION`, `CLASSIFICATION`, `MODULE-LOADING`, `NQP`, `DISPATCH`.
They keep their paths, because thirty-odd files link to them, and each stub says
what was merged and what the two copies had disagreed about.

`ARCHITECTURE.md`, `OPTIMIZATION.md`, `METAPROGRAMMING.md` and `RUNTIME.md`
stay. `ARCHITECTURE.md` lost the two sections that duplicated chapters — its
walk-through of the run modes and its `Value`/`Env` miniature — because those
were exactly where it had rotted, and both now point at the chapters with a note
saying why they were removed rather than updated.

## What the book gained

A chapter for **`--target=js`**, which appeared nowhere in either tree before
this phase despite being 2,759 lines of emitter, 5,070 hand-written lines of
runtime, five flags and its own corpus gate. It is placed at the end of Part VII
and its spine is the principle chapter 26 rests on — the generator never
reimplements the runtime — and the fact that this back end had to give it up.
43 chapters, 397 pages.

## Engine findings, not documentation

Four came out of the review and are tracked separately:

- **A regex silently stops matching past ~8 M characters into the subject.**
  `src/Regex.cpp:2518`'s step budget is shared across start positions, so an
  unanchored scan spends it before matching. `~~`, an anchored match and
  `.subst` all answer "no match"; Rakudo matches at any size. Found by the
  symbol checker grepping a 10 MB corpus.
- **`use Foo:ver<9.9+>` loaded a distribution that does not satisfy it** when its
  META6 `provides` maps the module: the fast path resolved without consulting
  `verReq`. **Fixed** — the mapping is gated exactly as the name-derived paths
  beside it are, with `t/regression/use-ver-meta-provides.raku` pinning both
  answers on both engines.
- **`--lsp` never reported an undeclared variable.** `src/Lsp.cpp` included
  `Lint.h` and not `DeclCheck.h`, so the language server published strictly less
  than `--lint` — breaking a rule chapter 38 itself states. **Fixed** — both now
  build the same finding list, pinned by
  `t/regression/lsp-reports-undeclared.raku`.
- **A worker thread's own loop costs ~15% more than the main thread's**, which
  caps every parallel ratio the project publishes.

Four source comments were corrected too, each the root of a book error:
`src/Interpreter.h`'s concurrency default, `src/BigInt.cpp`'s binary search,
`src/LtmNfa.h`'s "oracle harness only", and `src/Value.h`'s pointer to a file
that does not exist.

## What is left

The lanes ranked by reader impact and this phase applied the top of that
ranking: every wrong code claim and stale decision named above, the merges, and
the numbers that had moved far enough to mislead. What remains is the tail —
smaller stale figures, excerpts presented as complete without an elision mark,
and coverage gaps the lanes recorded (`take-rw` and `monitor` appear in no
chapter; Appendix C leaves 17 files unmapped). Each is in its lane report with a
ready-to-paste fix.
