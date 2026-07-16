# web — the showcases in the browser

Three little browser apps, each powered by **rakupp compiled to WebAssembly** —
the same interpreter as the CLI, running entirely client-side. No server and no
JavaScript re-implementation: each app builds a Raku program (a showcase's own
code plus your input) and shows what it prints.

| App | What it does |
|---|---|
| [`markdown.html`](markdown.html) | live Markdown editor — type on the left, see it rendered on the right |
| [`json.html`](json.html) | JSON beautifier / minifier |
| [`regex.html`](regex.html) | Raku regex tester with live match highlighting + a grammar parse-tree explorer |

## Run them

No server. Generate the embedded assets once, then just open the HTML files:

```sh
showcase/web/bundle.sh
# then open showcase/web/index.html  (double-click, or `open showcase/web/index.html`)
```

[`bundle.sh`](bundle.sh) builds Raku.js if needed
([`rakujs/build.sh`](../../rakujs/build.sh)), then writes `lib/`: the Emscripten
loader, the WebAssembly binary **as base64** (`lib/rakujs-wasm.js`), and the two
CLI showcase sources the JSON/Markdown apps reuse (`lib/sources.js`). The pages
load those with plain `<script src>` and run the interpreter on the page — so
they work opened straight from disk, `file://`, no `http` server at all.

`lib/` is git-ignored; re-run `bundle.sh` to refresh it after rebuilding Raku.js.

**Don't want to build anything?** Each tagged [release](https://github.com/ash/rakupp/releases)
attaches `rakujs-showcase-web-<tag>.zip` — these apps with the WebAssembly already
embedded. Download, unzip, open `index.html`. No toolchain, no server.

## How it works

- **`runner.js`** is a tiny main-thread `Raku` client: `raku.run(src)` returns a
  `Promise<{out, err, rc, ms}>`, plus a `debounced()` helper for live editing and
  a `rakuHeredoc()` that embeds arbitrary user text as a non-interpolating Raku
  heredoc. It instantiates the interpreter by handing Emscripten the embedded
  wasm as a `data:` URL (`locateFile`), so nothing is fetched over the network.
- **Each app** reuses the relevant showcase from `lib/sources.js` (e.g. the
  `md2html` source), strips its file-reading `MAIN`, appends a driver that binds
  your input and calls the showcase's function (`render`, `to-json`, …), runs it,
  and renders the result — on every keystroke, debounced. (`regex.html` builds
  its program inline, so it needs no source.)

So the app is thin; the actual Markdown/JSON/grammar work is the very same Raku
code the CLI showcases run.

Running on the main thread (rather than a Web Worker like the
[playground](../../rakujs/playground/)) keeps this simple and `file://`-openable;
the trade-off is no Stop button and a brief freeze on heavy input — fine for
these keystroke-fast apps.

## Notes / gotchas

- **Output must end in a newline.** WebAssembly stdout is line-buffered — a final
  line printed with `print` (no `\n`) is held until the next run. The apps use
  `say` so every result flushes immediately.
- **No sockets, no filesystem.** The sandbox can't run the server showcases
  (pastebin, rakus, chat, kvstore) — those need real TCP.
- **Recursion is capped** at a few hundred levels (a WebAssembly stack limit, not
  a Raku one); deeply recursive programs that run fine natively can hit it here.
- These are browser apps, so they aren't part of the `t/run.raku` suite; the Raku
  code they exercise is covered there via the CLI showcases.
