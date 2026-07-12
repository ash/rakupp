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
| `prefix:<…>`        | ✓ | `sub prefix:<¬>($x){…}; ¬$x` | symbolic + word forms, in expressions, as listop args, and in EVAL |
| `circumfix:<… …>`   | ✓ | `sub circumfix:<⟦ ⟧>($x){…}; ⟦1,2,3⟧` | symbolic + word brackets, in expressions and as listop args |
| `postcircumfix:<… …>` | ✓ | `sub postcircumfix:<¦ ¦>($o,$k){…}; $x¦7¦` | same- or distinct-delimiter brackets |

## Operator traits — precedence & associativity

These reshape the grammar's operator-precedence table. A custom `infix` defaults
to additive precedence; a trait slots it to a fresh level relative to a chosen
operator (levels are spaced so a new one fits *between* two built-ins).

| Feature | Status | Example |
|---|:---:|---|
| `is tighter(&infix:<+>)` | ✓ | binds tighter than `+` (`2 + 3 foo 4` → `2 + (3 foo 4)`) |
| `is looser(&infix:<*>)`  | ✓ | binds looser than `*` |
| `is equiv(&infix:<+>)`   | ✓ | same precedence as a chosen operator |
| `is assoc<left/right/non>` | ✓ | `<right>` makes `a foo b foo c` group as `a foo (b foo c)` |

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
| `$x userop= y` (assign metaop) | ✓ | `$m mns= 3` | user infix tight against `=`; works for any operands |
| meta-ops **over a user op** | ✓ | `[pl]`, `>>pl<<`, `Z§`, `X§` | reduce/hyper/zip/cross resolve user infixes; word-form `Zpl` (one token) still parses as an ident |

## Runtime MOP (meta-object) mutation

| Feature | Status | Example | Notes |
|---|:---:|---|---|
| `augment class Foo {…}` | ✓ | reopens a type and adds methods | ✓ user types (merged into the class) **and** built-ins — `augment class Int {…}` reaches `3.method`, walking the native ancestry so `augment class Cool` also covers Int/Str |
| `supersede class Foo {…}` | ✓ | replaces a method on a user class | |
| `.^add_method($name,$code)` | ✓ | inject a method at runtime | |
| `$x does Role` / `$x but Role` | ✓ | runtime role mixin into a value | |
| `.^methods` / `.^roles` / `.does` | ✓ | introspection (see [FEATURES.md](FEATURES.md)) | |
| `&sub.wrap({…})` / `.unwrap` | ✓ | soft-routine wrapping | wrapper runs in front of the routine; `callsame`/`callwith`/`nextsame` reach the original; wraps nest and `.unwrap` (LIFO or by handle) restores |

## Grammar & AST — the not-yet frontier

The deep end, where a program rewrites its own parser or manipulates the syntax
tree. This is genuine compiler-internals work and is essentially unimplemented —
and, notably, the Roast suite barely exercises it (no real `macro` declarations,
one incidental `RakuAST::` reference), so it is very low-yield to pursue.

| Feature | Status | Notes |
|---|:---:|---|
| `macro` / `quasi { … }` | ✗ | AST macros (`use experimental :macros`) |
| `RakuAST::…`            | ✗ | programmatic AST construction / introspection |
| slangs — `$~MAIN`, grammar derivation | ◑ | the slang language-objects (`$~MAIN`/`$~Quote`/`$~Regex`/`$~P5Regex`) exist as defined `Grammar` objects; the grammar can't actually be swapped mid-parse |
| `no strict` / relaxing pragmas | ✗ | strictness can't be turned off |
| `use experimental :…`  | ◑ | accepted syntactically; the feature itself is usually a no-op |

## Summary

Raku++ covers most of what everyday syntax-extending Raku uses:

- **All six custom-operator categories** — `infix`/`prefix`/`postfix`/`term`/
  `circumfix`/`postcircumfix` — declared as ordinary subs and dispatched in
  expressions and as listop args, with working precedence/associativity traits
  (`is tighter`/`looser`/`equiv`/`assoc`).
- **Meta-operators over user operators** too — `[userop]` reduce, `>>userop<<`
  hyper, `Z§`/`X§` zip/cross, and `$x userop= y` meta-assignment.
- **The whole phaser/`BEGIN`/`constant`/`EVAL` staging story**, including `EVAL`
  of code that uses locally-defined operators.
- **Runtime MOP mutation** — `augment`/`supersede` on user classes *and*
  `augment` on built-in types, `.^add_method`, `does`/`but` mixins,
  routine `.wrap`/`.unwrap`, `.^`-introspection.
- **Built-in meta-operators** — reduce, hyper, cross, zip, reverse.

The remaining gaps:

- **Small, self-contained**: the word-form of a user op inside a meta-operator
  (`Zpl`, which lexes as one identifier).
- **Large frontier** (compiler internals): `macro`/`quasi`, `RakuAST`, and slangs
  — the mechanisms by which a Raku program rewrites its own grammar.

_Snapshot taken against the current build on Darwin 25.5; statuses verified by
one-liner._
