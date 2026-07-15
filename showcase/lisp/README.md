# lisp — a Scheme interpreter

A small but real Scheme, in two parts: a Raku `grammar` (plus an actions class)
reads the source into Raku data structures, and a tree-walking evaluator runs
them. The numeric tower rides directly on Raku's own, so exact `Rat`s and
unbounded `Int`s come for free.

## Run it

```sh
build/rakupp showcase/lisp/lisp.raku showcase/lisp/examples/fact.scm
build/rakupp showcase/lisp/lisp.raku showcase/lisp/examples/closures.scm
build/rakupp showcase/lisp/lisp.raku                 # no file → a REPL
# or compile a standalone binary:
build/rakupp --exe -o lisp showcase/lisp/lisp.raku
```

## What it can do

- **Exact bignums** — `(fact 100)` prints all 158 digits, no overflow:

  ```
  $ build/rakupp showcase/lisp/lisp.raku showcase/lisp/examples/fact.scm
  5! = 120
  10! = 3628800
  20! = 2432902008176640000
  50! = 30414093201713378043612608166064768844377641568960512000000000000
  100! = 933262154439441526816992388562667004907159682643816214685929638952175999932299156089414639761565182862536979208272237582511852109168640000000…
  ```

- **Closures, mutable state, higher-order functions, recursion:**

  ```
  $ build/rakupp showcase/lisp/lisp.raku showcase/lisp/examples/closures.scm
  add5(10)   = 15
  add100(10) = 110
  counter: 1 2 3
  sum 1..10 = 55
  squares   = (1 4 9 16 25)
  even? 10  = #t
  odd?  7   = #t
  ```

## Language

| Feature | Notes |
|---|---|
| `define` / `lambda` / `let` | including `(define (f x) …)` function shorthand |
| `if` / `cond` / `and` / `or` | `cond` supports `else` |
| `set!` | mutate a binding in its defining scope |
| `quote` / `` `quasiquote `` / `,unquote` / `,@splice` | list templating |
| lexical closures | functions capture their defining environment |
| numeric tower | exact `Int` / `Rat`, promotes to `Num` as Raku does |
| builtins | `+ - * / = < > cons car cdr list map filter fold-left append reverse length …` |

## How it works

- **Reader** — a Raku `grammar` (`token`/`rule`) with an actions class turns text
  straight into Raku values: numbers become `Int`/`Rat`/`Num`, symbols an interned
  `Sym`, lists an `Array`. `;`-comments are stripped first so the grammar's
  whitespace rule stays trivial.
- **Evaluator** — a recursive `evaluate($form, $env)`: self-evaluating literals
  return themselves, symbols look up the environment chain, and a list is either a
  special form (`if`, `define`, …) or a procedure application.
- **Environments** — a chain of frames (`Env` with a parent), so closures just
  hold the environment they were defined in.
