#!/bin/zsh
# tiers.sh kernel S : best of 3 ns/op under each JSC tier configuration
k=$1; S=$2
for cfg in "LLInt:BUN_JSC_useJIT=false" "Baseline:BUN_JSC_useDFGJIT=false" "DFG:BUN_JSC_useFTLJIT=false" "FTL:X=1"; do
  name=${cfg%%:*}; e=${cfg#*:}
  best=999999; res=""
  for r in 1 2 3; do
    line=$(env $e ${BUN:-bun} ks.js $k $S)
    t=${${(s: :)line}[2]}; res=${${(s: :)line}[3]}
    if (( t < best )); then best=$t; fi
  done
  printf "%-8s %-9s %10.3f   %s\n" $k $name $best $res
done
