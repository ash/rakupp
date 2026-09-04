#!/usr/bin/env rakupp
# fourier — building shapes out of sine waves, and taking them apart again.
#
#   rakupp showcase/fourier/fourier.raku                      a square wave, built up
#   rakupp showcase/fourier/fourier.raku --wave=triangle -n 5
#   rakupp showcase/fourier/fourier.raku --coefficients=square -n 15
#   rakupp showcase/fourier/fourier.raku --converge=square
#   rakupp showcase/fourier/fourier.raku --gibbs
#   rakupp showcase/fourier/fourier.raku --spectrum=3,5,11
#   rakupp showcase/fourier/fourier.raku --dft=1,0,-1,0
#   rakupp showcase/fourier/fourier.raku --bench=512
#   rakupp showcase/fourier/fourier.raku --check
#
# The arithmetic lives in lib/Fourier.rakumod; this file is the terminal.

use lib $?FILE.IO.parent.add('lib').Str;
use Fourier;

constant RESET = "\e[0m";
my $colour = $*OUT.t && !%*ENV<NO_COLOR>;
sub dim($s)  { $colour ?? "\e[2m$s" ~ RESET !! $s }
sub bold($s) { $colour ?? "\e[1m$s" ~ RESET !! $s }

# ---- an oscilloscope, in characters --------------------------------------
# The target waveform in dots, the partial sum in hashes. Where they agree
# you see one mark; where the series has not caught up you see two.
sub plot(Str $shape, %c, Int $upto, :$duty = 0.25, Int :$width = 74, Int :$height = 21) {
    my @rows = ('' xx $height).map({ [' ' xx $width] });
    my $lo = -1.35, my $hi = 1.35;
    my sub row($v) { (($hi - $v) / ($hi - $lo) * ($height - 1)).round.Int max 0 min $height - 1 }
    for ^$width -> $x {
        my $t = $x / $width;
        @rows[row(wave-value($shape, $t, :$duty))][$x] = '.';
    }
    for ^$width -> $x {
        my $t = $x / $width;
        my $r = row(series-value(%c, $t, :$upto));
        @rows[$r][$x] = @rows[$r][$x] eq '.' ?? '*' !! '#';
    }
    my $zero = row(0);
    for ^$height -> $r {
        my $label = $r == row(1)  ?? ' +1 ' !! $r == row(-1) ?? ' -1 '
                 !! $r == $zero   ?? '  0 ' !! '    ';
        my $line = @rows[$r].join;
        $line = $line.subst(' ', '-', :g) if $r == $zero && $line.trim eq '';
        say dim($label) ~ ($r == $zero ?? dim('|') !! dim('|')) ~ $line;
    }
    say dim('    +' ~ ('-' x $width));
    say dim('     0' ~ (' ' x ($width div 2 - 6)) ~ 'half a period' ~
            (' ' x ($width - $width div 2 - 14)) ~ '1');
    say '';
    say "  {bold('#')} the partial sum   {dim('.')} the waveform it is chasing   "
        ~ "{bold('*')} where they agree";
}

sub coefficient-table(Str $shape, Int $n, :$duty = 0.25) {
    my %c = wave-coefficients($shape, $n, :$duty);
    say bold(sprintf('%4s  %12s  %12s  %10s  %s', 'n', 'a_n (cos)', 'b_n (sin)', 'amplitude', ''));
    say dim('-' x 68);
    my $max = (1..$n).map({ sqrt(%c<a>[$_] ** 2 + %c<b>[$_] ** 2) }).max max 1e-12;
    printf("%4s  %12.6f  %12s  %10s\n", 'a0/2', %c<a>[0] / 2, '', '');
    for 1..$n -> $k {
        my $amp = sqrt(%c<a>[$k] ** 2 + %c<b>[$k] ** 2);
        my $bar = '=' x (30 * $amp / $max).round;
        printf("%4d  %12.6f  %12.6f  %10.6f  %s\n", $k, %c<a>[$k], %c<b>[$k], $amp, $bar);
    }
}

sub converge(Str $shape, :$duty = 0.25) {
    say bold(sprintf('%9s  %12s  %12s  %s', 'harmonics', 'RMS error', 'peak', ''));
    say dim('-' x 62);
    my $prev;
    for 1, 3, 5, 9, 17, 33, 65, 129 -> $n {
        my %c = wave-coefficients($shape, $n, :$duty);
        my $e = rms-error($shape, %c, :upto($n), :$duty);
        my $peak = gibbs-peak(%c, :upto($n));
        my $ratio = $prev.defined && $e > 0 ?? sprintf('%.2fx better', $prev / $e) !! '';
        printf("%9d  %12.6f  %12.6f  %s\n", $n, $e, $peak, $ratio);
        $prev = $e;
    }
    say '';
    say dim("A jump in the signal costs you: the coefficients only fall off as 1/n,");
    say dim("so halving the error takes four times the harmonics -- and the peak");
    say dim("never comes down at all. That last column is Gibbs.");
}

sub gibbs() {
    say bold('The overshoot that will not go away');
    say '';
    say 'Add harmonics to a square wave and the partial sum gets closer everywhere';
    say 'except at the jump, where it always shoots about 9% too far. More terms';
    say 'make the overshoot narrower, never shorter.';
    say '';
    say bold(sprintf('%10s  %14s  %14s', 'harmonics', 'peak value', 'overshoot'));
    say dim('-' x 44);
    for 3, 5, 11, 21, 51, 101, 201, 401 -> $n {
        my %c = wave-coefficients('square', $n);
        my $peak = gibbs-peak(%c, :upto($n));
        printf("%10d  %14.7f  %13.4f%%\n", $n, $peak, ($peak - 1) / 2 * 100);
    }
    my $limit = 2 * si-pi() / pi;
    say dim('-' x 44);
    printf("%10s  %14.7f  %13.4f%%\n", 'limit', $limit, ($limit - 1) / 2 * 100);
    say '';
    say dim('The limit is 2*Si(pi)/pi, where Si is the sine integral -- computed');
    say dim('here by the same midpoint rule, not looked up.');
}

sub spectrum-of(Str $spec) {
    my @freq = $spec.split(/','/).map(*.trim.Num);
    my $n = 64;
    my @partials = @freq.map({ [$_, 1 / (1 + @freq.first($_, :k)), 0] });
    @partials = @freq.kv.map(-> $i, $f { [$f, 1 / ($i + 1), 0] });
    my @sig = tone-signal(@partials, $n);
    say bold("A signal made of {@freq.elems} tones, then taken apart again");
    say '';
    say dim('built from: ' ~ @partials.map({ sprintf('%g cycles at amplitude %.3f', .[0], .[1]) }).join(', '));
    say '';
    my @s = spectrum(@sig);
    my $max = @s.max max 1e-12;
    for @s.kv -> $k, $v {
        next if $v < 1e-6;
        printf("  %3d cycles  %8.4f  %s\n", $k, $v, '=' x (44 * $v / $max).round);
    }
    say '';
    say dim("Every bar the transform found is one of the tones that went in, at the");
    say dim("amplitude it went in with. Nothing else is there.");
}

sub show-dft(Str $spec) {
    my @x = $spec.split(/','/).map(*.trim.Num);
    my @f = @x.elems +& (@x.elems - 1) ?? dft(@x) !! fft(@x);
    say bold(sprintf('%4s  %24s  %12s  %12s', 'k', 'X[k]', '|X[k]|', 'phase'));
    say dim('-' x 60);
    for @f.kv -> $k, $z {
        printf("%4d  %11.6f %+11.6fi  %12.6f  %11.4f deg\n",
            $k, $z.re, $z.im, $z.abs, atan2($z.im, $z.re) * 180 / pi);
    }
    my @back = ifft(@f);
    say dim('-' x 60);
    say dim('inverse transform: ' ~ @back.map({ sprintf('%.6f', .re) }).join(', '));
}

sub bench(Int $n) {
    my @x = (^$n).map({ sin(2 * pi * 7 * $_ / $n) + 0.3 * cos(2 * pi * 23 * $_ / $n) });
    my $t0 = now;
    my @d = dft(@x);
    my $dt = now - $t0;
    $t0 = now;
    my @f = fft(@x);
    my $ft = now - $t0;
    my $err = (^$n).map({ (@d[$_] - @f[$_]).abs }).max;
    say bold("n = $n");
    printf("  DFT, from the definition   %8.3f s   %d multiplications\n", $dt, $n * $n);
    printf("  FFT, Cooley-Tukey          %8.3f s   %d\n", $ft, ($n * log($n) / log(2)).round);
    printf("  speed-up                   %8.1fx\n", $ft > 0 ?? $dt / $ft !! 0);
    printf("  they agree to              %8.2e\n", $err);
}

# ---- the checks -----------------------------------------------------------
sub check() {
    my ($ok, $bad) = 0, 0;
    my sub is-close($got, $want, $tol, $desc) {
        if abs($got - $want) <= $tol { $ok++; say "ok   $desc" }
        else {
            $bad++;
            printf("FAIL %s: got %.12g, want %.12g (tolerance %.1e)\n", $desc, $got, $want, $tol);
        }
    }

    # 1. the fast transform is the slow one
    my @x = (^32).map({ sin(2 * pi * 5 * $_ / 32) + 0.4 * cos(2 * pi * 11 * $_ / 32) - 0.2 });
    my @d = dft(@x);
    my @f = fft(@x);
    is-close((^32).map({ (@d[$_] - @f[$_]).abs }).max, 0, 1e-12, 'FFT agrees with the DFT definition');

    # 2. and it is invertible
    is-close((^32).map({ (ifft(@f)[$_].re - @x[$_]).abs }).max, 0, 1e-12,
             'the inverse transform gives the signal back');

    # 3. a pure tone lands in exactly one bin
    my @tone = tone-signal([[6, 1, 0]], 64);
    my @s = spectrum(@tone);
    is-close(@s[6], 1, 1e-12, 'a pure tone of 6 cycles puts amplitude 1 in bin 6');
    is-close(@s.kv.map(-> $k, $v { $k == 6 ?? 0 !! abs($v) }).max, 0, 1e-12,
             'and nothing anywhere else');

    # 4. Parseval, for a signal whose energy we know exactly. A square wave of
    #    amplitude 1 has mean square 1, and the coefficients must add to it.
    for 100, 1000 -> $n {
        my %c = wave-coefficients('square', $n);
        my $tail = 8 / (pi ** 2 * $n);         # the harmonics not summed yet
        is-close(parseval(%c), 1, $tail, "Parseval for a square wave, $n harmonics");
    }

    # 5. the closed-form coefficients are the integrals
    for <square sawtooth triangle pulse rectified> -> $shape {
        my %exact = wave-coefficients($shape, 6);
        my %num = analyse(-> $t { wave-value($shape, $t) }, 6, :samples(40000));
        my $worst = (0..6).map({ abs(%exact<a>[$_] - %num<a>[$_]) max abs(%exact<b>[$_] - %num<b>[$_]) }).max;
        is-close($worst, 0, 1e-6, "the closed-form coefficients of a $shape wave are its integrals");
    }

    # 6. Gibbs converges to 2*Si(pi)/pi and NOT to 1
    my $limit = 2 * si-pi() / pi;
    is-close($limit, 1.17897974447, 1e-8, 'the Gibbs constant 2*Si(pi)/pi');
    my %g = wave-coefficients('square', 201);
    is-close(gibbs-peak(%g, :upto(201)), $limit, 2e-5, 'the 201-harmonic peak sits on it');

    # 7. smoothness sets the rate: a jump gives 1/n, a kink 1/n^2
    my %sq = wave-coefficients('square', 101);
    my %tr = wave-coefficients('triangle', 101);
    is-close(%sq<b>[101] * 101 * pi / 4, 1, 1e-12, 'square coefficients fall off exactly as 1/n');
    is-close(abs(%tr<b>[101]) * (pi * 101) ** 2 / 8, 1, 1e-12, 'triangle coefficients as 1/n^2');

    say '';
    say $bad ?? "$bad of {$ok + $bad} checks FAILED" !! "all $ok checks passed";
    exit $bad ?? 1 !! 0;
}

# ---- MAIN -----------------------------------------------------------------
sub MAIN(
    Str  :$wave = 'square',      #= square / sawtooth / triangle / pulse / rectified
    Int  :n(:$harmonics) = 7,    #= how many harmonics to sum
    Real :$duty = 0.25,          #= the duty cycle of the pulse wave
    Str  :$coefficients,         #= tabulate the coefficients of a waveform
    Str  :$converge,             #= RMS error against the number of harmonics
    Bool :$gibbs,                #= the overshoot that will not go away
    Str  :$spectrum,             #= build a signal from tones, then find them again
    Str  :$dft,                  #= transform a comma-separated list
    Int  :$bench,                #= time the DFT against the FFT
    Bool :$check,                #= every identity this module can be held to
) {
    if $check { check() }
    if $gibbs { gibbs(); exit }
    if $coefficients { coefficient-table($coefficients, $harmonics, :$duty); exit }
    if $converge { converge($converge, :$duty); exit }
    if $spectrum { spectrum-of($spectrum); exit }
    if $dft { show-dft($dft); exit }
    if $bench { bench($bench); exit }

    die "unknown waveform: $wave (try {WAVEFORMS.join(', ')})" unless $wave eq any WAVEFORMS;
    my %c = wave-coefficients($wave, $harmonics, :$duty);
    say bold("$wave wave, the first $harmonics harmonics");
    say '';
    plot($wave, %c, $harmonics, :$duty);
    say '';
    printf("  RMS error %.6f   %s\n", rms-error($wave, %c, :upto($harmonics), :$duty),
        dim("try -n {$harmonics * 4} to see it fall"));
}
