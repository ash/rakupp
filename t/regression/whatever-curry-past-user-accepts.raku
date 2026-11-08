# A written `* ~~ D` curries when D is a user class with an ACCEPTS of its
# own, as `* ~~ Int` does: applyArithGeneral asked D.ACCEPTS(*) ahead of the
# curry, so `&m` was a Bool and `.map(* ~~ D)` died "Cannot map a List using
# 'Bool::False'". A Whatever VALUE still goes to ACCEPTS.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

class D { method ACCEPTS($x) { $x eq 'long' } }
my &m = * ~~ D;
ck &m.^name, 'WhateverCode', '* ~~ D curries';
ck m('long'), True, '…and asks ACCEPTS when called';
ck <long short>.map(* ~~ D).List, (True, False), 'in a .map';
ck (* !~~ D)('long'), False, '* !~~ D';
ck <long short>.grep(* ~~ D).List, ('long',), 'in a .grep';
my $w = *;
ck $w ~~ D, False, 'a Whatever value asks ACCEPTS';
ck 'long' ~~ D, True, 'a value (as before)';
ck (* ~~ Int)(3), True, '* ~~ Int (as before)';

say 'PASS';
