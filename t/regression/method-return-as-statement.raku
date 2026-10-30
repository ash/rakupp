# Regression: `EXPR.return` (Mu.return: return the invocant from the routine
# it is written in) cost about 160 µs a call, the unwinding of an exception
# thrown out of a method call, where `return EXPR` costs under a microsecond.
# Cro picks every message body's parser with `.return if
# .is-applicable($message)`, and Cro::RPC::JSON's WebSocket tests lost a race
# to it. As a whole statement it now IS `return EXPR`; these are the
# semantics it must keep.
#
# Expectations checked against Rakudo 2026.09
# (~/.rakubrew/versions/moar-2026.09/install/bin/rakudo). Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

sub first-over-two { for 1..5 { .return if $_ > 2 }; "none" }
ck first-over-two(), 3, '`.return if …` in a for loop returns the topic';
sub lit { 42.return; "after" }
ck lit(), 42, 'a literal invocant';
sub k($v) { $v.return if $v; "falsy" }
ck (k(0), k(7)), ("falsy", 7), 'a variable invocant, under a modifier';
sub from-map { (1, 2, 3).map({ .return if $_ == 2; $_ }).eager; "x" }
ck from-map(), 2, 'from a block inside a map, out of the routine';
my $x = 5;
sub rw { $x.return-rw }
rw() = 9;
ck $x, 9, '`.return-rw` returns the container';
class Sel {
    has @.ps = 1, 2, 3;
    method sel($n) { for @!ps { .return if $_ >= $n }; die "none" }
}
ck Sel.new.sel(2), 2, "in a method, Cro's selector shape";
sub in-expr { my $y = (7.return); "not reached" }
ck in-expr(), 7, 'inside an expression it still returns';

# …and it costs what `return` does, within a wide margin (it was ~200 times)
sub via-method($v) { $v.return }
sub via-stmt($v) { return $v }
via-method($_), via-stmt($_) for ^200;
my $t0 = now; via-method($_) for ^2000; my $tm = now - $t0;
$t0 = now; via-stmt($_) for ^2000; my $ts = now - $t0;
ck $tm < 20 * $ts, True, "2000 `.return`s take under 20 times as long as 2000 `return`s";

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
