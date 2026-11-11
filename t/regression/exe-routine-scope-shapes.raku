# --exe: routines whose meaning depends on the scope around them stay
# interpreted when the program bundles (a role or a package makes it), rather
# than getting a native body each: compiled, a role method's `$?CLASS.^name`
# answered "::?CLASS" (it is the class that composes the role) and a generic
# role's `T()` died "Undefined routine 'T'"; a module sub's `our $v; $v++`
# started from Any on every call (an `our` is its package's); a sub's own
# `use` imported nothing its body saw; and a parameter's `@p.VAR.name` was
# "element" where it names the caller's `my @x` (the same refusal holds for a
# program compiled whole). Found by t/exe/parity.raku
# (sigilled-compile-time-class, generic-role-bodies, our-belongs-to-its-package,
# our-is-one-container, ecosweep-2026-09, param-container-name). Every line
# answers the same under Rakudo 2026.09.
#
# Contract: exit 0 + last line PASS.
use lib $?FILE.IO.parent.add('lib').Str;
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

role Marked { method mine { $?CLASS.^name } }
class Taker does Marked { }
class Other does Marked { }
check Taker.new.mine, 'Taker', "a role method's \$?CLASS";
check Other.new.mine, 'Other', '…follows the consumer';

role R[::T] { method mk($v) { T($v) } }
class C does R[Int] { }
check C.new.mk('42'), 42, "a generic role's T()";

module M1 { our $v = 2; our sub g { our $v; $v++; $v } }
check M1::g(), 3, "a module sub's our";
check M1::g(), 4, '…is one container';

sub stash-foo { 'outer' }
sub inner-use { use RakuppStashTermFoo; stash-foo() }
check inner-use(), 'foo', "a sub's own use";

my @x = 1, 2;
sub pos-p(@p) { @p.VAR.name }
check pos-p(@x), '@x', "a parameter's .VAR.name";

if @fail { .say for @fail; say 'FAIL'; exit 1 }
say 'PASS';
