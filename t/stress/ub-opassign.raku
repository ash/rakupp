# CONTRACT test: compound assignment (OP=) on ONE shared container from
# several workers at once, with a reader copying each container. `$s ~= "a"`
# appends into the string's own buffer, and the other OP= arms compute from
# the slot and store the result back. Unguarded, two appenders reallocated
# one buffer (a double free), a reader copied a buffer freed under it, and a
# Rat, a big Int or a List computed from a slot another worker had just
# replaced read freed memory. Each OP= takes the slot's stripe for its copy
# out and its store back. Which updates are lost is undefined behavior;
# dying is not.
class O { method Str { 'o' } }
class A { has $.a is rw = '' }
subset S of Str;
my $N = (@*ARGS[0] // 3).Int;
my $M = (@*ARGS[1] // 2000).Int;
my $s   = '';
my $u   = '';
my $r   = 1/3;
my $big = 2**70;
my $l   = (1,);
my $x   = 's';
my $z   = '';
my $obj = '';
my S $t = '';
my $at  = A.new;
my @a   = '';
my %h   = k => 1/3;
my $o   = O.new;
sub hammer(Int $i) {
    $s ~= 'a';
    $s ~= $i;
    $u ~= 'é';
    $u ~= 1.5e0;
    $r += 1/3;
    $big *= 1;
    $l ,= 1;
    $l = (1,);
    $x xor= 't';
    $z Z~= 'z';
    $obj ~= $o;
    $t ~= 'a';
    $at.a ~= 'a';
    @a[0] ~= 'a';
    %h<k> += 1/3;
}
sub copy-all() {
    my $c;
    $c = $s; $c = $u; $c = $r; $c = $big; $c = $l; $c = $x; $c = $z;
    $c = $obj; $c = $t; $c = $at.a; $c = @a[0]; $c = %h<k>;
    $c.defined
}
my @workers = (^$N).map: -> $w { start { hammer($_) for ^$M } };
my $reader = start { my $n = 0; for ^$M { $n++ if copy-all() }; $n };
await @workers;
my $reads = await $reader;
say "survived: {$s.chars} chars appended, reads=$reads (which updates were lost is UB; dying is not)";
say 'PASS';
