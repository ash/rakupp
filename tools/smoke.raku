# Cross-platform smoke test for a freshly built rakupp binary.
# Exercises the numeric tower, UTF-8 regex, strings, and real threads, then
# exits non-zero if anything is wrong — so CI on macOS/Linux/Windows fails loud
# on a broken build. Run: `rakupp tools/smoke.raku`.

my $errors = 0;
sub check($got, $want, $desc) {
    if $got eqv $want {
        say "ok - $desc";
    }
    else {
        $errors++;
        say "NOT OK - $desc (got {$got.raku}, want {$want.raku})";
    }
}

# Big integers (arbitrary precision)
check 2 ** 100, 1267650600228229401496703205376, "2 ** 100";
# Overflow promotion Int -> BigInt
check 9223372036854775807 + 1, 9223372036854775808, "int64 overflow promotes";
# Exact rationals
check (1/3 + 1/6), 1/2, "exact Rat arithmetic";
check (1/3 + 1/6).WHAT.^name, "Rat", "Rat stays a Rat";
# Rat comparison (cross-multiply path)
check (1/3 < 1/2), True, "Rat comparison";
# UTF-8 aware regex character classes
check ("café" ~~ /(<[a..ÿ]>+)/ ?? ~$0 !! ""), "café", "UTF-8 char-class match";
check "héllo".chars, 5, "grapheme count";
# String ops
check "Hello, World".uc, "HELLO, WORLD", "uppercase";
check ("a".."e").join, "abcde", "range join";
# Real threads (start/Promise) on a big stack
my @p = (^8).map: { start { [+] ^10000 } };
check @p.map(*.result).sum, 8 * ([+] ^10000), "8 threaded promises";

if $errors == 0 {
    say "ALL SMOKE CHECKS PASSED";
    exit 0;
}
else {
    say "SMOKE FAILURES: $errors";
    exit 1;
}
