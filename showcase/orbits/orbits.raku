#!/usr/bin/env rakupp
# orbits — where the planets are, and what it costs to go there.
#
#   rakupp showcase/orbits/orbits.raku                     the solar system today
#   rakupp showcase/orbits/orbits.raku 2030-06-01
#   rakupp showcase/orbits/orbits.raku --map
#   rakupp showcase/orbits/orbits.raku --planet=Mars
#   rakupp showcase/orbits/orbits.raku --kepler=0.6
#   rakupp showcase/orbits/orbits.raku --events=Mars 2020 2035
#   rakupp showcase/orbits/orbits.raku --transfer=Earth,Mars
#   rakupp showcase/orbits/orbits.raku --check
#
# The arithmetic lives in lib/Orbit.rakumod; this file is the terminal.

use lib $?FILE.IO.parent.add('lib').Str;
use Orbit;

constant RESET = "\e[0m";
my $colour = $*OUT.t && !%*ENV<NO_COLOR>;
sub dim($s)  { $colour ?? "\e[2m$s" ~ RESET !! $s }
sub bold($s) { $colour ?? "\e[1m$s" ~ RESET !! $s }

sub parse-date(Str $s) {
    my ($y, $m, $d) = $s.split('-')>>.Int;
    julian-day($y, $m // 1, $d // 1);
}

# ---- the table ------------------------------------------------------------
sub table($jd) {
    say bold("The solar system on {jd-date($jd)}");
    say '';
    say bold(sprintf('%-8s %10s %9s %9s %9s %10s %8s  %s',
        'planet', 'longitude', 'latitude', 'r (au)', 'delta', 'elongation', 'light', 'period'));
    say dim('-' x 88);
    for PLANETS -> $p {
        my %pos = position($p, $jd);
        if $p eq 'Earth' {
            printf("%-8s %9.3f° %8.3f° %9.5f %9s %10s %8s  %8.2f d\n",
                $p, %pos<lon>, %pos<lat>, %pos<r>, '—', '—', '—', period(%pos<a>));
            next;
        }
        my %g = geocentric($p, $jd);
        printf("%-8s %9.3f° %8.3f° %9.5f %9.4f %9.2f° %6.1f m  %8.2f d\n",
            $p, %pos<lon>, %pos<lat>, %pos<r>, %g<delta>, %g<elongation>, %g<light>,
            period(%pos<a>));
    }
    say '';
    say dim('Heliocentric ecliptic longitude and latitude; delta is the distance from');
    say dim('the Earth, and light is how long the view of it is out of date.');
}

# ---- an orrery, in characters ---------------------------------------------
# One letter each, and no two the same: Mars gets a lowercase m so it does
# not collide with Mercury.
constant SYMBOL = %( Mercury => 'M', Venus => 'V', Earth => 'E', Mars => 'm',
                     Jupiter => 'J', Saturn => 'S', Uranus => 'U', Neptune => 'N' );

sub map-view($jd, Bool :$outer) {
    my @show = $outer ?? <Jupiter Saturn Uranus Neptune> !! <Mercury Venus Earth Mars>;
    my $scale = $outer ?? 31 !! 1.6;
    my ($w, $h) = 78, 31;
    my @grid = (^$h).map({ [' ' xx $w] });
    my sub put($x, $y, $ch) {
        my $cx = (($x / $scale) * ($w / 2 - 2) + $w / 2).round.Int;
        my $cy = ((-$y / $scale) * ($h / 2 - 1) + $h / 2).round.Int;
        @grid[$cy][$cx] = $ch if 0 <= $cy < $h && 0 <= $cx < $w;
    }
    # each orbit as a dotted ellipse, then the planet on it
    for @show.kv -> $i, $p {
        my %el = elements($p, $jd);
        for ^180 -> $k {
            my $E = 2 * pi * $k / 180;
            my $xp = %el<a> * (cos($E) - %el<e>);
            my $yp = %el<a> * sqrt(1 - %el<e> ** 2) * sin($E);
            my ($cw, $sw) = cosd(%el<w>), sind(%el<w>);
            my ($cn, $sn) = cosd(%el<node>), sind(%el<node>);
            put(($cw * $cn - $sw * $sn) * $xp + (-$sw * $cn - $cw * $sn) * $yp,
                ($cw * $sn + $sw * $cn) * $xp + (-$sw * $sn + $cw * $cn) * $yp, '.');
        }
    }
    put(0, 0, '@');
    for @show -> $p {
        my %pos = position($p, $jd);
        put(%pos<x>, %pos<y>, SYMBOL{$p});
    }
    say bold("{$outer ?? 'The outer' !! 'The inner'} solar system on {jd-date($jd)}"
             ~ "  ({$scale} au across the half-width)");
    say '';
    .join.trim-trailing.&{ say dim('  ') ~ $_ } for @grid;
    say '';
    say dim('  @ the Sun    ' ~ @show.map({ SYMBOL{$_} ~ ' ' ~ $_ }).join('    '));
    say dim('  Looking down on the ecliptic from the north; the ellipses are to scale.');
}

# ---- one planet -----------------------------------------------------------
sub planet-view(Str $p, $jd) {
    my %el = elements($p, $jd);
    my %pos = position($p, $jd);
    say bold("$p on {jd-date($jd)}");
    say '';
    say bold('the six elements');
    printf("  a     %12.7f au       semi-major axis: the size\n", %el<a>);
    printf("  e     %12.7f          eccentricity: the shape (0 is a circle)\n", %el<e>);
    printf("  i     %12.5f°         inclination to the ecliptic\n", %el<i>);
    printf("  node  %12.5f°         longitude of the ascending node\n", %el<node>);
    printf("  w     %12.5f°         argument of perihelion\n", %el<w>);
    printf("  M     %12.5f°         mean anomaly: where it would be if it moved evenly\n", %el<M>);
    say '';
    say bold("Kepler's equation, M = E - e sin E");
    my %k = kepler(%el<M> * DEG, %el<e>);
    my @t = %k<trail>;
    for @t.kv -> $i, $E {
        my $err = abs($E - %el<e> * sin($E) - %el<M> * DEG);
        printf("  step %d   E = %.12f   |error| = %.2e\n", $i, $E, $err);
        last if $i >= 6;
    }
    printf("  converged in %d steps\n", %k<steps>);
    say '';
    say bold('where that puts it');
    printf("  true anomaly     %10.5f°    the angle actually seen from the Sun\n", %pos<nu>);
    printf("  radius vector    %10.6f au   %.0f million km\n", %pos<r>, %pos<r> * 149.5978707);
    printf("  longitude        %10.5f°\n", %pos<lon>);
    printf("  latitude         %10.5f°\n", %pos<lat>);
    printf("  period           %10.3f d    %.4f years\n", period(%el<a>), period(%el<a>) / 365.25);
    my $r-km = %pos<r> * 149_597_870.7;
    my $a-km = %el<a> * 149_597_870.7;
    printf("  orbital speed    %10.4f km/s  (vis-viva at this distance)\n", vis-viva($r-km, $a-km));
    printf("  at perihelion    %10.4f km/s\n", vis-viva($a-km * (1 - %el<e>), $a-km));
    printf("  at aphelion      %10.4f km/s\n", vis-viva($a-km * (1 + %el<e>), $a-km));
    unless $p eq 'Earth' {
        my %g = geocentric($p, $jd);
        say '';
        printf("  from the Earth   %10.6f au   %.1f light-minutes, elongation %.2f°\n",
            %g<delta>, %g<light>, %g<elongation>);
    }
}

# ---- Kepler's equation, watched ------------------------------------------
sub kepler-view($e) {
    say bold("Newton-Raphson on M = E - e sin E, for e = $e");
    say '';
    say dim('Kepler could not solve his own equation in closed form, and neither can');
    say dim('anyone else -- E is not an elementary function of M. So we guess and');
    say dim('correct: each step roughly doubles the number of correct digits.');
    say '';
    say bold(sprintf('%6s  %10s  %s', 'M', 'steps', 'convergence, |error| per step'));
    say dim('-' x 74);
    for 0, 30, 60, 90, 120, 150, 180 -> $deg {
        my %k = kepler($deg * DEG, $e);
        my @errs = %k<trail>.map({ abs($_ - $e * sin($_) - $deg * DEG) });
        printf("%5d°  %10d  %s\n", $deg, %k<steps>,
            @errs.head(6).map({ $_ < 1e-16 ?? '     0' !! sprintf('%6.0e', $_) }).join(' '));
    }
    say '';
    say dim("Read a row left to right: 1e-01, 1e-03, 1e-07, 1e-15, 0. That doubling");
    say dim("is what quadratic convergence looks like from the outside.");
}

# ---- events ---------------------------------------------------------------
sub events(Str $p, $from, $to) {
    my $inner = $p eq 'Mercury' | 'Venus';
    say bold("$p: {$inner ?? 'conjunctions' !! 'oppositions'}, {jd-date($from)} to {jd-date($to)}");
    say '';
    say bold(sprintf('%-12s %10s %12s %10s  %s', 'date', 'distance', 'light', 'elongation', ''));
    say dim('-' x 62);
    for oppositions($p, $from, $to) -> %o {
        my $kind = $inner ?? (%o<delta> < 1 ?? 'inferior' !! 'superior') !! '';
        printf("%-12s %9.4f au %9.1f min %9.2f°  %s\n",
            jd-date(%o<jd>), %o<delta>, %o<light>, %o<elongation>, $kind);
    }
    say '';
    my %el = elements($p, 2451545.0);
    printf("%s\n", dim(sprintf('One every %.1f days -- the synodic period, 1/S = |1/T_earth - 1/T_%s|.',
        synodic(365.256898, period(%el<a>)), $p.lc)));
}

# ---- transfers ------------------------------------------------------------
sub transfer-view(Str $spec) {
    my ($from, $to) = $spec.split(/','/).map(*.trim.tc);
    my %h = transfer($from, $to);
    say bold("Hohmann transfer, $from to $to");
    say '';
    say dim('The cheapest two-burn move between two circular orbits: leave on an');
    say dim('ellipse that just touches both, then circularise on arrival.');
    say '';
    printf("  departure orbit speed   %8.3f km/s\n", %h<circ1>);
    printf("  on the transfer ellipse %8.3f km/s        first burn  %7.3f km/s\n", %h<vp>, %h<dv1>);
    say '';
    printf("  arriving at             %8.3f km/s\n", %h<va>);
    printf("  target orbit speed      %8.3f km/s        second burn %7.3f km/s\n", %h<circ2>, %h<dv2>);
    say '';
    printf("  %s %.3f km/s\n", bold('total delta-v'), %h<total>);
    printf("  time of flight          %8.1f days      %.2f years\n", %h<tof>, %h<tof> / 365.25);
    printf("  launch window every     %8.1f days      the synodic period\n", %h<synodic>);
    printf("  target must lead by     %+8.1f°         at the moment you leave\n", %h<phase>);
    say '';
    say dim('Both orbits are taken as circular and coplanar. A real mission solves');
    say dim("Lambert's problem for the actual departure and arrival dates instead,");
    say dim('which is why porkchop plots exist -- see the README.');
}

# ---- the checks -----------------------------------------------------------
sub check() {
    my $file = $?FILE.IO.parent.add('reference/known.tsv');
    my ($ok, $bad) = 0, 0;
    my sub judge($got, $want, $tol, $desc) {
        if abs($got - $want) <= $tol { $ok++ }
        else {
            $bad++;
            printf("FAIL %s: got %.6f, want %.6f (tolerance %g)\n", $desc, $got, $want, $tol);
        }
    }
    my %h = transfer('Earth', 'Mars');
    for $file.lines -> $l {
        next if $l.starts-with('#') || !$l.trim;
        my ($kind, $subject, $value, $tol, $note) = $l.split("\t");
        given $kind {
            when 'period' {
                my %el = elements($subject, 2451545.0);
                judge(period(%el<a>), $value.Num, $tol.Num, "$subject period");
            }
            when 'synodic' {
                my %el = elements($subject, 2451545.0);
                judge(synodic(365.256898, period(%el<a>)), $value.Num, $tol.Num,
                      "Earth-$subject synodic period");
            }
            when 'opposition' {
                my ($y, $m, $d) = $value.split('-')>>.Int;
                my $want = julian-day($y, $m, $d);
                my @e = oppositions($subject, $want - 40, $want + 40);
                unless @e {
                    say "FAIL $subject $value: no event found within 40 days";
                    $bad++;
                    next;
                }
                my $got = @e.min({ abs(.<jd> - $want) })<jd>;
                judge($got, $want, $tol.Num, "$subject event $value");
            }
            when 'hohmann' {
                judge(%h{$subject}, $value.Num, $tol.Num, "Earth-Mars $subject");
            }
            when 'speed' {
                my $got = $subject eq 'escape' ?? escape(6378.137) !! circular(6578.137);
                judge($got, $value.Num, $tol.Num, "$subject speed");
            }
        }
    }
    # Kepler's equation has to actually solve it, at every eccentricity
    for 0.0, 0.1, 0.3, 0.5, 0.7, 0.9, 0.95 -> $e {
        my $worst = 0;
        for 0, 15 ... 345 -> $deg {
            my %k = kepler($deg * DEG, $e);
            $worst = %k<residual> if %k<residual> > $worst;
        }
        if $worst < 1e-11 { $ok++ }
        else {
            $bad++;
            printf("FAIL Kepler at e=%.2f: worst residual %.2e\n", $e, $worst);
        }
    }
    # and the position it produces has to be self-consistent: r from the
    # elements must match r from the coordinates
    for PLANETS -> $p {
        my %pos = position($p, 2460000.5);
        my $r2 = sqrt(%pos<x> ** 2 + %pos<y> ** 2 + %pos<z> ** 2);
        if abs(%pos<r> - $r2) < 1e-12 { $ok++ }
        else { $bad++; say "FAIL $p: radius disagrees with its own coordinates" }
    }
    say '';
    say $bad ?? "$bad of {$ok + $bad} checks FAILED" !! "all $ok checks passed";
    exit $bad ?? 1 !! 0;
}

# ---- MAIN -----------------------------------------------------------------
sub MAIN(
    *@when,
    Bool :$map,                 #= an orrery drawn in characters
    Bool :$outer,               #= with --map: the outer planets instead
    Str  :$planet,              #= one planet: its elements, Kepler, its position
    Real :$kepler,              #= watch Newton-Raphson converge at this eccentricity
    Str  :$events,              #= oppositions (or conjunctions) of a planet
    Str  :$transfer,            #= a Hohmann transfer, e.g. Earth,Mars
    Bool :$check,               #= reproduce the catalogued values
) {
    if $check { check() }

    my $jd = @when && @when[0] ~~ /^ \d\d\d\d '-' / ?? parse-date(@when[0]) !! today-jd();

    if $kepler.defined { kepler-view($kepler); exit }
    if $transfer { transfer-view($transfer); exit }
    if $events {
        my $from = @when > 1 ?? julian-day(@when[0].Int, 1, 1) !! today-jd();
        my $to = @when > 1 ?? julian-day(@when[1].Int + 1, 1, 1) !! $from + 3652;
        events($events.tc, $from, $to);
        exit;
    }
    if $planet {
        die "unknown planet: $planet" unless $planet.tc eq any PLANETS;
        planet-view($planet.tc, $jd);
        exit;
    }
    if $map { map-view($jd, :$outer); exit }
    table($jd);
}
