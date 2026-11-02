#!/bin/zsh
# tm.sh label cmd... : best wall time of 3 runs, plus the program's output
best=999999; out=""
for r in 1 2 3; do
  s=$(perl -MTime::HiRes=time -e 'print time')
  out=$("${@:2}" 2>&1 | tail -1)
  e=$(perl -MTime::HiRes=time -e 'print time')
  t=$(perl -e "printf '%.3f', $e-$s")
  if (( t < best )); then best=$t; fi
done
printf "%-22s %8.3f s   %s\n" "$1" $best "$out"
