# Regression: NativeCall's `OpaquePointer` is another name for `Pointer`
# (Rakudo: `OpaquePointer === Pointer`). Raku++ did not know the name, so a
# signature using it died "Invalid typename 'OpaquePointer' in parameter
# declaration" whenever the code was compiled from source — Compress::Zlib's
# `sub inflateBack(z_stream, Callable, OpaquePointer, Callable, OpaquePointer)`
# among them, which loaded only from a precompiled copy (so the first run after
# every rebuild failed). The lexer now spells it `Pointer`, except as a
# method name.
#
# Expectations checked against Rakudo 2026.09 via /opt/homebrew/bin/rakudo.

use NativeCall;

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

sub take-it(OpaquePointer $p --> OpaquePointer) { $p }
sub native-decl(OpaquePointer, Callable, OpaquePointer) returns int32 is native { * }
class Holder { method OpaquePointer { 'a method of that name' } }

ck(OpaquePointer === Pointer, True, 'OpaquePointer is Pointer');
ck(take-it(Pointer) === Pointer, True, 'a parameter and a return type');
ck(&native-decl.signature.params.elems, 3, 'a native sub declared with it');
{ my OpaquePointer $p; ck($p === Pointer, True, 'a typed variable') }
ck(Holder.new.OpaquePointer, 'a method of that name', 'a method of that name is not renamed');

say $fails ?? "FAILED $fails" !! "PASS";
