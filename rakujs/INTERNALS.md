# Raku.js internals — how the WebAssembly build works, what it costs, where it stops

Reference material for the build in this directory. If you only want to *use*
Raku.js, [README.md](README.md) is enough; this page is for changing it,
debugging it, or deciding whether it can carry a given workload.

## Build

```sh
rakujs/build.sh            # release: -Oz  → playground/rakujs.{js,wasm}
rakujs/build.sh --debug    # -O0 -sASSERTIONS=2, for diagnosing crashes
```

It compiles `../src/*.cpp` (everything but `main.cpp`) plus `rakupp_web.cpp`.
Nothing in `../src` is modified — the web entry point is additive. It then
regenerates `playground/examples.js` with a native `rakupp` (`$RAKUPP`, a
`build*/rakupp`, or one on `PATH`); with none available it says so and leaves
the existing file alone.

Two environment variables retarget it:

```sh
RAKUJS_OUT=/tmp/rakujs-node   # output directory (default: rakujs/playground)
RAKUJS_ENV=node,web,worker    # -sENVIRONMENT (default: web,worker)
```

If `em++` isn't on `PATH`, `build.sh` clones and installs Emscripten into
`rakujs/emsdk/` (git-ignored, ~1 GB) on first run. `source
/path/to/emsdk/emsdk_env.sh` beforehand to use an existing install.

The link is `MODULARIZE`d as `RakuJS`, with `INVOKE_RUN=0` / `EXIT_RUNTIME=0`
(the module is a library, not a program), a 16 MiB stack and 32 MiB initial
memory growing on demand, and these exports:

```
_rakupp_run   _rakupp_highlight   _rakupp_version   _malloc   _free
ccall   cwrap   UTF8ToString
```

**Prebuilt, no Emscripten needed:** each tagged
[release](https://github.com/ash/rakupp/releases) attaches `rakujs-<tag>.zip` —
`rakujs.js` + `rakujs.wasm` plus the playground files, ready to drop into a
site. The release CI builds it; see
[`.github/workflows/release.yml`](../.github/workflows/release.yml).

### The smoke test

`smoke.cjs` runs the showcase interpreters through a Node-loadable build, so a
crash that only happens in wasm is caught here rather than by a reader:

```sh
RAKUJS_ENV=node,web,worker RAKUJS_OUT=/tmp/rakujs-node rakujs/build.sh
node --stack-size=6000 rakujs/smoke.cjs /tmp/rakujs-node
```

It exists because of a real escape: the grammar memo reaper started a
`std::thread`, which in a single-threaded build throws, and the escape reached
`std::terminate` — every parse past its threshold died in the browser as a bare
`Aborted()` while every native gate stayed green.

## How it works / design notes

- **Entry point.** `rakupp_run()` calls `rakupp::rakuppRun()` (see
  `../src/Runtime.h`), the same function the native CLI uses for a normal run.
  It lexes, parses, builds an `Interpreter`, and runs — catching `ParseError` /
  `RakuError` / `std::exception` and reporting them to stderr just like the CLI.
- **Runs in a Web Worker.** `rakupp_run()` is a *synchronous* call that runs a
  whole program to completion; on the main thread that freezes the UI (no
  spinner, output only at the end). The worker keeps the main thread free, so a
  page can animate a spinner after 300 ms, stream output live (ANSI cursor-home
  treated as a redraw, so `life` animates frame by frame), and **Stop** a
  runaway program by terminating the worker. Rendering is coalesced on a timer,
  not `requestAnimationFrame` — which pauses in background tabs and would drop
  output there.
- **No big-stack thread.** The native CLI runs on a 1 GiB pthread stack via
  `rakuppRunBigStack()`. WASM is single-threaded, so we call `rakuppRun()`
  directly. See the recursion note under limitations.
- **Exceptions: `-fexceptions`, not `-fwasm-exceptions`.** The interpreter leans
  on C++ exceptions for both errors AND control flow (`last`/`next`/`redo`,
  `when`/`succeed`). Native Wasm-EH would give deeper recursion, but with
  emscripten 6.0.3 its personality fails to match the interpreter's by-value
  control-exception catches (`catch (LastEx&)` …), so
  `last`/`next`/`given`/`when` escape to `std::terminate` and trap
  (`RuntimeError: unreachable`) — verified. `-fexceptions` handles them
  correctly, at the cost of recursion depth (below).
- **Output** goes to `std::cout`/`std::cerr`, which Emscripten routes to the
  `print`/`printErr` callbacks. No interpreter changes needed.
- **The filesystem is inherited, not requested.** `build.sh` passes no
  filesystem flags at all — no `-sFORCE_FILESYSTEM`, no `IDBFS`, no
  `NODERAWFS`. One is linked anyway, because the interpreter calls
  `fopen`/`stat`, and Emscripten's default backend is MEMFS: `/` with
  `/home/web_user`, `/tmp`, `/dev` and `/proc`, all in the instance's memory,
  created by `FS.staticInit` before any of our code runs. So Raku file IO works
  in the browser — a fact of the toolchain rather than a decision — with no path
  to the visitor's disk and no persistence. Lifetime is the module instance's:
  the worker builds one and reuses it across runs, so files survive from run to
  run and vanish with the instance (Stop, restart, `exit`, recursion `RangeError`,
  reload). [PLAYGROUND.md](PLAYGROUND.md#files) documents it for users.

## Performance vs native (and Node vs Bun vs browser)

> **Status: experimental.** Measuring the WebAssembly build is new and the
> methodology is still settling — the host runtimes differ in wasm tiering,
> stack limits, and timer behaviour (an in-browser figure exists only as a
> dated upper bound; see the † note). Treat these numbers as a first sounding,
> not a settled benchmark like the native tables in
> [../docs/status/BENCHMARKS.md](../docs/status/BENCHMARKS.md).

Measured 2026-07-22 on the same kernels, machine, and day as the native tables
in [../docs/status/BENCHMARKS.md](../docs/status/BENCHMARKS.md), both hosts
running the same Raku++ 1.0.0 wasm (`-Oz`, `-fexceptions`). Same policy
everywhere: 7 runs, first discarded, minimum of the remaining 6; one module
instance reused across runs (each run still gets a fresh `Interpreter`). Node
and Bun run a `-sENVIRONMENT=node` build via
[`bench-runtime.cjs`](bench-runtime.cjs).

| Benchmark | native interp | Node 20.11 (V8) | Bun 1.3.14 (JSC) |
|---|---:|---:|---:|
| bigint   | 31.8 ms  | 41.7 ms    | 79.2 ms    |
| strcat   | 13.1 ms  | 68.4 ms    | 142.2 ms   |
| hash     | 38.4 ms  | 244.5 ms   | 466.1 ms   |
| sortnums | 70.7 ms  | 272.6 ms   | 595.5 ms   |
| regex    | 86.3 ms  | 360.9 ms   | 744.8 ms   |
| arrayops | 114.9 ms | 524.0 ms   | 1,173.5 ms |
| loopsum  | 195.9 ms | 1,254.4 ms | 1,046.9 ms |
| fib      | 818.4 ms | 5,546.1 ms | 8,890.6 ms |
| *module init* | *(2.0 ms process)* | *20 ms* | *77 ms* |

**The wasm tax is 1.3–6.8× on a clean host** (the Node column): bigint 1.3×,
regex 4.2×, up to loopsum 6.4× and fib 6.8×. The penalty tracks **how
call-dense the workload is**, not how heavy it is: under `-fexceptions` (see
the design note above) every C++ call that might throw is routed through JS
`invoke_*` trampolines, so call-saturated `fib` pays the most while `bigint` —
whose time lives inside exception-free `BigInt` multiply loops that compile to
plain wasm — runs near-native. Native Wasm-EH would shrink the trampoline cost,
but is blocked today by the emscripten personality bug described above. Even
so, wasm `bigint` under either host here outruns *native* Rakudo (258.9 ms on
the same machine).

**Node vs Bun:** Node wins 7 of the 8 kernels, now by ~1.6–2.2× (the gap
widened from ~1.3–1.5× at the 0.7.0 measurement as the engine grew) — V8's
optimizing wasm tier copes better with this trampoline-dense profile — while
Bun still takes `loopsum` (1,047 vs 1,254 ms) and starts a module ~4× slower
(77 vs 20 ms). One practical asymmetry: **Bun's JavaScriptCore runs `fib(29)`
on its default stack; Node's default JS stack overflows on it** (C++ recursion
consumes the host JS stack under `-fexceptions`) and needs
`node --stack-size=6000`. (We use Bun as the ecosystem's script runner — e.g.
the spec site's verify harness — where wasm *execution* speed is irrelevant;
for timing-sensitive wasm work, prefer Node.)

† An earlier (2026-07-16, 0.7.0 wasm) measurement in the embedded
automation-driven browser pane sat a uniform ~3× above that day's Node column
(e.g. hash 797 ms, fib 20.4 s) — consistent with wasm staying on V8's baseline
tier in that context. Treat that as an upper bound; a regular browser tab
should land much nearer the Node column.

Perspective for page use: these are deliberately heavy kernels — every shipped
example still runs in browser-comfortable time even at the pane's upper-bound
numbers (hello-world ~50 ms, the full 75×30 Mandelbrot ~3.6 s), and a fresh
worker + module instance is ready in tens of ms after the one-time `.wasm`
download (cached thereafter).

## Known limitations (single-threaded browser build)

- **Deep recursion (a few hundred Raku levels, ~200) hits a hard browser
  limit.** Under `-fexceptions`, C++ recursion is routed through JS exception
  trampolines and so consumes the *JS engine* stack, which a page cannot grow
  (unlike the native build's 1 GiB thread stack). The tree-walker nests many
  calls per Raku level, so recursion caps around ~200 levels — beyond that the
  browser raises a `RangeError`, which the playground catches, reports as a
  recursion-limit message, and recovers from. This is a browser constraint, not
  a Raku one: the same program runs natively
  ([docs/guide/MEMORY.md](../docs/guide/MEMORY.md) compares the measured
  recursion budgets of all three modes). Raising it would require rewriting the
  interpreter onto an explicit heap stack (a `src/` change, out of scope here).
  `-sSTACK_SIZE` does **not** help — verified. Iterative/loop-based programs are
  unaffected.
- **`start` / `Promise` concurrency** relies on real threads; it isn't available
  in this single-threaded build (a threaded build needs cross-origin-isolation
  COOP/COEP headers, awkward for static hosting). Ordinary course examples don't
  need it. The same goes for threads the engine takes on its own behalf: a
  `std::thread` that cannot start throws, and an escape reaches
  `std::terminate`, which the page sees as a bare `Aborted()` — the failure
  [`smoke.cjs`](#the-smoke-test) now guards against.
- **Sockets** (the pastebin showcase) don't work in the browser sandbox.
- **Files are in-memory and temporary.** File IO works (MEMFS, above), but the
  tree starts empty apart from Emscripten's own directories and is discarded with
  the module instance. Programs that expect files on disk, or state that outlives
  a run, need the native binary.
- **`--compile` / native codegen** is irrelevant here — this ships the
  interpreter (`EVAL`), not the C++ transpiler.
- **`exit`** in user code aborts the worker's module instance; the worker
  rebuilds a fresh one for the next run (no page reload needed).
- **Binary size**: a few MB of `.wasm` (≈2 MB gzipped), dominated by the Unicode
  tables. It downloads once and is cached. Runtime memory per instance is kept
  small (16 MiB stack / 32 MiB initial, growing on demand) so recreating the
  worker on Stop/restart doesn't pile up memory. If the module ever fails to
  instantiate (memory pressure after very heavy use), the playground auto-retries
  a couple of times and only then asks for a reload — it doesn't silently hang.

## Where the interpreter itself is documented

The `Value` model and execution: [../docs/internals/RUNTIME.md](../docs/internals/RUNTIME.md).
Source to AST: [../docs/internals/PARSING.md](../docs/internals/PARSING.md).
The overall pipeline: [../docs/internals/ARCHITECTURE.md](../docs/internals/ARCHITECTURE.md).
