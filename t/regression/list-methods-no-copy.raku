# From the Cognates port (docs/rakupp-findings findings 2 and 3): `.elems`/`.end`/
# `.Bool`/`AT-POS` and every array subscript materialised a COPY of the whole array
# before answering, so one read was O(n) and a loop over an array was quadratic —
# 20,000 reads of a 20,000-element array took 12.9 s against 8 ms for a lexical.
# Those arms now read the live vector.
#
# This test guards the SEMANTICS the shortcut has to preserve; the speed is
# covered by tools/perf-guard.raku. Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

my @a = 10, 20, 30;
check(@a.elems, 3,     'elems');
check(@a.end,   2,     'end');
check(@a.Bool,  True,  'Bool on a populated array');
check(@a.AT-POS(1), 20, 'AT-POS');
check(@a.EXISTS-POS(2), True,  'EXISTS-POS in range');
check(@a.EXISTS-POS(9), False, 'EXISTS-POS past the end');
check(@a.AT-POS(-1), 30, 'AT-POS counts back from the end');
check(@a.EXISTS-POS(-1), True, 'EXISTS-POS does too');

my @e;
check(@e.elems, 0,      'elems of an empty array');
check(@e.end,   -1,     'end of an empty array is -1');
check(@e.Bool,  False,  'Bool of an empty array');
check(@e.AT-POS(0).defined, False, 'AT-POS out of range is undefined');
check(@e.AT-POS(0).^name, 'Any',  'and is an Any');

# Reaching the array through a scalar container — an attribute, or a copy — must
# give the same answers. This is the case the report hit.
class Holder { has @.items }
my $h = Holder.new(items => @a);
check($h.items.elems, 3,  'elems through an attribute');
check($h.items[1],    20, 'subscript through an attribute');
check($h.items[*-1],  30, 'a Whatever subscript too');
my $copy = @a;
check($copy.elems, 3,  'elems through a scalar');
check($copy[2],   30,  'subscript through a scalar');

# Slices still work, and still copy where they must.
check(@a[0, 2].List,  (10, 30), 'an index-list slice');
check(@a[1..2].List,  (20, 30), 'a Range slice');
check(@a[^2].List,    (10, 20), 'a ^n slice');

# The arms that MUTATE must still see a snapshot, not the live vector.
my @r = 1, 2, 3;
check(@r.reverse.List, (3, 2, 1), 'reverse returns a reversed copy');
check(@r.List,         (1, 2, 3), 'and leaves the original alone');
my @p = 1, 2, 3;
@p.push(4);
check(@p.elems, 4, 'push still grows the array');
check(@p.pop,   4, 'pop returns the last element');
check(@p.elems, 3, 'and shrinks it');

# Ranges and lazy lists take the general path, not the shortcut.
check((1..5).elems, 5, 'elems of a Range');
check((1..5).Bool, True, 'Bool of a Range');
my $lazy = (1..Inf).map(* * 2);
check($lazy[3], 8, 'a lazy list still indexes');

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
