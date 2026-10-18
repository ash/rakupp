# Embedding Raku++ — running Raku from your own program

The mirror of [EXTENSIONS.md](EXTENSIONS.md). That one is native code called
**from** Raku; this one is Raku called **from** native code. Same boundary, same
value vocabulary, one header including the other — because a project that grew
two ways to spell "a Raku value" would spend the rest of its life keeping them
in agreement.

- **The header:** [`include/rakupp/rakupp.h`](../../include/rakupp/rakupp.h), installed to
  `<prefix>/include/rakupp/rakupp.h`. It includes `rakupp_ext.h`, so everything
  in the [extension ABI](EXTENSIONS.md#the-abi) — `rk_int_get`, `rk_at_pos`,
  `rk_call`, `rk_root` — is available to a host too.
- **Worked example:** [`tools/embed/embed-host.c`](../../tools/embed/embed-host.c),
  plain C99, run as a gate by `tools/embed-smoke.raku`.
- **Real embedder:** [`rakujs/rakupp_web.cpp`](../../rakujs/rakupp_web.cpp) —
  the WebAssembly entry point behind raku.online, written against this API.
- **On a phone:** the Learn Raku app, for iOS and Android —
  [Mobile apps](#mobile-apps-ios-and-android) below.
- **Another:** [`src/McpServer.cpp`](../../src/McpServer.cpp) — `rakupp --mcp`
  ([MCP.md](integrations/MCP.md)) serves the interpreter to AI agent clients through this
  same API, in-process.

## Hello

```c
#include <rakupp/rakupp.h>
#include <stdio.h>

int main(void) {
    RkInterp rk = rk_new(0);              /* 0 = default config */
    RkValue v;

    if (rk_eval(rk, "(1..10).grep(*.is-prime).sum", &v) == RK_OK)
        printf("%lld\n", rk_int_get(rk_ctx(rk), v));      /* 17 */
    else
        fprintf(stderr, "raku: %s\n", rk_last_error(rk));

    rk_free(rk);
}
```

Link against `librakupp` (built with `-DRAKUPP_BUILD_SHARED=ON`) or against
the static archive set that `--exe` already uses —
`librakupp_{rt,parse,ucd_names,ucd_coll,ucd_props}.a`, inside
`-Wl,--start-group … -Wl,--end-group` on GNU ld (rt and parse reference each
other; ld64 and link.exe iterate on their own and need no group).

## An interpreter is a session

`rk_eval` runs in the mainline scope and keeps it, exactly as the REPL does:

```c
rk_eval(rk, "my $x = 41", 0);
rk_eval(rk, "$x + 1", &v);            /* 42 */
rk_eval(rk, "sub twice($n) { $n * 2 }", 0);
```

…which is what makes the next part work.

## Calling a Raku routine

There is no separate host-side call API, because the extension ABI already had
one. `rk_ctx` hands you the context and you use `rk_call`:

```c
RkCtx c = rk_ctx(rk);
RkValue arg = rk_int(c, 21);
RkValue out = rk_call(c, "twice", &arg, 1);       /* 42 */
```

If the routine throws, `rk_call` returns NULL and `rk_error(c)` has the message
— a Raku exception never unwinds through your frames, because C++ unwinding into
C is undefined behaviour rather than a diagnosable bug.

## `rk_eval` or `rk_run`?

Two different jobs, and picking the wrong one is the most likely early mistake.

| | `rk_eval` | `rk_run` |
|---|---|---|
| Runs | an expression, in the session | a whole program |
| Gives you | the value | the exit code |
| On failure | `RK_ERROR` + `rk_last_error` | prints `===SORRY!===` to stderr, exit code 1 |
| `MAIN`, `exit`, phasers | no | yes |
| Use it for | Raku as a scripting/rules language | hosting Raku *as* Raku |

`rk_eval_file` is `rk_eval` given a path instead of a string, and it is what most
embedders actually want: the path also becomes the interpreter's idea of the
running file, so `$?FILE` and a relative `use lib` resolve the way they would
from the command line. `rk_version` answers the engine's version string. There
are three statuses, not two — `RK_FATAL` is the third, for a failure the session
cannot continue past. And the header carries its own `RAKUPP_ABI`, currently 2,
which is the number a host checks; it is independent of the `RAKUPP_EXT_ABI` an
*extension* checks.

The playground uses `rk_run`; a game embedding Raku for its rules wants
`rk_eval`.

## Lifetime

**A value from `rk_eval` is valid until the next `rk_eval` on that
interpreter.** Each evaluation releases what the previous one produced — the
host-shaped spelling of the extension rule that a handle dies with its call.

To keep one longer, root it:

```c
RkValue kept = rk_root(c, v);   /* survives any number of evaluations */
...
rk_unroot(c, kept);             /* you free it */
```

That is the one place this ABI lets you leak, which is exactly why it is opt-in
and why the arena — where you cannot — is the default.

## Output and input

```c
void on_output(void* ud, const char* text, size_t len, int is_err) { ... }
rk_set_output(rk, on_output, 0);        /* capture everything Raku prints */
rk_set_output(rk, 0, 0);                /* hand the streams back */

rk_set_input(rk, "line one\n", 9);      /* `get`/`lines` read this, then EOF */
```

Without `rk_set_input`, a Raku program calling `get` blocks on the host's real
stdin — which a server or a GUI may not have.

## What a library must not do to its host

Three things the CLI does and this does **not**, unless asked:

```c
RkConfig cfg = {0};
cfg.size = sizeof cfg;
cfg.own_stack      = 1;   /* run Raku on a big-stack thread (deep recursion) */
cfg.handle_sigpipe = 1;   /* ignore SIGPIPE process-wide (TCP servers want it) */
cfg.own_stdout     = 1;   /* flush cout/cerr after each evaluation */
RkInterp rk = rk_new(&cfg);
```

All default off. `SIGPIPE` in particular is a **process-wide** disposition: it is
your signal handling, not ours, so you have to ask. Set `size` and the struct can
gain fields without breaking you.

One more field confines the Raku instead of sparing the host:
`cfg.sandbox = 1` runs it as `rakupp --sandbox` does, with no files, processes,
sockets, native code or environment ([SANDBOX.md](SANDBOX.md)).

## Threads

One interpreter, one thread, unless you serialise access yourself.

Raku code *inside* the interpreter still uses as many threads as it likes —
`start`, `race`, `hyper` — and since v3.0.0 that is the default rather than
opt-in. So a callback reached from Raku can be entered on a thread you never
created, and by more than one at once. Make those re-entrant.

## Limits worth knowing

- **One interpreter per process.** `rk_new` returns NULL if one is already live:
  the interpreter wires up process-global state at construction, so a second
  would quietly break the first. Sequential create/free is fine. Concurrent
  instances are a separate piece of work
  ([EMBED-PLAN](../dev/plans/EMBED-PLAN.md)'s E5).
- **The host's privileges, unless you ask.** An embedded Raku++ can do
  whatever your process can. Set `RkConfig.sandbox` and the Raku code it runs
  cannot touch files, start processes, open sockets or call native code, and
  its `%*ENV` starts empty ([SANDBOX.md](SANDBOX.md)). That confines the Raku
  code, not your process: only the interpreter's checks apply, not the OS
  layer that `rakupp --sandbox` adds, because your process still needs its
  own files and sockets. The switch is process-wide and one-way.

## Mobile apps: iOS and Android

The same C ABI works inside a phone app, as native code: the interpreter is
compiled for the device's processor, linked into the app, and called through
this header — not Raku.js in a web view. The Learn Raku app (the Course of
Raku for iOS and Android) is built this way: every runnable example in the
course has a Run button, and there is a playground. What follows is what that
took, as of 5.4.1.

Native rather than WebAssembly for three reasons. Raku code gets threads
(`start`, `await`, `hyper`), which the single-threaded WebAssembly build does
not have. Recursion goes as deep as the stack you give it, where the
WebAssembly build stops at about 200 nested calls ([rakujs/build.sh](../../rakujs/build.sh)).
And there is no JavaScript bridge between the app's UI and the engine.

### Building the library

There is no CMake preset for phones; the runtime is compiled with the
platform's own compiler. The sources are those of `rakupp_rt`: every
`src/**/*.cpp` except `main.cpp`, `Repl.cpp`, `Codegen.cpp`,
`JsRuntimeSrc.cpp`, `codegen/Js.cpp` and `src/stubs/`, plus
`rakujs/cnp_none.cpp` — the empty stencil table, because neither platform lets
an app map executable memory, so there is no JIT.

```sh
-std=c++17 -O2 -fPIC -fexceptions -frtti -fvisibility=hidden -fvisibility-inlines-hidden
-DRAKUPP_BUILDING -DRAKUPP_VERSION=\"5.4.1\" -Isrc -Iinclude -Iinclude/rakupp
```

| | iOS | Android |
|---|---|---|
| Targets | `arm64-apple-ios13.0` for devices; `arm64-` and `x86_64-apple-ios13.0-simulator` | `aarch64-linux-android24`, `armv7a-linux-androideabi24`, `x86_64-linux-android24` (NDK r28) |
| Library | a dynamic framework per slice, combined with `xcodebuild -create-xcframework` | `librakuengine.so` per ABI, in the app's `jniLibs/` |
| Link | `-dynamiclib -install_name @rpath/RakuEngine.framework/RakuEngine -liconv` | `-shared -static-libstdc++ -Wl,-z,max-page-size=16384 -Wl,--no-undefined -ldl -lm` |
| iconv | `-DRAKUPP_HAVE_ICONV`: libiconv is in the SDK | left out: Bionic has iconv only from API 28. That loses gb2312, gb18030 and windows-932 |
| Size | about 16 MB per architecture | 17–20 MB per ABI |

Google Play requires native code aligned to 16 KB pages, hence
`max-page-size`. `--no-undefined` makes a Bionic call that is missing at the
app's `minSdk` fail the link instead of the app. Both stores deliver only the
architecture a device needs.

5.4.1 needs four small source changes on these targets:

- **iOS:** `std::system` is unavailable. Guard `run` in `src/Jit.cpp`; the
  `--jit` path it serves cannot run on a phone anyway.
- **iOS:** `pthread_jit_write_protect_np` is unavailable, so the `MAP_JIT`
  fallback in `src/cnp/CnpEmit.cpp` has to go.
- **iOS:** the SDK has no `getentropy`. `osRandom` in `src/DataRandom.cpp` can
  use `arc4random_buf`.
- **Android below API 30:** Bionic declares `struct statx` but not `statx()`,
  so `fileBirthSecs` in `src/BuiltinsShared.h` has to take its fallback, and
  `IO::Path.created` answers 0.

### Calling it from the app

Flutter (`dart:ffi`), Swift and Kotlin (JNI) reach the C ABI the same way, and
the same few things shape the host side:

- **Run on a thread of your own.** `rk_run` is synchronous; the UI thread must
  never wait on it. Give the thread a big stack — 512 MB works on both
  platforms, and recursion 5,000 calls deep then runs — rather than setting
  `own_stack`, whose 1 GiB a 32-bit Android process may refuse. The recursion
  guard measures whatever stack it is on.
- **Copy output in the callback.** `rk_set_output` calls back on the run thread
  with bytes that live only for the call. Append them to a mutex-guarded buffer
  and let the UI collect it on a timer; 40 ms keeps output live and turns a loop
  of `say`s into a few updates. Cap the buffer: a runaway `loop { say 1 }`
  otherwise grows until the OS ends the app.
- **A fresh interpreter for every run.** There is one interpreter per process
  (above): `rk_new`, `rk_run`, `rk_free` each time, so one program's `class
  Point` never meets the next one's.
- **Input.** `rk_set_input` feeds a preset. For a prompt that waits, as in a
  terminal, install your own `std::streambuf` as `std::cin`'s buffer — the
  same swap `rk_set_input` makes — whose `underflow()` blocks until the UI
  sends a line or ends the input.
- **Stopping.** The ABI has no interrupt yet, and a Stop button needs one.
  Learn Raku adds `rk_interrupt(rk)`: a flag checked at each statement, block
  entry and Int-kernel loop iteration, which raises `X::AdHoc "Interrupted"` and
  stays raised until the run ends, so a `try` cannot swallow it.

### What a program can do there

- **Files** — inside the app's sandbox. Change to a writable directory (the
  app's temporary or cache directory) before `rk_run`, so relative paths work.
- **Processes** — `run`, `shell` and `Proc::Async` cannot spawn anything on iOS.
- **Network** — sockets work. HTTPS goes through the system's OpenSSL by
  NativeCall ([HTTPS.md](HTTPS.md)), which neither platform offers an app.
- **NativeCall** — iOS loads only libraries signed into the app bundle.

Apple's App Review guideline 2.5.2 allows an educational app to run code that
the user writes, as long as the app does not download code. Google Play
restricts downloading native code, not running interpreted programs.

## Bindings

You may not need the C surface directly. [bindings/](../../bindings/README.md)
holds six hosts over it — Python (ctypes; ships as a platform wheel with
librakupp bundled), JS/TS (bun:ffi), Go (cgo), Rust (zero-dependency crate),
C++ (`<rakupp/raku.hpp>` and `<rakupp/grammar.hpp>`, header-only, in the
install layout), and the Wolfram Language (`ForeignFunctionLoad`, 13.3+). Each gives you the same two things this header gives you,
in its own idiom: **run Raku** (evaluate source, call routines with host
values, read results back) and **parse with Raku grammars**
([GRAMMAR-PLAN](../dev/plans/GRAMMAR-PLAN.md)) — the workload embedding was
named for. All six drive the same Raku shim, which lives INSIDE the library
(`rk_grammar_shim`) so a binding can never skew against its engine; all five
are byte-compared against plain `rakupp` by `tools/grammar-smoke.raku`, and
their worked examples are checked by `tools/bindings-smoke.raku`, both in CI.
The other direction — your functions callable from Raku — is `rk_register`.

## See also

- [EXTENSIONS.md](EXTENSIONS.md) — the other direction, and the value ABI both
  share
- [ABI-PLAN.md](../dev/plans/ABI-PLAN.md) — why the boundary is C, why an
  extension never sees `Value`, and what is still to come
- [GRAMMAR-PLAN.md](../dev/plans/GRAMMAR-PLAN.md) — grammars as a service:
  the design the Python binding implements, and the measured cost of each
  access pattern
