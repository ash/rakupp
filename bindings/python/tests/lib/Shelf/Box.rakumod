# The module tests/test_objects.py loads by a two-part name, written with a
# dot as well as with `::`. Nothing declares Shelf itself: the engine makes
# it the package that Shelf::Box implies, which raku.main.Shelf walks.
unit module Shelf::Box;

our sub volume($w, $h, $d) { $w * $h * $d }
sub cube($s) is export { $s ** 3 }
