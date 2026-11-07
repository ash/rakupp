#!/bin/zsh
# run.sh [RAKUPP] : the FORTRAN-LESSONS array kernels in Fortran, C, untyped Raku
# (self-packing off and on) and native-typed Raku, interpreted and --exe -O.
# Each program builds its arrays once and times 5 passes of the kernel; a row
# is the fastest pass of 2 runs. Each checksum is held to gfortran's.
# gfortran: $GFORTRAN, else gfortran on PATH, else /opt/homebrew/bin/gfortran
# (a native one: an x86-64 one under Rosetta would not be a fair comparison).
here=${0:A:h}
RK=${1:-$here/../../../../build/rakupp}
FC=${GFORTRAN:-$(command -v gfortran || echo /opt/homebrew/bin/gfortran)}
T=$(mktemp -d)
trap 'rm -rf $T' EXIT
$FC -O3 -ffp-contract=off $here/kernels.f90 -o $T/f-O3 || exit 1
$FC -Ofast $here/kernels.f90 -o $T/f-Ofast || exit 1
cc -O3 -ffp-contract=off $here/kernels.c -o $T/c-O3 || exit 1
$RK --exe -O $here/kernels.raku -o $T/r-exe > /dev/null 2>&1 || echo "kernels.raku did not build"
$RK --exe -O $here/kernels-n.raku -o $T/n-exe > /dev/null 2>&1 || echo "kernels-n.raku did not build"
echo "$($FC --version | head -1); $(cc --version | head -1); $($RK --version)"
echo "ms for one pass of the kernel, the fastest of 2 runs x 5 passes; sum, dot, axpy over 10,000,000 doubles; matmul 200x200"
echo

best() {   # best ENV cmd… : "checksum ms" of the fastest of 2 runs
  local b=999999 cs="" out ms
  for r in 1 2; do
    out=$(env $1 "${@:2}" 2>/dev/null)
    ms=${${(f)out}[2]}
    cs=${${(f)out}[1]//[[:space:]]/}
    (( ms < b )) && b=$ms
  done
  echo "${cs:l} $b"
}
for k in sum dot axpy matmul; do
  ref=$(best X=1 $T/f-O3 $k); want=${ref%% *}
  printf "%s\n" "== $k (checksum $want)"
  row() {   # row LABEL ENV cmd…
    local r=$(best "${@:2}") flag=""
    [[ ${r%% *} == $want ]] || flag="  (checksum ${r%% *})"
    printf "  %-40s %10.2f%s\n" $1 ${r##* } $flag
  }
  printf "  %-40s %10.2f\n" "Fortran, gfortran -O3, loops" ${ref##* }
  row "Fortran, gfortran -O3, intrinsic" X=1 $T/f-O3 ${k}i
  row "Fortran, gfortran -Ofast, loops" X=1 $T/f-Ofast $k
  row "C, clang -O3" X=1 $T/c-O3 $k
  row "Raku untyped, --exe -O, packing on" X=1 $T/r-exe $k
  row "Raku untyped, --exe -O, packing off" RAKUPP_NO_SELFPACK=1 $T/r-exe $k
  row "Raku native types, --exe -O" X=1 $T/n-exe $k
  row "Raku untyped, interpreted, packing on" X=1 $RK $here/kernels.raku $k
  row "Raku untyped, interpreted, packing off" RAKUPP_NO_SELFPACK=1 $RK $here/kernels.raku $k
  row "Raku native types, interpreted" X=1 $RK $here/kernels-n.raku $k
done
