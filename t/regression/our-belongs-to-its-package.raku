# Regression: an `our` belongs to the package around it, and a typed one keeps
# its type however often it runs (review ledger O28).
#
# A routine's `our $x` named the package of whatever code CALLED it: the
# prefix the running code had, not the one the routine was written in. So a
# method's `our $k` inside `class C` was GLOBAL's `$k`, and so was a package
# sub's, and a class counting its instances with `our $cnt` in `new` counted
# nothing. The package is now found lexically, from the package body the
# routine closed over. A TYPED or defaulted `our` re-run in a loop or a sub
# took a fresh view of the variable each time and kept no record of its
# type: `our Int $x` stopped refusing a Str, `our Str $t ~= "x"` lost what it
# had, `our $d is default(5)` started over. And an `our` in a role's METHOD
# was accepted, where Rakudo refuses it as it does in the role body.
# Answers are Rakudo 2026.09's.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

class C1 { our $k = 5; method k { our $k } }
check C1.k, 5, "a method's our is its class's";
package P1 { our $x = 5; our sub f { our $x } }
check P1::f(), 5, "a package sub's our is its package's";
module M1 { our $v = 2; our sub g { our $v; $v++; $v } }
check M1::g(), 3, 'a module sub increments its own';
check $M1::v, 3, '…the qualified name sees it';
class C2 { our $cnt = 0; method new { our $cnt; $cnt++; self.bless } }
C2.new for ^3;
check $C2::cnt, 3, 'a class counting its instances';
class C3 { method set { our $e = 9 } }
C3.set;
check $C3::e, 9, 'an our first declared in a method';
check (our $e), Any, "…is not GLOBAL's";
class C4 { our $g = 1; method m { for ^3 { our $g; $g++ }; $g } }
check C4.m, 4, "a loop in a method";
check $C4::g, 4, '…writes the class variable';
class Outer { class Inner { our $i = 3; method i { our $i } } }
check Outer::Inner.i, 3, 'a nested class';

for ^3 { our Str $t1 ~= "x" }
check (our $t1), 'xxx', 'a typed our in a loop keeps its value';
for ^3 { our Int $t2; $t2++ }
check (our $t2), 3, 'our Int ++ in a loop';
for ^2 { our $t3 is default(5); $t3++ }
check (our $t3), 7, 'is default, re-run';
our $t4 is default(42);
$t4 = 7;
$t4 = Nil;
check $t4, 42, 'Nil resets it to the default';
my @refused;
for ^2 { our Int $t5; try { $t5 = "str" }; @refused.push: $!.^name }
check @refused, ['X::TypeCheck::Assignment', 'X::TypeCheck::Assignment'], 'a re-run typed our still refuses a Str';
our Int $t6 = 1;
try { $t6 = "str" }
check $!.^name, 'X::TypeCheck::Assignment', 'and the first run did';
check $t6, 1, '…keeping its value';
our Int $t7 = 1;
{ our Int $t7; $t7 = 3 }
check $t7, 3, 'a typed redeclaration in a block';
sub ts { our Int $t8 = 0 unless (our Int $t8).defined; $t8 += 2 }
ts(); ts();
check (our $t8), 4, 'a typed our in a sub';

try EVAL q[role R1 { method r { our $rv = 1 } }];
check $!.^name, 'X::Declaration::OurScopeInRole', "an our in a role's method is refused";
try EVAL q[role R2 { our $x = 1 }];
check $!.^name, 'X::Declaration::OurScopeInRole', '…as in the role body';
role R3 { method m { my class CI { method n { our $ok = 1; $ok } }; CI.n } }
class F3 does R3 { }
check F3.m, 1, "a class inside a role's method has its own package";

if @fail { note "FAILED:\n  " ~ @fail.join("\n  "); say 'FAIL' } else { say 'PASS' }
