# Native bodies subscripting CArrays, a Buf and AT-POS/AT-KEY objects (t/aot/run.raku).
#aot-expect: bundled
use NativeCall;
use Aot::Subscripts;

class Holder is repr('CStruct') {
    has CArray[num64] $.v;
    submethod BUILD { $!v := CArray[num64].new(7e0, 8e0, 9e0) }
}
my $h = Holder.new;     # kept alive: its field is a view of this struct's memory

say copy-out(CArray[int32].new(4, 5, 6), 3);
say copy-out($h.v, 3);
say second-byte(Buf.new(10, 20, 30));
class P { method AT-POS($i) { "pos $i" } }
class K { method AT-KEY($k) { "key $k" } }
say pos-of(P.new);
say key-of(K.new);
