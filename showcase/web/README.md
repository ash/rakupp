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

```sh
showcase/web/serve.sh          # port 8000
# open http://127.0.0.1:8000/
```

That's it. [`serve.sh`](serve.sh) gathers everything the apps load into `lib/` —
the Raku.js WebAssembly build (`rakujs.js` + `rakujs.wasm`, building it with
[`rakujs/build.sh`](../../rakujs/build.sh) first if needed) and the two CLI
showcase sources they reuse — then serves **this folder** with the
[`rakus`](../rakus/) showcase (rakupp serving its own WebAssembly build). URLs
stay short: `/` is the launcher, `/regex.html` an app. Pass a port as the first
argument (`serve.sh 9000`).

`lib/` is git-ignored; re-run `serve.sh` to refresh it after rebuilding Raku.js.

<details>
<summary>Serving by hand</summary>

Any static server works, and a `file://` open won't (Web Workers and
WebAssembly need `http(s)`). To serve without `serve.sh`, gather the assets into
`lib/` yourself, then point any server at this folder:

```sh
mkdir -p showcase/web/lib
cp rakujs/playground/rakujs.js rakujs/playground/rakujs.wasm showcase/web/lib/
cp showcase/markdown/md2html.raku showcase/json/json.raku      showcase/web/lib/
python3 -m http.server 8000 -d showcase/web    # or any static server
```

The apps load `lib/rakujs.{js,wasm}` and their `lib/*.raku` sources by relative
path, so the served root must be this folder (which contains `lib/`), not the
repo root.
</details>

## How it works

- **`worker.js`** loads `lib/rakujs.js`/`lib/rakujs.wasm` off the main thread and
  exposes one call: `rakupp_run(source)` runs a Raku program to completion,
  streaming whatever it prints back to the page.
- **`runner.js`** wraps that worker in a small `Raku` client (`raku.run(src) →
  Promise<{out, err, rc}>`), plus a `debounced()` helper for live editing and a
  `rakuHeredoc()` that embeds arbitrary user text as a non-interpolating Raku
  heredoc.
- **Each app** fetches the relevant showcase (e.g. `lib/md2html.raku`), strips its
  file-reading `MAIN`, appends a driver that binds your input and calls the
  showcase's function (`render`, `to-json`, …), runs it, and renders the result —
  on every keystroke, debounced. (`regex.html` builds its program inline, so it
  fetches nothing.)

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
