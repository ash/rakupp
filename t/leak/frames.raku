# A routine whose frame a routine made inside it captures: `my sub inner`, a
# block in a variable, a block that calls itself through its own variable.
# The frame holds the routine and the routine holds the frame, so every call
# leaves the pair behind unless both are freed together.
use Test;
my atomicint $freed = 0;
class S { submethod DESTROY { $freed⚛++ } }
sub with-sub($i)       { my $s = S.new; my sub inner() { $i + 1 }; inner() }
sub with-block($i)     { my $s = S.new; my &f = -> { $i }; f() }
sub with-recursion($i) { my $s = S.new; my &f = -> $n { $n ?? f($n - 1) !! $i }; f(2) }
for &with-sub, &with-block, &with-recursion -> &r {
    $freed ⚛= 0;
    r($_) for ^2000;
    $*VM.request-garbage-collection for ^3;
    ok $freed >= 1900, "{&r.name}: each call's frame is freed ($freed of 2000)";
}
done-testing;
