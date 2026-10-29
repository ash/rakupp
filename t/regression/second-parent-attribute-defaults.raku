# Regression: a class's second parent seeds and checks its attributes too
# (issue #60).
#
# Construction walked the PARENT chain alone, so a class parent reached any
# other way had its attribute defaults skipped: in `class X is A is B` every
# default of B's came back undefined, and in `class F does R is A` — where the
# role took the first parent slot — every default of A's did. `is required`
# on such a parent was never asked for either: the plain-class constructor
# looked for checked attributes on the parent chain alone. FunctionalParsers'
# MermaidJS actions are `does … does … is Raku::AST`, so their inherited
# `rule` and `terminal` actions were undefined and the EBNF parse came back
# as bare lists. Every answer below is Rakudo 2026.09's.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

my @log;
class A { has $.a = do { @log.push('A'); 'a' }; has &.f = { "A:$_" } }
class B { has $.b = do { @log.push('B'); 'b' } }
role R { has $.r = 'r' }

class X is A is B { has $.x = do { @log.push('X'); 'x' } }
my $x = X.new;
ck ($x.a, $x.b, $x.x), ('a', 'b', 'x'), '`is A is B`: the second parent seeds';
ck @log.List, <B A X>, '…least-derived first, the second parent before the first';
ck X.new(b => 5).b, 5, '…and binds a named argument';

class F does R is A { }
ck (F.new.a, F.new.r, F.new.f.(1)), ('a', 'r', 'A:1'), '`does R is A`: the parent behind a role';
class F2 does R { also is A }
ck F2.new.a, 'a', '…and with `also is A`';
class E is A does R { }
ck (E.new.a, E.new.r), ('a', 'r'), '…as `is A does R` always did';

my $made = 0;
class Base { has $.base = do { $made++; 'base' } }
class L is Base { }
class M is Base { }
class D is L is M { }
ck D.new.base, 'base', 'a diamond';
ck $made, 1, '…seeds its base once';

class Q { has $.q is required }
class Y is A is Q { }
ck (try { Y.new; 'built' }) // $!.^name, 'X::Attribute::Required', 'a second parent’s `is required`';
ck Y.new(q => 1).q, 1, '…satisfied';
class T { has Int:D $.t = 4 }
class Z is A is T { }
ck Z.new.t, 4, 'a second parent’s `Int:D` default';

class S { has $.s = 's0'; submethod BUILD(:$s) { } }
class W is A is S { }
ck W.new(s => 'given').s, 's0', 'a second parent with its own BUILD takes no named argument';

say 'PASS';
