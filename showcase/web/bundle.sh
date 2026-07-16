#!/bin/sh
# bundle.sh — prepare the browser showcases to open straight from disk.
#
# The apps run the Raku.js interpreter on the page with no server and no network:
# the WebAssembly binary is embedded as base64 and the showcase sources are
# inlined. This script generates those embedded assets into lib/. Run it once
# (again after rebuilding Raku.js), then just open the HTML files:
#
#   showcase/web/bundle.sh
#   open showcase/web/index.html          # or double-click it — no server
#
# lib/ is git-ignored; re-run this to refresh it.
set -e

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
build="$repo/rakujs/playground"

# 1. Make sure the WebAssembly build exists (build it if not).
if [ ! -f "$build/rakujs.wasm" ] || [ ! -f "$build/rakujs.js" ]; then
    echo "Raku.js build not found — building it (rakujs/build.sh)…"
    "$repo/rakujs/build.sh"
fi

# 2. Generate lib/: the Emscripten factory, the wasm as base64, and the two
#    showcase sources the JSON/Markdown apps reuse — all loadable via <script>.
mkdir -p "$here/lib"
cp "$build/rakujs.js" "$here/lib/rakujs.js"
python3 - "$build/rakujs.wasm" "$here/lib" \
         "$repo/showcase/markdown/md2html.raku" "$repo/showcase/json/json.raku" <<'PY'
import sys, base64, json, pathlib
wasm, libdir, md, js = sys.argv[1:5]
lib = pathlib.Path(libdir)
b64 = base64.b64encode(pathlib.Path(wasm).read_bytes()).decode()
(lib / "rakujs-wasm.js").write_text("window.RAKUJS_WASM_B64=" + json.dumps(b64) + ";\n")
sources = {
    "md2html": pathlib.Path(md).read_text(encoding="utf-8"),
    "json":    pathlib.Path(js).read_text(encoding="utf-8"),
}
(lib / "sources.js").write_text("window.RAKU_SOURCES=" + json.dumps(sources) + ";\n")
PY

echo "Bundled into $here/lib/"
echo "Now open showcase/web/index.html (or regex.html / markdown.html / json.html) directly — no server needed."
