# Runnable play buttons on the Raku course (embedding + theming)

How [course.raku.org](https://course.raku.org/) turns the solution code on its
**Part 6 (Addendum)** pages into runnable, editable Raku.js editors — and how the
widget's accent is tinted to the course's blue. This is a cross-repo
integration note: it touches three repos and the Raku.js embed widget.

## The three repos

| Repo | Role |
|---|---|
| **rakupp** (this repo) | The interpreter. `rakujs/build.sh` compiles it to WebAssembly (`rakujs.js` + `rakujs.wasm`) — the engine every embed runs. |
| **raku.online** | Hosts the engine and the embed widget `raku.js` at `https://raku.online/`. One shared instance; `course.raku.org/playground` now 301-redirects here. |
| **raku-course** | The consumer. Its page generator injects the widget on Part 6 solution pages. |

Nothing runs on a server: `raku.js` loads `rakujs.{js,wasm}` (relative to its own
`src`), builds a Blob Web Worker, and executes each program on the visitor's
machine. Cross-origin loads work because raku.online sends
`Access-Control-Allow-Origin: *` on `.js`/`.wasm`.

## What the course does (raku-course)

Two small changes, both **only** affecting Part 6 solution pages:

1. **Generator injection** — `raku-pages.raku`, in `render-page`, appends one
   script tag before `</body>` when the page path matches `addendum/**/solution`:

   ```raku
   if $dir ~~ /^ 'addendum/' .* '/solution' $/ {
       my $embed = '<script src="https://raku.online/raku.js"'
                 ~ ' data-selector=".highlight.raku" defer></script>';
       $html .= subst('</body>', "$embed\n</body>");
   }
   ```

   `data-selector=".highlight.raku"` points the widget at the course's
   Raku code blocks. The course wraps highlighted Raku as
   `<div class="highlight raku"><pre>…spans…</pre></div>`; console/output blocks
   are `.highlight` only, so they are left alone. `enhance()` reads
   `el.textContent` (spans stripped → clean source), so it consumes the
   already-highlighted block directly — no `<code class="language-raku">` needed.

2. **Accent colour** — `assets/course.css`, on `:root`:

   ```css
   --rk-embed-accent: #0969da;   /* the course's own blue */
   ```

Scope check after a build: 50 `addendum/**/solution` pages carry the script; 0
exercise pages and 0 Part-5 pages do.

## The theming hook (raku.js, in the raku.online repo)

Each editor lives in its **own Shadow DOM** (`:host{all:initial}`) so the host
page's CSS can't reach in and the widget's can't leak out — it looks identical
on any site. That isolation means the course cannot restyle it… except through
**CSS custom properties**, which `all: initial` does *not* reset and which
inherit across the shadow boundary.

So `raku.js` was changed to read its accent from an inheritable variable, with
the raku.online magenta as the default:

```js
// before:  --accent:#d33682;
// after:
'  --accent:var(--rk-embed-accent,#d33682);',
```

Any embedding page can now set `--rk-embed-accent` on an ancestor and the Run
button (and other accent uses) pick it up; pages that don't stay magenta. The
course sets it to `#0969da` — verified live: inside the widget's shadow root,
`--accent` computes to `#0969da` and the Run button background is
`rgb(9, 105, 218)`.

## Deploy chain (important ordering)

`raku.js` is **owned by the raku.online repo** — do not fork it into the course.
A change to the accent (or anything else in the widget) must be:

1. committed in **raku.online**, then
2. deployed with `raku.online/deploy.sh` (copies `www/` to the server; stamps a
   `?v=<md5>` cache tag over the engine files into `index.html` + `raku.js`).

Until the edited `raku.js` reaches the live server, the course — which loads
`https://raku.online/raku.js` — keeps showing the old (magenta) widget, **even
in a local build**, because the script is fetched from raku.online, not from the
course. `deploy.sh` re-stamps the cache tag, so the standard order is
**deploy → commit** (the script's own comment: "commit it so the repo mirrors
the live site"); when the tag is unchanged the two orders coincide.

Reference commit in raku.online: `Refresh interpreter; make embed accent
themable` (bundled the WASM refresh with the `--rk-embed-accent` hook).

## Caveats

- **No filesystem, network, argv, or stdin-from-a-terminal** in the WASM build
  (see `rakujs/TUTORIAL.md`). Addendum solutions are algorithmic, so almost all
  run as-is, but any that read/write files or reach the network will show a
  runtime error instead of output rather than silently doing nothing.
- **`defer` on the script is fine.** `raku.js` resolves its base URL from
  `document.currentScript || <fallback>`; the fallback handles the deferred case
  (verified: the widget loads and runs under `defer`).
- Only the English addendum is regenerated when you edit the generator; a full
  `raku raku-pages.raku` before publishing applies the injection across all
  languages and refreshes the search index.

## Verifying locally

Because the course loads the widget from live raku.online, a local build shows
whatever raku.online currently serves. To test an *unreleased* widget change
(e.g. the accent) without deploying, serve the edited `raku.online/www` and the
course `_out` **from the same origin** (a `/rkembed` route → `raku.online/www`),
and point one page's script at `/rkembed/raku.js`. Same origin ⇒ no CORS ⇒ the
Blob worker's `importScripts` of the engine just works. Confirmed end-to-end
this way and again against live raku.online:

```
say @totals;   # my @numbers = 2,4,6,8; my @totals = @numbers.map({ $sum += $_ })
→ [2 6 12 20]   — exit 0 · ~100 ms   (blue Run button)
```

## See also

- `rakujs/README.md` — the low-level `RakuJS()` API and how the WASM build works.
- `rakujs/TUTORIAL.md` — writing real browser Raku programs (input/output, no fs).
- raku.online `README.md` — the `raku.js` widget: `data-raku`, `data-auto`,
  `data-selector`, share links, and the embed builder.
