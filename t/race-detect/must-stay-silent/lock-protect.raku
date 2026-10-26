# race-detect: silent
# edges: lock
# expect: 40000
#
# The FAQ's Lock counter (guide/faq/threads.md), scaled down: every `$n++`
# is inside one Lock's protect, so each unlock releases and the next lock
# acquires.
my $lock = Lock.new;
my $n = 0;
await (^4).map: { start { for ^10_000 { $lock.protect: { $n++ } } } };
say $n;
