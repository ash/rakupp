# Regression: issue #154 (2026-10-11).
#
# `.index($needle, $pos)` / `.rindex` on a string beyond ASCII decoded the
# whole haystack on every call, so a scanning loop — `$text.index("\n", $pos)`
# — went quadratic. It now searches bytes through the grapheme table the
# string caches (the one `.substr` reads), taking a hit only on cluster
# boundaries. This file holds the answers; a 1 MB scan stays well inside
# the time a quadratic one needs.
#
# Expected answers are Rakudo 2026.09's (2026-10-11).

my $fails = 0;
sub check($got, $want, $what) {
    return if $got eqv $want;
    $fails++;
    say "not ok - $what\n  got:  {$got.raku}\n  want: {$want.raku}";
}

my $s = 'héllo wörld héllo';
check $s.index('l', 5), 9, 'index from a position, past an é';
check $s.index('héllo', 1), 12, 'a needle beyond ASCII';
check $s.rindex('l', 8), 3, 'rindex from a position';
check $s.index('é'), 1, 'index with no position';
check $s.rindex('héllo'), 12, 'rindex with no position';
check $s.index('', 4), 4, 'the empty needle answers the position';
check $s.index('l', 17), Nil, 'nothing at the end';
check "e\x[301]e".index('e', 0), 1, 'an e under a mark is no plain e';
check "e\x[301]e".index("e\x[301]"), 0, 'the whole cluster is';
check "e\x[301]e".index("\x[301]"), Nil, 'a mark alone is no match inside a cluster';
check "x\r\nyé".index("\n"), Nil, 'nor is half of a CRLF';
check "x\r\nyé".index("\r\n"), 1, 'the whole CRLF is';
check "🇺🇸🇫🇷x".index('🇫🇷', 0), 1, 'a flag after a flag';
check "🇺🇸🇫🇷x".index('x', 1), 2, 'and what follows it';

my $text = 'é' ~ "abc\n" x 250_000;
my $t = now;
my ($pos, $lines) = 0, 0;
while ($pos = $text.index("\n", $pos)).defined { $pos++; $lines++ }
check $lines, 250_000, 'a scan finds every line';
check now - $t < 15, True, 'and does not go quadratic';

if $fails { say "$fails failure(s)"; exit 1 }
say 'PASS';
