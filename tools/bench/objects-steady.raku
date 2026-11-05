# objects.raku at ten times the work — 1M `.new`s, 1.5M method calls and 2.5M
# attribute reads.
#
# V6-PLAN P0. At objects.raku's size Rakudo spends ~80 ms of its time starting
# up and warming up, which is a large part of its total, so the row reads
# rakupp far closer than it is: 1.4× there, 2.93× at this size on 2026-09-26.
# This row is the steady-state figure V6's first number is judged by. Keep the
# body identical to objects.raku; only the count differs.
class Point {
    has $.x;
    has $.y;
    method len2()     { $.x * $.x + $.y * $.y }
    method shift($d)  { Point.new(x => $.x + $d, y => $.y - $d) }
}
my $acc = 0;
for 1 .. 1_000_000 -> $i {
    my $p = Point.new(x => $i % 100, y => $i % 37);
    $acc += $p.shift(1).len2;
}
say $acc;
