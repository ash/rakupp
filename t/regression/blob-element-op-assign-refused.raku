# `$blob[0] += 1` is refused, as `$blob[0] = 2` is: the in-place lane for
# `$buf[$i] op= v` took a Blob as well as a Buf, so an immutable Blob was
# changed — `my $b = Blob.new(1,2); $b[0] += 1; say $b` said
# Blob:0x<02 02>. Found by the review of src/
# (docs/dev/findings/src-review-2026-11-09.md, A9). Rakudo 2026.09 refuses
# with X::Assignment::RO too (its message names the Int; ours the Blob, as
# our `=` does), and every other line answers the same there.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

sub refusal(&code) {
    code();
    return 'no refusal';
    CATCH { default { return .^name } }
}

my $blob = Blob.new(1, 2);
ck refusal({ $blob[0] += 1 }), 'X::Assignment::RO', '+= on a Blob element';
ck refusal({ $blob[1] *= 3 }), 'X::Assignment::RO', '*=';
ck $blob.list, (1, 2), '…and the Blob is as it was';

my $buf = Buf.new(1, 2);
$buf[0] += 1; $buf[1] *= 3;
ck $buf.list, (2, 6), 'a Buf takes both, as before';

say 'PASS';
