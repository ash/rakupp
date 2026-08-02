#!/bin/sh
# run-rakupp.sh <relpath> — run one corpus program under rakupp in a sandbox
# and diff stdout+exit against the Rakudo reference in expected/.
set -u
f="$1"
SCRATCH=/private/tmp/claude-501/-Users-ash-raku--/2b055e07-82bb-413d-911f-4d5f80a1598e/scratchpad
CORPUS=$SCRATCH/raku-corpus
RP=/Users/ash/raku++/build/rakupp
RES=$SCRATCH/rp-results
rel=${f#./}

case "$rel" in
  programs/*) unit=$(echo "$rel" | cut -d/ -f1-2);;
  *)          unit=$(dirname "$rel");;
esac
inunit=${rel#"$unit"}; inunit=${inunit#/}

o=$(mktemp); e=$(mktemp)
tmp=$(mktemp -d); cp -R "$CORPUS/$unit/." "$tmp/"
t0=$(perl -MTime::HiRes=time -e 'printf "%.0f", time()*1000')
( cd "$tmp/$(dirname "$inunit")" || exit 125
  b=$(basename "$inunit")
  case "$b" in
    *.sh) PATH="$SCRATCH/shim:$PATH" TZ=UTC perl -e 'alarm 30; exec "/bin/sh", $ARGV[0]' "$b" >"$o" 2>"$e" </dev/null;;
    *)    RP="$RP" TZ=UTC perl -e 'alarm 30; exec $ENV{RP},"-I.","-Ilib","-I../lib",$ARGV[0]' "$b" >"$o" 2>"$e" </dev/null;;
  esac
)
ec=$?
t1=$(perl -MTime::HiRes=time -e 'printf "%.0f", time()*1000')
rm -rf "$tmp"

exp_out=$SCRATCH/expected/$rel.out
exp_ec=$(awk -F'\t' -v F="$f" '$5==F {print $2}' "$CORPUS/run-status.tsv")

if [ "$ec" -eq 142 ]; then verdict=TIMEOUT
elif cmp -s "$o" "$exp_out"; then
  if [ "$ec" = "$exp_ec" ]; then verdict=MATCH; else verdict=DIFF-EXIT; fi
else
  if [ "$ec" = "$exp_ec" ]; then verdict=DIFF-OUT; else verdict=DIFF-BOTH; fi
fi

mkdir -p "$RES/$(dirname "$rel")"
if [ "$verdict" != MATCH ]; then
  cp "$o" "$RES/$rel.out"; [ -s "$e" ] && cp "$e" "$RES/$rel.err"
fi
printf '%s\t%s\t%s\t%s\t%s\n' "$verdict" "$ec" "$exp_ec" $((t1-t0)) "$f" > "$RES/$rel.verdict"
rm -f "$o" "$e"
exit 0
