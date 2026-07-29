# From the v2 module battery: three modules died at load time on package-stash
# writes rakupp could not represent —
#
#   Advent::GrammarProfiler   EXPORTHOW.WHO.<grammar> = ProfiledGrammarHOW
#   Sparrow6::DSL             BEGIN for <&a &b> { EXPORT::DEFAULT::{$_} = ::($_) }
#   RuntimeCreatedPackage     ::GLOBAL.WHO<X> := Metamodel::PackageHOW.new_type(…)
#
# The root cause was one thing: `.WHO` built a FRESH empty Hash on every call, so
# there was nothing persistent to assign into ("Target is not assignable"). WHO
# now answers one shared stash per package name, the lvalue path routes
# assignment through it, and `Foo::{EXPR}` (runtime key) desugars to WHO.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

# the stash persists across calls — this is the heart of the bug
my module M {}
M.WHO.<x> = 42;
check(M.WHO.<x>, 42, '.WHO.<k> assigns and reads back');
M.WHO.<x> = 43;
check(M.WHO.<x>, 43, 'and re-assigns through the same stash');

# both slot syntaxes see each other's writes
check(M::<x>, 43, 'Foo::<k> reads a WHO-written symbol');
my $k = 'dyn';
M::{$k} = 'via-brace';
check(M.WHO.<dyn>, 'via-brace', 'Foo::{EXPR} writes with a runtime key');

# the Sparrow6 shape: re-export by stash assignment in a BEGIN loop
sub helper-a { 'A' }
sub helper-b { 'B' }
my package EXPORT::DEFAULT {}
BEGIN for <&helper-a &helper-b> { EXPORT::DEFAULT::{$_} = ::($_) }
check(EXPORT::DEFAULT::{'&helper-a'}.defined, True, 'the BEGIN re-export loop populates the stash');
check(EXPORT::DEFAULT::{'&helper-b'}(), 'B', 'and the stored sub is callable');

# the GLOBAL shape from roast's RuntimeCreatedPackage
# (read back through WHO — `GLOBAL::<x>` routes through the pseudo-package
# machinery, a separate path this fix does not touch)
::GLOBAL.WHO<CreatedAtRuntime> := Metamodel::PackageHOW.new_type(name => 'CreatedAtRuntime');
check(::GLOBAL.WHO<CreatedAtRuntime>.^name, 'CreatedAtRuntime', 'a runtime package binds into GLOBAL.WHO');

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
