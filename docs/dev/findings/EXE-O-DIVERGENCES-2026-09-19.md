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

## The worst one: a nested loop that redeclares a name shares one C++ variable

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

The fix belongs in the hoist: a declaration that shadows a name already hoisted
in an enclosing scope needs a distinct C++ name and a scope to live in. The
unboxed lanes refuse to lane a loop whose header redeclares a name the lane
already holds, which is the same rule one level down, but that only protects the
lane — the boxed emission underneath it is what is wrong.

## And one interpreter divergence from Rakudo, also pre-existing

`1e0 / 0e0` answers `Inf` here — in the interpreter, compiled, and with or
without `-O` — where Rakudo throws `X::Numeric::DivideByZero`. It shows up with
no optimizer lane anywhere near it, so it belongs to the numeric tower rather
than to codegen. Noted because a floating-point lane is exactly the place
someone will later assume this was introduced.
