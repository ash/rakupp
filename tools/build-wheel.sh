#!/bin/sh
# Build a PLATFORM wheel of the Python binding with librakupp bundled inside,
# so `pip install rakulang-<...>.whl` works on a machine with no rakupp at
# all. The loader looks in the package's _lib/ first (_abi.py).
#
#   tools/build-wheel.sh <build-dir> [out-dir]     (out-dir default: dist-wheel)
#
# Run by release.yml on the macOS, Linux and Windows (MSVC) legs — on Windows
# under Git Bash, with <build-dir> the multi-config output directory that
# holds rakupp.dll (build/Release). A wheel's file name is a promise pip
# enforces, so three things are read off the library itself rather than
# assumed:
#
#   - the version: pyproject.toml must say what the library's rk_version()
#     says (a .postN suffix is allowed — a binding-only fix on the same
#     engine), or the build stops;
#   - the platform tag: on Linux manylinux_<glibc floor>_<arch>, the floor
#     being the newest GLIBC_ symbol version the .so needs (PEP 600); on macOS
#     macosx_<minimum OS>_<arch> from the library's LC_BUILD_VERSION and the
#     architectures in the file (universal2 when it holds both); on Windows
#     win_amd64 / win_arm64 / win32 from the PE header's machine field (a
#     Windows wheel names no OS floor — the DLL carries its own C runtime);
#   - the license text: bindings/python/LICENSE must be the byte copy of the
#     repository's LICENSE that the wheel carries.
#
# `twine check` runs last: it renders README.md the way PyPI will, so a
# description that would not display is caught here, not after the upload.
set -e

BUILD=${1:?usage: build-wheel.sh <build-dir> [out-dir]}
OUT=${2:-dist-wheel}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
PKG="$ROOT/bindings/python"

OS=$(uname -s)
case "$OS" in
    Darwin)               LIB=librakupp.dylib ;;
    MINGW*|MSYS*|CYGWIN*) LIB=rakupp.dll; OS=Windows ;;
    *)                    LIB=librakupp.so ;;
esac
SRC="$BUILD/$LIB"
if [ ! -e "$SRC" ]; then
    echo "build-wheel: $SRC not found (configure with -DRAKUPP_BUILD_SHARED=ON)" >&2
    exit 1
fi

# The interpreter that builds the wheel. PYTHON= names one outright. On
# Windows the name `python3` can be the Microsoft Store's stub rather than an
# interpreter, so the plain name is tried first there.
if [ -z "$PYTHON" ]; then
    if [ "$OS" = Windows ]; then
        PYTHON=$(command -v python || command -v python3 || true)
    else
        PYTHON=$(command -v python3 || command -v python || true)
    fi
fi
if [ -z "$PYTHON" ]; then
    echo "build-wheel: no python on PATH (set PYTHON=)" >&2
    exit 1
fi

if ! cmp -s "$ROOT/LICENSE" "$PKG/LICENSE"; then
    echo "build-wheel: bindings/python/LICENSE differs from LICENSE: copy it over" >&2
    exit 1
fi

# The version the wheel will claim, against the version the library reports.
WANT=$("$PYTHON" -c 'import re, sys
print(re.search(r"^version\s*=\s*\"([^\"]+)\"", open(sys.argv[1]).read(), re.M).group(1))' "$PKG/pyproject.toml")
HAVE=$("$PYTHON" -c 'import ctypes, os, sys
lib = ctypes.CDLL(os.path.abspath(sys.argv[1])); lib.rk_version.restype = ctypes.c_char_p
print(lib.rk_version().decode())' "$SRC")
case "$WANT" in
    "$HAVE"|"$HAVE".post*) ;;
    *)  echo "build-wheel: pyproject.toml says $WANT but $SRC is rakupp $HAVE:" \
             "bump version in bindings/python/pyproject.toml" >&2
        exit 1 ;;
esac

# The platform tag, from the library.
ARCH=$(uname -m)
case "$OS" in
    Darwin)
        MINOS=$(otool -l "$SRC" | awk '/LC_BUILD_VERSION/{f=1} f && /minos/{print $2; exit}')
        # a library built with an older toolchain records the floor differently
        [ -n "$MINOS" ] || MINOS=$(otool -l "$SRC" | awk '/LC_VERSION_MIN_MACOSX/{f=1} f && /version/{print $2; exit}')
        MINOS=$(echo "$MINOS" | cut -d. -f1,2 | tr . _)
        ARCHS=$(lipo -archs "$SRC")
        case "$ARCHS" in
            *arm64*x86_64*|*x86_64*arm64*) ARCH=universal2 ;;
            *) ARCH=$ARCHS ;;
        esac
        PLAT="macosx_${MINOS}_${ARCH}"
        ;;
    Windows)
        # The PE header's Machine field: IMAGE_FILE_MACHINE_AMD64 / ARM64 / I386.
        PLAT=$("$PYTHON" -c 'import struct, sys
with open(sys.argv[1], "rb") as f:
    f.seek(0x3C); (off,) = struct.unpack("<I", f.read(4))
    f.seek(off); sig = f.read(4); (machine,) = struct.unpack("<H", f.read(2))
if sig != b"PE\0\0": sys.exit(sys.argv[1] + ": not a PE image")
tags = {0x8664: "win_amd64", 0xAA64: "win_arm64", 0x14C: "win32"}
if machine not in tags: sys.exit("%s: unknown PE machine 0x%X" % (sys.argv[1], machine))
print(tags[machine])' "$SRC")
        ;;
    *)
        GLIBC=$(objdump -T "$SRC" | grep -o 'GLIBC_[0-9]*\.[0-9]*' | sed 's/GLIBC_//' \
                | sort -t. -k1,1n -k2,2n | tail -1)
        if [ -z "$GLIBC" ]; then
            echo "build-wheel: no GLIBC_ symbol versions in $SRC; cannot name a manylinux floor" >&2
            exit 1
        fi
        PLAT="manylinux_$(echo "$GLIBC" | tr . _)_${ARCH}"
        ;;
esac
echo "build-wheel: $LIB is rakupp $HAVE, platform tag $PLAT"

STAGE="$PKG/rakulang/_lib"
trap 'rm -rf "$STAGE"' EXIT
rm -rf "$STAGE"
mkdir -p "$STAGE"
# resolve the symlink chain: bundle the real file under the plain name
cp -L "$SRC" "$STAGE/$LIB"

# A scratch venv, not the system interpreter: a PEP 668 "externally managed"
# python (Homebrew's, Debian's) refuses `pip install setuptools`, and the old
# `|| true` swallowed that refusal — the build then died in the backend with
# "Cannot import 'setuptools.build_meta'". Inside a venv pip always may, and
# the retag step's `wheel` CLI is guaranteed present too.
mkdir -p "$OUT"
VENV="$OUT/.buildvenv"
"$PYTHON" -m venv "$VENV"
if [ "$OS" = Windows ]; then
    PY="$VENV/Scripts/python.exe"      # a venv on Windows has Scripts/, not bin/
else
    PY="$VENV/bin/python"
fi
"$PY" -m pip install --quiet --upgrade pip setuptools wheel twine
"$PY" -m pip wheel --no-deps --no-build-isolation -w "$OUT" "$PKG"
rm -rf "$STAGE"

WHL=$(ls "$OUT"/rakulang-*-py3-none-any.whl)
"$PY" -m wheel tags --remove --platform-tag "$PLAT" "$WHL" >/dev/null
"$PY" -m twine check --strict "$OUT"/rakulang-*.whl
rm -rf "$VENV"
echo "built: $(ls "$OUT"/rakulang-*.whl)"
