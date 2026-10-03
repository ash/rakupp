# The language server: Raku diagnostics in your editor

`rakupp --lsp` runs a [Language Server
Protocol](https://microsoft.github.io/language-server-protocol/) server, so an
editor can underline mistakes in Raku code as you type. It is a
**diagnostics** server: syntax errors, undeclared variables, and the findings
of `rakupp --lint`, each on the line it belongs to. It does nothing else:
there is no completion, no hover documentation and no go-to-definition.

The findings come from the same code that `rakupp -c` and `rakupp --lint` run,
so the editor and the command line always agree. The server parses your file
but never runs it.

This page starts with VS Code. Other editors are covered
[further down](#other-editors).

## VS Code

The extension is in [`editors/vscode/`](../../../editors/vscode/). It is not on
the Marketplace yet, so you build it once from the repository and install the
`.vsix` file. The extension is a thin client: it starts `rakupp --lsp` and lets
VS Code draw what the server reports. It also adds syntax highlighting for
`.raku`, `.rakumod`, `.rakutest` and the older `.p6`/`.pm6` extensions.

### 1. Have a `rakupp` that VS Code can start

```bash
command -v rakupp
```

Note the absolute path it prints; step 3 uses it. Any install works: the
one-liner, Homebrew, a release archive, or a build of this repository. The
`rakupp` must be **newer than 5.2.1**. Versions up to and including 5.2.1 reject the
`--stdio` flag that VS Code adds to the command, so the server never starts
(see [Troubleshooting](#troubleshooting)).

### 2. Build and install the extension

Building needs Node.js and npm. From the repository root:

```bash
(cd editors/vscode && npm install && npx @vscode/vsce package)
```

This writes `rakupp-0.9.1.vsix` into `editors/vscode/`. Install it:

```bash
code --install-extension editors/vscode/rakupp-0.9.1.vsix
```

If `code` is not found, run **Shell Command: Install 'code' command in PATH**
from VS Code's Command Palette (⇧⌘P), or install the file from the GUI:
open the Extensions view, click the **⋯** menu at its top, choose
**Install from VSIX…** and pick the file.

Run the build inside `editors/vscode/` itself. Packaging from a copy of the
folder elsewhere can fail with `Extension entrypoint(s) missing`, even though
`out/extension.js` is there.

### 3. Tell the extension where `rakupp` is

Open your user settings as JSON (Command Palette → **Preferences: Open User
Settings (JSON)**) and add:

```jsonc
"rakupp.path": "/absolute/path/to/rakupp"
```

The default is plain `rakupp`, looked up on `PATH`. Set an absolute path anyway.
VS Code started from the Dock or Spotlight does not always see the `PATH` your
shell builds, and a dead path fails in exactly the same way as a missing one.

Then reload the window (Command Palette → **Developer: Reload Window**).

### What you should see

Save this as `hello.raku` and open it:

```raku
my $unused = 1;
say $undeclared;
sub f { return 42 }
```

Four diagnostics appear, the same four that `rakupp --lint hello.raku`
prints:

| Line | Severity | Message |
|---|---|---|
| 1 | Warning (yellow) | `'$unused' is declared but never used` |
| 2 | Error (red) | `'$undeclared' is not declared` |
| 3 | Information (blue) | `'return' as the final statement is redundant; the block's last value is returned automatically` |
| 3 | Warning (yellow) | `lexical routine '&f' is never called` |

Hover over a squiggle to read its message. **View → Problems** (⇧⌘M)
lists every finding in the open files. Fix a line and its diagnostic goes
away while you type, without saving.

### Settings

| Setting | Default | What it does |
|---|---|---|
| `rakupp.path` | `rakupp` | The binary to start, with `--lsp`. |
| `rakupp.trace.server` | `off` | `messages` or `verbose` logs every LSP message in the **Raku++** output channel. |

### Troubleshooting

When nothing is underlined, open **View → Output** and pick **Raku++** in the
drop-down. The server's startup messages and errors appear there.

| What you see | Why, and the fix |
|---|---|
| `Illegal option --stdio`, then `Server initialization failed` five times | The `rakupp` is 5.2.1 or older. Upgrade it, or point `rakupp.path` at a newer build. |
| A pop-up saying *Raku++: failed to start language server* | `rakupp.path` names no runnable file. Check the path; it often points into a build directory that has since been removed. |
| No squiggles in a new, unsaved buffer | The extension only handles files on disk. Save the buffer as `something.raku`. |
| The status bar says *Plain Text* instead of *Raku* | VS Code does not treat the file as Raku. Click the language name in the status bar and choose **Raku**. |
| The diagnostics did not change after you rebuilt `rakupp` | The server is a long-running process that started from the old binary. Reload the window. |

If the problem is still unclear, set `"rakupp.trace.server": "verbose"`,
reload, and read the JSON exchanged between VS Code and the server in the same
output channel.

## What the server reports

| Finding | Severity | Code |
|---|---|---|
| The file does not parse | Error | `parse-error` |
| A variable nothing declares | Error | `undeclared-variable` |
| A lint warning (unused variable, routine never called, …) | Warning | the lint rule's id |
| A lint note (redundant `return`, …) | Information | the lint rule's id |

The codes are the rule ids that `rakupp --lint` prints in square brackets.
[LINT.md](../LINT.md) lists them all. `RAKUPP_NO_DECLCHECK=1` in the
environment that starts the editor turns the undeclared-variable check off,
just as it does for `--lint`.

How the reports behave:

- **Every edit re-checks the file.** The editor sends the whole document after each
  change, and the server publishes a fresh list for it. Closing the file
  clears its diagnostics.
- **A parse error hides everything else.** A file that does not parse cannot
  be linted, so you see the first syntax error alone until it is fixed.
- **Underlines cover the whole line.** The parser records the line of each
  finding but not its column, so the squiggle covers the line rather than the
  exact token.
- **Modules resolve from the folder you opened.** `use` statements are looked
  up from the server's working directory. In VS Code that is the folder you
  opened, so `lib/` there is found as it would be by `rakupp --lint` run from
  that folder.

What it does not do: completion, hover, go-to-definition, rename, and
formatting. To format code, run [`rakupp --fmt`](../FMT.md) from a terminal or
a VS Code task.

## Check the server without an editor

[`editors/lsp-demo.sh`](../../../editors/lsp-demo.sh) runs one complete LSP
session from the shell and prints the server's replies. Use it to tell an
editor problem from a server problem. From the repository root:

```bash
editors/lsp-demo.sh path/to/hello.raku | grep -o '"message":"[^"]*"'
```

```
"message":"'$unused' is declared but never used"
"message":"'$undeclared' is not declared"
"message":"'return' as the final statement is redundant; the block's last value is returned automatically"
"message":"lexical routine '&f' is never called"
```

It uses `rakupp` from `PATH`. Set `RAKUPP=/path/to/rakupp` to try another
build. [`editors/README.md`](../../../editors/README.md) shows how to write the
protocol messages by hand.

## Other editors

Any editor with an LSP client can use the server. The command is always
`rakupp --lsp` with no other arguments. The server talks over stdin and stdout,
and also accepts `--stdio` for clients that add it. For Neovim:

```lua
vim.lsp.start({
  name = "rakupp",
  cmd = { "rakupp", "--lsp" },
  filetypes = { "raku" },
  root_dir = vim.fn.getcwd(),
})
```

Only the VS Code client has been tested with a real editor.

## How it works

The server is [`src/Lsp.cpp`](../../../src/Lsp.cpp). It reads JSON-RPC messages
framed by `Content-Length` headers on stdin and writes replies the same way on
stdout. It announces one capability, full-document sync, and handles
`initialize`, `textDocument/didOpen`, `didChange`, `didClose`, `shutdown` and
`exit`. Any other request gets a *method not found* error, so a client never
waits for an answer that will not come.

For each version of a document it runs lexer → parser → `lintProgram` → the
undeclared-variable check, the same steps as `rakupp --lint`, and publishes
the result as a `textDocument/publishDiagnostics` notification. No interpreter
is created, which is why opening a file can never run it. Malformed input is
dropped rather than fatal: the framing rejects an impossible `Content-Length`,
the JSON parser limits nesting depth, and an exception in the declaration
check becomes an Information diagnostic instead of ending the server.

Two regression tests drive a real server over a pipe.
`t/regression/lsp-reports-undeclared.raku` checks that it reports everything
`--lint` reports. `t/regression/lsp-accepts-stdio.raku` checks that it starts
the way VS Code launches it.
