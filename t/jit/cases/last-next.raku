# `last` and `next` inside a kernel compile to break/continue.
my $sum = 0; my $hits = 0;
loop (my $i = 0; $i < 200000; $i++) {
    next if $i % 3 == 0;
    last if $i > 150000;
    $sum = $sum + $i;
    $hits = $hits + 1;
}
say "$sum $hits $i";
