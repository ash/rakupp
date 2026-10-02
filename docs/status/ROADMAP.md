# Raku++ Roadmap

The goal is broad coverage of documented Raku, working as both interpreter and
compiler, validated against Roast and tracked with `tools/run-roast.raku`.

## Done (MVP core)

- Lexer: numbers (int/float/hex/oct/bin, `_` separators), single/double-quoted
  strings, sigil variables + twigils, operators, comments, basic POD skipping,
  whitespace-significance flag for postcircumfix disambiguation.
- Parser: recursive-descent statements + Pratt expression parser with Raku
  precedence; `if`/`elsif`/`unless`/`while`/`until`/`for`, statement modifiers,
  `sub` with signatures, pointy blocks `-> $x {}`, anonymous blocks, list vs
  item assignment, ranges, ternary, pairs, `< >` word lists.
- Runtime: Int/Num/Str/Bool/Array/Hash/Range/Pair/Code/Type values; scopes &
  closures; user subs with positional/named/slurpy/default params; control flow
  via `return`/`last`/`next`; string interpolation (`$x`, `@x[]`, `{ EXPR }`).
- Operators: arithmetic, string (`~`, `x`, `xx`), numeric & string comparison,
  chained logical/short-circuit, `<=>`/`cmp`, ranges, basic `~~`.
- Builtins: `say`/`print`/`put`/`note`/`warn`/`die`, `sprintf`/`printf`,
  numeric & string & list functions, and a TAP-emitting `Test` module
  (`plan`, `ok`, `nok`, `is`, `isnt`, `is-deeply`, `cmp-ok`, `pass`, `flunk`,
  `diag`, `skip`, `dies-ok`, `lives-ok`, `done-testing`).
- ~75 common methods on values (`.elems`, `.map`, `.grep`, `.sort`, `.join`,
  `.keys`, string/numeric methods, array mutators, etc.).
- `given`/`when`/`default`, `do {}`, `try {}` (sets `$!`), C-style `loop`,
  `repeat … while/until`.
- Colonpairs/adverbs (`:foo`, `:!foo`, `:foo(x)`, `:foo<w>`, `:$var`).
- `Whatever` (`*`) and `WhateverCode` currying (`* + 1`, `*.method`).
- `.{ }` / `.[ ]` / `.( )` postcircumfix method syntax; `.method` topic calls.
- Whitespace-significant parsing for postcircumfix vs block and call-vs-listop.
- Signatures with type constraints + `:D`/`:U` smileys and sigilless `\a` params.
- **`EVAL`** (runtime lex+parse+exec) and the wider Test API: `isa-ok`,
  `is-approx`, `throws-like`, `subtest`, `eval-lives-ok`/`eval-dies-ok`.
- **Classes** (`class`/`role`): `has $.x`/`$!y` attributes with defaults,
  `method`/`submethod`/`multi method`, `self`, `$.attr`/`$!attr` access and
  assignment, default `.new(:attr(v))`, single inheritance (`is`), `BUILD`.
- Identity & equivalence: `===`, `!==`, `eqv`, `before`/`after`; argument
  flattening / slip with prefix `|` (`f(|@args)`).
- `Order` enum (`<=>`/`cmp` -> `Less`/`Same`/`More`), `$^a` placeholder params
  (2-arg blocks used as sort comparators), magic string increment (`$s++`,
  `.succ`, string ranges `'a'..'z'`), `\x`/`\o` escapes, zen-slice `[]`/`.[]`,
  `q//`/`qq//`/`Q//` quote forms, sigilless `my \x` and pointy `\T`/trait params,
  `!~~`, and phasers (`BEGIN`/`END`/…) on a block or single statement.

- **Number tower**: arbitrary-precision `Int` (hand-rolled `BigInt`, base 1e9 —
  add/sub/mul/divmod/pow/gcd) with a long-long fast path, and exact `Rat`
  (BigInt numerator/denominator, normalized). `/` yields `Rat`; `**` with
  negative/Int exponents, `%`/`div`/`mod`/`%%`, and all comparisons are exact.
  Big integer literals and `+"big"` numification.

## Landed since the MVP

All of the original "next" list has landed; the interpreter now covers whole
synopses rather than isolated features. Current standing, at v5.2.0: **100.00%
of Roast's tests pass** (218,420 / 218,420 with skip and todo left out), and
**all 1,424 files** of `spectest.data` pass completely — run the harness for
live numbers; definitions in [COUNTING.md](COUNTING.md).
Major subsystems now in:

- **Regex & grammars** (S05) — a CPS backtracking engine, `token`/`rule`/`regex`,
  `.parse`/`.subparse`/actions, `Match`/`$/`/`$0…`, substitution, and Unicode
  property classes. Parses the full Raku-course TOC grammar in ~0.55 s (≈2×
  faster than Rakudo).
- **OO & MOP** (S12/S14) — `class`/`role`/`grammar`, attributes, `multi method`,
  single inheritance/`does`, `bless`/`BUILD`, `enum`, and a working metamodel
  (`.^name`/`.^methods`/`.^add_method`/`.^find_method`, `.WHAT`/`.WHICH`/`.HOW`).
- **Signatures & dispatch** — `where`/literal/`:D`/`:U` multi-dispatch, plus
  sub-signature **destructuring** (`[$a,$b]` / `|c($x,$y)` / `*[…]`) with dispatch
  on the destructured arity.
- **Exceptions** (S04) — `die`/`try`/`CATCH`, `X::*`, `throws-like`/`fails-like`.
- **Junctions, lazy lists, hyperops, feeds** (S03/S07/S09).
- **Unicode** (S15, ~100% of assertions) — NFC/NFD/NFKC/NFKD, grapheme-correct
  `.chars` (UAX #29), UCA collation, names + numeric values, and
  category/script property classes, all from UCD/UCA 17.0 tables.
- **Modules** (S11) — `use`/`need`/`EXPORT`, `use lib`, resolving real
  zef-installed modules via the Rakudo CURI `short/` index.
- **Concurrency** (S17) — real `std::thread`s under a CPython-style GIL: promises
  (`start`/`await`/combinators/`.then`), `Supply`/`Supplier`/`react`/`whenever`,
  `Channel`, `Thread`, `Lock`/`Semaphore`. Blocking ops (`sleep`/`await`/
  subprocess waits) release the GIL, and **`RAKUPP_PARALLEL=1`** opts into true
  CPU parallelism (thread-local execution state, frozen symbol tables, real
  locks; ~3× on 8 cores, 0 Roast regressions, ThreadSanitizer-clean).
- **Tooling** — a parse-aware syntax highlighter (`--highlight`, HTML + ANSI, a
  `pygmentize` drop-in) and a self-hosted Roast harness written in Raku.

Landed in the 2026-07 Roast push (350 → 400 files, 73% → 81% of declared
tests; the systematic gap classification lives in
[dev/findings/ROAST-GAPS.md](../dev/findings/ROAST-GAPS.md)):

- **Roast fudge directives** — `#?rakudo skip` (statement/block extents, test
  counting, `#?DOES`), honoring what roast's own preprocessor does.
- **Unicode 17.0 across the board** — real UAX #29 data incl. rule GB9c,
  normalization from `UnicodeData.txt`, **UCA collation** (`unicmp`/`coll`,
  all 8,271 conformance tests), Hangul/Tangut/Nushu name synthesis, Unihan
  numerals; the names generator is Raku run by rakupp
  ([DOGFOODING.md](DOGFOODING.md)).
- **Iterator protocol** (S07: `.iterator`, `pull-one`, `push-*`, `IterationEnd`)
  and **lazy `^Inf`** (`.head`/`.skip`/`.grep`/`.first` compose lazily).
- **Parser clusters** — indirect-invocant colon `key($pair:)`, chained adverbs,
  zen slice `%h{}`, pseudo-packages (`::<$x>`, `MY::`, `$PROCESS::IN`),
  `sub infix:<<M>>`, `Q««…»»`.
- **Semantics** — `$!` always a defined exception, `Block`/`Sub`/`Method` type
  kinds, Capture `<named>` indexing, exact `sprintf %d` for big Ints,
  sigilless-param write-through, stray `next`/`last`/`redo` errors.

Landed in the 1.0 campaign + pre-1.0 hardening (2026-07, 400 → 433 files):

- **The big no-TAP files** — `rat.t` (869), `complex.t` (557), `reduce.t`,
  `rx.t` and the `.new` batch (Version, Duration, SetHash/BagHash/MixHash,
  buf8/blob8, shaped arrays) all fully pass; the `expected )` /
  term-position-op / Confused parser clusters (200+ files) unblocked.
- **Redispatch** — `callsame`/`nextsame`/`callwith`/`nextwith`, proto `{*}`.
- **An independent pre-1.0 review, then five fix waves**
  ([dev/findings/REVIEW-1.0.md](../dev/findings/REVIEW-1.0.md)): exception safety (scope guards,
  LEAVE under cooperative control flow), concurrency lock-order fixes, a
  stack-headroom recursion guard (no more SIGBUS), silent-wrong-answer fixes
  in the numeric tower, a regex step budget, and the nested-sub closure-cycle
  leak (~425 MB → ~3 MB on a 300k-call driver).
- **UTF-8-correct regex char classes** — `<-[x]>` / `<[é]>` / `<[a..ÿ]>` match
  whole codepoints, not bytes.
- **Native codegen hardening** — 389 of 416 fully-passing roast files
  transpile with `--exe`; injective name mangling; closure-capture
  correctness (bail to bundle instead of miscompiling).
- **Cross-platform delivery** — CI builds and smoke-tests macOS (universal
  binary, deployment target 11.0), Linux x86_64 and ARM64 (Clang, static
  libstdc++, built in a manylinux 2.28 container so the glibc floor is 2.28
  and a gate keeps it there; a separate GCC gate job), and Windows x64 (native MSVC, static CRT);
  tagged releases attach all three archives. Release binaries build with
  Clang everywhere it applies (measured 1.2–2.0× faster than GCC on the
  bench suite).

- **The v3.0.0 campaign (shipped 2026-08-08)** — three pillars, each with a
  written plan and each gated the usual way (zero Roast regressions, battery
  unchanged, `perf-guard --check`): **the command line**, one option parser in
  place of the position-sensitive mode cascade, with the Perl one-liner family,
  `-i` in-place editing, borrowed flags (`-M`, `-v`, `--target`) and
  `--profile` ([CLI-PLAN.md](../dev/plans/CLI-PLAN.md)); **real multicore
  parallelism, on by default** — `start` and worker threads run on all cores
  with no flag, and the cooperative GIL survives as `RAKUPP_GIL=1`
  ([PARALLEL-PLAN.md](../dev/plans/PARALLEL-PLAN.md)); and **true
  longest-token matching**, a side-effect-free declarative-prefix NFA for `|`
  and protoregex dispatch in place of the probe-and-rank approximation
  ([LTM-PLAN.md](../dev/plans/LTM-PLAN.md)).

## Next

Rakudo remains the reference: every divergence is still judged against it. The
per-version overview is [dev/plans/VERSIONS.md](../dev/plans/VERSIONS.md). With
Roast at 100.00%, the work that is planned is:

- **The errors behind v5 — [V5-PLAN.md](../dev/plans/V5-PLAN.md).** Batches B1–B5,
  which did not land before the v5.0.0 tag: no module file that compiled under
  the previous release fails to compile; no crash (the `evalCall` race, the
  battery's `use` smoke, every program `--cpp` accepts building); the silent
  wrong answers the survey found; the ecosystem failure clusters; and compiled
  backends that never disagree with the interpreter.
- **Speed — [V6-PLAN.md](../dev/plans/V6-PLAN.md).** Every perf-guard kernel at
  or below Rakudo's time at steady state, memory per value, `--exe` compiling
  at least 90% of the fully passing Roast files natively, compile and load time
  halved, and modules in the web editions (#83).
- **The ecosystem.** Distributions passing their own suites, measured against
  the 1,791 that Rakudo passes on the same machine through the same harness
  ([raku.online/modules/ecosystem](https://raku.online/modules/ecosystem/)).
- **Documentation conformance.** Every runnable example in the official docs
  run on both engines and classified; the figures and the list of divergences
  are at [raku.online/spec/rules/divergences](https://raku.online/spec/rules/divergences/).

## Compiler backend

**Done — step 1: bundling** (`rakupp --bundle`). Produces a standalone native
executable by embedding the program *source* and linking it against the runtime
static library (`librakupp_rt.a`). Needs no `rakupp` on the target, ~10 ms cold
start, works for the whole language — but re-parses and tree-walks the embedded
source at run time.

**Done — step 1b: real AOT** (`rakupp --aot`). Parses the program at build time
(reporting parse errors then) and emits C++ that rebuilds the exact AST at
startup, which the interpreter walks — so no lexing/parsing happens at run time.
Handles the whole language (grammars included, since the interpreter runs the
embedded tree); falls back to bundling only if a node can't be rebuilt.

**Done — step 2: native transpilation** (`rakupp --exe`). Transpiles the AST to
C++ that implements the program directly (calling the runtime only for `Value`
semantics), then compiles it — no interpreter inside. Hot code runs several
times faster than interpreted (e.g. `fib` ~3×, level with Rakudo). **Handles the
whole supported language**: it native-compiles nearly everything — scalars,
`@`/`%` (index+autoviv), all operators / ranges / reductions / chained
comparisons / postfix / interpolation, `if`/`while`/`loop`/`repeat`/`for`/
`given`-`when`, list-destructuring, `enum`s, subs, **`multi` dispatch**,
`&`-refs, closures, `WhateverCode`, **classes** (attributes incl. `@`/`%`,
defaults, methods, `self`, accessors, inheritance), method calls, regex,
junctions, `do`/`try`/`gather`/`EVAL`, **phasers** (`BEGIN`/`INIT`/`ENTER`/
`LEAVE`/`END`), **`CATCH`** blocks, `@*ARGS` — and transparently **falls back to
bundling** for the rest (mainly grammars), so it never refuses a program.

**Next:** grammars are best left bundled (they are the grammar engine).

Where the two engines stand is measured in [BENCHMARKS.md](BENCHMARKS.md) and
re-measured every release. The numbers live there and are deliberately not
copied here: a ratio quoted in two files goes stale in one of them, which is
exactly what this paragraph used to demonstrate. The shape of the result, as of
the 2026-09-03 sitting against a native arm64 Rakudo: `--exe` leads on most of
the fifteen kernels, and `objects` — 200k `.new` plus 300k method calls — is
the one it does not, which makes **method dispatch the measured weak spot**.
Read the tables for the current split and the per-kernel ratios.

## How to make progress efficiently

`build/rakupp tools/run-roast.raku --failed` lists every Roast file that does
not fully pass, with the source lines of its failing tests. Beyond Roast, the
work lists are the module battery (`RELEASING.md` gate 6), the ecosystem sweep,
[Rakugrid](https://raku.online/grid/) and the semantics sheets in
[dev/findings/semantics/](../dev/findings/semantics/).
