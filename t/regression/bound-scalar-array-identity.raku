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

# --- and ASSIGNMENT is never identity, whatever the two slots end up sharing --
# The engine cannot tell a bind from an assignment by looking at the values:
# both leave two slots over one payload. For `@`/`%` an assignment copies, so
# shared storage does prove a bind — but a Code or an Object travels by its
# handle, and reading identity off the payload made an assigned copy identical
# to its source. That is what broke `run-main-protocol.raku`.
sub ident-target() { }
my $held = &ident-target;
ck($held =:= &ident-target, False, 'a scalar holding a routine is its own container');
class C { }
my $o = C.new;
my $p = $o;
ck($p =:= $o, False, 'two scalars holding one object are two containers');
ck($o =:= $o, True, '…while a slot is itself');
my $d = @a;
ck($d =:= @a, False, 'assigning an array to a scalar is not binding it');
my $e = %h;
ck($e =:= %h, False, 'nor a hash');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
