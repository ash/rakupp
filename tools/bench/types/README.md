# TYPES-PLAN benchmarks

**Status: `--types` does not exist yet.** TYPES-PLAN phase N1 adds it. Until
then the tables below have no `--types` columns, and the runner says so when
it starts. It checks for the option on every run, and the columns appear
without any change here once the engine has it.

The kernels that measure [TYPES-PLAN.md](../../../docs/dev/plans/TYPES-PLAN.md):
the same computation over plain `my $` variables and over declared natives
(`my int`, `my num`), plus a Mandelbrot written with decimal literals, which
makes every step exact `Rat` arithmetic.

```bash
build/rakupp tools/bench/types/run.raku                  # every kernel, best of 3
build/rakupp tools/bench/types/run.raku --reps=5 mandel  # the Mandelbrot ones only
build/rakupp tools/bench/types/run.raku --rakudo=        # skip Rakudo
```

Each kernel prints a `sum=` checksum. The runner times every configuration
the engine offers (the interpreter, `--cnp`, and `--types` with and without
`--cnp` once the option exists) interleaved, round by round. It marks a cell
`≠` when that configuration printed a different checksum, and exits non-zero:
a speed-up that changes the answer is not a speed-up.

| kernel | what it is |
|---|---|
| `intloop-plain` / `-native` | an integer accumulator, 5M iterations |
| `numloop-plain` / `-native` | a `Num` accumulator, 5M iterations |
| `mandel-plain` / `-native` | the Mandelbrot kernel of NATIVE-MATH-PLAN.md, 300×260, `Num` literals |
| `mandel-rat` | the same kernel with `2.0`-style literals: exact `Rat` arithmetic, 150×130 |

## Baseline

Measured 2026-09-29 on Darwin 27.0, arm64, `build-arm64` at `5366241a`, with
the native-int arithmetic fix applied (uncommitted at the time), against
Rakudo 2026.09, best of 3. It was a working machine with other sessions
running (load 3–4), so read the ratios, not the seconds. The `--exe` columns
time the compiled binary only; every kernel compiled natively (none was
bundled with the interpreter).

| kernel | interp | `--cnp` | `--exe` | `--exe -O` | Rakudo |
|---|---:|---:|---:|---:|---:|
| intloop-plain | 1.065 s | 0.092 s | 0.164 s | 0.007 s | 0.699 s |
| intloop-native | 1.323 s | 1.335 s | 0.162 s | 0.007 s | 0.201 s |
| numloop-plain | 0.987 s | 0.029 s | 0.191 s | 0.007 s | 0.817 s |
| numloop-native | 1.128 s | 1.125 s | 0.196 s | 0.007 s | 0.615 s |
| mandel-plain | 1.766 s | 0.025 s | 0.198 s | 0.006 s | 0.644 s |
| mandel-native | 1.950 s | 1.988 s | 0.194 s | 0.183 s | 0.210 s |
| mandel-rat | 0.428 s | 0.238 s | 0.222 s | 0.221 s | 0.704 s |

What the table shows:

- **In the interpreter and under `--cnp`, declaring natives makes code
  slower.** Interpreted, `my int` costs 5–25%, because a native's value is
  still a full `Value`, with the width checks on top. Under `--cnp` a native
  loop gets no kernel at all (`--cnp=verbose`: "slot $i is a native or
  readonly container the kernel would write", and for `mandel-native` "a
  declaration with a type or trait"), so it runs 15–80× slower than the same
  loop over plain variables.
- **`--exe` runs the native kernels as fast as the plain ones,** because it
  ignores native widths altogether: a `my int` is compiled as an ordinary
  `Int`. That is also why compiled code does not wrap on native overflow,
  where the interpreter and Rakudo do.
- **`--exe -O` refuses `mandel-native`'s loop lanes** for the same reason
  `--cnp` does, typed declarations inside the loop: 0.183 s against 0.006 s
  for `mandel-plain`.
- **Rakudo shows the opposite of the interpreter:** its natives are 3–4×
  faster than its boxed variables, because it really unboxes them.
- **`mandel-rat` is exact `Rat` arithmetic everywhere,** and no
  configuration has a fast path for it yet (TYPES-PLAN phase N6).
