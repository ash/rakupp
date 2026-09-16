# Regression: `"5000000000000000000".abs` is that Int, not 5e+18.
#
# Every arm of `.abs` handled its own type and then fell through to
# `fabs(toNum())`, so a Str numified through a DOUBLE — and a Str is exactly
# what `.abs` gets when a program has split a number into parts. Lingua::EN::
# Numbers' `comma` does `my ($whole, $frac) = $i.split('.')` and then
# `$whole.abs`, so every value past 2**53 came back in scientific notation and
# the commas went into the exponent: `comma(5000000000000000000)` answered
# '5e,+18'.
#
# `.Numeric` had always answered Int for the same string; only this arm went the
# other way.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

check "5000000000000000000".abs,  5000000000000000000, 'a wide integer keeps its digits';
check "-5000000000000000000".abs, 5000000000000000000, '…and so does its negative';
check "5000000000000000000".abs.WHAT.^name, 'Int', 'and the type is Int, not Num';
check "9007199254740993".abs, 9007199254740993, 'one past 2**53 is exact';

# the narrower cases must not change
check "-7".abs,     7,      'a small negative';
check "123.45".abs, 123.45, 'a Rat keeps its type';
check "123.45".abs.WHAT.^name, 'Rat', '…and is a Rat';
check "1e5".abs,    100000e0, 'an exponent form stays a Num';
check (-5).abs,     5,      'an Int invocant';
check (-1.5).abs,   1.5,    'a Rat invocant';
check (-2.5e0).abs, 2.5e0,  'a Num invocant stays a Num';

# a Str that is not a number still refuses
check (try "abc".abs).defined, False, 'a non-numeric Str fails';
check do { try "abc".abs; $!.^name }, 'X::Str::Numeric', '…with X::Str::Numeric';

# the whole shape the module writes
my $n = 5000000000000000000;
my ($whole, $frac) = $n.split('.');
check $whole.abs.flip.comb(3).join(',').flip, '5,000,000,000,000,000,000',
      'the comma-grouping the distribution performs';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
