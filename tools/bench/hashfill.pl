# Perl 5 twin of hashfill.raku — the same work, byte-identical output.
my %h;
for my $i (1 .. 200_000) {
    $h{"key$i"} = $i * 2;
}
my $sum = 0;
$sum += $_ for values %h;
my $s = '';
$s .= 'x' for 1 .. 50_000;
print "$sum ", length($s), "\n";
