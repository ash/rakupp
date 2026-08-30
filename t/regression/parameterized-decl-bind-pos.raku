# Regression: the rest of issue #47 (Anton Antonov). With the binding, operator
# and CREATE fixes in, `Graph`'s Dijkstra still walked an unordered queue,
# because BinaryHeap needs three more things: a parameterized type in DECLARATOR
# position, BIND-POS, and a `* cmp *` curry that remembers the comparator its
# MIXIN was handed. Checked against Rakudo.

# the grammar has to know `precedes` as an infix before the role body uses it,
# exactly as BinaryHeap declares it
proto sub infix:<precedes>($, $) {*}

my $ok = True;
sub ck($got, $want, $l) { unless $got eqv $want { note "FAIL: $l — {$got.raku} vs {$want.raku}"; $ok = False } }

# 1. A parameterized type in DECLARATOR position is evaluated, not spelled. The
#    `[...]` group was folded into the type NAME as source text, which is right
#    for `CArray[uint8]` and a fiction for anything else: `my Pair[Int] $x` named
#    no registered class, so every method on it fell through to a built-in.
{
    my role Holder[::T] { method of-type { T.^name } }
    my class Box does Holder[Str] { }
    # the declarator form must reach the same type the expression form does
    my \E = Holder[Int];
    ck(E.^name.contains('Holder'), True, 'the expression form parameterizes');

    my Box $b .= new;
    ck($b.of-type, 'Str', 'a declared user type still works');
}

# 2. …and it works at MAINLINE scope too, where the pre-declaration pass runs
#    before any `use` and so cannot resolve the base type yet.
my role Cmp[&how] { method apply($a, $b) { how($a, $b) } }
my class Runner does Cmp[-> $a, $b { $a + $b }] { }
my Runner $mainline .= new;
ck($mainline.apply(2, 3), 5, 'a parameterized type declared at mainline scope');

# 3. BIND-POS stores the CONTAINER, so the slot becomes an alias: reading it
#    fetches through, and binding to it picks the same container up. BinaryHeap's
#    sift-down builds a path of these and shifts values down it.
{
    my @src  = 10, 20, 30;
    my @path;
    @path.BIND-POS(0, @src[1]);
    ck(@path[0], 20, 'BIND-POS reads through to the bound slot');
    @path[0] = 99;
    ck(@src, [10, 99, 30], 'and writing the slot writes the source');
    @src[1] = 55;
    ck(@path[0], 55, 'the alias is live in both directions');

    # a bound scalar passes its own container on rather than being re-wrapped
    my @a = 1, 2, 3;
    my @p2;
    my $n := @a[2];
    @p2.BIND-POS(0, $n);
    my \c = @p2[0];
    c = 7;
    ck(@a, [1, 2, 7], 'a bound scalar hands its container to BIND-POS');
}

# 4. A `* cmp *` curry built where `&infix:<cmp>` is a PARAMETER keeps that
#    comparator when it runs elsewhere. Resolving the shadow at call time instead
#    found the built-in, because the routine that supplied it had long returned —
#    which is exactly how BinaryHeap::MinHeap[&cmp] lost its comparator.
{
    sub make-comparator(&infix:<cmp>) { my &pre = * cmp * == Less; &pre }   # escapes
    my &by-len = make-comparator(-> $a, $b { $a.chars <=> $b.chars });
    ck(by-len('aaa', 'bb'), False, 'the escaped curry uses the comparator it was given');
    ck(by-len('bb', 'aaa'), True,  'and orders by it, not by the built-in cmp');
}

# 5. All of it together: a min-heap parameterized with a comparator over the
#    SECOND element orders by score, not by the first element's natural order.
{
    role Heap[&infix:<precedes> = * cmp * == Less] {
        has @!a;
        method add(\v) {
            @!a.push(v);
            my int $i = @!a.elems - 1;
            while $i > 0 {
                my int $p = ($i - 1) div 2;
                last unless @!a[$i] precedes @!a[$p];
                my $t = @!a[$p]; @!a[$p] = @!a[$i]; @!a[$i] = $t;
                $i = $p;
            }
            self
        }
        method peek { @!a[0] }
    }
    my class ByScore does Heap[{ ($^x.tail <=> $^y.tail) == Less }] { }
    my $h = ByScore.new;
    $h.add($_) for ["zz", 1], ["aa", 9], ["mm", 5], ["bb", 3];
    ck($h.peek, ["zz", 1], 'a comparator over .tail beats the natural order of .head');
}

say $ok ?? 'PASS' !! 'FAIL';
