# Regression: two ways the lexer lost the end of a regex literal.
#
# 1. In `rx/…/`, `m/…/` and `rx{…}` (Lexer's delimited-quote scanner) a `{`
#    inside a character class opened an embedded code block, which never
#    closed: `rx/ <[{"]> /` ran on to the end of the file. The class check
#    came after the block check; a `{` in a class is now a member.
# 2. In a bare `/…/`, a quote inside a `[ … ]` GROUP inside an assertion —
#    `/<!before ['#']>/` — was taken for a class member, because the scanner
#    told a class by "inside <…> and […] at once". Its `#` then started a
#    comment that swallowed the closing `/`. Classes have their own flag now,
#    so a group's quotes quote.
#
# Both found by RakuPress (a WordPress port), whose wptexturize port spells
# the WordPress regexes out with exactly these forms. Answers are Rakudo
# 2026.09's.
#
# Contract: exit 0 + last line PASS.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — got {$got.raku}, want {$want.raku}" }
}

ck ('a{b' ~~ rx/ <[{"]> /).Str, '{', 'rx/…/ with { and " in a class';
ck ('a{b' ~~ m/ <[(\[{"\-]> /).Str, '{', 'm/…/ with an escaped [ and a { in a class';
ck ('a{b' ~~ rx{ <[{"]> }).Str, '{', 'rx{…} with a { in a class';
ck 'a{b}c'.subst(rx/ <[{}]> /, '', :g), 'abc', 'a { and a } in one class';
ck ('a1b' ~~ rx/ <[0..9]> { } /).Str, '1', 'a code block after a class is still a block';
ck 'a&b'.subst(/'&' <!before ['#' \d+] ';'>/, 'X', :g), 'aXb', "a quoted '#' in a group in an assertion";
ck 'a&#1;'.subst(/'&' <!before ['#' \d+] ';'>/, 'X', :g), 'a&#1;', '…which still asserts';
ck 'a&b'.subst(/<!before ['#']>/, 'X'), 'Xa&b', "<!before ['#']>";
ck ('x' ~~ / <[a]> | [ 'x' | "y" ] /).Str, 'x', 'quotes in a group after a class';
ck ('a-b' ~~ / <[a]> '-' /).Str, 'a-', 'a quote after a class';
ck (Q[say "hi"] ~~ / '"' [ <!["]> . ]* '"' /).Str, '"hi"', 'a quote in a class assertion is a member';
ck (Q[say "hi"] ~~ rx/ '"' [ <!["]> . ]* '"' /).Str, '"hi"', '…in rx/…/ too';
ck ('a"b' ~~ / <?["]> . /).Str, '"', '<?["]>';

say $fails ?? "FAIL: $fails" !! 'PASS';
exit $fails ?? 1 !! 0;
