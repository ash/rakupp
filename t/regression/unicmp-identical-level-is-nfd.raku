# Regression: `unicmp` and `coll` broke a tie between equal collation keys
# on the code points as given. A Str is NFC, so `0627 0591 0653` (alef,
# etnahta, maddah) arrives as `0622 0591`, and sorted after `0622` itself;
# UTS #10's identical level (S3.10) compares the NFD forms, where it sorts
# before. Twelve of CollationTest_NON_IGNORABLE's 212,066 lines (18.0)
# disagreed: the Arabic and Hangul pairs below, and U+FFFE, which is lowest
# at the identical level as its primary is lowest of all. Found by
# t/oracle/unicode.raku (ORACLE-PLAN P3), which now passes all 207,603
# comparisons.
#
# The expected answers are CollationTest's: each pair is two consecutive
# lines of the file, whose order is the answer. Rakudo 2026.09 answers More
# for the first three groups (roast marks them TODO), so this is red there.
#
# Contract: exit 0 + last line PASS.

my $fails = 0;
sub check(@a, @b, Order $want) {
    my $got = @a.chrs unicmp @b.chrs;
    my $desc = "{@a.fmt('%04X')} unicmp {@b.fmt('%04X')} is $want";
    if $got === $want { say "ok - $desc" }
    else { $fails++; say "not ok - $desc"; note "GOT $got" }
}

# keys equal at all three levels, and the NFD forms decide
check (0x627, 0x591, 0x653, 0x61), (0x622, 0x61), Less;
check (0x627, 0x591, 0x654, 0x61), (0x623, 0x61), Less;
check (0x648, 0x591, 0x654, 0x61), (0x624, 0x61), Less;
check (0x64A, 0x591, 0x654, 0x61), (0x626, 0x61), Less;
check (0xAC00, 0x61), (0x1100, 0x1D165, 0x1161, 0x61), Less;
check (0x1100, 0x1175, 0x61), (0x1100, 0x1D165, 0x1175, 0x61), Less;
check (0x1112, 0x1161, 0x61), (0x1112, 0x1D165, 0x1161, 0x61), Less;
check (0x1112, 0x1175, 0x61), (0x1112, 0x1D165, 0x1175, 0x61), Less;

# U+FFFE before every other code point at the identical level
check (0x6C, 0xFFFE, 0x1D165, 0x66, 0x32, 0x61), (0x6C, 0x1, 0xFFFE, 0x66, 0x32, 0x61), Less;
check (0x6C, 0xFFFE, 0x1D165, 0x66, 0x61), (0x6C, 0x1, 0xFFFE, 0x66, 0x61), Less;
check (0x6C, 0x32, 0xFFFE, 0x1D165, 0x66, 0x61), (0x6C, 0x32, 0x1, 0xFFFE, 0x66, 0x61), Less;

# what did not change: canonical equivalents are Same, the reverse is More,
# and a key difference still decides before any tie-break
check (0xE9,), (0x65, 0x301), Same;
check (0x622, 0x61), (0x627, 0x591, 0x653, 0x61), More;
check (0x61,), (0x41,), Less;
check (0x61, 0x62), (0x61, 0x1, 0x62), More;

say $fails == 0 ?? 'PASS' !! 'FAIL';
exit($fails ?? 1 !! 0);
