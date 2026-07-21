#!/usr/bin/env bash
#
# build.sh — Raku.js: compile Raku++ to WebAssembly with Emscripten.
#
# Produces rakujs/playground/rakujs.js + rakujs.wasm from the C++ interpreter in
# src/ (unmodified) plus the thin web entry point rakujs/rakupp_web.cpp. If the
# Emscripten toolchain isn't on PATH, this script bootstraps a local copy under
# rakujs/emsdk (git-ignored) automatically.
#
# Usage:
#   rakujs/build.sh            # release build (-Oz, small)
#   rakujs/build.sh --debug    # -O0 + assertions, for diagnosing crashes
#
set -euo pipefail

RAKUJS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$RAKUJS_DIR/.." && pwd)"
SRC_DIR="$ROOT_DIR/src"
# Overridable for non-playground builds, e.g. the Node/Bun benchmark target:
#   RAKUJS_ENV=node RAKUJS_OUT=/tmp/rakujs-node rakujs/build.sh
OUT_DIR="${RAKUJS_OUT:-$RAKUJS_DIR/playground}"
RAKUJS_ENV="${RAKUJS_ENV:-web,worker}"

OPT="-Oz"
EXTRA_S=()
if [[ "${1:-}" == "--debug" ]]; then
  OPT="-O0"
  EXTRA_S+=(-sASSERTIONS=2 -sSAFE_HEAP=0)
  echo "==> debug build"
fi

# --- locate or bootstrap Emscripten -----------------------------------------
if ! command -v em++ >/dev/null 2>&1; then
  if [[ -f "$RAKUJS_DIR/emsdk/emsdk_env.sh" ]]; then
    # shellcheck disable=SC1091
    source "$RAKUJS_DIR/emsdk/emsdk_env.sh" >/dev/null 2>&1
  fi
fi
if ! command -v em++ >/dev/null 2>&1; then
  echo "==> Emscripten not found — bootstrapping into rakujs/emsdk (~1 GB download)"
  if [[ ! -d "$RAKUJS_DIR/emsdk" ]]; then
    git clone --depth 1 https://github.com/emscripten-core/emsdk.git "$RAKUJS_DIR/emsdk"
  fi
  "$RAKUJS_DIR/emsdk/emsdk" install latest
  "$RAKUJS_DIR/emsdk/emsdk" activate latest
  # shellcheck disable=SC1091
  source "$RAKUJS_DIR/emsdk/emsdk_env.sh"
fi
echo "==> using $(em++ --version | head -1)"

# --- gather sources (everything in src/ except the CLI entry main.cpp) -------
SOURCES=()
while IFS= read -r f; do SOURCES+=("$f"); done < <(find "$SRC_DIR" -name '*.cpp' ! -name 'main.cpp' | sort)
SOURCES+=("$RAKUJS_DIR/rakupp_web.cpp")
echo "==> compiling ${#SOURCES[@]} translation units"

# Version string that src/main.cpp normally gets from CMake.
VERSION="$(grep -oE 'VERSION[[:space:]]+[0-9]+\.[0-9]+\.[0-9]+' "$ROOT_DIR/CMakeLists.txt" | head -1 | awk '{print $2}')"
VERSION="${VERSION:-0.0.0}"

mkdir -p "$OUT_DIR"

# Regenerate playground/examples.js from ../examples/*.raku — using rakupp itself
# (Raku.js generates its own playground data with the interpreter it ships).
# Find a native rakupp binary; $RAKUPP overrides. If none is built, skip with a
# note rather than fail — examples.js may already exist from a previous run.
RAKUPP_BIN="${RAKUPP:-}"
if [[ -z "$RAKUPP_BIN" ]]; then
  for c in "$ROOT_DIR/build/rakupp" "$ROOT_DIR/build-arm64/rakupp" "$ROOT_DIR/rakupp"; do
    [[ -x "$c" ]] && { RAKUPP_BIN="$c"; break; }
  done
fi
[[ -z "$RAKUPP_BIN" ]] && command -v rakupp >/dev/null 2>&1 && RAKUPP_BIN="$(command -v rakupp)"
if [[ -n "$RAKUPP_BIN" ]]; then
  echo "==> generating examples.js with $RAKUPP_BIN"
  "$RAKUPP_BIN" "$RAKUJS_DIR/gen-examples.raku"
else
  echo "==> WARNING: no rakupp binary found — skipping examples.js regeneration."
  echo "    Build the interpreter first (top-level README) or set RAKUPP=/path/to/rakupp."
fi

# _GNU_SOURCE: the recursion guard uses pthread_getattr_np (a GNU/musl extension)
#   inside a graceful `if (... == 0)`; exposing it keeps that path compiling.
# -fexceptions: JavaScript-based C++ exception handling. The interpreter uses C++
#   exceptions heavily — both for errors (ParseError / RakuError) AND for control
#   flow (`last` / `next` / `redo`, and `when`/`succeed`). We deliberately do NOT
#   use -fwasm-exceptions: with emscripten 6.0.3 the Wasm-EH personality fails to
#   MATCH the interpreter's by-value control-exception catches (catch (LastEx&)
#   etc.), so `last`/`next`/`when` escape to std::terminate → `unreachable` trap.
#   Verified: with -fwasm-exceptions, `for {... last}` and `given/when` crash;
#   with -fexceptions they are correct. The cost is recursion depth (see below).
# -sSTACK_SIZE / -sINITIAL_MEMORY: kept small (16 MiB / 32 MiB) on purpose. They
#   do NOT control recursion depth: under -fexceptions every throwing call is routed
#   through a JS invoke_* trampoline, so C++ recursion consumes the *JS engine*
#   stack (which the app can't grow) and caps at a few hundred levels (~200) no
#   matter how big the WASM stack is — deeper recursion raises a host RangeError the
#   playground catches. So a large WASM stack/heap buys nothing but a bigger
#   per-instance footprint; ALLOW_MEMORY_GROWTH covers the occasional heavier
#   program. Smaller instances instantiate faster and, crucially, keep the browser
#   tab from running low on memory when the playground recreates the worker on
#   Stop / restart / recursion-recovery (which was the cause of occasional
#   stuck-at-"loading" states that needed a page reload). The native build avoids
#   the recursion cap because an OS thread can have a 1 GiB stack; raising it in the
#   browser would need the interpreter on an explicit heap stack (a src change, out
#   of scope). -Oz beats -O2 here: smaller AND deeper (206 vs 174) — verified.
em++ \
  -std=c++17 "$OPT" \
  -D_GNU_SOURCE \
  -DRAKUPP_VERSION="\"$VERSION\"" \
  -I"$SRC_DIR" \
  -fexceptions \
  "${SOURCES[@]}" \
  -sSTACK_SIZE=16777216 \
  -sALLOW_MEMORY_GROWTH=1 \
  -sINITIAL_MEMORY=33554432 \
  -sMODULARIZE=1 \
  -sEXPORT_NAME=RakuJS \
  -sENVIRONMENT="$RAKUJS_ENV" \
  -sINVOKE_RUN=0 \
  -sEXIT_RUNTIME=0 \
  -sERROR_ON_UNDEFINED_SYMBOLS=0 \
  -sEXPORTED_FUNCTIONS='["_rakupp_run","_rakupp_highlight","_rakupp_version","_malloc","_free"]' \
  -sEXPORTED_RUNTIME_METHODS='["ccall","cwrap","UTF8ToString"]' \
  ${EXTRA_S[@]+"${EXTRA_S[@]}"} \
  -o "$OUT_DIR/rakujs.js"

echo "==> built:"
ls -lh "$OUT_DIR/rakujs.js" "$OUT_DIR/rakujs.wasm"
echo "==> serve it:  cd $OUT_DIR && python3 -m http.server 8000  →  http://localhost:8000/"
