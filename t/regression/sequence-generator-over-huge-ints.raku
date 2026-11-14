# A sequence with a generator closure over Ints that outgrow a double
# (rakudo#2844). Each step numified the last element for the numeric
# step it was not using, and an Int of thousands of digits numified
# through its decimal string: `1, 1, * + * ... *` to 40k terms took 0.92 s
# here and 0.07 s in Rakudo. An Int of 36 limbs or more is past 1e315, so
# its Num is ±Inf with no string at all: 0.1 s. The addition then carried
# by dividing and grew its result a limb at a time: with a compare and one
# allocation, 0.03 s. These checks pin the values the faster paths must
# still give.

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}

my @fib = 1, 1, * + * ... *;
check @fib[999].chars, 209, 'the 1000th Fibonacci number has 209 digits';
check @fib[999] % 1_000_000, 228875, '…and ends in 228875';
check @fib[1999] - @fib[1998] - @fib[1997], 0, 'each term is the sum of the two before it';
check (1, 1, -> $a, $b { $a + $b } ... *)[3000] == @fib[3000], True, 'a pointy generator gives the same terms';
check (2, * * 3 ... *)[700], 2 * 3 ** 700, 'a one-argument generator past 1e308';
check ('a' ... *)[^4].join(','), 'a,b,c,d', 'a succ sequence, as before';

check (2 ** 1100).Num, Inf, 'an Int past every double numifies to Inf';
check (-(2 ** 1100)).Num, -Inf, '…and a negative one to -Inf';
check (10 ** 315 - 1).Num, Inf, '35 limbs, through the string, still Inf';
check (10 ** 308).Num, 1e308, 'an Int a double can hold is exact';
check (2 ** 1023).Num, 2e0 ** 1023, 'the largest power of two';
check 10 ** 400 > 1e308, True, 'a huge Int compares above a Num';
check (1, 10 ** 400 ... *)[2] == 2 * 10 ** 400 - 1, True, 'an arithmetic sequence over huge Ints stays exact';

# The addition itself, rewritten for the same issue: a carry is a compare.
check 999999999 + 1, 1000000000, 'a carry out of one limb';
check (10 ** 27 - 1) + 1, 10 ** 27, 'a carry through every limb';
check (2 ** 64 - 1) + (2 ** 64 - 1), 36893488147419103230, 'two full words';
check 0 + 10 ** 30, 10 ** 30, 'zero on the left';
check (-10 ** 30) + 10 ** 30, 0, 'a sum of zero';
my $big = 10 ** 40;
check $big + $big, 2 * 10 ** 40, 'an Int added to itself';
sub schoolbook(Str $a, Str $b) {    # decimal, digit by digit
    my @x = $a.flip.comb; my @y = $b.flip.comb; my $carry = 0; my @out;
    for ^max(@x.elems, @y.elems) -> $i {
        my $d = (@x[$i] // 0) + (@y[$i] // 0) + $carry;
        @out.push: $d % 10; $carry = $d div 10;
    }
    @out.push: $carry if $carry;
    @out.reverse.join.subst(/^0+ <?before \d>/, '')
}
my $bad = 0;
for ^300 {
    my $a = (1..9).pick ~ (^10).roll((0..120).pick).join;
    my $b = (1..9).pick ~ (^10).roll((0..120).pick).join;
    $bad++ if ($a.Int + $b.Int).Str ne schoolbook($a, $b);
}
check $bad, 0, '300 random sums agree with schoolbook addition';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
