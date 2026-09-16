# Regression: `--> CArray[Str]` keeps its element parameter.
#
# A NativeCall PARAMETER of that type already kept it — the unmarshalling needs
# the element width and, for `Str`, the fact that each slot is a `char *` to be
# decoded rather than an address to be handed back. The RETURN type dropped
# everything after the identifier, so a sub declared `--> CArray[Str]` answered a
# CArray of element type `int64`, and every slot read as the pointer's bits.
#
# Geo::Hash binds libgeohash's `char **` neighbour list and printed eight
# addresses where it expected eight geohashes — the values were right, sitting
# at those addresses, and nothing raised.
#
# Contract: exit 0 + last line PASS.
use NativeCall;
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want
}

# declared, never called — the question is what the signature RECORDS
sub arrow-str(--> CArray[Str])    is native('c') { * }
sub arrow-int(--> CArray[int32])  is native('c') { * }
sub arrow-ptr(--> Pointer[int8])  is native('c') { * }
sub returns-str() returns CArray[Str] is native('c') { * }

check ~&arrow-str.returns.^name.contains('CArray[Str]'),   'True', 'the --> form keeps [Str]';
check ~&arrow-int.returns.^name.contains('CArray[int32]'), 'True', '…and [int32]';
check ~&arrow-ptr.returns.^name.contains('Pointer[int8]'), 'True', '…and a parameterised Pointer';
check ~&returns-str.returns.^name.contains('CArray[Str]'), 'True', 'the `returns` form too';

# an ordinary return type is untouched, parameterised or not
sub plain(--> Int) { 1 }
check ~&plain.returns.^name, 'Int', 'a plain return type';
sub coerced(--> Str()) { 'x' }
check coerced(), 'x', 'a coercion return type still coerces';
sub positional(--> Positional) { (1,) }
check ~positional().elems, '1', 'a parameterless type still works';

# the read the parameter exists for: a Str slot is the string, not the pointer
my $a = CArray[Str].new('ab', 'cd');
check $a[0], 'ab', 'a CArray[Str] slot decodes';
check ~$a[1], 'cd', '…and the next one';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
