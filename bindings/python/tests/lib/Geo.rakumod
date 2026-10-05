# The module tests/test_objects.py loads with Interp.use: one of each thing a
# module can hand to Python.
unit module Geo;

class Point is export {
    has $.x;
    has $.y;
    has $.label is rw = '';
    method dist(Point $o) { sqrt(($!x - $o.x)² + ($!y - $o.y)²) }
    method moved(:$dx = 0, :$dy = 0) { Point.new(x => $!x + $dx, y => $!y + $dy) }
    method scaled(:$by_factor = 1) { Point.new(x => $!x * $by_factor, y => $!y * $by_factor) }
    method Str { "($!x, $!y)" }
}

sub origin() is export { Point.new(x => 0, y => 0) }
sub centroid(@points) is export {
    Point.new(x => @points.map(*.x).sum / @points, y => @points.map(*.y).sum / @points)
}
sub far-away(:$from-x = 0) is export { Point.new(x => $from-x + 1000, y => 0) }
multi sub area(Int $s) is export { $s * $s }
multi sub area($w, $h) is export { $w * $h }
sub evens() is export { (0..*).map(* * 2) }
sub corners() is export { (Point.new(x => 0, y => 0), Point.new(x => 1, y => 1)) }
sub labels() is export { %( a => Point.new(x => 1, y => 2), n => 3 ) }
sub secret() is export(:extra) { 42 }
sub broken() is export { fail "no such shape" }
our sub perimeter($w, $h) { 2 * ($w + $h) }
constant PI-ISH is export = 3.14;
enum Color is export <Red Green Blue>;
