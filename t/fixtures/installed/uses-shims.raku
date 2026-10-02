# NativeHelpers::{Blob,CStruct,Pointer} through their public API. Run by
# t/installed/run.raku with a binary that has no rakulib/ anywhere near it and a
# decoy dist of the same names on -I: every line below must come from the
# shims compiled into the binary.
use NativeCall;
use NativeHelpers::Blob;
use NativeHelpers::CStruct;
use NativeHelpers::Pointer;

my $b = Buf.new(72, 105, 33, 0);
say nativecast(Str, pointer-to($b));                          # the bytes C would see
say blob-from-pointer(pointer-to($b), :elems(3)).decode;     # and back again
my \typed = pointer-to($b, :typed);
say (typed + 1).deref;                                        # arithmetic in elements
my \c = carray-from-blob($b);
say c[0] + c[2];
say ptr-sized($b).list[1];

class Pair2 is repr('CStruct') { has int32 $.a is rw; has int32 $.b is rw }
my $la = LinearArray[Pair2].new(3);
$la[1].a = 7;
$la[1].b = 35;
my $s = nativecast(Pair2, pointer-to($la[1]));
say $s.a * $s.b;
say $*VM.config<nativecall_backend>;
