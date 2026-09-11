# Regression: `my @list is List = …` declares a List, not an Array.
#
# JSON::Fast::Hyper's suite round-trips `my @list is List = 1, 2, 3, @array,
# %hash` through to-json/from-json and asks is-deeply against @list. The
# container trait was read and dropped, so @list was an Array and the
# comparison with the List that came back failed on the type alone.
#
# Every expectation was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my @array = <a b c>;
my %hash = a => 42;
my @list is List = 1, 2, 3, @array, %hash;
ck(@list.^name, 'List', 'the variable is a List');
ck(@list.elems, 5, 'the nested array and hash are items, not flattened');
ck(@list[3], @array, 'the array item');
ck(@list eqv (1, 2, 3, @array, %hash), True, 'and it is the equal of a List literal');
ck(@list.raku, '(1, 2, 3, ["a", "b", "c"], {:a(42)})', '.raku spells it as a List');
my @plain = 1, 2;
ck(@plain.^name, 'Array', 'an untouched declaration is still an Array');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
