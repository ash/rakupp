# Raku++ for VS Code

Raku language support powered by the [rakupp](https://github.com/ash/rakupp)
language server — live diagnostics as you type, plus syntax highlighting.

## Features

- **Diagnostics** — syntax errors and lint findings (unused variables,
  redundant `return`, unreachable code, …) surfaced inline. They come straight
  from `rakupp --lsp`, the same engine that runs your code, so they never lie.
- **Syntax highlighting** for `.raku`, `.rakumod`, `.rakutest`, `.p6`, and
  friends.

More is planned (hover, completion, go-to-definition).

## Requirements

You need a `rakupp` binary newer than 5.2.1. Versions up to and including
5.2.1 reject the `--stdio` flag this client passes, and the server does not start.
Install it ([installation guide](https://github.com/ash/rakupp/blob/main/docs/guide/INSTALL.md))
or build it from the [rakupp repo](https://github.com/ash/rakupp), then set its
absolute path:

```jsonc
"rakupp.path": "/absolute/path/to/rakupp"
```

The full setup guide, with troubleshooting, is
[LSP.md](https://github.com/ash/rakupp/blob/main/docs/guide/integrations/LSP.md).

## Settings

| Setting | Default | Description |
|---|---|---|
| `rakupp.path` | `rakupp` | Path to the rakupp executable. |
| `rakupp.trace.server` | `off` | Trace LSP traffic in the **Raku++** output channel (`messages` / `verbose`). |

## Developing this extension

```sh
npm install
npm run compile
# then press F5 in VS Code to launch an Extension Development Host
```

See [`../README.md`](../README.md) for how to exercise the language server
directly from the command line — useful when debugging the server itself.
