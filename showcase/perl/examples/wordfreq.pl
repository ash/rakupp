# Word frequency: regex tokenizing, hashes, sort by value then key.
my $text = "the quick brown fox the lazy dog the fox";
my %freq;
for my $word (split /\s+/, $text) {
    $freq{$word}++;
}
for my $w (sort { $freq{$b} <=> $freq{$a} or $a cmp $b } keys %freq) {
    printf "%-6s %d\n", $w, $freq{$w};
}
