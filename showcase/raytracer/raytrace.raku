#!/usr/bin/env raku
# A path-tracing renderer, in the "Ray Tracing in One Weekend" tradition.
#
# It writes a PPM image to stdout: a floor plus a handful of spheres in three
# materials — matte (Lambertian), metal (reflective, optionally fuzzed), and
# glass (dielectric with refraction). Every pixel is an average of many rays
# that bounce recursively through the scene, so this is almost pure floating-
# point arithmetic in a tight loop — the case where `rakupp --exe` and
# `RAKUPP_PARALLEL=1` earn their keep.
#
#   build/rakupp --exe -o raytrace showcase/raytracer/raytrace.raku
#   ./raytrace > out.ppm            # then: convert out.ppm out.png
#
# Knobs (env vars, so the same binary serves quick previews and hero shots):
#   RT_WIDTH=400 RT_SAMPLES=50 RT_DEPTH=12 ./raytrace > out.ppm

# ---------- a 3-vector, used for points, directions and colours ----------
class V3 {
    has num $.x;
    has num $.y;
    has num $.z;

    method add(V3 $o)  { V3.new(x => $!x + $o.x, y => $!y + $o.y, z => $!z + $o.z) }
    method sub(V3 $o)  { V3.new(x => $!x - $o.x, y => $!y - $o.y, z => $!z - $o.z) }
    method mul(V3 $o)  { V3.new(x => $!x * $o.x, y => $!y * $o.y, z => $!z * $o.z) }
    method scale(num $s) { V3.new(x => $!x * $s, y => $!y * $s, z => $!z * $s) }
    method dot(V3 $o)  { $!x * $o.x + $!y * $o.y + $!z * $o.z }
    method len2        { $!x * $!x + $!y * $!y + $!z * $!z }
    method len         { sqrt(self.len2) }
    method unit        { self.scale(1e0 / self.len) }
    method cross(V3 $o) {
        V3.new(
            x => $!y * $o.z - $!z * $o.y,
            y => $!z * $o.x - $!x * $o.z,
            z => $!x * $o.y - $!y * $o.x,
        )
    }
}

sub v(num $x, num $y, num $z) { V3.new(:$x, :$y, :$z) }

# ---------- RNG helpers -------------------------------------------------
sub rnd() returns num { rand.Num }

sub random-in-sphere() returns V3 {
    loop {
        my $p = v(rnd() * 2e0 - 1e0, rnd() * 2e0 - 1e0, rnd() * 2e0 - 1e0);
        return $p if $p.len2 < 1e0;
    }
}

# reflect d about normal n
sub reflect(V3 $d, V3 $n) returns V3 {
    $d.sub($n.scale(2e0 * $d.dot($n)))
}

# ---------- materials ---------------------------------------------------
# kind: 0 = matte, 1 = metal, 2 = glass
class Material {
    has int $.kind;
    has V3  $.albedo;
    has num $.fuzz;
    has num $.ir;      # index of refraction (glass)
}

sub matte(V3 $a)            { Material.new(kind => 0, albedo => $a, fuzz => 0e0, ir => 1e0) }
sub metal(V3 $a, num $f)    { Material.new(kind => 1, albedo => $a, fuzz => $f, ir => 1e0) }
sub glass(num $ir)         { Material.new(kind => 2, albedo => v(1e0,1e0,1e0), fuzz => 0e0, ir => $ir) }

class Sphere {
    has V3  $.center;
    has num $.radius;
    has Material $.mat;
}

# ---------- ray/scene ---------------------------------------------------
# Returns (t, sphere) of the nearest hit in [tmin, tmax], or (Inf, Nil).
sub hit-scene(@world, V3 $orig, V3 $dir, num $tmin, num $tmax) {
    my num $closest = $tmax;
    my $best = Nil;
    for @world -> $s {
        my $oc = $orig.sub($s.center);
        my num $a = $dir.len2;
        my num $half-b = $oc.dot($dir);
        my num $c = $oc.len2 - $s.radius * $s.radius;
        my num $disc = $half-b * $half-b - $a * $c;
        next if $disc < 0e0;
        my num $sq = sqrt($disc);
        my num $t = (-$half-b - $sq) / $a;
        if $t < $tmin || $t > $closest {
            $t = (-$half-b + $sq) / $a;
            next if $t < $tmin || $t > $closest;
        }
        $closest = $t;
        $best = $s;
    }
    ($closest, $best)
}

sub refract(V3 $uv, V3 $n, num $etai-over-etat) returns V3 {
    my num $cos-theta = min(-$uv.dot($n), 1e0);
    my $r-out-perp = $uv.add($n.scale($cos-theta)).scale($etai-over-etat);
    my $r-out-par  = $n.scale(-sqrt(abs(1e0 - $r-out-perp.len2)));
    $r-out-perp.add($r-out-par)
}

sub reflectance(num $cos, num $ref-idx) returns num {
    my num $r0 = (1e0 - $ref-idx) / (1e0 + $ref-idx);
    $r0 = $r0 * $r0;
    $r0 + (1e0 - $r0) * (1e0 - $cos) ** 5
}

# Colour seen along a ray, recursing on each bounce.
sub ray-colour(@world, V3 $orig, V3 $dir, int $depth) returns V3 {
    return v(0e0, 0e0, 0e0) if $depth <= 0;

    my ($t, $s) = hit-scene(@world, $orig, $dir, 0.001e0, 1e30);
    if $s.defined {
        my $point  = $orig.add($dir.scale($t));
        my $normal = $point.sub($s.center).scale(1e0 / $s.radius);
        my $front  = $dir.dot($normal) < 0e0;
        $normal    = $front ?? $normal !! $normal.scale(-1e0);
        my $mat    = $s.mat;

        if $mat.kind == 0 {                       # matte
            my $target = $normal.add(random-in-sphere().unit);
            $target = $normal if $target.len2 < 1e-8;
            my $sc = ray-colour(@world, $point, $target, $depth - 1);
            return $mat.albedo.mul($sc);
        }
        elsif $mat.kind == 1 {                    # metal
            my $refl = reflect($dir.unit, $normal).add(random-in-sphere().scale($mat.fuzz));
            return v(0e0,0e0,0e0) if $refl.dot($normal) <= 0e0;
            my $sc = ray-colour(@world, $point, $refl, $depth - 1);
            return $mat.albedo.mul($sc);
        }
        else {                                     # glass
            my num $ratio = $front ?? (1e0 / $mat.ir) !! $mat.ir;
            my $ud = $dir.unit;
            my num $cos-theta = min(-$ud.dot($normal), 1e0);
            my num $sin-theta = sqrt(1e0 - $cos-theta * $cos-theta);
            my $out;
            if $ratio * $sin-theta > 1e0 || reflectance($cos-theta, $ratio) > rnd() {
                $out = reflect($ud, $normal);
            }
            else {
                $out = refract($ud, $normal, $ratio);
            }
            return ray-colour(@world, $point, $out, $depth - 1);
        }
    }

    # background: a vertical blue-to-white gradient
    my num $ty = 0.5e0 * ($dir.unit.y + 1e0);
    v(1e0,1e0,1e0).scale(1e0 - $ty).add(v(0.5e0, 0.7e0, 1e0).scale($ty))
}

# ---------- scene -------------------------------------------------------
sub build-world() {
    my @world;
    @world.push: Sphere.new(center => v(0e0, -1000e0, 0e0), radius => 1000e0,
                            mat => matte(v(0.5e0, 0.5e0, 0.5e0)));           # ground
    @world.push: Sphere.new(center => v(0e0, 1e0, 0e0), radius => 1e0,
                            mat => glass(1.5e0));                             # centre glass
    @world.push: Sphere.new(center => v(-4e0, 1e0, 0e0), radius => 1e0,
                            mat => matte(v(0.4e0, 0.2e0, 0.1e0)));            # left matte
    @world.push: Sphere.new(center => v(4e0, 1e0, 0e0), radius => 1e0,
                            mat => metal(v(0.7e0, 0.6e0, 0.5e0), 0e0));       # right mirror

    # a ring of small coloured spheres
    for 0 .. 11 -> $i {
        my num $ang = $i.Num * 0.5235987755982988e0;          # 2π/12
        my num $rad = 4.5e0;
        my $c = v($rad * cos($ang), 0.2e0, $rad * sin($ang));
        my $pick = $i % 3;
        my $m = $pick == 0 ?? matte(v(rnd()*rnd(), rnd()*rnd(), rnd()*rnd()))
              !! $pick == 1 ?? metal(v(0.5e0+0.5e0*rnd(), 0.5e0+0.5e0*rnd(), 0.5e0+0.5e0*rnd()), 0.3e0*rnd())
              !!               glass(1.5e0);
        @world.push: Sphere.new(center => $c, radius => 0.2e0, mat => $m);
    }
    @world
}

# ---------- camera + render loop ---------------------------------------
sub MAIN() {
    my int $width   = (%*ENV<RT_WIDTH>   // 300).Int;
    my int $samples = (%*ENV<RT_SAMPLES> //  40).Int;
    my int $depth   = (%*ENV<RT_DEPTH>   //  12).Int;
    my num $aspect  = 3e0 / 2e0;
    my int $height  = ($width.Num / $aspect).Int;

    srand(2026);                                   # deterministic scene + sampling
    my @world = build-world();

    # camera
    my $lookfrom = v(13e0, 2e0, 3e0);
    my $lookat   = v(0e0, 0e0, 0e0);
    my $vup      = v(0e0, 1e0, 0e0);
    my num $vfov = 20e0;
    my num $theta = $vfov * 3.141592653589793e0 / 180e0;
    my num $h = tan($theta / 2e0);
    my num $vh = 2e0 * $h;
    my num $vw = $aspect * $vh;

    my $w = $lookfrom.sub($lookat).unit;
    my $u = $vup.cross($w).unit;
    my $vv = $w.cross($u);

    my $origin = $lookfrom;
    my $horiz  = $u.scale($vw);
    my $vert   = $vv.scale($vh);
    my $lower  = $origin.sub($horiz.scale(0.5e0)).sub($vert.scale(0.5e0)).sub($w);

    note "rendering {$width}x{$height}, $samples spp, depth $depth ...";

    say "P3";
    say "$width $height";
    say "255";

    my num $scale = 1e0 / $samples.Num;
    for ($height - 1) ... 0 -> $j {
        note "  scanlines remaining: {$j + 1}   " if $j % 20 == 0;
        for 0 ..^ $width -> $i {
            my num $cr = 0e0;
            my num $cg = 0e0;
            my num $cb = 0e0;
            for 0 ..^ $samples {
                my num $s = ($i.Num + rnd()) / ($width - 1).Num;
                my num $t = ($j.Num + rnd()) / ($height - 1).Num;
                my $dir = $lower.add($horiz.scale($s)).add($vert.scale($t)).sub($origin);
                my $col = ray-colour(@world, $origin, $dir, $depth);
                $cr = $cr + $col.x;
                $cg = $cg + $col.y;
                $cb = $cb + $col.z;
            }
            # average, gamma-2 correct, map to 0..255
            my int $ir = (256e0 * clamp(sqrt($cr * $scale), 0e0, 0.999e0)).Int;
            my int $ig = (256e0 * clamp(sqrt($cg * $scale), 0e0, 0.999e0)).Int;
            my int $ib = (256e0 * clamp(sqrt($cb * $scale), 0e0, 0.999e0)).Int;
            say "$ir $ig $ib";
        }
    }
    note "done.";
}

sub clamp(num $x, num $lo, num $hi) returns num {
    $x < $lo ?? $lo !! $x > $hi ?? $hi !! $x
}
