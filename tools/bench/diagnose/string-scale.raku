# The quadratic detector — the sharpest tool in this directory, and the one
# that found the `.substr` bug in STRING-SCAN-QUADRATICS.md §5.
#
# It holds the OPERATION COUNT FIXED at 5,000 and grows the string. Identical
# work every row, so a rising column can only mean one thing: the per-call cost
# depends on the length of the string. That is the signature of an op which
# re-derives a string property by scanning it, or copies the whole invocant,
# on every call.
#
#     flat    -> correct: the length does not enter the per-call cost
#     rising  -> the bug. Roughly proportional to length is one such cost;
#                proportional to length AND called per character is the
#                quadratic that made a 421 KB parse take 13.9 s.
#
# Measured across the fix (5,000 calls, same in every row):
#
#     len  50000  substr x5000:  29 ms  ->  5 ms
#     len 100000  substr x5000:  44 ms  ->  5 ms
#     len 200000  substr x5000:  74 ms  ->  5 ms
#     len 400000  substr x5000: 135 ms  ->  5 ms
#
# Point it at any string method you suspect: swap the `.substr($n, 1).ord` line
# for `.index(…)`, `.comb`, a substitution, whatever is under suspicion. Runs
# under both engines.
for 5000, 10000, 20000, 40000 -> $k {
    my $s = "abcdefghij" x $k;      # 10*$k bytes, ASCII
    my $t0 = now; my int $n = 0; my $acc = 0;
    while $n < 5000 { $acc = $acc + $s.substr($n, 1).ord; $n = $n + 1; }
    say "len " ~ (10 * $k) ~ "  substr x5000: " ~ ((now - $t0) * 1000).Int ~ " ms";
}
