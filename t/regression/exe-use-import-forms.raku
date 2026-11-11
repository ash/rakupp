# --exe: the forms of `use` that import something other than the module's
# DEFAULT exports do so compiled as interpreted. The binary's `use` loaded the
# module and imported its defaults whatever the statement said: `use M <a>`
# ran M's `sub EXPORT` with no arguments, so a routine it hands out for an
# import list was "Undefined routine", and `need M` and `use M ()` — which
# import nothing — imported everything. Found by t/exe/parity.raku
# (unit-stash-exists-under-import-list.raku, which runs the import list
# natively; asking what was imported is a MY:: lookup, so this file bundles).
# Every line answers the same under Rakudo 2026.09.
#
# Contract: exit 0 + last line PASS.
use lib $?FILE.IO.parent.add('lib').Str;
use RakuppUnitExistsProbe <rakupp-uep-greet>;
need RakuppStashTermFoo;
use RakuppStashTermBar ();

my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

check uep-report-list()<names>, ('rakupp-uep-greet',), 'an import list reaches sub EXPORT';
check (MY::<&stash-foo>:exists), False, '`need` imports nothing';
check (MY::<&stash-bar>:exists), False, '`use M ()` imports nothing';

if @fail { .say for @fail; say 'FAIL'; exit 1 }
say 'PASS';
