# The Raku.js playground

A self-contained editor-and-console page that runs Raku in the browser. It is
what `rakujs/build.sh` builds into [`playground/`](playground), and what each
release attaches as `rakujs-<tag>.zip`.

For *embedding* runnable code in your own pages, you want the widget instead —
see [README.md](README.md#1-put-runnable-raku-on-a-page). This page is the
whole playground: one editor, one output pane, a menu of example programs.

> **Not the same as [raku.online](https://raku.online/).** That site's
> playground grew its own extras (live re-run while typing, a stdin strip, the
> site shell) and is maintained in the
> [raku.online](https://github.com/ash/raku.online) repository. It runs the
> `rakujs.{js,wasm}` built here. The page in this directory is the reference
> one: five files, no site around it, drops anywhere.

## Run it

```sh
cd rakujs/playground
python3 -m http.server 8000
# open http://localhost:8000/
```

Any static server does, as long as it serves `.wasm` as `application/wasm`
(otherwise Emscripten falls back to a slower non-streaming compile). `file://`
will not work at all: the page runs programs in a Web Worker, which a `file://`
document cannot start.

Edit on the left, press **▶ Run** (or ⌘/Ctrl-Enter); stdout and stderr appear on
the right. State resets each run — a fresh `Interpreter` per call. Output
streams as it is produced, so ANSI redraw programs animate live; try `life`
(Conway's Game of Life). Pressing **Run** while a program is still running
**restarts** it with the current source, so edit-then-Run just works; **■ Stop**
(or Escape) kills a runaway program by terminating the worker.

## Deploy it

Five self-contained, same-directory files (~7 MB, dominated by the `.wasm`):

```
index.html   worker.js   rakujs.js   rakujs.wasm   examples.js
```

All links are relative, so any path works — `/playground/`, `/raku/`, the site
root. Build them and copy:

```sh
rakujs/build.sh
cp rakujs/playground/{index.html,worker.js,rakujs.js,rakujs.wasm,examples.js} \
   /path/to/your-site/playground/
```

Or take a built copy without installing Emscripten: every
[release](https://github.com/ash/rakupp/releases) attaches `rakujs-<tag>.zip`
with exactly those five files, built by that release's CI (see
[`.github/workflows/release.yml`](../.github/workflows/release.yml)).

```sh
gh release download -R ash/rakupp --pattern 'rakujs-v*.zip'
```

Serving requirements are the two above: `application/wasm`, and `http(s)`.

## Shareable links

The playground opens pre-filled from four kinds of URL — none of them store
anything on the server, so there is no code database to spam:

| URL form | Where the code lives |
|---|---|
| `#code=<data>` | in the URL itself (deflate-compressed, base64url) |
| `?gist=<id>[&file=<name>]` | a GitHub gist, fetched client-side |
| `?gh=<owner>/<repo>/<branch>/<path>` | a file in a GitHub repo (raw) |
| `?url=<encoded-url>` | any https URL whose host allows CORS fetches |

The **🔗 Share** button makes the `#code=` form from the current editor
content, copies it to the clipboard, and shows it in a popover (a
"Hello, world" link is ~80 characters; the fragment never reaches the server at
all). The **📂 Open…** button is the reverse: paste a GitHub file URL, a gist
URL, a raw URL — or a bare gist id / `owner/repo/branch/path` — and it loads the
code and rewrites the address bar to the matching `?gist=`/`?gh=` form, so the
address bar becomes the persistent link. Those GitHub forms are the ones to use
for a demo link that lasts: keep the program in a gist or a repo, and edits
there show up at the same URL. `?gist=` picks the first `.raku`/`.rakumod` file
unless `&file=` names one.

Append `&run=1` (`&run` works too in the hash form) to run the code as soon as
the interpreter loads:

```
https://raku.online/?gh=ash/rakupp/main/examples/anagrams.raku&run=1
```

GitHub fetches happen in the visitor's browser (both `api.github.com` and
`raw.githubusercontent.com` send CORS headers), so the server stays fully
static. Shared programs are capped at 200 KB. Compression uses the native
`CompressionStream` API — in every evergreen browser since 2023.

## The examples

The dropdown is generated from [`../examples/`](../examples) — the same programs
the CLI ships — by [`gen-examples.raku`](gen-examples.raku), run with the native
`rakupp` binary by `build.sh`. Raku.js generates its own playground data with
the interpreter it ships, and `examples/` stays the single source of truth. All
21 run in the browser and match native `rakupp` output exactly (verified),
except `life`, which seeds a **random** board and so differs run to run.

Three concurrency/IO examples are **omitted** — `parallel`, `sleep-sort`
(threads) and `echo-server` (sockets) need real threads or sockets, which the
single-threaded WASM build doesn't have, so running them would hang the page.
Run those with native `rakupp`. See
[INTERNALS.md](INTERNALS.md#known-limitations-single-threaded-browser-build).

To regenerate after adding an example, re-run `build.sh` (or just `rakupp
gen-examples.raku`). Deep-recursion examples are unaffected by the ~200
recursion cap; all shipped examples stay well under it.

The dropdown also carries **language showcases** — whole interpreters (Lisp,
Forth, JS/TS, Perl, Python) from [`../showcase/`](../showcase), each running a
sample program in the browser. A Python program interpreted by a Raku program
interpreted by a C++ interpreter compiled to WebAssembly, in a fraction of a
second: [**Interpreters all the way down**](STACKED-INTERPRETERS.md) explains
how that stack works and why it's fast.

## Files

Programs in the playground can read and write files. Nothing in the build asks
for a filesystem — it arrives with Emscripten: the interpreter calls
`fopen`/`stat`, so the toolchain links its default filesystem, and the default
backend is **MEMFS**, a tree that lives in the module instance's memory. So
`spurt`, `slurp`, `open`, `dir`, `mkdir` and `unlink` all behave as they do
natively, on this tree:

| Path | |
|---|---|
| `/` | the working directory — `$*CWD` is `/`, so relative paths land here |
| `/home/web_user` | Emscripten's default home directory — `$*HOME` points at it |
| `/tmp` | empty at start; `$*TMPDIR` points at it |
| `/dev`, `/proc` | Emscripten's usual stubs |

```raku
say '/'.IO.dir;                          # (/dev /home /proc /tmp)
'/tmp/note.txt'.IO.spurt: 'some text';
say '/tmp/note.txt'.IO.slurp;            # some text
```

Two things follow from where that tree lives.

**Nothing leaves the page.** MEMFS is a few JavaScript objects on the worker's
heap. The build declares no `IDBFS`, no `NODERAWFS` and no persistence of any
other kind, so a program cannot see the visitor's disk, and what it writes never
reaches IndexedDB, the network or the server that served the page.

**It lasts exactly as long as the module instance.** The page builds one
instance and reuses it, so a file written by one **Run** is still there for the
next **Run** in the same tab. It is gone whenever the instance is replaced: a
page reload, **Stop**, pressing **Run** during a run, `exit` in the program, a
deep-recursion `RangeError`, or the load-stuck retry. This is scratch space for
a program that wants to write a file and read it back — not storage.

## How the page is put together

| File | Purpose |
|---|---|
| `index.html` | The editor, the output console, the example menu, the share/open plumbing — one file, no framework. |
| `worker.js` | Runs the interpreter in a Web Worker, so the UI stays responsive: a spinner after 300 ms, output streamed live, and a Stop button that actually stops. |
| `rakujs.js` + `rakujs.wasm` | The engine ([README](README.md), [INTERNALS.md](INTERNALS.md)). |
| `examples.js` | The example programs, generated from `../examples/`. |

Why a worker at all, why rendering is timer-coalesced rather than
`requestAnimationFrame`, and what happens when a program blows the stack:
[INTERNALS.md](INTERNALS.md#how-it-works--design-notes).
