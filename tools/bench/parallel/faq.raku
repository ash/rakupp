# The program from docs/guide/faq/threads.md: four units as a plain loop, then
# as four `start` blocks. A `my int` accumulator and a pointy loop variable
# `-> $i`, which is the shape most user code has.
#
#     rakupp tools/bench/parallel/faq.raku
#
# The first time printed is the threaded run; the speed-up follows it.

my $M = (@*ARGS[0] // 2_000_000).Int;

sub work($n) {
    my int $s = 0;
    for ^$M -> $i { $s = $s + $i % 7 }
    $s + $n
}
my $t0 = now;
my @plain = (^4).map({ work($_) });
my $plain = now - $t0;
$t0 = now;
my @threads = await (^4).map({ start work($_) });
my $threads = now - $t0;
die 'the two sides disagree' unless @plain eqv @threads;
say sprintf 'faq %.3fs with start, %.3fs plain: %.2f×  sum=%d',
    $threads, $plain, $plain / $threads, @threads.sum;
