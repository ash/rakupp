# Plan: `--types` — a variable keeps its type, and is stored as a native value

*Written 2026-09-29, before any code. It started as a question from the user:
could a pragma make a program's variables native and keep their types fixed,
so that `my Str $x`, or just `my $x = 's'`, stays a string, and a variable
that holds an `Int`, `Num` or `Rat` computes at native speed, without a
separate engine and without `Value` in the variable's storage. The design
went through a pragma and then a four-grade option. It settled on one
contract: code run with `--types` follows the rule below, and the engine
stores its variables as native values because of that.*

**The number a stranger can re-measure** shows where the engine stands with
the native types Raku already has. Declaring a variable native makes it
slower:

```bash
cat > /tmp/native.raku <<'EOF'
my int $i = 0; my num $s = 0e0;
my $t = now;
while $i < 3_000_000 { $s = $s + $i * 0.5e0; $i = $i + 1 }
say "native: ", (now - $t).round(0.01), " $s";
my $j = 0; my $u = 0e0; $t = now;
while $j < 3_000_000 { $u = $u + $j * 0.5e0; $j = $j + 1 }
say "plain:  ", (now - $t).round(0.01), " $u";
EOF
build/rakupp /tmp/native.raku
build/rakupp --cnp=stats /tmp/native.raku
rakudo /tmp/native.raku
```

| 3M-iteration loop | `my int` / `my num` | `my $` |
|---|---:|---:|
| rakupp, interpreted | 0.76 s | 0.70 s |
| rakupp `--cnp` | **0.79 s** (no kernel) | **0.02 s** |
| Rakudo 2026.09 | 0.70 s | 3.36 s |

Measured 2026-09-29 on Darwin 27.0, arm64, `build-arm64` at 5.0.1-4, on a
working machine with other sessions running. Read the ratios, not the
seconds. `--cnp=stats` reports two loops examined, both compiled, and one
kernel entered: the declared-native loop is refused at entry, and that
refusal is the whole 40×. In the interpreter, `my int` buys nothing, because
`natBits` only makes assignment wrap. The value is still a full `Value`.

---

## What the option is

```
rakupp --types prog.raku            # the contract: types stick, the engine stores natives
rakupp --types=check prog.raku      # report every place the code breaks the rule; change nothing
RAKUPP_TYPES=on | check | off       # the same setting through the environment
```

| value | a variable whose type changes | variables stored as | output compared with no option |
|---|---|---|---|
| `off` | as today (the default) | `Value` | — |
| `on` (bare `--types`) | dies with `X::TypeCheck::Assignment` | native values | identical, unless it dies |
| `check` | reported once on stderr, and allowed | `Value` | identical stdout |

`check` is how code gets ready for `on`: it lists every violation in one run
without stopping at the first.

**The flag and the variable are one setting.** `--types=VALUE` writes
`RAKUPP_TYPES=VALUE` into the process environment, and bare `--types` writes
`on`. The engine reads only the variable. This is how `--color` already
works: it sets `RAKUPP_COLOR` ([src/main.cpp](../../../src/main.cpp)). It
follows that:

- the flag wins over a variable already set;
- a bad value fails at start-up with one message, whichever way it was given;
- a child `rakupp` started through `run` or `Proc::Async` inherits the
  setting. A program that does not want that clears the variable in the
  child's environment.

**It covers every file the program runs:** the main program and every `use`d
module. A module that reuses a variable for a different type dies under
`on`. Phase N3 measures how often that happens in real modules before any
escape for modules is designed.

---

## The rule

Three sentences, then an example:

1. **A declared type is kept as written.** `my Int $x` holds Ints, exactly as
   in plain Raku.
2. **Without a declared type, the first value that is not `Nil` fixes the
   type.** That value can come in the declaration or in a later assignment.
3. **`Nil` empties the variable, as it always has, but does not change its
   type.**

With `--types`:

```raku
my $a = 5;         # $a gets its type from its first value: from now on it holds Ints
$a = 7;            # fine
$a = 'seven';      # dies: $a holds Ints
$a = Nil;          # fine: $a is empty again (Any), but it still holds Ints
$a = 'eight';      # dies
$a = 8;            # fine

my $b;             # no value yet, so no type yet
$b = 'x';          # the first value fixes it: $b holds Strs
$b = 1;            # dies

my Int $c = 5;     # a declared type is used as written, as in plain Raku
my Any $d = 5;     # declaring Any says "this variable may hold anything"
$d = 'five';       # fine
```

With `--types=check` the lines that die here print a report instead and run
as they do today.

**Numbers keep their Raku meaning. This is a user rule.** `0` is an `Int`,
`0e0` a `Num`, and `0.0`, `0/1` and `<1/3>` are `Rat`s. `0.1 + 0.2` is `0.3`,
and `0.1 + 0.2 == 0.3` is `True`, with the option and without it. An
inferred `Int` never wraps: it grows into a big integer as it always has,
and only a declared `int` wraps, exactly as in Rakudo. The option never
reads a decimal literal as a float and never makes `/` produce a `Num`.

**Letting a variable vary.** A variable that is meant to hold different
types declares a broad type: `my Any $x`, `my Cool $x` or `my Mu $x`. These
are ordinary Raku declarations with the same meaning on every engine. No new
syntax is needed.

**The fixed type is not a declared type.** `my $x = 5` under `--types` must
not behave like `my Int $x = 5` in anything visible:

- `$x.VAR.of` stays `Mu`;
- `$x = Nil` gives `Any`, not `(Int)`;
- error messages name the value's type as Rakudo's would.

So the fixed type lives beside the declared-type machinery, not in it:
`enforceTypedAssign` consults both. Only the refusal is shared.

**v1 covers `my` and `our` scalars.** Attributes, array and hash elements,
and parameters come later. They are also where declared types are not
checked today: `.=`, typed attributes, typed elements and typed `is copy`
parameters all store a wrong-typed value (see *Known gaps*).

---

## Native storage, and where `Value` remains

Under `on`, a scalar's storage follows its fixed type:

| the variable's type | stored as | note |
|---|---|---|
| `Int` | `long long` | on overflow the slot switches to a big integer. The *type* is still `Int`; only the representation grows, so the rule is not broken |
| `Num` | `double` | exact: a `Num` is a double |
| `Rat` | two `long long`s, numerator and denominator | falls back to a full `Rat` on overflow, never through a `double`, so `0.1 + 0.2` stays `3/10` |
| `Bool` | `bool` | |
| `Str` | the string itself, without the `Value` around it | a small saving; a `Str` is already a string |

Each slot also records whether it is defined, because `$a = Nil` leaves an
`Int` variable empty, and reading an empty one must behave as it does
today.

**Writes** check the type once and store the raw value. That check is the
only one: a wrong type dies there.

**Arithmetic reads the raw values directly.** This needs typed expression
evaluation in the interpreter: `$zr*$zr - $zi*$zi + $cr` computes on
`double`s from the slots to the result, with no `Value` at any step. Native
storage and typed evaluation must land together. Raw slots read through
today's evaluator would be boxed on every read, which is slower than now.

**A `Value` is built only at the boundary:**

- passing the variable to a sub without typed parameters, or to a builtin;
- `say $x`, string interpolation, method calls;
- storing it into an ordinary array or hash. A `my num @a` could later hold
  raw values too.

**Why this is sound here and was not in NATIVE-MATH-PLAN.**
[NATIVE-MATH-PLAN.md](NATIVE-MATH-PLAN.md) rejected typed variable storage
in the interpreter for two reasons. A pad slot is addressable (`MY::`,
`callframe`, lexical `EVAL`), and a *guessed* type can be broken by a write
no analysis saw, which obliges a deopt path, in effect a JIT. Under `on` the
type is not guessed: every write path checks it, including the ones reached
by name. The only representation change left is the `Int` that outgrows 64
bits, and that is a flag on the slot, not a deopt.

**The hard parts, in the order they have to be solved:**

1. **Closures** that capture the variable read and write the same raw slot.
2. **`is rw` parameters and `:=` binding** need a typed reference: a pointer
   to the raw slot and its kind.
3. **`MY::<$x>`, `EVAL`, `callframe`** reach variables by name, through an
   accessor that boxes on read and checks and unboxes on write.
4. **`$x.VAR`** produces a container object that fronts the raw slot.

**Compiled code gains the most.** The `--cnp` and `--exe -O` lanes
([UNBOX-PLAN.md](UNBOX-PLAN.md)) today check each variable's type when a
loop starts, copy it into a register, and write it back when the loop ends.
Over raw slots there is nothing to check and nothing to copy, and a lane no
longer has to end at a call just because the callee might see a stale
`Value`.

---

## Phases

| | | |
|---|---|---|
| **N0** | the probe: price native slots plus typed evaluation against `Value` slots, before any engine change | **DONE** — native storage adds 1.00×; see below |
| **N1** | the switch: `--types`, `RAKUPP_TYPES`, `on` / `check` / `off`, one parser, `--help` | |
| **N2** | the rule, checked, on today's `Value` storage: the fixed type, every scalar write path, the messages, `check`'s report | |
| **N3** | measure: Roast and the module battery with `--types` | |
| **N4** | native storage in the interpreter for `Int`, `Num` and `Bool`, with typed expression evaluation and the four hard parts | **dropped as a speed phase** by N0: typed evaluation (with guards, outside the option) carries the whole gain |
| **N5** | the compiled lanes over native slots: no entry checks, no copies, lanes that continue past calls | **v1 landed for `--cnp`**: kernels may call routines (see below) |
| **N6** | `Rat` stored as two integers | |
| **N7** | `Str` without the `Value` around it, if N0's numbers say it is worth having | |

### N0 — the probe

Build it the way UNBOX-PLAN priced its lanes: a C++ probe against the static
libraries, with no engine change. It runs the Mandelbrot kernel from
NATIVE-MATH-PLAN (`Num` literals throughout) and a 50M-iteration integer
loop three ways:

- `Value` slots, as today;
- native slots read through the current evaluator, boxing on each read;
- native slots with typed evaluation.

The second row exists to confirm that native storage alone is a loss, and
that typed evaluation has to come with it. If the third row's gain in the
interpreter is small, the order changes: N5 goes before N4.

### N0 — done 2026-09-29: native storage is worth nothing on its own

[`tools/types-n0-probe.cpp`](../../../tools/types-n0-probe.cpp) is a small
tree-walker over a hand-built tree of the Mandelbrot kernel
(`tools/bench/types/mandel-*.raku`), evaluated five ways, each printing the
kernel's checksum. Best of 3, two runs that agreed to within 3%, on a
working machine (load 2–3):

| | | 300×260 |
|---|---|---:|
| A | the real interpreter, `mandel-plain.raku` | 1,740–1,780 ms |
| B | walker: Value slots, arithmetic through `applyArith` | 325 ms |
| C | walker: Value slots, typed evaluation, a tag check at every leaf | 68 ms |
| D | walker: native slots, typed evaluation, no checks | 68 ms |
| E | walker: native slots, read back through Values | 290 ms |
| F | the loop written by hand in C++ | 1.9 ms |

```bash
c++ -std=c++17 -O2 -w -Isrc -Iinclude tools/types-n0-probe.cpp \
    build/librakupp_{rt,parse,ucd_names,ucd_coll,ucd_props,stubs}.a -liconv -o /tmp/n0 && /tmp/n0
```

What it decides:

- **Native storage adds nothing over guarded typed evaluation: D over C is
  1.00×.** A well-predicted tag compare at each leaf costs as much as not
  having one. The four hard parts of N4 (closures, `is rw`, `MY::`/`EVAL`,
  `.VAR`) would buy no speed. N4 is **dropped as a speed phase**. Under
  `--types` a variable stays a `Value`, and what the contract buys is
  correctness and diagnostics, plus the guarantee the compiled tiers can use.
- **Typed evaluation is the win, and needs no contract:** 4.8× on the
  arithmetic (B to C). That is NATIVE-MATH phase 4, ordinary engine work
  with guards at the leaves, and it moves to "outside the option" below.
- **But arithmetic is only a fifth of the real interpreter's time.** The
  walker's Value arithmetic is 325 ms of the interpreter's ~1,760, so typed
  evaluation can take the interpreter from about 1.76 s to about 1.5 s,
  around 15%. That agrees with NATIVE-MATH-PLAN's own revisit. The other
  ~1.4 s is statement and loop machinery, which no type work touches.
- **E is not slower than B** (1.1× faster), contrary to what this plan
  expected. Boxing on every read costs little next to `applyArith`'s
  dispatch. It is still no gain.
- **The compiled tiers are where the speed is.** `--cnp` runs the same
  kernel in 25 ms, against 68 ms for the best a tree-walker managed here, and
  the floor is 1.9 ms. So the next phases are the compiled ones: N5 (lanes
  and kernels that continue past calls, where the contract's guarantee helps)
  ahead of anything in the interpreter.

### N1 — the switch

- Add `--types` to the option table in `src/main.cpp`, next to `--color`.
- One function parses `RAKUPP_TYPES`. An unknown value fails at start-up
  and lists `on`, `check` and `off`.
- **The precompilation cache stays independent of the setting.** It caches
  the parse ([docs/guide/CACHING.md](../../guide/CACHING.md)), and the option
  acts on the parsed tree at run time. If a later phase needs the setting at
  parse time, it must go into the cache key in the same change.
- Tests: the flag and the variable give the same result; the flag wins; a
  child process inherits the setting; a bad value is refused both ways.

### N2 — the rule, checked

Correctness first, on the storage that exists. After N2, `--types` and
`--types=check` do everything the user can see, only not yet faster.

- **Where it hooks in.** The path that checks declared types, which since
  `a7e6e56d` includes compound assignment: `compoundCheckSlot` and
  `enforceTypedAssign` in [src/Interpreter.cpp](../../../src/Interpreter.cpp).
- **Write paths to cover**, each with a test:
  - plain `=` (both the simple-assign lane and the full path)
  - compound `op=`
  - `++` and `--`
  - `:=`
  - list assignment `($a, $b) = …`
  - `s///` on a variable
  - `.=`
  - `is rw` write-through
  - `temp` and `let` restores
  - `EVAL` and `MY::<$x>` stores
  - hyper and zip assignment
- **`check`** reports once per declaration and type pair: the variable, both
  types, where it was declared and where it changed. After that the variable
  is treated as varying, so a loop does not flood stderr.
- **Messages** use `X::TypeCheck::Assignment`, the class Rakudo raises for a
  declared type, with our own wording where no test asserts Rakudo's.

### N3 — how much real code already follows the rule

Run the fully-passing Roast files and the module battery's own test suites
with `--types=check`, and record every report and why it happened: a Raku
idiom, a deliberate reuse, or an engine bug. That decides whether `use`d
modules need an escape from `on`, and which idioms need a better answer than
"declare `Any`".

### N4 — native storage in the interpreter

The storage table above, for `Int`, `Num` and `Bool`, with typed expression
evaluation and the four hard parts. `Value` appears only at the boundaries
listed. Measure it on the Mandelbrot kernel, never on `loopsum`:
NATIVE-MATH-PLAN found that `$s = $s + $i` is already served by `fastShape`
and gains almost nothing.

### N5 — compiled lanes over native slots

The lanes drop their entry checks and copies, and continue past calls.
UNBOX-PLAN found 1 of 701 loops lane-eligible, because any call ends a lane.

### N5 v1 — landed for `--cnp`, 2026-09-29: kernels with calls in them

A copy-and-patch kernel may now contain a call to a named routine with
positional arguments, `say` and `sqrt` included. It still refuses a call
through a code value, named arguments, methods, and the routines that act on
their caller (`EVAL`, `callsame`, `return`, `take`, `temp`, …).

- **Around each call** the kernel writes its variables back to their
  containers (keeping a native's tags) and reloads them after, so a callee
  that closes over a loop variable sees it live and may change it, even to
  another type: registers carry their type at run time.
- **The call itself** is the interpreter's. The arguments the kernel
  evaluated are bound in a scratch scope, and the call, rewritten to read
  them, goes through `evalCall`, so lookup, multi dispatch and the special
  forms are unchanged. A builtin the program has not shadowed goes straight
  to its function (`rtCallB`, the route `--exe` calls builtins by).
- **`last` / `next` raised in a callee** leave or continue the innermost
  loop, as Rakudo's do (checked: `sub f($i) { last if $i == 2 }` ends the
  caller's loop). The helper reports them in the frame, and a `jctl` stencil
  after the call jumps.
- **A callee with an `is rw` or `is raw` parameter** (any candidate, for a
  multi) keeps the loop interpreted: the kernel passes values, not
  containers. This is checked at every kernel entry.

Measured on a working machine: a loop calling `sqrt` went from 0.34 s to
0.11 s (3×), and a loop calling a small user sub from 0.48 s to 0.37 s
(1.3×), since that call still runs the interpreter's full call path. Making
user calls cheaper means compiling the callee too, which is N5's next step.
The `--jit` backend still refuses calls.

Gates: `t/jit/run.raku --cnp` over its cases, `t/regression` and `examples`
agrees on 807 programs. One program printed nothing in the PLAIN run once,
under load, and passes both ways on its own. `t/jit/cases/calls.raku` holds
the call cases.

### N6 — `Rat`

`0.1 + 0.2` must stay exactly `3/10`, so the slot keeps both parts as
integers and uses the `gcd` on every operation. It is slower than a `double`
and should be far faster than today's boxed `Rat`. The number to report is
the Parrot-era `examples/mandel.raku`, which is written with decimal
literals, so every lane so far leaves it on the boxed path.

---

## Outside the option: ordinary engine work

Three improvements need no promise from the programmer, because they never
change what a program prints. They land in the default engine, gated by the
usual differentials like any other performance work, and the option builds
on them:

- **Declared natives get lanes** (UNBOX P4, NATIVE-MATH phase 3). This is the
  0.79 s against 0.02 s at the top of this file, and it comes first.
- **Typed intermediates with a check at each use** (NATIVE-MATH phase 4):
  the same typed evaluation N4 uses, with the leaf reading a `Value` and
  checking its tag instead of reading a native slot. N4 then drops the
  checks.
- **Lanes past calls** for a variable that no closure captures and no `EVAL`,
  `MY::` or `CALLER::` names (UNBOX P3). A call cannot see such a variable
  whatever its type.

---

## The gates

The standing ones, run for every phase (Roast after every change to `src/`
or `tools/`), plus:

- **The types differential.** It runs the corpus in `t/`, the fully-passing
  Roast files, `examples/` and the battery suites three ways.
  - `--types=check`: stdout must be byte-identical to a run without the
    option.
  - `--types`: each program either prints identically or dies with
    `X::TypeCheck::Assignment`, and each death is listed.

  The same program run with and without the option is the oracle. Rakudo is
  the oracle for the run without it, which is what Roast already checks.
- **`0.1 + 0.2` with and without the option, in every run mode**
  (interpreter, `--cnp`, `--exe`, `--exe -O`): it prints `0.3`, and
  `== 0.3` is `True`.
- **`perf-guard --check`** on every phase that touches an assignment path. A
  program run without the option must not pay for it. The compound-assignment
  check this plan builds on cost 5–6.5% on typed `+=` loops until its
  verdict was made decide-once.

---

## Known gaps this plan inherits

Declared types are not checked today in these places, and a fixed type
would not be either. Each stores a value its container refuses in Rakudo
(checked against Rakudo 2026.09):

- `my Int $x = 5; $x .= Str`
- a typed attribute: `has Int $.x is rw`, then `$o.x /= 2`
- a typed element: `my Int @a = 4; @a[0] /= 2`, and the same for hashes
- a typed `is copy` parameter, where even plain `=` stores
  (`sub f(Int $p is copy) { $p = "x" }`)

These are bugs today with or without `--types`, and they join N2's
write-path list once v1 grows past scalars.

A related difference is being fixed separately: `my num $a = 1/2` and
`my int $c = 7/2` convert silently here, where Rakudo dies at run time
because a `Rat` cannot be unboxed into a native.

---

## Decisions already made, and not to reopen

- **A command-line option, not a pragma.** A pragma may come later as a
  per-file opt-in on the same machinery.
- **One contract, not grades.** `--types` means the code follows the rule;
  `check` reports without enforcing. The earlier `speed` grade became
  ordinary engine work, because it never changed output and so needs no
  flag.
- **Under `on`, variables are stored as native values.** `Value` remains
  the type the engine uses between its parts, and is built only at the
  boundaries listed above.
- **Numbers keep their Raku meaning.** Considered and rejected: reading
  decimals as `Num`, and `/` on integers giving a `Num`.
  `0.1 + 0.2 == 0.3` holds everywhere.
- **An inferred `Int` grows; only a declared native wraps.**
- **The option covers every file**, the main program and `use`d modules.
- **The working name is `--types`.**
