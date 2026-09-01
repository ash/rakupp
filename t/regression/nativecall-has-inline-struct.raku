# Regression: `HAS` — a CStruct member laid out IN PLACE.
#
# C's `struct Outer { struct Inner in; int z; }` is spelled `HAS Inner $.in`,
# against `has Inner $.in` for the pointer form `struct Inner *in`. The engine
# knew only `has`, so the declarator parsed as an expression and the class died
# with "Variable $.in used where no 'self' is available" — every binding whose
# header nests a struct by value was unusable.
#
# What has to hold: the outer struct's SIZE and its members' OFFSETS account for
# the inlined struct's real width and alignment, the accessor hands back a view
# ONTO those bytes (so a write through it lands in the outer struct's memory),
# and a plain `has` of the same type still stores a pointer.
#
# Written to pass under Rakudo too — except one deliberate divergence, marked
# below. Contract: exit 0 + last line PASS.
use NativeCall;
my @fail;
sub check($ok, $what) { @fail.push($what) unless $ok }

class Three is repr('CStruct') { has int32 $.x is rw; has int32 $.y is rw; has int32 $.z is rw; }
class Inline is repr('CStruct') { has int8 $.tag is rw; HAS Three $.t; has int8 $.end is rw; }
class ByPtr  is repr('CStruct') { has int8 $.tag is rw; has Three $.t;  has int8 $.end is rw; }

# 1. layout: the inline member is 12 bytes aligned to 4, the pointer one is 8 to 8
check(nativesizeof(Three)  == 12, 'sizeof the inner struct');
check(nativesizeof(Inline) == 20, 'HAS lays the struct out in place');
check(nativesizeof(ByPtr)  == 24, 'has of the same type is still a pointer');

# 2. the view writes through to the OUTER struct's memory, at the right offset
my $o = Inline.new;
$o.tag = 1; $o.end = 2;
$o.t.x = 10; $o.t.y = 20; $o.t.z = 30;
check($o.t.x == 10 && $o.t.y == 20 && $o.t.z == 30, 'the inline members read back');
check($o.tag == 1 && $o.end == 2, 'and did not overwrite their neighbours');

# 3. C sees the same bytes: read the whole struct back through a Pointer
my $raw = nativecast(CArray[int32], nativecast(Pointer, $o));
check($raw[1] == 10 && $raw[2] == 20 && $raw[3] == 30,
      'C reads the inlined struct at the offset C would use');

# 4. two inline members in a row do not alias
class Pair2 is repr('CStruct') { HAS Three $.a; HAS Three $.b; }
my $p = Pair2.new;
$p.a.x = 7; $p.b.x = 9;
check($p.a.x == 7 && $p.b.x == 9, 'consecutive HAS members are distinct');
check(nativesizeof(Pair2) == 24, 'and sit back to back');

# 5. an ordinary nested write still calls its accessor the usual number of times
#    (the inline view is resolved from the DECLARATION, so `$a.b.c = 1` on plain
#    Raku objects must not gain an extra accessor call)
my $calls = 0;
class Leaf { has $.v is rw }
class Node { has Leaf $!l = Leaf.new; method leaf { $calls++; $!l } }
my $n = Node.new;
$n.leaf.v = 42;
check($n.leaf.v == 42, 'a plain nested write still works');
check($calls == 2,     'and calls the accessor twice, as before');

# 6. DELIBERATE DIVERGENCE, asserted so it stays deliberate: a union pads out to
#    its own alignment, as C does. Rakudo answers 12 for this one; that is
#    MoarVM under-reporting, and a struct embedding such a union would then place
#    every later field 4 bytes early.
class U is repr('CUnion') { HAS Three $.t; has int64 $.n is rw; }
my $rakupp = $*RAKU.compiler.name eq 'Raku++';
my $usize  = nativesizeof(U);
check($usize == ($rakupp ?? 16 !! 12), "union sizeof is $usize");

if @fail { note "FAILED: @fail[]"; say 'FAIL' } else { say 'PASS' }
