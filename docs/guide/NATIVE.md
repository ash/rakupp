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

**It falls back to bundling, with a note on stderr.** Four NativeCall shapes are not
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

They are not the only ones. A phaser, a `CATCH` block in some positions, a
`CONTROL` block, a native or typed
array (`my num @a`, `array[num]`, a `num @x` parameter), a parameterized type,
and a List of variables (`f(($a, $b))`) in a program that also writes through a
subscript of a scalar or a parameter are refused the same way. The note names
the line:

```
note: phaser / CATCH block (line 12) — not yet natively compiled; bundling the program with the interpreter instead, its routines compiled natively where they can be.
--exe: 29 of 30 program routines compiled natively
```

The bundled program still gets native code for its routines: each sub and
method whose body compiles runs natively, called by the interpreter, as an
embedded module's routines are, while the mainline and the routines that
cannot compile (the one holding the `CATCH`, here) are interpreted. A routine
with loops of arithmetic runs at close to native speed that way — a Mandelbrot
loop in a program with one `CATCH` elsewhere took 3.7 ms bundled, 3.5 ms fully
native and 30 ms interpreted — but a call into one costs what an interpreted
call costs, so many small routines called from the mainline gain little.
`RAKUPP_NO_AOT=1` runs such a binary with every body interpreted. The bodies
cost build time — a C++ compile, where a bundle used to need none: about
3 s instead of 1 s for a program of 30 routines.

The binary parses its source again when it runs, and attaches the bodies only
to a parse that matches the one they were compiled from; a program that a
slang or a localization rewrites as it is read runs fully interpreted.
A `--exe` build that fell back says `"mode":"bundle"` under `rakupp --exe-info`.

**When the C++ does not compile, it bundles too.** If the backend takes a
program and writes C++ the compiler rejects, that is the backend's bug, not
the program's: `--exe` says so in a note and builds the bundle instead of
leaving you without a binary.

Many constructs refuse on purpose, because compiled they would mean something
else: anything that asks the program about itself through names a native
local does not have (`EVAL`, `MY::`/`CALLER::` and the other pseudo-packages,
most `.^` metamodel calls, `callframe`, `.backtrace`), `temp` and `let`,
KEEP/UNDO, `.wrap`, a `multi` whose candidate order the native dispatcher
cannot prove right, and a handful more. They cost speed, never meaning.

The two modes are held to that. `t/exe/parity.raku` compiles every program in
`examples/` and `t/regression/` (about 1,570) and compares each binary's
output and exit status with the interpreted run; a difference fails the gate.
`t/exe/parity-known.txt` lists the differences not fixed yet: none, at
2026-11-12.

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

## What compiled natively, and what did not

Whether the program itself compiled natively is on the last line of the
compile: `Compiled (native) …`, or a `note:` naming the construct that made it
bundle (line number included). For the routines — every module routine, and
the program's own when it bundled — ask for the report at compile time:

```sh
RAKUPP_AOT_REPORT=report.tsv rakupp --exe app.raku -o app
```

It is written once the binary is built, one tab-separated row per routine,
then one per statement a native body hands to the interpreter:

```
# rakupp --exe module routine report
routine    Getopt::Long  263  method new-from-objects  native
routine    Getopt::Long  534  method get-options       interpreted  a sub declared inside a routine, closing over its variables
delegated  Getopt::Long  612  method get-options
```

| Row | Means |
|---|---|
| `routine … native` | the body runs as compiled C++ |
| `routine … interpreted REASON` | the body runs in the interpreter, and why |
| `delegated …` | a native body hands the statement at that line to the interpreter |
| `failed` / `disabled` at the top | the C++ compile failed / `RAKUPP_NO_AOT` was set |

A program's own routines appear under the module name `<main>`. When the whole
program compiled natively it has no rows: nothing in it is interpreted.

Two greps that answer most questions:

```sh
grep -P '\tinterpreted\t' report.tsv | cut -f6 | sort | uniq -c | sort -rn   # which refusals cost you most
grep get-options report.tsv                                                 # what became of one routine
```

The other switches, for when a compiled program and its interpreted run
disagree (please report it — it is a bug):

| Variable | When | Does |
|---|---|---|
| `RAKUPP_AOT_VERBOSE=1` | compile | names each routine that stays interpreted, and why, as it goes; shows the C++ compiler's errors when a native body is dropped |
| `RAKUPP_AOT_RUNLOG=FILE` | run | the binary writes, at exit, how often each native body ran |
| `RAKUPP_NO_AOT=1` | run | runs every module routine interpreted — if the difference goes away, a native body is the cause |
| `RAKUPP_AOT_RANGE=LO-HI[,LO-HI…]` | run | attaches only the native bodies with those ordinals (in load order): bisect to the one that is wrong |
| `RAKUPP_AOT_TRACE=1` | run | logs each body as it attaches, with its ordinal, and each one declined |

Bisecting with the last two, on a binary `./app` whose output differs:

```sh
RAKUPP_NO_AOT=1 ./app            # right now? then a native body is wrong
RAKUPP_AOT_RANGE=0-40 ./app      # halve the range until one ordinal is left …
RAKUPP_AOT_RANGE=3-3 RAKUPP_AOT_TRACE=1 ./app 2>&1 | grep '\[aot\] #'   # … and name it
```

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
