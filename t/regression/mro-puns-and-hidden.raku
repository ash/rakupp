# Regression: `.^mro` shows a role a class INHERITS (`is R`, `hides R`) as its
# pun, and `.^mro_unhidden` leaves out what is hidden (ROAST-TRACKS-PLAN C,
# 2026-09-27; 6.c/S12-class/mro-6c.t). A role a class DOES is composed and is
# never in the MRO. Before: no `.^mro_unhidden` at all, and `class C3 is R3a`
# reported C3's MRO without R3a.
#
# Expectations checked against Rakudo 2026.08 via /opt/homebrew/bin/rakudo — NOT
# the bare name `raku`, which on this box has pointed at rakupp. Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}
sub names(\type, :$unhidden) {
    (($unhidden ?? type.^mro_unhidden !! type.^mro).map(*.^name))[0..*-3].join(',')
}

{
    my class C1 { }; my class C2 is C1 is hidden { }; my class C3 is C2 { }
    ck(names(C3), 'C3,C2,C1', 'a hidden class is in the MRO');
    ck(names(C3, :unhidden), 'C3,C1', '…and not in mro_unhidden');
}
{
    my class C1 { }; my class C2 is C1 { }; my class C3 hides C2 { }; my class C4 is C3 { }
    ck(names(C4), 'C4,C3,C2,C1', '`hides` inherits');
    ck(names(C4, :unhidden), 'C4,C3,C1', '…and hides');
}
{
    my class C1 { }; my class C2 is C1 { }; my role R3a hides C2 { }; my class C3 does R3a { }; my class C4 is C3 { }
    ck(names(C4, :unhidden), 'C4,C3,C1', 'a composed role\'s `hides` is the composer\'s');
    ck(names(C3), 'C3,C2,C1', 'a role a class DOES is not in its MRO');
}
{
    my class C1 { }; my class C2 is C1 { }; my role R3a is C2 is hidden { }; my role R3b { }
    my class C3 is R3a is R3b { }; my class C4 is C3 { }
    ck(names(C4), 'C4,C3,R3a,C2,C1,R3b', 'a role a class inherits is in it, as its pun');
    ck(names(C4, :unhidden), 'C4,C3,C2,C1,R3b', 'a hidden role\'s pun is left out');
    ck(C4.^mro_unhidden[4] eqv R3b.^pun, True, 'the pun in the MRO is the role\'s .^pun');
}
{
    my class C1 { }; my class C2 is C1 { }; my role R3a is C2 { }; my role R3b { }
    my class C3 hides R3a is R3b { }; my class C4 is C3 { }
    ck(names(C4, :unhidden), 'C4,C3,C2,C1,R3b', '`hides` a role hides its pun');
}

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
