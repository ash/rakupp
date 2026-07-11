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
# jitter so distinct integers can't overtake each other.
constant TICK = 0.02;

my $out = Channel.new;

# `.eager` forces every `start` to launch now, rather than lazily.
my @sleepers = @input.map(-> $n {
    start {
        sleep $n * TICK;
        $out.send($n);
    }
}).eager;

# Once every sleeper has emitted its value, close the Channel; draining it
# then yields the values in the order they woke up — i.e. sorted.
await @sleepers;
$out.close;

say "in:  @input[]";
say "out: {$out.list}";
