# rakudo#5027: a reduce over a pointy block with sigilless parameters
# `\labels_, \token_` failed to parse, "expected { (got '_')". The lexer read
# `token_ {` as the regex declarator `token`, a rule named `_` and its body.
# The keyword is a whole word now, and never follows a `\`. And a sigilless
# name in a list assignment holds a list as it is: `my (\a, \b) = (1, 2), 3`
# makes a the List, not $(1, 2).

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}

my &f = -> \labels_, \token_ { labels_{token_} // 'none' };
check f({ 3 => 'three' }, 3), 'three', '\token_ before a block (rakudo#5027)';
my &g = -> \labels, \token { labels{token} // 'none' };
check g({ 4 => 'four' }, 4), 'four', '\token itself';
my &h = -> \rule_, \regex_x { rule_ ~ regex_x };
check h('a', 'b'), 'ab', 'rule_ and regex_x';
my token tok { x }
check ('x' ~~ /<tok>/).Str, 'x', 'a real token declaration, as before';

my (\a, \b) = (1, 2), 3;
check a.raku, '(1, 2)', 'a sigilless name holds the list itself';
my ($c, $d) = (1, 2), 3;
check $c.raku, '$(1, 2)', 'a $ variable itemizes it, as before';

my Map \labels = reduce(
    -> Map \labels_, Int \token_ {
        labels_{token_}:exists ?? labels_ !! Map.new(labels_.list, Pair.new(token_, token_))
    },
    Map.new, |(1 .. 3));
check labels.raku, 'Map.new(("1" => 1,"2" => 2,"3" => 3))', 'the issue\'s reduce';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
