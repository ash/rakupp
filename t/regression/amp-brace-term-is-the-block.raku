# Regression: `&{ … }` is a term — the block itself (issue #60).
#
# `&( … )` is the Callable contextualizer, and Rakudo reads `&{ … }` the same
# way over a braced term: the block (or, by the usual rule, the hash) itself.
# The parser took it for the infix `&` with nothing on its left — "unexpected
# operator in term position" — and after a listop it was that infix:
# `say &{ 42 }()` said an empty line, `f &{ … }` called f with nothing.
# FunctionalParsers' EBNF parser generator writes `apply(&{ … }, …)` into the
# class it EVALs. Every answer below is Rakudo 2026.09's.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

ck (&{ 42 }).^name, 'Block', 'a block';
ck (&{ 42 })(), 42, '…called';
my &dbl = &{ $_ * 2 };
ck dbl(21), 42, '…bound to a & variable';
my $c = &{ 7 };
ck $c(), 7, '…held in a $ variable';
ck (&{ $^a + $^b }).arity, 2, '…with its placeholders';
ck (&{ $^a + $^b })(1, 2), 3, '…which bind';
sub ap(&g, $x) { g($x) }
ck ap(&{ $_ + 1 }, 4), 5, 'an argument in parentheses';
ck (1, 2, 3).map(&{ $_ * 10 }).List, (10, 20, 30), 'a map\'s block';
ck ap(&{ .uc }, 'a'), 'A', 'its topic';
my $x = 5;
ck (&{ $x })(), 5, 'a closure';

# after a listop, as its first argument — not the infix `&`
sub one(&c) { c(1) }
ck (one &{ $_ + 1 }), 2, 'a listop\'s argument';
my $said = (sub (*@a) { @a.elems })(&{ 1 }, &{ 2 });
ck $said, 2, 'two of them';

# braces that are a hash stay one, and after a term `&` is still a junction
ck (&{ a => 1 }).^name, 'Hash', '`&{ a => 1 }` is a Hash';
ck (&{ }).^name, 'Hash', '…and `&{ }`';
ck (1 &{ 2 }).^name, 'Junction', 'after a term, the infix';
ck (1 & 2).^name, 'Junction', '…as ever';

say 'PASS';
