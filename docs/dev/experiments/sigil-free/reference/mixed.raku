my %inventory = {};
for <apple:fruit carrot:veg banana:fruit leek:veg> -> $pair {
    my ($item, $kind) = $pair.split(':');
    %inventory{$kind}.push($item);
}
for %inventory.keys.sort -> $kind {
    say $kind, ': ', %inventory{$kind}.sort.join(', ');
}
sub evens($limit) {
    gather for 1..$limit -> $i { take $i if $i %% 2 }
}
my $e = evens(10);
say $e;
sub classify($n) {
    given $n {
        when * < 0 { 'negative' }
        when 0     { 'zero' }
        default    { 'positive' }
    }
}
say (-2, 0, 5).map(&classify);
my &counter = do { my $c = 0; -> { ++$c } };
counter() for ^3;
say counter();
my @grid = (1..3).map(-> $r { [(1..3).map(-> $c { $r * $c })] }).Array;
say @grid[2][1];
say @grid.map(*.sum);
my $text = 'hello world';
say $text.subst('world', 'there');
say $text.comb.grep(/<[aeiou]>/).elems;
my %point = (x => 1, y => 2).Hash;
say %point<x> + %point<y>;
