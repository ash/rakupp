# Regression: a Callable used as a matcher, and a Regex as a Callable.
#
# `$matcher.ACCEPTS($name)` is `$matcher($name)` — paths filters every
# directory that way, its default matcher a pointy block — and rakupp had no
# ACCEPTS on a Block at all. And a Regex is a Method, so Routine, Block,
# Code, Callable: highlighter's regex needle binds to `Callable:D $needle`,
# where rakupp answered "expected Callable but got Regex". Built-in type
# objects accept by type check (`Pair.ACCEPTS($x)`), and a `--> Slip` return
# is satisfied by a Slip.
#
# Every expectation was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my $m = -> str $e { $e ne "x" };
ck($m.ACCEPTS("y"), True,  'a pointy block accepts by calling itself');
ck($m.ACCEPTS("x"), False, '…and rejects the same way');
my $w = * eq "t";
ck($w.ACCEPTS("t"), True, 'a WhateverCode in a variable does too');
ck(("y" ~~ $m), True, 'smartmatch against a block calls it');

sub takes(Callable:D $c) { 'callable' }
ck(takes(/a/), 'callable', 'a Regex binds to Callable:D');
ck((/a/ ~~ Callable), True, 'and smartmatches Callable');
ck((/a/ ~~ Code), True, 'and Code');

my $t = True;
ck($t.ACCEPTS("anything"), True,  'True.ACCEPTS answers True');
ck(False.ACCEPTS("anything"), False, 'False.ACCEPTS answers False');

ck(Pair.ACCEPTS((a => 1)), True,  'Pair.ACCEPTS a pair');
ck(Pair.ACCEPTS("a"),    False, 'but not a string');
ck(Regex.ACCEPTS(/a/),   True,  'Regex.ACCEPTS a regex');

sub sl(--> Slip) { (1, 2).Slip }
ck((sl()).elems, 2, 'a --> Slip return is satisfied by a Slip');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
