# Regression: binding a list to a `$` parameter ITEMIZES it. A scalar is one
# thing, so `sub f($x) { $x.raku }; f((1,2))` is `$(1, 2)` in Rakudo where
# rakupp said `(1, 2)`, and the itemized value does not flatten into a
# surrounding list. The same rule reaches loop variables (`for @t -> $x`), and
# an ARRAY's elements are scalar containers in their own right, so even the
# bare topic of `for @t { $_ }` arrives itemized — while a LIST's elements do
# not (`for ((1,2),(3,4)) { $_ }` stays bare).
#
# `is raw` binds the container itself and so is exempt.
#
# Found sweeping The Weekly Challenge: several authors print test descriptions
# straight from a loop variable, and every one of them came out unparenthesised.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want
}

# --- subroutine and block parameters ---
sub f($x) { $x.raku }
check f((1,2)), '$(1, 2)', 'a List bound to a $ param is itemized';
my @a = 1, 2;
check f(@a),    '$[1, 2]', 'an Array bound to a $ param is itemized';
sub raw($x is raw) { $x.raku }
check raw((1,2)), '(1, 2)', '`is raw` binds the container and does not itemize';
check (-> $x { $x.raku })((1,2)), '$(1, 2)', 'a pointy block itemizes too';
check ((1,2),(3,4)).map(-> $x { $x.raku }).join(' '), '$(1, 2) $(3, 4)',
      'and so does a mapper over a List';

# an itemized argument is ONE element in a surrounding list, and still indexes
sub elems3($x) { (1, $x, 2).elems }
check elems3((3,4)), 3, 'the itemized value does not flatten into a list';
sub first-of($x) { $x[0] }
check first-of((7,8)), 7, 'and it still indexes as a Positional';
sub count($x) { $x.elems }
check count((1,2)), 2, '…and still counts';

# --- loop variables and the topic ---
my @t = (1,2), (3,4);
check (my @r1 = do for @t -> $x { $x.raku }).join(' '), '$(1, 2) $(3, 4)',
      'a named $ loop variable itemizes';
check (my @r2 = do for @t { $_.raku }).join(' '), '$(1, 2) $(3, 4)',
      'an Array element reaches the bare topic itemized';
check (my @r3 = do for ((1,2),(3,4)) { $_.raku }).join(' '), '(1, 2) (3, 4)',
      'a List element does not';
my @nested = [1,2], [3,4];
check (my @r4 = do for @nested -> $x { $x.raku }).join(' '), '$[1, 2] $[3, 4]',
      'nested Arrays itemize the same way';
check (my @r5 = do for @t -> $p, $q { "$p.raku() $q.raku()" }).join('|'),
      '$(1, 2) $(3, 4)', 'a two-at-a-time loop itemizes both';

# --- what must NOT change ---
my @two = (1,2), (3,4);
for @two { }
check @two.raku, '[(1, 2), (3, 4)]', 'iterating does not stamp the flag onto the array';
my @dbl = 1, 2, 3;
for @dbl { $_ *= 2 }
check @dbl.raku, '[2, 4, 6]', 'rw aliasing through the topic still writes back';
# an `@` loop variable is not a scalar and must keep binding as a Positional
my @info = 'fmt', (1, 2), 'fmt2', (3, 4);
my @seen;
for @info -> $format, @tests { @seen.push("$format:{@tests.elems}") }
check @seen.join(' '), 'fmt:2 fmt2:2', 'an @ loop variable still binds as a Positional';
sub g(@x) { @x.elems }
check g((1,2)), 2, 'an @ parameter is untouched';

if @fail { note "FAILED: " ~ @fail.join('; '); say 'FAIL' } else { say 'PASS' }
