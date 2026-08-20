#!/usr/bin/env rakupp
# pick-fresh.raku — the N most recently released distributions in the REA index.
#
#   rakupp pick-fresh.raku --top=100 rea.json > fresh.tsv
#
# One dist-JSON-object per line, read line-wise: a full parse of an 18 MB
# string is not needed to read four fields, and rakupp's substr on a multi-MB
# string is O(position) (battery finding #5).
sub field($line, $key) {
    $line ~~ /'"' $key '":"' (<-["]>+) '"'/ ?? ~$0 !! ''
}

sub MAIN($rea, :$top = 100) {
    my %latest;
    for $rea.IO.lines -> $line {
        next unless $line.starts-with('{');
        my $date = field($line, 'release-date');
        my $dist = field($line, 'dist');
        # the dist string is the authority for the name: a line's first
        # "name" key can belong to a nested object (Termbox2 provides a
        # script called cc), the dist identity never does
        my $name = $dist ~~ /^ (.+?) ':ver<'/ ?? ~$0 !! field($line, 'name');
        next unless $name && $date;
        $dist ||= $name;
        my $ver  = field($line, 'version');
        my $have = %latest{$name};
        if !$have.defined || $date gt $have<date> {
            %latest{$name} = { :$name, :$date, :$dist, :$ver };
        }
    }
    note "{%latest.elems} distinct dists in the index";
    my @newest = %latest.values.sort({ $^b<date> leg $^a<date> })[^ $top.Int];
    say "date\tname\tversion\tdist";
    say "{.<date>}\t{.<name>}\t{.<ver>}\t{.<dist>}" for @newest;
}
