# Perl Weekly Challenge solutions: rakupp vs Rakudo divergences

Corpus: [perlweeklychallenge-club](https://github.com/manwar/perlweeklychallenge-club)
— community solutions to 370 weekly challenges, written by many authors over
six years, in both Perl-6-era (`.p6`) and modern (`.raku`) style. Wildly
varied idiom and quality, which is exactly what makes it a good real-world
workout.

Method: every `.raku`/`.p6` file (10,428) ran under Rakudo and `rakupp`
(stdin closed, sandbox cwd, no arguments, 8 s timeout). A file counts only
when Rakudo itself can run it headlessly: Rakudo failures with no output
(missing arguments/modules/input files), Rakudo timeouts, and anything
Rakudo cannot reproduce across two runs (randomness, timing) are skipped.
Comparison is stdout + success/failure. Sweep infrastructure note: bulk-running
unknown programs on macOS produces occasional *unkillable* (UE-state) children
— the harness runs each child in its own process group and abandons what it
cannot reap, and streams results incrementally (`results.jsonl`) so a wedge
loses nothing.

## Fix-round progress

Every batch passes the zero-regression Roast gate (full run, no pass-list
drops, benchmarks equal-or-faster) before it counts here. The **original**
row is the baseline this document describes; the cluster sections below are
kept as written for that baseline.

| sweep | byte-identical | mismatch | roast gate |
|---|---:|---:|---|
| **original (2026-07-14)** | **2,663** | **4,153** | 433 / 157,905 |
| after batches 1–3 (`b504561`) | 3,295 | 3,495 | 433 / 158,067 |
| after batch 4 | 3,458 | 3,325 | 434 / 158,225 |
| after batch 5 | 3,480 | 3,294 | — (gated with 6) |
| after batch 6 (`1fe6351`) | 3,525 | 3,246 | 437 / 158,267 |
| after batch 7 (`b90ae70`) | 3,811 | 2,960 | 437 / 158,277 |
| after batch 8 (`dfa43f8`) | 3,907 | 2,863 | 438 / 159,043 |
| after batch 9 (`da81842`) | 3,931 | 2,839 | 439 / 159,076 |
| after batch 10 (`e53f4a9`) | 4,044 | 2,725 | 439 / 159,105 |
| after batch 11 (`4580aff`) | 4,045 | 2,724 | 439 / 159,119 |
| after batch 12 (`1827ddd`) | 4,087 | 2,682 | 440 / 159,205 |
| batch 12, full re-verification | 4,037 | 2,726 | (same binary) |
| after batch 13 (`3b526ff`) | 4,043 | 2,720 | 441 / 159,117 |
| after batch 14 (`ab6e52e`) | 4,047 | 2,716 | 440 / 159,461 |

Batches 1–3: unit-form MAIN body/signature binding, required-named +
where-constraint dispatch (named & slurpy), Cool.printf/sprintf, no
fabricated trailing test plan, ÷=/×=/−=, `( stmt; stmt )` sequences,
`.».method`, `<<qww>>`, `:0x` pairs, typed declaration-list members,
`so */not *` currying, `.first` Nil/:k/:end, empty .max/.min = ∓Inf.
Batch 4: `+ints` sigilless slurpies, Any single-item list semantics
(scalar .sum/.join/.min/.max/.minmax), `$x.take`, Str.parse-base/.indices/
.chop(n)/.narrow/.UInt, Baggy.kxxv, Rat.base-repeating, Date
.days-in-month/.last-date-in-month/.day-of-year, sub forms splice/zip
(:with)/classify, Array()/List()/Set()/Bag()/Mix() as call-position
constructors. Next front: output-value diffs, the 660 in-solution test
failures, the `Confused` parse cascade, hang triage.
Batch 5: subset types in multi-dispatch (base chain + `where`, +2
specificity), literal signature returns (`--> 1`) at all three `-->`
sites, `sort {comparator}, @list`.
Batch 6 (post-GLR list semantics): map/flatmap keep each block result as
one element (only Slips splice) across all three map paths; prefix `|`
produces a real Slip outside call arguments too; bare comma lists are
Lists; .pairs/.kv/.antipairs return Seq; List.minmax → Range and
Range.minmax → (min max); `ff`/`fff` flip-flop operators. Side effect:
the roast sprintf-*.t files now build their full test tables (2,282 and
4,565 rows) and still fully pass.
Batch 7 (the strictness batch, +286): Str.index/.rindex start positions
(X::OutOfRange when out of range) — ends the while-index-advance hang
class; string methods on an undefined Any die like Rakudo (the
prompt-at-EOF death class in headless runs); reading a missing file dies
with Rakudo's message; typed-array gaps read as their type's default
(`my int @a` → 0, `my Int @b` → (Int)); `sub MAIN (sig);` unit form;
`~~ tr///` returns a real StrDistance (Str = result, + = count).
Batch 8 (parse cluster + iteration): statement-condition brace rule
(`when asc { }`), `.&{…}` calls, real `[X;Y]` multislices with lvalue
autoviv, `for %h`/`for set()` → Pairs, push-autoviv, `#|[…]` multi-line
declarator comments, the full bitwise/shift family (`+<` `+>` and all
compound assigns), `[\,]`, `start =>` pairs, scalar .Array/.List.
Batch 9: infix:<=>-family calls (label/fused-token/assign-through),
with/given statement modifiers run without an implicit block (the
"Variable not declared" class), anon slurpy decls, `^^=`, sub-grep →
Seq, combinations inners are Lists.
Batch 10 (+113 — the majority/histogram cascade): sub min/max/minmax
flatten list args (`max(values %s)`), for-loop rw params (`<->` /
`-> $i is rw` write back), rotor(size => gap), dynamic-handle attribute
assignment ($*OUT.out-buffer = 0).
Batch 11: stacked X/Z metaops (`XZ+`), one-level cross/zip operands
(sublists stay whole), Slip() as a routine.
Batch 12: element-read itemization (`my @row = @m[0]` keeps the row
whole — the is-deeply nested-structure class), Z/X at list-infix
precedence with `Z,` tuples, native-container element types
(`state int @a` zeros), `*²` superscripts, spaced `/` quote delimiters.
The re-verification row re-ran the full pass set from scratch (the
incremental sweeps only re-test mismatches, so a transiently-flapping
file could stick as "pass"). Result: ~50 of the ledger passes were
such flaps; exactly one file regressed vs the round start and it
proved to be hash-order nondeterminism. The verified figures are the
honest baseline going forward.
Batch 13: paramless blocks no longer define implicit $a/$b (a Perl-5-ism
that shadowed outer variables of those names in every such block).
Batch 14: hyper postfixes (`@w»[0]`, `@n»**2`, `»++`), Str-as-one-item
indexing (`"ab"[0]` is "ab"; Blob/Buf keep byte views — encode.t
gained), parameterized types in declaration lists.

Raw data (original sweep):
[pwc/pwc-mismatches.json](pwc/pwc-mismatches.json) (file, rc pair, both
outputs truncated to 600 chars, first stderr line for rakupp failures);
full signature table in [pwc/pwc-buckets.txt](pwc/pwc-buckets.txt).

## The numbers

| outcome | files |
|---|---:|
| **byte-identical output + status** | **2,663** |
| **mismatch** | **4,153** |
| skipped: Rakudo can't run it headlessly | 3,112 |
| skipped: Rakudo timeout | 294 |
| skipped: nondeterministic | 206 |
| **total** | **10,428** |

Of the 6,816 programs comparable at all, **39% already match byte-for-byte**.
The 4,153 mismatches collapse into a much smaller set of causes — the top 40
signatures cover 3,031 of them.

## Clusters, by leverage

### One-fix, hundreds-of-files

1. **`unit sub MAIN(…)` signature variables unbound — 416 files.**
   `unit sub MAIN(*@ints where .all ~~ Int); say @ints.max;` — the unit-form
   MAIN's body runs as the mainline but the signature variables are never
   bound (`Variable '@ints' is not declared`). Rakudo binds the (empty)
   slurpy and runs.
2. **`multi MAIN(:$test!)` beside a default MAIN — 191 files.**
   A very common PWC pattern: `multi MAIN(:$test!) { use Test; … }` and
   `multi MAIN($n = 5) { … }`. We try the named-only candidate and die
   (`Required named parameter 'test' not passed`) instead of dispatching to
   the default candidate.
3. **`Str.printf` as a method — 250 files.**
   `("fmt\n").printf: args` (one prolific author's template, used across
   every challenge). We only have `printf` as a sub and on filehandles.
4. **Trailing auto-plan from `use Test` — 155 files.**
   Files that `use Test` but end up running zero tests headlessly: Rakudo
   prints nothing, we emit a trailing `1..N`/`1..0` plan line.
5. **Parse-error long tail — ~435 files** in familiar buckets, matching the
   known Roast parse tail: `expected )` (112), operator in term position
   (109), `Confused` (107), `expected method name after '.'` (32),
   `expected {` (29), `expected }` (16), `expected ]` (15), declarator
   forms (15).
6. **Hangs — 126 files** (rakupp 8 s timeout where Rakudo finishes).
   Likely eager evaluation of lazy pipelines (infinite sequences consumed
   through paths that still materialize) plus a few regex blowups. Needs a
   dedicated triage pass — each hang is also a UE-zombie risk on macOS, so
   triage must use the process-group runner.

### Composite buckets that hide real divergences

7. **Test-assertion failures inside solutions — 662 files.** The solution
   itself is a mini test-suite (`use Test; is …`); some assertion fails
   under rakupp and the file dies mid-output. Each is a real behavioral
   divergence (the same categories as below, observed through `is()`),
   ~1,200 more files' worth of signal to mine during the fix rounds.
8. **Output diffs with equal status — ~1,000 files** after the specific
   gist/numeric buckets below are taken out: value-level differences that
   need per-file inspection (many will fold into the same root causes).

### Method / routine gaps (counted individually)

`printf` 250 (above), `join` 42 (on non-list invocants), `parse-base` 23,
`sprintf` 22 (as method on Str), `splice` (sub form) 18, `zip` (sub) 17,
`key`/`value` on non-Pairs 21, `sum` 15, `take` (method form) 15,
`Array`/`Set` as subs 26, `new` on built-in types 12,
`last-date-in-month` 11, `days-in-month` 10, `push` (autoviv shapes) 11,
`minmax` 10, `indices` 9, `base-repeating` 9, `kxxv` 9, `narrow` 8,
`UInt` coercer 8, `chop` 6, `classify` (sub) 5, plus a ~150-file tail of
one-to-four-file gaps (see pwc-buckets.txt).

### Semantic divergences

- **GIST: `(..)` vs `[..]`** and similar list/array presentation — 101
  files. The same List/Seq/Array-typing family the course round worked on,
  seen from more angles.
- **Empty-reduce/extremum defaults** — 27 files: `-Inf`/`Inf` vs `(Any)`
  (e.g. `.max` of an empty list), `Nil` vs `(Any)` — 9 files.
- **Numeric formatting/precision** — 13 files (Rat vs Num rendering,
  long decimals).
- **`Target is not assignable`** — 30 files (assignment through
  method-call/complex lvalues, includes the known self-referencing class
  stub bug seen in roast's S02 perl.t).
- **Recursion depth** — 6 files (`Too many levels of recursion` where
  Rakudo completes).

### Not our bugs (catalogued, not actionable)

- **Rakudo dies, rakupp succeeds — 312 files.** Mostly Perl-6-era code
  using constructs modern Rakudo has since removed or tightened; our
  leniency runs them. A handful may be cases where we *should* also die
  (type-check laxness) — worth a skim during fixing, not a blocker.
- **`Usage:` on stdout — ~50 files**: Rakudo prints MAIN usage where we
  auto-run with defaults (or vice versa); MAIN-usage semantics differ.
- **Stderr noise**: 29 files differ only because we print a
  `Potential difficulties:` warning where Rakudo does not (or vice versa).

## Suggested fix order (next round)

Items 1–4 are single fixes worth ~1,000 files combined. Then the parse
tail (already on the 1.0 campaign roadmap — the same buckets), then the
hang triage, then the method-gap list top-down. The 662 test-fail files
become the regression harness for all of it: re-sweep after each batch and
watch the pass count climb from 2,663.
