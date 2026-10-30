# Regression: an element BOUND to another (`%specs<x> := %data<tests>`) holds a
# Proxy, and every value context reads through it, as `~` and method calls
# already did. Prefix `+`/`-` counted the Proxy record's own three entries, and
# the zen slices (`<>`, `[]`) and `@(…)` walked those entries instead of the
# array. Template::Mustache's spec runner binds each file's tests that way,
# then plans `+$spec.value` tests and runs `for $spec.value<> { … }`: it
# planned 3 of 11 and died on "Type Array does not support associative indexing".
# A user's Proxy reads through FETCH the same way.
# Every expectation below is Rakudo's.
# Contract: exit 0 + last line PASS.
my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want }

my %data = tests => [ {a => 1}, {a => 2}, {a => 3}, {a => 4} ];
my %specs;
%specs<x> := %data<tests>;

ck +%specs<x>,  4,  'prefix + of a bound element';
ck -%specs<x>, -4,  'prefix -';
ck +%data<tests>, 4, '…and of the element it is bound to';
for %specs.pairs -> $spec {
    ck +$spec.value, 4, '+ of a bound element reached through .pairs';
    ck $spec.value<>.map(*<a>).List, (1, 2, 3, 4), 'the zen slice <> walks the array';
    ck $spec.value[].map(*<a>).List, (1, 2, 3, 4), 'and the zen slice []';
    ck @($spec.value).map(*<a>).List, (1, 2, 3, 4), 'and @(…)';
}

my $fetched = 0;
my $p := Proxy.new(FETCH => -> $ { $fetched++; [5, 6, 7] }, STORE => -> $, $ { });
ck +$p,          3, 'a Proxy numifies what FETCH answers';
ck @($p).elems,  3, '@(…) of a Proxy';
ck $p<>.elems,   3, 'the zen slice of a Proxy';
ck so($fetched), True, 'FETCH ran';

.say for @fail;
say @fail ?? 'FAIL' !! 'PASS';
