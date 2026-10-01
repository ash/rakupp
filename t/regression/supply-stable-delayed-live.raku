# Regression: `Supply.stable` and `.delayed` existed only for list-backed
# supplies, where they were silent no-ops; on a live Supply they were "No such
# method". `cro run` debounces its file watcher with `.stable(1)`. Both are now
# timer-driven supply blocks, and a time of 0 is the Supply itself (Roast
# S17-supply/stable.t and delayed.t).
# Contract: exit 0 + last line PASS.
my @fail;

my $s = Supplier.new;
my (@stable, @delayed);
react {
    whenever $s.Supply.stable(0.3) { @stable.push($_) }
    whenever $s.Supply.delayed(0.2) { @delayed.push($_) }
    whenever Promise.in(0.05) { $s.emit(1); $s.emit(2) }
    whenever Promise.in(0.6)  { $s.emit(3) }
    whenever Promise.in(1.2)  { done }
}
@fail.push("stable gave {@stable.raku}") unless @stable eqv [2, 3];
@fail.push("delayed gave {@delayed.raku}") unless @delayed eqv [1, 2, 3];

my $l = Supply.from-list(1..3);
@fail.push("stable(0) is not a noop") unless $l.stable(0) === $l;
@fail.push("delayed(0) is not a noop") unless $l.delayed(0) === $l;

say @fail ?? "FAIL: @fail.join('; ')" !! "PASS";
exit @fail ?? 1 !! 0;
