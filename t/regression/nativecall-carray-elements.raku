# Regression: NativeCall CArray element access — the bug that blocked
# certificate-verified HTTPS.
#   1. `$carray[$i] = v` REPLACED the whole byte-backed CArray with a fresh empty
#      Array. Every locally-built CArray therefore stopped being native the
#      moment you filled it in, and passing it to a native function handed C the
#      element COUNT where it wanted a pointer. That is what hung
#      ASN1_STRING_to_UTF8 (an `unsigned char **out` out-parameter) during X509
#      hostname verification.
#   2. Pointer-ish element types (Pointer, Str, a nested CArray) were sized as
#      int32; they are machine pointers.
#   3. Reading such an element gives something you can read THROUGH, so
#      `$out[0][^$n]` works after a native call writes a buffer address there.
#   4. A Range/list subscript slices.
# Contract: exit 0 + last line PASS.
use NativeCall;
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# 1. element assignment keeps the array native and round-trips every width
my $u8 = CArray[uint8].new;
$u8[$_] = 65 + $_ for ^5;
check((^5).map({ $u8[$_] }).join(','), '65,66,67,68,69', 'uint8-roundtrip');
check($u8.elems, '5', 'uint8-elems');

my $i32 = CArray[int32].new;
$i32[0] = -7; $i32[1] = 100000;
check("$i32[0],$i32[1]", '-7,100000', 'int32-roundtrip');
check($i32.elems, '2', 'int32-elems');

my $i64 = CArray[int64].new;
$i64[0] = 4294967296;          # needs the full 64 bits
check($i64[0], '4294967296', 'int64-roundtrip');

my $n64 = CArray[num64].new;
$n64[0] = 1.5; $n64[1] = -2.25;
check("$n64[0],$n64[1]", '1.5,-2.25', 'num64-roundtrip');

# construction from a list agrees with element assignment
my $made = CArray[int32].new(1, 2, 3);
check((^3).map({ $made[$_] }).join(','), '1,2,3', 'new-with-values');
check($made.elems, '3', 'new-with-values-elems');

# 2. a pointer element is pointer-sized, so one element is one pointer
my $ptr = CArray[Pointer].new;
$ptr[0] = Pointer;
check($ptr.elems, '1', 'pointer-elems');
my $pp = CArray[CArray[uint8]].new;
$pp[0] = CArray[uint8];
check($pp.elems, '1', 'nested-carray-elems');

# 3/4. slicing — how a native out-buffer is read back into a Buf
check($u8[^4].join(','), '65,66,67,68', 'range-slice');
check($u8[1, 3].join(','), '66,68',      'list-slice');
check($u8[^3].^name, 'Seq',             'slice-is-seq');
check(Buf.new($u8[^5]).decode('utf-8'), 'ABCDE', 'slice-into-buf');
check($u8[2], '67',                     'scalar-index-still-works');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
