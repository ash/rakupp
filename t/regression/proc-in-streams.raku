# Regression: `run(:in, …)` ignored the :out/:err adverbs entirely.
#
# A Bool `:in` defers the spawn — the child runs when its stdin is written or
# closed, so input can be fed and output collected without deadlocking. That
# deferred path hardcoded "capture stdout, send stderr to /dev/null", and the
# adverbs never travelled with the Proc. Three consequences:
#
#   * `run(cmd, :in, :out, :err)` came back with an EMPTY .err — the stderr the
#     caller asked for had gone to /dev/null.
#   * `run(cmd, :in)` with no :out swallowed both streams, where Rakudo lets the
#     child write to ours.
#   * `$p.in.close` without a preceding write was a no-op, so the child never
#     ran at all and .out was empty.
#
# The modes now ride on the Proc (out-mode/err-mode) and spawnWithInput honours
# them the same way spawnCapture does: capture, inherit, or discard per stream.
#
# Contract: exit 0 + last line PASS.
my @fail;

# a child that writes one line to each stream
my $CHILD = 'say "OUT"; note "ERR";';

# ---- captured: what the adverbs asked for comes back ------------------------
{
    my $p = run($*EXECUTABLE, '-e', $CHILD, :in, :out, :err);
    $p.in.print("x\n");
    $p.in.close;
    @fail.push("out+err out: {$p.out.slurp(:close).raku}") unless $p.out.slurp(:close) eq "OUT\n";
    @fail.push("out+err err: {$p.err.slurp(:close).raku}") unless $p.err.slurp(:close) eq "ERR\n";
    @fail.push("out+err exit: {$p.exitcode}") unless $p.exitcode == 0;
}

# :!out discards stdout while :err still captures
{
    my $p = run($*EXECUTABLE, '-e', $CHILD, :in, :!out, :err);
    $p.in.close;
    @fail.push("!out err: {$p.err.slurp(:close).raku}") unless $p.err.slurp(:close) eq "ERR\n";
}

# closing stdin with nothing written still runs the child
{
    my $p = run($*EXECUTABLE, '-e', $CHILD, :in, :out, :err);
    $p.in.close;
    @fail.push("close-only out: {$p.out.slurp(:close).raku}") unless $p.out.slurp(:close) eq "OUT\n";
}

# ---- inherited: an un-adverbed stream reaches OUR descriptors ----------------
# Asserted from one level up: the inner program leaves a stream un-adverbed, and
# the outer capture is where those bytes must show up.
sub outer(Str $inner) {
    my $p = run($*EXECUTABLE, '-e', $inner, :out, :err);
    my $o = $p.out.slurp(:close);
    my $e = $p.err.slurp(:close);
    ($o, $e)
}

my ($o1, $e1) = outer(
    'my $p = run($*EXECUTABLE, q{-e}, q{say "OUT"; note "ERR";}, :in, :out); $p.in.close; print $p.out.slurp(:close);');
@fail.push("inherit-err captured out: {$o1.raku}") unless $o1 eq "OUT\n";
@fail.push("inherit-err reached stderr: {$e1.raku}") unless $e1.contains('ERR');

my ($o2, $e2) = outer(
    'my $p = run($*EXECUTABLE, q{-e}, q{say "OUT"; note "ERR";}, :in); $p.in.close;');
@fail.push("inherit-both out: {$o2.raku}") unless $o2.contains('OUT');
@fail.push("inherit-both err: {$e2.raku}") unless $e2.contains('ERR');

# :!err discards rather than inheriting — nothing reaches the outer stderr
my ($o3, $e3) = outer(
    'my $p = run($*EXECUTABLE, q{-e}, q{say "OUT"; note "ERR";}, :in, :out, :!err); $p.in.close; print $p.out.slurp(:close);');
@fail.push("discard-err out: {$o3.raku}") unless $o3 eq "OUT\n";
@fail.push("discard-err leaked: {$e3.raku}") if $e3.contains('ERR');

# ---- input still reaches the child, and large output does not deadlock -------
{
    my $p = run($*EXECUTABLE, '-e', 'print $*IN.slurp.uc;', :in, :out);
    $p.in.print("hello\n");
    $p.in.close;
    @fail.push("input: {$p.out.slurp(:close).raku}") unless $p.out.slurp(:close) eq "HELLO\n";
}
{
    # more than a pipe buffer on both streams at once, while input is still going
    my $p = run($*EXECUTABLE, '-e', 'my $n = $*IN.slurp.trim.Int; print "o" x $n; $*ERR.print("e" x $n);',
                :in, :out, :err);
    $p.in.print("200000\n");
    $p.in.close;
    @fail.push("big out: {$p.out.slurp(:close).chars}") unless $p.out.slurp(:close).chars == 200000;
    @fail.push("big err: {$p.err.slurp(:close).chars}") unless $p.err.slurp(:close).chars == 200000;
}

if @fail {
    note "FAIL: $_" for @fail;
    say 'FAIL';
    exit 1;
}
say 'PASS';
