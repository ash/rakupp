# race-detect: silent
# edges: promise
# expect: 42
#
# A manual promise kept through its vow from a worker: the keep releases,
# the await acquires.
my $x;
my $p = Promise.new;
my $v = $p.vow;
start { $x = 42; $v.keep(True) };
await $p;
say $x;
