#!/usr/bin/env raku
# The same scene as raytrace.raku, rewritten for the transpiler's hot path:
# no per-operation object allocation. Vectors live as native `num` locals, the
# scene is a handful of flat `num` arrays, and the bounce loop is iterative
# (attenuation accumulated in place) rather than recursive. Compiled with
# `--exe` this renders the reference frame (300x200, 40 spp) in ~56 s, versus
# ~731 s for the object-oriented raytrace.raku — about 13x, all from the
# allocation-free rewrite. (The render is single-threaded; it does no `start`
# work, so RAKUPP_PARALLEL does not apply — a per-band `start` pool would be the
# way to add it.)
#
#   build/rakupp --exe -o raytrace-fast showcase/raytracer/raytrace-fast.raku
#   ./raytrace-fast > out.ppm        # then: convert out.ppm out.png
#
#   RT_WIDTH=600 RT_SAMPLES=100 RT_DEPTH=16 ./raytrace-fast > hero.ppm

# ---------- scene as parallel flat arrays -------------------------------
# material kind: 0 = matte, 1 = metal, 2 = glass
my num @cx; my num @cy; my num @cz; my num @rad;
my int @kind; my num @ar; my num @ag; my num @ab; my num @fuzz; my num @ir;

# Module-level scratch outputs. Native `is rw` params are not written back by
# the --exe codegen, so subs return their vectors through these instead: it is
# allocation-free and behaves identically under the interpreter and `--exe`.
my num $RSX = 0e0; my num $RSY = 0e0; my num $RSZ = 0e0;   # rand-sphere result
my num $RESR = 0e0; my num $RESG = 0e0; my num $RESB = 0e0; # ray-colour result

sub add-sphere(num $x, num $y, num $z, num $r,
               int $k, num $red, num $grn, num $blu, num $fz, num $index) {
    @cx.push($x); @cy.push($y); @cz.push($z); @rad.push($r);
    @kind.push($k); @ar.push($red); @ag.push($grn); @ab.push($blu);
    @fuzz.push($fz); @ir.push($index);
}

sub rnd(--> num) { rand.Num }

sub build-world() {
    add-sphere( 0e0, -1000e0, 0e0, 1000e0, 0, 0.5e0, 0.5e0, 0.5e0, 0e0, 1e0); # ground
    add-sphere( 0e0,     1e0, 0e0,    1e0, 2, 1e0,   1e0,   1e0,   0e0, 1.5e0); # glass
    add-sphere(-4e0,     1e0, 0e0,    1e0, 0, 0.4e0, 0.2e0, 0.1e0, 0e0, 1e0);  # matte
    add-sphere( 4e0,     1e0, 0e0,    1e0, 1, 0.7e0, 0.6e0, 0.5e0, 0e0, 1e0);  # mirror
    for 0 .. 11 -> int $i {
        my num $ang = $i.Num * 0.5235987755982988e0;
        my num $x = 4.5e0 * cos($ang);
        my num $z = 4.5e0 * sin($ang);
        my int $pick = $i % 3;
        if $pick == 0 {
            add-sphere($x, 0.2e0, $z, 0.2e0, 0,
                       rnd()*rnd(), rnd()*rnd(), rnd()*rnd(), 0e0, 1e0);
        }
        elsif $pick == 1 {
            add-sphere($x, 0.2e0, $z, 0.2e0, 1,
                       0.5e0+0.5e0*rnd(), 0.5e0+0.5e0*rnd(), 0.5e0+0.5e0*rnd(),
                       0.3e0*rnd(), 1e0);
        }
        else {
            add-sphere($x, 0.2e0, $z, 0.2e0, 2, 1e0, 1e0, 1e0, 0e0, 1.5e0);
        }
    }
}

# a random vector in the unit sphere, left in ($RSX, $RSY, $RSZ)
sub rand-sphere() {
    loop {
        $RSX = rnd() * 2e0 - 1e0;
        $RSY = rnd() * 2e0 - 1e0;
        $RSZ = rnd() * 2e0 - 1e0;
        last if $RSX*$RSX + $RSY*$RSY + $RSZ*$RSZ < 1e0;
    }
}

# ---------- iterative path tracer ---------------------------------------
# Trace one primary ray; leaves its colour in ($RESR, $RESG, $RESB).
sub ray-colour(num $ox0, num $oy0, num $oz0, num $dx0, num $dy0, num $dz0,
               int $max-depth) {
    my num $ox = $ox0; my num $oy = $oy0; my num $oz = $oz0;
    my num $dx = $dx0; my num $dy = $dy0; my num $dz = $dz0;
    my num $attr = 1e0; my num $attg = 1e0; my num $attb = 1e0;  # accumulated attenuation
    my int $n = @rad.elems;

    for ^$max-depth {
        # nearest hit
        my num $closest = 1e30;
        my int $hit = -1;
        my int $i = 0;
        while $i < $n {
            my num $ocx = $ox - @cx[$i];
            my num $ocy = $oy - @cy[$i];
            my num $ocz = $oz - @cz[$i];
            my num $a = $dx*$dx + $dy*$dy + $dz*$dz;
            my num $hb = $ocx*$dx + $ocy*$dy + $ocz*$dz;
            my num $c = $ocx*$ocx + $ocy*$ocy + $ocz*$ocz - @rad[$i]*@rad[$i];
            my num $disc = $hb*$hb - $a*$c;
            if $disc >= 0e0 {
                my num $sq = sqrt($disc);
                my num $t = (-$hb - $sq) / $a;
                if $t < 0.001e0 || $t > $closest {
                    $t = (-$hb + $sq) / $a;
                }
                if $t > 0.001e0 && $t < $closest {
                    $closest = $t;
                    $hit = $i;
                }
            }
            $i = $i + 1;
        }

        if $hit < 0 {
            # background gradient; fold into attenuation and finish
            my num $len = sqrt($dx*$dx + $dy*$dy + $dz*$dz);
            my num $uy = $dy / $len;
            my num $tt = 0.5e0 * ($uy + 1e0);
            my num $bgr = (1e0 - $tt) + $tt * 0.5e0;
            my num $bgg = (1e0 - $tt) + $tt * 0.7e0;
            my num $bgb = (1e0 - $tt) + $tt * 1e0;
            $RESR = $attr * $bgr;
            $RESG = $attg * $bgg;
            $RESB = $attb * $bgb;
            return;
        }

        # hit point and outward normal
        my num $px = $ox + $dx*$closest;
        my num $py = $oy + $dy*$closest;
        my num $pz = $oz + $dz*$closest;
        my num $inv = 1e0 / @rad[$hit];
        my num $nx = ($px - @cx[$hit]) * $inv;
        my num $ny = ($py - @cy[$hit]) * $inv;
        my num $nz = ($pz - @cz[$hit]) * $inv;
        my num $ndd = $dx*$nx + $dy*$ny + $dz*$nz;
        my int $front = $ndd < 0e0 ?? 1 !! 0;
        if $front == 0 { $nx = -$nx; $ny = -$ny; $nz = -$nz; }
        my int $k = @kind[$hit];

        if $k == 0 {                       # matte (Lambertian)
            rand-sphere();
            my num $tx = $nx + $RSX; my num $ty = $ny + $RSY; my num $tz = $nz + $RSZ;
            if $tx*$tx + $ty*$ty + $tz*$tz < 1e-8 { $tx = $nx; $ty = $ny; $tz = $nz; }
            $attr = $attr * @ar[$hit]; $attg = $attg * @ag[$hit]; $attb = $attb * @ab[$hit];
            $ox = $px; $oy = $py; $oz = $pz;
            $dx = $tx; $dy = $ty; $dz = $tz;
        }
        elsif $k == 1 {                    # metal
            my num $dl = sqrt($dx*$dx + $dy*$dy + $dz*$dz);
            my num $ux = $dx/$dl; my num $uy = $dy/$dl; my num $uz = $dz/$dl;
            my num $dot = $ux*$nx + $uy*$ny + $uz*$nz;
            my num $rx = $ux - 2e0*$dot*$nx;
            my num $ry = $uy - 2e0*$dot*$ny;
            my num $rz = $uz - 2e0*$dot*$nz;
            rand-sphere();
            my num $fz2 = @fuzz[$hit];
            $rx = $rx + $fz2*$RSX; $ry = $ry + $fz2*$RSY; $rz = $rz + $fz2*$RSZ;
            if $rx*$nx + $ry*$ny + $rz*$nz <= 0e0 {   # absorbed
                $RESR = 0e0; $RESG = 0e0; $RESB = 0e0; return;
            }
            $attr = $attr * @ar[$hit]; $attg = $attg * @ag[$hit]; $attb = $attb * @ab[$hit];
            $ox = $px; $oy = $py; $oz = $pz;
            $dx = $rx; $dy = $ry; $dz = $rz;
        }
        else {                             # glass (dielectric)
            my num $ratio = $front == 1 ?? (1e0 / @ir[$hit]) !! @ir[$hit];
            my num $dl = sqrt($dx*$dx + $dy*$dy + $dz*$dz);
            my num $ux = $dx/$dl; my num $uy = $dy/$dl; my num $uz = $dz/$dl;
            my num $cos-t = -($ux*$nx + $uy*$ny + $uz*$nz);
            $cos-t = 1e0 if $cos-t > 1e0;
            my num $sin-t = sqrt(1e0 - $cos-t*$cos-t);
            # Schlick reflectance
            my num $r0 = (1e0 - $ratio) / (1e0 + $ratio);
            $r0 = $r0 * $r0;
            my num $reflct = $r0 + (1e0 - $r0) * (1e0 - $cos-t) ** 5;
            my num $nrx; my num $nry; my num $nrz;
            if $ratio * $sin-t > 1e0 || $reflct > rnd() {   # reflect
                my num $dot = $ux*$nx + $uy*$ny + $uz*$nz;
                $nrx = $ux - 2e0*$dot*$nx;
                $nry = $uy - 2e0*$dot*$ny;
                $nrz = $uz - 2e0*$dot*$nz;
            }
            else {                                            # refract
                my num $px2 = ($ux + $cos-t*$nx) * $ratio;
                my num $py2 = ($uy + $cos-t*$ny) * $ratio;
                my num $pz2 = ($uz + $cos-t*$nz) * $ratio;
                my num $k2 = 1e0 - ($px2*$px2 + $py2*$py2 + $pz2*$pz2);
                $k2 = -$k2 if $k2 < 0e0;
                my num $pf = -sqrt($k2);
                $nrx = $px2 + $pf*$nx; $nry = $py2 + $pf*$ny; $nrz = $pz2 + $pf*$nz;
            }
            $ox = $px; $oy = $py; $oz = $pz;
            $dx = $nrx; $dy = $nry; $dz = $nrz;
        }
    }
    # ran out of bounces
    $RESR = 0e0; $RESG = 0e0; $RESB = 0e0;
}

# ---------- camera + render loop ---------------------------------------
sub MAIN() {
    my int $width   = (%*ENV<RT_WIDTH>   // 300).Int;
    my int $samples = (%*ENV<RT_SAMPLES> //  40).Int;
    my int $depth   = (%*ENV<RT_DEPTH>   //  12).Int;
    my num $aspect  = 3e0 / 2e0;
    my int $height  = ($width.Num / $aspect).Int;

    srand(2026);
    build-world();

    # camera basis (lookfrom (13,2,3) -> lookat origin, vfov 20°)
    my num $vfov = 20e0;
    my num $theta = $vfov * 3.141592653589793e0 / 180e0;
    my num $h = tan($theta / 2e0);
    my num $vh = 2e0 * $h;
    my num $vw = $aspect * $vh;

    my num $fx = 13e0; my num $fy = 2e0; my num $fz = 3e0;   # lookfrom
    # w = unit(lookfrom - lookat) = unit(fx,fy,fz)
    my num $wl = sqrt($fx*$fx + $fy*$fy + $fz*$fz);
    my num $wx = $fx/$wl; my num $wy = $fy/$wl; my num $wz = $fz/$wl;
    # u = unit(cross(up=(0,1,0), w))
    my num $ux0 = 1e0*$wz - 0e0*$wy;
    my num $uy0 = 0e0*$wx - 0e0*$wz;
    my num $uz0 = 0e0*$wy - 1e0*$wx;
    my num $ul = sqrt($ux0*$ux0 + $uy0*$uy0 + $uz0*$uz0);
    my num $ux = $ux0/$ul; my num $uy = $uy0/$ul; my num $uz = $uz0/$ul;
    # v = cross(w, u)
    my num $vx = $wy*$uz - $wz*$uy;
    my num $vy = $wz*$ux - $wx*$uz;
    my num $vz = $wx*$uy - $wy*$ux;

    my num $horx = $ux*$vw; my num $hory = $uy*$vw; my num $horz = $uz*$vw;
    my num $verx = $vx*$vh; my num $very = $vy*$vh; my num $verz = $vz*$vh;
    # lower-left = lookfrom - horiz/2 - vert/2 - w
    my num $llx = $fx - $horx*0.5e0 - $verx*0.5e0 - $wx;
    my num $lly = $fy - $hory*0.5e0 - $very*0.5e0 - $wy;
    my num $llz = $fz - $horz*0.5e0 - $verz*0.5e0 - $wz;

    note "rendering {$width}x{$height}, $samples spp, depth $depth ...";
    say "P3"; say "$width $height"; say "255";

    my num $scale = 1e0 / $samples.Num;
    my int $j = $height - 1;
    while $j >= 0 {
        note "  scanlines remaining: {$j + 1}" if $j % 20 == 0;
        my int $i = 0;
        while $i < $width {
            my num $cr = 0e0; my num $cg = 0e0; my num $cb = 0e0;
            my int $s = 0;
            while $s < $samples {
                my num $u = ($i.Num + rnd()) / ($width - 1).Num;
                my num $vv = ($j.Num + rnd()) / ($height - 1).Num;
                my num $ddx = $llx + $horx*$u + $verx*$vv - $fx;
                my num $ddy = $lly + $hory*$u + $very*$vv - $fy;
                my num $ddz = $llz + $horz*$u + $verz*$vv - $fz;
                ray-colour($fx, $fy, $fz, $ddx, $ddy, $ddz, $depth);
                $cr = $cr + $RESR; $cg = $cg + $RESG; $cb = $cb + $RESB;
                $s = $s + 1;
            }
            my int $ir = (256e0 * clamp(sqrt($cr * $scale), 0e0, 0.999e0)).Int;
            my int $ig = (256e0 * clamp(sqrt($cg * $scale), 0e0, 0.999e0)).Int;
            my int $ib = (256e0 * clamp(sqrt($cb * $scale), 0e0, 0.999e0)).Int;
            say "$ir $ig $ib";
            $i = $i + 1;
        }
        $j = $j - 1;
    }
    note "done.";
}

sub clamp(num $x, num $lo, num $hi --> num) {
    $x < $lo ?? $lo !! $x > $hi ?? $hi !! $x
}
