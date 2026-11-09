# A role declared with its own `[...]` over the bare role of its name —
# `role W { }; role W['X'] does W { }` — joins W's group, and the W it does
# is the one declared before it. It was refused, "Role 'W' cannot inherit
# from / compose itself", so PDF::Content, which declares
# `role PDF::Content::XObject['Form'] does PDF::Content::XObject`, could not
# load. A run-time `$obj does W['X']` then got none of the bare W's methods:
# the mixin looked the W it does up by name, and the group's latest
# candidate has that name too. A role that does itself is refused as before.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
sub refused($code) { try EVAL $code; $! ~~ Exception }

role W { method w { 5 } }
role W['X'] does W { method more { 6 } }
class CW does W['X'] { }
ck (CW.w, CW.more), (5, 6), "a class gets both roles' methods";
ck W['X'].^name, 'W[Str]', 'the candidate is a parameterization of W';
ck (CW ~~ W, CW.does(W)), (True, True), '…and does W';

class K { }
my $k = K.new;
$k does W['X'];
ck ($k.w, $k.more), (5, 6), 'a run-time `does` gets both too';
my $o = 42 but W['X'];
ck ($o.w, $o.more), (5, 6), '…and a `but`';

role A2 { has $.a = 7 }
role A2['X'] does A2 { has $.b = 8 }
class CA2 does A2['X'] { }
ck (CA2.new.a, CA2.new.b), (7, 8), "the bare role's attributes reach the class";
my $m = K.new;
$m does A2['X'];
ck ($m.a, $m.b), (7, 8), '…and the mixin';

role P { method w { 'base' } }
role P[$x] does P { method p { $x } }
class CP does P[7] { }
ck (CP.w, CP.p), ('base', 7), 'a value parameter';
ck ((1 but P[7]).w, (1 but P[7]).p), ('base', 7), '…at run time';

role B { method b { 'b' } }
role Q { method q { 'q' } }
role Q['X'] does B does Q { }
ck ((1 but Q['X']).q, (1 but Q['X']).b), ('q', 'b'), 'the bare role in a later `does`';

role S { method s { 'base' } }
role S['X'] does S { method s { 'spec' } }
class CS does S['X'] { }
ck (CS.s, (1 but S['X']).s), ('spec', 'spec'), "the candidate's own method wins";

role T[::U] { }
role T does T[Int] { }
ck T.^name, 'T', 'a bare role over a parameterization of its name, as before';

ck refused(q[role Zz1 does Zz1 { }]), True, 'a role that does itself is refused';
ck refused(q[role Yy1[$x] does Yy1 { }]), True, '…with a parameter list too';
ck refused(q[class Aa1 is Aa1 { }]), True, 'a class that is itself is refused';

say 'PASS';
