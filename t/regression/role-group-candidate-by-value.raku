# A class's `does W['B']` composes the candidate of W's group that the
# arguments fit, as a run-time `but W['B']` already did: with `role W['A']`
# beside `role W['B']` the class got W['A'] and none of W['B']'s methods,
# because a literal parameter took any value of its type. Among candidates
# that fit, the choice is Rakudo's multi-dispatch over their signatures: the
# narrowest types, then a literal, `where` or captured type that accepted,
# in declaration order, then the one taking exactly that many arguments; an
# untyped role parameter is Mu; and two still tied, or none that fits, is
# X::Role::Parametric::NoSuchCandidate, where the first was taken silently.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
sub refused($code) { try EVAL $code; $! ~~ X::Role::Parametric::NoSuchCandidate }

role W['A'] { method a { 'A' } }
role W['B'] { method x { 'B' } }
class CB does W['B'] { }
ck CB.x, 'B', 'a single-quoted literal picks its candidate';
ck CB.^can('a').elems, 0, '…and not the other one';
role DQ["A"] { method a { "A" } }
role DQ["B"] { method x { "B" } }
class DB does DQ["B"] { }
ck DB.x, 'B', 'a double-quoted one too';
ck (1 but W['A']).a, 'A', 'a run-time mixin, as before';
role N[1] { method r { 'one' } }
role N[2] { method r { 'two' } }
class N2 does N[2] { }
ck N2.r, 'two', 'a numeric literal';

role L['A'] { method r { 'lit' } }
role L[Str $x] { method r { 'str' } }
class LA does L['A'] { }
class LB does L['B'] { }
ck (LA.r, LB.r), ('lit', 'str'), 'a literal before its type, the type for the rest';

role P[Int $x] { method r { 'int' } }
role P[Int $x where * > 0] { method r { 'pos' } }
class P5 does P[5] { }
class Pm does P[-5] { }
ck (P5.r, Pm.r), ('pos', 'int'), 'a `where` that accepts goes first';

role G[$a, $b] { method r { 'diff' } }
role G[::T $a, T $b] { method r { 'same' } }
class GS does G[Array, Array] { }
class GD does G[Int, Hash] { }
ck (GS.r, GD.r), ('same', 'diff'), 'a captured type goes first';

role U[$x] { method r { 'untyped' } }
role U[Any $x] { method r { 'any' } }
class UI does U[Int] { }
ck UI.r, 'any', 'an untyped role parameter is Mu, wider than Any';

role O[$a, $b?] { method r { 'opt' } }
role O[$a] { method r { 'one' } }
class O1 does O[1] { }
ck O1.r, 'one', 'exactly that many arguments, over an optional one';

ck refused(q[role X1['A'] { }; role X1['B'] { }; class C1 does X1['C'] { }]),
   True, 'no candidate fits';
ck refused(q[role X2[$x] { }; role X2[$y] { }; class C2 does X2[5] { }]),
   True, 'two tie';
ck refused(q[role X3[::T] { }; role X3[$x] { }; class C3 does X3[Int] { }]),
   True, 'a capture and an untyped parameter tie';
ck refused(q[role X4[$x] { }; role X4[$y] { }; my $o = 1 but X4[5]]),
   True, '…at run time too';

say 'PASS';
