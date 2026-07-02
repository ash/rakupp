# Match a simple regex many times.
my $count = 0;
for 1 .. 50_000 {
    $count++ if "abc123def456" ~~ / \d+ /;
}
say $count;
