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

say $fails == 0 ?? 'PASS' !! 'FAIL';
exit($fails ?? 1 !! 0);
