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

They need the Raku.js WebAssembly build (`rakujs.js` + `rakujs.wasm`), which
[`rakujs/build.sh`](../../rakujs/build.sh) produces into
[`rakujs/playground/`](../../rakujs/playground/); these apps load it from there.
Then serve the **repo root** over HTTP and open the apps:

```sh
python3 -m http.server 8000        # from the repository root
# open http://localhost:8000/showcase/web/
```

(A plain `file://` open won't work — Web Workers and WebAssembly need `http(s)`.)

## How it works

- **`worker.js`** loads `rakujs.js`/`rakujs.wasm` off the main thread and exposes
  one call: `rakupp_run(source)` runs a Raku program to completion, streaming
  whatever it prints back to the page.
- **`runner.js`** wraps that worker in a small `Raku` client (`raku.run(src) →
  Promise<{out, err, rc}>`), plus a `debounced()` helper for live editing and a
  `rakuHeredoc()` that embeds arbitrary user text as a non-interpolating Raku
  heredoc.
- **Each app** fetches the relevant showcase (e.g. `../markdown/md2html.raku`),
  strips its file-reading `MAIN`, appends a driver that binds your input and calls
  the showcase's function (`render`, `to-json`, …), runs it, and renders the
  result — on every keystroke, debounced.

So the app is thin; the actual Markdown/JSON/grammar work is the very same Raku
code the CLI showcases run.

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
