# Regression: a redispatch from the last USER candidate of an operator the
# language has reaches the built-in. `multi infix:<*>(UInt $a, UInt $b) {
# callsame() mod $m }` — modular arithmetic layered over the core `*`, the shape
# of mutsu's bench-multi-dispatch.raku — got Nil from callsame, warned "Use of
# Nil in numeric context" on every call and computed the wrong checksum. A call
# no user candidate takes already reached the built-in; a redispatch that ran
# past the user candidates did not.
#
# Every expectation below was checked against Rakudo 2026.09.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

{
    multi infix:<*>(UInt $a, UInt $b --> UInt) { callsame() mod 7 }
    ck(5 * 4, 6, 'callsame from a user infix:<*> reaches the built-in *');
    my $x = 3;
    ck($x * 5, 1, 'with a variable operand too');
    ck((-2) * 3, -6, 'a call the user candidate refuses is the built-in alone');
}
ck(5 * 4, 20, 'outside the block the operator is the core one again');

{
    multi infix:<+>(Int $a, Int $b where * > 100) { "big " ~ nextsame() }
    ck(1 + 200, 201, 'nextsame returns the built-in result from the routine');
    ck(1 + 2, 3, 'and the narrower candidate leaves other calls alone');
}

{
    my $*modulus = 1_000_003;
    multi infix:<*>(UInt $a, UInt $b --> UInt) { callsame() mod $*modulus }
    multi infix:<+>(UInt $a, UInt $b --> UInt) { callsame() mod $*modulus }
    my $acc = 1;
    $acc = $acc * $_ + $_ for 1..2000;
    ck($acc, 221928, 'the benchmark shape: a modular fold over two operators');
}

# a user-only routine has no built-in behind it: the chain still ends in Nil
multi sub only-mine(Int $x) { callsame() // 'nil' }
ck(only-mine(1), 'nil', 'a routine the language does not have still ends in Nil');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
