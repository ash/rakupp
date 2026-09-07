# Divergence: loop control inside an EVAL'd string died instead of controlling
# the loop OUTSIDE the EVAL. `for ^3 { EVAL q[last] }` raised
# "last without a supporting loop construct" where Rakudo ends the for.
#
# Not a regression from a rakupp change — a long-standing gap, found by the
# control-flow differential run while gating NATIVE-MATH-PLAN phase 1
# (2026-09-06) and verified identical on the binary before that fix.
#
# The cause was in `evalString`: it turned EVERY NextEx/LastEx/RedoEx and every
# cooperative loop flag that reached the end of the EVAL'd unit into an
# X::ControlFlow error — the right answer only when no loop encloses the EVAL.
# It now hands the word onward when one does, mirroring what the same function
# already did for `return` and an enclosing routine. Two rules it must keep:
# the word travels as the EXCEPTION form, never the cooperative flag (an EVAL
# sits in expression position, where a leaked flag would let the rest of the
# statement run first), and only a USER-level EVAL hands it on — a regex `{ … }`
# block still absorbs it (docs/dev/findings/TRIAGE.md).
#
# Every expected value below is Rakudo's own answer, so this file passes under
# both engines.
#
# INTERPRETED ONLY. A `--exe` binary still errors on these: the codegen emits
# native C++ loops and never arms curLoopFrame, so evalString sees no enclosing
# loop (docs/dev/findings/TRIAGE.md). This file is run interpreted, like every
# other t/regression case, so it does not cover that path.

my $ok = True;
sub check($got, $want, $label) {
    unless $got eqv $want { note "FAIL: $label — {$got.raku} vs {$want.raku}"; $ok = False }
}

# --- the reported shapes -----------------------------------------------------
my $n = 0;
for ^3 { EVAL q[last]; $n++ }
check($n, 0, 'EVAL last ends the enclosing for');

my $m = 0;
for ^4 { EVAL q[next] if $_ == 1; $m++ }
check($m, 3, 'EVAL next skips one iteration');

# --- every loop form ---------------------------------------------------------
my $i = 0;
while $i < 5 { $i++; EVAL q[last] if $i == 2 }
check($i, 2, 'EVAL last ends a while');

my $c = 0;
loop (my $j = 0; $j < 5; $j++) { EVAL q[last] if $j == 2; $c++ }
check($c, 2, 'EVAL last ends a C-style loop');

my $k = 0; my $body = 0;
repeat { $k++; EVAL q[next] if $k == 2; $body++ } while $k < 4;
check("$k $body", '4 3', 'EVAL next in a repeat');

my $ri = 0; my $rr = 0;
for ^3 { $ri++; if $ri == 2 && $rr < 1 { $rr++; EVAL q[redo] } }
check("$ri $rr", '4 1', 'EVAL redo re-runs the iteration');

# --- across frames and nesting ----------------------------------------------
sub g() { EVAL q[last] }
my $x = 0;
for ^3 { g(); $x++ }
check($x, 0, 'EVAL last inside a sub CALLED from the loop still ends it');

my $outer = 0;
for ^3 { for ^3 { EVAL q[last] }; $outer++ }
check($outer, 3, 'EVAL last ends the INNERMOST loop only');

sub h() { my $t = 0; for ^4 { EVAL q[last] if $_ == 2; $t++ }; $t }
check(h(), 2, "EVAL last ends the sub's own loop");

check(EVAL(q[my $t = 0; for ^4 { last if $_ == 2; $t++ }; $t]), 2,
      'a loop INSIDE the EVAL still consumes its own last');

# --- the two positions a leaked cooperative flag would get wrong -------------
my $en = 0; my $ev = 0;
for ^4 { $ev = EVAL(q[last]) // 99; $en++ }
check("$en $ev", '0 0', 'EVAL last in expression position fires before the assignment');

my $sum = 0;
for ^4 -> $t { my $v = (EVAL q[next] if $t == 1) // 5; $sum += $v }
check($sum, 15, 'EVAL next as an OPERAND skips the iteration');

my @taken = gather { for ^4 { take $_; EVAL q[last] if $_ == 1 } }
check(@taken, [0, 1], 'EVAL last inside a gather ends the loop, keeping what was taken');

# --- what must still be an error --------------------------------------------
check(do { sub f() { EVAL q[return 42]; 7 }; f() }, 42, 'EVAL return still returns from the routine');

my $type = '';
try { EVAL q[last] }
$type = $!.^name if $!;
check($type, 'X::ControlFlow', 'no loop at all: a CATCHABLE X::ControlFlow, not a crash');

my $after = '';
for ^2 { }
try { EVAL q[next] }
$after = $!.^name if $!;
check($after, 'X::ControlFlow', 'a loop that already ENDED does not adopt it');

if $ok { say "PASS" } else { say "FAIL"; exit 1 }
