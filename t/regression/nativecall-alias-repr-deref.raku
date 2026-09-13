# Regression: four gaps between a dist and a C library, all found by installing
# freetype and following Font::FreeType down its own start-up path. Thirteen
# dists sit behind that one.
#
#  * a FORWARD DECLARATION's representation was lost. `class FT_Library is
#    repr('CPointer') {...}` up top, `class FT_Library { … }` with the methods
#    below — and the body installs a fresh class over the stub, so the type
#    became P6opaque. C types that reference each other are always declared this
#    way.
#  * a `constant` TYPE ALIAS was not resolved in a native signature. Font::FreeType
#    writes `constant FT_Int is export = int32` and then `FT_Int is rw`
#    everywhere; the parameter was not a native scalar at all, so nothing was
#    ever written back and the library version read as the empty string.
#  * an `is rw` Pointer write-back dropped the ARGUMENT's element type. A
#    signature says a bare `Pointer is rw`, because C only wants somewhere to put
#    a pointer; it is the CALLER who passed a `Pointer[FT_Library]` and who knows
#    what is on the other end.
#  * `.deref` on a `Pointer[T]` for a native class answered a raw Int, which
#    failed the receiving attribute's type check.
#
# The rows below use libc's `time`, which every platform this runs on has, so
# the file needs no third-party library to prove any of it.
#
# Runs clean under Rakudo too.

use NativeCall;

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# ---- a stub's representation survives the body --------------------------
class Stubbed is repr('CPointer') {...}
class Stubbed {
    method tag() { 'body' }
}
ck Stubbed.REPR, 'CPointer', "a forward declaration's repr survives its body";
ck Stubbed.tag, 'body', '…and the body is still the body';

# a class with no stub and no repr is unaffected
class Plain { method tag() { 'plain' } }
ck Plain.REPR, 'P6opaque', 'a class with no repr is still P6opaque';

# ---- a `constant` alias is a native type in a signature -----------------
# `time(time_t *)` writes the current time through its argument AND returns it.
# The two must agree: an alias that failed to resolve wrote nothing back, and
# the slot kept its zero.
constant MyTime = int64;
sub time(MyTime is rw --> MyTime) is native { * }
my MyTime $slot = 0;
my $returned = time($slot);
ck ($returned > 0), True, 'a native call through an aliased return type works';
ck ($slot == $returned), True,
   'an `is rw` parameter typed by a `constant` alias is written back';

# ---- a Pointer keeps what it points AT ----------------------------------
class Handle is repr('CPointer') { }
my $p = Pointer[Handle].new;
ck $p.raku.contains('Pointer[Handle]'), True, 'Pointer[T] knows its element type';

# ---- …and deref does not answer a raw Int -------------------------------
# A NULL pointer derefs to a FAILURE on both engines, so this row needs no
# library. The bug it pins is that we used to hand back a plain Int here — which
# is defined, truthy, and passed straight into whatever expected a T.
my $d = $p.deref;
ck ($d ~~ Int), False, 'Pointer[T].deref does not answer a raw Int';
ck ($d ~~ Failure), True, '…a NULL deref is a Failure';
$d.so;  # mark it handled, so it does not detonate at exit

say $fails ?? "\n$fails FAILED" !! "\nPASS";
exit $fails ?? 1 !! 0;
