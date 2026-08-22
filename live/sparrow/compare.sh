#!/bin/sh
# Run the Sparrow6 scenario under Rakudo and under Raku++ and diff STDOUT.
# This is the claim the entry makes, made checkable.
#
#   sh compare.sh                          # uses `rakupp` and `raku` from PATH
#   RAKUPP=../../build/rakupp sh compare.sh # …or name a build tree explicitly
#
# Sparrow6 is NOT installed by this script. Install it first, with either
# installer — both write the same store:
#
#   rakupp install Sparrow6      # or: zef install Sparrow6
#
# Nothing else is needed; an installed dist resolves for both engines. (To run
# against an unpacked checkout instead, export a comma-separated RAKULIB with
# Sparrow6's `resources/` on it as well as its `lib/`.)
#
# Sparrow prefixes every task line with a wall-clock timestamp, so the two runs
# are supposed to differ there; the timestamps are normalised before the diff.
set -e
# Resolve the binary BEFORE cd-ing: the shim below needs an absolute path, and
# a RAKUPP given relative to wherever you invoked this from must still resolve.
# `command -v` handles both spellings — a bare name found on PATH, or a path.
R="${RAKUPP:-rakupp}"
R=$(command -v "$R") || { echo "no rakupp: put it on PATH or set RAKUPP=<path>" >&2; exit 1; }
case "$R" in /*) ;; *) R=$(cd "$(dirname "$R")" && pwd)/$(basename "$R") ;; esac
RAKU="${RAKU:-raku}"

HERE=$(cd "$(dirname "$0")" && pwd)
cd "$HERE"

# Sparrow6 spawns Raku tasks with the literal `raku` from PATH, so a Raku++ run
# that does not shim it still starts a Rakudo for every Raku task — most of the
# scenario's time, and the whole point of the comparison. See the README.
SHIM=$(mktemp -d)
printf '#!/bin/sh\nexec %s "$@"\n' "$R" > "$SHIM/raku"
chmod +x "$SHIM/raku"
trap 'rm -rf "$SHIM"' EXIT

strip_ts() { sed 's/[0-9][0-9]:[0-9][0-9]:[0-9][0-9]/HH:MM:SS/g'; }

"$RAKU" scenario.raku 2>/dev/null | strip_ts > /tmp/sparrow-rakudo.out
PATH="$SHIM:$PATH" "$R" scenario.raku 2>/dev/null | strip_ts > /tmp/sparrow-rakupp.out

if diff -u /tmp/sparrow-rakudo.out /tmp/sparrow-rakupp.out > /tmp/sparrow.diff; then
    echo "MATCH — identical under Rakudo and Raku++ ($(wc -l < /tmp/sparrow-rakudo.out) lines)"
    exit 0
fi
echo "DIFF — see /tmp/sparrow.diff"
head -60 /tmp/sparrow.diff
exit 1
