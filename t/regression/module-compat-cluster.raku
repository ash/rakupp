# Four general gaps found burning down the v3 dist-suite bar (the Test::META
# chain: License::SPDX -> JSON::Class -> JSON::Unmarshal). Each verified
# against Rakudo. Runs under both engines.

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

say $fails == 0 ?? 'PASS' !! 'FAIL';
exit($fails ?? 1 !! 0);
