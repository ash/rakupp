# Regression: `$a.merge($b)` on a `supply { … }` block DRAINED the block first,
# as if it were a finite list. A block that never ends never returned, and
# `cro run` hung in `$service.metadata-changed.merge($service.source-changed)`
# without starting anything. merge/zip/zip-latest on a block subscribe when tapped.
# Contract: exit 0 + last line PASS. A hang (no output) is a failure.
my @fail;

my $feed = Supplier.new;
my $a = supply { whenever $feed.Supply.grep(*.starts-with('a')) { emit "A:$_" } }
my $b = supply { whenever $feed.Supply.grep(*.starts-with('b')) { emit "B:$_" } }
my @got;
react {
    whenever $a.merge($b) { @got.push($_) }
    whenever Promise.in(0.1) { $feed.emit("a1"); $feed.emit("b1"); $feed.emit("a2") }
    whenever Promise.in(0.5) { done }
}
@fail.push("merge gave {@got.raku}") unless @got eqv ["A:a1", "B:b1", "A:a2"];

say @fail ?? "FAIL: @fail.join('; ')" !! "PASS";
exit @fail ?? 1 !! 0;
