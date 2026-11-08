# Regression: a bare `package` block is a weak namespace declaration and may
# coexist with a later `class`/`role`/`grammar` of the same name that refines
# it. Surfaced by Cro::ResourceIdentifier (a `package Foo { our sub … }`
# followed by `role Foo { … }` in the same file). A genuine class/class or
# role/role clash must STILL be rejected, and so are a `module` before a class
# and a `package` after a role: Rakudo 2026.09 refuses both, which this file
# allowed until a second `module` became a redeclaration (package-gaps.raku).
use MONKEY-SEE-NO-EVAL;
my $ok = 0; my $n = 0;
sub ck($got, $want, $desc) {
    $n++;
    if $got eqv $want { $ok++ }
    else { say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# package then role, with an our-sub in the package namespace
package P1 { our sub helper() { 40 } }
role P1 { has $.x; method m() { 2 } }
class C1 does P1 { }
ck(C1.new(x => 9).x, 9, 'package-then-role: attribute composed');
ck(C1.new.m, 2, 'package-then-role: method composed');
ck(P1::helper(), 40, 'package-then-role: our-sub still callable');

# module then class: a module is no weak declaration
ck((try { EVAL 'module P2 { }; class P2 { method g() { 7 } }'; 'ran' }) // $!.^name,
   'X::Redeclaration', 'module-then-class is a redeclaration');

# role then package (reverse order): the package comes after a real one
ck((try { EVAL 'role P3 { method g() { 5 } }; package P3 { }'; 'ran' }) // $!.^name,
   'X::Redeclaration', 'role-then-package is a redeclaration');

say $ok == $n ?? 'PASS' !! "FAIL ($ok/$n)";
