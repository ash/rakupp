#!/bin/sh
# Time the scenario in the three configurations the README tabulates, and print
# the table. Best-of-N wall clock; every configuration is warmed first, because
# Rakudo's very first run compiles Sparrow6's module graph and takes tens of
# seconds — a real cost, but a one-off, and not what this is measuring.
#
#   sh bench.sh                  # uses `rakupp` and `raku` from PATH, 3 runs
#   RUNS=5 sh bench.sh           # more runs
#   RAKUPP=./build/rakupp sh bench.sh
#
# Install Sparrow6 first (see README). The three configurations are:
#
#   rakudo    raku runs the scenario, and `raku` runs the Raku tasks
#   mixed     rakupp runs the scenario, but Sparrow6 still spawns `raku` for
#             the Raku tasks — because it builds that command with a LITERAL
#             `raku`. This is what you get by just switching interpreters.
#   rakupp    rakupp runs the scenario AND the Raku tasks, via a `raku` shim on
#             PATH pointing at rakupp. This is the configuration to compare.
set -e

R="${RAKUPP:-rakupp}"
R=$(command -v "$R") || { echo "no rakupp: put it on PATH or set RAKUPP=<path>" >&2; exit 1; }
case "$R" in /*) ;; *) R=$(cd "$(dirname "$R")" && pwd)/$(basename "$R") ;; esac
RAKU="${RAKU:-raku}"
RUNS="${RUNS:-3}"

HERE=$(cd "$(dirname "$0")" && pwd)
cd "$HERE"

SHIM=$(mktemp -d)
printf '#!/bin/sh\nexec %s "$@"\n' "$R" > "$SHIM/raku"
chmod +x "$SHIM/raku"
trap 'rm -rf "$SHIM"' EXIT

# perl is already a hard requirement here (tasks/perl-check/task.pl), so it is
# a fair stopwatch — `date +%s%N` is not portable to macOS.
now_ms() { perl -MTime::HiRes=time -e 'printf "%.0f\n", time()*1000'; }

best_ms() {
    best=
    n=0
    while [ "$n" -lt "$RUNS" ]; do
        t0=$(now_ms)
        "$@" >/dev/null 2>&1 || true
        t1=$(now_ms)
        d=$((t1 - t0))
        if [ -z "$best" ] || [ "$d" -lt "$best" ]; then best=$d; fi
        n=$((n + 1))
    done
    echo "$best"
}

warm() { "$@" >/dev/null 2>&1 || true; }

echo "warming (Rakudo's first run compiles the module graph — this can take a while)"
warm "$RAKU" scenario.raku
warm "$R" scenario.raku
warm env PATH="$SHIM:$PATH" "$R" scenario.raku

echo "timing, best of $RUNS"
rakudo=$(best_ms "$RAKU" scenario.raku)
mixed=$(best_ms "$R" scenario.raku)
rakupp=$(best_ms env PATH="$SHIM:$PATH" "$R" scenario.raku)

echo
echo "| configuration                                      | wall clock |"
echo "|----------------------------------------------------|-----------:|"
printf "| Rakudo throughout                                  | %6s ms |\n" "$rakudo"
printf "| Raku++ scenario, Raku tasks still spawning \`raku\`  | %6s ms |\n" "$mixed"
printf "| Raku++ all the way down                            | %6s ms |\n" "$rakupp"
echo
echo "(best of $RUNS, warm; absolute numbers are machine-specific — the shape is the point)"
