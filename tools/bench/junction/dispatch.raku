# Is the ~400 ns per eigenstate the LOOP, or just what one plain smartmatch costs?
my $reps = 300000;
my $loop = do { my $t = now; for ^$reps { }; (now - $t)/$reps };
sub bench($label, &body) {
    my $t = now; body() for ^$reps;
    printf "%-26s %7.1f ns\n", $label, ((now - $t)/$reps - $loop)*1e9;
}
my ($a, $b) = 5, 6;
bench('$a ~~ $b   (plain)',  { $a ~~ $b });
bench('$a == $b   (plain)',  { $a == $b });
my $j3 = any(1, 3, 5);
bench('$a ~~ any(1,3,5)',    { $a ~~ $j3 });
