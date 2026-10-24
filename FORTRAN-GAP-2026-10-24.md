# The Fortran gap on a call-heavy float program, and what would close it

*2026-10-24, at 5.5.0-59-g64313848 (a clean tree), on an Apple M4, macOS, at a
load average of 2.3–3.0. Whole-program times are the fastest of 5 interleaved
rounds; kernel times are the best of 3 interleaved rounds. The references are
gfortran 16.2.0 (Homebrew GCC) `-O3` and Apple clang 21.0.0 `-O2`, both with
`-ffp-contract=off`.*

*The subject is the float-heavy benchmark program of
[FLOAT-HEAVY-ISSUES-2026-10-22.md](FLOAT-HEAVY-ISSUES-2026-10-22.md), and three
kernels shaped like its hot paths (§4). Two notes border this one:*

- *[FORTRAN-LESSONS-2026-10-15.md](FORTRAN-LESSONS-2026-10-15.md) covers
  Fortran's long loops over large arrays. This program has none.*
- *[NUM-UNBOX-PLAN.md](../plans/NUM-UNBOX-PLAN.md) is the plan for compiled
  float code without the box. This note measures where the plan's subject
  stands, answers two of its open questions, and adds what the Fortran
  version of the program shows that the plan does not cover.*

---

## The short answer

- **Compiled, Raku++ runs this program 51× slower than Fortran.** `--exe -O`
  is 3.5× CPython and gfortran is 178×. The 51 commits since 5.5.0-8 did not
  move it.
- **The gap has two factors.**
  - **About 3× is the program's data layout.** The Rust, C++ and TypeScript
    versions share the Raku version's layout and run at 55×.
  - **About 16× is the engine.** The same layout runs 16× faster in Rust than
    in Raku++.

  Fortran-class speed needs both: a program shaped like the Fortran version,
  and an engine that compiles that shape to plain machine code.
- **None of the time is arithmetic.** libm takes about a quarter of the
  Fortran version's time and 0.3% of the compiled Raku++ profile. There is no
  bigint work at all. The rest is boxes: 80-byte `Value`s built, checked and
  freed around every operation, element read and call.
- **Writing the program the Fortran way makes it slower today, not faster.**
  - Three kernels written with native types and fixed tables, as the Fortran
    version writes them, run 1.1–1.6× slower under `--exe -O` than the same
    kernels untyped.
  - With native arrays the whole program bundles. `-O` then makes it 2×
    slower still, through a bug (§5.1).
- **`--cnp` cannot reach it.** It tiers up loops, and 45% of the interpreted
  time is a recursion with no loop in it. It gives the program 4–6%.
- **What would close the engine's share** is one step beyond NUM-UNBOX-PLAN:
  a routine whose parameters, locals and result are all declared native
  compiles to a plain C++ function, with no guard inside and no bail.
  - Native semantics already are machine semantics: an `int` wraps and a
    `num` is a double.
  - So the only exit such a routine needs is one the boxed code takes too:
    division by zero, which can throw from the same point.
  - With native arrays compiled natively (the plan's P2) and their elements
    read as words (P3), the kernels' Fortran form would emit what the C
    version is.

---

## 1. Where it stands

The program, every mode, against the other languages in the same interleaved
run. Times are µs per call. The ratio is the mean over six input sizes:

| | middle input | largest input | × CPython |
|---|--:|--:|--:|
| CPython 3.12 | 241 | 600 | 1× |
| Fortran (gfortran `-O3`) | 1.30 | 5.24 | **178×** |
| Go | 1.88 | 6.51 | 123× |
| Rust | 4.40 | 12.1 | 56× |
| C++, TypeScript (Bun) | 4.03–4.55 | 12.7–15.7 | 55× |
| Raku++ `--exe -O` (that is, `-O2`) | 63.2 | 261 | **3.49×** |
| Raku++ `--exe -O3` | 62.6 | 261 | 3.51× |
| Raku++ `--exe` | 100 | 580 | 2.08× |
| Raku++ `--exe --cnp` | 99.8 | 580 | 2.07× |
| Raku++ interpreted | 411 | 2,366 | 0.49× |
| Raku++ `--cnp` | 384 | 2,257 | 0.51× |

The native-typed copy of the program is slower in every compiled mode: 2.67×
with `-O`, 1.76× without.

- **Against 5.5.0-8, interleaved, it is 1.00×** in every mode on both copies.
  The 5.5.0-8 interpreter was rebuilt from that commit for the comparison.
- **The commits in between were loop work,** and the program has no loop they
  reach:
  - FORTRAN-LESSONS items 1a, 1b and 2: typed loop parameters, loop-header
    natives, and packed-array words under `--cnp`;
  - native-array store semantics.
- **`-O3` matches `-O2`.** `-O` already passes clang `-O2`. Raku++'s own `-O`
  passes are the 1.6–1.75× over plain `--exe`.
- **`--exe --cnp` is `--exe`.** The compile modes accept `--cnp` and ignore
  it (the comment above `g_jitOpt` in [main.cpp](../../../src/main.cpp)).
- **`--cnp` compiles 10 of the program's 25 loops** (12 of 29 in the
  native-typed copy). The routines it reaches get faster: one 1.69×, one
  1.46×. Those routines hold about 10% of the interpreted time. The recursion
  holds 45%.

## 2. The gap, decomposed

| input | Rust ÷ Fortran | Raku++ `-O` ÷ Rust | Raku++ `-O` ÷ Fortran |
|---|--:|--:|--:|
| smallest | 3.8× | 16.9× | 64× |
| middle | 3.4× | 14.4× | 48× |
| largest | 2.3× | 21.6× | 50× |
| mean of six | **3.1×** | **16.6×** | **51×** |

**The first column is layout, not language.** FLOAT-HEAVY-ISSUES reports it:

- The Fortran version keeps identifiers as integers in fixed arrays, writes
  results into tables it owns, and formats text only at the end. It allocates
  nothing per call, and its profile has 0% in malloc.
- The Rust version builds a heap string per identifier and a fresh vector
  per result, and 65% of its profile is malloc, free and formatting.
- The Raku version has Rust's layout.

**The second column is the engine.** It grows at the largest input, where a
recursion of about 1,000 calls per call dominates, and a compiled Raku++ call
is far from a C call (§4).

## 3. Where the compiled time goes

A `sample` profile of the program under `--exe -O` (`--slim=safe,+symbols`),
middle and largest input. Each sample counts once, for the function at the
top of the stack, and the waiting main thread is left out:

| share of samples | middle | largest |
|---|--:|--:|
| the generated functions' own code: inlined `Value` construction, teardown and tag checks | 29.8% | 36.6% |
| array and argument-list machinery: `RVec<Value>` growth, copy and teardown, `rtIndexGet`, `rtIndexRef`, `listToArray`, `Value::array`, `rtMPush` | 27.9% | 20.7% |
| boxed scalar operations: `rtAdd`, `rtMul`, `rtSub`, `rtLtB`, `rtGe`, `rtEq`, `boolify`, `truthy`, `toInt` | 21.9% | 29.0% |
| malloc, free, memmove | 7.8% | 4.6% |
| methods dispatched by name (`methodCall…`) | 2.4% | 0.9% |
| `Value` copies outside the generated code | 1.8% | 2.7% |
| thread-local storage (`_tlv_get_addr`) | 1.4% | 0.7% |
| the subscript key check (`detonateFailureKey`) | 0.9% | 2.0% |
| libm (`pow`) | 0.3% | 0.3% |
| bigint arithmetic | 0% | 0% |
| the rest (strings, payload release, `numToIntExact`, …) | 5.8% | 2.8% |

The largest single function at the largest input is the recursive routine's
own generated code, with 16.6% of samples.

**This answers NUM-UNBOX-PLAN's open question.** The plan found 88% of the
5.5.0-8 profile in what P1–P4 address, and guessed that the other 12% was
the exact decimal rounding on bigints. It is not:

- the rounding stays on the `Int` fast path, so no bigint code runs;
- libm, the only work the program has to do, is 0.3%;
- what is left is more box handling: `Value` copies, the key check,
  thread-local reads and payload release.

So the plan's bound of "about 8×, from the other 12%" is pessimistic. The
remainder is the same kind of cost as the 88%.

**The subscript key check is new since 5.5.0-8** (85d56001, a Failure used as
a subscript throws). `rtIndexGet` calls `detonateFailureKey` out of line on
every read. For an `Int` key that is two tag tests behind a call, and at the
largest input it is 2% of samples. Testing `key.t` inline at the call, and
calling only for a Hash or Array key, would remove it.

## 4. The Fortran form, measured

Three kernels with the shapes of the program's hot paths:

- **walk:** a binary recursion of about 500 calls, with three `int`
  parameters and one `num`. At each leaf it updates a small table keyed by
  two integers.
- **norm:** a 6-element `num` array, summed and normalized.
- **round:** an exact rounding of a `num`, done in integers that fit in 64
  bits.

Each is written five ways:

| form | |
|---|---|
| C | fixed arrays, nothing allocated per call |
| Fortran | the same |
| untyped Raku | as the program writes it: context passed as arguments, tables and result arrays built per call |
| the Fortran form in Raku, plain arrays | native scalars, tables allocated once at file scope, nothing built per call |
| the Fortran form in Raku, native arrays | the same with `my num @` and `my int @` tables |

Every form prints the same three checksums, to the last digit, as C and
gfortran, in every mode.

ns per call, best of 3 interleaved rounds (× C in brackets):

| | walk | norm | round |
|---|--:|--:|--:|
| C (clang `-O2`) | 833 | 3.4 | 1.8 |
| gfortran `-O3` | 887 (1.1×) | 3.4 (1.0×) | 2.1 (1.2×) |
| untyped, `--exe -O` | 52,873 (63×) | 793 (233×) | 84.9 (47×) |
| Fortran form, plain arrays, `--exe -O` | 75,580 (91×) | 844 (248×) | 135 (75×) |
| Fortran form, native arrays, `--exe -O` (bundled) | 556,534 (668×) | 4,179 (1,229×) | 473 (263×) |
| untyped, `--exe` | 139,343 (167×) | 1,376 (405×) | 173 (96×) |
| Fortran form, plain arrays, `--exe` | 151,585 (182×) | 1,225 (360×) | 216 (120×) |
| Fortran form, native arrays, `--exe` (bundled) | 273,977 (329×) | 4,353 (1,280×) | 469 (261×) |
| untyped, interpreted | 537,205 (645×) | 3,889 (1,144×) | 765 (425×) |
| untyped, `--cnp` | 530,331 (637×) | 4,071 (1,197×) | 822 (457×) |
| Fortran form, plain arrays, interpreted | 696,690 (836×) | 7,642 (2,248×) | 700 (389×) |
| Fortran form, plain arrays, `--cnp` | 698,793 (839×) | 2,119 (623×) | 776 (431×) |
| Fortran form, native arrays, interpreted | 615,376 (739×) | 5,580 (1,641×) | 700 (389×) |
| Fortran form, native arrays, `--cnp` | 619,824 (744×) | 1,522 (448×) | 772 (429×) |

- **Under `--exe -O`, the untyped form is the fastest Raku form.** The
  Fortran form with plain arrays is 1.43× slower on walk, 1.06× on norm and
  1.59× on round.
- **gfortran and clang agree within 6–17%,** so C is a fair stand-in for
  Fortran here. (NUM-UNBOX-PLAN used C because gfortran was not installed; it
  is now.)
- **`--cnp` helps only norm's Fortran form, by 3.6–3.7×.** That is the one
  kernel whose time is in loops: counted loops over arrays.
  - walk is recursion, and round has no loop.
  - The untyped norm's two `for @p -> $x` loops compile too, and `--cnp=stats`
    counts about 2,000 kernel entries over 1,000 calls. They run no faster.
- **The bundled form loses 2× to `-O` on walk.** That is §5.1: under `-O`,
  the table-update routine's native body does not compile, so every leaf
  update runs interpreted.

### What the Fortran form compiles to

The kernels compile the way any routine of the same shape does. A small one:

```raku
my @t = 0e0 xx 8;
sub f(int $n, int $i, num $x) {
    if $n == 0 { @t[$i] = @t[$i] + $x; return }
    my num $y = $x * 0.5e0;
    f($n - 1, $i, $y);
    f($n - 1, ($i + 1) % 8, $x - $y);
}
sub g(int $a --> int) { my int $b = ($a % 1000) * 3; $b div 7 }
```

Under `--exe -O`, trimmed:

- each parameter is guarded and bound as a `Value`;
- every native operation is a runtime call;
- every native store is checked by name;
- the routine sits inside a `try` for `return`.

```cpp
static Value u_f(Value v_sn, Value v_si, Value v_sx) {
    if (!rtTypedFast(v_sn, RTB_MACHINE_INT)) { … v_sn = RT.bindTypedParam(std::move(v_sn), __pd0); }
    itemizeScalar(v_sn);
    …                                    // the same for $i and $x
    try {
    if (([&]() -> bool { do { if (!(rtIntBox(v_sn))) break; return (v_sn.i == 0LL); } while (0); return rtEqB(v_sn, Value::integer(0LL)); }())) {
        rtIndexRef(v_at, v_si, false) = rtAdd(rtIndexGet(v_at, v_si, false), v_sx);
        return Value::nil();
    }
    Value v_sy = rtNativeStoreK(rtNativeMul(v_sx, Value::number(0.5)), 1, "num", "$y", true);
    rtSinkStmt(RT, u_f(rtNativeSub(v_sn, Value::integer(1LL)), v_si, v_sy));
    return u_f(rtNativeSub(v_sn, Value::integer(1LL)), rtNativeArith("%", rtNativeAdd(v_si, Value::integer(1LL)), Value::integer(8LL)), rtNativeSub(v_sx, v_sy));
    } catch (ReturnEx& __r) { if (__r.target) throw; return __r.v; }
}
```

The C it should be:

```c
static void f(int64_t n, int64_t i, double x) {
    if (n == 0) { t[i] = t[i] + x; return; }
    double y = x * 0.5;
    f(n - 1, i, y);
    f(n - 1, (i + 1) % 8, x - y);
}
```

`g` shows why `my int` costs more than an untyped `Int`:

```cpp
Value v_sb = rtNativeStoreK(rtNativeMul(rtNativeArith("%", v_sa, Value::integer(1000LL)), Value::integer(3LL)), 2, "int", "$b", true);
return rtNativeArith("div", v_sb, Value::integer(7LL));
```

- **`%` and `div` on an `int` go through `rtNativeArith`, with the operator
  as a string.** The untyped code calls `rtMod` and `rtDiv`, which have `Int`
  fast paths.
- **Every assignment to a native passes `rtNativeStoreK`** with the type and
  the variable's name, even where the value is already that native.

FLOAT-HEAVY-ISSUES issue 16 says natives now run "at the untyped code's speed
on every probe". The probes were single operations; in routines that mix
them, natives are still slower.

## 5. Found on the way

### 5.1 In a bundled program, `-O` silently drops a routine that updates a file-scope variable

```raku
my num @a = 1e0;                # a native array anywhere: the program bundles
my int $n = 0;
sub f(int $x) { $n++; $x + 1 }
say f(41), " ", $n;
```

- **Actual:** with `--exe -O`, `RAKUPP_AOT_REPORT` says `sub f`
  `interpreted  its native body did not compile`. `RAKUPP_AOT_VERBOSE=1`
  shows the error: `use of undeclared identifier 'v_sn'`. The answer is
  right (`42 1`); only the speed is lost.
- **Expected:** `native`, as it is without `-O`. Without `-O` the same
  statement reaches `$n` through `rtAotOuter(__env, "$n")`.
- **Which statements:** `$n++`, `++$n`, `$n--` and `$n = $n + 1` on a
  file-scope variable, native or not, drop the routine. `$n += 1` and plain
  reads of `$n` compile.
- **Impact:**
  - walk's bundled Fortran form takes 556 µs with `-O` against 274 µs
    without, because its table-update routine (a `++` on a file-scope `int`)
    runs interpreted.
  - A 4-parameter recursion that counts its calls in a file-scope `int`
    costs 400 ns a call with `-O` and 250 without. 400 ns is what the
    interpreter takes.
- **Why the gates miss it:** `t/exe/run.raku` holds output, and the output
  is right. A gate would compile one such program with `-O` and
  `RAKUPP_AOT_REPORT`, and require `native`.

### 5.2 `try` around a returned Failure: compiled and interpreted disagree

```raku
sub h($a, $b) { $a / $b }
my $r = try { h(1e0, 0e0) };
say $r.^name, " | ", $!.^name;
```

| | `$r` | `$!` |
|---|---|---|
| interpreted | `Any` | `X::Numeric::DivideByZero` |
| `--exe`, with and without `-O` | `Failure` | `Nil` |

The interpreted run disarms the Failure the block returns and sets `$!`. The
compiled run hands back the live Failure, with `$!` unset. The interpreter
is the oracle `t/exe/run.raku` holds `--exe` to, so the compiled answer is
the one to change.

## 6. What Fortran-class needs, ranked

Each item names the kernel row it moves. An estimate is labelled as one.

### 1. Fix §5.1 (S)

The `-O` paths for `++`, `--` and `$v = $v op …` should reach an outer
variable through the AOT outer table, as the plain path does, or stand
aside in an AOT body. Moves the bundled walk from 556 to 274 µs.

### 2. Native operations without dispatch by name (S)

- `%` and `div` on two `int`s should get the direct helpers `+`, `-` and
  `*` have, or an inline arm, rather than `rtNativeArith("%", …)`.
- A store of a value that is already the target's native should skip
  `rtNativeStoreK`'s checks.

This is FLOAT-HEAVY-ISSUES issue 23 seen from the code. Measuring stick:
round's Fortran form, 135 ns, should come down to the untyped 85 ns or
below. A native declaration should never cost more than leaving it out.

### 3. Native routines as plain functions (M): the Fortran lesson for calls

**The Fortran lesson:** a declared type is never guarded (FORTRAN-LESSONS
section 1). That note applied it to loops. This program needs it for
routines.

**The rule.** A routine compiles to a plain C++ function when all of these
hold:

- every parameter is declared `int` (or a sized int) or `num`;
- every local is native;
- the result is native, or there is none;
- the body calls only other such routines.

For example:

```cpp
static void f_f(int64_t n, int64_t i, double x);   // §4's f
```

**Why it needs no bail.** NUM-UNBOX-PLAN's P1 twin is a speculative copy of a
routine whose types are inferred. It must bail and re-run the boxed body
exactly (KERNEL-PLAN contract 2), so it may write nothing outside itself. A
native routine is not speculative:

- **`int` arithmetic wraps,** in both modes, so it has no overflow exit:

  ```raku
  my int $x = 9223372036854775807; $x++; say $x;   # -9223372036854775808
  ```

- **A `num` is a double.**
- **A `num` division by zero fails,** in both modes, and the Failure throws
  as soon as it is stored in a native. A native routine throws what the
  boxed body throws, from the same point. Every write it made before the
  throw is a write the boxed body would also have made, so nothing needs
  undoing.

**What that opens.** With no re-run, a native routine may write outside
itself:

- to file-scope natives and native arrays, which is how the Fortran version
  keeps its tables (module variables);
- through native array parameters.

The P1 contract forbids exactly that shape.

**At the boundary:**

- **A call from boxed code** goes through a `Value` wrapper, which binds the
  arguments once: today's `rtTypedFast` and `bindTypedParam`.
- **A call between native routines** is a direct C++ call with unboxed
  arguments. Clang then inlines leaf routines such as walk's table update.

**What refuses it:** the `planKernels` refusals (`wrap`, multis, protos,
methods, traits, `is rw`, exports), plus a native captured by a closure.

**Measuring stick:** walk's Fortran form, today 75.6 µs, 91× C. The
recursion's own calls stop paying the guard, the bind, the `try` and four
`rtNative*` calls each. The table lives in arrays, so the rest needs items 4
and 5. *Estimate:* with 3–5, within about 2× of C, the bounds checks being
most of what remains.

### 4. Native arrays compiled natively (M): NUM-UNBOX-PLAN P2

Today one `my num @a` anywhere bundles the program. That is the 668× row: the
Fortran form, written the way Fortran users write it, compiles worst of all.
P2 as planned. Moves the bundled rows onto the plain-array rows, and then
below them, with item 5.

### 5. Element reads and writes as word loads (M): NUM-UNBOX-PLAN P3

As planned, with one simplification inside a native routine. Its writes need
no undo log, because it never re-runs (item 3). P3b's undo log is only for a
speculative twin.

- **Reads and writes:** `@t[$i] = @t[$i] + $x` becomes a bounds check and
  a word store. `--cnp` already does this since 4405241a.
- **Fixed-size native arrays,** `my num @t[64]`: a 1-D shaped array cannot
  grow, so its word pointer and length are fixed for its life. A loop bounded
  by a variable no greater than the shape then needs one check at entry,
  Fortran's fixed trip count (FORTRAN-LESSONS item 2).

Measuring sticks: norm's Fortran form, 844 ns (248× C), and walk's table
update.

### 6. The program as written, untyped (M–L): NUM-UNBOX-PLAN P1, P1b, P4

The plan's estimate for the untyped program is about 29× CPython. §3 says the
cap is looser than the plan's 8×, since its remainder is boxes too. This is
the route for code that declares nothing. It cannot reach Fortran, because
the program's layout costs 3× before the engine starts (§2).

### 7. `--cnp` routine kernels (M–L)

`--cnp` compiles loops, and this program's time is in calls. A routine that
item 3 would compile is also a routine `--cnp` could stitch at its Nth call,
for the same reason: with no speculation there is no bail to land. 07508052
already compiles a small sub in place inside a loop kernel; this would make
the routine itself the unit. Moves walk and round, which no loop tier can
reach.

### 8. The program's own part

Not an engine change, and the factor of about 3 that Fortran's version has
over Rust's:

- identifiers as integers in fixed tables;
- results written into tables the caller owns;
- text produced once, at the end.

In Raku that is the Fortran form of §4, with native arrays. Items 1–5 are
what would let that form run at its C speed.

### What it adds up to

- **Today:** the program as written runs 51× slower than Fortran. Its
  Fortran form is slower still under `-O`.
- **After items 1–2:** a native declaration costs nothing, and the Fortran
  form is no slower than untyped.
- **After items 3–5:** the Fortran form's kernels should come within about
  2× of C (*estimate*). The whole program is then bound by what those
  kernels do not cover, parsing input and building output, which NUM-UNBOX
  P5 would measure.
- **The untyped program,** through item 6, gets the plan's 29× CPython or
  somewhat more, short of Rust's 55×.

---

## Appendix: re-measuring

**§4.** The kernel sources are not in this repository. The forms ran three
interleaved rounds:

- C and Fortran at 200,000 iterations;
- each Raku form under `--exe` and `--exe -O` at 5,000;
- each Raku form interpreted and under `--cnp` at 1,000.

At 300 iterations, C, Fortran and every Raku form and mode print the same
three checksums.

**§3.** The whole-program profile: build the program with
`--exe -O --slim=safe,+symbols`, run one input size for 10 s or more, and
take `sample <pid> 8 1`. Bucket the "Sort by top of stack" section by
function name.

**§5.1:**

```bash
RAKUPP_AOT_REPORT=/dev/stdout RAKUPP_AOT_VERBOSE=1 build/rakupp --exe -O repro.raku -o /tmp/r
```
