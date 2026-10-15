# Regression: an `int` or `num` parameter holds a native
# however the caller's value arrived, so arithmetic on it is the native
# candidate, as on a `my int` / `my num`: an int wraps at 64 bits, and a num
# result converts into a native int. Before, the bind checked the value and did
# not mark it native: `sub f(int $x) { $x * 2 }; f(2**62)` printed 2**63,
# `sub f(num $x) { my int $i = $x * 2e0 }` was refused, a `num32` parameter
# kept every bit of a double, and only a caller passing its own native
# variable got the native (the tags came along with the value). The sized ints
# (`int64`, `int8`, …) were marked by their wrap and were right.
# Oracle-checked against Rakudo 2026.09; found by WINDFALL-PLAN's `--exe`
# natives work.
# Contract: exit 0 + last line PASS.
my @fail;
sub is($got, $want, $label) { @fail.push("$label: got {$got.raku}, want {$want.raku}") unless $got eqv $want }
my $min = -9223372036854775808;

sub dbl(int $x) { $x * 2 }
is dbl(2**62), $min, 'a computed Int argument';
is dbl(4611686018427387904), $min, 'a literal argument';
{ my $b = 2**62;     is dbl($b), $min, 'a boxed variable' }
{ my Int $b = 2**62; is dbl($b), $min, 'a typed Int variable' }
{ my int $b = 2**62; is dbl($b), $min, 'a native variable, as before' }

{ sub f(int $x) { $x + $x };  is f(2**62), $min, '$x + $x' }
{ sub f(int $x) { $x * $x };  is f(2**62), 0, '$x * $x' }
{ sub f(int $x) { $x + 1 };   is f(9223372036854775807), $min, 'int.max + 1' }
{ sub f(int $x) { $x - 1 };   is f($min), 9223372036854775807, 'int.min - 1' }
{ sub f(int $x) { -$x };      is f($min), $min, 'negating int.min' }
{ sub f(int $x) { $x ** 3 };  is f(2**30), 0, '$x ** 3' }
{ sub f(int $x) { $x div -1 }; is f($min), $min, 'int.min div -1' }
{ sub f(int $x, int $y) { $x * $y }; my $two = 2; is f(2**62, $two), $min, 'two int parameters' }
{ sub f(int $x) { my $r = $x * 2; $r + 0 }; is f(2**62), $min, 'the result into a boxed variable' }
{ sub f(int $x) { my $s = 0; for ^3 { $s = $x * 4 }; $s }; is f(2**62), 0, 'read in a nested block' }
{ sub f(int $x) { $x.WHAT }; is f(5), Int, 'it is still an Int' }

# every way into a parameter
{ sub f(int $x is copy) { $x = $x * 2; $x }; is f(2**62), $min, 'is copy' }
{ sub f(int $x = 2**62) { $x * 2 }; is f(), $min, 'a default'; is f(2**62), $min, '…or passed' }
{ sub f(int $x, :$z) { $x * 2 }; is f(2**62, :z), $min, 'beside a named parameter' }
{ sub f(int $x, $y?) { $x * 2 }; is f(2**62), $min, 'beside an optional one' }
{ class K { method m(int $x) { $x * 2 } }; is K.m(2**62), $min, 'a method parameter' }
{ is (-> int $x { $x * 2 })(2**62), $min, 'a pointy block parameter' }
{ sub f(int64 $x) { $x * 2 }; is f(2**62), $min, 'int64, as before' }
{ my $d = 0; for ^2000 { $d = dbl(2**62) }; is $d, $min, 'called often enough to compile' }

# a num parameter
{ sub f(num $x) { my int $i = $x * 2e0; $i }; is ((try f(1.5e0)) // $!.message), 3, 'a num product into a native int' }
{ sub f(num $x) { my int $i = $x; $i };        is ((try f(1.5e0)) // $!.message), 1, 'a num into a native int truncates' }
{ sub f(int $x) { my num $n = $x * 2; $n };    is ((try f(3)) // $!.message), 6e0, 'an int product into a native num' }
{ sub f(num $x is copy) { my int $i = $x * 2e0; $i }; is ((try f(1.5e0)) // $!.message), 3, 'is copy' }
{ sub f(num $x, :$z) { my int $i = $x * 2e0; $i }; is ((try f(1.5e0, :z)) // $!.message), 3, 'beside a named parameter' }
{ sub f(num $x) { $x.WHAT }; is f(1e0), Num, 'it is still a Num' }
{ sub f(num32 $x) { $x };   is ((try f(0.1e0)) // $!.message), 0.10000000149011612e0, 'num32 rounds to single precision' }

# …and what is NOT native stays Int arithmetic
{ sub f(int $x) { my $b = 2; $x * $b }; is f(2**62), 2**63, 'times a boxed variable' }
{ sub h($y) { $y * 2 }; sub f(int $x) { h($x) }; is f(2**62), 2**63, 'passed on to an untyped parameter' }
{ sub f(uint $x) { $x * 2 }; is f(2**63), 2**64, 'uint arithmetic is Int arithmetic' }
{ sub f(num $x) { my $b = 2e0; my int $i = $x * $b; $i }; my $r = (try f(1.5e0)) // $!.message;
  is $r, 'This type cannot unbox to a native integer: P6opaque, Num', 'num times a boxed Num is boxed' }

say @fail ?? "FAIL: {@fail.join('; ')}" !! 'PASS';
exit @fail ?? 1 !! 0;
