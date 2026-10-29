# Regression: prefix `+` of a string or a Match that is not a number is a
# Failure, in a natively compiled program too, and of a number it is that
# number (found fixing issue #189).
#
# Codegen wrote `+$x` as `0 + $x`, so a compiled `+"=42"` died "Cannot convert
# string to number" where Rakudo and the interpreter answer a Failure, and once
# a Match numified as its text (#189) a compiled `+$/` of ｢=42｣ died the same
# way. It is rtPlus now, which asks prefixNumeric as the interpreter does (as
# rtNeg does for `-`). The interpreter's own `+` of such a Match was Any; it is
# the same Failure. t/run.raku compiles this file with --exe and holds it to
# native.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

my $s = '=42';
ck (+$s).^name, 'Failure', '+ of a non-numeric Str is a Failure';
'id=42' ~~ / '=' (\d+) /;
ck (+$0), 42, '+ of a capture of digits';
ck (+$0).^name, 'Int', '…an Int';
'=42' ~~ / '=' \d+ /;
ck (+$/).^name, 'Failure', '+ of a non-numeric Match is a Failure';
ck (+'3.5'), 3.5, '+ of a decimal Str is a Rat';
ck (+'1e3'), 1e3, '…of an exponent a Num';
my $i = 7;
ck (+$i), 7, '+ of an Int';
my $n = 2.5e0;
ck (+$n), 2.5e0, '+ of a Num';
ck (+True), 1, '+ of True';
my int $ni = 3;
ck (+$ni).^name, 'Int', '+ of a native int is an Int';

say 'PASS';
