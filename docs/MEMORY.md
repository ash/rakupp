# Memory: demands and limitations

What rakupp reserves, what it actually uses, how deep recursion can go, and
how the three execution modes (interpreter, `--exe` native, WebAssembly)
differ. All figures below were measured on macOS arm64 with the probe

```raku
sub deep(Int $n) { $n == 0 ?? 0 !! 1 + deep($n - 1) }
```

Depths scale inversely with frame size, so a sub with more parameters,
locals, or closures recurses less deep; a leaner one recurses deeper.

## Reserved vs. resident

rakupp *reserves* large stacks but only *commits* what a program touches.
Reservation is address space, not RAM: a hello-world costs a few megabytes
of resident memory even though a gigabyte of stack is reserved behind it.
Memory grows as recursion deepens, page by page, and only a program running
near the recursion limit actually occupies the full reservation.

| | reserved (virtual) | resident, `say 1` | resident at max recursion |
|---|---|---|---|
| interpreter | 1 GiB main + 256 MiB per worker | 2.7 MB | ~950 MB |
| `--exe` native | 1 GiB main + 256 MiB per worker | 5 MB | ~1.07 GB |
| wasm (raku.online) | 16 MiB stack + 32 MiB heap, heap growable | n/a (per-tab) | bounded by the JS engine, not the wasm stack |

So: you do not *need* a gigabyte of RAM to run rakupp — you need a gigabyte
of *address space* (a non-issue on 64-bit hosts), and as much RAM as your
program's data plus its deepest live recursion actually touch.

## Stack layout by mode

**Interpreter.** The mainline never runs on the OS main thread: `onBigStack`
(src/Runtime.cpp) moves it to a thread with a **1 GiB** stack. Every `start`
block / Promise worker gets its own **256 MiB** stack (`BigStackThread`).
The GIL serializes execution, but each parked worker keeps its reservation.

**`--exe` native.** Identical layout since the generated `main()` routes the
whole program through `rakuppMainOnBigStack` — the same 1 GiB thread. (The
binaries also carry link-time main-stack flags — 512 MiB `-stack_size` on
macOS, 256 MiB `/STACK` on Windows — as belt-and-suspenders; execution does
not rely on them anymore.) Workers are the same 256 MiB threads, provided by
the linked runtime.

**Wasm.** The Emscripten build ships `-fexceptions`, which routes every
throwing call through a JavaScript `invoke_*` trampoline — so recursion
consumes the *JS engine's* stack, which a page cannot grow. The wasm-side
`STACK_SIZE` (16 MiB) and `INITIAL_MEMORY` (32 MiB, growable) are kept small
deliberately: raising them buys no recursion depth, only a fatter per-tab
footprint. See rakujs/build.sh for why `-fwasm-exceptions` is not usable yet
(Wasm-EH mismatches the interpreter's by-value control-flow catches).

## Recursion depth in practice

| mode | measured limit (probe above) | ≈ per frame | failure mode |
|---|---|---|---|
| interpreter, mainline | ~22,400 levels | ~47 KB | catchable `X::Recursion` |
| interpreter, inside `start` | ~5,500 levels | ~47 KB | catchable `X::Recursion` |
| `--exe`, interpreter-dispatched calls | as interpreter | ~47 KB | catchable `X::Recursion` |
| `--exe`, direct native sub calls | ~460,000 levels | ~2.3 KB | **process death (SIGBUS), not catchable** |
| wasm | ~200 levels | JS-engine frames | host `RangeError`, caught by the playground |

The interpreter's guard (`DepthGuard`, src/Interpreter.cpp) fires while
about **2 MiB of headroom** remains on the current thread's stack, so the
throw itself unwinds safely instead of the process taking a stack-overflow
signal. On stacks smaller than 8 MiB the reserve scales down to a quarter of
the stack. A hard backstop of 100,000 frames exists but the stack limit
binds first at current frame sizes.

Known limitation: natively-compiled subs call each other as plain C++
functions with **no guard**, so a runaway *native* recursion dies with
SIGBUS at stack exhaustion (~460k levels of the probe) instead of a
catchable `X::Recursion`. Native code that recurses through interpreter
dispatch — closures in variables, dynamic dispatch, `&`-vars — passes
through the guard and gets the catchable error. Bounded recursion of any
realistic depth is unaffected; the interpreter reaches only ~5% of the
native limit before its own guard fires.

Foreign threads (not created by rakupp) get the scaled-down guard reserve
and whatever stack their creator gave them.

## Data-side bounds and guardrails

- **Values** are shared-structure (`shared_ptr` arrays/hashes): assignment
  of containers copies structure lazily at mutation boundaries; `@a = …` /
  `%h = …` refill the existing container in place.
- **Env→closure cycles** from named inner subs are collected at frame
  teardown (`breakSelfClosures`) — a 300k-call loop that used to retain
  ~425 MB of dead frames now stays around 3 MB.
- **BigInt** is arbitrary precision: memory grows with the number. There is
  no cap; `10 ** 10 ** 8` will happily eat what it needs.
- **Infinite ranges** never materialize: `flatten()` on `1..Inf` yields a
  bounded 10,000-element prefix; lazy iteration streams beyond that.
- **Regex matching** carries an 8-million-step budget shared across start
  positions, so a pathological pattern degrades to a fast failure rather
  than unbounded time/memory.
- **sprintf** width/precision are capped at 10,000,000 to keep a typo like
  `%999999999d` from allocating gigabytes.

## Footprint on disk

| artifact | size |
|---|---|
| `rakupp` binary (interpreter + compiler) | 7.4 MB |
| `librakupp_rt.a` (runtime archive `--exe` links) | 11 MB |
| a typical `--exe` output binary | ~7 MB (static runtime included) |
| wasm bundle (raku.online) | see rakujs/README.md |

`--exe` binaries are self-contained: the size is almost entirely the linked
runtime, so it stays flat as the program grows.

## Practical guidance

- Deep recursion on purpose? The interpreter gives ~22k levels of a simple
  sub; convert to iteration or `gather`/lazy sequences beyond that, or
  compile with `--exe` where direct native recursion reaches hundreds of
  thousands of levels (but overflow there is fatal, not catchable).
- Recursion inside `start` blocks has about a quarter of the mainline
  budget (256 MiB vs 1 GiB).
- On the wasm playground, treat recursion beyond ~150 levels as
  non-portable.
- 32-bit hosts are not supported: the address-space reservations alone
  exceed a 32-bit process.
