# Increment counters in a hash.
my %counts;
for 1 .. 100_000 {
    %counts{$_ % 1_000}++;
}
say %counts.elems, " ", %counts{0};
