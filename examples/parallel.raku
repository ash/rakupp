#!/usr/bin/env raku
# Concurrency: prime counting spread across worker threads, then a
# producer/consumer pipeline over a Channel. Both are deterministic — the
# work is split up and run in parallel, but the results are merged back in
# order, so the output is always the same.

# --- Promises: fan out, then await ---------------------------------------
#
# Split the range 2 .. LIMIT into CHUNKS slices, count the primes in each
# slice on its own thread with `start`, then `await` all of them and sum.
constant LIMIT  = 100_000;
constant CHUNKS = 8;

my $chunk = LIMIT div CHUNKS;

my @jobs = (^CHUNKS).map: -> $i {
    my $lo = 2 max $i * $chunk;
    my $hi = ($i + 1) * $chunk - 1;
    start {
        my $count = ($lo .. $hi).grep(*.is-prime).elems;
        %( :$lo, :$hi, :$count );
    }
}

my @done = await @jobs;
my $total = @done.map(*<count>).sum;

say "Primes below {LIMIT}, counted on {CHUNKS} threads:";
for @done -> $r {
    say sprintf('  [%6d .. %6d] -> %d primes', $r<lo>, $r<hi>, $r<count>);
}
say "  total = $total";
say '';

# --- Channel: a producer/consumer pipeline -------------------------------
#
# One thread pushes work onto a Channel and closes it; the main thread
# drains it. `.list` on a Channel blocks and yields every value that was
# sent, in order, until the Channel is closed.
my $ch = Channel.new;

start {
    for 1 .. 12 -> $n {
        $ch.send($n * $n);
    }
    $ch.close;
}

my @squares = $ch.list;
say "Squares received over a Channel: @squares[]";
say "Their sum is {@squares.sum}";
