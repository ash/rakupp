# A `for` nested in a kernel body over a numeric range (jit::nestedForLoop),
# a `-> @row` loop variable (a bind, not a list assignment), and a `my @x`
# declared in the body (a fresh Array each pass).
my $s = 0; my $i = 0;
while $i < 30 { for ^10 -> $j { $s = $s + $i * $j } $i++ }
say $s;
my $u = 0; my $n = 7; my $m = 0;
while $m < 30 { for 2 ..^ $n -> $x { next if $x == 3; last if $x == 6; $u = $u + $x } $m++ }
say $u;
my $v = 0; my $hstr = "4"; my $p = 0;
while $p < 30 { for ^$hstr -> $x { $v = $v + $x } $p++ }
say $v;
my @grid = (^150).map({ [$_, $_ * 2] });
my $g = 0;
for @grid -> @row { $g = $g + @row[0] + @row[1] + @row.elems }
say $g;
for @grid -> @row { @row[1] = 0 if @row[0] %% 2 }
say @grid[0], " ", @grid[1];
my @out; my $k = 0;
while $k < 150 { my @pair; @pair.push($k); @pair.push(-$k); @out.push(@pair); $k++ }
say @out.elems, " ", @out[149], " ", @out[0];
