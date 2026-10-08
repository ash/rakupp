# Interleaved best-of-R for the parallel benches in this directory.
#
# Rounds go on the outside and configurations on the inside, so every
# configuration is measured at every point of the sitting (see "The method"
# in docs/guide/PARALLEL-SPEEDUP.md). Each run's first `N.NNNs` is its time.
# The table gives the minimum and the max/min spread per configuration, and
# a speed-up wherever a `serial` row has a `parallel` twin.
#
#     rakupp tools/bench/parallel/sweep.raku                     # every set, 9 rounds
#     rakupp tools/bench/parallel/sweep.raku --set=fanout,worker --rounds=5
#     rakupp tools/bench/parallel/sweep.raku --vs=path/to/other/rakupp
#     rakupp tools/bench/parallel/sweep.raku --vs-env=RAKUPP_PRIVATE_SLOTS=0
#
# `--vs` adds a second lane run by another binary; `--vs-env` adds one run by
# the same binary with an environment variable set (both together: the other
# binary with that variable). Every configuration runs on every lane in each
# round, lanes alternating, so the comparison is interleaved too.
#
# Runs are under `nice -n 10`. The sets use up to 8 threads; each run takes
# well under a second, so a full sweep at 9 rounds is a few minutes.

sub MAIN(
    Str  :$set     = 'fanout,worker,atomic,kernel,faq,idle',
    Int  :$rounds  = 9,
    Str  :$rakupp  = $*EXECUTABLE.absolute,
    Str  :$vs,                              # a second binary
    Str  :$vs-env,                          # K=V for the second lane
    Bool :$quiet   = False,                 # no per-run lines
) {
    my $dir = $*PROGRAM.parent;
    my %sets =
        fanout => [ |(1, 2, 4, 8).map(-> $n { |<serial parallel>.map(-> $m {
                        "cpu-fanout N=$n $m" => ['cpu-fanout.raku', $n, 300_000, $m] }) }) ],
        worker => [ |(2, 4).map(-> $n { "per-worker N=$n" => ['per-worker.raku', $n, 300_000] }) ],
        atomic => [ |<contended sharded counters>.map(-> $s { |<serial parallel>.map(-> $m {
                        "atomic $s N=4 $m" => ['atomic-counter.raku', 4, 300_000, $s, $m] }) }) ],
        kernel => [ |<outer param>.map(-> $sh { |(1, 4).map(-> $n { |<serial parallel>.map(-> $m {
                        "kernel $sh N=$n $m" => ['kernel-fanout.raku', $n, 3_000_000, $sh, $m] }) }) }) ],
        faq    => [ 'faq' => ['faq.raku'] ],
        idle   => [ |<kernel interp>.map(-> $sh { |<alone idle>.map(-> $m {
                        "idle-worker $sh $m" => ['idle-worker.raku', $sh, $m] }) }) ];
    my @configs = $set.split(',').map({ |(%sets{$_} // die "no set '$_' (have: {%sets.keys.sort})") });

    my @lanes;
    @lanes.push: %(label => 'main', bin => $rakupp, env => Hash.new);
    if $vs.defined || $vs-env.defined {
        my %env = $vs-env.defined ?? ($vs-env.split('=', 2)[0] => $vs-env.split('=', 2)[1]) !! ();
        @lanes.push: %(label => 'vs', bin => ($vs // $rakupp), env => %env);
    }

    my %t;          # "lane|config" => [times]
    for 1..$rounds -> $round {
        for @configs -> $c {
            for @lanes -> %lane {
                my @argv = 'nice', '-n', '10', %lane<bin>, $dir.add($c.value[0]).Str, |$c.value[1..*];
                my %env = %*ENV;
                %env{.key} = .value for %lane<env>.pairs;
                my $p = run |@argv, :out, :err, :%env;
                my $out = $p.out.slurp(:close);
                my $err = $p.err.slurp(:close);
                my $time = $out ~~ / (\d+ '.' \d+) 's' / ?? +$0 !! Nil;
                if $p.exitcode != 0 || !$time.defined {
                    note "round $round, %lane<label>, {$c.key}: exit {$p.exitcode}\n$out$err";
                    next;
                }
                %t{"%lane<label>|{$c.key}"}.push: $time;
                note "[$round] %lane<label> {$c.key}: {$out.trim}" unless $quiet;
            }
        }
    }

    my @heads = 'configuration', |@lanes.map({ "$_<label> best" }), |@lanes.map({ "$_<label> spread" });
    @heads.push: 'vs/main' if @lanes > 1;
    say '| ', @heads.join(' | '), ' |';
    say '|', ('---|' x @heads), '';
    for @configs -> $c {
        my @best   = @lanes.map: -> %l { my @x = |(%t{"%l<label>|{$c.key}"} // []); @x ?? @x.min !! NaN };
        my @spread = @lanes.map: -> %l { my @x = |(%t{"%l<label>|{$c.key}"} // []); @x ?? @x.max / @x.min !! NaN };
        my @row = $c.key, |@best.map({ sprintf '%.3fs', $_ }), |@spread.map({ sprintf '%.2f', $_ });
        @row.push: sprintf('%.2f×', @best[1] / @best[0]) if @lanes > 1;
        say '| ', @row.join(' | '), ' |';
    }
    # speed-ups: a `serial` row against its `parallel` twin, per lane
    my @pairs = @configs.grep({ .key.contains('serial') }).map: -> $s {
        my $pk = $s.key.subst('serial', 'parallel');
        @configs.first(*.key eq $pk) ?? ($s.key => $pk) !! Empty
    };
    if @pairs {
        say '';
        say '| speed-up | ', @lanes.map(*<label>).join(' | '), ' |';
        say '|---|', ('---|' x @lanes), '';
        for @pairs -> $pr {
            my @r = @lanes.map: -> %l {
                my @s = |(%t{"%l<label>|{$pr.key}"} // []); my @p = |(%t{"%l<label>|{$pr.value}"} // []);
                @s && @p ?? sprintf('%.2f×', @s.min / @p.min) !! '—'
            };
            say '| ', $pr.key.subst(' serial', ''), ' | ', @r.join(' | '), ' |';
        }
    }
}
