# Regression (issue #255): a program compiled with `--exe` never bound
# `@*ARGS` in the interpreter's scope — its main sets the arguments aside
# and never runs the code a run binds them with — so a module routine
# reading `@*ARGS` (Getopt::Long's `get-options-from(@*ARGS, …)`) bound Any:
# "Type check failed in binding to parameter '@args'; expected Positional but
# got Any". And native code read `@*ARGS` as a List, not the Array it is.
# This program compiles natively (no Pair of a variable goes to the module).
#
# t/exe/parity.raku compiles this file and compares the binary with the
# interpreter. Contract: exit 0 + last line PASS. Runs unchanged under Rakudo.
use lib $?FILE.IO.parent.add('lib').Str;
use RakuppOptionStore;

my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku}") unless $got eqv $want }

ck args-seen(), '[]', 'a module routine sees @*ARGS, an empty Array';
ck @*ARGS.raku, '[]', '…and so does the program';
ck args-via-capture('v' => 0), 0, '@*ARGS through a capture binds a Positional';
@*ARGS.push('pushed');
ck args-seen(), '["pushed"]', 'one @*ARGS, the program\'s write seen by the module';

say @fail ?? "FAIL\n" ~ @fail.join("\n") !! 'PASS';
