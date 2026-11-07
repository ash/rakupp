# Guard: `.elems` (and `.end`, `.Numeric`) of a hash is its size, answered
# without listing it.
#
# `%h.elems` reached the general list arm, which counted what toList made of
# the hash: a Pair built for every entry, one allocation each, on every call
# (found measuring VALUE32-PLAN's string stores: `say %h.elems` was the one
# malloc per entry left in a 200,000-key benchmark). A plain Hash now answers
# its size in methodCallEntry (MethodCallEntry.cpp). Everything that is not a
# plain Hash keeps the general arm: a Map, Set, Bag or Mix, a role mixed in,
# a subclass, an augmented Hash.

sub check($got, $want, $what) {
    die "hash-elems-size: $what: got {$got.raku}, expected {$want.raku}" unless $got eqv $want;
}

my %h = a => 1, b => 2, c => 3;
check %h.elems, 3, '.elems';
check %h.end, 2, '.end';
check %h.Numeric, 3, '.Numeric';
check +%h, 3, 'prefix +';
my %e;
check %e.elems, 0, 'an empty hash';
check %e.end, -1, "an empty hash's .end";
%h<b>:delete;
check %h.elems, 2, 'after a :delete';
my Int %t = x => 1, y => 2;
check %t.elems, 2, 'a typed hash';
my %d is default(7) = p => 1;
my $unused = %d<nope>;
check %d.elems, 1, 'a hash with a default, after reading a missing key';
my %o{Any} = 1 => 'a', (2, 3) => 'b';
check %o.elems, 2, 'an object hash';
check Map.new((a => 1, b => 2)).elems, 2, 'a Map';
check set(<a b c>).elems, 3, 'a Set';
check bag(<a a b>).elems, 2, 'a Bag';
check (a => 2, b => 0.5).Mix.elems, 2, 'a Mix';
role R { method elems { 42 } }
my %m = a => 1;
check (%m but R).elems, 42, 'a role mixed in with but';
my %n = q => 1;
%n does R;
check %n.elems, 42, 'a role mixed in with does';
class H is Hash { }
my $hh = H.new; $hh<k> = 1; $hh<l> = 2;
check $hh.elems, 2, 'a Hash subclass';

# it does not list the hash: many calls on a large hash take about what they
# take on a small one (when it listed, the large one took thousands of times
# longer). The slack is wide, for a loaded machine. Through a sub, so the
# interpreter makes the call: a loop the integer kernels compile answers
# `%h.elems` itself (IntKernel.cpp, celemsFn).
sub size(%x) { %x.elems }
my %big; %big{$_} = $_ for ^20_000;
my %small = a => 1, b => 2;
my $n = 0;
my $t0 = now; $n += size(%small) for ^2_000; my $small = now - $t0;
$t0 = now;    $n += size(%big) for ^2_000;   my $large = now - $t0;
check $n, 2_000 * 2 + 2_000 * 20_000, 'the sizes summed';
die "hash-elems-size: 2,000 .elems on a 20,000-key hash took {$large}s, on a 2-key one {$small}s"
    if $large > 50 * $small + 0.5;

say "PASS";
