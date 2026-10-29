# Regression: the grapheme table a non-ASCII string builds for `.index`,
# `.substr` and their kin, and its `.chars`, cost one walk over the bytes
# (issue #179), and the clusters are the ones UAX #29 makes.
#
# cowGraphemeIndex (src/Builtins.cpp) decoded the whole string into codepoints,
# indexed every codepoint's byte offset, found the cluster starts among them
# and mapped those back to bytes: 17 ms a MB, so the first `$s.index("ab", $i)`
# on 4 MB with one é in it took 68 ms, and `.chars` decoded it all the same.
# Both walk the UTF-8 once now (graphemeWalkUtf8, src/Unicode.cpp), and a pair
# of codepoints below U+0300 with no CR first is a break without the property
# lookups. The cases below sit on that line: what is below it, and what joins
# a cluster across it.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

my $pad = 'é' x 24;   # long enough to be a string with a body, and not ASCII
sub at($tail) { $pad ~ $tail }

ck at("a\r\nb").chars, 27, 'CR LF is one character';
ck at("a\r\nb").index("\n"), Nil, '…so its LF is not found alone';
ck at("a\r\nb").index("b"), 26, '…and what follows it counts it once';
ck at("e\x[301]z").chars, 26, 'e and a combining acute are one character';
ck at("e\x[301]z").index("e"), Nil, '…so a bare e is not found in it';
ck at("e\x[301]z").index("z"), 25, '…and z comes after it';
ck at("\x[2FF]\x[300]").chars, 25, 'U+02FF takes the U+0300 after it';
ck at("\x[A9]\x[200D]\x[1F600]x").chars, 26, '© ZWJ emoji is one character';
ck at("\x[A9]\x[1F600]").chars, 26, '© then an emoji are two';
ck at("\x[0600]ab").chars, 26, 'a Prepend takes the next character';
ck at("\x[1F1FA]\x[1F1F8]\x[1F1FA]").chars, 26, 'two regional indicators pair, a third stands alone';
ck at("\x[1100]\x[1161]\x[11A8]a").chars, 26, 'a Hangul syllable of three jamo is one character';
ck at("\x[0915]\x[094D]\x[0937]a").chars, 26, 'a Devanagari conjunct is one character';
ck at("x\x[AD]y").chars, 27, 'a soft hyphen is a character of its own';
ck at("\x[A9]\x[200D]\x[1F600]x").index("x"), 25, 'index past an emoji sequence';
ck at("e\x[301]z").substr(24, 1), "e\x[301]", 'substr takes the whole cluster';
ck at("a\r\nb").substr(25, 1), "\r\n", '…and the whole CR LF';
ck (at("ab") x 3).index("ab", 27), 50, 'index from a position';
ck (at("ab") x 3).rindex("ab", 49), 24, 'rindex from a position';

# the cost: `.chars` and the first `.index` from a position, 16 MB with an é
# in every eight characters (before the fix ~0.5 s together here)
my $s = 'abcdéfgh' x (16 * 131072);
my $t = now;
ck $s.chars, 16 * 1048576, '.chars of 16 MB';
ck $s.index('ab', 9), 16, 'the first .index from a position';
my $took = now - $t;
unless $took < 0.15 {
    say "FAIL: .chars and the first .index on 16 MB took {$took.round(0.001)} s";
    exit 1;
}

say 'PASS';
