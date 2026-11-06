# Plan: inferred native types — one set of facts for every engine

*Written 2026-11-06, before any code: a note for later, not scheduled. It
started with `--target=arduino` refusing `my $x = 42;`. Several other places
ask the same question, what native type an untyped variable holds, and each
answers it alone or not at all. It belongs to v6's theme, speed
([V6-PLAN.md](V6-PLAN.md)).*

---

## Where the question is asked

1. **`--target=arduino` refuses the variable.** `my $x = 42;` stops the
   build with "the untyped variable $x — give it a native type"
   (`declaration` in src/codegen/Arduino.cpp). A board has no boxed values
   and no interpreter to fall back on, so it needs a proof, not a guess
   behind a guard.
   - The Numbers section of [ARDUINO-PLAN.md](ARDUINO-PLAN.md) promises that
     an untyped variable whose every value is known while building folds.
     That is not built: today only `constant` folds.
2. **`--exe` loop lanes already infer.** An untyped loop variable gets I64 or
   F64 from a fixpoint (`uTypeOf` in src/Codegen.cpp). This works one loop
   at a time, with guards and the boxed path behind them. It is why untyped
   intloop and numloop run at C's speed under `--exe -O`
   ([PERF-REVIEW-2026-11-02](../findings/PERF-REVIEW-2026-11-02.md) §1.1).
3. **Routine lanes** ([NUM-UNBOX-PLAN.md](NUM-UNBOX-PLAN.md)) extend that
   fixpoint to a routine's parameters, locals and result. It is seeded from
   declared types and literals. An untyped parameter that is only read in a
   float context is F64 (the `dualRead` rule).
4. **Typed expression evaluation in the interpreter**
   ([NATIVE-MATH-PLAN.md](NATIVE-MATH-PLAN.md) phase 4). A subtree that is
   provably numeric is computed without a `Value` for each intermediate,
   with guards at the leaves.
5. **Untyped arrays** (PERF-REVIEW-2026-11-02 item 9). An Array whose
   elements are all Num, or all Int, is stored packed. This is a run-time
   kind, but static facts would tell a lane where it can trust that kind.
   Untyped dot is 26 ns against 1.05 ns native under `--exe -O`.
6. **`--types`** ([TYPES-PLAN.md](TYPES-PLAN.md), paused): the opt-in
   contract under which `my $x = 's'` stays a Str. Here inference is a rule
   the program accepts, not a proof.

## The shape it could take

PERF-REVIEW-2026-11-02 item 10 describes one fact layer. It is recorded on
the AST by the walk DeclCheck already makes, and holds:
- declared and inferred types;
- escape, meaning a closure captures the variable or `:=` or `\` binds it;
- whether an expression can run user code.

Each evaluator would then read the same facts, where today each derives its
own under its own whitelist.

For a variable, the inference is:
- seeded from its declared type and from literals;
- carried to a fixpoint over every assignment, `op=`, `++` and call result it
  receives;
- abandoned, so the variable stays boxed, when it escapes. It escapes when a
  closure that outlives the scope captures it, `:=` binds it, it is passed to
  an `is rw` parameter, or something reaches it by name (`EVAL`, `::('$x')`).

The consumers differ in what happens when the proof fails:
- **The interpreter, `--cnp` and `--exe`** guard the value and fall back to
  the boxed path. Inference only has to be right most of the time.
- **The board** has no fallback. Inference has to be right every time, and
  when it can't prove a type, the build refuses, as it does today.

## The question only the board asks: how wide

On a host, a variable that only holds Ints is an int64 lane, and the lane
falls back to the boxed path on overflow. A board has no boxed path, and the
width has a cost: an `int` takes 8 bytes of an Uno's 2 KB, and each of its
operations is a multi-word sequence on the 8-bit CPU. There are two answers,
and neither is decided:
- **Always `int`.** It wraps at 2⁶³, which matches what the board already
  does for an untyped expression ([ARDUINO.md](../../guide/ARDUINO.md),
  "Integer expressions are 64-bit on the board"). It is slow on AVR.
- **The narrowest type a range proof allows.** That covers a counter over
  `^10` or a variable that only takes constants. Where no range can be
  proved, the build refuses, so the program keeps the exact meaning it has
  in the interpreter.

## First steps, smallest first

1. **The fold the Arduino plan promises.** An untyped `my` with a known
   initializer, which nothing changes afterwards, compiles as a constant.
   That is exactly Raku's meaning, and `my $x = 42; say $x` builds for a
   board.
2. **The fixpoint and the escape facts, in one place, with the Arduino
   emitter as their first reader.** A board program is a closed world: it
   has no closures and calls no module routines (ARDUINO.md, "What the board
   refuses"), so every assignment is in view.
3. **Move the other readers onto the layer,** in this order:
   - `uTypeOf`'s loop fixpoint reads from the layer instead of deriving its
     own types;
   - NUM-UNBOX's routine lanes;
   - the interpreter (NATIVE-MATH phase 4).

**The number:**
- For the board, step 1's program builds, and `--sim` prints what the
  interpreter prints.
- For the hosts, the untyped columns of PERF-REVIEW-2026-11-02 §1.1 under
  `--exe -O` reach the native columns, with the program unchanged:
  - dot: 26 → 1.05 ns;
  - matmul: 35 → 3.9 ns;
  - stencil: 39 → 1.0 ns.

  The three array kernels also need item 9's element kinds.
