# Everything the loop touched must be readable, and correct, after the kernel
# has run — the loop is not a black box the interpreter loses sight of.
my $a = 1; my $b = 2; my $c = 3;
my $i = 0;
while $i < 120000 {
    $a = $a + 1;
    $b = $b * 1;
    $c = $c - 1;
    $i = $i + 1;
}
say "$a $b $c $i";
$a = $a + 1;
say $a;
