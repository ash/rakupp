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
- `Order` enum (`<=>`/`cmp` → `Less`/`Same`/`More`), `$^a` placeholder params
  (2-arg blocks used as sort comparators), magic string increment (`$s++`,
  `.succ`, string ranges `'a'..'z'`), `\x`/`\o` escapes, zen-slice `[]`/`.[]`,
  `q//`/`qq//`/`Q//` quote forms, sigilless `my \x` and pointy `\T`/trait params,
  `!~~`, and phasers (`BEGIN`/`END`/…) on a block or single statement.

- **Number tower**: arbitrary-precision `Int` (hand-rolled `BigInt`, base 1e9 —
  add/sub/mul/divmod/pow/gcd) with a long-long fast path, and exact `Rat`
  (BigInt numerator/denominator, normalized). `/` yields `Rat`; `**` with
  negative/Int exponents, `%`/`div`/`mod`/`%%`, and all comparisons are exact.
  Big integer literals and `+"big"` numification.

## Next (high value, roughly ordered)

1. **Robustness of the core** — drive up the *partial→full* and *no-TAP→partial*
   counts in S02–S04, S06, S32. Each fix typically unlocks many files.
   - `given`/`when`, `do {}`, `try {}`/`CATCH`, `loop` (C-style), `repeat`.
   - Better number tower: `Rat`, bigint `Int`, `Complex`, proper `/` semantics.
   - Hash literals `{ ... }`, `%(...)`, adverbs (`:exists`, `:delete`, `:k/:v`).
   - `Whatever` (`*`) and `WhateverCode` (`* + 1`), `&`-references.
2. **Regex & grammars** (S05) — a regex engine and `Grammar`/`token`/`rule`,
   `~~` matching, `Match`/`$/`, substitution. Large but unlocks a whole synopsis.
3. **OO & MOP** (S12, S14) — `class`/`role`/`has`/method dispatch, `multi`
   dispatch resolution, `enum`, `subset`, type checking against constraints.
4. **Exceptions** (S04) — `X::` hierarchy, `try`/`CATCH`/`throws-like`, `fail`.
5. **Junctions, lazy lists, feeds, hyperops** (S03, S07, S09).
6. **Unicode correctness** — graphemes, `NFC`, proper `.chars`/`.codes`/`.ords`.
7. **Module system** (S11) — `use`/`module`/`EXPORT`, `EVAL`.

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

Run `build/rakupp tools/run-roast.raku <synopsis>` to find the cheapest wins: files that are
*partial* (a single missing builtin/operator) or *no-TAP* with a one-line parse
error. Fixing parser/runtime gaps tends to unlock files in bulk.
