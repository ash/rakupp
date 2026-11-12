# Regression: attribute traits and a core-type parent in a program compiled
# with --exe. The native class registration copied an attribute's name, type,
# `is rw`, `is required` and default and dropped everything else, silently:
# a `where` or a `:D` type took any value, `handles` answered nothing, `is
# default` read (Any), a shaped `@` attribute was unshaped and `is Set` an
# ordinary Hash; a coercion type refused the value it should coerce; and a
# class whose parent is a core type (`is Int`) died in `.new(7)`. Such a class
# now bundles with the interpreter. A `my (…)` slot with a coercion type
# (`my ($a, Int() $b) = …`) did the same: it type-checked rather than coerced.
#
# Contract: exit 0 + last line PASS. Runs unchanged under Rakudo.
my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku}") unless $got eqv $want }

class A { has Int $.n where * > 0; }
ck (try A.new(n => -1)) // 'refused', 'refused', 'a `where` on an attribute';
class B { has @.items handles <elems>; }
ck B.new(items => [1, 2, 3]).elems, 3, '`handles`';
class D { has Int:D $.x = 1; }
ck (try D.new(x => Int)) // 'refused', 'refused', 'a `:D` attribute';
class E { has $.x is default(5); }
ck E.new.x, 5, '`is default`';
class F { has @.a[2]; }
ck F.new.a.shape, (2,), 'a shaped attribute';
class H { has %.s is Set; }
ck H.new.s.^name, 'Set', 'a container type';
class P { has IO::Path() $.path; }
ck P.new(path => '/tmp').path.^name, 'IO::Path', 'a coercion type';
class G is Int { }
ck G.new(7) + 1, 8, 'a class that is an Int';

my ($a, Int() $b) = "3", "42";
ck ($a, $b), ("3", 42), 'a coercion slot in a list declaration';

if @fail { .say for @fail; say 'FAIL'; exit 1 }
say 'PASS';
