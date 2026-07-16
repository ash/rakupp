#!/bin/sh
# serve.sh — run the browser showcases as a self-contained static site.
#
# The apps need the Raku.js WebAssembly build (rakujs.js + rakujs.wasm) and the
# two CLI showcase sources they reuse (md2html.raku, json.raku). This script
# gathers all of that into lib/, then serves *this* folder with the rakus
# showcase (rakupp serving its own WebAssembly). The URLs are then short —
# http://127.0.0.1:PORT/ is the launcher, /regex.html is an app.
#
#   showcase/web/serve.sh            # port 8000
#   showcase/web/serve.sh 9000       # a different port
#
# lib/ is git-ignored; re-run this to refresh it after rebuilding Raku.js.
set -e

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
build="$repo/rakujs/playground"
port="${1:-8000}"

# 1. Make sure the WebAssembly build exists (build it if not).
if [ ! -f "$build/rakujs.wasm" ] || [ ! -f "$build/rakujs.js" ]; then
    echo "Raku.js build not found — building it (rakujs/build.sh)…"
    "$repo/rakujs/build.sh"
fi

# 2. Make sure rakupp itself is built.
rakupp="$repo/build/rakupp"
if [ ! -x "$rakupp" ]; then
    echo "rakupp not found at $rakupp — build it first (see the top-level README)." >&2
    exit 1
fi

# 3. Gather everything the apps load into lib/ so this folder is self-contained:
#    the WebAssembly build, and the CLI showcase sources json/markdown reuse.
mkdir -p "$here/lib"
cp "$build/rakujs.js" "$build/rakujs.wasm" "$here/lib/"
cp "$repo/showcase/markdown/md2html.raku" "$repo/showcase/json/json.raku" "$here/lib/"

echo "Serving $here"
echo "  open http://127.0.0.1:$port/"
exec "$rakupp" "$repo/showcase/rakus/rakus.raku" "$port" "$here"
