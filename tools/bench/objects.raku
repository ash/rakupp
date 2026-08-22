# Object construction and method dispatch — 200k `.new`s, 300k method calls and
# 500k attribute reads. Every other kernel in this directory is built from
# builtin types and subs; this one is the only measure of what a program pays
# for `class`, `has`, and the method dispatcher, which is the shape most real
# Raku code is written in.
class Point {
    has $.x;
    has $.y;
    method len2()     { $.x * $.x + $.y * $.y }
    method shift($d)  { Point.new(x => $.x + $d, y => $.y - $d) }
}
my $acc = 0;
for 1 .. 100_000 -> $i {
    my $p = Point.new(x => $i % 100, y => $i % 37);
    $acc += $p.shift(1).len2;
}
say $acc;
