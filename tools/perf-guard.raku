#!/usr/bin/env raku
# Lightweight interpreter performance guard — run before/after a change to catch
# per-iteration regressions in the eval/exec hot path. Unlike tools/run-bench.raku
# (which also times --exe and Rakudo across the full kernel set), this times only
# the interpreter on a few tight-loop kernels, best-of-3, in a couple of seconds.
#
# The kernels deliberately stress the paths a batch is most likely to slow down:
#   fib     — recursive sub calls + int arithmetic (dispatch + applyArith)
#   asg     — a plain scalar `=` assignment loop
#   loopsum — a `+=` compound assignment over a Range with the `$_` topic
#   hash    — hash-index post-increment in a loop
# (loopsum/hash were added after a ~8-22% interp regression on exactly these
# shapes slipped past the old fib+asg-only guard over the 0.7.1->0.9.0 cycle.)
#
# Usage — A/B two binaries:
#     RAKUPP=/path/to/old rakupp tools/perf-guard.raku
#     RAKUPP=/path/to/new rakupp tools/perf-guard.raku
# Default RAKUPP is ./build/rakupp. It also runs under Rakudo (`raku`), since it
# only spawns the target binary as a subprocess and times it.

my $repo   = $*PROGRAM.absolute.IO.parent.parent;
my $RAKUPP = %*ENV<RAKUPP> // $repo.add('build/rakupp').Str;
my $RUNS   = 4;   # 1 warm-up (discarded) + 3 measured

my %kernels =
    fib     => 'sub fib($n) { $n < 2 ?? $n !! fib($n-1) + fib($n-2) }; say fib(29);',
    asg     => 'my $x = 0; for ^2_000_000 { $x = $x + 1 }; say $x;',
    loopsum => 'my $t = 0; for 1 .. 1_000_000 { $t += $_ }; say $t;',
    hash    => 'my %c; for 1 .. 100_000 { %c{$_ % 1_000}++ }; say %c.elems;';

sub measure(Str $code --> Numeric) {
    my $tmp = $*TMPDIR.add("perf-guard-{$*PID}-{1e6.rand.Int}.raku");
    $tmp.spurt($code);
    my @ms;
    for ^$RUNS {
        my $t0 = now;
        my $p  = run($RAKUPP, $tmp.Str, :out, :err);
        $p.out.slurp(:close); $p.err.slurp(:close);
        @ms.push: ((now - $t0) * 1000).round(0.1);
    }
    $tmp.unlink;
    @ms.skip(1).min;   # best of the measured runs
}

# --check compares against tools/perf-baseline.raku and EXITS NON-ZERO on a
# regression, so a release can be gated on it. --record rewrites the baseline.
# Without either it just prints the numbers, as it always did.
my $BASEFILE = $repo.add('tools/perf-baseline.raku');
my $check  = so @*ARGS.grep('--check');
my $record = so @*ARGS.grep('--record');

say "perf-guard: $RAKUPP";
my %now;
say "kernel      best (ms)";
say "-" x 24;
for <fib asg loopsum hash> -> $k {
    %now{$k} = measure(%kernels{$k});
    printf "%-10s %8.1f\n", $k, %now{$k};
}

if $record {
    my %b = EVAL slurp $BASEFILE;
    my @l = $BASEFILE.slurp.lines;
    # rewrite only the numeric `baseline` fields, keeping every comment in place —
    # the file explains WHY the baseline is what it is, and that is the valuable part
    my @out;
    for @l -> $line {
        my $new = $line;
        for %now.kv -> $k, $v {
            if $line ~~ /^ \s* "'" $k "'" \s* '=>' / {
                $new = $line.subst(/"'baseline'" \s* '=>' \s* <[\d.]>+/, "'baseline' => $v");
                # a kernel that got FASTER also moves `best`
                my $old-best = %b<kernels>{$k}<best>;
                if $v < $old-best {
                    $new = $new.subst(/"'best'" \s* '=>' \s* <[\d.]>+/, "'best' => $v");
                }
            }
        }
        @out.push: $new;
    }
    $BASEFILE.spurt(@out.join("\n") ~ "\n");
    say "";
    say "recorded {%now.elems} kernels into {$BASEFILE.basename}";
    exit 0;
}

if $check {
    my %b   = EVAL slurp $BASEFILE;
    my $tol = %b<tolerance-pct>;
    my @bad;
    say "";
    say "gate: baseline {$BASEFILE.basename} (recorded %b<recorded>), tolerance {$tol}%";
    say "kernel        now   baseline    delta   vs best";
    say "-" x 52;
    for <fib asg loopsum hash> -> $k {
        my $base = %b<kernels>{$k}<baseline>;
        my $best = %b<kernels>{$k}<best>;
        my $d    = 100 * (%now{$k} - $base) / $base;
        my $vb   = 100 * (%now{$k} - $best) / $best;
        printf "%-10s %7.1f  %9.1f  %+6.1f%%  %+6.1f%%\n", $k, %now{$k}, $base, $d, $vb;
        @bad.push("$k {$d.round(0.1)}% slower than baseline") if $d > $tol;
    }
    say "";
    if @bad {
        note "perf-guard FAILED: @bad.join('; ')";
        note "A release must not ship a performance regression. Either fix it, or —";
        note "if the cost is understood and accepted — re-record the baseline with";
        note "`rakupp tools/perf-guard.raku --record` and say why in the CHANGELOG.";
        exit 1;
    }
    say "perf-guard OK — no kernel is more than {$tol}% slower than the last release.";
    my @debt = <fib asg loopsum hash>.grep({
        100 * (%now{$_} - %b<kernels>{$_}<best>) / %b<kernels>{$_}<best> > $tol });
    note "note: still behind the best ever measured on: @debt.join(', ') "
       ~ "(see the `best` column — standing debt, not a new regression)" if @debt;
    exit 0;
}
