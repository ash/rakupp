# Follow-ups

*Collected 2026-10-20; added to 2026-11-08.*

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

- [ ] **Two imports of one name in a block keep the first** (found
  2026-11-08 fixing #230, eb574770):

  ```raku
  { use Other; use Other2; say run-word("a") }   # other:a, Rakudo other2:a
  ```

  The mainline keeps the last, as Rakudo does, because it publishes to
  global_. In a block or a module, the lexical import roads in loadModuleImpl
  skip a name the scope holds (`if (!saved->local(k))`, and the repeat-use
  and `import` loops), even one that only an earlier import put there.

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
- [ ] **`q;…;`, `q,…,` and `q.….` are not quotes** (found 2026-10-30, 594d4aac,
  which made the rest of a bare `m`'s punctuation delimiters the q
  family's):

  ```raku
  say q;abc;   # Undefined routine 'q', Rakudo abc
  say q,abc,   # the same
  say q.abc.   # a parse error, Rakudo abc
  ```

  Left out on purpose in bareRxDelim (src/Lexer.cpp): those three are what
  follows a sigilless term named `q`, and the lexer knows only the terms it
  has seen declared (`my \q`, `\q`, `|q`, `constant q`). dist-suite-quartet's
  `-> \s, |q { $cap = q; callsame };` read as a quote to the `;` before
  `|q` counted as one. Done when a declared term wins over the quote however
  it was declared, and these three then follow Rakudo.
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
- [ ] **`.AST` still drops `is export`, the attribute traits it has no flag
  for, and a `use`'s import list** (found 2026-10-31 making the view keep the
  rest, acf27e4b; on 23e143f2 too). Each renders valid Raku that means
  something else, so `.AST.EVAL` runs it silently:

  ```raku
  print Q[class G is export { has $.a }].AST.DEPARSE;     # class G {
  print Q[sub f is export { 1 }].AST.DEPARSE;             # sub f {
  print Q[class H { has $.x is default(3) }].AST.DEPARSE; # has $.x
  print Q[class K { has @.l handles <elems> }].AST.DEPARSE; # has @.l
  print Q[use Test <foo>; 1].AST.DEPARSE;                 # use Test;
  print Q[use Test :DEFAULT; 1].AST.DEPARSE;              # use Test;
  ```

  Rakudo 2026.09 writes each as given. `is export` is a flag plus tags on
  every declaration kind (ClassDecl, SubDecl, EnumDecl, SubsetDecl), and
  the view has a Trait::Is builder now (`traitIs` in src/RakuAstView.cpp);
  `is default`, `handles`, `is DEPRECATED` and user traits sit on AttrDecl
  and ClassDecl in their own fields. The import list cannot be rendered from
  what the parse keeps: `<foo>` and `:foo` both land in
  `UseStmt::importArgs` as the word `foo`, and `:!foo` and `:foo<v>` not at
  all, so it needs the spelling recorded (an Ast.h field and a
  kAstSerialVersion bump). Done when each line matches Rakudo 2026.09, and
  t/regression/rakuast-view-declarations.raku carries them.
- [ ] **An anonymous `rule` interpolated as `<$r>` matches nothing, and its
  `.raku` is a `rx//`** (found 2026-11-08 making a stored `rule` keep its
  sigspace; the same before it):

  ```raku
  my $r = rule { a b };
  say "x a b" ~~ /<$r>/;   # Nil, Rakudo ｢a b｣
  say $r.raku;             # rx/ a b /, Rakudo rule { a b }
  ```

  The value carries its kind (hashKind "rule" or "token") and the scope it
  closed over; the `<$var>` assertion and the Regex `.raku` read neither.

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
  the name.

## Operators and types

- [ ] **A FALLBACK gets the list methods Any has, and an object keeps
  Exception's** (found 2026-11-07 fixing #225, e7bbb97c):

  ```raku
  class W { method FALLBACK($n, |) { "fb:$n" } }
  say W.new.first;   # fb:first, Rakudo W.new (Any.first of one item)
  say W.new.elems;   # fb:elems, Rakudo 1
  say (try W.new.throw) // $!.^name;   # W, Rakudo fb:throw
  ```

  `.first`, `.head`, `.keys`, `.elems`, `.grep`, `.map`, `.sort`, `.unique`,
  `.Array` and the rest of Any's list methods reach methodCallUnresolved on
  a user object, where Rakudo answers them from Any on the one-item list;
  `.throw`, `.rethrow`, `.resume` and `.backtrace` answer on an object that
  is no Exception, where Rakudo hands them to FALLBACK.

- [ ] **A user `multi` on a built-in operator hides the built-in candidates,
  and `&postfix:<++>` is Any** (found 2026-10-19 while fixing undeclared
  operator names, 145cddbf; the rest of that item fixed 2026-11-08):

  ```raku
  multi prefix:<->(Str $x) { "str" }; say -"a", -3;   # dies, Rakudo str-3
  say prefix:<->("a");                                 # the same, called by name
  say &postfix:<++>;                                   # (Any), Rakudo &postfix:<++>
  ```

  A declared operator of a built-in's name is called as the routine alone,
  without the built-in candidates beside it. `&postfix:<++>` needs a
  Callable that steps the container it is handed (the call form now
  rewrites to the operator, ParserWordPairs.cpp builtinOpCall).

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

- [ ] **A declarator block's `.raku` is its text, a trailing `#=` after a
  routine's `}` is lost, and `for $=pod` does not iterate** (found 2026-11-08
  making a Pod block's `.raku` its constructor):

  ```raku
  #| a
  sub f {
  }
  #= b
  say &f.WHY.raku;   # "a", Rakudo Pod::Block::Declarator.new(WHEREFORE => sub f { … }, leading => ["a"], trailing => [["b"],], config => {}, contents => [])
  say ~&f.WHY;       # a, Rakudo "a\nb"
  for $=pod { }      # one pass with the whole list, Rakudo one per block
  ```

  A declarator block keeps only its text and `declLine`: no WHEREFORE, no
  leading/trailing split. `$=pod` is held itemized.

- [ ] **A `Buf` parameter takes a Blob, and a `Hash` one or variable a Map**
  (found 2026-11-08 making `--> Buf` and `--> Hash` refuse them; `--> Rat`
  and `my Rat $r` refuse a FatRat since):

  ```raku
  sub a(Buf $x) { 1 }; say a(Blob.new(1));      # 1, Rakudo: X::TypeCheck::Binding::Parameter
  sub b(Hash $x) { 1 }; say b(Map.new);         # 1, the same
  my Hash $h = Map.new; say $h.^name;           # Map, Rakudo: X::TypeCheck::Assignment
  ```

  typeMatchesArg (src/InterpreterCore.cpp) lets any buffer kind match any
  buffer type name, and a Map match `Hash` (hashKindIsMap); `~~` answers
  both right. Before narrowing it, check that no built-in answers a Blob or
  a Map where Rakudo answers a Buf or a Hash (`.read`, `.subbuf`, `~` of two
  Bufs, `%h.Map`), or a `Buf $b` parameter that took one starts refusing it.

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

- [ ] **Two roles' conflicting multi says less than Rakudo** (found
  2026-11-08 checking the role candidate key):

  ```raku
  role A2 { multi method m($a, :$x!) { "A" } }; role B2 { multi method m($a, :$x!) { "B" } }
  class E does A2 does B2 { }
  # Method 'm' must be resolved by class E because it exists in multiple roles (A2, B2)
  # Rakudo: Multi method 'm' with signature :(E $:: $a, :$x!, *%_)' must be resolved by class E
  #         because it exists in multiple roles (B2, A2)   — at compile time
  ```

## Control flow and containers

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

- [ ] **A FIRST or INIT written last does not give the block its value**
  (found 2026-11-08 making every other phaser written last Nil):

  ```raku
  say (1, 2, 3).map({ $_; FIRST { 7 } });   # (1 2 3), Rakudo (7 7 7)
  sub g { 1; INIT { 5 } }; say g();         # 1, Rakudo 5
  ```

  Both are r-value phasers: Rakudo keeps the value of the one run and
  answers it on every pass. ENTER written last already answers its value
  (runEnterPhasers' `\x01enter-tail`); FIRST runs once per closure clone
  and a hoisted INIT before the mainline, so each needs a slot that keeps
  its value for the body's tail to read (trailingCatchBlock leaves both out
  of the Nil rule).
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
  say @d.WHAT.^name;                # Array[Int], Rakudo Array[Int:D] (and Hash[Int] for `my Int:D %h`)
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
    twice when Kid and a parent both declare candidates (Rakudo: once);
  - `.signature.raku` prints `Hash(List:D(Pair:D)):D :$x!` for
    `Hash:D(List:D(Pair:D)) :$x!`, and `List(Pair:D):D` for
    `List:D(Pair:D)`: the smiley goes after the coercion;
  - `R.^candidates.map(*.HOW.^name)` dies "Cannot map a List using a Str":
    `.^name` after `*.HOW` is not curried into the WhateverCode;
  - closing the tap of a supply block calls the tapper's `done` callback
    (`supply { whenever Supply.interval(10) { } }.tap(done => { note
    "done" }).close` says "done"; Rakudo says nothing);
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
  - `.^roles` of a class that does a value-parameterized role names it as
    written, `(R2[1])`, where Rakudo names it by type, `(R2[Int])`; and
    `R2[1].^roles` is `((R2[1]))`, Rakudo `()`;
  - an unknown parent's suggestions leave out Rakudo's native types and
    terms: `class C is Strr {}` suggests 'Str', Rakudo 'str' and 'Str';
    `class C is Iny {}` 'Any' and 'Int', Rakudo 'Int', 'Inf' and 'Any';
  - a class refused for an unknown parent leaves its name behind: after
    `try EVAL 'class C is nosuch {}'`, `EVAL 'class C {}'` is a
    redeclaration (Rakudo: none; a refused role's name stays in both);
  - a program that `use`s a module other than Test is not checked for a
    gobbled block, since the module may export the name; Rakudo knows its
    exports and refuses `use JSON::Fast; if Nope { }`. And `given 1 { when
    Later {} }; class Later {}` says "illegal post-declaration", Rakudo the
    gobbling message;
  - two routines of one name in an EVAL are X::Redeclaration here, an
    X::Comp::Group holding it twice on Rakudo;
  - the JS backend's `substr` start past the end hints "use *--7" for
    `"abc".substr(10)` (src/JsRuntimeSrc.cpp), Rakudo "use *-10";
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

- [ ] **An `exit` on a worker thread runs no END phaser; Rakudo runs them**
  (found 2026-11-08 making a cued die end the process):

  ```raku
  END note "END ran"; start { exit 3 }; sleep 0.5;   # status 3 in both, Rakudo also "END ran"
  ```

  B["exit"] (src/BuiltinsRegister.cpp) calls `_exit` off the main thread,
  and so does endOnUnhandled (src/Interpreter.cpp: an unhandled cued die, a
  sunk `start`'s, a tapper's on an interval or a Channel), where Rakudo's
  `exit(1)` runs the END phasers. The main thread is still running the
  program then.

## Backends

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

- [ ] **`--exe`: two Buf element steps** (found 2026-11-08 making
  `$b[1]++` step a Buf's element, which compiled code already did):

  ```raku
  my $b = Buf.new(2, 0); $b[*-1]--; say $b.list;   # interpreted (2 255), --exe: Index out of range. Is: -1
  my @a = Buf.new(5); @a[0][0]++; say @a[0].list;  # interpreted (6), --exe: [1]
  ```

  Codegen's nativeElemTarget takes an element step to nativeElemStep, which
  hands a Buf to bufElemStep, only for a variable base and a plain index; a
  `*-1` index or an Index base takes the general step through rtIndexRef,
  which has no place for a Buf's byte (the second makes the Buf an Array).

- [ ] **`--exe`: a multi dispatch no candidate takes says so in its own
  words** (found 2026-11-08 checking the X::Multi::NoMatch profile):

  ```raku
  class A { multi method m(Int) { } }; A.new.m("x");
  # interpreted and Rakudo: Cannot resolve caller m(A:D: Str:D); none of these signatures matches…
  # --exe: No matching multi-method candidate for m
  ```

## Performance

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

- [ ] **An undeclared variable is named where its spelling first appears,
  inside a literal too**: in names-v1's integration/advent2014-day16.t,
  `$a` is undeclared in `Q:s/foo $a\n/` (line 22, where Rakudo names it),
  and Raku++ names line 16, where `say Q/foo $a \n/` holds the spelling in a
  literal that interpolates nothing. Both refuse the file; only the line
  differs (found 2026-10-23). Likely DeclCheck's textual scan.
