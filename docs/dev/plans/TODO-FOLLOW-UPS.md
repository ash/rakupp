# Follow-ups

*Collected 2026-10-20; added to 2026-11-10.*

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

- [ ] **Four word-list spellings still read differently from Rakudo** (found
  2026-11-08 fixing #231, 5f6d78e4; each the same before it):

  ```raku
  say « :x\<a ».raku;       # rakupp: couldn't find final '>'   Rakudo: (:x, "<a")
  say « infix:<+> ».raku;   # rakupp: "infix:<+>"               Rakudo: ("infix", "+")
  my $x = 5; say « :$x:y ».raku;  # rakupp: (:x(5), :y)        Rakudo: Variable '$x:y' is not declared
  say qww« b' ».raku;       # rakupp: ("b", " ")                Rakudo: couldn't find final "'"
  ```

  The first is the lexer: it reads the `<` after a backslash in a «…» list
  as an opening angle. The second is Rakudo's keyless colonpair, `:<+>`,
  whose word-list reading is its value (`« :<a b> »` is ("a", "b")). The
  third is the ordinary colonpair parser: `(:$x:y)` in code is two pairs
  too, where Rakudo reads `$x:y` as one long name. The fourth is the qww
  scan, which accepts a quoted span that never closes.

- [ ] **A `my &f`, a variable or a class beside an import of its name
  compiles** (found 2026-11-08 fixing #230, eb574770): Rakudo refuses each
  with X::Redeclaration, in either order.

  ```raku
  # Other exports `sub run-word`; M exports `our $shared` and `class K`
  use Other; my &run-word = -> $x { "lam" }; say run-word(1);   # lam, Rakudo: Redeclaration of symbol '&run-word'.
  use M; my $shared = 7; say $shared;                          # 7, Rakudo: … '$shared'.
  use M; class K { }                                           # compiles, Rakudo: … 'K'.
  ```

  importClashesWithLocal (InterpreterModules.cpp) judges routines only: a
  `my &f` is not hoisted, so it lands on the import afterwards, and a pad may
  hold its slot before the `use` runs. Variables come in through
  importPackageExports, which skips a local; types through the import loops.
  Found 2026-11-08 (with b8d8799d): a module's exported variables and types are
  not lexical imports at all. loadModuleImpl publishes them to global_ even
  from a block, so `{ use M }; say $shared` says 1 and `K` is M::K after
  the block, where Rakudo refuses both as undeclared, and a `my $shared` in
  the block is a different variable from the one the import made. And
  `my $shared = 7; use M; say $shared` says 1 (the import wins). A lexical
  road for them is the first step; the clash check follows from it.

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
- [ ] **A `token` the packrat memo answers fires its action once** (found
  2026-10-30 making actions fire as subrules complete, 594d4aac; the final
  build fired it once before that too):

  ```raku
  grammar G { token TOP { <a> 'x' || <a> 'y' }; token a { \d+ } }
  my $n = 0; class A { method a($/) { $n++ } }
  G.parse('12y', :actions(A)); say $n;   # 1, Rakudo 2
  ```

  Rakudo has no memo, so the second `<a>` matches again and fires again.
  GrammarMatcher::matchSubMeta's ratchet path serves the second call from
  memo_ and fires only an entry a probe left unfired (MemoEntry::fired).
  Firing on every replay would mean building the node again for each call
  (the cache in grammarParse's `fired` is keyed by the child map, which a
  replay shares). Done when the count matches Rakudo's without a JSON-grammar
  slowdown (tools/bench/grammar-json).
- [ ] **A sub the main program imports is visible to a module that never
  imported it** (found 2026-10-31 hiding the program's operators from
  modules, 8a6c9d9a, which did that for operators only):

  ```raku
  # lib/Helper.rakumod:      unit module Helper; sub helper() is export { "helper" }
  # lib/CallsHelper.rakumod: unit module CallsHelper; sub call-helper() is export { helper() }
  use Helper; use CallsHelper;
  say call-helper();   # helper; Rakudo refuses to compile CallsHelper
  ```

  The program's top scope is global_, the root of every scope; 8a6c9d9a
  puts a hole in moduleRoot_ (between the modules and global_) for each
  OPERATOR the program defines (hideProgramOperator,
  src/InterpreterModules.cpp), because an operator there shadowed the
  built-in a module meant. A routine name the program imports reaches a
  module the same way, which only changes what code Rakudo would reject —
  unless it shadows a built-in a module calls (`say`, `dd`). Done when the
  program's lexicals live in a scope of their own, under global_, and the
  holes go.
- [ ] **`.AST` still drops a `use`'s import list, and a few spellings** (found
  2026-10-31 making the view keep the rest, acf27e4b; `is export`, `is
  DEPRECATED`, an attribute's `is default` and `handles` are kept since e4ec869c):

  ```raku
  print Q[use Test <foo>; 1].AST.DEPARSE;                 # use Test;
  print Q[use Test :DEFAULT; 1].AST.DEPARSE;              # use Test;
  print Q[say -> { 1 }; my $b = 1].AST.DEPARSE;           # the block over three lines and no `;`, Rakudo `say -> { 1 };`
  ```

  Rakudo 2026.09 writes each as given. The import list cannot be rendered
  from what the parse keeps: `<foo>` and `:foo` both land in
  `UseStmt::importArgs` as the word `foo`, and `:!foo` and `:foo<v>` not at
  all, so it needs the spelling recorded (an Ast.h field and a
  kAstSerialVersion bump). An attribute's traits come in a fixed order, as
  its flags, where Rakudo keeps the source's; a renaming `handles(:a<b>)`
  and a user trait on an attribute are not kept. The block on one line
  and the `;` after a statement ending in one are Rakudo's style only (the
  program is the same).

- [ ] **An unknown parent is refused when the declaration runs; Rakudo
  refuses it at compile time** (found 2026-11-08 making a subset's unknown
  trait an unknown parent, f59ff84c):

  ```raku
  say "ran"; class C is nosuch { }          # ran, then the error; Rakudo: ===SORRY!=== and nothing
  say "ran"; role R is nosuch { }           # the same
  say "ran"; subset S is nosuch of Int;     # the same
  ```

  The class and role declarations (src/InterpreterModules.cpp, around
  unknownParentMessage) and applySubsetTraits (src/InterpreterBinding.cpp)
  run with the declaration, after any `trait_mod:<is>` candidate has had
  the name. A method two roles bring (X::Role::Unresolved::Method and
  ::Multi) and an only method beside a role's multi are the same: Rakudo
  refuses them at compile time, here the class declaration refuses them as
  it runs (refuseRoleClashes, since 846ae003), and an EVAL's is no
  `+{X::Comp}`.

## Operators and types

- [ ] **A Range of big Ints assigned to an array reads as endless** (found
  2026-11-10 testing self-packed Int arrays; the same on 62474b1f and on a
  build of 30 October):

  ```raku
  my @r = 2**64 .. 2**64 + 19;
  say @r.elems;   # rakupp: Cannot elems a lazy list   Rakudo: 20
  say @r[0];      # rakupp: 9223372036854775807        Rakudo: 18446744073709551616
  ```

  `coerceArray`'s Range arm (`src/Interpreter.cpp`) asks `rTo() >=
  9000000000000000000` for an endless Range before it looks at the carried
  big-Int ends, which `Value::flatten()` walks correctly:
  `(2**64 .. 2**64 + 3).elems` is 4. The other tests of that sentinel may make
  the same mistake.

- [ ] **An enum value's `.WHAT` is not `eqv` to its type** (found 2026-11-10
  writing the same test; the same on 62474b1f and on a build of 30 October):

  ```raku
  enum Colour <Red Green Blue>;
  my $g = Green;
  say $g.WHAT eqv Colour;            # rakupp: False   Rakudo: True
  say (Green.WHAT,) eqv (Colour,);   # rakupp: False   Rakudo: True
  say $g.WHAT === Colour;            # True in both
  ```

- [ ] **A method's `handles` delegations: the renaming form, `.can` and
  `.^methods`** (found 2026-11-09 reviewing pull request #233, 88f81c1f;
  each the same before it):

  ```raku
  class Pg { method pg { "p" } }
  class H { method x handles(:page<pg>) { Pg.new } }
  say H.new.page;      # rakupp: No such method 'page'   Rakudo: p
  class A { has $.p handles <page> }
  say A.can("page").elems, A.^methods.grep(*.name eq "page").elems;  # 00, Rakudo 11
  ```

  The parser's method trait ladder (Parser.cpp, the `handles` arm beside
  `is export`) takes words and strings, not colonpairs; an attribute's
  `handles` keeps `handlesTo` for the renaming, and a method's would need
  the same in `methodHandles`. Rakudo makes each delegated name a real
  method, which is why `.can` and `.^methods` list it; rakupp keeps
  delegations in tables (ClassAttr::handles, ClassInfo::methodHandles), so
  both would have to look there. In the same spirit, a class's own `method
  page` beside a composed role's `method P handles <page>` is
  X::Method::Duplicate on Rakudo and the class's method here.

- [ ] **Role and method declaration errors' words and types** (found
  2026-11-09 fixing c0aa3e0e and 655d13dd; each the same before them):
  X::Role::Parametric::NoSuchCandidate says "No appropriate parametric role
  variant available for 'R'" where Rakudo adds a colon and the dispatch's
  own line, "Cannot resolve caller (C, Str:D); none of these signatures
  matches: …" or "Ambiguous call to '(C, Int)'; …"; and two methods of one
  name in a class are X::Redeclaration, Rakudo's X::Method::Duplicate
  (`class A { method m { 1 }; method m { 2 } }`, InterpreterModules.cpp's
  "one name, two ONLY methods" check), in the same words.

- [ ] **A Range of objects** (found 2026-11-08 making a FALLBACK leave Any's
  methods to Any, 34e48087; Any's list methods and mutators on a plain
  object answer since 586a7f14 and a343a825):

  ```raku
  class P { }; say (P.new .. P.new).raku;   # 0..0, Rakudo P.new..P.new
  say P.new.minmax.raku;                    # (P.new, P.new), Rakudo P.new..P.new
  ```

  A Range here holds numbers or strings; its ends would need to be any
  value, compared with `cmp`.

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

- [ ] **A sub-signature binds a value that cannot Capture, and a nested
  one's refusal leaves out the outer parameter** (found 2026-11-09 making
  `--exe` bundle a sub-signature, 9102ff01; its refusal is Rakudo's words
  since f907b840):

  ```raku
  sub q(($a, $b)) { }; q("xy");            # lives, Rakudo X::Cannot::Capture
  (1, 2).map(-> ($a, $b) { }).eager;       # lives, Rakudo "Cannot unpack or Capture `1`."
  sub z(@o ($a, @i ($b, $c))) { }; z((1, (2,)));
  # "… in sub-signature or parameter @i in sub-signature",
  # Rakudo "… in sub-signature or parameter @i in sub-signature or parameter @o"
  ```

  bindParams's `destructure` (src/InterpreterCore.cpp) binds a value that
  is no list as a list of itself, where Rakudo asks its `.Capture`: a Str,
  an Int or a Bool has none (X::Cannot::Capture), a Num is an empty
  Capture (the arity refusal), and an object's is its public attributes as
  nameds. refuseSubSigArity knows only the innermost holder's name, and
  how deep it is (subSigBind); each level would add its own as the
  refusal goes out, as Rakudo's binder does.

- [ ] **A `for` loop's `-> @a` binds what its sigil refuses** (found
  2026-10-27 while fixing issue #207, 3ddcd58b, which made a `%` parameter take
  an Associative only; the rest of that item fixed 2026-11-08, 0ca4968e):

  ```raku
  for (1,) -> @a { say @a.^name }               # Int, Rakudo: expected Positional but got Int (1)
  ```

  The name-only loop binder: pointyParamNeedsBinding (src/Parser.cpp) sends
  `%` to bindParams since 3ddcd58b and not `@`, because `for @rows -> @r` is
  common and full binding costs it (see the Performance item); `given` and
  `with` send both since 0ca4968e. Done when the line answers as in Rakudo 2026.09
  and the loop benchmarks hold.

- [ ] **The JS runtime's DateTime string check** (found 2026-10-28 while
  fixing issue #208, 0f19ab0e; the interpreter's non-numeric field and Str
  timezone answer as Rakudo's since e8a8ba53): src/js-rt/40-objects.js has a
  check of its own (the `pos.length === 1` line, mirrored in
  src/JsRuntimeSrc.cpp). By reading, its DateTime pattern is not anchored
  at the end, so trailing text passes, and it wants a time, so
  `DateTime.new('2023-03-04')` is refused where Rakudo gives midnight.
  Unchecked: there is no node on the Mac this was found on. The
  semantics sheet's two Rakudo bugs (DT-19's date-only `2012-13-22` and its
  `.got` Str) are not to be copied.

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

- [ ] **A `$=pod` declarator block names its declarand only once `.WHY` is
  asked** (found 2026-11-09 making a `#=` after a body the declaration's,
  983b9524; the same before it):

  ```raku
  #| a
  class C { }
  say $=pod[0].WHEREFORE.raku;   # Any, Rakudo C (and C here once `C.WHY` ran)
  #| b
  sub f {
  }
  #= c
  say $=pod.elems;               # 3, Rakudo 2: "c" is an entry of its own
  say &f.raku;                   # &f, Rakudo sub f { #`(Sub|…) ... }
  my $x = #|{ p } sub { }; say $x.WHY.raku;   # Nil, Rakudo the block "p"
  ```

  `$=pod` is a textual scan of the source (parsePod, src/Pod.cpp), which
  runs on a precomp hit too, with no parser behind it, so it cannot see
  where a body closes; `.WHY` links an entry to its declarand by line when
  it is asked (the declarator lambda in src/Builtins.cpp). The Lexer
  records a bracketed `#|{ … }` on the line it ends on, and a declaration
  on that same line looks only above it. And a multi-line `#={ … }` after
  code keeps only its first line in `$=pod`, where `.WHY` has all of it.

- [ ] **A WhateverCode gists as `WhateverCode.new`** (found 2026-11-08 making
  arithmetic on a Whatever value die):

  ```raku
  say 5 ~~ *;                      # WhateverCode.new, Rakudo 5 ~~ * (its source)
  my &f = * * 2; say &f.raku;      # WhateverCode.new, Rakudo * * 2
  say (*.uc).gist;                 # WhateverCode.new, Rakudo (*.uc).gist (the method curries in)
  ```

  A WhateverCode is a builtin closure here, made at eight sites (`grep
  isWhateverCode = true`), none of which keeps the expression it curried.
  Each would carry its Expr, and the gist and `.raku` deparse it through the
  RakuAST view (RakuAstView.cpp) as `.AST.DEPARSE` does.

## Control flow and containers

- [ ] **A self-packed Array shared between threads dies when they refill it**
  (found 2026-11-11 by t/race/array-refill-shared.raku, red since 6501ade2:
  about 125 runs in 200 died, SIGSEGV, SIGTRAP or SIGABRT). It is not new
  with the Int kind: the same program over Nums (`my @shared =
  (^20).map(* + 0e0)`, refilled from eight `start` workers) died 94 runs in
  100 on 62474b1f, 0 under RAKUPP_NO_SELFPACK=1; the gate refilled with Ints
  only. arrayStore no longer packs a refill while workers are live, as
  packedElemStore already did, which leaves 3 to 7 deaths in 200 (Ints) and
  5 in 100 (Nums): an Array that packed BEFORE the workers started still
  changes form under them, since each refill's `raceW(lv) = nv` swaps the
  variable's payload from the PackedArr to the list outside the stripe the
  refill takes, while another thread reads `lv` to find that stripe. Done
  when the gate and the Num form are 0 in 1,000, and the gate refills a Num
  array too.
- [ ] **t/race/varcell.raku answers `bad: 1` about one run in 300 to 500**
  (found 2026-11-11; on d03d7a19 and on 62474b1f, so older than self-packed
  Ints; no death, a worker's result was not True). Not yet reduced: a
  variant that prints which of the three reads went wrong ran 400 times
  clean.

- [ ] **A top-level `use` and `constant` run in the mainline, after BEGIN,
  CHECK and INIT** (found 2026-10-30 checking what a #190 build runs; on
  main at 8aec0a66). Rakudo runs a unit's compile-time parts while it
  parses, in source order — a `use` (and the module's mainline with it), a
  BEGIN, a constant — then CHECK, then INIT, then the mainline.
  Interpreter::run (src/InterpreterModules.cpp) runs the top-level BEGIN
  blocks first, then CHECK and INIT, and meets a `use` or a `constant` only
  when the mainline reaches it, after the mainline statements above it:

  ```raku
  # lib/N.rakumod: unit module N; note 'N mainline'; sub nf is export { 'nf' }
  note 'mainline 1';
  use N;
  BEGIN note 'BEGIN';
  constant C = do { note 'constant'; 42 };
  CHECK note 'CHECK';
  INIT note 'INIT';
  END note 'END';
  note 'mainline 2';
  # rakupp -I lib: BEGIN, CHECK, INIT, mainline 1, N mainline, constant, mainline 2, END
  # Rakudo:        N mainline, BEGIN, constant, CHECK, INIT, mainline 1, mainline 2, END
  ```

  Where it breaks a program: a top-level BEGIN cannot call what a `use`
  above it imports. `BEGIN note nf()` in that program dies "Undefined
  routine 'nf'" (Rakudo prints nf), though `use Test; BEGIN ok 1` works. A
  binary built since 946d29e4 inherits the order: its build runs the
  BEGINs, then the `use`s, then the constants, and the binary runs each
  module's mainline again when it starts, after INIT. TODO.md's "A `use`
  inside a routine runs when the routine is CALLED" is the same gap one
  level in.

  A way in: run()'s partition already pulls the top-level BEGIN and CHECK
  blocks out of the mainline; a `use` and a constant would join the BEGINs
  there, all in source order, before CHECK. Two things lean on today's
  order: the module INIT hoisting in src/InterpreterModules.cpp (its
  comment says "In rakupp a `use` is a mainline statement") and
  ctMainlineStmt (src/CompileTime.cpp), which picks the `use`s and
  constants out of the mainline loop for a binary's build and would move
  with them. A constant reading a mainline variable must keep Rakudo's
  answer (`my $n = 5; constant C = $n` is 5). Done when the program above
  prints Rakudo's order, and `BEGIN note nf()` prints nf.

- [ ] **A typed scalar's container handed out by an expression takes
  anything** (found 2026-10-28 while making scalar aliases keep their type,
  5f87f445; on v5.5.0 too; a list assignment into one checks it since 73940d6e).
  An alias of a typed variable refuses what the variable refuses now
  (scalarAliasOf), but these containers are not made by an alias, and store
  as they come (a list literal's item, `($a, 2)[0] = 'l'`, refuses since d5186165,
  and a `where` alias since 5932b773):

  ```raku
  my Int $a = 1;
  my Int $p = 1; (k => $p).value = 'pv';    # stores; Rakudo refuses, naming $p
  sub r() is rw { $a }; r() = 'rr';   # stores; Rakudo refuses, naming $a
  class C { has Int $.x is rw = 1 }; my $o = C.new; my $ox := $o.x; $ox = 5;
  say $o.x;                           # 1: the bind took a copy; Rakudo 5
  ```

  The first two hand out the variable's cell (a Pair's value, an `is rw`
  routine's tail). A Pair cannot simply hold scalarAliasOf's Proxy: tried
  2026-11-09, `:$value` as a named argument then handed the Proxy itself
  to the parameter it bound (`self.bless(:$value)` stored it in the
  attribute: S32-trig/cos.t's NotComplex, the YAMLish suite), so a Pair's
  value would need its type kept apart from its cell. The routine's tail
  reaches the store as a slot pointer with no name (lvalueThroughRw). A
  `where` variable has one more of its own: `$x = Nil` resets it to Any,
  where Rakudo resets it to the anonymous subset `(<anon>)` (found
  5932b773; its type is asked before its clause since edc36182).
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
- [ ] **A typed element's refusal through `.map` or `.push` names "the
  container"** (found 2026-10-28 while fixing issue #184, 17e33a45; the
  three aliases that took anything refuse since ba6f7edf and 4c47943e, and
  a push of a type object into a `:D` array since 3341d028):

  ```raku
  my Int @a = 1, 2;
  @a.map({ $_ = 'x' }).eager;   # "… for an element of the container; …", Rakudo "… of @a; …"
  @a.push('x');                 # the same
  ```

  `.map`, `.grep`, `.first`, their hyper forms, and `.push`, `.append`,
  `.unshift` and `.prepend` see the array's value, not the expression that
  named it, so elemAliasGuard and checkElemType are given no name
  (MethodCallTail.cpp passes ""). The call's invocant expression has it,
  as containerNameOf finds it for an assignment; a lookup on every method
  call would cost the calls that never refuse anything. And an
  `Array[Str:D].new`, which no variable declared, names nothing on Rakudo:
  `my @a := Array[Str].new; @a[0] = 5` says "Type check failed in
  assignment to ; expected Str but got Int (5)" there, "for an element of
  @a" here.
- [ ] **An untyped array's element alias answers `.VAR` as a value**
  (found 2026-10-28 while fixing issue #184, 17e33a45; on v5.5.0 too; a
  Nil through it resets the element since 649cb2b1):

  ```raku
  my @p = 1, 2; my $e := @p[0];
  say $e.VAR.^name;                 # Int, Rakudo Scalar
  say $e.VAR.name;                  # No such method 'name', Rakudo @p (and `.VAR.of` (Mu))
  ```

  A typed element's alias carries a guard (kSlotGuard) that builds `.VAR`'s
  Scalar; an untyped one carries none, to keep the slot proxy at four
  entries (BinaryHeap builds one per sift step). `.VAR` needs the
  container's name, which only the bind site has.
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
- [ ] **What ThreadSanitizer still reports under Cro's tests** (found
  2026-10-30; a92d9cfd and 0d089427 fixed the rest). A build configured
  with `-DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS=-fsanitize=thread
  -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread` and run on Cro::RPC::JSON's
  t/060-websocket.rakutest and Cro::HTTP's t/http-session-inmemory.rakutest
  still reports: callNative's nativeSymCache and nativeCifCache, each
  published once (the second's atypes vector is read while another thread
  builds its own); the fatalPragma an EVAL sets on its caller's scope while
  another thread reads it in fatalHere; and setupRwLinks' anyRwLinks_ flag.
  Four more places save and restore `$/` through `find`, which walks past
  the routine into scopes other threads share (two in regexMatch's
  code-block arms in InterpreterCore.cpp, one each in MethodCallPart2.cpp
  and MethodCallPart3.cpp), InterpreterModules.cpp's loop reads one that
  way, and a `where { … ~~ /…/ }` block, unlike `where /…/`, still sets the
  declaring scope's `$/` from any thread. Each should go through
  matchVarSlot or keep its match. Seen since a11aa7ab, and not from it: a
  class declared at run time once workers exist (060's `my class
  WS-Actor-Child`) is inserted into classes_ by execDeclStmt while a worker
  reads it (userShadowsCoreRole), the kind of insert after the freeze
  0d089427 stopped for exception types; and `keep` inserts the promise
  hash's "vowed" key outside ps->m while another thread reads that hash
  (the react arm's `count("kind")`).
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
  second call since 3e97932e). Looked at again 2026-11-08: the read of `$kl`
  goes through listOfContainersRead (src/InterpreterCore.cpp), which deconts
  on every `$` read, and Rakudo writes through the containers by more roads
  than the parameter: `my $l2 = $kl; $l2[0] = 7` (7), `sub v(@l) { @l[0] = 3 }`
  (3) and `sub x(\l) { l[0] = 4 }` (4) all die here the same way.

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

- [ ] **Smaller differences met on the way** (2026-10-30):
  - `Kid.^find_method('foo').candidates` lists a role's multi candidate
    twice when Kid does the role and declares candidates of its own, and
    leaves out a parent class's (Rakudo: each once, the parent's first);
    a role's candidate shows no invocant in `.signature.raku`; and a
    role a role does lists its candidate twice too, `role R4 does S4`
    giving (Int, Str, Int) where Rakudo gives (Str, Int), each with its
    `E4 $::` (mergeParentRoleMultis, src/InterpreterModules.cpp; found
    again 2026-11-09); and a multi method's refusal, which lists the
    candidates since 07dd7b4a, lists only the role's when the class has its
    own beside it (Rakudo lists both, the class's first);
  - a Proxy bound to a variable answers `.VAR` as its value:
    `my $p := Proxy.new(…); $p.VAR.^name` is Int, Rakudo Proxy;
  - Cro::HTTP's t/http2-request-parser.rakutest fails now and then (3 runs
    in 20 at 12a8fee3, 2 in 20 after it): the "Header1 + Continuation1 +
    Header2 + Data1" case passes as soon as its second request is checked,
    while the first request's `start` block is still awaiting its body, and
    that block's "ok - check 4" lands in the next subtest's plan. A race in
    the test; Rakudo's scheduling hides it;
  - Cro::RPC::JSON's t/060-websocket.rakutest fails now and then under
    six-way CPU load (2 runs in 20 at c067446c, 1 in 20 at its parent; none
    in 10 idle), and failed once in a fresh `rakupp install` (then passed
    six times running): the client reads the close code, or the actor's
    reaction to the close, before the server's `whenever $close` body has
    run. Cro's handler emits its Close reply before it keeps that promise,
    so Rakudo races too. Timed with notes under that load, the server sets
    the code a median 0.2 ms ahead of the client's check here (0.15 ms
    before c067446c queued socket writes as Rakudo does) and 1 to 6 ms
    there. The steps are the same; Rakudo's lead is its client's `await
    Promise.anyof(Promise.in(…), $!closer)`, two thread-pool hops of 0.5 to
    2 ms where rakupp's wait wakes in 0.05 ms. a11aa7ab's pool for promise
    continuations speeds both ends alike and leaves the margin as it was
    (medians 0.23 and 0.27 ms against 0.24 and 0.21). Once under
    ThreadSanitizer, "Async Notifications With Object" failed too: the
    test's own JRPC-WS-Actor ticker walks `%!subscriptions.keys` while an
    `rpc.off` on another thread deletes them, and its `++` brings ns2 and
    ns3 back at 1;
  - IO::Socket::Async::SSL's t/encoding.rakutest failed once in a fresh
    `rakupp install` (it read "¸ВО" for "ПИВО": the reply's first bytes were
    lost) and has passed 80 runs since, idle and under load;
  - a program that `use`s a module other than Test is not checked for a
    gobbled block, since the module may export the name; Rakudo knows its
    exports and refuses `use JSON::Fast; if Nope { }`. And `given 1 { when
    Later {} }; class Later {}` says "illegal post-declaration", Rakudo the
    gobbling message;
  - two routines of one name in an EVAL are X::Redeclaration here, an
    X::Comp::Group holding it twice on Rakudo;
  - Cro::RPC::JSON's test-only dependencies JSON::Unmarshal
    (t/080-trait.rakutest), URI (t/query.rakutest) and Test::META
    (t/010-use.t) still fail their own suites. Cro::RPC::JSON's tests do
    not need them, so `rakupp install` skips them.
- [ ] **A cell bound inside a method and returned `is raw` is not the
  attribute's any more** (found 2026-10-31 checking the return-type fix,
  5ebfc05f; the build before it answered the same):

  ```raku
  class C {
      has $!n;
      method bind is raw { my $t = 5; $!n := $t; $t }
      method n { $!n }
  }
  my $c = C.new;
  my $cell := $c.bind;
  $cell = 8;
  say $c.n;   # 5, Rakudo 8
  ```

  `$!n := $t` makes the attribute a Proxy over `$t`'s cell, but the `is raw`
  return hands back a container that is not that cell. Done when the
  returned container and the attribute share it.

## Processes and diagnostics

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

- [ ] **`--exe`: a block's `my $x` is the program's `$x` when the program
  declares one too** (found 2026-11-10 writing t/race/match-var-threads.raku;
  interpreted it is right):

  ```raku
  my &f = { my $n = 5; $n };
  my $n = 1;
  f();
  say $n;      # 1; a compiled binary says 5
  ```

  Codegen declares the program's `$n` as the static `v_sn` and the block's
  `my $n` assigns that static rather than a local of the closure, wherever
  the two declarations stand. A sub's `my` is its own. A `start` block's
  counter named like the main thread's makes the two threads share one
  Value, so t/regression/threads-own-match-var-native.raku names them apart.
  Done when a block's `my` is a C++ local of its closure (or its own static
  when it is a `state`), as a sub's is.

- [ ] **A worker's failed match still walks into the program's scope for
  `$0`, `$1`…** (found 2026-11-10 with the `$/` fix, workerOwnScope): after
  a match, regexMatch clears each `$N` the match does not have by
  `tctx().cur->find`, which walks past the worker's own frame into the
  spawner's scope, read while the main thread may insert its first `$0`
  there. A worker's own `$/` and the `$N` it defines are its own now; the
  lookup is the one shared read left, the same in interpreted code. Done
  when that walk stops at the routine frame without leaving an outer `$0`
  visible after a failed match.

- [ ] **`--exe` bundles the interpreter for a sub-signature in a pointy
  block or a sub** (found 2026-11-09 fixing #235; until 9102ff01 clang refused
  the program, "use of undeclared identifier 'v_sa'"):

  ```raku
  say ((1, <a b>), (2, <c d>)).map(-> ($k, @n) { "$k:@n[]" });  # bundled
  sub f(($a, $b)) { $a + $b }; say f((1, 2));                     # bundled
  for (1, 2), (3, 4) -> ($a, $b) { say $a + $b }                 # native
  ```

  Codegen's bindParams binds such a parameter as an anonymous value and
  refuses it, as it refuses a `where`, so the program is bundled with the
  interpreter. REBASELINE-PLAN.md has the `sub k([$b, @c])` form as a
  lead. Done when both forms compile natively, unpacking as the
  interpreter's binder does and refusing what it refuses.

- [ ] **`--target=js`: a block's itemized result loses its container, a Set
  of a Set has its elements, and a sub-signature takes any number of
  positionals** (found 2026-11-09 giving the JavaScript runtime its
  `flatmap`, 2867e40e; a Set iterates as its Pairs, and `.flat` takes
  `:hammer`, since 8f55e40a):

  ```raku
  say (1, 2).map({ $($_, $_) }).raku;   # ((1, 1), (2, 2)).Seq, Rakudo ($(1, 1), $(2, 2)).Seq
  my @a = (1, 2), (3, 4); say @a.map({ $_ }).flat.raku;   # (1, 2, 3, 4).Seq, Rakudo ($(1, 2), $(3, 4)).Seq
  say set(set(1, 3)).elems;             # 2, Rakudo 1: the Set is one element
  sub f(($a, $b)) { }; f((1, 2, 3));    # lives, Rakudo "Too many positionals passed to 'f'; …"
  ```

  The JavaScript backend keeps no Scalar containers: the emitter itemizes
  where it knows a value is in one, and a block's `return` does not ask,
  so mapList never sees the item. mkSetty copies a Setty argument's
  elements, and a Set as an element would need a value key (whichKey
  keys an object by identity). The emitted sub-signature unpacks without
  counting. `flatmap` inherits the first two:
  t/regression/flatmap-is-map-then-flat.raku disagrees under
  t/js/run.raku on them, and t/regression/sub-signature-arity-message.raku
  on the last. Done when the four lines are Rakudo 2026.09's.

- [ ] **Python: `dir()` of a package that comes back as an Object lists
  Python's attributes** (found 2026-11-03 with 992c75cf; the same for any
  nested package before it):

  ```python
  raku.use("Outer::Inner")
  dir(raku.main.Outer)       # ['__bool__', '__call__', …, 'close'], not ['Inner']
  ```

  A Module has `__dir__` (rk-py-module-names in
  bindings/python/rakulang/object_shim.raku); an Object has none, and a
  package reached through an attribute is an Object. Done when `dir()` of a
  type object or package lists its stash's names as a Module's does, with a
  test in bindings/python/tests.

- [ ] **A binary computes at start a constant in a module, a routine or a
  class body** (found 2026-10-30 fixing issue #190, 946d29e4). The build
  embeds what the MAIN program's compile-time code came to — its top-level
  constants, its BEGIN values, its top-level BEGIN blocks' variables — and
  nothing deeper:

  ```raku
  # lib/M.rakumod: unit module M; constant DATA is export = 'data.txt'.IO.slurp;
  use M; say DATA;                       # the binary reads data.txt at start
  class C { constant X = 'data.txt'.IO.slurp }   # so does this one
  sub f { constant Y = 'data.txt'.IO.slurp; Y }  # and this, at each call
  ```

  Rakudo's precompiled module keeps all three. computeCompileTimeValues
  (src/CompileTime.cpp) records only what the pass's mainline computes: a
  module's constants need each embedded module's tree (BundledModule)
  numbered and substituted the same way, at the load the pass already does;
  a class body's need ctMainlineStmt to walk the body's constants; a
  routine's need the static environment runStaticPhasers builds for a nested
  BEGIN. A BEGIN statement inside a routine or block still runs at start
  too, and keeps the top-level ones running when it touches the program's
  variables.

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

- [ ] **`--exe`: a natively compiled routine's `&` parameter takes
  anything** (found 2026-10-27 while fixing issue #207, 3ddcd58b; an `@`
  or `%` one refuses what its sigil refuses since ae3321f2):

  ```raku
  sub f(&c) { &c.^name }; my $x = 1; say (try f($x)) // $!.message   # --exe: Int, interpreted and Rakudo the binding failure
  say f({ 1 });                                                       # --exe: Sub, interpreted and Rakudo Block
  sub g(:&c) { &c.^name }; say g();                                   # --exe: Sub, interpreted and Rakudo Callable
  ```

  rtSigilArg (src/InterpreterBinding.cpp) asks an `@` or `%` parameter
  what the interpreter's binder asks; an `&` one would ask notCallableArg
  and refuseNotCallable the same way, on each of bindParams's paths in
  src/Codegen.cpp (positional, named, default, optional, and -O's
  direct-Value routines), an unpassed one holding the Callable type
  object. That a compiled block's `.^name` is Sub is the compiled Code's
  own, not the binding's.

- [ ] **`--target=js`: a routine's `@` or `%` parameter checks no sigil**
  (found 2026-11-09 with 63b92af7, whose test disagrees under t/js/run.raku; the
  same before it):

  ```raku
  sub u(@a) { @a.^name }; my $x = 1; say (try u($x)) // $!.message   # JS Int, interpreted and Rakudo the binding failure
  sub h(%h) { %h.^name }; say (try h($x)) // $!.message               # JS Int, the same
  ```

  The emitted binding (src/codegen/Js.cpp) takes what came. The runtime
  would ask what the interpreter's notPositionalArg and notAssociativeArg
  ask (src/InterpreterBinding.cpp) and throw their refusals' words, as
  rtSigilArg does for `--exe` since ae3321f2. Done when
  t/regression/copy-array-param-refuses-scalar.raku agrees under
  t/js/run.raku.

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
  it a named one of 10,100 so that a fix could land without the split (raised
  to 10,200 when the typed-store fix took it to 10,143, to 10,400 at 10,317,
  and to 10,500 when compiled native multi dispatch took it to 10,467). The
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

- [ ] **`--exe`: a multi dispatch no candidate takes says so in its own
  words** (found 2026-11-08 checking the X::Multi::NoMatch profile):

  ```raku
  class A { multi method m(Int) { } }; A.new.m("x");
  # interpreted and Rakudo: Cannot resolve caller m(A:D: Str:D); none of these signatures matches…
  # --exe: No matching multi-method candidate for m
  ```

## Performance

- [ ] **Untyped Int arrays as words: what the interpreter's packing left**
  (2026-11-10, branch `claude/selfpack-ints`;
  [SELFPACK-EXPERIMENT.md](../experiments/SELFPACK-EXPERIMENT.md), *Ints*). An
  untyped Array of machine Ints is 8-byte words in the interpreter, and a
  sieve to 2M went 2,396M → 1,395M instructions and 198 → 107 MB. Left, in
  the order worth taking:

  - **Compiled code packs Nums only** (`rtSelfPack` passes `ints = false`).
    Int words need what Num words have in `src/Codegen.cpp`: the statement
    store (`rtSelfPackedStore` takes them already), reads and stores in the
    `-O` loop lanes (`rtNumWords`), and `++` and `op=`. With Ints packed and
    none of that, an `--exe -O` `@h[$x % 1000]++` loop retired 45% more
    instructions and a store loop 21% more. `src/Codegen.cpp` is at its line
    ceiling.
  - **An array that packs and is then asked for its elements costs more than
    it did**: 2.5% more instructions and 5–8% more memory on a million Ints
    through `.map`, `.sort` and `.grep`, and on `tools/bench/sortnums.raku`.
    `.sort`, `.map`, `.grep`, `.join` and a slice could read the words;
    `.sort` of machine words would be faster than of Values. An unpacked
    array also keeps its words, 8 bytes an element beside the 32.
  - **`0 xx N`, a `.map` result and other built lists** are built as Values
    and packed after, so the peak holds both: `0 xx 1_000_000` stored by index
    peaks at 57 MB where the words alone are 8.
  - **Arrays that start empty never pack**, Ints or Nums: only an assignment
    packs. `my @a; @a.push($_) for …` is the commonest way an array is built.
  - **A loop kernel that pushes unpacks the array on entry.** A push may move
    the words under a store's undo log and a read.
  - **Gates owed:** perf-guard on a quiet machine, the module battery, the
    ASan and TSan builds, and Linux. Roast and `t/run.raku` are in
    SELFPACK-EXPERIMENT.md.

- [ ] **A user method call is still ~3.5 times Rakudo's** (found 2026-11-01
  fixing issue #217, 608774f1 and 5321f41c; the encoders that issue timed
  run faster than Rakudo's now, but not because a call got cheap):

  ```raku
  class C { method !p(Str:D $s --> Str) { $s }; method m(Str $x --> Str) { self!p($x) } }
  my $c = C.new; my $t = now; $c.m('x') for ^1_000_000; say now - $t;   # 0.55 s, Rakudo 0.08
  ```

  What `sample` puts on top, none of it one line:

  - a method call is ~240 ns against a sub call's ~130 (Rakudo ~70):
    invokeMethod, bindRoutineParams, setupRwLinks (run for every call,
    since evalMethodCallExpr hands `rwInvocantExpr` to all of them), the
    `self` definition and the frame guards. callPlainSub is the sub
    path's lean lane; a method has nothing like it;
  - a fresh Buf costs a StrBody and two allocations for its identity
    (`identify`: a ValueExt and a `make_shared<char>`), and freeing them
    again in the next loop iteration's scope reset: `my $r = Buf.new` ~310
    ns in a loop;
  - a typed `is copy` parameter allocates its frame's EnvExtras for
    varDefault on every call;
  - Mathematica::Serializer's varint loop (`repeat { my $byte = $v +&
    0x7f; $v = $v +> 7; $byte +|= 0x80 if $v; $r.push($byte) } while $v`,
    in a `(Int:D $v is copy --> Blob)` private method) is ~1.7 µs a call
    against Rakudo's ~0.6: evalAssignInner's general path, statement by
    statement.

- [ ] **A promise-ordering test fails under load** (found 2026-11-02 in the
  suite runs for 5321f41c; dcdf14ba fails it too):
  t/regression/supply-tapper-death-is-not-a-quit.raku ("a whenever over a
  promise": `second` before `first`). It passes alone; run beside a build or
  another suite, dcdf14ba failed it 4 runs in 48. It keeps two promises in
  turn and expects their `whenever`s to run in that order, which a
  continuation on a pooled worker (a11aa7ab) does not promise. Done when
  either the test stops assuming the order or the delivery keeps it, and 48
  loaded runs pass. (The other test this item named,
  promise-whenever-fires-later-and-in-turn.raku, stopped failing with
  116a638e and 3c68ff57.)


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

- [ ] **Issue #179 is still slower than Rakudo: a non-ASCII string's first
  position call costs its length** (found 2026-10-30 timing the closed
  issue after 308aecdf). Each `.index($n, $pos)` after the first is as fast
  as Rakudo's, but the first `.chars`, `.substr` or `.index` from a
  position on a string holding a non-ASCII character builds its grapheme
  table (cowGraphemeIndex, src/Builtins.cpp), about 1.5 ms a MB, where
  Rakudo, whose strings are stored by grapheme, has nothing to build. The
  issue's loop, 500 calls, ms per call at 1 / 4 / 16 MB:

  ```
  with é          rakupp 0.004 / 0.011 / 0.057   Rakudo 0.001 / 0.001 / 0.001
  ASCII           rakupp 0.003 / 0.001 / 0.002   Rakudo 0.003 / 0.001 / 0.001
  ```

  The difference is all in the first call, 1.7 / 5.2 / 27.9 ms for the é
  string (Rakudo 0.004–0.006); each call after it is 0.0007–0.0008 ms on
  both. One call on a fresh 16 MB string with an é, ms:

  ```
                       rakupp   Rakudo
  .chars                 15.9      0.5
  .substr(8e6, 5)        30.3      0.3
  .index('gh', 8e6)      24.1      0.4
  ```

  So a program that makes many large non-ASCII strings and touches each
  only a few times (a file decoded window by window, each window searched
  once) pays the table for every one. Ways in: build the table only as far
  as the position asked for and extend it on demand; skip ASCII runs a word
  at a time in graphemeWalkUtf8 (src/Unicode.cpp); hand a table on to what
  `x`, `~` and `.substr` make from strings that have one. Done when the
  first `.index($n, $pos)` on 16 MB with an é costs under 2 ms.

- [ ] **`.slurp(:bin)` peaks at five times the file's size** (found
  2026-10-30 measuring issue #176's fix; Rakudo needs two). Peak memory
  footprint (`/usr/bin/time -l`) reading a random 256 MB file, with the
  empty program's 2 MB (Rakudo's 73 MB) beside it:

  ```
                                            rakupp     Rakudo 2026.09
  'f256.bin'.IO.slurp(:bin)                1,283 MB      589 MB
  $fh.slurp(:bin)  (opened :bin)           1,923 MB    1,329 MB
  $fh.read('f256.bin'.IO.s)                  514 MB      336 MB
  ```

  64 MB slurps to 322 MB (Rakudo 205), so it grows with the file. The path
  slurp (src/MethodCallPart3.cpp) holds three copies of the bytes at once:
  an ostringstream's (`ss << in.rdbuf()`, which regrows as it reads, the
  old buffer and the new both live), `ss.str()`'s, and the one
  `binBuf(text)` makes, since it takes its bytes by value and is handed
  `text` rather than moved (the Value it builds moves them, as CowStr's
  take does). Size the string from the file's `stat`, read it in once and
  move it into binBuf; the handle's `.slurp(:bin)` and a `.read` of the
  whole file want the same look. Done when the first line's 256 MB peaks
  under 300 MB, one copy of the file and the interpreter.

## Gates

- [ ] **A `#?requires` regression case is skipped on every machine without
  its module, the suite's own included** (found 2026-11-09 reviewing pull
  request #234): t/regression/method-also-role-composition-2026-09-02.raku
  died on every build from 0b29ef00 (2026-10-30) to 6004f84a, and t/run.raku
  said "skip" each time, because Method::Also is installed nowhere it runs.
  6004f84a added a case with no module for that shape; the rest of the
  `#?requires` cases (`grep -l '^#?requires' t/regression/*.raku`) have no
  such twin. Either install those modules where the suite runs (the
  opt-in `t/deps` route), or have t/run.raku name, at the end, every case
  it skipped for a missing module, so a run says how much it did not test.

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
- [ ] **Roast's S17-promise/nonblocking-await.t fails now and then under
  load** (found 2026-10-30 gating 67b676cf): in one full run, with every
  file reparsing after the AST cache's version change, it passed 6 of 28,
  and once more in fourteen runs of it alone:

  ```bash
  for i in $(seq 20); do ROAST=~/roast build/rakupp tools/run-roast.raku S17-promise/nonblocking-await.t | grep nonblocking; done
  ```

  The build of 5e740224 passed eleven of eleven, too few to say it is new.
  The file has no class, enum or BUILD, only awaits on timers and
  promises (twelve sleeps, Promise.in and timeouts); start with which of
  its 28 tests the 22 failures are, and whether a loaded machine makes
  them miss a deadline or the scheduler lose an await. Done when it
  passes 50 of 50 runs beside a full Roast run.

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
