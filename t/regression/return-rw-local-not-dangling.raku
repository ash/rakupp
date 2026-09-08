# Regression: `return-rw` of a routine-local BOUND to an element handed the
# caller a pointer into the frame that was about to die. The caller then wrote
# through it into reused memory and read a null hash back out — a SEGFAULT, with
# no output at all because the TAP was still buffered.
#
# The shape is `my $root := $container; $root := $root{$step}; return-rw $root`,
# which is how Crane's `at` walks (issue #69). `$root` holds the Proxy that a
# `:=` to an element leaves behind, and that slot lives in the routine's frame.
# The `is rw` work that came before this resolved a rw-LINKED parameter to the
# caller's own slot, which is safe; a plain local has no such link and fell back
# to its own frame.
#
# A Proxy carries its own FETCH/STORE closures, so the fix is to copy the VALUE
# out rather than pass the pointer on: writing to the copy still reaches the real
# container. Only a Proxy slot is treated this way — every other one keeps
# handing back its pointer, which is what an outer lexical and an attribute
# accessor need (t/regression/tries-with-frequencies.raku is the case that says
# so, and rw-sub-lvalue.raku the one for linked parameters).
#
# The whole sequence below is load-bearing: the crash needed the earlier calls to
# have run, because it turned on which memory the dead frame's slot had been
# reused for.
#
# Runs under both engines. Only what BOTH agree on is asserted — reaching
# through a method's rw result with a subscript is still open, and is not
# claimed here.
#
# Contract: exit 0 + last line PASS.
my @fail;

sub v1($container, @steps) is rw {
    my $root := $container;
    $root := $root{@steps[0]};
    return-rw $root;
}
class K { method m($c, *@s) is rw { my $r := $c; return-rw v1($r, @s) } }

# 1. reading the result is fine, and must not disturb anything. The `.raku`
#    here is not decoration: the crash needed the allocation this makes.
my %a = :x({:y(1)});
my $shown = K.m(%a, 'x').raku;
@fail.push("read: $shown") unless K.m(%a, 'x') eqv {:y(1)};

# 2. a direct assign through the same method, whose result is the dead slot
my %c = :x({:y(1)});
K.m(%c, 'x') = 'replaced';
my $after-direct = %c.raku;

# 3. the subscripted assignment that used to crash — the point is that we
#    reach the next line at all
my %b = :x({:y(1)});
K.m(%b, 'x'){'z'} = 9;
my $after-indexed = %b.raku;
@fail.push('lost the container') unless $after-indexed.contains('y');

# 4. binding the result and writing through the binding reaches the container
my %d = :x({:y(1)});
my $got := K.m(%d, 'x');
$got<z> = 9;
@fail.push("bound write: {%d.raku}") unless %d<x><z> == 9;

# 5. a deeper chain of the same shape, to keep the frames stacking
sub v2($c, @s) is rw { my $r := $c; $r := $r{@s[0]}; return-rw v1($r, @s[1..*]) }
my %e = :p({:q({:r(1)})});
my $deep := v2(%e, ('p', 'q'));
$deep<s> = 2;
@fail.push("deep bound write: {%e.raku}") unless %e<p><q><s> == 2;

if @fail {
    note $_ for @fail;
    die "{+@fail} check(s) failed";
}
say 'PASS';
