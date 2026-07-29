# From the Cognates port (docs/rakupp-findings finding 5): a sub whose RETURN type
# names a class declared in the SAME module could not be called from another
# compilation unit — "Type 'Thing' is not declared", raised at CALL time and naming
# a type the caller never mentioned. A class in `unit module M` registers as
# `M::Thing` while the routine writes `Thing`; classAliases_ holds that mapping and
# every other type-check path consulted it, but the return check did not.
# Contract: exit 0 + last line PASS.
use lib $?FILE.IO.parent.add('lib').Str;
use RT::SameMod;

my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

check(Thing.new(name => 'a').name, 'a',        'the class itself imports');
check(make('d').name,              'd',        'a sub with a --> SameModuleClass return type');
check(describe(Thing.new(name => 'c')), 'thing: c', 'a typed parameter still works');
check(Thing.^name, 'RT::SameMod::Thing',       'the class keeps its qualified name');

# An undeclared return type must STILL be an error — the alias lookup must not
# turn the check off.
my $died = False;
try { EVAL 'sub bad(--> NoSuchTypeAnywhere) { 1 }; bad()'; CATCH { default { $died = True } } }
check($died, True, 'an genuinely undeclared return type still throws');

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
