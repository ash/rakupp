# Regression: a list operator followed by the contextualized match capture
# `@$<name>`. `make @$<seg>.map(~*).join('|')` parsed as a NULLARY `make`
# followed by a separate expression, so an action method made Nil — the
# capture variable lexes as a bare `$` with the `<` glued to it, which the
# listop-argument check did not take for the start of a term (`@$/` and `@$x`
# it did). `say @$<x>.elems` printed an empty line the same way.
#
# Contract: exit 0 + last line PASS. Runs unchanged under Rakudo.
my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku}") unless $got eqv $want }

grammar G { token TOP { <seg>+ % ',' }; token seg { \w+ } }
class A    { method TOP($/) { make @$<seg>.map(~*).join('|') } }
ck G.parse('a,bb,ccc', actions => A.new).made, 'a|bb|ccc', 'make @$<seg>…';

sub parse-segs(Str $s) {
    my class Acts { method TOP($/) { make @$<seg>.join('-') } }
    G.parse($s, actions => Acts.new).made
}
ck parse-segs('x,y'), 'x-y', 'the same in a `my class` inside a sub';

'q,r' ~~ / $<seg>=[\w] ',' $<seg>=[\w] /;
sub first-of(*@a) { @a[0] }
ck (first-of @$<seg>).Str, 'q', 'a user listop takes @$<seg> as its argument';
ck (join '+', @$<seg>), 'q+r', 'join @$<seg>';

if @fail { .say for @fail; say 'FAIL' }
else     { say 'PASS' }
