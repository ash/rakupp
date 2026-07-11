#!/usr/bin/env raku
# Towers of Hanoi: move a stack of N disks from one peg to another, never
# placing a larger disk on a smaller one. The recursive insight is tiny --
# to move N disks, move the top N-1 out of the way, move the biggest, then
# move the N-1 back on top. Shows recursion and a running move counter.

my $moves = 0;

sub hanoi($n, $from, $to, $via) {
    return if $n == 0;
    # Move the top n-1 disks off onto the spare peg.
    hanoi($n - 1, $from, $via, $to);
    # Move the largest remaining disk to its destination.
    $moves++;
    say "  move {$moves.fmt('%2d')}: disk $n   $from -> $to";
    # Bring the n-1 disks back on top.
    hanoi($n - 1, $via, $to, $from);
}

my $disks = 4;
say "Towers of Hanoi with $disks disks:";
say '';
hanoi($disks, 'A', 'C', 'B');

say '';
say "Solved in $moves moves.";
# A stack of N disks always takes exactly 2^N - 1 moves -- the minimum.
say "Minimum for $disks disks is 2^$disks - 1 = {2 ** $disks - 1}.";
say 'Optimal? ', $moves == 2 ** $disks - 1;
