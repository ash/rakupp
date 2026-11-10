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

It has since been acted on a third time, and that one moved the baseline again.
Since October 2026 every `--exe` build does two things with no flag: a sub
that only does integer arithmetic gets an `int64` twin, and a loop that calls
nothing runs on unboxed locals — the second of which had been an `-O` pass
for two weeks. Both have sections of their own after the three passes. What `-O` still decides is passes 1 to 3
themselves, and the tables at the end say how much is left for it.

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
`Value`, operators in value position go through `applyArith` (spelled
`applyArithValue` at a site with no literal `*`, Chapter 26), and every
user-sub call packs its arguments into a `ValueList` — a heap allocation per
call.

That last clause was true without qualification when these passes were written,
and it is the sentence they were written against. It is now half true: the
short lists a call actually builds come off a free list rather than the
allocator (Chapter 12), so what remains is a list built and torn down per call
without touching `malloc`. The passes below still remove it outright, which is
still worth more.

```cpp
// sub fib($n) { $n < 2 ?? $n !! fib($n-1) + fib($n-2) }
// compiled with RAKUPP_NO_KERNELS=1, which leaves the int64 twin out
static Value u_fib(ValueList __a) {
    Value v_sn = rtBoxedItem(rtPos(__a, 0));
    return rtLtB(v_sn, Value::integer(2LL))       // condition: always inline
         ? v_sn
         : applyArithValue("+",
             u_fib(ValueList{
                 applyArithValue("-", v_sn, Value::integer(1LL))}),
             u_fib(ValueList{
                 applyArithValue("-", v_sn, Value::integer(2LL))}));
}
```

For a hot recursive function that is two costs on every one of about 1.6 million
calls: a `ValueList` allocation, and value-position `applyArith` calls that
dispatch on the operator *string* before touching the operands.

That is the code the three passes were written against, and it is what a
build with `RAKUPP_NO_KERNELS=1` in the environment still emits. An ordinary
build now puts an `int64` twin in front of this body, described below, so
`fib` itself no longer shows the costs: it is the illustration, and any sub
that takes a string or returns a list is the case.

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
indirect calls through `&fib`, and method calls are untouched. So is every sub
in a program that mentions `wrap`, since a wrapper has to see every call.

Six method calls have the same treatment under `-O`: `.key`, `.value` and
`.Int` with no arguments, `.elems`, `.defined`, and a one-argument `.push` are
emitted as `rtMKey` and its siblings, which answer inline for the invocant
they are nearly always asked of and call `methodCall` for every other.

The same idea covers **37 named builtins**, each of which has a real C++
function (`rtBAbs`, `rtBUc`, `rtBSin`, …) rather than a `std::function` in a
map. Chapter 28 tells that story, because the reason it works is not the one
that was first assumed.

## Pass 2 — inline arithmetic

For the common operators the generator emits inline helpers instead of
`applyArith`:

| ops | helper | fast path |
|---|---|---|
| `+` `-` `*` | `rtAdd` / `rtSub` / `rtMul` | native `int64`, overflow to bignum; two `Num`s as doubles |
| `**` | `rtPow` | integer power by squaring |
| `/` | `rtSlash` | |
| `<` `<=` `>` `>=` `==` `!=` | `rtLt` … | `int64` compare |
| `%` `mod` `%%` `div` | `rtMod` / `rtModOp` / `rtDivides` / `rtDiv` | floored `int64` |
| `+&` `+\|` `+^` `+<` `+>` | `rtBitAnd` … `rtShr` | |
| `~` | `rtConcat` | direct concat when both are `Str` |
| `eq` `ne` `lt` `gt` `le` `ge` | `rtEqS` … | byte-wise when both are **plain** `Str` |

```cpp
// src/Interpreter.h
inline Value rtAdd(const Value& l, const Value& r) {
    long long z; double a, b;
    if (rtBothInt(l, r) && !rakupp::add_ovf(l.i, r.i, &z))
        return Value::integer(z);
    if (rtNumPair(l, r, a, b)) return Value::number(a + b);
    return applyArith("+", l, r);     // Rats, bignums, mixed types, coercions
}
```

`rtBothInt` tests that both operands are `VT::Int` and neither has grown a
bignum. Overflow promotes to bignum exactly as `applyArith` does. The
`rtNumPair` line is newer: a pair of plain `Num`s is added as two doubles, so floating
point no longer falls through to the string dispatch.

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

## Loop lanes — pass 3 for a whole loop, and no longer `-O`

Pass 3 removes the box for one expression. What it leaves is a type check per
statement, per iteration, and a write back into a `Value` after each one.
Nothing inside a loop that *calls nothing* can change a plain scalar's tag, so
the check belongs at the loop's entry, once, and the values belong in C++
locals for the loop's extent:

```cpp
// for 1..1000 { $sum += $_ * 2 - 1 }        (abridged)
{ bool __ulane4 = false; do {
    if (!(rtIntSlot(v_ssum))) break;          // ONE guard, at entry
    long long __u1 = v_ssum.i;                // unbox
    long long __ue5 = __u1;                   // entry snapshot
    bool __ubail2 = false;
    for (long long __u0 = 1LL; __u0 <= 1000LL; __u0++) {
        long long __ut8, __ut9, __ut10;
        if (rakupp::mul_ovf(__u0, 2LL, &__ut8)) { __ubail2 = true; break; }
        if (rakupp::sub_ovf(__ut8, 1LL, &__ut9)) { __ubail2 = true; break; }
        if (rakupp::add_ovf(__u1, __ut9, &__ut10)) { __ubail2 = true; break; }
        __u1 = __ut10;
    }
    if (__ubail2) { v_ssum.i = __ue5; break; }   // rewind
    v_ssum.i = __u1;                             // commit
    __ulane4 = true;
} while (0);
if (!__ulane4) { /* the boxed loop, unchanged, as the fallback */ } }
```

The generated code leaves the loop with a `goto` where the excerpt says
`break`, and computes the two bounds with overflow checks first. An overflow
rewinds every variable to its entry value and lets the boxed loop run the
whole thing again from the start — rare, and simpler to be sure of than
resuming mid-flight.

Each variable gets one lane type, settled by a fixpoint over the loop and
never changed afterwards: `long long`, `double`, or a string. The `double`
lane was the first floating-point lane this compiler had; before it, every
`Num` operator in a compiled program dispatched on an operator string. `2.0`
in Raku is a `Rat`, so rational code correctly gets no lane; only the `2e0`
spelling reaches the float one. Because the lane's `a * b + c` would be fused
into one multiply-add by the C++ compiler, and the interpreter rounds every
operation on its own, generated code is compiled with `-ffp-contract=off`.

`while`, `until`, C-style `loop`, and `for A..B` over integer endpoints can
carry a lane. The body may hold arithmetic, comparisons, `if`, `given`/`when`,
nested loops, unlabelled `last`/`next` and `my` declarations — and no call,
which is the rule the whole thing rests on: if the loop can call something,
that something can observe a variable whose live value is sitting in a
register.

The lanes landed under `-O` on 2026-09-19 and have run without it since
2026-10-02, when the interpreter gained loop kernels of the same guarded,
bail-and-rerun shape. `RAKUPP_NO_KERNELS=1` at compile time makes them
`-O`-only again. A lane emitted without `-O` has the plain boxed loop as its
fallback; with `-O`, the fallback is the loop with passes 1 to 3 applied.

## Integer twins and native routines — also not `-O`

The other October change answers the `fib` case from the opposite end. A
top-level sub whose body uses nothing but its own `$` parameters, integer
literals, integer arithmetic and comparisons, `if`, `return`, and calls to
subs that qualify themselves gets a second body over `int64_t`:

```cpp
// redundant parentheses and casts removed
static int64_t k_u_fib(int64_t p0, __KR& __r) {
    if (__r.bail || --__r.depth < 0) { __r.bail = true; return 0; }
    /* … restore the depth on the way out … */
    return p0 < INT64_C(2)
         ? p0
         : __kAdd(k_u_fib(__kSub(p0, INT64_C(1), __r), __r),
                  k_u_fib(__kSub(p0, INT64_C(2), __r), __r), __r);
}
static Value u_fib(ValueList __a) {
    { static std::atomic<int> __kb{0};
      if (__kb.load(std::memory_order_relaxed) < 8 && __a.size() == 1 &&
          __kInt(__a[0])) {
        __KR __kr; int64_t __kv = k_u_fib(__a[0].i, __kr);
        if (!__kr.bail) return Value::integer(__kv);
        __kb.fetch_add(1, std::memory_order_relaxed);
      } }
    /* … the Value body, as before … */
}
```

The twin recurses into itself with no `Value` anywhere. A bail — an overflow,
a zero divisor, the depth limit — abandons it, and the `Value` body answers
the call from the start; that is sound because such a body has no side
effects. A twin that has bailed eight times stops being tried. Which subs
qualify is a fixpoint (`Codegen::planKernels`): a sub qualifies only if every
sub it calls does. An operator the program declares itself is refused, and so
is every twin in a program that mentions `wrap`.

The same rules run in the interpreter (`src/IntKernel.cpp`), which is why the
two were built together. The commit that added the twins measured `fib(29)`
at 45 ms before and 4 ms after under plain `--exe`.

A sub whose parameters are all declared full-width `int` or `num` goes one
step further (`planNative`, 2026-10-25). It also compiles to a plain C++
function over `int64_t` and `double`, called directly by other routines of
the kind:

```cpp
// sub hyp(num $a, num $b --> num) { $a * $a + $b * $b }
static double n_hyp(double nv_sa, double nv_sb) {
    return ((((nv_sa) * (nv_sa))) + (((nv_sb) * (nv_sb))));
}
```

It never bails, because native arithmetic has no exact answer to fall short
of: an `int` wraps and a `num` is a double, which is the interpreter's native
semantics too. Wherever a native operation could answer something other than
a number, it takes the boxed path from that point.

## Three defaults that are not `-O`

Three changes look like optimisations and are shipped as defaults in every mode,
because each fixes a **correctness wart** rather than adding speed.

**In-place `~=`.** `$s ~= …` naively rebuilds the whole string each step, so *n*
appends do quadratic work. The interpreter and Rakudo both build strings in
linear time, so this was a wart:

```cpp
// src/Interpreter.h
inline void rtCatAssign(Value& l, const Value& r) {
    if (l.t == VT::Str && /* … neither side tagged … */) {
        if (r.t == VT::Str) { rtCatAppendText(l, r.s.str()); return; }
        /* … an Int, Num, Rat or Bool appends its text the same way … */
    }
    /* … an undefined accumulator starts from the empty string … */
    l = applyArith("~", l, r);
}
```

It began as the two-line version — append when both sides are `Str`. The
number arms came with an issue: `$s ~= $_` over Ints still rebuilt the whole
string every time, 0.7 s for 40,000 appends.

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
| *(none)* | passes 1–3 off; twins and loop lanes on | `-O2` |
| `-O` | on | `-O2` |
| `-O3` / `-Os` / `-Ofast` / `-O0` | on | that level |

**The integer passes only pay off *with* C++ inlining.** At `-O0` the `rt*`
helpers become real calls with no fast path, so `-O0` is *slower* than the
default. It is for inspecting the generated C++, not for speed.

`-O` is a speed switch and not a size lever: the binary is dominated by the
statically linked runtime, mostly the Unicode tables, not by the program's own
code. `-Os` shrinks nothing that matters and costs 20 to 50% of the lane
speed-up. Use `-O` for speed and `strip` if size matters.

## Measured

`--exe`, best of six after a discarded warm-up, startup-inclusive, measured
on 2026-07-17 — before the twins and the default loop lanes existed, so the
left column is the generic code of the first section:

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

The top three rows are history now. At v5.5.0 (2026-10-21, on an Apple M4, so
compare by ratio) plain `--exe` runs `fib` in 5.2 ms, `loopsum` in 3.4 and
`streq` in 6.9, with no flag: the twin takes the first and a loop lane the
other two.

A dedicated showcase suite in `tools/optbench/`, each program written to lean on
one pass, is compiled twice and checked byte-identical against the interpreter
and Rakudo before being timed. One pass, best of five, on 2026-10-08 at v5.3.0
on the M3:

| benchmark | `--exe` | `--exe -O` | speed-up | rakudo |
|---|---:|---:|---:|---:|
| sieve | 450.5 ms | **22.1 ms** | **20.4×** | 1411.7 ms |
| arrayidx | 94.9 ms | **53.7 ms** | 1.8× | 639.1 ms |
| methodcalls | 151.0 ms | 123.9 ms | 1.2× | 278.7 ms |
| nummath | 217.2 ms | 194.0 ms | 1.1× | 444.1 ms |
| fibcalls | 13.7 ms | 13.1 ms | 1.0× | 970.4 ms |
| bigmul | 12.2 ms | 12.1 ms | 1.0× | 812.1 ms |
| stringbuild | 4.9 ms | 4.9 ms | 1.0× | 126.5 ms |
| intsum | 4.4 ms | 4.4 ms | 1.0× | 595.9 ms |
| powmod | 4.0 ms | 4.0 ms | 1.0× | 459.2 ms |

Six weeks earlier this table read 40.6× for `sieve`, 10.5× for `powmod`, 7.9×
for `intsum` and 3.7× for `fibcalls`. The `-O` column barely moved; the plain
column caught up with it. `intsum` and `powmod` are loops that call nothing
and `fibcalls` is an integer sub, so all three are compiled the fast way
without the flag. `sieve` is still `-O`'s: the generator emits no loop lane
for it, and pass 3 runs its inner loop — `while $d * $d <= $n`, `if $n %% $d`,
`$d++` — as raw `int64`. `stringbuild` is flat because in-place `~=` is
default in both. The middle rows name what nothing reaches yet: indexed array
access, this shape of `Num` math, and a method call that is not devirtualized.

## The box stays; only its per-operation cost goes

A common question: does a native-compiled `my int $x` become a bare C++ `long
long`? **No.** The generated code keeps one uniform representation at every
optimization level. `my int $s` compiles to `Value v_ss`.

- **Default `--exe`:** `$s = $s + $i` is `rtAdd`, which constructs a fresh
  `Value` for the result.
- **`-O` pass 3:** the arithmetic runs as raw `int64` in registers and the result
  is written into the existing box's `.i` slot. No `Value` is constructed per
  operation — but the *storage* is unchanged, and loops still copy full `Value`s.
- **A loop lane:** the variable is a `long long` for the extent of the loop,
  read from the box at entry and written back at exit. The box is still where
  it lives before and after.

Why keep the box? Because the transpile is uniform: any expression must be able
to flow anywhere — into a runtime function, an array element, `say()`, an
untyped assignment — and Raku's `my int` is readable in `Any` context while a
non-native `Int` can hold `Nil` or promote to bignum. Emitting a bare `long
long` local is only sound once an analysis proves the variable never escapes
into a `Value` context. Two narrow versions of that proof now exist: a loop
that calls nothing, and a routine whose signature is all native. The general
one, for any typed local, is still not built.

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
- **`Num` lanes** — built for loops, as the `double` loop lane above; a `Num`
  expression outside a loop that qualifies still goes through `rtAdd` and its
  family;
- **array-element lanes** — `@a[$i]` inside the lane, with a bounds and tag
  guard. A loop lane reads a packed `Num` array's element; the general case,
  the `arrayidx` row above, is not built;
- **native int locals** — the big one, and a real escape-analysis pass: prove a
  typed local never escapes into a `Value` context and never leaves `Int`, then
  emit a raw `long long` with no box at all, eliminating both the storage and
  the per-iteration copies. Built for the two cases named in the previous
  section, and planned as one set of inferred facts for every back end
  (`docs/dev/plans/INFER-PLAN.md`, written 2026-11-06 and not scheduled).
