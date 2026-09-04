# The browser API: the same engine, reached from JavaScript.
#
# `use JS` gives the transpiled program the host's global object. Everything
# below is registered on `globalThis.eclipse`, and every value that crosses
# the boundary is plain data — hashes, arrays, numbers, strings — so the page
# never has to know what a Raku object is.

use JS;

sub record($e) {
    %(
        kind => $e.kind, type => $e.type, stamp => $e.stamp, date => $e.date,
        k => $e.k.Num, jd => $e.jd.Num, gamma => $e.gamma.Num, u => $e.u.Num,
        magnitude => $e.magnitude.Num, penumbral => $e.penumbral.Num,
        saros => $e.saros, central => $e.central, sinF => $e.sinF.Num,
        dtotal => $e.dtotal.Num, dpartial => $e.dpartial.Num,
        dpenumbral => $e.dpenumbral.Num,
        year => $e.year,
    );
}

my %api =

    # Every eclipse in a range of years. kind is 'solar', 'lunar' or 'both'.
    list => -> $from, $to, $kind {
        eclipses(julian-day($from.Int, 1, 1), julian-day($to.Int + 1, 1, 1), :$kind)
            .map({ record($_) }).Array;
    },

    # One eclipse by lunation number, for the diagrams.
    at => -> $k {
        my $e = eclipse-at($k);
        $e ?? record($e) !! Nil;
    },

    # sin F at every syzygy of a range: the eclipse-season strip.
    seasons => -> $from, $to, $lunar {
        my $frac = $lunar ?? 0.5 !! 0;
        my @out;
        my $k = floor(lunation($from.Int)) - 1;
        my $stop = lunation($to.Int + 1);
        while $k <= $stop {
            my $kk = $k + $frac;
            my %a = moon-args($kk);
            my $e = eclipse-at($kk);
            @out.push: %(
                k => $kk.Num,
                date => jd-date(td-to-ut(phase-jde($kk))),
                sinF => sind(%a<f>).Num,
                type => $e ?? $e.type !! '',
                gamma => $e ?? $e.gamma.Num !! 0e0,
            );
            $k++;
        }
        @out.Array;
    },

    # A whole saros family, 223 lunations at a time.
    saros => -> $series, $lunar {
        saros-run($series.Int, :lunar($lunar ?? True !! False)).map({ record($_) }).Array;
    },

    # The four phases of the Moon in one month.
    phases => -> $year, $month {
        my $y = $year.Int;
        my $m = $month.Int;
        my $from = julian-day($y, $m, 1);
        my $to = julian-day($m == 12 ?? $y + 1 !! $y, $m == 12 ?? 1 !! $m + 1, 1);
        phases($from, $to).map(-> ($k, $name, $jd) {
            %( k => $k.Num, name => $name, stamp => jd-stamp($jd) )
        }).Array;
    },

    # The mean phase and its corrections, term by term: chapter 2's widget.
    terms => -> $k {
        my %a = moon-args($k);
        my $e = %a<e>;
        my $m = %a<m>;
        my $mp = %a<mp>;
        my $f = %a<f>;
        my $om = %a<om>;
        my $mean = mean-phase($k);
        my @t =
            %( name => "-0.40720 sin M'",      v => -0.40720 * sind($mp) ),
            %( name => "+0.17241 E sin M",     v =>  0.17241 * $e * sind($m) ),
            %( name => "+0.01608 sin 2M'",     v =>  0.01608 * sind(2 * $mp) ),
            %( name => "+0.01039 sin 2F",      v =>  0.01039 * sind(2 * $f) ),
            %( name => "+0.00739 E sin(M'-M)", v =>  0.00739 * $e * sind($mp - $m) ),
            %( name => "-0.00514 E sin(M'+M)", v => -0.00514 * $e * sind($mp + $m) ),
            %( name => "+0.00208 E2 sin 2M",   v =>  0.00208 * $e * $e * sind(2 * $m) ),
            %( name => "-0.00111 sin(M'-2F)",  v => -0.00111 * sind($mp - 2 * $f) ),
            %( name => "-0.00057 sin(M'+2F)",  v => -0.00057 * sind($mp + 2 * $f) ),
            %( name => "-0.00017 sin Omega",   v => -0.00017 * sind($om) );
        my $jde = phase-jde($k);
        %(
            k => $k.Num, mean => $mean.Num, jde => $jde.Num,
            meanStamp => jd-stamp($mean), stamp => jd-stamp(td-to-ut($jde)),
            m => %a<m>.Num, mp => %a<mp>.Num, f => %a<f>.Num, om => %a<om>.Num,
            e => $e.Num, t => %a<t>.Num,
            sinF => sind($f).Num,
            deltaT => delta-t(jd-year($jde)).Num,
            terms => @t.map({ %( name => $_<name>, v => $_<v>.Num, minutes => ($_<v> * 1440).Num ) }).Array,
            rest => ($jde - $mean - @t.map({ $_<v> }).sum).Num,
        );
    },

    # The lunation number closest to a calendar date, New Moon or Full.
    nearest => -> $y, $m, $d, $full {
        my $jd = julian-day($y.Int, $m.Int, $d.Num);
        my $frac = $full ?? 0.5 !! 0;
        my $k = (lunation-of-jd($jd) - $frac).round + $frac;
        $k.Num;
    },

    # TD - UT over a span of years: chapter 7's curve.
    deltat => -> $from, $to, $step {
        my @out;
        my $y = $from.Num;
        while $y <= $to.Num {
            @out.push: %( year => $y, dt => delta-t($y).Num );
            $y += $step.Num;
        }
        @out.Array;
    },

    # The three months whose near-commensurability *is* the saros.
    cycles => -> {
        %( synodic => 29.530588861, draconic => 27.212220817, anomalistic => 27.554549878 );
    },
    ;

JS<eclipse> = %api;
