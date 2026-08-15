# Regression: prefix `|` on a Blob/Buf must slip ELEMENTS (bytes, or words
# for blob32), not the buffer as one item. `for blob32 { }` was already
# fixed; `|blob32` in argument position was not. The Rosetta MD5 golf
# emits with `reduce { $^buf.write-uint32: … }, buf8.new, |$state`, so a
# 4-word blob32 wrote ONE word — the element count 4 — and every digest
# was `04000000` instead of 16 bytes.
# Contract: exit 0 + last line PASS.
my @fail;

# 1. argument-position slip (the golf's last reduce)
sub collect(*@a) { @a }
@fail.push("arg blob32: {collect(|blob32.new(1, 2, 3, 4))}")
    unless collect(|blob32.new(1, 2, 3, 4)).List eqv (1, 2, 3, 4);
@fail.push("arg blob8: {collect(|Blob.new(9, 8))}")
    unless collect(|Blob.new(9, 8)).List eqv (9, 8);
my $held = blob32.new(5, 6, 7);
@fail.push("arg scalar: {collect(|$held)}")
    unless collect(|$held).List eqv (5, 6, 7);   # `|` slips even from a scalar

# 2. the exact emit shape the golf uses
my $state = blob32.new(0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476);
my $out = reduce { $^buf.write-uint32: $buf.end + 1, $^int, LittleEndian },
    buf8.new, |$state;
@fail.push("reduce |blob32 elems: {$out.elems}") unless $out.elems == 16;
@fail.push("reduce |blob32 bytes: {$out.list».fmt('%02x').join}")
    unless $out.list».fmt('%02x').join eq '0123456789abcdeffedcba9876543210';

# 3. Unary `|` is a Slip of the elements (list-literal / assignment consumers)
@fail.push("unary elems: {(|blob32.new(1, 2, 3)).elems}")
    unless (|blob32.new(1, 2, 3)).elems == 3;
@fail.push("list-lit: {(|blob32.new(10, 20), 30)}")
    unless (|blob32.new(10, 20), 30).List eqv (10, 20, 30);
my @bound := |blob32.new(1, 2);
@fail.push("bind: {@bound}") unless @bound.List eqv (1, 2);

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' }
else     { say 'PASS' }
