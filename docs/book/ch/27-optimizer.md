# The `-O` Optimizer

An optimizer for a language this dynamic has a narrower job than the name
suggests. It is not looking for cleverness; it is looking for generality the
program never asked for and is being charged for anyway. Chapter 14 measured a
call and found the cost sitting in the boxed argument list rather than in
dispatch, and the three passes here follow from that one finding.

The finding has since been acted on twice. This chapter is the first way:
compile the list out of existence where the signature allows it. The second
came later and from inside the interpreter — make the allocation itself nearly
free, with a free list of small blocks (Chapter 12) — which narrowed the gap
these passes close without changing what they do. Removing a cost is still
worth more than cheapening it, and the passes below still measure what they
always did against the `-O`-off baseline; the honest note is that the baseline
moved.

`--exe` and its inspection twin `--cpp` accept `-O`. Everything under it is
**semantics-preserving**: it changes how the compiled program computes, never
what it computes. It is opt-in and off by default.

```sh
rakupp --exe    prog.raku -o prog     # no optimizer
rakupp --exe -O prog.raku -o prog     # optimizer on
rakupp --cpp -O prog.raku             # print the optimized C++
```

## What the generated code looks like without it

By default the transpiler is faithful but generic: every value is a boxed
`Value`, operators in value position go through `applyArith`, and every user-sub
call packs its arguments into a `ValueList` — a heap allocation per call.

That last clause was true without qualification when these passes were written,
and it is the sentence they were written against. It is now half true: the
short lists a call actually builds come off a free list rather than the
allocator (Chapter 12), so what remains is a list built and torn down per call
without touching `malloc`. The passes below still remove it outright, which is
still worth more.

```cpp
// sub fib($n) { $n < 2 ?? $n !! fib($n-1) + fib($n-2) }
static Value u_fib(ValueList __a) {
    Value v_sn = rtPos(__a, 0);
    return rtLtB(v_sn, Value::integer(2LL))       // condition: always inline
         ? v_sn
         : applyArith("+",
             u_fib(ValueList{applyArith("-", v_sn, Value::integer(1LL))}),
             u_fib(ValueList{applyArith("-", v_sn, Value::integer(2LL))}));
}
```

For a hot recursive function that is two costs on every one of about 1.6 million
calls: a `ValueList` allocation, and value-position `applyArith` calls that
dispatch on the operator *string* before touching the operands.

## Pass 1 — direct-arity calls

A sub whose signature is entirely **plain required positional scalars** — no
named, slurpy, optional, defaulted or destructured parameters — gets a
direct-`Value` overload, plus a boxed adapter so any unusual call site still
resolves. C++ overload resolution picks the right one.

```cpp
static Value u_fib(Value v_sn) { … }                                // fast
static Value u_fib(ValueList __a) { return u_fib(rtPos(__a, 0)); }  // adapter
```

A call site takes the fast overload when it passes exactly the right number of
plain positional arguments — no `:name(…)` pairs, no `|@slurp`. Multi subs,
indirect calls through `&fib`, and method calls are untouched.

The same idea covers **37 named builtins**, each of which has a real C++
function (`rtBAbs`, `rtBUc`, `rtBSin`, …) rather than a `std::function` in a
map. Chapter 28 tells that story, because the reason it works is not the one
that was first assumed.

## Pass 2 — inline arithmetic

For the common operators the generator emits inline helpers instead of
`applyArith`:

| ops | helper | fast path |
|---|---|---|
| `+` `-` `*` | `rtAdd` / `rtSub` / `rtMul` | native `int64`, overflow to bignum |
| `**` | `rtPow` | integer power by squaring |
| `<` `<=` `>` `>=` `==` `!=` | `rtLt` … | `int64` compare |
| `%` `%%` `div` | `rtMod` / `rtDivides` / `rtDiv` | floored `int64` |
| `~` | `rtConcat` | direct concat when both are `Str` |
| `eq` `ne` `lt` `gt` `le` `ge` | `rtEqS` … | byte-wise when both are **plain** `Str` |

```cpp
// src/Interpreter.h
inline Value rtAdd(const Value& l, const Value& r) {
    long long z;
    if (rtBothInt(l, r) && !rakupp::add_ovf(l.i, r.i, &z))
        return Value::integer(z);
    return applyArith("+", l, r);     // Rats, bignums, mixed types, coercions
}
```

`rtBothInt` tests that both operands are `VT::Int` and neither has grown a
bignum. Overflow promotes to bignum exactly as `applyArith` does.

The string comparisons matter more than the integer ones, because they sat
*late* in `applyArith`'s dispatch chain — about 118 nanoseconds against 10 for a
direct call. The guard is that both operands are **plain** `Str`: no
`Version`/`IO`/`Buf` tag and no enum identity. A tagged value falls back to the
full chain, which preserves `Version` part-comparison, enum stringification,
junction autothreading and `Whatever` currying.

With both passes plus pass 3's condition lane, `fib` becomes:

```cpp
static Value u_fib(Value v_sn) {
    return ([&]() -> bool {                        // condition lane
        do { if (!(rtIntBox(v_sn))) break;         //   guard the box
             return (v_sn.i < 2LL); } while (0);   //   compare as raw int64
        return rtLtB(v_sn, Value::integer(2LL));   //   guard failed
    }())
         ? v_sn
         : rtAdd(u_fib(rtSub(v_sn, Value::integer(1LL))),
                 u_fib(rtSub(v_sn, Value::integer(2LL))));
}
```

No heap allocation, no string dispatch. Under a real C++ optimizer that
collapses to tight native-integer recursion.

## Pass 3 — guarded native-int lanes

Passes 1 and 2 still build a boxed `Value` for every intermediate result:
`$sum += $_ * 2 - 1` constructs four of them per evaluation. Pass 3 removes
them.

A straight-line integer expression whose leaves are **int literals and plain
scalar variables** is computed in raw `int64`. Each leaf is tag-guarded at run
time, each operation is overflow-checked, and the result is stored **into the
target's existing box** with no `Value` construction at all:

```cpp
// $sum += $_ * 2 - 1     (inside a native range loop)
{ bool __lok = false; do {
    if (!(rtIntBox(v__t0) && rtIntSlot(v_ssum))) break;
    long long __t1; if (rakupp::mul_ovf(v__t0.i, 2LL, &__t1)) break;
    long long __t2; if (rakupp::sub_ovf(__t1, 1LL, &__t2)) break;
    long long __t3; if (rakupp::add_ovf(v_ssum.i, __t2, &__t3)) break;
    v_ssum.i = __t3; __lok = true;
} while (0);
if (!__lok) { v_ssum = rtAdd(v_ssum,
                  rtSub(rtMul(v__t0, Value::integer(2LL)),
                        Value::integer(1LL))); } }
```

Any guard failure, overflow, or domain failure falls through to the untouched
boxed emission. That is sound because **lane leaves are pure** — literals and
variable reads — so re-evaluating them on the slow path has no side effects.

The lane applies to statement-position assignment to a plain scalar, statement-
position `++`/`--`, and conditions. The store additionally requires `rtIntSlot`:
not an enum-typed box, whose stringification is its name rather than its number.

Operations covered: `+ - *` overflow-checked, unary minus, floored `%` mirroring
`rtMod` bit for bit, `%%`, and the six comparisons. Everything else — `Num`s,
strings, `Rat`s, bignums, array elements, method calls — fails the lane at
compile time or its guards at run time.

## Three defaults that are not `-O`

Three changes look like optimisations and are shipped as defaults in every mode,
because each fixes a **correctness wart** rather than adding speed.

**In-place `~=`.** `$s ~= …` naively rebuilds the whole string each step, so *n*
appends do quadratic work. The interpreter and Rakudo both build strings in
linear time, so this was a wart:

```cpp
// src/Interpreter.h
inline void rtCatAssign(Value& l, const Value& r) {
    if (l.t == VT::Str && r.t == VT::Str) { l.s += r.s; return; }
    l = applyArith("~", l, r);
}
```

The interpreter pairs it with sink context, so a loop body's assignment does not
copy its growing result either.

**`.sort($key)` extracts the key once per element.** `.sort` takes either a
2-ary comparator or a 1-ary key extractor, and the difference is asymptotic: a
comparator is *supposed* to run per comparison, but a key extractor runs **once
per element**. Raku++ used to call the 1-ary block inside the comparator:

```cpp
// after — a Schwartzian transform: n calls, then compare the keys
ValueList keys(items.size());
for (size_t i = 0; i < items.size(); i++)
    keys[i] = callCallable(blk, {items[i]});
std::stable_sort(order.begin(), order.end(), [&](size_t x, size_t y) {
    return valueCmp(keys[x], keys[y]) < 0;
});
```

Sorting codepoints by the length of their Unicode name:

| N | before | after | Rakudo |
|---|---:|---:|---:|
| 8 K | 5.70 s | 0.29 s | 0.34 s |
| 64 K | 18.09 s | 0.68 s | 0.63 s |
| 128 K | still running at 95 s | 1.33 s | 1.09 s |

The ratio grows with log *n*, which is the shape the change predicts.

**Never numify a string by throwing**, which is Chapter 11's `stod` story and
was found while profiling the comparator case above.

## Forwarding the C++ optimization level

`--exe` compiles the generated C++ at `-O2` by default; a level on the flag is
passed through:

| flag | codegen passes | C++ compile |
|---|---|---|
| *(none)* | off | `-O2` |
| `-O` | on | `-O2` |
| `-O3` / `-Os` / `-O0` | on | that level |

**The integer passes only pay off *with* C++ inlining.** At `-O0` the `rt*`
helpers become real calls with no fast path, so `-O0` is *slower* than the
default. It is for inspecting the generated C++, not for speed.

`-O` is a speed switch and not a size lever: the binary is dominated by the
statically linked runtime, mostly the Unicode tables, not by the program's own
code. `-Os` shrinks nothing that matters and costs 20 to 50% of the lane
speed-up. Use `-O` for speed and `strip` if size matters.

## Measured

`--exe`, best of six after a discarded warm-up, startup-inclusive:

| Benchmark | `--exe` | `--exe -O` | speed-up | what `-O` reached |
|---|---:|---:|---:|---|
| fib | 166.5 ms | **47.0 ms** | 3.5× | direct-arity calls, condition lane |
| loopsum | 27.1 ms | **8.6 ms** | 3.2× | the `+=` lane |
| streq | 46.5 ms | **17.5 ms** | 2.7× | int lanes atop inline `eq` |
| arrayops | 102.0 ms | **77.8 ms** | 1.3× | map/grep body boxing |
| regex | 61.9 ms | 60.3 ms | 1.0× | the regex engine dominates |
| hash | 15.3 ms | 14.9 ms | 1.0× | hash slots, not scalars |
| sortnums | 49.9 ms | 48.6 ms | 1.0× | `.sort` dominates |
| bigint | 29.2 ms | 29.2 ms | 1.0× | `BigInt` multiply |
| strcat | 3.9 ms | 4.8 ms | 0.8× | `~=` already linear by default |

The pattern is clear and worth stating: **where the time is inside a runtime
method — the regex engine, bignum multiply, `.sort` — `-O` cannot reach it.**
What it removes is boxing and allocation in the program's own code.

A dedicated showcase suite in `tools/optbench/`, each program written to lean on
one pass, is compiled twice and checked byte-identical against the interpreter
and Rakudo before being timed:

| benchmark | `--exe` | `--exe -O` | speed-up | rakudo |
|---|---:|---:|---:|---:|
| sieve | 1029.3 ms | **25.4 ms** | **40.6×** | 994.5 ms |
| powmod | 531.5 ms | **50.6 ms** | 10.5× | 716.8 ms |
| intsum | 283.1 ms | **35.9 ms** | 7.9× | 624.0 ms |
| fibcalls | 701.3 ms | **190.9 ms** | 3.7× | 1353.3 ms |
| stringbuild | 22.3 ms | 21.9 ms | 1.0× | 204.7 ms |

`sieve`'s whole inner loop — `while $d * $d <= $n`, `if $n %% $d`, `$d++` — runs
as raw `int64` under pass 3, taking it from a tie with Rakudo at plain `--exe`
to far ahead. `stringbuild` is flat because in-place `~=` is default in both.

## The box stays; only its per-operation cost goes

A common question: does a native-compiled `my int $x` become a bare C++ `long
long`? **No.** The generated code keeps one uniform representation at every
optimization level. `my int $s` compiles to `Value v_ss`.

- **Default `--exe`:** `$s = $s + $i` is `rtAdd`, which constructs a fresh
  `Value` for the result.
- **`-O` pass 3:** the arithmetic runs as raw `int64` in registers and the result
  is written into the existing box's `.i` slot. No `Value` is constructed per
  operation — but the *storage* is unchanged, and loops still copy full `Value`s.

Why keep the box? Because the transpile is uniform: any expression must be able
to flow anywhere — into a runtime function, an array element, `say()`, an
untyped assignment — and Raku's `my int` is readable in `Any` context while a
non-native `Int` can hold `Nil` or promote to bignum. Emitting a bare `long
long` local is only sound once an analysis proves the variable never escapes
into a `Value` context, which is a genuine escape-analysis pass and the natural
successor to pass 3.

## Correctness

`-O` is validated to produce output byte-for-byte identical to the interpreter:

- every benchmark program matches with `-O` on;
- every deterministic example compiles with `--exe -O` and matches its golden
  output;
- the arithmetic fallbacks are checked directly — int64 overflow to bignum,
  `Int`/`Num` mixes, string coercion, `<=>` (left on the general path), and `+=`
  at the int64 boundary;
- the lane fallbacks likewise: `+=`/`++`/`*=` crossing int64, floored `%` with
  negative operands, `%= 0`, and comparisons whose intermediates overflow.

The design leans on the fallback. Anything the fast path does not recognise
routes to the same runtime code the non-`-O` build uses, which is why "identical
output" is a checkable claim rather than a hope.

## What is next

- **value-position lanes** — laneable integer expressions inside larger
  expressions (call arguments, list elements) still box;
- **`Num` lanes** — the same trick for `double`, with tag guards and no overflow
  checks;
- **array-element lanes** — `@a[$i]` inside the lane, with a bounds and tag
  guard;
- **native int locals** — the big one, and a real escape-analysis pass: prove a
  typed local never escapes into a `Value` context and never leaves `Int`, then
  emit a raw `long long` with no box at all, eliminating both the storage and
  the per-iteration copies.
