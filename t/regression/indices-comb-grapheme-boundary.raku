# Regression: `Str.indices` and `Str.comb(Str)` searched the UTF-8 bytes, so
# a needle that is only part of a grapheme (the "\r" or "\n" of a CRLF, a
# lone combining mark) was found inside it. `index`, `rindex`, `contains`,
# `split` and `subst` already refused such a match. CSV::Native's pure-Raku
# parser avoided `indices` for this reason.
#
# `indices` also counted each hit's position from the start of the string,
# which made it quadratic in the number of hits; it now counts on from the
# previous hit.
#
# Contract: exit 0 + last line PASS. Runs under Rakudo unchanged; every
# expectation was checked against Rakudo 2026.08 on 2026-10-19.
my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

ck("x\r\ny\rz\n".indices("\r"), (3,), 'no "\r" inside a "\r\n"');
ck("x\r\ny\rz\n".indices("\n"), (5,), 'no "\n" inside a "\r\n"');
ck("q\x[301]z".indices("\x[301]"), (), 'no combining mark inside its cluster');
ck("a\r\nb".comb("\n").elems, 0, 'comb finds no "\n" inside a "\r\n"');
ck("a\r\nb\r\n".comb("\r\n").elems, 2, 'but finds the "\r\n" itself');
ck("q\x[301]z\x[301]".comb("\x[301]").elems, 0, 'comb finds no lone mark either');
ck("ab\r\ncd".indices("cd"), (3,), 'positions after a "\r\n" count it once');
ck("aXbXc".indices("X", :overlap), (1, 3), ':overlap still works');
ck(("é" x 3).indices("é"), (0, 1, 2), 'non-ASCII positions are graphemes');
ck("aaaa".indices("aa", :overlap), (0, 1, 2), 'overlapping hits count on from each other');
ck("aaaa".indices("aa"), (0, 2), 'and so do plain ones');
ck("éxéxé".indices("x", 2), (3,), 'a start position still applies');
ck("abc".indices(""), (0, 1, 2, 3), 'an empty needle is still found everywhere');
my $long = ("é" ~ "x" x 9) x 2000;
ck($long.indices("é").elems, 2000, 'many hits in a long non-ASCII string');
ck($long.indices("é")[*-1], 19990, 'and the last one is where it should be');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
