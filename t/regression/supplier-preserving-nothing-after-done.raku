# Regression: a `react` reading a Supplier::Preserving that another thread
# floods could receive a value AFTER its `whenever` said `done` — [1,2,3,4,5,528]
# — a few runs in a hundred under load (Roast S17-supply/supplier-preserving.t).
# The react's teardown wrote the tap record's flags without the supplier's
# lock while the producer's emit was reading the same map, so the emit could
# miss `closed`. A race: this file narrows the window, it cannot pin it.
# Contract: exit 0 + last line PASS.
my @fail;
my $closings = Channel.new;

sub make-supply() {
    my $s = Supplier::Preserving.new;
    my $done = False;
    start { until $done { $s.emit: ++$ } }
    $s.Supply.on-close({ $closings.send('x'); $done = True })
}

for ^40 {
    my $s2 = make-supply;
    my @received;
    react {
        whenever $s2 -> $n {
            push @received, $n;
            done if $n >= 5;
        }
    }
    @fail.push("round $_: {@received.raku}") unless @received eqv [1, 2, 3, 4, 5];
}
$closings.close;
my $c = $closings.list.join;
@fail.push("on-close ran {$c.chars} times") unless $c eq 'x' x 40;

say @fail ?? "FAIL: @fail.head(3).join('; ')" !! "PASS";
exit @fail ?? 1 !! 0;
