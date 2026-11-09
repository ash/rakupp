# A multi method no candidate takes is refused with the candidates'
# signatures, as Rakudo lists them and as a multi sub's refusal already
# did: `A.new.m("x")` said "Cannot resolve caller m(A:D: Str:D); none of
# these signatures matches" and stopped there, where Rakudo goes on with a
# line for each candidate, `(A $:: Int, *%_)`; a role's candidate names the
# class that composed the role.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
sub msg(&c) { try c(); $!.message }

class A { multi method m(Int $a) { }; multi method m(Str $b, Int $c) { } }
ck msg({ A.new.m(1.5) }),
   "Cannot resolve caller m(A:D: Rat:D); none of these signatures matches:\n    (A \$:: Int \$a, *%_)\n    (A \$:: Str \$b, Int \$c, *%_)",
   'each candidate';
class B { multi method m(Int) { } }
ck msg({ B.new.m("x") }), "Cannot resolve caller m(B:D: Str:D); none of these signatures matches:\n    (B \$:: Int, *%_)", 'an unnamed parameter';
class P { proto method p(|) {*}; multi method p(Int $x, :$n) { } }
ck msg({ P.new.p("y") }), "Cannot resolve caller p(P:D: Str:D); none of these signatures matches:\n    (P \$:: Int \$x, :\$n, *%_)", 'beside a proto';
role R { multi method r(Int $x) { } }; class C does R { }; class D is C { }
ck msg({ C.new.r("z") }), "Cannot resolve caller r(C:D: Str:D); none of these signatures matches:\n    (C \$:: Int \$x, *%_)", "a role's, named for its class";
ck msg({ D.new.r("z") }), "Cannot resolve caller r(D:D: Str:D); none of these signatures matches:\n    (C \$:: Int \$x, *%_)", 'the class that composed it';
ck (try { A.new.m(1.5); 1 }) // $!.^name, 'X::Multi::NoMatch', 'its type, as before';
multi f(Int $x) { }; multi f(Num $y, Str :$z) { }
ck msg({ f("x") }), "Cannot resolve caller f(Str:D); none of these signatures matches:\n    (Int \$x)\n    (Num \$y, Str :\$z)", 'a sub, as before';

say 'PASS';
