# Follow-ups

*Collected 2026-10-20; added to 2026-10-23.*

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

- [ ] **A non-ASCII name after a twigil is dropped by the lexer** (found
  2026-10-19 by FatRoast's `names-v1`, while fixing non-ASCII binding in
  edf09560):

  ```raku
  class A { has $.ч = 1 }; say A.new.ч                    # No such method 'ч', Rakudo 1
  class A { has $!ч = 4; method m { $!ч } }; say A.new.m  # Two terms in a row, Rakudo 4
  my $*ч = 3; sub f { $*ч }; say f()                      # Target is not assignable, Rakudo 3
  say { $^ч + $^ш }(1, 2)                                 # Undefined routine 'ч', Rakudo 3
  say { $:ч }(ч => 5)                                     # ч => True, Rakudo 5
  ```

  `rakupp --ast` shows `has $.ч` as `$.`. src/Lexer.cpp (~3346) takes a
  twigil only when `isIdentStart(peek(1))`, which is ASCII. Downstream,
  `ascii::isalpha(name[2])` tests (src/InterpreterCalls.cpp ~4792;
  src/Parser.cpp ~7500, 8628, 11253, 11665, 13902, 13907, 15368) want
  `nameLetterAt(name, 2)` from src/Ast.h. Probably most of what is left of
  FatRoast's S12 divergence. Test `.` `!` `*` `^` `:` with Cyrillic,
  Latin-1, CJK and astral names.

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

## Control flow and containers

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

- [ ] **Triage the first FatRoast leads** (found 2026-10-20 by the first full
  run of tools/run-fatroast.raku). Read-only: report, then file each finding
  here or in TODO-MISCELLANEOUS.md.
  1. `ops-v1` costs rakupp 17 files that pass in plain Roast:
     S02-literals/numeric.t, S02-literals/quoting.t (noTAP),
     S02-names-vars/varnames.t (noTAP), S02-names/pseudo-6e.t,
     S02-types/infinity.t, S02-types/whatever.t, S03-operators/assign.t
     (noTAP), S03-smartmatch/signature-signature.t (noTAP),
     S04-declarations/our.t, S05-grammar/protoregex.t, S06-advanced/wrap.t
     (noTAP), S06-other/main-usage.t, S06-other/main.t,
     S06-signature/introspection.t (noTAP), S09-typed-arrays/hashes.t
     (noTAP), S12-meta/primitives.t, S32-list/pick.t (noTAP). For each, does
     Rakudo lose it too (the transform or a shared bug), or only rakupp (a
     lead: reduce it to a one-to-three-line reproducer)? assign.t and
     pick.t are at least partly the transform bug below.
  2. `strings-v1.mixed` S32-io/io-cathandle.t takes ~32 s under rakupp (17 s
     system time) against ~1 s plain, so it times out at run-roast's 10 s;
     run alone rakupp passes 15/31 and Rakudo 3/31. Find what is slow
     (`sample`/dtrace), and why rakupp passes 12 more.

  Run: `nice build/rakupp tools/run-fatroast.raku -j2 --parts=roast,ops-v1
  --engine=build/rakupp,/opt/homebrew/bin/rakudo --overlay=<scratch>`;
  results in OVERLAY/lists/ops-v1.rakupp-vs-rakudo.diff, *.lost, *.log.

- [ ] **FatRoast `ops-v1` rewrites `+<=` and `~<=` into `+≤`, which does not
  compile** (in the FatRoast repo, /Users/ash/fatroast, not this one; found
  2026-10-20):
  lib/FatRoast/Transform.rakumod lines 262-263 (the `comparison` rules for
  `<=`→`≤` and `>=`→`≥`) also fire inside `+<=`, `+>=`, `~<=`, `~>=`.
  Committed victims: fat/ops-v1/S03-operators/assign.t (4 lines) and
  fat/ops-v1/S32-list/pick.t:173 (`$v +≤ 1`); Rakudo rejects them
  ("Preceding context expects a term, but found infix ≤"). First decide
  whether to fix ops-v1 in place or bump to ops-v2 (docs/PLAN.md invariant
  3: a changed rule is a new version, which renames the fat/ tree and the
  run-fatroast part). Then extend the lookbehinds, scan fat/ops-v1 for
  other rewrites that fail `rakudo -c` where the plain file passes, add a
  case to t/01-transforms.rakutest, and regenerate (`nice rakupp
  bin/fatroast fatten --roast=$HOME/roast --out=fat --jobs=2`, Roast at
  da425eb92).
