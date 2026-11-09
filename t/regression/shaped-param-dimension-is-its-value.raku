# A shaped parameter's literal dimension is the literal's VALUE: the parser
# read the token's spelling with stoll, so `@a[0x10]` was a dimension of 0
# and `@a[1_0]` of 1, and one past an int64 — `sub f(@a[99999999999999999999])`
# — ended the compile with "Internal error: stoll: out of range". Found by
# the review of src/ (docs/dev/findings/src-review-2026-11-09.md, G2). Every
# line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

sub hex(@a[0x10]) { 'bound' }
sub under(@a[1_0]) { 'bound' }
sub three(@a[3]) { 'bound' }
sub huge(@a[99999999999999999999]) { 'bound' }

my @sixteen[16]; my @ten[10]; my @four[4]; my @three[3];
ck hex(@sixteen), 'bound', 'a hexadecimal dimension';
ck under(@ten), 'bound', 'one written with an underscore';
ck three(@three), 'bound', 'a plain one';
ck (try three(@four)) // 'refused', 'refused', 'a different shape is refused';
ck (try hex(@ten)) // 'refused', 'refused', '…by the hexadecimal one too';
ck (try huge(@three)) // 'refused', 'refused', 'a dimension past an int64 compiles, and matches nothing';

say 'PASS';
