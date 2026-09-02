# Regression: a Hash in `splice`'s replacement is ONE element, itemized or not
# (only `|%h` flattens it, and then as NAMED args splice ignores). rakupp
# flattened a bare `%h` to its pairs, scattering the record — Crane's positional
# `add` splices a `$value` holding a Hash exactly this way. Rakudo 2026.08 answers.
my $fails = 0;
sub ok($c, $w) { $fails++ unless $c; note "not ok - $w" unless $c }

my @a = [1, 2, 3];
my %h = :x(1), :y(2);
@a.splice(1, 0, %h);
ok(@a.elems == 4, 'a bare %h splices in as ONE element');
ok(@a[1] ~~ Associative && @a[1]<x> == 1 && @a[1]<y> == 2, '…and keeps all its keys');
ok(@a[0] == 1 && @a[2] == 2 && @a[3] == 3, '…around the untouched neighbours');

# an itemized hash was already whole — must stay whole
my @b = [1, 2];
@b.splice(1, 0, $%h);
ok(@b.elems == 3 && @b[1]<x> == 1, 'an itemized $%h stays one element');

# an Array still flattens in the default language version — the "itemized $[…]
# stays whole" rule is 6.e only, so the Hash asymmetry (always whole) is the
# point being pinned here, not a blanket "containers stay whole".
my @c = [1, 2, 3];
@c.splice(1, 0, [10, 20]);
ok(@c.elems == 5 && @c[1] == 10 && @c[2] == 20, 'a bare array flattens');
my @d = [1, 2];
@d.splice(1, 0, $[10, 20]);
ok(@d.elems == 4 && @d[1] == 10 && @d[2] == 20, 'an itemized $[…] flattens too (pre-6.e)');

# two hashes: each one element
my @e = [];
@e.splice(0, 0, %h, %(:z(3)));
ok(@e.elems == 2 && @e[0]<x> == 1 && @e[1]<z> == 3, 'two hashes are two elements');

# the removed elements still come back
my @f = [1, 2, 3, 4];
my @removed = @f.splice(1, 2, %h);
ok(@removed eqv [2, 3], 'splice returns the removed elements');
ok(@f.elems == 3 && @f[1]<x> == 1, '…and inserts the hash whole');

say $fails ?? "FAIL ($fails)" !! "PASS";
