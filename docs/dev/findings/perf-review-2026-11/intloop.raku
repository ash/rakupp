# K1: integer accumulate, the innermost loop of every Fortran program
my $n = 20_000_000;
my $s = 0;
my $i = 0;
while $i < $n { $s = ($s + $i * $i) % 1000003; $i++ }
say $s;
