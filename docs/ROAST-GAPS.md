# What still blocks a full Roast pass

*A classification of every currently-failing Roast file. Originally scanned at
the 350-file baseline (2026-07-11); the per-bucket counts in sections B–F are
from that scan. **Current standing after working the list: 401 / 1,464 fully
passing, 152,316 / 188,486 declared assertions (80.8%).** A re-scan of the
no-TAP set at the 400-file mark (`rc-work/notap-now.tsv`, git-ignored) shows
the remaining 449 no-TAP files bucket as: 104 `expected )`, 91 missing
method (26 of them `.new` on missing types), 62 term-position operators,
48 `Confused`, 26 `expected {`, 24 missing routines, 13 borderline-slow,
12 declarator forms, plus the long tail.*

The suite originally split into 350 fully-passing files, 622 partial,
478 no-TAP and 12 timeouts; it now stands at **401 / 603 / 449 / 9**. The
blockers fall into six classes, ordered roughly by leverage; struck-through
items are done.

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

- ~~**`sprintf` float flag combos**~~ **DONE** — the `-`+`0` Rakudo quirk
  (precision+1, zero-pad-left, `-` never left-justifies) is implemented as
  roast encodes it; plus big-Int/big-Rat `%b/%o/%x/%u`, positional `%2$s`
  arguments, `%n`/`%p` dying, and `NaN`/`Inf` casing. `sprintf-f.t`
  (2,282 tests) fully passes; `sprintf.t` 167/174. **+484 assertions,
  standing 401 / 152,316 (80.8%).**
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
   write-through (`sub undefine(\a) { a = Nil }`).
   *Batch 3* (+4 full: `env.t`, `catch_type_cast_mismatch.t`, `ro.t`,
   `atanh.t`): caught errors stored in `$!`/`$_` are now always **defined
   exception instances** (`.defined`/`.message`/`~~ X::Type` all work even for
   bare-type throws; exceptions gist to their message); `is readonly` on a
   variable is the spec compile error; `sub infix:<<M>>` op names; Capture
   associative indexing (`c<named>`) with plain-Array `$aref<k>` a type error;
   `$hashref[0]` returns self; `cmp-ok` handles any infix (`===`, `eqv`, …);
   `$` contextualizer as a listop argument (`ok $%*ENV`). Standing:
   **393 / 1,464, 143,490 assertions.**
   *Batch 4* (+2 full: `basic-types.t`, `sprintf-d.t` — the latter 4,565 tests):
   bare/pointy blocks are **Block** (`{ $^a }.WHAT`, `isa-ok -> {}, Code` via a
   Code-family isa hierarchy; anonymous `sub {}` stays Sub); `sprintf %d`
   formats big Ints from their exact decimal digits (no more saturation at
   2⁶³); stray `next`/`last`/`redo` outside any loop is an error and counts as
   a death for `dies-ok`; `pointy.t`/`control.t` no longer abort mid-file.
   Standing: **395 / 1,464, 143,537 assertions (76.2%).**
5. ~~**Emoji/UAX#29 data pass** (D)~~ — **DONE, +4,056 assertions, +12 full
   files (378 → 390)**. Grapheme breaking now uses REAL UCD 17.0 data
   (`tools/gen_unicode_gb.py` → `unicode_gb_gen.cpp`: GraphemeBreakProperty +
   emoji-data ranges) instead of a general-category approximation — that fixes
   skin-tone modifiers (Sk but Extend), ZWNJ (Cf but Extend), Prepend, and the
   exact Extended_Pictographic set for GB11 — plus rule **GB9c** (Indic/Myanmar/
   Khmer/Balinese conjuncts via InCB). `.comb` segments by grapheme (shared
   `uniGraphemeStarts`). Normalization tables regenerated from UnicodeData.txt
   17.0 (`tools/gen_unicode_norm.py` → `unicode_norm_gen.cpp`) instead of
   Python's lagging `unicodedata`; `Uni.Str` NFC-normalizes (NFG semantics).
   Now fully passing: emoji-test (3825), GraphemeBreakTest-0..3, mass-equality,
   nf{c,d,kc,kd}-9, nfk{c,d}-1. **S15: 99.9% of assertions, 68/82 files.**

6. **UCA collation (`unicmp`/`coll`)** — a full Unicode Collation Algorithm
   implementation from DUCET 17.0 (`tools/gen_unicode_coll.py` →
   `unicode_coll_gen.cpp`): 38,785 single-codepoint entries + 964 contractions,
   longest-first contiguous matching (a 3-cp contraction's 2-cp prefix need not
   be an entry), **discontiguous matching** per S2.1 (NFD reordering splits
   contractions like `0DD9+0DCA`), implicit weights with the real
   `Unified_Ideograph` property and the Tangut/Nushu/Khitan fixed bases, and
   the conformance-test codepoint tie-break. Plus `Q««…»»` guillemet quote
   parsing and `Uni.new(@list)` flattening. **All 4
   `CollationTest_NON_IGNORABLE` files (8,271 tests) pass with zero real
   failures.** Standing: **400 / 1,464, 151,831 assertions — 80.6%.**

7. **UCD 17.0 everywhere + dogfooded generator** — all remaining tables
   (names, categories, scripts, blocks, bidi, binary props) moved to 17.0;
   the names/categories/numerics generator is now
   [`tools/gen-unicode.raku`](../tools/gen-unicode.raku), written in Raku and
   run by rakupp itself (see [DOGFOODING.md](../DOGFOODING.md)). Hangul
   syllable names synthesized both directions, Unihan numerals in `unival`.
   Dogfooding surfaced and fixed a real bug: `.hash` on a flat list now pairs
   consecutive elements (`@a.kv.reverse.hash`). Standing: **400 / 1,464,
   151,832 assertions (80.6%)** — plus two S17 stress files that flip
   400↔402 with suite load.

8. **Where the leverage is now (401 / 152,316 / 80.8%).** The re-scan of the
   449 remaining no-TAP files (`rc-work/notap-now.tsv`) points at the next
   round, ordered by unlocked assertions:
   - **`Rat.new` / `Complex` literals** — a single missing constructor gates
     `rat.t` (869 declared), and the `i` postfix gates `complex.t` (557). Both
     are one-feature fixes behind very large files.
   - **One-line parse blockers on big files** — `reduce.t` (580, `[\(…)]`
     reduction meta at col 25), `rx.t` (756, parse at 648),
     `multislice-6e.t` (549, adverb `:` at 286). Each aborts an otherwise-large
     file at a single construct.
   - **The `.new` batch** (26 files) — `Version`, `Duration`, `SetHash`/
     `BagHash`/`MixHash`, `buf8`/`blob8`, native shaped arrays (class C).
   - **The `expected )` cluster** (104 files) — the single largest first-error
     bucket; term-position operators (62) and `Confused` (48) follow.
   The cheapest *file-count* growth stays the 252 partials losing only 1–2
   tests each (attack-order item 4); the biggest *assertion* wins are the two
   constructors above.
