# Regression: `@a.BIND-POS($i, $var)` and `%h.BIND-KEY($k, $var)` bind the
# variable's CONTAINER, the shared cell `$a := $var` makes, and a readonly
# parameter's value in a container of its own.
#
# They used to store an env-slot alias instead: a Proxy holding the variable's
# whole frame alive and finding the name again, through a FETCH call, on every
# read of the element. BinaryHeap 2.0 binds every heap node this way
# (`@!array.BIND-POS($pos, $node)` with `$node` a parameter), so a heap of N
# elements kept N method frames, and each comparison paid a name lookup. The
# alias also followed the NAME: after `$v := $w` the element read `$w`, where
# Rakudo's element keeps the container `$v` had when it was bound.
#
# Runs clean under Rakudo too.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# ---- a `my` variable: one container, both ways ----
{
    my @a;
    my $m = 40;
    @a.BIND-POS(0, $m);
    $m = 41;
    ck @a[0], 41, 'BIND-POS of a variable sees a later write to it';
    @a[0] = 42;
    ck $m, 42, '…and a write to the element lands in the variable';
    ck (@a[0] =:= $m), True, '…the two are one container';
}
{
    my @a;
    my $v = 1;
    @a.BIND-POS(0, $v);
    my $w = 5;
    $v := $w;
    $v = 6;
    ck @a[0], 1, 'rebinding the variable leaves the element its old container';
}
{
    my @a;
    { my $k = 50; @a.BIND-POS(0, $k) }
    ck @a[0], 50, 'a block-scoped variable outlives its block in the element';
    @a[0] = 51;
    ck @a[0], 51, '…and the element stays writable';
}
{
    my @a;
    my $q = 7; my $r := $q;
    @a.BIND-POS(0, $r);
    $q = 8;
    ck @a[0], 8, 'BIND-POS of a bound alias shares the original container';
}
{
    my %h; my $hv = 1;
    %h.BIND-KEY('k', $hv);
    $hv = 2;
    ck %h<k>, 2, 'BIND-KEY of a variable sees a later write to it';
}

# ---- parameters ----
{
    my @a;
    sub f($x) { @a.BIND-POS(0, $x) }
    my $p = 10; f($p); $p = 11;
    ck @a[0], 10, 'a readonly parameter binds its value, not the caller\'s variable';
    sub g($x is copy) { @a.BIND-POS(1, $x); $x = 21 }
    g(20);
    ck @a[1], 21, 'an `is copy` parameter binds its own container';
    sub h($x is rw) { @a.BIND-POS(2, $x) }
    my $rw = 30; h($rw); $rw = 31;
    ck @a[2], 31, 'an `is rw` parameter binds the caller\'s container';
}

# ---- element aliases taken from such slots hand the container on ----
{
    class H {
        has @!array;
        method ins($pos, $value) { @!array.BIND-POS($pos, $value) }
        method mv($to, $from) { my \p = @!array[$from]; @!array.BIND-POS($to, p) }
        method mvc($to, $from) { my $c := @!array[$from]; @!array.BIND-POS($to, $c) }
        method all { @!array.List }
    }
    my $h = H.new;
    $h.ins(0, 5); $h.ins(1, 6);
    $h.mv(2, 0);
    $h.ins(0, 7);
    ck $h.all, (7, 6, 5), 'an alias of a bound slot keeps its node when the slot is rebound';
    $h.mvc(1, 2);
    $h.ins(2, 9);
    ck $h.all, (7, 5, 9), '…through `my $c :=` as through `my \p =`';
}

# ---- a heap in BinaryHeap 2.0's shape still sorts ----
{
    role GenHeap[&infix:<tops>] {
        has @!array;
        has $!elems = 0;
        method elems() { $!elems }
        method push(*@values) { self!insert($_) for @values; self }
        method !insert($value) {
            my $pos = $!elems++;
            while $pos > 0 {
                my $parent = ($pos - 1) div 2;
                my \p = @!array[$parent];
                last unless infix:<tops>($value, p);
                @!array.BIND-POS($pos, p);
                $pos = $parent;
            }
            @!array.BIND-POS($pos, $value);
        }
        method pop() {
            return Nil unless $!elems;
            my $top = @!array[0];
            my $last = @!array[--$!elems];
            @!array[$!elems]:delete;
            self!sift-down(0, $last) if $!elems;
            $top
        }
        method !sift-down($pos is copy, $node) {
            my $n = $!elems;
            loop {
                my $c = 2 * $pos + 1;
                last if $c >= $n;
                my \left = @!array[$c];
                if $c + 1 < $n {
                    my \right = @!array[$c + 1];
                    $c++ if infix:<tops>(right, left);
                }
                my $child := @!array[$c];
                last unless infix:<tops>($child, $node);
                @!array.BIND-POS($pos, $child);
                $pos = $c;
            }
            @!array.BIND-POS($pos, $node);
        }
    }
    class MinHeap does GenHeap[* cmp * == Less] { }
    my @data = (^300).map({ ($_ * 7919) % 1009 });
    my $heap = MinHeap.new;
    $heap.push(@data);
    my @out;
    @out.push($heap.pop) while $heap.elems;
    ck @out, [@data.sort], 'a heap built from BIND-POS of parameters pops in order';
}

say $fails ?? "FAIL ($fails)" !! 'PASS';
