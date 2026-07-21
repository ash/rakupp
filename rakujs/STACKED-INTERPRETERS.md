# Interpreters all the way down

How the Raku.js language showcases work — and why a three-layer interpreter
stack still runs in a fraction of a second in a browser tab.

## The setup

The playground ships several *language showcases*. Each one is a complete
interpreter for **another** language, written in Raku, running in your browser:

| Showcase | Interpreter (Raku source) | Interprets |
|---|---|---|
| Lisp        | `lisp.raku`   — 432 lines / 15 KB   | a Scheme subset |
| Forth       | `forth.raku`  — 223 lines / 8 KB    | Forth |
| JavaScript / TypeScript | `js.raku` — 2,167 lines / 90 KB | JS / TS |
| Perl        | `perl.raku`   — 1,630 lines / 65 KB | Perl 5 |
| Python      | `python.raku` — 1,472 lines / 63 KB | Python 3 |

Open the Python interpreter with the "Classes" sample —
[**raku.online/?ex=python-interpreter&in=classes&run**](https://raku.online/?ex=python-interpreter&in=classes&run)
— and this is the stack, top to bottom:

```
  classes.py       ← your Python program (in the Stdin box)
    ▲  parsed & executed by
  python.raku      ← a 1,472-line Python 3 interpreter, written in Raku
    ▲  parsed & executed by
  Raku++           ← a Raku interpreter, written in C++
    ▲  compiled to
  WebAssembly      ← running in your browser tab, no server
```

Three interpreters, stacked.

## What happens on each Run

**Compiled once, ahead of time** (not per run):

- Raku++ → WebAssembly, via Emscripten. The ~4.6 MB module is instantiated
  once, when the page loads.

**Redone from scratch on every single Run** (nothing is cached between runs):

1. Raku++ (itself running as WebAssembly) lexes and parses the ~63 KB /
   1,472-line `python.raku` into an AST.
2. It tree-walks that AST — building the Python grammar, the class model, the
   environment, and installing every builtin, in memory, from nothing.
3. That freshly-built Python interpreter preprocesses your Python (the off-side
   rule becomes INDENT/DEDENT tokens), parses it with a *Raku grammar*, and
   tree-walks the resulting Python AST.

## The timings

Measured warm — after the first run has warmed up the browser's JIT — on
raku.online:

| Program | Time | What it measures |
|---|---|---|
| `print(1)` | ~110 ms | essentially just "parse `python.raku` and build the whole Python runtime" |
| `classes.py` | ~330 ms | the above, plus closures, `map`/`filter`, generator expressions |

So the fixed cost of parsing a 1,472-line Raku program **and** constructing a
complete Python runtime from it is on the order of ~100 ms; the sample's own
work adds a couple hundred more.

Two caveats on the numbers:

- **Cold vs warm.** The first run of a session is ~2–3× slower: it includes the
  one-time WebAssembly instantiation and a cold JIT. Repeated clicks are warm.
- **Which browser.** These figures come from an automated test browser that
  runs WebAssembly on a slower compiler tier; a normal desktop browser is
  faster. Treat them as an upper bound — the number the playground prints on
  *your* machine is the accurate one.

## Why it's (pleasantly) fast

Nothing in the stack is clever, and that is rather the point:

- Raku++ is a plain tree-walking interpreter, but C++ compiled to WebAssembly
  runs close to native once the browser tiers it up to its optimizing compiler.
- Parsing 60–90 KB of source and building an interpreter from it is simply not
  much work in absolute terms — tens of milliseconds of parsing, tens more of
  setup.
- Stacking a second interpreter on top multiplies the *per-operation* cost, but
  the sample programs are small, so the total stays well under a second.

## The limits of the stack

Two constraints fall directly out of the layering:

- **Recursion depth.** Each level of recursion in the guest language costs many
  C++ stack frames inside Raku++, and a browser caps the WebAssembly call stack
  at a few hundred frames. So a naive recursive `fib` in the guest language can
  overflow even at shallow depth — which is why the recursion-heavy samples
  (e.g. Python's `fib.py`) are left out, while loop-based programs run fine.
- **No threads or sockets.** The single-threaded WASM build has neither, so an
  interpreter can't offer guest programs that would need them.

## Try it

Each link opens the interpreter in the editor with a sample program in the
Stdin box; edit either and press Run.

- Python — https://raku.online/?ex=python-interpreter&in=classes&run
- Perl — https://raku.online/?ex=perl-interpreter&in=sieve&run
- JavaScript / TypeScript — https://raku.online/?ex=javascript-typescript&run
- Forth — https://raku.online/?ex=forth-interpreter&run
- Lisp — https://raku.online/?ex=lisp-interpreter&run

The interpreter itself is the Raku source in the editor. It is the same code
that runs from the command line with native `rakupp` — see
[`../showcase/`](../showcase/).
