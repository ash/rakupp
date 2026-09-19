# Shapes the whitelist refuses, all in one file: a call in the body, an array
# index, a method call, a `for` loop. Every one must run interpreted and give
# the ordinary answer.
sub twice($x) { $x * 2 }
my @a = 1 .. 1000;
my $s = 0; my $i = 0;
while $i < 1000 { $s = $s + twice(@a[$i]); $i = $i + 1 }
say $s;
my $u = 0;
for 1 .. 1000 { $u = $u + $_ }
say $u;
my $str = "x"; my $j = 0;
while $j < 100 { $str = $str ~ "y"; $j = $j + 1 }
say $str.chars;
