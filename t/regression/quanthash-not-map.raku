# Regression: a Set, Bag or Mix (and SetHash, BagHash, MixHash) smartmatched
# `Map` and bound a `Map $m` parameter. They do Associative on both engines,
# but they are not Maps. YAML::Native's serializer worked around it with
# `~~ Hash || .isa(Map)`. `$*RAKU`, a Lock::Async, a
# CompUnit::DependencySpecification, a wrap handle, a CompUnit and an IO::Pipe
# ride on hashes here and matched `Associative` as well; an IO::Path::Parts is
# Associative but not a Map.
#
# Contract: exit 0 + last line PASS. Runs under Rakudo unchanged; every
# expectation was checked against Rakudo 2026.08 on 2026-10-19.
my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

for set(1), bag(1), mix(1), SetHash.new, BagHash.new, MixHash.new -> \q {
    my $n = q.^name;
    ck(q ~~ Associative, True, "a $n is Associative");
    ck(q ~~ Map, False, "but not a Map");
    ck(q ~~ Hash, False, "nor a Hash");
}
sub g(Map $m) { "bound" }
ck((try g(set(1))) // "refused", "refused", 'a Map parameter refuses a Set');
ck(g(Map.new((a => 1))), "bound", 'and takes a Map');
ck(g(%(a => 1)), "bound", 'and a Hash');
multi h(Map $m) { "map" }
multi h(Setty $s) { "setty" }
ck(h(set(1)), "setty", 'a Set reaches its Setty candidate');
ck(h(%(a => 1)), "map", 'a Hash reaches the Map one');
sub assoc(Associative $a) { "ok" }
ck(assoc(bag(1)), "ok", 'an Associative parameter still takes a Bag');

ck($*RAKU ~~ Associative, False, '$*RAKU is not Associative');
ck($*RAKU.compiler ~~ Associative, False, 'nor its compiler');
ck(Lock::Async.new ~~ Associative, False, 'nor a Lock::Async');
ck(CompUnit::DependencySpecification.new(:short-name<A>) ~~ Associative, False,
   'nor a CompUnit::DependencySpecification');
sub w { }
ck(&w.wrap({ callsame }) ~~ Associative, False, 'nor a wrap handle');
ck("a/b".IO.parts ~~ Associative, True, 'an IO::Path::Parts is Associative');
ck("a/b".IO.parts ~~ Map, False, 'but not a Map');
ck(PROCESS:: ~~ Map, True, 'a stash is still a Map');
ck(Map.new ~~ Map, True, 'and a Map is a Map');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
