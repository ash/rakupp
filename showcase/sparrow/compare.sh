#!/bin/sh
# Run the Sparrow6 scenario under Rakudo and under Raku++ and diff STDOUT.
# This is the claim the showcase makes, made checkable.
#
#   RAKUPP=../../build/rakupp sh compare.sh
#
# Sparrow6 and its six dependencies are NOT installed by this script: point
# SP6LIB at a comma-separated list of `lib` directories holding them (or
# `zef install Sparrow6`, in which case SP6LIB can be left unset).
#
# Sparrow prefixes every task line with a wall-clock timestamp, so the two runs
# are supposed to differ there; the timestamps are normalised before the diff.
set -e
# Absolutise the binary BEFORE cd-ing: the shim needs an absolute path, and a
# RAKUPP given relative to wherever you invoked this from must still resolve.
R="${RAKUPP:?set RAKUPP=<path to the rakupp binary>}"
R=$(cd "$(dirname "$R")" && pwd)/$(basename "$R")
RAKU="${RAKU:-raku}"

HERE=$(cd "$(dirname "$0")" && pwd)
cd "$HERE"

if [ -n "$SP6LIB" ]; then
    RAKULIB="$SP6LIB"; export RAKULIB
fi

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
