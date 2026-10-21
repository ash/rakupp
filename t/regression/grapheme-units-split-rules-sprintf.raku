# Regression: five more places that counted codepoints or bytes where Raku
# counts characters (graphemes).
#
#   · `.split('')` split codepoints: "a\r\nb" gave a, CR, LF, b, and
#     "q\x[301]" a q and a stray mark.
#   · `<space>` and `<cntrl>` matched the CR of a CR LF alone, where `\s`
#     takes the whole character: `"a\r\nb" ~~ /a <space> <space> b/` matched.
#   · sprintf's `%.1s` and `%5s` counted codepoints: `sprintf("[%.1s]",
#     "q\x[301]x")` cut the mark off, and `%5s` padded a mark-carrying
#     string one short.
#   · `Match.raku` printed the engine's byte offsets: `("éab" ~~ /ab/).raku`
#     said `:from(2), :pos(4)`; .from and .to were right all along.
#   · `.indices($needle, $pos)` and `.contains(/rx/, $pos)` took $pos in
#     codepoints: `"a\r\nbab".indices("b", 3)` found the b at 2 too.
#
# Found by the 2026-10-21 review (O3). Runs clean under Rakudo too.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my $thumb = "\x[1F44D]\x[1F3FD]";     # 👍🏽 — a base and a skin-tone modifier
my $flags = "\x[1F1FA]\x[1F1F8]\x[1F1EB]\x[1F1F7]";   # 🇺🇸🇫🇷
my $family = "\x[1F468]\x[200D]\x[1F469]\x[200D]\x[1F467]";

# ---- .split('') ----
ck "a\r\nb".split('').List, ("", "a", "\r\n", "b", ""), "split('') keeps CR LF whole";
ck "q\x[301]".split('').List, ("", "q\x[301]", ""), '…and a letter with its mark';
ck "e\x[301]\x[302]z".split('').List, ("", "e\x[301]\x[302]", "z", ""), '…and with two';
ck "$thumb!".split('').List, ("", $thumb, "!", ""), '…an emoji with its modifier';
ck "{$flags}x".split('').List, ("", "\x[1F1FA]\x[1F1F8]", "\x[1F1EB]\x[1F1F7]", "x", ""), '…flags, one per pair';
ck "$family!".split('').elems, 4, '…a ZWJ family is one piece';
ck "a\r\nb".split('', 3).List, ("", "a", "\r\nb"), '…a limit counts characters';
ck "q\x[301]ab".split('', 2).List, ("", "q\x[301]ab"), '…the rest kept whole';
ck "a\r\nb".split('', :skip-empty).List, ("a", "\r\n", "b"), '…and :skip-empty';
ck "abc".split('').List, ("", "a", "b", "c", ""), 'ASCII as before';
ck "abc".split('', 2).List, ("", "abc"), '…with a limit';

# ---- <space> and <cntrl> over CR LF ----
ck so("a\r\nb" ~~ /a <space> b/), True, '<space> matches a CR LF';
ck so("a\r\nb" ~~ /a <space> <space> b/), False, '…as one character, as \s does';
ck so("a\r\nb" ~~ /a \s \s b/), False, '(\s)';
ck so("a\r\nb" ~~ /a <cntrl> b/), True, '<cntrl> matches a CR LF';
ck so("a\r\nb" ~~ /a <cntrl> <cntrl> b/), False, '…as one character';
ck so("\r\n" ~~ /^ <space> $/), True, "a CR LF alone is one <space>";
ck so("\r\n\r\n" ~~ /^ <space> ** 2 $/), True, '…two are two';
ck so("\r" ~~ /^ <space> $/), True, 'a lone CR is still one';
ck so("\n\r" ~~ /^ <space> <space> $/), True, '…and LF CR is two';
ck "x\r\ny z".comb(/<space>/).List, ("\r\n", " "), '.comb(/<space>/)';
ck ("a\r\nb" ~~ /<alpha> <space> <alpha>/).Str, "a\r\nb", 'between letters';

# ---- sprintf %s width and precision ----
ck sprintf("[%.1s]", "q\x[301]x"), "[q\x[301]]", '%.1s keeps the mark with its letter';
ck sprintf("[%5s]", "q\x[301]x"), "[   q\x[301]x]", '%5s pads by characters';
ck sprintf("[%-5s]", "q\x[301]x"), "[q\x[301]x   ]", '%-5s';
ck sprintf("[%05s]", "q\x[301]x"), "[000q\x[301]x]", '%05s';
ck sprintf("[%*s]", 6, "q\x[301]x"), "[    q\x[301]x]", '%*s';
ck sprintf("[%.2s]", "{$flags}x"), "[$flags]", '%.2s takes two flags';
ck sprintf("[%3s]", $thumb), "[  $thumb]", '%3s around an emoji with its modifier';
ck sprintf("[%5.1s]", "{$thumb}abc"), "[    $thumb]", '%5.1s';
ck sprintf("[%-3s]", "\r\n"), "[\r\n  ]", 'a CR LF is one character';
ck "q\x[301]x".fmt("[%5s]"), "[   q\x[301]x]", '.fmt';
ck sprintf("[%3s|%.2s|%-4s]", "abcdef", "abcdef", "ab"), "[abcdef|ab|ab  ]", 'ASCII as before';

# ---- Match.raku ----
ck ("éab" ~~ /ab/).raku, 'Match.new(:orig("éab"), :from(1), :pos(3))', 'Match.raku counts characters';
ck ("q\x[301]ab" ~~ /ab/).raku, 'Match.new(:orig("' ~ "q\x[301]ab" ~ '"), :from(1), :pos(3))', '…after a mark';
ck ("a\r\nbab" ~~ /b(a)b/).raku,
   'Match.new(:orig("a\r\nbab"), :from(2), :pos(5), :list((Match.new(:orig("a\r\nbab"), :from(3), :pos(4)),)))',
   '…after a CR LF, and in its captures';
ck ("{$flags}ab" ~~ /b/).raku, 'Match.new(:orig("' ~ $flags ~ 'ab"), :from(3), :pos(4))', '…after flags';
ck ("é ab" ~~ /$<w>=[a]b/).raku,
   'Match.new(:orig("é ab"), :from(2), :pos(4), :hash(Map.new((:w(Match.new(:orig("é ab"), :from(2), :pos(3)))))))',
   '…a named capture';
{
    my $m = "q\x[301]x" ~~ /x/;
    my $back = EVAL $m.raku;
    ck ($back.from, $back.to), (1, 2), 'EVAL of .raku gives the match back';
}

# ---- .indices and .contains(/rx/) from a position ----
ck "a\r\nbab".indices("b", 3), (4,), '.indices from a character position after CR LF';
ck "a\r\nbab".indices("b", 2), (2, 4), '…from the b itself';
ck "{$flags}abab".indices("ab", 3), (4,), '…after flags';
ck "a\r\nbab".indices("B", 3, :i), (4,), '…with :i';
ck "é\r\nab".indices("b", 3), (3,), '…after é and a CR LF';
ck "abcabc".indices("b", 2), (4,), 'ASCII as before';
ck "q\x[301]ab".contains(/a/, 2), False, '.contains(/rx/, $pos) after a mark';
ck "q\x[301]ab".contains(/a/, 1), True, '…from the a';
ck "q\x[301]ab".contains(/b/, 2), True, '…the b';
ck "\x[1F1FA]\x[1F1F8]ab".contains(/a/, 1), True, '…after a flag';
ck "\x[1F1FA]\x[1F1F8]ab".contains(/a/, 2), False, '…past the a';
ck "é\r\nab".contains(/a/, 3), False, '…after é and a CR LF';
ck "abc".contains(/c/, 2), True, 'ASCII as before';

say $fails ?? "FAIL ($fails)" !! 'PASS';
