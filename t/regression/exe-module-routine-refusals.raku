# Regression: the constructs of the exe-*.raku files of 2026-10-03 inside the
# routines of a module, which --exe compiles to native bodies the interpreter
# enters (src/AotModules.h). There a refused statement is handed to the
# interpreter, or the routine stays interpreted, instead of the whole program
# bundling — so each needs the module side checked too:
#   &?ROUTINE, callframe   — the statement runs in the interpreter, in the frame
#   $Other::var            — reached through the frame, never a C++ local
#   -> &f                  — callable by name in the native body
#   a read-only `$p` write — the WHOLE routine stays interpreted (a delegated
#                            statement would get a writable copy)
#   my Int @b, an enum or a class declared in the routine — interpreted; the
#                            class's methods would not see the native locals
#
# t/exe/run.raku compiles this file and compares the binary with the interpreter.
# Contract: exit 0 + last line PASS. Runs unchanged under Rakudo.
use lib $?FILE.IO.parent.add('lib').Str;
use RakuppAotRefusals;
my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku}") unless $got eqv $want }

for ^2 {   # a plain sub settles its shape on the first call; check the second too
    ck routine-name(), 'routine-name', '&?ROUTINE.name';
    ck routine-fact(5), 120, '&?ROUTINE recursion';
    ck frame-line(), 0, 'callframe(0).line';
    ck other-package(), 'hello from the package!', 'another module\'s `our` variable';
    ck loop-codes(), 'routine-name,hello from the package!', 'for … -> &f { f() }';
    ck (try ro-assign(1)) // 'died', 'died', 'assigning a read-only parameter';
    ck (try ro-incr(1)) // 'died', 'died', 'incrementing a read-only parameter';
    ck ro-copy(1), 5, 'an `is copy` parameter is writable';
    ck typed(), 'Array[Int]', 'my Int @b in a module routine';
    ck captured-class(3), 'n=6', 'a class in a routine sees the routine\'s locals';
    ck computed-enum(), '2 5', 'an enum declared in a routine';
}

if @fail { .say for @fail.unique; say 'FAIL' }
else     { say 'PASS' }
