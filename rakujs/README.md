# Raku.js — Raku in the browser (WebAssembly)

▶ **Try it live: [raku.online](https://raku.online/)**

**Raku.js** is the Raku++ interpreter compiled to **WebAssembly**. The name is
branding, not mechanism: it is *not* a reimplementation of Raku in JavaScript.
It is the exact same C++ interpreter from [`../src`](../src), built with
Emscripten, so a program behaves in the browser as it does under the native
`rakupp` and against the Roast suite. Nothing in `../src` is modified —
everything in this directory is additive.

Programs run **in the visitor's browser**: no server, no round-trip, nothing
uploaded. A page that embeds it costs you static files and nothing to operate.

There are three ways to use it. Take the first one that fits.

## 1. Put runnable Raku on a page

One script tag, and any element you mark becomes a real editor with a Run
button:

```html
<script src="https://raku.online/raku.js"></script>

<pre data-raku>say "Hello from an embedded editor!";</pre>
```

That is the whole integration — no build step, no npm, no account, and it works
on any host: a blog, a docs site, a slide deck. Ten editors on one page share a
single interpreter (one download, one worker), each lives in its own Shadow DOM
so nobody's CSS leaks either way, and `data-auto` on the script tag makes the
`<pre><code class="language-raku">` blocks your Markdown *already* emits
runnable without touching them.

| | |
|---|---|
| **[raku.online/embed/](https://raku.online/embed/)** | The guide — every option, in order |
| **[raku.online/builder/](https://raku.online/builder/)** | Paste code, tick options, copy a ready snippet |
| **[raku.online/demo/](https://raku.online/demo/)** | Every pattern side by side, all live |
| [COURSE-PLAY-BUTTONS.md](COURSE-PLAY-BUTTONS.md) | A worked cross-repo integration: how course.raku.org did it |

The widget itself (`raku.js`) belongs to the
[raku.online](https://github.com/ash/raku.online) repository. What *this*
directory builds is the engine underneath it.

## 2. Serve it from your own site

Nothing has to be loaded from raku.online. `raku.js` resolves everything
relative to its own URL, so **three files in one directory** of your site are a
complete, offline install:

| File | What it is |
|---|---|
| `raku.js` | the editor widget — the file your `<script src>` points at (32 KB) |
| `rakujs.js` | the Emscripten loader (110 KB) |
| `rakujs.wasm` | the interpreter (7 MB, ≈2 MB gzipped; downloaded once, then cached) |

```sh
mkdir -p your-site/raku && cd your-site/raku
curl -O https://raku.online/raku.js
curl -O https://raku.online/rakujs.js
curl -O https://raku.online/rakujs.wasm
```

…then `<script src="/raku/raku.js"></script>` instead of the raku.online URL.

To pin the engine to a released interpreter instead, every
[release](https://github.com/ash/rakupp/releases) attaches
`rakujs-<tag>.zip` with that version's `rakujs.js` + `rakujs.wasm` (plus the
standalone playground — see [PLAYGROUND.md](PLAYGROUND.md)):

```sh
gh release download -R ash/rakupp --pattern 'rakujs-v*.zip'
```

The widget `raku.js` is not in the zip; take it from the URL above.

Your server needs to do two things: send `.wasm` as `application/wasm` (most
static hosts already do — otherwise the browser cannot stream-compile), and
serve over `http(s)` (a `file://` page cannot start a Web Worker). No CORS
headers are needed for files on your own origin.

Full instructions, including what changes for the ↗ button:
**[raku.online/embed/#host-it-yourself](https://raku.online/embed/#host-it-yourself)**.

## 3. Call the interpreter from your own JavaScript

If you are building something of your own rather than embedding an editor — a
docs site that runs snippets its own way, a REPL, a grader, a test harness —
skip the widget and drive the module directly. It is `MODULARIZE`d under the
global `RakuJS`, and there is one function:

```html
<script src="rakujs.js"></script>
<script>
  RakuJS({
    print:    line => append(line + "\n"),
    printErr: line => append(line + "\n"),
  }).then(mod => {
    const rc = mod.ccall('rakupp_run', 'number', ['string', 'string'],
                         ['say 42;', '']);   // (source, stdin) → exit code
  });
</script>
```

The whole program is that first string; the second is what `get` / `lines` /
`prompt` read before hitting EOF. There are no files and no command-line
arguments — the browser sandbox has neither. Output arrives through the
`print` / `printErr` callbacks.

| Export | |
|---|---|
| `rakupp_run(source, stdin)` | runs a program to completion, returns its exit code |
| `rakupp_version()` | the interpreter's version string |
| `rakupp_highlight(source)` | the source as highlighted HTML, from `rakupp`'s own tokenizer |

`rakupp_run` is **synchronous** and runs a whole program, so call it from a Web
Worker unless you know every program is short — on the main thread it freezes
the page for the duration.

**[TUTORIAL.md](TUTORIAL.md) is the guide to this**: multi-line sources,
feeding input, getting results back into your page, and a complete working
example. Start there rather than from the snippet above.

## Build it

```sh
rakujs/build.sh            # release (-Oz) → playground/rakujs.{js,wasm}
rakujs/build.sh --debug    # -O0 + assertions
```

If `em++` isn't on your `PATH`, `build.sh` installs Emscripten into
`rakujs/emsdk/` (git-ignored, ~1 GB) on first run; `source
/path/to/emsdk/emsdk_env.sh` first to use an existing install. If a native
`rakupp` is around, the build also regenerates the playground's example list
with it. Flags, environment variables and the smoke test are in
[INTERNALS.md](INTERNALS.md#build).

## The rest of the documentation

| Page | What's in it |
|---|---|
| **[PLAYGROUND.md](PLAYGROUND.md)** | The standalone playground that ships in the release zip — running it, deploying it, its shareable links, its examples |
| **[TUTORIAL.md](TUTORIAL.md)** | Writing real browser Raku programs against `rakupp_run` |
| **[INTERNALS.md](INTERNALS.md)** | How the WebAssembly build works, what it costs (measured), and where it stops — recursion, threads, sockets |
| **[STACKED-INTERPRETERS.md](STACKED-INTERPRETERS.md)** | Python interpreted by Raku interpreted by C++ compiled to wasm, in a fraction of a second — and why it's fast |
| **[COURSE-PLAY-BUTTONS.md](COURSE-PLAY-BUTTONS.md)** | The course.raku.org integration, end to end |

## What's in this directory

| File | Purpose |
|------|---------|
| `rakupp_web.cpp` | The entry point — exports `rakupp_run` / `rakupp_version` / `rakupp_highlight` over the interpreter's existing `rakupp::rakuppRun()`. |
| `build.sh` | Compiles `../src/*.cpp` (minus `main.cpp`) + `rakupp_web.cpp` to `playground/rakujs.{js,wasm}`. Bootstraps Emscripten if absent. |
| `gen-examples.raku` | Generates `playground/examples.js` from `../examples/*.raku` — run with `rakupp` itself, so Raku.js generates its own data with the interpreter it ships. |
| `playground/` | The self-contained playground page (`index.html` + `worker.js`) and the built engine. |
| `smoke.cjs` | Runs the showcase interpreters through a Node-loadable build, so a browser-only crash is caught in CI. |
| `bench-runtime.cjs`, `bench.js` | The Node/Bun benchmark harness behind the numbers in [INTERNALS.md](INTERNALS.md#performance-vs-native-and-node-vs-bun-vs-browser). |
