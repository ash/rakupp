# Triage — behavioural quirks to cover later

Gaps found outside the Roast harness (see [ROAST-GAPS.md](ROAST-GAPS.md) for the
Roast-derived classification). These surfaced while using rakupp to write real
programs, so they aren't yet mapped to specific failing test files — but each is
a real behavioural gap with a minimal repro. All verified on HEAD. Most are
interpreter gaps; the dated TODO sections at the end record gaps in the native
`--exe` compiler, where the fix that landed was a fallback and the rest is still
to do.

## From writing the `examples/` programs (2026-07-11)

| # | Symptom (repro) | Correct behaviour | Workaround used |
|---|---|---|---|
| 1 | `my @a = 1,2,3; my @b = @a; @b.push(4)` mutates **`@a`** too | `=` should copy the array, not alias it | `my @b = @a.clone` |
| 2 | `(1..20).grep(5 <= * <= 10)` returns **all** 20 | chained compare with `*` should curry one predicate | `.grep({ 5 <= $_ <= 10 })` |
| 3 | `gather { for 1..* -> $n { take $n } }` yields **`()`** | infinite `for` over a Range inside `gather` should stream | `loop` with a manual counter |
| 4 | `.map(-> $n { start {…} })` may never launch the threads (lazy) | mapping that spawns `start` should still run | append `.eager` |
| 5 | a `sub` in a `class {}` body → `Undefined routine` when called unqualified from a method | lexical subs should resolve from methods | define the helper at file scope |
| 6 | `"$x-1"` interpolates as the subtraction `$x - 1` | `-1` after `$x` is literal text | `"{$x}-1"` |
| 7 | `gcd(a, b)` / `lcm(a, b)` (call form) return empty | should equal the infix result | infix `a gcd b`, or `[gcd]` |
| 8 | `[|@a, 3]` builds `[[1 2] 3]` — the slip doesn't flatten in a literal | `|@a` should flatten into the array | `.clone` + `.push`, or build then flatten |
| 9 | `rule TOP { \d+ }` fails to parse `"42 "` (trailing space) | `TOP` should allow trailing whitespace like Rakudo | `.trim` the input first |
| 10 | `~(355/113)` → `3.141593` (truncated to ~6 places) | `Rat.Str` should not lose precision | `.nude` / `.numerator` / `.denominator` |
| 11 | `sprintf("%{$w}d", 7)` → `"d"` — `%{…}` read as hash interpolation | should be a dynamic field-width format | `sprintf('%*d', $w, 7)` |
| 12 | `constant N = 8; N` in term position → `Undefined routine 'N'` | a sigilless constant should be usable bare | `constant \N = 8` |

## From the native-`--exe` / parallel-harness round (2026-07-12)

| # | Symptom (repro) | Correct behaviour | Workaround used |
|---|---|---|---|
| 13 | `my @o = <a b>.map({ [1,2,3] })` → **6** elements — assignment deep-flattens the map result, even itemized `[…]` elements (`$(…)` doesn't protect them either) | 2 elements, each an itemized array (Rakudo: 2) | `@o.push(f($_)) for <a b>` — `push` keeps each tuple one item |
| 14 | `next` inside `.map({ next if …; $_ })` escapes to the **enclosing loop** — `for 1..3 { @r.push: (1..5).map({ next if $_ == 2; $_ }).elems }` leaves `@r` **empty** | `next` skips the map element; `@r` = `[4 4 4]` | `.grep` the elements away instead of `next` |
| 15 | `return` inside `CATCH` yields **Nil**: `sub f { die "x"; CATCH { default { return 42 } } }; f()` → Nil | returns 42 (Rakudo) | set a result variable in CATCH, return after |
| 16 | `1, 4, 9 ... 100` silently guesses a step from the last difference (21 elements, ends at 99) | Rakudo dies: "Unable to deduce arithmetic or geometric sequence" | give the generator explicitly: `1, 4, 9, { … } ... 100` |

## TODO — native `--exe` cannot dispatch most multi methods (2026-08-20)

Not a quirk with a workaround: a **compiler**-side gap, currently paid for with
a fallback rather than solved. Recorded here so the remaining half is not lost.

**What was wrong.** `Codegen::classRegister` emits a multi-method dispatcher
whose guard sees positional arity and nominal type and nothing else, yet it
decided every call anyway. Three things it cannot decide were being decided
wrongly:

- a REQUIRED named is invisible to the guard, so `multi method g(:$size!)`
  matched a call passing none and bound `$size` to `Any`;
- a `where` clause or a `:D`/`:U` smiley never enters the guard;
- a candidate declared in an ANCESTOR is unreachable, though in Rakudo a multi's
  candidate set spans the MRO (the interpreter defers up the chain — the
  `parentNext` branch in `Interpreter::invokeMethod`).

```raku
class P { multi method g(UInt:D $s = 1) { self.g(:size($s)) }
          multi method g(UInt:D :$size = 1) { !!! } }
class K is P { multi method g(UInt:D :$size) { "k$size" } }
say K.new.g;     # interpreter and Rakudo: k1 — compiled: k
```

**What landed.** Codegen now refuses such a group, so the program falls back to
AOT bundling and both faces agree. This is the call `Codegen::multiDef` already
made for multi SUBS (`"a multi candidate with a where/:D constraint"`); multi
METHODS simply never had the equivalent bail, which is why the above compiled at
all. Guarded by `t/regression/multi-method-compiled-dispatch.raku`.

**What it costs** (measured over 1,822 files — `examples/`, `showcase/`,
`t/regression/`, `tools/`, and the Roast checkout; 1,143 of them transpile
natively today):

| | files |
|---|---:|
| currently-native files that use a multi method at all | 23 |
| …now falling back to AOT | 21 |
| — because of a `where`/`:D` constraint | 5 |
| — because a candidate is (or may be) in a parent class | 16 |

The parent-class bails are dominated by `multi method new` on a class with a
built-in parent (`class NotComplex is Cool`, the S32-trig files) — where the
parent's `new` genuinely exists and genuinely must be reachable, so the bail is
earning its keep rather than being paranoid.

**Still open.** Native dispatch for these forms. Note the measurement rules out
the obvious cheap answer: teaching the emitted guard about named parameters wins
back **zero** files, because every currently-native file with a named-parameter
multi method also carries a `where`/`:D` constraint and bails anyway. Reproducing
`scoreCandidate` in generated C++ — smileys, `where` bodies, MRO deferral into
built-in parents — is the whole interpreter dispatcher.

The promising design is the one `MAIN` already uses: ship the **signatures** as
metadata and keep the **bodies** compiled. `mainSigBlob` serializes MAIN's
signatures (bodies detached, via the AstSerial module-cache serializer) into the
binary, and `RT.runCompiledMain` feeds them through the same `mainProtocol` the
interpreter uses. The same shape here — emit each candidate as a real `Callable`
carrying its `Param` list, register the group as a multi dispatcher — would let
the interpreter's own `scoreCandidate` and `parentNext` pick the candidate while
every body stays native code. That wins back all 21 files without a second
dispatcher to keep in sync with the first.

## Role BODY lexicals are shared across composers (2026-08-31)

Found while making `ML::TriesWithFrequencies` install (issue #53). A role's body
runs **once**, and every class that composes the role closes over that one pad;
Rakudo instantiates the body per composition, so each composer gets its own copy.

```raku
role R { my $x = 'ROOT'; method get { $x }; method set($v) { $x = $v } }
class A does R { }
class B does R { }
A.new.set('A!');
say A.new.get, " ", B.new.get;   # Rakudo: A! ROOT   ·   rakupp: A! A!
```

The same shows through the `my $.x` class-level accessor that issue #53 added
(`Parser::desugarDotDecl`), since the accessor reads exactly such a lexical:
after `C.rl = 'X'` on a class composing `role R { my Str $.rl = 'ROOT'; … }`,
Rakudo still answers `ROOT` for `R.rl` and rakupp answers `X`.

Nothing measured depends on it — the divergence needs two classes composing one
role *and* a mutated body lexical, and no distribution in the sweep does that —
which is why it is recorded rather than fixed. The fix is not local: the
ClassDecl path in `Interpreter.cpp` builds one `bodyEnv` per role declaration and
the composition loop copies the method `Value`s with their closures intact, so a
faithful version has to re-run (or clone) the body per composition and repoint
each composed `Callable`'s `closure` at the fresh env.

## An `INIT` block is hoisted only when nothing precedes it (2026-09-01)

`INIT` runs once before the mainline wherever it is written. rakupp hoists it
only when it is the first statement; written lower, it runs in place.

```raku
sleep 2;
my $manual = INIT now;
say ($*INIT-INSTANT - $manual).round(0.001);
```

rakupp `-2.011`, Rakudo `-0.076` — the whole preceding `sleep` sits inside the
gap, because the `INIT now` was evaluated where it stands rather than at program
start. With the `INIT` on the first line both engines answer ~0.

Not a regression: v3.23.0 answers `-2.011` too. Found while writing
`t/regression/datetime-timer-clock.raku`, whose `$*INIT-INSTANT` check has to sit
at the top of the file for exactly this reason — Roast's
`S28-named-variables/init-instant.t` never sees it because its `INIT` is on
line 7 with nothing slow above it.

## Loop control inside a regex `{ … }` block cannot reach the enclosing loop (2026-09-07)

A `{ … }` block in a regex is an internal reparse: it runs through `evalString`
with `mainlinePH` false, so a `last` in it becomes an X::ControlFlow error, and
`regexBlockErrorStaysQuiet` (Interpreter.cpp) then swallows that error on
purpose so the match can continue. The enclosing loop never learns anything
happened.

```raku
my $n = 0;
for ^3 { "abc" ~~ / a { last } /; $n++ }
say $n;      # rakupp 3 · Rakudo 0
```

The EVAL half of the same divergence was fixed on 2026-09-06 — a user-level
`EVAL q[last]` now hands the word to an enclosing loop instead of erroring
(`t/regression/eval-loop-control.raku`) — and the fix was deliberately scoped to
exclude this path. The gate for changing it is the CPS regex engine, not
`evalString`: letting a `LastEx` unwind out of a regex block means unwinding
through the matcher's cursor and memo state mid-match, and nothing today shows
that path is exception-safe — every exit from it returns. That is the work this
entry is holding, and `regexBlockErrorStaysQuiet` matches the exact message
`evalString` produces, so both halves of the decision sit in one place.

### The same word inside a `--exe` binary (2026-09-07)

The EVAL fix is interpreter-only, and not because of `evalString`: a compiled
program never arms `curLoopFrame`, because the codegen emits native C++ loops
rather than going through the interpreter's loop runner. So `ownedOutside` reads
false and the error comes back.

```raku
my $n = 0;
for ^3 { EVAL q[last]; $n++ }
say "n=$n";     # interpreted n=0 · --exe: dies "last without a supporting loop construct"
```

What is already in place: a compiled loop **does** catch a `LastEx` thrown from
a called routine — `sub g() { last }; for ^3 { g(); $n++ }` answers `n=0` both
interpreted and compiled — so the generated loop has the handler, and the one
missing piece is arming (and restoring) `curLoopFrame` around an emitted loop
body. That would reach only interpreter-executed code inside the loop, which is
precisely EVAL and regex blocks: the codegen turns a statically visible
`next`/`last` into C++ `break`/`continue` and never consults the sentinel.

## `END` phasers: what the fix reaches, and what it does not (2026-09-07)

`END` is registered where it is written and runs at program exit (issue #70,
`t/regression/end-phaser-in-sub.raku`). Four follow-ups closed the gaps this
entry used to hold open, in the same session:

- the scope a phaser runs in is captured on entry of **every** block that holds
  it, not only its own, so `sub f($n) { if $n == 1 { END say $n } }` called
  f(1), f(2) says 2 as Rakudo does — Rakudo flattens that `if` body into the
  routine's frame, and re-capturing at each enclosing entry is the same answer;
- an `END` in a block that never ran now runs **leniently** (`no strict`'s rule:
  a name that resolves nowhere reads as Any), because the containers Rakudo's
  compile-time pad would have carried never came to exist. It used to die with
  X::Undeclared and be swallowed whole by the catch-all every END body runs
  under, so `sub f($n) { my $x = $n * 2; END say "x=$x" }`, uncalled, printed
  nothing where Rakudo prints `x=`;
- a module's ENDs sort at the position of the `use` that loaded it, so they run
  **after** the mainline ENDs written below that `use` and before those written
  above it (`EndPhaser::key`). Both orders now agree with Rakudo: `use` at the
  top with an `END` below it — the usual layout — and File::Temp's `t/03` shape,
  where the `use` follows the file's own END and which
  `t/regression/open-modes-bind-end-shift.raku` pins;
- an EVAL's ENDs are keyed as registered when the EVAL **runs**, after every
  compiled one, because EVAL is not compile time: `sub f { EVAL q[END …] }`
  ends before a mainline `END` written after it.

The thread-safety lesson is worth keeping: the first cut of the last two kept
the per-unit ordering context on the interpreter. A `{ … }` block in a regex is
an EVAL, so `await ^30 .map: { start { S/.+/{$/.chars.print}/ … } }` had thirty
workers mutating one `std::unordered_map` — `t/regression/concurrent-match-scoping.raku`
aborted, silently and every time. That context lives in the thread-local
`ExecContext` now, and the shared registry is read either under its mutex or
through a per-`Callable` `PublishedOnce` cache.

### The same phasers under `--target=js` (2026-09-07)

The JS backend used to hoist an `END` body to the top level wherever it was
written, so a nested one was emitted OUT of the scope it was written in and a
reference to an enclosing lexical became a free variable:
`sub f($n) { END say "end-$n" }` died at exit with
`ReferenceError: v_n is not defined`. It now emits the body into a slot that
block entry assigns — the interpreter's shape — with the hoisted copy kept as
the fallback for a block that never ran, and sorts the exit hooks by the
phaser's source position (`Block::srcPos`), which its own emission order does
not give: a sub's body is emitted after the mainline that calls it.

Two residues, both smaller than the crash they replace. A body that reaches a
scope which never ran prints nothing where the interpreter (and Rakudo) print
the undefined value — JS has no lenient lookup to fall back on, and the guard
that keeps the ReferenceError from taking the program down at exit swallows the
body with it. And the capture is per block rather than per enclosing frame, so
the `if $n == 1` case above answers 1 there where the interpreter answers 2.

`--exe` needs nothing: it refuses to compile a nested `END` natively and bundles
the interpreter, which now gets all of this right.

## An allomorph is kept or shed the other way round by `floor`/`round`/`abs` (2026-09-07)

Rakudo KEEPS the allomorph where rakupp builds a fresh number, on exactly the
methods whose Rakudo implementation returns `self`:

```raku
my $i = IntStr.new(493, "0o755");   my $r = RatStr.new(1.5, "1.50");
say $i.floor.WHAT.^name, " ", $i.ceiling.WHAT.^name, " ", $i.round.WHAT.^name, " ", $r.abs.WHAT.^name;
```

Rakudo `IntStr IntStr IntStr RatStr` (`Int.floor` and `Real.abs` hand back
`self`, and for an allomorph `self` is the allomorph); rakupp `Int Int Int Rat`.

The opposite direction from the `.Numeric` gap fixed alongside this entry, and
the reason it is recorded rather than fixed with it: there it was clear which
answer is right, because the shed value is what `.Numeric` is FOR and the kept
one silently stringified as its original text. Here the divergence is an
artifact of Rakudo returning `self` from an identity operation, and rakupp's
fresh number is arguably the better answer — matching it would mean
reproducing an implementation detail, not a decision.

Roast pins the neighbouring case and rakupp already passes it: `S32-num/
rounders.t` asserts with `is-deeply` that the ARGUMENT form sheds —
`IntStr.new(42,"42").round(42)` is a plain `42`, and `.round(42e0)` a `42e0` —
which is what rakupp answers. It is the NO-argument identity form measured
above that nothing asserts, on either side. (That file's own six failures here
are `floor(NaN)`, `ceiling(Inf)` and their kin, unrelated to allomorphs and
unmoved by the `.Numeric` fix — 6 before, 6 after.)

Not a regression: identical on the v3.25.0 release build.

## A radix-prefixed string `mode` renders as `0o000` in the X::IO messages (2026-09-07)

`X::IO::Mkdir` and `X::IO::Chmod` compose their `.message` from their attributes
(the X::IO table in `src/MethodCallPart2.cpp`). The `mode` attribute goes through
`toInt()`, which does not parse a radix prefix, so a Str mode written `0o755`
numifies to 0:

```raku
X::IO::Mkdir.new(path => 'P', mode => "0o755", os-error => 'e').throw
```

    mode => 0o755   (Int)   Rakudo 0o755    rakupp 0o755     agree
    mode => "755"   (Str)   Rakudo 0o1363   rakupp 0o1363    agree
    mode => "0o755" (Str)   Rakudo 0o755    rakupp 0o000     diverge

Only the third row diverges, and the engine itself always passes an Int, so
nothing in live code reaches it — it needs a program that constructs an X::IO
exception by hand with a string mode, which in practice means a suite mocking
one. Recorded rather than fixed for that reason; the three rows above are the
whole of the measurement, so the next person need not repeat it.

Not a regression: identical on the v3.25.0 release build. Traced to e1679c3.
Surfaced when the END-exception report (issue #70) began printing exceptions
that had previously been swallowed, which made a hand-constructed X::IO::Mkdir
visible for the first time.

## `run`/`shell` took `:env` only as a bare Hash — FIXED

Found 2026-09-12, fixed the same day; `t/regression/run-env-adverb-shapes.raku`
holds the matrix. Rakudo takes `.hash` of whatever `:env` holds, so a Hash, a
Pair, a list of pairs and a hash followed by pairs are all legal spellings.
Only the bare Hash was read here. Every other shape fell past the branch that
reads it and was dropped in SILENCE — and a dropped `:env` is not an empty
environment, it is the parent's, so the child inherited everything.

```raku
# the child reports the probe and whether anything else came with it
my $code = 'print (%*ENV<PROBE> // "-") ~ "/" ~ (%*ENV<PATH>:exists ?? "inherited" !! "clean")';
run($*EXECUTABLE, '-e', $code, :out, :env(%*ENV, PROBE => 'M')).out.slurp(:close);
```

                                     Rakudo          rakupp was
    no :env                          -/inherited     -/inherited     agree
    :env(%h)                         M/clean         M/clean         agree
    :env(%*ENV, PROBE => 'M')        M/inherited     -/inherited     diverge
    :env(('PROBE', 'M'))             M/clean         -/inherited     diverge
    :env(PROBE => 'M')               M/clean         -/inherited     diverge
    shell(…, :env(%h))               M               none            diverge

The last row is the sharpest: `shell` did not parse `:env` at any shape. And the
third-from-last is the one that matters most — `:env(PROBE => 'M')` asks for a
clean environment holding one variable, and got the parent's whole environment
instead. A child meant to run isolated was not isolated, and nothing said so.

Found while testing that `-V`'s Camelia falls back to ASCII through a pipe: the
first version of that test added one variable with the `%*ENV, k => v` spelling
and the forced butterfly never appeared. `shell`'s `:cwd` was the same shape of
gap — accepted and ignored — and is fixed with it; see the next entry.

## `shell` accepted `:cwd` and ignored it — FIXED

Found and fixed 2026-09-12; `t/regression/shell-cwd-adverb.raku` holds the
rows. `run` has parsed `:cwd` since it was first reported, `shell` never did,
so the command ran in the calling process's directory — and a `:cwd` naming a
directory that does not exist ran it there anyway and exited 0. `spawnCapture`
already took the directory; `shell` passed the empty string.

                              Rakudo        rakupp was      now
    shell :cwd('/tmp')        /private/tmp  the caller's    /private/tmp
    shell :cwd('tools')       …/tools       the caller's    …/tools
    shell :cwd('/no/such')    did not run   RAN, exit 0     did not run

## A `:cwd` that does not exist: exit 126 here, -1 on Rakudo — open

Surfaced by the row above, and it is the same in `run`, so it is not something
`shell` acquired. Both engines refuse to run the command; they disagree on what
the Proc then reports.

```raku
run('pwd', :out, :cwd('/no/such/dir/here')).exitcode    # Rakudo -1, rakupp 126
```

Rakudo reports -1, its convention for a child that never started. rakupp lets
the child's own 126 through — the shell's "found but could not execute" — which
is a true statement about the child and a different statement from Rakudo's.
Telling them apart means distinguishing "the spawn failed" from "the child
exited 126" in the parent, which is why this is recorded rather than folded
into the `:cwd` fix. `t/regression/shell-cwd-adverb.raku` deliberately asserts
only that the command did not run, so it passes under both engines.
