# Four general gaps found burning down the v3 dist-suite bar (the Test::META
# chain: License::SPDX -> JSON::Class -> JSON::Unmarshal). Each verified
# against Rakudo. Runs under both engines.
#
# Section 5 imports the two dists by name, and `is json-name` is a trait, so
# the import has to happen at compile time — there is no probing it from
# inside. Hence the declarations below: without them this file passed on
# every developer machine (where zef has installed them) and failed on every
# CI runner, which is how it shipped in v3.0.0.
#?requires JSON::Name
#?requires JSON::Unmarshal

my $fails = 0;
sub check(Str $desc, $got, $want) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "not ok - $desc"; note "GOT [{$got.raku}] WANT [{$want.raku}]" }
}

# 1. a bare `does R` binds a parameterized role's parameter DEFAULTS
role RP[Bool :$opt = False] { method opt-val { $opt } }
class CDefault does RP {}
class COn does RP[:opt] {}
check('bare does binds role param defaults', CDefault.new.opt-val, False);
check('explicit role args still bind', COn.new.opt-val, True);

# 2. OUR::{'&name'} := binds into the package namespace (JSON::Class's
#    EXPORT::DEFAULT re-exports trait_mod:<is> this way)
module M2 {
    OUR::{'&answer'} := sub { 42 };
}
check('OUR::{...} := binding survives module load', 1, 1);

# 3. .HOW.archetypes + .HOW.^can — the introspection JSON::Unmarshal's
#    ClassLike subset gates on
class C3 { has $.x }
check('.HOW.archetypes.nominal on a class', ?C3.HOW.archetypes.nominal, True);
check('.HOW.archetypes.parametric on a class', ?C3.HOW.archetypes.parametric, False);
# the exact gate JSON::Unmarshal's ClassLike subset uses (a where clause is
# the only context Rakudo boolifies .HOW.^can in without exploding)
my subset ClassLike of Mu where { .HOW.archetypes.nominal && .HOW.^can('attributes') };
check('the ClassLike-style introspection gate admits a class', C3 ~~ ClassLike, True);

# 4. attribute .type carries the container shape; =:= discriminates types;
#    a Positional type object binds an @-sigiled multi param (the JSON::
#    Unmarshal dispatch surface, end to end)
class L4 {}
class C4 { has L4 @.xs; has L4 $.one; }
check('@-attr .type is Positional[T]', C4.^attributes[0].type.^name, 'Positional[L4]');
check('@-attr .type.of is the element type', C4.^attributes[0].type.of.^name, 'L4');
check('type objects discriminate under =:=', (L4 =:= Any), False);
check('same type is =:= itself', (Any =:= Any), True);
multi tsel($j, @x) { 'array-of-' ~ @x.of.^name }
multi tsel($j, Mu) { 'mu' }
check('Positional[T] type object reaches the @-param multi',
      tsel(1, C4.^attributes[0].type), 'array-of-L4');

# 5. attribute USER traits surface on the meta-object — the JSON::Name /
#    JSON::Unmarshal role checks and accessors (the Test::META chain reads
#    licenseId through exactly this)
use JSON::Name;
use JSON::Unmarshal;
class C5 {
    has Str $.plain;
    has Str $.mapped is json-name('mappedName');
    has Str $.when   is unmarshalled-by(-> $d { "got:$d" });
}
my $a5 = C5.^attributes[1];
check('is json-name applies the NamedAttribute role', ?($a5 ~~ JSON::Name::NamedAttribute), True);
check('plain attr is NOT NamedAttribute', ?(C5.^attributes[0] ~~ JSON::Name::NamedAttribute), False);
check('.json-name answers the stored name', $a5.json-name, 'mappedName');
check('unmarshal(json) runs the is unmarshalled-by code',
      C5.^attributes[2].unmarshal('x', Str), 'got:x');
check('unmarshal end to end maps the json name',
      unmarshal('{"plain":"a","mappedName":"b"}', C5).mapped, 'b');

# 6. a multi whose slurpy-HASH carries a where clause loses when named args
#    arrive (License::SPDX's resource-loading constructor recursed forever)
class C6 {
    has $.x;
    multi method new(*%v where { not $_.keys }) { self.bless(x => 'empty') }
}
check('nameds reject the empty-only slurpy-where multi', C6.new(x => 5).x, 5);
check('no args still take it', C6.new.x, 'empty');

# 7. an unbound dynamic reads as undefined so // falls back (Test::META's
#    get-meta candidates), and a fresh `my` is visible to its own initializer
sub c7() { @*NO-SUCH-DYNAMIC-C7 // <fell back> }
check('unbound @*dynamic falls through //', c7().join(' '), 'fell back');
my @res7;
sub probe7() { @res7.push((@*C7-CANDS // <default>).elems) }
{
    my @*C7-CANDS = do { probe7(); 'x', 'y' };
}
check('my @*dyn is visible (empty) during its own initializer', @res7[0], 0);

# 8. ".".IO.parent climbs to ".." (Test::META resolves the dist dir from t/)
check('parent of . is ..', '.'.IO.parent.Str, '..');

say $fails == 0 ?? 'PASS' !! 'FAIL';
exit($fails ?? 1 !! 0);
