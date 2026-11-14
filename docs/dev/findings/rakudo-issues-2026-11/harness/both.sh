#!/bin/zsh
# usage: both.sh FILE — each line of FILE is one -e program; prints both engines' output, 5 s limit each
F=${1:A}; cd "${0:A:h}/../run" || exit 1
while IFS= read -r code; do
  [[ -z "$code" || "$code" == \#* ]] && continue
  print -r -- "== $code"
  for e in pp rk; do
    if [[ $e == pp ]]; then bin=/Users/ash/rakupp/build/rakupp; else bin=/Users/ash/.rakubrew/versions/moar-2026.09/install/bin/rakudo; fi
    out=$(perl -e 'alarm 5; exec @ARGV' $bin -e "$code" 2>&1 </dev/null | head -4 | tr '\n' '␤' | cut -c1-220)
    print -r -- "  $e: $out"
  done
done < "$F"
