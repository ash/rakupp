# Issue #139: a code block or code assertion in a GRAMMAR rule sees a
# capture's own captures.
#
#   `<al> <?{ $<al><s>.elems == 3 }>` read `$<al>` as a bare span: the `$/`
#   handed to the block was built from spans, so `$<al><s>` was Nil and the
#   assertion failed silently. The same span-only build lost a positional
#   capture's captures (`$0<s>`) and a quantified `(\w)+`'s list, and the
#   assertion was never handed the cursor's occurrence lists at all. Also, a
#   `$<item><sym>` inside a code block was refused at compile time as a
#   `<sym>` call outside a proto.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want }

my $*SEEN;
my @lines;
grammar T {
  # the issue's rows
  rule top-capture    { <al> <?{ $<al> }> }
  rule nested-capture { <al> <?{ $<al><s> }> }
  rule nested-count   { <al> <?{ $<al><s>.elems == 3 }> }
  rule in-code-block  { <al> { $*SEEN = so $<al><s> } }
  rule al { <s> [ ',' <s> ]* }
  token s { \w+ }

  # a positional capture's captures, in a block and in an assertion
  token pos-block  { (<s> ',' <s>) { $*SEEN = $0<s>.elems } }
  token pos-assert { (<s> ',' <s>) <?{ $0<s>.elems == 2 }> }
  # a quantified positional capture is its list
  token rep-assert { (\w)+ <?{ $0.elems == 3 }> }
  token rep-block  { (\w)+ { $*SEEN = $0.elems } }
  # two levels down, and through a quantified name
  token deep       { '[' <al> ']' }
  token deep-block { <deep> { $*SEEN = $<deep><al><s>[1].Str } }
  token lines      { [ <line> { @lines.push: $<line>[*-1]<w>.elems } ]+ }
  token line       { <w>+ % ' ' \n }
  token w          { \w+ }
  # a negative assertion decides on the nested capture too
  token neg-assert { <al> <!{ $<al><s>.elems > 2 }> }
}
grammar L {
  rule TOP  { <item>+ % ',' <?{ $<item>[0]<w>.Str eq 'x' }> }
  token item { <w> }
  token w    { \w+ }
}

for <top-capture nested-capture nested-count> -> $r {
  check so(T.parse('a, b, c', :rule($r))), True, $r;
}
$*SEEN = False; T.parse('a, b, c', :rule<in-code-block>);
check $*SEEN, True, 'in-code-block';

$*SEEN = Nil; T.parse('a,b', :rule<pos-block>);
check $*SEEN, 2, 'a positional capture\'s captures in a block';
check so(T.parse('a,b', :rule<pos-assert>)), True, '... in an assertion';
check so(T.parse('abc', :rule<rep-assert>)), True, '(\w)+ is a list in an assertion';
$*SEEN = Nil; T.parse('abc', :rule<rep-block>);
check $*SEEN, 3, '(\w)+ is a list in a block';
$*SEEN = Nil; T.parse('[a,b,c]', :rule<deep-block>);
check $*SEEN, 'b', 'two levels down';
T.parse("a b\nc d e\nf\n", :rule<lines>);
check @lines.List, (2, 3, 1), 'the last occurrence of a repeated name';
check so(T.parse('a,b', :rule<neg-assert>)), True,  'a negative assertion passes';
check so(T.parse('a,b,c', :rule<neg-assert>)), False, 'a negative assertion fails';
check so(L.parse('x, y, z')), True,  'an element of a quantified name';
check so(L.parse('q, y, z')), False, 'an element of a quantified name, refusing';

# `<sym>` read from a capture in a code block is not a `<sym>` call
my $ran = False;
grammar S {
  proto token item {*}
  token item:sym<num> { <sym> \d+ }
  token TOP { <item> { my $x = $<item><sym>; $ran = True } <?{ $<item><sym>; True }> }
}
check so(S.parse('num42')), True, '$<item><sym> in a code block compiles';
check $ran, True, '... and the block runs';

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
