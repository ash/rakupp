# Regression (issue #99): a submatch's `.orig`, `.target`, `.prematch` and
# `.postmatch` are about the WHOLE string being matched, as `.from` and `.to`
# are. Grammar submatches — subrule calls, `$<x>=[…]`, `( )`, at any depth, in
# the finished tree, inside an action and inside a code block — carried only
# their own span, so an action working out a line number from
# `$/.orig.substr(0, $/.from)` counted from the enclosing match.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want
}
sub row($m) { "{$m.orig}|{$m.target}|{$m.prematch}|{$m.postmatch}|{$m.from}" }

grammar W
{
    token TOP { <w> [ ' ' <w> ]* }
    token w   { \w+ }
}
check row(W.parse('aa bb cc')<w>[1]), 'aa bb cc|aa bb cc|aa | cc|3', '<w> from the tree';

my $in-action;
class WA { method w($/) { $in-action = row($/) if ~$/ eq 'bb' } }
W.parse('aa bb cc', actions => WA.new);
check $in-action, 'aa bb cc|aa bb cc|aa | cc|3', '<w> inside the action';

grammar X { token TOP { \w+ ' ' $<x>=[\w+] } }
check row(X.parse('aa bb')<x>), 'aa bb|aa bb|aa ||3', '$<x>=[...] in a grammar';

grammar Y { token TOP { \w+ ' ' (\w+) } }
check row(Y.parse('aa bb')[0]), 'aa bb|aa bb|aa ||3', '( ) in a grammar';

grammar N
{
    token TOP  { <pair> ' ' <pair> }
    token pair { <w> '=' <w> }
    token w    { \w+ }
}
check N.parse('a=b c=d')<pair>[1]<w>[0].orig, 'a=b c=d', 'two levels down';

my $in-block;
grammar B { token TOP { <w> ' ' <w> { $in-block = row($<w>[1]) } }; token w { \w+ } }
B.parse('aa bb');
check $in-block, 'aa bb|aa bb|aa ||3', 'a submatch seen from a code block';

check row(W.subparse('xx yy zz')<w>[2]), 'xx yy zz|xx yy zz|xx yy ||6', 'subparse';

my $line;
grammar L
{
    token TOP { <w>+ % \n }
    token w   { \w+ }
}
class LA { method w($/) { $line = $/.orig.substr(0, $/.from).lines.elems + 1 if ~$/ eq 'cc' } }
L.parse("aa\nbb\ncc", actions => LA.new);
check $line, 3, 'line number from .orig in an action';

if @fail { .say for @fail; say "FAIL" } else { say "PASS" }
