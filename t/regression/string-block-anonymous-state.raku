# An anonymous state variable in a string's `{ … }` belongs to that block's
# closure, fresh each time the string is built, as in Rakudo (rakudo#1961):
# `"count {$++}" for ^3` counted 0 1 2, and two blocks in one string shared
# one `$`. A closure called again keeps its state, as before.

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}

check ("count {$++}" for ^3).join(','), 'count 0,count 0,count 0', 'fresh per string (rakudo#1961)';
sub g { "x{$++}" }
check (g() for ^3).join(','), 'x0,x0,x0', 'inside a sub';
check ("a{$++}b{$++}" for ^2).join(','), 'a0b0,a0b0', 'two blocks, two variables';
check ("c {state $s = 0; $s++}" for ^3).join(','), 'c 0,c 0,c 0', 'a named state, as before';
my &f = { $++ };
check (f() for ^3).join(','), '0,1,2', 'one closure keeps its state';
my $v = 5;
check "v={$v + 1}", 'v=6', 'a block reading an outer variable';
$_ = 3;
check "t={$_ * 2}", 't=6', 'the topic';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
