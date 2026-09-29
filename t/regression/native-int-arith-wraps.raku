# Regression: integer arithmetic on a native int is the NATIVE candidate and
# wraps at 64 bits, as Rakudo's dispatch picks it; everywhere else it is Int
# arithmetic and grows. Oracle-checked against Rakudo 2026.09.
#
# An operand is native when it names a variable DECLARED native int (resolved
# lexically: `my int $x`, `sub f(int $p)`), or is itself a native operation;
# the other operand must be native too or an integer literal that fits 64 bits.
# Before, `int.max + 1` died "Cannot unbox 64 bit wide bigint", `$f * 4` printed
# 2**64, and a native's tags leaked into boxed containers — an array element,
# a hash value, a parameter, a `for` variable — which then refused big values.
# Contract: exit 0 + last line PASS.
my @fail;
sub is($got, $want, $label) { @fail.push("$label: got {$got.raku}, want {$want.raku}") unless $got eqv $want }

my int $c = 9223372036854775807;
my $one = 1; my int $n1 = 1; my Int $I = 1;

# which candidate: native + native / literal wraps, native + boxed grows
is $c + 1,     -9223372036854775808, 'native + literal';
is $c + $n1,   -9223372036854775808, 'native + native';
is $c + $one,   9223372036854775808, 'native + boxed variable';
is $c + $I,     9223372036854775808, 'native + typed Int variable';
is 1 + $c,     -9223372036854775808, 'literal + native';
is $c * $c,     1,                   'native * native';
is -$c - 2,     9223372036854775807, 'negated native - literal';
is $c ** 2,     1,                   'native ** literal';
is $c div 2,    4611686018427387903, 'native div literal';
{ my int $m = -9223372036854775807 - 1; is -$m, -9223372036854775808, 'negating int.min' }
is $c + 1e0,    9.223372036854776e18, 'native + Num is Num';
is $c + 0.5,    9223372036854775807.5, 'native + Rat is Rat';
{ my int $z = 2**62; is ($z * 4) div 2, 0, 'a native result stays native' }
is $n1 + 2**70, 1180591620717411303425, 'a literal wider than 64 bits is Int';
{ my $sum = 0; $sum = $c + 1; is $sum, -9223372036854775808, 'a native result into a boxed variable' }
{ my int $v = 9223372036854775807; $v = $v + 1; is $v, -9223372036854775808, 'native = native + 1' }
{ my int $s = 0; my int $i = 0; while $i < 10 { $s = $s * 3000000000 + 7; $i = $i + 1 };
  is $s, 5371122695617286663, 'a wrapping accumulator' }
{ my int $f = 2**62; is $f * 4, 0, 'native * literal, printed directly' }
{ my int $i = -7; is ($i div 2, $i % 3, $i mod 3), (-4, 2, 2), 'div / % / mod floor' }
{ my int $i = 5; is ($i ** 3, $i ** 0), (125, 1), 'native ** small' }
{ my int8 $b = 127; $b = $b + 1; is $b, -128, 'int8 wraps at its width on store' }

# a native's value in a boxed place is an Int
{ sub f($x) { $x + 1 };     is f($c), 9223372036854775808, 'readonly parameter' }
{ sub f(Int $x) { $x + 1 }; is f($c), 9223372036854775808, 'Int parameter' }
{ sub g(int $x) { $x + 1 }; is g($c), -9223372036854775808, 'native parameter stays native' }
{ my @a = $c;       is @a[0] + 1, 9223372036854775808, 'array element' }
{ my %h = a => $c;  is %h<a> + 1, 9223372036854775808, 'hash value' }
{ sub g2 { $c };    is g2() + 1, 9223372036854775808, 'return value' }
{ is ($c, 1)[0] + 1, 9223372036854775808, 'list element' }
{ for $c -> $v { is $v + 1, 9223372036854775808, 'for variable' } }
{ given $c { is $_ + 1, 9223372036854775808, 'given topic' } }
{ sub f($x) { $x }; f($c); is $c + 1, -9223372036854775808, 'the caller keeps its native' }

# …and a boxed container that took a native's value holds anything
{ my int $g = 5; my @a = $g;        @a[0] = 2**70; is @a[0], 2**70, 'array from a native' }
{ my int $g = 5; my @a; @a = $g, 1; @a[0] = 2**70; is @a[0], 2**70, 'list-assigned array' }
{ my int $g = 5; my @a; @a.push($g); @a[0] = 2**70; is @a[0], 2**70, 'pushed element' }
{ my int $g = 5; my %h = a => $g;   %h<a> = 2**70; is %h<a>, 2**70, 'hash from pairs' }
{ my int $g = 5; my @a = $g + 1;    @a[0] = 2**70; is @a[0], 2**70, 'array from a native result' }
{ my int $g = 5; my @a = ($g, $g);  @a[1] = 0.5;   is @a[1], 0.5,   'no truncation in a boxed element' }

# an untyped `is rw` is Int arithmetic even bound to a native: the store refuses
{ sub f($x is rw) { $x = $x + 1 }; my int $n = $c;
  my $died = False; try { f($n); CATCH { default { $died = True } } };
  @fail.push('is rw over a native must refuse the grown Int') unless $died }
{ my int $n = $c; my $died = False;
  try { for $n -> $v is rw { $v = $v + 1 }; CATCH { default { $died = True } } };
  @fail.push('for is rw over a native must refuse the grown Int') unless $died }

say @fail ?? "FAIL: {@fail.join('; ')}" !! 'PASS';
exit @fail ?? 1 !! 0;
