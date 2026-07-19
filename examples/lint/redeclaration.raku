# Demonstrates: [redeclaration]
# A stray second `my` shadows the first `$total` in the same scope — the
# accumulated sum is silently thrown away and reset to 0.
# Run:  rakupp --lint redeclaration.raku

my $total = 0;
$total += $_ for 1..10;

my $total = 0;          # <-- redeclaration: the running total is lost
say $total;
