# A class's candidate overrides a role's only when the signatures are the
# same, required named arguments included: `class C does I { multi method
# m($a) }` replaced I's `m($a, :$cast!)` too, so `C.new.m(1, :cast)` answered
# the class's. Composition keyed a candidate by its positionals alone.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

role I { multi method m($a, :$cast!) { 'cast' }; multi method m($a) { 'plain' } }
class C does I { multi method m($a) { 'own' } }
ck C.new.m(1, :cast), 'cast', 'the role\'s candidate with the required named stays';
ck C.new.m(1), 'own', 'the class\'s overrides the same signature';
class C2 does I { multi method m($a, :$cast!) { 'own-cast' } }
ck C2.new.m(1, :cast), 'own-cast', 'the same signature, named included, overrides';
ck C2.new.m(1), 'plain', '…and leaves the other';
role Rq { method m($a, :$x!) { ... } }
class F does Rq { method m($a) { 'f' } }
ck F.new.m(1), 'f', 'a stub is fulfilled by a method of the name (as before)';

say 'PASS';
