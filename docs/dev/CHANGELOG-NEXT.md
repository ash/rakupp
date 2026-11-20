# The next release's CHANGELOG entry, collected as work lands

*Emptied at the v5.10.0 tag: everything collected here is in
[CHANGELOG.md](../../CHANGELOG.md). This is not a CHANGELOG entry yet:
[RELEASING.md](RELEASING.md) step 2 writes that at the tag, with the tag's
own gate figures. Add each item as it lands, in the CHANGELOG's own shape —
what changed, what it measured and how, with the commit — so the entry starts
from this page. Move each item into CHANGELOG.md at the tag and delete it
here.*

*Covered so far: the `my class` shadowing fix; allomorph literals, nested classes and the first mechanical round under `--exe`.*

---

### New

- `--exe` compiles seven more constructs where it bundled the program: a
  class whose parent is `Exception`, `Cool`, `Numeric`, `Real`, `Stringy` or
  an `X::` type; a block or routine whose last statement is a plain
  declaration (`lives-ok { my $x = f() }`); a plain declaration as a
  `while`, `if` or `given` condition; `my @a := …` of a value nothing else
  holds, with the interpreter's own checks (bindPositional, now one function
  both engines call); `\(…)` of values; and sigilless names (`my \x`,
  `sub f(\x)`, `-> \c`). Still refused: a sigilless slurpy or capture
  parameter, a `constant` inside a routine, a slip into an `@` parameter, a
  type declared in a class body, and `.candidates`/`.cando`/`.signature` of
  a multi sub.
- `--exe` compiles a class or grammar declared inside a routine or block
  when that changes nothing a program can see (codegen/NestedClass.cpp): it
  was the first blocker of 92 Roast files, a test block's `class Dog {}` the
  commonest; 13 of them now compile natively, and the note says why a class
  stays (a role, an anonymous class, a name the code around it declares, …).
  With the allomorph literals below, 577 of the 1,425 Roast files compile
  natively, against 545 (V6-PLAN number 4: 40%).
- `--exe` compiles an allomorph literal (`<42>`, `<1.5>`, the numeric words
  of `< … >`): it was the first construct to bundle in 42 of the Roast files
  that do not compile natively, and 19 of those now compile natively (16 give
  the interpreter's TAP; the other three, main-usage.t and argfiles.t, spawn
  `$*EXECUTABLE`, which a compiled binary refuses, the same as when they were
  bundled). The fallback note names every construct in words; it said
  "an unsupported construct (NK 24)" for an allomorph literal, and the same
  "NK" form for `redo` and a subset declaration.

### Faster

### Fixed: silent wrong answers

- Compiled by `--exe`, more answers that differed from the interpreter's,
  in top-level programs too: a sub checked no argument count
  (`sub f($a, $b) {…}; f($one)` ran with `$b` unbound; now the interpreter's
  X::TypeCheck::Argument, or X::Signature::ArityMismatch through a slip or a
  `&f` variable); one `$!` served the whole binary, so a `try` in a sub
  rewrote its caller's; `-> Rat $_ { $_ * 2 }` read the outer topic;
  `$buf."$write"(…)` wrote a copy; `&f.arity` counted a parameter with a
  default; `my $x := 1, 2` itemized the list; and a block whose value is a
  variable's (`{ $p }`, `{ if 1 { $p } }`, `return $p`, an `is raw` tail)
  had it sunk by the statement that called it, so a Failure detonated.
- Compiled by `--exe`, each of these answered differently from the
  interpreter, in top-level programs too (found as the files above became
  native): `$x ~~ EXPR` evaluated EXPR with the outer `$_`; `say @objects`
  ignored a class's own `gist`; `@a[lazy ^N]` gave N elements where it ends
  with the array; `5 ~~ *` was not a WhateverCode, nor `*.&{ … }`; a
  computed `self."!$n"()` called the private method; and `has Cursor $.c`
  refused a Match. A `.&` chain on a `*` (`*.uc.&{ … }.flip`) is refused
  rather than compiled wrongly.

- A Num literal compiled by `--exe` keeps its sign: `-0e0` was written into
  the C++ as the Int `-0`, so `val(<-0e0>)` was +0, and the real part of
  `<-0-0i>` lost its sign too (S32-num/negative-zero.t, 4 tests, once it
  compiled natively). `Inf` and `NaN` were written as `inf` and `nan`, which
  is not C++.

- A `my class` (or `my role`) declared in a routine or block no longer takes
  the name from a global type of the same name for good: once
  `sub f { my class Point {} }` had run, `Point` named f's class everywhere
  and the program's own `class Point` could not be reached. The type
  registry holds one type per name; the shadowed one is now put back when the
  declaring scope leaves, with its `temp`s, whether it returned or died, and
  in order through recursion and nested scopes (registerDeclaredType). A type
  declared directly in an EVAL keeps the name, as it did.
  t/regression/my-class-shadow.raku, which Rakudo 2026.09 passes too.

### Fixed: crashes

### Behaves like Rakudo

### Platforms and packaging

### Tools

### Left open

- A closure or method that outlives the routine which declared a shadowing
  `my class` finds the outer type under that name, where Rakudo finds the
  routine's own; and a method of the outer class, called while the routine
  runs, finds the routine's class. Both follow from one registry entry per
  name.
- What the `--exe` fixes above cost per call, measured natively against the
  build before them (alternating order, 15 rounds, instructions retired
  agreeing): a sub that contains `try`, `CATCH` or `$!` looks `$!` up once
  more to keep its caller's, ~11 ns a call (+27% on a 2M-call loop of
  `sub safe($x) { try {…}; $x + 1 }`; it was +44% with two lookups, before
  the guard kept the slot it found); the argument-count check, now inline
  (rtArityCheck, its error out of line in rtArityFail), ~20 instructions on
  a call through the boxed entry (+2-4% on 3M calls); and a block whose value
  is a variable's copies it to mark it contained, ~40 instructions (+6% on
  3M calls of `-> $x { my $p = $x + 1; $p }`). tools/bench is unchanged
  natively (fib, objects, sortby, multiwhere, strcat, regex, rats, arrayops
  within ±4% either way) and perf-guard's interpreter kernels alike, its
  red junctionwide the same on the build before.
