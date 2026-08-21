# Native (--exe) parity probes: things that once diverged between the
# interpreter and natively-compiled binaries (t/run.raku "native parity").

# 1. deep recursion: the exe main thread needs a real stack, not the OS
#    default (macOS ld -stack_size / Windows /STACK) — depth 30k overflows
#    a default main stack in native code long before the guard fires
sub deep(Int $n) { $n == 0 ?? 0 !! 1 + deep($n - 1) }
say "deep: ", deep(30_000);

# 2. a caught builtin error answers .message (exceptionFor builds a real
#    exception object; a bare type payload would die inside CATCH and mask
#    the original error)
{
    42.nosuchmethod;
    CATCH { default { say "caught: ", .message } }
}

# 3. a block-final if/elsif/else chain is the pointy block's value
my &pick = -> $x {
    if    $x == 1 { "one" }
    elsif $x == 2 { "two" }
    else          { "many" }
};
say "pick: ", pick(1), " ", pick(2), " ", pick(3);

# 4. sort with a comparator returning hand-built Order values (the enum
#    numerics must survive native name-term resolution)
my @s = (5, 2, 9, 1).sort(-> $x, $y { $x < $y ?? Less !! ($x > $y ?? More !! Same) });
say "sort: ", @s.join(",");

# 5. SHAPED ARRAYS. `my @a[3;2]` was not implemented in the native backend at
#    all: it compiled without a word and then answered `(*)` to .shape and
#    nonsense to `@a[1;1]`. The declaration builds a shaped container now, and
#    a multi-dim subscript goes through the same AT-POS/ASSIGN-POS the
#    interpreter walks.
my @m[3;2] = (1..6).rotor(2);
say "shape: ", @m.shape, " elems: ", @m.elems, " at: ", @m[1;1];
@m[1;0] = 99;
@m[0;1] += 10;
say "raku: ", @m.raku;
my @flat = @m;                        # a shaped source contributes its LEAVES
say "flat: ", @flat.elems, " ", @flat.join(",");
my @g[2;3];
my $c = 0;
for ^2 -> $y { for ^3 -> $x { $c++; @g[$y;$x] = $c } }
say "grid: ", @g.raku, " sum: ", @g.sum;
my $n = 2;
my @dyn[$n;$n] = (1,2),(3,4);         # dimensions may be expressions
say "dyn: ", @dyn.shape, " ", @dyn[1;1];
