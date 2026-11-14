# A reduction over ONE Array operand takes its elements one level deep, as the
# single-argument rule says: `[+] [[1,2],[3,4]]` adds two lists, 2 + 2. A
# compiled program flattened the operand all the way and answered 10, and
# `[Z]` of a matrix zipped one flat row. t/exe/parity.raku runs this file
# natively too.

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}

check ([+] [[1,2],[3,4]]), '4', '[+] of an Array of Arrays';
my @a = [1,2], [3,4];
check ([+] @a), '4', '[+] of an array variable';
check ([»+«] @a).join(','), '4,6', '[»+«] of an array variable';
check ([Z] [[1,2],[3,4]]).List.raku, '((1, 3), (2, 4))', '[Z] of a matrix';
check ([~] [<a b>, <c d>]), 'a bc d', '[~] of an Array of Lists';
check ([+] 1..4), '10', 'a Range still spreads';
check ([+] (1,2),(3,4)), '4', 'two operands, each a List';
check ([+] [1,2,3]), '6', 'an Array of numbers';
check ([max] [3,9,2]), '9', '[max] of an Array';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
