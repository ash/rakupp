# Regression: a shaped array's ELEMENT SEQUENCE is its leaves, row-major.
#
# `my @a[3;2]` iterates as six values under Rakudo — `my @flat = @a` is six
# elements, and so are `for @a`, `flat @a`, `|@a` and `@a.Array` — even though
# `.elems` answers the FIRST DIMENSION, 3, and `.raku` shows the rows. This
# engine handed out the rows wherever the leaves were wanted, so a caller who
# stored `random-variate($dist, [3, 2])` (Statistics::Distributions returns a
# shaped array) in a plain `my @m` got 3 elements here and 6 under Rakudo.
#
# What must NOT change with it: `.elems`, `.raku`, `.shape`, `@a[i;j]`, and
# binding — `sub f(@a)` binds the shaped array itself, shape and all.
#
# Contract: exit 0 + last line PASS. Passes under both engines, so it doubles as
# an oracle.
my @fail;

my @r[3;2] = (1..6).rotor(2);

# 1. the shaped array itself is unchanged
{
    @fail.push('shape')  unless @r.shape.gist eq '(3 2)';
    @fail.push('elems')  unless @r.elems == 3;          # the FIRST DIMENSION
    @fail.push('raku')   unless @r.raku eq 'Array.new(:shape(3, 2), [1, 2], [3, 4], [5, 6])';
    @fail.push('index')  unless @r[1;1] == 4;
    @fail.push('is-int') unless @r[2;0] == 5;
}

# 2. storing it in an unshaped array takes the leaves
{
    my @flat = @r;
    @fail.push("assign: {@flat.raku}") unless @flat.elems == 6 && @flat.join(',') eq '1,2,3,4,5,6';
    @fail.push('assign eqv')  unless @flat eqv [1, 2, 3, 4, 5, 6];
    # …and so does an `is copy` parameter, which is a store rather than a bind
    sub copies(@a is copy) { @a.elems }
    @fail.push('is copy') unless copies(@r) == 6;
}

# 3. iterating it walks the leaves
{
    my @seen;
    for @r -> $x { @seen.push($x) }
    @fail.push("for: {@seen.raku}") unless @seen.join(',') eq '1,2,3,4,5,6';
    @fail.push('for as expression') unless (do for @r { $_ }).elems == 6;
    @fail.push('map')  unless @r.map({ $_ * 2 }).join(',') eq '2,4,6,8,10,12';
    @fail.push('sum')  unless @r.sum == 21;
    @fail.push('join') unless @r.join(',') eq '1,2,3,4,5,6';
}

# 4. the list-shaped views: List, Seq, Array, flat, slip, append, list assignment
{
    @fail.push('.List')  unless @r.List.elems == 6;
    @fail.push('.Seq')   unless @r.Seq.elems == 6;
    @fail.push('.Array') unless @r.Array.elems == 6 && @r.Array.join(',') eq '1,2,3,4,5,6';
    @fail.push('.flat')  unless @r.flat.elems == 6;
    @fail.push('flat()') unless (flat @r).elems == 6;
    @fail.push('slip')   unless (|@r).elems == 6;
    @fail.push('.Slip')  unless @r.Slip.elems == 6;

    my @p;
    @p.append(@r);
    @fail.push('append') unless @p.elems == 6 && @p.join(',') eq '1,2,3,4,5,6';
    # push takes it as ONE element, as it does any array
    my @q;
    @q.push(@r);
    @fail.push('push') unless @q.elems == 1;

    my ($a, $b, $c, $d) = @r;
    @fail.push("list assign: $a $b $c $d") unless "$a $b $c $d" eq '1 2 3 4';

    # A hyperop distributes over the leaves and answers the flat list. (Rakudo
    # prints "Ignoring [Any] as shape specification" on this line while doing
    # so — its own warning, from building the result; the ANSWER is what is
    # asserted here, and both engines give it.)
    @fail.push('hyper') unless (@r >>+>> 1).join(',') eq '2,3,4,5,6,7';
}

# 5. binding is NOT a store: the shaped array itself arrives, shape intact
{
    sub binds(@a) { (@a.shape.gist, @a.elems).join(' ') }
    @fail.push('bind keeps shape') unless binds(@r) eq '(3 2) 3';
    # a slurpy flattens, as it does for any array
    sub slurps(*@a) { @a.elems }
    @fail.push('slurpy') unless slurps(@r) == 6;
    # …and a pointy block's positional binds too
    @fail.push('pointy') unless (-> @x { @x.elems })(@r) == 3;
}

# 6. one dimension and three behave the same way
{
    my @one[4] = 1, 2, 3, 4;
    my @o = @one;
    @fail.push('1-D assign') unless @o.elems == 4 && @o.join(',') eq '1,2,3,4';
    @fail.push('1-D elems')  unless @one.elems == 4;

    my @cube[2;3;2] = ((1,2),(3,4),(5,6)), ((7,8),(9,10),(11,12));
    my @flat3 = @cube;
    @fail.push("3-D assign: {@flat3.elems}") unless @flat3.elems == 12;
    @fail.push('3-D order') unless @flat3.join(',') eq '1,2,3,4,5,6,7,8,9,10,11,12';
    @fail.push('3-D elems') unless @cube.elems == 2;     # still the first dimension
    @fail.push('3-D index') unless @cube[1;2;1] == 12;
    my @seen3;
    for @cube -> $x { @seen3.push($x) }
    @fail.push('3-D for') unless @seen3.elems == 12;
}

# 7. a leaf that is ITSELF an array stays whole: the walk descends exactly as
#    many levels as there are dimensions
{
    my @nest[2;2];
    @nest[0;0] = [1, 2]; @nest[0;1] = [3];
    @nest[1;0] = [4, 5]; @nest[1;1] = [6];
    my @f = @nest;
    @fail.push("nested leaves: {@f.elems}") unless @f.elems == 4;
    @fail.push('nested first') unless @f[0].elems == 2 && @f[0][1] == 2;
    @fail.push('nested last')  unless @f[3].elems == 1 && @f[3][0] == 6;
}

die "FAILED: {@fail.join(', ')}" if @fail;
say 'PASS';
