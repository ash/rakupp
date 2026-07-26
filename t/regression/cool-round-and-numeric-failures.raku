# Regression: Cool's numeric coercions and an EXACT `.round`.
#   * `.round($scale)` is `(self / $scale + 1/2).floor * $scale`. Done in double
#     arithmetic it drifts — `round(1000, 23.01)` came out 989.4300000000001 —
#     and a big Int rounded by 1 lost its exactness. Whenever neither side is a
#     Num it now runs in Value arithmetic, so the answer is exact and an Int
#     stays an Int.
#   * coercing a NON-NUMERIC string is a Failure carrying X::Str::Numeric — the
#     same thing `+$str` already produced. `.Int`/`.Num`/`.Numeric`/`.Rat`/
#     `.FatRat`/`.Complex` were quietly answering zero.
#   * a string that spells a COMPLEX number answers the Complex methods through
#     it: `abs "6+8i"` is 10, not the real part.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# .round is exact when nothing is a Num
check(1.7.round,             '2',      'round-to-an-int');
check(1.07.round(0.1),       '1.1',    'round-to-a-tenth');
check(21.round(10),          '20',     'round-to-a-ten');
check(round(1000, 23.01),    '989.43', 'round-by-an-arbitrary-scale');
check((-0.5).round,          '0',      'a-half-rounds-toward-plus-infinity');
check((0.5).round,           '1',      'and-so-does-the-positive-half');
check((-0.55).round(0.1),    '-0.5',   'a-negative-tenth');
check((0.55).round(0.1),     '0.6',    'a-positive-tenth');
check((7/3).round(1/3),      '2.333333', 'rounding-by-a-rat');
check(9930972392403501.round(1).raku,       '9930972392403501', 'a-big-int-rounded-by-an-int-is-itself');
check(9930972392403501.round(1e0).raku,     '9.9309723924035e+15', 'a-num-scale-still-floats');
check(9930972392403501.round(1e0).Int.raku, '9930972392403500',    'and-truncates-when-asked');
check((-3.7).round,          '-4',     'a-negative-round');
check(2.5.round,             '3',      'two-and-a-half');
check((-2.5).round,          '-2',     'minus-two-and-a-half');
check(3.round,               '3',      'an-int-is-itself');

# a non-numeric string coerces to a Failure
check("foo".Int.^name,     'Failure', 'int-of-a-non-number');
check("foo".Num.^name,     'Failure', 'num-of-a-non-number');
check("foo".Numeric.^name, 'Failure', 'numeric-of-a-non-number');
check("foo".Rat.^name,     'Failure', 'rat-of-a-non-number');
check("foo".FatRat.^name,  'Failure', 'fatrat-of-a-non-number');
check("foo".Complex.^name, 'Failure', 'complex-of-a-non-number');
check((+"foo").^name,      'Failure', 'and-prefix-plus-agrees');
# real numbers still coerce
check("42".Int,            '42',    'int-of-a-number-string');
check("1.5".Num,           '1.5',   'num-of-a-number-string');
check("42".Numeric.^name,  'Int',   'numeric-keeps-the-narrowest-type');
check("1.5".Rat,           '1.5',   'rat-of-a-number-string');
check("0x1f".Int,          '31',    'a-radix-prefix-still-parses');
check(<42>.Int,            '42',    'an-allomorph-is-unaffected');

# a complex-shaped string answers the Complex methods
check(abs("6+8i"),         '10',     'abs-of-a-complex-string');
check("1+2i".conj.Str,     '1-2i',   'conj-of-a-complex-string');
check("1+2i".Complex.Str,  '1+2i',   'complex-of-a-complex-string');
check((-2).abs,            '2',      'abs-of-a-plain-number');
check("-3".abs,            '3',      'abs-of-a-numeric-string');
check(1+1i.Complex.Str,    '1+1i',   'complex-of-a-complex');
check(<1.3>.Complex.Str,   '1.3+0i', 'complex-of-a-rational-allomorph');
check((-4/3).Complex.Str,  '-1.3333333333333333+0i', 'complex-of-a-rat');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
