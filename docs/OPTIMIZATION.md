# Raku++ — the `-O` optimizer

Raku++ runs a program three ways (see [BENCHMARKS.md](BENCHMARKS.md) for the
speed picture):

- **interp** — tree-walk the AST (the default).
- **`--aot` / `--bundle`** — standalone binaries that still *tree-walk* an
  embedded program, so they run at interpreter speed.
- **`--exe`** — transpile the program to C++ and compile it to a native binary,
  with no interpreter inside. This is the only mode whose runtime performance
  differs, and the only mode the optimizer touches.

For how `--exe` codegen fits the pipeline see [ARCHITECTURE.md](ARCHITECTURE.md)
(§4); it emits C++ that calls the *same* runtime the interpreter uses (`Value`,
`applyArith`, …), documented in [RUNTIME.md](RUNTIME.md). [NATIVE.md](NATIVE.md)
compares compiled vs. interpreted on the example programs.

`--exe` (and its inspection twin `--cpp`, which prints the generated C++ instead
of compiling it) accept **`-O`**. This document is about what `-O` does.

Everything under `-O` is **semantics-preserving** — it changes *how* the compiled
program computes, never *what* it computes. It is opt-in and off by default.

```sh
rakupp --exe    prog.raku -o prog     # default: no optimizer
rakupp --exe -O prog.raku -o prog     # optimizer on
rakupp --cpp -O prog.raku             # print the optimized C++ to stdout
```

## What the generated code looks like without `-O`

By default the transpiler is faithful but generic: every value is a boxed
`Value`, operators in **value position** go through the runtime's string-keyed
dispatcher (`applyArith`), and every user-sub call packs its arguments into a
`ValueList` (a `std::vector<Value>` — a heap allocation per call). (Two dispatch
cuts apply even without `-O`, because they are plumbing rather than speculation:
comparisons in **conditions** use the inline `rtLtB`/`rtEqSB`-family helpers,
and builtin calls go through pointers cached once at startup — see
[dev/DISPATCH.md](dev/DISPATCH.md).)

```cpp
// sub fib($n) { $n < 2 ?? $n !! fib($n-1) + fib($n-2) }
static Value u_fib(ValueList __a) {
    Value v_sn = rtPos(__a, 0);
    return rtLtB(v_sn, Value::integer(2LL))     // condition: inline int compare (always on)
         ? v_sn
         : applyArith("+", u_fib(ValueList{applyArith("-", v_sn, Value::integer(1LL))}),
                           u_fib(ValueList{applyArith("-", v_sn, Value::integer(2LL))}));
}
```

For a hot recursive function this is two costs on every one of ~1.6M calls: a
`ValueList` `malloc`, and the value-position `applyArith` calls that dispatch on
the op *string* before touching the operands.

## The passes

### 1. Direct-arity calls (skip the per-call `ValueList`)

A sub whose signature is entirely **plain required positional scalars** (no
named, slurpy, optional, defaulted, or destructured params) gets a direct-`Value`
overload — its parameters *are* the C++ arguments — plus a boxed adapter so any
unusual call site still resolves. C++ overload resolution picks the right one.

```cpp
static Value u_fib(Value v_sn) { … }                                 // fast overload
static Value u_fib(ValueList __a) { return u_fib(rtPos(__a, 0)); }   // adapter
```

A call site takes the fast overload when it passes exactly the right number of
**plain positional** arguments (no `:name(…)` pairs, no `|@slurp`); otherwise it
calls through the adapter unchanged. Multi subs, indirect calls (`&fib`), and
method calls are untouched.

The same idea covers a growing set of **named builtins**: `abs`, `chr`, `ord`
have real C++ functions (`rtBAbs`/`rtBChr`/`rtBOrd`), and a single-plain-arg
call site emits the direct call — no `ValueList`, no `std::function`, and for
`abs` an inline plain-Int hot path at the call site. An `abs`-heavy loop went
1112.9 → 198.3 ms (5.6×); details and the general recipe in
[dev/DISPATCH.md](dev/DISPATCH.md).

### 2. Inline int64 arithmetic (skip the string dispatch and boxing)

For the common operators the codegen emits inline helpers (declared `inline` in
`Interpreter.h`) instead of `applyArith("…", …)`:

| ops | helper | fast path |
|---|---|---|
| `+` `-` `*` | `rtAdd`/`rtSub`/`rtMul` | native `int64`, overflow → bignum |
| `**` | `rtPow` | integer power by squaring, overflow → bignum |
| `<` `<=` `>` `>=` `==` `!=` | `rtLt`/… | `int64` compare |
| `%` `%%` `div` | `rtMod`/`rtDivides`/`rtDiv` | `int64` mod / divisibility / floor division |
| `~` | `rtConcat` | direct `std::string` concat when both are `Str` |
| `eq` `ne` `lt` `gt` `le` `ge` | `rtEqS`/… | byte-wise compare when both are **plain** `Str`s (no Version/IO/Buf tag, no enum identity) — tagged values fall back to the full chain. These matter more than the int ops: string comparisons sit *late* in `applyArith`'s dispatch chain (~118 ns vs ~10 ns direct — see [dev/DISPATCH.md](dev/DISPATCH.md)) |

Each inlines its fast case and falls back to `applyArith` for everything else:

```cpp
inline Value rtAdd(const Value& l, const Value& r) {
    long long z;
    if (rtBothInt(l, r) && !__builtin_add_overflow(l.i, r.i, &z)) return Value::integer(z);
    return applyArith("+", l, r);   // Rats, bignums, mixed types, coercions
}
```

`rtBothInt` is `l.t == VT::Int && r.t == VT::Int && !l.big && !r.big`. Integer
overflow is detected with `__builtin_*_overflow` and promotes to bignum, exactly
as `applyArith` does. This pass covers both binary operators and compound
assignment (`$s += …`).

With both passes (plus pass 3's condition lane), `fib` transpiles to:

```cpp
static Value u_fib(Value v_sn) {
    return ([&]() -> bool {                        // pass-3 condition lane:
        do { if (!(rtIntBox(v_sn))) break;         //  guard the box…
             return (v_sn.i < 2LL); } while (0);   //  …compare as raw int64
        return rtLtB(v_sn, Value::integer(2LL));   //  guard failed: boxed compare
    }())
         ? v_sn
         : rtAdd(u_fib(rtSub(v_sn, Value::integer(1LL))),
                 u_fib(rtSub(v_sn, Value::integer(2LL))));
}
```

No heap allocation, no string dispatch — pure inlinable code. Under a real C++
optimizer this collapses to tight native-int recursion.

### 3. Guarded native-int expression lanes (skip the `Value` box entirely)

Passes 1–2 still build a boxed `Value` for every intermediate result — `$sum +=
$_ * 2 - 1` constructs four `Value`s per evaluation even with `rtAdd`/`rtSub`/
`rtMul`. Pass 3 removes them: a straight-line integer expression whose leaves
are **int literals and plain scalar variables** is computed in raw `int64`.
Each `Value` leaf is tag-guarded at runtime (`rtIntBox`: an `Int`, not a
bignum), each op is overflow-checked, and the result is stored **into the
target's existing box** (`.i`) with no `Value` construction at all. Any guard,
overflow, or domain failure falls through to the untouched boxed emission —
lane leaves are pure (literals and variable reads), so re-evaluating them on
the slow path is safe.

```cpp
// $sum += $_ * 2 - 1        (inside a native range loop; __i3 is the counter)
{ bool __lok = false; do { // -O int lane
    if (!(rtIntBox(v__t0) && rtIntSlot(v_ssum))) break;
    long long __t1; if (rakupp::mul_ovf(v__t0.i, 2LL, &__t1)) break;
    long long __t2; if (rakupp::sub_ovf(__t1, 1LL, &__t2)) break;
    long long __t3; if (rakupp::add_ovf(v_ssum.i, __t2, &__t3)) break;
    v_ssum.i = __t3; __lok = true;
} while (0);
if (!__lok) { v_ssum = rtAdd(v_ssum, rtSub(rtMul(v__t0, Value::integer(2LL)), Value::integer(1LL))); } }
```

The lane applies to:

- **statement-position assignment** to a plain scalar — `$x = <int expr>` and
  `$x += -= *= %= <int expr>` (in-place stores additionally require
  `rtIntSlot`: not an enum-typed box, whose stringification is its name);
- **statement-position `++`/`--`** on a plain scalar;
- **conditions** (`if`/`while`/`until`/ternary) that are int comparisons or
  `%%` — the whole comparison evaluates unboxed inside a `bool` lambda.

Ops covered: `+ - *` (overflow-checked, promoting via the boxed path), unary
minus, `%` (floored, mirroring `rtMod`'s int case bit-for-bit; a zero divisor
falls to the boxed path, which throws), `%%`, and the six comparisons.
Everything else — `Num`s, strings, `Rat`s, bignums, array elements, method
calls — fails the lane at compile time or its guards at runtime and takes the
boxed route unchanged.

## A related default: in-place `~=` (not gated by `-O`)

`$s ~= …` naively rebuilds the whole string each step — `$s = $s ~ "x"` copies
the growing buffer every iteration, so *n* appends do O(n²) work. That is a
correctness wart, not a missing optimization (the interpreter and Rakudo both
build strings in O(n)), so both the tree-walker and the `--exe` codegen now
append into the accumulator's existing buffer **by default** via `rtCatAssign`:

```cpp
inline void rtCatAssign(Value& l, const Value& r) {
    if (l.t == VT::Str && r.t == VT::Str) { l.s += r.s; return; }  // O(1) amortized
    l = applyArith("~", l, r);                                     // anything else
}
```

It applies to scalar and element (`@a[i] ~=`, `%h{k} ~=`) targets; non-`Str`
operands fall back to `applyArith`. The interpreter pairs it with *sink context*:
a loop body's value is discarded, so the assignment doesn't copy its (growing)
result either. Because this is now the default in every mode, `-O` adds nothing
on top of it — `strcat` looks flat between `--exe` and `--exe -O`.

**Native-bool conditions** are the same idea. An `if`/`while`/ternary condition
that is a comparison (`$n < 2`) used to compile to `RT.boolify(rtLt(…))` — build a
`Bool` `Value`, then read it back. The codegen now emits a `bool`-returning helper
(`rtLtB`/`rtLeB`/…, joined by the string forms `rtEqSB`/`rtLtSB`/… — see
[dev/DISPATCH.md](dev/DISPATCH.md)) straight into the condition, skipping the
round-trip. It is default (not `-O`-gated); on `fib`, whose ternary runs 1.6M
times, it took `--exe` from 186 → 165 ms and `--exe -O` from 84 → 66 ms; on
`streq` the string form is most of a 15× cut in plain `--exe`.

## Forwarding the C++ optimization level

`--exe` compiles the generated C++ at **`-O2`** by default. A level on the `-O`
flag is passed straight through to the C++ compiler:

| flag | codegen passes | C++ compile |
|---|---|---|
| *(none)* | off | `-O2` |
| `-O` | on | `-O2` |
| `-O3` / `-Os` / `-Ofast` / `-O0` / … | on | that level |

```sh
rakupp --exe -O3    prog.raku      # codegen opt + cc -O3
rakupp --exe -Ofast prog.raku      # codegen opt + cc -Ofast
```

**The int passes only pay off *with* C++ inlining.** The `rt*` helpers and the
direct-arity split are wins because the C++ compiler inlines them. At `-O0` they
become real function calls with no fast path, so `-O0` is *slower* than the
default — it's for inspecting/debugging the generated C++, not for speed.

### Speed or size?

`-O` is a **speed** switch, and the C++ level is not a size lever: the binary
is dominated by the statically linked runtime (`librakupp_rt.a`, mostly the
Unicode tables), not by the program's own generated code. Measured (fib /
mandel, this machine):

| | binary | fib time |
|---|---:|---:|
| `--exe` (default `-O2`) | 6,300 KB | 166 ms |
| `--exe -O`  | 6,300 KB | **47 ms** |
| `--exe -Os` | 6,307 KB | 72 ms |
| `--exe -O3` | 6,300 KB | 47 ms |
| `--exe -O` + `strip` | **5,380 KB** | 47 ms |

`-Os` shrinks nothing that matters (it can even come out a few KB *larger*)
and costs 20–50% of the lane speed-up, so there is no size/speed trade to
make at this flag — use `-O` for speed and **`strip`** on the output (~15%
smaller) if size matters. A genuinely smaller binary would need a slimmed
runtime build (e.g. without the full Unicode tables) — a build-system project,
not a codegen flag.

## Measured impact

`--exe`, best of 6 runs after a discarded warm-up (startup-inclusive, as in
[BENCHMARKS.md](BENCHMARKS.md)); measured 2026-07-17 on a lightly loaded machine.

| Benchmark | `--exe` | `--exe -O` | speed-up | what `-O` reached |
|---|---:|---:|---:|---|
| fib      | 166.5 ms | **47.0 ms** | 3.5× | direct-arity calls + int-lane condition |
| loopsum  | 27.1 ms  | **8.6 ms**  | 3.2× | `+=` lane over the native counter |
| streq    | 46.5 ms  | **17.5 ms** | 2.7× | int-lane counters atop the inline `eq`/`lt` |
| arrayops | 102.0 ms | **77.8 ms** | 1.3× | map/grep-body boxing |
| regex    | 61.9 ms  | 60.3 ms | 1.0× | (regex engine dominates) |
| hash     | 15.3 ms  | 14.9 ms | 1.0× | (hash slots, not scalars) |
| sortnums | 49.9 ms  | 48.6 ms | 1.0× | (`.sort` dominates) |
| bigint   | 29.2 ms  | 29.2 ms | 1.0× | (`BigInt` multiply) |
| strcat   | 3.9 ms   | 4.8 ms  | 0.8× | (`~=` already O(n) by default) |

These `--exe` baselines are much lower than they once were: the runtime's
`applyArith` now hot-paths `Int`/`Int` `+ - * < <= > >= == != %` with a char
switch instead of a chain of `op == "…"` string compares, and `--exe` (which
links that runtime) inherits it; plain `--exe` also emits inline string
comparisons in *conditions* and calls builtins through pointers cached at
startup (see [dev/DISPATCH.md](dev/DISPATCH.md) — that is what makes the
`streq` baseline low). So `-O`'s remaining edge is the boxing/allocation it
removes *entirely* — the per-call `ValueList` (`fib` direct calls), and with
pass 3 every `Value` temporary in laneable int statements and conditions
(`loopsum`, and the showcase kernels below).

Every kernel here is already ahead of Rakudo at plain `--exe` (fib included, now
that the runtime hot-paths integer arithmetic). Where the time is **inside a
runtime method** — the regex engine, `BigInt` multiply, `.sort` — `-O` can't reach
it, so those are unmoved. `fib` is the standout: a tiny body called millions of
times, where removing the per-call `ValueList` allocation still halves the time.

## Correctness

`-O` is validated to produce output byte-for-byte identical to the interpreter:

- all benchmark programs match with `-O` on;
- every deterministic example in `examples/` compiles with `--exe -O` and
  matches its golden output in `t/expected/`;
- the arithmetic fast-path fallbacks are checked directly — int64 overflow →
  bignum, `Int`/`Num` mixes, string coercion (`"3" + 4`), `<=>` (left on the
  general path), sorting, and `+=` at the int64 boundary;
- the lane fallbacks likewise: `+=`/`++`/`*=` crossing int64 promote to bignum,
  floored `%` with negative operands, `%= 0`, and comparisons whose
  intermediate overflows all match the interpreter exactly.

The design leans on the fallback: anything the fast path doesn't recognize (a
`Rat`, a bignum operand, a non-`Int` type, a named/slurpy call) routes to the
same runtime code the non-`-O` build uses.

## Limits and what's next

`-O` is deliberately conservative — it removes per-operation overhead without
changing the value model. Pass 3 delivered the first slice of **leaving the
`Value` box entirely** for statements and conditions; the remaining levers, in
rough order of expected payoff:

- **value-position lanes** — laneable int expressions inside larger
  expressions (call arguments, list elements) still box;
- **`Num` lanes** — the same trick for `double` arithmetic (no overflow
  checks needed, just tag guards);
- **array-element lanes** — `@a[$i]` reads/writes inside the lane (a bounds +
  tag guard against the underlying vector);
- **native int locals** — proving a `my $x` never escapes or leaves `Int` and
  emitting a raw `long long` with no box at all (full type inference);
- devirtualizing monomorphic method calls, constant-folding literal
  arithmetic, and specializing `.map`/`.grep`/`.sort` on native element types.

`-O` today is the three passes above.

## Showcase suite

[`tools/optbench/`](../tools/optbench) holds programs each written to lean on one
pass, and [`tools/run-optbench.raku`](../tools/run-optbench.raku) compiles every one
twice — `--exe` and `--exe -O` — checks the two builds agree with the interpreter
byte-for-byte, then times both and reports the `-O` speed-up (Rakudo shown for
reference):

```sh
./build/rakupp tools/run-optbench.raku
```

Best of 5 runs each, on this machine (macOS/Darwin 24.6, Rakudo v2026.06,
measured 2026-07-17 with all three passes):

| benchmark | `--exe` | `--exe -O` | `-O` speed-up | rakudo | showcases |
|---|---:|---:|---:|---:|---|
| sieve       | 1029.3 ms | **25.4 ms**  | **40.6×** | 994.5 ms  | primes <200k — `* <= %%` |
| powmod      | 531.5 ms  | **50.6 ms**  | **10.5×** | 716.8 ms  | 1M `** 3` then `% 1000` |
| intsum      | 283.1 ms  | **35.9 ms**  | **7.9×**  | 624.0 ms  | 5M `+= $_ * 2 - 1` |
| fibcalls    | 701.3 ms  | **190.9 ms** | **3.7×**  | 1353.3 ms | fib(32) — calls + `< + -` |
| stringbuild | 22.3 ms   | 21.9 ms      | 1.0×      | 204.7 ms  | 400k `~=` — already O(n) by default |

The int lanes (pass 3) are what moved this table: `sieve`'s whole inner loop —
`while $d * $d <= $n`, `if $n %% $d`, `$d++` — now runs as raw `int64`, taking
it from a tie with Rakudo at plain `--exe` (1029 vs 995 ms) to 39× ahead;
`intsum` went from a small edge (arithmetic already fast, boxing dominant) to
7.9× once the four per-iteration `Value` constructions disappeared.
`stringbuild` is flat because in-place `~=` is default in both builds. As
always this is only the subset of Raku both engines run identically — not a
coverage claim (see [BENCHMARKS.md](BENCHMARKS.md)).

## See also

- [BENCHMARKS.md](BENCHMARKS.md) — the full speed comparison across all modes.
- [`tools/optbench/`](../tools/optbench) + `tools/run-optbench.raku` — the showcase above.
- `rakupp --cpp [-O] SRC` — inspect exactly what the transpiler emits.
