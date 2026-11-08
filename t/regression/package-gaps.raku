# Three package gaps beside Rakudo's: `GLOBAL::.keys` listed nothing, where
# it holds every package-scoped name (`class A::B` gives it A); a second
# `module A {}` compiled, where Rakudo says "Redeclaration of symbol 'A'" (a
# module counts as a class does, and a `package` after either is one too;
# a `package` alone is a weak declaration a later one refines); and `P.new` on
# a package said "No such method 'new'", where Rakudo says "You cannot create
# an instance of this type (P)".
# Every line answers the same under Rakudo 2026.09.

use MONKEY-SEE-NO-EVAL;
sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

class Z2 { }
module Z4 { }
class Z3::Q { }
ck GLOBAL::.keys.grep(/^Z/).sort.List, <Z2 Z3 Z4>, 'GLOBAL:: lists them';
ck GLOBAL::<Z2>.^name, 'Z2', '…as the types they are';

# (each EVAL with names of its own: a package one declares is GLOBAL's after it)
for 'module A1 {}; module A1 {}', 'module A2 {}; class A2 {}', 'class A3 {}; module A3 {}',
    'module A4 {}; package A4 {}' -> $code {
    try EVAL $code;
    ck $!.^name, 'X::Redeclaration', "$code is a redeclaration";
}
for 'package B1 {}; package B1 {}', 'package B2 {}; class B2 {}', 'package B3 {}; module B3 {}',
    'module B4::C {}; module B4 {}', 'module B5 {...}; module B5 {}' -> $code {
    ck (try { EVAL $code; 'ok' }), 'ok', "$code compiles";
}

package P { }
try P.new;
ck $!.message, 'You cannot create an instance of this type (P)', 'P.new';
module M { }
try M.new;
ck $!.message, 'You cannot create an instance of this type (M)', 'M.new';
package Q { }
class Q { has $.x }
ck Q.new(:1x).x, 1, 'a class that refines a package has instances';

say 'PASS';
