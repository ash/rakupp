# Three silent wrong answers found by re-reading the Rakudo issue threads the
# sweep had marked UNCLEAR (RAKUDO-ISSUES-PLAN R12).

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}

# rakudo#6793: `~~` that holds values (the X, Z, hyper and reduce forms, and
# `infix:<~~>` called) never called a Callable on the right: always False
check ((1, 2) X~~ -> $ { True }).join(','), 'True,True', 'X~~ calls the Callable (rakudo#6793)';
check ((1, 2) Z~~ (-> $ { True }, -> $ { False })).join(','), 'True,False', 'Z~~';
check (1 »~~» -> $x { $x == 1 }).raku, 'Bool::True', '»~~»';
check ([~~] 1, -> $x { $x == 1 }), True, '[~~]';
check infix:<~~>(1, -> $ { True }), True, 'infix:<~~> called';
check ((1, 2) X!~~ -> $ { True }).join(','), 'False,False', 'X!~~';
check ((1, 2) X~~ *.so).join(','), 'True,True', 'a WhateverCode matcher';
check 3 ~~ -> $ { True }, True, 'the written form, as before';

# rakudo#4102: an encoding name nothing knows dies, as in Rakudo, where it
# handed back UTF-8 bytes
try "中".encode("gbk");
check $!.^name, 'X::Encoding::Unknown', '.encode("gbk") dies (rakudo#4102)';
check $!.message, "Unknown string encoding 'gbk'", '…saying which';
check "x".encode("latin1").bytes, 1, 'latin-1, as before';
check "é".encode("utf-16").bytes, 2, 'utf-16, as before';
check "x".encode("ascii").bytes, 1, 'ascii, as before';

# rakudo#1872: `.base(…, :no-trailing-zeroes)`, which the issue closed by adding
check <1/2>.base(10, 20, :no-trailing-zeroes), '0.5', 'a Rat drops its trailing zeroes (rakudo#1872)';
check 2.5.base(16, 4, :no-trailing-zeroes), '2.8', '…in any base';
check <3/1>.base(10, 3, :no-trailing-zeroes), '3', '…and the point, when nothing is left';
check 12.base(10, 3, :no-trailing-zeroes), '12.000', 'an Int keeps them, as in Rakudo';
check 1e0.base(10, 3, :no-trailing-zeroes), '1.000', '…and so does a Num';
check <1/3>.base(10, 5, :!no-trailing-zeroes), '0.33333', 'the negated flag changes nothing';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
