# Issue #134: `say !! "Hi"` — a SPACED prefix `!!` after a listop name.
#
#   The listop-argument check refused a `!!` with a space after it, so that
#   `1 ?? Nil !! Any` keeps its else-marker instead of reading `Nil(!!Any)`.
#   But it refused it everywhere: `say !! "Hi"` became a bare `say` followed
#   by a stray `?"Hi"` ("Useless use of constant string ?"Hi" in sink
#   context"). A spaced `!!` is the else-marker only while a `??` at the same
#   bracket depth is still waiting for one; anywhere else it is prefix boolify.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want }

sub ev($code) { my $r is default(Nil) = try EVAL $code; $! ?? $!.^name !! $r }

check ev('sub f($x) { $x }; f !! "Hi"'),        True,  'f !! "Hi"';
check ev('sub f($x) { $x }; f !! True'),        True,  'f !! True';
check ev('sub f($x) { $x }; f !! 0'),           False, 'f !! 0';
check ev('sub f($x) { $x }; f !!"Hi"'),         True,  'f !!"Hi" (tight)';
check ev('so !! 5'),                            True,  'so !! 5';

# …and the else-marker still wins inside a pending ternary
check ev('1 ?? Nil !! Any'),                    Nil,   '1 ?? Nil !! Any';
check ev('0 ?? Nil !! Any'),                    Any,   '0 ?? Nil !! Any';
check ev('sub f { 5 }; 1 ?? f !! 3'),
      'X::Syntax::ConditionalOperator::SecondPartGobbled', 'a listop in the middle still gobbles';

# a ternary that is already closed, or sits outside the brackets, waits for nothing
check ev('sub f($x) { $x }; 0 ?? 1 !! f !! 0'), False, 'after a closed ternary';
check ev('sub f($x) { $x }; 1 ?? (f !! 0) !! 2'), False, 'in parens inside the middle part';
check ev('sub f($x) { $x }; 1 ?? [f !! 0] !! 2'), [False], 'in brackets inside the middle part';

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
