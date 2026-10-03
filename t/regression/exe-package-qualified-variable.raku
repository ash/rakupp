# Regression: a package-qualified variable in a program compiled with --exe.
# `$RakuppPkgVar::greeting` reached the backend's local-variable path and became
# a reference to a C++ local nothing declares, so the C++ compiler failed and
# the whole --exe build with it — instead of falling back to bundling, which is
# what a construct the backend cannot compile is supposed to do. It is refused
# now (a module routine still reaches the name through its frame).
#
# t/exe/run.raku compiles this file and compares the binary with the interpreter.
# Contract: exit 0 + last line PASS. Runs unchanged under Rakudo.
use lib $?FILE.IO.parent.add('lib').Str;
use RakuppPkgVar;
my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku}") unless $got eqv $want }

ck $RakuppPkgVar::greeting, 'hello from the package', 'read an `our` scalar of a used module';
ck @RakuppPkgVar::list.elems, 3, 'read an `our` array of a used module';
$RakuppPkgVar::greeting = 'changed';
ck $RakuppPkgVar::greeting, 'changed', 'assign to it';

if @fail { .say for @fail; say 'FAIL' }
else     { say 'PASS' }
