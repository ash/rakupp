# A method two roles bring is refused in Rakudo's words: the roles listed
# last-composed first, "(C, B, A)" for `does A does B does C`, where they
# came in name order; a multi candidate two roles bring with one signature
# is X::Role::Unresolved::Multi, "Multi method 'm' with signature
# :(E $:: $a, *%_)' must be resolved by class E …", naming the first such
# candidate, where it was said as an only method; and an only method from one
# role beside a multi of its name from another is refused, "Cannot have a
# multi candidate for 'm' when an only method is also in the package 'E'",
# where the class took them both. A stub is no clash, and candidates of
# different signatures merge as before.
# Every line answers the same under Rakudo 2026.09.

use MONKEY-SEE-NO-EVAL;
sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
sub refused($code) { try EVAL $code; $! ?? ($!.^name.subst('+{X::Comp}', ''), $!.message) !! 'lived' }
sub tail($c, $roles) { " must be resolved by class $c because it exists in multiple roles ($roles)" }
my $both = "Cannot have a multi candidate for 'm' when an only method is also in the package ";

ck refused('role A1 { method m { } }; role B1 { method m { } }; role C1 { method m { } }; class E1 does A1 does B1 does C1 { }'),
   ('X::Role::Unresolved::Method', "Method 'm'" ~ tail('E1', 'C1, B1, A1')), 'three roles';
ck refused('role A2 { method m { } }; role B2 { method m { } }; role C2 { method n { } }; class E2 does C2 does A2 does B2 { }'),
   ('X::Role::Unresolved::Method', "Method 'm'" ~ tail('E2', 'B2, A2')), 'two of three';
ck refused('role A3 { multi method m($a, :$x!) { "A" } }; role B3 { multi method m($a, :$x!) { "B" } }; class E3 does A3 does B3 { }'),
   ('X::Role::Unresolved::Multi', "Multi method 'm' with signature :(E3 \$:: \$a, :\$x!, *%_)'" ~ tail('E3', 'B3, A3')), 'a multi';
ck refused('role A4 { multi method m($a) { }; multi method m(Int $a, $b) { } }; role B4 { multi method m(Int $a, $b) { }; multi method m($a) { } }; class E4 does A4 does B4 { }'),
   ('X::Role::Unresolved::Multi', "Multi method 'm' with signature :(E4 \$:: Int \$a, \$b, *%_)'" ~ tail('E4', 'B4, A4')),
   'the first in conflict';
ck refused('role A5 { method m { } }; role B5 { multi method m($a) { } }; class E5 does A5 does B5 { }'),
   ('X::Comp::AdHoc', $both ~ "'E5'"), 'only and multi';
ck refused('role B6 { multi method m($a) { } }; role A6 { method m { "a" } }; class E6 does B6 does A6 { }'),
   ('X::Comp::AdHoc', $both ~ "'E6'"), 'multi and only';
ck EVAL(q[role A7 { method m { ... } }; role B7 { multi method m($a) { 1 } }; class E7 does A7 does B7 { }; E7.new.m(2)]), 1,
   'a stub is no clash';
ck EVAL(q[role A8 { multi method m(Int $a) { "A" } }; role B8 { multi method m(Str $a) { "B" } }; class E8 does A8 does B8 { }; E8.new.m(1) ~ E8.new.m("s")]),
   'AB', 'two signatures merge';
ck EVAL(q[class P9 { method m { 1 } }; role B9 { multi method m($a) { 2 } }; class E9 is P9 does B9 { }; E9.new.m(5)]), 2,
   'a parent class is no role';

say 'PASS';
