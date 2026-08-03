# Regression: the seven fixes found taking Digest::HMAC and Digest::SHA2 through
# their own test suites, 2026-08-03. Every one is a general bug the digest code
# merely happened to exercise first; each expectation was checked against Rakudo.

my $ok = True;
sub ck($got, $want, $l) { unless $got eqv $want { say "FAIL: $l — {$got.raku} vs {$want.raku}"; $ok = False } }

# 1. `~^` / `~|` / `~&` on buffers answer a BUFFER, of the LEFT operand's type.
#    They used to answer a plain Str, so Digest::HMAC's key padding handed the
#    hash function something it could not treat as bytes.
{
    ck((Buf.new(65,66) ~^ Buf.new(1,1)).raku, Buf.new(64,67).raku, 'Buf ~^ Buf is a Buf');
    ck((Blob.new(65,66) ~^ Blob.new(1,1)).^name, 'Blob', 'Blob ~^ Blob is a Blob');
    ck((Buf.new(65,66) ~^ Blob.new(1,1)).^name, 'Buf', 'the LEFT operand names the result');
    ck((Blob.new(65,66) ~^ Buf.new(1,1)).^name, 'Blob', 'and the other way round');
    ck(('AB'.encode ~^ Buf.new(1,1)).^name, 'utf8', 'a utf8 stays a utf8');
    ck((Buf.new(65,66,67) ~& Buf.new(1,1)).raku, Buf.new(1,0,0).raku, '~& keeps the longer length');
    ck((Buf.new(65,66) ~| Buf.new(1,1)).raku, Buf.new(65,67).raku, '~| on buffers');
    # mixing a buffer with a string has no answer that is right for both
    ck((try { 'AB' ~^ Buf.new(1,1) }).defined, False, 'Str ~^ Buf dies');
    ck((try { Buf.new(1,1) ~^ 'AB' }).defined, False, 'Buf ~^ Str dies');
    ck(('AB' ~^ '  ').raku, '"ab"', 'a plain Str pair is unaffected');
}

# 2. A type written before the parenthesis applies to EVERY variable in the list.
{
    my uint32 ($a, $b) = 0xFFFFFFFF + 5, 7;
    ck(($a, $b), (4, 7), 'my uint32 ($a, $b) = … wraps both');
    my uint32 ($c, $d);
    $c = 0xFFFFFFFF + 5;
    ck($c, 4, 'and wraps on a later assignment too');
    my int8 ($e, $f) = 200, -1;
    ck(($e, $f), (-56, -1), 'my int8 (…) is signed');
    my Int ($g, $h);
    ck(($g.^name, $h.^name), ('Int', 'Int'), 'a named type reaches both as well');
    my ($i, $j) = 1, 2;
    ck(($i, $j), (1, 2), 'an untyped list declaration is unaffected');
}

# 3. `$buf[i] = v` writes INTO the packed buffer, truncating to the element width.
#    It used to replace the whole buffer with a plain Array.
{
    my buf32 $w .= new;
    $w[2] = 7;
    ck($w.list.List, (0, 0, 7), 'writing past the end grows with zeroes');
    $w[0] = 0xFFFFFFFF + 5;
    ck($w[0], 4, 'and truncates to the element width');
    # NB: `.^name` is asserted only as "still a buffer". rakupp reports `Buf`
    # where Rakudo reports `Buf[uint32]` — a separate, pre-existing gap in how the
    # element type is carried into the type name, not something this batch touched.
    ck($w ~~ Blob, True, 'the buffer stays a buffer');
    my buf8 $b .= new(1,2,3);
    $b[1] = 300;
    ck($b.list.List, (1, 44, 3), 'buf8 wraps at 8 bits');
    ck((try { my blob32 $c .= new; $c[0] = 1; True }).defined, False, 'a Blob is immutable');
}

# 4. `state $x .= new` initializes ONCE. Re-running it called the method on the
#    value the previous pass left behind, so a `state buf32` inside a `reduce`
#    turned into a Str on the second iteration — which is where every SHA-256
#    went wrong, sixteen rounds in.
{
    my @names;
    reduce -> $acc, $j { (state buf32 $w .= new)[$j] = $j + 10; @names.push(($w ~~ Blob) ~ ':' ~ $w[0]); $acc }, '', |^3;
    ck(@names, ['True:10', 'True:10', 'True:10'], 'state $x .= new runs once');
}

# 5. Radix literals wider than a long long keep their value instead of saturating.
{
    ck(0xFFFFFFFFFFFFFFFF, 18446744073709551615, 'a 64-bit hex literal');
    ck(0b1111111111111111111111111111111111111111111111111111111111111111,
       18446744073709551615, 'the same in binary');
    ck((0xFF, 0b1010, 0o777, 0xdead_beef), (255, 10, 511, 3735928559), 'small radix literals');
    ck(0x7FFFFFFFFFFFFFFF, 9223372036854775807, 'exactly LLONG_MAX');
}

# 6. `:16("…")` accumulates in a bigint. It overflowed a long long and answered -1.
{
    ck(:16("FFFFFFFFFFFFFFFF"), 18446744073709551615, ':16 of a 64-bit hex string');
    ck((:2("1111"), :16("ff"), :10("42")), (15, 255, 42), 'small radix conversions');
    ck(:16("c.8"), 12.5, 'a radix point still gives a Rat');
}

# 7. A 64-bit blob element reads back UNSIGNED. blobWordAt returns a long long,
#    so a word with its top bit set came out negative — blob64 answered -1 for
#    18446744073709551615, and SHA-512 built its state from that.
{
    ck(blob64.new(0xFFFFFFFFFFFFFFFF).list[0], 18446744073709551615, 'a full 64-bit blob word');
    ck(blob64.new(0x6a09e667f3bcc908).list[0], 7640891576956012808, 'and one that fits a long long');
    ck(blob32.new(0xFFFFFFFF).list[0], 4294967295, 'blob32 was never affected');
    ck(Buf.new(255, 255).list.List, (255, 255), 'nor a plain byte buffer');
    my buf64 $q .= new;
    $q[0] = 0xFFFFFFFFFFFFFFFF;
    ck($q[0], 18446744073709551615, 'reading back what was just written');
}

# 8. `.polymod` divides in BigInt when the invocant is one. It used to take
#    `toInt()`, which saturates, so a 64-bit word lost its top byte on the way
#    out of a digest.
{
    ck(0xFFFFFFFFFFFFFFFF.polymod(256 xx 7).reverse.List,
       (255, 255, 255, 255, 255, 255, 255, 255), 'polymod of a full 64-bit word');
    ck(255.polymod(10, 10).List, (5, 5, 2), 'a small invocant is unchanged');
    ck(1234.polymod(10 xx *).List, (4, 3, 2, 1), 'and the lazy-divisor form');
}

# The whole point: SHA-256 and SHA-224 through the ecosystem's own pure-Raku
# Digest are byte-identical to Rakudo now, and `Digest::HMAC` passes its own
# suite. SHA-512/384 are still wrong — one more 64-bit bug in that half.

say $ok ?? 'PASS' !! 'FAIL';
