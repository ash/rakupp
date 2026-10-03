# Integrations: Raku++ inside other tools

The `rakupp` binary can serve other programs as well as run your own: an
editor, an AI agent, a notebook. Each of these is a mode of the same binary,
started by the host program, talking a standard protocol over stdin and stdout
(or, for Jupyter, over sockets). Nothing extra has to be installed on the Raku
side.

| You want | Command | Protocol | Guide |
|---|---|---|---|
| Errors underlined, hover, completion and go-to-definition in VS Code or another editor | `rakupp --lsp` | Language Server Protocol | [LSP.md](LSP.md) |
| Raku in Emacs: highlighting, errors as you type, completion, documentation, a REPL buffer | `rakupp --lsp` and `rakupp` | Language Server Protocol; a REPL on a pty | [EMACS.md](EMACS.md); never used Emacs: [EMACS-101.md](EMACS-101.md) |
| An AI agent (Claude Code, Claude Desktop, Cursor, …) that can run Raku and parse with grammars | `rakupp --mcp` | Model Context Protocol | [MCP.md](MCP.md) |
| Raku cells in JupyterLab, Notebook, `jupyter console` or VS Code notebooks | `rakupp --jupyter-install`, then pick **Raku++** | Jupyter messaging over ZMTP | [JUPYTER.md](JUPYTER.md) |
| Raku called from Mathematica or the Wolfram Engine | ``Get["RakuLang.wl"]`` | the Wolfram FFI over `librakupp` | [bindings/wolfram](../../../bindings/wolfram/README.md) |

The first three run `rakupp` as a separate process. The Wolfram Language
package loads the engine into its own process as a library instead, which
makes it one of the [language bindings](../../../bindings/README.md), alongside
Python, JavaScript, Go, Rust and C++. All of them are built on the C API in
[EMBEDDING.md](../EMBEDDING.md), which also lets you embed Raku in your own C
or C++ program.

The command-line flags for each mode are also in [CLI.md](../CLI.md#serving).
