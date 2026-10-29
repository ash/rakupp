# Regression: arithmetic on a Match numifies the text it matched, as a Str
# does: `+$0` of ｢42｣ is Int 42 in a natively compiled routine, and `0 + $0`
# and `$0 * 1` are Int in both engines (issue #189).
#
# applyArith let a Str operand into the exact tower (numifyStr: Int, Rat or
# Num by the literal) but not a Match, which went to the double path, so the
# answer was a Num. The interpreter's own prefix `+` asks the Match, which is
# why `+$0` there was already an Int; compiled code writes `+$x` as `0 + $x`,
# so under --exe `+$0` was 42e0, and a `--> Int` routine returning it or an
# `Int` parameter taking it died of the type check. t/run.raku compiles this
# file with --exe and holds it to native.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

sub capture-num(--> Int) { ('id=42' ~~ / '=' (\d+) /); +$0 }
sub takes-int(Int $n) { $n * 2 }

ck capture-num(), 42, '+$0 of digits is an Int, through a --> Int return';
'id=42' ~~ / '=' (\d+) /;
ck (+$0), 42, '+$0';
ck (0 + $0), 42, '0 + $0';
ck ($0 * 1), 42, '$0 * 1';
ck ($0 - 2), 40, '$0 - 2';
ck takes-int(+$0), 84, '…and binds to an Int parameter';
ck ($0 == 42), True, '$0 == 42';
ck ($0 < 100), True, '$0 < 100';
my $m = 'x=3.5' ~~ / '=' (\S+) /;
ck (+$m[0]), 3.5, 'a decimal capture is a Rat';
ck ($m[0] + 0), 3.5, '…on the left too';
my $e = 'v=1e3' ~~ / '=' (\S+) /;
ck (+$e[0]), 1e3, 'an exponent capture is a Num';

say 'PASS';
