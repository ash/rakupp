# A multi and an only method of one name are refused when a role a role does
# brings one of them, as Rakudo refuses them: `role S { multi method m($a) {}
# }; role R does S { method m {} }; class E does R {}` ran, where Rakudo says
# "Cannot have a multi candidate for 'm' when an only method is also in the
# package 'E'". And a method two parameterizations of one role bring names
# the role by its own name, `(A, A)`, where it said `(A[2], A[1])`.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
sub msg($code) { try EVAL $code; $!.message }

ck msg(q[role S { multi method m($a) { } }; role R does S { method m { } }; class E does R { }]),
   q[Cannot have a multi candidate for 'm' when an only method is also in the package 'E'], 'a multi from below';
ck msg(q[role S2 { method m { } }; role R2 does S2 { multi method m($a) { } }; class E2 does R2 { }]),
   q[Cannot have a multi candidate for 'm' when an only method is also in the package 'E2'], 'an only from below';
ck msg(q[role T3 { }; role S3 { multi method m($a) { } }; role R3 does T3 does S3 { method m { } }; class E3 does R3 { }]),
   q[Cannot have a multi candidate for 'm' when an only method is also in the package 'E3'], 'a second does';
ck msg(q[role A[$t] { method m { } }; class E4 does A[1] does A[2] { }]),
   q[Method 'm' must be resolved by class E4 because it exists in multiple roles (A, A)], 'a parameterized role by its name';
ck (try { EVAL q[role A5[$t] { method m { } }; class E5 does A5[1] does A5[2] { }]; 1 }) // ($! ~~ X::Role::Unresolved::Method), True, 'its type';

role S6 { multi method m(Int $a) { 1 } }; role R6 does S6 { multi method m(Str $a) { 2 } }; class E6 does R6 { }
role S7 { method m { 1 } }; role R7 does S7 { method m { 2 } }; class E7 does R7 { }
ck (E6.m(1), E6.m("a"), E7.m), (1, 2, 2), 'multis together, and an only over an only, as before';

say 'PASS';
