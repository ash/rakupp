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
