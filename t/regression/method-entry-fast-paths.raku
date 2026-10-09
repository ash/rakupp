# Guards for methodCall's entry fast paths (2026-10-09): `.Num`, `.Int`,
# `.Str`, `.defined` and `.elems` on plain core values, `.push` onto a plain
# Array, and a NativeCall struct's field. Each is answered before the general
# method chain, so each case the chain treats specially has to keep going
# there: an enum value, an allomorph, a big Int, a native, a lazy or shaped
# array, a typed or `is default` array, a Slip. Expected values are Rakudo
# 2026.09's.
use Test;
use NativeCall;
plan 31;

# coercions and queries
is 3.Num.raku, '3e0', 'Int.Num';
is 2.5e0.Num.raku, '2.5e0', 'Num.Num';
is (2**70).Num.raku, '1.1805916207174113e+21', 'a big Int .Num';
is (2**70).Int.raku, '1180591620717411303424', 'a big Int .Int';
is Less.Int.raku, '-1', 'an enum value .Int is the plain Int';
is Less.Int.WHAT.raku, 'Int', '…of type Int';
is <42>.Int.WHAT.raku, 'Int', 'an IntStr .Int is an Int';
is <4.5e0>.Num.WHAT.raku, 'Num', 'a NumStr .Num is a Num';
is "abc".Str, 'abc', 'Str.Str';
enum E (a => 'x');
is E::a.Str, 'x', 'a Str-valued enum .Str is its value';
my int $ni = 7;
is $ni.Num.raku, '7e0', 'a native int .Num';
my num $nn = 1.5e0;
is $nn.Num.WHAT.raku, 'Num', 'a native num .Num is a Num';
ok 5.defined && 0e0.defined && ''.defined, '.defined on plain values';
nok Int.defined, 'a type object is not defined';

# .elems
is [1, 2, 3].elems, 3, 'Array.elems';
is (1, 2).elems, 2, 'List.elems';
my @shaped[2;3];
is @shaped.elems, 2, 'a shaped array .elems is its first dimension';
is (1..Inf).list.is-lazy, True, 'a lazy list is lazy';
my $seq = (1, 2, 3).map(* * 2);
is $seq.elems, 3, 'a Seq .elems';
is $seq.list.join(','), '2,4,6', '…and the Seq stays readable';

# .push
my @a;
@a.push: 1;
@a.push: 2, 3;
is @a.join(','), '1,2,3', 'push one and several';
is (my @r = 1).push(2).raku, '[1, 2]', 'push answers the array';
my @s;
@s.push: slip(7, 8);
is @s.elems, 2, 'a Slip argument slips';
@s.push: Empty;
is @s.elems, 2, '…and Empty adds nothing';
my @d is default(42);
@d.push: Nil;
is @d[0], 42, 'a pushed Nil is the default';
my Int @t;
throws-like { @t.push: 'x' }, X::TypeCheck, 'a typed array checks a push';
my @l = (1..*);
throws-like { @l.push: 1 }, X::Cannot::Lazy, 'a lazy array refuses push';
throws-like { (1, 2).push: 3 }, X::Immutable, 'a List refuses push';

# a NativeCall struct's field, and a method of the same name
class S is repr('CStruct') {
    has int32 $.n;
    has num64 $.x;
    submethod BUILD(:$n, :$x) { $!n = $n; $!x = $x }
}
my $s = S.new(n => 5, x => 2.5e0);
is $s.n, 5, 'a struct field';
is $s.x, 2.5e0, '…a num field';
class T is repr('CStruct') {
    has int32 $.n;
    method n() { 'method' }
}
is T.new.n, 'method', 'a method of the field\'s name wins';
