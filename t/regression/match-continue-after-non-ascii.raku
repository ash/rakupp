# Regression: `:continue(N)` and `:pos(N)` searched from BYTE N, not character N.
#
# A Raku position counts characters (graphemes), but the subject is held as
# UTF-8, and InterpreterRegex's match path handed the count straight to the
# byte-indexed search. With any non-ASCII text before N the search began early,
# so `.match($rx, :c(N))` could answer with a match that STARTS BEFORE N — a
# wrong answer, and an infinite loop for the common `:c($m.to)` scan, which
# found the same match again forever. `:p(N)` compared a byte offset with a
# character count and refused a match that was there. `m:c(N)/…/` had its own
# conversion, which counted codepoints rather than graphemes.
#
# Found by RakuPress (a WordPress port) scanning a post containing `×`.
# Every expectation below is Rakudo 2026.09's answer.
#
# Contract: exit 0 + last line PASS.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — got {$got.raku}, want {$want.raku}" }
}

my $t = ("× " x 10) ~ "<!-- a --> <!-- b -->";        # 'a' at 20, 'b' at 31
my $m = $t.match(/ '<!--' \s+ (\w) /, :continue(25));
ck $m.from, 31, ':continue searches from character 25';
ck ~$m[0], 'b', '…and finds the second comment';
ck $t.match(/ '<!--' /, :c(20)).from, 20, ':c at a match finds it';
ck $t.match(/ '<!--' /, :p(20)).from, 20, ':p at a match anchors there';
ck $t.match(/ '<!--' /, :p(21)), Nil, ':p one past it does not';
ck $t.match(/ '<!--' /, :c(100)), Nil, ':c past the end finds nothing';

# The scan that hung: each match resumes from where the last one ended.
my @found;
my $p = 0;
while $t.match(/ '<!--' \s+ (\w) /, :c($p)) -> $mm {
    @found.push: ~$mm[0];
    $p = $mm.to;
    last if @found > 5;
}
ck @found, ['a', 'b'], 'a :c scan visits each match once';

# A grapheme of two codepoints counts once: CR LF, and a letter with a mark.
my $g = "e\x[301]\r\nxyz";                              # x at 2
ck $g.match(/x/, :c(2)).from, 2, ':c counts graphemes, not codepoints';
ck $g.match(/y/, :p(3)).from, 3, ':p counts graphemes';

# The smartmatch form.
ck ($t ~~ m:c(25)/ '<!--' \s+ (\w) /) ?? ~$/[0] !! Nil, 'b', 'm:c(25)/…/';
ck ($g ~~ m:c(2)/x/) ?? $/.from !! Nil, 2, 'm:c(N) counts graphemes';

say $fails ?? "FAIL: $fails" !! 'PASS';
exit $fails ?? 1 !! 0;
