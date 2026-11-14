# Regression (RAKUDO-ISSUES-PLAN R5; rakudo#2217): set membership reads a
# Range as the Set of what it iterates.
#
# Before the fix `½ ∈ ^2` was True: the Range was asked whether ½ lay between
# its ends. Rakudo coerces it to a Set, so an Int range holds the Ints
# between its ends (and anything that is an Int: True, <1>), any other range
# the values it steps through, and an endless range is refused as lazy.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check(Str $what, $got, $want) {
    @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}
check '½ ∈ ^2 (rakudo#2217)', ½ ∈ ^2, False;
check 'an Int in it', (1 ∈ ^2, 2 ∈ ^2), (True, False);
check 'a Rat or a Str is no element', (1.0 ∈ ^2, '1' ∈ ^2), (False, False);
check 'what is an Int is', (True ∈ ^2, <1> ∈ ^2), (True, True);
check 'a Rat range holds its steps', (1.5 ∈ 1.5..3, 2.5 ∈ 1.5..3, 2 ∈ 1.5..3), (True, True, False);
check 'a Str range its strings', ('b' ∈ 'a'..'c', 'aa' ∈ 'a'..'c'), (True, False);
check '∉ and ∋', (1 ∉ ^2, ^2 ∋ ½, ^3 (cont) 2), (False, False, True);
check 'a bigint end', 10**30 ∈ 0..10**42, True;
my $msg = 'nothing';
try { my $in = 5 ∈ 1..*; CATCH { default { $msg = .message } } }
check 'an endless range is lazy', $msg, 'Cannot (elem) a lazy list';

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
