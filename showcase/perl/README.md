# perl — a Perl 5 interpreter

A working interpreter for a practical slice of Perl 5 — the language Raku grew
out of. One Raku grammar parses Perl into an AST; a tree-walking evaluator runs
it, carrying Perl's sigil variables (`$`/`@`/`%`), scalar-vs-list context,
string interpolation, statement modifiers, and regexes. The regex side is a
small backtracking engine written inside the interpreter, because rakupp can't
build a regex from a runtime string.

```sh
build/rakupp showcase/perl/perl.raku showcase/perl/examples/fizzbuzz.pl
build/rakupp showcase/perl/perl.raku showcase/perl/examples/regex.pl
build/rakupp showcase/perl/perl.raku --ast=file.pl     # dump the parsed AST
build/rakupp showcase/perl/perl.raku                   # no file → a REPL
```

Six example programs live in [`examples/`](examples/):

| File | Shows off |
|---|---|
| `fizzbuzz.pl`  | statement modifiers, `.=` concatenation, a C-style `for` |
| `sieve.pl`     | arrays, `(x) x n` list repetition, nested loops, `grep` |
| `quicksort.pl` | recursion, `shift`, `grep` partitioning, list-returning subs |
| `wordfreq.pl`  | `split` on whitespace, hashes, sort by value then key, `printf` |
| `regex.pl`     | `m//` capture, `s///` with backreferences, `//g` in list context |
| `histogram.pl` | `split`, hash counting, `x` repetition, formatted columns |

All six produce byte-identical output under the system `perl` and under
`perl.raku` — including Perl's `%.15g` float stringification (`10/3` prints
`3.33333333333333`), string-`cmp` default sort, and scalar-context array counts.

## What runs

**Core.** `my`/`our` declarations (scalar, list, array, hash), `sub` with `@_`
and hoisting (a sub may be called before it is defined), `return` (including as
a statement modifier), recursion, `if`/`elsif`/`else`, `unless`, `while`,
`until`, C-style `for (init; cond; step)`, list `for`/`foreach` with an explicit
or implicit `$_`, `last`/`next`, and bare blocks.

**Values and context.** Number/string duality on scalars, arrays and hashes as
first-class containers, and scalar-vs-list context throughout: an array in
scalar context yields its count, a comma list yields its last element, list
assignment distributes and an array/hash slurps the rest, and `wantarray`-style
list-returning subs flatten into their caller.

**Operators.** Arithmetic `+ - * / % **`, string `.` and `x` (with `(LIST) x n`
list repetition), numeric compares `== != < > <= >= <=>`, string compares
`eq ne lt gt le ge cmp`, logical `&& || // ! and or not xor` with short-circuit,
ternary `?:`, ranges `..`, `++`/`--`, compound assignment (`+= -= .= x= //=` …),
`=~`/`!~`, and the named-unary / list-operator call forms (`print LIST`,
`sort keys %h`, `length`).

**Variables.** `$x`, `@a`, `%h`, elements `$a[i]` / `$h{k}` (with autovivification
and negative indices), `$#a` last-index, `(LIST)[i]` subscripts, the default
`$_`, `@ARGV`, and the regex capture variables `$1`…`$9` and `$&`.

**Strings.** Single- and double-quoted literals, `qw(...)`, and double-quote
interpolation of `$x`, `${x}`, `$a[i]`, `$h{k}`, `@a` (space-joined), `$#a`, and
the usual `\n \t \r \0 \e` escapes.

**Regex.** `m//`, bare `/.../` after `=~`, and `s///`, matched by a backtracking
engine that supports literals, `.`, `^ $`, `\b`/`\B`, character classes
`[...]`/`[^...]` with ranges and `\d \w \s` (and negations), capturing and
non-capturing `(?:...)` groups, alternation `|`, greedy and lazy `* + ? {n,m}`,
and the flags `i` (fold case), `g` (global), `m` (`^`/`$` per line) and `s`
(`.` matches newline). `s///` interpolates `$1`… into its replacement.
`split` accepts a regex or a string separator.

**Builtins.** `print`, `say`, `printf`/`sprintf`, `warn`, `die`; `length`,
`substr`, `index`, `rindex`, `uc`, `lc`, `ucfirst`, `lcfirst`, `join`, `split`,
`reverse`, `sort` (default and `{ $a <=> $b }` block forms), `map`, `grep`;
`push`, `pop`, `shift`, `unshift`, `splice`; `keys`, `values`, `exists`,
`delete`; `scalar`, `defined`, `chomp`, `abs`, `int`, `sqrt`, `chr`, `ord`.

## What's out of scope

Perl is the language famous for "only `perl` can parse Perl," and this is a
slice, not the whole. Deliberately left out — the parts that make full Perl
undecidable or that need machinery beyond a showcase:

- **References and nested data structures** (`\@`, `\%`, `$ref->[0]`,
  `$ref->{k}`, `@{ ... }`) — a `\` is parsed and ignored rather than making a ref.
- **The `/`-divide-vs-regex parse ambiguity** — a bare `/` is always division
  here; write `m/.../` (or `/.../ ` on the right of `=~`) for a match.
- **`tr///`**, **`qr//`**, regex lookaround, backreferences and named captures.
- **Packages, modules, OO** (`package`, `bless`, `use`, `require`), **prototypes**,
  **typeglobs**, **`tie`**, **`local`**, **source filters**, and **file I/O**
  (`open`, `<$fh>`, `<STDIN>`).
- **`each`**, `wantarray`, and the long tail of builtins not listed above.

When the parser meets something outside the subset it fails cleanly rather than
guessing.

## How it works

The pieces mirror the other language showcases:

1. A **grammar** (`PerlGrammar`) with a proto-rule statement dispatcher and a
   precedence ladder of expression rules. Comments are blanked before parsing.
2. An **actions class** that builds a plain-hash AST (`%( t => 'kind', … )`).
3. A **tree-walking evaluator** split into `eval-stmt`, `eval-scalar` and
   `eval-list` — the last two are how scalar-vs-list context is threaded.
4. A **regex engine** (`rx-compile` + a CPS backtracking matcher) for `m//`,
   `s///` and regex `split`.

Every program also compiles to a standalone native binary with
`rakupp --exe showcase/perl/perl.raku -o perl.exe` (it bundles the interpreter,
since the evaluator uses `CATCH` for control flow).
