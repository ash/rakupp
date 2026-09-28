# A gather nothing has pulled from yet, used as a NUMBER, counts what it will
# take (roast S32-list/flat.t: `plan 2 * make-test-data() + 21` planned 21
# tests where Rakudo plans 45). Its buffer is empty until something reads it,
# and the arithmetic operators counted the buffer; `+$seq` and `.elems` were
# right. Every line here answers the same under Rakudo 2026.08.

my $fails = 0;
sub check(Str $desc, $got, $want) {
    if $got eqv $want {
        say "ok - $desc";
    }
    else {
        $fails++;
        say "not ok - $desc";
        note "GOT [{$got.raku}] WANT [{$want.raku}]";
    }
}

sub mk { gather { take 1; take 2; take 3 } }

check('Int * gather', 2 * mk(), 6);
check('gather * Int', mk() * 2, 6);
check('Int + gather', 2 + mk(), 5);
check('gather - Int', mk() - 1, 2);
check('gather == Int', mk() == 3, True);
check('a gather in a variable', do { my $g = gather { take 1; take 2 }; 10 * $g }, 20);
check('a gather written inline', 2 * (gather { take $_ for ^4 }), 8);
check('prefix + still counts', +mk(), 3);
check('a plain Seq was already right', 2 * (1, 2, 3).Seq, 6);

say $fails == 0 ?? 'PASS' !! 'FAIL';
exit($fails ?? 1 !! 0);
