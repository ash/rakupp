#!/bin/sh
# Run the same jsonreq commands under Rakudo and under Raku++ and diff STDOUT
# byte-for-byte — the modinfo compare, for the JSON client. A rakus instance
# (the static-server showcase) serves this directory's sample/ folder, so the
# whole check is three showcases and two of our modules talking to each other,
# with no network beyond the loopback.
#
#   RAKUPP=../../build/rakupp sh compare.sh
#
# JSON::Native and HTTP::Simple are not installed by this script: point
# RAKU_MODULES at a github.com/ash/raku-modules checkout (default ~/raku-modules),
# or set RAKULIB yourself, or `zef install` them once they are on fez and leave
# both unset.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
START=$(pwd)
cd "$HERE"

R="${RAKUPP:?set RAKUPP=<path to the rakupp binary>}"
# RAKUPP=build/rakupp from the repo root is the natural spelling — resolve it
# against the CALLER's directory, since everything below runs from $HERE
case "$R" in /*) ;; */*) R="$START/$R" ;; esac
RAKU="${RAKU:-raku}"
PORT="${PORT:-8123}"

# Both engines accept a comma-separated RAKULIB, so one setting serves both.
if [ -z "$RAKULIB" ]; then
    RAKU_MODULES="${RAKU_MODULES:-$HOME/raku-modules}"
    RAKULIB="$RAKU_MODULES/JSON-Native/lib,$RAKU_MODULES/HTTP-Simple/lib"
fi
export RAKULIB

# The test server: rakus serving sample/, torn down when the script ends.
"$R" ../rakus/rakus.raku "$PORT" sample >/dev/null 2>&1 &
SERVER=$!
# `|| true`: under set -e a failing kill in the EXIT trap would REPLACE the
# script's own exit status — a byte-identical MATCH then reported failure
trap 'kill $SERVER 2>/dev/null || true' EXIT
i=0
until nc -z 127.0.0.1 "$PORT" 2>/dev/null; do
    i=$((i + 1)); [ "$i" -gt 50 ] && { echo "rakus did not come up on :$PORT"; exit 1; }
    sleep 0.1
done
# The port answering is not enough: with SO_REUSEADDR a squatter and our
# server can coexist on :$PORT and the squatter may win the accept — every
# request then compares identical 404s and the MATCH means nothing. So probe
# IDENTITY, not liveness: only sample/users.json answers ada here.
probe=$("$R" "$HERE"/jsonreq.raku "http://127.0.0.1:$PORT/users.json" --query=.users[0].login -r 2>/dev/null || true)
[ "$probe" = "ada" ] || {
    echo "the server on :$PORT is not serving sample/ — is something else holding the port? (set PORT= to move)"
    exit 1
}

BASE="http://127.0.0.1:$PORT"

# Object-printing cases pass --sorted: Rakudo's hash order is randomized per
# process, so unsorted pretty output is SUPPOSED to differ between runs, let
# alone engines. The 404/405 cases print the server's HTML error body verbatim
# and exit 1 — captured, because that behaviour is part of the contract too.
# The last two cases skip the server entirely: a bare path is a local document.
run_all() {
    engine=$1
    for cmd in \
        "$BASE/users.json --sorted" \
        "$BASE/users.json --compact --sorted" \
        "$BASE/users.json --query=.count" \
        "$BASE/users.json --query=.page" \
        "$BASE/users.json --query=.users[0].name -r" \
        "$BASE/users.json --query=.users[-1].karma" \
        "$BASE/users.json --query=.users[1].languages --compact" \
        "$BASE/users.json --query=.users[2].active" \
        "POST $BASE/users.json --json={\"probe\":true}" \
        "$BASE/nope.json" \
        "sample/users.json --sorted --compact" \
        "sample/users.json --query=.users[1].login -r"
    do
        echo "===== jsonreq $cmd"
        st=0
        # shellcheck disable=SC2086
        $engine "$HERE"/jsonreq.raku $cmd 2>/dev/null || st=$?
        echo "(exit $st)"
    done
}

run_all "$RAKU" > /tmp/jsonreq-rakudo.out
run_all "$R"    > /tmp/jsonreq-rakupp.out

if diff -u /tmp/jsonreq-rakudo.out /tmp/jsonreq-rakupp.out > /tmp/jsonreq.diff; then
    echo "MATCH — byte-identical under Rakudo and Raku++ ($(wc -l < /tmp/jsonreq-rakudo.out) lines)"
    exit 0
fi
echo "DIFF — see /tmp/jsonreq.diff"
head -60 /tmp/jsonreq.diff
exit 1
