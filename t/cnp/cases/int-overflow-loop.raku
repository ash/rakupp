# The Int fast lane is guarded by an overflow check; past it the cold block
# hands the operands to the interpreter's own operator, which promotes to a
# bignum. The loop keeps running on boxed registers afterwards.
my $x = 1; my $i = 0; my $sum = 0;
while $i < 80 {
    $x = $x * 2;
    $sum = $sum + $x;
    $i = $i + 1;
}
say $x;
say $sum;
say $x.WHAT.^name;
