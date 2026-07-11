# What still blocks a full Roast pass

*A classification of every currently-failing Roast file, from a systematic scan
(2026-07-11): each of the 478 no-TAP files was run once and its first error
bucketed; each of the 622 partial files was re-run and its failing tests
collected. Raw diagnostics: `rc-work/notap-diag.tsv`, `rc-work/partial-diag.tsv`
(git-ignored). Baseline for this scan: 350 / 1,462 fully passing, 138,000
assertions. (Item A1 below has since landed, moving the standing to 369 /
138,573 — the per-bucket counts predate it.)*

The suite splits into 350 fully-passing files, **622 partial** (run but lose
9,697 assertions between them), **478 no-TAP** (abort before any test runs) and
**12 timeouts**. The blockers fall into six classes, ordered roughly by
leverage.

## A. Test-infrastructure gaps (a multiplier, not a language gap)

1. ~~**`#?rakudo skip` fudge directives are not honored.**~~ **DONE** — the
   lexer's fudge pre-pass now implements `skip` faithfully to roast's own
   `fudge` tool (comment out the next N test statements / column-0 blocks, emit
   `skip($reason, numtests)`, count tests via the fudge `$IS` function list and
   `#?DOES n`), plus `emit`, with `eval`/`try` treated as skip. Result:
   **350 → 369 fully-passing files (+19), zero regressions**, no-TAP 478 → 464,
   timeouts 12 → 10. Some of the guarded constructs *Rakudo itself* cannot run
   (`::=` is NYI there; its fudger comments them out the same way).
2. **Borderline-slow files.** 12 files emit clean TAP standalone but were
   no-TAP under the harness (`gb18030/gb2312/shiftjis-encode-decode.t`,
   `substr-rw.t`, `advent2009-day21.t`, …) — they straddle the 10 s timeout
   under full-suite load.
3. **Two files wedge the harness itself** (their killed child becomes
   uninterruptible): `S04-statements/try.t` (uncaught `ResumeEx` hangs during
   teardown — the `.resume` control flow needs real support) and
   `S12-construction/destruction.t` (`await start { loop }` waiting for GC
   finalization that never comes). Run the suite with these moved aside.

## B. Parser gaps — 334 no-TAP files stop at a parse error

First-error buckets (a fix often reveals the next error, so these are lower
bounds on files unlocked, but the clusters are real):

| Cluster | ~files | Example (verified vs Rakudo) |
|---|---:|---|
| Indirect-invocant colon in calls | 18+ | `key($pair:)` ≡ `$pair.key`; also chained adverbs `f(:x("a"):y("b"))` |
| Zen slice `%h{}` / `@a[]` as postfix on vars | ~19 | `%h{}.join` — most of the `Confused (got '}')` bucket |
| Pseudo-package access | ~11 | `::<$x>`, `CALLER::`, `MY::<$x>`, `OUR::` (the term-position `::` bucket) |
| Unicode operators & identifiers | ~15 | `−` (U+2212 minus), `∓`, subscript `₀`, curly quote `’` in identifiers, `「…」` |
| Hyper markers in unexpected positions | ~15 | `»`/`<<`/`>>`-led forms (`«$x»` interpolation words, `»+«` variants) |
| Declarator forms | ~14 | `my Array of Int @box` (`of`), anonymous `my sub {42}()`, `my … constant \a .= new: 42` |
| Literal/exotic parameters | ~10 | `multi m(-1)` negative literals, non-ASCII digits `f(-١)`, `submethod BUILD(::?CLASS:D:)` |
| Quote-adverb tails | ~11 | `qq:!a:!c/…/` (negated adverbs), radix with expression `:16<dead_beef*16**8>` |
| Operator names in `<<…>>` | few | `&infix:<<(<+)>>` |
| Long tail | rest | one-off constructs; see `rc-work/notap-diag.tsv` |

## C. Missing types & runtime subsystems — 125 no-TAP files die at runtime

| Subsystem | Blocks | Notes |
|---|---|---|
| **Iterator protocol** (`.iterator`, `pull-one`, …) | `range-iterator.t` (0/690!), `set/bag/mix-iterator.t`, `S32-list/iterator.t`, S07 | biggest single runtime gap; also behind several partials |
| **Lazy/infinite lists** | most of the 12 timeouts | `^Inf .head/.skip` materializes eagerly and spins (`head.t`, `skip.t`, `range-int.t`) |
| **Native buffers & shaped arrays** | ~10 | `buf8/blob8/utf8.new`, `Array.new(:shape)`, native `is rw` traits |
| **Mutable quanthashes** | ~4 | `SetHash/BagHash/MixHash.new` |
| Missing `.new` on assorted types | ~15 | `Version`, `Duration`, `Format`, `Collation`, `Lock::Async`, `IO::Path::{Win32,Unix,Cygwin}` |
| `$*SCHEDULER.cue` | 5 | |
| Metamodel tail | ~10 | `.^lookup`, `.^find_method` variants, `.WHY` (declarator POD), `Metamodel::…new_type` |
| Missing routines | ~23 | `deepmap`/`duckmap`, `cross`, `unimatch`, `symlink`/`chdir`/`cat`, `cas`, `once`, … |

## D. Unicode data & algorithms — 43% of all lost assertions in partials

S15 alone loses 4,139 of the 9,697 partial assertions:

| File(s) | Lost | Root cause |
|---|---:|---|
| `emoji-test.t` | 2,908 | emoji **modifier/ZWJ sequences** (skin tones etc.) must form one grapheme |
| `GraphemeBreakTest-{0..3}.t` | ~530 | UAX #29 edge rules (regional indicators, InCB, …) |
| `mass-equality.t` | 420 | canonical equivalence over newer combining marks (e.g. U+1ACF+) |
| `nf{c,d,kc,kd}-9.t` | 272 | normalization tail for recent UCD additions |
| `unicode-whitespace.t` | 50 | `.words` / whitespace classes over exotic spaces |

This class is pure data/algorithm work against UCD tables — no parser or
architecture changes.

## E. Semantic bugs — wrong answers in otherwise-running files

The long tail of the remaining ~5,500 lost partial assertions. Confirmed
recurring clusters:

- **`sprintf` float flag combos** — `%-08.2f`-style flag interactions (470 lost
  across `sprintf-f.t` + `sprintf.t`).
- **NFG-aware string ops** — `index`/`substr` with accented chars & multiple
  needles (`index.t`, 102).
- **Magic string increment beyond Latin** — `'ΩΩ'++` should be `'ΑΑΑ'`
  (`autoincrement-range.t`, 81).
- **Regex `:nth`/`:x` adverb semantics + exception messages** (`counted.t`, 85).
- **Subset type-constraint enforcement on assignment** (`subset.t` family).
- **PRE/POST phasers, `.first` return shape, placeholder-var corners,
  loop-control details** — a few tests each across many files.

252 partial files lose **only 1–2 tests** — the cheapest route to growing the
fully-passing count; 140 files lose >10 and usually indicate a class-C/D
subsystem above.

## F. Concurrency timing (S17) — environment-sensitive

305 lost assertions + 4 timeouts. Measured identically on clean HEAD on the
same machine, so much of this is scheduling/timing sensitivity of the GIL-based
runtime rather than logic regressions; the `Proc::Async` stress files
(`stress.t`, `many-processes-*.t`, `no-runaway-file-limit.t`) also hit real fd
/process-management limits.

## Suggested attack order

1. ~~**`#?rakudo skip` fudge support** (class A1)~~ — **DONE, +19 files
   (350 → 369), zero regressions.**
2. **Parser clusters B1–B3** (invocant colon, zen slice, pseudo-packages) —
   each is one contained parser feature blocking 10–20 files.
   *B1 and B2 landed:* indirect-invocant colon `key($pair:)`, chained adverbs
   `f(:x(1):y(2))`, zen slice `%h{}`, plus a lexer fix so `:y(…)`/`:q(…)`/`:s(…)`
   adverbs tight after `:` are pairs, not quote-forms. no-TAP 464 → 455,
   +79 assertions, no regressions. *B3 (pseudo-packages) still open.*
3. ~~**Iterator protocol + lazy `^Inf`** (C)~~ — **largely DONE**: the full
   Iterator protocol landed (`.iterator` on collections; `pull-one`,
   `push-all/-exactly/-at-least/-until-lazy`, `skip-*`, `sink-all`,
   `count-only`/`bool-only`, `IterationEnd`), `set/bag/mix-iterator.t` fully
   pass, `range-iterator.t` went 0/690 → 575 passing. The `^Inf` hang was a
   C++ UB bug — casting `Inf` to `long long` gives `LLONG_MIN` on x86, so
   `^Inf` built a *backwards* range that bypassed all the lazy handling;
   `toInt()` now saturates. Lazy `grep`/`first`/`skip` compose over infinite
   sources, and `head.t`/`skip.t`/`range-int.t` converted from timeouts to
   partials. Also fixed: a bare untyped `my @a` re-evaluated in the same scope
   (loop conditions) keeps its container; pseudo-packages (`::<$x>`, `MY::`,
   `$OUR::x`, `CORE::<&not>`, `$PROCESS::IN`) parse and resolve via the scope
   chain. Standing after this batch: **372 / 1,464, 139,647 assertions.**
4. **The 252 one-or-two-test partials** — steady fully-pass growth.
   *First two batches landed* (+6 full): file-level ENTER before mainline,
   BEGIN-visible list declarations `my ($a,$b)`, `1<2` = whitespace parse error,
   `uniname` → `<unassigned>`, `Regex ~~ Hash/Array` (any key/element),
   `X::* ~~ Exception`, `$*KERNEL.release`, `Raku.KERNELnames`, sigilless-param
   write-through (`sub undefine(\a) { a = Nil }`). Standing: **378 / 1,464,
   139,688 assertions.**
5. **Emoji/UAX#29 data pass** (D) — one data-table project worth ~3.5k
   assertions.
