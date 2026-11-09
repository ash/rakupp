# `.flat` keeps an itemized Hash whole, as it keeps an itemized List or
# Array: `my $h = %(b => 2); ($h, 1).flat` was (:b(2), 1), the Hash spread
# into its pairs, where Rakudo answers (${:b(2)}, 1). A Hash a list holds
# bare still spreads.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

my %h = b => 2;
my $hs = %h;
ck ($hs, 1).flat.raku, '(${:b(2)}, 1).Seq', 'a Hash in a $ variable stays whole';
ck ($%h, 1).flat.raku, '(${:b(2)}, 1).Seq', '…and so does $%h';
ck ($hs, 1).flat.map(*.^name).List, ('Hash', 'Int'), 'its elements';
ck ((1, 2), $%h).map({ $_ }).flat.raku, '(1, 2, ${:b(2)}).Seq', 'after a map';
ck (%h, 1).flat.raku, '(:b(2), 1).Seq', 'a bare Hash spreads';
ck ({b => 2}, 1).flat.raku, '(:b(2), 1).Seq', '…and so does a literal one';
ck flat($hs, 1).raku, '(${:b(2)}, 1).Seq', 'the flat sub, as before';

say 'PASS';
