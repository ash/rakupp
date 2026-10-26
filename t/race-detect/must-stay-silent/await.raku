# race-detect: silent
# edges: promise
# expect: 42
#
# A worker writes $x and finishes; the mainline awaits it and reads $x. The
# worker's settle releases, the `await` acquires.
my $x;
my $p = start { $x = 42; 'done' };
await $p;
say $x;
