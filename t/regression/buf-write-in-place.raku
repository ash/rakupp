# Regression: Buf's write-* methods write the storage every holder of the
# Buf shares, and its read-* methods copy nothing (issues #186, #178).
#
# Interpreter::bufBitOp took `buf.s.mut()` for every op. On a buffer another
# Value shared, that copied ALL of it first: `.read-uint64` on an 8 MB Buf
# cost 8 MB a call (#178), and a write landed in a private copy, so
# `my $c = $b; $c.write-uint16(…)` left `$b` as it was — though `$c[0] = 9`
# already reached it. A routine's write reached its caller only through the
# interpreter's post-call copy-back, which a natively compiled caller does not
# have (#186; t/aot/cases/buf-args covers the compiled side). Reads now look
# at the bytes where they are, and a write within the buffer goes in place.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

my $b = Buf.new(0 xx 8);
my $c = $b;
$c.write-uint16(0, 0x0201, LittleEndian);
ck $b.list, (1, 2, 0, 0, 0, 0, 0, 0), 'write-uint16 through a second holder';
$c.write-uint32(4, 0xDEADBEEF, BigEndian);
ck $b.list, (1, 2, 0, 0, 0xDE, 0xAD, 0xBE, 0xEF), 'write-uint32, big-endian';
$c.write-int8(2, -1);
ck $b[2], 255, 'write-int8';
$c.write-num32(0, 1.5e0);
ck $b.read-num32(0), 1.5e0, 'write-num32 / read-num32';
my $h = Buf.new(0 xx 16);
my $h2 = $h;
$h2.write-uint128(0, 1);
ck $h.read-uint128(0), 1, 'write-uint128';

# a routine's write reaches its caller's Buf
sub poke(Buf $x) { $x.write-uint16(0, 0x0403, LittleEndian) }
my $d = Buf.new(0, 0, 0);
poke($d);
ck $d.list, (3, 4, 0), "a routine's write-uint16 reaches the caller";

# a typed buf addresses elements (and needs offset + width of them, Rakudo's
# rule, or the write grows the buffer)
my $w = buf32.new(1, 2, 3, 4, 5);
my $w2 = $w;
$w2.write-uint32(1, 7, NativeEndian);
ck $w[0..2].List, (1, 7, 3), 'buf32: the offset counts elements';

# reads leave a shared buffer shared — and see what was written through it
my $big = buf8.allocate(1_000_000);
my $alias = $big;
$alias[999_999] = 42;
ck $big.read-uint8(999_999), 42, 'a read sees a write through another holder';
ck $big.read-uint64(0, NativeEndian), 0, 'read-uint64';

# a write past the end still grows the buffer that was written
my $g = Buf.new(1);
$g.write-uint16(2, 0x0605, LittleEndian);
ck $g.list, (1, 0, 5, 6), 'a write past the end grows the buffer';

# and a Blob stays immutable, the Str a Buf was made from untouched
my $s = 'a string long enough to share its storage';
my $e = Buf.new($s.encode('latin-1'));
$e.write-uint8(0, 65);
ck $s.substr(0, 1), 'a', 'the source Str is untouched';
ck $e[0], 65, '…the Buf made from it is written';

say 'PASS';
