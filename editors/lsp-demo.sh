#!/usr/bin/env bash
# Drive `rakupp --lsp` through one full LSP session from the shell and print
# the server's replies. No editor required — this is the quickest way to see
# the language server producing diagnostics.
#
#   ./lsp-demo.sh                 # uses `rakupp` from PATH, demo document
#   RAKUPP=../build/rakupp ./lsp-demo.sh
#   ./lsp-demo.sh path/to/file.raku   # diagnose a real file
set -euo pipefail

RAKUPP="${RAKUPP:-rakupp}"

# Frame each argument as an LSP message: a Content-Length header (byte-accurate)
# then a blank line then the JSON body.
lsp_send() {
  local m
  for m in "$@"; do
    printf 'Content-Length: %d\r\n\r\n%s' "$(printf %s "$m" | wc -c | tr -d ' ')" "$m"
  done
}

# JSON-string-escape stdin (\, ", newline, tab) so a file's text can be embedded.
json_escape() {
  sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' | awk 'BEGIN{ORS=""} {print (NR>1?"\\n":"") $0}'
}

if [[ $# -ge 1 && -f "$1" ]]; then
  TEXT="$(json_escape < "$1")"
  URI="file://$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
else
  # A demo document with one lint warning ($x unused).
  TEXT='my \$x = 3;\nsay 42\n'
  URI="file:///demo.raku"
fi

lsp_send \
  '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"capabilities":{}}}' \
  "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{\"uri\":\"$URI\",\"languageId\":\"raku\",\"version\":1,\"text\":\"$TEXT\"}}}" \
  '{"jsonrpc":"2.0","id":2,"method":"shutdown"}' \
  '{"jsonrpc":"2.0","method":"exit"}' \
  | "$RAKUPP" --lsp
echo
