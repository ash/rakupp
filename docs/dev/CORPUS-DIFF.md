# Corpus differential run — 2026-07-18

> **UPDATE (same day, after the first fix batch):** **1475 / 1789 exact matches
> (82.4%)**, up from 1443. Fixed: clusters 3–8 below plus the grammar pipeline
> (cluster 1): `%%` trailing separator in the regex engine, Rakudo-shape
> `Match.gist` (one line per list-capture element, position-ordered,
> depth-indented), `parsefile` no longer strips the trailing newline,
> `$/.make(v)` writes through to the parse-tree node, `.comb("")` == `.comb`,
> Date..Date ranges, `Date.succ/pred`, `:formatter` in say/gist, `first(:k/:kv/:p)`
> on lazy seqs, type-aware `eqv`, `$:name` named placeholders,
> placeholder-arity `for` batching, flattening-slurpy `@_` (with synthesis in
> for-bodies), `[Z]/[X]/[,]` empty-reduce identities, `[<] ()` → True, `.IO` on
> undefined dies, `Grammar.parse(Any)` dies. The `lang` compiler, both mini-langs,
> both Brainfuck interpreters, calc1 and grammar1 now match byte-for-byte.
> Roast gate: 529 fully-pass (baseline 507), no regressions attributable to the
> batch (S02-magicals/sub.t fails identically on the pre-batch binary;
> S32-io/out-buffering.t is load-flaky). Data: `corpus-diff/rp-verdicts2.tsv`,
> `rp-classes2.tsv`. Still parked: grammar2/4 (embedded code blocks reading
> `$0`/`$<name>` mid-match, probe double-execution), op48 (`:=` container
> aliasing), metaop18 (`S&`), channel9 deadlock.

First run of the [raku-corpus](https://github.com/ash/raku-corpus) differential
harness: every corpus program with a deterministic Rakudo reference output was run
under rakupp and compared byte-for-byte on stdout + exit code (stderr ignored:
error wording is implementation-specific).

- Corpus: ash/raku-corpus @ `5774cf7` (1931 programs; 1789 with reference outputs)
- Reference: Rakudo v2026.06, TZ=UTC, sandbox dir copy, stdin /dev/null, 30 s limit
- rakupp: `build/rakupp` of 2026-07-18 21:40 (= build-arm64/rakupp-c52)
- Data: `docs/dev/corpus-diff/rp-verdicts.tsv` (verdict, rakupp exit, expected exit,
  ms, file) and `rp-classes.tsv` (diff class per mismatch). Mismatch outputs are in
  the session scratchpad `rp-results/` tree (regenerate by rerunning the harness).

## Headline

**1443 / 1789 exact matches (80.7%).**

| Verdict | Count | Meaning |
|---|---:|---|
| MATCH | 1443 | stdout and exit code identical |
| DIFF-OUT | 242 | same exit code, different stdout |
| DIFF-BOTH | 90 | different stdout and exit code |
| DIFF-EXIT | 13 | same stdout, different exit code |
| TIMEOUT | 1 | rakupp exceeded 30 s (`snippets/perl6tests/parallel/channel9.pl` — channel deadlock?) |

Per section: advent-of-raku-2020 100%, course 97%, calendar 90%, at-a-glance 86%,
PWC 84%, project-euler 76%, using-raku 75%, advent-of-code 73%, snippets 70%,
programs 69%, one-liners 68%.

## Mismatch classes

| Class | Count | Notes |
|---|---:|---|
| VALUE-DIFF | 245 | genuinely different output values (clusters below) |
| STDERR | 75 | rakupp emitted an error (parse / runtime) |
| EMPTY-OUT | 12 | rakupp printed nothing where Rakudo printed output |
| ORDER-ONLY | 10 | same lines, different order (hash iteration / thread timing) — harness should sort-compare these |
| FLOAT-PRECISION | 3 | differ only past 4 decimal digits |
| WHITESPACE | 1 | |

## Bug clusters (by breadth, with sample repro files)

1. **Grammar-driven programs silently produce nothing** — the whole
   grammar/actions pipeline: `programs/lang/lang.pl`, `programs/perl6-grammar-play/
   mini-lang/lang*.pl`, `books/perl6-at-a-glance/grammar1/2/4.pl`, both Brainfuck
   interpreters (`books/using-raku/brainfuck.pl`, `snippets/yr2017-perl6/20-brainfuck.pl`).
   Runs exit 0 with empty stdout, or print `(Any)`.
2. **Fully-qualified calls to module subs fail** — `Undefined routine 'Math::sum'`,
   `'Address::get_next'`, `'N::n'` etc. (~12 files): `books/perl6-at-a-glance/mod9.pl`,
   `snippets/perl6tests/oop/22.pl`. Also missing builtins spotted in the same
   cluster: `rename`, `move`, `take-rw`.
3. **Bundled one-liner flags `-ne'...'` / `-npe'...'` rejected** ("Illegal option") —
   Rakudo accepts the glued form; 12 of 16 book one-liner `.sh` files fail on this
   alone. Trivial argv-parsing fix, big one-liner compatibility win.
4. **Spurious trailing output in sink context** — an extra `Nil`, `(Any)` or blank
   line after correct output: `books/perl6-at-a-glance/calc1.pl`,
   `books/raku-one-liners/13a/13b/16a/16b*.raku`.
5. **Date arithmetic** — counting Sundays over a `Date` range gives 0 instead of 171
   (`one-liners/33a,34a,34b`), custom `Date.new(..., formatter => ...)` ignored
   (`one-liners/32`), `No such method 'day' for invocant of type 'Int'` (3 files).
6. **`first(..., :k)` returns the value instead of the index**
   (`one-liners/22-fibonacci-1000-digits-index.raku`: 10⁷⁶-digit number vs 4782).
7. **Slurpy hash parameters lose their keys** — `one-liners/43-slurpy-hash.raku`
   prints ` = ` lines.
8. **Relational/junction operator wrong polarity** — True/False inverted:
   `books/perl6-at-a-glance/op45.pl`, `op46.pl`, `op48.pl`, `metaop18.pl`.
9. **`**=` / compound-assignment anomaly** — `sub28.pl` prints 1 instead of 625;
   also `Unsupported operator '+='` errors in 3 files and
   `Target is not assignable` in 5.
10. **Unicode introspection gists** — `Str.encode` should gist as
    `utf8:0x<6E 61 ...>` (prints the plain string instead), `NFC/NFD/NFKC/NFKD`
    gists wrong (`uni2/3/4.pl`).
11. **Exception text and backtrace** — e.g. Rakudo: `Attempt to divide 1 by zero
    when coercing Rational to Str` + backtrace; rakupp: shorter message, no
    backtrace (`try1.pl`). Only matters where programs print exceptions to stdout.
12. **`dir()` gist** — `./name` vs Rakudo's `"name".IO`; also these dir-listing
    programs' reference outputs are stale (captured before `.precomp` cleanup) and
    inherently environment-dependent → regenerate + flag in the corpus.

## Not rakupp bugs (harness policy)

- `18b-random-integers-seeded.sh`: `srand(N); rand` sequences are
  implementation-specific — reclassify as cross-implementation NONDET.
- ORDER-ONLY class: compare sorted, or flag as hash-order-dependent.
- FLOAT-PRECISION: decide a tolerance policy (compare to ~10 significant digits).

## Next steps

- Fix clusters top-down (1–4 are the broadest); rerun harness after each fix —
  the MATCH count is the regression metric.
- Regenerate reference outputs for dir-listing programs; add per-file
  overrides (sort-compare, float tolerance, skip) to the corpus metadata.
- Rerun the 16 corpus TIMEOUT programs with a larger budget to grow the
  reference set.
