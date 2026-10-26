# race-detect: report
# location: scalar $x
#
# A missing edge: the worker is never awaited, and `sleep` is not
# synchronization. The `say` reads $x with nothing ordering the worker's
# write before it, however long the sleep. (A program that is correct only
# because the worker "has surely finished by now" is what this catches.)
my $x = 0;
start { $x = 1 };
sleep 0.2;
say $x;
