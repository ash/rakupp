# Regression: `for $h.kv -> $k, $v is rw` over a scalar holding the Array or
# Hash writes through — issue #123, the forms the compiling backends still hand
# to the interpreter (rw-kv-loop-over-deref.raku has the compiled ones).
#
# Contract: exit 0 + last line PASS. Runs unchanged under Rakudo.
my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku}") unless $got eqv $want }

my $s = [1, 2];
for $s.kv -> $i, $x is rw { $x = $i * 3 }
ck $s, [0, 3], '$h.kv over an Array';

my $n = {q => 1};
for $n.kv -> $k, $v is rw { $v = 'z' }
ck $n, {q => 'z'}, '$h.kv over a Hash';

my $ro = [1, 2];
for @$ro.kv -> $i, $x { try { $x = 5 } }
ck $ro, [1, 2], 'without `is rw` nothing is written';

if @fail { .say for @fail; say 'FAIL'; exit 1 }
say 'PASS';
