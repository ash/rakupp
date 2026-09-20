# A user-defined operator that overloads a BUILT-IN spelling, in every position
# a compiled backend emits that operator from. Until this fixture existed, every
# compiled backend — `--exe`, `--exe -O`, `--jit` and `--cnp` — answered
# differently from the interpreter here, because Codegen emitted the built-in
# operator straight through and `applyArith` cannot see a user routine.
#
# Run under the interpreter AND compiled; the two outputs are diffed against
# each other (t/run.raku), so neither is trusted on its own.
class Money {
    has $.c;
    method Bool() { $!c != 0 }
    method Str()  { "£$!c" }
}
multi sub infix:<+>(Money $a, Money $b) { Money.new(c => $a.c + $b.c) }
multi sub infix:<+>(Money $a, Int $b)   { Money.new(c => $a.c + $b) }
multi sub infix:«<»(Money $a, Int $b)   { $a.c < $b }
multi sub prefix:<->(Money $a)          { Money.new(c => -$a.c) }

# --- the value position: `$m + $i` must reach the user's candidate -----------
my $m = Money.new(c => 0); my $i = 0;
while $i < 5 { $m = $m + $i; $i = $i + 1 }
say "value    ", $m.Str, " ", so $m;

# --- the CONDITION position, which is a different emission lane. `rtLtB` and
# friends are the built-in comparison with an Int fast path in front, so a loop
# whose condition is overloaded kept comparing numerically — and once `+` was
# fixed but `<` was not, this loop stopped terminating at all.
my $n = Money.new(c => 3); my $k = 0;
while $n < 10 { $n = $n + 2; $k = $k + 1 }
say "cond     ", $n.Str, " $k";

# --- compound assignment: `$x += $y` is `$x = $x + $y` ----------------------
my $p = Money.new(c => 1);
$p += 4;
$p += Money.new(c => 5);
say "assign   ", $p.Str;

# --- prefix ------------------------------------------------------------------
say "prefix   ", (-Money.new(c => 7)).Str;

# --- AND THE HALF THAT MUST NOT CHANGE --------------------------------------
# The user's candidates take a Money. Ordinary arithmetic has no candidate, so
# it must fall through to the built-in exactly as before — that fall-through is
# the whole reason this cannot be "call the user's routine and be done".
my $s = 0; my $j = 0;
while $j < 10 { $s = $s + $j; $j = $j + 1 }
say "ints     $s $j ", 2 + 3, " ", -4, " ", 7 < 9;
my $t = 1; $t += 6; $t += 2;
say "intasgn  $t";
my @a = 1, 2, 3;
say "strings  ", "a" ~ "b", " ", @a.elems + 1;

# --- an error the user's operator RAISES is its answer, not a fall-through ---
# Written with `try` and `$!` rather than a CATCH block ON PURPOSE: a CATCH
# makes `--exe` give up on native emission and bundle the interpreter instead,
# and a bundled binary would be comparing the interpreter with itself. The whole
# point of this fixture is to exercise the EMITTED operator dispatch, so nothing
# in it may push the program onto the bundling path — which is why t/run.raku
# also asserts that the compile said "(native)".
multi sub infix:<->(Money $a, Money $b) { die "Money cannot be subtracted" }
my $x = try Money.new(c => 1) - Money.new(c => 2);
say "raised   ", $!.defined ?? $!.message !! "no";
