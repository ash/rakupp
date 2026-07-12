# Raku++ vs Rakudo on RosettaCode

A real-world stress test: fetch Raku solutions straight off
[RosettaCode](https://rosettacode.org/wiki/Category:Raku) and run each one under
**both** Rakudo and Raku++, comparing the output. Unlike Roast (a curated spec
suite), these are full, idiomatic programs that reach for every corner of the
language — and many external modules — so it is a much harsher bar.

## Method

For each task in `Category:Raku`, the harness (`tools/rc-compare.raku`):

1. fetches the wiki page, extracts the **first** Raku solution
   (`<syntaxhighlight lang="raku">…`);
2. runs it under `raku` (Rakudo) and `rakupp`, each with a **10 s timeout** and
   **closed stdin** (so input-readers hit EOF instead of hanging);
3. compares stdout and buckets the result.

## Results (333-task sample)

| Category | Count | Meaning |
|---|---:|---|
| `rakudo-error` | 91 | Rakudo itself errors — needs a module / a file / arguments. **Not comparable.** |
| `rakudo-timeout` | 29 | interactive or long-running under Rakudo too. **Not comparable.** |
| `no-code` | 16 | no Raku solution extracted (sub-pages, unusual markup) |
| **`rakupp-error`** | **108** | Rakudo runs clean, rakupp errors — a real gap |
| **`rakupp-timeout`** | **8** | rakupp doesn't finish in 10 s |
| **`DIFFER`** | **47** | both run, output differs (a bug **or** nondeterminism) |
| **`MATCH`** | **34** | byte-identical output ✅ |

Excluding the ~136 tasks that aren't usable as an oracle (Rakudo errors/timeouts,
no code), **~197 tasks are directly comparable**:

- **byte-identical (MATCH): 34 / 197 ≈ 17%**
- **runs to completion (MATCH + DIFFER): 81 / 197 ≈ 41%**
- errors/timeouts: 116 / 197 ≈ 59%

A sample of clean matches: *99 bottles of beer, A+B, Abstract type, Accumulator
factory, Ackermann function, Fivenum, FizzBuzz, Formatted numeric output,
Forward difference, Fractal tree, Find minimum number of coins…*

## Where rakupp falls short

Aggregating the `rakupp-error` failures by cause:

| Cause | ~count | Notes |
|---|---:|---|
| Parse errors | ~90 | the biggest bucket — each blocks a whole program |
| `No such method` | 37 | ~half are **external modules** (Term::termios, Cro, Net::FTP) — out of scope |
| `Undefined routine` | 35 | missing builtins + external subs |

The parse errors break down into recurring, *fixable* features:

- **Unicode operators** in term/infix position: `≤ ≥ ≠ × ÷` (~8), superscript
  powers `²`/`.²`/`³` (~5), `»`-led hyperops (~5), `∧ ∙ ≥`…
- `&(expr)(args)` — call a code value produced by an expression
- `».++` — hyper-increment; `.{…}` postfix hash-slice edge cases

The most common missing routines were `cache` (×5), `cis`, `comma`, and the rest
a long tail of one-offs (many were actually external-module symbols).

## Fixes landed from this survey

Each was a genuine, general Raku feature surfaced by real programs — all
committed with **zero Roast regressions**:

| Fix | What it enables |
|---|---|
| infix `min` / `max` | `$_ min 4`, `@a[$x max 0]` (the evaluator already had them — only the parser lacked them) |
| `srand($seed?)` | seeding the RNG (note: rakupp's PRNG ≠ MoarVM's, so it doesn't reproduce Rakudo's exact *sequence*) |
| `≤ ≥ ≠ × ÷` | Unicode operator aliases → `<= >= != * /` |
| `.cache` | list/Seq caching method |
| sigilless-var disambiguation | a declared `\x` (or `\a` param / `-> \d`) is a term, so `x < 0 \|\| x > 7` is two comparisons, not `x(<0 \|\| x>)` |
| `%h<key>=val` (no space) | the glued `>=` no longer swallows the rest of the statement — the `>` correctly closes the subscript |
| `!===` | negated value identity (`3 !=== 4`) |
| `[\op]` triangular reduce | running partial reductions: `[\+] 1,2,3,4` → `(1 3 6 10)` |
| `.clone` on immutables/containers | `5.clone`, `@a.clone` (independent copy), `%h.clone` |
| `cache(…)` sub form | `cache .&f` (the routine, alongside the `.cache` method) |
| cyclic `.raku`/`.gist` | a self-referential structure (`$foo<b> = $foo`) renders a `{...}` back-reference instead of recursing until it exhausts memory |

## Real bugs found (noted for follow-up)

The `DIFFER` bucket is a mix of nondeterminism (random shuffles, hash ordering)
and genuine bugs. Confirmed bugs:

- **`100 doors`** — `for @array[slice] { .=not }` doesn't rw-alias slice
  elements (works for a plain `@a`, not a subscript).
- **`10001th prime`** — a prime-sieve pipeline yields `(Any)` instead of the number.
- **`First-class functions`** — `0` where `0.5` expected (integer vs Rat division).
- **`Find square difference`** — wildly wrong output (`one` vs `501`).
- **gist**: a random list printed as `[…]` (Array) where Rakudo prints `(…)` (List).

## Reproducing

```sh
raku tools/rc-compare.raku [N=40] [skip=0] [rakupp=./build/rakupp]
```

Fetched programs are cached in `rc-cache/`, keyed by task name (e.g.
`A_search_algorithm.raku`), so a task is fetched **once** and reused forever —
re-runs after a rakupp rebuild only pay for the interpreter runs, not the network.
Per-run scratch (task list, `results.tsv`, captured output) goes to `rc-work/`.
Both directories are git-ignored (kept on disk, never committed).
