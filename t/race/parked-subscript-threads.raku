# The parked-subscript race, run until it shows or 200 runs have not shown it.
#
#   build/rakupp t/race/parked-subscript-threads.raku               # 200 runs
#   build/rakupp t/race/parked-subscript-threads.raku --runs=50
#
# The binary that runs this file is the one under test.
#
# An element assignment evaluates its subscript once, parks the answer for
# lvalue() to collect (so a subscript with side effects runs once), and
# eval() looks for it. The parking vector was a field of the interpreter,
# shared by every thread: eight `start` blocks assigning `%h{$k.lc} = …` each
# into a hash of their own pushed into it at once and corrupted the heap
# ("pointer being freed was not allocated"). Every run of this program died
# under 5.4.1-35; a Webraku blog server died within seconds of eight
# concurrent requests. The vector is per thread now, behind a shared count
# that keeps eval()'s check one relaxed load.
#
# Each run is a fresh process. The only correct output is `total: 48000`.
# The last line is PASS or FAIL, and the exit code is 0 or 1, as in t/stress.

my constant REPRO = q:to/END/;
    my @w = (^8).map: -> $t {
        start {
            my %h;
            for ^3000 -> $i { my $k = "Key-$i"; %h{$k.lc} = $i; %h{$k.lc ~ '!'} = $i }
            %h.elems;
        }
    };
    say "total: {[+] await @w}";
    END

my $runs = 200;
for @*ARGS -> $a {
    if $a ~~ / ^ '--runs=' (\d+) $ / { $runs = +$0 }
    else { note "parked-subscript-threads: unknown argument: $a"; exit 2 }
}

my $prog = $*TMPDIR.add("rakupp-parked-subscript-race-{$*PID}.raku");
$prog.spurt(REPRO);
my $engine = $*EXECUTABLE.absolute;
say "parked-subscript-threads: $engine, $runs runs";

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
    elsif $out.trim ne 'total: 48000' {
        $wrong++;
        %wrong{$out.trim.lines.tail // '(no output)'}++;
    }
    note "parked-subscript-threads: $i of $runs, $deaths deaths, $wrong wrong answers" if $i %% 50 && $i < $runs;
}
$prog.unlink;

say "parked-subscript-threads: $runs runs in {(now - $t0).round(0.1)} s — $deaths deaths, $wrong wrong answers";
say "  $_.key(): $_.value()" for %signals.sort;
say "  printed '$_.key()': $_.value()" for %wrong.sort;
if $deaths || $wrong {
    say 'FAIL';
    exit 1;
}
say 'PASS';
