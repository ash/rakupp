# Each thread's `$/` in a compiled binary, run until a race shows or 50 runs
# of each build have not shown it.
#
#   build/rakupp t/race/match-var-threads.raku              # 50 runs each way
#   build/rakupp t/race/match-var-threads.raku --runs=200
#
# The binary that runs this file is the one under test: it compiles the repro
# once with --exe and once with --exe -O, and runs each binary in fresh
# processes.
#
# A `start` block's compiled closure makes no scope of its own, so its matches
# wrote `$/` and `$0` into the scope current where the worker was spawned, the
# program's own, while the main thread wrote the same slot: two threads
# assigning one Value freed its block twice. Every run of either binary died
# with SIGSEGV (found 2026-11-10 auditing issue #209); interpreted it was fine.
# Fixed by starting every worker in a routine frame of its own
# (workerOwnScope, Interpreter.cpp). The only correct output is `ok 10000`.
#
# The last line is PASS or FAIL, and the exit code is 0 or 1, as in t/stress.

my constant REPRO = q:to/END/;
    my $t = start { my $wn = 0; for ^10000 { $wn++ if 'a1' ~~ /(\d)/ && ~$0 eq '1' }; $wn };
    my $mn = 0;
    for ^10000 { $mn++ if 'b2' ~~ /\d/ }
    my $w = await $t;
    say $w == $mn ?? "ok $mn" !! "worker $w, main $mn";
    END

my $runs = 50;
for @*ARGS -> $a {
    if $a ~~ / ^ '--runs=' (\d+) $ / { $runs = +$0 }
    else { note "match-var-threads: unknown argument: $a"; exit 2 }
}

my $prog = $*TMPDIR.add("rakupp-match-var-race-{$*PID}.raku");
$prog.spurt(REPRO);
my $engine = $*EXECUTABLE.absolute;
say "match-var-threads: $engine, $runs runs of each binary";

my $failed = False;
for '', '-O' -> $opt {
    my $bin = $*TMPDIR.add("rakupp-match-var-race$opt-{$*PID}").Str;
    my $b = run $engine, '--exe', |($opt || Empty), $prog.absolute, '-o', $bin, :out, :err;
    $b.out.slurp(:close);
    my $msg = $b.err.slurp(:close);
    unless $b.exitcode == 0 && $msg.contains('(native)') {
        say "--exe $opt did not build the repro natively:\n$msg";
        $failed = True;
        next;
    }
    my (%signals, %wrong);
    my ($deaths, $wrong) = 0, 0;
    my $t0 = now;
    for 1..$runs -> $i {
        my $p = run 'perl', '-e', 'alarm 30; exec @ARGV or die "exec: $!"', $bin, :out, :err;
        my $out = $p.out.slurp(:close); $p.err.slurp(:close);
        my $sig = $p.signal // 0;
        if $sig {
            $deaths++;
            %signals{$sig == 14 ?? 'hung (30 s cap)' !! "signal $sig"}++;
        }
        elsif $out.trim ne 'ok 10000' {
            $wrong++;
            %wrong{$out.trim.lines.tail // '(no output)'}++;
        }
    }
    say "--exe{$opt && " $opt"}: $runs runs in {(now - $t0).round(0.1)} s — $deaths deaths, $wrong wrong answers";
    say "  $_.key(): $_.value()" for %signals.sort;
    say "  printed '$_.key()': $_.value()" for %wrong.sort;
    $failed = True if $deaths || $wrong;
    try unlink $bin;
}
$prog.unlink;

if $failed {
    say 'FAIL';
    exit 1;
}
say 'PASS';
