# BROKE: a name that starts with a quote word and goes on beyond ASCII was cut
# at the quote word (found 2026-10-23 by FatRoast's names-v1, which renamed
# integration/advent2009-day21.t's `class Question` to `class Q̲갮e̲ò中갢갨丧`,
# a Q, U+0332 COMBINING LOW LINE, …):
#
#   class Q̲x { }; my $t = Q { a: b };   # Undeclared name 'Q'
#   class Q̲x { }; say Q̲x.^name;         # a Q quote delimited by U+0332
#
# The lexer's pre-scan for routines and types that shadow a quote word read a
# declared name in ASCII only, so `class Q̲x` declared a `Q` and every `Q { … }`
# after it was a type name; and tryQuoteForm took the mark after `Q` for an
# adjacent non-ASCII delimiter (`Q♥…♥`).
#
# FIXED: scanDeclaredSubNames reads the whole name (uniIdentCharAt /
# uniIdentEnd, src/Unicode.h), and tryQuoteForm starts no quote where an
# identifier character beyond ASCII follows the word. Checked against Rakudo
# 2026.09.
#
# Contract: exit 0 + last line PASS.

my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

class Q̲x { method m { 'm' } }
ck(Q { a: b }, ' a: b ', 'Q { … } stays a quote after class Q̲x');
ck(Q̲x.^name, 'Q̲x', 'Q̲x is the class');
ck(Q̲x.m, 'm', '…and its method');
sub q̲ { 1 }
ck(q{x}, 'x', 'q{…} stays a quote after sub q̲');
ck(q̲(), 1, 'q̲() calls the sub');
sub mé { 3 }
ck(mé, 3, 'mé is a call, not a match');
sub sé($x) { $x }
ck((sé 4), 4, 'sé 4 is a call, not a substitution');
ck(q«a b», 'a b', 'q«…» still quotes');
ck(Q♥raw♥, 'raw', 'Q♥…♥ still quotes');

if @fail { note $_ for @fail; say 'FAIL' }
else { say 'PASS' }
