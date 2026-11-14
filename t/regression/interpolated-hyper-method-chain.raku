# A hyper method call joins an interpolated variable's method chain, as in
# Rakudo (rakudo#1460): `"%add».sort.map(…)"` stayed literal text. As for any
# link, the chain interpolates up to its last parenthesised call, and Rakudo
# takes a hyper link only where no bare `.name` is still pending.

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}

my %h = :2b, :1a;
my @a = <x y>;
check "%h».Int.values.sort.join(",")", '1,2', 'the issue\'s shape, on a hash (rakudo#1460)';
check "@a».uc()", 'X Y', 'a hyper call with parens';
check "@a>>.uc()", 'X Y', 'the ASCII spelling';
check "@a».uc.join("-")", 'X-Y', 'a bare hyper link committed by a later call';
check "@a».uc", '@a».uc', 'with no call after it, literal';
check "@a.sort».uc()", '@a.sort».uc()', 'not after a pending bare link (Rakudo leaves it literal)';
check "@a.sort()».uc()", 'X Y', 'after a committed call';
check "%h<a>»", '1»', 'a » that is no hyper call stays text';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
