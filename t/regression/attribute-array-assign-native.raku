# Regression: `@!xs = …` in a method — and `@.xs = …`, `@!xs .= …`, a
# dynamic `@*d = …` — compiles under --exe.
#
# 500c29e2 (`my int @a` under --exe) sent every `@a = …` through
# rtArrayAssignTo(RT, <the array's place>, …), so a typed array is filled as
# the interpreter fills it, and named that place with varRef: a C++ local. An
# attribute or a dynamic has none, and the generated C++ did not compile
# ("use of undeclared identifier 'v_a_21xs'"). 42f79e98's rtNativeStoreLike
# did the same for a native `$*n = …`, which Rakudo refuses, so it is not
# here. Both take the place the store writes now (lvalueExpr). t/run.raku
# compiles this file with --exe and holds it to native.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

class C {
    has @!xs;
    has Int @.ints;
    method set(@in) { my @ys = @in; @!xs = @ys; self }
    method list-in(*@in) { @!xs = @in.reverse; self }
    method sorted { @!xs .= sort; self }
    method xs { @!xs }
    method fill { @.ints = 3, 4; self }
}

ck C.new.set([1, 2, 3]).xs, [1, 2, 3], '@!xs = @ys';
ck C.new.list-in(1, 2).xs, [2, 1], '@!xs = a Seq';
ck C.new.set([3, 1, 2]).sorted.xs, [1, 2, 3], '@!xs .= sort';
my $c = C.new.fill;
ck $c.ints.List, (3, 4), '@.ints = … into a typed attribute';
ck $c.ints.of, Int, '…which keeps its type';

my @*d = 1, 2;
sub set-d { @*d = 5, 6 }
set-d;
ck @*d, [5, 6], '@*d = … from a sub';

say 'PASS';
