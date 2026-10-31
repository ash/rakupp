# Regression: a bare block's implicit `$_` parameter answers what every
# Parameter does (issue #77, FCO's item 1).
#
# `{ .say }.signature.params[0]` is built apart from written parameters,
# and carried only some of their fields: `.named_names` died "No such
# method 'named_names' for invocant of type 'Parameter'". Red inspects a
# transaction callback's parameters for named ones this way.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

my &block = { .say };
my $p = &block.signature.params[0];
ck $p.named_names, (), 'named_names is empty';
ck ($p.copy, $p.capture, $p.invocant, $p.multi-invocant), (False, False, False, False), 'the flags';
ck ($p.prefix, $p.suffix, $p.modifier), ('', '?', ''), 'the affixes';
ck $p.sub_signature, Signature, 'no sub-signature';
ck ($p.name, $p.optional, $p.raw, $p.named, $p.positional), ('$_', True, True, False, True), 'what it already said';

# …and as for a written one
ck -> $_ { }.signature.params[0].named_names, (), 'an explicit $_ agrees';
ck -> :$x, :y(:$z) { }.signature.params.map(*.named_names.sort.List).List, (('x',), ('y', 'z')), 'named ones list their names';

say 'PASS';
