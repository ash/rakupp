# Regression: a type a `use`d module declares, written just before a pointy
# block, was a parse error: `for Cro::Iri -> $p { … }` died "Function
# 'Cro::Iri' needs parens to avoid gobbling block". The parser knew only the
# types its own file declared, so it read the name as a routine taking the
# block as its argument. Cro::Core's uri test runs every case
# `for Cro::Uri, Cro::Iri -> $parser { … }`.
#
# A name nothing declares is still that error, as in Rakudo (Roast's
# S32-exceptions/misc.t: `CATCH { when X::Y {} }`).
#
# Expectations checked against Rakudo 2026.09
# (~/.rakubrew/versions/moar-2026.09/install/bin/rakudo). Green on both.

use lib $?FILE.IO.parent.add('lib').Str;
use RakuppTypeBeforePointy;

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my @seen;
for RakuppTBP -> $t { @seen.push: $t.^name }
ck @seen, ['RakuppTBP'], 'a plain class name';

@seen = ();
for RakuppTBP::Inner -> $t { @seen.push: $t.^name }
ck @seen, ['RakuppTBP::Inner'], 'a qualified class name';

@seen = ();
for RakuppTBP, RakuppTBP::Inner -> $t { @seen.push: $t.^name }
ck @seen, ['RakuppTBP', 'RakuppTBP::Inner'], 'the last of a list (Cro::Core)';

@seen = ();
for RakuppTBPRole -> $t { @seen.push: $t.^name }
ck @seen, ['RakuppTBPRole'], 'a role';

@seen = ();
given RakuppTBP::Grammar -> $g { @seen.push: $g.parse('x').Str }
ck @seen, ['x'], 'a grammar, in a given';

my $err = try { EVAL 'for RakuppNoSuchType -> $t { }'; Nil } // $!;
ck $err.^name, 'X::Comp::Group', 'an undeclared name is still a gobbled block';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
