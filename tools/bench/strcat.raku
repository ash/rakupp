# Build a string by repeated concatenation.
my $s = "";
for 1 .. 50_000 {
    $s ~= "x";
}
say $s.chars;
