# `a < b < c` in a kernel: each operand once, each link the chain's own rule
# (Interpreter::chainLink, which bridges an object operand), False at the
# first failing link without evaluating what follows it.
my $c = 0; my $i = 0;
while $i < 300 { $c++ if 10 <= $i < 50; $i++ }
say $c;
my $calls = 0;
sub f($v) { $calls++; $v }
my $j = 0; my $h = 0;
while $j < 200 { $h++ if 100 < $j < f(150); $j++ }
say "$h $calls";
my $w = "a"; my $s = 0; my $q = 0;
while $q < 150 { $s++ if "a" le $w lt "b"; $q++ }
say $s;
class B { has $.v; method Bridge { $!v.Num } method Real { $!v } method Numeric { $!v } }
my $b = B.new(v => 5); my $bb = 0; my $r = 0;
while $r < 150 { $bb++ if 1 < $b < 10; $r++ }
say $bb;
