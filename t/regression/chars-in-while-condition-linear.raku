# Regression: issue #133 (2026-11-10).
#
#   `$s.chars` in the condition of a block-form `while` cost time in
#   proportion to the string's length on every pass: 80k passes over a
#   400k-character string took 7.6 s (Rakudo 16 ms). And `$s = $s.substr(5)`
#   inside a block loop ran 4.5x slower than the statement-modifier form.
#
# Expected answers are Rakudo 2026.09's (2026-11-10). The output carries no
# timings. Each case runs at 10k and at 80k steps, best of three: eight times
# the work takes about eight times as long when the case is linear and about
# sixty-four when it is quadratic. A case fails only when its growth is past
# 24x AND the long run took 100 ms or more, so load slows both sizes without
# turning a linear case red, and a case too fast to time cannot fail on noise.

my $fails = 0;
sub check($got, $want, $what) {
    return if $got eqv $want;
    $fails++;
    say "not ok - $what\n  got:  {$got.raku}\n  want: {$want.raku}";
}

my @cases =
    'while $i < $n && $s.chars { $i++ }' => -> $n { my $s = 'abcde' x $n; my $i = 0; while $i < $n && $s.chars { $i++ }; $i },
    'while $s.chars && $i < $n { $i++ }' => -> $n { my $s = "abcde" x $n; my $i = 0; while $s.chars && $i < $n { $i++ }; $i },
    'until !$s.chars || ...'             => -> $n { my $s = 'abcde' x $n; my $i = 0; until !$s.chars || $i >= $n { $i++ }; $i },
    'while $s.chars { substr(5) }'       => -> $n { my $s = 'abcde' x $n; my $i = 0; while $s.chars { $s = $s.substr(5); $i++ }; $i },
    'for ^$n { $s = $s.substr(5) }'      => -> $n { my $s = "abcde" x $n; for ^$n { $s = $s.substr(5) }; $n - $s.chars },
    'loop { $s = $s.substr(5) }'         => -> $n { my $s = 'abcde' x $n; loop (my $i = 0; $i < $n; $i++) { $s = $s.substr(5) }; $i };

sub ms(&case, $n) {
    my $best = Inf;
    for ^3 { my $t = now; case($n); $best min= (now - $t) * 1000 }
    $best
}

for @cases -> $c {
    my $name = $c.key; my &case = $c.value;
    check case(10), 10, "$name: answers the step count";
    my $short = ms(&case, 10_000);
    my $long  = ms(&case, 80_000);
    my $quadratic = $long >= 100 && $long / max($short, 0.01) >= 24;
    check $quadratic, False, "$name: linear";
    note "  $name: {$short.fmt('%.1f')} ms at 10k, {$long.fmt('%.1f')} ms at 80k" if $quadratic;
}

if $fails { say "$fails failure(s)"; exit 1 }
say 'PASS';
