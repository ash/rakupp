# The multi-dispatch cache remembers "no candidate takes this shape, the
# built-in answers" (2026-11-05, SPEED-BACKLOG 52). Math::NumberTheory adds six
# `infix:<gcd>` candidates, none for two Ints, and euler-phi's `$_ gcd $n`
# scored all six at every call before the built-in answered: ~11,000
# instructions a call, all of issue #47's driver's distance from Rakudo
# (euler-phi over 1 .. 3000: 3.37 s, Rakudo 1.54 s; 1.39 s with the entry).
# The entry only skips the scoring, so every answer below must be the one the
# full dispatch gives, whatever shape came first and however often it repeats.
#
# Expectations checked against Rakudo 2026.09
# (~/.rakubrew/versions/moar-2026.09/install/bin/rakudo). Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# Math::NumberTheory's shape: candidates for Complex and Rat operands only
multi infix:<gcd>(Int:D $a, Complex:D $b) { "int-complex" }
multi infix:<gcd>(Complex:D $a, Int:D $b) { "complex-int" }
multi infix:<gcd>(Rat:D $a, Rat:D $b)     { "rat-rat" }
multi infix:<gcd>(Int:D $a, Rat:D $b)     { "int-rat" }
multi infix:<gcd>(Rat:D $a, Int:D $b)     { "rat-int" }
ck((^20).map({ $_ gcd 12 }).List, (12, 1, 2, 3, 4, 1, 6, 1, 4, 3, 2, 1, 12, 1, 2, 3, 4, 1, 6, 1),
   'two Ints, twenty times: the built-in, every time');
ck(1.5 gcd 3, "rat-int", 'then a shape a candidate takes: the candidate');
ck(3 gcd 1.5, "int-rat", '…and its mirror');
ck(4 gcd 6, 2, 'and two Ints again: the built-in');
ck(3 gcd 1i, "int-complex", 'a Complex operand: its candidate');
my int $ni = 18;
ck($ni gcd 12, 6, 'a native int operand: the built-in');
ck(4 gcd 6 gcd 9, 1, 'a chain of two Int pairs');
ck(((1 | 2) gcd 6).raku, any(1, 2).raku, 'a Junction operand autothreads');
ck(((4, 6) Zgcd (6, 9)).List, (2, 3), '`Zgcd` over Ints');
ck(([gcd] 12, 18, 30), 6, '`[gcd]` over Ints');

# a block that adds a candidate for two Ints (narrower than the core's by its
# `where`): its own dispatcher, and a where-aware one
{
    multi infix:<gcd>(Int:D $a, Int:D $b where * > 100) { "mine" }
    ck(4 gcd 600, "mine", 'a candidate for two Ints added in a block takes them');
    ck((4 gcd 6, 4 gcd 600, 4 gcd 8).List, (2, "mine", 4), '…where its `where` holds, call after call');
}
ck(4 gcd 600, 4, '…and outside the block the built-in still answers');

# an operator with no built-in: no candidate is an error, every time
multi infix:<qq>(Str $a, Str $b) { "qq" }
ck("a" qq "b", "qq", 'a user-only operator takes its candidate');
for ^3 {
    my $e = (try { 1 qq 2 }) // $!;
    ck($e ~~ X::Multi::NoMatch, True, "…and two Ints are X::Multi::NoMatch (call {$_ + 1})");
}

# a named routine's built-in answers what its candidates decline
multi sub is-prime(Complex:D $z) { "gaussian" }
ck((^12).map(&is-prime).List, (False, False, True, True, False, True, False, True, False, False, False, True),
   'is-prime over Ints, beside a Complex candidate: the built-in');
ck(is-prime(3i), "gaussian", '…and the Complex candidate takes a Complex');

# a class operand after Int pairs were answered
class Pt { has $.x }
multi infix:<lcm>(Pt $a, Int $b) { "pt-lcm" }
ck((1 .. 6).map({ $_ lcm 4 }).List, (4, 4, 12, 4, 20, 12), 'Int pairs: the built-in `lcm`');
ck(Pt.new(x => 1) lcm 4, "pt-lcm", '…then a (Pt, Int) pair: the candidate');
ck(6 lcm 4, 12, '…then Ints again: the built-in');

# candidates over Bool and Rat as the only nominal types
multi f(Bool $b) { "bool" }
multi f(Rat $r)  { "rat" }
multi f(Str $s)  { "str" }
ck((f("x"), f("y"), f(True), f(0.5), f("z")).List, ("str", "str", "bool", "rat", "str"),
   'Bool, Rat and Str candidates, a Str first');

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
