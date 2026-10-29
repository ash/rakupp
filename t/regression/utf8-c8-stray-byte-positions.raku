# Regression: a string decoded with utf8-c8 from bytes that hold a stray
# continuation byte answers positions on every call, not only the first
# (found while fixing issue #179).
#
# cowGraphemeIndex (src/Builtins.cpp) caches an EMPTY grapheme table for such
# a string, where decoding and a count of lead bytes disagree, to mean "no
# table: take the decoding path". The first call answered that; every later
# one handed the empty table back as a table, so `.substr` saw -1 characters
# ("should be in 0..-1"), `.index` from a position found nothing, and
# `.rindex` read before the table's start and died of SIGSEGV.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

my $s = 'abcdefghijklmnopqrstuvwxyz' ~ Blob.new(0x61, 0x80, 0x62).decode('utf8-c8');
ck $s.chars, 29, 'chars';
for ^2 {   # (the first call was right; the second is the one that was not)
    ck $s.index('a', 1), 26, 'index from a position';
    ck $s.index('b', 27), 28, 'index past the stray byte';
    ck $s.rindex('a'), 26, 'rindex';
    ck $s.substr(26, 1), 'a', 'substr before the stray byte';
    ck $s.substr(28), 'b', 'substr after it';
}

say 'PASS';
