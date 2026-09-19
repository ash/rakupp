# String operators go through the runtime's own eq/lt/~ helpers.
my $c = 0; my $i = 0; my $w = "";
while $i < 50000 {
    my $s = $i % 2 == 0 ?? "even" !! "odd";
    $c = $c + 1 if $s eq "even";
    $w = $s if $i == 49999;
    $i = $i + 1;
}
say "$c $w";
