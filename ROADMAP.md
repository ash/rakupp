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
synopses rather than isolated features. Current standing: **252 / 1,464 Roast
files fully pass**, 119,873 assertions on the files that run (run the harness for
live numbers). Major subsystems now in:

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
- **Unicode** (S15, ~95% of assertions) — NFC/NFD/NFKC/NFKD, grapheme-correct
  `.chars` (UAX #29), names + numeric values, and category/script property
  classes, all from UCD 15.1 tables.
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

## Next

The cheap Roast wins are largely spent; moving the full-pass count now takes
*whole features* (parse + runtime + dispatch together), as sub-signature
destructuring showed. Roughly ordered:

1. **Redispatch** — `callsame`/`nextsame`/`callwith`/`nextwith` and proto `{*}`
   (unblocks a cluster of S06/S12 multi tests).
2. **More signature binding** — `is rw`/`is copy` inside sub-signatures,
   `-> [$a,$b]` pointy destructure, `($x,$y)` as a single list argument.
3. **Symbolic references** — `::('name')`, compile-time `::?CLASS`/`::?ROLE`.
4. **POD subsystem** (S26) — `=begin/=end`, `$=pod`, `.pod` — a self-contained
   chunk of no-TAP files.
5. **Test/subprocess helpers** — `is_run`/`tap-ok` (Test::Util), the last
   blockers on a set of otherwise-passing files.
6. **Widen native `--exe` codegen** toward the constructs that still fall back to
   bundling (see below).
7. **Real-application hardening** — keep driving
   [covid.observer](https://github.com/ash/covid.observer) and the Raku-course
   generator toward running unmodified; each surfaces gaps Roast doesn't.

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
