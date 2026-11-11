# Regression: `Str.match($rx, :c(N))` ignored N when the regex carried
# `:m` / `:ignoremark`, and searched from the start every time, so the
# usual walk `while $s.match($rx, :$c) { $c = $/.pos }` found its last
# match again forever. The `:m` path matches on mark-folded text and now
# maps the start position into it (issue #246).
# Every line answers the same under Rakudo 2026.09.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

sub walk($hay, $rx) {
    my int $c; my @at;
    while $hay.match($rx, :$c) {
        @at.push: $/.from;
        $c = $/.pos;
        last if @at > 10;
    }
    @at.List
}
check walk("nine", rx/ine/), (1,), 'plain';
check walk("nine", rx:m/ine/), (1,), ':m';
check walk("nïne nine", rx:m/ine/), (1, 6), ':m over a marked letter';
check walk("nine", rx:i:m/INE/), (1,), ':i:m';
check "nine".match(rx:m/ine/, :c(4)), Nil, ':c past the last match';
check "nine".match(rx:m/n/, :c(1)).from, 2, ':c skips an earlier match';
check ~"nïne".match(rx:m/ine/, :c(1)), 'ïne', 'the match keeps its mark';
check ~"ab".match(rx:m/b/, :p(1)), 'b', ':p at the match';
check "ab".match(rx:m/b/, :p(0)), Nil, ':p before it';
check "nïne".subst(rx:m/n/, "X", :c(1)), 'nïXe', '.subst(:c)';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
