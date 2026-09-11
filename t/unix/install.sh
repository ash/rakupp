#!/bin/sh
# The gate for tools/install.sh -- install, upgrade, uninstall.
#
#   sh t/unix/install.sh rakupp-macos-universal.tar.gz
#
# Drives the POSIX engine installer end to end against a local release archive
# and asserts what it did, in TAP. The POSIX sibling of t/windows/install.ps1,
# and it checks the same properties that one does:
#
#   * the engine it unpacked RUNS (6 * 7, not a file-exists check)
#   * --no-raku-alias leaves no `raku`, --raku-alias makes one that runs -- and
#     that is a COMPLETE engine, so `raku --exe` compiles through it
#   * the `raku` link is RELATIVE, which is what lets the prefix be moved
#   * a re-run keeps the name the install had, and adds no second startup line
#   * a startup file gains exactly one line, and every other byte of it comes
#     back unchanged -- the property the whole env-file design exists for
#   * the env file actually puts bin/ on PATH when a shell sources it
#   * the piped form works: under `curl ... | sh` the script IS standard input,
#     so a question that reads stdin would eat the rest of the installer
#   * --uninstall removes the prefix and restores the startup file byte for byte
#   * an unsupported platform prints the source build and fails, rather than
#     downloading something that cannot run
#
# Everything happens inside a temporary HOME, so unlike the Windows gate -- which
# really does edit HKCU while it runs -- this touches nothing on the machine.

set -eu

ARCHIVE=${1-}
INSTALLER=${2-}

HERE=$(cd "$(dirname "$0")" && pwd)
[ -n "$INSTALLER" ] || INSTALLER="$HERE/../../tools/install.sh"
[ -f "$INSTALLER" ] || { echo "no installer at $INSTALLER" >&2; exit 2; }
[ -n "$ARCHIVE" ] || { echo "usage: sh t/unix/install.sh <archive> [installer]" >&2; exit 2; }
[ -f "$ARCHIVE" ] || { echo "no archive at $ARCHIVE" >&2; exit 2; }
ARCHIVE=$(cd "$(dirname "$ARCHIVE")" && pwd)/$(basename "$ARCHIVE")

N=0
BAD=0
ok() { # <0|1> <what> [detail]
    N=$((N + 1))
    if [ "$1" = 1 ]; then
        echo "ok $N - $2"
    else
        BAD=$((BAD + 1))
        echo "not ok $N - $2"
        [ $# -gt 2 ] && [ -n "$3" ] && echo "# $3" || true
    fi
}
is() { # <got> <want> <what>
    if [ "$1" = "$2" ]; then ok 1 "$3"; else ok 0 "$3" "want [$2] got [$1]"; fi
}

ROOT=$(mktemp -d "${TMPDIR:-/tmp}/rakupp-gate.XXXXXX")
cleanup() { chmod -R u+w "$ROOT" 2>/dev/null || true; rm -rf "$ROOT"; }
trap cleanup EXIT INT TERM

HOME_DIR="$ROOT/home"
PREFIX="$ROOT/home/.rakupp"
PREFIX2="$ROOT/home/.rakupp2"
BIN="$PREFIX/bin"
EXE="$BIN/rakupp"
ALIAS="$BIN/raku"
mkdir -p "$HOME_DIR"
echo "# prefix $PREFIX"

# A startup file with content of its own on BOTH sides of where the installer
# will write, so a run that rewrites the file rather than appending one line is
# caught, and so is an uninstall that takes a neighbour with it.
PROFILE="$HOME_DIR/.profile"
printf '# gate: line before\nexport RAKUPP_GATE_BEFORE=1\n' >  "$PROFILE"
printf '# gate: line after\nexport RAKUPP_GATE_AFTER=1\n'   >> "$PROFILE"
cp "$PROFILE" "$ROOT/profile.orig"

# SHELL decides which startup files the installer writes to; pin it so the
# gate asserts against a known set rather than whatever runs CI.
run_installer() { # args...
    env -i HOME="$HOME_DIR" PATH="$PATH" SHELL=/bin/sh TMPDIR="$ROOT" \
        sh "$INSTALLER" "$@" > "$ROOT/out" 2>&1 && RC=0 || RC=$?
    sed 's/^/# /' < "$ROOT/out"
    return 0
}

answer() { # exe -> what it says for 6 * 7
    "$1" -e 'print 6 * 7' 2>/dev/null || true
}

lines_matching() { # file pattern -> count
    # `grep -c` prints the count AND exits 1 when the count is zero, so a
    # `|| echo 0` fallback appends a SECOND zero and every comparison against
    # an expected 0 fails. Take the output and let the status go.
    [ -f "$1" ] || { echo 0; return; }
    _c=$(grep -cF "$2" "$1" 2>/dev/null || true)
    [ -n "$_c" ] || _c=0
    echo "$_c"
}

# ---- install, without the second name --------------------------------------
run_installer --archive "$ARCHIVE" --dir "$PREFIX" --no-raku-alias --yes
is "$RC" 0 'installer exits 0'
[ -x "$EXE" ] && ok 1 'bin/rakupp is there' || ok 0 'bin/rakupp is there'
is "$(answer "$EXE")" 42 'the installed rakupp runs'
[ -e "$ALIAS" ] && ok 0 '--no-raku-alias leaves no raku' || ok 1 '--no-raku-alias leaves no raku'
[ -f "$PREFIX/rakupp-install.json" ] && ok 1 'the install left a receipt' || ok 0 'the install left a receipt'
is "$(lines_matching "$PROFILE" '# rakupp')" 1 'the startup file gained exactly one line'

# The env file has to WORK, not merely exist: sourcing it is what a new shell
# does, and it is the only thing standing between the install and a PATH that
# never mentions it.
SOURCED=$(env -i HOME="$HOME_DIR" PATH=/usr/bin:/bin sh -c '. "$HOME/.rakupp/env"; command -v rakupp' 2>/dev/null || true)
is "$SOURCED" "$EXE" 'sourcing the env file puts bin/ on PATH'

# ---- upgrade, asking for the second name -----------------------------------
run_installer --archive "$ARCHIVE" --dir "$PREFIX" --raku-alias --yes
is "$RC" 0 'a re-run over an existing install exits 0'
is "$(answer "$ALIAS")" 42 'raku runs, and finds the runtime beside it'
is "$(lines_matching "$PROFILE" '# rakupp')" 1 'the re-run did not add a second line'

# A RELATIVE link, so the prefix can be moved afterwards. An absolute one works
# until someone renames a directory above it, and then silently runs whatever
# is at the old path -- or nothing.
LINK=$(readlink "$ALIAS" 2>/dev/null || echo '(not a symlink)')
is "$LINK" rakupp 'the raku name is a relative symlink to rakupp'

# A second name is a complete engine, not a stub: --exe has to find the runtime
# through it. This is the check that would fail if rakupp ever resolved its
# home from argv[0] instead of from its real executable path.
( cd "$ROOT" && echo 'say "gate-exe-ok"' > g.raku && "$ALIAS" --exe g.raku -o g >/dev/null 2>&1 ) || true
is "$( [ -x "$ROOT/g" ] && "$ROOT/g" 2>/dev/null || echo FAILED)" gate-exe-ok 'raku --exe compiles and the binary runs'

# Moving the prefix must not break the name -- that is what relative buys.
mv "$PREFIX" "$ROOT/moved"
is "$(answer "$ROOT/moved/bin/raku")" 42 'the raku name survives a moved prefix'
mv "$ROOT/moved" "$PREFIX"

# ---- a run with no say either way keeps the name it had ---------------------
run_installer --archive "$ARCHIVE" --dir "$PREFIX" --yes
is "$RC" 0 'a third run exits 0'
is "$(answer "$ALIAS")" 42 'an upgrade keeps the raku name it had'

# ---- the piped form, which is how the one-liner actually arrives ------------
# `curl ... | sh` makes the SCRIPT standard input. A prompt that read stdin
# would consume the rest of the installer and run half a program.
cat "$INSTALLER" | env -i HOME="$HOME_DIR" PATH="$PATH" SHELL=/bin/sh TMPDIR="$ROOT" \
    sh -s -- --archive "$ARCHIVE" --dir "$PREFIX2" --raku-alias --no-path --yes \
    > "$ROOT/out2" 2>&1 && RC=0 || RC=$?
sed 's/^/# /' < "$ROOT/out2"
is "$RC" 0 'the piped form exits 0'
is "$(answer "$PREFIX2/bin/raku")" 42 'the piped form installs a working raku'
is "$(lines_matching "$PROFILE" '# rakupp')" 1 '--no-path left the startup file alone'

# ---- uninstall, through the copy the install left in the prefix -------------
# Someone who installed through `curl | sh` has no file of their own, so that
# copy IS the documented undo command -- run it, do not merely look for it.
COPY="$PREFIX/install.sh"
[ -f "$COPY" ] && ok 1 'the install left a copy of itself in the prefix' \
               || ok 0 'the install left a copy of itself in the prefix'
UNDO=$INSTALLER
[ -f "$COPY" ] && UNDO=$COPY
env -i HOME="$HOME_DIR" PATH="$PATH" SHELL=/bin/sh sh "$UNDO" --dir "$PREFIX" --uninstall --yes \
    > "$ROOT/out3" 2>&1 && RC=0 || RC=$?
sed 's/^/# /' < "$ROOT/out3"
is "$RC" 0 'uninstall exits 0'
[ -e "$PREFIX" ] && ok 0 'uninstall removed the prefix' || ok 1 'uninstall removed the prefix'
is "$(lines_matching "$PROFILE" '# rakupp')" 0 'uninstall removed the startup line'
if diff -q "$ROOT/profile.orig" "$PROFILE" >/dev/null 2>&1
    then ok 1 'the startup file came back byte for byte'
    else ok 0 'the startup file came back byte for byte' "$(diff "$ROOT/profile.orig" "$PROFILE" | head -5)"
fi

# ---- a platform with no archive --------------------------------------------
# It must say so and fail, not download something that cannot run. A uname of
# our own on PATH is the whole fixture.
mkdir -p "$ROOT/fakebin"
printf '#!/bin/sh\ncase "$1" in -s) echo Plan9 ;; -m) echo vax ;; esac\n' > "$ROOT/fakebin/uname"
chmod +x "$ROOT/fakebin/uname"
env -i HOME="$ROOT/home3" PATH="$ROOT/fakebin:/usr/bin:/bin" sh "$INSTALLER" --yes \
    > "$ROOT/out4" 2>&1 && RC=0 || RC=$?
sed 's/^/# /' < "$ROOT/out4"
[ "$RC" != 0 ] && ok 1 'an unsupported platform fails' || ok 0 'an unsupported platform fails' "exit $RC"
if grep -q 'cmake' "$ROOT/out4"
    then ok 1 'and it prints the source build instead'
    else ok 0 'and it prints the source build instead' "$(head -3 "$ROOT/out4")"
fi

echo "1..$N"
if [ "$BAD" -gt 0 ]; then echo "# $BAD of $N checks failed"; exit 1; fi
echo "# all $N checks passed"
exit 0
