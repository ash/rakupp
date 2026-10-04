# Regression guard for the regex search prefilters (Regex::buildPrefilter).
# An unanchored search now skips start positions whose byte cannot begin a
# match, and gives up at once when the subject lacks every literal a match
# must contain. Both must be unobservable: every case below is one where a
# careless filter would change the answer — case folding that crosses into
# non-ASCII, ignoremark, graphemes, look-behind reaching before the start,
# code that runs inside failing attempts, and every search entry point
# (:g, :ov, :ex, comb, subst, split, a later start position).
# (Perl 5 syntax has no case here: Rakudo 2026.09 refuses `m:P5`.)
#
# Every expectation below was checked against Rakudo 2026.09.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# the first byte of a match
ck(~("xxabcxx" ~~ / abc /), 'abc', 'a literal found mid-subject');
ck(("xxx" ~~ / abc /).so, False, 'and not found');
ck(~("xyzQQQ" ~~ / \w+ 'QQQ' /), 'xyzQQQ', 'a required literal present');
ck(("xyz" ~~ / \w+ 'QQQ' /).so, False, 'and absent');
ck(~("--wwq--" ~~ / [ 'zzq' | 'yyq' | 'xxq' | 'wwq' ] /), 'wwq', 'one literal of an alternation');
ck(("--wwx--" ~~ / [ 'zzq' | 'yyq' | 'xxq' | 'wwq' ] /).so, False, 'none of them');
ck(~("ab12" ~~ / \d+ /), '12', 'a class');
ck(~("a b" ~~ / <[\s]> b /), ' b', 'a whitespace class');

# case folding and ignoremark
ck(~("xxKxx" ~~ / :i k /), 'K', ':i matches the other ASCII case');
ck(~("x\x[212A]x" ~~ / :i k /), "\x[212A]", ':i k matches the KELVIN SIGN');
ck(~("xxäxx" ~~ / :m a /), 'ä', ':ignoremark a matches ä');
ck(~("xxa\x[308]xx" ~~ / :m a /), "a\x[308]", 'and a decomposed one');
ck(~("xxaxx" ~~ / :m <[\x[e1]]> /), 'a', ':ignoremark class: á takes a plain a');
ck(("xa xb" ~~ m:g:m/ <[\x[e1]]> /).elems, 1, 'and :g finds it');

# graphemes: no match may begin inside one
ck(("\x[1E0A]\x[323]" ~~ / \x[323] /).so, False, 'a combining mark inside a grapheme is not a start');
ck(("a\r\nb" ~~ / \n /).so, True, '\n matches the CRLF grapheme');

# zero-width terms at the start
ck(~("12ab" ~~ / <?after \d> ab /), 'ab', 'a look-behind before the first literal');
ck(("x ab" ~~ / <?after \d> ab /).so, False, 'and when it fails');
ck(~("x ab" ~~ / << ab /), 'ab', 'a word boundary first');
ck(("xab" ~~ / << ab /).so, False, 'and none inside a word');
ck(~("abc" ~~ / ^ abc /), 'abc', 'an anchored pattern');
ck(~("abc\ndef" ~~ / ^^ def /), 'def', 'a line anchor');
ck(~("aaa" ~~ / a* $ /), 'aaa', 'a pattern that can match empty');
ck(~("xyz" ~~ / a? /), '', 'and matches empty at the start');

# later start positions and every search entry point
ck(("ab ab ab" ~~ m:g/ ab /).elems, 3, ':g');
ck(("aaaa" ~~ m:ov/ aa /).elems, 3, ':ov');
ck(("abcab" ~~ m:ex/ a .* b /).elems, 3, ':ex');
ck("a1b22c333".comb(/ \d+ /).List, ('1', '22', '333'), 'comb');
ck("a-b-c".subst(/ '-' /, '+', :g), 'a+b+c', 'subst :g');
ck("a1b2c".split(/ \d /).List, ('a', 'b', 'c'), 'split');
ck(("abcabc" ~~ m:c(2)/ abc /).from, 3, ':c(2) starts later');
ck(("abc" ~~ m:c(1)/ abc /).so, False, 'and finds nothing past the only literal');

# code inside a failing attempt still runs at every position it ran before
my $n = 0;
"aaaa" ~~ / { $n++ } 'QQQ' /;
ck($n, 5, 'a code block before a missing literal runs once per position');
my $m = 0;
"xyz" ~~ / x { $m++ } 'QQQ' /;
ck($m, 1, 'one after the first literal runs where that literal matched');
my @seen;
"abc" ~~ / <?{ @seen.push($/.from); False }> /;
ck(@seen.elems, 4, 'an assertion block runs at every position');

# a subrule and an interpolated value are not second-guessed
my token q { 'QQQ' }
ck(~("xQQQ" ~~ / <q> /), 'QQQ', 'a lexical token');
my $lit = 'zz';
ck(~("azzb" ~~ / $lit /), 'zz', 'an interpolated variable');
ck(~("aXXb" ~~ / <{ 'XX' }> /) eq 'XX', True, 'an interpolated block');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
