# Regression: a Regex binds a `&`-sigil parameter in MULTI dispatch, and a
# Callable's loop phasers can be asked for.
#
# `rak(/ pattern /, :paths(…))` reaches `multi sub rak(&pattern, *%n)`; the
# candidate scorer required a Code for `&pattern` and refused the Regex
# ("Cannot resolve caller rak(); no matching multi candidate") although a
# plain sub's `&p` already bound one. rak then asks
# `&pattern.has-loop-phasers` and `.callable_for_phaser('FIRST')` to run a
# pattern's FIRST/NEXT/LAST itself; neither method existed.
#
# Every expectation was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

multi sub k(&p, *%n)  { "slurpy" }
multi sub k(&p, %n)   { "hash" }
ck(k(/a/, :x(1)), "slurpy", 'a Regex binds &p in a multi with a slurpy');
ck(k(/a/, {:x(1)}), "hash",   '…and the hash candidate still wins a Hash');
ck(k(* + 1, :x(1)), "slurpy", 'a WhateverCode binds it too');

my $plain = -> $x { $x + 1 };
ck($plain.has-loop-phasers, False, 'a block without phasers has none');
my $with = -> $x { FIRST { 1 }; LAST { 2 }; $x };
ck($with.has-loop-phasers, True, 'FIRST/LAST count as loop phasers');
ck($with.callable_for_phaser('FIRST')(), 1, 'the FIRST phaser as a callable');
ck($with.callable_for_phaser('LAST')(), 2,  '…and LAST');
ck(/a/.has-loop-phasers, False, 'a Regex has none');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
