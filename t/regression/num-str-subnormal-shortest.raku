# A subnormal Num printed fifteen significant digits it does not have.
#
# Num.Str is the shortest decimal that reads back to the same double, and the
# search for it started at fifteen digits. A normal double always needs at
# least that many, but a subnormal holds fewer bits, so its shortest form can
# be one digit: `3e-320 * 3` is 9e-320 in Rakudo and printed as
# 8.99989980464415e-320 here. Every fixture below is one where the old
# fifteen-digit form round-trips too, so the check fails on the broken engine.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

check(~(3e-320 * 3),    '9e-320',          'a product lands on a short subnormal');
check(~5e-324,          '5e-324',          'the smallest subnormal');
check(~(4.9e-324 * 7),  '3.5e-323',        'a multiple of the smallest');
check(~1e-310,          '1e-310',          'a subnormal literal');
check(~(-1.5e-315),     '-1.5e-315',       'a negative subnormal');
check(~1.234567e-310,   '1.234567e-310',   'seven digits, not fifteen');
check((3e-320 * 3).gist, '9e-320',         'gist agrees');
# the boundary: the smallest NORMAL double still needs all seventeen digits,
# and the largest subnormal sixteen
check(~2.2250738585072014e-308, '2.2250738585072014e-308', 'the smallest normal');
check(~2.225073858507201e-308,  '2.225073858507201e-308',  'the largest subnormal');
# and normal Nums keep their form
check(~(2**-24).Num,    '5.960464477539063e-08', 'a power of two (tie)');
check(~1e300,           '1e+300',          'a large Num');

if @fail { .say for @fail; say "FAIL" } else { say "PASS" }
