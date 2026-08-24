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

**Status:** current release **v3.6.0** (2026-08-21) — *the Perl 5 lessons*: a
compact insertion-ordered hash payload and three allocation fixes found by
benchmarking against perl (the hash-fill kernel now measures a statistical tie
with Perl 5 under `--exe -O3`), plus first `DESTROY` support. The release
before it was *the 6.e language revision*: `use v6.e.PREVIEW;` turns on the
whole of Raku 6.e, and its behaviour changes can affect existing code. Every
release is written up in the [CHANGELOG](CHANGELOG.md).

**Current focus:** the ecosystem sweep — all 2,524 distributions of the zef
ecosystem run against rakupp, and the engine gets fixed until real modules
install and pass their own test suites. As of the first sweep-and-fix round
(August 2026) **637 of 2,524 pass**, with another 437 blocked by a failing
dependency before their own tests could run; what the sweep finds drives what
gets built next ([the findings](docs/dev/findings/ECOSWEEP-2026-08.md), with
the green list and per-dist results). (The 59 in the table below is a small
curated battery gated on every release; the sweep is the whole ecosystem.)

| | v3.6.0 | at v2.0.0 |
|---|---:|---:|
| Roast, per individual test — of what the suite declares‡ | **198,642 of ~218,600 (90%)** | 197,090 of ~203,500 (97%) |
| Roast, all-or-nothing — files fully passing, of 1,464 | **633 (43%)** | 594 |
| Official documentation examples byte-identical on both engines | **949**† | 952 |
| Ecosystem distributions passing their own `zef` install-time test suite | **49 / 59**† | 50 / 59 |
| Local regression suite | **499** | 312 |
| `say "Hello"` compiled with `--exe --slim` | **5,247,200 B** | 9,830,680 B (no `--slim`) |

‡ Counted against each file's declared `plan N`, so a file that aborts is
charged for every test it failed to run; on the all-or-nothing bar a file
counts only if *every* assertion in it passes. Both are measured with
parallelism and true LTM on — the same binary configuration users get. How the
runs are profiled and gated: [COUNTING.md](docs/status/COUNTING.md).

† Measured at v3.5.0 — these two batteries were not re-run for v3.6.0; the
[CHANGELOG](CHANGELOG.md) says why.

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
| `FILE` / `-e 'CODE'` / *(stdin)* | Run a program from a file, a one-liner, or standard input |
| `-I <path>` / `-M <module>` | Add a module search directory / load a module first (both repeatable) |
| `-n` / `-p` / `-a` / `-F<sep>` / `-i[.ext]` | The perl one-liner family: line loop, autoprint, autosplit, in-place edit (clusters: `-lane`, `-pi.bak`) |
| `--profile[=FILE]` | Routine-level wall-time profile after the run (`.json` for machine-readable) |
| `--exe SRC -o OUT` | Native-compile to a standalone binary (also `--bundle`, `--aot`) |
| `--highlight [SRC]` | Syntax-highlight Raku to HTML (`--html`) or terminal (`--ansi`) |
| `--mcp` | Serve the interpreter over the Model Context Protocol for AI agent clients |
| `--lint SRC` | Static-analyze without running: unused variables, unreachable code, etc. |
| `-c` / `--ast SRC` | Syntax-check only / print the parsed AST |

Flags are position-independent and cluster like perl's (`rakupp -pi.bak -e
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
  them.

## Run Raku in the browser — Raku.js

▶ **Try it live: [raku.online/play](https://raku.online/play)** · **Learn it interactively: [raku.online/tour](https://raku.online/tour/)**

**[Raku.js](rakujs)** is the *same* interpreter compiled to **WebAssembly** with
Emscripten — the exact semantics as native `rakupp`, running entirely client-side
with no server. It powers an in-page [playground](rakujs/playground) and can be
embedded in any static page to make Raku snippets runnable — handy for docs,
tutorials, or a course. Details in [rakujs/README.md](rakujs/README.md).

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
tools: **`raku_eval`**, one persistent session per conversation, with exact
Rat and big-integer arithmetic; and **`raku_parse`**, grammars as
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

Guide: **[MCP.md](docs/guide/MCP.md)**. Gated by `tools/mcp-smoke.raku`,
which drives the server exactly as a client does, on every push.

## Documentation

Start with the **[presentation](presentation)** (a slide deck — PDF or
interactive HTML), **[HIGHLIGHTS.md](docs/guide/HIGHLIGHTS.md)** (the key
features on one page), or **[GUIDE.md](docs/guide/GUIDE.md)** (the full
overview). The complete annotated index is **[docs/README.md](docs/README.md)**;
the shape of it:

- **[docs/guide/](docs/guide/)** — the manual: [FEATURES.md](docs/guide/FEATURES.md),
  the [REFERENCE.md](docs/guide/REFERENCE.md) lookup sheet, a
  [COOKBOOK.md](docs/guide/COOKBOOK.md), the [faq/](docs/guide/faq/), modules,
  Unicode, async, networking, NativeCall, embedding, the CLI.
- **[docs/internals/](docs/internals/)** — how it works inside:
  [ARCHITECTURE.md](docs/internals/ARCHITECTURE.md), parsing, the runtime
  model, the optimizer — and the 320-page book *Raku++ Internals*
  ([docs/book/](docs/book/)).
- **[docs/status/](docs/status/)** — how good it is:
  [ROAST.md](docs/status/ROAST.md) per-section statistics,
  [COUNTING.md](docs/status/COUNTING.md) methodology,
  [BENCHMARKS.md](docs/status/BENCHMARKS.md) vs Rakudo (and perl),
  [ROADMAP.md](docs/status/ROADMAP.md).
- **The story:** [MILESTONES.md](docs/status/MILESTONES.md) (the dated
  timeline), [JOURNEY.md](docs/dev/JOURNEY.md) (the method),
  [LONGREAD.md](LONGREAD.md) (the whole arc, long-form).

## Author

Raku++ is created by [Andrew Shitov](https://andrewshitov.com). Read the
announcement:
[Raku++ — the fastest Raku compiler](https://andrewshitov.com/2026/07/13/raku-the-fastest-raku-compiler/).

## License

[Artistic License 2.0](LICENSE) — the same license Raku itself uses.
