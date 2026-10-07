class Counter {
    has $.count = 0;
    has @.history = [];
    method bump($step = 1) {
        $!count += $step;
        @!history.push($!count);
        self
    }
    method report { "count={$!count} history={@!history}" }
}
my $c = Counter.new;
$c.bump.bump(5);
say $c.report;
say $c.count;
