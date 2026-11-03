# Regression: `class A::B` and `use A::B` declare the package A as well, as in
# Rakudo, but Raku++ made only A::B: a bare `A` was "Undeclared name 'A'" and
# `::('A')` a Failure, so `A.WHO<B>` could not walk down to the module, and
# neither could the Python binding's `raku.main.A.B`, which walks the same
# stashes. A later `class A` or `module A` is the real A and replaces the stub.
#
# Expectations checked against Rakudo 2026.09
# (~/.rakubrew/versions/moar-2026.09/install/bin/rakudo). Green on both.

use lib $?FILE.IO.parent.add('lib').Str;
use RakuppImplied::Leaf;

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

ck ::('RakuppImplied').^name, 'RakuppImplied', "a used module's leading name is a package";
ck RakuppImplied.HOW.^name, 'Perl6::Metamodel::PackageHOW', '…made by PackageHOW';
ck RakuppImplied.WHO.keys.List, ('Leaf',), '…holding the module';
ck RakuppImplied.WHO<Leaf>.WHO<&hi>(), 'leaf', '…so the stashes walk down to its sub';

class Shelf::Box::Lid { method m { 'lid' } }
ck Shelf.^name, 'Shelf', 'a class declares each package its name passes through';
ck ::('Shelf::Box').HOW.^name, 'Perl6::Metamodel::PackageHOW', '…the middle one too';
ck Shelf.WHO<Box>.WHO<Lid>.m, 'lid', '…and they walk down to it';

class Hall::Door { }
class Hall { method m { 'hall' } }
ck Hall.m, 'hall', 'a later class replaces the stub';
ck Hall.HOW.^name, 'Perl6::Metamodel::ClassHOW', '…and is made by ClassHOW';
ck Hall.WHO.keys.List, ('Door',), '…keeping what the stub held';

class Porch::Step { }
my class Porch { method m { 'porch' } }
ck Porch.m, 'porch', 'a later `my class` replaces it too';
ck Porch.HOW.^name, 'Perl6::Metamodel::ClassHOW', '…and is made by ClassHOW';

module Yard::Gate { }
module Yard { our sub g { 'g' } }
ck Yard::g(), 'g', 'a later module replaces the stub';
ck Yard.HOW.^name, 'Perl6::Metamodel::ModuleHOW', '…and is made by ModuleHOW';

class Attic::Trunk { }
ck Attic.^name, 'Attic', 'a package a class implies may be named before a later `package` of it';
package Attic { our $x = 5 }
ck $Attic::x, 5, '…which then adds to the package';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
