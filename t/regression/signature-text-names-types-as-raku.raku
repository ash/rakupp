# A signature's text names each type as the type's `.raku` does (issue #220's
# follow-up, 2026-11-07). `sub f(Pointer $x)` rendered `:(Pointer $x)` where
# Rakudo, and `.params[0].type.raku` here, say NativeCall::Types::Pointer: the
# type was right, its rendering short. A program's own `class Pointer` keeps
# its own name.
#
# Found beside it, both rendering and checking:
#   - a `--> Int:D` after a parameter, `($p --> Int:D)`, lost its smiley. The
#     signature read `($p --> Int)` and the routine returned the Int type
#     object Rakudo refuses; only a `-->` with no parameter before it kept it.
#   - a coercion's smiley belongs to its target and is written first:
#     `Str:D(Int) $y` rendered `Str(Int):D $y`, and `--> Str:D(Int)` dropped
#     its source, `--> Str()` its `Any`.
#
# Contract: exit 0 + last line PASS. Every answer is Rakudo 2026.09's — run
# this file under `rakudo` too.
use NativeCall;
my @fail;
sub check($what, Mu $got, Mu $want) { @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want }

# the follow-up's two routines
sub f(Pointer $x) { }
sub g(Pointer $x, CArray[uint32] $y) { }
check 'f', &f.signature.raku, ':(NativeCall::Types::Pointer $x)';
check 'g', &g.signature.raku, ':(NativeCall::Types::Pointer $x, NativeCall::Types::CArray[uint32] $y)';
check '…and .type.raku agrees', &f.signature.params[0].type.raku, 'NativeCall::Types::Pointer';
check 'a Parameter on its own', &f.signature.params[0].raku, 'NativeCall::Types::Pointer $x';
check '…and its .gist', &g.signature.params[1].gist, 'NativeCall::Types::CArray[uint32] $y';

sub h(Pointer[int32] $p, Str $s --> Pointer) { }
check 'a parameterized Pointer and a return type', &h.signature.raku,
      ':(NativeCall::Types::Pointer[int32] $p, Str $s --> NativeCall::Types::Pointer)';
sub k(Pointer:D $p --> Pointer:D) { }
check 'with smileys', &k.signature.raku, ':(NativeCall::Types::Pointer:D $p --> NativeCall::Types::Pointer:D)';
check 'the type itself is still Pointer', &f.signature.params[0].type === Pointer, True;
check 'a :(…) literal', :(Pointer $x, Int $y).raku, ':(NativeCall::Types::Pointer $x, Int $y)';

# a `--> T:D` after a parameter keeps its smiley, and is checked
sub j($p --> Int:D) { Int }
check 'rendered', &j.signature.raku, ':($p --> Int:D)';
check 'checked', (try j(1)) // $!.message, 'Type check failed for return value; expected Int:D but got Int (Int)';
my &b = -> $x --> Int:U { 1 };
check 'a pointy block', &b.signature.raku, ':($x --> Int:U)';
check '…checked', (try b(1)) // $!.message, 'Type check failed for return value; expected Int:U but got Int (1)';
sub fine($p --> Int:D) { 3 }
check 'a defined value passes', fine(1), 3;

# a coercion: the target's smiley, then its source
sub c(Str() $x, Str:D(Int) $y, Int(Str) $z, Int:D(Str:D) $w, Int:U() $u) { }
check 'coercion parameters', &c.signature.raku, ':(Str(Any) $x, Str:D(Int) $y, Int(Str) $z, Int:D(Str:D) $w, Int:U(Any) $u)';
check '…one of them', &c.signature.params[1].raku, 'Str:D(Int) $y';
sub co(--> Str:D(Int)) { 4 }
sub cp(--> Str(Int)) { 4 }
sub cq(--> Str()) { 4 }
sub cr($a --> Int:D(Str:D)) { 1 }
check '--> Str:D(Int)', &co.signature.raku, ':( --> Str:D(Int))';
check '--> Str(Int)', &cp.signature.raku, ':( --> Str(Int))';
check '--> Str()', &cq.signature.raku, ':( --> Str(Any))';
check '--> Int:D(Str:D) after a parameter', &cr.signature.raku, ':($a --> Int:D(Str:D))';
check '…and it still coerces', co().raku, '"4"';

.say for @fail;
say @fail ?? "FAIL ({+@fail})" !! 'PASS';
exit @fail ?? 1 !! 0;
