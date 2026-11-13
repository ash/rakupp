# A `*` that opens a list operator's arguments with a tight hyper method call is
# a WhateverCode, as in Rakudo (rakudo#5901): `say *>>.fmt("%02x")(42)` died
# "unexpected operator in term position (got '>>')", the `*` read as `say`
# multiplied by something.

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}

sub first-arg(|c) { c[0] }
check first-arg(*>>.fmt("%02x"))(42).join, '2a', 'in parentheses, as before';
my $c = first-arg *>>.fmt("%02x");
check $c(42).join, '2a', 'as a list operator\'s first argument (rakudo#5901)';
my $u = first-arg *».uc;
check $u(<a b>).join(','), 'A,B', 'the guillemet spelling';
check (first-arg *>>.uc, 5).raku.contains('Whatever') || (first-arg *>>.uc, 5) ~~ Callable, 'True', 'a comma after it ends the argument';
my @a = 1, 2;
check 3 * @a>>.Int, '6', 'a spaced `*` is still multiplication';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
