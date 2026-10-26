# race-detect: report
# location: scalar $n
#
# A missing edge: two `.then` continuations on one promise. Each starts with
# the settler's clock, but they are not ordered with each other (they run
# concurrently, unserialized), so their two writes of $n race.
my $n = 0;
my $go = Promise.new;
my $p = start { await $go; 1 };
my @q = $p.then({ $n++ }), $p.then({ $n++ });
$go.keep;
await @q;
say $n;
