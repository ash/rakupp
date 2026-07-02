# Sort 50_000 pseudo-random integers.
my @nums   = (1 .. 50_000).map({ ($_ * 2654435761) % 100_000 });
my @sorted = @nums.sort;
say @sorted.head, " ", @sorted.tail, " ", @sorted.elems;
