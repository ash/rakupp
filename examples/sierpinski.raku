#!/usr/bin/env raku
# The Sierpinski triangle, drawn by an elementary cellular automaton.
#
# Rule 90 is the simplest rule that produces a fractal: each cell in the
# next row is the exclusive-or (+^) of its two upper neighbours. Start
# from a single live cell in the middle of an otherwise empty row and let
# the rule run, and the live cells trace out the Sierpinski gasket — the
# same self-similar triangle you get from Pascal's triangle taken mod 2.
#
# Features: infix bit ops (`+^`), array building, `.map`/`.join` for the
# row-to-text step. No randomness; the picture is identical every run.

my $rows  = 32;
my $width = 2 * $rows - 1;      # room for the triangle to spread each side

# One row of cells: 0 = dead, 1 = live. Seed a single live cell in the
# centre.
my @row = 0 xx $width;
@row[$width div 2] = 1;

for ^$rows {
    # Draw the current generation. '#' is a live cell, a space is a dead one.
    say @row.map({ $_ ?? '#' !! ' ' }).join;

    # Rule 90: next[i] = row[i-1] +^ row[i+1], with the world padded by
    # dead cells beyond both edges.
    my @next;
    for ^$width -> $i {
        my $left  = $i > 0          ?? @row[$i - 1] !! 0;
        my $right = $i < $width - 1  ?? @row[$i + 1] !! 0;
        @next[$i] = $left +^ $right;
    }
    @row = @next;
}
