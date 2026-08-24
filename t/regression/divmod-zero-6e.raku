# Divide-by-zero and the language revision — oracle-verified against Rakudo
# 2026.08, shape by shape:
#
#   1 div 0    6.d: Failure (soft)                        6.e: Failure
#   1 mod 0    6.d: throws X::Numeric::DivideByZero       6.e: Failure
#
# core.e redoes the div/mod candidates, which moves `mod` to the soft-fail
# side under the pragma; 6.d keeps the eager throw roast pins
# (S03-operators/arith.t, the issue-2125 block).
#
# The 6.e half runs through EVAL because rakupp's EVAL RAISES the revision —
# a deliberate divergence (Rakudo 2026.08 accepts the inner pragma but leaves
# it inert; docs/guide/faq/6e.md §2), so this file also pins that. It is
# rakupp-only for exactly that reason: under Rakudo the EVAL'd pragma would
# not turn 6.e on.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

# 6.d (this unit's revision): div soft-fails, mod throws on the spot.
my $d = 1 div 0;
check($d.^name, 'Failure', '6.d: div by zero answers a Failure');
my $threw = False;
try { my $x = 1 mod 0; }
if $! {
    $threw = True;
    check($!.^name.contains('DivideByZero') || $!.message.contains('divide'), True,
          '6.d: mod by zero throws the DivideByZero shape');
}
check($threw, True, '6.d: mod by zero throws on the spot');

# 6.e (raised inside EVAL): both soft-fail.
check(EVAL(q[use v6.e.PREVIEW; (1 div 0).^name]), 'Failure', '6.e: div by zero answers a Failure');
check(EVAL(q[use v6.e.PREVIEW; (1 mod 0).^name]), 'Failure', '6.e: mod by zero answers a Failure (2026.08 core.e)');

if @fail {
    .say for @fail;
    say 'FAIL';
    exit 1;
}
say 'PASS';
