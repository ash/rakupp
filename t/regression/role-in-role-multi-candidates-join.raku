# Regression: a role's own multi candidates JOIN those of a role it does
# (issue #77, FCO's item 5).
#
# `role SQLMapping does BaseMapping` puts BaseMapping in SQLMapping's parent
# slot, which is walked rather than copied. A class doing SQLMapping found
# SQLMapping's `mapping` group and stopped there, so BaseMapping's narrower
# `('int' --> 'integer')` never competed with `(Str $value)`: Red's SQL type
# aliases came back untranslated, and SQLite refused an auto-incrementing key
# typed `int` rather than INTEGER.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

role BaseMapping {
    multi method mapping('int' --> 'integer') { }
}
role SQLMapping does BaseMapping {
    multi method mapping(Str $value) { $value }
}
class Mapping does SQLMapping { }

ck Mapping.new.mapping('int'), 'integer', 'the inner role\'s literal candidate wins';
ck Mapping.new.mapping('text'), 'text', '…and the outer role\'s takes the rest';
ck SQLMapping.new.mapping('int'), 'integer', 'a pun of the outer role too';

role Big { multi method m(Int $x where * > 3) { 'big' } }
role Small does Big { multi method m(Int $v) { 'small' } }
class Sized does Small { }
ck Sized.new.m(5), 'big', 'a where-constrained candidate from the inner role';
ck Sized.new.m(1), 'small', '…falls back to the outer one';

role A { multi method w('a') { 'A' } }
role B does A { multi method w('b') { 'B' } }
role C does B { multi method w(Str $s) { "C:$s" } }
class Deep does C { }
ck (Deep.new.w('a'), Deep.new.w('b'), Deep.new.w('c')).join(' '), 'A B C:c', 'three roles deep';

# the outer role's candidate still overrides a same-signature inner one
role Inner { multi method o(Int $v) { 'inner' } }
role Outer does Inner { multi method o(Int $v) { 'outer' } }
class Over does Outer { }
ck Over.new.o(1), 'outer', 'a same-signature candidate stays overridden';

# …and a class's own candidates join both
role R1 { multi method j(Int $v) { 'R1' } }
role R2 does R1 { multi method j(Str $v) { 'R2' } }
class Own does R2 { multi method j(Num $v) { 'own' } }
ck (Own.new.j(1), Own.new.j('x'), Own.new.j(1e0)).join(' '), 'R1 R2 own', 'with a class candidate too';

say 'PASS';
