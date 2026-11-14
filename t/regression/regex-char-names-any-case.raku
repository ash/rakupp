# `\c[NAME]` in a regex takes the name in any case, as a string does and as in
# Rakudo (rakudo#5799): `/o\c[combining ring above]/` resolved the name to
# nothing, so the pattern was a bare `o`, which cannot match the grapheme o̊,
# and `/.\c[combining ring above]/` matched it, the `.` taking the whole
# grapheme and the empty escape nothing. A base and its mark are one literal.

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}

my $s = "o\c[combining ring above]";
check ($s ~~ /o/).so, 'False', 'a bare o is not the grapheme o̊';
check ($s ~~ /.\c[combining ring above]/).so, 'False', 'the . takes the whole grapheme (rakudo#5799)';
check ($s ~~ /o\c[combining ring above]/).so, 'True', 'a base and a named mark are one literal';
check ($s ~~ /o\c[COMBINING RING ABOVE]/).so, 'True', '…an upper-case name, as before';
check ($s ~~ /\x[6F]\x[30A]/).so, 'True', '…two \x escapes';
check ($s ~~ /"o\c[combining ring above]"/).so, 'True', '…inside a quoted literal';
check ('a' ~~ /\c[latin small letter a]/).Str, 'a', 'a lower-case name alone';
check ('a' ~~ /"\c[latin small letter a]"/).Str, 'a', '…quoted';
check ($s ~~ /\c[combining ring above]/).so, 'False', 'a mark alone is not the grapheme';
check ('ao̊b' ~~ /a o\c[combining ring above] b/).Str, 'ao̊b', 'inside a longer pattern';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
