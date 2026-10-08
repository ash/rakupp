# `do return False unless … if $!ver;` — a statement prefix whose statement
# carries a modifier, inside a statement that carries another.
#
#   f3b1fe01 allowed one conditional modifier per expression, which is right
#   for `(1 if $x if $y)`, and counted the `do`'s own modifier and the
#   enclosing statement's against the same budget, so the line above was
#   "Missing semicolon". In Rakudo the `do` takes one and leaves the next to
#   what encloses it. Pakku's Pakku::Spec writes its ACCEPTS that way, and the
#   battery scan found both copies of it refused.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want }

sub f($x, $y) { do return 1 unless $x if $y; 2 }
check (f(0, 1), f(1, 1), f(0, 0)), (1, 2, 2), 'do return … unless … if …';

my $s = '';
do $s ~= 'a' unless 0 if 1;
do $s ~= 'b' if 0 if 1;
do $s ~= 'c' if 1 if 0;
check $s, 'a', 'the inner and the outer condition both decide';

my $x = do 5 if 1 if 1;
check $x, 5, 'a do value under two conditions';
my $y = do 5 if 1 if 0;
check $y, Any, 'the outer condition false';

check (do 1 if 1 if 1), 1, 'inside parens the second belongs to the parens';
my @a = (do $_ for 1..2 if 1);
check @a, [1, 2], 'a loop modifier, then a condition';
my @g = gather take 1 if 1 if 1;
check @g, [1], 'gather takes a statement the same way';
my $t = '';
try $t ~= 't' unless 0 if 1;
check $t, 't', 'so does try';

check (try EVAL 'do 1 if 1 if 1 if 1; 1'), Nil, 'three conditions are still an error';

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
