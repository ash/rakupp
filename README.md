# Raku++

A from-scratch implementation of the [Raku](https://raku.org) programming
language in **C++17, with no third-party dependencies** — a
[hand-written](docs/guide/faq/hand-written.md) lexer, parser, and tree-walking
evaluator that runs real Raku (classes, roles, grammars, regexes,
multi-dispatch, junctions, lazy sequences, a bignum tower, Unicode-correct
strings, and concurrency), can also **compile** a program to a standalone
native binary, and — as **[Raku.js](rakujs)** — **runs in the browser** via
WebAssembly, no server required. It is not a fork of Rakudo and shares no code
with it; it targets the *language*, measured against
[**Roast**](https://github.com/Raku/roast), the official Raku test suite.

**Status:** current release **v3.24.0** (2026-09-01) — *what other people's
code asked for*: the first release after the consolidation arc, and almost none
of it was chosen here. Eight GitHub issues, six third-party distributions that
would not install, and one outside report that two dists ran ~40% slower than
Rakudo — which is what drove the dispatch work. Method calls and attribute reads
stop allocating; the ecosystem was measured again rather than carried forward.
The release gates caught three regressions it had introduced, none of them
visible in the headline count. Every release is written up in the
[CHANGELOG](CHANGELOG.md).

**Current focus:** the ecosystem sweep — all 2,526 distributions of the Raku
ecosystem run against rakupp, and the engine gets fixed until real modules
install and pass their own test suites. As of the 2026-08-30 re-sweep
**746 of 2,526 pass**, with another 383 blocked by a failing
dependency before their own tests could run; what the sweep finds drives what
gets built next ([the findings](docs/dev/findings/ECOSWEEP-2026-08.md), with
the green list and per-dist results — and **every distribution with how it
ran is browsable at
[raku.online/modules/ecosystem](https://raku.online/modules/ecosystem/)**).
(The comparison table below quotes the sweep itself; a small 59-dist battery
remains the per-release QA gate — see
[RELEASING.md](docs/dev/RELEASING.md) — an instrument, not the ecosystem
picture.)

**Three consolidation releases, now done.** Language work has the property Larry
Wall kept pointing at: push the design in one place and something pops out in
another. After a lot of correct individual changes, that is where this engine
was — so these three added nothing new.

- **v3.21.0** — *the state after the changes.* ✅ What had accumulated since
  v3.20.1, measured together in one sitting. It passed all seven gates and
  shipped a silent wrong answer anyway, which set the next release's agenda.
- **v3.22.0** — *the instruments, fixed and then proved.* ✅ Six of the seven
  gates had a defect of their own. All fixed, and **every gate that can fail
  detects a planted defect** — `rakupp tools/prove-gates.raku --all`.
- **v3.23.0** — *the re-baseline.* ✅ Every gated figure from one run, baselines
  re-recorded, each naming what produced it — a gate whose baseline predates the
  review is not a gate. It also corrected a claim: conformance has **no red path
  at all**, so it is a report, not a gate.
- **v3.24.0** — *what other people's code asked for.* ✅ Method calls and
  attribute reads stop allocating — five OO kernels went into the perf gate
  first, because it covered sub calls at ~2x Rakudo and could not see method
  calls at 5.8x at all. Ecosystem re-measured to **746 / 2,526**. Gate 1 and the
  module battery caught three regressions this release introduced, while the
  headline count moved inside its own flap band.

Left open by the arc: the source review is **three files of eighty-three**. The
performance baseline had moved twice with no cause found; v3.24.0 rebuilt
v3.23.0's own source on the same machine and every kernel landed within 1-3% of
its recorded number, so the drift did not recur and this release's figures are a
comparison of code.

Then the standing target: **1000 of 2,526** distributions passing their own test
suites, up from 746, where the lever is the 383 that never ran their own tests at
all because a dependency failed first. The plans are in
[docs/dev/plans/VERSIONS.md](docs/dev/plans/VERSIONS.md).

| | v3.24.0 | at v2.0.0 |
|---|---:|---:|
| Roast, per individual test — of what the suite declares‡ | **199,846 of ~219,374 (90%)** | 197,090 of ~203,500 (97%) |
| Roast, all-or-nothing — files fully passing, of 1,464 | **646 (44%)** | 594 |
| Official documentation examples byte-identical on both engines | **953** | 952 |
| Of the Raku ecosystem's [2,526 distributions](https://raku.online/modules/ecosystem/), passing their own test suites | **746** | — |
| Local regression suite | **609** | 312 |
| `say "Hello"` compiled with `--exe --slim` | **6,286,248 B** | 9,830,680 B (no `--slim`) |

‡ Counted against each file's declared `plan N`, so a file that aborts is
charged for every test it failed to run; on the all-or-nothing bar a file
counts only if *every* assertion in it passes. Both are measured with
parallelism and true LTM on — the same binary configuration users get. How the
runs are profiled and gated: [COUNTING.md](docs/status/COUNTING.md).

## Install

```sh
brew tap ash/rakupp && brew install rakupp    # macOS (Apple Silicon: prebuilt binary)
```

Or unpack a **prebuilt archive** — macOS universal, Linux x86_64 (static
libstdc++), Windows x64 — from the
[Releases page](https://github.com/ash/rakupp/releases/latest) and put its
`bin/` on your `PATH`.

### Build from source

```sh
# Needs a C++17 compiler + CMake → produces build/rakupp
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

`cmake --install build --prefix ~/.local` then installs the binary plus the
runtime that `--exe` links against. Windows (MSVC) specifics, the **GNU Guix**
channel, and the **Nix flake** are in
**[INSTALL.md](docs/guide/INSTALL.md)**.

## Quick start

**Write it. Run it. Compile it.**

```sh
rakupp -e 'say "hello, world"'   # a one-liner  (or: echo 'say 42' | rakupp)
rakupp app.raku                  # run a file — no build step
rakupp --exe app.raku -o app     # compile it
./app                            # one file, and it needs nothing you have
```

## Common options

| Option | Meaning |
|---|---|
| `FILE` / `-e 'CODE'` / `-` *(stdin)* | Run a program from a file, a one-liner, or standard input (`rakupp - ARGS…` gives a stdin program its `@*ARGS`) |
| `-I <path>` / `-M <module>` | Add a module search directory / load a module first (both repeatable) |
| `-n` / `-p` / `-a` / `-F<sep>` / `-i[.ext]` | The Perl one-liner family: line loop, autoprint, autosplit, in-place edit (clusters: `-lane`, `-pi.bak`) |
| `--profile[=FILE]` | Routine-level wall-time profile after the run (`.json` for machine-readable) |
| `--exe SRC -o OUT` | Native-compile to a standalone binary (also `--bundle`, `--aot`) |
| `--highlight [SRC]` | Syntax-highlight Raku to HTML (`--html`) or terminal (`--ansi`) |
| `--mcp` | Serve the interpreter over the Model Context Protocol for AI agent clients |
| `--jupyter FILE` | Run as a Jupyter kernel (`--jupyter-install` registers it with Jupyter) |
| `--lint SRC` | Static-analyze without running: unused variables, unreachable code, etc. |
| `-c` / `--ast SRC` | Compile-check only (parse + every variable declared) / print the parsed AST |

Flags are position-independent and cluster like Perl's (`rakupp -pi.bak -e
'$_ = $_.subst("a", "b")' *.txt` works as you'd hope). Full reference:
[CLI.md](docs/guide/CLI.md).

## Modules

Raku++ installs modules from the ecosystem with **its own installer** —
compatible with [zef](https://github.com/ugexe/zef), so a module installed by
either tool is picked up by `use` under either engine:

```sh
rakupp install JSON::Fast        # or: zef install JSON::Fast   (via Rakudo)
```

```raku
use JSON::Fast;                             # works after either install
say to-json({ name => 'Ada' }, :!pretty);   # {"name":"Ada"}
```

It also loads your own module files from `lib/` (and `-I` / `RAKULIB` / `use lib`
paths), and a `use` that cannot be found or fails to compile is **fatal**.
How much of the ecosystem runs today: all 2,526 distributions, each with its
sweep verdict, are listed at
[raku.online/modules/ecosystem](https://raku.online/modules/ecosystem/).
Full guide: **[MODULES.md](docs/guide/MODULES.md)**.

## Code to read and run

Three directories of runnable programs — as much for exploring Raku as for
exploring Raku++:

- **[examples/](examples)** — complete example programs: Mandelbrot, Game of
  Life, a JSON parser on a Raku grammar, a quine, …
- **[showcase/](showcase)** — mid-size programs: a Scheme interpreter built on
  a Raku grammar, and a pastebin HTTP server on raw sockets.
- **[live/](live)** — real software from the ecosystem, run unmodified: whole
  tools people already use, driven by Raku++ exactly as their authors wrote
  them. The other direction — other people's software that reached for *this*
  engine — is **[live/ADOPTIONS.md](live/ADOPTIONS.md)**: a Wolfram paclet, a
  browser playground offering rakupp as one of four runtimes, a Guix channel.

## Run Raku in the browser — Raku.js

▶ **Try it live: [raku.online/play](https://raku.online/play)** · **Learn it interactively: [raku.online/tour](https://raku.online/tour/)**

**[Raku.js](rakujs)** is the *same* interpreter compiled to **WebAssembly** with
Emscripten — the exact semantics as native `rakupp`, running entirely client-side
with no server. Putting a real, running Raku editor on any static page is one
script tag:

```html
<script src="https://raku.online/raku.js"></script>

<pre data-raku>say "Hello from an embedded editor!";</pre>
```

Handy for docs, tutorials, or a course — and nothing has to be loaded from
raku.online: three files copied into a directory of your own site are a
complete install. It also powers a standalone
[playground](rakujs/PLAYGROUND.md), and answers to `rakupp_run()` if you would
rather drive it from your own JavaScript. All three routes are in
[rakujs/README.md](rakujs/README.md).

## Use Raku from Python, JavaScript, Go, Rust, C++, Wolfram Language

*Work in progress: committed so it is not lost and re-gated on every push,
but not announced yet — the official announcement will come when it settles.*

`librakupp` embeds the interpreter behind a small C ABI, and
**[bindings/](bindings/README.md)** wraps it for six host languages. Each
gives you the same two things in its own idiom: **run Raku** — evaluate
source, call Raku routines with your own values, read results back as native
types — and **parse with Raku grammars**, where the grammar stays a `.raku`
file and `.made` values are computed by Raku actions during the parse. Every
language has a guide and two runnable examples in
[bindings/examples/](bindings/examples/README.md), kept honest by two smoke
gates that re-run everything the guides claim.

## Give an AI agent a Raku interpreter — MCP

*Work in progress, on the same terms as the bindings above.*

`rakupp --mcp` serves the interpreter over the
[Model Context Protocol](https://modelcontextprotocol.io) — JSON-RPC on
stdio — so MCP clients (Claude Code, Claude Desktop, and their kind) get two
tools: **`raku`**, one persistent session per conversation, with exact
Rat and big-integer arithmetic; and **`raku-parse`**, grammars as
deterministic text extraction, with line/column/rule diagnosis when a parse
fails. Registering it with Claude Code is one line:

```sh
claude mcp add raku -- /path/to/rakupp --mcp
```

— or, where there is no `claude` CLI (the desktop app alone is enough), a
`.mcp.json` at the project root, read automatically when a session starts:

```json
{
  "mcpServers": {
    "raku": {
      "command": "/absolute/path/to/rakupp",
      "args": ["--mcp"]
    }
  }
}
```

This repository carries one, pointing at `./build/rakupp`: a built checkout
serves its own interpreter to the agent working on it.

Guide: **[MCP.md](docs/guide/MCP.md)**. Gated by `tools/mcp-smoke.raku`,
which drives the server exactly as a client does, on every push.

## Raku in a notebook — Jupyter

`rakupp --jupyter-install` registers the binary as a Jupyter kernel; after
that, `jupyter console --kernel raku` or picking **Raku++** in JupyterLab runs
notebook cells through this engine. One interpreter serves the whole notebook,
so a sub defined in cell 3 is callable in cell 9; a cell's output streams as it
is produced; a cell that dies leaves the session intact; and
`jupyter-display($html, 'text/html')` hands the frontend something to render.

Nothing needs installing on the Raku side — **no ZeroMQ, no Python module, no
shared library**. The binary speaks ZMTP and signs its own messages, because
this project links no third-party libraries.

Guide: **[JUPYTER.md](docs/guide/JUPYTER.md)**. Gated by
`tools/jupyter-smoke.raku` — a Jupyter client written in Raku, with its own
HMAC-SHA256 pinned to the RFC 4231 vectors, so both halves of the protocol
have to agree.

## Documentation

Start with the **[presentation](presentation)** (a slide deck — PDF or
interactive HTML), **[HIGHLIGHTS.md](docs/guide/HIGHLIGHTS.md)** (the key
features on one page), or **[GUIDE.md](docs/guide/GUIDE.md)** (the full
overview). The complete annotated index is **[docs/README.md](docs/README.md)**;
the shape of it:

- **[docs/guide/](docs/guide/)** — the manual: [FEATURES.md](docs/guide/FEATURES.md),
  the [REFERENCE.md](docs/guide/REFERENCE.md) lookup sheet, a
  [RECIPES.md](docs/guide/RECIPES.md), the [faq/](docs/guide/faq/), modules,
  Unicode, async, networking, NativeCall, embedding, the CLI.
- **[docs/cookbook/](docs/cookbook/)** — whole tasks worked end to end, the
  programs beside the page. Mirrored at
  [raku.online/cookbook/](https://raku.online/cookbook/).
- **[docs/internals/](docs/internals/)** — how it works inside:
  [ARCHITECTURE.md](docs/internals/ARCHITECTURE.md), parsing, the runtime
  model, the optimizer — and the 320-page book *Raku++ Internals*
  ([docs/book/](docs/book/)).
- **[docs/status/](docs/status/)** — how good it is:
  [ROAST.md](docs/status/ROAST.md) per-section statistics,
  [COUNTING.md](docs/status/COUNTING.md) methodology,
  [BENCHMARKS.md](docs/status/BENCHMARKS.md) vs Rakudo, mutsu, and Perl,
  [ROADMAP.md](docs/status/ROADMAP.md).
- **The story:** [MILESTONES.md](docs/status/MILESTONES.md) (the dated
  timeline), [JOURNEY.md](docs/dev/JOURNEY.md) (the method),
  [LONGREAD.md](LONGREAD.md) (the whole arc, long-form).

## Talks

Raku++ is being presented at the two forthcoming Perl & Raku conferences —
what it is, why it exists, and how it is developed:

| When | Where | Event |
|---|---|---|
| Saturday **21 November 2026** | The Café at Zoopla, The Cooperage, 5 Copper Row, London SE1 2LH, UK | [London Perl & Raku Workshop 2026](https://act.yapc.eu/lpw2026/) ([proposal 8059](https://act.yapc.eu/lpw2026/talk/8059)) |
| **14–16 April 2027** | Stadtteilzentrum Nordstadt Bürgerschule, Klaus-Müller-Kilian-Weg 2, 30167 Hannover, Germany | [29. Deutscher Perl/Raku-Workshop 2027](https://act.yapc.eu/gpw2027/) ([proposal 8053](https://act.yapc.eu/gpw2027/talk/8053)) |

FOSDEM 2027 (Brussels, ULB Solbosch) is on the wish list, but its dates and
call for participation are not out yet. The running list, with what the talks
cover and the slides, is **[TALKS.md](TALKS.md)**.

## Author

Raku++ is created by [Andrew Shitov](https://andrewshitov.com). Read the
announcement:
[Raku++ — the fastest Raku compiler](https://andrewshitov.com/2026/07/13/raku-the-fastest-raku-compiler/).

## License

[Artistic License 2.0](LICENSE) — the same license Raku itself uses.
