# Regression: a finite list made lazy (`lazy 1..3`) is lazy until it has been
# read to its end, as in Rakudo. It was the Range itself with a flag that only
# `.is-lazy` read, and an Array assigned from it was an ordinary three-element
# Array: `.elems` answered, `say` showed the elements, `.is-lazy` said False.
# Now `lazy` builds a Seq lazy by declaration, which refuses what needs its
# length, renders as `[...]` / `(...)`, keeps its laziness through `[ ]`,
# `.Array`, `.Seq` and assignment, and stops being lazy once a `for` (or
# `.eager`) has read it out.
#
# Every expectation below was checked against Rakudo 2026.09. The refusal
# messages are our own wording, so only the exception type is compared.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}
sub refuses(&code, $desc) {
    my $r = try { code(); 'answered' };
    ck(($r // $!.^name), 'X::Cannot::Lazy', $desc);
}

my @e = lazy 1..3;
ck(@e.is-lazy, True, 'an Array assigned from lazy 1..3 is lazy');
ck(@e.gist, '[...]', 'and gists as [...]');
ck(@e.raku, '[...]', 'and .raku is [...]');
ck(@e.Str, '...', 'and Strs as ...');
ck("@e[]", '...', 'and interpolates as ...');
ck(?@e, True, 'it is true');
ck(@e[1], 2, 'it indexes');
ck(@e.head(2).List, (1, 2), 'and heads');
ck(@e.first(2), 2, 'and finds');
ck(@e.join(","), '1,2,...', 'and joins what it has read, then ...');
refuses({ @e.elems }, '.elems refuses');
refuses({ +@e }, 'numifying refuses');
refuses({ @e.end }, '.end refuses');
refuses({ @e.sum }, '.sum refuses');
refuses({ @e.sort }, '.sort refuses');
refuses({ @e.reverse }, '.reverse refuses');
refuses({ @e.List }, '.List of a lazy Array refuses');
ck(@e.Seq.is-lazy, True, '.Seq stays lazy');
ck([@e].is-lazy, True, '[@e] stays lazy');
my @x = @e;
ck(@x.is-lazy, True, 'and so does an Array assigned from it');
ck(@e.map(* + 1).is-lazy, True, 'and .map over it');
ck(@e.reduce(&[+]), 6, '.reduce reads it out');

my @f = lazy 1..3;
ck(@f.eager.elems, 3, '.eager reads it out');
ck(@f.is-lazy, False, 'and then it is no longer lazy');

my @g = lazy 1..3;
my $seen = '';
for @g { $seen ~= $_ }
ck($seen, '123', 'a for loop sees every element');
ck(@g.is-lazy, False, 'and reads it out');
ck(@g.elems, 3, 'so .elems answers');

ck((lazy 1..3).WHAT, Seq, 'lazy 1..3 is a Seq');
ck((lazy 1..3).gist, '(...)', 'which gists as (...)');
ck((lazy 1..3).raku, '(1, 2, 3).lazy.Seq', 'and .raku says it was lazy');
ck((lazy (1, 2, 3)).is-lazy, True, 'lazy of a List');
ck([lazy 1..3].is-lazy, True, '[lazy 1..3]');
ck((lazy 1..3).Array.is-lazy, True, '(lazy 1..3).Array');
ck((lazy 1..3).list.is-lazy, True, '(lazy 1..3).list');

my @y = lazy 1..3; @y[0] = 9;
ck(@y.is-lazy, True, 'storing an element leaves it lazy');
ck(@y[0], 9, 'and stores');

my @h = 1..*;
refuses({ +@h }, 'an endless Array refuses to numify too');
my @plain = 1, 2, 3;
ck(+@plain, 3, 'an ordinary Array still numifies');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
