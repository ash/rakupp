# Regression: `for @$h.kv -> $i, $x is rw` writes through — issue #123.
#
# Over a dereferenced scalar (`@$h.kv`, `%$h.kv`) the write to `$x` was
# silently lost; `@a.kv` and `for @$h -> $x is rw` were fine. Terminal::UI
# turns its `fr => 1` pane heights into rows this way. Both compiling backends
# refused every read-write loop parameter; this `.kv` form over a container
# variable is now compiled, and every loop here is (rw-kv-loop-over-scalar.raku
# has the forms that still bundle).
#
# Contract: exit 0 + last line PASS. Runs unchanged under Rakudo.
my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku}") unless $got eqv $want }

my $h = [1, 2];
for @$h.kv -> $i, $x is rw { $x = 5 }
ck $h, [5, 5], '@$h.kv';

my @a = 1, 2;
for @a.kv -> $i, $x is rw { $x = $i + 10 }
ck @a, [10, 11], '@a.kv';

my $m = {a => 1, b => 2};
for %$m.kv -> $k, $v is rw { $v = $k x 2 }
ck $m, {a => 'aa', b => 'bb'}, '%$h.kv';

my %plain = x => 1;
for %plain.kv -> $k, $v is rw { $v++ }
ck %plain, {x => 2}, '%h.kv';

my $g = [1, 2, 3];
for @$g.kv -> $i, $x is rw { next if $i == 1; last if $i == 2; $x = 7 }
ck $g, [7, 2, 3], 'next and last keep what was written';

my $c = [1, 2];
for @$c.kv -> $i, $x is rw { my $f = { $x = $i + 40 }; $f() }
ck $c, [40, 41], 'a write from a closure in the body';

my $grow = {a => 1};
for %$grow.kv -> $k, $v is rw { $grow<b> = 2; $v = 0 }
ck $grow, {a => 0, b => 2}, 'a key added mid-loop is not walked';

sub rows($heights, $total) { for @$heights.kv -> $i, $x is rw { $x = $x * $total }; $heights }
ck rows([1, 2], 3), [3, 6], 'inside a sub, through a parameter';

if @fail { .say for @fail; say 'FAIL'; exit 1 }
say 'PASS';
