# A C-style loop: the init has already run when the kernel is entered, so its
# variables are slots, not locals. $n must be visible and correct afterwards.
my $acc = 0;
loop (my $n = 0; $n < 100000; $n++) { $acc = $acc + $n * 2 }
say "$acc $n";
