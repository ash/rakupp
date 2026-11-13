# A quoted word in an operator name's « » list is the word, as in Rakudo
# (rakudo#1264): `sub infix:«"z"» (…)` declared an operator named `"z"`,
# quotes and all, so `42 z 42` and `"42 z 42".EVAL` never reached it.

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}

sub infix:«"z"» ($, $) { 'here' }
check (42 z 42), 'here', 'the operator is z';
check '42 z 42'.EVAL, 'here', '…in EVAL too (rakudo#1264)';
check &infix:<z>(1, 2), 'here', '…by its angle-bracket name';
sub infix:<<"zz">> ($, $) { 'there' }
check (1 zz 2), 'there', 'the ASCII << >> spelling';
sub infix:«zzz» ($, $) { 'plain' }
check (1 zzz 2), 'plain', 'an unquoted word, as before';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
