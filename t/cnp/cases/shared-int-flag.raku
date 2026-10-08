# CNP: refused — and terminating is the whole of what this checks.
# As shared-flag.raku, with an Int flag: a Bool condition is one the loop
# kernel declines anyway, an Int one it compiles. A loop kernel holds its
# variables in its frame until the loop ends, so a loop polling a flag ANOTHER
# THREAD sets would never see the write. While other threads are live, a loop
# kernel is entered only when no other thread can reach its variables
# (PARALLEL-SCALING-PLAN P5); `$stop` is named in the `start` block, so this
# loop stays interpreted, the write lands, and the program ends. The main
# thread's own counting loop, whose variables nothing else can reach, may
# still run compiled beside it.
#
# The output says nothing about how many times the worker went round: that is
# a race, and two plain runs would not agree with each other either.
my $stop = 0;
my $worker = start {
    my $n = 0;
    while $stop == 0 { $n = $n + 1 }
    'the worker saw the flag'
};
my $ticks = 0;
loop (my $i = 0; $i < 500; $i++) { $ticks = $ticks + 1 }
sleep 0.01;
$stop = 1;
say await($worker);
say "main ran $ticks times";
