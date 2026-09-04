# The browser API: the same engine, reached from JavaScript.

use JS;

sub pos-view(Str $p, $jd) {
    my %v = position($p, $jd);
    %( name => $p, x => %v<x>.Num, y => %v<y>.Num, z => %v<z>.Num,
       r => %v<r>.Num, lon => %v<lon>.Num, lat => %v<lat>.Num,
       nu => %v<nu>.Num, a => %v<a>.Num, e => %v<e>.Num,
       steps => %v<steps>, period => period(%v<a>).Num );
}

my %api =

    planets => -> { PLANETS.Array },

    # every planet at one instant, plus its orbit for drawing
    system => -> $jd {
        PLANETS.map(-> $p {
            my %el = elements($p, $jd.Num);
            my %v = pos-view($p, $jd.Num);
            %v<path> = orbit-path(%el<a>, %el<e>, %el<i>, %el<w>, %el<node>, :points(160))
                       .map({ %( x => .<x>.Num, y => .<y>.Num ) }).Array;
            %v<i> = %el<i>.Num;
            %v;
        }).Array;
    },

    # one planet's six elements and where they put it
    planet => -> $name, $jd {
        my %el = elements($name, $jd.Num);
        my %v = pos-view($name, $jd.Num);
        %( elements => %( a => %el<a>.Num, e => %el<e>.Num, i => %el<i>.Num,
                          node => %el<node>.Num, w => %el<w>.Num, M => %el<M>.Num ),
           position => %v );
    },

    # an ellipse from six numbers the reader chose: chapter 1's sliders
    orbit => -> $a, $e, $i, $w, $node, $M {
        my @p = orbit-path($a.Num, $e.Num, $i.Num, $w.Num, $node.Num, :points(240));
        my %k = kepler($M.Num * DEG, $e.Num);
        my $E = %k<E>;
        my $xp = $a.Num * (cos($E) - $e.Num);
        my $yp = $a.Num * sqrt(1 - $e.Num ** 2) * sin($E);
        my ($x, $y, $z) = to-ecliptic($xp, $yp, $i.Num, $w.Num, $node.Num);
        my $r = sqrt($x ** 2 + $y ** 2 + $z ** 2);
        my $a-km = $a.Num * AU;
        %( path => @p.map({ %( x => .<x>.Num, y => .<y>.Num, z => .<z>.Num ) }).Array,
           x => $x.Num, y => $y.Num, z => $z.Num, r => $r.Num,
           nu => (true-anomaly($E, $e.Num) / DEG).Num,
           E => ($E / DEG).Num, steps => %k<steps>,
           period => period($a.Num).Num,
           speed => vis-viva($r * AU, $a-km).Num,
           peri => ($a.Num * (1 - $e.Num)).Num, apo => ($a.Num * (1 + $e.Num)).Num,
           vperi => vis-viva($a-km * (1 - $e.Num), $a-km).Num,
           vapo => vis-viva($a-km * (1 + $e.Num), $a-km).Num );
    },

    # Kepler's equation, with the whole iteration trail: chapter 2
    kepler => -> $M, $e {
        my $m = $M.Num * DEG;
        my %k = kepler($m, $e.Num);
        %( E => (%k<E> / DEG).Num, steps => %k<steps>, residual => %k<residual>.Num,
           nu => (true-anomaly(%k<E>, $e.Num) / DEG).Num,
           trail => %k<trail>.map(-> $E {
               %( E => ($E / DEG).Num, error => abs($E - $e.Num * sin($E) - $m).Num )
           }).Array );
    },

    # the three anomalies across a whole orbit, for the curve that shows how
    # far uniform motion is from the real thing
    anomalies => -> $e, $points {
        my $p = $points.Int;
        (0..$p).map(-> $k {
            my $M = 360 * $k / $p;
            my %kk = kepler($M * DEG, $e.Num);
            %( M => $M.Num, E => (%kk<E> / DEG).Num,
               nu => norm360(true-anomaly(%kk<E>, $e.Num) / DEG).Num,
               steps => %kk<steps> );
        }).Array;
    },

    # a Hohmann transfer between two radii, in au
    hohmann => -> $r1, $r2 {
        my %h = hohmann($r1.Num * AU, $r2.Num * AU);
        %( dv1 => %h<dv1>.Num, dv2 => %h<dv2>.Num, total => %h<total>.Num,
           tof => %h<tof>.Num, phase => %h<phase>.Num,
           circ1 => %h<circ1>.Num, circ2 => %h<circ2>.Num,
           vp => %h<vp>.Num, va => %h<va>.Num,
           a => (%h<a> / AU).Num,
           synodic => synodic(period($r1.Num), period($r2.Num)).Num,
           path => orbit-path((($r1.Num + $r2.Num) / 2), abs($r2.Num - $r1.Num) / ($r1.Num + $r2.Num), 0, 0, 0, :points(240))
                   .map({ %( x => .<x>.Num, y => .<y>.Num ) }).Array );
    },

    # oppositions or conjunctions across a span of years
    events => -> $name, $from, $to {
        oppositions($name, julian-day($from.Int, 1, 1), julian-day($to.Int + 1, 1, 1))
            .map({ %( jd => .<jd>.Num, date => jd-date(.<jd>), delta => .<delta>.Num,
                      light => .<light>.Num, elongation => .<elongation>.Num ) }).Array;
    },

    # the distance to a planet, sampled -- the curve the events sit on
    distance => -> $name, $from, $to, $points {
        my $a = julian-day($from.Int, 1, 1);
        my $b = julian-day($to.Int + 1, 1, 1);
        my $p = $points.Int;
        (0..$p).map(-> $k {
            my $jd = $a + ($b - $a) * $k / $p;
            %( jd => $jd.Num, delta => geocentric($name, $jd)<delta>.Num );
        }).Array;
    },

    jd => -> $y, $m, $d { julian-day($y.Int, $m.Int, $d.Num).Num },
    date => -> $jd { jd-date($jd.Num) },
    today => -> { today-jd().Num },
    ;

JS<orbits> = %api;
