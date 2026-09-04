# Eclipse — when the Sun and the Moon line up.
#
# The whole prediction, from first principles, in one module:
#
#   * calendar dates <-> Julian Day, and TD - UT (Delta T);
#   * the instant of any New or Full Moon, from Meeus's periodic series;
#   * the geometry of an eclipse at that instant -- gamma (how far the shadow
#     axis misses the centre) and u (the radius of the umbral cone), from
#     which the type, the magnitude and the durations all follow;
#   * the saros: which 1,300-year family an eclipse belongs to.
#
# Everything is a plain function of one integer: k, the number of New Moons
# since the one of 2000 January 6. No ephemeris file, no table of eclipses --
# a few hundred sine terms and the sky is predicted.
#
# Source of the series: Jean Meeus, *Astronomical Algorithms*, 2nd ed.,
# chapter 49 (phases of the Moon) and chapter 54 (eclipses). Delta T is the
# Espenak & Meeus piecewise fit used by NASA's eclipse catalogue.
#
# Accuracy, against the NASA Five Millennium Canon: the type and the date are
# right for every eclipse of 1900-2100; the instant of greatest eclipse is
# within about a minute over that span, and degrades slowly outside it as
# Delta T becomes a guess rather than a measurement.

unit module Eclipse;

# --------------------------------------------------------------- angles ---

constant DEG = pi / 180;

sub sind($x) is export { sin($x * DEG) }
sub cosd($x) is export { cos($x * DEG) }

# An argument like M' grows by 385.8 degrees per lunation, so after 20,000
# lunations it is a seven-digit number. Fold it back before taking a sine:
# a double has 15 digits, and we want all of them in the fraction.
sub norm360($x) is export { $x - 360 * floor($x / 360) }

# ------------------------------------------------------------ calendars ---

#| The Julian Day of a calendar date. Gregorian from 1582 October 15,
#| Julian before it -- the same rule the historical record uses.
sub julian-day(Int $y, Int $m, $d) is export {
    my ($yy, $mm) = $m > 2 ?? ($y, $m) !! ($y - 1, $m + 12);
    my $gregorian = $y > 1582 || ($y == 1582 && ($m > 10 || ($m == 10 && $d >= 15)));
    my $b = 0;
    if $gregorian {
        my $a = floor($yy / 100);
        $b = 2 - $a + floor($a / 4);
    }
    floor(365.25 * ($yy + 4716)) + floor(30.6001 * ($mm + 1)) + $d + $b - 1524.5;
}

#| The inverse: a Julian Day back to (year, month, day-with-fraction).
sub calendar-date($jd) is export {
    my $z = floor($jd + 0.5);
    my $f = $jd + 0.5 - $z;
    my $a = $z;
    if $z >= 2299161 {
        my $alpha = floor(($z - 1867216.25) / 36524.25);
        $a = $z + 1 + $alpha - floor($alpha / 4);
    }
    my $b = $a + 1524;
    my $c = floor(($b - 122.1) / 365.25);
    my $d = floor(365.25 * $c);
    my $e = floor(($b - $d) / 30.6001);
    my $day = $b - $d - floor(30.6001 * $e) + $f;
    my $month = $e < 14 ?? $e - 1 !! $e - 13;
    my $year = $month > 2 ?? $c - 4716 !! $c - 4715;
    ($year.Int, $month.Int, $day);
}

#| The decimal year of a Julian Day, good enough to pick a Delta T polynomial.
sub jd-year($jd) is export {
    my ($y, $m, $d) = calendar-date($jd);
    $y + ($m - 0.5) / 12;
}

#| 2026-08-12 18:46 UT, from a Julian Day. Rounded to the minute, because
#| that is the honest precision of the series below.
sub jd-stamp($jd) is export {
    my $r = ($jd * 1440 + 0.5).floor / 1440;   # round to the minute
    my ($y, $m, $d) = calendar-date($r);
    my $day = $d.floor;
    my $minutes = (($d - $day) * 1440 + 0.5).floor;
    my $hh = ($minutes / 60).floor;
    my $mi = $minutes % 60;
    sprintf('%04d-%02d-%02d %02d:%02d', $y, $m, $day, $hh, $mi);
}

sub jd-date($jd) is export { jd-stamp($jd).substr(0, 10) }

# ---------------------------------------------------------------- DeltaT ---

#| TD - UT, in seconds. The Moon's tides slow the Earth down, so a clock
#| that keeps uniform dynamical time (TD) -- the one the theory of the Moon
#| runs on -- drifts away from the rotating Earth's own time (UT). The drift
#| is measured, not predicted: before 1620 it is read off ancient eclipse
#| records, after 2030 it is an extrapolated parabola. Espenak & Meeus's fit.
sub delta-t($year) is export {
    my $t;
    if $year < -500 {
        my $u = ($year - 1820) / 100;
        return -20 + 32 * $u * $u;
    }
    elsif $year < 500 {
        $t = $year / 100;
        return 10583.6 - 1014.41 * $t + 33.78311 * $t**2 - 5.952053 * $t**3
             - 0.1798452 * $t**4 + 0.022174192 * $t**5 + 0.0090316521 * $t**6;
    }
    elsif $year < 1600 {
        $t = ($year - 1000) / 100;
        return 1574.2 - 556.01 * $t + 71.23472 * $t**2 + 0.319781 * $t**3
             - 0.8503463 * $t**4 - 0.005050998 * $t**5 + 0.0083572073 * $t**6;
    }
    elsif $year < 1700 {
        $t = $year - 1600;
        return 120 - 0.9808 * $t - 0.01532 * $t**2 + $t**3 / 7129;
    }
    elsif $year < 1800 {
        $t = $year - 1700;
        return 8.83 + 0.1603 * $t - 0.0059285 * $t**2 + 0.00013336 * $t**3 - $t**4 / 1174000;
    }
    elsif $year < 1860 {
        $t = $year - 1800;
        return 13.72 - 0.332447 * $t + 0.0068612 * $t**2 + 0.0041116 * $t**3
             - 0.00037436 * $t**4 + 0.0000121272 * $t**5 - 0.0000001699 * $t**6
             + 0.000000000875 * $t**7;
    }
    elsif $year < 1900 {
        $t = $year - 1860;
        return 7.62 + 0.5737 * $t - 0.251754 * $t**2 + 0.01680668 * $t**3
             - 0.0004473624 * $t**4 + $t**5 / 233174;
    }
    elsif $year < 1920 {
        $t = $year - 1900;
        return -2.79 + 1.494119 * $t - 0.0598939 * $t**2 + 0.0061966 * $t**3 - 0.000197 * $t**4;
    }
    elsif $year < 1941 {
        $t = $year - 1920;
        return 21.20 + 0.84493 * $t - 0.076100 * $t**2 + 0.0020936 * $t**3;
    }
    elsif $year < 1961 {
        $t = $year - 1950;
        return 29.07 + 0.407 * $t - $t**2 / 233 + $t**3 / 2547;
    }
    elsif $year < 1986 {
        $t = $year - 1975;
        return 45.45 + 1.067 * $t - $t**2 / 260 - $t**3 / 718;
    }
    elsif $year < 2005 {
        $t = $year - 2000;
        return 63.86 + 0.3345 * $t - 0.060374 * $t**2 + 0.0017275 * $t**3
             + 0.000651814 * $t**4 + 0.00002373599 * $t**5;
    }
    elsif $year < 2050 {
        $t = $year - 2000;
        return 62.92 + 0.32217 * $t + 0.005589 * $t**2;
    }
    elsif $year < 2150 {
        return -20 + 32 * (($year - 1820) / 100)**2 - 0.5628 * (2150 - $year);
    }
    else {
        my $u = ($year - 1820) / 100;
        return -20 + 32 * $u * $u;
    }
}

#| Dynamical time to Universal Time: the same instant, on the Earth's clock.
sub td-to-ut($jde) is export { $jde - delta-t(jd-year($jde)) / 86400 }

# ------------------------------------------------------- lunation number ---

# k counts New Moons from the one of 2000 January 6. It is an integer for a
# New Moon, +0.25 for First Quarter, +0.5 for Full, +0.75 for Last Quarter --
# and, because the eclipse series below are functions of k, it is also the
# only handle the whole module needs.

constant SYNODIC   = 29.530588861;   # New Moon to New Moon,   days (mean)
constant DRACONIC  = 27.212220817;   # node to node,           days (mean)
constant ANOMALISTIC = 27.554549878; # perigee to perigee,     days (mean)

#| The (fractional) lunation number of a decimal year.
sub lunation($year) is export { ($year - 2000) * 12.3685 }

#| The lunation number of a Julian Day.
sub lunation-of-jd($jd) is export { ($jd - 2451550.09766) / SYNODIC }

# The four arguments every series below is built from, for a given k.
# T is the time in Julian centuries; E corrects for the slowly shrinking
# eccentricity of the Earth's orbit, which modulates the solar terms.
sub moon-args($k) is export {
    my $t = $k / 1236.85;
    %(
        t  => $t,
        e  => 1 - 0.002516 * $t - 0.0000074 * $t ** 2,
        # the Sun's mean anomaly
        m  => norm360(2.5534 + 29.10535670 * $k - 0.0000014 * $t**2 - 0.00000011 * $t**3),
        # the Moon's mean anomaly
        mp => norm360(201.5643 + 385.81693528 * $k + 0.0107582 * $t**2
                      + 0.00001238 * $t**3 - 0.000000058 * $t**4),
        # the Moon's argument of latitude: how far it is from a node
        f  => norm360(160.7108 + 390.67050284 * $k - 0.0016118 * $t**2
                      - 0.00000227 * $t**3 + 0.000000011 * $t**4),
        # the longitude of the ascending node, drifting backwards in 18.6 y
        om => norm360(124.7746 - 1.56375588 * $k + 0.0020672 * $t**2 + 0.00000215 * $t**3),
    );
}

#| The mean phase: a straight line in k, plus a whisper of curvature.
#| Everything else in this module is a correction to this one number.
sub mean-phase($k) is export {
    my $t = $k / 1236.85;
    2451550.09766 + SYNODIC * $k
        + 0.00015437 * $t**2 - 0.000000150 * $t**3 + 0.00000000073 * $t**4;
}

# The fourteen planetary arguments: Venus and Jupiter tugging on the Earth's
# orbit, worth up to half an hour between them.
sub planetary($k, $t) is export {
    (0.000325 * sind(299.77 +  0.107408 * $k - 0.009173 * $t**2)) +
    (0.000165 * sind(251.88 +  0.016321 * $k)) +
    (0.000164 * sind(251.83 + 26.651886 * $k)) +
    (0.000126 * sind(349.42 + 36.412478 * $k)) +
    (0.000110 * sind( 84.66 + 18.206239 * $k)) +
    (0.000062 * sind(141.74 + 53.303771 * $k)) +
    (0.000060 * sind(207.14 +  2.453732 * $k)) +
    (0.000056 * sind(154.84 +  7.306860 * $k)) +
    (0.000047 * sind( 34.52 + 27.261239 * $k)) +
    (0.000042 * sind(207.19 +  0.121824 * $k)) +
    (0.000040 * sind(291.34 +  1.844379 * $k)) +
    (0.000037 * sind(161.72 + 24.198154 * $k)) +
    (0.000035 * sind(239.56 + 25.513099 * $k)) +
    (0.000023 * sind(331.55 +  3.592518 * $k));
}

#| The instant (TD, as a Julian Day) of a phase of the Moon.
#| $k must be an integer plus 0, 0.25, 0.5 or 0.75.
sub phase-jde($k) is export {
    my $frac = $k - floor($k);
    my %a = moon-args($k);
    my ($t, $e, $m, $mp, $f, $om) = %a<t e m mp f om>;
    my $jde = mean-phase($k) + planetary($k, $t);

    if $frac == 0 || $frac == 0.5 {
        # New Moon and Full Moon differ only in the fifth decimal of the
        # first two coefficients -- the same physics seen from either side.
        my $c1 = $frac == 0 ?? -0.40720 !! -0.40614;
        my $c2 = $frac == 0 ??  0.17241 !!  0.17302;
        my $c3 = $frac == 0 ??  0.01608 !!  0.01614;
        my $c4 = $frac == 0 ??  0.01039 !!  0.01043;
        my $c5 = $frac == 0 ??  0.00739 !!  0.00734;
        my $c6 = $frac == 0 ?? -0.00514 !! -0.00515;
        my $c7 = $frac == 0 ??  0.00208 !!  0.00209;
        $jde += $c1 * sind($mp)
              + $c2 * $e * sind($m)
              + $c3 * sind(2 * $mp)
              + $c4 * sind(2 * $f)
              + $c5 * $e * sind($mp - $m)
              + $c6 * $e * sind($mp + $m)
              + $c7 * $e * $e * sind(2 * $m)
              - 0.00111 * sind($mp - 2 * $f)
              - 0.00057 * sind($mp + 2 * $f)
              + 0.00056 * $e * sind(2 * $mp + $m)
              - 0.00042 * sind(3 * $mp)
              + 0.00042 * $e * sind($m + 2 * $f)
              + 0.00038 * $e * sind($m - 2 * $f)
              - 0.00024 * $e * sind(2 * $mp - $m)
              - 0.00017 * sind($om)
              - 0.00007 * sind($mp + 2 * $m)
              + 0.00004 * sind(2 * $mp - 2 * $f)
              + 0.00004 * sind(3 * $m)
              + 0.00003 * sind($mp + $m - 2 * $f)
              + 0.00003 * sind(2 * $mp + 2 * $f)
              - 0.00003 * sind($mp + $m + 2 * $f)
              + 0.00003 * sind($mp - $m + 2 * $f)
              - 0.00002 * sind($mp - $m - 2 * $f)
              - 0.00002 * sind(3 * $mp + $m)
              + 0.00002 * sind(4 * $mp);
    }
    else {
        $jde += -0.62801 * sind($mp)
              + 0.17172 * $e * sind($m)
              - 0.01183 * $e * sind($mp + $m)
              + 0.00862 * sind(2 * $mp)
              + 0.00804 * sind(2 * $f)
              + 0.00454 * $e * sind($mp - $m)
              + 0.00204 * $e * $e * sind(2 * $m)
              - 0.00180 * sind($mp - 2 * $f)
              - 0.00070 * sind($mp + 2 * $f)
              - 0.00040 * sind(3 * $mp)
              - 0.00034 * $e * sind(2 * $mp - $m)
              + 0.00032 * $e * sind($m + 2 * $f)
              + 0.00032 * $e * sind($m - 2 * $f)
              - 0.00028 * $e * $e * sind($mp + 2 * $m)
              + 0.00027 * $e * sind(2 * $mp + $m)
              - 0.00017 * sind($om)
              - 0.00005 * sind($mp - $m - 2 * $f)
              + 0.00004 * sind(2 * $mp + 2 * $f)
              - 0.00004 * sind($mp + $m + 2 * $f)
              + 0.00004 * sind($mp - 2 * $m)
              + 0.00003 * sind($mp + $m - 2 * $f)
              + 0.00003 * sind(3 * $m)
              + 0.00002 * sind(2 * $mp - 2 * $f)
              + 0.00002 * sind($mp - $m + 2 * $f)
              - 0.00002 * sind(3 * $mp + $m);
        my $w = 0.00306 - 0.00038 * $e * cosd($m) + 0.00026 * cosd($mp)
              - 0.00002 * cosd($mp - $m) + 0.00002 * cosd($mp + $m) + 0.00002 * cosd(2 * $f);
        $jde += $frac == 0.25 ?? $w !! -$w;
    }
    $jde;
}

constant PHASE-NAME = ('New Moon', 'First Quarter', 'Full Moon', 'Last Quarter');

#| Every phase of the Moon between two Julian Days, as
#| (k, name, JD in UT) triples.
sub phases($jd-from, $jd-to) is export {
    my @out;
    my $k = floor(lunation-of-jd($jd-from)) - 1;
    my $stop = lunation-of-jd($jd-to) + 1;
    while $k <= $stop {
        for 0, 0.25, 0.5, 0.75 -> $frac {
            my $kk = $k + $frac;
            my $ut = td-to-ut(phase-jde($kk));
            @out.push: ($kk, PHASE-NAME[($frac * 4).Int], $ut) if $jd-from <= $ut <= $jd-to;
        }
        $k++;
    }
    @out;
}

# --------------------------------------------------------------- eclipses ---

#| One predicted eclipse. Every field is derived from k alone.
class Prediction {
    has Str  $.kind      is rw;   # 'solar' or 'lunar'
    has Str  $.type      is rw;   # total / annular / hybrid / partial / penumbral
    has Real $.k         is rw;   # the lunation number
    has Real $.jde       is rw;   # greatest eclipse, dynamical time
    has Real $.jd        is rw;   # greatest eclipse, Universal Time
    has Real $.gamma     is rw;   # least distance of the axis from the centre
    has Real $.u         is rw;   # radius of the umbral cone, Earth radii
    has Real $.magnitude is rw;
    has Real $.penumbral is rw;   # lunar: penumbral magnitude
    has Int  $.saros     is rw;
    has Real $.sinF      is rw;   # how far from the node, as a sine
    has Real $.dpartial  is rw;   # lunar: how long each phase lasts, minutes
    has Real $.dtotal    is rw;
    has Real $.dpenumbral is rw;
    has Bool $.central   is rw;

    method date()  { jd-date($!jd) }
    method stamp() { jd-stamp($!jd) }
    method year()  { calendar-date($!jd)[0] }
}

# The saros numbering, from the arithmetic of two cycles.
#
# Within one saros series, eclipses are 223 lunations apart. Between two
# series numbered n and n+1 lies one *inex*, 358 lunations. So every eclipse
# sits at k = k0 + 223a + 358b, and its series number is s0 + b. Since
# 358 = 135 (mod 223) and 135 * 38 = 1 (mod 223), b -- and with it the series
# number -- is just 38 * (k - k0), reduced mod 223.
#
# The two anchors are famous eclipses: the total solar eclipse of 2017-08-21
# (saros 145, k = 218) and the total lunar eclipse of 2018-07-27 (saros 129,
# k = 229.5). Series numbers repeat every 223 lunations of the anchor; over
# the four millennia this module covers, the representative in 0..222 is the
# one in use.
sub saros-series($k, Bool :$lunar) is export {
    my ($k0, $s0) = $lunar ?? (229, 129) !! (218, 145);
    my $n = (38 * (floor($k) - $k0) + $s0) % 223;
    $n <= 0 ?? $n + 223 !! $n;
}

#| The eclipse at lunation $k, or Nil if the Moon misses.
#| An integer $k asks about a solar eclipse (New Moon), $k + 0.5 about a
#| lunar one (Full Moon).
sub eclipse-at($k) is export {
    my $lunar = ($k - floor($k)) == 0.5;
    my %a = moon-args($k);
    my ($t, $e, $m, $mp, $f, $om) = %a<t e m mp f om>;

    # The one-line filter. F is the Moon's angular distance from a node; the
    # Sun and the Moon are about half a degree wide and the Moon's orbit is
    # tilted about five degrees, so unless the Moon is within roughly 21
    # degrees of a node at the syzygy, no shadow can reach anything.
    my $sinF = sind($f);
    return Nil if abs($sinF) > 0.36;

    # F corrected for the wobble of the node, and the one planetary term that
    # survives at this precision.
    my $f1 = $f - 0.02665 * sind($om);
    my $a1 = 299.77 + 0.107408 * $k - 0.009173 * $t ** 2;

    # The instant of greatest eclipse -- the mean phase again, but with the
    # series truncated where an eclipse no longer cares.
    my $c1 = $lunar ?? -0.4065 !! -0.4075;
    my $c2 = $lunar ??  0.1727 !!  0.1721;
    my $jde = mean-phase($k)
        + $c1 * sind($mp)
        + $c2 * $e * sind($m)
        + 0.0161 * sind(2 * $mp)
        - 0.0097 * sind(2 * $f1)
        + 0.0073 * $e * sind($mp - $m)
        - 0.0050 * $e * sind($mp + $m)
        - 0.0023 * sind($mp - 2 * $f1)
        + 0.0021 * $e * sind(2 * $m)
        + 0.0012 * sind($mp + 2 * $f1)
        + 0.0006 * $e * sind(2 * $mp + $m)
        - 0.0004 * sind(3 * $mp)
        - 0.0003 * $e * sind($m + 2 * $f1)
        + 0.0003 * sind($a1)
        - 0.0002 * $e * sind($m - 2 * $f1)
        - 0.0002 * $e * sind(2 * $mp - $m)
        - 0.0002 * sind($om);

    # P and Q are the shadow axis resolved along and across the ecliptic,
    # in Earth radii; gamma combines them with the Moon's latitude.
    my $p = 0.2070 * $e * sind($m)
          + 0.0024 * $e * sind(2 * $m)
          - 0.0392 * sind($mp)
          + 0.0116 * sind(2 * $mp)
          - 0.0073 * $e * sind($mp + $m)
          + 0.0067 * $e * sind($mp - $m)
          + 0.0118 * sind(2 * $f1);
    my $q = 5.2207
          - 0.0048 * $e * cosd($m)
          + 0.0020 * $e * cosd(2 * $m)
          - 0.3299 * cosd($mp)
          - 0.0060 * $e * cosd($mp + $m)
          + 0.0041 * $e * cosd($mp - $m);
    my $w = abs(cosd($f1));
    my $gamma = ($p * cosd($f1) + $q * sind($f1)) * (1 - 0.0048 * $w);

    # u: the radius of the Moon's umbral cone where it crosses the
    # fundamental plane, in Earth radii. Negative means the cone still has
    # length to spare when it arrives -- a total eclipse. Positive means it
    # ran out above the ground, and the Sun shows as a ring.
    my $u = 0.0059
          + 0.0046 * $e * cosd($m)
          - 0.0182 * cosd($mp)
          + 0.0004 * cosd(2 * $mp)
          - 0.0005 * $e * cosd($m + $mp);

    my $g = abs($gamma);
    my $ec = Prediction.new(
        kind => $lunar ?? 'lunar' !! 'solar',
        k => $k, jde => $jde, jd => td-to-ut($jde),
        gamma => $gamma, u => $u, sinF => $sinF,
        saros => saros-series($k, :$lunar),
        central => False, magnitude => 0e0, penumbral => 0e0,
        dpartial => 0e0, dtotal => 0e0, dpenumbral => 0e0, type => '',
    );

    if $lunar {
        # Two shadows: the umbra, radius 0.7403 - u, and the penumbra around
        # it. The magnitude is the fraction of the Moon's diameter inside
        # the umbra; 1.0 or more and the Moon is wholly inside it.
        my $umbral = (1.0128 - $u - $g) / 0.5450;
        my $pen    = (1.5573 + $u - $g) / 0.5450;
        return Nil if $pen <= 0;
        $ec.magnitude = $umbral;
        $ec.penumbral = $pen;
        $ec.type = $umbral >= 1 ?? 'total' !! $umbral > 0 ?? 'partial' !! 'penumbral';

        # n is the Moon's speed through the shadow, in Earth radii per hour;
        # the chord across a circle of radius r is 2*sqrt(r^2 - gamma^2), so
        # the phase lasts 2 * 60/n * that half-chord. Meeus gives the
        # semiduration; these fields are the whole thing, in minutes.
        my $n = 0.5458 + 0.0400 * cosd($mp);
        my $rp = 1.0128 - $u;      # the Moon touching the umbra
        my $rt = 0.4678 - $u;      # the Moon wholly inside it
        my $rh = 1.5573 + $u;      # the outer edge of the penumbra
        $ec.dpenumbral = 120 / $n * sqrt($rh ** 2 - $gamma ** 2) if $rh ** 2 > $gamma ** 2;
        $ec.dpartial   = 120 / $n * sqrt($rp ** 2 - $gamma ** 2) if $rp ** 2 > $gamma ** 2;
        $ec.dtotal     = 120 / $n * sqrt($rt ** 2 - $gamma ** 2) if $rt ** 2 > $gamma ** 2;
    }
    else {
        return Nil if $g > 1.5433 + $u;
        if $g < 0.9972 {
            # The axis hits the Earth: somewhere the eclipse is central.
            $ec.central = True;
            $ec.magnitude = 1e0;
            if $u < 0 {
                $ec.type = 'total';
            }
            elsif $u > 0.0047 {
                $ec.type = 'annular';
            }
            else {
                # The narrow band where the cone's tip skims the surface:
                # the eclipse is annular at the ends of its track and total
                # in the middle. Meeus's test for it.
                my $omega = 0.00464 * sqrt(1 - $gamma ** 2);
                $ec.type = $u < $omega ?? 'hybrid' !! 'annular';
            }
        }
        else {
            $ec.type = 'partial';
            $ec.magnitude = (1.5433 + $u - $g) / (0.5461 + 2 * $u);
            # Between 0.9972 and 0.9972 + |u| the axis misses the globe but
            # the cone itself still grazes a pole: total or annular, and
            # non-central.
            $ec.type = 'non-central ' ~ ($u < 0 ?? 'total' !! 'annular')
                if $g < 0.9972 + abs($u);
        }
    }
    $ec;
}

#| Every eclipse whose greatest phase falls between two Julian Days.
sub eclipses($jd-from, $jd-to, Str :$kind = 'both') is export {
    my @out;
    my $k = floor(lunation-of-jd($jd-from)) - 2;
    my $stop = lunation-of-jd($jd-to) + 2;
    while $k <= $stop {
        for 0, 0.5 -> $frac {
            next if $kind eq 'solar' && $frac == 0.5;
            next if $kind eq 'lunar' && $frac == 0;
            my $e = eclipse-at($k + $frac);
            @out.push: $e if $e && $jd-from <= $e.jd <= $jd-to;
        }
        $k++;
    }
    @out.sort(*.jd);
}

#| Every eclipse of one saros series, walked out 223 lunations at a time
#| from any member. A series is born at one pole, marches across the globe
#| and dies at the other, over roughly 1,300 years.
sub saros-run(Int $series, Bool :$lunar, Int :$limit = 100) is export {
    # Find a member: step through lunations until the series matches.
    my $frac = $lunar ?? 0.5 !! 0;
    my $seed;
    for -400 .. 400 -> $k {
        if saros-series($k, :$lunar) == $series {
            $seed = $k;
            last;
        }
    }
    return () unless $seed.defined;
    my @out;
    # Back up to the start of the series, then walk forward.
    my $k = $seed;
    $k -= 223 while eclipse-at($k - 223 + $frac) && $k > $seed - 223 * $limit;
    while @out < $limit {
        my $e = eclipse-at($k + $frac);
        last unless $e;
        @out.push: $e;
        $k += 223;
    }
    @out;
}
