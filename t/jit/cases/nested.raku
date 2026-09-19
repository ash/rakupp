# A nest: the OUTERMOST loop is the one that gets compiled, and the inner
# loops come along inside its kernel.
my $t = 0;
loop (my $y = 0; $y < 300; $y++) {
    loop (my $x = 0; $x < 300; $x++) { $t = $t + $x * $y }
}
say "$t $y";
