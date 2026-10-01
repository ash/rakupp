# Regression: a routine whose PARAMETER is `$_` still sees the loop's own topic
# inside a `for`, `given` or statement-modifier loop. The pad pass gave `$_` a
# slot like any other parameter, and a `$_` inside `for 16, 8 ... 0 { … }` was
# read from that slot — the parameter — instead of the topic the loop bound.
# Subs had it since pads landed; methods took pads on 2026-09-30 and Cro's
# HTTP/2 frame serializer (`method !form-header(Frame $_)`) wrote every byte
# of a length field as the same value.
#
# Expectations checked against Rakudo 2026.09 via /opt/homebrew/bin/rakudo. Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

sub f($_) { my @o; for 16, 8, 0 { @o.push: $_ }; @o }
ck(f(7), [16, 8, 0], 'a `$_` sub parameter: the for loop sees its own topic');
class A {
    method m($_) { my @o; for 16, 8, 0 { @o.push: $_ }; @o }
    method bytes(Int $_) { my $num = $_; my @b; for 16, 8 ... 0 { @b.push: ($num +> $_) +& 0xFF }; @b }
}
ck(A.m(7), [16, 8, 0], '…and a `$_` method parameter');
ck(A.bytes(7), [0, 0, 7], 'a 24-bit field written byte by byte');
sub g($_) { my @o; @o.push($_) for 1, 2; given 5 { @o.push: $_ }; @o.push: $_; @o }
ck(g(9), [1, 2, 5, 9], 'statement-modifier for and given rebind it; the parameter is back after');

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
