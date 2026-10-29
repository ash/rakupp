# Follow-ups

*Collected 2026-10-20; added to 2026-10-29.*

Findings that sessions on 2026-10-19 and 2026-10-20 proposed as follow-up
work and nobody started: gaps found in passing while doing something else.
Each was re-checked on 2026-10-20 against Raku++ 5.4.1-155 and Rakudo 2026.09
(`/opt/homebrew/bin/rakudo`; bare `raku` on this box is rakupp), and each
still reproduces. Each item says how to reproduce it, where to look, and
where it came from. When an item is done, remove it. A small one can move to
[TODO-MISCELLANEOUS.md](TODO-MISCELLANEOUS.md); one that grows into a plan
moves to [TODO.md](TODO.md).

---

## Names and parsing

- [ ] **A plain `constant` leaks out of its block and out of an EVAL** (found
  2026-10-20 while caching the bare `Less` term, b35718ba; the leak predates
  that cache):

  ```raku
  sub mention { Less }
  { constant Less = 42; say Less }       # 42 in both
  say Less.raku;                          # 42, Rakudo Order::Less
  say mention().raku;                     # 42, Rakudo Order::Less
  say EVAL('constant Less = 42; Less'); say Less.raku;   # leaks here too
  ```

  `my constant` in a block is already right. An our-scoped constant belongs
  in the package stash plus a lexical alias in the declaring scope, so a
  bare name outside that scope should not see it. Also check an ordinary
  name (`{ constant FOO = 1 }; say FOO`) against the oracle.

- [ ] **An anonymous `rule { … }` has no sigspace** (found 2026-10-23 while
  making compiled regex literals values; compiled code answers the same):

  ```raku
  my $r = rule { a b }; say 'a b' ~~ $r   # Nil, Rakudo ｢a b｣
  say 'ab' ~~ $r                          # ｢ab｣, Rakudo Nil
  ```

  It matches as a `token` would. `rx:s/a b/` and a named `my rule` are
  right. Start at regexMatch's `wired` test (src/InterpreterCore.cpp
  ~20533), which reads the `rule` kind into "sr" flags only for a value it
  counts as wired.

- [ ] **An undeclared type-like name before a statement's block is refused
  only in an EVAL** (found 2026-10-27 while making statement conditions
  Rakudo's xblock, 4af53b07; on daf9c03c too):

  ```raku
  CATCH { when X::Y {} }          # runs, Rakudo ===SORRY!=== Function 'X::Y' needs parens to avoid gobbling block (…)
  EVAL 'CATCH { when X::Y {} }';  # that group, as in Rakudo (S32-exceptions/misc.t)
  ```

  noteGobbleSite (src/Parser.cpp) records the name, and only the EVAL path
  in src/InterpreterModules.cpp asks bareNameResolves of `gobbleSites_`
  after its parseProgram; a program's and a module's parse never do. Done
  when the first line is refused at compile time, as in Rakudo 2026.09.

- [ ] **`Empty` before a pointy block is read as a listop taking it** (found
  2026-10-27 while fixing issue #207, 3ddcd58b; on 170bb19a too, where 4af53b07
  had a condition's listop start taking its block):

  ```raku
  for Empty -> $x { }; say 'ok'   # ===SORRY!=== Function 'Empty' needs parens to avoid gobbling block, Rakudo ok
  for 1, Empty -> \v { say v }    # the same, Rakudo 1
  for 1, Empty { .say }           # 1 in both
  ```

  `Empty` is a term, as `now`, `e`, `pi` and `rand` are, and those keep the
  pointy for the statement already. The exemptions are the `rand`/`i`/type
  test 4af53b07 added to parsePrimary (src/Parser.cpp, "…and so, before a
  block or a pointy"). Done when the first two lines answer as in Rakudo
  2026.09.

- [ ] **A package declaration that starts a statement does not take an
  infix after it** (found 2026-10-27 while fixing issue #206, 2b9494ae; on
  4083b26b too):

  ```raku
  say quietly { class A {} === A }   # ===SORRY!=== unexpected operator in term position, Rakudo True
  say quietly { class D {} ~~ D }    # ===SORRY!=== Expected a term, but found either infix ~~ …, Rakudo True
  say quietly { class {} ~ "x" }     # x in both, but here as `class {}; ~"x"`
  ```

  A package declaration is a term in Rakudo. The statement parser's
  `class`/`role`/`grammar`/`module` branch (parseStatementInner,
  src/Parser.cpp, "…and the built-in package declarators") wraps the
  declaration as a term only before a tight `.` or before `but`/`does`, and
  otherwise ends the statement at its `}`, so the infix starts a statement
  of its own. checkTextAfterBlock lets an operator after a package's `}`
  through (lastPkgClose_) because of it: Roast's S03-operators/u2212-minus.t
  has `class {} − class {}`. Done when the three lines answer as in Rakudo
  2026.09 and that exemption is gone.
- [ ] **A module sees what the program imported before it** (found
  2026-10-28 while fixing issue #211, 0e44c5a6; on v5.5.0 too):

  ```raku
  # A.rakumod
  module A {
      multi trait_mod:<is>(Parameter:D $p, :$zork! --> Nil) is export { }
      sub a-helper() is export { 1 }
  }
  # B.rakumod
  unit module B;
  sub b-count() is export { &trait_mod:<is>.candidates.elems }
  sub b-sees-a() is export { (try a-helper()) // 'no' }
  # main.raku, run with -I.
  use A; use B;
  say b-count();     # 10, the engine's 9 and A's
  say b-sees-a();    # 1; Rakudo refuses B: "Undeclared routine: a-helper"
  ```

  It is TODO.md's "Module code can name the program's lexicals" for
  imports: a module's unit scope chains to `global_`, and a `use` in the
  program's mainline defines its imports there. Issue #211 was this
  reaching NativeLibs: Cro::HTTP::Router's `is` multis, imported by the
  app, were the `&trait_mod:<is>` NativeLibs' EXPORT searched, and the
  search found nothing until 0e44c5a6 made a user group list the engine's
  candidates too. Done when B is refused as in Rakudo and b-count is 9;
  size it with the TODO.md item, which is the same chain.
- [ ] **`loop(…)` calling a `&loop` parameter is parsed as a loop** (found
  2026-10-28 while writing c50eb3b8's test, for issue #146):

  ```raku
  sub count(&loop) { my $n = 0; loop({ $n++ }); loop({ $n++ }); $n }
  say count(-> &b { b() });
  # Raku++: Malformed loop spec (expected 3 parts separated by semicolon, got 1)
  # Rakudo 2026.09: 2
  ```

  Rakudo reads a keyword followed straight by `(`, with no space, as a call
  of that name, and calls the `&loop` in scope. Here the statement parser
  takes `loop` as the loop keyword whatever follows it. Done when the
  program prints 2; check `if(…)`, `while(…)` and `for(…)` beside it.

- [ ] **A constant's initializer calls a routine declared below it** (found
  2026-10-29 fixing issue #196, f303f0c0; on v5.5.0 too):

  ```raku
  sub f { 0 }; { constant A = f(); sub f { 1 }; say A }                 # 1, Rakudo 0
  constant A = abs(-3); sub abs($x) { 99 }; say A                       # 99, Rakudo 3
  sub g { try f() }; constant A = g(); sub f { 1 }; say A // 'undef'    # 1, Rakudo undef
  ```

  f303f0c0 refuses what Rakudo refuses; these Rakudo compiles, with what was
  declared when the constant ran: the outer `f`, the builtin `abs`, and,
  inside the `try`, no `f` at all. rakupp declares a scope's routines before
  the scope runs (hoistSubs) and evaluates the constant where it stands, so
  it calls the one below. Also left to run, since the check reports only
  certainties: a call in a block that runs at compile time, `constant A =
  [1].map({ f() }).eager` or `sub g { if 1 { f() } }; constant A = g()`,
  where Rakudo dies as the block runs. Done when a constant's initializer and
  a BEGIN block see only the routines declared above them.

## Operators and types

- [ ] **Built-in prefix and postfix operators fail when called by name**
  (found 2026-10-19 while fixing undeclared operator names, 145cddbf):

  ```raku
  say prefix:<->(3);                  # Undefined routine 'prefix:<->', Rakudo -3
  say prefix:<so>(1);                 # Undefined routine, Rakudo True
  say postfix:<++>(my $y = 1);        # Undefined routine, Rakudo 1 (and $y is 2)
  say postcircumfix:<[ ]>((1,2,3), 1);   # Undefined routine, Rakudo 2
  say &postfix:<++>;                  # (Any), Rakudo &postfix:<++>
  say infix:«<=»(1, 2);               # Undefined routine 'infix', Rakudo True
  say infix:<<∈>>(1, (1,2));          # Undefined routine 'infix', Rakudo True
  ```

  Some `&` forms work (`&prefix:<->(3)` is -3): the `bare.rfind("prefix:<",
  0)` blocks in evalVarExpr are the model, and the call form ends in
  evalCall's undeclaredRoutine. The `«»`/`<<>>` call forms come out of the
  parser as just `infix`. InterpreterCore.cpp is near its size ceiling;
  helpers go in InterpreterOperators.cpp.

- [ ] **A write through a `$_` that aliases a typed variable skips its type
  check** (found 2026-10-20 while fixing issue #185; andthen now shares the
  gap `with` already had):

  ```raku
  my Int $x = 1; with $x { $_ = "s" }; say $x     # s, Rakudo: Type check failed in assignment to $x
  my Int $x = 1; $x andthen $_ = "s"; say $x      # s, same
  ```

  Also `:D`, `where`, subsets, and `for $a { $_ = "s" }` over a typed
  scalar. Assignment checks are keyed by the target NAME (varDefault /
  varSmiley / varWhere, VarExpr declType, around evalAssignInner); the
  `$_` bound to the variable's cell (`Value::cellHolder`, from
  execGivenStmt and topicAliasedRhs in src/InterpreterRegex.cpp) has no
  constraint of its own. Carry the constraint with the cell, or give the
  aliasing scope's `$_` the source's entries. Plain `with $x { … }` loops
  must not get measurably slower (instructions retired).

- [ ] **A native `int` times `2**62` does not wrap** (found 2026-10-22 by the
  FLOAT-HEAVY-ISSUES work, e30e2827; on v5.5.0 too):

  ```raku
  sub f(int $x) { $x * 2**62 }; say f(3)     # 13835058055282163712, Rakudo -4611686018427387904
  my int $i = 3; say $i * 2**62               # the same
  my int $k = 3; say $k * 4611686018427387904 # -4611686018427387904 in both
  my int $j = 3; my $b = 2**62; say $j * $b   # 13835058055282163712 in both
  ```

  Rakudo folds `2**62` into a literal, which the native candidate takes; a
  written literal already wraps here, and an Int in a variable promotes in
  both. nativeIntArith (src/InterpreterCore.cpp) takes the other operand as
  native when it is an IntLit; an expression of literals that folds to an
  Int in range (`2**62`, `1 +< 40`) is the same case. Check the `-O` lane
  (Codegen `nativeOp`, which asks `vInt`) agrees.

- [ ] **A native return type is not checked** (found 2026-10-22 while making
  compiled routines check `--> T`, e30e2827; on v5.5.0 too):

  ```raku
  sub rn(--> num) { "s" }; say rn()   # s, Rakudo: This type cannot unbox to a native number: P6opaque, Str
  sub ri(--> int) { "s" }; say ri()   # s, Rakudo: …to a native integer: P6opaque, Str
  sub rs(--> str) { 5 }; say rs()     # 5, Rakudo: …to a native string: P6opaque, Int
  ```

  checkRetType (src/InterpreterCore.cpp) leaves a native name unchecked on
  purpose ("a boxed value has no native tag"), and Codegen::retCheckOf
  follows it, so both engines agree today. A fix is the binder's refusal
  (nativeParamTakes, throwNativeUnbox in src/InterpreterBinding.cpp) at the
  return: `--> num` takes a Num or a NumStr, `--> int` an Int, `--> str` a
  Str. Then drop the native exclusion in retCheckOf and leanReturn's
  LeanNative arm.

- [ ] **Two types cannot share a short name** (found 2026-10-27 while making
  DateTime::Timezones install, 50aebf54; on v5.5.0 too). Types are known by
  their names throughout, so a second type of a name is the first one:

  ```raku
  my $x = do { my class A { method m { 1 } }; A };
  my $y = do { my class A { method m { 2 } }; A };
  say $x.m, $y.m, " ", $x === $y;                 # 22 True, Rakudo 12 False
  class DateTime is DateTime { method hi { "hi" } }
  say DateTime.new(0).hi;                         # X::NYI, Rakudo hi
  class Int is Int { }; say 5 ~~ Int;             # X::NYI, Rakudo False
  class DateTime { has $.x }; say DateTime.now;   # the core .now, Rakudo: No such method 'now'
  ```

  The two in the middle are refused on purpose (refuseCoreNamesake,
  src/InterpreterModules.cpp): the class would replace the core type it
  inherits from, and before 50aebf54 `is CORE::DateTime` crashed there. In a
  package the same declaration works, because the class's name is qualified
  (`unit module Timezones; class DateTime is DateTime` is
  Timezones::DateTime; parentIsOuterType). What assumes a type's registry key
  and its `.^name` are one string: `classes_` (keyed by name),
  `Value::typeObj(name)`, `ObjectData::cls->name`, the type checks by name
  (typeMatchesResolved, typeAliasTarget) and the AST cache. A design is an
  internal key per declaration, a separate display name, and the short name
  bound lexically; size the change before writing it. Done when all four
  answer as Rakudo does, the refusal is gone, and the Roast list is
  v5.5.0's.

- [ ] **Some parameters still bind what their sigil or type refuses** (found
  2026-10-27 while fixing issue #207, 3ddcd58b, which made a `%` parameter take
  an Associative only; on 170bb19a too):

  ```raku
  for (1,) -> @a { say @a.^name }               # Int, Rakudo: expected Positional but got Int (1)
  given 5 -> @a { say @a.^name }                # Int, the same
  sub r(:&c) { &c.^name }; say r(:c(1))         # Int, Rakudo: expected Callable but got Int (1)
  sub t(Int %h) { %h.^name }; say t(%(a => 1))  # Hash, Rakudo: expected Associative[Int] but got Hash. You have to pass an explicitly typed hash, …
  sub c(%h is copy) { %h.^name }; say c(Map.new((a => 1)))   # Map, Rakudo Hash (and a Bag copies to {:a(2)})
  sub f(%h is rw) { }                           # accepted, Rakudo ===SORRY!=== Can only use 'is rw' on a scalar ('$' sigil) parameter
  my %h := (a => 1); say %h.^name               # Hash, Rakudo Pair (a `%` parameter keeps it since 3ddcd58b)
  ```

  The first two are the name-only loop binder: pointyParamNeedsBinding and
  the given/with `anySub` test in src/Parser.cpp send `%` to bindParams
  since 3ddcd58b and not `@`, because `for @rows -> @r` is common and full
  binding costs it (see the Performance item). The named arm of bindParams
  has no copy of the positional arm's Callable test; `Int %h` wants the
  Associative[T] check that `Int @a` has (bindParams' "explicitly typed
  array" test); `is copy` copies through coerceHash, which keeps a Map's or
  a QuantHash's kind; the `:=` arms for a VarExpr target are in evalAssign.
  Done when each line answers as in Rakudo 2026.09.

- [ ] **A binding failure leaves out parts of Rakudo's message** (found
  2026-10-27 while fixing issue #207, 3ddcd58b; on 170bb19a too):

  ```raku
  sub f(Int $x) { }; f(Failure.new("boom"))   # Rakudo opens "Earlier failure:\n boom\n\nFinal error:\n "
  sub g(:@l) { }; g(l => {a => 1})            # Rakudo goes on ". You have to pass an explicitly typed array, …"
  ```

  Rakudo's X::TypeCheck.priors and .explain (its Exception.rakumod). The
  hint is word-wrapped with the rest of the explanation; refuseNotAssociative
  in bindParams does both for `%` and is the model. Done when the two
  messages are Rakudo 2026.09's.

- [ ] **Date and DateTime strings: what is left of the semantics sheet's
  DT-08 and DT-19** (found 2026-10-28 while fixing issue #208, 0f19ab0e, which made
  every malformed string X::Temporal::InvalidFormat):

  ```raku
  say Date.new("٢٠١٠-٠١-٠٢");                       # InvalidFormat, Rakudo 2010-01-02
  try Date.new(2015, 13, 1); say $!.comment;        # No such method 'comment', Rakudo (Any)
  try DateTime.new("2012-12-22T07:02:00+00:99");
  say $!.what, "|", $!.range, "|", $!.got.^name;    # minute|0..59|Str, Rakudo minutes of timezone|0..^60|Rat
  try DateTime.new("2012-13-22T07:02:00Z"); say $!.got.^name;   # Str, Rakudo Int
  ```

  isoTemporalShape (src/MethodCallPart2.cpp) takes ASCII digits only, and
  the reading after it is sscanf's; Rakudo's `\d` is any decimal digit, which
  `.Int` reads. The three exceptions are thrown in the same `m == "new"` arm.
  The JS runtime has a check of its own (src/js-rt/40-objects.js, the
  `pos.length === 1` line, mirrored in src/JsRuntimeSrc.cpp): by reading, its
  DateTime pattern is not anchored at the end, so trailing text passes, and
  it wants a time, so `DateTime.new('2023-03-04')` is refused where Rakudo
  gives midnight. Unchecked: there is no node on the Mac this was found on.
  Done when the lines above and both probe lines of DT-08 and DT-19 in
  docs/dev/findings/semantics/Date-Time.md answer as in Rakudo 2026.09,
  less the two Rakudo bugs the sheet says not to copy.

- [ ] **`Str.new` with a positional answers a string** (found 2026-10-28
  while fixing issue #208, 0f19ab0e; on ad5482f4 too):

  ```raku
  say Str.new(1).raku;      # "1", Rakudo: Default constructor for 'Str' only takes named arguments
  say "Date".new(1).raku;   # "", the same
  my $t = "Date"; say "[$t.new(1)]";   # [], the same
  ```

  Rakudo's Mu.new takes named arguments only, so a positional to a class
  with no `new` of its own dies; the third line is how a test probe written
  as `"$t.new(...)"` printed empty strings here instead of dying.

- [ ] **A built-in method whose parameter takes a Junction whole threads it
  instead** (found 2026-10-28 while fixing issue #192, f483f0e6; on f30323e3 too):

  ```raku
  say 1.polymod(2 & 3);                              # all((1 0), (1 0)), Rakudo (all(1, 1) 0)
  my @b; @b.append(1|2); say @b.raku;                # [1, 2], Rakudo [any(1, 2)]
  my @c; @c.unshift(1&2); say @c.raku;               # [2, 1], Rakudo [all(1, 2)]
  say Set.new(1|2).raku;                             # any(Set.new(1), Set.new(2)), Rakudo Set.new(any(1, 2))
  say [1, 2].splice(0, 1, 5|6).raku;                 # any([1], [5]), Rakudo [1]
  my @a; @a.push(1|2); say @a.raku;                  # [any(1, 2),], Rakudo [any(1, 2)]
  ```

  Rakudo's candidates take these as slurpies or `Mu`, so the junction is a
  value there. methodCall (src/Builtins.cpp) threads every junction argument
  that methodTakesJunction does not exempt, and that asks only about methods
  a program declares; the built-ins need a list of their own, as the
  matcher methods have. `.push` keeps it whole already, and its `.raku`
  alone is wrong: a one-element Array holding a junction gets a trailing
  comma. Done when the lines above are Rakudo 2026.09's.
- [ ] **A signature drops `--> Nil`** (found 2026-10-28 while writing
  issue #211's regression test, 0e44c5a6; on v5.5.0 too):

  ```raku
  sub f(Int $x --> Nil) { }
  say &f.signature.returns.^name;    # Mu, Rakudo Nil (`--> Int` is Int in both)
  say &f.signature ~~ :(Int);        # True, Rakudo False
  say :(Int --> Nil) ~~ :(Int);      # True, Rakudo False
  ```

  `&f.returns` is Nil, so the routine keeps it; its Signature object does
  not (`returns` in its hash), and sigAcceptsSig (src/Builtins.cpp), which
  compares the two sides' `\x01ret`, takes `--> Nil` for no return type.
  Cro's trait multis are declared `--> Nil`, so a search among
  `&trait_mod:<is>.candidates` by a signature without one matches here what
  it misses in Rakudo. Done when the three lines are Rakudo 2026.09's.
- [ ] **A metaclass `compose` finds the accessors early through `.^can`
  and `.^find_method`** (found 2026-10-28 while fixing `rakupp install
  AttrX::Lazy`, 8c871a25; on v5.5.0 too):

  ```raku
  my role H {
      method compose(Mu \type) {
          say so type.^can('bar');            # True, Rakudo False
          say so type.^find_method('bar');    # True, Rakudo False
          callsame;
          say so type.^can('bar');            # True in both
      }
  }
  multi trait_mod:<is>(Attribute:D $a, :$hook!) { $a.package.HOW does H }
  class Foo { has $.bar is hook }
  ```

  Rakudo makes a public attribute's accessor in the base compose, so
  before the hook's `callsame` no lookup finds it. 8c871a25 made
  `.^methods`, `.^method_names` and `.^method_table` skip a class's
  accessors while `ClassInfo::awaitingCompose` is set; `.^can`
  (src/MethodCallPart2.cpp, the arm that adds an accessor stub after the
  method tables) and `.^find_method`/`.^lookup` (the same file, and
  Builtins.cpp for core types) do not. Red's model compose asks `.^can`
  for a column accessor before its `nextsame` (the comment on
  awaitingCompose in src/Value.h), so check a Red model's columns and
  accessors still build before changing it. Done when the three lines
  are Rakudo 2026.09's.

- [ ] **A grammar's HOW is no `Metamodel::GrammarHOW`** (found 2026-10-29
  checking issue #214; on v5.5.0 too):

  ```raku
  grammar G {}
  say G.HOW.^name;                        # Perl6::Metamodel::ClassHOW, Rakudo …GrammarHOW
  say G.HOW ~~ Metamodel::GrammarHOW;     # False, Rakudo True
  say G.HOW ~~ Metamodel::ClassHOW;       # True in both
  ```

  Issue #214's golfed `Int.HOW ~~ Metamodel::ClassHOW` is True on
  5.5.0-228, and the shape Cro::RPC::JSON's HOW-onto-type has (a method
  trait doing `given $*PACKAGE.HOW { when Metamodel::ClassHOW { $how does
  R } }`) passes here, so the issue may be closed already; Cro::RPC::JSON
  is not installed on this box to run the issue's program. Done when a
  grammar's HOW is a GrammarHOW that is still a ClassHOW, and the issue's
  `use Cro::RPC::JSON:api<2>` program compiles.

## Control flow and containers

- [ ] **A phaser written last does not make the block's value Nil**
  (found 2026-10-28 while fixing `rakupp install
  https://github.com/antononcube/Raku-Woxi`, 642a320a; on v5.5.0 too).
  In Rakudo every phaser is a statement, so one written last is the
  block's or routine's last statement, what precedes it is sunk, and the
  value is Nil. 642a320a does this for a trailing CATCH or CONTROL in a
  block as well as a routine; the others still skip to the statement
  before them:

  ```raku
  sub f { 42; LEAVE { } }; say f().raku;        # 42; Rakudo Nil
  say (1, 2).map({ $_ * 2; LEAVE { } });        # (2 4); Rakudo (Nil Nil)
  my $x = do { 42; KEEP { } }; say $x.raku;     # 42; Rakudo Any
  my $y = do { 42; ENTER { } }; say $y.raku;    # 42; Rakudo Any
  ```

  The three tail scans (execBlockFull's lastIdx, the sub path's and the
  method path's lastReal in src/InterpreterCore.cpp) skip isBlockPhaser
  blocks; trailingCatchBlock would answer for any phaser block instead.
  `sub f { …; LEAVE … }` is common in modules, so check rakulib/ and the
  embedded Raku sources for a routine that relies on the value before
  the phaser. A trailing END (Nil already) and a named sub written last
  (Rakudo answers the sub) are each their own case. Done when the four
  lines are Rakudo 2026.09's.
- [ ] **A typed scalar's container handed out by an expression takes
  anything** (found 2026-10-28 while making scalar aliases keep their type,
  5f87f445; on v5.5.0 too). An alias of a typed variable refuses what the
  variable refuses now (scalarAliasOf), but these containers are not made by
  an alias, and store as they come:

  ```raku
  my Int $a = 1;
  ($a, my $z) = 'la', 2;              # stores; Rakudo: Type check failed in assignment to $a
  ($a, 2)[0] = 'l';                   # stores; the same in Rakudo
  my Int $p = 1; (k => $p).value = 'pv';    # stores; Rakudo refuses, naming $p
  sub r() is rw { $a }; r() = 'rr';   # stores; Rakudo refuses, naming $a
  my $wh where Int = 1; my $w := $wh; $w = 'q';   # stores; Rakudo: expected <anon>
  class C { has Int $.x is rw = 1 }; my $o = C.new; my $ox := $o.x; $ox = 5;
  say $o.x;                           # 1: the bind took a copy; Rakudo 5
  ```

  The first is list assignment into a typed scalar, with no alias at all:
  the destructuring store skips the `=` path's type checks. The next three
  hand out the variable's cell (a list literal's kept item, a Pair's value,
  an `is rw` routine's tail), so each could take scalarAliasOf's Proxy
  instead of the bare cell, as `f(($a, 2))` already does. A `where` clause
  runs in the declaring scope, which the alias's record does not keep.
- [ ] **A refusal through a copy-in/copy-out alias comes at the write-back**
  (found 2026-10-28 while fixing issue #184, 17e33a45). A `for` loop's `$_`,
  `given`'s topic and `.map`'s `$_` hold a copy of the element, written back
  after the body; the element type is checked there, so the rest of the body
  runs first and a `try` inside it does not see the refusal:

  ```raku
  my Int @a = 1, 2;
  for @a { $_ = 'w'; say 'after' }                  # prints after, then dies; Rakudo dies first
  for @a { try { $_ = 'w'; CATCH { default { say 'caught' } } } }   # dies; Rakudo: caught caught
  ```

  A native array's element is judged at the assignment because the topic
  carries its tags (nativeElemTag); a typed element could carry its guard
  the same way, on the loop variable's Env (varDefault plus a table naming
  the container, so the message is "for an element of @a").
- [ ] **Three aliases of a typed array's elements still take anything**
  (found 2026-10-28 while fixing issue #184, 17e33a45):

  ```raku
  my Int @a = 1, 2, 3;
  @a.hyper.map({ $_ = 'x' }).eager;             # stores; Rakudo dies (+X::HyperRace::Died)
  for @a.kv -> $k, $v is rw { $v = 'x' }        # stores; Rakudo dies
  @a.first({ $_ = 'x'; True });                 # stores nothing, no error; Rakudo dies
  ```

  The hyper `.map` sets tctx().topicWriteback in runParallel's worker
  (MethodCallTail.cpp) without the guard the eager `.map` sets beside it
  (tctx().topicWritebackGuard). And `.map`/`.grep`'s refusal names "the
  container", not `@a`: the method sees the array's value, not the
  expression that named it.
- [ ] **An untyped array's element alias answers as a value** (found
  2026-10-28 while fixing issue #184, 17e33a45; on v5.5.0 too):

  ```raku
  my @p = 1, 2; my $e := @p[0];
  $e = Nil; say @p.raku             # [Nil, 2], Rakudo [Any, 2]
  say $e.VAR.of                     # Nil (the Proxy deproxies), Rakudo (Mu)
  ```

  A typed element's alias carries a guard (kSlotGuard) that resets a Nil and
  builds `.VAR`'s Scalar; an untyped one carries none, to keep the slot
  proxy at four entries (BinaryHeap builds one per sift step). Nil could
  reset to Any in slotProxyWriteRef with no entry; `.VAR` needs the
  container's name, which only the bind site has.
- [ ] **A `:D` element type is half-kept** (found 2026-10-28 while fixing
  issue #184, 17e33a45; on v5.5.0 too):

  ```raku
  my Int:D @d = 1;
  @d[0] = Nil; say @d.raku          # Array[Int].new(Int); Rakudo dies, perhaps Nil was assigned to a :D…
  say @d.of;                        # (Int), Rakudo (Int:D)
  ```

  The smiley lives in the declaring Env's varSmiley, not in the array's
  ofType, so `.of`, `.raku` and the Nil arm of element assignment
  (InterpreterCore.cpp, "Storing Nil into an ELEMENT") do not see it. An
  alias of the element does now (elemAliasGuard reads elemSmileyOf).
- [ ] **`.map` over a shared Array writes its elements back** (found
  2026-10-28 while fixing issue #193, b05e989b, on its program). After a
  block's call, callCallableRaw copies the block's `$_` back into the element
  `.map` aliased (tctx().topicWriteback, topicWriteBack in InterpreterCore.cpp) unless
  Value::sameRep finds nothing changed. For an element the block never
  touched it still finds a difference, presumably a flag the binder sets on
  the topic (which one is not pinned down: lldb cannot read the frame in an
  optimised build), so the element is written on every call. One thread
  stores what was there; eight `start` blocks mapping one shared Array, as
  the issue's json-encode does with `$v.map({ json-encode($_) })`, write its
  elements while the others copy it. Under ThreadSanitizer every report
  t/regression/issue-193-shared-hash-reads.raku leaves (36) is this:
  Value::operator= at that line against RVec's copy constructor. No wrong
  answer showed in 300 runs of the repros, but a torn copy is possible. The
  fix is to write back only what the block assigned: a flags-only difference
  skipped, or a mark on the topic's store.
- [ ] **An enum's type object is a list of its pairs** (found 2026-10-27
  while making statement conditions Rakudo's xblock, 4af53b07; on daf9c03c
  too). A type object is one item, and `Int` is, but an enum's is not:

  ```raku
  enum Col <red green>;
  .say for Col;                     # red => 0, green => 1; Rakudo (Col)
  for Col -> $c { say $c.^name }    # Pair Pair; Rakudo Col
  say Col.elems; my @a = Col; say @a.elems;   # 2 2; Rakudo 1 1
  say red.elems;                    # 2; Rakudo 1 (though `for red { }` runs once)
  ```

  Look for where an enum type, and a member's `.elems`, answer as the
  enum's pairs. Done when the four lines answer as in Rakudo 2026.09.

- [ ] **A `fail` in a block written at the mainline returns from the routine
  that runs it** (found 2026-10-22 while making `fail` lexotic, 3e97932e; on
  v5.5.0 too):

  ```raku
  sub ap(&b) { b() }
  my $r = ap({ fail "x" }); say $r.^name   # Failure, Rakudo dies "x" at the mainline
  ```

  The builtin (B["fail"], src/BuiltinsRegister.cpp) now returns from the
  routine the `fail` was written in, but with none (lexicalRoutineFrame 0)
  it still returns from the dynamic one. Rakudo throws the exception there.
  The catch: Test's `eval-lives-ok` runs its string in a routine of its own
  (evalAsRoutine, same file), and a `fail` at the string's top level must
  return from that. Give evalAsRoutine an Env that is a routine frame, so
  the string's code has a lexical routine, then make lf == 0 die. Roast's
  advent2009-day20.t is the case that relies on it.

- [ ] **A List kept in a variable loses its containers when passed to a sub**
  (found 2026-10-22 while timing writes through bindings, e30e2827; on
  v5.5.0 too):

  ```raku
  sub w($l) { $l[0] = 1 }
  my $m = 0; my $ml = ($m, 2); $ml[0] = 5; say $m   # 5 in both
  my $k = 0; my $kl = ($k, 2); w($kl); say $k       # Cannot modify an immutable List ((0 2)), Rakudo 1
  ```

  keepListContainers keeps `$k`'s container when the literal is assigned to
  `$kl`, and a write through `$kl` itself works; one through the parameter
  meets values. Look at what the call hands the parameter: decontList
  (src/InterpreterCalls.cpp) gives built-ins a List's values, and the
  binder's copy (bindParams' simple path, callPlainSub) may take that form
  or drop `holdsCells`. `f(($a, 2))` itself works since d7c6d9d5 (and on a
  second call since 3e97932e).

- [ ] **The interpreter does not sink a ternary's branches** (found
  2026-10-23 while making compiled regex literals values; compiled code is
  right):

  ```raku
  $_ = 'abc'; False ?? /q/ !! /c/; say $/   # Nil, Rakudo and --exe ｢c｣
  ```

  Sink context reaches a ternary's branches in Rakudo, and a sunk regex
  literal matches `$_`. Interpreter::exec's ExprStmt arm
  (src/InterpreterCore.cpp, "A bare regex STATEMENT") matches only a
  RegexLit that is the statement itself; codegen's exSunk takes a ternary
  apart. t/regression/exe-regex-literal-values.raku skips that line for the
  interpreter; drop the skip with the fix.

- [ ] **A LEAVE runs on `exit` in both engines; Rakudo runs none** (found
  2026-10-27 while fixing the compiled LEAVE, fc262224):

  ```raku
  for ^2 -> $i { LEAVE { say "leave$i" }; exit 0 if $i == 0 }   # here leave0, Rakudo nothing
  ```

  Rakudo's `exit` runs the END phasers and ends without unwinding, so no
  block's LEAVE runs. The interpreter's block executors run theirs for any
  exception, ExitEx included (execPlainBlock's `catch (...)`, leaveByError,
  src/InterpreterCore.cpp), and compiled code now matches it: its LEAVE
  guards are destroyed as ExitEx unwinds. Change both at once, since
  t/exe/run.raku holds them to each other. Done when the line above prints
  nothing in either, and END still runs.
- [ ] **A typed `$` parameter's `.VAR` is a Scalar** (found 2026-10-28
  while fixing issue #146, c50eb3b8; the same before it):

  ```raku
  sub t(Channel $in) { say $in.VAR.^name }   # Scalar, Rakudo Channel
  t(Channel.new);
  sub s(Str $s) { say $s.VAR.^name }         # Scalar, Rakudo Str
  s('x');
  sub l(List $l) { say $l.VAR.^name }        # Scalar in both
  l((1, 2));
  ```

  Rakudo binds a typed read-only `$` parameter to its value with no Scalar
  around it; its binder itemizes an Iterable, and an untyped or Any
  parameter is handed its argument's container. c50eb3b8 made `for` read such
  a parameter as bare through VarExpr::bareParam, which the parser sets;
  `.VAR` still sees the Scalar the binder makes. Done when the three lines
  are Rakudo 2026.09's.
- [ ] **A `let` is not undone when a LEAVE dies during the unwinding**
  (found 2026-10-28 while fixing issue #205, 099a2055):

  ```raku
  our $x = 1;
  sub f { let $x = 5; LEAVE die "from LEAVE"; die "first" }
  try f(); say $x;    # 5, Rakudo 1
  ```

  callCallableRaw's and invokeMethod's handlers run the LEAVEs
  (runLeavePhasers) before runLetRestoresOf, and a LEAVE that dies throws
  past the restore. 099a2055 made a method put its caller's scope back on that
  path; nothing restores the `let`s. Done when this prints 1, for a method
  as well.
- [ ] **A write past a Buf's end grows a buffer only one holder sees**
  (found 2026-10-28 while fixing issue #186, 18ef8f73; the same before it):

  ```raku
  my $d = Buf.new(1); my $e = $d;
  $e.write-uint8(3, 9);
  say $d.elems;    # 1, Rakudo 4
  ```

  18ef8f73 writes within a Buf in place (CowStr::mutInPlace), but growing one —
  bufBitOp's `wbytes`, the element store in evalAssignInner and
  rtElemStoreN's bufElemStore — detaches it first (`mut()`), so every other
  holder keeps the old bytes. A routine that grows a Buf it was passed
  reaches an interpreted caller by copy-back (setupRwLinks' rwSynced, and
  rtAotParamBack for a native body) but not a compiled one, which is why
  t/aot/cases/buf-args leaves that case out. Growing the shared body in
  place moves its bytes, so a pointer handed to C (nativecast,
  NativeHelpers::Blob's pointer-to) needs thought first. Done when this
  prints 4, and a compiled caller sees what `sub widen(Buf $b) {
  $b.write-uint8($b.elems, 9) }` added.
- [ ] **`$b[1]++` on a Buf element dies** (found 2026-10-28 while fixing
  issue #186, 18ef8f73; the same on 50122f46):

  ```raku
  my $b = Buf.new(1, 2);
  $b[1]++;          # Index out of range. Is: 1, should be in 0..0
  say $b.list;      # Rakudo (1 3)
  ```

  `$b[0] += 10` and `$b[1] = 7` are right; only the step is not, in a
  variable and in a parameter alike. Done when it prints Rakudo's (1 3),
  compiled too.
- [ ] **An array slot alias still names a position after `@a = …`** (found
  2026-10-29 while fixing issue #215, 67c35490; on v5.5.0 too). Rakudo's
  alias is the element's Scalar, which a list assignment replaces; ours
  (makeArraySlotProxy) reads the same position of the refilled storage:

  ```raku
  my @a = 1, 2, 3;
  my $x := @a[0]; my \y = @a.head;
  @a = 7, 8, 9;
  say $x, y;        # 77; Rakudo 11
  ```

  A kept `@a.head(n)` and a bound slice `my \s = @a[0, 2]` detach on a
  refill now (keptEndsRefill in src/InterpreterCalls.cpp: the array's storage
  finds their buffers). Slot aliases could register the same way, but
  BinaryHeap's sift makes about thirty per call, so the registration must
  cost that path nothing. An `.unshift` after the bind moves the elements
  past the positions every one of these aliases names (Rakudo's keep their
  Scalars: `my \s = @a[2, 0]; @a.unshift(1)` is still (30 10)). Done when
  the line prints 11.
- [ ] **The rest of an Array's views copy its values** (found 2026-10-29
  while fixing issue #215, 67c35490). Only a kept `.head(n)` / `.tail(n)`
  holds the array's elements; the others still copy them, as does copying
  a kept one:

  ```raku
  my @c = 1, 2, 3; my $r = @c.reverse; @c[0] = 42; say $r;   # (3 2 1); Rakudo (3 2 42)
  my @s = 1, 2, 3; my \k = @s.skip(1); @s[2] = 9; say k;      # (2 3); Rakudo (2 9)
  my @m = 1, 2, 3; my \m = @m.map(* * 2); @m[0] = 9; say m;   # (2 4 6); Rakudo (18 4 6)
  my @h = 1, 2, 3; my $h = @h.head(2); my $g = $h; @h[0] = 9; say $g;   # (1 2); Rakudo (9 2)
  my @p = 1, 2, 3; my $p = @p.head(2); @p.shift; say $p;       # (1 2); Rakudo (2 3)
  ```

  `.Seq`, `.reverse`, `.rotate` and `.sort` attach an ElemView, so a write
  through them reaches the array, but not the other way. The last line is Rakudo's
  lazy head reading the shortened array; once read, Rakudo keeps (1 2) too.
  `--exe` bundles a program that keeps a head and writes the array (Codegen's
  noteKeptEnds). Done when each line prints Rakudo's.
- [ ] **A read racing a refill of the same array can read Nil or die**
  (found 2026-10-29 while fixing the concurrent-refill abort, c9560e0d; on
  8a23004a too, behind that abort). Two refills no longer free one buffer
  twice (arrayStore takes the storage's stripe; t/race/array-refill-shared.raku),
  but a READ on another thread takes the element's stripe, not the storage's:

  ```raku
  my @shared = ^20;
  await (^8).map: { start { for ^600 -> $i { my $n = @shared[1] // say "Nil";
                                            @shared = ^20 if $i %% 3 } } };
  # 11 runs in 200 print Nil; reading with `== 1` instead, 3 in 200 died SIGSEGV. Rakudo: neither
  ```

  The move-assign destroys the old elements, then frees their buffer, while
  the read's fast path in evalIndex (the "torn-copy contract, the ELEMENT
  half") has already taken an element's address. Swapping the new buffer in
  and freeing the old after the stripe made it worse (one run in four died
  with one writer). Either the read takes the storage's stripe too (every
  read of a shared array then queues on one lock) or the old buffer is freed
  only once no read can be in it (an epoch, or locking every stripe before
  the free). Done when the repro neither prints Nil nor dies in 200 runs, and
  perf-guard's parallel kernels hold.

## Processes and diagnostics

- [ ] **`run` and `shell` silently accept named arguments they do not take**
  (found 2026-10-20 writing tools/run-fatroast.raku: `run(…, :ENV(%env))`
  ran the child with the parent's environment, and a whole campaign ran
  against the wrong directory):

  ```raku
  run("true", :bogus); say "ok"       # ok, Rakudo: Cannot resolve caller run(true, :bogus) (X::Multi::NoMatch)
  shell("true", :ENV(%())); say "ok"  # same
  ```

  `run` takes `in out err bin chomp merge enc nl cwd env arg0
  win-verbatim-args`. Methods stay lenient in both engines
  (`Proc::Async.new("true").start(:bogus)` is fine). Check Roast (S29-os,
  S17-procasync, `#?rakudo todo`) and the repo's own callers (`grep -rn
  ':ENV(' tools/ t/ lib/`) before refusing.

- [ ] **"Variable '$x' is not declared" inside a string past line 200 is
  reported at line 1** (found 2026-10-20: a typo on line 268 of
  tools/run-fatroast.raku was reported at the shebang):

  ```bash
  { for i in $(seq 300); do echo "#"; done; echo 'say "$nope";'; } > /tmp/g.raku; build/rakupp -c /tmp/g.raku   # :1, want :301
  ```

  A bare `say $nope;` gets 301; 199 lines get the right line too.
  reportUndeclaredVars() in src/DeclCheck.cpp: a variable from the
  interpolation sub-parser carries no line, the finding lands on line 1,
  and `locate()` walks only 200 lines forward from it. Give interpolated
  variable nodes the line of their string; do not widen the window. Cover
  `"$nope"`, `"a {$nope} b"`, `qq/$nope/` and a multi-line statement.

- [ ] **A parse error shows its line and column only when a file is run**
  (found 2026-10-27 while fixing issue #206, 2b9494ae):

  ```bash
  build/rakupp -e 'if 1 { } say 2'       # the header and "at -e:1", no excerpt
  printf 'if 1 { } say 2\n' > /tmp/s.raku; build/rakupp -c /tmp/s.raku   # the header alone
  build/rakupp /tmp/s.raku               # the header, the line, a ^ under `say`, "at /tmp/s.raku:1"
  ```

  The excerpt is printed by rakuppRunOn's ParseError handler
  (src/Runtime.cpp), which reads `-e` code through seedSrcLines only after
  the parse has succeeded; the `-c` handler in src/main.cpp prints the
  header line and nothing else. ParseError::col, which puts the `^` under
  the excerpt, is set only by checkTextAfterBlock; Parser::error knows the
  token it stopped at (`got`) and could set it for every parse error. Done
  when `-e` and `-c` show the excerpt, and most parse errors a column.
- [ ] **`is hidden-from-backtrace` hides nothing** (found 2026-10-28 while
  reading issue #211's backtrace; on v5.5.0 too):

  ```raku
  class C {
      method !inner() is hidden-from-backtrace { die "boom" }
      method go { self!inner }
  }
  sub s() is hidden-from-backtrace { C.new.go }
  s();
  # here:   in method C::inner (with its excerpt), in method C::go, in sub s, in block <unit>
  # Rakudo: in method go, in block <unit>
  ```

  The trait is accepted (src/Interpreter.cpp lists it among the known
  routine traits) and then dropped; the backtrace builder needs it on the
  frame's routine. DBIish raises every error through two hidden methods,
  so a DBIish failure here starts with `error-dispatch` and `set-err`
  (#211's report does). The class in the frame's name is ours on purpose
  (BACKTRACE-PLAN.md). Done when the frames above are Rakudo 2026.09's,
  bar that name, and `--ll-exception` still shows them.
- [ ] **Issue #211's other two reports** (open 2026-10-28; 0e44c5a6 fixed
  its NativeLibs warning). A Cro app using DBIish:
  - In "developer mode", with `:database<:memory:>`, `$dbh.execute` in the
    app's `init-db` dies in DBDish::SQLite's StatementHandle.execute, where
    `sqlite3_step` failed; a file database gets past it. DBIish treats
    `:memory:` as a file name it leaves alone, and nothing else in its
    driver differs. The report omits the SQLite message (the line before
    `(X::DBDish::DBError)`), which names the failure.
  - With a file database, Cro::HTTP::Router dies "Parameter type
    SessionStore::Session not allowed on a request unpack parameter" from
    the app's `auth-routes`. Cro skips that check for a first parameter
    that `is auth` or whose type does Cro::HTTP::Auth, and both pass here,
    a session class in a module of its own and routes `include`d into an
    outer `route` among them.

  Needs the reporter's `lib/DB.rakumod` (to line 13), its SQLite message,
  and how `SessionStore::Session` is declared; or DBIish installed here to
  try `:memory:` under Cro. Done when each runs as under Rakudo, or turns
  out not to be rakupp's.

## Backends

- [ ] **`--exe`: `:=` inside a natively compiled routine copies instead of
  aliasing** (found 2026-10-19 verifying the non-ASCII binding fix; the
  ASCII copy fails the same way, so it is older):

  ```raku
  sub f { my $s = 1; my $r := $s; $r += 10; $s }
  say f();                                    # interpreted 11, --exe 1
  sub g { my @l = 1,2; my $e = 5; @l[0] := $e; $e = 7; @l[0] }
  say g();                                    # interpreted 7, --exe 5
  ```

  The compile reports both routines native, so nothing falls back. At
  mainline level `--exe` already refuses non-scalar binding and bundles.
  In src/Codegen.cpp, emit a shared container for `:=` between VarExprs and
  for an Index bound to a VarExpr, or refuse the routine natively; keep the
  t/exe gate green (strict only on `-O`: list native-vs-interpreted per
  program before and after).

- [ ] **`--exe`: a `die` or `exit` in the mainline skips every END phaser**
  (found 2026-10-20 while making Test's exit status match Rakudo, 07456d29):

  ```raku
  END { say "end" }; exit 3     # interpreted: end, status 3; --exe: status 3, no "end"
  ```

  Top-level ENDs are emitted inline at the end of the main `try` in
  `__rakupp_main_body` (transpileToCpp, `g.topLevelEnds` / emitPhaserBody,
  src/Codegen.cpp ~7100). Moving them after the try breaks scoping: some
  top-level `my`s are C++ locals inside it (`my ($p, $q) = 1, 2;`, and `my
  $y = 3 if True;`, for which `END { say $y }` already makes `--exe` print
  "Compilation failed" with no bundling fallback). Hoist every top-level
  `my` an END can name, or refuse natively so `--exe` bundles. Then: ENDs in
  reverse order, an `exit` inside an END sets the status, a die inside one
  is reported and leaves it. Gate in t/exe/test-status.raku.

- [ ] **`--exe`: a natively compiled routine checks no sigil** (found
  2026-10-27 while fixing issue #207, 3ddcd58b, which made the interpreter's
  `%` parameter take an Associative only; base 170bb19a compiles the same):

  ```raku
  sub u(@a) { @a.^name }; sub h(%h) { %h.^name }
  my $x = 1; say (try u($x)) // $!.^name; say (try h($x)) // $!.^name   # interpreted: the binding failure twice, --exe: Int Int
  sub p(%h) { %h.^name }; say (try p((a => 1, b => 2))) // $!.^name   # interpreted: the failure, --exe: List
  sub d(:%h = (a => 1, b => 2)) { %h.^name }; say (try d()) // $!.^name   # the same
  ```

  bindAs (src/Codegen.cpp) makes an `@` or `%` parameter `unitemized(…)`
  of whatever came, with or without `-O`, and t/exe/run.raku compares the
  two binaries with each other, so both agree on it. A runtime check the
  generated code calls (as rtBindTyped is for a typed parameter), with the
  interpreter's rules (notPositionalArg, notAssociativeArg and the `&`
  test in bindParams), or a refusal so the routine bundles. Done when
  t/regression/issue-207-hash-param-takes-associative.raku and
  at-param-binding-nonpositional.raku pass compiled, `-O` too, and the
  t/exe gate is as green as before.

- [ ] **`--target=js`: `$_` in `with`/`given` and on the right of
  `andthen`/`orelse`/`notandthen` is a copy** (found 2026-10-20 while fixing
  issue #185 in the interpreter):

  ```raku
  my $x = 1; with $x { $_ = 5 }; say $x        # JS 1, interpreter and Rakudo 5
  my $x = 1; $x andthen $_ = 5; say $x         # JS 1, should be 5
  my @a = 1; with @a[0] { $_ = 5 }; say @a     # JS [1], should be [5]
  ```

  `build/rakupp --target=js j.raku -o j.js && node j.js`. src/codegen/Js.cpp
  ~1321 (`withTopic(t, rhs)` holds the value in a temp) and the with/given
  emission; the runtime is src/js-rt/*.js. Only right sides that can write
  `$_` need the alias (topicAliasedRhs / topicWriteFree in
  src/InterpreterRegex.cpp). Add a t/js case; the gate needs RAKUPP_JS=node.
  `--exe` declines these operators and bundles, so only JS copies.

- [ ] **Split src/Codegen.cpp** (2026-10-27, fc262224): at 10,048 lines it
  is past the default file ceiling, and tools/source-helpers/budget.txt gives
  it a named one of 10,100 so that a fix could land without the split. The
  build waits on its slowest file. The statement emitters (stmtNative and
  what it calls: loopBody, emitSeq, the ctl and LEAVE helpers) and the
  expression emitter (ex and its arms) are the natural halves; the scripts in
  tools/source-helpers/ split Interpreter.cpp and Builtins.cpp the same way.
  Done when each half is under 10,000 and the named ceiling is gone.

- [ ] **Split src/Builtins.cpp again** (2026-10-28, f483f0e6, issue #192): the named
  ceiling of 10,512 that 5df4494a recorded had one line left on f30323e3,
  and the junction-order fix took the file to 10,532, so
  tools/source-helpers/budget.txt gives it 10,600 rather than split it
  inside a fix. methodCall and methodCallInner's first segment are most of
  it; tools/source-helpers/builtins.plan is the plan that cut the
  MethodCallPart1b/1c segments out of it, and can cut another. Done when it
  is under 10,000 and the named ceiling is gone.

- [ ] **Split src/InterpreterCore.cpp** (2026-10-28, c50eb3b8 and
  642a320a): the named ceiling of 32,900 that 34d064ca recorded had one
  line left on 50122f46. Two fixes on parallel branches each passed it —
  the `for` over an uncontained value to 32,909, the `try`/trailing-CATCH
  one to 32,923 — and each gave it 33,000 in tools/source-helpers/budget.txt
  rather than split it inside a fix; together they take it to 32,951.
  tools/source-helpers/interpreter.plan is the plan that
  split Interpreter.cpp, and split.raku can cut along another one. Done
  when it is under its 34d064ca ceiling again.

- [ ] **Compiled prefix `+` of a non-numeric Str or Match dies** (found
  2026-10-29 fixing issue #189, 7f28cba0; the Str case on v5.5.0 too):

  ```raku
  my $s = "=42"; say (+$s).^name;          # --exe dies "Cannot convert string to number"; Rakudo Failure
  "=42" ~~ / "=" (\d+) /; say (+$/).^name; # --exe dies the same; interpreted Any; Rakudo Failure
  ```

  Codegen's prefixBuiltin writes `+$x` as `applyArith("+", 0, $x)`, whose
  Str arm (a Match's too, since 7f28cba0) numifies with numifyStrOrThrow. `-$x`
  goes through rtNeg to prefixNumeric, which answers the interpreter's
  Failure; `+` can do the same. The interpreter's own `+` of a Match is Any
  where Rakudo has a Failure. Done when both engines answer Rakudo's
  Failure for both.

- [ ] **`--exe` does not embed a module found through the program's own
  `use lib`** (found 2026-10-29 reproducing issue #189; on v5.5.0 too):

  ```sh
  mkdir mods; printf 'unit module UL;\nsub hi() is export { "hi" }\n' > mods/UL.rakumod
  printf 'use lib "mods";\nuse UL;\nsay hi();\n' > a.raku
  rakupp --exe a.raku -o a.bin
  # --exe: not embedded: UL — not found on the module search path (the binary will need the disk at run time)
  ```

  `rakupp -I mods --exe a.raku` embeds it and compiles its routine
  natively. collectModuleGraph (src/main.cpp) searches
  effectiveSearchPath(libPaths): `-I`, RAKULIB and the defaults, `lib`
  among them (so `use lib "lib"` looks right), not what the program's `use
  lib` adds, literal or `$?FILE.IO.parent.add(…)`. The binary then loads
  the module from disk, interpreted, and dies where it is absent; issue
  #189's program, built so, never compiled the routine it was about. Done
  when the paths a `use lib` adds at compile time are searched too.

## Performance

- [ ] **A composed curry like `* cmp * == More` is about 2.2x Rakudo per
  call** (found 2026-10-20 after the Order-path work, b35718ba, which gained
  this shape only 14%):

  ```raku
  my &more = * cmp * == More; my $n = 0;
  for ^1_000_000 -> $i { $n++ if more($i, 500_000) }
  ```

  200k calls of `tops(@a[3], @a[5])`: ~80 ms against Rakudo's ~36. The
  Whatever-curry closure built in applyArithGeneral (the `// Whatever-currying`
  block, `resolve` lambda) calls its inner curry `* cmp *` through
  `I.callCallable`, a full call, which dominates `sample`. Give an
  engine-built inner curry (a builtin, isWhateverCode, no user code, no
  wrappers, no pending arg writer) a direct path, or flatten a
  curry-of-curry when it is built. Keep the `*++ + *--` write-back
  (pendingArgWriter), the captured `shadow` operator and lexShadowedInfix.
  Measure on top of f9f0cac1 (the `tctx()` change) with interleaved pairs
  and instructions retired, plus BinaryHeap 2.0.3's benchmark/ scripts.
  BinaryHeap's comparator has exactly this shape.

- [ ] **A `for` that binds through bindParams runs at half speed, and so
  does the loop after it** (found 2026-10-27 while fixing issue #207,
  3ddcd58b, which sends `-> %h` that way; on 170bb19a too):

  ```raku
  my @h = (%(a => $_) for ^200_000); my @o = (%(a => $_) for ^200_000); my $s = 0;
  for @h -> $h { $s += $h<a> }          # 0.025 s
  for @o -> $h is copy { $s += $h<a> }  # 0.056 s: bound through bindParams
  for @h -> $h { $s += $h<a> }          # 0.047 s, a different array and a plain loop
  for @o -> $h { $s += $h<a> }          # 0.025 s
  ```

  Interpreted, best of 3 on 170bb19a, each loop timed with `now`. Every
  loop whose signature pointyParamNeedsBinding sends to real binding pays
  it (a type over anything but an integer Range, `is copy`, a default, a
  sub-signature, a slurpy, `%h`): execForStmt's `fs->params` arm makes a
  slab Env and a row ValueList per iteration and calls bindParams. Then
  whichever loop runs next is slow too, so something of those 200,000
  iterations lingers; profile that third loop first. Done when it runs as
  the first does, and the bound loop is nearer the plain one; then `-> @a`
  can take the same path (see "Some parameters still bind what their sigil
  or type refuses").

- [ ] **A control word as an operand still throws in five shapes** (found
  2026-10-27 while fixing issue #195, 48b76d56, which took `// next`, `or
  next` and `// return` to the cooperative flag):

  ```raku
  sub f($i) { $i %% 2 ?? $i !! Nil }
  for ^10_000 -> $i { my $x = try { f($i) } // next }    # 69.6 µs a skip
  for ^10_000 -> $i { my $x = do { f($i) } // next }     # 69.6
  my $s = 0; for ^10_000 -> $i { $s += f($i) // next }   # 34.5
  for ^10_000 -> $i { my ($a, $b) = f($i) // next }      # 56.9
  my @a; for ^10_000 -> $i { push @a, f($i) // next }    # 43.2
  ```

  `my $x = f($i) // next` is 0.6 on the same box. Each throws a C++
  exception because ctlOnStmtPath (src/InterpreterCore.cpp) cannot prove
  that nothing uses the Any the flag form hands back. A block run inside
  the statement (`try`, `do`) replaces `curStmtExpr` with its own statements
  and no frame guard puts it back: restore it where such a block returns, or
  in exec's ExprStmt arm, measured against perf-guard's asg and loopsum. A
  compound or list assignment stores through paths that do not guard their
  store (coopStoreShape takes only a plain `=` into one variable or
  element): guard the op= path as evalAssignInner's general path is guarded.
  A call argument cannot take the flag, since the call would run. Done when
  the first four run within 2x of `my $x = f($i) // next`.

- [ ] **Issue #179's TIRak figure is not explained** (open 2026-10-29;
  308aecdf made the grapheme table one walk): a 16 MB window decoded as
  latin-1 and searched for an Arabic keyword took 20–30 s in TIRak, where
  Rakudo took 17 ms. Building the table that `.index` from a position reads
  costs 25 ms for 16 MB now, once per string, and each call after it 0.5
  µs. Seconds mean it is built again and again, or skipped: a `.index` on a
  string made afresh each call (a `.substr`, `.lc`, a `~`), or one the
  start-position paths do not take (a Num or Rat position, `:i` or `:m`, a
  needle that is not a plain Str). Needs the reporter's search loop.

## Gates

- [ ] **t/jit/run.raku: 5 of its 21 tier-up cases enter no kernel** (found
  2026-10-22 gating the FLOAT-HEAVY-ISSUES work; on v5.5.0 too, on macOS
  arm64): the gate ends `not ok - only 16 of the 21 tier-up cases actually
  entered a kernel`. The five are t/jit/cases/calls.raku, elements.raku,
  inline.raku, methods.raku and natives.raku:

  ```bash
  RAKUPP_NO_KERNELS=1 build/rakupp --jit=sync,threshold=0,nocache,stats t/jit/cases/calls.raku
  ```

  prints `kernels entered 0` (the other 16 print 1 or more). Each still
  agrees with the interpreter, so only the coverage check fails. For each
  case, `--jit=verbose` says why its loop is declined: a whitelist the
  case's construct (a call, an element, an inlined sub, a method, a native)
  has fallen off, or the case no longer shows what its header says. Then
  fix the backend or the case.

## FatRoast

- [ ] **What FatRoast's v2 transforms still break on both engines** (in
  the FatRoast repo, /Users/ash/fatroast, not this one; found 2026-10-23).
  strings-v2, names-v2 and ops-v2 left alone what v1 broke (the CHANGELOG
  entry has the counts): no fattened file is refused any more where the
  plain one compiles, and of the files that fully pass in Roast, Rakudo
  fails 743 under strings-v2, 148 under names-v2 and 1 under ops-v2 (v1:
  883, 240, 27). Most of strings-v2's are the `mixed` palette doing what it
  says (it keeps neither case nor order). Candidates for a v3, each a
  changed rule (docs/PLAN.md invariant 3):
  - strings: a PATH in a string (`open('t')` in S16-io/getc.t, file names):
    mapped, it names no file, and two tests running in one overlay can
    collide on the mapped name (S16-filehandles/filetest.t, open.t and getc.t
    passed in one Raku++ run and failed in the next).
  - strings: a hash key spelled both ways, bare (`foo => 1`, `:foo`) and
    quoted (`%h<foo>`, `'foo' => 1`). Leaving `%h<foo>` subscripts alone was
    measured: 26 files gained and 16 lost, a wash, so it was not taken.
  - names: a renamed class or sub spelled in a string (`is Foo.^name,
    'Foo'`): the rename could follow a string that is exactly the name.
  - FatRoast's tokens come from this repo's highlighter: a misread there is
    an edit in the wrong place (the 2026-10-23 fixes are in CHANGELOG-NEXT).

  To measure a candidate: fatten into a scratch directory with this repo's
  build/rakupp (it is the highlighter FatRoast tokenizes with), give it
  fat/README.md, and run `build/rakupp tools/run-fatroast.raku
  --fatroast=<scratch> --engine=build/rakupp,rakudo --overlay=<scratch2>`;
  a compile-only check (`rakupp -c` on each fattened file and its plain
  one, from an overlay of Roast) finds the breaking rules in two minutes.
  Under Rakudo, kill S12-enums/thorough.t by hand in the names parts (see
  "run-roast leaves grandchildren behind" in TODO.md).

- [ ] **An undeclared variable is named where its spelling first appears,
  inside a literal too**: in names-v1's integration/advent2014-day16.t,
  `$a` is undeclared in `Q:s/foo $a\n/` (line 22, where Rakudo names it),
  and Raku++ names line 16, where `say Q/foo $a \n/` holds the spelling in a
  literal that interpolates nothing. Both refuse the file; only the line
  differs (found 2026-10-23). Likely DeclCheck's textual scan.

- [ ] **`.index(Str)` and `.indices(Str)` die as X::TypeCheck::Binding::
  Parameter; Rakudo 2026.09 dies X::Multi::NoMatch** ("Cannot resolve caller
  indices(Str:D: Str:U)"), naming the candidates. Roast asserts only
  `dies-ok` (S32-str/index.t, indices.t). Found 2026-10-23.
