# Follow-ups

*Collected 2026-10-20; added to 2026-10-27.*

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

## Control flow and containers

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

## Gates

- [ ] **t/aot/run.raku is red, 13 of 16, from stale expectations** (found
  2026-10-20 gating 07456d29; all three date from 48223469, 2026-10-16, and
  went unnoticed because the gate is not part of t/run.raku):
  - bundle-fallback and subscripts: line ~228 detects bundling with
    `'bundling the whole program'`, but src/main.cpp ~1900 now says "…
    bundling the program with the interpreter instead, its routines compiled
    natively where they can be". Key the check on something stable (a
    marker, or RAKUPP_AOT_REPORT), not prose.
  - dynamic-vars: five subs in t/aot/cases/dynamic-vars/lib marked `#aot:
    native` are now interpreted ("a dynamic variable declared inside a
    routine", Codegen.cpp ~1456). Re-annotate them, or implement TODO.md's
    native `my $*X` if it exists by now.

  Baseline on a clean worktree build: the main checkout's binary and tree
  drift apart and fail 0/16.

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
