# Sum the integers 1 .. 1_000_000 with an explicit loop.
my $total = 0;
for 1 .. 1_000_000 {
    $total += $_;
}
say $total;
