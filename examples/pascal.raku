#!/usr/bin/env raku
# Pascal's triangle, and two things it secretly encodes.
#
# Shows off list building (each row grows from the one above by adding
# neighbours with the zip operator `Z+`), combinatorics, and a little
# ASCII formatting. Every entry is an exact arbitrary-precision `Int`.

my $rows = 12;

# Build the triangle. The next row is 1, the pairwise sums of the current
# row's neighbours, then 1 — which is exactly what `Z+` (zip-with-plus)
# gives when a row is added to itself shifted by one.
my @triangle;
my @row = 1;
for ^$rows {
    @triangle.push: @row.clone;   # snapshot — `@row = …` below refills the same container
    @row = 1, |(@row Z+ @row[1 .. *]), 1;
}

# Print it centred. Each entry sits in a fixed-width cell, and every row
# is indented by half a cell so the triangle stays symmetric.
my $cell = @triangle[*-1].map(*.chars).max + 1;
say "Pascal's triangle:";
for @triangle -> @r {
    my $indent = ' ' x ($cell * ($rows - @r.elems) div 2);
    say $indent ~ @r.map({ sprintf('%*d', $cell, $_) }).join;
}

# The entry in row n, position k is the binomial coefficient "n choose k".
# Compute one directly and check it against the triangle we built.
sub choose($n, $k) { ([*] $n - $k + 1 .. $n) div [*] 1 .. $k }

say '';
say 'Row 11 is the binomial coefficients C(11, k):';
say '  from triangle: ', @triangle[11];
say '  from C(11, k): ', (0 .. 11).map({ choose(11, $_) });
say '  match: ', @triangle[11].List eqv (0 .. 11).map({ choose(11, $_) }).List;

# Colour each entry by parity: the odd numbers alone trace out the
# Sierpinski triangle, a fractal hiding inside the arithmetic.
say '';
say 'Odd entries only (the Sierpinski triangle):';
for @triangle -> @r {
    my $indent = ' ' x ($rows - @r.elems);
    say $indent ~ @r.map({ $_ % 2 ?? '#' !! ' ' }).join(' ');
}
