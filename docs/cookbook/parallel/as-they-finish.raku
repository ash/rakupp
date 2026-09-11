#!/usr/bin/env rakupp
# Results in the order they arrive, not the order they were started — and a
# deadline over the whole set.

sub job(Int $id, Real $seconds) {
    start {
        sleep $seconds;
        "job $id (took {$seconds}s)"
    }
}

sub MAIN(Real :$deadline = 1.5) {
    my @jobs = job(1, 0.9), job(2, 0.3), job(3, 0.6), job(4, 2.0);
    my $t0   = now;

    # react blocks until every whenever it set up is done, or until something
    # inside it calls done.
    react {
        for @jobs -> $p {
            whenever $p -> $result {
                say "  { ((now - $t0) * 1000).Int } ms  $result";
            }
        }
        whenever Promise.in($deadline) {
            say "  { ((now - $t0) * 1000).Int } ms  deadline: not waiting for the rest";
            done;
        }
    }

    say 'finished: ', @jobs.grep({ .status == Kept }).elems, ' of ', @jobs.elems;
    say 'still running: ', @jobs.grep({ .status == Planned }).elems;
}
