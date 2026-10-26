# race-detect: silent
# edges: promise
# expect: 42 done
#
# `.result` waits as `await` does, and acquires the same edge.
my $x;
my $p = start { $x = 42; 'done' };
my $r = $p.result;
say "$x $r";
