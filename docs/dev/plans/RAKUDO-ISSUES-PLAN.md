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

- **rakudo#4757 ✓:** `2 R- 3 R- 4` gives 3; it should give −1. R must
  reverse the associativity too.
- **rakudo#6128 ✓, #1616 ✓:** R does not compose over Z or S (`RZ`,
  `&[RZ,]`).
- **rakudo#4128 ✓:** `reduce &[>], 6, 5, 4` gives False. A chaining operator
  must chain under `reduce`.
- **rakudo#2217 ✓:** `½ ∈ ^2` is True. A Range coerces to the Set of its
  integers.
- **rakudo#2000 ✓:** `&postcircumfix:<{ }>` is not a callable routine.
- **rakudo#1447 ✓:** `roundrobin` does not apply the single-argument rule.
- **rakudo#2485 ✓:** `.head(*.elems - 1)` gives `()`.
- **rakudo#3881 ✓:** `{ … } ...^ 21` never stops.
- **rakudo#1692 ✓:** `eager Seq.from-loop: {…}, {1}` dies with "Cannot eager".
- **rakudo#1629 ✓:** `.combinations(2.3..^3.6)` drops a combination.
- **rakudo#1355 ✓, #3621 ✓:** the `$( )` of an itemised inner list is lost by
  `+l` and by `.raku`.
- **rakudo#5951 ✓:** `[»+«]` over two arrays loses a level.
- **rakudo#5138 ✓:** `(1..*).Slip.is-lazy` is False.
- **rakudo#1399 ✓, #1398 ✓:** `.sink for 1..*` returns at once. A pure loop
  over an infinite range is elided; it must run forever.
- Found by R2: the set predicates reduced over nothing or one operand.
  `[⊆] ()` is Any and `[∈] (1,)` is 1; Rakudo gives True, as a chain does.
- The rest of the lists, hashes and numeric sections.

### R6 — regexes and grammars

- **rakudo#1551 ✓:** `'hello' ~~ TR/e/1/` gives False.
- **rakudo#1243 ✓:** `.subparse(:pos)` ignores `:pos`, and a failed subparse
  returns Nil instead of a failed Match.
- **rakudo#4752 ✓:** `<same> ** 2..4` never matches.
- **rakudo#5799 ✓:** a literal base character plus a combining mark in a
  pattern does not match its grapheme (NFG).
- **rakudo#2791 ✓:** `<!before \S>` at the end of the string fails inside a
  grammar rule.
- **rakudo#6810 ✓:** a `my token` in a sub cannot see the sub's lexicals
  through `<{$re}>`.
- **rakudo#3410 ✓:** `self` inside an anonymous token's code block is
  refused.
- **rakudo#3775 ✓:** ｢{｣ corner-bracket quotes with `:my $L` in a regex fail
  to parse.
- **rakudo#2059 ✓:** an actions class's `FALLBACK` is never called.
- The rest of the regex and grammar sections.

### R7 — valid code we refuse

- **R7.0, make room.** Move a self-contained group of functions out of
  src/Parser.cpp into a file of its own, and record the new ceilings in
  tools/source-helpers/budget.txt. Commit this alone, so the moves and the
  fixes can be reviewed apart.
- **rakudo#1360 ✓, #2078 ✓:** an anonymous class as the first term of a
  statement takes no postfix or infix after it (`class {…}.new for ^5`,
  `class {…}.new.z = 42`).
- **rakudo#2243 ✓:** `LEAVE { … } if False;` is refused by the #206 rule. That
  rule must allow a statement modifier after a block-statement phaser.
- **rakudo#5027 ✓:** sigilless parameters whose names end in `_` fail to
  parse.
- **rakudo#2336 ✓:** `my (&op:(Int, Int --> Int)) = …` fails to parse.
- **rakudo#3376 ✓:** `enum E does Positional <a b>` fails to parse.
- **rakudo#5901 ✓:** `*>>.fmt("%02x")` fails to parse.
- **Found while probing:** `($n++; last if $n > 3) for 1..*` is "Two terms in
  a row". A statement modifier inside a parenthesised statement list is not
  accepted.
- **rakudo#1264 ✓:** a user `infix:«"z"»` is invisible to EVAL.
- **rakudo#2129 ✓:** `return 0.flip or die "bar"` returns `(Any)`.
- The rest of the syntax PPBUG section: 34 cases in all.

### R8 — strings, numbers, dates, formatting

- **rakudo#1460 ✓:** `"%h».sort…"` is not interpolated.
- **rakudo#1961 ✓:** `"count {$++}" for ^3` gives 0 1 2. Each closure clone
  needs its own anonymous state.
- **rakudo#2434 ✓:** `dd` of a Failure later throws it.
- **rakudo#2097 ✓:** a leap second is lost in a timezone that is not a whole
  number of minutes.
- The rest of the strings, formatting, numeric and datetime sections.

### R9 — missing features, most useful first

- **`use Telemetry` ✓** (rakudo#1513, #3844, #5144). It is Rakudo's, not
  Roast's, but it is how people measure a Raku program: `T<max-rss>`,
  `snapper`, `report`, `Telemetry::Instrument::*`. Independent of everything
  else; it can start any time.
- **`CALLERS::`** (rakudo#2639), a specced pseudo-package.
- **`Routine.prec` ✓** (rakudo#4780).
- **`⚛==` ✓** (rakudo#6873), and whatever else of the atomic operator family
  is missing.
- **IO::Socket::Async over Unix sockets**: `listen-path` and `connect-path`
  (rakudo#5358, #5359).
- **IPv6 sockets** (rakudo#1879).
- **Nested coercion types** (rakudo#4517), and `role R[::T] does array[T]`
  (rakudo#4361).
- **`Metamodel::EnumHOW.add_enum_value` ✓** (rakudo#2165), and `.twigil` on a
  method's invocant Parameter ✓ (rakudo#2275).
- **`use trace`** (rakudo#5546), last.

### R10 — strictness: code we run and Rakudo refuses

Each item can stop code that runs here today, so each lands only with a clean
battery scan, and some need the decisions below first.

- **Test's and NativeCall's exports visible without `use`** (✓ `is-deeply`,
  ✓ `CArray`; also `ok`, `lives-ok`, `cglobal`, `Pointer`).
- **`use v6.e` without `.PREVIEW` ✓**, and **`is cached` without
  `use experimental :cached` ✓**.
- **Declarations Rakudo refuses**, all ✓: `has Int $.count!`, `sub ($!x) {}`
  outside a class, and `-> < 1/2> {…}`.
- **Calls Rakudo refuses**, all ✓:
  - `'x'.substr(/x/, 'x')` (Rakudo suggests `subst`);
  - `self."!$name"()`;
  - `await start { return }`.
- The rest of the LAX list: 95 cases, mostly in oo, syntax and dispatch.
- **Unquoted symbols in a regex are matched literally** (moved here from
  R1's rakudo#3775, ✓). Raku++ accepts `, = - / ` … → € © ¿` unquoted; Rakudo
  refuses each ("Unrecognized regex metacharacter … (must be quoted to match
  literally)"). Raku++ already refuses `;` and `!`.
- **A phaser called like a routine** (found by R2). Raku++ runs
  `FIRST($_ * 10)` as the phaser; Rakudo refuses it as an undeclared
  routine, as it does `ENTER(…)` and `INIT(…)`.
- **Not here:** a missing `;` between statements on separate lines. That is
  TODO's open decision on EVAL's strict statement separation. The tracker
  adds five issues whose snippets run only because of it.

### R11 — speed, handed to SPEED-BACKLOG.md

- **rakudo#2844 ✓:** `1,1,*+*...*` to 40k terms takes 1.5 s here and 0.28 s
  in Rakudo. The time grows quadratically, and lives in the sequence
  operator's two-argument generator path; the same numbers from a `gather`
  loop take 0.23 s.
- **rakudo#1416 ✓:** a hot loop of `my $ = …` is about 3.4× slower than in
  Rakudo.

These are scheduled by the speed track (one speed item per sitting), not
here.

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
2. **(R10) A missing `;` across lines.** This is already in TODO's decisions;
   the tracker data is new evidence for it.
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
not, and are TODO §2's.
