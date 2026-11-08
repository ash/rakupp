# A typed read-only `$` parameter is bound to its value with no Scalar
# around it, as Rakudo's binder does, so its `.VAR` is the value's type:
# `sub t(Channel $in) { $in.VAR.^name }` said Scalar, where Rakudo says
# Channel. An Iterable (a List, a Hash, a Range, an Array, a Map) is still
# itemized, and an untyped or Any parameter, an `is copy` or an `is rw` one
# has its Scalar; a named, an optional or a defaulted typed one is bare too,
# in a method and a pointy block as in a sub.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

sub t(Channel $in) { $in.VAR.^name }
sub s(Str $s) { $s.VAR.^name }
sub l(List $l) { $l.VAR.^name }
sub a($a) { $a.VAR.^name }
sub y(Any $a) { $a.VAR.^name }
sub i(Int $i is copy) { $i.VAR.^name }
sub w(Int $i is rw) { $i.VAR.^name }
sub h(Hash $h) { $h.VAR.^name }
sub r(Range $r) { $r.VAR.^name }
sub ar(Array $r) { $r.VAR.^name }
sub m(Map $m) { $m.VAR.^name }
sub n(Int :$n) { $n.VAR.^name }
sub o(Int $o?) { $o.VAR.^name }
sub d(Int $d = 5) { $d.VAR.^name }
class C { method m(Str $s) { $s.VAR.^name } }
my &p = -> Str $s { $s.VAR.^name };

my $x = 1;
my @ar = 1, 2;
ck t(Channel.new), 'Channel', 'a Channel';
ck s('x'), 'Str', 'a Str';
ck s(Str), 'Str', 'a type object';
ck (l((1, 2)), h({a => 1}), r(1..2), ar(@ar), m(Map.new)), ('Scalar' xx 5).List, 'an Iterable is itemized';
ck (a(1), a($x), y($x)), ('Scalar' xx 3).List, 'untyped and Any';
ck (i(1), w($x)), ('Scalar', 'Scalar'), 'is copy and is rw';
ck (n(:n(1)), o(), o(2), d(), d(3)), ('Int' xx 5).List, 'named, optional and defaulted';
ck (C.m('x'), p('y'), (for 'a' -> Str $s { $s.VAR.^name }).head), ('Str' xx 3).List, 'a method, a pointy block, a for';

say 'PASS';
