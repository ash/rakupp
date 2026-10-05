# Raku++

[raku.online](https://raku.online) is the official public area of this project.

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

**Status:** current release **v5.2.1** (2026-10-03) — **100.00% of Roast.**
Of the tests Roast expects an implementation to pass, **all 218,420** pass, and
**all 1,424 files** in Roast's `spectest.data` pass completely (Roast
`1f749e338`). On Roast `1f521d798`, which v5.0.0 was measured against and whose
todo for one TTY test does not name macOS 27, it is 1,423 files; Rakudo 2026.08,
measured the same way on the same machine, passes 1,414 of them
([ROAST.md](docs/status/ROAST.md)). v5.2.0 interprets faster than v5.1.0 on every
benchmark kernel, and 92-97% faster where a sub or loop is plain integer and
string work ([BENCHMARKS.md](docs/status/BENCHMARKS.md)). Every release is
written up in the [CHANGELOG](CHANGELOG.md), and what each major set out to do is in
[VERSIONS.md](docs/dev/plans/VERSIONS.md).

**Next:** v6 is about speed ([V6-PLAN.md](docs/dev/plans/V6-PLAN.md)). Beside
it, the ecosystem: **1,019 of 2,547** distributions pass their own test suites,
measured by a fresh sweep on v5.0.0. Rakudo, run on the same machine through the
same harness (2026-09-16, 2,529 dists), passes 1,791; the other 738 cannot pass
there under any engine, for want of libgsl, fontconfig or a network. Every distribution, with how it ran, is at
[raku.online/modules/ecosystem](https://raku.online/modules/ecosystem/).

| | v5.2.1 | at v4.0.0 | at v3.0.0 |
|---|---:|---:|---:|
| Roast tests, skip and todo left out‡ | **218,420 of 218,420 (100.00%)** | — | — |
| Roast tests, every declared test‡ | **220,055 of 220,055 (100.00%)** | 200,843 of 219,610 (91%) | 197,191 of 218,772 (90%) |
| Roast files fully passing, of 1,424 | **1,424** | 656 | — |
| Official documentation examples byte-identical on both engines | **1,006**¶ | 957 | 945 |
| Modules — the 59-dist battery, each against its own suite | **48 / 59**¶ | 49 / 59† | 47 / 59 |
| Modules — the whole [ecosystem](https://raku.online/modules/ecosystem/) | **1,019 of 2,547**§ | 1,006 of 2,529 | — |
| Local regression suite | **1,240** | 1,020 | 398 |

Dashes mean the measurement did not exist yet. The v5.2.1 Roast figures are on
the 1,424 files `spectest.data` lists at Roast `1f749e338`, the same as
v5.2.0's; the older columns were measured on the whole checkout (1,464 files
at v4.0.0, 1,462 at v3.0.0), and 656 is how many of the 1,424 v4.0.0 passed.

¶ Measured for v5.0.0 (the documentation examples) and v5.0.1 (the battery);
v5.1.0, v5.2.0 and v5.2.1 did not re-run those two sweeps.

§ A fresh sweep of every distribution in the index on the v5.0.0 release binary, 2026-09-29 ([ECOSWEEP](docs/dev/findings/ECOSWEEP-2026-08.md)); v4.0.0's figure was a warm-store board.

† v4.0.1, re-measured against Rakudo on 2026-09-29. v4.0.0 published 50 / 59,
but its runner's reference engine was Raku++ itself (see the v5.0.0
[CHANGELOG](CHANGELOG.md) entry).

‡ How these are counted and gated — [COUNTING.md](docs/status/COUNTING.md).

## Install

```sh
curl -fsSL https://raku.online/install.sh | sh          # macOS, Linux, the BSDs
```

```powershell
# Windows, in a PowerShell window — irm and iex are PowerShell's own aliases
irm https://raw.githubusercontent.com/ash/rakupp/main/tools/install-windows.ps1 | iex
```

Neither needs root or an administrator. Each downloads the archive for the
machine it is running on, checks it against the published SHA-256, unpacks it
into a per-user prefix, asks whether you also want the engine under the second
name `raku`, and puts it on your `PATH`. Then open a new terminal.

```sh
rakupp upgrade        # later: replace it with the latest release, in place
```

Or run it from **Docker**, with nothing installed at all — every release is an
image for `linux/amd64` and `linux/arm64`:

```sh
docker run --rm -it ghcr.io/ash/rakupp                       # the REPL
docker run --rm -v "$PWD:/work" ghcr.io/ash/rakupp main.raku  # a file from here
```

The tags are `latest`, the version and the minor (`5.2.1`, `5.2`); building on
the image, modules and `--exe` inside it are in
[INSTALL.md](docs/guide/INSTALL.md#docker).

On Debian and Ubuntu there is a `.deb` —
`sudo apt install ./rakupp-linux-x86_64.deb` after downloading it, see
[INSTALL.md](docs/guide/INSTALL.md#debian-and-ubuntu-a-deb). On macOS there is
the Homebrew tap — `brew tap ash/rakupp && brew install rakupp` — and on
Windows a wizard:
**[rakupp-setup-windows-x64.exe](https://github.com/ash/rakupp/releases/latest/download/rakupp-setup-windows-x64.exe)**, with the same two questions
as checkboxes and an Add/Remove Programs entry.

Or unpack a **prebuilt archive** — macOS universal, Linux x86_64 and ARM64
(static libstdc++, glibc 2.28+), OpenBSD, Windows x64 — from the
[Releases page](https://github.com/ash/rakupp/releases/latest) and put its
`bin/` on your `PATH`. Linux on **RISC-V** (riscv64) has no archive yet; it
builds from source with no changes, as below.

### Build from source

```sh
# Needs a C++17 compiler + CMake → produces build/rakupp
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 4
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
| `--jit[=SPEC]` | *(work in progress)* Compile hot loops to native code *while the program runs* and enter them mid-loop. Off by default; see [JIT.md](docs/guide/JIT.md) |
| `--cnp[=SPEC]` | *(work in progress)* The same, by copy-and-patch: hot loops are stitched from machine-code snippets compiled into rakupp itself, so no C++ compiler is needed and nothing is cached. Off by default. These two are one feature with two backends, and the plan is for `--cnp` to become the default and `--jit` to go; see [JIT.md](docs/guide/JIT.md#--cnp-the-same-thing-without-a-compiler) |
| `--exe SRC -o OUT` | Native-compile to a standalone binary (also `--bundle`, `--aot`) |
| `--target=js SRC -o OUT.js` | Transpile to JavaScript for Node, Bun, Deno or a browser (`--verify` checks it against the interpreter; see [JS.md](docs/guide/JS.md), walked through in [JS-TUTORIAL.md](docs/guide/JS-TUTORIAL.md)) |
| `--highlight [SRC]` | Syntax-highlight Raku to HTML (`--html`) or terminal (`--ansi`) |
| `--mcp` | Serve the interpreter over the Model Context Protocol for AI agent clients |
| `--jupyter FILE` | Run as a Jupyter kernel (`--jupyter-install` registers it with Jupyter) |
| `--lint SRC` | Static-analyze without running: unused variables, unreachable code, etc. |
| `-c` / `--ast SRC` | Compile-check only (parse + every variable declared) / print the parsed AST |
| `-q` / `--quiet` | Drop what a mode says about itself (`Syntax OK`, `Compiled …`, the installer's `already installed:`); output, warnings and errors stay. Any mode |
| `--watch` | Rerun on change — the program file, or a module under `-I`/`lib/`; with `-c` or `--lint`, a live check loop |
| `--repl-after` / `--trace` / `--stagestats` | Run, then a session on the program's live state (python `-i`) / print each statement as it runs / phase timings and module-load times |
| `--json` | `-c` and `--lint` findings as a JSON array (file, line, severity, rule, message) |
| `--seed[=N]` / `--stack-size=N` | Pin the random generator (bare `--seed` picks and prints one) / size the recursion ceiling (default 1G, a bare number is MiB) |
| `--env-file=FILE` / `RAKUPP_OPT` / `--color=WHEN` / `-x` | Load a `.env` first / standing options from the environment (options only) / colour `auto`, `always`, `never` (`NO_COLOR` honoured) / skip to the `#!` line |
| `rakupp doc SYMBOL` / `--completions=SHELL` | Look a builtin, method or operator up offline / a completion script for bash, zsh or fish |
| `-v` / `--version` · `-V` / `--info` | The release, the build it came from and the platform, on one line / the full build report: also the Raku version implemented, the compiler, the FFI backend NativeCall found and which binary answered. Quote `--info` whole in a bug report |

Flags are position-independent and cluster like Perl's (`rakupp -pi.bak -e
'$_ = $_.subst("a", "b")' *.txt` works as you'd hope). Full reference:
[CLI.md](docs/guide/CLI.md).

## Modules

Raku++ installs modules from the ecosystem with **its own installer** —
compatible with [zef](https://github.com/ugexe/zef), so a module installed by
either tool is picked up by `use` under either engine:

```sh
rakupp install JSON::Fast        # or: zef install JSON::Fast   (via Rakudo)
rakupp install ./my-dist         # a checkout, or a URL:
rakupp install https://github.com/ash/raku-modules/tree/main/Prompt-Hidden
```

```raku
use JSON::Fast;                             # works after either install
say to-json({ name => 'Ada' }, :!pretty);   # {"name":"Ada"}
```

It also loads your own module files from `lib/` (and `-I` / `RAKULIB` / `use lib`
paths), and a `use` that cannot be found or fails to compile is **fatal**.
How much of the ecosystem runs today: all 2,547 distributions, each with its
sweep verdict, are listed at
[raku.online/modules/ecosystem](https://raku.online/modules/ecosystem/).
Full guide: **[MODULES.md](docs/guide/MODULES.md)**.

### …and the batteries, with nothing installed

`use Data::Native` is answered by the **compiler itself** — no file is read, no
module is loaded, no dependency is resolved:

```raku
use Data::Native;

say to-json({ ok => True });            # JSON
say from-csv("a,b\n1,2\n", :headers);   # CSV
say sha256-hex('abc');                  # digests and HMAC
say uncompress(compress('big'.encode));  # zlib / gzip / raw deflate
say crypt_random_buf(32);               # bytes from the OS CSPRNG
```

Five tags, thirty-two names, every signature copied from the ecosystem module it
stands in for. It is **designed** to be portable — the companion distribution,
which would compose `JSON::Fast`, `Digest::SHA2`, `Compress::Zlib` and
`Crypt::Random` so the same program runs on Rakudo unchanged, is **not published
yet**. Until it is, `use Data::Native` works on Raku++ and nowhere else.
A program using it compiles to a standalone binary, because there is nothing
left to find at run time.

Guide: **[DATA-NATIVE.md](docs/guide/DATA-NATIVE.md)**.

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
  browser playground offering rakupp as one of four runtimes, a course of Raku
  koans graded by Raku.js, a Guix channel, an agent runtime and an invariants
  tool shipped as standalone files built by Raku++.

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

## Errors underlined as you type — the language server

`rakupp --lsp` is a Language Server: an editor sends it your code and it sends
back the syntax errors, undeclared variables and `--lint` findings, which the
editor underlines. These are the same findings `rakupp --lint` prints, from the
same code, and the server never runs the file. The VS Code extension in
[editors/vscode](editors/vscode/) is a thin client for it; any other editor with
an LSP client can start `rakupp --lsp` directly.

Guide: **[LSP.md](docs/guide/integrations/LSP.md)**. It covers building and
installing the extension, what each diagnostic means, and what to check when
nothing is underlined.

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

Guide: **[MCP.md](docs/guide/integrations/MCP.md)**. Gated by `tools/mcp-smoke.raku`,
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

Guide: **[JUPYTER.md](docs/guide/integrations/JUPYTER.md)**. Gated by
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
  [ROADMAP.md](docs/status/ROADMAP.md). Every one of those numbers charted
  release over release — Roast, the ecosystem, and each benchmark kernel on
  all four engines — is at
  [raku.online/spec/dashboard](https://raku.online/spec/dashboard/).
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
More posts about Raku and Raku++: **[BLOG.md](BLOG.md)**.

What other people write about Raku++ — the newsletters, posts and remarks —
is collected in **[MENTIONS.md](MENTIONS.md)**; other people's software that
reached for the engine is in **[live/ADOPTIONS.md](live/ADOPTIONS.md)**.

Organisations that run Raku on Raku++ can get commercial support — support
agreements, fixes and features on demand, help moving code from Rakudo,
performance work and training — from the team that builds it:
**[raku.online/support](https://raku.online/support/)**.

## License

[Artistic License 2.0](LICENSE) — the same license Raku itself uses.
