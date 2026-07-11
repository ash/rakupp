#!/usr/bin/env raku
# Conway's Game of Life on a toroidal grid, rendered as ASCII frames.
#
# A cell lives into the next generation if it has 2 or 3 live neighbours;
# a dead cell with exactly 3 live neighbours is born. The board wraps around
# at the edges (a torus). The starting field is a fresh random "soup" every
# run — each cell alive with probability DENSITY — so no two runs evolve the
# same way; watch the population settle into still lifes and blinkers.

constant \W = 30;
constant \H = 16;
constant \GENERATIONS = 100;
constant \DENSITY = 0.28;

# Seed: a random soup. `xx` re-evaluates `rand` for every cell, and builds
# H independent rows.
my @grid = [ [ (rand < DENSITY) xx W ] xx H ];

sub neighbours(@g, $r, $c) {
    my $n = 0;
    for -1, 0, 1 -> $dr {
        for -1, 0, 1 -> $dc {
            next if $dr == 0 && $dc == 0;
            $n++ if @g[($r + $dr) % H][($c + $dc) % W];
        }
    }
    $n;
}

sub step(@g) {
    my @next = [ [False xx W] xx H ];
    for ^H -> $r {
        for ^W -> $c {
            my $n = neighbours(@g, $r, $c);
            @next[$r][$c] = @g[$r][$c] ?? (2 <= $n <= 3) !! ($n == 3);
        }
    }
    @next;
}

# Repaint a frame in place. `\e[H` moves the cursor back to the top-left
# instead of scrolling, so each generation overwrites the previous one. Every
# row is full width, so nothing from the old frame is ever left behind.
sub show(@g, $gen) {
    my $alive = @g.map({ .grep(?*).elems }).sum;
    print "\e[H";
    say "generation $gen ($alive alive)    ";
    for ^H -> $r {
        say @g[$r].map({ $_ ?? '#' !! '.' }).join;
    }
}

sub MAIN(Real :$delay = 0.08) {
    print "\e[2J\e[?25l";          # clear the screen, hide the cursor
    LEAVE print "\e[?25h\n";       # restore the cursor on the way out
    for 0 ..^ GENERATIONS -> $gen {
        show(@grid, $gen);
        @grid = step(@grid);
        sleep $delay;
    }
}
