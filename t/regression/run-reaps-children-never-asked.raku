# Regression: a `run` child whose Proc is never asked for a status was never
# reaped (2026-10-19). `run(…, :in, :out, :err)` keeps the child live, and it
# was waitpid()ed only when the program read `.exitcode`, `.signal`, or the
# Proc's truth or number. A program that writes the input, drains and closes
# the output and moves on left one zombie per child. raku.online's modules
# build spawns `rakupp --highlight` once per code block that way, and after
# about 900 children the per-user process limit (1,333 on that Mac) refused the
# next fork: every later block came out unhighlighted. Descriptors did not leak.
#
# Each spawn now reaps, without waiting, the children that have exited (as the
# Proc::Async path already did), keeping the status for a Proc asked later.
# Rakudo leaves no zombies either: its event loop reaps each child as it exits.
#
# The test counts this process's children rather than spawning to the limit,
# which on a Linux box is far higher than any test can reach.
#
# Expectations checked against Rakudo v2026.09 via /opt/homebrew/bin/rakudo
# (this file prints PASS there too).

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

if $*DISTRO.is-win {
    say "ok - skipped: no ps and no sh on Windows";
    say 'PASS';
    exit 0;
}

sub children() {
    my $uid = qx[id -u].trim;
    +qqx[ps -o ppid= -U $uid].lines.grep(*.trim eq ~$*PID)
}

# --- fifty children, drained and closed, never asked ----------------------------
for ^50 -> $i {
    my $p = run 'sh', '-c', "cat; echo $i", :in, :out, :err;
    $p.in.print("x");
    my $held = $p.in.close;
    $p.out.slurp(:close);
    $p.err.slurp(:close);
}
sleep 0.2;
{ my $p = run 'true', :out; $p.out.slurp(:close) }   # one more spawn sweeps the rest
# what is left: that last child, if it has not been reaped yet, and the ps
ck children() <= 2, True, 'children never asked about are reaped, not left as zombies';

# --- a status asked after the sweep reaped the child --------------------------------
my $a = run 'sh', '-c', 'echo a; exit 3', :out;
my $ao = $a.out.slurp(:close);
my $b = run 'sh', '-c', 'kill -TERM $$', :out;
$b.out.slurp(:close);
sleep 0.2;
for ^3 { my $p = run 'true', :out; $p.out.slurp(:close) }
ck ($ao, $a.exitcode, so $a), ("a\n", 3, False), 'an exit code survives the sweep';
ck ($b.exitcode, $b.signal), (0, 15), '…and so does a signal';

# --- children spawned and asked from several threads ---------------------------------
my @codes = await (^6).map: -> $i { start {
    my $p = run 'sh', '-c', "exit $i", :out;
    $p.out.slurp(:close);
    $p.exitcode
} };
ck @codes, [0, 1, 2, 3, 4, 5], 'statuses asked from several threads';

say $fails ?? "FAILED $fails" !! 'PASS';
