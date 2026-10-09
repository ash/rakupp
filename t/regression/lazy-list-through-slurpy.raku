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
# An endless RANGE is not the representation a lazy sequence has, and the
# binder spread it: 10,000 elements of `1..*` and no more, so `@a.is-lazy` was
# False, `@a.elems` 10000 and `@a[20000]` Any (2026-10-09). An endless argument
# now ends the slurpy lazily, after whatever came before it.
check flat-slurpy(1..*),       True,  'an endless Range stays lazy through *@a';
check one-arg-slurpy(1..*),    True,  '…and through +@a';
check flat-slurpy(1, 2, 1..*), True,  '…behind other arguments too';
check flat-slurpy(^Inf),       True,  '…and `^Inf`';
check flat-slurpy('a'..*),     True,  '…and a Str range';
check flat-slurpy(1..3),       False, 'a finite Range does not';
sub nth(*@a)  { @a[20000] }
sub head4(*@a) { @a[^4] }
sub elems(*@a) { @a.elems }
check nth(1..*),               20001, 'the slurpy reads as far as asked';
check head4(7, 1..*),          (7, 1, 2, 3), '…the arguments ahead of it first';
check (try elems(1..*)) // $!.^name, 'X::Cannot::Lazy', '…and counting it dies';
# the arguments AFTER a lazy one are read too, in order, once it runs out
sub upto7(*@a) { @a[^7].grep(*.defined).List }
check upto7((lazy 1..3), 5),          (1, 2, 3, 5), 'a finite lazy list, then an item';
check upto7(0, (lazy 1..3), (6, 7)),  (0, 1, 2, 3, 6, 7), '…then a list, flattened';
check upto7((lazy 1..2), (lazy 3..4)), (1, 2, 3, 4), '…then another lazy list';
check flat-slurpy((lazy 1..2), 5),    True, 'a slurpy holding one is lazy';
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
