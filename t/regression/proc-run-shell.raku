# Regression (github.com/ash/rakupp issue #10): `run`/`shell` on Windows.
#
#   * `shell` ran its argument through a hardcoded `/bin/sh -c`. There is no
#     /bin/sh on Windows, so EVERY shell() call there failed with exitcode -1 and
#     no output whatsoever. It uses %COMSPEC% (cmd.exe) there now.
#   * the Windows spawn passed GetStdHandle(STD_INPUT_HANDLE) straight into
#     STARTF_USESTDHANDLES, which requires all three handles to be valid AND
#     inheritable. GetStdHandle guarantees neither — with stdin redirected, or no
#     console at all, it can hand back INVALID_HANDLE_VALUE and then CreateProcess
#     fails for every command.
#   * a failed spawn returned exitcode -1 in silence. That is undiagnosable: the
#     reporter saw `exitcode -1`, empty out-str, empty err-str and nothing else.
#     The OS error text is reported now.
#   * CreateProcess cannot start a .bat/.cmd directly, so a not-found spawn
#     retries through the command processor.
#   * every argument was quoted unconditionally when building the Windows command
#     line, which breaks a switch — cmd.exe does not recognise a quoted `"/c"`.
#     Quoting is now applied only to arguments that need it.
#
# The Windows halves cannot be exercised here; what this pins is that the POSIX
# behaviour they share is unchanged, including the argument-quoting rule.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# shell goes through the command processor, so its argument is one command line
check(shell('echo hello', :out).out.slurp(:close).trim, 'hello', 'shell captures stdout');
check(shell('echo a b   c', :out).out.slurp(:close).trim, 'a b c', 'the shell collapses the spacing');
check(shell('exit 3').exitcode, '3', 'and the exit code comes back');

# run takes an argv — an argument containing a space stays ONE argument
check(run('echo', 'x y', :out).out.slurp(:close).trim, 'x y', 'a spaced argument survives');
check(run('printf', '%s-%s', 'a', 'b', :out).out.slurp(:close), 'a-b', 'several arguments');
check(run('printf', '%s', '', :out).out.slurp(:close), '', 'an EMPTY argument is still an argument');
check(run('printf', '%s', 'a"b', :out).out.slurp(:close), 'a"b', 'and one containing a quote');

# .command reports what was asked for
check(run('echo', 'hi', :out).command.join('|'), 'echo|hi', 'run .command is the argv');
check(shell('echo hi', :out).command.join('|'), 'echo hi', 'shell .command is the command string');

# a missing command does not throw; it reports through the Proc
my $missing = run('definitely-not-a-real-cmd-xyz', :out, :err);
check(($missing.exitcode != 0).gist, 'True', 'a missing command is a nonzero exit');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
