# An interpolated string in a kernel runs the interpreter's own interpolate()
# over the parts (rk_cnp_call): directly when no part is an object, as a call —
# the loop's variables written back around it — when one is, since its .Str
# may be the user's. A Junction part autothreads the whole string, and the
# parts are NFC-normalised across their boundaries.
my $s = ""; my $i = 0;
while $i < 300 { $s = "$s,$i"; $i++ }
say $s.chars, " ", $s.substr(0, 20);
my $cnt = 0;
class D { method Str { $cnt++; "d" } }
my $d = D.new; my $j = 0; my $t = "";
while $j < 150 { $t = "[$d]"; $j++ }
say "$t $cnt";
my $n = 0; my $w = "";
while $n < 150 { my $v = $n * 2; $w = "{$v + 1}-{$n}"; $n++ }
say $w;
my $junc = 1|2; my $jj; my $q = 0;
while $q < 150 { $jj = "v=$junc"; $q++ }
say $jj;
