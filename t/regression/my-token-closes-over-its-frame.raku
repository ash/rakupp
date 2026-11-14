# A `my token` (or regex) returned from a sub closes over the sub's frame
# (rakudo#6810): `<{$re}>` and `$x` inside it read the lexicals of the call
# that declared it. It died "Variable '$re' is not declared" once returned.
# Each call's `&F` is its own routine, `===` and `.WHICH` included; Rakudo
# 2026.09 shares one object between the calls, so its first one ends up
# matching the second call's pattern. That half is checked on Raku++ only.

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}

sub build(@items) {
    my $re = @items.join('|');
    my token FULLRE { ^ <{$re}> };
    return &FULLRE;
}

my $a = build(['a']);
check ('a' ~~ $a).so, 'True', 'returned, it sees $re (rakudo#6810)';
my $b = build(['b']);
check ('b' ~~ $b).so, 'True', 'a later call has its own';
check ($a === $a), 'True', 'one routine is itself';

sub inside { my $q = 'c'; my token Q { $q }; ('c' ~~ /<Q>/).so }
check inside(), 'True', 'used where it was declared';
my token T { b }
'abc' ~~ &T;
check $/.Str, 'b', 'a top-level one sets $/';

sub mkc($x) { sub { $x } }
my $p = mkc(1);
my $r = mkc(2);
check set($p, $r, $p).elems, '2', 'two closures are two Set elements';
check ($p.WHICH eq $r.WHICH), 'False', '…and two .WHICHes';
check (&infix:<+> === &infix:<+>), 'True', 'a built-in operator is one routine';

# Raku++ only: Rakudo 2026.09 shares one object between the two calls
if $*RAKU.compiler.name eq 'Raku++' {
    check ('a' ~~ $a).so, 'True', 'a later call leaves it alone (Rakudo: False)';
    check ('b' ~~ $a).so, 'False', '…and does not change it (Rakudo: True)';
    check ($a === $b), 'False', 'two calls, two routines (Rakudo: True)';
    check ($a.WHICH eq $b.WHICH), 'False', '…by .WHICH too (Rakudo: True)';
    sub mk($x) { my regex R { ^ $x $ }; &R }
    my $z = mk('z');
    my $y = mk('y');
    check ('z' ~~ $z).so, 'True', 'a my regex with a plain variable (Rakudo: False)';
    check ('y' ~~ $y).so, 'True', '…each its own';
}

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
