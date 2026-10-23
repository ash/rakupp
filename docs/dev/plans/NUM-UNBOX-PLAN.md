# Plan: compiled float code without the box

*Written 2026-10-23, before any code. It answers the question
[FLOAT-HEAVY-ISSUES-2026-10-22.md](../findings/FLOAT-HEAVY-ISSUES-2026-10-22.md)
leaves open: can `--exe -O` run float-heavy Raku at the speed of the other
compiled languages, and of Fortran? It continues
[UNBOX-PLAN.md](UNBOX-PLAN.md) (its P3, "let a lane survive a call"), keeps
[KERNEL-PLAN.md](KERNEL-PLAN.md)'s contract, and brings
[PACKED-ARRAY-PLAN.md](PACKED-ARRAY-PLAN.md) to the native backend.*

## The answer, first

- Where `-O` already leaves the box, it is at C's speed. The F64 loop lane
  runs a dependent chain of float adds at 0.47 ns an iteration, the cycle
  count clang gets, and both print the same 17 digits.
- Where it does not, it is 6 to 190 times slower than C. That covers a call
  of a Num routine, an array element, and a small array built per call.
- The findings file measured what per-construct fast paths do: 2–5× on each
  construct moved the whole program 1.06×. The lever is the representation,
  not the dispatch.
- With P1–P3 below, the call rows and the large-array rows of the probe
  should land within about 3× of C, wherever the types are declared or the
  fixpoint can see them. The 10-element row keeps a guard on every call, and
  its target is under 10 ns. With P4, kernels written the Fortran way
  (preallocated `num` arrays, nothing built per call) should run
  Fortran-class.
- The findings program, as written, gets less. Its profile bounds P1–P4 at
  about 8× (see [What the program would get](#what-the-program-would-get)):
  about 29× CPython, against Rust/C++'s 55× and Fortran's 177×.

## The numbers a stranger can re-measure

```sh
cc -O2 -ffp-contract=off tools/num-unbox-probe.c -o /tmp/nupc
./build/rakupp --exe -O tools/num-unbox-probe.raku -o /tmp/nup
./build/rakupp --exe -O tools/num-unbox-probe-native.raku -o /tmp/nupn
for r in 1 2 3; do /tmp/nupc; /tmp/nup; /tmp/nupn; done
```

[tools/num-unbox-probe.raku](../../../tools/num-unbox-probe.raku) and
[tools/num-unbox-probe.c](../../../tools/num-unbox-probe.c) are row for row
the same kernels. Each prints its answer, and every answer below is identical
to the last digit between the two. The `my num @a` row is in
[a file of its own](../../../tools/num-unbox-probe-native.raku), because one
native array makes `--exe` bundle the whole program (issue 14), which would
have moved every other row.

Apple M4, macOS, 5.5.0-7-g8ea7dfc3 (only docs differ from 5.5.0-8), load
about 1.9. Three interleaved rounds, the best of 5 in each, then the best of
the three; ns per unit:

| row | C | `--exe -O` | gap | moved by |
|---|---:|---:|---:|---|
| control: the F64 loop lane, per iteration | 0.47 | 0.47 | 1.0× | — (UNBOX P1) |
| control: fib(27) on Int, per call | 0.48 | 1.13 | 2.4× | — (`planKernels`) |
| fib(27) on Num, per call | 0.78 | 10.15 | 13× | P1 |
| a leaf Num sub with a local, called from a loop | 1.83 | 17.76 | 9.7× | P1, P1b |
| a 1M-element Num Array read by index, per element | 0.49 | 14.53 | 30× | P3 |
| the same over a `my num @a` | 0.49 | 19.5 (bundled); 98 interpreted | 40× | P2, P3 |
| a 10-element Num Array summed, per call (issue 21) | 0.90 | 168.13 | 187× | P1 with P3 |
| a 3-element array built per call | 17.22 (malloc); 4.58 (the caller's buffer) | 101.05 | 5.9×; 22× | P4 |

Three things about the reference:

- **It is C, not Fortran.** gfortran is not on this machine. The C build
  passes `-ffp-contract=off`, the contract generated code keeps (KERNEL-PLAN,
  item 4): no multiply is fused into an add. gfortran contracts by default,
  so a Fortran build may run some kernels a few percent faster than this C
  does, and would print different bits on them. Parity here means parity
  with C held to the same rounding.
- **Clang hoisted pure calls out of the timing loop twice** while the probe
  was being written, and those rows read 0.00 ns. Inputs now arrive through
  volatiles and globals, and the 10-element row has a memory clobber.
- **The first run of the control row read C 0.74 against Raku++ 0.53.** That
  run landed on a slower core. Interleaved, both read 0.47 in every round,
  which is why every figure here is interleaved.

## Why the box costs what it does

A `Value` is 80 bytes (VALUE32-PLAN design A). `fib` on Int gets an int64
twin from `planKernels` in [Codegen.cpp](../../../src/Codegen.cpp):

```cpp
static int64_t k_u_fib(int64_t p0, __KR& __r) {
    if (__r.bail || --__r.depth < 0) { __r.bail = true; return 0; }
    ...
    return ((p0 < 2) ? p0 : __kAdd(k_u_fib(__kSub(p0, 1, __r), __r), k_u_fib(__kSub(p0, 2, __r), __r), __r));
}
static Value u_fib(Value v_sn) {
    ... if (__kb < 8 && __kInt(v_sn)) { __KR __kr; int64_t __kv = k_u_fib(v_sn.i, __kr);
                                        if (!__kr.bail) return Value::integer(__kv); ... }
    <the boxed body>
}
```

The same sub on Num has no twin, so every call runs the boxed body. That means
`itemizeScalar`, a `try` frame for `ReturnEx`, `rtLtB`, two `rtSub`s each
building a `Value`, two recursive results, and `rtAdd`. It costs 10 ns where C
spends 0.78.

The loop lanes have the same gap from the other side. UNBOX-PLAN's lane takes
a loop that calls nothing and reads no element. So in the probe only `scal`'s
loop gets one. `lerps` calls a sub, and `sum10` and `asum` read `@a[$i]`; each
iteration of those builds and tears down `Value`s:

```cpp
while ([&]{ ... rtLtB(v_si, v_sk); }()) {
    v_ss = rtNilToAny(rtAdd(v_ss, rtIndexGet(v_aa, v_si, false)));   // an 80-byte copy, per element
    <the ++ lane>
}
```

So the plan's four steps are the box's four homes in float code:

| step | the box's home |
|---|---|
| P1 | a routine's parameters and result |
| P2 | a native array in compiled code (today the program bundles instead) |
| P3 | an element read in a loop |
| P4 | a small array that never leaves its routine |

## P0. First, a wrong answer: the F64 lane divides by zero

```raku
sub f($x) { my $s = 0e0; my $i = 0; while $i < 3 { $s = $s + 1e0 / $x; $i++ }; $s }
my $r = f(0e0); say $r.^name; say (try { $r + 0 } // "died: $!");
```

- **Interpreted, and in Rakudo 2026.09:** dies, `Attempt to divide 1 by zero
  using /`.
- **Compiled, with `-O` and without:** prints `Num` and `Inf`.
- **The same division outside a loop** (`sub g($x) { 0e0 + 1e0 / $x }`)
  dies correctly compiled. Only the lane is wrong.

The lane emits `__u2 = 1.0 / __u3` with no test. UNBOX-PLAN P1 says why:
"`Num` needs no bail at all … division by zero gives `Inf`, which is what this
engine's boxed path already answers." That no longer holds. `rtSlash` leaves a
zero divisor to `applyArith`, which fails as Rakudo does, so the lane and the
boxed path now disagree.

Two reasons the gates did not see it:

- **`t/exe/run.raku` compares `-O` with plain `--exe`,** and both carry the
  lane. The lanes are on without `-O` too (`loopLanesByDefault`; only
  `RAKUPP_NO_KERNELS=1` turns them off). This is the class of bug UNBOX-PLAN's
  "The gate this needed and does not have" describes.
- **`t/exe/fuzz.raku`'s operator matrix has `0e0` only as a truth value,**
  never as a divisor.

**Done, 2026-10-23.** The fix turned up two more defects in how the lane
leaves:

- **The divide.** The lane's F64 `/`, binary and compound, bails when the
  divisor is `0.0` (`-0.0` and an Int 0 included). `/=` is refused by the
  type pass today, so its arm is a guard for the day it is not.
- **A bail with nowhere to land.** The entry snapshot and the landing pad
  were emitted only for a lane holding an I64 slot, on the reasoning that
  only integer overflow bails. A Num `**` that underflows bailed too, and so
  does the fixed `/`. In a lane of Num slots alone, a bail jumped to the
  label, fell through to the commit, and the loop counted as finished with
  the values it held at the bail:
  - `$t = $x ** ($k + 1e0)` with `$x = 1e-200` answered `Num 1e-200`, where
    Rakudo gives an `X::Numeric::Underflow` Failure. This was wrong before
    this work.
  - The first version of the fixed `/` answered the quotient before the
    zero.

  Every lane gets the pad now. Where nothing can bail, clang drops it as dead
  code.
- **`&&` and `||`.** Their right side's checks had been hoisted ahead of the
  whole condition. With a check on `/`, `$d != 0e0 && 1e0 / $d > …` would
  have left the lane on every zero `$d` it steps around, so those checks now
  run only when Raku evaluates the right side.
- **Prefix `-`.** Past int64, it bailed with a `break`, which left only the
  innermost loop: UNBOX-PLAN's bug 1, in the one place it survived. It is a
  `goto` now.

The gates for it:

- **`t/regression/exe-num-lane-bails.raku`** compiles its program with `-O`
  and without, and holds each build to Rakudo's 20 answers. The build before
  this fix fails 11 of them.
- **`t/exe/fuzz.raku`'s operator matrix** gains a zero divisor (`0`, `0e0`,
  `-0e0`) under four shapes: stored, accumulated, and guarded by `&&` and by
  `||`.

Every step below adds code that divides, and each one keeps the same rule: an
operation whose boxed answer is not a plain number bails, and every bail has
somewhere to land.

## P1. Num twins of routines

The design is to widen `planKernels` from closed int64 subs to subs whose
parameters, locals and result each have a lane type, I64 or F64, and to reuse
the loop lanes' machinery rather than write a second one.

### The twin

```cpp
static double f_u_lerp(double p0, double p1, double p2, __KR& __r);
```

- **Types.** Each parameter, local and the result gets its lane type from the
  fixpoint `uTypeOf` already runs for loops, seeded from:
  - declared types: `num`, `num64` and `Num` give F64; `int`, `int64` and
    `Int` give I64;
  - literals: `1e0` gives F64, `1` gives I64.

  An untyped parameter that is only ever read in a float context is F64, and
  takes an Int argument as well, converted on entry. This is the lanes'
  `dualRead` rule.
- **Vocabulary, v1.** What `kStmts` takes today (an expression statement,
  `return`, `if`/`elsif`/`else`, the ternary, comparisons, `&&`/`||` in a
  condition), plus:
  - `my` locals with a lane type;
  - loops the lane emitter takes, emitted inside the twin with no entry guard
    of their own, since the twin is the lane;
  - calls of other twins;
  - F64 `+ - * /`, unary minus, and the comparisons.

  The vocabulary does not include:
  - `min` and `max` on F64, which the lanes already refuse;
  - `%` on F64, also refused by the lanes;
  - `**`;
  - math functions. Each would be admitted one at a time, after checking that
    the boxed path calls the same libm function.
- **Entry.** As `u_fib` does today: the Value function tries the twin when
  every argument passes its guard (`rtNumSlot`, `__kInt`, or `__kIntN` for an
  `int`), and stops trying after eight bails (`__kb`).
- **Bail.** A twin bails on:
  - I64 overflow;
  - a zero divisor, for either type (P0's rule);
  - `__KR::depth`.

  A bail is an exact re-run (KERNEL-PLAN contract 2): a twin writes nothing
  outside itself, so the Value function starts from the beginning and gives
  the same answer and the same error it always did.
- **Result.** The wrapper rebuilds the Value the boxed body would have
  returned. For an untyped or `--> Num` sub that is `Value::number`. One case
  needs settling by a probe before any code: what the Value function of a
  `--> num` sub hands back, a plain Num or one carrying native tags
  (`rtNativeNumOf`). The twin has to return the same; the int twins settled
  the same question for `--> int`.
- **Parameter count.** `kernelParams` takes at most six. The findings
  program's dominant recursion has ten parameters, so a twin takes any
  number. Nothing in a twin builds a `ValueList`.
- **Refusals.** Unchanged from `planKernels`:
  - `wrap` anywhere in the program;
  - multis, protos, methods, traits, `is rw`, module exports;
  - a user-declared operator for any operator the body uses (`opDeclared`);
  - `RAKUPP_NO_KERNELS=1`.

  An `augment` adds methods, not operators, and operators are what a twin
  calls.
- **Rounding.** Twins are plain C++ expressions compiled under
  `-ffp-contract=off`, so every operation rounds on its own, as it does in the
  interpreter.

Measuring sticks: fib(27) on Num, 10.15 ns per call, should land within about
3× of C's 0.78, as the Int twin is within 2.4× of C. The leaf sub's row
measures the call cost itself.

### P1b. A lane may call a twin

UNBOX-PLAN's no-calls rule exists because a callee could observe a boxed
variable that is stale while the lane holds its live value in a register. A
twin cannot observe anything: it reads its parameters and its own locals.

- A lane may call a twin directly, with unboxed arguments.
- A bail inside the twin bails the lane, whose snapshot restore re-runs that
  iteration boxed.
- Clang then inlines the static function itself, which is UNBOX P3 by a
  shorter route than inlining at the AST.

Measuring stick: the leaf-sub row, 17.76 ns per call, against C's 1.83.

### P1.0. Before P1: the refusal census

After UNBOX P0–P2, the lane fired on one of 701 programs, because real loops
call things. This plan has the same risk, and a cheap way to price it
first:

- **The report.** A `-O` report, `RAKUPP_LANE_REPORT=1` at compile time, that
  prints each routine and loop, whether it got a twin or a lane, and if not,
  the first construct that refused it.
- **The corpus.** Run it over `examples/`, `t/regression/`, `tools/optbench/`
  and the findings program.
- **Reading it.** If the top refusals in the findings program's hot routines
  are `.push`, `.elems` and element reads (it has 215 method call sites),
  then P3's vocabulary has to land with P1, not after it.

## P2. Native arrays in compiled code

Today, compiled code refuses native arrays in two places:

- `refuseDeclTraits` refuses every typed `@` or `%` declaration ("a typed @
  variable (my num @a)"), and the whole program bundles.
- A `num @x` parameter refuses as a parameterized type.

Inside the bundle, the routines are native, but they reach the array through
runtime helpers that call `arr()`, and on a packed payload `arr()` unpacks.
`RAKUPP_PACKED_CENSUS=1` on the probe's bundle shows exactly that: one call
site, one unpack, a million elements. That is why the `my num @a` row
(19.5 ns) is slower than a plain Array (14.5).

The plan:

- **Declaration.** Emit `my num @a` and `my int @a`, full width only as in
  PACKED-ARRAY v1, through one runtime helper. The helper builds what the
  interpreter builds: the `ofType`, the strict list assignment and the packed
  payload. One helper, so the semantics are the interpreter's and not a
  second copy; that drift is what `rtNameTerm` exists to prevent.
- **Element reads and stores.** Go through helpers that use the words when the
  array is packed and the subscript is in range, as the interpreter's Index
  fast path and `Assign::simpleSlot` 7 do, and take the general path
  otherwise.
  - A store the words cannot hold unpacks and stores as today. That keeps the
    lax element stores (`@a[1] = "x"` into an `int` array stores the Str),
    which differ from Rakudo's but are this engine's answer in both modes.
- **`.elems`, `.push` and `for @a`.** Take the packed arms the interpreter
  already has.
- **Parameters.** A `num @x` parameter binds as the interpreter binds it.
- **Left for later.** `my Num @a`, a boxed typed array, stays refused. Its
  store checks are a separate piece of work.

Measuring stick: the `my num @a` row. Before P3, three things should hold:

- `--exe-info` says `"mode":"native"`;
- `RAKUPP_PACKED_CENSUS=1` shows no unpack;
- the time per element is at or below the plain Array's.

## P3. Element lanes

This is SPEED-BACKLOG #48 and OPTIMIZATION.md's "array-element lanes";
KERNEL-PLAN task 1 did the same for the interpreter's kernels.

- **What it takes.** `@a[EXPR]` read in a lane or a twin, where `@a` is an `@`
  variable or parameter and `EXPR` is I64.
- **Entry guard, per array:** a plain Array (`rtElemsPlain`'s test), packed
  `num`, packed `int` or boxed.
- **What the loop may not do to the array:** resize it (`push`, `pop`,
  `splice`, or a list assignment to `@a`), rebind it with `:=`, or call
  anything but twins.
- **Per read, packed:** a bounds check, then load the word.
- **Per read, boxed:** a bounds check, and the element must be a plain Num or
  Int, with no container and no tags. Anything else bails: a bound element
  (`@a[0] := $x`), a Rat, or a read past the end, whose answer is the array's
  default, not a number. The boxed path then answers.
- **Hoisting.** When the subscript is the loop counter and its bound is read
  once at entry (`$i < $k` with `$k = @a.elems`, and nothing resizes the
  array), one range check at entry replaces the check per read. On a packed
  array the loop is then a walk over a `const double*`:
  - clang can vectorize it where no reassociation is needed (a map);
  - it cannot vectorize a sum, whose IEEE adds are a dependent chain in C
    and in Fortran alike without `-ffast-math`.
- **A twin may take an `@` parameter,** passed by const reference and
  guarded at the twin's entry as above. The 10-element row needs this,
  because `sum10` is a sub called a million times.
- **Order.** Reads first. Writes come second, as P3b, with KERNEL-PLAN task
  1's undo log: a store made before a bail has to be taken back for the
  boxed re-run to be exact.
- **Also.** `for @a -> $x` as a lane source, as UNBOX P2 did for Ranges.

Measuring sticks:

| row | today | target | C |
|---|---:|---:|---:|
| 1M-element Num Array, per element | 14.53 ns | towards 0.5 | 0.49 |
| the same over `my num @a`, per element | 19.5 ns | towards 0.5 | 0.49 |
| 10-element Num Array summed, per call | 168 ns | under 10 | 0.90 |

The 10-element row keeps a twin's entry guard and a range check on every call.

## P4. Small arrays that never leave

`[$a + $b, $a - $b, $a * $b]` costs 101 ns. That is a `ValueList` allocation,
three 80-byte Values, the Array Value, reference counts and teardown. C takes
17 ns with `malloc` and 4.6 with the caller's buffer. The findings file names
this as the difference between Fortran and the rest: "Their versions allocate
nothing per call", and about 65% of the Rust version's profile was malloc,
free and formatting.

- **P4a, general and cheaper.** Allocate a small `ValueList` from SlabPool
  ([PAYLOAD-SLAB-PLAN.md](PAYLOAD-SLAB-PLAN.md)), and size an array literal's
  list exactly. Measure before promising: that plan's own lesson was an
  estimate three times too optimistic.
- **P4b, the Fortran lever: scalar replacement.** It applies when:
  - a twin returns an array literal of fixed length with F64 elements;
  - its caller is a twin or a lane;
  - the caller reads constant subscripts from the array and lets it go:
    binds it to a `my $v` that it never passes, returns, stores, rebinds or
    iterates.

  Then the twin returns a struct of doubles and nothing is allocated. A boxed
  caller still calls the Value function and gets a real Array. This is escape
  analysis within one routine, plus the callee's return shape.
- **Later.** `my @a` filled by a bounded number of `.push` calls inside a
  twin, as a fixed buffer. The census decides whether it is worth doing.

Measuring stick: the 3-element row, 101 ns, against the caller's-buffer
4.6.

## What the program would get

The findings file's compiled profile on 5.5.0-8 has 88% of samples in what
this plan addresses:

| share | what |
|---:|---|
| 31% | the generated functions' own box handling and tag checks |
| 25% | array machinery |
| 22% | boxed scalar operations |
| 8% | malloc, free and memmove |
| 2% | `methodCall` |

Even if all of that went, the other 12% bounds the program at about 8×: from
3.5× CPython to about 29×. That is about half of Rust and C++'s 55×, a quarter
of Go's 122×, and a sixth of Fortran's 177×. What the 12% is, the findings
file does not attribute; the exact decimal rounding on bigints is the obvious
candidate.

So for that program, as written and untyped:

- **P1–P4** are the way to roughly half of Rust's speed.
- **Rust and C++ speed** needs, beyond them, whatever its unattributed 12% is.
- **Fortran speed** needs the program written as the Fortran version is.

The native-typed copy is the better first subject, because its types are
declared rather than inferred. That makes the findings file's issue 23
relevant here:

- Every `.push` of a `my num` value, and every `.defined` on one, leaves the
  call-site answer for `methodCall`.
- `rtPushPlainArg` and `rtQueryPlain` refuse any value that carries native
  tags.
- Three `.push`es of a `my num $v` take 588 ns, against 56 untyped.

That is a small fix of its own, and worth making before P1 is measured on the
copy.

## Gates

1. **The oracle** is the same build with `RAKUPP_NO_KERNELS=1`, then Rakudo
   (KERNEL-PLAN contract 3). Twins and element lanes obey the variable as the
   int twins and the loop lanes do.
2. **`t/exe/fuzz.raku`, widened.** The operator matrix gets zero divisors
   (P0). A new twin matrix generates subs over Int, Num, untyped and native
   parameters, crossed with the vocabulary and with the call shapes:
   recursion, mutual recursion, and a lane calling a twin. It compares
   interpreted against `-O`. This is the gate that can see a bug both
   compiled builds share, which `t/exe/run.raku` cannot (P0 is one).
3. **Regression files** with the values Rakudo prints:
   - twin bails: overflow, zero divisor, depth;
   - element-lane bails: out of range, a bound element, a non-Num element;
   - native arrays under `--exe`: strict list assignment, lax element
     stores, `.push`, `for`.
4. **The standing gates:** `t/exe/run.raku`, `t/jit/run.raku` (kernels share
   the emitter), `t/run.raku`, Roast as the control, and `perf-guard
   --check`.
5. **OPTIMIZATION.md's showcase rows `nummath` and `arrayidx`,** which were
   added to measure exactly these levers.
6. **The probe in this file,** re-run, with every row's answer identical to
   C's.

## What would falsify this plan

- **The census finds the hot routines refused by something P1–P3 do not
  take:** strings, hashes, method calls on objects. Then the plan fires on
  nothing, as UNBOX's lane did on 700 of 701 programs, and the census's top
  refusal is the next step instead of P1.
- **The Num twin lands far from C.** The Int twin is 2.4× C on fib: an
  overflow check per operation, the depth counter, and the bail flag. If the
  Num twin is not within about 3×, the guard and bail scheme is the cost, and
  a twin needs a cheaper exit. For example, a non-recursive twin needs no
  depth counter, and a Num-only twin with no division needs no bail at all.
- **Native arrays unpack on the program's first statements.** This is
  PACKED-ARRAY-PLAN's own falsifier, now in compiled code.
  `RAKUPP_PACKED_CENSUS=1` on the program decides it.
- **The findings program moves much less than its profile predicts** after
  P1–P3, say under 3×. Then the profile's categories are not what its hot
  routines are made of, and the projection above is wrong.

## Phases

| | | |
|---|---|---|
| **P0** | the F64 lane's zero divisor: a wrong answer, with and without `-O` | **DONE** |
| **P1.0** | the refusal census, `RAKUPP_LANE_REPORT=1` | before P1 |
| **P1** | Num and mixed twins of routines | |
| **P1b** | a lane may call a twin | |
| **P2** | `my num @a` / `my int @a` compiled natively, on the packed payload | |
| **P3** | element lanes; twins with `@` parameters; reads, then writes with an undo log | |
| **P4** | small arrays that never leave: the slab first, then scalar replacement | |
| **P5** | the findings program re-measured against CPython, beside Rust, C++, Go and Fortran | |
