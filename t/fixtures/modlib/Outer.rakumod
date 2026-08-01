# Depth 1: exports a sub, a class, and an operator, so a compiled binary that
# carries this module is checked for more than "the file was found".
unit module Outer;
use Deep::Inner;

sub outer-value() is export { 'outer(' ~ inner-value() ~ ')' }

class Shape is export {
    has $.n;
    method describe { "shape-{$!n}" }
}

sub infix:<⊕>($a, $b) is export { $a * 10 + $b }
