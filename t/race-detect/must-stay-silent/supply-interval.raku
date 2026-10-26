# race-detect: silent
# edges: start react
# expect: 10 11 12
#
# A supply ticker runs on a thread of its own, spawned when the whenever taps
# it, after $base was written (the fork). Its body runs on that thread, and
# the react's end orders it before the `say`.
my $base = 10;
my @ticks;
react {
    whenever Supply.interval(0.01) {
        @ticks.push($base + $_);
        done if @ticks == 3;
    }
}
say @ticks.join(' ');
