# Regression: a CArray knows its own type, 2026-08-04. Found taking
# NativeHelpers::Array through its own suite, where `isa-ok($c, CArray[int32])`
# failed because the value reported itself as a plain Str. Checked against
# Rakudo.

use NativeCall;

my $ok = True;
sub ck($got, $want, $l) { unless $got eqv $want { say "FAIL: $l — {$got.raku} vs {$want.raku}"; $ok = False } }

my $c = CArray[int32].new;
$c[0] = 1;
$c[1] = 2;

# A CArray is stored as raw bytes in a Str and used to report "Str", so `.^name`
# lied and every type test on it was False.
#
# The name is asserted by SUFFIX on purpose. Rakudo says
# `NativeCall::Types::CArray[int32]` and we say `CArray[int32]` — a real, KNOWN
# divergence: rakupp uses the short name for the type object too, so it is at
# least self-consistent, and qualifying it would touch the whole NativeCall
# surface. What matters here is that the element type is reported at all.
ck($c.^name.ends-with('CArray[int32]'), True, 'a CArray names its element type');
ck($c ~~ CArray[int32], True, 'and smartmatches the parameterized type');
ck($c ~~ CArray,        True, '…and the bare one');

# `.isa` is what isa-ok uses, and it compared the composed name against a type
# object whose parameter is carried separately.
ck($c.isa(CArray[int32]), True, '.isa on the parameterized type');
ck($c.isa(CArray),        True, 'a parameterized CArray IS a CArray');
ck($c.isa(Int),           False, 'and is not something else');

# the values are untouched by any of this
ck(($c[0], $c[1]), (1, 2), 'the elements still read back');

my $u = CArray[uint8].new;
$u[0] = 65;
ck($u.^name.ends-with('CArray[uint8]'), True, 'another element type');
ck($u.isa(CArray[int32]), False, 'a different parameter does not match');

say $ok ?? 'PASS' !! 'FAIL';
