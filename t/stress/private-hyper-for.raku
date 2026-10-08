# CONTRACT (PARALLEL-SCALING-PLAN P3), as private-start-capture.raku: the
# iterations of a `hyper for` store pointer-carrying values into a `my` of the
# routine and copy it, from several workers at once.
#
# The statement form parses as `do { for … }`, so the body is a closure of its
# own: `$v` is reached by name, which keeps the stripe in every mode, and this
# program passes under RAKUPP_PRIVATE_SLOTS=all too. It is here for the day
# the body becomes an inline block of the routine; then the sweep's hyper rule
# (resolvePads' ShareSweep) is what keeps `$v` shared, and `=all` crashes it.
my $M = (@*ARGS[0] // 60000).Int;
sub owner() {
    my $v = 'start';
    hyper for ^$M -> $i {
        $v = $i %% 2 ?? "h-$i" !! [1, 2, $i];
        my $c = $v;
        $c.defined;
    }
    'done'
}
say "survived: {owner()}";
say 'PASS';
