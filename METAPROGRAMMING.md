# Raku++ — language-mutation & metaprogramming coverage

Raku lets a program reshape the language it is written in: define new operators,
bend the precedence table, run code at compile time, rewrite classes at runtime,
and — at the deep end — swap out the parser itself. This page catalogues those
features and what Raku++ currently does with each.

Legend: **✓** implemented · **◑** partial · **✗** not implemented. Every row was
checked against the current build with a one-liner; the "example" is roughly what
was run.

## Custom operators

New operators are just subs named `<category>:<symbol>`.

| Feature | Status | Example | Notes |
|---|:---:|---|---|
| `infix:<…>`         | ✓ | `sub infix:<avg>($a,$b){…}; 4 avg 10` | full: usable as an operator, as `&infix:<avg>`, and inside `EVAL` |
| `postfix:<…>`       | ✓ | `sub postfix:<!>($n){…}; 5!` | |
| `term:<…>`          | ✓ | `sub term:<π²>{ pi²  }; π²` | nullary custom term |
| `prefix:<…>`        | ✗ | `sub prefix:<¬>($x){…}; ¬$x` | the *declaration* parses, but no call site consumes a user prefix (the parser tracks `userInfix_`/`userPostfix_` but has no `userPrefix_` hook) |
| `circumfix:<… …>`   | ✗ | `sub circumfix:<⟦ ⟧>($x){…}` | parse error |
| `postcircumfix:<… …>` | ✗ | `sub postcircumfix:<¦ ¦>(\o,\k){…}` | parse error |

## Operator traits — precedence & associativity

These reshape the grammar's operator-precedence table.

| Feature | Status | Example |
|---|:---:|---|
| `is tighter(&infix:<+>)` | ✓ | binds tighter than a chosen operator |
| `is looser(&infix:<*>)`  | ✓ | binds looser |
| `is equiv(&infix:<+>)`   | ✓ | same precedence as a chosen operator |
| `is assoc<left/right/non>` | ✓ | associativity of a custom operator |

## Compile-time & staged evaluation

| Feature | Status | Example | Notes |
|---|:---:|---|---|
| `BEGIN { … }`      | ✓ | runs at compile time | |
| `CHECK` / `INIT` / `END` | ✓ | compile-end / run-start / run-end phasers | |
| `ENTER` / `LEAVE` / `FIRST` / `NEXT` / `LAST` | ✓ | block/loop phasers | |
| `constant NAME = …` | ✓ | compile-time constant | |
| `proto` + `multi`  | ✓ | user-extensible dispatch | |
| `EVAL(str)`        | ✓ | `use MONKEY-SEE-NO-EVAL; EVAL("2*21")` | **sees user-defined operators** — `EVAL("40 z 2")` resolves a `sub infix:<z>` in scope |

## Meta-operators

Built-in operators that *transform* another operator.

| Feature | Status | Example | Notes |
|---|:---:|---|---|
| reduce `[+]`        | ✓ | `[+] 1..5` | over **built-in** operators |
| hyper `>>+<<`       | ✓ | `(1,2,3) >>+<< (10,20,30)` | |
| cross `X`           | ✓ | `(1,2) X (3,4)` | |
| zip `Z`             | ✓ | `(1,2) Z (3,4)` | |
| reverse `R-`        | ✓ | `10 R- 3` → `-7` | |
| `&infix:<op>` as a value | ✓ | `(1,2,3).reduce(&infix:<pl>)` | a user op passed as a callable works |
| meta-op **over a user op** | ✗ | `[pl]`, `>>pl<<`, `pl=` | the meta-operator machinery only resolves built-in operators |

## Runtime MOP (meta-object) mutation

| Feature | Status | Example | Notes |
|---|:---:|---|---|
| `augment class Foo {…}` | ◑ | adds methods to an existing **user** class | ✓ user types; ✗ built-ins (`augment class Int {…}` has no effect) |
| `supersede class Foo {…}` | ✓ | replaces a method on a user class | |
| `.^add_method($name,$code)` | ✓ | inject a method at runtime | |
| `$x does Role` / `$x but Role` | ✓ | runtime role mixin into a value | |
| `.^methods` / `.^roles` / `.does` | ✓ | introspection (see [FEATURES.md](FEATURES.md)) | |
| `&sub.wrap({…})` | ✗ | soft-routine wrapping | `No such method 'wrap'` |

## Grammar & AST — the not-yet frontier

The deep end, where a program rewrites its own parser or manipulates the syntax
tree. None of this is implemented — it is genuine compiler-internals work.

| Feature | Status | Notes |
|---|:---:|---|
| `macro` / `quasi { … }` | ✗ | AST macros (`use experimental :macros`) |
| `RakuAST::…`            | ✗ | programmatic AST construction / introspection |
| slangs — `$~MAIN`, grammar derivation | ✗ | swapping the language grammar mid-parse |
| `no strict` / relaxing pragmas | ✗ | strictness can't be turned off |
| `use experimental :…`  | ◑ | accepted syntactically; the feature itself is usually a no-op |

## Summary

Raku++ covers most of what everyday syntax-extending Raku uses:

- **Custom `infix`/`postfix`/`term` operators with the full precedence-trait
  system** (`is tighter`/`looser`/`equiv`/`assoc`) — real operator-table mutation.
- **The whole phaser/`BEGIN`/`constant`/`EVAL` staging story**, including `EVAL`
  of code that uses locally-defined operators.
- **Runtime MOP mutation** — `augment`/`supersede` on user classes,
  `.^add_method`, `does`/`but` mixins, `.^`-introspection.
- **Built-in meta-operators** — reduce, hyper, cross, zip, reverse.

The gaps fall into two tiers:

- **Small, self-contained** (each a bounded feature): `prefix` custom operators
  (the parser already has the sibling infix/postfix hooks — the cheapest fix),
  `circumfix`/`postcircumfix` custom operators, meta-operators over *user-defined*
  operators (`[userop]` / `>>userop<<` / `userop=`), `augment` on built-in types,
  and routine `.wrap`.
- **Large frontier** (compiler internals): `macro`/`quasi`, `RakuAST`, and slangs
  — the mechanisms by which a Raku program rewrites its own grammar.

_Snapshot taken against the current build on Darwin 25.5; statuses verified by
one-liner._
