# Interpreter vs compiled (`--exe`) — the examples

> **Write it. Run it. Compile it.**
>
> ```sh
> rakupp app.raku                  # write it, run it — no build step
> rakupp --exe app.raku -o app     # compile it
> ./app                            # one file (what it needs from the OS: COMPILERS.md)
> ```

Every program in [examples/](../../examples) compiles to a standalone native binary
with `rakupp --exe`, and every binary produces byte-identical output to the
interpreter run (`life.raku` is seeded random, so its check uses an
`srand`-pinned copy). This page compares the two modes of Raku++ itself —
interpreting the source versus running the transpiled-to-C++ binary. It is not
a comparison with any other Raku implementation.

(How `--exe` transpiles and reuses the runtime is in
[ARCHITECTURE.md](../internals/ARCHITECTURE.md) §4 and [RUNTIME.md](../internals/RUNTIME.md); the `-O`
optimizer is in [OPTIMIZATION.md](../internals/OPTIMIZATION.md).)

```sh
build/rakupp examples/mandel.raku            # interp: tree-walk the source
build/rakupp --exe -o mandel examples/mandel.raku
./mandel                                     # native: no interpreter loop inside
```

`--bundle` and `--aot` also produce standalone binaries but tree-walk the
program, so they run at interp speed; `--exe` is the mode measured here.

### What `--exe` does not compile

Two things a reader meets early and this page used not to prepare them for.

**It falls back silently, to bundling.** Four NativeCall shapes are not
transpiled yet, and `--exe` bundles the whole program with the interpreter
instead — which is correct, and runs at interpreter speed, so none of the
numbers below apply to it. It says so on stderr as it happens:

```
note: a NativeCall sub with an `is rw` out-parameter — not yet natively compiled; bundling the whole program with the interpreter instead.
note: a NativeCall sub whose library name is an expression — …
note: a NativeCall sub with a buffer/CArray parameter (needs copy-back) — …
note: a NativeCall sub with `is native(&sub)` — …
```

The same four are listed from the NativeCall side in
[FFI.md](FFI.md#what-happens-when-there-is-no-libffi); this is the same list, not a second one.

**And it can fail to build.** A program that reads a `use`d module's `our`
variable (`$Mod::thing`) does not survive the C++ compile step: the generated
code names an identifier it never declared, and you get exit 5 and no binary.
Interpreted, the same program is fine.

The `examples/` claim above still holds byte for byte. Outside it, the two modes
are not twins in every corner — a compiled `>>op<<` can answer where both the
interpreter and Rakudo throw — so a program that must behave identically in both
should be tested in both.

## Modules

A compiled program carries the modules it `use`s inside the binary, as their
parsed ASTs, so it runs with the module tree deleted. `--exe` also compiles
each module routine's **body** to native code. The interpreter still loads the
module — it declares the packages, classes and roles, applies the traits,
exports the symbols, dispatches every call and binds every signature, so all of
that behaves exactly as it does interpreted — and where it would then walk the
routine's statements, it runs the native body instead.

The compile says how much of that it managed:

```
--exe: embedded 32 modules: Data::TypeSystem::Predicates … Math::NIntegrate
--exe: 225 of 255 module routines compiled natively
```

A routine stays interpreted when its meaning lives in the call protocol around
its statements — a phaser or `CATCH` in the body, an `is rw` routine, an
`is rw`/`is raw`/sigilless parameter or a sub-signature, placeholder
parameters, a `proto` — or when one of its statements can be neither compiled
nor handed to the interpreter on its own. `RAKUPP_AOT_VERBOSE=1` at compile
time names each such routine and the reason; `RAKUPP_AOT_REPORT=FILE` writes
the whole account — every routine, native or not and why, and each statement
a native body hands to the interpreter — and a compiled binary run with
`RAKUPP_AOT_RUNLOG=FILE` writes, at exit, how often each native body ran. A native body that the C++
compiler rejects costs only that routine: the error is traced back to it and
the binary is built again without it.

When the program itself falls back to bundling, its modules' routines are
still compiled natively. A plain `--bundle` leaves them interpreted and builds
faster.

`RAKUPP_NO_AOT=1` in the environment runs a binary's modules interpreted — the
same binary, so the two can be compared — and, at compile time, leaves the
native bodies out.

[Math::NIntegrate](https://raku.land/zef:antononcube/Math::NIntegrate), 32
modules, in-process time of each workload (module loading excluded), best of
three, 2026-10-03 on the M3; the results are identical in both columns:

| Workload | interpreted | `--exe` | `--exe` is |
|---|---:|---:|---:|
| gauss-kronrod | 407.4 ms | 250.4 ms | 1.63× |
| gauss-kronrod (Rat) | 529.4 ms | 326.9 ms | 1.62× |
| trapezoidal (Rat) | 12,482.7 ms | 7,821.0 ms | 1.60× |
| trapezoidal | 3,425.3 ms | 2,169.8 ms | 1.58× |
| multidimensional | 473.5 ms | 299.7 ms | 1.58× |
| cartesian Gauss-Kronrod | 463.7 ms | 298.6 ms | 1.55× |
| newton-cotes | 530.1 ms | 341.7 ms | 1.55× |
| clenshaw-curtis | 595.9 ms | 398.3 ms | 1.50× |
| clenshaw-curtis (Rat) | 621.4 ms | 413.0 ms | 1.50× |

What is left is mostly method dispatch: a native body calls a module's methods
through the interpreter's dispatcher, the same one an interpreted call uses.

## Numbers

User CPU time, best of 3, arm64 build on an M3 (macOS). Measured 2026-07-12.
`life` runs with `--delay=0` (its `sleep` would otherwise dominate);
`echo-server`, `sleep-sort`, and `parallel` are excluded — their runtime is
network/timer wall-clock, not compute.

**User CPU is not wall clock** — don't compare these numbers with the
`startup` row in [BENCHMARKS.md](../status/BENCHMARKS.md). That row is wall time
through the bench harness (spawn + output capture, 3.0 ms there, 2.7 ms
for a bare cold start). User CPU counts only the cycles the program itself
burns — process creation, the dynamic linker, and kernel time are excluded —
which is how a do-almost-nothing program reads ~1 ms here (measured on this
machine: `say 1` is ~3–11 ms wall but ~1.1 ms user CPU).

| Example | interp | native (`--exe`) | native is |
|---|---:|---:|---:|
| life (`--delay=0`) | 746.2 ms | 171.6 ms | 4.3× |
| nqueens | 70.2 ms | 27.9 ms | 2.5× |
| mandel | 106.6 ms | 45.8 ms | 2.3× |
| roman | 3.5 ms | 1.5 ms | 2.3× |
| sierpinski | 4.5 ms | 2.2 ms | 2.0× |
| rpn | 1.9 ms | 1.3 ms | 1.5× |
| factorize | 2.0 ms | 1.4 ms | 1.4× |
| calculator | 1.8 ms | 1.3 ms | 1.4× |
| anagrams | 2.0 ms | 1.6 ms | 1.3× |
| fibonacci | 2.1 ms | 1.6 ms | 1.3× |
| matrix | 1.6 ms | 1.2 ms | 1.3× |
| quicksort | 1.4 ms | 1.1 ms | 1.3× |
| json | 1.6 ms | 1.3 ms | 1.2× |
| pascal | 1.6 ms | 1.3 ms | 1.2× |
| rationals | 1.5 ms | 1.3 ms | 1.2× |
| wordcount | 1.4 ms | 1.2 ms | 1.2× |
| primes | 2.8 ms | 2.5 ms | 1.1× |
| cipher | 1.9 ms | 1.8 ms | 1.1× |
| hanoi | 1.1 ms | 1.0 ms | 1.1× |
| quine | 1.0 ms | 0.9 ms | 1.1× |
| brainfuck | 7.5 ms | 8.2 ms | 0.9× |

## How to read this

- **The compute-heavy programs gain the most.** `life`, `nqueens`, and
  `mandel` spend their time in loops over arrays and arithmetic — exactly the
  interpreter overhead `--exe` removes.
- **The 1–2 ms rows are startup-dominated.** Most examples finish in a couple
  of milliseconds either way; at that scale the ratio mostly measures process
  startup, not the program. They are listed for completeness, not as evidence
  of speedup.
- **`brainfuck` is slightly slower natively.** Its hot loop is already served
  by the interpreter's indexing fast path, and the generated code goes through
  the generic runtime helpers instead. Known, small, and honest.
- **Grammar programs run the same engine in both modes.** `calculator` and
  `json` register their grammars with the embedded runtime, so parsing itself
  is identical — only the surrounding code compiles.

## Reproducing

```sh
for f in examples/*.raku; do
  n=$(basename $f .raku)
  build/rakupp --exe -o /tmp/$n $f
  time build/rakupp $f > /dev/null   # interp
  time /tmp/$n > /dev/null           # native
done
```

Output-identity check: run both modes and `cmp` the outputs (seed `life.raku`
with `srand` first, and skip the wall-clock-driven examples).
