# Writing browser Raku programs with Raku.js

The [README](README.md) shows the *hook* —

```js
mod.ccall('rakupp_run', 'number', ['string'], ['say 42;']);
```

— but not how you'd write anything bigger than `say 42`. This page fills that
gap. It's short; by the end you'll be able to run real programs, feed them
input, and get results back into your page.

## The one idea

There is a single entry point:

```
rakupp_run(source)   →   runs the Raku source string, returns the exit code
```

**The whole program is that one string.** There's no stdin, no command-line
arguments, no files — the browser sandbox has none of those. Anything the
program `say`s or `print`s is handed back to your page through Emscripten's
`print` / `printErr` callbacks. So a Raku.js app is always this shape:

> build a Raku *source string* → run it → read what it printed.

Everything below is variations on that.

## The files you need, and where they go

Building Raku.js ([`build.sh`](build.sh)) produces exactly two files into
[`playground/`](playground/):

| File | What it is |
|------|------------|
| `rakujs.js`   | the Emscripten loader — a small JS glue file that defines the global `RakuJS` factory. This is the file you `<script src>`. |
| `rakujs.wasm` | the actual compiled interpreter (a few MB). You never reference it by name; `rakujs.js` loads it for you. |

The one rule: **`rakujs.wasm` must sit in the same directory as `rakujs.js`.**
When you load `rakujs.js`, it fetches `rakujs.wasm` *from its own directory* (a
relative URL derived from the script's location), so as long as the two are
side by side and served over `http(s)`, it just works — you don't write the
`.wasm` path anywhere:

```
your-site/
  app.html          ← your page:  <script src="lib/rakujs.js"></script>
  lib/
    rakujs.js        ← the loader
    rakujs.wasm      ← must be next to rakujs.js  (auto-found)
```

`app.html` can be anywhere; only the two library files must stay together.

**If you must split them** (e.g. serve the `.wasm` from a CDN), tell the loader
where to look with `locateFile` — it's called with each filename and returns a
URL:

```js
RakuJS({
  locateFile: name => 'https://cdn.example.com/wasm/' + name,   // rakujs.wasm → there
  print: line => …,
});
```

**Using a Web Worker (§6)?** Add a third file, `worker.js`, and it too must be
able to reach `rakujs.js`. A worker loads the loader with `importScripts`, and
because a worker's relative paths resolve against the worker script's own URL,
point both `importScripts` and `locateFile` at wherever the two library files
live:

```js
// worker.js — sitting next to app.html, library one folder down in lib/
const BASE = 'lib/';
importScripts(BASE + 'rakujs.js');          // load the factory
RakuJS({ locateFile: name => BASE + name,   // and let it find rakujs.wasm
         print: … });
```

That's the whole placement story: **two library files together, served over
http; your page and worker just need a path that reaches them.** The
[`showcase/web/`](../showcase/web/) apps do exactly this — see
[`worker.js`](../showcase/web/worker.js), whose `BASE` points at
`playground/` where `build.sh` left the pair.

## 1. Run a program

The build is `MODULARIZE`d under a global factory `RakuJS`. Instantiate it once,
wiring up where output should go, then call `rakupp_run` as many times as you
like:

```html
<script src="rakujs.js"></script>
<pre id="out"></pre>
<script>
  const out = document.getElementById('out');
  RakuJS({
    print:    line => out.textContent += line + "\n",   // stdout
    printErr: line => out.textContent += line + "\n",   // stderr (errors)
  }).then(mod => {
    mod.ccall('rakupp_run', 'number', ['string'], ['say 42;']);
  });
</script>
```

`print`/`printErr` fire **once per line**, as the program produces output.

## 2. Bigger programs are just longer strings

`say 42;` isn't special — the string can be a whole program. In JavaScript, a
backtick template literal is the natural way to hold multi-line Raku:

```js
const program = `
sub fib($n) { $n < 2 ?? $n !! fib($n-1) + fib($n-2) }
say "fib(10) = " ~ fib(10);

for 1..5 -> $i {
    say "$i! = " ~ [*] 1..$i;
}
`;
mod.ccall('rakupp_run', 'number', ['string'], [program]);
```

Subs, loops, `grammar`s, classes — anything the CLI runs, this runs, because
it's the same interpreter. Two things to keep in mind:

- **End your output with a newline — use `say`, not `print`.** WebAssembly's
  stdout is *line-buffered*: a final line printed without a trailing `\n` sits in
  the buffer until the next run, so it looks lost (or shows up glued to the next
  run's output). `say` adds the newline; `print "…\n"` works too.
- **Recursion is capped at a few hundred levels** in the browser (a WebAssembly
  stack limit, not a Raku one — see the README). `fib(10)` is fine; a deeply
  recursive `fib(35)` will hit it. Loops are unaffected.

## 3. Giving the program input

This is the part that isn't obvious. Since there's no stdin or file to read, the
input has to become **part of the source string**. Two techniques:

### A number or short value — build a literal

```js
const n = 20;                                   // from an <input>, say
const program = `
my $n = ${Number(n)};                           // interpolate a *validated* number
say "$n! = " ~ [*] 1..$n;
`;
```

Only interpolate values you've validated (here, `Number(n)` guarantees it's a
number). Don't drop raw user text straight into code.

### Arbitrary text — use a heredoc

For multi-line or untrusted text, wrap it in a Raku **non-interpolating
heredoc** so nothing inside it is treated as code:

```js
// Emit:  my $VAR = q:to/END/;\n<text>\nEND
function rakuString(varName, text, term = "__INPUT__") {
  let body = String(text);
  // make sure the text can't contain the terminator on its own line
  if (body.split("\n").some(l => l.trim() === term)) body = body.replaceAll(term, term + "_");
  return `my ${varName} = q:to/${term}/;\n${body}\n${term}\n`;
}

const userText = document.getElementById('box').value;
const program =
  rakuString('$text', userText) +
  `for $text.words.Bag.sort(-*.value) -> $p { say $p.key ~ "\\t" ~ $p.value }`;
```

`q:to/END/` is a heredoc: the `;` goes on the opening line, the terminator sits
alone on its own line, and nothing between them is interpolated — so quotes,
`$sigils`, and backslashes in the user's text are safe. (Add `.chomp` in your
Raku if you don't want the heredoc's trailing newline: `$text.chomp`.)

## 4. Getting results back into the page

Your program prints; your `print` callback collects. From there you decide the
format:

- **Plain text** — just show it (a REPL, a formatter).
- **Tab/line-separated** — `say "$key\t$value"` per row, then `split("\n")` and
  `split("\t")` in JS to build a table or highlight matches.
- **JSON** — build a JSON string in Raku and `JSON.parse` it in JS.

A tiny wrapper that runs a program and hands back everything it printed:

```js
function run(mod, source) {
  let out = "", err = "";
  // (set these on the module once; shown inline here for clarity)
  mod.printOverride    = t => out += t + "\n";
  mod.printErrOverride = t => err += t + "\n";
  const rc = mod.ccall('rakupp_run', 'number', ['string'], [source]);
  return { out, err, rc };
}
```

(In practice you set `print`/`printErr` when you create the module, and reset a
buffer before each run — see [`showcase/web/runner.js`](../showcase/web/runner.js).)

## 5. A complete example

A self-contained page: type text, get a live word-frequency count computed by
Raku. It shows all three ideas — build a source string, embed the input with a
heredoc, read the output back.

```html
<!doctype html><meta charset="utf-8">
<textarea id="box" rows="4" cols="50">the quick brown fox the lazy dog the end</textarea>
<pre id="out"></pre>
<script src="rakujs.js"></script>
<script>
const box = document.getElementById('box');
const out = document.getElementById('out');
let buf = "";
const ready = RakuJS({ print: t => buf += t + "\n", printErr: t => buf += t + "\n" });

function rakuString(v, text, term = "__INPUT__") {
  let b = String(text);
  if (b.split("\n").some(l => l.trim() === term)) b = b.replaceAll(term, term + "_");
  return `my ${v} = q:to/${term}/;\n${b}\n${term}\n`;
}

ready.then(mod => {
  const render = () => {
    buf = "";
    const program = rakuString('$text', box.value) +
      `for $text.words.Bag.sort({ -.value, .key }) -> $p { say $p.value.fmt('%3d') ~ "  " ~ $p.key }`;
    mod.ccall('rakupp_run', 'number', ['string'], [program]);
    out.textContent = buf;
  };
  box.addEventListener('input', render);
  render();
});
</script>
```

Type in the box and the counts update — a Raku `Bag`, sorted, running in your
browser.

## 6. Keep the UI responsive (a worker)

`rakupp_run` is **synchronous** — it runs the whole program before returning, so
on the main thread a slow program freezes the page. For anything non-trivial,
run the module in a **Web Worker** and talk to it with messages. You don't have
to write this from scratch:

- [`playground/worker.js`](playground/worker.js) — a ready-made worker that loads
  the module and runs `rakupp_run` off the main thread, streaming output back.
- [`showcase/web/runner.js`](../showcase/web/runner.js) — a small `Raku` client
  around that worker: `raku.run(src)` returns a `Promise<{out, err, rc}>`, plus a
  `debounced()` helper for live editing and the `rakuHeredoc()` from §3.

The [`showcase/web/`](../showcase/web/) apps (live Markdown, JSON formatter,
regex/grammar explorer) are complete, working examples built on exactly these
pieces — a good next read.

## 7. Reusing an existing `.raku` program

You don't have to inline everything. A common pattern: keep the real logic in a
normal `.raku` file, `fetch()` it at runtime, strip its `sub MAIN`, and append a
small driver that calls its functions with your input:

```js
const lib = (await (await fetch('mylib.raku')).text())
              .replace(/\n\s*sub MAIN\b[\s\S]*$/, '\n');   // drop the CLI entry point
const program = lib + rakuString('$in', userInput) + 'say process($in);';
```

That's how the `showcase/web/` apps reuse the CLI showcases unchanged — the same
Raku code runs in the terminal and in the browser.

## Gotchas, in one place

| Symptom | Cause | Fix |
|---|---|---|
| Last line missing / glued to next run | WASM stdout is line-buffered | end output with `say` (or `print "…\n"`) |
| `RangeError` / "recursion limit" | ~200-level browser stack cap | rewrite iteratively; run deep recursion natively |
| Program hangs the tab | `start`/threads or sockets | not available in the sandbox — use native `rakupp` |
| `sub MAIN` never runs | no argv in the browser | call your subs directly, or strip `MAIN` and drive it |
| Second run behaves oddly | leftover state / partial output | reuse `say` (see row 1); or recreate the worker per run |

That's the whole model: **a program is a string, input goes into the string,
output comes back as text.** Everything else is ordinary Raku.
