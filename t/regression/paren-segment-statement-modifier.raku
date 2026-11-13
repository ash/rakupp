# Every `;` segment of a parenthesised list takes a statement modifier, as the
# first one already did and as in Rakudo (found while probing for
# RAKUDO-ISSUES-PLAN R7): `($n++; last if $n > 3) for 1..*` was "Two terms in
# a row".

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}

my $n = 0;
($n++; last if $n > 3) for 1..*;
check $n, '4', 'a modifier in the second segment';
check (1; 2 if True).raku, '(1, 2)', '`if` true';
check (1; 2 if False; 3).raku, '(1, 3)', '`if` false leaves no value';
check (;1 if True).raku, '(Nil, 1)', 'after an empty first segment';
check (1; $_ * 2 for ^3)[1].join(','), '0,2,4', 'a `for` modifier';
check (1 if True; 2).raku, '(1, 2)', 'the first segment, as before';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
