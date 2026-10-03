# The cell-promotion race, run until it shows or 200 runs have not shown it.
#
#   build/rakupp t/race/varcell.raku               # 200 runs
#   build/rakupp t/race/varcell.raku --runs=50
#
# The binary that runs this file is the one under test.
#
# `f(:$v)`, `a => $v`, `($v, 1)` and `given $v` take the variable's CONTAINER,
# and the first one to do so promoted the variable's storage slot to a shared
# cell in place (Interpreter::varCell). The slot is a several-word Value, so a
# worker reading the same outer variable meanwhile saw "" (its Str emptied by
# the move), Any (the tag already reset), or a Cell kind with no cell behind it
# (SIGSEGV). Eight `start` blocks passing one outer `$tmp` as a named argument
# promoted it eight times at once: bin/rakuglaze's run-chunk died with "No
# such method 'IO' for invocant of type 'Any'" on `$tmp.IO`, 2026-10-03.
#
# Only the first wave of fresh workers races (after it the slot IS a cell), so
# each run is a fresh process. Before the fix about two runs in three printed
# a wrong answer or died. The only correct output is `bad: 0`.
#
# The last line is PASS or FAIL, and the exit code is 0 or 1, as in t/stress.

my constant REPRO = q:to/END/;
    sub g(:$v!) { $v }
    my $bad = 0;
    for ^20 {
        my $named = 'n' x 20;
        my $paired = 'p' x 20;
        my $listed = 'l' x 20;
        my $go = Promise.new;
        my @p = (^8).map: {
            start {
                await $go;
                (g(:v($named)) eq 'n' x 20) && ((a => $paired).value eq 'p' x 20) &&
                    (($listed, 1)[0] eq 'l' x 20)
            }
        };
        $go.keep;
        for @p { $bad++ unless try .result }
    }
    say "bad: $bad";
    END

my $runs = 200;
for @*ARGS -> $a {
    if $a ~~ / ^ '--runs=' (\d+) $ / { $runs = +$0 }
    else { note "varcell: unknown argument: $a"; exit 2 }
}

my $prog = $*TMPDIR.add("rakupp-varcell-race-{$*PID}.raku");
$prog.spurt(REPRO);
my $engine = $*EXECUTABLE.absolute;
say "varcell: $engine, $runs runs";

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
    elsif $out.trim ne 'bad: 0' {
        $wrong++;
        %wrong{$out.trim.lines.tail // '(no output)'}++;
    }
    note "varcell: $i of $runs, $deaths deaths, $wrong wrong answers" if $i %% 50 && $i < $runs;
}
$prog.unlink;

say "varcell: $runs runs in {(now - $t0).round(0.1)} s — $deaths deaths, $wrong wrong answers";
say "  $_.key(): $_.value()" for %signals.sort;
say "  printed '$_.key()': $_.value()" for %wrong.sort;
if $deaths || $wrong {
    say 'FAIL';
    exit 1;
}
say 'PASS';
