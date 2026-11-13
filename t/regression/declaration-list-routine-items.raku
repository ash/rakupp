# A `&` item in a declaration list takes a signature after it, and declares a
# routine name the program may call, as in Rakudo (rakudo#2336):
# `my (&op:(Int, Int --> Int)) = …` failed to parse, "expected ) (got ':')",
# and `my (&op) = …; op(2, 3)` was "Undeclared routine". Rakudo does not
# check the signature against what is assigned, and neither does Raku++.

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}

my (&op:(Int, Int --> Int)) = (sub (Int $a, Int $b --> Int) { $a * $b });
check op(2, 3), '6', 'a signature on a list item (rakudo#2336)';
my (&f:(Int), $x) = (sub (Int $i) { $i }), 5;
check "{f(4)} $x", '4 5', '…beside another item';
my (&g) = (sub ($a, $b) { $a + $b });
check g(2, 3), '5', 'a routine declared in a list is callable';
my (&h:(Str)) = (sub (Int $i) { $i });
check h(7), '7', 'the signature is not checked, as in Rakudo';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
