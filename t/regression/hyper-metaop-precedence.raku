# Regression: a hyper metaop takes the PRECEDENCE of the operator inside it.
# `>>*<<` binds tighter than `>>+<<` exactly as `*` binds tighter than `+`,
# but rakupp classified every `>>OP<<` at one fixed additive level, so
# `(2,2) >>+<< (3,3) >>*<< (4,4)` evaluated left to right and answered
# (20 20) where Rakudo answers (14 14).
#
# Found sweeping The Weekly Challenge.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want
}

check ((2,2) >>+<< (3,3) >>*<< (4,4)).join(' '), '14 14', 'multiplication first';
check ((2,2) >>*<< (3,3) >>+<< (4,4)).join(' '), '10 10', '…whichever side it is on';
check ((2,2) >>*<< (3,3) >>**<< (2,2)).join(' '), '18 18', 'exponentiation binds tighter still';
check ((1,2) >>+<< (1,1) >>~<< (9,9)).join(' '), '29 39', 'concatenation is looser than +';
check ((10,10) >>-<< (2,2) >>-<< (3,3)).join(' '), '5 5', 'left-associative ops stay left-associative';
check ((2,2) >>**<< (3,3) >>**<< (2,2)).join(' '), '512 512', 'and ** stays right-associative';
check (2 <<+>> (1,2) <<*>> (10,10)).join(' '), '12 22', 'the one-sided forms follow the same rule';

if @fail { note "FAILED: " ~ @fail.join('; '); say 'FAIL' } else { say 'PASS' }
