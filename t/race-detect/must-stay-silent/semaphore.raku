# race-detect: silent
# edges: semaphore
# expect: 42
# gil: skip (a Semaphore does nothing under RAKUPP_GIL=1: faq/threads.md)
#
# `.release` releases into the semaphore, `.acquire` acquires from it.
my $s = Semaphore.new(0);
my $x;
my $w = start { $s.acquire; $x * 2 };
$x = 21;
$s.release;
say await $w;
