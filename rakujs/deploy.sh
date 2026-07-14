#!/usr/bin/env bash
#
# deploy.sh — copy the built Raku.js playground bundle to a target directory,
# e.g. the /playground folder of a static site (course.raku.org/playground).
#
#   rakujs/deploy.sh /path/to/course-site/playground
#
# The bundle is 5 self-contained, same-directory files (index.html + worker.js +
# rakujs.js + rakujs.wasm + examples.js); all their links are relative, so served
# from any /playground/ path it just works. Build them first with rakujs/build.sh
# — this script runs it automatically if rakujs.wasm is missing.
#
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$HERE/playground"
DEST="${1:-}"

if [[ -z "$DEST" ]]; then
  echo "usage: rakujs/deploy.sh <target-dir>   (e.g. .../course-site/playground)" >&2
  exit 2
fi

[[ -f "$SRC/rakujs.wasm" ]] || "$HERE/build.sh"

mkdir -p "$DEST"
cp "$SRC/index.html" "$SRC/worker.js" "$SRC/rakujs.js" "$SRC/rakujs.wasm" "$SRC/examples.js" "$DEST/"

echo "==> deployed Raku.js playground → $DEST"
du -ch "$DEST"/index.html "$DEST"/worker.js "$DEST"/rakujs.js "$DEST"/rakujs.wasm "$DEST"/examples.js | tail -1
echo "    serve it under a path ending in / (e.g. https://course.raku.org/playground/)"
