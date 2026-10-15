# Regression: a native parameter refuses what it cannot hold the way Rakudo's
# binder does — an X::AdHoc saying it cannot UNBOX the value, not a binding
# type check. Before, a Str, an object, a list, a type object into an `int`
# parameter was X::TypeCheck::Binding::Parameter ("expected int but got Str"),
# a Num or Rat was an X::AdHoc of rakupp's own wording ("Cannot put a Num …"),
# a Junction autothreaded into it, and an allomorph or enum bound as itself
# rather than as its number or string. The wording follows the kind of native
# (integer, unsigned integer, number, string) and the value's representation
# (P6opaque, or another: a Blob is VMArray). `my int $x = 1.5e0` shares the
# Num/Rat refusal and its wording. Oracle-checked against Rakudo 2026.09.
# Contract: exit 0 + last line PASS.
use NativeCall;
my @fail;
sub refused(&code, $want, $label) {
    my $got = (try { code(); 'no exception' }) // "{$!.^name}: {$!.message}";
    @fail.push("$label: got $got.raku(), want $want.raku()") unless $got eq "X::AdHoc: $want";
}
sub binds(&code, $want, $label) {
    my $got = try code();
    @fail.push("$label: got {$got.raku} ({$!.?message // 'no exception'}), want {$want.raku}")
        unless $got eqv $want;
}

class C { }
class D is Int { }
enum E <ea eb>;
class S is repr('CStruct') { has int32 $.a }
class CP is repr('CPointer') { }
sub fi(int $x)        { $x }
sub fi-slow(int $x, :$n) { $x }
sub fi8(int8 $x)      { $x }
sub fa(atomicint $x)  { $x }
sub fu(uint $x)       { $x }
sub fu8(uint8 $x)     { $x }
sub fb(byte $x)       { $x }
sub fn(num $x)        { $x }
sub fn32(num32 $x)    { $x }
sub fs(str $x)        { $x }
sub fs-slow(str $x, $y?) { $x }

# a concrete value of the wrong kind
my $s = "abc";
for (&fi, &fi-slow, &fi8, &fa) -> &f {
    refused { f($s) },            'This type cannot unbox to a native integer: P6opaque, Str', "{&f.name}(Str)";
    refused { f(1.5e0) },         'This type cannot unbox to a native integer: P6opaque, Num', "{&f.name}(Num)";
    refused { f(1.5) },           'This type cannot unbox to a native integer: P6opaque, Rat', "{&f.name}(Rat)";
}
refused { fi(1+2i) },             'This type cannot unbox to a native integer: P6opaque, Complex', 'int(Complex)';
refused { fi([1, 2]) },           'This type cannot unbox to a native integer: P6opaque, Array', 'int(Array)';
refused { fi(%(a => 1)) },        'This type cannot unbox to a native integer: P6opaque, Hash', 'int(Hash)';
refused { fi((a => 1)) },         'This type cannot unbox to a native integer: P6opaque, Pair', 'int(Pair)';
refused { fi(1..3) },             'This type cannot unbox to a native integer: P6opaque, Range', 'int(Range)';
refused { fi((1, 2).Seq) },       'This type cannot unbox to a native integer: P6opaque, Seq', 'int(Seq)';
refused { fi(sub {}) },           'This type cannot unbox to a native integer: P6opaque, Sub', 'int(Sub)';
refused { fi(C.new) },            'This type cannot unbox to a native integer: P6opaque, C', 'int(C)';
refused { fi(Failure.new("x")) }, 'This type cannot unbox to a native integer: P6opaque, Failure', 'int(Failure)';
refused { fi(<1.5e0>) },          'This type cannot unbox to a native integer: P6opaque, NumStr', 'int(NumStr)';
{ my $j = 1 | 2; refused { fi($j) }, 'This type cannot unbox to a native integer: P6opaque, Junction', 'a Junction is not autothreaded' }
for (&fu, &fu8, &fb) -> &f {
    refused { f($s) },    'This type cannot unbox to a native unsigned integer: P6opaque, Str', "{&f.name}(Str)";
    refused { f(1.5e0) }, 'This type cannot unbox to a native unsigned integer: P6opaque, Num', "{&f.name}(Num)";
}
for (&fn, &fn32) -> &f {
    refused { f(2) },     'This type cannot unbox to a native number: P6opaque, Int', "{&f.name}(Int)";
    refused { f(1.5) },   'This type cannot unbox to a native number: P6opaque, Rat', "{&f.name}(Rat)";
    refused { f($s) },    'This type cannot unbox to a native number: P6opaque, Str', "{&f.name}(Str)";
    refused { f(True) },  'This type cannot unbox to a native number: P6opaque, Bool', "{&f.name}(Bool)";
    refused { f(<42>) },  'This type cannot unbox to a native number: P6opaque, IntStr', "{&f.name}(IntStr)";
    refused { f(eb) },    'This type cannot unbox to a native number: P6opaque, E', "{&f.name}(enum)";
}
for (&fs, &fs-slow) -> &f {
    refused { f(2) },     'This type cannot unbox to a native string: P6opaque, Int', "{&f.name}(Int)";
    refused { f(1.5e0) }, 'This type cannot unbox to a native string: P6opaque, Num', "{&f.name}(Num)";
    refused { f(True) },  'This type cannot unbox to a native string: P6opaque, Bool', "{&f.name}(Bool)";
    refused { f(eb) },    'This type cannot unbox to a native string: P6opaque, E', "{&f.name}(enum)";
    refused { f(C.new) }, 'This type cannot unbox to a native string: P6opaque, C', "{&f.name}(C)";
}

# a representation other than P6opaque says so
refused { fi(Blob.new(1)) },  'This representation (VMArray) cannot unbox to a native int (for type Blob)', 'int(Blob)';
refused { fu(buf8.new(1)) },  'This representation (VMArray) cannot unbox to an unsigned native int (for type Buf[uint8])', 'uint(Buf)';
refused { fn(Blob.new(1)) },  'This representation (VMArray) cannot unbox to a native num (for type Blob)', 'num(Blob)';
refused { fs(Blob.new(1)) },  'This representation (VMArray) cannot unbox to a native string (for type Blob)', 'str(Blob)';
refused { fi(CArray[int32].new(1)) },
    'This representation (CArray) cannot unbox to a native int (for type NativeCall::Types::CArray[int32])', 'int(CArray)';
refused { fn(S.new) },        'This representation (CStruct) cannot unbox to a native num (for type S)', 'num(CStruct)';
refused { fn(Pointer.new) },
    'This representation (CPointer) cannot unbox to a native num (for type NativeCall::Types::Pointer)', 'num(Pointer)';

# a type object
for (&fi, &fi-slow, &fi8, &fa) -> &f {
    refused { f(Int) }, 'Cannot unbox a type object (Int) to int.', "{&f.name}(Int:U)";
    refused { f(Any) }, 'Cannot unbox a type object (Any) to int.', "{&f.name}(Any)";
}
refused { fi(Mu) },  'Cannot unbox a type object (Mu) to int.', 'int(Mu)';
refused { fi(C) },   'Cannot unbox a type object (C) to int.', 'int(C:U)';
{ my $n = Nil; refused { fi($n) }, 'Cannot unbox a type object (Any) to int.', 'int(a variable holding Nil)' }
for (&fu, &fu8, &fb) -> &f { refused { f(Int) }, 'Cannot unbox a type object (Int) to an unsigned int.', "{&f.name}(Int:U)" }
for (&fn, &fn32) -> &f     { refused { f(Num) }, 'Cannot unbox a type object (Num) to a num.', "{&f.name}(Num:U)" }
for (&fs, &fs-slow) -> &f  { refused { f(Str) }, 'Cannot unbox a type object (Str) to a str.', "{&f.name}(Str:U)" }

# …and what a native takes, it takes as the native
binds { fi(True) },     1,       'True into an int is 1';
binds { fi(D.new(5)) }, 5,       'an Int subclass into an int';
binds { fi(<42>) },     42,      'an IntStr into an int is its Int';
binds { fi(eb) },       1,       'an enum into an int is its value';
binds { fu8(eb) },      1,       '…into a uint8 too';
binds { fs(<42>) },     '42',    'an IntStr into a str is its Str';
binds { fn(<1.5e0>) },  1.5e0,   'a NumStr into a num is its Num';
binds { fs(<1.5e0>) },  '1.5e0', 'a NumStr into a str is its Str';
binds { fs($s) },       'abc',   'a Str into a str';
binds { fi(Pointer.new) }, 0,    'a Pointer into an int is its address';
{ my $p = nativecast(Pointer, CArray[int32].new(1, 2));
  binds { fu($p) }, +$p,         'a live Pointer into a uint';
  binds { fi8($p) }, (+$p + 128) % 256 - 128, '…and into an int8, wrapped' }
binds { fi(nativecast(CP, Pointer.new(4096))) }, 4096, 'a CPointer class instance into an int is its address';

# assignment refuses a number of the other kind with the same words
my $num = 1.5e0; my $rat = 1/2; my $int = 1;
refused { my int $x = $num },   'This type cannot unbox to a native integer: P6opaque, Num', 'my int = Num';
refused { my uint $x = $num },  'This type cannot unbox to a native unsigned integer: P6opaque, Num', 'my uint = Num';
refused { my int $x = $rat },   'This type cannot unbox to a native integer: P6opaque, Rat', 'my int = Rat';
refused { my num $x = $int },   'This type cannot unbox to a native number: P6opaque, Int', 'my num = Int';
refused { my num32 $x = $rat }, 'This type cannot unbox to a native number: P6opaque, Rat', 'my num32 = Rat';
refused { my int $x = 1; $x += $num }, 'This type cannot unbox to a native integer: P6opaque, Num', 'int += Num';
refused { my uint8 $x = 1; $x = $num }, 'This type cannot unbox to a native unsigned integer: P6opaque, Num', 'uint8 = Num';

say @fail ?? "FAIL: {@fail.join('; ')}" !! 'PASS';
exit @fail ?? 1 !! 0;
