# FAQ — why -O is not the default

`--exe` compiles a program to C++ and links it against the runtime; adding
`-O` turns on the optimising code generator, and on the right program it is
worth an order of magnitude — the prime sieve in `tools/optbench/` runs 40×
faster with it. So the question is fair: if it is that good, why do you have
to ask for it?

```bash
rakupp --exe    prog.raku -o prog     # default: the faithful translation
rakupp --exe -O prog.raku -o prog     # the optimising code generator
```

## The short answer

The default `--exe` output is a **faithful translation**: every value is the
same boxed `Value` the interpreter uses, every operation calls the same
runtime, and the generated C++ maps line for line onto your program. `-O`
replaces parts of that translation with **specialised lanes** — direct-arity
calls, native integer arithmetic, unboxed loop counters — that are guesses
about what your program does, guarded so they stay correct.

The project draws one line through everything in this area: a change that
fixes a *wart* ships as a default in every mode; a change that *speculates
for speed* is a switch. In-place `~=` (linear string building) is a wart fix
— default everywhere. The integer lanes are speculation — they live under
`-O`.

## Because a default must never lose

`-O` is not a strict win, and the numbers are on record in
[OPTIMIZATION.md](../../internals/OPTIMIZATION.md):

| benchmark | `--exe` | `--exe -O` | speed-up |
|---|---:|---:|---:|
| sieve | 1029.3 ms | 25.4 ms | **40.6×** |
| fib | 166.5 ms | 47.0 ms | 3.5× |
| loopsum | 27.1 ms | 8.6 ms | 3.2× |
| regex | 61.9 ms | 60.3 ms | 1.0× |
| bigint | 29.2 ms | 29.2 ms | 1.0× |
| strcat | 3.9 ms | 4.8 ms | **0.8×** |

Where a program's time is inside a runtime method — the regex engine, bignum
multiply, `.sort`, hash probing — `-O` cannot reach it and buys nothing. And
on `strcat` it *loses*: string appending is already optimal by default, so
the lane machinery is pure overhead there. A flag you chose may cost you 20%
on the wrong program; a **default** may not. That asymmetry is the policy.

## Not because it is unsafe, and not because it is slow to compile

Everything under `-O` is semantics-preserving — it changes *how* the
compiled program computes, never *what* it computes. The showcase suite in
`tools/optbench/` is compiled both ways and checked **byte-identical**
against the interpreter and against Rakudo before any timing is believed,
and a lane whose compile-time conditions or runtime guards fail falls back
to the faithful path for that expression, not to a wrong answer.

Nor does it cost build time. Measured for this page (2026-08-22, Apple
Silicon, warm, best of two): `fib.raku` compiles in 1.34 s with `-O` and
1.34 s without. The C++ compiler dominates the build either way; the extra
code generation is noise.

## The readable C++ is part of the product

`--cpp` prints the generated C++ instead of compiling it, and with the
default translation that output is the debugging tool: one expression in
your program, one recognisable call in the C++. The `-O` output is
specialised overloads and guarded fast paths — correct, but no longer a
line-for-line mirror. When something looks wrong, the first question is
"does the faithful translation do it too?"
([debugging.md](debugging.md) — telling your bug from ours), and the
default being the faithful translation is what keeps that question cheap.

## Passes graduate when they stop being speculation

The boundary is not frozen; it is earned across. Two things that began as
optimisation ideas run in plain `--exe` today because they proved to be
plumbing rather than speculation: comparisons in conditions use inline
helpers, and builtin calls go through pointers cached once at startup.
In-place `~=` ships in every mode, interpreter included, because quadratic
string building was a correctness-shaped wart, not a speed feature. A lane
that someday shows itself universally non-regressing crosses the same line.
Until then, `-O` is where speculation lives — one flag away, never assumed.

## So when do I reach for it?

When the hot code is *your* code: tight integer or scalar arithmetic, hot
user-sub calls, loop-heavy kernels. That is where the 3× to 40× lives. Skip
it when the time is in strings, IO, regexes, sorting or hashes — the
runtime already owns those paths at full speed. Since it costs nothing at
compile time, the practical answer is to build both and time them; the
current sweep across all kernels stays maintained in
[BENCHMARKS.md](../../status/BENCHMARKS.md), and the pass-by-pass story is
[OPTIMIZATION.md](../../internals/OPTIMIZATION.md).
