# Regression: `callframe` in a program compiled with --exe. It reads the
# interpreter's frames, and natively compiled code runs in none of them:
# `callframe(0).line` answered 0. The backend now refuses `callframe` (a program
# bundles the interpreter; a module routine hands that statement to it).
#
# t/exe/run.raku compiles this file and compares the binary with the interpreter.
# Contract: exit 0 + last line PASS. Runs unchanged under Rakudo.
my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku}") unless $got eqv $want }

sub here { callframe(0).line - $?LINE }   # 0: the line callframe is called on
sub bare { callframe.line - $?LINE }
sub caller-line { callframe(1).line }

ck here(), 0, 'callframe(0).line is the line of the call to callframe';
ck bare(), 0, 'callframe.line (no parens)';
my $l = caller-line(); my $at = $?LINE;
ck +$l, $at, 'callframe(1).line is the caller\'s line';

if @fail { .say for @fail; say 'FAIL' }
else     { say 'PASS' }
