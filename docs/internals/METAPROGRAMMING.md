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
| `supersede class Foo {…}` | ✗ | — | Not implemented in any form: the word is in the syntax highlighter's keyword list and nowhere else, so `supersede class S {…}` dies with `X::Redeclaration`. Use `augment`, or `.^add_method` to replace |
| `.^add_method($name,$code)` | ✓ | inject a method at runtime | |
| `$x does Role` / `$x but Role` | ✓ | runtime role mixin into a value | |
| `.^methods` / `.^roles` / `.does` | ✓ | introspection (see [FEATURES.md](../guide/FEATURES.md)) | |
| `&sub.wrap({…})` / `.unwrap` | ✓ | soft-routine wrapping | wrapper runs in front of the routine; `callsame`/`callwith`/`nextsame` reach the original; wraps nest and `.unwrap` (LIFO or by handle) restores |

## Grammar & AST — the frontier, and what has crossed it

The deep end, where a program rewrites its own parser or manipulates the syntax
tree. This is genuine compiler-internals work, and the Roast suite barely
exercises it (no real `macro` declarations, one incidental `RakuAST::`
reference) — so the reason to build it was ecosystem reach, not test counts: a
module that uses RakuAST does not degrade, it hard-fails, and Rakudo is making
RakuAST its default front end.

**RakuAST is in.** The 489-class hierarchy, and all four operations over it:
`.AST` builds the tree, `.DEPARSE` renders it back to Raku, `.EVAL` runs it,
`visit-children` walks it, and `.rakudoc` answers a unit's documentation blocks.
It is a **view on demand** over rakupp's own parse rather than a second front
end — the measured reason is in [RAKUAST-PLAN](../dev/plans/RAKUAST-PLAN.md)
Part I, and it is why none of this costs the ordinary path anything. Against
Rakudo's own trees over the raku-corpus programs both engines can tree, the view
carries **72.9%** of the nodes (`rakupp --rakuast` prints ours,
`tools/rakuast-oracle-dump.raku` prints Rakudo's, and the comparison is a
`diff`); the largest gap left is the `Regex::*` subtree.

Macros and general slangs are still out.

| Feature | Status | Notes |
|---|:---:|---|
| `macro` / `quasi { … }` | ✗ | AST macros (`use experimental :macros`) |
| `RakuAST::…`            | ✓ | the classes behind `use experimental :rakuast` (or 6.e) — constructible, with Rakudo's own `.^mro`/`.^parents`/`~~`/`.does` — plus `.AST`, `.DEPARSE`, `.EVAL`, `visit-children` and `.rakudoc`, and `rakupp --rakuast` to print the tree ([RAKUAST-PLAN](../dev/plans/RAKUAST-PLAN.md)) |
| slangs — `$~MAIN`, grammar derivation | ◑ | the slang language-objects (`$~MAIN`/`$~Quote`/`$~Regex`/`$~P5Regex`) exist as defined `Grammar` objects; the grammar can't actually be swapped mid-parse. One family works anyway: `use L10N::DE;` writes a whole program in German, because that slang renames keywords rather than changing the grammar ([SLANG-PLAN](../dev/plans/SLANG-PLAN.md), [FAQ](../guide/faq/l10n.md)) |
| `no strict` / relaxing pragmas | ◑ | `strict` is lexical and both directions work (`no strict` auto-vivifies undeclared variables, `use strict` turns the check back on); the other relaxing pragmas are accepted and ignored |
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
- **Runtime MOP mutation** — `augment` on user classes *and* on built-in
  types, `.^add_method`, `does`/`but` mixins, routine `.wrap`/`.unwrap`,
  `.^`-introspection. (`supersede` is not implemented.)
- **Built-in meta-operators** — reduce, hyper, cross, zip, reverse.

The remaining gaps:

- **Small, self-contained**: the word-form of a user op inside a meta-operator
  (`Zpl`, which lexes as one identifier).
- **Large frontier** (compiler internals): `macro`/`quasi` and slangs — the
  mechanisms by which a Raku program rewrites its own grammar. RakuAST used to
  sit here and no longer does. One family of slangs works anyway: `use
  L10N::XX;` writes a whole program in German or Japanese, because an L10N slang
  renames keywords rather than changing the grammar, and a rename is a rewrite
  of the token stream ([the FAQ article](../guide/faq/l10n.md)).

_Snapshot taken against the current build (2026-09-12) on Darwin 24.6; statuses verified by_
_one-liner._
