# Module routines for t/regression/exe-module-buf-and-bind-issues-160-161-200.raku:
# compiled to native bodies under --exe (src/AotModules.h).
unit module RakuppIssues160To200;
use NativeCall;

sub libc { $*KERNEL.name eq 'darwin' ?? '/usr/lib/libSystem.B.dylib' !! $*DISTRO.is-win ?? 'msvcrt' !! 'libc.so.6' }
sub malloc(size_t --> Pointer) is native(&libc) {*}
sub free(Pointer) is native(&libc) {*}
sub memcpy-to-buf(Buf, Pointer, size_t --> Pointer) is native(&libc) is symbol('memcpy') {*}
sub memcpy-from-blob(Pointer, Blob, size_t --> Pointer) is native(&libc) is symbol('memcpy') {*}
sub memset-buf(Buf, int32, size_t --> Pointer) is native(&libc) is symbol('memset') {*}

# #160: what C writes into a Buf argument reaches the routine's Buf
sub buf-from-c(--> Buf) is export {
    my $p = malloc(4);
    memcpy-from-blob($p, Blob.new(1, 2, 3, 4), 4);
    my Buf $b = Buf.allocate(4);
    memcpy-to-buf($b, $p, 4);
    free($p);
    $b
}
sub buf-untyped-from-c() is export {
    my $b = Buf.allocate(3);
    memset-buf($b, 7, 3);
    $b.list
}
sub buf-arg-from-c(Buf $b) is export {
    memset-buf($b, 0xAB, $b.elems);
    $b.list
}

# #161: an element store keeps a Buf in an untyped variable a Buf
sub buf-store() is export { my $b = Buf.new(1, 2, 3); $b[0] = 9; ($b.^name, $b.list) }
sub buf-from-blob() is export { my $c = Buf.new(Blob.new(4, 5)); ($c.^name, $c.list) }

# #200: `my @a := <expr>[0]` binds the element, not a list wrapping it
sub aoa() { [[10, 20, 30],] }
sub state-aoa() { state @t = [[10, 20, 30],]; @t }
sub v1(--> Str) is export { my @a := aoa()[0]; "@a.elems() @a[1].raku()" }
sub v2(--> Str) is export { my @a := state-aoa()[0]; "@a.elems() @a[1].raku()" }
sub v3(--> Str) is export { my @outer = [10, 20, 30],; my @a := @outer[0]; "@a.elems() @a[1].raku()" }
sub v4(--> Str) is export { my $x = aoa()[0]; "{$x.elems} {$x[1].raku}" }
sub sbox(Int $i) is export { my @s := state-aoa()[0]; @s[$i] }
