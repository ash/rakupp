# Orbit — where the planets are, and what it costs to go there.
#
# Two ideas carry the whole module.
#
# The first is Kepler's equation, M = E - e sin E, which relates where a body
# WOULD be if it moved uniformly (the mean anomaly M) to where it actually is
# (the eccentric anomaly E). It cannot be solved in closed form -- Kepler said
# so himself, and three centuries of attempts agreed -- so we iterate, and it
# converges in a handful of steps.
#
# The second is that an orbit is six numbers. Size, shape, and three angles
# that orient the ellipse in space, plus where the body was at a known moment.
# Feed those six through Kepler's equation and you get a position; feed the
# planets' six through it and you get the solar system.
#
# The elements themselves come from Standish's linear fit (JPL, "Approximate
# Positions of the Major Planets"), which is a value per element at J2000 plus
# a rate per century -- good to a fraction of an arcminute from 1800 to 2050.
#
# Sources: Meeus, *Astronomical Algorithms*, chapters 30 and 33; Standish's
# element table; the vis-viva equation, which is just conservation of energy.

unit module Orbit;

constant DEG = pi / 180;
constant AU = 149_597_870.7;             # km
constant DAY = 86_400;                   # s
constant GM-SUN = 1.32712440018e11;      # km^3/s^2
constant GM-EARTH = 398_600.4418;        # km^3/s^2
constant R-EARTH = 6378.137;             # km

sub sind($x) is export { sin($x * DEG) }
sub cosd($x) is export { cos($x * DEG) }
sub norm360($x) is export { $x - 360 * floor($x / 360) }
sub norm180($x) is export { my $a = norm360($x); $a > 180 ?? $a - 360 !! $a }

# ------------------------------------------------------- Kepler's equation ---

#| Solve M = E - e sin E for E, by Newton-Raphson. Everything is in radians
#| here because the derivative would otherwise carry a stray factor.
#|
#| Newton's step is E <- E - (E - e sin E - M) / (1 - e cos E). Started from
#| M it converges quadratically -- each step roughly doubles the number of
#| correct digits -- so five or six iterations reach the limit of a double
#| even for a comet. Returns the answer and the number of steps it took, so
#| a caller can watch it converge.
sub kepler($m-rad, $e, :$tolerance = 1e-12, :$max = 60) is export {
    my $m = $m-rad - 2 * pi * floor($m-rad / (2 * pi));
    # a better start than M for eccentric orbits: the first term of the
    # equation of the centre
    my $ecc = $e;
    my $E = $ecc < 0.8 ?? $m + $ecc * sin($m) !! pi;
    my $steps = 0;
    my @trail = $E;
    for 1..$max {
        my $f = $E - $ecc * sin($E) - $m;
        my $fp = 1 - $ecc * cos($E);
        my $d = $f / $fp;
        $E -= $d;
        @trail.push: $E;
        $steps++;
        last if abs($d) < $tolerance;
    }
    %( E => $E, steps => $steps, trail => @trail,
       residual => abs($E - $ecc * sin($E) - $m) );
}

#| The true anomaly: the angle actually subtended at the focus, as against
#| the eccentric anomaly's angle at the centre of the ellipse.
sub true-anomaly($E, $e) is export {
    2 * atan2(sqrt(1 + $e) * sin($E / 2), sqrt(1 - $e) * cos($E / 2));
}

#| The distance from the focus, in the same units as a.
sub radius($a, $e, $E) is export { $a * (1 - $e * cos($E)) }

# ------------------------------------------------------------- the planets ---

# Standish's table: for each planet, the six elements at J2000 and their rates
# per Julian century. Columns are
#   a (au)  e  I (deg)  L (deg)  longitude of perihelion  longitude of node
constant PLANETS = <Mercury Venus Earth Mars Jupiter Saturn Uranus Neptune>;

my %ELEMENTS =
    Mercury => [[ 0.38709927,  0.20563593,  7.00497902,  252.25032350,  77.45779628,  48.33076593],
                [ 0.00000037,  0.00001906, -0.00594749, 149472.67411175, 0.16047689,  -0.12534081]],
    Venus   => [[ 0.72333566,  0.00677672,  3.39467605,  181.97909950, 131.60246718,  76.67984255],
                [ 0.00000390, -0.00004107, -0.00078890, 58517.81538729,  0.00268329,  -0.27769418]],
    Earth   => [[ 1.00000261,  0.01671123, -0.00001531,  100.46457166, 102.93768193,   0.0       ],
                [ 0.00000562, -0.00004392, -0.01294668, 35999.37244981,  0.32327364,   0.0       ]],
    Mars    => [[ 1.52371034,  0.09339410,  1.84969142,   -4.55343205, -23.94362959,  49.55953891],
                [ 0.00001847,  0.00007882, -0.00813131, 19140.30268499,  0.44441088,  -0.29257343]],
    Jupiter => [[ 5.20288700,  0.04838624,  1.30439695,   34.39644051,  14.72847983, 100.47390909],
                [-0.00011607, -0.00013253, -0.00183714,  3034.74612775,  0.21252668,   0.20469106]],
    Saturn  => [[ 9.53667594,  0.05386179,  2.48599187,   49.95424423,  92.59887831, 113.66242448],
                [-0.00125060, -0.00050991,  0.00193609,  1222.49362201, -0.41897216,  -0.28867794]],
    Uranus  => [[19.18916464,  0.04725744,  0.77263783,  313.23810451, 170.95427630,  74.01692503],
                [-0.00196176, -0.00004397, -0.00242939,   428.48202785,  0.40805281,   0.04240589]],
    Neptune => [[30.06992276,  0.00859048,  1.77004347,  -55.12002969,  44.96476227, 131.78422574],
                [ 0.00026291,  0.00005105,  0.00035372,   218.45945325, -0.32241464,  -0.00508664]];

#| The six elements of a planet at a given Julian Day.
sub elements(Str $planet, $jd) is export {
    my $rows = %ELEMENTS{$planet} // die "unknown planet: $planet";
    my $t = ($jd - 2451545.0) / 36525;
    my @v = (^6).map({ $rows[0][$_] + $rows[1][$_] * $t });
    %( a => @v[0], e => @v[1], i => @v[2],
       L => norm360(@v[3]), peri => @v[4], node => @v[5],
       # what the propagation actually needs
       w => @v[4] - @v[5],                  # argument of perihelion
       M => norm180(@v[3] - @v[4]),         # mean anomaly
       t => $t );
}

#| Heliocentric ecliptic coordinates of a planet, in au.
#| Solve Kepler, place the body in its own orbital plane, then rotate that
#| plane into the ecliptic by the argument of perihelion, the inclination and
#| the longitude of the node -- three rotations, in that order.
sub position(Str $planet, $jd) is export {
    my %el = elements($planet, $jd);
    my %k = kepler(%el<M> * DEG, %el<e>);
    my $E = %k<E>;
    # in the orbital plane, perihelion along +x
    my $xp = %el<a> * (cos($E) - %el<e>);
    my $yp = %el<a> * sqrt(1 - %el<e> ** 2) * sin($E);
    my ($x, $y, $z) = to-ecliptic($xp, $yp, %el<i>, %el<w>, %el<node>);
    %( x => $x, y => $y, z => $z,
       r => sqrt($x ** 2 + $y ** 2 + $z ** 2),
       lon => norm360(atan2($y, $x) / DEG),
       lat => atan2($z, sqrt($x ** 2 + $y ** 2)) / DEG,
       E => $E, nu => true-anomaly($E, %el<e>) / DEG,
       steps => %k<steps>, a => %el<a>, e => %el<e> );
}

#| Rotate a point from the orbital plane into the ecliptic: by the argument
#| of perihelion, then the inclination, then the longitude of the node.
#| Three rotations, always in that order -- get it wrong and the orbit comes
#| out mirrored, which is the classic way to lose a spacecraft on paper.
sub to-ecliptic($xp, $yp, $i, $w, $node) is export {
    my ($cw, $sw) = cosd($w), sind($w);
    my ($ci, $si) = cosd($i), sind($i);
    my ($cn, $sn) = cosd($node), sind($node);
    ( ($cw * $cn - $sw * $sn * $ci) * $xp + (-$sw * $cn - $cw * $sn * $ci) * $yp,
      ($cw * $sn + $sw * $cn * $ci) * $xp + (-$sw * $sn + $cw * $cn * $ci) * $yp,
      ($sw * $si) * $xp + ($cw * $si) * $yp );
}

#| The ellipse itself, as points in the ecliptic frame -- what a drawing of
#| an orbit actually needs. The Sun sits at a focus, not at the centre.
sub orbit-path($a, $e, $i, $w, $node, Int :$points = 240) is export {
    (0..$points).map(-> $k {
        my $E = 2 * pi * $k / $points;
        my $xp = $a * (cos($E) - $e);
        my $yp = $a * sqrt(1 - $e ** 2) * sin($E);
        my ($x, $y, $z) = to-ecliptic($xp, $yp, $i, $w, $node);
        %( x => $x, y => $y, z => $z );
    }).Array;
}

#| The orbital period, from Kepler's third law: T^2 proportional to a^3.
#| In days, with a in au.
sub period($a) is export { 365.256898326 * $a ** 1.5 }

#| Speed at distance r on an orbit of semi-major axis a, in km/s.
#| The vis-viva equation, which is conservation of energy in disguise:
#| kinetic plus potential is a constant, and that constant is -GM/2a.
sub vis-viva($r-km, $a-km, $mu = GM-SUN) is export {
    sqrt($mu * (2 / $r-km - 1 / $a-km));
}

#| Where a planet appears from the Earth: elongation from the Sun, and the
#| distance to it. Opposition is elongation 180, conjunction 0.
sub geocentric(Str $planet, $jd) is export {
    my %p = position($planet, $jd);
    my %e = position('Earth', $jd);
    my ($dx, $dy, $dz) = %p<x> - %e<x>, %p<y> - %e<y>, %p<z> - %e<z>;
    my $delta = sqrt($dx ** 2 + $dy ** 2 + $dz ** 2);
    my $sun-lon = norm360(%e<lon> + 180);
    my $lon = norm360(atan2($dy, $dx) / DEG);
    %( delta => $delta, lon => $lon,
       elongation => abs(norm180($lon - $sun-lon)),
       r => %p<r>, light => $delta * AU / 299_792.458 / 60 );   # minutes
}

#| The synodic period: how long between one alignment and the next.
#| 1/S = |1/T1 - 1/T2| -- two hands of a clock running at different rates.
sub synodic($t1, $t2) is export { abs(1 / (1 / $t1 - 1 / $t2)) }

#| The dates of opposition (or conjunction, for the inner planets) between
#| two Julian Days: the moments when the elongation is stationary at 180
#| (or 0). Found by scanning for a sign change in the rate.
sub oppositions(Str $planet, $jd-from, $jd-to) is export {
    my $inner = $planet eq 'Mercury' | 'Venus';
    my sub metric($jd) {
        my $x = geocentric($planet, $jd)<elongation>;
        $inner ?? $x !! 180 - $x;                # zero at the event
    }
    my @out;
    my $step = 2;
    my $prev = metric($jd-from);
    my $prev-jd = $jd-from;
    loop (my $jd = $jd-from + $step; $jd <= $jd-to; $jd += $step) {
        my $cur = metric($jd);
        # a minimum of the metric is the event; look for the turning point
        my $next = metric($jd + $step);
        if $cur < $prev && $cur < $next && $cur < 10 {
            # refine by golden-section on the three points
            my ($lo, $hi) = $jd - $step, $jd + $step;
            for ^40 {
                my $mid = ($lo + $hi) / 2;
                metric($mid - 0.01) < metric($mid + 0.01) ?? ($hi = $mid) !! ($lo = $mid);
            }
            my $at = ($lo + $hi) / 2;
            my %g = geocentric($planet, $at);
            @out.push: %( jd => $at, delta => %g<delta>,
                          elongation => %g<elongation>, light => %g<light> );
        }
        $prev = $cur;
    }
    @out;
}

# ------------------------------------------------------------- getting there ---

#| A Hohmann transfer: the cheapest two-burn move between circular orbits.
#| Leave on an ellipse whose perihelion is where you are and whose aphelion
#| is where you are going, then circularise on arrival. Radii in km.
sub hohmann($r1, $r2, $mu = GM-SUN) is export {
    my $at = ($r1 + $r2) / 2;
    my $v1 = sqrt($mu / $r1);                 # the circular speed you start with
    my $v2 = sqrt($mu / $r2);                 # and the one you need at the end
    # (named circ1/circ2 in the result: a bare `v1` before a fat arrow is a
    #  version literal in Raku, and reads badly even where it parses)
    my $vp = vis-viva($r1, $at, $mu);         # on the transfer ellipse, at r1
    my $va = vis-viva($r2, $at, $mu);         # and at r2
    my $tof = pi * sqrt($at ** 3 / $mu) / DAY;
    %( dv1 => abs($vp - $v1), dv2 => abs($v2 - $va), dv => abs($vp - $v1) + abs($v2 - $va),
       total => abs($vp - $v1) + abs($v2 - $va),
       tof => $tof, a => $at, circ1 => $v1, circ2 => $v2, vp => $vp, va => $va,
       # where the target must be when you leave, so it arrives when you do
       phase => norm180(180 - 360 * $tof / period($r2 / AU)) );
}

#| The transfer between two planets, taken as circular coplanar orbits at
#| their mean distances -- the textbook approximation, and the reason a real
#| mission planner needs Lambert's problem instead.
sub transfer(Str $from, Str $to) is export {
    my $r1 = %ELEMENTS{$from}[0][0] * AU;
    my $r2 = %ELEMENTS{$to}[0][0] * AU;
    my %h = hohmann($r1, $r2);
    %h<synodic> = synodic(period($r1 / AU), period($r2 / AU));
    %h<from> = $from;
    %h<to> = $to;
    %h;
}

#| Escape speed, and the speed of a circular orbit, at a distance from a body.
sub escape($r-km, $mu = GM-EARTH) is export { sqrt(2 * $mu / $r-km) }
sub circular($r-km, $mu = GM-EARTH) is export { sqrt($mu / $r-km) }

# --------------------------------------------------------------- calendars ---

sub julian-day(Int $y, Int $m, $d) is export {
    my ($yy, $mm) = $m > 2 ?? ($y, $m) !! ($y - 1, $m + 12);
    my $a = floor($yy / 100);
    my $b = 2 - $a + floor($a / 4);
    floor(365.25 * ($yy + 4716)) + floor(30.6001 * ($mm + 1)) + $d + $b - 1524.5;
}

sub calendar-date($jd) is export {
    my $z = floor($jd + 0.5);
    my $f = $jd + 0.5 - $z;
    my $alpha = floor(($z - 1867216.25) / 36524.25);
    my $a = $z + 1 + $alpha - floor($alpha / 4);
    my $b = $a + 1524;
    my $c = floor(($b - 122.1) / 365.25);
    my $d = floor(365.25 * $c);
    my $e = floor(($b - $d) / 30.6001);
    my $day = $b - $d - floor(30.6001 * $e) + $f;
    my $month = $e < 14 ?? $e - 1 !! $e - 13;
    my $year = $month > 2 ?? $c - 4716 !! $c - 4715;
    ($year.Int, $month.Int, $day);
}

sub jd-date($jd) is export {
    my ($y, $m, $d) = calendar-date($jd);
    sprintf('%04d-%02d-%02d', $y, $m, $d.floor);
}

sub today-jd() is export {
    my $n = DateTime.now(:timezone(0));
    julian-day($n.year, $n.month, $n.day + $n.hour / 24);
}
