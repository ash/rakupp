# Raku++ for VS Code

Raku support from [Raku++](https://raku.online), an interpreter and compiler
for Raku. The extension starts `rakupp --lsp`, the language server built into
the `rakupp` binary, so the editor checks your code with the same engine that
runs it.

## Features

- **Errors and lint findings as you type**: syntax errors, undeclared
  variables, unused variables and routines, a redundant final `return`,
  unreachable code. Each is underlined on the token it is about and listed in
  **View → Problems**.
- **Hover**: the declaration of the name under the cursor with its `#|`
  comment, or the reference entry for a built-in such as `say`.
- **Completion**: variables in scope, your subs and classes, and the built-in
  subs and methods; after a `.`, methods.
- **Go to definition** (F12) for names declared in the file.
- **Syntax highlighting** for `.raku`, `.rakumod`, `.rakutest`, `.rakudoc`,
  `.p6`, `.pl6` and `.pm6` files.

Hover, completion and definitions cover the open file and the built-ins, not
the modules it `use`s.

## Requirements

The extension needs the `rakupp` binary, which it does not include. Install it
with one command.

macOS, Linux and the BSDs:

```sh
curl -fsSL https://raku.online/install.sh | sh
```

Windows, in PowerShell:

```powershell
irm https://raw.githubusercontent.com/ash/rakupp/main/tools/install-windows.ps1 | iex
```

Then restart VS Code so that it sees the new `PATH`. The
[installation guide](https://github.com/ash/rakupp/blob/main/docs/guide/INSTALL.md)
lists the other ways to get `rakupp`.

With `rakupp` 5.2.1 or older you get the underlines only. Hover, completion
and go-to-definition need a newer `rakupp`; `rakupp upgrade` updates it in
place.

## Settings

| Setting | Default | Description |
|---|---|---|
| `rakupp.path` | `rakupp` | The `rakupp` executable. Set an absolute path if VS Code does not find it on `PATH`. |
| `rakupp.trace.server` | `off` | Log the messages exchanged with the server in the **Raku++** output channel (`messages` or `verbose`). |

## Troubleshooting

If no underlines appear, open **View → Output** and choose **Raku++** in the
drop-down: a server that failed to start says why there. A new, unsaved file
gets no checks until you save it with a Raku extension such as `.raku`.

The full guide, with every kind of finding, is
[LSP.md](https://github.com/ash/rakupp/blob/main/docs/guide/integrations/LSP.md).
Report problems at [github.com/ash/rakupp/issues](https://github.com/ash/rakupp/issues).

## Developing this extension

```sh
npm install
npm run compile
# then press F5 in VS Code to launch an Extension Development Host
```

[editors/README.md](https://github.com/ash/rakupp/blob/main/editors/README.md)
shows how to drive the language server from the command line.
