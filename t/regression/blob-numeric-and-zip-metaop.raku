# Found driving Digest::HMAC in the v2 module battery: the probe's hash was wrong,
# and unwound into four independent general bugs. The module is a good witness
# because it exercises all four in two lines:
#
#   if +$key < $block-size { $key ~= Blob.new: 0 xx ($block-size - +$key) }
#   reduce -> $m, $i { &hash(blob8.new(@$key Z[+^] $i xx *) ~ $m) }, $msg, 0x36, 0x5c
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

# 1. A Blob is Positional, so numeric context is its ELEMENT COUNT. This threw
#    "Cannot convert string to number", reading the bytes as the text "key".
my $k = "key".encode;
check(+$k, 3, 'a Blob numifies to its element count');
check(+Blob.new(1, 2, 3, 4), 4, 'and so does a plain Blob');

# 2. Blob ~ Blob is a Buf of the bytes — NOT a Str, and never NFC-normalized.
my $cat = $k ~ Blob.new(0, 0, 0);
check($cat.elems, 6, 'concatenating two Blobs keeps every byte');
check($cat.list.List, (107, 101, 121, 0, 0, 0), 'and the bytes are untouched');
check($cat.^name, 'Buf', 'the result is a Buf');
my $app = "key".encode;
$app ~= Blob.new(0, 0, 0);
check($app.list.List, (107, 101, 121, 0, 0, 0), '~= appends bytes too');
check($app.^name, 'Buf', 'and yields a Buf');

# 3. `Z[op]` is the zip metaop with a BRACKETED operator. The brackets used to
#    parse as a REDUCTION of the right operand, so `(1,2,3) Z[+] (10,20,30)`
#    answered ((1, 60),) — one pair, holding the sum of the whole right side.
check(((1,2,3) Z[+]  (10,20,30)).List, (11, 22, 33), 'Z[+] zips with +');
check(((1,2,3) Z[+^] (10,20,30)).List, (11, 22, 29), 'Z[+^] zips with +^');
check(((1,2,3) Z+    (10,20,30)).List, (11, 22, 33), 'the unbracketed form still works');

# 4. Zipping against a LAZY list only saw its materialized prefix — one element.
check(((1,2,3) Z[+] (7 xx *)).List, (8, 9, 10), 'a zip against an infinite list stops at the shortest');
check(((7 xx *) Z[+] (1,2)).List,   (8, 9),     'and on either side');

# …and a Z=> key keeps its type rather than being stringified.
check(((1,2) Z=> (3,4)).List, (1 => 3, 2 => 4), 'Z=> keeps an Int key');

# The composite: HMAC's ipad step, which needs all four fixes at once. Digest::SHA1
# is not vendored here, so this pins the padded-and-XORed KEY BLOCK rather than the
# final digest — that block is what rakupp was getting wrong. (The full chain is
# verified in the module battery: Digest::HMAC now matches Rakudo and Python's
# hmac byte for byte.)
my $key = "key".encode;
$key ~= Blob.new: 0 xx (64 - +$key);
check($key.elems, 64, 'the key pads to the block size');
my @ipad = @$key Z[+^] 0x36 xx *;
check(@ipad.elems, 64, 'the ipad XOR covers the whole block');
check(@ipad[^6].List, (93, 83, 79, 54, 54, 54), 'and produces the reference bytes');

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
