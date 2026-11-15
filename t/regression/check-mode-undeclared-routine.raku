# Regression: `-c` refuses an undeclared routine, as a run does (issue #264).
#
#     say "start";
#     no-such-routine(1);
#
# A run refused this before its first statement ("Undeclared routine:
#     no-such-routine used at line 2"), but `-c` printed "Syntax OK" and exited
# 0 — it ran the undeclared-variable and will-never-work checks and not the
# routine one, so a file could pass `-c` in CI and still not compile. Rakudo's
# `-c` reports the same compile error as its run and exits 1.
#
# `-c` now asks the question the run asks (Interpreter::checkUndeclaredCalls),
# on the same terms: plain statements only, so a program with declarations is
# not refused here.
#
# Contract: exit 0 + last line PASS.
my @fail;

sub rakupp(*@args) {
    my %env = %(%*ENV.grep({ .key ne 'RAKUPP_NO_DECLCHECK' }));
    my $p = run($*EXECUTABLE, |@args, :out, :err, :%env);
    my %r = out => $p.out.slurp(:close), err => $p.err.slurp(:close);
    %r<exit> = $p.exitcode;
    %r;
}

my $f = $*TMPDIR.add("rakupp-264-{$*PID}.raku");
$f.spurt(qq:to/END/);
say "start";
no-such-routine(1);
END

# 1. the reported file: -c refuses it with the run's report, byte for byte
my %run = rakupp($f.Str);
my %chk = rakupp('-c', $f.Str);
@fail.push("-c exit {%chk<exit>}, want 1") unless %chk<exit> == 1;
@fail.push("-c still says Syntax OK") if %chk<out>.contains('Syntax OK');
@fail.push("-c report {%chk<err>.raku} is not the run's {%run<err>.raku}")
    unless %chk<err> eq %run<err>;
@fail.push("report names the routine and line")
    unless %chk<err>.contains('no-such-routine used at line 2');
@fail.push("-c ran the program") if %chk<out>.contains('start');

# 2. -e, and Rakudo's suggestions come through
my %e = rakupp('-c', '-e', 'say length("abc")');
@fail.push("-e: exit {%e<exit>}") unless %e<exit> == 1;
@fail.push("-e: no suggestion in {%e<err>.raku}") unless %e<err>.contains("Did you mean 'elems'");

# 3. --json gives a finding, not "Syntax OK"
my %j = rakupp('-c', '--json', $f.Str);
@fail.push("--json: exit {%j<exit>}") unless %j<exit> == 1;
@fail.push("--json: {%j<out>.raku}") unless %j<out>.contains('undeclared-routine');

# 4. what must still pass
for 'say 1; say "x".uc; dd 42',
    'my &f = { 42 }; say f()',
    'sub f { 1 }; say f()',
    'say f(); sub f { 1 }',
    'say infix:<+>(1, 2)' -> $code {
    my %ok = rakupp('-c', '-e', $code);
    @fail.push("allowed {$code.raku}: exit {%ok<exit>} err={%ok<err>.trim}")
        unless %ok<exit> == 0 && %ok<out>.contains('Syntax OK');
}

$f.unlink;
if @fail { .note for @fail; say "FAIL"; exit 1 }
say "PASS";
