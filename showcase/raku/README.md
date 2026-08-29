# raku — a grammar of Raku, written in Raku, run by rakupp

The other showcases parse a language rakupp is not: Lisp, Forth, JavaScript,
Python, JSON, Markdown. This one parses the language rakupp *is*. The
precedence ladder, statement forms and term shapes in
[`raku-grammar.raku`](raku-grammar.raku) are the ones in
[`src/Parser.cpp`](../../src/Parser.cpp), restated as a Raku grammar instead of
as a recursive-descent parser — and rakupp runs it, so the compiler ends up
parsing its own language with a grammar it executes itself.

```sh
build/rakupp showcase/raku/raku-grammar.raku examples/fibonacci.raku
build/rakupp showcase/raku/raku-grammar.raku --tree examples/hanoi.raku
build/rakupp showcase/raku/raku-grammar.raku --check=examples
```

`--tree` prints the parse tree, collapsing the levels an expression passed
straight through, so what is left is the structure the operators imposed:

```
$ build/rakupp showcase/raku/raku-grammar.raku --tree /tmp/x.raku
statementlist  my $x = 1 + 2 * 3; say $x if $x > 5;
  declaration  my $x = 1 + 2 * 3
    scope  my
    assign-expr  $x = 1 + 2 * 3
      variable  $x
      assign-op  =
      additive-expr  1 + 2 * 3
        number  1
        additive-op  +
        multiplicative-expr  2 * 3
          number  2
          multiplicative-op  *
          number  3
```

## The ladder is the compiler's

The fifteen precedence levels come from `classifyInfix` in
[`src/Parser.cpp`](../../src/Parser.cpp), where they are `BP_OR` through
`BP_POW`. Each level in the grammar is one rule of the form
`<tighter>+ % <operators at this level>`, named after the `BP_*` constant it
mirrors, so the grammar states what the binding-power table states:

| Level | Operators |
|---|---|
| `BP_OR` | `or` `xor` `orelse` `==>` `<==` |
| `BP_AND` | `and` `andthen` `notandthen` |
| `BP_ZIP` | `...` `...^` `^...` `^...^` `Z` `X` `minmax` |
| `BP_COMMA` | `,` |
| `BP_ASSIGN` | `=` `:=` `.=` `+=` … `=>` |
| `BP_TERNARY` | `?? !!`, the eight flip-flops |
| `BP_OROR` | `\|\|` `//` `^^` |
| `BP_ANDAND` | `&&` |
| `BP_COMPARE` | `==` `eq` `~~` `<=>` `===`, the set relations |
| `BP_RANGE` | `..` `..^` `^..` `^..^` `o` `∘` |
| `BP_CONCAT` | `~` |
| `BP_REPLICATE` | `x` `xx` |
| `BP_ADD` | `+` `-` `min` `max`, the set combinators |
| `BP_MUL` | `*` `/` `%` `div` `mod` `does` `but` |
| `BP_POW` | `**` (right-associative) |

Two consequences fall out of writing it this way. `/` starting a regex and `/`
meaning division never conflict, because a regex literal is only reachable
where a term is expected and the ladder has already taken the infix. And the
`--tree` output above is a direct reading of the table: `2 * 3` sits under
`1 + 2 * 3` because `BP_MUL` is tighter than `BP_ADD`.

## What it covers, measured

Run against the Raku in this repository, comparing with what the compiler
accepts:

| Corpus | Files parsed |
|---|---|
| `examples/` | 36 / 36 |
| `showcase/` | 25 / 27 |
| `tools/` | 69 / 75 |
| `live/`, `bindings/` | 6 / 6 |
| `t/` | 280 / 406 |
| **total** | **418 / 553** |

`raku-grammar.raku` is one of the 25 in `showcase/`: the grammar parses itself.

`t/` is the regression suite, so it is deliberately a catalogue of unusual
syntax — atomic operators, `%?RESOURCES`, binding to hash keys, declarator
doc-comments — and it is where the long tail lives.

## What it is not

Raku is not a context-free language, and this is not a complete grammar of it.
Three things in the compiler sit outside any fixed grammar, and they are worth
naming because they are the reason a hand-written parser was chosen in the
first place (see [PARSING.md](../../docs/internals/PARSING.md)):

- **The terminal alphabet grows during the parse.** `sub infix:<◐>` adds an
  operator, and `scanOpsIn` reads *other files* on `use Foo` to register Foo's
  operators before parsing starts. The grammar can recognise the declaration;
  it cannot then honour it.
- **The precedence relation is computed.** `is tighter(&infix:<+>)` resolves
  through `infixBpOf` at parse time, so the ladder itself is program-dependent.
- **Parse-time predicates gate productions.** `use MONKEY-TYPING` decides
  whether `augment` parses at all.

So this describes one member of a family: Raku with the built-in operator
table. Two further boundaries are drawn deliberately rather than forced:

- **The regex sublanguage is bracketed, not parsed.** A regex or `token` body
  is matched for balance — quotes, character classes and nested assertions are
  respected so a `}` inside `<-[}]>` does not end the block early — but its
  contents are not described.
- **Heredoc bodies are lifted out before parsing.** A heredoc's body begins
  after the *statement* that introduces it ends, so its extent is not a
  function of the text where `q:to/END/` appears. The driver removes bodies
  first (replacing them with blank lines, so reported line numbers stay true),
  which is where rakupp's lexer handles it too.

It is a recogniser for well-formed Raku, and it errs toward accepting. On a set
of eight malformed inputs it agrees with the compiler's accept/reject on six;
the two it wrongly accepts are cases where a statement separator is missing
(`if $x say 1;`). Requiring separators strictly was tried and measured: it
recovers one of those two, and costs 73 files of real coverage (418 → 345). For
a grammar meant to describe the language, wrongly rejecting valid Raku is the
worse failure, so the lenient rule is the one that ships.
