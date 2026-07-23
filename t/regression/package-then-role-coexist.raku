# Regression: a bare `package`/`module` block is a weak namespace declaration
# and may coexist with a later `class`/`role`/`grammar` of the same name that
# refines it. Surfaced by Cro::ResourceIdentifier (a `package Foo { our sub … }`
# followed by `role Foo { … }` in the same file). A genuine class/class or
# role/role clash must STILL be rejected.
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

# module then class
module P2 { }
class P2 { method g() { 7 } }
ck(P2.new.g, 7, 'module-then-class refines the name');

# role then package (reverse order)
role P3 { method g() { 5 } }
package P3 { }
class C3 does P3 { }
ck(C3.new.g, 5, 'role-then-package still composes');

say $ok == $n ?? 'PASS' !! "FAIL ($ok/$n)";
