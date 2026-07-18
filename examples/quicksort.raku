#!/usr/bin/env raku
# Quicksort the functional way: pick a pivot, partition the rest into the
# elements below and above it with `.grep`, recurse on each half, and glue
# the sorted pieces back together. Shows recursion, `.grep` with a pointy
# block, list flattening with `|`, and multi-dispatch as a base case.

# Multi-dispatch gives us the empty-list base case for free: the narrower
# candidate wins when the argument matches, so no explicit `if` is needed.
multi quicksort()        { () }
multi quicksort($pivot, *@rest) {
    my @less = @rest.grep(* < $pivot);
    my @more = @rest.grep(* >= $pivot);
    |quicksort(|@less), $pivot, |quicksort(|@more);
}

# For contrast, a merge sort: split in half, sort each half, then merge the
# two sorted runs by repeatedly taking the smaller head element.
sub merge-sort(@xs) {
    return @xs if @xs <= 1;
    my $mid = @xs div 2;
    merge(merge-sort(@xs[^$mid]), merge-sort(@xs[$mid .. *]));
}
sub merge(@a, @b) {
    my @out;
    my ($i, $j) = 0, 0;
    while $i < @a && $j < @b {
        if @a[$i] <= @b[$j] {
            @out.push: @a[$i++];
        }
        else {
            @out.push: @b[$j++];
        }
    }
    |@out, |@a[$i .. *], |@b[$j .. *];
}

my @data = 5, 3, 8, 1, 9, 2, 7, 4, 6, 0;
say 'Input:      ', @data;

my @quick = quicksort(|@data);
my @mergey = merge-sort(@data);
my @builtin = @data.sort;

say 'quicksort:  ', @quick;
say 'merge-sort: ', @mergey;
say 'builtin:    ', @builtin;

# Confirm every method agrees with the built-in `.sort`.
say '';
say 'quicksort matches .sort?  ', @quick eqv @builtin;
say 'merge-sort matches .sort? ', @mergey eqv @builtin;

# Comparisons chain in Raku — `1 ≤ 2 ≤ 3` is a single True expression. So the
# same reduce metaop that turns `*` into a product ([*] 1..5) turns `≤` into
# a sortedness test: [≤] chains it across every neighbouring pair.
say 'sorted, per [≤]?          ', [≤] @quick;

# The whole algorithm also fits on one line as a recursive closure.
my &qs = -> @x {
    @x <= 1 ?? @x !! (|qs(@x.grep(* < @x[0])), @x[0], |qs(@x[1..*].grep(* >= @x[0])))
};
say '';
say 'one-liner:  ', qs(@data);
