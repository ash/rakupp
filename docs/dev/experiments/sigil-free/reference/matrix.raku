my @rows = [[1, 2, 3], [4, 5, 6]];
for @rows -> @row {
    my $sum = 0;
    for @row -> $cell { $sum += $cell }
    say $sum;
}
say @rows.elems;
