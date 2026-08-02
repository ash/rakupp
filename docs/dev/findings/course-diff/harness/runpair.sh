#!/bin/sh
# runpair.sh <snippet.raku>  →  ID<TAB>VERDICT<TAB>rakudo_rc<TAB>rakupp_rc
F="$1"; ID=$(basename "$F" .raku)
S=/private/tmp/claude-501/-Users-ash-raku-course/87a9e944-c2e4-4816-9563-0e333d860791/scratchpad
TMO="$S/timeout.sh"; RAKUPP=/Users/ash/raku++/build-arm64/rakupp; RES="$S/results"
export TZ=UTC
D=$(mktemp -d) || exit 1
cp "$F" "$D/s.raku"; cd "$D" || exit 1
"$TMO" 12 raku    s.raku </dev/null >r1.out 2>/dev/null; rc1=$?
"$TMO" 12 raku    s.raku </dev/null >r2.out 2>/dev/null; rc2=$?
"$TMO" 12 "$RAKUPP" s.raku </dev/null >p.out 2>/dev/null; pc=$?
V=""
if [ "$rc1" = 124 ]; then V=RAKUDO-TIMEOUT
elif ! cmp -s r1.out r2.out || [ "$rc1" != "$rc2" ]; then V=NONDET
elif [ "$pc" = 124 ]; then V=RAKUPP-TIMEOUT
elif cmp -s r1.out p.out && [ "$rc1" = "$pc" ]; then V=MATCH
elif cmp -s r1.out p.out; then V=DIFF-EXIT
elif [ "$rc1" = "$pc" ]; then V=DIFF-OUT
else V=DIFF-BOTH
fi
if [ "$V" != MATCH ] && [ "$V" != RAKUDO-TIMEOUT ] && [ "$V" != NONDET ]; then
  mkdir -p "$RES"
  { echo "=== SOURCE ($ID) ==="; cat "$F"
    echo "=== RAKUDO rc=$rc1 ==="; cat r1.out
    echo "=== RAKUPP rc=$pc ==="; cat p.out; } > "$RES/$ID.txt"
fi
cd /; rm -rf "$D"
printf '%s\t%s\t%s\t%s\n' "$ID" "$V" "$rc1" "$pc"
