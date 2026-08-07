# The ⚛ family, oracle-verified against Rakudo 2026.07 (v3 parallel P4).
# The lexer used to DROP the ⚛ marker ("under the GIL, atomic ops are plain
# ops"), which compiled $x⚛++ to a plain racy ++ — the stress suite measured
# 12,540 of 20,000 increments surviving in parallel mode. The marker now
# reaches the parser and lowers to the atomic-* calls, which do their RMW
# under striped locks shared with cas. Passes under both engines.

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

my atomicint $a = 5;
$a⚛++;
check('postfix ⚛++ increments',            $a + 0, 6);
check('postfix ⚛++ returns the OLD value', $a⚛++, 6);
check('…and increments',                   $a + 0, 7);
check('prefix ++⚛ returns the NEW value',  ++⚛$a, 8);
check('prefix ⚛ reads',                    ⚛$a, 8);
$a ⚛= 42;
check('⚛= stores',                         $a + 0, 42);
check('atomic-fetch-add returns the OLD',  atomic-fetch-add($a, 8), 42);
check('…and adds',                         $a + 0, 50);
check('prefix --⚛ returns the NEW value',  --⚛$a, 49);
check('postfix ⚛-- returns the OLD value', $a⚛--, 49);
check('…and decrements',                   $a + 0, 48);
check('⚛+= returns the value AFTER',       ($a ⚛+= 2), 50);
check('⚛-= returns the value AFTER',       ($a ⚛-= 10), 40);
check('⚛= coerces numerics (Rakudo: 4.5 stores 4)', do { $a ⚛= 4.5; $a + 0 }, 4);
{
    # the exception TYPE differs by engine (Rakudo: X::AdHoc from the unbox;
    # rakupp: X::TypeCheck::Assignment, matching what roast pins for the
    # `my Int` scalar case) — what both agree on is that it THROWS
    my $threw = False;
    try { $a ⚛= 'foo'; CATCH { default { $threw = True } } }
    check('⚛= on atomicint rejects a Str', $threw, True);
    check('…and the value is untouched', $a + 0, 4);
}
my $plain = 's';
$plain ⚛= 't';
check('⚛= on a plain Scalar takes anything (atomic reference assignment)', $plain, 't');
full-barrier;
check('full-barrier lives', True, True);

say $fails == 0 ?? 'PASS' !! 'FAIL';
exit($fails ?? 1 !! 0);
