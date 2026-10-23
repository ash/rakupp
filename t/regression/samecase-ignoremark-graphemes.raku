# BROKE: case and mark transfer counted codepoints or ASCII bytes where Rakudo
# counts graphemes in any script (found 2026-10-23 by a differential of string
# operations over FatRoast's `mixed` palette, whose letters include a letter
# plus U+0332 COMBINING LOW LINE):
#
#   say "e̲xab".samecase("Ab");                      # e̲xab — the mark took "b"'s slot
#   my $x = "e̲E̲"; $x ~~ s:ii/\w+/xy/; say $x;     # xy, Rakudo xY — ASCII only
#   say "w̲x" ~~ m:m/ w̲x /;                          # False — the pattern's mark
#   say "ÉX" ~~ m:i:m/ ex /;                         # False — :i folded ASCII only
#
# FIXED: sameCase (src/Builtins.cpp) walks both strings by grapheme, and both
# `.samecase` and `s:ii`/`:samecase` ask it; an `:ignoremark` literal steps a
# grapheme of its own text as it steps one of the input, and `:i` with it
# compares the simple case folds. Every expectation checked against Rakudo
# 2026.09.
#
# The leading `:my` case: `/:my $x = 1; a/` was refused ("Unrecognized regex
# metacharacter ;") because the parser took the declaration for an adverb the
# lexer had prepended and checked the rest of the pattern without it.
#
# Contract: exit 0 + last line PASS.

my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }
sub m(Mu $m) { $m ?? ~$m !! Nil }

# --- .samecase ------------------------------------------------------------------
ck("e̲xab".samecase("Ab"), "E̲xab", 'a mark does not take a pattern position');
ck("ab".samecase("e̲X"), "aB", '…nor in the pattern');
ck("ПРИВЕТ".samecase("aB"), "пРИВЕТ", 'Cyrillic, the last pattern case repeating');
ck("FOO".samecase("a b"), "fOo", 'an uncased pattern character leaves it alone');
ck("abc".samecase(""), "abc", 'an empty pattern changes nothing');

# --- s:ii -----------------------------------------------------------------------
{ my $x = "e̲E̲e̲E̲ q"; $x ~~ s:ii/\w+/xyz/; ck($x, "xYz q", 's:ii over marked letters') }
{ my $x = "Привет"; $x ~~ s:ii/привет/мир/; ck($x, "Мир", 's:ii in Cyrillic') }
{ my $x = "Foo"; $x ~~ s:ii/foo/bar/; ck($x, "Bar", 's:ii in ASCII still') }

# --- :ignoremark ------------------------------------------------------------------
ck(m("w̲x" ~~ m:m/ w̲x /), "w̲x", 'the pattern literal carries a mark');
ck(m("wx" ~~ m:m/ "w̲x" /), "wx", '…in quotes, over an unmarked input');
{ my $p = "w̲x"; ck(m("ŵx" ~~ m:m/ $p /), "ŵx", '…interpolated, over another mark') }
ck(m("e\x[301]x" ~~ m:m/ ex /), "éx", 'an unmarked pattern over a marked input');
ck(m("ÉX" ~~ m:i:m/ ex /), "ÉX", ':i:m folds beyond ASCII');
ck(m("ПРИВЕТ" ~~ m:i:m/ привет /), "ПРИВЕТ", ':i:m in Cyrillic');

# --- a leading :my ----------------------------------------------------------------
ck(m("ab" ~~ /:my $y = 1; a/), "a", '/:my $y = 1; a/ compiles and matches');
{ my $seen; "ab" ~~ /:my $z = 5; a { $seen = $z }/; ck($seen, 5, '…and the code sees it') }

if @fail { note $_ for @fail; say 'FAIL' }
else { say 'PASS' }
