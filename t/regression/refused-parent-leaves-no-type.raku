# A class or role refused for an unknown parent leaves nothing behind, as
# Rakudo's does: after `try EVAL 'class C is nosuch {}'`, `EVAL 'class C
# {}'` was a redeclaration of C, and so for a role, where Rakudo declares
# it afresh; and the name C, there, is undeclared.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

ck (try { EVAL q[class C is nosuch { }]; 1 }) // $!.^name, 'X::Inheritance::UnknownParent', 'refused';
ck EVAL(q[class C { method m { 5 } }; C.m]), 5, 'then declared';
try EVAL q[role R is nosuch { }];
ck EVAL(q[role R { method m { 6 } }; R.m]), 6, 'a role';
try EVAL q[class C::D is nosuch { }];
ck EVAL(q[class C::D { method m { 7 } }; C::D.m]), 7, 'a qualified name';
try EVAL q[class Gone is nosuch { }];
ck (try { EVAL q[Gone]; 1 }) // $!.^name, 'X::Undeclared::Symbols', 'undeclared';
try EVAL q[module M is nosuch { }];
ck EVAL(q[module M { our sub f { 9 } }; M::f()]), 9, 'a module, as before';

say 'PASS';
