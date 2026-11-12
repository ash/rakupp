# Regression: `PROCESS::<$X> = v` INSTALLS the process-wide dynamic `$*X`.
# Compiled with `--exe`, the store went through the write a plain `$*X = v`
# takes, which refuses a name nothing declares, so Cro's
# set-unhandled-error-reporter died "Dynamic variable
# $*CRO-UNHANDLED-ERROR-REPORTER not found" and the server never started.
# t/exe/parity.raku compares the binary with the interpreter.
# Contract: exit 0 + last line PASS. Runs unchanged under Rakudo.
my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku}") unless $got eqv $want }

sub install($v) { PROCESS::<$PSS-REPORTER> = $v }
install('r1');
ck $*PSS-REPORTER, 'r1', 'a routine installs a PROCESS dynamic';
PROCESS::<$PSS-TOP> = 7;
ck $*PSS-TOP, 7, '…and the mainline';
ck (try { $*PSS-NOWHERE = 1; 'wrote' }) // 'died', 'died', 'a plain $*X write still needs a declaration';

say @fail ?? "FAIL\n" ~ @fail.join("\n") !! 'PASS';
