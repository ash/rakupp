# What the new `--exe` optimizer differential found on its first full run

*2026-09-19. `t/exe/run.raku` was written for
[UNBOX-PLAN.md](../plans/UNBOX-PLAN.md) and compares a program compiled **with
`-O`** against the **same program compiled without it**. Everything below is
pre-existing: none of these files emits a single unboxed loop lane, and each one
also fails under a build that has none of that pass in it.*

Over all 702 programs of `t/regression/` and `examples/`: 666 agreeing, **4
differing**, 27 refused by the backend (it fell back to bundling, which is not a
failure), 5 skipped as nondeterministic. The four are below.

Reproduce any of them with:

```bash
build/rakupp --exe    -q t/regression/FILE.raku -o /tmp/plain && /tmp/plain
build/rakupp --exe -O -q t/regression/FILE.raku -o /tmp/opt   && /tmp/opt
```

## The four, and what they have in common

| file | what diverges |
|---|---|
| `code-accepts-and-regex-callable.raku` | `smartmatch against a block calls it` — `False` under `-O`, `True` without |
| `definiteness-types-and-runtime-classes.raku` | a `:D` / `:U` definiteness question |
| `failure-is-concrete-for-smiley.raku` | whether a `Failure` is concrete for a smiley |
| `pair-and-list-are-immutable.raku` | reading a `Pair` by its key answers `Nil`, and assigning into a literal `Pair` fails to throw `X::Assignment::RO` |

**None of these has been diagnosed** — they are reported here as reproductions,
not as explanations, and the cause is the next person's job. What can be said is
what they have in common: all four sit around **definiteness smileys, smartmatch,
and what a routine parameter's constraint does**, which is enough to suspect one
cause rather than four.

`-O` is supposed to be semantics-preserving. Each of these is therefore a wrong
answer rather than a slow one, and it appears only in a compiled binary built
with a flag most people never pass — which is why none of them had been seen.

## Why they were not found before

There was no gate on this axis. `t/run.raku` compiles a handful of programs with
`--exe` and checks their output; nothing compiled every program twice and
compared the two binaries. An optimizer pass is a second implementation of
something the runtime already does, so the only test that can catch it
disagreeing is one that runs both.

The first draft of the gate compared compiled output against **interpreted**
output and produced 122 failures, essentially none of them the optimizer's: a
compiled binary is its own `$*EXECUTABLE`, its `$*PROGRAM` is itself, and a
program that asks anything about how it is running legitimately answers
differently. Comparing the two compiled lanes against each other cancels all of
that and leaves exactly the optimizer. That is the axis the gate settled on, and
these four are what it found once it was pointed the right way.

The last one has an extra wrinkle worth recording: `--exe` without `-O` REFUSES
that program (`a :D constraint on a routine parameter — not yet natively
compiled`) and bundles the interpreter, where it passes. With `-O` it does not
refuse, compiles, and answers wrongly. So `-O` is not only optimizing here, it
is changing which programs the backend is willing to compile at all — and the
fallback is what was keeping the answer right.

## Also found, and not an optimizer bug

`my int8` is ignored by the native backend entirely. 300 increments of a
`my int8` answer **44** interpreted and under Rakudo — wrapping at eight bits,
as the type says — and **300** compiled, with and without `-O`. Native
container widths are not honoured anywhere in `Codegen`. This is
NATIVE-MATH-PLAN phase 3's territory and a prerequisite for ever laning a
declared native.

## The worst one: a nested loop that redeclares a name shared one C++ variable

**FIXED 2026-09-19**, same day, in `Codegen::block`: the records of which names
already have a C++ variable — `hoisted` for expression-position declarations and
`topVars_`/`atTopLevel_` for top-level ones — are now per C++ SCOPE rather than
per function body, so an inner declaration emits its own variable and shadows the
outer exactly as Raku scopes it. `t/regression/loop-header-my-shadows.raku`
pins it, and `t/exe/fuzz.raku` now generates the same-name nested variant of
every loop shape alongside the distinct-name one. What follows is what it cost
while it was live.

### The report

Found by enumerating loop shapes while building the unboxed lanes, not by any
gate — and the gate above **cannot** find it, because both compiled lanes are
wrong in the same way and they are only compared with each other.

```raku
my $t = 0;
loop (my $i = 0; $i < 3; $i++) { loop (my $i = 0; $i < 3; $i++) { $t = $t + 1 } }
say $t;
```

| | |
|---|---|
| interpreted | **9** |
| Rakudo | **9** |
| `--exe` | **3** |
| `--exe -O` | **3** |

A `my` in a C-style loop's header is hoisted to the enclosing C++ scope, and the
hoist is keyed by NAME — so the inner `my $i` does not get a C++ variable of its
own, it reuses the outer one. The inner loop's init then resets the outer
counter, the outer loop runs exactly once, and the answer is silently a third of
the right one. It scales with nesting depth and is worse the more iterations the
outer loop should have run.

This is a **silent wrong answer in the default compile**, with no diagnostic and
nothing in the output to suggest it. Shadowing a loop variable is not exotic —
`$i` inside `$i` is what a person writes when the two loops were written at
different times.

The fix turned out not to need a distinct C++ name at all. The inner
declaration is emitted inside the outer loop body's own braces, so plain C++
shadowing gives precisely Raku's scoping — an inner `my` that shadows within the
body, an outer that survives the loop. All that was wrong was the bookkeeping:
`hoisted` answered "does a C++ variable for this name exist anywhere in this
function body" where every one of its five readers wanted "…in the current
scope". Scoping that set to `block()` was the whole change.

(The unboxed lanes already refused to lane a loop whose header redeclares a name
the lane holds, which is the same rule one level down — but that only ever
protected the lane, and the boxed emission underneath it was what was wrong.)

### And the same defect in the other set, found while checking the fix

`topVars_` answers the identical question about top-level `my` variables, which
become C++ globals, and `atTopLevel_` stayed true through every nested block of
the mainline. So a `my` inside any block that happened to share a name with a
top-level one assigned the GLOBAL rather than declaring a local:

```raku
my $j = 7;
{ my $j = 100; say "inner $j" }   # inner 100 — correct
say "outer $j";                   # 7 interpreted and under Rakudo, 100 compiled
```

**The JavaScript backend does not have either bug.** `--target=js` answers 9 on
the loop repro, because it emits `let`, which JavaScript scopes to the block —
so Raku's scoping comes for free there. Both bugs were the C++ backend's alone,
where block scoping has to be modelled by hand and the model was one level too
coarse. That is worth knowing before the next backend: this is a class of defect
a language with block-scoped declarations cannot have and a hand-rolled emitter
into C++ has to earn.

Worse than the loop case in one way, because it needs no loop and no nesting
beyond a single block — `for 1 .. 3 { my $i = $_ * 10; … }` in a program that
also has a top-level `$i` writes the program's own variable. Fixed in the same
place, by clearing `atTopLevel_` at a block boundary, and pinned by the same
regression file.

## And one interpreter divergence from Rakudo, also pre-existing

`1e0 / 0e0` answers `Inf` here — in the interpreter, compiled, and with or
without `-O` — where Rakudo throws `X::Numeric::DivideByZero`. It shows up with
no optimizer lane anywhere near it, so it belongs to the numeric tower rather
than to codegen. Noted because a floating-point lane is exactly the place
someone will later assume this was introduced.
