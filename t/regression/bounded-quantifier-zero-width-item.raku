# A BOUNDED quantifier repeats a zero-width item, as Rakudo does (rakudo#4752):
# `'00000' ~~ m{^ 0 <same> ** 2..4}` never matched, because an iteration that
# consumed nothing ended the loop below the minimum. Rakudo repeats it to the
# maximum (or, frugal, to the minimum), recording each capture. Under `*` and
# `+` it is still taken once; Rakudo loops forever there.

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}

check ('00000' ~~ m{^ 0 <same> ** 2..4}).<same>.elems, '4', 'the issue (rakudo#4752)';
check ('00000' ~~ m{^ 0 <same> ** 2..200}).<same>.elems, '200', 'to the maximum';
check ('00' ~~ /0 <same> ** 3/).<same>.elems, '3', 'an exact count';
check ('00' ~~ /0 <same> **? 2..4/).<same>.elems, '2', 'frugal: to the minimum';
check ('00' ~~ /:r 0 <same> ** 2..4/).<same>.elems, '4', 'under :ratchet';
check ('00' ~~ /0 <same> ** 2..4 0/).Str, '00', 'what follows still matches';
check ('x' ~~ /x ("") ** 3/)[0].elems, '3', 'an empty capture group';
check ('ab' ~~ /a (<?before b>) ** 2..5 b/)[0].elems, '5', 'a lookahead';
check ('x' ~~ /x <wb> ** 2/).<wb>.elems, '2', 'a word boundary';
check ('00' ~~ /0 <same> ** 2..4 % ','/).so, 'False', 'a separator must still match';
check ('00' ~~ /0 <same> ** 1..100000/).<same>.elems, '100000', 'a long bounded run';
check ('aaa' ~~ /a ** 2..3/).Str, 'aaa', 'a consuming item is unchanged';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
