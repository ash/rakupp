# Raku.js — Raku in the browser (WebAssembly)

**Raku.js** builds the Raku++ interpreter to **WebAssembly** so Raku programs run
entirely in the browser — no server, no round-trips. The intended use is
embedding runnable Raku examples directly in web pages (e.g. a Raku course).

The name is branding, not mechanism: Raku.js is **not** a from-scratch
reimplementation of Raku in JavaScript. It is the exact same C++ interpreter from
`../src`, compiled with Emscripten, so semantics are identical to the native
`rakupp` and to what it validates against the Roast suite. Nothing in `../src` is
modified — everything here is additive:

| File | Purpose |
|------|---------|
| `rakupp_web.cpp` | Thin entry point exporting `rakupp_run(src)` — calls the interpreter's existing `rakupp::rakuppRun()`. |
| `build.sh` | Compiles `../src/*.cpp` (minus `main.cpp`) + `rakupp_web.cpp` to `playground/rakujs.{js,wasm}`. Bootstraps Emscripten locally if absent. |
| `gen-examples.raku` | Generates `playground/examples.js` from `../examples/*.raku` (the single source of truth) — run with `rakupp` itself. |
| `playground/index.html` | A self-contained editor + output console with the example programs. |
| `playground/worker.js` | Runs the WASM interpreter in a Web Worker so the UI stays responsive (spinner, live output, a working Stop button). |

## Build

```sh
rakujs/build.sh            # release (-Oz)
rakujs/build.sh --debug    # -O0 + assertions
```

If the Emscripten toolchain (`em++`) isn't on your `PATH`, `build.sh` clones and
installs it into `rakujs/emsdk/` (git-ignored, ~1 GB) on first run. To use an
existing install instead, `source /path/to/emsdk/emsdk_env.sh` beforehand.

## Try it

```sh
cd rakujs/playground
python3 -m http.server 8000
# open http://localhost:8000/
```

Edit code on the left, press **▶ Run** (or ⌘/Ctrl-Enter); stdout/stderr appear
on the right. State resets each run (a fresh `Interpreter` per call). Output
streams as it's produced, so ANSI redraw programs animate live — try `life`
(Conway's Game of Life). Pressing **Run** while a program is already running
**restarts** it with the current source (so edit-then-Run just works); **■ Stop**
(or Escape) halts a long or runaway program by terminating the worker.

## Examples

The example dropdown is generated from [`../examples/`](../examples/) (the same
programs the CLI ships) by [`gen-examples.raku`](gen-examples.raku) — run with the
native `rakupp` binary by `build.sh`, so Raku.js generates its own playground data
with the interpreter it ships, and `examples/` stays the single source of truth.
All 21 run in the browser and match native `rakupp` output exactly (verified),
except `life`, which seeds a **random** board so it differs run to run.

The three concurrency/IO examples — `parallel`, `sleep-sort` (threads),
`echo-server` (sockets) — are **omitted**: they need real threads or sockets,
which the single-threaded WASM build doesn't have, so running them would hang the
page. Run those with native `rakupp`.

To regenerate after adding an example, re-run `build.sh` (or just
`rakupp gen-examples.raku`). Deep-recursion examples are unaffected by the ~200
recursion cap; all shipped examples stay well under it.

## Embedding in your own page

The build is `MODULARIZE`d under the global `RakuJS`. Capture the program's
output through Emscripten's `print` / `printErr` and call the exported function:

```html
<script src="rakujs.js"></script>
<script>
  RakuJS({
    print:    line => append(line + "\n"),
    printErr: line => append(line + "\n"),
  }).then(mod => {
    const rc = mod.ccall('rakupp_run', 'number', ['string'], ['say 42;']);
    // rc is the Raku exit code; output already delivered via print/printErr
  });
</script>
```

## How it works / design notes

- **Runs in a Web Worker** (`playground/worker.js`). `rakupp_run()` is a
  *synchronous* call that runs a whole program to completion; on the main thread
  that freezes the UI (no spinner, output only at the end). The worker keeps the
  main thread free, so the page animates a spinner after 300 ms, streams output
  live (ANSI cursor-home is treated as a redraw, so `life` animates frame by
  frame), and can **Stop** a runaway program by terminating the worker. rendering
  is coalesced on a timer, not `requestAnimationFrame` (which pauses in
  background/hidden tabs and would drop output there).
- **Entry point.** `rakupp_run()` calls `rakupp::rakuppRun()` (see
  `../src/Runtime.h`), the same function the native CLI uses for a normal run.
  It lexes, parses, builds an `Interpreter`, and runs — catching `ParseError` /
  `RakuError` / `std::exception` and reporting them to stderr just like the CLI.
- **No big-stack thread.** The native CLI runs on a 1 GiB pthread stack via
  `rakuppRunBigStack()`. WASM is single-threaded, so we call `rakuppRun()`
  directly. See the recursion note under limitations.
- **Exceptions: `-fexceptions`, not `-fwasm-exceptions`.** The interpreter leans
  on C++ exceptions for both errors AND control flow (`last`/`next`/`redo`,
  `when`/`succeed`). Native Wasm-EH would give deeper recursion, but with
  emscripten 6.0.3 its personality fails to match the interpreter's by-value
  control-exception catches (`catch (LastEx&)` …), so `last`/`next`/`given`/`when`
  escape to `std::terminate` and trap (`RuntimeError: unreachable`) — verified.
  `-fexceptions` handles them correctly, at the cost of recursion depth (below).
- **Output** goes to `std::cout`/`std::cerr`, which Emscripten routes to the
  `print`/`printErr` callbacks. No interpreter changes needed.

## Known limitations (single-threaded browser build)

- **Deep recursion (a few hundred Raku levels, ~200) hits a hard browser limit.**
  Under `-fexceptions`, C++ recursion is routed through JS exception trampolines
  and so consumes the *JS engine* stack, which a page cannot grow (unlike the
  native build's 1 GiB thread stack). The tree-walker nests many calls per Raku
  level, so recursion caps around ~200 levels — beyond that the browser raises a
  `RangeError`, which the playground catches, reports as a recursion-limit message,
  and recovers from. This is a browser constraint, not a Raku one: the same program
  runs natively. Raising it would require rewriting the interpreter onto an explicit
  heap stack (a `src/` change, out of scope here). `-sSTACK_SIZE` does **not**
  help — verified. Iterative/loop-based examples are unaffected.
- **`start` / `Promise` concurrency** relies on real threads; it isn't available
  in this single-threaded build (a threaded build needs cross-origin-isolation
  COOP/COEP headers, awkward for static hosting). Ordinary course examples don't
  need it.
- **Sockets** (the pastebin showcase) don't work in the browser sandbox.
- **`--compile` / native codegen** is irrelevant here — this ships the
  interpreter (`EVAL`), not the C++ transpiler.
- **`exit`** in user code aborts the worker's module instance; the worker rebuilds
  a fresh one for the next run (no page reload needed).
- **Binary size**: a few MB of `.wasm` (≈1–3 MB gzipped), dominated by the
  Unicode tables. It downloads once and is cached. Runtime memory per instance is
  kept small (16 MiB stack / 32 MiB initial, growing on demand) so recreating the
  worker on Stop/restart doesn't pile up memory. If the module ever fails to
  instantiate (memory pressure after very heavy use), the page auto-retries a
  couple of times and only then asks for a reload — it doesn't silently hang.

## Publishing (GitHub Pages)

`rakujs.js` / `rakujs.wasm` (and generated `examples.js`) are git-ignored by
default. To publish the playground, either commit them deliberately
(`git add -f rakujs/playground/rakujs.* rakujs/playground/examples.js`) or build
them in CI and upload `rakujs/playground/` as the Pages artifact. Serve `.wasm`
with `Content-Type: application/wasm` (GitHub Pages already does).
