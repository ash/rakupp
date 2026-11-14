# Plan: fixing what Rakudo's issue tracker found

*Written 2026-11-12 from
[findings/RAKUDO-ISSUES-2026-11.md](../findings/RAKUDO-ISSUES-2026-11.md), at
Raku++ 5.7.0-111-g56aec789 against Rakudo v2026.09.*

- **Notation.** `rakudo#N` is issue N of rakudo/rakudo; a bare `#N` is one of
  ours.
- **Scope.** The full list of findings, grouped by area, is
  [FIX-LIST.md](../findings/rakudo-issues-2026-11/FIX-LIST.md). This plan puts
  it in order.
- **The snippets.** They live in
  [raku-corpus-snippets](https://github.com/ash/raku-corpus-snippets), under
  `rakudo-issues/{open,closed}/<issue>/<k>`.

## How the order is chosen

1. **A crash or a hang comes before anything else.** It costs the user the
   whole program and its output.
2. **A wrong answer comes before a refusal.** A silent wrong value is the
   worst class; a parse error at least says something is wrong. Within the
   class, reach decides:
   - first, the fix that settles many cases at once (R2 settles 42);
   - then what ordinary programs hit: attributes, dispatch, operators, lists;
   - corners last.
3. **Valid code we refuse comes before invalid code we accept.** A refusal
   stops someone porting a program to Raku++. A laxness only lets a program
   fail later, on Rakudo, and tightening it can break code that runs here
   today.
4. **Usefulness counts, not officialness.** A Rakudo module or routine that
   people actually use ranks with the language features, whether or not Roast
   specifies it. `use Telemetry` sits beside `Routine.prec`, not at the
   bottom.
5. **Rakudo's insides and RakuAST's newer corners come last.** That covers:
   - `nqp::`, CompUnit and precompilation internals, `--ll-exception`;
   - the RakuAST API's extras (`.AST` of a grammar rule, RakuDoc trees,
     `RakuAST::Deparse::Highlight`);
   - error-message wording;
   - the open Rakudo bugs we share, which wait on each issue's own consensus.

## How every batch runs

- **Re-verify first.** 104 findings were re-run by hand (✓ in FIX-LIST.md).
  The rest are a reviewer's reading of recorded outputs, and a few of those
  turned out to be misreadings. Rerun the batch's paths in
  raku-corpus-snippets with `RAKUPP=… harness/diff.sh ./rakudo-issues/…`.
  Reduce each live finding to a one-liner, and drop the dead ones with a note
  in the batch's row below.
- **One regression file per fix** under `t/regression/`, written so that
  Rakudo 2026.09 passes it too, as for #254.
- **Gates.**
  - `build/rakupp tools/source-helpers/budget.raku` (1 s).
  - Roast (~6 min) first, then t/run.raku (~20 min).
  - t/exe/parity.raku after a codegen change.
  - For any new strictness, a clean `tools/battery-scan.raku` against the
    last release.
- **A CHANGELOG-NEXT.md entry** naming the rakudo issues.
- **The progress number.** After the batch, run `harness/diff.sh` over the
  whole snippet set. Record the MATCH count (baseline 1,264 of 1,777), and diff
  its `summary.tsv` against the previous run's. A PPWIN that turns into a
  MATCH is a regression to Rakudo's bug, not progress. When a batch touches a
  PPWIN's area, turn that PPWIN into a regression test.
- **Parser.cpp has no room.** It is at 18,459 lines against an 18,460 ceiling.
  A batch that touches the parser either starts with R7.0 or folds each change
  into existing lines.

## Order

R1–R6 are the wrong answers and crashes. R7–R8 are refusals and the
remaining wrong answers. R9 is missing features, R10 strictness, and R11
speed, which is handed to the speed track. R12 is last.

**R9's Telemetry item is independent of the rest** and can be done at any
time, beside any batch.

### R1 — crashes and hangs

*Done 2026-11-12 (041789fd, 147d0b95, 7fc5b00b, b4f6e472). rakudo#5868 is
deferred to PARALLEL-PLAN, by decision 4.*

- **rakudo#5868 (SIGBUS) — deferred to
  [PARALLEL-PLAN](PARALLEL-PLAN.md)'s lock on the type tables.** Raku++
  keeps one role per call, and with it its `[1 2 3]`.
  - **Symptom.** `(^100_000).race.map` calls a sub that returns
    `Foo.new but role :: { … }`. It crashes in parallel mode with or without
    the closed-over lexical, and is clean under the GIL.
  - **Cause, from ThreadSanitizer.** Every call declares a new anonymous
    role and a new mixin class, written into `lastDecl_` and `classes_`
    with no lock while other workers read them. This is TODO §2's
    `S17-promise/nonblocking-await.t` flake at a much higher rate.
  - **How Rakudo differs.** It makes one role per declaration and one mixin
    type for every call (`mk(1).WHAT === mk(2).WHAT`). Its methods see the
    last call's lexicals: `[3 3 3]` where Raku++ prints `[1 2 3]`.
  - **The two ways to fix it:**
    - adopt Rakudo's model and lock the one creation;
    - lock the type tables themselves, PARALLEL-PLAN's open item. This is
      the one chosen.
  - **Reproducer.** raku-corpus-snippets `rakudo-issues/open/5868/0.raku`
    crashes in parallel mode on nearly every run, where
    `nonblocking-await.t` crashes in about 1 run in 10.
- **rakudo#5537 — fixed (7fc5b00b).** A user `class Rat` was taken for the
  core Rat by name, and its `.new` boxed a `Rat.new` that was itself. Found
  beside it and moved to R3 (type identity by name).
- **rakudo#3775 — fixed (041789fd).**
  - A regex value that names itself is called when the match reaches it,
    not pasted into itself; `/ a <$r>? /` matches as in Rakudo.
  - Left recursion throws X::Recursion where it took SIGBUS, and where
    Rakudo hangs.
  - Refusing the bare `…` turned out to be one case of a wider gap, unquoted
    symbols in a regex, and moved to R10.
- **rakudo#5835 — fixed (147d0b95).** An index no machine could reach is
  refused with Rakudo's "Unable to allocate an array of N elements", at all
  fourteen places that grow an array to an index.
- **rakudo#4853, rakudo#3686 — fixed (b4f6e472).** Two causes:
  - a channel's `.closed` never ran its continuations, so a react never
    heard of it;
  - the channel reader held the react for its whole polling loop, so a
    `whenever $promise` body never got its turn.

  #3686's own program now reads every value, which is what the open issue
  asks of Rakudo.

Each fix has a regression test, and each hang case runs under a guard, so a
regression fails the test rather than wedging the suite.

### R2 — a phaser used as a value runs when it is reached

*Done 2026-11-13 (00c30ed1, b980a365).*

- **Value phasers — fixed (b980a365).** The parser wrapped a value phaser in
  a `do` that ran where it stood. Now each runs at its phase:
  - BEGIN, CHECK and INIT values come from the pass that runs the static
    phasers before the code around them, the top level included. A phaser
    that pass cannot model runs once, where it is first reached.
  - ENTER values are computed at each entry of their block.
  - `FIRST <expr>` is a term (rakudo#4351). Its value is computed at a
    loop's first iteration, and every iteration reads it.
  - At the top level of a program or an EVAL, FIRSTs and then ENTERs run
    on entry, each in source order, in either form. EVAL'd code had run
    even its statement-form ENTERs in place.
  - `--exe` compiled a value phaser in place. A program that uses one now
    bundles with the interpreter, its routines native where they can be,
    and the JIT leaves a loop that holds one to the interpreter.
  - TODO §2's INIT default that reads an earlier parameter
    (`$y = INIT { $x }`) runs once, at program start, and sees Any, as in
    Rakudo.

  Every snippet of the 23 issues now prints a positive time, except one
  that calls `nqp::time_n`.
- **Set and bag op-assign — fixed (00c30ed1), found by this batch.** Roast's
  S04-phasers/enter-leave.t builds a set with `∪=` beside
  `last if ENTER $set<>`. It passed by accident while ENTER ran in place.
  At the block's entry it exposed `my $s; $s ∪= 0` giving Set.new(Any, 0):
  an undefined target now starts from set() or bag(), as in Rakudo, and so
  does a reduce over nothing.
- **Left, found beside it.**
  - Rakudo's `INIT $c` and `ENTER { …; $i }` hand back the variable
    itself, so a later assignment shows through:
    `our $c = 1; my $v := INIT $c; $c = 5; say $v` prints 5 in Rakudo.
    Raku++ keeps the value from the phase, (Any). No issue asks for it.
  - Rakudo runs a top-level FIRST even before INIT (`INIT say "i"; FIRST
    say "f"` prints f first). Raku++ runs it after INIT, beside the ENTERs.
  - `--target=js` still runs a value phaser where it stands, as it ran
    `INIT <expr>` before R2. Refusing one would stop such a program
    compiling to JavaScript without `--fallback=wasm`.
  - `FIRST(…)` with parentheses is a call of the phaser in Raku++; Rakudo
    refuses it as an undeclared routine. That belongs to R10.

### R3 — objects: read-only attributes, typed containers, roles, the MOP

*Run with R4, by decision 3: 2026-11-13. The named items are done but the
coercion ones and four narrow ones below; the rest of FIX-LIST.md's oo PPBUG
section is not started.*

- **rakudo#5908, #6113, #4463 — fixed (4f507a0f).** `$.x` in a method is
  `$(self.x)`. Through a read-only accessor a plain `$.b = 666` refuses, and
  any other write (`$.n++`, `$.h<k> = 1`) reaches a copy, as in Rakudo.
  Stricter in one corner: `($.a) = 5` refuses too, where Rakudo's list
  assignment changes the copy.
- **rakudo#2068 — fixed (a64cd841).** A typed hash's Pairs carry its element
  type. Left: a typed Array's `.pairs` writes unchecked, `.value = Nil`
  does not reset to `is default`, and the message does not name `%h`.
- **rakudo#2663 — fixed (91033936).** A private call resolves in the package
  it is written in, in the interpreter (methodCall) and in native code (a
  class's `self!p` is emitted as `!Class::p`). A compiled `$o!Pkg::name`
  works now; it died.
- **rakudo#3976 — fixed (2987aab9).** A `multi submethod` is in
  `.^submethod_table`, and submethods are out of `.^method_table`.
- **rakudo#3292 — fixed (b66ccda3).** Assigning to a sigilless name bound to
  an object with STORE calls it. Left: `foo = 1, 2` is an item assignment
  here, and a sigilless parameter bound to a caller's `$o` calls STORE where
  Rakudo assigns the container (older than this batch).
- **rakudo#2174, the `does(…)` half of #1959 — dead.** Rakudo refuses
  `does(…)` as a routine too, and #2174's snippets agree.
- **Left, coercion types in a parameterization: rakudo#1285, #6031.** Raku++
  drops the coercion when it binds `R[Int()]`'s `::T` and when it records
  `Set[Int()]` (`.keyof` is Int, Rakudo's Int(Any)). The fix runs through
  the binder's captured types, the parser and every QuantHash write.
- **Left, narrow:**
  - rakudo#4543, a CALL-ME mixed into `&grep`;
  - rakudo#1855, `Rational[IntStr, IntStr]`'s numerator type;
  - rakudo#1959, `$set does "foo"` (X::AdHoc here, nothing in Rakudo);
  - a user class named like a core type (`class Rat {}; my Rat $r = 2.2`,
    `Rat.new ~~ Numeric`, `class Nil` getting the built-in `.new`).
- The rest of FIX-LIST.md's oo PPBUG section: 27 cases in all.

### R4 — dispatch: ambiguity, coercion, `is cached`, `.assuming`

*Run with R3: 2026-11-13. The named items are done; the rest of the
dispatch PPBUG section is not started.*

- **rakudo#1286 — fixed (401e19e8).** Ambiguity counts sigilless, `is raw`
  and `is copy` parameters as plain ones, as Rakudo does; `is rw` still
  decides. User-versus-core ambiguity waits on TODO's open decision, as
  planned.
- **rakudo#6860 — fixed (bdb022c4).** The cause was not the coercion: a user
  class's TYPE OBJECT never met the program's `multi infix:<op>`. A core
  type object still goes to the built-in (`"x" ~ Int` beside a user
  `(Str, Any)` candidate), because the merged candidate group then lost
  X::Numeric::Uninitialized.
- **rakudo#4665, #4666 — fixed (cee2cd42).** `is cached` caches, keyed on
  the Capture's gist as in Rakudo. Two answers are better than Rakudo's:
  through `samewith` (11 calls to Rakudo's 2,047) and with a Set argument
  (1 call to 6). Left: `is cached` on a method runs uncached, where Rakudo
  refuses it as NYI. Requiring `use experimental :cached` stays with R10.
- **rakudo#4553 — fixed (29f64dcc, 38cb1d1e).** `.assuming`'s residual
  `.default` is callable. Under `--exe`, a routine whose defaults the program
  reads with `.default` runs in the interpreter.
- The rest of the dispatch PPBUG section.

### R5 — operators, lists and sequences

*2026-11-13. The named items are done but rakudo#5138; the rest of the
lists, hashes and numeric sections is not started.*

- **rakudo#4757 — fixed (521d34ae).** R reverses the associativity as well
  as the operands: `2 R- 3 R- 4` groups from the right and gives −1, and
  `2 R** 3 R** 2` from the left. A comparison is not regrouped. The issue's
  snippet still differs: Rakudo refuses the file for a missing semicolon,
  which Raku++ accepts, and that is R10's.
- **rakudo#6128, #1616 — fixed (8c513f23, 585a8ee1).** R composes over Z
  and X: `RZ`, `[RZ]`, `&[RZ,]`, `RZcmp`, and with a symbolic inner
  operator, `RZ+`, which had given ((2 1)). Z and X compose over R too
  (`ZR-`), which was an undeclared name. Left: rakudo#1616's second snippet,
  a deep `RR…ZZ…RR…SS+` stack with S in it, is an undefined routine here.
- **rakudo#4128 — fixed (a0437c10, 703198f5, 1f205847).** `reduce` with a
  built-in chaining comparison chains, as `[op]` does, and the reduce metaop
  over a built-in's Callable folds it: `[[&infix:«>»]] 6, 5, 4` is False.
- **rakudo#2217 — fixed (cb91f67b).** Set membership reads a Range as the
  Set of what it iterates: `½ ∈ ^2` is False, and `"aa" ∈ "a".."c"` too.
  An Int range answers from its ends without iterating. Over an endless
  range `(elem)` dies X::Cannot::Lazy, as in Rakudo; it had answered True.
- **Found by R2 — fixed (2416bbe5).** A set relation reduces as a chain:
  `[⊆] ()` and `[∈] (1,)` are True. Rakudo refuses `[(!elem)]`, `[(!=)]`
  and `[(<>)]` at compile time; that is R10's.
- **rakudo#2000 — fixed (22520fae).** `&postcircumfix:<[ ]>` and
  `&postcircumfix:<{ }>` are routines. Called by name or through a
  variable they subscript, their named arguments are the adverbs, and a
  third operand is assigned. Left:
  - assigning to the call's result refuses
    (`&postcircumfix:<[ ]>(@a, 0) = 99`), as any `f(…) = …` through a
    `my &f` does;
  - `:BIND` is not read;
  - `.^name` is Sub, where Rakudo says `Sub+{is-nodal}`.
- **rakudo#1447 — fixed (133560ad).** `roundrobin` follows the
  single-argument rule.
- **rakudo#2485 — fixed (ad10ad81).** A Callable count to `.head`, `.tail`
  and `.skip` is called with 0 and added to the element count.
- **rakudo#1629 — fixed (3db5cae5).** `.combinations` over a non-Int Range
  takes the sizes Rakudo does.
- **rakudo#1399, #1398 — fixed (c109e8ec).** `EXPR for 1..*` runs as long
  as the range does; it stopped after 10,000 iterations.
- **rakudo#1692 — fixed (c47aa1ea).** `eager` runs an unconditioned
  `Seq.from-loop` to its `last`, and a body whose value is Empty adds
  nothing. Left as Rakudo's quirk: it stops such a loop at the first
  body whose value is Empty.
- **rakudo#5951 — fixed (cfbda86a, 57b07f65).** `[»op«]` over one operand
  is the inner operator's one-argument form, and the one-argument `~` joins
  a list. Over several, the answer is one item, as a hyper's answer takes
  its left operand's container: `my @r = [»+«] @a` holds the row. Left:
  `@a[0] »+« @a[1]` is `[4, 6]`, Rakudo's `$[4, 6]`, since an element read
  from an Array is not marked as an item.
- **rakudo#1355 — fixed (ccda30e9).** A lone Array bound through a `+`
  slurpy keeps its elements' containers: `(1, $(2, 3))`. Left:
  `+@l is raw` over a lone Seq binds a List, where Rakudo keeps the Seq.
- **rakudo#3621 — fixed (1c4da191).** `.=` into a `$` variable itemizes
  what it stores, as `=` does.
- **Found by `--exe` parity on rakudo#5951's test — fixed (eca56b5a,
  1ba950a5, 9bb82a6c).** In a compiled program:
  - calling `infix:<~>(…)` by name died;
  - a reduction flattened a lone Array operand all the way:
    `[+] [[1,2],[3,4]]` was 10, where it is 4;
  - a hyper operator zipped two flat lists: `%h »+« %g` was `(2,)`.
- **rakudo#3881 — fixed (7dc4809d).** A generator sequence stops at an
  excluded endpoint however it is read; printing `{ … } ...^ 21` never
  ended. A Range after a lone generator is a list endpoint, as in Rakudo.
- **rakudo#5138 — left.** `(1..*).Slip.is-lazy` is False. Marking the Slip
  lazy broke `(|(1..*).Slip)[^4]`, which gave Nils: slipping a lazy list
  into a list literal loses its laziness, an older gap. The change was
  reverted.
- **Left, found beside.**
  - From a statement-modifier `with @a` or `given @a`, `.=reverse` does
    not write back to the array; the block forms do.
  - A generator sequence with a literal endpoint, `{ $++ } ...^ 5`, says
    `.is-lazy` True, where Rakudo says False.
- The rest of the lists, hashes and numeric sections.

### R6 — regexes and grammars

*2026-11-13. The named items are done; the rest of the regex and grammar
sections is not started.*

- **rakudo#1551 — fixed (6b709af5).** `$x ~~ TR///` answers the
  transliterated Str: `'hello' ~~ TR/e/1/` is "h1llo". An older regression
  test asserted the previous answer, which Rakudo 2026.09 does not give
  either; it now asserts Rakudo's. A StrDistance's `.gist` and `.raku` are
  its constructor, as Rakudo's.
- **rakudo#1243 — fixed (31206e76).** `.parse` and `.subparse` start at
  `:pos`, in characters. A failed `.subparse` is a failed Match of the
  grammar's type: false, defined, `#<failed match>`, with Rakudo's `.to` of
  -3. A failed `.parse` stays Nil.
- **rakudo#4752 — fixed (a2fcf5bb).** A bounded quantifier repeats a
  zero-width item to its maximum (its minimum when frugal), each capture
  recorded: `<same> ** 2..4` is four. Under `*` and `+` it is still taken
  once, where Rakudo loops forever.
- **rakudo#2791 — fixed (e9aaf6b3).** Not the lookahead: `.parse` and
  `.subparse` took their first argument as the target, so
  `Chord.subparse(:rule<bare-key>, 'C#')` parsed the `:rule` Pair. An
  undefined target dies, as Rakudo's does; the check was there but never
  fired.
- **rakudo#6810 — fixed (9e693ab0).** A `my token` or `my regex` returned
  from a sub closes over the call that declared it. Each call's `&F` is
  its own routine, by `===` and `.WHICH`, and a routine's `.WHICH` is now a
  serial, as Rakudo's `Sub|…` is (two closures of one `sub { … }` had been
  one Set element). Rakudo 2026.09 shares one object between the calls, so
  its first `&F` ends up matching the second call's pattern; the issue asks
  for what Raku++ now does.
- **rakudo#3410 — fixed (edc8bf39).** `self` in a standalone token's code
  is the cursor it was entered with: where the rule began, and for a token
  matched directly, where the scan began (Rakudo's 0 and 5). A plain
  `/…/` gets one too, where Rakudo refuses `self` at compile time; that is
  R10's.
- **rakudo#3775 — fixed (1ea5c984).** A ｢…｣ quote in a regex is a literal
  whatever brackets it holds, a `<&name>` call is one atom to the parse
  checks, and `<&?BLOCK>` recurses into the regex itself; it matched
  nothing. Left:
  - the issue's `:my $L = ~$0;` after `{}` warns once that `$0` is
    undefined, because the declaration runs before the cursor's `$0` is
    published;
  - a `"$0"` after the recursion is Nil, where Rakudo interpolates an
    empty string and matches;
  - Rakudo refuses a `…` in a regex as an unknown metacharacter
    (rakudo#3775's third snippet), and Raku++ accepts it; that is R10's.
- **rakudo#2059 — fixed (2137a05d).** An actions class's FALLBACK is
  called, as `FALLBACK($name, $/)`, for every rule it has no method for.
  The actions of `ws` (implicit or written) and of an inlined
  one-character token called as `<.q>` fire too; neither did. The issue's
  program now dies where Rakudo's does, on `ws`. The X::Multi::NoMatch
  message shows the arguments' types, where Rakudo's shows their values.
- **rakudo#5799 — fixed (d45f616e).** `\c[NAME]` in a regex, bare or in a
  quoted literal, takes the name in any case. The lower-case name resolved
  to nothing, so `o\c[combining ring above]` was a bare `o`. An unknown
  name still resolves to nothing, where Rakudo refuses it; that is R10's.
- The rest of the regex and grammar sections.

### R7 — valid code we refuse

*2026-11-13. The named items are done; the rest of the syntax PPBUG section
is not started.*

- **R7.0 — done (9e12d938).** String interpolation (parseInterpString and
  the two helpers only it uses) moved to src/ParserInterp.cpp, unchanged.
  Parser.cpp went from 18,455 lines against 18,460 to 17,766 against a
  re-recorded 18,299. Only its ceiling was re-recorded: `--record` rewrites
  every one, and would have loosened the others.
- **rakudo#1360, #2078 — fixed (70ee4e8b, 9badbec4).** An anonymous class
  that opens a statement takes a modifier, and its postfix chain an infix:
  `class { … } for ^5`, `class { … }.new.z = 42`. 9badbec4 repairs what
  70ee4e8b broke: a term-position `my class C {…}.Str, "x"` handed its
  caller one list (Roast's S12-methods/calling_syntax.t). A NAMED class
  before a bare modifier is left alone: Rakudo declares it once, at compile
  time.
- **rakudo#1360's other snippet — fixed (6fcd9181, b590c1ec).** A `for`
  that is its block's value does not sink its iterations
  (`{ Failure.new for ^5 }()` threw), in the interpreter and in compiled
  code. b590c1ec repairs what 6fcd9181 exposed: a gather block's last
  statement is sunk, as in Rakudo, so `gather for ^5 { Sinker.new }` takes
  again (Roast's S04-statements/for.t), and `gather { take 1; Failure.new }`
  throws, as Rakudo's does.
- **rakudo#2078's third snippet — fixed (f102e0f5).** An item in a
  `has (…)` list takes its own traits before its default:
  `has ($.z is rw = 42)` lost both. Left: `dd` of the accessor shows the
  value, where Rakudo names the container (`$!z = 42`).
- **rakudo#2243 — fixed (b1040540).** A block phaser takes a modifier on its
  line. The phaser stays declared whatever the condition; the modifier
  governs the statement's value, Nil, queued after it. The issue's sub now
  prints 42 and answers 99, as it expects; Rakudo 2026.09 crashes there
  ("setcodeobj needs a code ref"). Rakudo's compile-time "Undeclared
  routines" for `LEAVE { blah } if foo` is R10's.
- **rakudo#5027 — fixed (d77e1185).** Not the underscore: the lexer read
  `token_ {` as the declarator `token`, a rule named `_` and a regex body.
  The keyword is now a whole word and never follows a `\`. And a sigilless
  name in a list assignment holds a list as it is (`my (\a, \b) = (1,2), 3`).
- **rakudo#2336 — fixed (953adf0a).** A `&` item in a declaration list takes
  a signature, which Rakudo neither checks nor does Raku++, and declares a
  callable name. The single-variable form `my &op:(…) = …` is a compile error
  in Rakudo and a run-time one here; that is R10's.
- **rakudo#3376 — fixed (73357985).** `enum E does R <a b>` parses. An
  enum's metaobject is composalizable, and every archetype answers 1 or 0,
  as Rakudo's native ints do. Left: the role is consumed, not composed into
  the values (Rakudo's `a.hi` works).
- **rakudo#5901 — fixed (fdaa60ec).** `say *>>.fmt("%02x")(42)`: a tight
  hyper method call on `*` after a list operator is a WhateverCode.
- **Found while probing — fixed (f8b5555c).** Every `;` segment of a
  parenthesised list takes a modifier: `($n++; last if $n > 3) for 1..*`.
  Left: a `for` modifier in a later segment yields a Seq, where Rakudo's is
  a List.
- **rakudo#1264 — fixed (ce72763f).** A quoted word in an operator name's
  `« »` list is the word: `infix:«"z"»` is `z`, in EVAL too.
- **rakudo#2129 — fixed (f9028851).** `return` takes its value as a list
  operator, below the loose `and`/`or`: `return 0 or 5` is 0. And a
  declaration called at once takes a postfix chain, `sub foo {…}().say`.
- The rest of the syntax PPBUG section: 34 cases in all.

### R8 — strings, numbers, dates, formatting

*2026-11-13. The named items are done; the rest of the strings, formatting,
numeric and datetime sections is not started.*

- **rakudo#1460 — fixed (6b8c1d1a).** A hyper method call joins an
  interpolated variable's method chain: `"%add».sort.map(…)"`. As Rakudo
  does, it joins only where no bare `.name` is still pending
  (`"@a.sort».uc()"` stays literal).
- **rakudo#1961 — fixed (7e5a643e).** An anonymous state variable in a
  string's `{ … }` is that block's, fresh each time the string is built:
  `"count {$++}" for ^3` is count 0 three times, as in Rakudo. It had
  counted 0 1 2, and two blocks in one string shared one `$`.
- **rakudo#2434 — fixed (eb482a9f).** `dd` answers Nil, so a Failure it
  showed is not sunk after it. A built-in exception's `.raku` leaves out the
  `message` slot (a method in Rakudo), and X::Numeric::DivideByZero shows
  Rakudo's attributes in Rakudo's order. Left:
  `X::Numeric::DivideByZero.new(numerator => 5).message` is (Any), where
  Rakudo composes the text; only DivideByZero has Rakudo's order so far.
- **rakudo#2097 — fixed (bfd530f9).** A leap second stays one when moved to
  a timezone that is not a whole number of minutes, as in Rakudo's own
  in-timezone: the shift moves its minute. Rakudo's `.Instant` of such a
  converted leap second is its own (1483228863 for the issue's); Raku++'s
  is not compared.
- The rest of the strings, formatting, numeric and datetime sections.

### R9 — missing features, most useful first

*2026-11-13. Done but nested coercion types (rakudo#4517).*

- **rakudo#1513, #3844, #5144 — done (fcc6d577).** `use Telemetry` loads:
  rakulib/Telemetry.rakumod ports Rakudo's lib/Telemetry.rakumod to plain
  Raku (both Artistic 2.0), over one built-in, `rakupp-getrusage`, and is
  embedded as a shadow module. `T<max-rss>`, `snap`, `snapper`, `periods`,
  `report` and the END report are Rakudo's, column for column. `T{"max-rss"}`
  needed a parser change: a brace tight against a routine's name subscripts
  the call's answer, as in Rakudo. Left:
  - Telemetry::Instrument::AdHoc does not work, because `*@v is raw` does not
    hand on the caller's containers (`.VAR.name` is the parameter's, and a
    kept `.VAR` does not follow later writes).
  - The Thread and ThreadPool instruments read zeros: there is no
    `Thread.usage` or `$*SCHEDULER.usage` yet.
  - `snap(:heap)` dies; there is no heap to snapshot.
- **rakudo#2639 — done (ef2e778b).** `CALLERS::('$_')`, `CALLER::(…)` and
  `M::(…)` look the name up in the package; they called a sub named
  `CALLERS::`. CALLERS reaches a routine's own `$_` or the mainline's, not a
  `given` or `for` topic, as in Rakudo, so the issue's snippet answers Any
  there and here. Left: a COMPUTED name (`CALLERS::("\$_")`, double quotes)
  goes through the pseudo-stash, which still finds a block topic.
- **rakudo#4780 — done (bec86076).** `.prec`, `.precedence`, `.associative`,
  `.thunky` and `.iffy`, from Rakudo's table for the built-ins and from
  `is equiv/tighter/looser/assoc/prec` for declared operators.
  `.prec('prec')` answers the string, where Rakudo dies on its own return
  type, which is the bug the issue records. Left:
  - The ordering itself: a prefix `is tighter(&postcircumfix:<[ ]>)` still
    applies after the postcircumfix, so the snippet's `§42⸨0⸩` is
    "prefix called last". The issue and Rakudo both say "postcircumfix
    called last".
  - No callable: `&postfix:<i>`, `&postfix:<ⁿ>`, `&prefix:<let>`,
    `&prefix:<temp>`, `&prefix:<^>`, `&prefix:<|>`, `&prefix:<−>`,
    `&postcircumfix:<[; ]>` and `&postcircumfix:<{; }>`.
  - `::("&infix:«<»")`, a symbolic lookup of a «»-spelled name, finds nothing.
- **rakudo#6873 — done (ea466f67).** `⚛−=` with U+2212, and the callables
  `&prefix:<⚛>`, `&prefix:<++⚛>`, `&prefix:<--⚛>`, `&postfix:<⚛++>` and
  `&postfix:<⚛-->`. Rakudo reads `$x ⚛== 1` as a write; the issue calls
  that a bug, with no consensus on comparing or refusing. Raku++ keeps
  refusing it, now with a message that says to write `⚛$x == 1`.
- **rakudo#5358, #5359 — done (9a6eeb85).** `IO::Socket::Async.listen-path`
  and `.connect-path`, UNIX-domain sockets, with Rakudo's names, ports and
  error words. Both issues' own subjects are Rakudo's: `throttle`'s
  `:vent-at(0)`, and a buffer overflow on close. Neither is reproduced here.
- **rakudo#1879 — done (a247fad2).** IO::Socket::INET over IPv6:
  `:family(PF_INET6)`, or an IPv6 literal address; `.family`, `.host` and
  `.port`.
- **rakudo#4361 — done (112a0cdb).** `role A[::T] does array[T] { }`
  declares. Left for R10: composing it is not refused, where Rakudo says
  "array[int] is not composable".
- **rakudo#4517 — not done.** None of the nested coercion types works.
  `Array[Int]()` as a term dies "No such method 'CALL-ME'". A value coerced
  into `Array[Int]` (`my Array[Int]() $x = (1, 2)`) is "Impossible
  coercion". `Array[Int()]` loses its `()`, so its elements are not coerced
  (`my Int() @a` is). The term's arm is in InterpreterCore.cpp, at its
  ceiling, so this needs room made there first.
- **rakudo#2165 — done (2eafbb23).** `E.HOW.add_enum_value`, `enum_values`,
  `enum_value_list` and `enum_from_value`. `.pick` and `.roll` of an enum
  type answer its members, so a Str-valued one keeps its `.value` (it was
  0). `.^enum_values` is a Hash. Left: `E.HOW.elems(E)` answers 1, because a
  type object's `.elems` arm answers first.
- **rakudo#2275 — done (41150ad1).** `Parameter.twigil`; `.sigil` of `\g`
  is `\`, of `|c` `|`. Left: a method with a capture parameter still lists
  an implicit `*%_`, which Rakudo leaves out.
- **rakudo#5546 — done (3a3fc21e).** `use trace`, numbered and worded as
  Rakudo's, statement for statement on the programs compared. The issue's
  own `my %a{Str}` is Rakudo's bug ("Invalid hash shape") and runs here.
- **Found by the gates — done (62ada605).** Under `--exe`, a program
  reading a calling routine's `$_` through CALLERS got Any: the routine was
  compiled natively, and its `$_` was a native local. Such a program now
  keeps its routines interpreted.
- **Found beside them — done (251842c2).** R8's leftover:
  X::Method::NotFound and X::Numeric::DivideByZero made with `.new` have
  Rakudo's `.message`. Their composed `message` was passed positionally, so
  a second `.new` died.
- **Found beside them — open.** `dd $x` prints the value without Rakudo's
  `$x = ` prefix. It goes to stderr, so the snippet harness never compared
  it.

### R10 — strictness: code we run and Rakudo refuses

*2026-11-14. The named items are done; the rest of the LAX list is not.
Both decisions are answered (below). The battery scan against v5.8.0 is
green: 2,775 module files, 0 regressions.*

- **Test's and NativeCall's exports — done for their routines (1f3377ea).**
  `ok`, `is-deeply`, `lives-ok`, `plan`, `done-testing` and the rest, and
  `nativecast`, `cglobal`, `nativesizeof`, `explicitly-manage` and
  `refresh`, are "Undeclared routine" without a `use` of the module
  (rakudo#1631, #3607, #4967, #1612, #2191). The call check reports them.
  A routine the code declares, a sigilless name, or a `use`d module that
  may re-export the name keeps the call. EVAL'd code sees its caller's
  `use`. Left: NativeCall's TYPES (`CArray`, `Pointer`, `long`), which a
  declaration's check passes as parameterized shapes (rakudo#1778, #1767).
- **`use v6.e` without `.PREVIEW` — done (9f94cac7)** (rakudo#2783), with
  X::Language::Unsupported for a revision or modifier no compiler has.
- **`is cached` without `use experimental :cached` — done (2fe0aeeb).**
- **Declarations — done (8f7ac1df).**
  - `has Int $.count!` (rakudo#1427);
  - `sub ($!x)` and `-> $!x` outside a method;
  - `-> < 1/2> {…}` (rakudo#1828);
  - a default on a required positional (rakudo#1426). A required NAMED
    parameter with a default is left: GLib in the battery writes one;
  - a placeholder in a method (rakudo#2320);
  - a CStruct with no attributes (rakudo#2673).
- **Calls — done (6de9d27b, ce54f928).**
  - `'x'.substr(/x/, 'x')`;
  - `self."!$name"()` (rakudo#3802);
  - `Lock.new.protect({})` (rakudo#1644);
  - `(0...∞).chrs` (rakudo#2742);
  - `await start { return }`, which breaks its Promise (rakudo#2753).
    A `start` block inside a routine is unchanged: Rakudo panics there.
- **Unquoted symbols in a regex — done (d3059bf5)** (rakudo#3775, #4927).
  `,`, `=`, `-`, a backtick and symbols beyond ASCII are refused at the
  pattern's own level. Not inside assertions, quotes (nested where Raku
  nests them), code, comments or `\c[…]`/`\x[…]`. An `=` after a capture
  name or variable is an alias.
- **A phaser called like a routine — done (f1491b3e):** `FIRST($_ * 10)`
  and the other phaser names with a tight paren. Left: `CATCH(1)`, which an
  earlier arm reports as "Missing block".
- **From the LAX list — done (06212d97):** `42:foo` and `False:so`
  (rakudo#1476, #3426), and `say 1, 2, and 3` (rakudo#4107).
- **Kept: `m:P5`, `rx:P5`, `s:P5`** (rakudo#2624). Rakudo 2026.09 refuses
  the Perl 5 adverb everywhere. Raku++'s Perl 5 regexes are a deliberate
  extra, and two battery modules use them (HTTP::Tinyish, Net::Ethereum).
- **Found beside them — done:**
  - `method !q { … }` lexed `q {…}` as a quote, and the method vanished
    (6de9d27b);
  - battery-scan and two other tools passed `:env` a List, which no longer
    binds (a213ccad). The scan could not run until this.
- **Decision 2's re-measure — done (e89cc6a7).** It fixed the false
  positives of the strict separation that are valid Raku:
  - `return not …`, which also returned Nil in files, an R7 regression;
  - `GLOBAL::<x>:delete`, which parsed as two statements;
  - `!!! ::?CLASS.^name ~ …`;
  - `… or last LABEL`, whose label was dropped.

  With the battery's libraries on the path, whole-file strictness would
  now refuse 31 of 8,169 files that compile today:
  - 17 in Qwiratry, all under its slang: `X and return Y` and its `σ`
    operator;
  - 7 using units a module's EXPORT sub makes (`2.5nm`, Physics::Measure
    and ::Constants);
  - 7 single cases (Temp::Path's `make-temp-path :content<…>`, a Red
    `model` as a term, Mi6::Helper's `++$n` that opens a line, and four
    more).

  Each is valid Raku that Rakudo compiles. So the file decision stays open:
  turning it on needs the parser to see operators and terms that a module
  makes at EXPORT time, and slangs.
- The rest of the LAX list (about 80 cases): not started.
  - `has $.foo is rw := …` with an undeclared type (rakudo#1288);
  - the `where` subtype instantiation cases (rakudo#1556, #1557);
  - the ambiguous-multi cases (rakudo#1286), with R4;
  - and the rest of FIX-LIST.md's LAX section.

### R11 — speed, handed to SPEED-BACKLOG.md

Done 2026-11-14 (02b24803, 9e948ff3), but for the `nqp::` forms of rakudo#1416,
which are SPEED-BACKLOG row 56.

- **rakudo#2844, done:** `1, 1, * + * ... *` to 40k terms took 0.92 s here and
  0.07 s in Rakudo, and our own `gather` 0.30 s. Both engines grow
  quadratically here, as the terms do; the factor was in two places.
  - Each lazy step numified the last element for a numeric step a generator
    closure does not use. An Int of thousands of digits numified through its
    decimal string and strtod: 51 µs at 9,500 digits, against 4.5 µs for the
    addition. An Int of 36 limbs or more (at least 1e315) is now ±Inf without
    the string, and the closure path reads no number.
  - BigInt addition carried with a division and grew its result a limb at a
    time. It now carries with a compare, into one allocation.
  - 40k terms: 0.92 → 0.03 s. 200k Fibonacci steps in a plain loop: 2.46 →
    0.71 s (Rakudo 0.79).
  - `t/regression/sequence-generator-over-huge-ints.raku`, with 300 random sums
    against a schoolbook addition.
- **rakudo#1416, its Raku shapes done:** a loop of `my $ = …` was slow in two
  ways.
  - An anonymous `my $` is named `$` + "\x01anonN", which failed the name test
    of the declaration lane and of the loop kernel. `for ^3_000_000 { my $ = 1 }`
    took 0.48 s against 0.003 s for `my $x = 1`; both now take 0.003 s.
  - A ternary on the right, the Raku shape of the issue's `nqp::if`, took the
    full path. A ternary whose condition the lane computes, and whose picked
    branch it can give, now takes the lane. `my $ = $n ?? 1 !! Nil` went from
    0.54 to 0.21 s (Rakudo 0.22).
  - `t/regression/anonymous-my-and-ternary-on-declaration-lane.raku`.
  - **Left, SPEED-BACKLOG row 56:** a right side the lane cannot compute
    without effects (a call, a method, an `nqp::` op) pays the full declaration
    path. The issue's `my $ = nqp::if($n, 1)` over 30M takes 5.4 s against
    Rakudo's 1.4, and with `nqp::isne_i` 6.9 s against 0.77. The fix lets
    evalAssignInner take a right side already evaluated. It lives in
    InterpreterCore.cpp, which is at its line ceiling.

### R12 — last

- **The RakuAST API's extras:**
  - `.AST` of a grammar rule (rakudo#5479, #5978, #5991);
  - RakuDoc `.AST` trees (rakudo#5798, #5721);
  - `RakuAST::Deparse::Highlight` (rakudo#5628);
  - our demand for `use experimental :rakuast` where Rakudo makes none
    (rakudo#6134, #6137);
  - the 4 rakuast PPBUG cases.
- **INTERNAL (29 cases):** `nqp::` ops, CompUnit and precomp internals,
  `--ll-exception`. No shared expectation; fix only if a real module needs one.
- **ERRMSG (41 cases):** error wording. Fix when a batch is already in the
  code.
- **SHARED (72 cases, 63 issues):** open Rakudo bugs we reproduce. Each waits
  for the issue thread to agree on the answer, and is a chance to be right
  first.
- **UNCLEAR (43 cases):** re-read with the issue's comments, which the sweep
  did not download.

## Decisions for the maintainer

Each is answered when its batch starts, not before.

1. **(R10) Tightenings that can break code that runs on Raku++ today:**
   - Test's and NativeCall's exports without their `use`;
   - `use v6.e` without `.PREVIEW`;
   - `is cached` without its pragma.

   Refuse them as Rakudo does, or warn for one release first?

   *Answered 2026-11-13: refuse now,* as Rakudo does, with a message that
   names the `use` to add, re-exports honored, behind a clean battery scan.
   No battery file relies on any of them. The few hits get Test or
   NativeCall through a re-export (HTTP::Tiny::Test, NativeLibs).
2. **(R10) A missing `;` across lines.** This is already in TODO's decisions;
   the tracker data is new evidence for it.

   *Answered 2026-11-13: fix the checker first, then re-measure.* A probe
   that gave whole files EVAL's strict statement separation refused 106 of
   the 8,208 battery files that compile today. Every one was valid Raku
   that Rakudo compiles: slang declarators (`model M {`, Red's 71 files),
   unit postfixes (`10pt`), `return not (…)`, `GLOBAL::<x>:delete`,
   `!!! ::?CLASS.^name`, `or last LOOP`, and a `++$n` that opens a line.
   Three of those make EVAL refuse valid code today. R10 fixes the false
   positives, reruns the probe, and brings the file decision back with the
   new numbers.
3. **(R3) Order of R3 and R4.** The read-only attribute (R3) and the ambiguity
   check (R4) are the two fixes most likely to turn up breakage in the
   battery. Doing them together, behind one battery scan, saves a scan.

   *Answered 2026-11-13: together.* R3 and R4 run as one batch, behind one
   battery scan.
4. **(R1) rakudo#5868: one role per declaration, or a lock on the type
   tables?** Rakudo's model would end the per-call type churn, and with it
   the crash and a leak of one class per call. But it changes what such a
   method sees: the last call's lexicals (`[3 3 3]`), where Raku++ gives each
   call its own (`[1 2 3]`). Locking `classes_` and `lastDecl_` keeps today's
   answers, but it is PARALLEL-PLAN's open project: every reader of those
   tables races a writer.

   *Answered 2026-11-12: defer to PARALLEL-PLAN.* Keep the per-call roles
   and their answers. The crash is fixed when `classes_` and `lastDecl_` are
   locked, and rakudo#5868's snippet is that work's reproducer.

## Progress

| batch | state | commits | snippets MATCH (of 1,777) | Roast files | t/run.raku |
|---|---|---|---:|---|---|
| baseline, 2026-11-12 | — | 6ee8f413 | 1,264 | 1,425/1,425 | 2259/2259 |
| R1, 2026-11-12 | done; rakudo#5868 deferred to PARALLEL-PLAN (decision 4) | 041789fd, 147d0b95, 7fc5b00b, b4f6e472 | 1,267 | 1,425/1,425 | 2263/2263 |
| R2, 2026-11-13 | done | 00c30ed1, b980a365 | 1,270 | 1,425/1,425 | 2265/2265 |
| R3 + R4, 2026-11-13 | named items done but the coercion ones and four narrow ones; PPBUG remainders open | 4f507a0f, 401e19e8, cee2cd42, 29f64dcc, bdb022c4, 38cb1d1e, a64cd841, 91033936, 2987aab9, b66ccda3 | 1,277 | 1,425/1,425 | 2274/2274 |
| R5, 2026-11-13 | named items done but rakudo#5138; PPBUG remainders open | 521d34ae, a0437c10, 703198f5, cb91f67b, 2416bbe5, 8c513f23, 133560ad, ad10ad81, 3db5cae5, c109e8ec, c47aa1ea, cfbda86a, ccda30e9, 22520fae, 1c4da191, 585a8ee1, eca56b5a, 1ba950a5, 7dc4809d, 9bb82a6c, 57b07f65, 1f205847 | 1,289 | 1,425/1,425 | 2293/2293 |
| R6, 2026-11-13 | named items done; regex and grammar remainders open | 6b709af5, 31206e76, a2fcf5bb, e9aaf6b3, 9e693ab0, edc8bf39, 1ea5c984, 2137a05d, d45f616e | 1,302 | 1,425/1,425 | 2302/2302 |
| R7, 2026-11-13 | R7.0 and the named items done; syntax PPBUG remainders open | 9e12d938, 70ee4e8b, 6fcd9181, f102e0f5, b1040540, d77e1185, 953adf0a, 73357985, 9badbec4, b590c1ec, f9028851, fdaa60ec, f8b5555c, ce72763f | 1,318 | 1,425/1,425 | 2313/2313 |
| R8, 2026-11-13 | named items done; strings, numeric and datetime remainders open | bfd530f9, eb482a9f, 7e5a643e, 6b8c1d1a | 1,324 | 1,425/1,425 | 2317/2317 |
| R9, 2026-11-13 | done but rakudo#4517 (nested coercion types) | 41150ad1 … 62ada605 | 1,329 | 1,425/1,425 | 2329/2329 |
| R10, 2026-11-14 | named items done; NativeCall types, `CATCH(1)` and the rest of LAX open; whole-file strictness still open (decision 2) | 9f94cac7 … a213ccad | 1,358 | 1,425/1,425 | 2339/2339 |
| R11, 2026-11-14 | done but rakudo#1416's `nqp::` forms (SPEED-BACKLOG row 56) | 02b24803, 9e948ff3 | 1,357 | 1,425/1,425 | 2341/2341 |

R1's per-file changes against the baseline:
- **3686 and 5835:** TIMEOUT to MATCH.
- **5537:** to DIFF-EXIT; the R3 item above is what is left.
- **1407 and 5995:** DIFF-EXIT to MATCH; unrelated, and not looked into.
- **2181:** MATCH to DIFF-OUT; noise, the snippet prints `<0 1>.pick`.

R2's per-file changes against R1:
- **4351:** DIFF-BOTH to MATCH (`say FIRST 2`).
- **4504/0:** DIFF-OUT to MATCH (`now - ENTER now` at the top level).
  4504/1, the same with a `multi`, stays DIFF-OUT on Rakudo's side: its
  `.race` runs serially there and prints 3, and Raku++ prints 1.
- **2181:** back to MATCH; noise, as in R1.
- The other timing snippets are NONDET, so the diff run leaves them out.
  Run through the harness by name, each prints a positive time, except
  3620/0, which calls `nqp::time_n`.

R3 and R4's per-file changes against R2:
- **1286, 2068/2, 4463, 4553, 5908, 6113, 6860, 2663:** to MATCH.
- **3683:** MATCH to DIFF-OUT; noise, the snippet prints today's date.

Codegen changed three ways in R3 + R4 (cached and `.default`-reading
routines refused, a class's `self!p` qualified), so the full
t/exe/parity.raku ran too: 1,599 agree, 0 differ.

The battery run decision 3 asked for (RELEASING.md step 6,
tier2/run-dist-tests.raku, run inside harness/battery.sb, so with no
network) found no regression. The candidate passes the same test files
as v5.8.0 in every dist: 41 PASS, 10 DIFF, 7 ENV, 1 without tests. All ten
DIFFs are there at v5.8.0 too. Against the battery's committed baseline
(v5.0.0, run outside the sandbox), seven dists went PASS -> DIFF or ENV;
five need the network the sandbox denies. Data::Dump and Log::Async do
not, and are fixed (6eef76fd, ecaee71c): a candidate's `where` check
evaluated defaults in the caller's scope (broken since v5.3.0), and
`try EXPR` left no frame for Log::Async to drop, exposed when v5.7.0
hid `is hidden-from-backtrace` frames. With them the battery reads 43
PASS, 8 DIFF, 7 ENV, 1 without tests, and the only dists below the v5.0.0
baseline are the five that need the network. AttrX::Mooish (1/35) never
passed: at v4.0.0 Rakudo too ran only 1 of its 35 files there. Its first
blocker is fixed (a mixed-in Attribute's `.^get_attribute_for_usage`).
The rest is Rakudo's MOP internals: a role mixed into the class's HOW,
its `@!BUILDPLAN` rewritten through `nqp::getattr`/`nqp::bindattr` so
construction runs its lazy builders, and Attribute's own `$!composed`.

R5's per-file changes against R3 + R4:
- **1447/0, 1629/2, 1692/2, 2217/0, 2485/0, 3621/2, 3881/0, 4128/0,
  5951/0, 6128/0, 6128/1:** to MATCH.
- **1705/2:** DIFF-OUT to MATCH; `[(elem)] 2334`, the set-predicate
  reduction (2416bbe5).
- **1692/0 and 2000/0:** DIFF-BOTH to DIFF-OUT. The exit codes agree now;
  what is left is in the R5 section (Rakudo's Empty quirk, assigning
  through the call's result).
- **5868/0:** DIFF-EXIT to TIMEOUT; noise. It is the `.race` crash
  deferred by decision 4, and four runs gave SIGSEGV twice, SIGBUS once and
  one timeout.

R5 changed no codegen, but three of its fixes are in the runtime that
compiled programs call (by-name infix calls, a lone reduce operand, the
hyper operator), so the full t/exe/parity.raku ran: 1,618 agree, 0 differ.

R6's per-file changes against R5:
- **1243/0 and /1, 1551/0, 2059/0, 2791/0, 3410/0, 3775/0 and /1, 4752/0,
  /1 and /2, 5799/1:** to MATCH.
- **5782/0 and /1:** DIFF-OUT to MATCH; unlisted, but they call
  `G.parse(:rule<bar>, …)` with the named argument first, which e9aaf6b3
  fixed.
- **6810/0:** MATCH to DIFF-OUT, and **6810/1 and /2** DIFF-BOTH to
  DIFF-OUT. Raku++ now gives each call its own routine, which the issue
  asks for; Rakudo 2026.09 shares one, so `$a === $b` is True there.
- **2181:** MATCH to DIFF-OUT and **5329/0** DIFF-OUT to MATCH; noise.
- **5868/0:** TIMEOUT to DIFF-EXIT; the deferred race, noise.

The regex engine and the grammar runtime are shared with compiled
programs, so the full t/exe/parity.raku ran: 1,627 agree, 0 differ.

R7's per-file changes against R6:
- **1264/0, 1360/0 and /2, 2078/0 and /1, 2129/0, 2336/2, 3376/0, 5027/0,
  /1 and /2, 5901/0:** to MATCH.
- **2174/0, /1 and /2:** DIFF-EXIT to MATCH; `class {}.new does Foo` is the
  anonymous-class statement 70ee4e8b fixed.
- **4522/1:** DIFF-BOTH to MATCH; a `has (…)` item's traits (f102e0f5).
- **2243/0:** MATCH to DIFF-BOTH. Both engines had printed nothing; Raku++
  now prints the 42 the issue expects, where Rakudo crashes ("setcodeobj
  needs a code ref").
- **2181, 5329/0, 5868/0:** noise, as before.

Full Roast found two regressions on the way, both fixed before the batch
closed: 9badbec4 (S12-methods/calling_syntax.t) and b590c1ec
(S04-statements/for.t). The final run had S17-supply/unique.t at 15/16,
a `sleep`-timed test that passes alone, three runs of three. Codegen
changed (6fcd9181), so the full t/exe/parity.raku ran: 1,638 agree,
0 differ.

R8's per-file changes against R7:
- **1961/0, 2097/2, 2434/0 and /1:** to MATCH.
- **2265/1, 2266/0 and /1, 2452/1:** DIFF-EXIT to MATCH; each is a `dd` of a
  Failure, which no longer explodes after it (eb482a9f).
- **1460/0** prints Rakudo's words in its own hash order, and is NONDET.
- **2181, 5868/0:** noise.

The full t/exe/parity.raku ran too (dd, exceptions and interpolation are
shared with compiled programs): 1,642 agree, 0 differ.

R9's per-file changes against R8:
- **1879/0, 2275/0, 2639/0, 4361/1:** to MATCH.
- **4780/0:** MATCH to DIFF-BOTH. Rakudo dies on its own `.prec('prec')`,
  which is the issue's bug. Raku++ prints the output the issue wants.
- **2181/0, 5329/0:** noise. 5329 prints from two `hyper` threads.

The full t/exe/parity.raku agreed on 1,653 programs and differed on one:
callers-call-form.raku, whose compiled routines kept `$_` where CALLERS
could not see it. 62ada605 keeps such a program interpreted, and the 21
programs that mention CALLER, with the new tests, agree.

R10's per-file changes against R9 (29 files, all to MATCH, none away from it):
- **Test or NativeCall routines without their `use`** (1f3377ea), now
  undeclared as in Rakudo: 2191/0, 2402/0 (was TIMEOUT), 3316/0, 3607/0,
  1612/2, 1631/2, 4967/0.
- **Refused declarations** (8f7ac1df): 1426/0 and /1, 1427/2, 1828/1,
  2320/0, 2673/0 and 4961/0 (CStruct with no attributes).
- **Refused calls** (6de9d27b): 1314/0, 1644/1, 2742/1, 3802/0.
- **Regex symbols that must be quoted** (d3059bf5): 3775/2, 4927/2.
- **Adverb on a literal, word infix after a comma** (06212d97): 1476/0,
  3426/0 and /1, 4107/0.
- **`use v6.e` without PREVIEW** (9f94cac7): 2783/1. **`use v6` with no `;`**
  now stops at the end of the line: 2516/0.
- **`return` in a mainline `start` block** (ce54f928): 2753/1.
- **Pasted chat transcripts** that Rakudo refuses at the same line:
  1237/0, 1815/0.

The battery scan against the v5.8.0 release build was GREEN: 2,775 files,
2,744 compile under both, 31 fail under both, 0 regressions. Parser,
regex checker and interpreter changed, so the full t/exe/parity.raku ran:
1,664 agree, 0 differ, 11 skipped.

R11's per-file changes against R10:
- **5329/0:** MATCH to DIFF-OUT. This is noise, as in R9: its output differs
  between runs of one binary. No other snippet changed. The two issues'
  snippets print their own timings, so they are compared by hand above.

The kernel and interpreter changed, so the full t/exe/parity.raku ran: 1,665
agree, 0 differ, 12 skipped. t/scaling's run domain had 76 shapes linear and
the 2 known ones superlinear. perf-guard flagged junctionwide at +52%
against its recorded baseline. It takes 24–25 ms on this batch and on v5.8.0
alike (an A/B with the order swapped), so the baseline is stale, and the
batch did not cause it.
