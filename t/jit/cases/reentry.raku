# The same loop node, run many times. The kernel is compiled once and entered
# on every later execution.
sub run-one($n) { my $s = 0; my $i = 0; while $i < $n { $s = $s + $i; $i = $i + 1 }; $s }
my $tot = 0;
for 1 .. 60 { $tot = $tot + run-one(3000) }
say $tot;
