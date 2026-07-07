# Raku++ — the `-O` optimizer

Raku++ runs a program three ways (see [BENCHMARKS.md](BENCHMARKS.md) for the
speed picture):

- **interp** — tree-walk the AST (the default).
- **`--aot` / `--bundle`** — standalone binaries that still *tree-walk* an
  embedded program, so they run at interpreter speed.
- **`--exe`** — transpile the program to C++ and compile it to a native binary,
  with no interpreter inside. This is the only mode whose runtime performance
  differs, and the only mode the optimizer touches.

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
`Value`, every operator goes through the runtime's string-keyed dispatcher
(`applyArith`), and every user-sub call packs its arguments into a
`ValueList` (a `std::vector<Value>` — a heap allocation per call).

```cpp
// sub fib($n) { $n < 2 ?? $n !! fib($n-1) + fib($n-2) }
static Value u_fib(ValueList __a) {
    Value v_n = rtPos(__a, 0);
    return RT.boolify(applyArith("<", v_n, Value::integer(2LL)))
         ? v_n
         : applyArith("+", u_fib(ValueList{applyArith("-", v_n, Value::integer(1LL))}),
                           u_fib(ValueList{applyArith("-", v_n, Value::integer(2LL))}));
}
```

For a hot recursive function this is two costs on every one of ~1.6M calls: a
`ValueList` `malloc`, and several `applyArith` calls that compare the op *string*
before touching the operands.

## The two passes

### 1. Direct-arity calls (skip the per-call `ValueList`)

A sub whose signature is entirely **plain required positional scalars** (no
named, slurpy, optional, defaulted, or destructured params) gets a direct-`Value`
overload — its parameters *are* the C++ arguments — plus a boxed adapter so any
unusual call site still resolves. C++ overload resolution picks the right one.

```cpp
static Value u_fib(Value v_n) { … }                                  // fast overload
static Value u_fib(ValueList __a) { return u_fib(rtPos(__a, 0)); }   // adapter
```

A call site takes the fast overload when it passes exactly the right number of
**plain positional** arguments (no `:name(…)` pairs, no `|@slurp`); otherwise it
calls through the adapter unchanged. Multi subs, indirect calls (`&fib`), and
method calls are untouched.

### 2. Inline int64 arithmetic (skip the string dispatch and boxing)

For the common operators the codegen emits inline helpers (declared `inline` in
`Interpreter.h`) instead of `applyArith("…", …)`:

| ops | helper | fast path |
|---|---|---|
| `+` `-` `*` | `rtAdd`/`rtSub`/`rtMul` | native `int64`, overflow → bignum |
| `<` `<=` `>` `>=` `==` `!=` | `rtLt`/… | `int64` compare |
| `%` `%%` | `rtMod`/`rtDivides` | `int64` mod (sign follows divisor) / divisibility |
| `~` | `rtConcat` | direct `std::string` concat when both are `Str` |

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

With both passes, `fib` transpiles to:

```cpp
static Value u_fib(Value v_n) {
    return RT.boolify(rtLt(v_n, Value::integer(2LL)))
         ? v_n
         : rtAdd(u_fib(rtSub(v_n, Value::integer(1LL))),
                 u_fib(rtSub(v_n, Value::integer(2LL))));
}
```

No heap allocation, no string dispatch — pure inlinable code. Under a real C++
optimizer this collapses to tight native-int recursion.

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

**The two passes only pay off *with* C++ inlining.** The `rt*` helpers and the
direct-arity split are wins because the C++ compiler inlines them. At `-O0` they
become real function calls with no fast path, so `-O0` is *slower* than the
default — it's for inspecting/debugging the generated C++, not for speed.

## Measured impact

`--exe`, best of 10 runs (startup-inclusive, as in
[BENCHMARKS.md](BENCHMARKS.md)).

| Benchmark | `--exe` | `--exe -O` | speed-up | what `-O` reached |
|---|---:|---:|---:|---|
| fib      | 547 ms | **79 ms** | 6.9× | calls + `+ - <` |
| loopsum  | 85 ms  | **30 ms** | 2.8× | `+=` |
| strcat   | 52 ms  | **28 ms** | 1.9× | `~=` |
| hash     | 34 ms  | **25 ms** | 1.4× | `% 1000` |
| arrayops | 96 ms  | **73 ms** | 1.3× | `* %% 3` over 200k |
| sortnums | 60 ms  | **51 ms** | 1.2× | map-body arithmetic |
| regex    | 84 ms  | 83 ms | — | (regex engine) |
| bigint   | 44 ms  | 44 ms | — | (`BigInt` multiply) |

`fib` was the *only* kernel where Rakudo led at the default `--exe`; with `-O` it
runs ~5× ahead. `-O` helps in proportion to how much of a kernel's time is
arithmetic/string ops the codegen emits. Where the time is **inside a runtime
method** — the regex engine, `BigInt` multiply — `-O` can't reach it, so those two
are unmoved. `sortnums`/`arrayops` move only partway: their arithmetic
(`.map` bodies, the `grep` predicate) speeds up, but `.sort` and the per-element
closure iteration dominate and stay in the runtime.

## Correctness

`-O` is validated to produce output byte-for-byte identical to the interpreter:

- all benchmark programs match with `-O` on;
- the arithmetic fast-path fallbacks are checked directly — int64 overflow →
  bignum, `Int`/`Num` mixes, string coercion (`"3" + 4`), `<=>` (left on the
  general path), sorting, and `+=` at the int64 boundary.

The design leans on the fallback: anything the fast path doesn't recognize (a
`Rat`, a bignum operand, a non-`Int` type, a named/slurpy call) routes to the
same runtime code the non-`-O` build uses.

## Limits and what's next

`-O` is deliberately conservative — it removes obvious per-operation overhead
without changing the value model. The bigger remaining lever is **leaving the
`Value` box entirely**: proving a parameter or local is always an `Int` and
emitting native `int64` variables and arithmetic, so there's no `Value`
construction/copy at all. That needs light type inference over the function body
and is the natural next `-O` pass. Beyond it: devirtualizing monomorphic method
calls, constant-folding literal arithmetic, and specializing common list methods
(`.map`/`.grep`/`.sort`) on native element types.

None of that is here yet; `-O` today is the two passes above.

## See also

- [BENCHMARKS.md](BENCHMARKS.md) — the full speed comparison across all modes.
- `rakupp --cpp [-O] SRC` — inspect exactly what the transpiler emits.
