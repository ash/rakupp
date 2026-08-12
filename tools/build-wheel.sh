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

python3 -m pip install --quiet wheel setuptools >/dev/null 2>&1 || true
python3 -m pip wheel --no-deps --no-build-isolation -w "$OUT" "$PKG"
rm -rf "$STAGE"

WHL=$(ls "$OUT"/rakulang-*-py3-none-any.whl)
PLAT=$(python3 -c 'import sysconfig; print(sysconfig.get_platform().replace("-", "_").replace(".", "_"))')
python3 -m wheel tags --remove --platform-tag "$PLAT" "$WHL" >/dev/null
echo "built: $(ls "$OUT"/rakulang-*.whl)"
