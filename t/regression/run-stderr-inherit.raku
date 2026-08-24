# Regression: an un-adverbed `run`/`shell` sent the child's stderr to /dev/null.
# spawnCapture only ever gave the child two stderrs — a pipe when the caller
# asked for :err, /dev/null otherwise — so `:!err` (discard) and no adverb at
# all (Rakudo INHERITS) collapsed into the same silence. Every diagnostic a
# child wrote vanished: what surfaced it was raku-eye's weekly run, where a
# sub-rakupp refused its command line, printed its MAIN usage, and left a CI
# log showing nothing but a later "no such file" from the missing output.
#
# Fixed by giving spawnCapture a third mode: with no capture and no :!err the
# child's STDERR_FILENO is left exactly as the parent got it.
#
# Contract: exit 0 + last line PASS.
my @fail;

my $exe = $*EXECUTABLE.absolute;
my $tmp = $*TMPDIR.add("run-stderr-inherit-{$*PID}");
$tmp.mkdir;

# The grandchild: one line on each stream, and a distinctive exit code.
my $noisy = $tmp.add('noisy.raku');
$noisy.spurt: qq:to/END/;
    note "CHILD-STDERR";
    say "CHILD-STDOUT";
    exit 7;
    END

# Each driver is a child that runs \$noisy one way; the test captures BOTH of
# the driver's streams, so what reaches its stderr is what the child inherited.
sub driver($name, $body) {
    my $f = $tmp.add("$name.raku");
    $f.spurt: $body;
    my $p = run($exe, ~$f, ~$noisy, :out, :err);
    my $out = $p.out.slurp(:close);
    my $err = $p.err.slurp(:close);
    ($out, $err)
}

# 1. no adverb — the child writes straight to the stderr we handed the driver
my ($o1, $e1) = driver 'inherit', q:to/END/;
    my $p = run($*EXECUTABLE.absolute, @*ARGS[0]);
    say "exit={$p.exitcode}";
    END
@fail.push("inherit: stderr lost (got '$e1')")     unless $e1.contains('CHILD-STDERR');
@fail.push("inherit: stdout lost (got '$o1')")     unless $o1.contains('CHILD-STDOUT');
@fail.push("inherit: exit code lost (got '$o1')")  unless $o1.contains('exit=7');

# 2. :!err — asked to be silent, and is
my ($o2, $e2) = driver 'discard', q:to/END/;
    my $p = run($*EXECUTABLE.absolute, @*ARGS[0], :!err);
    say "exit={$p.exitcode}";
    END
@fail.push("discard: stderr leaked (got '$e2')")   if $e2.contains('CHILD-STDERR');
@fail.push("discard: stdout lost (got '$o2')")     unless $o2.contains('CHILD-STDOUT');

# 3. :err — captured, so it reaches neither our stderr nor the driver's
my ($o3, $e3) = driver 'capture', q:to/END/;
    my $p = run($*EXECUTABLE.absolute, @*ARGS[0], :err);
    print "CAPTURED:", $p.err.slurp(:close);
    END
@fail.push("capture: not captured (got '$o3')")    unless $o3.contains('CAPTURED:CHILD-STDERR');
@fail.push("capture: also leaked (got '$e3')")     if $e3.contains('CHILD-STDERR');

# 4. shell() inherits the same way run() does
my ($o4, $e4) = driver 'shell', q:to/END/;
    shell("\"{$*EXECUTABLE.absolute}\" \"{@*ARGS[0]}\"");
    END
@fail.push("shell: stderr lost (got '$e4')")       unless $e4.contains('CHILD-STDERR');
@fail.push("shell: stdout lost (got '$o4')")       unless $o4.contains('CHILD-STDOUT');

.unlink for $tmp.dir;
$tmp.rmdir;

die @fail.join("; ") if @fail;
say "PASS";
