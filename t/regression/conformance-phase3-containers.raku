# Regression: raku-spec conformance Phase 3 — value/container defects.
#   1. a QuantHash's `.raku` is the EXPRESSION that rebuilds it, not a Hash
#      literal: the weighted kinds as a pair list coerced to the kind, the Set
#      family as a constructor, a Map as Map.new((…)).
#   2. `.splice` hands back an Array (it came out of one); Complex.reals is a
#      List.
#   3. `\(…)` is exactly the parenthesised group — parsing a full prefix made
#      `\(1,2).list` a capture OF `(1,2).list`.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# 1. QuantHash .raku rebuilds the value
# single-key values only — a QuantHash's ITERATION ORDER is arbitrary in Rakudo,
# so only the FORM is comparable across engines
check(bag(<b b>).raku,          '("b"=>2).Bag',            'bag-raku');
check(<b b>.BagHash.raku,       '("b"=>2).BagHash',        'baghash-raku');
check((a => 0.5).Mix.raku,      '("a"=>0.5).Mix',          'mix-raku');
check((a => 0.5).MixHash.raku,  '("a"=>0.5).MixHash',      'mixhash-raku');
check(set(<a>).raku,            'Set.new("a")',            'set-raku');
check(<a>.SetHash.raku,         'SetHash.new("a")',        'sethash-raku');
check(Map.new((a => 1)).raku,   'Map.new((:a(1)))',        'map-raku');
# a plain Hash still renders as a Hash literal
check({ a => 1 }.raku,          '{:a(1)}',                 'plain-hash-raku');
# and the gists are untouched
check(bag(<b b>).gist,          'Bag(b(2))',               'bag-gist-unchanged');
check(set(<a>).gist,            'Set(a)',                  'set-gist-unchanged');

# 2. the container each operation hands back
my @a = <a b c d e f g>;
check(@a.splice(2, 3).raku, '["c", "d", "e"]', 'splice-returns-an-array');
check((3 + 5i).reals.raku,  '(3e0, 5e0)',      'complex-reals-is-a-list');

# 3. `\(…)` is the group, and postfixes attach to the CAPTURE
check(\(1, 2).list.raku,          '(1, 2)',            'capture-then-list');
check(\(1, 2, :x(3)).hash.raku,   'Map.new((:x(3)))',  'capture-then-hash');
check(\(1, 2, :x(3)).elems,       '2',                 'capture-then-elems');
check(\().elems,                  '0',                 'empty-capture');
my $c = \(1, 2);
check($c.raku, '\\(1, 2)', 'capture-raku-round-trips');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
