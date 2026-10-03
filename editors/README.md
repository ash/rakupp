# Editor integration — the rakupp language server

> **Using it?** The user guide is
> [docs/guide/integrations/LSP.md](../docs/guide/integrations/LSP.md): VS Code
> setup in three steps, what each diagnostic means, and troubleshooting. This
> page is for working on the server and the extension.

> **Status.** The server answers diagnostics, hover, completion and
> go-to-definition; diagnostic ranges cover the token a finding is about
> (`LintFinding::subject`, `ParseError::got`), found on the finding's line in
> the token stream, since `struct Node` carries a line but no column. Hover,
> completion and definition are `src/LspIndex.cpp`. Checked with VS Code 1.140
> (diagnostics) and with Emacs 31.1 through eglot and lsp-mode (everything).
> The VS Code extension's package.json names version 0.9.1 and has never been
> published.


`rakupp --lsp` runs a [Language Server Protocol](https://microsoft.github.io/language-server-protocol/)
server on stdin/stdout. It reuses the exact same pipeline as `rakupp --lint`
(lex → parse → lint) plus parse-error reporting, and pushes the results to your
editor as **diagnostics** — the red/yellow squiggles under mistakes — and
answers hover, completion and go-to-definition from the open file's tokens and
the baked `docs/guide/REFERENCE.md`.

Because it *is* the same binary that runs your code, the diagnostics can never
disagree with the interpreter, and every parser improvement sharpens them for
free.

- **Diagnostics:** syntax errors (severity *Error*) and lint findings such as
  unused variables, redundant `return`, unreachable code (severity *Warning* /
  *Info*), each on the token it is about.
- **Hover, completion, go-to-definition:** see
  [LSP.md](../docs/guide/integrations/LSP.md#hover-completion-and-go-to-definition).
- **Not written:** find references, rename, signature help, formatting.

There are two ways to see it work: straight from the command line (no editor
needed — great for debugging), or through the VS Code extension.

---

## 1. See it from the command line

An LSP server just reads JSON-RPC messages framed with a `Content-Length`
header and writes replies the same way. You can feed it a whole session by hand.

### The turn-key way — `lsp-demo.sh`

```sh
# from this directory (editors/), pointing at any rakupp build:
RAKUPP=../build/rakupp ./lsp-demo.sh
```

It opens a small demo document, and the server answers with its capabilities
and a diagnostic:

```json
Content-Length: 163

{"id":1,"jsonrpc":"2.0","result":{"capabilities":{ ... },"serverInfo":{"name":"rakupp-lsp"}}}
Content-Length: 290

{"jsonrpc":"2.0","method":"textDocument/publishDiagnostics","params":{"diagnostics":[
  {"code":"unused-variable","message":"'$x' is declared but never used",
   "range":{"start":{"line":0,"character":0},"end":{"line":0,"character":10}},
   "severity":2,"source":"rakupp"}],"uri":"file:///demo.raku"}}
```

Point it at a real file to diagnose that instead:

```sh
RAKUPP=../build/rakupp ./lsp-demo.sh myprogram.raku
```

To pull out just the human-readable findings:

```sh
RAKUPP=../build/rakupp ./lsp-demo.sh myprogram.raku | grep -o '"message":"[^"]*"'
```
```
"message":"'return' as the final statement is redundant; the block's last value is returned automatically"
"message":"'$unused' is declared but never used"
```

### The manual way — frame the messages yourself

Every message is `Content-Length: <bytes>\r\n\r\n<json>`. This tiny shell
helper computes the byte count for you:

```sh
lsp_send() {
  for m in "$@"; do
    printf 'Content-Length: %d\r\n\r\n%s' "$(printf %s "$m" | wc -c | tr -d ' ')" "$m"
  done
}

lsp_send \
  '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"capabilities":{}}}' \
  '{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///t.raku","languageId":"raku","version":1,"text":"my $x = 3;\nsay 42\n"}}}' \
  '{"jsonrpc":"2.0","method":"exit"}' \
  | rakupp --lsp
```

A minimal session is three messages:

| Message | Purpose |
|---|---|
| `initialize` | handshake; the server replies with its `capabilities` |
| `textDocument/didOpen` | hand the server a document (`uri` + full `text`); it replies with a `publishDiagnostics` notification |
| `exit` | shut the server down |

To re-check after an edit, send `textDocument/didChange` with the new full text
(the server advertises **full** document sync, so send the whole document, not
a delta):

```json
{"jsonrpc":"2.0","method":"textDocument/didChange",
 "params":{"textDocument":{"uri":"file:///t.raku","version":2},
           "contentChanges":[{"text":"my $x = 3; say $x;\n"}]}}
```

The next `publishDiagnostics` for that `uri` will come back empty — the variable
is used now, so the warning is gone.

### Reading a diagnostic

Each entry in the `diagnostics` array is standard LSP:

| Field | Meaning |
|---|---|
| `range` | `{start,end}` positions, **0-based** line & UTF-16 character (the token the finding is about, else the line without its indentation) |
| `severity` | `1` Error · `2` Warning · `3` Info · `4` Hint |
| `code` | the stable rule id — `parse-error`, `unused-variable`, … (same ids as `rakupp --lint`) |
| `source` | always `"rakupp"` |
| `message` | the human-readable text |

Cross-check any finding against the CLI linter — they are the same engine:

```sh
rakupp --lint myprogram.raku
```

---

## 2. Use it in VS Code

The extension in [`vscode/`](vscode/) is a thin client: it launches
`rakupp --lsp` and lets VS Code render the squiggles. It also ships basic
syntax highlighting.

### Run it from source

```sh
cd vscode
npm install     # required once — F5 compiles but does not install deps
```

Then open the **`vscode/` folder itself** in VS Code (not the repo root) and
press **F5**. The bundled [`.vscode/launch.json`](vscode/.vscode/launch.json)
defines a *Run Extension* configuration that compiles the TypeScript and opens
an **Extension Development Host** window.

In that new window, open a `.raku` file and introduce an error. A squiggle
appears as you type. Two things to check if it doesn't:

- **Save the file to disk.** The client attaches to files with the `file://`
  scheme, so a brand-new untitled buffer gets no diagnostics until you save it
  as `something.raku`.
- **Make sure `rakupp` is found.** If it is not on your `PATH`, set
  `rakupp.path` (see below). Open **View → Output → "Raku++"** to see the
  server channel; a startup error shows there.

If `rakupp` is not on your `PATH`, set the binary explicitly in Settings:

```jsonc
// settings.json
"rakupp.path": "/absolute/path/to/rakupp/build/rakupp"
```

To flush a redistributable `.vsix`:

```sh
npx @vscode/vsce package    # produces rakupp-<version>.vsix
code --install-extension rakupp-*.vsix
```

### Other editors

Any LSP-capable editor can use the server — the command is always
`rakupp --lsp` over stdio. For example, Neovim with `nvim-lspconfig`:

```lua
vim.lsp.start({
  name = "rakupp",
  cmd = { "rakupp", "--lsp" },
  filetypes = { "raku" },
  root_dir = vim.fn.getcwd(),
})
```
