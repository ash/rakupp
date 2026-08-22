# Rational arithmetic — the shape every decimal literal in Raku produces. `0.01`
# is a Rat, not a float, so a money or unit-conversion loop is 200k short-lived
# Rats created, added into an accumulator and read once each. This is the one
# value class the representation work (Value 344 -> 128 bytes) could have taxed
# rather than paid: a Rat keeps its numerator/denominator pair behind the
# lazily-allocated cold block, so a value that is created and read but never
# copied pays the allocation without collecting the copy saving. The multiplier
# keeps the denominator bounded, so this times Rat handling and not the BigInt
# path (bigint.raku covers that), while the gcd reduction still varies per
# iteration. tools/perf-guard.raku runs the same loop as its `rats` kernel.
my $t = 0;
my $d = 0;
for 1 .. 200_000 {
    my $r = 0.01 * ($_ % 97);
    $t += $r;
    $d += $r.denominator;
}
say $t, " ", $d;
