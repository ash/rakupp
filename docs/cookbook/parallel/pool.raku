#!/usr/bin/env rakupp
# A worker pool: a fixed number of workers taking jobs off one queue.
#
# The queue is a Channel, which is the thread-safe part — nothing here locks
# anything, and no two workers can take the same job.

sub MAIN(Int :$workers = 3, Int :$jobs = 9, Real :$each = 0.3) {
    my $work = Channel.new;
    my $out  = Channel.new;
    my $t0   = now;

    # Every worker reads the same Channel. `for $chan.list` blocks until there
    # is something to take, and ends when the channel is closed.
    my @running = (1 .. $workers).map(-> $id {
        start {
            for $work.list -> $job {
                sleep $each;                    # stand-in for the real work
                $out.send({ :$id, :$job });
            }
        }
    });

    $work.send($_) for 1 .. $jobs;
    $work.close;                                # tells the workers when to stop
    await @running;                             # every worker has finished
    $out.close;

    my @done = $out.list;
    my %by-worker = @done.classify(*.<id>);

    say "$jobs jobs of { $each }s across $workers workers: { ((now - $t0) * 1000).Int } ms";
    say "  (one after another would be { ($jobs * $each * 1000).Int } ms)";
    for %by-worker.keys.sort -> $id {
        say "  worker $id: { %by-worker{$id}.map(*.<job>).sort.join(' ') }";
    }
}
