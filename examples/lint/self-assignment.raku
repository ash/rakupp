# Demonstrates: [self-assignment]
# The swap is wrong: the second line assigns `$a` to itself instead of copying
# the saved value back, so the two variables end up equal.
# Run:  rakupp --lint self-assignment.raku

my ($a, $b) = 1, 2;

my $tmp = $a;
$a = $b;
$a = $a;          # <-- meant `$b = $tmp`

say "$a $b";
