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

You need the `rakupp` binary. Install it (`brew install rakupp`) or build it
from the [rakupp repo](https://github.com/ash/rakupp). If it is not on your
`PATH`, set:

```jsonc
"rakupp.path": "/absolute/path/to/rakupp"
```

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
