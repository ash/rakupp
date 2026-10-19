# Raku++ vs Rakudo vs mutsu — features and tools

What each engine offers beyond the language itself: tools, platforms and
integrations. Speed is compared in [BENCHMARKS.md](BENCHMARKS.md), conformance
in [ROAST.md](ROAST.md).

**Checked 2026-10-16** against each project's own documentation: Rakudo's
[rakudo.org](https://rakudo.org), with [raku.land](https://raku.land) for
community modules; Raku++'s [CLI.md](../guide/CLI.md) and
[integrations](../guide/integrations/README.md); mutsu's
[README](https://github.com/tokuhirom/mutsu),
[manual](https://tokuhirom.github.io/mutsu/manual.html) and
[docs/](https://github.com/tokuhirom/mutsu/tree/main/docs).

● from the engine's own project · ○ through a community module or tool ·
◐ in the engine's repository, not yet in a release · an empty cell means not
available or not documented.

| | Rakudo | Raku++ | mutsu | Notes |
|---|:-:|:-:|:-:|---|
| Written in | NQP and Raku, on MoarVM or the JVM | C++ | Rust | |
| REPL | ● | ● | ● | mutsu: `mutsu --repl` |
| Package manager | ● zef | ● `rakupp install` | ● mzef | `rakupp install` writes the store zef uses ([CLI.md](../guide/CLI.md#installing-modules)); mzef is zef, bundled to run on mutsu |
| NativeCall | ● | ● | ● | Raku++: [FFI.md](../guide/FFI.md) |
| Profiler | ● | ● | ● | `--profile` in all three |
| JVM backend | ● | | | |
| Native executables | | ● | | `rakupp --exe` |
| Runs in the browser | | ● | ● | Raku.js; mutsu's WebAssembly npm package and playground |
| Language server | ○ | ● | ◐ | Rakudo: Raku Navigator; mutsu: `mutsu-lsp`, built from source, not in the release tarball |
| VS Code extension | ○ | ● | | Rakudo: Raku Navigator; Raku++: `DeepSoft.rakupp` |
| Jupyter kernel | ○ | ● | | Rakudo: Jupyter::Kernel, Jupyter::Chatbook |
| Linter | | ● | | `rakupp --lint` |
| MCP server for AI agents | | ● | | `rakupp --mcp` |
| Linux and macOS binaries | ● | ● | ● | |
| Windows binaries | ● | ● | | mutsu publishes Linux and macOS builds only |
| Docker image | ● | ● | ● | `rakudo-star`, `ghcr.io/ash/rakupp`, `ghcr.io/tokuhirom/mutsu` |

Left out on purpose: a formatter row, because Rakudo has the old community
Perl6::Tidy and whether it works on a current Rakudo was not checked; and
maturity, conformance and speed, which have pages of their own.
