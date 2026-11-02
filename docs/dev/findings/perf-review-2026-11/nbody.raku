# K7: Raku as people write it — objects, attributes, methods, a list of bodies
class Body {
    has $.x is rw; has $.y is rw; has $.vx is rw; has $.vy is rw; has $.m;
    method advance($dt) { $!x += $dt * $!vx; $!y += $dt * $!vy }
}
my @b = (^5).map({ Body.new(x => $_ * 1e0, y => $_ * 0.5e0, vx => 0e0, vy => 0e0, m => 1e0 + $_) });
sub energy(@b) { my $e = 0e0; for @b -> $p { $e += 0.5e0 * $p.m * ($p.vx * $p.vx + $p.vy * $p.vy) }; $e }
for ^20000 {
    for ^5 -> $i {
        my $bi = @b[$i];
        for ($i+1)..^5 -> $j {
            my $bj = @b[$j];
            my $dx = $bi.x - $bj.x; my $dy = $bi.y - $bj.y;
            my $d2 = $dx * $dx + $dy * $dy + 0.01e0;
            my $mag = 0.001e0 / ($d2 * sqrt($d2));
            $bi.vx -= $dx * $bj.m * $mag; $bi.vy -= $dy * $bj.m * $mag;
            $bj.vx += $dx * $bi.m * $mag; $bj.vy += $dy * $bi.m * $mag;
        }
    }
    .advance(0.001e0) for @b;
}
say energy(@b);
