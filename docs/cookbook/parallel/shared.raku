#!/usr/bin/env rakupp
# Three ways for eight threads to produce 16,000 numbers, one of which is a
# bug whether or not it happens to work today.

sub MAIN(Int :$threads = 8, Int :$each = 2000) {
    my $expected = $threads * $each;

    # 1. Everybody pushes to the same Array. Array is not thread-safe: two
    #    pushes that overlap can land on the same slot.
    my @out;
    await (1 .. $threads).map(-> $n {
        start { @out.push($_) for ($n - 1) * $each ..^ $n * $each }
    });
    say "shared Array   : expected $expected, got { @out.elems }";

    # 2. The same Array, with a Lock around the push. Correct, and the lock is
    #    taken 16,000 times.
    my @locked;
    my $lock = Lock.new;
    await (1 .. $threads).map(-> $n {
        start { for ($n - 1) * $each ..^ $n * $each { $lock.protect({ @locked.push($_) }) } }
    });
    say "Array + Lock   : expected $expected, got { @locked.elems }";

    # 3. No sharing at all: each thread builds its own list and returns it.
    #    Nothing is locked because nothing is shared.
    my @parts = await (1 .. $threads).map(-> $n {
        start { (($n - 1) * $each ..^ $n * $each).List }
    });
    say "one list each  : expected $expected, got { @parts.map(*.elems).sum }";
}
