# Regression: `"<$k>"` in a regex matches the text `<` $k `>` (issue #188).
#
# The interpolation pass reads `<$var>` — `<` right before the variable and
# `>` right after — as the assertion that compiles the value as a regex. It
# did so inside a double-quoted span too, where the angles are only text, so
# `/ "<$k>" /` looked for `abc` as a pattern with the quotes around nothing
# and never matched `<abc>`; `"<$k"`, `"$k>"` and `"<{$k}>"` were right.
# An XML field extractor, `/ "<$tag>" (.*?) "</$tag>" /`, found nothing.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

my $k = 'abc';
ck so('x<abc>y' ~~ / "<$k>" /), True, '"<$k>" matches <abc>';
ck so('xabcy'   ~~ / "<$k>" /), False, '"<$k>" needs the angles';
ck so('x<abc> y' ~~ / "<$k> " /), True, 'with more text in the quotes';
ck so('x<a=abc>y' ~~ / "<a=$k>" /), True, '"<a=$k>" is text, not an aliased call';

my $tag = 't';
ck ~('<t>val</t>' ~~ / "<$tag>" (.*?) "</$tag>" /)[0], 'val', 'the XML field extractor';

# …and outside quotes the assertion forms keep compiling the value
my $p = 'a.c';
ck so('xabcy' ~~ / <$p> /), True, '<$p> compiles its value';
ck ~('xabcy' ~~ / <x=$p> /)<x>, 'abc', '<x=$p> captures under x';
ck so('xa.cy' ~~ / $p /), True, 'a bare $p matches literally';
ck so('xabcy' ~~ / $p /), False, '…and only literally';

say 'PASS';
