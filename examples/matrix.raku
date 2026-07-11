#!/usr/bin/env raku
# Linear algebra by hand on arrays-of-arrays: multiply, transpose, an
# identity matrix, and a 3x3 determinant. No library — just nested loops
# and reductions. Everything stays exact `Int`, so the answers are precise.

# Pretty-print a matrix with right-aligned, fixed-width columns.
sub show(@m, $label) {
    my $cell = @m.map(*.map(*.chars).max).max + 1;
    say $label;
    for @m -> @row {
        say '  ' ~ @row.map({ sprintf('%*d', $cell, $_) }).join;
    }
}

# Matrix product: entry (i, j) is the dot product of row i of A with
# column j of B. The inner `[+]` reduces the pairwise products to a sum.
sub multiply(@a, @b) {
    my @out;
    for ^@a.elems -> $i {
        my @row;
        for ^@b[0].elems -> $j {
            @row.push: [+] (^@b.elems).map({ @a[$i][$_] * @b[$_][$j] });
        }
        @out.push: @row;
    }
    @out;
}

# Transpose: swap rows and columns.
sub transpose(@m) {
    my @out;
    for ^@m[0].elems -> $j {
        my @col;
        for @m -> @row {
            @col.push: @row[$j];
        }
        @out.push: @col;
    }
    @out;
}

# An n-by-n identity matrix: 1 on the diagonal, 0 elsewhere.
sub identity($n) {
    my @out;
    for ^$n -> $i {
        my @row;
        for ^$n -> $j {
            @row.push: $i == $j ?? 1 !! 0;
        }
        @out.push: @row;
    }
    @out;
}

# Determinant of a 3x3 matrix by cofactor expansion along the top row.
sub det3(@m) {
    @m[0][0] * (@m[1][1] * @m[2][2] - @m[1][2] * @m[2][1])
  - @m[0][1] * (@m[1][0] * @m[2][2] - @m[1][2] * @m[2][0])
  + @m[0][2] * (@m[1][0] * @m[2][1] - @m[1][1] * @m[2][0]);
}

my @a = [1, 2, 3], [4, 5, 6];
my @b = [7, 8], [9, 10], [11, 12];

show(@a, 'A (2x3):');
show(@b, 'B (3x2):');
show(multiply(@a, @b), 'A * B (2x2):');
show(transpose(@a), 'transpose of A (3x2):');

# Multiplying by the identity leaves a matrix unchanged.
my @sq = [2, 0, 1], [3, 5, 4], [1, 1, 0];
my @i3 = identity(3);
show(@i3, 'I (3x3):');
say '';
say 'A square matrix, times I, is itself: ', multiply(@sq, @i3) eqv @sq;

say '';
say 'det of [[2,0,1],[3,5,4],[1,1,0]] = ', det3(@sq);
