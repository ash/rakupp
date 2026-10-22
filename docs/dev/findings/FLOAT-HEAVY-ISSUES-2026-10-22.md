# Float-heavy code under Raku++: 21 issues, interpreted and `--exe`

*First measured 2026-10-15 at 5.4.1-78-g52df8573, re-checked 2026-10-22 at
5.5.0, on Apple Silicon (arm64), macOS, at a load average of 3–5. Times are
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
- The probe scripts in the appendix reproduce the cost numbers in either mode.

## How it compares with other languages

The same computation was written in Rust, C++, TypeScript (Bun), Go and
Fortran, every version producing identical output. Each was timed against
CPython 3.12 in one interleaved run: the steady-state time per call, averaged
over six input sizes.

| | × CPython |
|---|--:|
| Fortran (gfortran `-O3`) | 176× |
| Go | 123× |
| Rust, C++, TypeScript | ~55× each |
| Raku++ 5.5.0, `--exe -O` | 3.2× (2.5× on 5.4.1) |
| Raku++ 5.5.0, `--exe -O`, native types | 2.2× (1.7×) |
| Raku++ 5.5.0, interpreted | 0.47× (0.44×) |

- **Fortran and Go lead on data layout, not language.** Their versions
  allocate nothing per call: integers and doubles in fixed arrays, and buffers
  reused. The Rust, C++ and TypeScript versions build a heap string per
  identifier, and about 65% of Rust's profile was malloc, free and string
  formatting.
- **Raku++'s gap is the same kind of cost on a larger scale.** Every value is a
  128-byte box, and every operation, array access and `.push` is a runtime
  call. The profile under [Found on 5.5.0](#found-on-550) shows no single
  hotspot.
- **Per construct, compiled Raku++ beats CPython only on scalar float work.**
  It is 2–10× cheaper there, and level with CPython on arrays and calls. Its
  speed-up falls from 3.5× on the smallest input to 2.2× on the largest,
  where a recursion of about 1,000 ten-argument calls dominates. A compiled
  call costs 23 ns against CPython's 36.

## Status on 5.5.0

| # | issue | on 5.5.0 |
|---|---|---|
| 1 | `Num.Rat(0)` is not exact | still present |
| 2 | one list literal bundles the whole program | still bundles; the bundle's routines are now native |
| 3 | `for $*IN.lines` buffers all of stdin (`--exe`) | **fixed** |
| 4 | `CATCH` or `Int(…)` bundles the whole program | `Int(…)` **fixed**; `CATCH` still bundles, routines native |
| 5 | no `Num` fast path | **fixed** for `+ - * /` and comparisons; unary minus is issue 18 |
| 6 | `+&` `+>` `+<` off the int fast path | **fixed** |
| 7 | labelled `next` is a control exception | **fixed**; still about 20× a `while`/`last` loop |
| 8 | type-object terms looked up by name | **fixed** |
| 9 | closures are not direct calls (`--exe`) | still present, 5.2× (was 6.5×) |
| 10 | a `use`d module's sub costs ~6× (`--exe`) | still present |
| 11 | smaller costs | three of four **fixed**; `constant` still costs |
| 12 | a Failure stored in a native becomes `2` | **fixed**: dies |
| 13 | a typed `for` parameter breaks the `--exe` build | **fixed** |
| 14 | native arrays bundle the whole program | still bundles; routines native |
| 15 | the loop lane gives a read-only variable an int slot | **fixed** |
| 16 | the code generator ignores native types | partly: fast arms added, still boxed; `num` code 3× untyped |
| 17 | native types leave the interpreter's kernels | **fixed** for `int`, not for `num` |
| 18 | unary minus on a `Num` is generic | new |
| 19 | Array methods dispatch by name (`--exe`) | new |
| 20 | a sub with an `@` parameter takes the `ValueList` path | new |
| 21 | iterating a small array costs about what CPython does | new |

## Behaviour

### 1. `Num.Rat(0)` does not return the exact value of the double

```raku
say 2.6799999999999997e0.Rat(0).nude;
say 2.6799999999999997e0.Rat(1e-300).nude;
say 0.1e0.Rat(0).nude;
```

| | actual (both modes) | expected |
|---|---|---|
| `2.6799999999999997e0.Rat(0)` and `.Rat(1e-300)` | `(392892154991943 146601550370128)` | `(377176468792279 140737488355328)` |
| `0.1e0.Rat(0)` | `(1 10)` | `(3602879701896397 36028797018963968)` |

The expected values have a power-of-two denominator, the double's exact
binary value. That is what Rakudo returns, and what CPython's
`float.as_integer_ratio()` returns.

- **Impact:** any exact-arithmetic code that relies on `.Rat(0)` to recover a
  double's true value gets a nearby rational instead.
- **Workaround:** decompose the double by hand: scale by powers of two, which
  is exact, and take `floor()`.
- **On 5.5.0:** unchanged in both modes; the same three outputs.

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
