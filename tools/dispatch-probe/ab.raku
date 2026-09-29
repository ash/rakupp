# Interleaved A/B of engine binaries on the dispatch kernels —
# docs/dev/findings/DISPATCH-PROBES.md.
#
#   rakupp tools/dispatch-probe/ab.raku --reps=7 BASE VARIANT...
#
# Each round runs every kernel once on every binary, in turn, so a machine that
# drifts during the run drifts under all of them alike; the table keeps each
# binary's MINIMUM. A checksum that differs from the first binary's is printed,
# because a variant that is faster and wrong measured nothing.
sub MAIN(*@bins, Int :$reps = 7, Str :$kernels = 'k1,k2,k3,k4,k5') {
    my $script = $*PROGRAM.parent.add('kernels.raku').Str;
    my @ks = $kernels.split(',');
    my %best; my %sum;
    for ^$reps {
        for @ks -> $k {
            for @bins -> $b {
                my $out = run($b, $script, $k, :out).out.slurp(:close).trim;
                my ($, $ms, $sum) = $out.words;
                %best{$b}{$k} = min(%best{$b}{$k} // Inf, +$ms);
                %sum{$k} //= $sum;
                note "  CHECKSUM $b $k: $sum != %sum{$k}" if $sum ne %sum{$k};
            }
        }
    }
    my @names = @bins.map(*.IO.basename);
    say sprintf('%-6s', 'kernel'), @names.map({ sprintf('%16s', $_) }).join;
    for @ks -> $k {
        my $base = %best{@bins[0]}{$k};
        say sprintf('%-6s', $k), @bins.map(-> $b {
            my $v = %best{$b}{$k};
            sprintf('%16s', $b eq @bins[0] ?? sprintf('%.1f ms', $v)
                                          !! sprintf('%.1f %+.1f%%', $v, ($v / $base - 1) * 100))
        }).join;
    }
}
