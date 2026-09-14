# Regression: a type mixin on a PARAMETERIZED container keeps the element
# type. `(Array[Foo] but R).of` answered Mu where `Array[Foo].of` answers Foo —
# the parameter rides on the type Value, not on the class, and building the
# mixed type `Array[Foo]+{R}` dropped it. JSON::Class declares its typed arrays
# exactly this way (`constant TestObjects = (Array[TestObject] but JSON::Class)`),
# and JSON::Unmarshal then asked the mixed type `.of` to know what to build each
# element into — it got Mu, and every element came back a plain Hash
# (JSON::Class t/050-array.t).
#
# Every expectation below was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

role Tag { method tagged { True } }
class Item { has Str $.name }

constant Items = (Array[Item] but Tag);
ck(Items.of.^name, 'Item', '(Array[Item] but Tag).of is the element type');
ck(Items.^name, 'Array[Item]+{Tag}', 'and the mixed type is named after both');
ck(Items ~~ Positional, True, 'it is still Positional');
ck(Items.tagged, True, 'and answers the role');
ck(Array[Item].of.^name, 'Item', 'the bare parameterized type still does');

# the same through `does`, and on a Hash
constant Keyed = (Hash[Item] but Tag);
ck(Keyed.of.^name, 'Item', '(Hash[Item] but Tag).of is the value type');
ck(Keyed ~~ Associative, True, 'and it is still Associative');

# an unparameterized base is unchanged: Mu, as before
ck((Array but Tag).of.^name, 'Mu', '(Array but Tag).of stays Mu');

# what the unmarshaller does with it: build each element as the element type
my \T = Items.of;
my @built := Array[T].new;
@built.append(T.new(name => 'a'), T.new(name => 'b'));
ck(@built.map(*.name).join(','), 'a,b', 'the element type builds the elements');
ck(@built.of.^name, 'Item', 'into an array of that type');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
