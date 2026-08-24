# NativeHelpers::Blob, the rakupp shadow. The ecosystem distribution of this
# name (zef:salortiz) reads MoarVM's REPR memory layout BY DESIGN — its
# MoarVM::Guts::REPRs scans object headers for a sentinel to find the data
# offset — which no other engine can satisfy (it would not run on JVM Rakudo
# either). What its dependents (the DBDish drivers first among them) actually
# use is the API below: a data pointer into a Blob/CArray, and bytes back out
# of a pointer. Those are engine primitives here, served by
# Rakupp::Internals::Blob; this file keeps the dist's exact surface.
#
# One departure, documented rather than hidden: pointer-to answers a pointer
# to the engine's byte storage. For buffers of 64 bytes and up that is the
# SAME memory the Blob reads (C writing through the pointer is visible);
# below 64 bytes the engine stores strings inline, so the pointer is to a
# stable COPY — fine for every read use, not a write-back channel.

unit module NativeHelpers::Blob;

use NativeCall;

our $debug = False;

my sub elem-type(\arr) {
    my \t = (try arr.of) // uint8;   # a bare Buf's element is the byte
    t === Mu || t === Any ?? uint8 !! t
}

multi sub pointer-to(Blob:D \blob, :$typed) is export {
    my \ptr = Rakupp::Internals::Blob.addr(blob);
    $typed ?? nativecast(Pointer[elem-type(blob)], ptr) !! ptr
}

multi sub pointer-to(array:D \arr, :$typed) is export {
    my \ptr = Rakupp::Internals::Blob.addr(arr);
    $typed ?? nativecast(Pointer[elem-type(arr)], ptr) !! ptr
}

multi sub pointer-to(CArray:D \arr, :$typed) is export {
    my \ptr = Rakupp::Internals::Blob.addr(arr);
    $typed ?? nativecast(Pointer[elem-type(arr)], ptr) !! ptr
}

multi sub sizeof(Blob:D \blob) { blob.bytes }

multi sub sizeof(Mu:D \arr) is export {
    arr.elems * nativesizeof(elem-type(arr))
}

sub ptr-sized(Mu:D \arr) is export {
    \(pointer-to(arr), sizeof(arr))
}

multi sub buf-sized(Blob:D \b) is export {
    \(b, b.bytes)
}

multi sub buf-sized(Str:D \s) is export {
    buf-sized(s.encode);
}

# back compatibility only, as in the dist
sub BPointer(Blob:D \blob, :$typed) is export {
    pointer-to(blob, :$typed);
}

sub carray-from-blob(Blob:D \blob, :$managed) is export {
    my \t = elem-type(blob);
    my $arr = CArray[t].new;
    $arr[blob.elems - 1] = 0 if blob.elems;   # force allocation to size
    $arr[$_] = blob[$_] for ^blob.elems;
    $arr
}

sub carray-is-managed(CArray:D \arr) is export {
    Rakupp::Internals::Blob.managed(arr)
}

sub blob-allocate(Blob:U \blob, $elems) is export {
    blob.allocate($elems.Int)
}

sub blob-from-pointer(Pointer:D \ptr, Int :$elems!, Blob:U :$type = Buf) is export {
    my \t = $type.of === Mu || $type.of === Any ?? uint8 !! $type.of;
    my $bytes = $elems * nativesizeof(t);
    Rakupp::Internals::Blob.read(ptr, $bytes, $type.^name)
}

sub utf8-from-pointer(Pointer:D \ptr, Int $size) is export {
    blob-from-pointer(ptr, :elems($size), :type(utf8));
}

sub blob-from-carray(CArray:D \arr, Int :$size) is export {
    my \t = elem-type(arr);
    my $elems = $size // arr.elems;
    die "Need :size for unmanaged CArray" without $elems;
    blob-from-pointer(pointer-to(arr), :$elems, :type(Buf[t]))
}

# vim: expandtab shiftwidth=4
