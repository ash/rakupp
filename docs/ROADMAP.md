# Raku++ Roadmap

The goal is broad coverage of documented Raku, working as both interpreter and
(eventually) compiler, validated against Roast. This is necessarily
incremental — Roast is ~1,460 files covering the entire language. We grow
coverage milestone by milestone and track it with `tools/run-roast.raku`.

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
synopses rather than isolated features. Current standing: **501 / 1,462 Roast
files fully pass (~34%)**, **171,817 / 213,617 declared assertions (~80%)** —
run the harness for live numbers; definitions in [COUNTING.md](COUNTING.md).
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
[docs/ROAST-GAPS.md](dev/ROAST-GAPS.md)):

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
  ([dev/REVIEW-1.0.md](dev/REVIEW-1.0.md)): exception safety (scope guards,
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
  binary, deployment target 11.0), Linux x86_64 (Clang, static libstdc++;
  a separate GCC gate job), and Windows x64 (native MSVC, static CRT);
  tagged releases attach all three archives. Release binaries build with
  Clang everywhere it applies (measured 1.2–2.0× faster than GCC on the
  bench suite).

## Next

The cheap Roast wins are largely spent; moving the full-pass count now takes
*whole features* (parse + runtime + dispatch together). Roughly ordered:

1. **The subtest Pair-form campaign** — `subtest 'desc' => {…}` bodies
   currently never run (auto-pass); the fix is implemented but held out until
   the ~31 pre-existing bugs it exposes are fixed (sprintf recovered already;
   remaining: Rat 0-denominator semantics, 6.e roles/MRO/submethods, IO
   seek/chdir/print/prompt, `is_deeply` needs a real Junction type — the full
   list is in [dev/REVIEW-1.0.md](dev/REVIEW-1.0.md)). Landing it re-measures
   the suite honestly.
2. **Case-folding tail** — `.fc` full folding (`ß` → `ss`), `:ignorecase` /
   `:ignoremark` on non-ASCII (incl. codepoint classes), `samemark`; and
   routing `.collate`/`.sort` through the UCA machinery.
3. **EVAL lexical isolation** — eval-born symbols must not leak to the caller
   (a recurring S02 tail).
4. **Test/subprocess helpers** — the `is_run`-based files (Test::Util), shaped
   -array bounds, `OUR::.<>`-style Stash objects.
5. **Widen native `--exe` codegen** toward the constructs that still fall back
   to bundling (roles/packages, symbolic refs, `s///` — see below).
6. **The v1.0 gate** — reach 90–92% of declared assertions with no
   architecture changes and no performance regressions, then tag v1.0.
   Architecture work (grammar-engine LTM/packrat, GIL removal follow-through,
   `.resume`) comes after.

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
Performance-wise native `--exe` now beats Rakudo on every benchmark except
`fib`, where it ties (see [BENCHMARKS.md](BENCHMARKS.md)) — a recent small-int
fast path for `%`/`mod`/`%%` (they had gone through `BigInt::divmod`) closed the
last collection/hash gaps. The remaining lever is `fib`-style deep recursion of a
tiny body, exactly what an optimizing JIT specializes best.

## How to make progress efficiently

Run `build/rakupp tools/run-roast.raku <synopsis>` to find the cheapest wins:
files that are *partial* (a single missing builtin/operator) or *no-TAP* with a
one-line parse error. Fixing parser/runtime gaps tends to unlock files in bulk.
At the current standing, though, most full-pass gains come from implementing a
whole feature end to end (see **Next**), not one-line patches.
