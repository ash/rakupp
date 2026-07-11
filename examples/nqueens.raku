#!/usr/bin/env raku
# The N-Queens puzzle: place N queens on an N by N board so that no two
# share a row, column, or diagonal. This walks the classic backtracking
# search — one queen per row, extending a partial placement column by
# column and pruning any position that clashes with a queen already down.
#
# Shows off recursion, array building, and the junction-free `none`/`grep`
# style of checking a candidate against every placement made so far.

constant \N = 8;

# @placed[$row] = the column chosen for the queen in that row.
# A new queen at ($row, $col) is safe when no earlier queen sits in the
# same column or on either diagonal (row-col and row+col are constant
# along the two diagonal directions).
sub safe(@placed, $col) {
    my $row = @placed.elems;
    for ^$row -> $r {
        my $c = @placed[$r];
        return False if $c == $col;
        return False if $r - $c == $row - $col;
        return False if $r + $c == $row + $col;
    }
    True;
}

my $count = 0;
my @first;

sub solve(@placed) {
    if @placed.elems == N {
        $count++;
        @first = @placed.List if @first.elems == 0;
        return;
    }
    for ^N -> $col {
        if safe(@placed, $col) {
            my @next = @placed.clone;
            @next.push($col);
            solve(@next);
        }
    }
}

solve([]);

say "Solutions for N=" ~ N ~ ": " ~ $count;
say '';
say 'One solution:';
for @first -> $col {
    my @row = '.' xx N;
    @row[$col] = 'Q';
    say @row.join(' ');
}
