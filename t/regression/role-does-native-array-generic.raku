# A parametric role may do `array[T]` of its own type parameter, as in Rakudo
# (rakudo#4361): `role A[::T] does array[T] { }` died "Invalid typename
# 'array'". (Rakudo refuses it once composed: an array is not composable.)

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}

role A[::T] does array[T] { }
check 'declared', 'declared', 'role A[::T] does array[T] (rakudo#4361)';
role B[::T] { has array[T] $.b; }
role C[::T] does Blob[T] { }
check 'Alive', 'Alive', '…and the issue\'s other two, as before';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
