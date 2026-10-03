# Regression: an element reached through `@$h[…]` / `%$h<…>` is assignable —
# issue #122.
#
# `@$h[0] = 5` and `@$h[1]++` died "Target is not assignable" (the same element
# as `$h[0]` was fine), in the interpreter and, through it, in --exe; the JS
# backend wrote into a copy ("Cannot modify an immutable List", or silently
# nothing for `%$h<k> = v`). Terminal::UI sizes panes with `@$heights[…]++`.
# What `$h` holds when it is not an Array (Hash) stays unassignable, as on
# Rakudo: `@$h` is then a fresh value.
#
# Contract: exit 0 + last line PASS. Runs unchanged under Rakudo.
my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku}") unless $got eqv $want }

my $h = [1, 2, 3];
@$h[0] = 5;         ck $h, [5, 2, 3], '@$h[i] = v';
@$h[1]++;           ck $h, [5, 3, 3], '@$h[i]++';
@$h[2] += 10;       ck $h, [5, 3, 13], '@$h[i] += v';
@$h[*-1] = 0;       ck $h, [5, 3, 0], '@$h[*-1] = v';
@($h)[0] = 1;       ck $h, [1, 3, 0], '@($h)[i] = v';

my $heights = [1, 1, 1];
my @changed = 0, 2;
@$heights[@changed[$_]]++ for ^@changed;
ck $heights, [2, 1, 2], 'the Terminal::UI shape';

my %hh; my $r = %hh;
%$r<k> = 1; %$r<k>++; %$r<k> += 3;
ck %hh, {k => 5}, '%$h<k> writes the hash $h holds';

my @aoa = [1, 2], [3, 4];
@(@aoa[1])[0] = 30;
ck @aoa[1], [30, 4], '@(@a[i])[j] = v';

sub bump($arr) { @$arr[0]++; $arr }
ck bump([41]), [42], 'through a parameter';

my $list = (1, 2);
ck (try { @$list[0] = 5; 'assigned' }) // $!.^name, 'X::Assignment::RO', 'a List stays immutable';
my $u;
ck (try { @$u[0] = 1; 'assigned' }) // $!.^name, 'X::Assignment::RO', 'an undefined scalar is not autovivified';
ck $u, Any, '…and is left alone';

if @fail { .say for @fail; say 'FAIL'; exit 1 }
say 'PASS';
