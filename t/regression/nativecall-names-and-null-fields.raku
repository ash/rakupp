# Regression, 2026-10-10: NativeCall gaps TODO.md listed (found with
# Math::SparseMatrix::Native and #136), each answered as Rakudo 2026.09 does:
#   - `.^name` and a type object's `.raku` carry the NativeCall::Types package
#   - a NULL Pointer or CArray CStruct field reads as the type object
#   - a CArray stored in a CStruct field keeps its length when read back, and
#     the view still writes through to the shared memory
#   - a `CArray[Pointer]` element is a Pointer (NULL included), not a view
#   - a Pointer's `.gist` and `.raku` name the package
use Test;
use NativeCall;
plan 15;

is CArray[int32].^name, 'NativeCall::Types::CArray[int32]', 'CArray[int32].^name';
is CArray[int32].new(1).^name, 'NativeCall::Types::CArray[int32]', '…of an instance';
is Pointer.^name, 'NativeCall::Types::Pointer', 'Pointer.^name';

class S is repr('CStruct') {
    has CArray[int32] $.arr is rw;
    has Pointer $.p;
    submethod TWEAK(CArray[int32] :$a) { $!arr := $a if $a.defined }
}
my $empty = S.new;
nok $empty.p.defined, 'a NULL Pointer field is the type object';
is $empty.p.raku, 'NativeCall::Types::Pointer', '…and says so';
nok $empty.arr.defined, 'a NULL CArray field is the type object';

my $c = CArray[int32].new(1, 2, 3);
my $s = S.new(a => $c);
is $s.arr.elems, 3, 'a stored CArray keeps its length';
is $s.arr[2], 3, '…and its elements';
$s.arr[0] = 9;
is $c[0], 9, '…and a write through the field reaches it';

my $cp = CArray[Pointer].new(Pointer.new(0), Pointer.new(8));
is $cp[1].^name, 'NativeCall::Types::Pointer', 'a CArray[Pointer] element is a Pointer';
is $cp[1].Int, 8, '…at its address';
ok $cp[0].defined, '…and a NULL one is Pointer.new(0)';

is Pointer.new(16).gist, 'NativeCall::Types::Pointer<0x10>', 'Pointer.gist';
is Pointer.new(0).gist, 'NativeCall::Types::Pointer<NULL>', '…of NULL';
class Handle is repr('CPointer') { }
is Pointer[Handle].new.raku, 'NativeCall::Types::Pointer[Handle].new(0)', 'Pointer[T].new.raku';
