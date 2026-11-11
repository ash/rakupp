# Regression: issue #93 (2026-11-10).
#
#   `do { ... } while COND` ran the block as a Perl 5 do-while loop. Rakudo
#   refuses it at compile time with X::Obsolete, "Unsupported use of
#   do...while. In Raku please use: repeat...while or repeat...until".
#
# Expected answers are Rakudo 2026.09's (2026-11-10).

my $fails = 0;
sub check($got, $want, $what) {
    return if $got eqv $want;
    $fails++;
    say "not ok - $what\n  got:  {$got.raku}\n  want: {$want.raku}";
}

sub refusal(Str $code) {
    my $r = try EVAL $code;
    $! ?? ($!.^name, $!.message.words.join(' ').substr(0, 83)) !! ('ran', $r)
}

check refusal('my $i = 0; do { say "{$i++}"; } while $i < 3;'),
    ('X::Obsolete', 'Unsupported use of do...while. In Raku please use: repeat...while or repeat...until'), 'the report: do { } while';
check refusal('my $i = 0; do { $i++ } while $i < 3'),
    ('X::Obsolete', 'Unsupported use of do...while. In Raku please use: repeat...while or repeat...until'), 'without the semicolon';
check refusal('my $i = 0; do { $i++ } until $i > 3;'),
    ('X::Obsolete', 'Unsupported use of do...until. In Raku please use: repeat...while or repeat...until'), 'do { } until';

check refusal('my $i = 0; repeat { $i++ } while $i < 3; $i'), ('ran', 3), 'repeat ... while still runs';
check refusal('my $i = 0; repeat { $i++ } until $i >= 3; $i'), ('ran', 3), 'repeat ... until still runs';
check refusal('my $i = 0; $i++ while $i < 3; $i'), ('ran', 3), 'a statement modifier while';
check refusal('my $i = 0; (do { $i++ }) while $i < 3; $i'), ('ran', 3), 'a parenthesized do is a statement';
check refusal('my $i = 0; do $i++ while $i < 3; $i'), ('ran', 3), 'do on a non-block statement';

if $fails { say "$fails failure(s)"; exit 1 }
say 'PASS';
