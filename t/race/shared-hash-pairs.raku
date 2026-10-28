# The shared-hash Pairs race (issue #193), run until it shows or 200 runs have
# not shown it.
#
#   build/rakupp t/race/shared-hash-pairs.raku               # 200 runs
#   build/rakupp t/race/shared-hash-pairs.raku --runs=50
#
# The binary that runs this file is the one under test.
#
# A plain Hash's Pairs hold its own containers, and the FIRST Pair a hash
# handed out moved its entries into a generation the Pairs co-own
# (ValueHash::aliasOf). So reading a hash was, once, a write: two `start`
# blocks taking `.pairs` of one shared hash both made the move, the second
# moving an emptied list — the hash read as `{}`, its values as Nil, or a Pair
# pointed into freed memory (SIGSEGV) — and a thread iterating a hash while
# another took its first Pair read the list mid-move. `iz4 status` did that
# to %*ENV: its driver thread's `run` copies %*ENV for the child while the main
# thread builds the tests' environment with `%*ENV.grep(…)`, and about one run
# in a hundred died naming a sub's own parameter.
#
# Only a hash's first Pair races, so each round uses a fresh hash and each run
# is a fresh process (%*ENV takes its first Pairs once). At 5.2.1 about three
# runs in four died or printed a wrong answer (36 of 50: 27 deaths, most of
# them SIGSEGV, and 9 runs where six or seven threads of a round read the hash
# as emptied). The only correct output is `bad: 0`.
#
# The last line is PASS or FAIL, and the exit code is 0 or 1, as in t/stress.

my constant REPRO = Q:to/END/;
    sub enc($v) {
        given $v {
            when Associative { '{' ~ $v.pairs.sort(*.key).map({ .key ~ ':' ~ enc(.value) }).join(',') ~ '}' }
            when Positional  { '[' ~ $v.map({ enc($_) }).join(',') ~ ']' }
            default          { $v.Str }
        }
    }
    my $bad = 0;
    # iz4 status's shape: %*ENV's first Pairs on one thread, `run` copying it on three
    {
        my $n = %*ENV.elems;
        my $go = Promise.new;
        my @p = (^4).map: -> $w {
            start {
                await $go;
                $w == 0 ?? %*ENV.grep({ .key ne '' }).elems == $n
                        !! run('true', :out, :err).exitcode == 0
            }
        };
        $go.keep;
        for @p { $bad++ unless try .result }
    }
    # eight threads walking one shared hash with `.pairs`, a fresh hash each round
    for ^20 -> $r {
        my %d = a => [1, 2, %( q => $r )], b => %( c => 3, d => [4, %( e => 5 )] ),
                f => %( (^8).map({ "k$_" => $_ * $r }) );
        my $want = enc(%( a => [1, 2, %( q => $r )], b => %( c => 3, d => [4, %( e => 5 )] ),
                          f => %( (^8).map({ "k$_" => $_ * $r }) ) ));
        my $go = Promise.new;
        my @p = (^8).map: { start { await $go; enc(%d) } };
        $go.keep;
        for @p { $bad++ unless (try .result) eq $want }
    }
    say "bad: $bad";
    END

my $runs = 200;
for @*ARGS -> $a {
    if $a ~~ / ^ '--runs=' (\d+) $ / { $runs = +$0 }
    else { note "shared-hash-pairs: unknown argument: $a"; exit 2 }
}

my $prog = $*TMPDIR.add("rakupp-shared-hash-pairs-race-{$*PID}.raku");
$prog.spurt(REPRO);
my $engine = $*EXECUTABLE.absolute;
say "shared-hash-pairs: $engine, $runs runs";

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
    note "shared-hash-pairs: $i of $runs, $deaths deaths, $wrong wrong answers" if $i %% 50 && $i < $runs;
}
$prog.unlink;

say "shared-hash-pairs: $runs runs in {(now - $t0).round(0.1)} s — $deaths deaths, $wrong wrong answers";
say "  $_.key(): $_.value()" for %signals.sort;
say "  printed '$_.key()': $_.value()" for %wrong.sort;
if $deaths || $wrong {
    say 'FAIL';
    exit 1;
}
say 'PASS';
