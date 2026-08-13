# Regression: a capture parameter `|c` flattened a single Positional argument.
#
# `sub f(|c)` saw THREE positionals from `f([1,2,3])` where Rakudo sees one. The
# capture shared the `+@a` single-argument rule in bindParams — right for `+@`,
# wrong for a capture, which is the argument list AS PASSED and never gets to
# reinterpret it. Fixed by routing a capture through the no-flatten branch.
#
# Not hypothetical: `sub wrapper(|c) { inner(|c) }` is the standard pass-through
# idiom, and the shipped JSON::Native declared `to-json(|c) { jf-to-json(|c) }`.
# Its `to-json([1,2,3])` returned `1` — the array reached JSON::Fast as three
# separate arguments, so it serialised only the first. Found while building the
# ABI A1 extension gate.
#
# Contract: exit 0 + last line PASS.
my @fail;

sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

sub cap(|c) { c.list.elems }

# --- the three forms that were wrong ----------------------------------------
my @arr = 1, 2, 3;
check cap([1, 2, 3]), 1, 'an Array argument stays one argument';
check cap(@arr),      1, 'an @array argument stays one argument';
check cap((1, 2, 3)), 1, 'a List argument stays one argument';

# --- the five that were already right, and must stay right -------------------
check cap([1, 2], [3, 4]), 2, 'two Array arguments stay two';
check cap({a => 1}),       1, 'a Hash argument stays one';
check cap($[1, 2]),        1, 'an itemized array stays one';
sub slurpy(*@a) { @a.elems }
check slurpy([1, 2, 3]), 3, '*@a SHOULD flatten a lone Iterable';
sub one($x) { $x.WHAT.^name }
check one([1, 2, 3]), 'Array', 'a scalar parameter binds the Array itself';

# --- the other slurpy kinds share the branch that was changed ----------------
sub plus(+@a)   { @a.elems }
sub nonflat(**@a) { @a.elems }
check plus([1, 2, 3]),      3, '+@a flattens a lone Iterable (single-argument rule)';
check plus([1, 2], [3, 4]), 2, '+@a keeps two Iterables apart';
check slurpy([1, 2], [3, 4]), 4, '*@a flattens both Iterables';
check nonflat([1, 2, 3]),   1, '**@a never flattens';

# --- the idiom this actually broke ------------------------------------------
sub inner-count(|c) { c.list.elems }
sub wrapper(|c)     { inner-count(|c) }
check wrapper([1, 2, 3]), 1, 'a capture forwarded through a wrapper keeps its arity';
check wrapper(1, 2, 3),   3, '…and three arguments stay three';

# Forwarding to a REAL signature is where the arity mattered: the array has to
# arrive as $x, not spread across the parameters.
sub target($x, :$y) { "{$x.elems}/{$y // 'none'}" }
sub forward(|c)     { target(|c) }
check forward([1, 2, 3]),          '3/none', 'an Array forwards into one parameter';
check forward([1, 2, 3], :y<set>), '3/set',  '…with named arguments alongside';

# Named arguments and the empty capture still round-trip.
sub named-count(|c) { c.hash.elems }
check named-count(:a(1), :b(2)), 2, 'named arguments reach the capture';
check cap(),                     0, 'an empty capture has no positionals';
check named-count([1, 2]),       0, 'a positional is not counted as named';

# A capture sub-signature destructures the positionals it was given, so it sees
# the same argument list the capture does.
sub destructure(|c ($first, *@rest)) { "{$first.elems}:{@rest.elems}" }
check destructure([1, 2, 3]), '3:0', 'a sub-signature sees the Array as one argument';

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
