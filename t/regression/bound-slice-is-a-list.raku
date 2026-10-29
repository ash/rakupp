# Regression: a slice bound to a `$` or sigilless name is the List of those
# elements' containers, not one element (found while fixing issue #215).
#
# `my \x = @a[0, 2]` bound 30 and `my \w = @a[0..1]` bound 10: the bind arm
# that makes `$x := @a[$i]` the array's SLOT read the subscript as one index,
# which for a list is its length and for a Range its first element. `my \x =
# %h<a b>` bound a new entry under the one key "a b". Each binds the List of
# those elements' containers now, as `my @s := @a[0, 2]` already did: a write
# through it lands in the array or hash, and `@a = …` leaves it the elements it
# had, as a kept `@a.head(2)` does. Every answer below is Rakudo 2026.09's.
# Left as it is: an `.unshift` after the bind, which moves the elements past
# the positions an alias names (a single element's alias does the same).

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

{
    my @a = 10, 20, 30, 40;
    my \x = @a[0, 2];
    ck (x.WHAT, x.cache), (List, (10, 30)), 'the report: a list subscript';
    my $z := @a[0, 1];
    ck $z.cache, (10, 20), '…bound with :=';
    my \w = @a[0..1];
    ck w.cache, (10, 20), 'a Range';
    my \t = @a[1..*]; my \v = @a[*]; my \u = @a[*-2..*]; my \c = @a[0, *-1];
    ck (t.cache, v.cache, u.cache, c.cache), ((20, 30, 40), (10, 20, 30, 40), (30, 40), (10, 40)),
       'an endless Range, `*`, a WhateverCode range and a WhateverCode key';
    my @i = 0, 1; my \s = @a[@i]; my \r = @a["1", 0];
    ck (s.cache, r.cache), ((10, 20), (20, 10)), 'an array of keys, and a Str key';
    my $one = (0, 1); my \o = @a[$one];
    ck o, 30, 'an itemized list is still one index';
    ck (try { my \n = @a[-1, 0]; 1 }) // $!.^name, 'X::OutOfRange', 'a negative key dies';
}
{
    my @a = 10, 20, 30, 40;
    my \x = @a[0, 2];
    x[0] = 5;
    ck @a, [5, 20, 30, 40], 'a write through it lands in the array';
    @a[2] = 9;
    ck x.cache, (5, 9), '…and a write to the array shows through it';
    for x { $_++ }
    ck @a, [6, 20, 10, 40], '…and `for` aliases the elements';
    sub f(\y) { y[1] = 0 }
    f(x);
    ck @a, [6, 20, 0, 40], '…and so does a sigilless parameter';
}
{
    my @a = 10, 20;
    my \w = @a[0, 5];
    ck (w.cache, @a.elems), ((10, Any), 2), 'a place past the end reads Any and makes nothing';
    w[1] = 9;
    ck @a, [10, 20, Any, Any, Any, 9], '…and a write to it grows the array';
}
{
    my @a = 10, 20, 30;
    my \w = @a[0, 1]; my \v = @a[1, 2];
    @a = 4, 5, 6;
    ck w.cache, (10, 20), 'a refill makes new containers: the old elements stay';
    w[1] = 0;
    ck (v.cache, @a), ((0, 30), [4, 5, 6]), '…shared by two slices, and kept out of the array';
}
{
    my Int @a = 1, 2, 3;
    my \w = @a[0, 1];
    ck (try { w[0] = 'x'; 1 }) // $!.^name, 'X::TypeCheck::Assignment', 'a typed array checks a write through it';
    w[1] = 5;
    ck @a.List, (1, 5, 3), '…and takes one it allows';
    my int @n = 1, 2, 3;
    my \m = @n[0, 5];
    ck m.cache, (1, 0), 'a native array: its values';
    my @l = 1..*;
    my \l = @l[3, 5];
    ck l.cache, (4, 6), 'a lazy array: its values';
    my @nest = [1, 2], [3, 4];
    my \p = @nest[0, 1];
    ck (p.elems, p[1]), (2, [3, 4]), 'an element that is an Array stays one';
}
{
    my %h = a => 1, b => 2, c => 3;
    my \x = %h<a b>;
    ck x.cache, (1, 2), 'a hash slice';
    ck %h.keys.sort.List, <a b c>, '…which makes no entry named "a b"';
    x[0] = 9; %h<b> = 7;
    ck (%h<a>, x.cache), (9, (9, 7)), '…and is the entries\' containers';
    my $y := %h{'a', 'z'};
    ck ($y.cache, %h.elems), ((9, Any), 3), 'a missing key reads Any and is not made';
    $y[1] = 5;
    ck %h<z>, 5, '…until it is assigned';
    my %w = k => 1;
    my \all = %w{*};
    all[0] = 4;
    ck %w, {k => 4}, '`{*}`';
    my Int %t = a => 1;
    my \tx = %t<a b>;
    ck (try { tx[0] = 's'; 1 }) // $!.^name, 'X::TypeCheck::Assignment', 'a typed hash checks a write';
}

say 'PASS';
