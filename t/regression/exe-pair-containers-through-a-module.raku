# Regression (issue #255): Getopt::Long's `get-options("length=i" => $length)`
# worked interpreted and failed compiled with `--exe`, in two ways:
#   - a compiled program never bound `@*ARGS` (exe-args-in-a-module.raku);
#   - an element BOUND to a variable (`%h{.key} := .value`, a Proxy over the
#     variable's cell) was overwritten, not written through, by a native
#     method's `$!values{$k} = $v`, so `$length` never saw its option. A
#     native module routine's `%h{…} := …` now keeps the routine interpreted,
#     and a native program handing `"k" => $var` to a module's routine
#     bundles: its Pair could hold only a copy of the C++ variable.
#
# t/exe/parity.raku compiles this file and compares the binary with the
# interpreter. Contract: exit 0 + last line PASS. Runs unchanged under Rakudo.
use lib $?FILE.IO.parent.add('lib').Str;
use RakuppOptionStore;

my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku}") unless $got eqv $want }

my $length = 1;
my $file   = 'default';
ck store-options([5, 'x.txt'], 'length' => $length, 'file' => $file), 5, 'the store reads back';
ck ($length, $file), (5, 'x.txt'), 'and writes through to the variables';


say @fail ?? "FAIL\n" ~ @fail.join("\n") !! 'PASS';
