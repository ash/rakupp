my @queue = 1, 2, 3;
my %seen;
my $limit = 2;
while @queue && %seen.elems < $limit {
    my $item = @queue.shift;
    %seen{$item} = True;
}
say %seen.keys.sort;
say @queue;
