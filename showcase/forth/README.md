# forth — a Forth interpreter

The stack-machine counterpart to the [Lisp](../lisp/) tree-walker. Forth has no
parse tree: source is a flat stream of whitespace-separated *words*, each of which
pushes a number or runs a dictionary entry against a shared data stack. `: name … ;`
defines new words from old ones, so the language grows itself from a handful of
primitives.

## Run it

```sh
build/rakupp showcase/forth/forth.raku showcase/forth/examples/demo.fth
build/rakupp showcase/forth/forth.raku               # no file → a REPL
# or compile a standalone binary:
build/rakupp --exe -o forth showcase/forth/forth.raku
```

## A taste

```forth
: square ( n -- n*n )  dup * ;
: fact ( n -- n! )  1 swap 1+ 1 do i * loop ;
5 square .      \ prints 25
10 fact .       \ prints 3628800
```

The included [`examples/demo.fth`](examples/demo.fth) walks through arithmetic,
word definitions, conditionals, both loop forms, and drawing with `emit`:

```
$ build/rakupp showcase/forth/forth.raku showcase/forth/examples/demo.fth
3 4 + 2 * = 14
5 square = 25
3 cube = 27
sign 7: positive
sign -3: negative
sign 0: zero
5! = 120
10! = 3628800
countdown from 5: 5 4 3 2 1
fib 10 = 55
a 10x3 box:
**********
**********
**********
```

## Words it knows

| Group | Words |
|---|---|
| arithmetic | `+ - * / mod /mod negate abs min max 1+ 1- 2* 2/` |
| comparison | `= <> < > <= >= 0= 0< 0>` (Forth truth: `0` false, `-1` true) |
| bitwise | `and or xor invert` |
| stack | `dup ?dup drop swap over rot nip tuck depth` |
| control | `if … else … then`, `begin … until`, `begin … while … repeat`, `do … loop` (`i`, `j`) |
| output | `. .s cr space spaces emit bl` |
| defining | `: name … ;` |

## How it works

- **Reader** — trivial: strip `\` and `( … )` comments, split on whitespace. The
  interest isn't the parsing, it's the execution model.
- **Compile** — the token stream is parsed once into a small node tree, so the
  structured control words (`if`/`else`/`then`, the loops) become nested nodes
  instead of being re-scanned every time.
- **Execute** — walk the nodes against a `@stack`: a literal pushes, a word runs
  its dictionary body (recursively) or a primitive block, `do`/`loop` tracks the
  index for `i`. A user definition is just its node list stored under a name.
