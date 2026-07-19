# The classic. Statement modifiers, string comparison, a C-style loop.
for (my $i = 1; $i <= 20; $i++) {
    my $out = '';
    $out .= 'Fizz' if $i % 3 == 0;
    $out .= 'Buzz' if $i % 5 == 0;
    $out = $i if $out eq '';
    print "$out\n";
}
