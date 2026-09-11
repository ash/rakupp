# Regression: EXPORTHOW::DECLARE — a module can add its own spelling of `class`.
#
#     my package EXPORTHOW { package DECLARE { constant model = MetamodelX::Red::Model } }
#
# after which `model Foo { … }` (and `unit model Foo;`) declares a class whose
# metaobject is that HOW. rakupp had none of it: every Red model was "Undefined
# routine 'model'". What the declarator needs behind it is the rest of this
# file — the HOW must BE the type's .HOW, `.^anything` must reach it, a HOW
# whose own parent is the built-in Metamodel::ClassHOW must still reach the MOP
# operations, and its `compose` must run (and its errors must be heard, not
# swallowed).
#
# Every expectation was checked against Rakudo.

use lib $?FILE.IO.parent.add('lib').Str;
use RakuppDeclProbe;

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

widget Gadget { has $.n; method own { "OWN" } }

# --- the declarator declares a class, with the module's HOW ---------------
ck(Gadget.^name, 'Gadget',            'the type has the name it was given');
ck(Gadget.HOW.^name, 'ProbeHOW',      'and the module-supplied metaobject');
ck(Gadget.own, 'OWN',                 'its own methods work');
ck(Gadget.new(n => 5).n, 5,           '…and so do its attributes');

# --- the HOW's compose ran ------------------------------------------------
ck(Gadget.composed, 'COMPOSED:Gadget','compose ran and added a method');
ck(Gadget ~~ Marker, True,            '…and composed a role through add_role');
ck(Gadget.marked, 'MARKED',           '…whose methods answer');
ck(Gadget.^seeded-of(Gadget.new)<seeded>, 1, 'an attribute it added carries its set_build value');

# --- `.^whatever` reaches the metaobject ----------------------------------
ck(Gadget.^widget-of, 'WIDGET-OF:Gadget', 'an unknown .^method asks the .HOW');
ck(Gadget.^attr-names, '$!n,%!___SEEDED___', 'and the HOW can read back through the MOP');
ck(?Gadget.^declares('own'), True,        'declares_method sees a declared method');
ck(?Gadget.^declares('gist'), False,      '…and not an inherited one');

# --- the built-in meta-methods still win ----------------------------------
ck(Gadget.^name, 'Gadget',            '.^name is still the built-in');
ck(?Gadget.^attributes.first(*.name eq '$!n'), True, '.^attributes too');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
