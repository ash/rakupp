# An unknown parent is reported in Rakudo's words, "'C' cannot inherit from
# 'nosuch' because it is unknown.", where the class's said "Class 'C' cannot
# inherit …" and a subset's trait another wording; a suggestion follows as
# Rakudo's does ("Did you mean 'Grammar'?"), and `.suggestions` is an Array.
# A role's `is` that no type and no `trait_mod:<is>` answers is an unknown
# parent too: `role R is nosuch {}` and `role R is Nosuch {}` compiled, the
# name dropped. A role still takes a class as its parent, its own type
# capture (`role RC[::T] is T`), and a trait a candidate takes.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
sub refusal($code) {
    try { my $r = EVAL $code; return $r ~~ Str ?? $r !! 'lived' }
    $!.^name eq 'X::Inheritance::UnknownParent' ?? $!.message !! $!.^name ~ ': ' ~ $!.message
}

ck refusal('class C is nosuch { }'), "'C' cannot inherit from 'nosuch' because it is unknown.", 'a class';
ck refusal('grammar G is Gramar { }'),
   "'G' cannot inherit from 'Gramar' because it is unknown.\nDid you mean 'Grammar'?\n", 'a suggestion';
ck refusal('module M is nosuch { }'), "'M' cannot inherit from 'nosuch' because it is unknown.", 'a module';
ck refusal('subset S is nosuch of Int'), "'S' cannot inherit from 'nosuch' because it is unknown.", 'a subset';
ck refusal('role R is nosuch { }'), "'R' cannot inherit from 'nosuch' because it is unknown.", 'a role';
ck refusal('role R is Nosuch { }'), "'R' cannot inherit from 'Nosuch' because it is unknown.", 'a role, upper case';
ck refusal('role R[::T] is nosuch { }'), "'R' cannot inherit from 'nosuch' because it is unknown.",
   'a parametric role';
try EVAL 'class C is nosuch { }';
ck ($!.child, $!.parent, $!.suggestions), ('C', 'nosuch', []), 'its attributes';
try EVAL 'grammar G is Gramar { }';
ck $!.suggestions, ['Grammar'], 'its suggestions';

# (each declares names of its own: a refused declaration leaves its name behind)
ck refusal('role R1 is Exception { }; class C1 does R1 { }; C1.^parents.raku'), '(Exception,)', 'a role is a class';
ck refusal('class P2 { }; role R2 is P2 { }; class C2 does R2 { }; C2.^parents.raku'), '(P2,)',
   'a role is a user class';
ck refusal('role R4[::T] is T { }; class C4 does R4[Exception] { }; C4.^parents.raku'), '(Exception,)',
   "a role's own type capture";
ck refusal('multi trait_mod:<is>(Mu:U $r, :$description!) { }; role Loser is description { }; 1'), 'lived',
   'a trait a candidate takes';
ck refusal('role R3 is export is rw is hidden is DEPRECATED is implementation-detail { has $.x }; 1'), 'lived',
   'the built-in traits';

say 'PASS';
