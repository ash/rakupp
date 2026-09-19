# An enum value is an Int with a NAME, and the name is its identity: unboxed to
# a bare int64 it would print as its ordinal the moment it was copied out.
enum Colour <Red Green Blue>;
my $g = Green; my $b = Blue;
my $c = $g; my $i = 0; my $seen = "";
while $i < 6 {
    $c = $i %% 2 ?? $g !! $b;
    $seen = $seen ~ $c ~ " ";
    $i = $i + 1;
}
say $seen;
say $c;
say $c.WHAT.^name;
say $c == 2;
