# Regression: `=~=` against zero, and a readable `$*TOLERANCE`.
#
# Rakudo's approximate-equality tolerance is RELATIVE — the difference over the
# larger magnitude — except when one side is exactly zero, where no relative test
# can ever succeed and the comparison becomes absolute. This engine had only the
# relative branch, so `1e-17 =~= 0` was False where Rakudo says True; Math::Trig
# checks a cartesian z-component with `ok($z =~= 0)` and that was its last
# failing assertion.
#
# The tolerance itself is $*TOLERANCE, which a program may set. It was hardcoded
# at the comparison and read back as Any.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

# the zero case: absolute, so anything under the tolerance is equal to zero
check (1e-17 =~= 0),  True,  'a tiny number is approximately zero';
check (0 =~= 1e-17),  True,  'and the comparison is symmetric';
check (1e-15 =~= 0),  False, 'the tolerance itself is not under it';
check (1e-14 =~= 0),  False, 'nor is anything larger';
check (0 =~= 0),      True,  'zero is zero';
check (0.0 =~= 0),    True,  'across types too';

# NEITHER side zero: relative, so two tiny numbers that differ by a factor are
# NOT equal — this is the case that tells the two branches apart
check (1e-17 =~= 1e-18), False, 'two tiny numbers are still compared relatively';

# the ordinary relative cases
check (1 =~= 1 + 1e-16),  True,  'a relative difference under tolerance';
check (1 =~= 1 + 1e-14),  False, 'and one over it';
check (100 =~= 100.0000000001), False, 'scale does not rescue it';

# the Unicode spelling is the same operator
check (1e-17 ≅ 0), True, 'the ≅ spelling agrees';

# $*TOLERANCE reads, and setting it moves the comparison
check $*TOLERANCE, 1e-15, 'the default tolerance is readable';
{
    my $*TOLERANCE = 1e-3;
    check $*TOLERANCE, 1e-3, 'a program may set it';
    check (1 =~= 1.0005), True, 'and the comparison follows';
    check (1e-4 =~= 0),   True, 'including the zero branch';
}
check (1 =~= 1.0005), False, 'and it is restored on the way out';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
