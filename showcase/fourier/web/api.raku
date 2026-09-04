# The browser API: the same engine, reached from JavaScript.
#
# Everything registered on `globalThis.fourier` takes and returns plain data,
# so the page never has to know what a Raku Complex is.

use JS;

my %api =

    # The named waveforms, for the pickers.
    waves => -> { WAVEFORMS.Array },

    # The closed-form coefficients of a waveform.
    coefficients => -> $shape, $n, $duty {
        my %c = wave-coefficients($shape, $n.Int, :duty($duty.Num));
        %( a => %c<a>.map(*.Num).Array, b => %c<b>.map(*.Num).Array );
    },

    # A waveform and the partial sum chasing it, sampled for drawing.
    curve => -> $shape, $n, $duty, $points {
        my $p = $points.Int;
        my %c = wave-coefficients($shape, $n.Int, :duty($duty.Num));
        my @target = (^$p).map({ wave-value($shape, $_ / $p, :duty($duty.Num)).Num });
        my @series = (^$p).map({ series-value(%c, $_ / $p, :upto($n.Int)).Num });
        %( target => @target.Array, series => @series.Array,
           a => %c<a>.map(*.Num).Array, b => %c<b>.map(*.Num).Array,
           rms => rms-error($shape, %c, :upto($n.Int), :samples(1000),
                            :duty($duty.Num)).Num );
    },

    # The series from coefficients the reader set by hand: one slider each.
    custom => -> @a, @b, $points {
        my $p = $points.Int;
        my %c = %( a => @a.map(*.Num).Array, b => @b.map(*.Num).Array );
        %( series => (^$p).map({ series-value(%c, $_ / $p).Num }).Array,
           energy => parseval(%c).Num );
    },

    # How fast the error falls as harmonics are added -- the smoothness law.
    converge => -> $shape, $duty {
        my @out;
        for 1, 2, 3, 5, 7, 11, 15, 21, 31, 45, 63, 91 -> $n {
            my %c = wave-coefficients($shape, $n, :duty($duty.Num));
            @out.push: %( n => $n,
                          rms => rms-error($shape, %c, :upto($n), :samples(600),
                                           :duty($duty.Num)).Num,
                          peak => gibbs-peak(%c, :upto($n), :samples(600)).Num );
        }
        @out.Array;
    },

    # The overshoot, and the curve around the jump that shows it.
    gibbs => -> $n, $points {
        my $h = $n.Int;
        my $p = $points.Int;
        my %c = wave-coefficients('square', $h);
        # a window around the jump at t = 1/2, wide enough to hold the ringing
        my $w = 8 / ($h + 1);
        my @curve = (^$p).map(-> $j {
            my $t = 0.5 - $w + 2 * $w * $j / $p;
            series-value(%c, $t, :upto($h)).Num;
        });
        %( curve => @curve.Array, window => $w.Num,
           peak => gibbs-peak(%c, :upto($h), :samples(1200)).Num,
           limit => (2 * si-pi(:samples(20000)) / pi).Num );
    },

    # A signal built from pure tones (plus optional noise), and the spectrum
    # that finds them again.
    tones => -> @partials, $n, $noise {
        my @p = @partials.map({ [$_[0].Num, $_[1].Num, ($_[2] // 0).Num] });
        my @sig = tone-signal(@p, $n.Int);
        my $amp = $noise.Num;
        @sig = @sig.map({ $_ + $amp * (2 * rand - 1) }) if $amp > 0;
        %( signal => @sig.map(*.Num).Array, spectrum => spectrum(@sig).map(*.Num).Array );
    },

    # The transform itself, either way round, with the operation count that
    # makes the difference between them visible.
    transform => -> @x, $fast {
        my @v = @x.map(*.Num);
        my $n = @v.elems;
        my @f = $fast ?? fft(@v) !! dft(@v);
        %( re => @f.map(*.re.Num).Array, im => @f.map(*.im.Num).Array,
           abs => @f.map(*.abs.Num).Array,
           ops => ($fast ?? ($n * log($n) / log(2)).round !! $n * $n) );
    },

    # The spectrum of a sampled waveform: where a square wave's energy lives.
    wave-spectrum => -> $shape, $n, $duty {
        my @sig = sample-wave($shape, $n.Int, :duty($duty.Num));
        %( signal => @sig.map(*.Num).Array,
           spectrum => spectrum(@sig).map(*.Num).Array );
    },
    ;

JS<fourier> = %api;
