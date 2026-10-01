# Regression: a `react` that ended with `done` left its `whenever $supply-block`
# subscriptions open, so the supply's CLOSE phasers ran at program exit or never.
# Cro's runner kills the services it started from its CLOSE, so Ctrl-C on
# `cro run` left them running.
# Contract: exit 0 + last line PASS.
my @fail;
my @log;

my $feed = Supplier.new;
my $s = supply {
    whenever $feed.Supply { emit $_ }
    CLOSE { @log.push("close") }
}
react {
    whenever $s { @log.push($_) }
    whenever Promise.in(0.1) { $feed.emit("v") }
    whenever Promise.in(0.3) { done }
}
@log.push("after");
@fail.push("order {@log.raku}") unless @log eqv ["v", "close", "after"];

say @fail ?? "FAIL: @fail.join('; ')" !! "PASS";
exit @fail ?? 1 !! 0;
