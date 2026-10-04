# The topic write-back race, run until it shows or 200 runs have not shown it.
#
#   build/rakupp t/race/topic-alias-shared.raku               # 200 runs
#   build/rakupp t/race/topic-alias-shared.raku --runs=50
#
# The binary that runs this file is the one under test.
#
# `given $x { … }` and `with $x { … }` alias `$_` to `$x`, and when the block
# ends the topic is compared with what it started as, to write a changed one
# back. The comparison was valueEqv — a deep, element-by-element walk, with no
# lock — even when `$_` was still the very same object. `with $channel {
# .send(…) }` from several threads therefore walked the Channel's own queue
# while the thread reading it drained it: Selkie's t/81-trace.rakutest died
# with SIGSEGV in ValueHash::findSlot about one run in three (found by the
# battery t/ comparison, 2026-10-04; ThreadSanitizer named
# TopicAlias::~TopicAlias against Channel.list). The same object now counts
# as unchanged without being walked.
#
# Each run is a fresh process. The only correct output is `seen: 400`.
# The last line is PASS or FAIL, and the exit code is 0 or 1, as in t/stress.

my constant REPRO = q:to/END/;
    my $ch = Channel.new;
    my $reader = start { my $n = 0; for $ch.list -> %e { $n++ if %e<args><i>.defined }; $n };
    my @producers = (^4).map: -> $w {
        start {
            for ^100 -> $i {
                my %args = worker => $w, i => $i, note => 'x' x 20;
                with $ch { .send(%(name => 'ev', args => %args).Hash) }
            }
        }
    };
    await @producers;
    $ch.close;
    say "seen: {await $reader}";
    END

my $runs = 200;
for @*ARGS -> $a {
    if $a ~~ / ^ '--runs=' (\d+) $ / { $runs = +$0 }
    else { note "topic-alias-shared: unknown argument: $a"; exit 2 }
}

my $prog = $*TMPDIR.add("rakupp-topic-alias-race-{$*PID}.raku");
$prog.spurt(REPRO);
my $engine = $*EXECUTABLE.absolute;
say "topic-alias-shared: $engine, $runs runs";

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
    elsif $out.trim ne 'seen: 400' {
        $wrong++;
        %wrong{$out.trim.lines.tail // '(no output)'}++;
    }
    note "topic-alias-shared: $i of $runs, $deaths deaths, $wrong wrong answers" if $i %% 50 && $i < $runs;
}
$prog.unlink;

say "topic-alias-shared: $runs runs in {(now - $t0).round(0.1)} s — $deaths deaths, $wrong wrong answers";
say "  $_.key(): $_.value()" for %signals.sort;
say "  printed '$_.key()': $_.value()" for %wrong.sort;
if $deaths || $wrong {
    say 'FAIL';
    exit 1;
}
say 'PASS';
