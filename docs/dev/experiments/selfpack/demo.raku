#!/usr/bin/env rakupp
# demo.raku — self-packing arrays (SELFPACK-EXPERIMENT.md) on one untyped
# program: a 200×200 matrix product, written the way Raku is usually written,
# with no native types. One binary runs it four ways:
#
#   interpreted, packing off (RAKUPP_NO_SELFPACK=1) and on
#   compiled with --exe -O, the SAME binary run with packing off and on
#
# and, when `bun` is on PATH, Bun runs the same program in JavaScript. Each
# prints its checksum and the milliseconds its loops took (timed inside the
# program, so start-up is left out); the best of 3 runs is shown, and the
# checksums must all agree.
#
#   build/rakupp docs/dev/experiments/selfpack/demo.raku
#   build/rakupp docs/dev/experiments/selfpack/demo.raku --n=300    # a bigger matrix
#
# It runs the rakupp that runs it ($*EXECUTABLE), so build first.

sub MAIN(Int :$n = 200, Int :$runs = 3) {
    my $dir = $*TMPDIR.add("rakupp-selfpack-demo-{$*PID}");
    $dir.mkdir;
    LEAVE { try { .unlink for $dir.dir; $dir.rmdir } }

    my $raku = $dir.add('matmul.raku');
    $raku.spurt: qq:to/RAKU/;
        my \$n = $n;
        my \@a = (^(\$n * \$n)).map(\{ (\$_ % 7) * 0.25e0 \});
        my \@b = (^(\$n * \$n)).map(\{ (\$_ % 5) * 0.5e0 \});
        my \@c = 0e0 xx (\$n * \$n);
        my \$t0 = now;
        for ^\$n -> \$i \{
            for ^\$n -> \$j \{
                my \$s = 0e0;
                for ^\$n -> \$k \{ \$s += \@a[\$i * \$n + \$k] * \@b[\$k * \$n + \$j] }
                \@c[\$i * \$n + \$j] = \$s;
            }
        }
        my \$ms = (now - \$t0) * 1000;
        say [+] \@c;
        say \$ms;
        RAKU
    my $js = $dir.add('matmul.js');
    $js.spurt: qq:to/JS/;
        const n = $n, a = [], b = [], c = new Array(n * n).fill(0);
        for (let i = 0; i < n * n; i++) \{ a.push((i % 7) * 0.25); b.push((i % 5) * 0.5); }
        const t0 = performance.now();
        for (let i = 0; i < n; i++)
            for (let j = 0; j < n; j++) \{
                let s = 0;
                for (let k = 0; k < n; k++) s += a[i * n + k] * b[k * n + j];
                c[i * n + j] = s;
            }
        const ms = performance.now() - t0;
        console.log(c.reduce((x, y) => x + y, 0));
        console.log(ms);
        JS

    # best of $runs: (checksum, ms)
    sub best(*@cmd, :$off) {
        my %env = %*ENV;
        %env<RAKUPP_NO_SELFPACK> = '1' if $off;
        my ($sum, $ms) = '', Inf;
        for ^$runs {
            my $p = run |@cmd, :out, :err, :%env;
            my @l = $p.out.slurp(:close).lines;
            $p.err.slurp(:close);
            return ('failed', Inf) unless $p.exitcode == 0 && @l >= 2;
            $sum = @l[0];
            $ms = min($ms, +@l[1]);
        }
        ($sum, $ms)
    }

    my $rk = $*EXECUTABLE.absolute;
    say "untyped Raku: a {$n}×{$n} matrix product, {($n ** 3).flip.comb(3).join(',').flip} multiply-adds";
    my $v = run($rk, '--version', :out, :err);
    say "(ms spent in the loops, best of $runs; {$v.out.slurp(:close).lines.head // 'rakupp'})";
    $v.err.slurp(:close);
    say '';
    my %r;
    %r<interp-off> = best($rk, $raku.Str, :off);
    %r<interp-on>  = best($rk, $raku.Str);
    my $bin = $dir.add('matmul-bin');
    my $c = run $rk, '--exe', '-O', $raku.Str, '-o', $bin.Str, :out, :err;
    $c.out.slurp(:close); $c.err.slurp(:close);
    if $c.exitcode == 0 {
        %r<exe-off> = best($bin.Str, :off);
        %r<exe-on>  = best($bin.Str);
    }
    my $bun = (try run('bun', '--version', :out, :err)) ?? 'bun' !! Nil;
    if $bun {
        %r<bun> = best($bun, $js.Str);
    }

    sub ms($x) { $x == Inf ?? '—' !! $x < 10 ?? sprintf('%.1f', $x) !! sprintf('%.0f', $x) }
    sub row($label, $off, $on) {
        my $x = $off && $on && $on[1] > 0 ?? sprintf('%.0f×', $off[1] / $on[1]) !! '';
        say sprintf('  %-22s %12s %12s %10s', $label, $off ?? ms($off[1]) !! '', $on ?? ms($on[1]) !! '', $x);
    }
    say sprintf('  %-22s %12s %12s %10s', '', 'packing off', 'packing on', 'speed-up');
    row 'interpreted', %r<interp-off>, %r<interp-on>;
    row '--exe -O', %r<exe-off>, %r<exe-on> if %r<exe-on>;
    row 'Bun (JavaScript)', Nil, %r<bun> if %r<bun>;
    say '';
    my @sums = %r.values.map(*[0]).unique;
    say @sums == 1
        ?? "every run answers {@sums[0]}"
        !! "the answers DIFFER: {%r.sort.map({ "{.key} {.value[0]}" }).join(', ')}";
    say 'no bun on PATH: the JavaScript row is left out' unless $bun;
    exit(@sums == 1 ?? 0 !! 1);
}
