#!/bin/sh
# Build a PLATFORM wheel of the Python binding with librakupp bundled inside,
# so `pip install rakulang-<...>.whl` works on a machine with no rakupp at
# all. The loader looks in the package's _lib/ first (_abi.py).
#
#   tools/build-wheel.sh <build-dir> [out-dir]     (out-dir default: dist-wheel)
#
# Run by release.yml on the macOS and Linux legs; the wheel is retagged from
# py3-none-any to this platform's tag because it carries a native library.
set -e

BUILD=${1:?usage: build-wheel.sh <build-dir> [out-dir]}
OUT=${2:-dist-wheel}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
PKG="$ROOT/bindings/python"

case "$(uname -s)" in
    Darwin) LIB=librakupp.dylib ;;
    *)      LIB=librakupp.so ;;
esac

STAGE="$PKG/rakulang/_lib"
rm -rf "$STAGE"
mkdir -p "$STAGE"
# resolve the symlink chain: bundle the real file under the plain name
cp -L "$BUILD/$LIB" "$STAGE/$LIB"

# A scratch venv, not the system interpreter: a PEP 668 "externally managed"
# python (Homebrew's, Debian's) refuses `pip install setuptools`, and the old
# `|| true` swallowed that refusal — the build then died in the backend with
# "Cannot import 'setuptools.build_meta'". Inside a venv pip always may, and
# the retag step's `wheel` CLI is guaranteed present too.
mkdir -p "$OUT"
VENV="$OUT/.buildvenv"
python3 -m venv "$VENV"
PY="$VENV/bin/python"
"$PY" -m pip install --quiet --upgrade pip setuptools wheel
"$PY" -m pip wheel --no-deps --no-build-isolation -w "$OUT" "$PKG"
rm -rf "$STAGE"

WHL=$(ls "$OUT"/rakulang-*-py3-none-any.whl)
PLAT=$("$PY" -c 'import sysconfig; print(sysconfig.get_platform().replace("-", "_").replace(".", "_"))')
"$PY" -m wheel tags --remove --platform-tag "$PLAT" "$WHL" >/dev/null
rm -rf "$VENV"
echo "built: $(ls "$OUT"/rakulang-*.whl)"
