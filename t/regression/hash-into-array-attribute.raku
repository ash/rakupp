# Regression: a Hash given to an `@` attribute stayed ONE element.
#
#   class A { has @.data }
#   A.new(data => {a => 1}).data    # Rakudo: [a => 1]; rakupp: [{a => 1}]
#
# Construction is list assignment, and `my @a = %h` spreads the hash into its
# pairs. coerceToSigil, which every construction path, an attribute default
# and `.clone` go through, pushed any Hash as a single element, so a Map, a
# Set and a Bag stayed whole too, and `has Pair @.p` refused `{a => 1}`
# outright. Only the ITEMIZED forms (`$%h`, `%h.item`, a `$h` holding one) are
# one element, as rtArrayVal already decided for `@a = …`.
#
# Found beside attribute-shadowed-by-other-sigil.raku, which passed a list
# of pairs to step around it. Every expectation was checked against Rakudo
# 2026.09.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — got {$got.raku} want {$want.raku}" }
}

class A { has @.data }

# --- spread: a Hash, a Map, a Set, a Bag ---------------------------------------
ck A.new(data => {a => 1}).data, [a => 1], 'a Hash literal is its pairs';
{
    my %h = a => 1, b => 2;
    ck A.new(data => %h).data.sort.List, (a => 1, b => 2), 'a %-variable is its pairs';
}
ck A.new(data => {}).data, [], 'an empty Hash is no elements';
ck A.new(data => Map.new((a => 1))).data, [a => 1], 'a Map is its pairs';
ck A.new(data => set <a>).data, [a => True], 'a Set is its pairs';
ck A.new(data => bag <a a>).data, [a => 2], 'a Bag is its pairs';

# --- kept whole: an itemized Hash ------------------------------------------------
{
    my %h = a => 1;
    ck A.new(data => $%h).data, [{a => 1},], '$%h is one element';
    ck A.new(data => %h.item).data, [{a => 1},], '%h.item is one element';
    my $hr = {x => 1};
    ck A.new(data => $hr).data, [{x => 1},], 'a $-variable holding a Hash is one element';
}

# --- what was right already stays right ------------------------------------------
ck A.new(data => (1, 2)).data, [1, 2], 'a List is its elements';
ck A.new(data => $[1, 2]).data, [[1, 2],], 'an itemized Array is one element';
ck A.new(data => 1..3).data, [1, 2, 3], 'a Range is its values';
ck A.new(data => (a => 1)).data, [a => 1], 'a Pair is one element';

# --- every path into the attribute ---------------------------------------------
class E  { has @.data = {z => 1} }
class E2 { has @.data = $%(z => 1) }
class E3 { has @.data = set <q> }
ck E.new.data, [z => 1], 'a Hash default is its pairs';
ck E2.new.data, [{z => 1},], 'an itemized Hash default is one element';
ck E3.new.data, [q => True], 'a Set default is its pairs';
ck A.new.clone(data => {b => 2}).data, [b => 2], '.clone(data => {…}) is its pairs';
class F is A { }
ck F.new(data => {a => 1}).data, [a => 1], 'an inherited @ attribute';
class C { has Pair @.p }
{
    my @p := C.new(p => {a => 1}).p;
    ck (@p.of.^name, @p.List), ('Pair', (a => 1,)), 'a typed Pair @ attribute takes the pairs';
}
class I { has Int @.n }
ck (try I.new(n => {a => 1})) // $!.^name, 'X::TypeCheck::Assignment', 'a typed Int @ attribute refuses them';

say $fails == 0 ?? 'PASS' !! 'FAIL';
exit $fails == 0 ?? 0 !! 1;
