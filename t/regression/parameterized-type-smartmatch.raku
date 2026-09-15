# Regression: a parameterized type is matched by its parameter, 2026-09-15
# (issue #89). `$e ~~ CArray[Str]` was True for a CArray[N-Error], and
# `Array[Int] ~~ Array[Str]`, `Hash[Int] ~~ Hash[Str]` were True too: the
# smartmatch compared bare type names and never looked at the parameter.
# Every answer below was checked against Rakudo.

use NativeCall;

my $ok = True;
sub ck($got, $want, $l) { unless $got eqv $want { say "FAIL: $l - {$got.raku} vs {$want.raku}"; $ok = False } }

constant gchar-pptr = CArray[Str];
class N-Error is repr('CStruct') { has uint32 $.domain; }
my $e = CArray[N-Error].new(N-Error);
my $s = CArray[Str].new("a");

# the report, verbatim
ck($e ~~ gchar-pptr,          False, 'a CArray[N-Error] is not the CArray[Str] a constant names');
ck($e ~~ CArray[Str],         False, '...nor the one written inline');
ck($e ~~ CArray[N-Error],     True,  'it is its own type');
ck(gchar-pptr ~~ CArray[Str], True,  'and the constant is the type it names');
ck(CArray[int] ~~ Int,        False, 'a CArray of ints is not an Int');
ck(Array[Int] ~~ Array[Str],  False, 'two Array parameterizations differ');
ck(Array[Int] ~~ Array[Int],  True,  '...and one matches itself');

# the same base type: identical parameters, and a bare type is not a
# parameterized one
ck(CArray[Str] ~~ CArray,     True,  'a parameterized CArray is a CArray');
ck(CArray ~~ CArray[Str],     False, 'a bare CArray is not a CArray[Str]');
ck($s ~~ CArray[N-Error],     False, 'a CArray[Str] instance is not a CArray[N-Error]');
ck($s ~~ CArray[Str],         True,  '...but is a CArray[Str]');
ck(CArray[int] ~~ CArray[int32], False, 'int and int32 are two parameters');
ck(Array ~~ Array[Int],       False, 'a bare Array is not an Array[Int]');
ck([1, 2] ~~ Array[Int],      False, '...and neither is an untyped array');
ck([1, 2] ~~ Array[Mu],       False, '...not even an Array[Mu]');
ck([1, 2] ~~ Array,           True,  'it is still an Array');
ck((my Int @a = 1, 2) ~~ Array[Int], True,  'a typed array is its Array[T]');
ck((my Int @b = 1, 2) ~~ Array[Str], False, '...and not another');
ck(Array[Int:D] ~~ Array[Int], False, 'a definedness smiley makes a distinct type');
ck(Hash[Int] ~~ Hash[Str],    False, 'two Hash parameterizations differ');
ck(Hash[Int,Str] ~~ Hash[Int], False, 'a value-and-key Hash type is not the value-only one');
ck((my Int %h = a => 1) ~~ Hash[Int], True,  'a typed hash is its Hash[V]');
ck((my Int %i = a => 1) ~~ Hash[Str], False, '...and not another');
ck((my Int %j{Str} = a => 1) ~~ Hash[Int,Str], True, 'an object hash answers both parameters');
ck((my Int %k{Str} = a => 1) ~~ Hash[Int],     False, '...and is not the value-only Hash[Int]');
ck((my Int %l{Str} = a => 1) ~~ Hash[Int,Int], False, '...but not a different key type');
ck((my uint8 @n = 1) ~~ array[uint8], True,  'a native array is its array[T]');
ck((my uint8 @o = 1) ~~ array[int8],  False, '...and not another');

# a parameterized role: the element type conforms
ck(Array[Int] ~~ Positional,        True,  'an Array[Int] is Positional');
ck(Array[Int] ~~ Positional[Int],   True,  '...a Positional[Int]');
ck(Array[Int] ~~ Positional[Cool],  True,  '...a Positional[Cool], since Int is Cool');
ck(Array[Int] ~~ Positional[Mu],    True,  '...and a Positional[Mu]');
ck(Array[Int] ~~ Positional[Str],   False, 'but not a Positional[Str]');
ck(Array[Int] ~~ Positional[Int:D], False, 'nor a Positional[Int:D], which is narrower');
ck([1, 2] ~~ Positional[Mu],        False, 'an untyped array is not a Positional[Mu]');
ck((1, 2) ~~ Positional[Int],       False, 'nor is a list a Positional[Int]');
ck((my Int @c = 1, 2) ~~ Positional[Int], True,  'a typed array is a Positional[T]');
ck((my Int @d = 1, 2) ~~ Positional[Str], False, '...and not of another T');
ck(CArray[Str] ~~ Positional[Str],  True,  'a CArray[Str] is a Positional[Str]');
ck(CArray[Str] ~~ Positional[Int],  False, '...and not a Positional[Int]');
ck($s ~~ Positional[Str],           True,  'so is the instance');
ck(Hash[Int] ~~ Associative[Int],   True,  'a Hash[Int] is an Associative[Int]');
ck(Hash[Int] ~~ Associative[Str],   False, '...and not an Associative[Str]');
ck({a => 1} ~~ Associative[Int],    False, 'an untyped hash is not an Associative[Int]');
ck((my Int %m = a => 1) ~~ Associative[Int], True,  'a typed hash is');
ck((my Int %n = a => 1) ~~ Associative[Str], False, '...of its own value type');
ck(Hash[Int,Str] ~~ Associative[Int], True,  'a Hash[V,K] is an Associative[V]');
ck(Hash[Int,Str] ~~ Associative[Int,Str], False, '...and no container does Associative[V,K]');

# a user class that composes a parameterized role is out of this fix's reach:
# the composition records the role by name only. What must not change is that
# the bare role still answers.
class P does Positional[Int] { method AT-POS($i) { $i } }
ck(P ~~ Positional,        True, 'a class doing Positional[Int] is Positional');
ck(P ~~ Positional[Int],   True, '...and answers its own parameterization');
role R[::T] { method t { T } }
ck(R[Int].new ~~ R[Int],   True,  'a pun matches its own parameterization');
ck(R[Int].new ~~ R[Str],   False, '...and not another');

# the .WHAT of a CArray instance is the type object that names it
ck($e.WHAT === CArray[N-Error], True,  'a CArray instance .WHAT is its parameterized type');
ck($e.WHAT === CArray[Str],     False, '...and not another');
# (by suffix: Rakudo says NativeCall::Types::CArray[N-Error], a known divergence)
ck($e.WHAT.^name.ends-with('CArray[N-Error]'), True, '...spelled the same as before');

# a live Pointer keeps its element type in a slot of its own; the first
# version of this fix read only the slots an Array or a CArray literal uses,
# and `$p.succ ~~ Pointer[uint16]` (top100-roots-2026-09-02) went False
my $ca = CArray[uint16].new(10, 20, 30);
my $p = nativecast(Pointer[uint16], $ca);
ck($p ~~ Pointer[uint16],       True,  'a Pointer[uint16] is its own type');
ck($p.succ ~~ Pointer[uint16],  True,  '...after pointer arithmetic too');
ck($p ~~ Pointer[uint8],        False, '...and not another element type');
ck($p ~~ Pointer,               True,  'it is still a Pointer');
ck(nativecast(Pointer, $ca) ~~ Pointer[uint16], False, 'a void pointer is not a Pointer[uint16]');
ck($p.WHAT === Pointer[uint16], True,  'its .WHAT is the parameterized type');
ck(Pointer[uint16] ~~ Pointer[uint8], False, 'two Pointer parameterizations differ');
ck(Pointer ~~ Pointer[uint16],  False, 'a bare Pointer is not a Pointer[uint16]');
ck(Pointer[uint16] ~~ Pointer,  True,  '...but a parameterized one is a Pointer');

say $ok ?? 'PASS' !! 'FAIL';
