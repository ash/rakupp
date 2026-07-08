# Build a large string by repeated `~=` append.
# Without `-O` each step rebuilds the whole string (O(n²) copying); with `-O`
# the accumulator is appended in place (O(n)). This is an algorithmic win, so
# the gap widens with the loop count.
my $s = "";
for 1 .. 400_000 { $s ~= "ab"; }
say $s.chars;
