# A text histogram: split, hashes, list ops, and `x` for repetition.
my $data = "apple banana apple cherry banana apple date cherry banana apple";
my %count;
$count{$_}++ for split /\s+/, $data;

for my $item (sort { $count{$b} <=> $count{$a} or $a cmp $b } keys %count) {
    my $bar = '#' x $count{$item};
    printf "%-8s %s (%d)\n", $item, $bar, $count{$item};
}
