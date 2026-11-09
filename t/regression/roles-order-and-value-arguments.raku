# `.^roles` answers as Rakudo's does: a class's own roles last-declared
# first, each followed by the roles it does (a role's own `does` in the order
# written); a role parameterized by a value is named by the value's type,
# `R2[Int]` for `does R2[1]`; and a parameterized role's type object lists
# what it does, not itself (`R2[1].^roles` is empty). Each listed the roles
# as declared, named `R2[1]`, and put the role itself first.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
sub names(\t, *%o) { t.^roles(|%o).map(*.^name).List }

role A { }; role B { }; role R1 { }; role R2 does R1 { }; role R5 { }
role R7 does R1 does R5 { }
class G does A does B { }
class C does R2 does R5 { }
class D is C does A { }
class E does R7 { }
ck names(G), <B A>, 'the last first';
ck names(C), <R5 R2 R1>, 'each with what it does';
ck names(C, :!transitive), <R5 R2>, ':!transitive';
ck (names(D), names(D, :local)), (<A R5 R2 R1>, ('A',)), "a parent's after";
ck (names(E), names(R7)), (<R7 R1 R5>, <R1 R5>), "a role's in the order written";

role P[$x] { }; role Q[Int $x, Str $y] { }; role P4[$x] does R1 { }
class K does P[1] does Q[2, "b"] { }
class F does P[1.5] { }
ck (names(K), names(F)), (<Q[Int,Str] P[Int]>, ('P[Rat]',)), 'a value by its type';
ck K ~~ K.^roles[1], True, 'and it is the role K does';
ck (names(P[1]), names(P[Int]), names(P4[1])), ((), (), ('R1',)), 'a role type lists what it does';
ck (names(P[1].new), names(P4[1].new)), (('P[Int]',), <P4[Int] R1>), 'its pun the role too, as before';

say 'PASS';
