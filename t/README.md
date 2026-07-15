# t/ — the example + showcase regression suite

One command runs every program in [`examples/`](../examples/) and every
[`showcase/`](../showcase/), with the **same `rakupp` binary that runs the
suite**, and checks their output:

```sh
build/rakupp t/run.raku
```

It prints TAP (`ok 1 …` through `ok 35`), ends with `# all N checks passed`, and
exits non-zero if anything drifts — so it drops straight into CI next to
`tools/smoke.raku`. Nothing to set up: the pastebin and chat servers are started
and stopped by the suite itself.

## Layout

| Path | What it is |
|---|---|
| `run.raku` | the whole suite — helpers + every check, in one self-contained file |
| `expected/` | golden output, one file per deterministic program |
| `expected/lisp/` | golden output for the Lisp example programs |
| `fixtures/lisp-features.scm` | a Scheme program that exercises the interpreter's breadth in one run |
| `fixtures/chat-client.raku` | a two-client checker the suite runs as its own process against the chat server |

It is deliberately one file rather than many `.t` files: rakupp's module
`is export` is still unreliable for many-sub helper modules, so the helpers live
inline in `run.raku`.

## What each part checks

- **examples/** — every deterministic example is compared **byte-for-byte**
  against `expected/<name>.out`. `life.raku` seeds from random, so it is only
  smoke-checked (runs, exits 0, draws something).
- **lisp** — the `fact.scm` and `closures.scm` examples against their goldens, a
  feature fixture (lists, higher-order fns, quote/quasiquote, `cond`/`let`, exact
  bignums), and a spot check that `(fact 100)` is exact to all 158 digits.
- **markdown** — `sample.md` against a golden page, plus an assertion that every
  block and inline construct is present in the output.
- **pastebin** — the server is launched on a fixed port; the suite makes real
  HTTP requests (GET the form, POST a paste, view it, fetch it raw).
- **chat** — the server is launched and a separate client process connects two
  users to check nick, join notice, broadcast, and `/who`.

## Updating a golden

When a program's output changes **on purpose**, regenerate its golden and commit
the new file alongside the code change:

```sh
build/rakupp examples/NAME.raku            > t/expected/NAME.out
build/rakupp showcase/lisp/lisp.raku FILE  > t/expected/lisp/NAME.out
build/rakupp showcase/markdown/md2html.raku showcase/markdown/sample.md > t/expected/markdown-sample.out
```

## Notes on the server tests

The servers are long-lived accept loops. rakupp's `Proc::Async` deadlocks when
the same process then does blocking socket I/O, so the suite backgrounds each
server through the shell, polls until it accepts, drives it over real sockets,
then `pkill`s it by script name. They run on fixed, uncommon ports (pastebin
8391, chat 6691) to avoid colliding with a server you might have running.
