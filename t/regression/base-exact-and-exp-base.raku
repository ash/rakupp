# Regression: `.base` computed exactly, and `.exp` using its base argument.
#
# `.base` walked the fraction through a double. Every expansion longer than a
# double's mantissa drifted into float noise — `(2/3).base(10, 40)` ended
# `...074547720199916511774063` instead of forty sixes — and a value smaller
# than the double could hold vanished: `(1/10000000000).base(3)` was `0.000000`.
# It also never padded an explicit digit count on an integer (`121.base(11, 3)`
# lost its `.000`), never rounded a carry into the integer part
# (`(98/99).base(10, 0)` was 0, not 1), and accepted a negative digit count.
#
# `.exp($base)` dropped the base and answered e**self, so `5.exp(2)` was 148.4
# where it is 32 — `$base ** self`, the inverse of `.log($base)`.
#
# (Int-Num-Rat semantics sheet, N-20 and N-25; S32-num/base.t and exp.t.)
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

# --- N-20: an Int pads its explicit digits, and refuses a negative count -----
check 121.base(11, 3), '100.000', 'an integer pads the digits it was asked for';
check 121.base(11, 0), '100',     'and zero digits leave off the radix point';
check 255.base(16, 2), 'FF.00',   'in any base';
check 42.base(16),     '2A',      'the plain forms are untouched';
check (-12).base(16),  '-C',      'including the sign';
check 0.base(2),       '0',       'and zero';
check 1.base(10, -1).exception.^name, 'X::OutOfRange', 'a negative digit count fails';
check 1.base(37).exception.^name,     'X::OutOfRange', 'so does a base above 36';
check 1.1.base(1).exception.^name,    'X::OutOfRange', 'and one below 2';

# --- N-20: a Rational is EXACT at any length --------------------------------
check (2/3).base(10, 40),
      '0.6666666666666666666666666666666666666667', 'forty digits, the last rounded';
check (1/3).base(10, 40),
      '0.3333333333333333333333333333333333333333', 'and forty that do not round';
check (1/10000000000).base(3),
      '0.000000000000000000001', 'a value far below a double still has digits';
check (1/3).base(10),      '0.333333',  'the default is six digits';
check (1/128).base(10, *), '0.0078125', 'Whatever runs until it terminates';
check (3/1024).base(16, *), '0.00C',    'in any base';
check (3/2).base(10, 1),   '1.5',       'one digit';
check (98/99).base(10, 1), '1.0',       'a carry that reaches the integer part';
check (98/99).base(10, 0), '1',         'even with no fraction digits at all';
check (100/99).base(10, 0), '1',        'and one that does not carry';
check (49/999).base(10, 1), '0.0',      'rounding down';
check (.01).base(10, 0),   '0',         'and a value under half';

# --- N-20: a Num expands its exact binary value -----------------------------
check 16.999e0.base(16, 9), '10.FFBE76C8B', 'the exact hex expansion of a double';
check 16.99999e0.base(16, 3), '11.000',     'rounded, carrying into the integer';
check 16.99999e0.base(16, 0), '11',         'with no fraction at all';
check pi.base(10, 4),  '3.1416',            'pi to four places';
check 16e0.base(16, 3), '10.000',           'explicit zeros are produced';
check pi.base(10),     '3.14159265',        'the Num default is eight decimal places';
check 1e-10.base(10),  '0.00000000',        'however small the value';
check 0.5e0.base(10),  '0.5',               'but it stops when the expansion ends';
check 0.1e0.base(2).chars, 28,              'and the count follows the base';
check (try Inf.base(16)) // $!.^name, 'X::Numeric::CannotConvert', 'Inf has no digits';
check (try NaN.base(16)) // $!.^name, 'X::Numeric::CannotConvert', 'nor has NaN';
# a digit count that cannot be unboxed is an error, not a silent huge string
check (try 255.base(16, 10**49)) // $!.^name, 'X::AdHoc', 'a bigint digit count is refused';

# --- N-25: exp takes its base -----------------------------------------------
check 5.exp(2),      32, '5.exp(2) is 2**5';
check 5.Rat.exp(2) == 32, True, 'whatever the invocant type';
check 5.Num.exp(2) == 32, True, 'Num too';
check 0.exp(2),       1, 'and a zero exponent';
check exp(2, 10),   100, 'the sub form keeps an Int base exact';
check 5.exp.round(0.0001), 148.4132, 'the one-argument form is still e**x';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
