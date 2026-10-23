# Float-heavy code under Raku++: 23 issues, interpreted and `--exe`

*First measured 2026-10-15 at 5.4.1-78-g52df8573, re-checked 2026-10-22 at
5.5.0, and re-measured whole 2026-10-23 at 5.5.0-8-g15a27449, on Apple Silicon
(arm64), macOS, at a load average of 3–5. Times are
the best of 3 unless an entry says otherwise; read the ratios rather than the
absolute times.*

The subject is one float-heavy benchmark program. It ran under the
interpreter and native-compiled with `raku --exe -O`, and so did a copy with
native types (`num`, `int`). The program does a lot of `Num` arithmetic,
recursion, short loops over arrays of 2–10 elements, and exact decimal
rounding on arbitrary-precision `Int`s. Every repro below is a few standalone
lines.

- Each entry keeps its 5.4.1 report and adds an **On 5.5.0** line. Issues
  18–21 are new on 5.5.0.
- **After the fix branch** (`claude/float-heavy-fixes`, 2026-10-22) says what
  that branch changed, measured against a v5.5.0 build in the same sitting,
  best of 3. Its second pass (issue 17, and the wrong answers the first left
  open) is measured against the first pass's build, d7c6d9d5.
  [Found while fixing](#found-while-fixing) lists the wrong answers the work
  turned up on the way, most of them fixed there too.
- **On 5.5.0-8** (2026-10-23, 5.5.0-8-g15a27449, with the fix branch merged)
  the whole program was re-measured, in one interleaved run with the other
  languages and head to head with 5.5.0. That measurement found issues 22
  and 23; see [Re-measured on 5.5.0-8](#re-measured-on-550-8).
- The probe scripts in the appendix reproduce the cost numbers in either mode.

## How it compares with other languages

The same computation was written in Rust, C++, TypeScript (Bun), Go and
Fortran, every version producing identical output. Each was timed against
CPython 3.12 in one interleaved run: the steady-state time per call, averaged
over six input sizes.

| | × CPython |
|---|--:|
| Fortran (gfortran `-O3`) | 177× |
| Go | 122× |
| Rust, C++, TypeScript | ~55× each |
| Raku++ 5.5.0-8, `--exe -O` | 3.5× (5.5.0: 3.2×, 5.4.1: 2.5×) |
| Raku++ 5.5.0-8, `--exe -O`, native types | 2.7× (2.2×, 1.7×) |
| Raku++ 5.5.0-8, interpreted | 0.48× (0.47×, 0.44×) |

The other languages' figures are from the 2026-10-23 run and match the
2026-10-15 run within 3%.

- **Fortran and Go lead on data layout, not language.** Their versions
  allocate nothing per call: integers and doubles in fixed arrays, and buffers
  reused. The Rust, C++ and TypeScript versions build a heap string per
  identifier, and about 65% of Rust's profile was malloc, free and string
  formatting.
- **Raku++'s gap is the same kind of cost on a larger scale.** Every value is a
  128-byte box, and every operation, array access and `.push` is a runtime
  call. The profiles under [Found on 5.5.0](#found-on-550) and
  [Re-measured on 5.5.0-8](#re-measured-on-550-8) show no single hotspot.
- **Per construct, compiled Raku++ beats CPython only on scalar float work.**
  It is 2–10× cheaper there, and level with CPython on array reads and loops.
  On 5.5.0-8 its speed-up falls from 4.0× on the smallest input to 2.3× on
  the largest, where a recursion of about 1,000 ten-argument calls
  dominates. A compiled call costs 23 ns against CPython's 36.

## Status on 5.5.0

| # | issue | on 5.5.0 | after the fix branch |
|---|---|---|---|
| 1 | `Num.Rat(0)` is not exact | still present | **misreported**: Rakudo answers the same; fixed where the two differed |
| 2 | one list literal bundles the whole program | still bundles; the bundle's routines are now native | still bundles; three wrong answers around it **fixed** |
| 3 | `for $*IN.lines` buffers all of stdin (`--exe`) | **fixed** | |
| 4 | `CATCH` or `Int(…)` bundles the whole program | `Int(…)` **fixed**; `CATCH` still bundles, routines native | unchanged |
| 5 | no `Num` fast path | **fixed** for `+ - * /` and comparisons; unary minus is issue 18 | |
| 6 | `+&` `+>` `+<` off the int fast path | **fixed** | |
| 7 | labelled `next` is a control exception | **fixed**; still about 20× a `while`/`last` loop | |
| 8 | type-object terms looked up by name | **fixed** | |
| 9 | closures are not direct calls (`--exe`) | still present, 5.2× (was 6.5×) | **fixed**: 1.06× a top-level sub |
| 10 | a `use`d module's sub costs ~6× (`--exe`) | still present | unchanged |
| 11 | smaller costs | three of four **fixed**; `constant` still costs | `constant` **fixed**, both modes |
| 12 | a Failure stored in a native becomes `2` | **fixed**: dies | |
| 13 | a typed `for` parameter breaks the `--exe` build | **fixed** | |
| 14 | native arrays bundle the whole program | still bundles; routines native | unchanged |
| 15 | the loop lane gives a read-only variable an int slot | **fixed** | |
| 16 | the code generator ignores native types | partly: fast arms added, still boxed; `num` code 3× untyped | **at the untyped code's speed** on every probe; still boxed |
| 17 | native types leave the interpreter's kernels | **fixed** for `int`, not for `num` | **fixed** for `num`: a `num` sub and loop at the untyped code's cost |
| 18 | unary minus on a `Num` is generic | new | **fixed**, both modes |
| 19 | Array methods dispatch by name (`--exe`) | new | **fixed** for `.elems`, `.defined`, a one-argument `.push` |
| 20 | a sub with an `@` parameter takes the `ValueList` path | new | **fixed** |
| 21 | iterating a small array costs about what CPython does | new | 9–12% faster through 19 and 20; no array lane |
| 22 | `.pop`, `.splice` and methods on an array element dispatch by name | — | found on 5.5.0-8 |
| 23 | the native-typed program is still ~1.3× slower than untyped | — | found on 5.5.0-8 |

## Behaviour

### 1. `Num.Rat(0)` does not return the exact value of the double

```raku
say 2.6799999999999997e0.Rat(0).nude;
say 2.6799999999999997e0.Rat(1e-300).nude;
say 0.1e0.Rat(0).nude;
```

| | actual (both modes, and Rakudo 2026.09) | the double's exact value |
|---|---|---|
| `2.6799999999999997e0.Rat(0)` and `.Rat(1e-300)` | `(392892154991943 146601550370128)` | `(377176468792279 140737488355328)` |
| `0.1e0.Rat(0)` | `(1 10)` | `(3602879701896397 36028797018963968)` |

The expected values have a power-of-two denominator, the double's exact
binary value. That is what CPython's `float.as_integer_ratio()` returns.
*(The 5.4.1 report also said Rakudo returns it. It does not: see the fix
branch line below.)*

- **Impact:** any exact-arithmetic code that relies on `.Rat(0)` to recover a
  double's true value gets a nearby rational instead.
- **Workaround:** decompose the double by hand: scale by powers of two, which
  is exact, and take `floor()`.
- **On 5.5.0:** unchanged in both modes; the same three outputs.
- **After the fix branch:** the three outputs above are Rakudo 2026.09's too:
  its Num.Rat walks the continued fraction until the fractional part is 0 or
  b/d is within epsilon, and `.Rat(0)` is not the exact value in either
  engine. Raku++ now walks it step for step as Rakudo does, where it had
  differed: its convergents were int64, so `1e-300.Rat(0)` was 0/1 (Rakudo:
  1/10³⁰⁰); a fractional part below 1e-18 stopped a convergent early; and b/d
  past 2⁵³ was rounded twice (MoarVM's `div_In` rounds it once). Where
  Rakudo's own walk never ends — `7.01492021304424e-06.Rat(0)` grows Ints
  until memory runs out, `5e-324.Rat(0)` dies — Raku++ answers the exact
  value. 13,222 (input, epsilon) pairs agree with Rakudo; the rest of the
  120,054 were not judged, Rakudo slowing to a crawl in long runs.
  **Workaround** for the exact value, in either engine: the decomposition
  above.

### 2. `--exe` bundles the whole program over one list literal

```raku
sub total(@x) { my $s = 0e0; $s += $_ for @x; $s }
sub bump($arr) { $arr[0] = $arr[0] + 1 }
my $p = 0.25e0; my $q = 0.75e0;
say total(($p, $q));            # a List literal of variables, passed to a sub ...
my @counts = 0; bump(@counts);  # ... and, anywhere else, a write through a parameter's subscript
```

- **Actual:** `--exe` prints the note `a List that holds a variable's
  container, written through a subscript — not yet natively compiled;
  bundling the whole program with the interpreter instead.` `--exe-info` then
  reports `"mode":"bundle"`.
- **Expected:** native code. The two constructs are unrelated and sit in
  different subs.
- **Cause:** the check is whole-program and goes by name (`Codegen.cpp`,
  `keptListVar_ && listWriteVia_`). A single call site like `f(($a, $b, $c))`
  anywhere disables native compilation of everything. Nothing reports this
  except the note, which is easy to miss in a build log.
- **Workaround:** `my @xs = $p, $q; total(@xs)`.
- **On 5.5.0:** still `"mode":"bundle"`. The note now names both lines (`a
  List that holds a variable's container (line 4), anywhere in a program that
  writes through a subscript of a scalar or parameter (line 2)`), and the
  bundle compiles the routines natively: `--exe: 2 of 2 program routines
  compiled natively`. Going by the note, one occurrence now costs the
  mainline its native code rather than every routine (not timed).
- **After the fix branch:** still bundles. Testing it found that those native
  routines wrote into a kept List's item instead of through it — the
  bundle's answers differed from the interpreter's — and two more holes like
  it; see [Found while fixing](#found-while-fixing).

### 3. `--exe`: `for $*IN.lines` reads all of stdin before the first iteration

```raku
for $*IN.lines -> $line { say "got: $line"; $*OUT.flush }
```

- **Actual:** the compiled binary prints nothing until stdin is closed. The
  interpreter answers each line as it arrives.
- **Expected:** the interpreter's behaviour, since `lines` is lazy.
- **Impact:** a line-oriented request/response loop deadlocks any caller that
  waits for each answer before sending the next request.
- **Workaround:** `while (my $line = $*IN.get).defined { ... }`, which streams
  in both modes.
- **On 5.5.0:** fixed. With `(echo a; sleep 2; echo b) | ./prog`, the
  compiled binary answers `a` before `b` is written.

### 4. A `CATCH` block or an `Int(...)` coercion call makes `--exe` bundle the whole program

```raku
sub f($x) { CATCH { default { return -1 } }; die "no" if $x < 0; $x }
say f(-1);
```

```raku
say Int(2.5e0);
```

- **Actual:** the notes `phaser / CATCH block — not yet natively compiled` and
  `a type-coercion call Int(…) — not yet natively compiled`, then
  `"mode":"bundle"`.
- **Why list it:** both are known limitations. They are listed because one
  occurrence anywhere costs the whole program its native code, not just the
  routine that contains it.
- **Workaround:** `try { ... }` with `$!` instead of `CATCH`; `floor()` or
  `.Int` instead of `Int(...)`.
- **On 5.5.0:** `say Int(2.5e0)` compiles natively and prints `2`. The `CATCH`
  program still bundles (`phaser / CATCH block (line 1)`), but its routines
  are compiled natively inside the bundle, as in issue 2.
- **After the fix branch:** unchanged.

## Performance

### 5. `Num` arithmetic and comparisons have no fast path

`rtAdd`, `rtSub`, `rtMul`, `rtLtB`, … in `Interpreter.h` test `rtBothInt`, and
otherwise call `applyArith(std::string(op), …)`. That reaches the `Num` case
only deep in `applyArithGeneral`, after the set-op, lazy-list, type-object and
`Code` checks.

| `--exe -O`, ns/op | Num | Int |
|---|---:|---:|
| `$a * $b + $a` | 41.4 | 2.6 |
| `$a < $b` | 23.0 | 7.2 |

- **Profile:** in a float-heavy program compiled with `--exe -O`,
  `applyArithGeneral` plus `applyArith` were 25% of all samples.
- **Suggested fix:** a `rtBothNum` branch in those inline helpers, and in
  `applyArith(const char*)`. This is probably the largest single win
  available for float-heavy code.
- **Natives don't avoid it:** `num`-typed variables also end in `applyArith`
  through `rtNativeArith`.
- **On 5.5.0:** fixed. `rtAdd`/`rtSub`/`rtMul`/`rtLtB`… take a `rtNumPair`
  arm, `/` compiles to `rtSlash`, and `rtNativeArith` has its own `Num` arm.
  `$a * $b + $a` on Nums 41.4 → 8.8 ns (Int: 1.5), `$a < $b` 23.0 → 6.3 ns
  (Int: 6.4). In the same program's compiled profile, `applyArith` is down to
  about 2% of samples, from 25%. Unary minus was left out; see issue 18.

### 6. `+&`, `+>`, `+<` miss the int64 fast path

They compile to `applyArithValue("+&", …)`, which is generic, under `--exe`,
and they are generic in the interpreter too. `%` and `div` with the same
operands take the fast path:

| ns/op | `--exe -O` | interpreted |
|---|---:|---:|
| `$m +& (2**50 - 1)` | 81.4 | 181.6 |
| `$m % 2**50` | 2.8 | 5.3 |
| `$m +> 50` | 86.2 | 186.0 |
| `$m div 2**50` | 3.3 | 3.9 |

- **On 5.5.0:** fixed. `+&` 6.2 ns compiled / 2.4 ns interpreted, `+>`
  6.2 / 3.2, against `%` 1.7 / 3.5 and `div` 1.5 / 3.5.

### 7. A labelled `next` is an expensive control exception

```raku
ROW: for ^3 -> $i { for ^3 -> $j { next ROW if $j == 1; ... } }
```

- **Cost per jump:** about 53 µs interpreted and about 8 µs compiled.
- **Comparison:** three such jumps take 157,926 ns, against 53 ns for the same
  loop written with `while`/`last`.
- **In practice:** in the benchmark program, a single labelled `next` in a
  hot loop accounted for about a quarter of interpreted run time.
- **On 5.5.0:** fixed, no exception any more. Three jumps take 1,015 ns
  interpreted and 121 ns compiled, against 41.7 and 5.8 ns for the
  `while`/`last` form. The labelled form is still about 20× slower; that is
  small enough to ignore in all but the tightest loop.

### 8. Under `--exe`, type-object terms are looked up by name at run time

`Any`, `Empty` and `Int` compile to `RT.rtNameTerm("Any")` and so on, on every
evaluation. `Nil` compiles to a constant.

| `--exe -O`, ns/op | |
|---|---:|
| `Any` | 239.8 |
| `Empty` | 232.1 |
| `Nil` | 2.7 |
| `()` | 25.1 |
| `$v ~~ Int && $v !~~ Bool` | 1181.2 |
| `$v.^name eq 'Int'` | 117.8 |

- **On 5.5.0:** fixed. `Any` 5.0 ns, `Empty` 8.8, `Nil` 1.5, `()` 10.7,
  `$v ~~ Int && $v !~~ Bool` 335.6, `$v.^name eq 'Int'` 65.9.

### 9. Under `--exe`, closures are not direct calls

| Recursion with 2¹⁰ leaves | `--exe -O` | interpreted |
|---|---:|---:|
| recursive `my sub`, a closure over a counter | 408 µs | 481 µs |
| the same as a top-level sub, state passed in | 63 µs | 626 µs |

Compiled, the closure is 6.5× slower; interpreted, it is the faster of the
two.

- **On 5.5.0:** still present. The closure takes 209 µs compiled against
  40 µs for the top-level sub (5.2×). Interpreted, 361 against 465 µs.
- **After the fix branch:** fixed under `-O`: 42.5 µs against 39.9 (210.6 on
  5.5.0). Every call of a lexical sub found `&walk` in the runtime env and
  went through callCallable; a call site in its scope now reaches the body directly,
  through a weak handle (so a body that calls itself does not keep itself
  alive), and takes the old path once that scope has gone. A sub with a
  return type, a trait or other than plain parameters, and every sub in a
  program that says `wrap`, keeps the old path.

### 10. Under `--exe`, calling a sub from a `use`d module costs about 6×

```raku
# lib/M.rakumod
unit module M;
sub add3($a, $b, $c) is export { $a + $b + $c }
```

```raku
# main.raku — raku --exe -O -I lib main.raku -o main
use M;
sub add3-local($a, $b, $c) { $a + $b + $c }
my $N = 2_000_000;
my $t = now; my $s = 0; for ^$N { $s = add3($s, 1, 2) }
printf "module sub : %6.1f ns/call\n", (now - $t) / $N * 1e9;
$t = now; $s = 0; for ^$N { $s = add3-local($s, 1, 2) }
printf "local sub  : %6.1f ns/call\n", (now - $t) / $N * 1e9;
```

- **Actual:** the module sub costs about 68–71 ns per call against about
  10.6 ns for the local one, measured on an idle machine.
- **Build output:** `--exe` reports `embedded 1 module: M` and
  `1 of 1 module routines compiled natively`, and `--exe-info` says
  `"mode":"native"`. So the cost is in the call path, not in the routine
  falling back to the interpreter.
- **Workaround:** concatenate the modules into the main program before
  compiling.
- **On 5.5.0:** still present: 64–75 ns per module call against about 10 ns
  for the local sub, and `--exe-info` still says `"mode":"native"`.
- **After the fix branch:** unchanged. The call is `callEnvFirstAmp("&add3",
  …)` — a lookup by name, callCallable, and the interpreter's own int kernel
  for `add3`, which runs before the module's native body is reached. The
  module's body is an `(Interpreter&, Env*, Value&)` function reading its
  parameters from the frame, so a direct call needs a second, Value-parameter
  entry per module routine, and the program's call site the proof that the
  name still means it.

### 11. Smaller costs

| construct | `--exe -O` | interpreted | cheaper spelling, `--exe -O` / interpreted |
|---|---:|---:|---|
| `($x * 2**52).Int` | 132.1 | 240.2 | `floor($x * 2**52)`: 32.2 / 192.6 |
| two `constant` references | 12.6 | 388.7 | two literals: 2.3 / 6.7 |
| `101 <= $f < 200` (chained) | 29.3 | 230.4 | `$f >= 101 && $f < 200`: 18.2 / 169.5 |
| `$p.key` / `$p.value` on a Pair | 386.9 | 616.4 | `$p[0]` / `$p[1]` on a 2-element Array: 53.3 / 283.1 |

All figures are ns/op. Interpreted numbers for all-`Int` subs are low because
the interpreter runs them as int kernels. That is why the `Int` rows of
issues 5 and 6 are close to the floor.

**On 5.5.0** (ns/op, `--exe -O` / interpreted):

| construct | 5.5.0 | cheaper spelling on 5.5.0 | |
|---|---:|---:|---|
| `($x * 2**52).Int` | 9.3 / 203.0 | `floor(…)`: 10.6 / 165.4 | **fixed** compiled |
| two `constant` references | 8.4 / 234.7 | two literals: 1.5 / 5.7 | still present |
| `101 <= $f < 200` | 9.8 / 203.7 | `&&` form: 11.7 / 136.4 | **fixed** compiled |
| `$p.key` / `$p.value` | 20.3 / 477.5 | `$p[0]` / `$p[1]`: 32.4 / 201.2 | **fixed** compiled |

Interpreted, the chained comparison and the Pair methods still cost about
1.5× and 2.4× their alternatives.

**After the fix branch:** two `constant` references cost 1.5 ns compiled and
5.7 interpreted, against 9.2 / 213.9 on 5.5.0 (two literals: 1.7 / 5.5). The
constant kept the sub off its int kernel in both engines; a top-level
`constant` holding a machine Int is now that literal to both kernel
compilers. The interpreter's takes it only where the term resolves from the
global scope itself — a top-level sub, a mainline loop — so a shadowing
declaration that has not run yet at the first call cannot leave the global's
value in the kernel, and none while other threads run.

## Native types (`num`, `int`, native arrays)

The same program was rewritten with native types. About 150 declarations
changed: `num`/`int` scalars, parameters, return types and loop counters. No
floating-point operation or its order changed.

| Mode | Result |
|---|---|
| Interpreted | flat, within ±2% per benchmark case |
| `--exe -O` | about 1.5× slower than the untyped program, e.g. 91.6 → 131.7 µs per call |

On 5.5.0 both versions got faster, and the ratio did not change:

| Mode | Result on 5.5.0 |
|---|---|
| Interpreted | 3–7% slower than the untyped program |
| `--exe -O` | still about 1.5× slower than the untyped program, e.g. 68.1 → 102 µs per call |

Issues 12 and 13 below produce wrong results or no binary; 14–17 are about
cost. Issues 12–14 were re-checked against the build named at the top.

### 12. A Failure stored in a native variable silently becomes `2`

```raku
my num $z = 0e0;
my num $r = 1e0 / $z;   say $r;          # 2   (also 7e0/$z, -1e0/$z, 0e0/$z)
my int $i = 1 div 0;    say $i;          # 2
my num $f = Failure.new("x"); say $f;    # 2
```

- **Actual:** `2`, silently, in both modes.
- **Expected:** the Failure's exception (`Attempt to divide 1 by zero using
  /`), or `Inf` as Rakudo's native `div_n` gives. Not a finite wrong number.
- **What works:** `say 1e0 / $z`, an untyped target, binding to a `num`
  parameter, and a `--> num` return all raise correctly.
- **Workaround:** keep any division whose divisor can be zero out of native
  variables.
- **On 5.5.0:** fixed, no silent value any more. Each line dies, in both
  modes, with `This type cannot unbox to a native number: P6opaque, Failure`
  (`… native integer …` for the `int` line).

### 13. A typed `for` loop parameter breaks the `--exe` build

```raku
sub f(@v) { my num $s = 0e0; for @v -> num $x { $s = $s + $x }; $s }
say f([1e0, 2e0]);
```

- **Actual:** interpreted, it prints `3`. With `--exe`, with or without `-O`,
  clang fails with `error: use of undeclared identifier 'v_sx'` and no binary
  is produced.
- **Same failure for:** `-> Num $x`, `-> Int $i`, `for ^$n -> int $i`.
- **Workaround:** `my int $i = 0; while $i < @v.elems { my num $x = @v[$i]; …; $i++ }`.
- **On 5.5.0:** fixed. All four spellings compile natively under `--exe -O`
  and print the interpreter's answer.

### 14. Native and typed arrays make `--exe` bundle the whole program

```raku
my num @a = 1e0, 2e0;
say @a[1];
```

- **Actual:** the note `a typed @ variable (my num @a) — not yet natively
  compiled; bundling the whole program`, and `"mode":"bundle"`.
- **Same for:** `my int @a`, `my Num @a`, `array[num].new(...)`, and a
  `num @x` parameter, which report `the parameterized type 'array[num]'`.
- **Impact:** one native array anywhere costs every routine its native code.
  In the test program, one benchmark went from 132 to 494 µs per call.
- **On 5.5.0:** `my num @a` still bundles (`a typed @ variable (my num @a)
  (line 1)`), but the routines inside the bundle are compiled natively, as in
  issue 2. The other spellings were not re-checked.
- **After the fix branch:** unchanged.

### 15. The `-O` loop lane gives a read-only variable an int slot

```raku
sub f(num $x --> num) { my num $s = 0e0; my int $i = 0;
                        while $i < 8 { $s = $s + $x * 2e0; $i++ }; $s }
```

- **Actual:** the lane's entry guard is `rtIntSlot(v_si) && rtNumSlot(v_ss) &&
  rtIntSlot(v_sx)`. `$x` gets an int slot although it is declared `num`, or
  holds a Num when untyped. The guard always fails and the loop runs boxed.
- **Cost:** 339 ns, against 46 ns once `$x` is also assigned inside the loop
  and so gets a `rtNumSlot`. Untyped code is affected the same way (246 vs
  7.5 ns).
- **Expected:** a read-only slot typed from its declaration or its value.
- **On 5.5.0:** fixed. The guard is `rtNumSlot(v_sx)` for `num $x`, and
  `rtNumSlot(v_sx) || rtIntBox(v_sx)` untyped. The untyped read-only loop went
  from 246 to 8.5 ns, the same as when `$x` is also written (8.7). The
  `num`-typed versions take 28–29 ns whether `$x` is read or written; that gap
  is the per-call native cost in issue 16, not the guard.

### 16. The code generator ignores declared native types

`num` and `int` variables are boxed `Value`s like any other:

| Operation on a native variable | Compiles to |
|---|---|
| arithmetic | `rtNativeArith` → `applyArith(std::string)`, the slow path from issue 5 |
| comparison | an int fast-path guard, then the generic `rtGtB` |
| declaration or store | `rtNativeValue(…, "num", "$a", …)`, which builds two `std::string`s per store |
| parameter | `rtBindTyped` |

- **Effect:** the whole program runs about 1.5× slower compiled than its
  untyped version.
- **Integer sub:** an `int`-typed integer sub is 23× slower than its untyped
  twin (44.2 vs 1.9 ns).
- **Example:** `$a / ($k - 1e0)` on `num` locals compiles to
  `rtNativeValue(applyArithValue("/", rtNativeArith("*", ...),
  rtNativeArith("-", ...)), "num", "$a", false)`. It never becomes C++
  `double` arithmetic.
- **On 5.5.0:** partly addressed. `rtNativeArith` now has `Int` and `Num`
  arms that skip `applyArith`, and stores go through `rtNativeStore`, whose
  fast arm builds no strings. `my num $r = $a / ($k - 1e0)` now compiles to
  `rtNativeStore(rtSlash(v_sa, rtNativeArith("-", v_sk, Value::number(1))),
  "num", "$r", false)`. But a native is still a boxed `Value`, and every
  `num` parameter still goes through `rtBindTyped`. The untyped code got
  faster too, so the gap is wider than before:

  | `--exe -O`, ns/op | untyped | `num` / `int` | `Num` |
  |---|---:|---:|---:|
  | `sub ($a, $b) { $a * $b + 1e0 }` | 9.6 | 29.9 | 22.8 |
  | `Num < Num` in a sub | 6.2 | 19.5 | |
  | the integer sub, `(int $m --> int)` | 1.5 | 10.6 | |

  The whole native-typed program is still about 1.5× slower compiled than
  its untyped version.
- **After the fix branch:** each probe at the untyped code's speed; natives
  are still boxed Values. Three costs were left: `rtNativeArith` and
  `rtNumArith` dispatched on the operator's text and were not inlined (a
  helper per operator now); a typed parameter that already conforms was moved
  back into itself and its descriptor's static guard read (it stays where it
  is now); and a native store compared the type's name with `strcmp`, out of
  line, twice (the kind is decided where the code is generated).

  | `--exe -O`, ns/op | 5.5.0 | fix branch | untyped |
  |---|---:|---:|---:|
  | `(num $a, num $b --> num)` | 29.1 | 10.0 | 9.7 |
  | `(Num $a, Num $b --> Num)` | 22.3 | 9.7 | |
  | `num < num` | 19.5 | 6.2 | 6.2 |
  | `(int $m --> int)` | 10.6 | 5.5 | 1.5 |
  | the issue-15 `num` loop, `$x` read only | 28.5 | 9.5 | 8.7 |
  | …and `$x` also written | 28.1 | 9.4 | 8.7 |

  Interpreted, the native subs cost what they did (issue 17).

### 17. Native types take subs off the interpreter's machine-value kernels

| Interpreted, ns/call | untyped | typed |
|---|---:|---:|
| all-`Int` sub (typed version: `(int $m --> int)`) | 11.4 | 406 |
| all-`Num` sub (typed version: `num`) | 152.7 | 419.4 |

The typed version is slower for both, and the float loops in issue 15 follow
the same pattern.

- **On 5.5.0:** fixed for `int`, not for `num`. Interpreted, the
  `(int $m --> int)` sub costs 6.2 ns against 6.8 untyped. The `num` sub
  costs 418.5 against 140.8, and the issue-15 float loops cost 1,075 / 2,362 ns
  typed against 276 / 410 untyped (`$x` read only / also written).
- **After the fix branch:** unchanged by its first pass. The `num` sub
  missed the lean call path (`plainSubShape` took untyped `$` parameters
  only) and paid the general binder's type-name lookups and `checkRetType`
  on every call; the int sub escaped them only because it runs as a kernel.
  The loops entered no loop kernel: one refused every native variable.
- **After the second pass:** a sub whose parameters and return name `num`,
  `int`, `Num`, `Int` or `Str` takes the lean path when each argument already
  is of its type, and binds it as the general binder does (any other argument
  goes the general way). A loop kernel takes a full-width `int` or `num`
  variable as a plain Int or Num, bails where a result leaves int64 (the
  native path then wraps), and writes the number back into the native cell.
  Interpreted, against d7c6d9d5, ns per call: the `num` sub 387.1 → 152.6
  (untyped 144.8), the `Num` one 360.8 → 148.3, `num < num` 245.2 → 143.1
  (untyped 134.6), the issue-15 loops 1,024 / 2,332 → 292 / 581 (untyped
  282 / 407; the second's `num $x is copy` still takes the general binder).
  perf-guard's strscan, whose loop counts in a `my int`, runs as a kernel:
  96.4 → 6.0 ms.

## Found on 5.5.0

The program from the top of this file runs about 1.3× faster compiled on
5.5.0, mostly from issue 5's fix. What is left has no single hotspot. A
`sample` profile of the compiled program (`--slim=safe,+symbols`) has no
function above 6% of samples:

| share | where |
|---:|---|
| 26% | array and argument-list machinery: `RVec<Value>::growAndBuild`, `destroyAll`, `alloc`, `relocate`, `append`, the copy constructor, `rtIndexGet`, `listToArray`, `Value::array` |
| 26% | the generated `u_*` functions' own code, mostly inlined `Value` construction, teardown and tag checks |
| 23% | boxed scalar operations: `rtAdd`/`rtMul`/`rtSub`/`rtGe`/`rtLtB`…, `boolify`, `truthy`, `toInt`, `applyArith` |
| 6% | `Interpreter::methodCall` and `methodCallInner` |
| 8% | malloc, free, memmove, bzero |

Issues 18–21 are the parts of that table which a code-generator change could
reach. Figures are ns/op, best of 3, from the extended probe script in the
appendix.

### 18. Unary minus on a `Num` still goes through the generic dispatcher

```raku
sub neg($x) { -$x }
```

- **Compiles to:** `applyArith("-", Value::integer(0), v_sx)` under `--exe -O`:
  the string-keyed dispatcher, with a mixed `Int`/`Num` pair, so the new
  `rtNumPair` arm is never reached.
- **Cost:** `-$x` 13.4 ns compiled, against 6.5 ns for `0e0 - $x`.
  Interpreted, 140.1 against 122.3.
- **Where it shows:** the usual `abs` spelling, `$x < 0e0 ?? -$x !! $x`. In
  one float program, 25 of the 27 `applyArith(` call sites left in the
  generated C++ were `applyArith("-", Value::integer(0), …)`.
- **Suggested fix:** a `rtNeg` helper with `Int` and `Num` arms.
- **After the fix branch:** fixed. Compiled, `-$x` is `rtNeg`, with Num and
  machine-Int arms inline and the interpreter's prefixNumeric for the rest:
  5.2 ns against 6.5 for `0e0 - $x` (12.9 on 5.5.0). That also fixed two
  disagreements: `0 - $x` gave +0 for `-$x` on 0e0, where the interpreter and
  Rakudo give -0, and numified an object through `.Bridge` where they ask
  `.Numeric`. Interpreted, the operator has the same arm and a per-node
  handler: 128.6 against 126.9 (143.5 on 5.5.0). `my $y = -$x` is still about
  100 ns slower than `my $y = 0e0 - $x`, as `my $y = +$x` and `?$x` are: the
  declaration lane takes only some right-hand sides.

### 19. Under `--exe`, `.push`, `.elems` and `.defined` dispatch by name

```raku
sub elems(@a) { @a.elems }
sub three() { my @a; @a.push(1e0); @a.push(2e0); @a.push(3e0); @a }
```

- **Compiles to:** `RT.methodCall(v_aa, "elems", ValueList{})` and
  `RT.methodCall(v_aa, "push", ValueList{Value::number(1)})`. Each call
  builds a `std::string` name and an argument list, then goes through the
  generic method dispatcher (`methodCall` → `methodCallInner` →
  `methodCallPart2` in the profile).
- **Cost:** `@a.elems` 29.2 ns against 1.6 ns for an empty sub call;
  `$x.defined` 12.3 ns; `my @a`, three `.push`, return 89.6 ns; `[1e0, 2e0,
  3e0]` 64.9 ns. CPython runs the same `len(a)` in about 25 ns and the three
  `append`s in about 117 ns.
- **Scale:** the benchmark program has 215 such call sites: 60 `.push`, 30 `.defined`
  and 24 `.elems` among them.
- **Suggested fix:** when the receiver is known to be an `Array` (an `@`
  variable or parameter), call the method's C++ body directly. `.defined` on
  any `Value` is a tag test.
- **After the fix branch:** fixed for these three. methodCall already
  answered them in its first lines for a plain invocant; `-O` now makes the
  same tests at the call site (one set of predicates, which methodCall uses
  too): `@a.elems` 8.0 ns (24.9), `$x.defined` 6.4 (9.7), `my @a`, three
  `.push`, return 54.1 (89.8). `[1e0, 2e0, 3e0]` is unchanged at 66.

### 20. A sub with an `@` parameter gets no direct-arity overload

```raku
sub at(@x) { 0 }
sub dollar($x) { 0 }
my @ten = 1e0 xx 10;
at(@ten); dollar(@ten);
```

- **Cost:** `sub (@x)` 16.5 ns a call against 3.9 ns for `sub ($x)` passed the
  same Array. Interpreted, 225.9 against 142.7.
- **Why:** `-O`'s direct-arity pass takes only plain `$` parameters, so a sub
  with an `@` parameter is called through a `ValueList` built and torn down
  per call.
- **Suggested fix:** let an `@` parameter that is bound, not copied, take a
  `Value` in the direct overload.
- **After the fix branch:** fixed: a plain `@` or `%` parameter takes the
  direct overload, bound unitemized as the boxed path binds it. 3.9 ns, the
  same as `sub ($x)` (16.8 on 5.5.0).

### 21. Iterating a small array costs about what CPython does

```raku
sub sum-for(@a)   { my $s = 0e0; for @a -> $x { $s = $s + $x }; $s }
sub sum-while(@a) { my $s = 0e0; my $i = 0; my $k = @a.elems;
                    while $i < $k { $s = $s + @a[$i]; $i++ }; $s }
```

| 10 Nums, ns per call | `--exe -O` | interpreted | CPython 3.12, the same loop |
|---|---:|---:|---:|
| `for @a -> $x` | 169.4 | 915.4 | 127 |
| `while` with `@a[$i]` | 178.1 | 1,591.4 | 195 |

- **What each `for` iteration compiles to:**
  - a 128-byte `Value` copy of the element (`itemizedScalar`);
  - a `-O` int64 lane for `$s = $s + $x` whose `rtIntBox` guard always fails
    on a `Num`;
  - then `rtAdd`.
- **The `double` loop lane does not apply.** It takes no loop that iterates an
  array or reads an element. [OPTIMIZATION.md](../../internals/OPTIMIZATION.md#limits-and-whats-next)
  lists array-element lanes as future work; this is the measurement for that
  line.
- **For comparison:** compiled scalar `Num` arithmetic is 2–10× cheaper than
  CPython's, so a program that mixes the two lands at roughly 3× CPython
  overall.
- **After the fix branch:** `for @a -> $x` 148.8 ns and the `while` loop
  155.2 (168.6 / 170.6 on 5.5.0), from issues 19 and 20; there is still no
  array-element lane.

## Found while fixing

Checked against the interpreter and Rakudo 2026.09 while the issues above were
worked on. All but the last two groups are fixed on the fix branch, each with
a regression file.

- **A kept List was not written through in compiled code**
  (`t/regression/exe-kept-list-writes.raku`). `f(($a, 2))` with `sub f($x) {
  $x[0] = 5 }` writes `$a` in Rakudo and interpreted. Compiled, three ways it
  did not: (A) bundled for issue 2, the program's native routines stored into
  the List's item instead of through it, so `$a` stayed 1 and a second write
  to the item died "Cannot modify an immutable List"; (B) a native program
  passing such a List to a **module's** routine built it of values, and the
  module's write died; (C) a module routine keeping one did the same. Now
  `rtIndexRef` writes through the cell a List's item shares with its
  variable (an Array copied from such a List takes the values, so writing
  the copy writes no variable), a call that hands a kept List to a routine
  outside the program bundles it, and a module routine keeping one stays
  interpreted.
- **`fail` in a compiled routine died** (`exe-fail-in-routine.raku`). The
  builtin returns from the routine only when the interpreter has a routine
  frame, which compiled code never has, so `q(-1) // "failed"` printed the
  exception and exited 1, with or without `-O`. It returns the Failure now;
  in a block closure of the routine it is aimed at the routine's frame, as a
  `return` there is; and it still dies while a compiled `try` runs, which is
  what makes `(try q(-1)) // "caught: $!"` catch it, as interpreted.
- **Compiled `-$x` on 0e0 was +0**, and on an object asked `.Bridge`
  (issue 18; `exe-float-fast-paths.raku`).
- **Second pass, compiled against interpreted**, each on v5.5.0 too:
  - A store into a `$` neither reset on Nil nor checked a declared type
    (`exe-scalar-store-nil-and-type.raku`): `my $r = try die "x"` left `$r`
    Nil (interpreted and in Rakudo: Any), `my @a = Nil` was `[]`, `my Int $x;
    $x = "a"` stored the Str and `$x = Nil` the Nil. A store checks the type
    the parser resolved the name to (`VarExpr::readType`) as the interpreter
    checks it; in a bundle, a module-mode routine's store into one of the
    interpreter's variables asks that variable's declaration.
  - `-O` answered differently from plain `--exe` on `nfc-join-window.raku`
    and `typed-subtree-native-and-rat-leaves.raku`
    (`exe-nfc-join-and-destructure.raku`). `~` and an interpolation joined
    two Strs byte by byte where the interpreter joins them NFC (`"e" ~
    "\x[301]"` was two code points); `my ($a, $b) = $j` with a List in `$j`
    bound the List to `$a` instead of iterating it; `!=` between an unsigned
    native and a negative signed one was True in the condition lane (Rakudo:
    False); and `my $bound := $q` compiled as a copy, so `$bound` missed
    later writes (a `:=` of a scalar to a container is not compiled natively
    now: it bundles).
  - A `fail` in a block that another routine runs (`sub q { ap({ fail "z" });
    … }`) returned from `ap`, and `q` sank the Failure and died: compiled, a
    sub without a frame of its own caught a return aimed past it, and a
    module routine's block asked the builtin, which saw `ap`'s frame.
- **Second pass, interpreter against Rakudo**
  (`duration-slip-fail-destructure.raku`), each on v5.5.0 too:
  - `-$duration` and `abs $duration` were bare Rats (Rakudo: Durations), and
    `0 - $duration`, `7 % $duration` and `5 - $instant` Durations (Rakudo:
    the Num of the Bridges); `-$instant` is a Num. Compiled, every operator
    dropped the tag (`$d - 0` was a Rat).
  - `@a.push($s)` and `.unshift($s)` with a Slip in `$s` stored the Slip
    (Rakudo slips it); the `unshift` sub answered the new length, slipped
    nothing and checked no element type.
  - `my ($k, $v) = %h` put the Hash in `$k` (Rakudo: its pairs).
  - The `fail` in a block above, interpreted: the builtin returned from the
    routine that ran the block. It returns from the one it was written in,
    as a `return` does, and dies once that one has returned.
- **Second pass, found on the way:** from a sub's second call on, the lean
  call path took `f(($a, 2))` and dropped what makes the List keep `$a`'s
  container, so `$x[0] = 5` in `f` died "Cannot modify an immutable List"
  (`native-typed-sub-lean-and-loop-kernel.raku`). On v5.5.0 too.
- **`t/exe/fuzz.raku`'s 20 `*-shadow` programs were not valid Raku.** The
  inner `while` / `until` loop's counter is a `my` of the outer body, written
  after that body has read the outer counter, which Rakudo refuses to compile
  as rakupp does. The generator now names the inner counter apart; the loop
  variable `$a` still shadows.
- **Third pass**, each on v5.5.0 too:
  - Roast's `S07-hyperrace/basics.t` segfaulted in its last test (one
    RaceSeq iterator pulled from several threads) about one run in six
    (`worker-exit-vs-cycle-collector.raku`). A worker gave its count in
    `liveWorkers_` back at the end of its body, before its closure's captures,
    its ExecContext and its frame pools were let go; the cycle collector,
    which runs on the main thread once that count is 0, walked containers
    those releases were freeing. The count now goes back when the thread has
    let go of everything (`WorkerExit`, the thread's first thread_local):
    300 runs of that file without a crash.
  - A compiled routine did not apply its `--> T`
    (`exe-return-type-checks.raku`): `sub r(--> Int) { "x" }` answered "x",
    `--> Str()` did not coerce, `--> Int:D` passed Int. Every way out of a
    top-level, lexical or anonymous sub, multi candidate, method or pointy
    block is checked now (`Codegen::retWrap`, by `checkRetType`; a value of a
    core type passes inline, at no cost measurable against 41e4d32c's
    unchecked call). On the way: `-O` did not
    compile a `--> Int` sub of no parameters that is an int kernel (its
    kernel guard named the `__a` its direct overload has not got).
  - A write through a kept List into a typed variable was not checked, in
    either engine (`kept-list-typed-writes.raku`): `my Int $a; f(($a, 2))`
    writing a Str stored it, and a Nil stayed Nil instead of resetting to the
    default. The kept item carries the variable's type, name and default;
    interpreted, a store through it checks them, and compiled, where only a
    bundle's module-mode routines meet such a List, `X[k] = v` and `X[k] op=
    v` do.
- **Not fixed.** Interpreter against Rakudo: a native `int` times an Int that
  does not fit (`sub f(int $x) { $x * 2**62 }; f(3)`) is the BigInt
  13835058055282163712 where Rakudo wraps to -4611686018427387904; a `--> num`
  routine returning a Str hands it back where Rakudo dies ("This type cannot
  unbox to a native number"); and a `fail` in a block written at the
  mainline, run by a routine, returns from that routine where Rakudo dies
  (Test's `eval-lives-ok` relies on a `fail` at a string's top level
  returning). In both engines, a List kept in a variable and handed to a sub
  (`my $kl = ($k, 2); w($kl)` with `sub w($l) { $l[0] = 1 }`) dies "Cannot
  modify an immutable List" where Rakudo writes `$k`. Each is in
  [TODO-FOLLOW-UPS.md](../plans/TODO-FOLLOW-UPS.md), and issue 21 is
  [SPEED-BACKLOG.md](../plans/SPEED-BACKLOG.md) #48. On the third pass `t/exe/run.raku`'s three `-O` disagreements
  and `t/jit/run.raku`'s three are files that print a measurement, and five
  of the JIT gate's 21 tier-up cases (calls, elements, inline, methods,
  natives) still enter no kernel, as on v5.5.0.
- **Rakudo itself:** `Num.Rat(0)` never returns for some doubles (issue 1),
  and in a long run Rakudo answers ever more slowly, growing past a gigabyte
  on pairs it answers at once in a fresh process.

## Re-measured on 5.5.0-8

*2026-10-23, 5.5.0-8-g15a27449 against v5.5.0 in the same sitting. The whole
program ran interleaved, 5 rounds, fastest of each; the probes ran
interleaved, best of 3. Load average about 4.*

**The whole program.** Head to head with 5.5.0:

| 5.5.0 → 5.5.0-8 | smallest input | middle | largest | mean |
|---|---:|---:|---:|---:|
| `--exe -O` | 1.17× | 1.06× | 1.02× | 1.06× |
| `--exe -O`, the native-typed copy | 1.24× | 1.22× | 1.29× | 1.24× |
| interpreted, either copy | 1.03× | 1.02× | 1.01× | 1.01× |

The output was unchanged: identical to the other languages' on every input
tried, in both modes and both copies.

**Per construct, this sitting** (ns per operation, `--exe -O`):

| construct | 5.5.0 | 5.5.0-8 |
|---|---:|---:|
| `@a.elems` | 28.7 | 7.9 |
| `$x.defined` | 13.3 | 5.7 |
| `my @a`, three `.push`, return it | 88.2 | 52.7 |
| `sub (@x)` | 16.8 | 3.9 |
| `-$x` | 12.9 | 5.0 |
| two `constant` references | 8.7 | 1.5 |
| recursive `my sub`, 2¹⁰ leaves | 213 µs | 42 µs |
| `$a[$i]` | 14.3 | 14.4 |
| `[1e0, 2e0, 3e0]` | 64.4 | 65.9 |
| `for @a -> $x`, 10 Nums | 166.3 | 165.7 |
| `while` with `@a[$i]`, 10 Nums | 177.9 | 168.1 |
| a 10-parameter call | 17.6 | 17.3 |

These confirm the fix branch's figures for issues 9, 11 and 16–20. The one
exception is `for @a -> $x`, where this sitting did not reproduce the branch's
168.6 → 148.8.

**Why a 2–5× gain per construct gives 1.06× overall.** The constructs the
fixes reached were a small share of the run. The compiled profile, 5.5.0
against 5.5.0-8:

| share of samples | 5.5.0 | 5.5.0-8 |
|---|---:|---:|
| the generated `u_*` functions themselves (inlined box handling, tag checks, and now the call-site `.push`/`.elems`) | 26% | 31% |
| array and argument-list machinery: `RVec<Value>` growth and teardown, `rtIndexGet`, `listToArray`, `Value::array` | 26% | 25% |
| boxed scalar operations: `rtAdd`/`rtMul`/`rtGe`…, `boolify`, `truthy`, `toInt` | 23% | 22% |
| `methodCall` and its parts | 6% | 2% |
| malloc, free, memmove | 8% | 8% |

No function has more than 4.3% of samples on 5.5.0-8; the largest is
`RVec<Value>::growAndBuild`. Reading array elements and iterating arrays did
not move, and the boxed `Value` is about half of the time. Issue 21 (an
array-element lane, and `Num` locals that are plain `double`s) is the lever
left. More fast paths at call sites are not.

### 22. `.pop`, `.splice` and methods on an array element still dispatch by name

```raku
sub arr-pp(@a, $x) { @a.push($x); @a.pop }
sub elem-pp($acc, $x) { $acc[0].push($x); $acc[0].pop }
sub elem-elems($acc) { $acc[0].elems }
```

- **Cost, `--exe -O`:**
  - `@a.push($x); @a.pop` takes 140.7 ns. A `.push` is 10–15 ns (three of
    them, with the array's creation, take 52.7), so `.pop` is about 125.
  - Three `.push`es and a `.splice(1)` take 268.7 ns.
  - `$acc[0].push($x); $acc[0].pop` takes 170.3 ns.
  - `$acc[0].elems` takes 18.1 ns against 7.9 for `@a.elems`.
- **Compiles to:** `RT.methodCall(v_aa, "pop", …)` and
  `RT.methodCall(v_aa, "splice", …)`. A method on an element or on a `$`
  that holds an Array goes through `rtMutMethod`.
- **Suggested fix:** extend issue 19's call-site answers to `.pop`, `.shift`,
  `.unshift`, `.splice` and `.append`, and to an invocant that is an element
  or a `$` holding an Array.

### 23. The native-typed program is still about 1.3× slower than its untyped twin

- **Actual:** every native probe runs at the untyped code's speed (issue 16),
  but the program's native-typed copy is not:
  - compiled, the middle input takes 83.1 µs against 63.1 untyped;
  - against CPython it is 2.7× against 3.5× over the whole run.
- **Profile, against the untyped copy's** (share of samples):
  - The `methodCall` family (`methodCall`, `methodCallInner`,
    `methodCallPart2`, `methodCallPart3`, `methodCallTail`) takes about 12%,
    against about 2%. It is reached from `rtMPush`, `rtMutMethod` and
    `rtMDefined`, so some of the copy's `.push`/`.defined` calls miss the
    call-site answer. A plain `.push` or `.defined` of a `num` or `int`
    value does not miss it: 55 and 5.5 ns.
  - The native arithmetic helpers (`rtNativeAdd`, `rtNativeMul`,
    `rtNativeSub`, `rtNativeArithSlow`, `rtNativeIntOp`,
    `Value::natWidthOfType`) take about 6%, against none. `rtNativeArithSlow`
    is there, so some native operations still leave the per-operator
    helpers.
- **Not yet located:** which shapes in the copy take those paths. Its arrays
  are untyped (issue 14 keeps native arrays out), and its loops are `while`
  loops over `int` counters.

## Checked and found correct

- **`Str.Num`** is correctly rounded: 200,000 decimal strings, compared with
  CPython's `float()`.
- **`$x ** 2` on a `Num`** calls libm `pow` in both modes, and clang does not
  fold it to `x*x`. This matters because on macOS `pow(x, 2.0)` and `x*x`
  differ for about 0.13% of inputs.
- **`--exe` and `--jit`** compile the generated C++ with `-ffp-contract=off`,
  so no fused multiply-adds are introduced.
- **`1e0 / 0e0`** throws `X::Numeric::DivideByZero`.
- **Interpreted and `--exe`** produced byte-identical output on every input
  tried: tens of thousands of runs of a float-heavy program.
- **`--cnp`** compiled 11–12 short loops in that program, with unchanged
  output and no measurable speed-up. **`--jit`** found no eligible loop.
  On 5.5.0, `--cnp=stats` reports `examined 25, eligible 10, compiled 10,
  failed 0`. The output is still unchanged, and the gain is 0–6%.
- **On 5.5.0**, interpreted and `--exe` output again agreed on every input
  tried, both untyped and with native types.
- **Native-type behaviour consistent with Rakudo:**
  - native `int` wraps on overflow: `9223372036854775807 + 1` gives
    `-9223372036854775808`;
  - binding a bignum or `Any` to a native refuses with `Cannot unbox …`;
  - an uninitialised `num` is `0`;
  - `my num $x = 2` is a compile-time error;
  - `**` on a `num` is still libm `pow`: 200,000 inputs, 0 differences from
    CPython's `x ** 2` in either mode.

## Appendix: probe script

Runs in either mode:

```bash
raku probes.raku
```

```bash
raku --exe -O probes.raku -o probes && ./probes
```

Each probe is a sub called in a loop, so the loop itself is not what is
measured. The empty-sub line is the floor to subtract.

```raku
sub bench($name, &f, $n) {
    my $t = now;
    f($n);
    printf "  %-40s %9.1f ns/op\n", $name, (now - $t) / $n * 1e9;
}
my $N = 200_000;

say "call floor";
sub p-empty($x) { $x }
bench 'empty sub', -> $n { for ^$n { p-empty(5) } }, $N;

say "Num arithmetic vs Int arithmetic";
sub p-num($a, $b) { $a * $b + $a }
sub p-int($a, $b) { $a * $b + $a }
bench 'Num: $a * $b + $a', -> $n { for ^$n { p-num(1.5e0, 2.5e0) } }, $N;
bench 'Int: $a * $b + $a', -> $n { for ^$n { p-int(3, 5) } }, $N;
sub p-numlt($a, $b) { $a < $b }
sub p-intlt($a, $b) { $a < $b }
bench 'Num: $a < $b', -> $n { for ^$n { p-numlt(1.5e0, 2.5e0) } }, $N;
bench 'Int: $a < $b', -> $n { for ^$n { p-intlt(3, 5) } }, $N;

say "bitwise ops vs div/%";
sub p-band($m) { $m +& 1125899906842623 }
sub p-mod($m)  { $m % 1125899906842624 }
sub p-shr($m)  { $m +> 50 }
sub p-div($m)  { $m div 1125899906842624 }
bench '$m +& (2**50 - 1)', -> $n { for ^$n { p-band(6034823946274365) } }, $N;
bench '$m % 2**50', -> $n { for ^$n { p-mod(6034823946274365) } }, $N;
bench '$m +> 50', -> $n { for ^$n { p-shr(6034823946274365) } }, $N;
bench '$m div 2**50', -> $n { for ^$n { p-div(6034823946274365) } }, $N;

say "Num -> Int";
sub p-dotint($x) { ($x * 4503599627370496e0).Int }
sub p-floor($x)  { floor($x * 4503599627370496e0) }
bench '($x * 2**52).Int', -> $n { for ^$n { p-dotint(2.68e0) } }, $N;
bench 'floor($x * 2**52)', -> $n { for ^$n { p-floor(2.68e0) } }, $N;

say "constant vs literal";
constant BIG = 1125899906842624;
sub p-const($m) { ($m % BIG) + ($m div BIG) }
sub p-lit($m)   { ($m % 1125899906842624) + ($m div 1125899906842624) }
bench 'two constant references', -> $n { for ^$n { p-const(6034823946274365) } }, $N;
bench 'two literals', -> $n { for ^$n { p-lit(6034823946274365) } }, $N;

say "type-object terms and type tests";
sub p-any() { Any }
sub p-nil() { Nil }
sub p-empty-list() { Empty }
sub p-paren-list() { () }
sub p-smart($v) { $v ~~ Int && $v !~~ Bool }
sub p-name($v)  { $v.^name eq 'Int' }
bench 'Any', -> $n { for ^$n { p-any() } }, $N;
bench 'Nil', -> $n { for ^$n { p-nil() } }, $N;
bench 'Empty', -> $n { for ^$n { p-empty-list() } }, $N;
bench '()', -> $n { for ^$n { p-paren-list() } }, $N;
bench '$v ~~ Int && $v !~~ Bool', -> $n { for ^$n { p-smart(5) } }, $N;
bench "\$v.^name eq 'Int'", -> $n { for ^$n { p-name(5) } }, $N;

say "comparisons";
sub p-chain($f) { 101 <= $f < 200 }
sub p-and($f)   { $f >= 101 && $f < 200 }
bench '101 <= $f < 200', -> $n { for ^$n { p-chain(150) } }, $N;
bench '$f >= 101 && $f < 200', -> $n { for ^$n { p-and(150) } }, $N;

say "Pair vs 2-element array";
my $pair = 'a' => 1.5e0;
my $arr = ['a', 1.5e0];
sub p-pair($p) { $p.key eq 'a' ?? $p.value !! 0 }
sub p-arr($p)  { $p[0] eq 'a' ?? $p[1] !! 0 }
bench '$p.key / $p.value', -> $n { for ^$n { p-pair($pair) } }, $N;
bench '$p[0] / $p[1]', -> $n { for ^$n { p-arr($arr) } }, $N;

say "labelled next (control exception) vs while/last";
sub p-labeled() {
    my $c = 0;
    ROW: for ^3 -> $i { for ^3 -> $j { next ROW if $j == 1; $c++ } }
    $c
}
sub p-flag() {
    my $c = 0;
    for ^3 -> $i { my $j = 0; while $j < 3 { last if $j == 1; $c++; $j++ } }
    $c
}
bench 'three `next ROW` from an inner loop', -> $n { for ^$n { p-labeled() } }, 20_000;
bench 'the same with while/last', -> $n { for ^$n { p-flag() } }, 20_000;

say "recursion: closure vs top-level sub (2**10 leaves)";
sub p-closure($depth) {
    my $leaves = 0;
    my sub walk($d) { if $d == 0 { $leaves++; return } ; walk($d - 1); walk($d - 1) }
    walk($depth);
    $leaves
}
sub p-walk($d, $acc) { if $d == 0 { $acc[0] = $acc[0] + 1; return }; p-walk($d - 1, $acc); p-walk($d - 1, $acc) }
sub p-toplevel($depth) { my $acc = [0]; p-walk($depth, $acc); $acc[0] }
bench 'my sub (closure) recursion', -> $n { for ^$n { p-closure(10) } }, 200;
bench 'top-level sub recursion', -> $n { for ^$n { p-toplevel(10) } }, 200;

say "unary minus vs binary minus on a Num";
sub p-neg($x)  { -$x }
sub p-zsub($x) { 0e0 - $x }
bench '-$x', -> $n { for ^$n { p-neg(1.5e0) } }, $N;
bench '0e0 - $x', -> $n { for ^$n { p-zsub(1.5e0) } }, $N;

say "Array methods";
my @ten = 1e0 xx 10;
sub p-elems(@a) { @a.elems }
sub p-defined($x) { $x.defined }
sub p-push3() { my @a; @a.push(1e0); @a.push(2e0); @a.push(3e0); @a }
sub p-lit3() { [1e0, 2e0, 3e0] }
bench '@a.elems', -> $n { for ^$n { p-elems(@ten) } }, $N;
bench '$x.defined', -> $n { for ^$n { p-defined(1.5e0) } }, $N;
bench 'my @a; three .push; return @a', -> $n { for ^$n { p-push3() } }, $N;
bench '[1e0, 2e0, 3e0]', -> $n { for ^$n { p-lit3() } }, $N;

say "a sub with an @ parameter vs a \$ parameter";
sub p-at(@x) { 0 }
sub p-dollar($x) { 0 }
bench 'sub (@x), passed an Array', -> $n { for ^$n { p-at(@ten) } }, $N;
bench 'sub ($x), passed the same Array', -> $n { for ^$n { p-dollar(@ten) } }, $N;

say "summing 10 Nums";
sub p-for(@a) { my $s = 0e0; for @a -> $x { $s = $s + $x }; $s }
sub p-while(@a) { my $s = 0e0; my $i = 0; my $k = @a.elems; while $i < $k { $s = $s + @a[$i]; $i++ }; $s }
bench 'for @a -> $x', -> $n { for ^$n { p-for(@ten) } }, $N;
bench 'while with @a[$i]', -> $n { for ^$n { p-while(@ten) } }, $N;
```

The second script measures native types (issues 15–17), in the same way:

```raku
sub bench($name, &f, $n) {
    my $t = now;
    f($n);
    printf "  %-52s %9.1f ns/op\n", $name, (now - $t) / $n * 1e9;
}
my $N = 200_000;

say "float arithmetic in a sub";
sub a-plain($a, $b) { $a * $b + 1e0 }
sub a-num(num $a, num $b --> num) { $a * $b + 1e0 }
sub a-Num(Num $a, Num $b --> Num) { $a * $b + 1e0 }
bench 'untyped ($a, $b)', -> $n { for ^$n { a-plain(1.5e0, 2.5e0) } }, $N;
bench '(num $a, num $b --> num)', -> $n { for ^$n { a-num(1.5e0, 2.5e0) } }, $N;
bench '(Num $a, Num $b --> Num)', -> $n { for ^$n { a-Num(1.5e0, 2.5e0) } }, $N;

say "integer arithmetic in a sub";
sub i-plain($m) { ($m % 1125899906842624) * 25 + ($m div 1125899906842624) }
sub i-int(int $m --> int) { ($m % 1125899906842624) * 25 + ($m div 1125899906842624) }
bench 'untyped ($m)', -> $n { for ^$n { i-plain(6034823946274365) } }, $N;
bench '(int $m --> int)', -> $n { for ^$n { i-int(6034823946274365) } }, $N;

say "comparison";
sub c-plain($a, $b) { $a < $b }
sub c-num(num $a, num $b) { $a < $b }
bench 'untyped Num < Num', -> $n { for ^$n { c-plain(1.5e0, 2.5e0) } }, $N;
bench 'num < num', -> $n { for ^$n { c-num(1.5e0, 2.5e0) } }, $N;

say "a float loop with no calls (the -O unboxed loop lane)";
sub ro-plain($x) { my $s = 0e0; my $i = 0; while $i < 8 { $s = $s + $x * 2e0; $i++ }; $s }
sub ro-num(num $x --> num) { my num $s = 0e0; my int $i = 0; while $i < 8 { $s = $s + $x * 2e0; $i++ }; $s }
sub rw-num(num $x is copy --> num) { my num $s = 0e0; my int $i = 0; while $i < 8 { $s = $s + $x * 2e0; $x = $x + 0e0; $i++ }; $s }
sub rw-plain($x is copy) { my $s = 0e0; my $i = 0; while $i < 8 { $s = $s + $x * 2e0; $x = $x + 0e0; $i++ }; $s }
bench 'untyped, $x only read in the loop', -> $n { for ^$n { ro-plain(1.5e0) } }, $N;
bench 'num, $x only read in the loop', -> $n { for ^$n { ro-num(1.5e0) } }, $N;
bench 'num, $x also written in the loop', -> $n { for ^$n { rw-num(1.5e0) } }, $N;
bench 'untyped, $x also written in the loop', -> $n { for ^$n { rw-plain(1.5e0) } }, $N;
```

The third script measures issue 22, in the same way:

```raku
sub bench($name, &f, $n) {
    my $best = 1e300;
    for ^3 { my $t = now; f($n); my $d = (now - $t) / $n * 1e9; $best = $d if $d < $best }
    printf "  %-46s %8.1f ns/op\n", $name, $best;
}
my $N = 300_000;
sub arr-pp(@a, $x) { @a.push($x); @a.pop }
sub elem-pp($acc, $x) { $acc[0].push($x); $acc[0].pop }
sub elem-p3($acc, $x) { my $c = $acc[0]; $c.push($x); $c.push($x); $c.push($x); $c.splice(1) }
sub arr-p3(@a, $x) { @a.push($x); @a.push($x); @a.push($x); @a.splice(1) }
sub elem-elems($acc) { $acc[0].elems }
sub arr-elems(@a) { @a.elems }
my @a = 1e0; my $acc = [[1e0],];
bench '@a.push($x); @a.pop', -> $n { for ^$n { arr-pp(@a, 1.5e0) } }, $N;
bench '$acc[0].push($x); $acc[0].pop', -> $n { for ^$n { elem-pp($acc, 1.5e0) } }, $N;
bench '@a: three .push, then .splice(1)', -> $n { for ^$n { arr-p3(@a, 1.5e0) } }, $N;
bench '$c = $acc[0]: three .push, then .splice(1)', -> $n { for ^$n { elem-p3($acc, 1.5e0) } }, $N;
bench '@a.elems', -> $n { for ^$n { arr-elems(@a) } }, $N;
bench '$acc[0].elems', -> $n { for ^$n { elem-elems($acc) } }, $N;
```
