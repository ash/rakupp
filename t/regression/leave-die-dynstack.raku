# Regression: a LEAVE phaser that dies on a block's NORMAL exit left the
# caller's frame on the dynamic-lookup stack (2026-10-05). The frame was freed
# later, and the next `$*` lookup that walked that far read freed memory:
# S04-phasers/enter-leave.t segfaulted at random in Test::Util's is_run
# (`$*EXECUTABLE.absolute`), about half the runs.
#
# The stress below leaks 2000 frames on a broken build and crashed the 5.2.1
# build two runs in three; each subprocess must exit cleanly. The scope is
# restored too: the routine still sees its own lexicals after the LEAVE died.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my $stress = q:to/CODE/;
    sub h { my $*Z = 1; my @pad = ^20; try { LEAVE { die 'x' } } }
    for ^2000 { h(); my @churn = ^50 }
    my $seen = 0;
    for ^200 { $seen++ unless (try $*NOT-DECLARED).defined }
    print $seen;
    CODE
for ^3 -> $n {
    my $p = run $*EXECUTABLE, '-e', $stress, :out, :err;
    my $out = $p.out.slurp(:close); $p.err.slurp(:close);
    ck(($p.exitcode, $out), (0, '200'), "dying LEAVEs leave no stale dynamic frame (run $n)");
}

my $x = 5;
sub m { try { LEAVE { die 'y' } }; $x }
ck(m(), 5, 'the scope is back after a LEAVE died');
sub d { my $*IN-D = 'd'; try { LEAVE { die 'z' } }; $*IN-D }
ck(d(), 'd', 'a routine still sees its own dynamic after a LEAVE died');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
