# Regression: a call written in a `token` (or `rule`) commits to the callee's
# first answer, even when the callee is a `regex` or a proto of them. What
# follows the call failing does not send the token back into the callee for a
# longer match; the token fails there. A `regex` calling a `regex` still
# backtracks in.
#
# The grammar path threaded the caller's continuation through every non-ratchet
# callee, whatever the caller was, so the parser's `noBack` mark on the call
# was never read. Template::Mustache's
#   token linetag { ^^ (\h*) <tag> \h* [\n|$] }
# then stretched `regex tag:sym<section> { … (\N*?) … }` over
# `{{#a}}{{line}}` to reach the newline, and its section came out named
# "a}}{{line": every Iterable section rendered empty (t/11-iterable).
#
# Every expectation below is Rakudo's.
# Contract: exit 0 + last line PASS.
my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want }

grammar TokRegex   { token TOP { <t> 'x' }; regex t { a .*? b } }
grammar RegexRegex { regex TOP { <t> 'x' }; regex t { a .*? b } }
grammar TokProto   { token TOP { <t> 'x' }; proto regex t {*}; regex t:sym<a> { a .*? b } }
grammar Nested     { regex TOP { <u>* }; token u { <t> 'x' | 'y' }; regex t { a .*? b } }
grammar Alias      { token TOP { <w=t> 'x' }; regex t { a .*? b } }

ck so(TokRegex.parse('abbx')),     False, 'a token does not backtrack into a regex';
ck so(TokRegex.parse('abx')),      True,  '…and takes its first answer when that fits';
ck so(RegexRegex.parse('abbx')),   True,  'a regex still backtracks into a regex';
ck so(TokProto.parse('abbx')),     False, 'a token does not backtrack into a proto regex';
ck Nested.subparse('abbx').Str,    '',    'nor does a token inside a regex';
ck so(Alias.parse('abbx')),        False, 'an aliased call commits too';
ck ~(Alias.parse('abx')<w> // ''), 'ab',  '…and records its capture under the alias';

# the Mustache shape: a standalone tag must END its line; a tag followed by more
# text is no linetag, so the plain `<tag>` takes `{{#a}}`
grammar Tags {
    token TOP     { [ <linetag> || <tag> || . ]* }
    token linetag { ^^ \h* <tag> \h* [\n | $] }
    proto regex tag {*}
    regex tag:sym<section> { '{{' '#' (\N*?) '}}' }
    regex tag:sym<var>     { '{{' (\w+) '}}' }
}
my $m = Tags.parse("\{\{#a}}\{\{line}}\n");
ck ~($m<tag>[0][0] // ''), 'a', 'the section is named "a", not "a}}{{line"';
ck $m<tag>.elems,          2,   'then {{line}} is a tag of its own';
ck $m<linetag>.elems,      0,   'and neither stands on its own line';

.say for @fail;
say @fail ?? 'FAIL' !! 'PASS';
