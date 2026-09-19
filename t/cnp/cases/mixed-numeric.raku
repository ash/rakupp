# Int and Num in the same register file. Raku's `+` on an Int and a Num is a
# Num; `/` on two Ints is a Rat, not an Int; and `3.14` is a Rat literal, not a
# floating-point one. The stencils have a fast lane for the first and none at
# all for the other two.
my $i = 0; my $f = 0e0; my $r = 0; my $pi = 0;
while $i < 30 {
    $f = $f + $i * 1.5e0;
    $r = $r + $i / 2;
    $pi = $pi + 3.14;
    $i = $i + 1;
}
say $f, " ", $r, " ", $r.WHAT.^name, " ", $pi, " ", $pi.WHAT.^name;
