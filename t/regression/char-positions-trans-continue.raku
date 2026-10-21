# Regression: positions that count CHARACTERS (graphemes) where a string holds
# CR LF or non-ASCII text.
#
# 8edfb4c7 made `:continue(N)` and `:pos(N)` count characters, not bytes. Two
# callers were left speaking the old units:
#
#   · `.trans` with a Regex key handed `:c` a CODEPOINT count and read the
#     match's .from/.to back as codepoints, so after a CR LF or a combining
#     mark it replaced in the wrong place:
#     `"line1\r\nline2 x".trans(/x/ => "X")` was "line1\r\nline2Xx".
#   · a bare `m:c` (continue from `$/`) took `$/`'s BYTE offset for a
#     character count, so after any non-ASCII text it started too far on:
#     `$_ = "ééaa"; m/a/; m:c/a/` found nothing.
#
# And the conversion itself walked every grapheme from the front on each
# call, which made a `m:c($pos)` tokenizer quadratic with a large constant
# (5,000 ASCII tokens: 29.9G instructions, now 0.46G). Found by the
# 2026-10-21 review. `.contains`, `.comb` and `.split` with a regex now take
# their pattern from the per-thread cache instead of compiling it on every
# call; a `<?{…}>` in the pattern still runs.
#
# Runs clean under Rakudo too.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# ---- .trans with a Regex key ----
ck "line1\r\nline2 x".trans(/x/ => "X"), "line1\r\nline2 X", '.trans(/rx/) after a CR LF';
ck "\r\n\r\nab".trans(/b/ => "B", /a/ => "A"), "\r\n\r\nAB", '…two keys after two CR LFs';
ck "q\x[301]ab".trans(/b/ => "B"), "q\x[301]aB", '…after a combining mark';
ck "ééxé".trans(/x/ => "-"), "éé-é", '…after precomposed letters';
ck "a\r\nb".trans(/\n/ => "N"), "aNb", '…a CR LF is one character, all of which \n matches';

# ---- a bare m:c continues from $/ ----
{
    $_ = "ééaa";
    m/a/;
    ck $/.from, 2, 'the first match';
    m:c/a/;
    ck ($/ ?? $/.from !! Nil), 3, 'm:c continues after it, past non-ASCII text';
    $_ = "éébab";
    m/b/; m:c/b/;
    ck ($/ ?? $/.from !! Nil), 4, '…and finds the next one';
}

# ---- a m:c($pos) tokenizer ----
{
    my $s = "ab€ q\x[301]x \r\nyz ";
    my @w; my $pos = 0;
    while $s ~~ m:c($pos)/\w+/ { @w.push: ~$/; $pos = $/.to }
    ck @w, ['ab', "q\x[301]x", 'yz'], 'm:c($pos) steps word by word over mixed text';
    my $a = 'abc ' x 200; my $n = 0; $pos = 0;
    while $a ~~ m:c($pos)/\w+/ { $n++; $pos = $/.to }
    ck $n, 200, '…and over ASCII';
}

# ---- regex methods from the pattern cache ----
for ^3 {
    ck "a b  c".split(/\s+/).List, ("a", "b", "c"), '.split(/rx/), call after call';
    ck "abcab".comb(/<[a..b]>+/).List, ("ab", "ab"), '.comb(/rx/)';
    ck "abcd".contains(/c/, 1), True, '.contains(/rx/, pos)';
    ck "abcd".contains(/a/, 1), False, '…and from past the only match';
    ck "a1b2c".split([/\d/, /b/]).List, ("a", "", "", "c"), '.split with several regex delimiters';
}
my $seen = 0;
ck "aXbYc".split(/<[XY]> <?{ $seen++; True }>/).List, ("a", "b", "c"), 'a code assertion in .split runs';
ck $seen > 0, True, '…really';

say $fails ?? "FAIL ($fails)" !! 'PASS';
