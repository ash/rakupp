# JIT: refused — and being refused is the whole of what it checks.
#
# A user `infix:<+>` shadows the built-in for the operand shapes it has
# candidates for. A kernel emits the BUILT-IN operator and may not call
# anything — that rule is what pins its slot pointers, and for --cnp what makes
# hoisting the registers sound — so there is nowhere to put the user's call.
# Both tier-up backends therefore refuse a loop whose operators the program
# overloads, and the loop stays interpreted.
#
# `--exe` does NOT refuse it: it can emit the call, and t/fixtures/operator-overloads.raku
# is where that half is checked. What this file pins is that the two tier-up
# lanes agree with the interpreter, which they did not before the refusal
# existed — the first loop answered 10 instead of £10, and the second never
# terminated once `+` was overloaded but `<` was still the built-in.
class Money { has $.c; method Bool() { $!c != 0 } method Str() { "£$!c" } }
multi sub infix:<+>(Money $a, Money $b) { Money.new(c => $a.c + $b.c) }
multi sub infix:<+>(Money $a, Int $b)   { Money.new(c => $a.c + $b) }
multi sub infix:«<»(Money $a, Int $b)   { $a.c < $b }

my $m = Money.new(c => 0); my $i = 0;
while $i < 5 { $m = $m + $i; $i = $i + 1 }
say $m.Str, " ", so $m;

my $n = Money.new(c => 3); my $k = 0;
while $n < 10 { $n = $n + 2; $k = $k + 1 }
say $n.Str, " $k";

# An Int loop in the SAME program is refused too, because the refusal is per
# operator spelling and not per operand: `+` is overloaded here, so no loop
# using `+` can be compiled. That is conservative on purpose — the alternative
# is deciding at compile time what the operands will be at run time.
my $s = 0; my $j = 0;
while $j < 100 { $s = $s + $j; $j = $j + 1 }
say "$s $j";
