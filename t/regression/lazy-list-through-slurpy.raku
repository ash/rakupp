# Regression: a LAZY list handed to a flattening slurpy stays lazy.
#
# `*@a` and `+@a` both built a fresh array and pushed every element into it,
# which pulls a lazy source dry. The routine could then no longer tell it had
# been handed something lazy, and a module that BRANCHES on that took the wrong
# path: Int::polydiv asks `@divs.is-lazy` and runs a different loop for each
# answer, so `16.polydiv: 16 xx *` ran the finite branch and emitted a trailing
# 0 that the lazy branch stops before.
#
# The slurpy is a view of the argument, not a copy of it. Only the sole-argument
# case is bound through — with several arguments the slurpy really is collecting
# them, and must.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

sub flat-slurpy(*@a)   { @a.is-lazy }
sub one-arg-slurpy(+@a) { @a.is-lazy }

check flat-slurpy(16 xx *),    True,  'a lazy repeat stays lazy through *@a';
check one-arg-slurpy(16 xx *), True,  '…and through +@a';
# STILL OPEN: an infinite RANGE through a slurpy is not lazy here (`1..Inf`).
# A Range is not the same representation as a lazy sequence and takes another
# path; `16 xx *`, which is what the distribution passes, is covered above.
check one-arg-slurpy((1,2,3)), False, 'an eager list is still eager';
check flat-slurpy(1,2,3),      False, '…and so are separate arguments';

# the slurpy still WORKS as a slurpy — laziness must not cost the semantics
sub count(+@a)  { @a.elems }
sub joined(*@a) { @a.join('|') }
check count(1, 2, 3),        3, '+@a collects separate arguments';
check count((1, 2, 3)),      3, '…and flattens a lone list';
check count([1,2], [3,4]),   2, '…and keeps two Arrays apart';
check joined((1,2), (3,4)),  '1|2|3|4', '*@a flattens through lists';
check joined([1,2], [3,4]),  '1|2|3|4', '…and through Arrays (both engines)';
check count(1..3),           3, 'a Range flattens';

# a lazy slurpy is still indexable without being consumed
sub first-of(+@a) { @a[0] }
check first-of(16 xx *), 16, 'a lazy slurpy indexes';

# the shape the distribution writes
sub takes-divisors(+@divs) { @divs.is-lazy ?? 'lazy' !! 'eager' }
check takes-divisors(16 xx *),  'lazy',  'the branch Int::polydiv takes';
check takes-divisors(5, 2),     'eager', '…and the one it takes otherwise';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
