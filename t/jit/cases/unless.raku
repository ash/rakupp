# `unless` inside a kernel, as a block and as a statement modifier. The
# copy-and-patch lowering ignored IfStmt::isUnless and compiled every `unless`
# as an `if`, so these printed 1 where the interpreter and Rakudo print 3.
my $k = 0; my $w = 0;
while $k < 5 { $w = $w + 1 unless $k < 2; $k = $k + 1 }
say $w;
my $j = 0; my $v = 0;
while $j < 5 { unless $j < 2 { $v = $v + 1 }; $j = $j + 1 }
say $v;
my @a = 1, 2; my $i = 0; my $u = 0;
while $i < 5 { $u = $u + 1 unless @a[$i].defined; $i = $i + 1 }
say $u;
