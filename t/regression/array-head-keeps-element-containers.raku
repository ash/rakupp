# Regression: a kept `@a.head(n)` / `@a.tail(n)` holds the array's element
# containers, so a later write to the array shows through it (issue #215).
#
# `my \head = @array.head(2); @array[0] = 42; head.raku` was (1, 2).Seq, and
# `my $head = @array.head(2)`, read and then followed by `@array[0] = 42`, still
# read (1 2): the Seq was a copy of the elements taken when `.head` ran. Rakudo's
# reads the array when it is first iterated, and its cached list holds the
# array's own Scalars, so both say 42. Kept — assigned or bound to a `$`
# variable, a name or one element — it now holds the array's positions,
# which it reads and writes, until `@a = …` refills the array: that makes
# new containers in Rakudo, so the Seq keeps the elements the array had.
#
# Every answer below is Rakudo 2026.09's. Left as they were: a `.shift` or
# `.pop` before the Seq is first read (Rakudo's lazy head sees the shorter
# array; this one keeps the elements, as it does after a read), a native or
# lazy array (values), and `.skip`, `.reverse`, `.map` and the other views.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

{
    my @array = 1, 2, 3;
    my \head = @array.head(2); @array[0] = 42;
    ck head.raku, '(42, 2).Seq', 'the issue: a sigilless head sees a later write';
}
{
    my @array = 1, 2, 3;
    my $head = @array.head(2);
    ck $head.gist, '(1 2)', 'a head in a Scalar, read';
    @array[0] = 42;
    ck $head.gist, '(42 2)', '…and read again after a write (the comment on the issue)';
    @array[1] = 43;
    ck $head.gist, '(42 43)', '…and again';
}
{
    my @a = 1, 2, 3;
    my \t = @a.tail(2); @a[2] = 9;
    ck t.raku, '(2, 9).Seq', 'a tail';
    my $t := @a.head(2); @a[0] = 7;
    ck $t.raku, '(7, 2).Seq', 'a head bound with :=';
    ck ($t.WHAT, $t.is-lazy), (Seq, False), '…is still an eager Seq';
}
{
    my @a = 1, 2, 3;
    my \h = @a.head(2); h[0] = 5;
    ck @a, [5, 2, 3], 'a write through the head reaches the array';
    my $h = @a.head(2); $h[1] = 7;
    ck @a, [5, 7, 3], '…through a head in a Scalar too';
    for h { $_ *= 10 }
    ck @a, [50, 70, 3], '…and through `for`';
}
{
    my @a = 1, 2, 3;
    my $n = 2;
    my $w = @a.head(*-1); my $v = @a.head($n); my $e = @a.head(10); my $z = @a.head(0); my $all = @a.tail(*);
    @a[0] = 0;
    ck ($w.cache, $v.cache, $e.cache, $z.cache, $all.cache), ((0, 2), (0, 2), (0, 2, 3), (), (0, 2, 3)),
       'every count: WhateverCode, a variable, past the end, none, all';
}
{
    my @a = 1, 2, 3;
    my \h = @a.head(2);
    for h { }
    ck (try { for h { }; 1 }) // $!.^name, 'X::Seq::Consumed', 'it is still read once';
}
{
    my @a = 1, 2, 3;
    my $m = @a.head(2); my $s = @a.head(2); my $l = @a.head(2); my $j = @a.head(2);
    my $q = @a.head(2); my $t = @a.head(2); my $c = @a.head(2);
    @a[0] = 9;
    ck ($m.map(* * 2).List, $s.sum, $l.elems, "{$j.join('-')}", $q eqv (9, 2).Seq, $t ~~ (9, 2)),
       ((18, 4), 11, 2, '9-2', True, True), 'what reads it sees the write';
    my @copy = $c.list;
    @a[1] = 0;
    ck @copy, [9, 2], 'an array assigned from it takes values';
}
{
    my Int @a = 1, 2, 3;
    my \h = @a.head(2); @a[0] = 9;
    ck h.cache, (9, 2), 'a typed array';
    ck (try { h[1] = 'x'; 1 }) // $!.^name, 'X::TypeCheck::Assignment', '…checks a write through it';
    my Int $i = 5;
    ck (try { $i = @a.head(2); 1 }) // $!.^name, 'X::TypeCheck::Assignment', 'a typed Scalar still refuses a Seq';
}
{
    my @a = 1, 2, 3; my $h = @a.head(2); @a.unshift(0);
    ck $h.cache, (0, 1), 'an unshift before the read';
    my @b = 1, 2, 3; my $g = @b.head(2); @b.splice(0, 1, 9, 9);
    ck $g.cache, (9, 9), 'a splice';
    my @c = 1, 2, 3; my $f = @c.head(2); @c[0]:delete;
    ck $f.cache, (Any, 2), 'a delete';
    my @d = 1, 2, 3; my $x; my $d = @d.head(2); @d[0] := $x; $x = 5;
    ck $d.cache, (5, 2), 'a bind';
    my @s = 1, 2, 3; my $s = @s.head(2);
    ck $s.gist, '(1 2)', 'read';
    @s.shift;
    ck $s.cache, (1, 2), '…then a shift keeps the elements';
}
{
    my @a = 1, 2, 3;
    my $h = @a.head(2); my \t = @a.tail(2);
    @a = 7, 8, 9;
    ck ($h.cache, t.cache), ((1, 2), (2, 3)), 'a refill makes new containers: the old elements stay';
    @a[0] = 0;
    ck $h.cache, (1, 2), '…and a write to the array no longer shows';
    my @b = 1, 2, 3;
    my $g = @b.head(2); my $k = @b.head(2);
    @b[0] = 9; @b = 4, 5, 6; $g[0] = 0;
    ck ($g.cache, $k.cache, @b), ((0, 2), (0, 2), [4, 5, 6]), '…the write before it does, and one through it stays out of the array';
    for <map list STORE> -> $how {
        my @c = 1, 2, 3; my $c = @c.head(2);
        given $how {
            when 'map'   { @c = @c.map(* + 10) }
            when 'list'  { @c = [7, 8, 9] }
            when 'STORE' { @c.STORE((7, 8, 9)) }
        }
        ck $c.cache, (1, 2), "…refilled by $how";
    }
}
{
    my @a = 1, 2, 3;
    my @x = @a.head(2); my ($p, $q) = @a.head(2);
    @a[0] = 9;
    ck (@x, $p), ([1, 2], 1), 'an Array or a list of Scalars takes the values';
    my $h = @a.head(2); $h = 5;
    ck @a, [9, 2, 3], 'assigning the Scalar again leaves the array alone';
    my @o; my %h; my @p; my $i = 1;
    @o[0] = @a.head(2); %h<k> = @a.tail(2); @p[$i] = @a.head(1);
    @a[0] = 7; @a[2] = 0;
    ck (@o[0].cache, %h<k>.cache, @p[1].cache), ((7, 2), (2, 0), (7,)), 'an element keeps one too';
    my @q; @q[0, 1] = @a.head(2); @a[0] = 5;
    ck @q, [7, 2], 'a slice takes the values';
    class C { has @.a = 1, 2, 3; method h { my $h = @!a.head(2); @!a[0] = 9; $h } }
    ck C.new.h.cache, (9, 2), 'an attribute';
}

say 'PASS';
