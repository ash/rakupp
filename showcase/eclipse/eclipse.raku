#!/usr/bin/env rakupp
# eclipse — predict solar and lunar eclipses from first principles.
#
#   rakupp showcase/eclipse/eclipse.raku                 the next dozen eclipses
#   rakupp showcase/eclipse/eclipse.raku 2026            one year
#   rakupp showcase/eclipse/eclipse.raku 2024 2030       a range
#   rakupp showcase/eclipse/eclipse.raku --solar 1900 1950
#   rakupp showcase/eclipse/eclipse.raku --saros=139     one 1,300-year family
#   rakupp showcase/eclipse/eclipse.raku --phases=2026-08
#   rakupp showcase/eclipse/eclipse.raku --seasons 2024 2027
#   rakupp showcase/eclipse/eclipse.raku --explain=2026-08-12
#   rakupp showcase/eclipse/eclipse.raku --check
#
# The arithmetic lives in lib/Eclipse.rakumod; this file is the terminal.

use lib $?FILE.IO.parent.add('lib').Str;
use Eclipse;

# Colour only when a terminal is watching: piped or redirected output stays
# plain, so a golden file holds text rather than escape codes.
constant RESET = "\e[0m";
my $colour = $*OUT.t && !%*ENV<NO_COLOR>;
sub dim($s)  { $colour ?? "\e[2m$s" ~ RESET !! $s }
sub bold($s) { $colour ?? "\e[1m$s" ~ RESET !! $s }

sub today-jd() {
    my $n = DateTime.now(:timezone(0));
    julian-day($n.year, $n.month, $n.day + $n.hour / 24 + $n.minute / 1440);
}

# The magnitude a reader wants to see: for a penumbral eclipse the Moon never
# reaches the umbra, so the umbral magnitude is negative and meaningless.
sub shown-magnitude($e) {
    $e.kind eq 'lunar' && $e.type eq 'penumbral' ?? $e.penumbral !! $e.magnitude;
}

sub note-for($e) {
    if $e.kind eq 'lunar' {
        my @n;
        @n.push: sprintf('totality %dm', $e.dtotal.round)  if $e.dtotal > 0;
        @n.push: sprintf('partial %dm', $e.dpartial.round) if $e.dpartial > 0 && $e.dtotal == 0;
        @n.push: sprintf('penumbral %dm', $e.dpenumbral.round) if $e.type eq 'penumbral';
        return @n.join(', ');
    }
    return 'central' if $e.central;
    '';
}

sub header() {
    say bold(sprintf('%-10s %-5s  %-5s  %-18s %7s %8s %6s  %s',
        'date', 'UT', 'kind', 'type', 'mag', 'gamma', 'saros', 'notes'));
    say dim('-' x 78);
}

sub row($e) {
    printf("%-10s %-5s  %-5s  %-18s %7.3f %+8.4f %6d  %s\n",
        $e.date, $e.stamp.substr(11), $e.kind, $e.type,
        shown-magnitude($e), $e.gamma, $e.saros, note-for($e));
}

# ---- the eclipse seasons strip -------------------------------------------
# sin F is how far the Moon is from a node at the moment of New Moon. The
# eclipse window is |sin F| < 0.36; the strip shows the window closing and
# reopening twice a year, a little earlier each year as the nodes regress.
sub seasons($from, $to, Bool :$lunar) {
    my $frac = $lunar ?? 0.5 !! 0;
    my $w = 31;                                   # half-width in columns
    say bold(sprintf('%-16s %7s  %s', ($lunar ?? 'full moon' !! 'new moon'), 'sin F',
        'node ' ~ ('-' x ($w - 5)) ~ '+' ~ ('-' x ($w - 5)) ~ ' node'));
    my $k = floor(lunation($from)) - 1;
    my $stop = lunation($to);
    while $k <= $stop {
        my $kk = $k + $frac;
        my $jd = td-to-ut(phase-jde($kk));
        my %a = moon-args($kk);
        my $s = sind(%a<f>);
        my $e = eclipse-at($kk);
        my @c = ' ' xx (2 * $w + 1);
        @c[$w - (0.36 * $w).round] = '[';
        @c[$w + (0.36 * $w).round] = ']';
        @c[$w] = '|';
        my $col = ($w + $s * $w).round.Int max 0 min 2 * $w;
        @c[$col] = $e ?? ($e.type.starts-with('total') ?? '#' !! '*') !! '.';
        my $line = sprintf('%-16s %+7.3f  %s', jd-date($jd), $s, @c.join);
        say $e ?? bold($line) !! dim($line);
        $k++;
    }
    say '';
    say dim(". new moon, no eclipse   * an eclipse   # a total one   [ ] the +/-0.36 window");
}

# ---- one eclipse, term by term -------------------------------------------
sub explain($date) {
    my ($y, $m, $d) = $date.split('-')>>.Int;
    my $jd0 = julian-day($y, $m, $d);
    my @found = eclipses($jd0 - 1.5, $jd0 + 2.5);
    unless @found {
        note "no eclipse within a day of $date";
        exit 1;
    }
    my $e = @found[0];
    my $k = $e.k;
    my %a = moon-args($k);
    my ($t, $E, $M, $Mp, $F, $Om) = %a<t e m mp f om>;

    say bold("$date — a {$e.type} {$e.kind} eclipse");
    say '';
    say bold('1. the lunation number');
    printf("   k        = %s          %s since the New Moon of 2000-01-06\n",
        $k, $k >= 0 ?? 'lunations' !! 'lunations before');
    printf("   T        = k/1236.85 = %+.6f Julian centuries from 2000.0\n", $t);
    printf("   E        = %.6f       the Earth's orbital eccentricity factor\n", $E);
    say '';
    say bold('2. the four angles (degrees)');
    printf("   M        = %10.4f   the Sun's mean anomaly\n", $M);
    printf("   M'       = %10.4f   the Moon's mean anomaly\n", $Mp);
    printf("   F        = %10.4f   the Moon's argument of latitude\n", $F);
    printf("   Omega    = %10.4f   the ascending node\n", $Om);
    printf("   sin F    = %+9.4f    %s (the window is +/-0.36)\n",
        sind($F), abs(sind($F)) < 0.36 ?? 'inside the window' !! 'no eclipse possible');
    say '';
    say bold('3. the instant');
    my $mean = mean-phase($k);
    printf("   mean phase                 JDE %.5f  = %s\n", $mean, jd-stamp($mean));
    my @terms =
        ('-0.4075 sin M\''        , ($e.kind eq 'lunar' ?? -0.4065 !! -0.4075) * sind($Mp)),
        (' 0.1721 E sin M'        , ($e.kind eq 'lunar' ??  0.1727 !!  0.1721) * $E * sind($M)),
        (' 0.0161 sin 2M\''       ,  0.0161 * sind(2 * $Mp)),
        ('-0.0097 sin 2F1'        , -0.0097 * sind(2 * ($F - 0.02665 * sind($Om)))),
        (' 0.0073 E sin(M\'-M)'   ,  0.0073 * $E * sind($Mp - $M)),
        ('-0.0050 E sin(M\'+M)'   , -0.0050 * $E * sind($Mp + $M));
    for @terms -> ($name, $v) {
        printf("     %-22s %+9.5f d  = %+7.1f minutes\n", $name, $v, $v * 1440);
    }
    printf("     %-22s %+9.5f d\n", '... 10 smaller terms',
        $e.jde - $mean - @terms.map(*[1]).sum);
    printf("   greatest eclipse           JDE %.5f  = %s TD\n", $e.jde, jd-stamp($e.jde));
    printf("   Delta T                    %.1f s\n", delta-t(jd-year($e.jde)));
    printf("   greatest eclipse           JD  %.5f  = %s UT\n", $e.jd, $e.stamp);
    say '';
    say bold('4. the geometry');
    printf("   gamma    = %+.4f    the shadow axis misses the Earth's centre by\n", $e.gamma);
    printf("                        %.0f km (Earth radii x 6378)\n", abs($e.gamma) * 6378);
    printf("   u        = %+.4f    the umbral cone's radius on the fundamental plane\n", $e.u);
    say '';
    say bold('5. the verdict');
    if $e.kind eq 'solar' {
        printf("   |gamma| = %.4f %s 0.9972  -> %s\n", abs($e.gamma),
            abs($e.gamma) < 0.9972 ?? '<' !! '>',
            $e.central ?? 'the axis hits the Earth: central' !! 'the axis misses: not central');
        printf("   u %s 0  -> %s\n", $e.u < 0 ?? '<' !! '>',
            $e.u < 0 ?? 'the cone still has length: TOTAL' !! 'the cone fell short: ANNULAR')
            if $e.central;
        printf("   magnitude %.4f\n", $e.magnitude) unless $e.central;
    }
    else {
        printf("   umbral magnitude    = (1.0128 - u - |gamma|) / 0.5450 = %.4f\n", $e.magnitude);
        printf("   penumbral magnitude = (1.5573 + u - |gamma|) / 0.5450 = %.4f\n", $e.penumbral);
        printf("   -> %s\n", $e.type.uc);
        printf("   penumbral phase %.0f min, partial %.0f min, totality %.0f min\n",
            $e.dpenumbral, $e.dpartial, $e.dtotal);
    }
    say '';
    printf("   saros %d: k = %d, and 38 x (k - anchor) mod 223 picks the family\n",
        $e.saros, $k);
}

# ---- the catalogue check --------------------------------------------------
sub check() {
    my $file = $?FILE.IO.parent.add('reference/catalogue.tsv');
    my ($ok, $bad, $marginal) = 0, 0, 0;
    for $file.lines -> $l {
        next if $l.starts-with('#') || !$l.trim;
        my ($date, $kind, $type, $saros, $flag) = $l.split("\t");
        my ($y, $m, $d) = $date.split('-')>>.Int;
        my @e = eclipses(julian-day($y, $m, $d) - 1.5, julian-day($y, $m, $d) + 2.5, :$kind);
        unless @e {
            say "MISS  $date $kind — the engine predicts no eclipse";
            $bad++;
            next;
        }
        my $e = @e[0];
        my $got = $e.type.subst('non-central ', '');
        if $e.date eq $date && $got eq $type && $e.saros == $saros.Int {
            $ok++;
        }
        elsif $flag && $flag eq 'marginal' {
            printf("near  %s  catalogue says %s, the series says %s (magnitude %.4f)\n",
                $date, $type, $got, $e.magnitude);
            $marginal++;
        }
        else {
            printf("DIFF  %s want %-8s saros %3d   got %s %-8s saros %3d\n",
                $date, $type, $saros.Int, $e.date, $got, $e.saros);
            $bad++;
        }
    }
    say '';
    say "$ok of {$ok + $bad + $marginal} catalogued eclipses reproduced exactly (date, type and saros)";
    say "$marginal marginal case documented in reference/catalogue.tsv" if $marginal;
    exit $bad ?? 1 !! 0;
}

# ---- MAIN -----------------------------------------------------------------
sub MAIN(
    *@years,
    Bool :$solar,               #= solar eclipses only
    Bool :$lunar,               #= lunar eclipses only
    Int  :$saros,               #= list one saros series instead
    Str  :$phases,              #= the Moon's four phases in a month, YYYY-MM
    Str  :$explain,             #= one eclipse, term by term, YYYY-MM-DD
    Bool :$seasons,             #= a strip chart of sin F over the range
    Bool :$check,               #= reproduce the catalogued eclipses
) {
    my $kind = $solar ?? 'solar' !! $lunar ?? 'lunar' !! 'both';

    if $check { check() }

    if $explain { explain($explain); exit }

    if $phases {
        my ($y, $m) = $phases.split('-')>>.Int;
        my $from = julian-day($y, $m, 1);
        my $to = julian-day($m == 12 ?? $y + 1 !! $y, $m == 12 ?? 1 !! $m + 1, 1);
        say bold(sprintf('%-14s %-16s %s', 'phase', 'instant (UT)', 'lunation k'));
        say dim('-' x 46);
        for phases($from, $to) -> ($k, $name, $jd) {
            printf("%-14s %-16s %s\n", $name, jd-stamp($jd), $k);
        }
        exit;
    }

    if $saros {
        my @run = saros-run($saros, :lunar($kind eq 'lunar'));
        unless @run {
            note "no such saros series: $saros";
            exit 1;
        }
        say bold("Saros {$saros} — {@run.elems} {$kind eq 'lunar' ?? 'lunar' !! 'solar'} eclipses, "
            ~ "{@run[0].date} to {@run[*-1].date}");
        say dim("one every 6585.32 days; gamma drifts from one edge of the Earth to the other");
        say '';
        header();
        row($_) for @run;
        exit;
    }

    my ($from, $to);
    if @years == 0 {
        $from = today-jd();
        $to = $from + 5 * 365.25;
    }
    elsif @years == 1 {
        $from = julian-day(@years[0].Int, 1, 1);
        $to = julian-day(@years[0].Int + 1, 1, 1);
    }
    else {
        $from = julian-day(@years[0].Int, 1, 1);
        $to = julian-day(@years[1].Int + 1, 1, 1);
    }

    if $seasons {
        seasons(jd-year($from), jd-year($to), :lunar($kind eq 'lunar'));
        exit;
    }

    my @e = eclipses($from, $to, :$kind);
    @e = @e[^12] if @years == 0 && @e > 12;
    header();
    row($_) for @e;
    say '';
    say dim("{@e.elems} eclipses"
        ~ (@years == 0 ?? ' — the next twelve from today' !! ", {jd-date($from)} to {jd-date($to)}"));
}
