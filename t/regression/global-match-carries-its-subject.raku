# Regression: every Match from a `:g` match must carry the WHOLE subject as its
# .orig, exactly as a single match does. It did not — `.match(:g)`, `.comb(:match)`
# and the s/// adverb family all build their Matches in `substSelect`, which
# omitted the subject that the `m//` path attaches. Two consequences, one loud
# and one silent:
#
#   * `.orig` / `.prematch` / `.postmatch` answered the MATCHED TEXT rather than
#     the subject — visibly wrong, but only if you looked.
#   * `.from` / `.to` fell through the no-subject branch of the byte→grapheme
#     conversion and reported BYTE offsets. Correct by accident on ASCII, wrong
#     on any subject holding a multi-byte character, which is why it survived:
#     the offsets drift further the more non-ASCII precedes the match.
#
# Found by a tool, not by a test: tools/check-figures.raku scans docs for a
# figure and then looks BACKWARD from `$m.from` for the count beside it. On
# docs/status/ROAST.md — which is full of em dashes — the window it cut opened
# 67 characters late and swept in text from AFTER the match, so the checker
# accused a paragraph whose arithmetic was correct.
#
# Every expectation below is Rakudo's own answer, so this file passes unmodified
# under Rakudo too. Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got «$got» want «$want»") unless $got eq $want }

# Three em dashes: 1 character each, 3 BYTES each. Character offsets of a/b/c
# are 2/6/10; the byte offsets are 4/10/16.
my $s = "— a — b — c";

my @m = $s.match(/<[abc]>/, :g);
check(+@m,                        3,                 'three matches');
check(@m.map(*.from).join(','),   '2,6,10',          '.from is a character offset');
check(@m.map(*.to).join(','),     '3,7,11',          '.to is a character offset');
check(@m.map(*.Str).join(','),    'a,b,c',           'the matched text');

# .orig is the whole subject on every one of them, not the matched text
check(@m.map(*.orig).unique.join('|'), $s,           '.orig is the subject');
check(@m[0].prematch,             '— ',              '.prematch of the first');
check(@m[2].prematch,             '— a — b — ',      '.prematch of the last');
check(@m[0].postmatch,            ' — b — c',        '.postmatch of the first');

# ASCII stays right (it always was — that is why this went unnoticed)
my @a = "x1y2z3".match(/\d/, :g);
check(@a.map(*.from).join(','),   '1,3,5',           'ASCII .from unchanged');

# the same subject through .comb(:match), which shares substSelect
my @c = $s.comb(/<[abc]>/, :match);
check(@c.map(*.from).join(','),   '2,6,10',          '.comb(:match) .from');
check(@c[1].orig,                 $s,                '.comb(:match) .orig');

# a capture inside a :g match carries the subject too
my @p = "— k=1, k=2".match(/ 'k=' (\d) /, :g);
check(@p.map(*.from).join(','),   '2,7',             'captured-match .from');
check(@p[1][0].Str,               '2',               'the capture itself');
check(@p[1][0].orig,              "— k=1, k=2",      'the capture .orig is the subject');

if @fail { .say for @fail; say 'FAIL'; exit 1 }
say 'PASS';
