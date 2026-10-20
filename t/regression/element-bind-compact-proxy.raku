# Regression: binding to an array element (`my $x := @a[i]`, `my \l = @a[i]`,
# `@p.BIND-POS(i, @a[j])`) and binding a variable to a variable (`$a := $b`)
# keep their semantics now that both proxies are compact.
#
# The array-slot proxy had grown to six hidden entries (array, index, length
# at bind time, last value, FETCH, STORE), one past the four a ValueHash keeps
# without an index, so every element bind grew a second chunk and built an
# index: five allocations and a rehash, 10-20x Rakudo's cost per bind, which is
# most of BinaryHeap 2.0's ~10x. It is four entries now, with the index and the
# length packed into the array entry's word, and the shared-cell proxy holds its
# cell as a cell holder instead of a boxed `ext` handle.
#
# Runs clean under Rakudo too.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# ---- an element alias reads and writes the element ----
{
    my @a = 10, 20, 30;
    my $x := @a[1];
    ck $x, 20, 'my $x := @a[1] reads the element';
    $x = 21;
    ck @a[1], 21, '…a write through the alias lands in the array';
    @a[1] = 22;
    ck $x, 22, '…and a write to the element is seen through the alias';
    ck ($x =:= @a[1]), True, '…and the two are one container';
    @a.push(40);
    $x = 23;
    ck @a, [10, 23, 30, 40], '…still element 1 after the array grows';
}
{
    my @a = 1, 2, 3;
    my \l = @a[2];
    ck l, 3, 'my \l = @a[2] reads the element';
    @a[2] = 33;
    ck l, 33, '…and sees a later write';
    my @big = ^2000;
    my $far := @big[1500];
    $far = -1;
    ck @big[1500], -1, 'an alias far into a long array writes its element';
    my $new := @big[2100];
    $new = 7;
    ck @big.elems, 2101, 'an alias past the end autovivifies up to it';
    ck @big[2100], 7, '…and writes there';
}

# ---- once the array shrinks, the alias keeps its element's value ----
{
    my @a = <a b c>;
    my $x := @a[2];
    ck $x, 'c', 'an alias to the last element';
    @a.pop;
    ck $x, 'c', '…keeps the value after the element is popped';
    $x = 'z';
    ck @a, [<a b>], '…and a write to it no longer reaches the array';
    ck $x, 'z', '…but is kept by the alias';
}

# ---- an alias handed on is the same alias ----
{
    my @a = 1, 2, 3;
    my $x := @a[0];
    my $y := $x;
    $y = 100;
    ck @a[0], 100, 'an alias bound from an alias writes the same element';
    my @p;
    @p.BIND-POS(0, @a[2]);
    @p.BIND-POS(1, $x);
    @p[0] = 300;
    @p[1] = 101;
    ck @a, [101, 2, 300], 'BIND-POS of an element and of an alias writes through both';
    @a[2] = 3;
    ck @p[0], 3, '…and reads the array back';
}

# ---- BinaryHeap's sift-down: a path of aliases, values shifted down it ----
{
    my @heap = 1, 9, 2, 8, 7;
    my @path;
    my $node := @heap[0];
    @path.BIND-POS(0, $node);
    my \left = @heap[1];
    @path.BIND-POS(1, left);
    my \right = @heap[4];
    @path.BIND-POS(2, right);
    my $value = $node;
    for 1, 2 -> $i {
        my \child = @path[$i];
        $node = child;
        $node := child;
    }
    $node = $value;
    ck @heap, [9, 7, 2, 8, 1], 'a path of element aliases shifts values down it';
}

# ---- a variable bound to a variable shares its container ----
{
    my $b = 1;
    my $a := $b;
    $b = 2;
    ck $a, 2, '$a := $b sees a write to $b';
    $a = 3;
    ck $b, 3, '…and $b sees a write to $a';
    ck ($a =:= $b), True, '…one container';
    my $c := $b;
    ck ($c =:= $a), True, 'a second alias of $b is the same container';
    $b := 9;
    ck $a, 3, 'rebinding $b leaves $a with the old container';
    ck ($c =:= $a), True, '…which $c still shares';
}
{
    my @a;
    my $x = 1;
    @a[0] := $x;
    $x = 5;
    ck @a[0], 5, '@a[0] := $x shares $x';
    @a[0] = 6;
    ck $x, 6, '…both ways';
    my %h;
    %h<k> := $x;
    $x = 7;
    ck %h<k>, 7, '%h<k> := $x shares $x';
}

# ---- .clone gives a bound attribute its own container ----
{
    class C { has $.v is rw; method bind(\x) { $!v := x } }
    my $outer = 1;
    my $o = C.new;
    $o.bind($outer);
    my $c = $o.clone;
    $outer = 2;
    ck $o.v, 2, 'an attribute bound to a variable sees it';
    ck $c.v, 1, '…its clone kept the value, not the container';
}

say $fails ?? "FAIL ($fails)" !! 'PASS';
