# Regression: a scalar bound to an array IS that array — `=:=` says so.
#
# `my $h := @a; $h =:= @a` is True in Rakudo: binding aliases the object.
# rakupp's bound handle shared @a's storage (a push through either showed in
# both) but carried a different list flag, and container identity compared
# the flag too — so hyperize's "degree 1 hands back the invocant" check
# (`$hyper := @a.&hyperize(12, 1); ok $hyper =:= @a`) was False.
#
# Every expectation was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my @a = 1, 2;
my $y := @a;
ck($y =:= @a, True, 'a bound scalar is the array');
$y.push(3);
ck(@a, [1, 2, 3], 'and writes through it reach the array');
sub through(\it, $d) { $d == 1 ?? it !! it.List }
my $h := @a.&through(1);
ck($h =:= @a, True, 'the invocant handed back through a raw parameter');
my $n := @a.&through(2);
ck($n =:= @a, False, 'a different object is different');
my @b = 1, 2, 3;
ck(@b =:= @a, False, 'equal contents are not identity');
my %h; my $hh := %h;
ck($hh =:= %h, True, 'the same for a hash');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
