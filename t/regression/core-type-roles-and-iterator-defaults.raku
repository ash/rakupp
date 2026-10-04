# Regression: what the core types say about their roles and ancestry, the
# Iterator role's default methods, and how a parameterized role shows its
# value arguments. Found running mutsu's own t/ suite (oo/role/, 2026-10-04).
#
# - Hash IS a Map; Map/Hash/Range/Pair/Seq and the QuantHash family have
#   their Rakudo MROs (Seq does Sequence); `.^roles` answers for core types,
#   `:!transitive` too; a role type object's REPR is Uninstantiable and a
#   buffer's is VMArray.
# - A class that does Iterator gets push-exactly, push-all, sink-all,
#   skip-one, … built on its pull-one, and `.can` sees them.
# - `R["x"].^name` is R[Str] (the key still tells R[5] from R[6]); a role
#   named like a quote keyword (`my role Q[&f]`) is parameterized, not quoted.
#
# Every expectation below was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}
sub names(\list) { list.map(*.^name).join(',') }

# --- ancestry and roles of core types ----------------------------------------
ck(Hash.isa(Map), True, 'Hash isa Map');
ck(names(Hash.^mro), 'Hash,Map,Cool,Any,Mu', 'Hash.^mro');
ck(names(Range.^mro), 'Range,Cool,Any,Mu', 'Range.^mro');
ck((1, 2).Seq ~~ Sequence, True, 'a Seq does Sequence');
ck(names(Int.^roles), 'Real,Numeric', 'Int.^roles');
ck(names(Int.^roles(:!transitive)), 'Real', 'Int composes Real directly');
ck(names(Seq.^roles(:!transitive)), 'Sequence,Iterable', 'Seq direct roles');
ck(names(Mix.^roles), 'Mixy,Baggy,QuantHash,Associative', 'Mix.^roles');
role R0 { }
ck((R0.REPR, Positional.REPR, Int.REPR, Buf.new.REPR), ('Uninstantiable', 'Uninstantiable', 'P6opaque', 'VMArray'),
   'REPR of roles, a class and a buffer');

# --- the Iterator role's defaults -------------------------------------------
class I does Iterator {
    has $.n = 0;
    method pull-one { $!n < 3 ?? $!n++ !! IterationEnd }
}
my @a;
ck(I.new.push-exactly(@a, 2), 2, 'push-exactly answers the count');
ck(@a, [0, 1], '... and pushed that many');
my @b;
ck(I.new.push-all(@b) =:= IterationEnd, True, 'push-all answers IterationEnd');
ck(@b, [0, 1, 2], '... having pushed everything');
ck(I.new.skip-at-least(5), 0, 'skip-at-least past the end');
ck(I.new.is-lazy, False, 'is-lazy defaults to False');
ck(I.new.can('push-all').so, True, '.can sees the defaults');

# --- parameterized role names -----------------------------------------------
role R[$x] { method v { $x } }
ck((R["x"].^name, R[5].^name, R[Int].^name), ('R[Str]', 'R[Int]', 'R[Int]'), 'value arguments show their type');
class C does R[5] { }
ck((C ~~ R[5], C ~~ R[6], C.new.v), (True, False, 5), 'R[5] is still not R[6]');
my role Q[&f] { }
sub foo($a) { }
ck((Q[{ 1 }].^name, Q[&foo].^name, Q[* + 1].^name), ('Q[Block]', 'Q[Sub]', 'Q[WhateverCode]'),
   'a role named Q is parameterized, not a quote');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
