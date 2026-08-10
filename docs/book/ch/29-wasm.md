# Raku in the Browser

The four modes in Chapter 24 all target a machine with a filesystem, threads
and a dynamic loader. There is a fifth target that has none of those: the same
runtime compiled to **WebAssembly**, which is Raku.js — the interpreter running
in a browser tab, with no server.

It is not a fifth back end. It is mode 1, the tree walk, with a different host.
What makes it worth a chapter is that the host removes almost everything the
interpreter assumes, and the adaptation is **94 lines that live entirely outside
`src/` and change nothing in the interpreter**.

## The whole boundary

```cpp
// rakujs/rakupp_web.cpp — the three exported functions
EMSCRIPTEN_KEEPALIVE int         rakupp_run(const char* src,
                                            const char* stdin_text);
EMSCRIPTEN_KEEPALIVE const char* rakupp_highlight(const char* src);
EMSCRIPTEN_KEEPALIVE const char* rakupp_version();
```

`rakupp_run` calls `rakupp::rakuppRun` — the same public entry point the native
CLI uses — which lexes, parses, builds an `Interpreter`, runs it, and catches
`ParseError`, `RakuError` and `std::exception`, reporting them to stderr exactly
as the CLI does. The exit code that comes back is the process exit code the CLI
would have produced.

The other two exports matter more than they look: `rakupp_highlight` is the
tokenizer behind `rakupp --highlight` (Chapter 35), and it is what lets a
browser editor paint Raku with the compiler's own knowledge rather than a
JavaScript approximation. That thread is picked up at the end of this chapter.

## Input is one `rdbuf` swap

The interpreter reads standard input exclusively through `std::cin`. A browser
has no standard input, and Emscripten's TTY device brings its own buffering and
EOF state. So feeding a program is a stream-buffer swap and nothing else:

```cpp
// rakujs/rakupp_web.cpp
static std::istringstream web_stdin;
web_stdin.clear();
web_stdin.str(stdin_text ? stdin_text : "");
std::streambuf* old_in = std::cin.rdbuf(web_stdin.rdbuf());

int rc = rakupp::rakuppRun(src ? src : "", {}, "web", "rakupp.wasm", {});

std::cin.rdbuf(old_in);
```

`get`, `lines`, `prompt` and `$*IN.slurp` read that text and then see
end-of-file, so **nothing ever blocks** — which matters, because a blocking read
in a WebAssembly module with no thread to park is a hung tab.

Two details make it reliable across repeated runs. `rdbuf()` clears `cin`'s
eof and fail bits as a side effect, so a program that read to EOF does not
poison the next run. And a caller built against the older one-argument
signature passes no second argument, which arrives as null and means an empty
stdin rather than undefined behaviour.

## Output, and the flush that is not optional

Output needs no interpreter change at all: `std::cout` and `std::cerr` are
routed by Emscripten to the `print` and `printErr` callbacks the host page
installs. What needed care is the *end* of a run.

```cpp
// rakujs/rakupp_web.cpp — after the program returns
std::cout.flush();  std::cerr.flush();
std::fflush(stdout); std::fflush(stderr);
fsync(STDOUT_FILENO);
fsync(STDERR_FILENO);
```

Emscripten's TTY only emits a line to `print` **on a newline**. A program ending
in `print` rather than `say` therefore left its final, newline-less line sitting
in the device buffer — and that line then appeared on the *first line of the next
run*, attributed to a program that never wrote it.

`fflush` does not clear it. `fsync` does, because that is what triggers the
TTY's flush operation. Those two lines are load-bearing, and the bug they fix is
the kind that only shows up when someone runs two programs in a row.

## The stack is chosen, not inherited

Every native entry point runs the program on a thread with a very large stack
(Chapter 12), because the tree walker's recursion depth is the Raku recursion
budget. A single-threaded WebAssembly build cannot spawn that thread.

So the web entry point deliberately calls `rakuppRun` rather than
`rakuppRunBigStack`, and reserves a large *WebAssembly* stack at link time
instead. The interpreter's own recursion guard measures the real stack and
throws `X::Recursion` before it overflows, so this is safe rather than merely
lucky.

The honest consequence is a much lower ceiling — **a few hundred Raku levels,
around 200** — and the reason is the next section.

## Exceptions: `-fexceptions`, not `-fwasm-exceptions`

This is the most interesting engineering decision in the WebAssembly build, and
it connects directly to Chapter 14.

The interpreter leans on C++ exceptions for both errors **and control flow**:
`last`, `next`, `redo`, `when`, `succeed`, `return` across a boundary. Native
WebAssembly exception handling would be faster and would give deeper recursion —
but with the toolchain in use its personality routine fails to match the
interpreter's by-value control-exception catches. A `catch (LastEx&)` does not
catch, so `last`, `next`, `given` and `when` escape to `std::terminate` and the
module traps with `RuntimeError: unreachable`. That was verified, not assumed.

So the build ships `-fexceptions`: JavaScript-based exception handling, which
handles them correctly. The cost is exactly the recursion ceiling above — under
`-fexceptions` every throwing call is routed through a JavaScript trampoline and
consumes the **JavaScript engine's** stack, which a page cannot grow.

`-sSTACK_SIZE` does not help, and that was verified too. So was something less
obvious: `-Oz` beats `-O2` here on *both* axes — smaller **and** deeper, 206
levels against 174 — because a smaller compiled body means fewer frames per Raku
level.

This is also the sharpest illustration of why Chapter 14's cooperative control
flow exists. On this host, a C++ throw is not merely slow; it is the thing that
bounds how deep a Raku program may recurse. Every `return` and `last` the
interpreter resolves with a flag instead of a throw is one that does not touch
the JavaScript stack at all.

## Off the main thread

`rakupp_run` is **synchronous**: it runs a whole program to completion. On the
main thread that freezes the page — no spinner animates, and output appears only
at the end, if at all.

So the playground runs it in a dedicated worker:

```js
// rakujs/playground/worker.js
importScripts('rakujs.js' + V);          // the MODULARIZE factory

function makeModule() {
  return RakuJS({
    locateFile: p => p + V,
    print:    t => { if (inRun) post('out', { text: t + '\n', cls: ''    }); },
    printErr: t => { if (inRun) post('out', { text: t + '\n', cls: 'err' }); },
  }).then(m => { Module = m; return m; });
}
```

Three things follow from the worker that could not be had without it.

**Output streams.** `print` posts to the main thread as it fires, so a program
that redraws — Game of Life, say — animates frame by frame instead of arriving
as one block at the end. The page treats an ANSI cursor-home as a redraw.
Repainting is coalesced on a timer rather than `requestAnimationFrame`, because
that pauses in a hidden tab and would drop output there.

**A runaway program can be stopped**, by terminating the worker. There is no
other way to interrupt a synchronous call.

**A failure is recoverable.** A `RangeError` from deep recursion, or an `exit`
in user code, leaves the module instance in an unknown state, so the worker
throws it away and builds a clean one for the next run:

```js
// rakujs/playground/worker.js
catch (err) {
  Module = null;
  ready = makeModule();
  post('runerror', { message: String(err),
                     deep: err instanceof RangeError
                           || /call stack/i.test(String(err)) });
}
```

The `inRun` flag is a small thing worth noticing: outside a run, `print` is
Emscripten's own load-time diagnostics, which belong in the devtools console
rather than in the user's output pane.

## Live coding: one script tag

The playground is one page. The *live-coding* mode is the interesting part,
because it turns any page on any site into a Raku environment:

```html
<script src="https://raku.online/raku.js"></script>
<pre data-raku>say "Hello from an embedded editor!";</pre>
```

Every element carrying `data-raku` becomes an editable, runnable editor. That is
what makes the specification site, the tour and the course pages show examples
that a reader can change and re-run, rather than screenshots of output.

Five decisions hold it up.

**One interpreter per page.** Multiple blocks share a single WebAssembly
instance — one download, one instance — so a page with twenty examples does not
fetch twenty engines.

**The worker is built from a Blob.** A plain `new Worker('https://…')` is
blocked cross-origin; a Blob worker that `importScripts` the cross-origin engine
is allowed. That single workaround is why the embed works from someone else's
domain at all.

**Runs are queued, not dropped.** Only one program runs at a time, so a request
made during another run waits its turn:

```js
// raku.js
function enqueue(block) {
  if (queue.indexOf(block) < 0) queue.push(block);
  relabel();
}
```

This used to be a single `queued` slot, and a third request overwrote the second
— press Run on several blocks and only the last one ever started. Every path
that ends a run now calls `next()`, **including the failures**, because a load
error or a killed worker used to clear the current block and leave the rest of
the queue stranded for ever.

**Each editor lives in its own Shadow DOM**, so the host page's CSS cannot reach
in and the embed's cannot leak out. It looks the same on any site.

**The editor is a transparent textarea over a highlighted `<pre>`.** The two are
positioned on top of one another and their scroll offsets are kept in step:

```js
// raku.js
hl.innerHTML = html;
hl.scrollTop = ta.scrollTop; hl.scrollLeft = ta.scrollLeft;
```

And the highlighting is the payoff for exporting `rakupp_highlight`. Until the
engine has loaded, editors paint with a fast JavaScript approximation; once it
is there, **every editor repaints through rakupp itself** — the same tokenizer as
`rakupp --highlight`, so an embedded editor colours Raku exactly as the compiler
understands it, and keeps doing so as the reader types.

Nothing is sent anywhere. The program runs in the visitor's browser.

## What the host takes away

| | |
|---|---|
| **deep recursion** | around 200 Raku levels, then a `RangeError` the page reports as a recursion-limit message and recovers from |
| **`start` / `Promise`** | needs real threads; a threaded build needs cross-origin isolation headers, awkward for static hosting (Chapter 34) |
| **sockets** | not available in the browser sandbox |
| **NativeCall** | takes its no-libffi fallback path by construction — there is no shared library to open (Chapter 32) |
| **`--exe` and the code generator** | irrelevant: this ships the interpreter, not the transpiler |
| **`exit`** | aborts the module instance; the worker rebuilds a fresh one |

The recursion limit is the one users actually meet, and the message says what it
is: a WebAssembly stack limit, not a Raku one. The same program runs natively.
Lifting it would mean rewriting the tree walker onto an explicit heap stack —
a change to `src/`, and a large one.

## Size and memory

A few megabytes of `.wasm`, roughly one to three compressed, **dominated by the
Unicode tables** (Chapter 23). It downloads once and is cached, with a hash-based
cache tag so a release invalidates it exactly when the engine changes and not
otherwise.

Per-instance memory is kept deliberately small — a 16 MiB stack and 32 MiB
initial heap, growing on demand — precisely because the worker recreates the
instance on every Stop and every crash, and a large fixed footprint would pile
up. If instantiation ever fails under memory pressure, the page retries a couple
of times before asking for a reload rather than hanging silently.

## Why this is worth having

Two reasons beyond the obvious one.

It is a **fourth client of the runtime**, after the interpreter, the native code
generator and the extension ABI — and the one with the least in common with the
others. Every assumption the runtime makes about its host that turned out to be
unnecessary was found by porting it here.

And it is a **test surface**. The playground runs the same example programs the
local suite does, so a divergence between the WebAssembly build and the native
one is a real bug in something shared. The showcase interpreters run inside it
too: a JavaScript interpreter, written in Raku, running on Raku++ compiled to
WebAssembly, inside a browser — which is a stack that exercises a great deal at
once.
