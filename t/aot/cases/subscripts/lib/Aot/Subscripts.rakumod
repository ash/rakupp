# Subscripts a native body hands to rtIndexGet on values that are neither an
# Array nor a Hash: a NativeCall CArray (owned, or live over C memory), a Buf,
# and an object with its own AT-POS / AT-KEY. Each read back the missing-element
# default here — NativeHelpers::Array's copy-to-array returned all Nils, so
# Math::SparseMatrix::Native printed every matrix as zeros under --exe.
unit module Aot::Subscripts;
use NativeCall;

sub copy-out(CArray $c, Int $n --> Array) is export {   #aot: native
    my @a;
    @a[$_] = $c[$_] for ^$n;
    @a
}
sub second-byte(Blob $b) is export {   #aot: native
    $b[1]
}
sub pos-of($o) is export {   #aot: native
    $o[2]
}
sub key-of($o) is export {   #aot: native
    $o<k>
}
