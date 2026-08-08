#!/usr/bin/env raku
# Sleep sort: the famous joke sorting algorithm, which is really a
# demonstration of concurrency wearing a funny hat.
#
# For each number we launch a thread that sleeps for a time proportional to
# the number's value and then emits it. Small numbers wake up first, large
# numbers last, so the values arrive already in order — the scheduler does
# the "comparing" for us. It is O(max value) in wall-clock time and no use
# for real sorting, but it is a tidy little tour of `start`, a `Channel`,
# and `await`.

my @input = 5, 1, 8, 2, 9, 3, 7, 4, 6;

# One tick per unit of value. Keep it comfortably larger than scheduler
# jitter so distinct integers can't overtake each other — 20 ms proved
# too tight on loaded CI runners.
constant TICK = 0.1;

my $out = Channel.new;

# The starting gun: every sleeper blocks on this Promise until all of them
# are launched, THEN they start their sleeps together. Without it the sort
# also races thread LAUNCH time — on a busy two-core CI runner spawning the
# ninth thread can lag the first by more than a whole tick, which reordered
# the output no matter how wide the tick was.
my $go = Promise.new;

# `.eager` forces every `start` to launch now, rather than lazily.
my @sleepers = @input.map(-> $n {
    start {
        await $go;
        sleep $n * TICK;
        $out.send($n);
    }
}).eager;
$go.keep;

# Once every sleeper has emitted its value, close the Channel; draining it
# then yields the values in the order they woke up — i.e. sorted.
await @sleepers;
$out.close;

say "in:  @input[]";
say "out: {$out.list}";
