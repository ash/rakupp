# Regression: `.Slip` of a gather nothing had pulled from yet spliced NOTHING
# into a map or for: `(gather { take 1; take 2 },).map(*.Slip)` was `()`.
# `.Slip` handed back the Seq's still-empty buffer, and what splices a Slip
# reads that buffer. Graph's neighborhood-graph collects one `gather {…}.cache`
# per vertex and flattens them with `.values.map(*.Slip)`, so every
# neighbourhood came back empty (issue #47: Graph would not install).
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

check (gather { take 1; take 2 },).map(*.Slip).List, (1, 2), 'map(*.Slip)';
check (gather { take 1; take 2 }.cache,).map({ .Slip }).List, (1, 2), 'a cached gather';
check (for (gather { take 1; take 2 },) { .Slip }).List, (1, 2), 'for';

sub nb() {
    my %n;
    for <a b> -> $v { %n{$v} = gather { take $v; take "$v!" }.cache }
    %n.values.map(*.Slip).sort.List
}
check nb(), <a a! b b!>, 'the neighborhood-graph shape';

# an endless source stays lazy
check (gather { take $_ for 1..* }).Slip[^3].List, (1, 2, 3), 'endless gather';
check (1..Inf).Slip[^3].List, (1, 2, 3), 'endless range';

if @fail { die "FAIL:\n" ~ @fail.join("\n") }
say "PASS";
