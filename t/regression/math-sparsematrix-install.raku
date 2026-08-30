# Regression: the six engine gaps that stood between rakupp and
# `rakupp install Math::SparseMatrix` (Anton Antonov's, v0.0.15 + its
# ::Native half), found 2026-08-31 — five of them checked below. The dist's own
# 20-file suite is what surfaced them; every shape here was checked against
# Rakudo 2026.08 first, and each line's expectation is Rakudo's answer (this
# file passes unchanged on both engines).
#
# LibraryMake and Distribution::Builder::MakeFromJSON — the suspected
# blockers — turned out to work already: the build hook ran and the .dylib
# loaded. What failed was all engine-side, and none of it is about matrices.

my $fails = 0;
sub ok($cond, $what) { $fails++ unless $cond; say "not ok - $what" unless $cond }

# NOT covered here: the sixth fix, writing an element of a LIVE CArray reached
# through an accessor (`self.values[$i] = …`, how a CStruct's own CArray members
# are filled). Reproducing it needs a C library to hand back a non-null pointer,
# and this suite must stay runnable on a machine with no compiler — so
# Math::SparseMatrix::Native's own suite is what covers it. rakupp's own
# `nativecast(Pointer, $carray)` answers NULL, so it cannot stand in.

# 2 — eqv on two repr('CStruct') instances. Their state is in C memory, not
#     in `attrs`, so the generic attribute walk compared MALLOC ADDRESSES and
#     called every pair of distinct structs unequal. Rakudo compares the
#     scalar fields and does not compare pointer fields (a raw pointer carries
#     no length), which is what `$m[2] eqv $m.row-at(2)` relies on.
{
    use NativeCall;
    class Pt is repr('CStruct') { has int32 $.x is rw; has num64 $.y is rw }
    my $a = Pt.new; $a.x = 3; $a.y = 1.5e0;
    my $b = Pt.new; $b.x = 3; $b.y = 1.5e0;
    my $c = Pt.new; $c.x = 4; $c.y = 1.5e0;
    ok($a eqv $b,  'CStructs with equal fields are eqv');
    ok(!($a eqv $c), 'CStructs with differing fields are not eqv');
}

# 3 — a user-defined postcircumfix on the BUILT-IN brackets. rakupp registered
#     user postcircumfix operators only for custom bracket pairs, and `[` is a
#     bracket the parser had already claimed, so `postcircumfix:<[ ]>` and
#     `postcircumfix:<[; ]>` compiled and were then never reached.
#     Math::SparseMatrix exports both as its element accessor.
{
    class Mx {}
    multi sub postcircumfix:<[ ]>(Mx:D $m, $i)   { "one($i)" }
    multi sub postcircumfix:<[; ]>(Mx:D $m, @ix) { "many({@ix.join(',')})" }
    my $m = Mx.new;
    ok($m[7]      eq 'one(7)',       'user postcircumfix:<[ ]> is dispatched');
    ok($m[3;2]    eq 'many(3,2)',    'user postcircumfix:<[; ]> is dispatched');
    ok($m[1;2;3]  eq 'many(1,2,3)',  '…with more than two dimensions');
    my @plain = 10, 20, 30;           # the built-in subscript is untouched
    ok(@plain[1] == 20, 'a plain array subscript still uses the built-in');
}

# 4 — reading an element out of a LIST must not itemize it. An ARRAY's
#     elements each live in a Scalar container, a LIST's do not. Itemizing
#     both made every sublist read out of a list arrive as one opaque item,
#     which a SLURPY then took for a single argument: the dist's
#     `row-slice(*@indexes)` was handed one $("b","c") instead of two strings
#     and rejected them.
{
    sub first-of(@ix) { @ix[0] }
    ok(first-of((<b c>, 0)).raku eq '("b", "c")', 'a list element is not itemized');
    ok(first-of([<b c>, 0]).raku eq '$("b", "c")', 'an array element still is');
    my $scalar = (1, 2);              # …and an already-itemized element stays so
    ok(first-of(($scalar, 0)).raku eq '$(1, 2)', 'an itemized element is left alone');
}

# 5 — a user-defined PREFIX operator on an object outranks the built-in one,
#     as its narrower candidate does in Rakudo. The user lookup sat at the
#     foot of the prefix evaluator, reached only when no built-in arm claimed
#     the operator first, so an overload of a built-in NAME was unreachable:
#     `-$matrix` numified the object and answered -0. (infix:<+> overloads
#     already worked, which is why this looked like a general gap and was not.)
{
    class Neg { has $.v = 5 }
    multi sub prefix:<->(Neg:D $n) { "neg({$n.v})" }
    ok((-Neg.new) eq 'neg(5)', 'a user prefix:<-> on an object wins');
    ok(-3 == -3 && -(2+1) == -3, 'the built-in prefix:<-> is untouched');
}

# 6 — assigning through a `handles`-delegated `is rw` accessor. The lvalue
#     path fell through to `attrs[<name>]` on the OUTER object, creating a
#     phantom attribute nothing reads: the write vanished while the accessor
#     went on answering the delegate's untouched value. Math::SparseMatrix
#     delegates `implicit-value` to its core matrix and sets it this way.
{
    class Core  { has $.iv is rw = 0; has $.ro = 9 }
    class Outer { has Core $.core is rw handles <iv ro> = Core.new }
    my $o = Outer.new;
    $o.iv = 7;
    ok($o.iv == 7,      'assignment through a delegated rw accessor sticks');
    ok($o.core.iv == 7, '…and lands on the delegate, not a phantom attribute');
    ok((try { $o.ro = 1; True }) !=== True, 'a delegated READ-ONLY accessor still refuses');
}

say $fails == 0 ?? 'PASS' !! "FAIL ($fails)";
exit $fails ?? 1 !! 0;
