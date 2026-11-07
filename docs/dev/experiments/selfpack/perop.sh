#!/bin/zsh
# perop.sh [RAKUPP] : ns per operation of perf-review-2026-11's dot, matmul and
# stencil kernels, untyped, with self-packing off (RAKUPP_NO_SELFPACK=1) and on,
# interpreted and as --exe -O binaries. Best of 3 wall times less start-up; the
# packed runs are 8-125x longer, so start-up does not swamp them.
here=${0:A:h}
RK=${1:-$here/../../../../build/rakupp}
K=$here/../../findings/perf-review-2026-11
T=$(mktemp -d)
trap 'rm -rf $T' EXIT
echo 'say 1;' > $T/empty.raku
cp $K/dot.raku $K/matmul.raku $K/stencil.raku $T/
sed 's/for \^5000 {/for ^50000 {/'  $K/dot.raku > $T/dot10.raku
sed 's/for \^5000 {/for ^500000 {/' $K/dot.raku > $T/dot100.raku
sed 's/my \$n = 120;/my $n = 240;/' $K/matmul.raku > $T/matmul240.raku
sed 's/my \$n = 120;/my $n = 600;/' $K/matmul.raku > $T/matmul600.raku
sed 's/for \^400 {/for ^4000 {/'   $K/stencil.raku > $T/stencil10.raku
sed 's/for \^400 {/for ^40000 {/'  $K/stencil.raku > $T/stencil100.raku
for f in empty dot matmul stencil dot100 matmul600 stencil100; do
  $RK --exe -O $T/$f.raku -o $T/$f.O > /dev/null 2>&1 || echo "build failed: $f"
done
t3() {   # t3 ENV cmd… : best wall time of 3 (s)
  local best=999999 s e t
  for r in 1 2 3; do
    s=$(perl -MTime::HiRes=time -e 'print time')
    env $1 "${@:2}" > /dev/null 2>&1
    e=$(perl -MTime::HiRes=time -e 'print time')
    t=$(perl -e "printf '%.4f', $e-$s")
    (( t < best )) && best=$t
  done
  echo $best
}
si=$(t3 X=1 $RK $T/empty.raku); se=$(t3 X=1 $T/empty.O)
echo "start-up: interp $si s, exe $se s"
row() {   # row LABEL OPS START ENV cmd…
  local t=$(t3 $4 "${@:5}")
  printf "%-26s %9s s  %8.2f ns/op\n" "$1" $t $(perl -e "print(($t - $3) * 1e9 / $2)")
}
row "dot interp off"          5000000   $si RAKUPP_NO_SELFPACK=1 $RK $T/dot.raku
row "dot interp on"           50000000  $si X=1 $RK $T/dot10.raku
row "dot exe-O off"           5000000   $se RAKUPP_NO_SELFPACK=1 $T/dot.O
row "dot exe-O on"            500000000 $se X=1 $T/dot100.O
row "matmul interp off"       1728000   $si RAKUPP_NO_SELFPACK=1 $RK $T/matmul.raku
row "matmul interp on"        13824000  $si X=1 $RK $T/matmul240.raku
row "matmul exe-O off"        1728000   $se RAKUPP_NO_SELFPACK=1 $T/matmul.O
row "matmul exe-O on"         216000000 $se X=1 $T/matmul600.O
row "stencil interp off"      1598400   $si RAKUPP_NO_SELFPACK=1 $RK $T/stencil.raku
row "stencil interp on"       15984000  $si X=1 $RK $T/stencil10.raku
row "stencil exe-O off"       1598400   $se RAKUPP_NO_SELFPACK=1 $T/stencil.O
row "stencil exe-O on"        159840000 $se X=1 $T/stencil100.O
