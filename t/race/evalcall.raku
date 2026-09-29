# The evalCall race, run until it shows or 1,000 runs have not shown it.
#
#   build/rakupp t/race/evalcall.raku               # 1,000 runs
#   build/rakupp t/race/evalcall.raku --runs=200
#
# The binary that runs this file is the one under test. It is opt-in, and no
# suite runs it, until V5-PLAN B2 has brought it to 0 (a fix there moves it into
# the TSan ratchet of t/stress/run.raku).
#
# The program below is the nine-line repro of
# docs/dev/findings/EVALCALL-RACE-2026-09-17.md, reduced from
# S17-promise/start.t: four promise threads call one sub at once, 200 rounds,
# and every call fails to bind, so every promise breaks and CATCH counts it.
# Its only correct output is `deaths: 200`. At v4.0.0 the process segfaulted
# about one run in two, in the shared name-lookup state `evalCall` reads.
#
# Each run is a fresh process with a 30 s cap. A run that ends by a signal is a
# DEATH; a run that ends normally without printing `deaths: 200` is a WRONG
# ANSWER. The suite passes when there are neither. One clean run proves nothing
# about a race, which is why the default is 1,000.
#
# The last line is PASS or FAIL, and the exit code is 0 or 1, as in t/stress.

my constant REPRO = q:to/END/;
    sub worker(Any $a, Int $b) {}
    my $deaths = 0;
    for ^200 {
        my $value = Any;
        my @workers = (^4).map: { start { worker($value) } };
        try {
            await @workers;
            CATCH { default { $deaths++ } }
        }
    }
    say "deaths: $deaths";
    END

my $runs = 1000;
for @*ARGS -> $a {
    if $a ~~ / ^ '--runs=' (\d+) $ / { $runs = +$0 }
    else { note "evalcall: unknown argument: $a"; exit 2 }
}

my $prog = $*TMPDIR.add("rakupp-evalcall-race-{$*PID}.raku");
$prog.spurt(REPRO);
my $engine = $*EXECUTABLE.absolute;
say "evalcall: $engine, $runs runs";

my (%signals, %wrong);
my ($deaths, $wrong) = 0, 0;
my $t0 = now;
for 1..$runs -> $i {
    my $p = run 'perl', '-e', 'alarm 30; exec @ARGV or die "exec: $!"', $engine, $prog.absolute,
                :out, :err;
    my $out = $p.out.slurp(:close); $p.err.slurp(:close);
    my $sig = $p.signal // 0;
    if $sig {
        $deaths++;
        %signals{$sig == 14 ?? 'hung (30 s cap)' !! "signal $sig"}++;
    }
    elsif $out.trim ne 'deaths: 200' {
        $wrong++;
        %wrong{$out.trim.lines.tail // '(no output)'}++;
    }
    note "evalcall: $i of $runs, $deaths deaths, $wrong wrong answers" if $i %% 250 && $i < $runs;
}
$prog.unlink;

say "evalcall: $runs runs in {(now - $t0).round(0.1)} s — $deaths deaths, $wrong wrong answers";
say "  $_.key(): $_.value()" for %signals.sort;
say "  printed '$_.key()': $_.value()" for %wrong.sort;
if $deaths || $wrong {
    say 'FAIL';
    exit 1;
}
say 'PASS';
