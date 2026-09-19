# The kernel's simplest shape: two mainline scalars mutated by a while loop.
my $s = 0; my $i = 0;
while $i < 200000 { $s = $s + $i; $i = $i + 1 }
say "$s $i";
