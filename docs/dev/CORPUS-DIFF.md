# Corpus differential — Round 3 (2026-07-22, same day, after the fix batch)

**1509 / 1789 exact matches (84.3%)** — 19 files fixed over Round 2's 1490, zero
regressed. Fixed clusters: glued `-ne'…'`/`-npe'…'` one-liners (with the
`perl6`/`raku` shim restored in the harness for `.sh` wrappers), typed-scalar
assignment of undefined values (the "prompt at EOF" cluster's real cause),
big-part Rat→Num single-rounding, the bare-`$` anonymous state variable
(`say ++$` line numbering), and qq escapes in substitution replacements
(`s/$/\n/`). Roast moved 194,501 → 194,506 across gates cb1–cb3 (type.t +2,
classify +1, weird-errors +1). Verdicts: `corpus-diff/rp-verdicts4.tsv`;
**MATCH = 1509** is now the regression metric. The cluster table below is
otherwise unchanged — grammar/Match residue remains the top target.


---

# Corpus differential — Round 2 (2026-07-22, v1.0.0 + hp1)

Re-run of the full harness against the corpus's committed Rakudo v2026.06
references (`expected/` + `run-status.tsv`, same protocol: sandbox copy, TZ=UTC,
stdin /dev/null, 30 s alarm, `-I. -Ilib -I../lib`).

**Headline: 1490 / 1789 exact matches (83.3%)**, up from 1475 (82.4%) at Round 1's
fix batch. 37 of the 299 mismatches are environmental (`src-hint` files: the
reference recorded a 2026-07-18 date, a srand sequence, thread ids, or machine
paths) — **against the 1,752 stable references the match rate is 85.0%**, and the
real divergence backlog is ~262 files. Verdicts: `corpus-diff/rp-verdicts3.tsv`;
per-file clusters: `corpus-diff/rp-classes3.tsv`.

## What's left, clustered (fix-priority order)

| # | Cluster | Files | Notes / example |
|---|---|---:|---|
| 1 | **Grammar/Match residue** | ~41 | The largest real block. Match tree shape: named keys render positionally (` 0 => ｢letter｣` for ` type => ｢letter｣`, regex/36) and multi-level capture nesting mis-indents (grammars/6, 10). Values lost through actions: `.made`/`make` chains yield `(Any)`/`Nil` (grammar2/4 — the parked embedded-code-block cluster — plus grammars/12, 15, calculator-grammars.pl → `0`). `$/` as an action-method parameter fails its binding constraint (grammars/22). |
| 2 | **Regex-engine string ops** | ~8 | `s:g` with quantified captures (compressor.pl prints the input unchanged), `.comb` with a regex range (array-comb.pl `[21 23]` vs `[21 23 25 27 29]`), recursive/balanced patterns (balanced-brackets.pl), roman-numeral parse (roman2). |
| 3 | **CLI one-liner flags** | 14 | Glued `-ne'…'`/`-npe'…'` still rejected (12), plus `.sh` wrappers that invoke `perl6`/`raku` by name (needs argv0/shim handling in the harness or a real alias). |
| 4 | **parse gaps** | ~20 | Postfix-dot forms (`.=`, `.7`, `."meth"` — 6 files); pod declarator blocks; `polar` (Confused at `)`); underscored fractional literals (05-pi); unicode set ops `⊄` and bare-term emoji; `<<mm>>` word-quote vs hyper ambiguity; misc (std-deviation, comment edge). |
| 5 | **prompt/EOF behaviour** | 9 | `prompt` at EOF: Rakudo returns Nil and the program stops quietly; rakupp prints `(Any)` or proceeds with an empty answer (palindrome3, exceptions/04, allomorphs). |
| 6 | **encode/NF*/IO gists** | ~9 | `utf8:0x<…>`/`NFC:0x<…>` vs our `Blob:0x<…>`/codepoint arrays (uni2/3/4); `dir()` entries gist as `"x".IO` in Rakudo vs our `./x` (file6, 19-parallel-dir). Round-1 clusters 10/12, still open. |
| 7 | **exceptions & exits** | ~12 | Uncaught-exception exit codes (we exit 0 where Rakudo exits 1 — exceptions/09-2, 11, 12, 19, sleep-sort); backtrace shape (`  in block <unit> at … line N`); message detail (`divide 1 by zero`); typed-vs-AdHoc identity differences (14.pl); `X::` ACCEPTS on Int (op51). |
| 8 | **is-rw returns / lvalue subs** | 5 | `return-rw`/`is rw` sub results not assignable ("Target is not assignable", return_rw-6.pl) — the known multis/indirect rw-param gap. |
| 9 | **modules/our-scope** | ~7 | `our`-scoped vars/constants invisible cross-module (x.pl, read-pi); qualified `Module::sub()` calls; `import`; `Undefined routine 'N::n'`. |
| 10 | **numeric gists** | 6 | `Num` prints 15 significant digits where Rakudo round-trips 17 (01-multiply, 27-power); `Rat` gist digit budget (newton); `("9" + 1).WHAT` Num-vs-Int = spec divergence #42 (mul.pl). |
| 11 | **bignum edges** | 2 | `.substr(*-10)` on a large Int ignores the Whatever start (euler 048); euler 013 prints an int64-max prefix (sum path overflows somewhere the minimal repro doesn't hit). |
| 12 | **containers/gist** | ~8 | Array-vs-List brackets after hyper/assignment (`(11 13…)` vs `[11 13…]`, whitespace.pl the other way); slurpy-hash iteration order (43-slurpy-hash — insertion order vs ours); hash3. |
| 13 | **introspection** | 6 | `$*THREAD` gist, `.HOW` gist (`Perl6::Metamodel::ClassHOW.new`), `Int.^methods`, `IO::Handle` type name (we say `FileHandle`). |
| 14 | **parked from Round 1** | 4 | op48 (`:=` container aliasing), metaop18 (`S&`), channel9 + one more deadlock TIMEOUT. |
| 15 | **singles** | ~15 | classz (`!meth` private-call), 47-call-all-methods (`.^methods` walk misses), 52a/b stars-homework, EVAL-of-expression calculator (AoC-18 ×2), take-rw/rename/move builtins, `Cannot resolve caller chars(Any:U)`, recursion-depth difference, and friends — one file each, see rp-classes3.tsv. |

## Round-2 protocol notes

- References are now **committed in the corpus repo** (`expected/`,
  `run-status.tsv`) — the harness no longer depends on a session scratchpad.
  Runner: `corpus-diff/run-rakupp.sh` pattern with CORPUS/RES paths adjusted;
  eligibility = run-status rows OK/NONZERO (1,789 files).
- The 37 `src-hint` mismatches are expected drift (dates, srand, threads,
  machine paths), not bugs; consider regenerating `expected/` periodically or
  teaching the differ per-file compare policies (Round 1's still-open idea).
- MATCH count remains the regression metric: **1490** (Round 1: 1443 → 1475).


---

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
