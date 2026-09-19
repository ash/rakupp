# Strings never leave the box array; the point is that everything AROUND them
# still runs on registers and the two stay in step. The `.chars` is outside the
# loop on purpose — a method call inside would refuse the loop.
my $i = 0; my $out = ""; my $n = 0;
while $i < 60 {
    $out = $out ~ (($i %% 10) ?? "|" !! ".");
    $n = $n + $i;
    $i = $i + 1;
}
say $out.chars, " ", $n, " ", $out;
