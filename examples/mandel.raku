#!/usr/bin/env raku
# Print the Mandelbrot set.
#
# History:
#   Originally languages/perl6/examples/mandel.p6 in the Parrot project
#   (~2004), translated from C by Glenn Rhoads and ported to Perl 6 by
#   Leopold Tötsch <lt@toetsch.at>. That file no longer compiles under
#   present-day Raku (nor under raku++) because of two language changes
#   since the Parrot era; see docs/dev/MANDEL.md for the full story.
#
# This is a faithful modernisation. Only two things changed, both forced
# by the language rather than by any single implementation:
#   1. Whitespace is now required around the  <  and  >  comparison
#      operators. The original wrote  $k<112 ; today  $k<...>  is
#      angle-bracket subscript/word-quote syntax, so it must be  $k < 112.
#   2. A C-style comma expression used as a loop condition is now a List
#      (always true) instead of "the value of the last element". The
#      per-iteration side effects that used to live in the condition are
#      moved into the loop init / body / step.
#
# The algorithm, the character ramp, and the 75x30 geometry are unchanged.
# Output is byte-for-byte identical under Rakudo and raku++.

sub MAIN() {
    my ($x, $y, $k);
    my @b = (' ', '.', ':', ',', ';', '!', '/', '>', ')', '|', '&', 'I', 'H', '%', '*', '#');

    my ($r, $i, $z, $Z, $t, $c, $C);
    loop ($y = 30; $y > 0; $y--) {
        $C = $y * 0.1 - 1.5;
        loop ($x = 0; $x < 75; $x++) {
            $c = $x * 0.04 - 2.0;
            $z = 0.0;
            $Z = 0.0;
            loop ($r = $c, $i = $C, $k = 0; $k < 112; $k++) {
                $t = $z * $z - $Z * $Z + $r;
                $Z = 2.0 * $z * $Z + $i;
                $z = $t;
                last if $z * $z + $Z * $Z > 10.0;
            }
            print @b[$k % 16];
        }
        print "\n";
    }
}

