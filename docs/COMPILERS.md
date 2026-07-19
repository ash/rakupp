# Compilers: which one, which architecture

A C++ compiler shows up in **two** places when you use Raku++, and they can be
different compilers:

1. **Building `rakupp` itself** — the interpreter/compiler binary, built once
   from `src/` with CMake.
2. **The compiler `--exe` invokes at runtime** — `rakupp --exe program.raku`
   transpiles your program to C++ and shells out to a C++ compiler to build the
   native binary. (`--bundle` and `--aot` do the same.) That compiler is picked
   at run time, independently of whatever built `rakupp`.

This page is the practical "what should I use" for both, per platform.

## TL;DR

| Platform | Build `rakupp` with | `--exe` uses | Notes |
|---|---|---|---|
| **macOS (Apple Silicon)** | Apple Clang, **arm64** | `c++` (Apple Clang) | Pass `-DCMAKE_OSX_ARCHITECTURES=arm64` — the default can be x86_64/Rosetta, ~2× slower. |
| **macOS (Intel)** | Apple Clang | `c++` | Native x86_64; nothing special. |
| **Linux** | GCC or Clang (either) | `c++` (system default) | Clang is ~1.3–2× faster on this codebase; GCC is the `cmake` default and fully supported. |
| **Windows** | MSVC (`cl`) or MinGW-w64 (`g++`) | `cl`, else `g++` | MSVC: static CRT + `--config Release`. MinGW: MSYS2. |

Release binaries are built with **Clang everywhere** (Apple Clang on macOS,
`CC=clang` on Linux, MSVC on Windows). GCC is kept as a *portability gate*, not
the shipping compiler.

## macOS — architecture matters more than the compiler

On Apple Silicon the single biggest performance lever is **building for arm64,
not x86_64-under-Rosetta**. A stock `cmake -S . -B build` can resolve to x86_64
(depending on how CMake and your shell were installed), and the resulting binary
runs under Rosetta 2 at roughly **half** native speed. Always pin the arch:

```sh
# Apple Silicon — native arm64
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build -j

# A universal binary (both arches), as the release ships:
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64"
```

Check what you got: `file build/rakupp` should say `arm64`, and
`build/rakupp --version` should start instantly. If a build feels ~2× slow for
no reason, it's almost always an accidental x86_64/Rosetta build.

**Compiler:** Apple Clang (the default `clang++`/`c++`, no separate install) is
the recommendation and what `--exe` uses automatically. Homebrew GCC works and
builds cleanly, but it is ~1.3–2× slower on this codebase and is only used here
as a portability check — there is no reason to prefer it for day-to-day use.
Note that Apple Clang **rejects `-mcpu=native`** (it already tunes for the host
M-chip), and LTO buys nothing measurable; `-O3` (the CMake Release default) is
the whole story.

## Linux — GCC and Clang both fine

Either toolchain builds `rakupp` cleanly. `cmake --build` defaults to GCC, which
is fully supported; Clang is what the release job uses and is a hair faster.
Pick whichever you have:

```sh
# default (usually GCC)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j

# Clang explicitly
CC=clang CXX=clang++ cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
```

**Portable binaries:** the Linux release links `libstdc++`/`libgcc` statically so
the archive runs on any distro with no runtime dependency:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
      "-DCMAKE_EXE_LINKER_FLAGS=-static-libgcc -static-libstdc++"
```

`--exe` uses the system `c++` (GCC or Clang, whichever is default). Set `$CXX`
to override — see below.

## Windows — MSVC (`cl`) or MinGW-w64 (`g++`)

Both toolchains are first-class; the release ships **both** a MSVC and a MinGW
archive.

### MSVC (`cl.exe`)

Build from a **Developer Command Prompt** (so `cl` and the Windows SDK are on
`PATH`). The Visual Studio generator is *multi-config*, so the configuration goes
on the **build** step, not `-DCMAKE_BUILD_TYPE`:

```sh
cmake -S . -B build -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded
cmake --build build --config Release      # -> build\Release\rakupp.exe
```

`CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded` selects the **static CRT** (`/MT`).
This matters because `--exe` compiles your program with `/MT` and links it
against the runtime archive — a default (`/MD`) `rakupp` would make every
`--exe` link fail with `LNK2038`. (Recent `rakupp` defaults MSVC builds to the
static CRT, so this flag is belt-and-suspenders, but keep it if in doubt.)

For `--exe`, `rakupp` finds `cl` on `PATH`; if you run it from a plain
`cmd`/PowerShell instead of a Developer Prompt, it locates Visual Studio via
`vswhere` and bootstraps `vcvars64.bat` automatically, so `--exe` works out of
the box on any machine with VS or the Build Tools installed.

### MinGW-w64 (`g++`, via MSYS2)

The GNU toolchain most people install through MSYS2. Static-link so the exe
carries its own runtime:

```sh
# from a MinGW64 shell (mingw-w64-x86_64-gcc, -cmake, make)
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      "-DCMAKE_EXE_LINKER_FLAGS=-static"
cmake --build build -j
```

If `cl` isn't present, `--exe` uses `g++` (then `clang-cl`, then `clang++`)
from `PATH`.

## How `--exe` picks its compiler

At run time, in order: **`$CXX`** if set → on Windows `cl`, then `g++`,
`clang-cl`, `clang++` (first found on `PATH`) → everywhere else `c++`. So:

```sh
# force a specific compiler for the transpiled program
CXX=clang++ rakupp --exe program.raku -o program
CXX=g++-14  rakupp --exe program.raku -o program
```

The compiler that builds the transpiled program is independent of the one that
built `rakupp`; you can build `rakupp` with Clang and still compile `--exe`
output with GCC, or vice versa.

## Clang vs GCC — why we ship Clang

On two of the three platforms there's no decision to make: macOS uses **Apple
Clang** and Windows uses **MSVC** as their native toolchains — GCC there is a
foreign add-on (Homebrew GCC, MinGW) that a release has no reason to depend on.
The only place a real GCC-vs-Clang choice exists is **Linux**, and there the
answer is speed: Clang produces a meaningfully faster `rakupp`.

Measured on this tree — same machine (Apple Silicon, arm64), same `-O3`, Apple
Clang 17.0 vs Homebrew GCC 16.1, interpreter kernels, lower is better:

| Kernel | Clang | GCC | GCC / Clang |
|---|---:|---:|---:|
| strcat   | 10 ms  | 20 ms   | **2.00×** |
| hash     | 36 ms  | 64 ms   | **1.75×** |
| sortnums | 60 ms  | 100 ms  | **1.66×** |
| asg      | 449 ms | 721 ms  | **1.61×** |
| loopsum  | 187 ms | 292 ms  | **1.56×** |
| streq    | 590 ms | 920 ms  | **1.55×** |
| fib      | 785 ms | 1192 ms | **1.52×** |
| regex    | 80 ms  | 110 ms  | **1.37×** |
| bigint   | 30 ms  | 40 ms   | **1.33×** |
| arrayops | 100 ms | 130 ms  | **1.30×** |

GCC never wins a kernel; the gap ranges ~1.3–2.0× (median ~1.55×). This is not
a universal "Clang beats GCC" — it's specific to what `rakupp` *is*. A
tree-walking interpreter is a huge `eval`/`exec` dispatch built from thousands
of small functions and deep `switch`es; Clang's inlining and branch layout
happen to optimize that shape better. It is also **version- and
machine-dependent** — a measured decision, worth re-checking (rebuild with
`-DCMAKE_CXX_COMPILER=g++` and re-run `tools/perf-guard.raku`) if a future GCC
closes the gap.

**So why keep GCC at all?** Because a plain `cmake --build` on Linux *defaults
to GCC* — that's what a user who clones and builds gets. If the code only
compiled under Clang, those users would be broken. GCC and Clang differ in ways
that bite a large C++ codebase (`__int128`/builtin availability, ISO-conformance
strictness, template lookup, warning sets), so CI keeps a **build-only GCC job**
purely to catch "compiles under Clang, breaks under GCC" — without that artifact
ever being what's published. GCC has to *work*; it just isn't the one shipped.

`-O3` (the CMake Release default) is the whole optimization story: neither LTO
nor `-mcpu`/`-march=native` showed a measurable win (Apple Clang even rejects
`-mcpu=native`, since it already tunes for the host chip). So: Clang if you have
a choice, GCC if it's what's there — both are correct, and both are tested in CI
on every release.

See [BENCHMARKS.md](BENCHMARKS.md) for the interp-vs-`--exe`-vs-Rakudo numbers
(all measured with the Clang build), and the [README build section](../README.md#build-from-source)
for the canonical per-platform commands.
