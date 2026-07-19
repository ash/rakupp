# Quicksort: recursion, list operators, grep partitioning.
sub quicksort {
    my @list = @_;
    return @list if @list <= 1;
    my $pivot = shift @list;
    my @less = grep { $_ < $pivot } @list;
    my @more = grep { $_ >= $pivot } @list;
    return (quicksort(@less), $pivot, quicksort(@more));
}
my @data = (5, 2, 9, 1, 7, 3, 8, 4, 6);
my @sorted = quicksort(@data);
print "@sorted\n";
