#!/usr/bin/env rakupp
# The same arithmetic, once in this thread and once spread over several.

sub count-primes(Int $from, Int $to --> Int) { ($from ..^ $to).grep(*.is-prime).elems }

sub ms($t0) { ((now - $t0) * 1000).Int }

sub MAIN(Int $limit = 400_000, Int :$parts = 8) {
    my $t0 = now;
    my $serial = count-primes(2, $limit);
    my $serial-ms = ms($t0);

    # One promise per chunk. `start` hands the block to the scheduler and
    # returns immediately; `await` on the list gives the results in order.
    my $size = ($limit / $parts).ceiling;
    $t0 = now;
    my @promises = (^$parts).map(-> $i {
        start count-primes(max(2, $i * $size), min($limit, ($i + 1) * $size))
    });
    my $parallel = ([+] await @promises);
    my $parallel-ms = ms($t0);

    say "primes below $limit";
    say "  one thread    : $serial in {$serial-ms} ms";
    say "  $parts promises   : $parallel in {$parallel-ms} ms";
    say "  ratio         : { ($serial-ms / max($parallel-ms, 1)).round(0.1) }x";

    # race and hyper say the same thing in one line. They return the right
    # answer; see the page for what they cost on this engine.
    $t0 = now;
    my $raced = (2 ..^ $limit).race(:degree($parts), :batch(4096)).grep(*.is-prime).elems;
    say "  race(:degree($parts)) : $raced in { ms($t0) } ms";
}
