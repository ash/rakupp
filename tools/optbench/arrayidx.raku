# Indexed array read-modify-write in a hot loop. The arithmetic is all int, but
# the operands are array ELEMENTS, and pass 3's lane leaves are literals and
# plain scalar variables only — so the lane refuses this loop and every step
# builds boxed `Value`s exactly as it does without `-O`.
#
# "array-element lanes — `@a[$i]` reads/writes inside the lane (a bounds + tag
# guard against the underlying vector)" is the third lever in OPTIMIZATION.md's
# "Limits and what's next". This is its measuring stick: the same shape as
# intsum, differing only in where the operands live.
my @a = 0 xx 1000;
my $i = 0;
while $i < 2_000_000 {
    my $k = $i % 1000;
    @a[$k] = @a[$k] + $i % 7;
    $i = $i + 1;
}
my $s = 0;
for @a -> $v { $s = $s + $v }
say $s;
