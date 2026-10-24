# `my num @c = @b` copies a packed `int` / `num` array's words (packedCopy).
# The assignment read @b's Values through coerceArray, which unpacked @b for
# good: every later read of @b went the long way, and FORTRAN-LESSONS-2026-10-15's
# axpy-loop, which starts from `my num @c = @b`, took 410 ms where the same
# loop over arrays built by `»*»` takes 65.
#
# What changed is the speed, and a87a6b73's build passes this file too: it
# holds the copy's answers, Rakudo 2026.09's, which the words must keep. The
# copy is the target's own, a store into either leaves the other alone, and
# what is not the same full-width kind takes the general path.
#
# Contract: exit 0 + last line PASS.
my @fail;

sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

my num @b = 1e0, 2e0, 3e0;
my num @c = @b;
@c[0] = 9e0;
@b[1] = 7e0;
check (@b.List, @c.List), ((1e0, 7e0, 3e0), (9e0, 2e0, 3e0)), 'a num copy is its own';
my int @x = 1, 2, 3;
my int @y = @x;
@y.push(4);
check (@x.List, @y.List), ((1, 2, 3), (1, 2, 3, 4)), 'an int copy grows alone';
my int @z = @x;
@z = @z;
check @z.List, (1, 2, 3), 'a self-assignment';
my @plain = @x;
@plain[0] = "s";
check (@plain.List, @x.List), (("s", 2, 3), (1, 2, 3)), 'into a plain array';
my num @m;
check (try { @m = @x; 'stored' }) // $!.message, 'This type cannot unbox to a native number: P6opaque, Int', 'int into num';
my int8 @t = @x;
check (@t.List, @t.^name), ((1, 2, 3), 'array[int8]'), 'into an int8';
my num @q = 0e0;
@q = @b;
check @q.List, (1e0, 7e0, 3e0), 'a reassignment';
my int @e;
my int @f = @e;
check @f.elems, 0, 'an empty copy';
my $s = 0e0;
$s = $s + $_ for @b;
check $s, 11e0, 'the source reads after the copy';

if @fail { .say for @fail; say "FAIL"; exit 1 }
say "PASS";
