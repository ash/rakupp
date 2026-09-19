# `my` inside the body is a kernel LOCAL, fresh each iteration, and must not
# leak into the slot set.
my $out = 0;
my $i = 0;
while $i < 100000 {
    my $a = $i * 2;
    my $b = $a + 1;
    $out = $out + $b - $a;
    $i = $i + 1;
}
say $out;
