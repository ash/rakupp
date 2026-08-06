# Supply.interval is a real timer, not the old finite 0..4 stand-in:
# - emits ascending Ints starting at 0, the first immediately (delay 0),
# - one tick per interval of REAL time (the old stub fired all five at t=0),
# - runs until done/last ends the subscription (it never ends on its own),
# - the two-argument form delays the first tick,
# - a direct .tap ticks too, and .close stops it.
# Passes on both engines.

my @got;
my $t0 = now;
react {
    whenever Supply.interval(0.2) {
        push @got, $_;
        done if $_ >= 2;
    }
}
my $took = now - $t0;
die "ticks wrong: @got[]" unless @got == 3 && @got[0] == 0 && @got[1] == 1 && @got[2] == 2;
# three ticks 0.2 apart: the run must take at least ~0.4s of real time
# (the stub finished in microseconds) and finish well under the timeout
die "no real timing: took $took" if $took < 0.3;
die "too slow: took $took" if $took > 3;

# delayed first tick: interval(0.15, 0.4) fires 0 at ~0.4s
my $t1 = now;
my $first-at;
react {
    whenever Supply.interval(0.15, 0.4) {
        $first-at = now - $t1;
        done;
    }
}
die "delay ignored: first at $first-at" if $first-at < 0.3;

# `last` ends the subscription and the react
my @l;
react {
    whenever Supply.interval(0.1) {
        push @l, $_;
        last if $_ >= 1;
    }
}
die "last wrong: @l[]" unless @l == 2;

# direct .tap ticks; .close stops it
my @t;
my $tap = Supply.interval(0.1).tap({ push @t, $_ });
sleep 0.35;
$tap.close;
my $n = @t.elems;
die "tap saw no ticks" if $n < 2;
sleep 0.3;
die "close did not stop the ticker" unless @t.elems == $n;

say "PASS";
