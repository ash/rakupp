#!/bin/sh
# run-one.sh <relpath> — run a corpus program twice under Rakudo in a sandbox
# copy of its directory; write expected output + status row.
set -u
f="$1"                                  # e.g. ./books/using-raku/palindrome.pl
SCRATCH=/private/tmp/claude-501/-Users-ash-raku--/2b055e07-82bb-413d-911f-4d5f80a1598e/scratchpad
CORPUS=$SCRATCH/raku-corpus
EXP=$SCRATCH/expected
rel=${f#./}

# Sandbox unit: whole program root for programs/*, else the file's directory.
case "$rel" in
  programs/*) unit=$(echo "$rel" | cut -d/ -f1-2);;
  *)          unit=$(dirname "$rel");;
esac
inunit=${rel#"$unit"}; inunit=${inunit#/}

run_once() {  # $1 = out file, $2 = err file; echoes "exit<TAB>ms"
  t0=$(perl -MTime::HiRes=time -e 'printf "%.0f", time()*1000')
  ( cd "$tmp/$(dirname "$inunit")" || exit 125
    b=$(basename "$inunit")
    case "$b" in
      *.sh) TZ=UTC perl -e 'alarm 30; exec "/bin/sh", $ARGV[0]' "$b" >"$1" 2>"$2" </dev/null;;
      *)    TZ=UTC perl -e 'alarm 30; exec "raku","-I.","-Ilib","-I../lib",$ARGV[0]' "$b" >"$1" 2>"$2" </dev/null;;
    esac
  )
  ec=$?
  t1=$(perl -MTime::HiRes=time -e 'printf "%.0f", time()*1000')
  printf '%s\t%s' "$ec" $((t1-t0))
}

mkdir -p "$EXP/$(dirname "$rel")"
o1=$(mktemp); e1=$(mktemp); o2=$(mktemp); e2=$(mktemp)

tmp=$(mktemp -d); cp -R "$CORPUS/$unit/." "$tmp/"
r1=$(run_once "$o1" "$e1"); rm -rf "$tmp"
tmp=$(mktemp -d); cp -R "$CORPUS/$unit/." "$tmp/"
r2=$(run_once "$o2" "$e2"); rm -rf "$tmp"

ec1=$(printf '%s' "$r1" | cut -f1); ms1=$(printf '%s' "$r1" | cut -f2)
ec2=$(printf '%s' "$r2" | cut -f1)

flags=""
if [ "$ec1" -eq 142 ]; then status=TIMEOUT
elif ! cmp -s "$o1" "$o2" || [ "$ec1" != "$ec2" ]; then status=NONDET
elif [ "$ec1" -ne 0 ]; then status=NONZERO
else status=OK
fi

# Source-level nondeterminism hints (things a same-second double run can't catch)
src=$CORPUS/$rel
if grep -qE '\brand\b|\.rand|\bsrand\b|\.pick\b|\.roll\b|\bnow\b|DateTime\.now|Date\.today|\bsleep\b|\bstart\b|\bawait\b|Promise|Supply|Channel|\$\*PID' "$src" 2>/dev/null; then
  flags="src-hint"
fi
[ -s "$e1" ] && flags="$flags,stderr"
flags=${flags#,}

if [ "$status" = OK ] || [ "$status" = NONZERO ]; then
  cp "$o1" "$EXP/$rel.out"
  [ -s "$e1" ] && cp "$e1" "$EXP/$rel.err"
fi
printf '%s\t%s\t%s\t%s\t%s\n' "$status" "$ec1" "$ms1" "${flags:--}" "$f" > "$EXP/$rel.status"
rm -f "$o1" "$e1" "$o2" "$e2"
exit 0
