# Guard: a hash entry never moves once it exists.
#
# The lvalue paths and autovivification hold a reference into a hash's payload
# across further inserts, so ValueHash's entries must stay where they were
# constructed (src/ValueHash.h, REFERENCE STABILITY). They lived in a std::deque
# for that; VALUEHASH-SMALL-PLAN.md replaced it with ChunkList — chunks that
# double from a small first one — to stop every hash, and every object's
# attribute table, allocating a 4 KB block for its first key. A container that
# moved an entry when it grew would leave a bound slot pointing at freed memory
# and the write below would vanish, or corrupt something else.
#
# Every figure here crosses many chunk boundaries (the first chunk holds 4, or
# what reserve() asked for; an object's table is sized from its class).

sub check($got, $want, $what) {
    die "hash-entry-stability: $what: got {$got.raku}, expected {$want.raku}" unless $got eqv $want;
}

# a bound element slot, held across 2000 inserts, then written through
my %h;
my $r := %h<a>;
%h{"k$_"} = $_ for ^2000;
$r = 5;
check %h<a>, 5, 'write through a slot bound before 2000 inserts';
check %h.elems, 2001, 'element count';

# nested autovivification into entries made long before
my %g;
%g<x><y> = 1;
%g{"z$_"}<w> = $_ for ^500;
%g<x><y>++;
check %g<x><y>, 2, 'increment through an early nested entry';
check %g<z499><w>, 499, 'late nested entry';

# a Pair taken from the hash keeps the hash's own container
my $pair = %h.pairs.first(*.key eq 'a');
%h{"q$_"} = 1 for ^300;
check $pair.value, 5, 'Pair value after 300 more inserts';

# objects: the attribute table is presized from the class, and a subclass adds to it
class Point { has $.x; has $.y }
class Point3 is Point { has $.z; has @.tags }
my @p = (^1000).map({ Point3.new(x => $_, y => -$_, z => $_ * 2, tags => [$_]) });
check @p[999].x + @p[999].y + @p[999].z, 1998, 'attributes of a presized object';
check @p[500].tags, [500], 'array attribute';

# copies are independent, and a reset leaves nothing behind
my %c = (^100).map({ $_ => $_ * 2 });
my %d = %c;
%d<5> = 0;
check %c<5>, 10, 'original after writing the copy';
check %d.elems, 100, 'copy size';
%h = ();
check %h.elems, 0, 'reset';

say "PASS";
