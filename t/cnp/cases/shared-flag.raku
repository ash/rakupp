# CNP: refused — and being refused is the whole of what it checks.
# The registers are hoisted, so a loop whose condition is a flag ANOTHER THREAD
# sets would never see the write and would never end. A kernel is therefore not
# entered at all while another Raku thread is live; this loop stays interpreted,
# the write lands, and the program terminates. Nothing here is about speed — it
# is about the one thing hoisting gives up, and it hung before the guard existed.
#
# The output says nothing about HOW MANY times the worker went round, because
# that is a race and two plain runs would not agree with each other either.
my $stop = False;
my $worker = start {
    my $n = 0;
    until $stop { $n = $n + 1 }
    'the worker saw the flag'
};
my $ticks = 0;
loop (my $i = 0; $i < 500; $i++) { $ticks = $ticks + 1 }
$stop = True;
say await($worker);
say "main ran $ticks times";
