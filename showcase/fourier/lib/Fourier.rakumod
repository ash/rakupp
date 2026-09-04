# Fourier — every periodic signal is a sum of sines.
#
# The claim is outrageous and it is true: give me a repeating shape, however
# square or jagged, and I will build it out of nothing but smooth sine waves.
# This module is that claim made computable, in four parts:
#
#   * synthesis  — the coefficients of the classic waveforms, in closed form,
#                  and the partial sums they build;
#   * analysis   — going the other way, from samples to coefficients, by
#                  numerical integration and by the discrete transform;
#   * the fast transform — Cooley and Tukey's 1965 trick, which turns the
#                  O(n^2) sum into O(n log n) by noticing that a transform of
#                  length n is two transforms of length n/2;
#   * the checks — Parseval's theorem, the DFT/FFT agreement, and the Gibbs
#                  overshoot, each of which has an exact value to compare to.
#
# Everything is periodic with period 1, so the nth harmonic is cos(2*pi*n*t)
# and sin(2*pi*n*t) and there are no stray scale factors to carry around.
#
#   f(t) = a0/2 + sum over n of [ a_n cos(2 pi n t) + b_n sin(2 pi n t) ]

unit module Fourier;

constant TAU = 2 * pi;

# ------------------------------------------------------------- waveforms ---

#| The exact value of a named waveform at t, for t anywhere on the real line.
#| These are the targets the series has to reproduce.
sub wave-value(Str $shape, $t, :$duty = 0.25) is export {
    my $x = $t - floor($t);          # fold into one period
    given $shape {
        when 'square'   { $x < 0.5 ?? 1e0 !! -1e0 }
        when 'sawtooth' { 2 * ($x < 0.5 ?? $x !! $x - 1) }
        when 'triangle' { $x < 0.25 ?? 4 * $x !! $x < 0.75 ?? 2 - 4 * $x !! 4 * $x - 4 }
        when 'pulse'    { $x < $duty / 2 || $x > 1 - $duty / 2 ?? 1e0 !! 0e0 }
        when 'rectified' { abs(sin(pi * $x)) }
        default         { die "unknown waveform: $shape" }
    }
}

constant WAVEFORMS = <square sawtooth triangle pulse rectified>;

#| The Fourier coefficients of those waveforms, in closed form -- the
#| integrals done on paper once, so the engine never has to do them again.
#| Returns a0 and the harmonics as two arrays indexed 0..$n.
#|
#|   square    b_n = 4/(pi n),          odd n only   -- 1/n, so it converges
#|                                                      slowly and rings
#|   sawtooth  b_n = 2(-1)^(n+1)/(pi n)              -- also 1/n, also rings
#|   triangle  b_n = 8(-1)^((n-1)/2)/(pi n)^2, odd n -- 1/n^2: no ringing,
#|                                                      because it is continuous
#|   pulse     a_n = 2 sin(pi n d)/(pi n)            -- centred, so it is even:
#|                                                      cosines only, and the
#|                                                      envelope is a sinc
#|
#| The rule underneath: the smoother the signal, the faster the coefficients
#| die away. A jump gives 1/n, a kink gives 1/n^2, smooth gives faster than
#| any power.
sub wave-coefficients(Str $shape, Int $n, :$duty = 0.25) is export {
    my @a = 0e0 xx ($n + 1);
    my @b = 0e0 xx ($n + 1);
    given $shape {
        when 'square' {
            for 1..$n -> $k { @b[$k] = 4 / (pi * $k) if $k % 2 }
        }
        when 'sawtooth' {
            for 1..$n -> $k { @b[$k] = 2 * (-1) ** ($k + 1) / (pi * $k) }
        }
        when 'triangle' {
            for 1..$n -> $k {
                @b[$k] = 8 * (-1) ** (($k - 1) div 2) / (pi * $k) ** 2 if $k % 2;
            }
        }
        when 'pulse' {
            @a[0] = 2 * $duty;
            for 1..$n -> $k { @a[$k] = 2 * sin(pi * $k * $duty) / (pi * $k) }
        }
        when 'rectified' {
            # |sin(pi t)| = 2/pi - (4/pi) sum cos(2 pi k t)/(4k^2 - 1).
            # Every harmonic is present and they die away as 1/k^2, because
            # the signal is continuous but has a corner at every zero.
            @a[0] = 4 / pi;
            for 1..$n -> $k { @a[$k] = -4 / (pi * (4 * $k ** 2 - 1)) }
        }
        default { die "unknown waveform: $shape" }
    }
    %( a => @a, b => @b );
}

#| The partial sum: the first $upto harmonics of %c, evaluated at t.
#| Watching this as $upto grows is the whole subject in one picture.
sub series-value(%c, $t, Int :$upto) is export {
    my @a := %c<a>;
    my @b := %c<b>;
    my $n = ($upto // @a.end) min @a.end;
    my $s = @a[0] / 2;
    for 1..$n -> $k {
        my $w = TAU * $k * $t;
        $s += @a[$k] * cos($w) + @b[$k] * sin($w);
    }
    $s;
}

# -------------------------------------------------------------- analysis ---

#| The other direction: coefficients from samples, by the integrals
#| themselves. a_n = 2 * average of f(t) cos(2 pi n t), and likewise for b_n
#| with a sine -- estimated here by the midpoint rule, which for a periodic
#| function is spectacularly accurate.
sub analyse(&f, Int $n, Int :$samples = 2048) is export {
    my @a = 0e0 xx ($n + 1);
    my @b = 0e0 xx ($n + 1);
    for ^$samples -> $j {
        my $t = ($j + 0.5) / $samples;
        my $v = &f($t);
        for 0..$n -> $k {
            my $w = TAU * $k * $t;
            @a[$k] += $v * cos($w);
            @b[$k] += $v * sin($w);
        }
    }
    @a = @a.map(2 * * / $samples);
    @b = @b.map(2 * * / $samples);
    %( a => @a, b => @b );
}

# ------------------------------------------------------------- transforms ---

#| The discrete Fourier transform, straight from its definition:
#|   X[k] = sum over j of x[j] * exp(-2 pi i j k / n)
#| n outputs, each a sum of n terms, so n^2 multiplications. Correct, slow,
#| and the thing the fast version has to agree with.
sub dft(@x) is export {
    my $n = @x.elems;
    my @out;
    for ^$n -> $k {
        my ($re, $im) = 0e0, 0e0;
        for ^$n -> $j {
            my $w = -TAU * $k * $j / $n;
            my $c = @x[$j].Complex;
            $re += $c.re * cos($w) - $c.im * sin($w);
            $im += $c.re * sin($w) + $c.im * cos($w);
        }
        @out.push: Complex.new($re, $im);
    }
    @out;
}

#| Cooley-Tukey, radix 2. The whole idea in three lines: split the input into
#| even- and odd-indexed halves, transform each (recursively), and then every
#| output of the full transform is one output of the even half plus a rotated
#| output of the odd half. n log n instead of n^2 -- for n = 4096 that is
#| 49,152 operations instead of 16,777,216.
sub fft(@x) is export {
    my $n = @x.elems;
    return @x.map(*.Complex).Array if $n <= 1;
    die "fft needs a power-of-two length, got $n" if $n +& ($n - 1);
    my @even = fft(@x[(0, 2 ...^ $n)]);
    my @odd  = fft(@x[(1, 3 ...^ $n)]);
    my $half = $n div 2;
    my @out = (0+0i) xx $n;
    for ^$half -> $k {
        my $w = -TAU * $k / $n;
        my $t = Complex.new(cos($w), sin($w)) * @odd[$k];   # the twiddle factor
        @out[$k]         = @even[$k] + $t;
        @out[$k + $half] = @even[$k] - $t;
    }
    @out;
}

#| The inverse: conjugate, transform, conjugate, divide by n.
sub ifft(@x) is export {
    my $n = @x.elems;
    my @c = @x.map({ .Complex.conj });
    fft(@c).map({ .conj / $n }).Array;
}

#| The one-sided magnitude spectrum of a real signal: bin k is the amplitude
#| of the component at k cycles per window. Only the first half is meaningful
#| -- above n/2 the bins are mirror images (that is aliasing, drawn).
sub spectrum(@x) is export {
    my $n = @x.elems;
    my @f = $n +& ($n - 1) ?? dft(@x) !! fft(@x);
    (0 .. $n div 2).map(-> $k {
        my $m = @f[$k].abs * 2 / $n;
        $k == 0 || $k == $n div 2 ?? $m / 2 !! $m;
    }).Array;
}

# ----------------------------------------------------------- the checks ---

#| Parseval's theorem: the mean square of a signal equals the sum of the
#| squares of its coefficients. Energy is energy, whichever way you look.
#|   mean of f^2 = (a0/2)^2 + (1/2) sum (a_n^2 + b_n^2)
sub parseval(%c) is export {
    my @a := %c<a>;
    my @b := %c<b>;
    (@a[0] / 2) ** 2 + 0.5 * (1..@a.end).map({ @a[$_] ** 2 + @b[$_] ** 2 }).sum;
}

#| The mean square of the signal itself, by direct integration -- the number
#| Parseval says the sum above must equal.
sub mean-square(&f, Int :$samples = 20000) is export {
    (^$samples).map({ &f(($_ + 0.5) / $samples) ** 2 }).sum / $samples;
}

#| Gibbs: the highest the partial sum reaches near a jump. It does NOT settle
#| down to the top of the jump as harmonics are added -- it settles down to
#| about 8.95% past it, forever, and only the width of the overshoot shrinks.
#| The exact limit for a unit square wave is 2*Si(pi)/pi = 1.178979744...
sub gibbs-peak(%c, Int :$upto, Int :$samples = 4000) is export {
    my $n = $upto // %c<a>.end;
    # The overshoot sits just left of the jump at t = 1/2, and it gets NARROWER
    # as harmonics are added -- the first maximum is at 1/2 - 1/(2(n+1)) -- so
    # a fixed grid over the whole half-period walks straight past it. Scan a
    # window that shrinks with n instead.
    my $from = 0.5 - 3 / ($n + 1);
    $from = 0e0 if $from < 0;
    my $best = -1e30;
    for ^$samples -> $j {
        my $t = $from + (0.5 - $from) * $j / $samples;
        my $v = series-value(%c, $t, :upto($n));
        $best = $v if $v > $best;
    }
    $best;
}

#| Si(pi), the sine integral, by the same midpoint rule. The Gibbs constant
#| is 2*Si(pi)/pi and this is where it comes from.
sub si-pi(Int :$samples = 200000) is export {
    (^$samples).map({
        my $t = pi * ($_ + 0.5) / $samples;
        sin($t) / $t;
    }).sum * pi / $samples;
}

#| How far the first $upto harmonics are from the real thing, in RMS.
sub rms-error(Str $shape, %c, Int :$upto, Int :$samples = 4000, :$duty = 0.25) is export {
    my $s = 0e0;
    for ^$samples -> $j {
        my $t = ($j + 0.5) / $samples;
        my $d = series-value(%c, $t, :$upto) - wave-value($shape, $t, :$duty);
        $s += $d * $d;
    }
    sqrt($s / $samples);
}

# --------------------------------------------------------------- signals ---

#| A signal built by adding pure tones: `partials` is a list of
#| (cycles-per-window, amplitude, phase) triples. This is what the spectrum
#| widget takes apart again.
sub tone-signal(@partials, Int $n) is export {
    (^$n).map(-> $j {
        my $t = $j / $n;
        @partials.map(-> @p { @p[1] * sin(TAU * @p[0] * $t + (@p[2] // 0)) }).sum;
    }).Array;
}

#| Sampling a waveform into $n points, ready for the transform.
sub sample-wave(Str $shape, Int $n, :$duty = 0.25) is export {
    (^$n).map({ wave-value($shape, $_ / $n, :$duty) }).Array;
}
