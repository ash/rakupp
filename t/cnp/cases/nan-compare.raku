# `!(a < b)` and `a >= b` are different questions when an operand is NaN, and a
# branch that is taken when a condition is FALSE must ask the first one. If the
# lowering inverted the operator to save a stencil, this loop would not stop.
my $x = 0e0; my $nan = NaN; my $k = 0; my $seen = 0;
while $k < 5 {
    $seen = $seen + 1 if !($nan < 1e0);
    $seen = $seen + 10 if !($nan >= 1e0);
    $k = $k + 1;
}
say "$seen $k";
my $m = 0;
until $nan < 1e0 { $m = $m + 1; last if $m > 3 }
say $m;
