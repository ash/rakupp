# Regression: what a routine's parameter TYPES do when it binds, and what a
# `for ^N` / `for A..B` loop iterates, the same interpreted and compiled. The
# native backend (`--exe`, t/exe/run.raku) bound every typed parameter as if it
# were untyped — `sub f(Int $n)` took a Str, `Int() $n` did not coerce, and an
# unpassed `Int $x?` was Any — and counted `^2.5` and `0..^2.5` as integer
# ranges, stopping at 1.
#
# Expectations checked against Rakudo 2026.09 via /opt/homebrew/bin/rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

sub fi(Int $n) { $n.^name }
sub fc(Int() $n) { $n.^name }
sub fst(Str $x) { $x.^name }
sub fr(Real $x) { $x.^name }
sub fopt(Int $x?) { $x.^name }
sub fnamed(Int :$x) { $x.^name }
sub fdef(Int $x = 7) { $x }
sub two(Int $a, Str $b) { "$a$b" }
class C { method m(Int $x) { $x * 2 } }

my $str = "x"; my $one = 1; my $rat = 1/2;
ck(fi(5), 'Int', 'an Int into Int');
ck(fi(True), 'Bool', 'a Bool into Int');
ck(fi(Int), 'Int', 'the Int type object into Int');
ck(fc("3"), 'Int', 'Int() coerces a Str');
ck(fr($rat), 'Rat', 'a Rat into Real');
ck(fopt(), 'Int', 'an unpassed Int $x? is (Int)');
ck(fnamed(), 'Int', 'an unpassed Int :$x is (Int)');
ck(fnamed(:x(3)), 'Int', 'a passed Int :$x');
ck(fdef(), 7, 'a typed default');
ck(two(1, "b"), '1b', 'two typed parameters');
ck(C.new.m(4), 8, 'a typed method parameter');
ck((try fi($str)) // $!.^name, 'X::TypeCheck::Binding::Parameter', 'a Str into Int is refused');
ck((try fi(Any)) // $!.^name, 'X::TypeCheck::Binding::Parameter', 'Any into Int is refused');
ck((try fst($one)) // $!.^name, 'X::TypeCheck::Binding::Parameter', 'an Int into Str is refused');
ck((try fr($str)) // $!.^name, 'X::TypeCheck::Binding::Parameter', 'a Str into Real is refused');
ck((try two($one, $one)) // $!.^name, 'X::TypeCheck::Binding::Parameter', 'the second parameter is checked');
ck((try C.new.m($str)) // $!.^name, 'X::TypeCheck::Binding::Parameter', 'a method parameter is checked');
try fi($str);
ck($!.message, q[Type check failed in binding to parameter '$n'; expected Int but got Str ("x")], 'the message');

# native parameters bind as natives: an int8 wraps, a uint8 takes -1 as 255,
# and a value of the wrong kind is refused
sub n8(int8 $x) { $x }
sub nu8(uint8 $x) { $x }
sub nint(int $x) { $x + 1 }
sub nstr(str $x) { $x }
my $big = 2**70; my $three = 300; my $neg = -1;
ck(n8($three), 44, 'an int8 parameter wraps');
ck(nu8($neg), 255, 'a uint8 parameter wraps');
ck(nint(True), 2, 'a Bool into int');
ck((try nint($big)) // $!.^name, 'X::AdHoc', 'a BigInt does not fit an int');
# (Rakudo says X::AdHoc, "cannot unbox"; the interpreter a binding type
# check — so only the refusal is asserted)
ck((try { nint($str); 1 }) // 'refused', 'refused', 'a Str into int is refused');
ck((try { nstr($one); 1 }) // 'refused', 'refused', 'an Int into str is refused');

sub fib(Int $n) { $n < 2 ?? $n !! fib($n - 1) + fib($n - 2) }
ck(fib(20), 6765, 'a typed integer routine');

# for ^N and fractional ranges
{ my @s; my $n = 2.5; for ^$n { @s.push($_) }; ck(@s, [0, 1, 2], '^2.5') }
{ my @s; my $n = 2.5; for 0..^$n { @s.push($_) }; ck(@s, [0, 1, 2], '0..^2.5') }
{ my @s; for 0.5..3 { @s.push($_) }; ck(@s, [0.5, 1.5, 2.5], '0.5..3') }
{ my @s; my @a = <x y z>; for ^@a { @s.push($_) }; ck(@s, [0, 1, 2], '^@a') }
{ my @s; my $s = "3"; for ^$s { @s.push($_) }; ck(@s, [0, 1, 2], '^"3"') }
{ my $c = 0; my $z = 0; for ^$z { $c++ }; my $m = -2; for ^$m { $c++ }; ck($c, 0, '^0 and ^-2 are empty') }
{ my $c = 0; for 1..10 { last if ++$c > 3 }; ck($c, 4, 'last if ++$c > 3') }
{ my $t = 0; for ^3 { for ^$_ { $t++ } }; ck($t, 3, 'nested ^N') }

say $fails ?? "FAILED $fails" !! "PASS";
