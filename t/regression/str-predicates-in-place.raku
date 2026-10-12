# Regression: .starts-with / .ends-with / .contains($needle, $pos) on a long
# string cost time in proportion to its length (the #154 shape): every call
# copied the whole haystack, and .contains with a position decoded it twice
# more. 22 us a call on a 1M-char string, 87 us for .contains($n, $pos); now
# they read it in place and take positions off the cached grapheme table
# (t/scaling: str-contains-pos-walk, str-starts-ends-with).
#
# The answers must not change: whole graphemes only ("\r\n" is one, so is a
# base with its combining marks), positions in graphemes, and the general path
# (forced here by a no-op `:!i`) must agree with the fast one. Expectations are
# Rakudo 2026.09's.

my $fails = 0;
sub is($got, $want, $what) {
    return if $got eqv $want;
    $fails++;
    note "not ok - $what: got {$got.raku}, want {$want.raku}";
}

# long enough to be promoted to a shared body (the cached tables live there)
my $pad = 'x' x 100;
my @hay =
    $pad ~ "a\r\nb" ~ $pad,
    $pad ~ "e\x[301]z" ~ $pad,
    $pad ~ "日本語👍🏽" ~ $pad,
    "\x[1F1FA]\x[1F1F8]" ~ $pad ~ "é",
    'ab' x 200,
    "a\r\nb",
    "q\x[301]";
my @needles = "\r", "\n", "\r\n", "e", "\x[301]", "e\x[301]", "z", "日", "👍", "👍🏽", "\x[1F1FA]", "é", "x", "ab", "", "b";

for @hay.kv -> $hi, $h {
    for @needles.kv -> $ni, $n {
        for <starts-with ends-with contains> -> $m {
            is $h."$m"($n), $h."$m"($n, :!i), "$m hay $hi needle $ni";
        }
        for 0, 1, 99, 100, 101, 102, $h.chars - 1, $h.chars, $h.chars + 1 -> $p {
            next if $p < 0;
            is $h.contains($n, $p), $h.contains($n, $p, :!i), "contains hay $hi needle $ni at $p";
        }
    }
}

is "a\r\nb".starts-with("a\r"), False, 'a prefix ending inside "\r\n"';
is "q\x[301]".ends-with("\x[301]"), False, 'a suffix that is a lone combining mark';
is ($pad ~ "e\x[301]z").contains("e", 100), False, 'no hit inside a cluster, from a position';
is ($pad ~ "e\x[301]z").contains("z", 101), True, 'positions count graphemes';
is ($pad ~ "e\x[301]z").contains("z", 102), False, '...past the hit';
is ('ab' x 200).contains('b', 400), False, 'a start at the end';
is ('ab' x 200).contains('', 400), True, 'the empty needle at the end';
is ('ab' x 200).contains('b', 401), False, 'a start past the end';
is ('ab' x 200).starts-with(''), True, 'the empty prefix';

say $fails == 0 ?? 'PASS' !! "FAIL ($fails)";
exit $fails == 0 ?? 0 !! 1;
