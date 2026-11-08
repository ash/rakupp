# A Whatever held in a variable keeps its type as a Pair's key: `my $w = *;
# ($w => 1).raku` was `"*" => 1`, its key the Str "*", where Rakudo keeps the
# Whatever and writes `* => 1`. The Pair now keeps the key as it does an Int
# or a type object, so `.key === $w` holds. (A written `*` there still curries.)
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

my $w = *;
ck ($w => 1).raku, '* => 1', '.raku';
ck ($w => 1).key.^name, 'Whatever', 'the key';
ck ($w => 1).key === $w, True, 'the same Whatever';
ck Pair.new($w, 2).raku, '* => 2', 'Pair.new';
ck ((1 => 2) => 3).raku, '(1 => 2) => 3', 'a Pair as a key';
ck (Int => 1).key.^name, 'Str', 'a bare name before => is still quoted';

say 'PASS';
