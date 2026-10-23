# `for ^$n -> int $i` and `-> Int $i` take the counted Int-Range path, the
# loop kernels and the --cnp site, as `-> $i` does (FORTRAN-LESSONS-2026-10-15,
# item 1a). The type used to send the loop through general signature binding
# — a scope and a bindParams call per iteration, and no kernel at all — so the
# loop a Fortran programmer writes first ran 20-70x slower than untyped.
#
# What the fast path has to keep from the binding it skips, each value checked
# against Rakudo 2026.09: the variable is read-only; an `int` one is a native,
# so `+` past int64 wraps and `** -1` is 0, where an `Int` one does neither;
# each iteration is its own binding (closures, `redo`, phasers, `do for`);
# and over anything but an integer Range the type is still checked.
use Test;
plan 29;

throws-like q[for ^1 -> int $i { $i = 5 }], Exception, 'an `int` loop variable is read-only';
throws-like q[for ^1 -> Int $i { $i = 5 }], Exception, '…and an `Int` one';

my $r;
for 1..1 -> int $i   { $r = $i + 9223372036854775807 }
is $r, -9223372036854775808, '`int`: `+` past int64 wraps';
for 1..1 -> int64 $i { $r = $i + 9223372036854775807 }
is $r, -9223372036854775808, '`int64` too';
for 1..1 -> Int $i   { $r = $i + 9223372036854775807 }
is $r, 9223372036854775808, '`Int` does not';

# kernel-sized: a result that leaves int64 bails out of the loop kernel, and
# the interpreter's re-run has to mint the native, or the wrap is lost
my $w = 0;
for ^100 -> int $i { $w = $i + 9223372036854775800 }
is $w, -9223372036854775717, 'a wrap inside a kernel-sized `int` loop';
$w = 0;
for ^100 -> Int $i { $w = $i + 9223372036854775800 }
is $w, 9223372036854775899, '…and none inside an `Int` one';
my $p = 0;
for 1..100 -> int $i { $p = $p + $i ** -1 }
is $p, 0, '`int` ** -1 is the native candidate';
for 2..2 -> Int $i { $p = $i ** -1 }
is $p, 0.5, '`Int` ** -1 is a Rat';

my $s = 0;
for ^10000 -> int $i { $s = $s + $i }
is $s, 49995000, 'a sum over an `int` counter';
my int $acc = 0;
for ^10 -> int $i { $acc = $acc + $i }
is $acc, 45, '…into a `my int`';
$s = 0;
for ^300 -> int $i { for ^20 -> int $j { $s = $s + $i * $j } }
is $s, 8521500, 'nested `int` loops';

my @c;
for ^3 -> int $i { @c.push: { $i } }
is-deeply @c».(), [0, 1, 2], 'each closure keeps its own iteration';
my $o = '';
for ^5 -> int $i { next if $i == 1; last if $i == 3; $o ~= $i }
is $o, '02', '`next` and `last`';
my $n = 0;
for ^3 -> int $i { $n++; redo if $n == 2 }
is $n, 4, '`redo`';
$o = '';
for ^3 -> int $i { FIRST $o ~= 'F'; LAST $o ~= 'L'; $o ~= $i }
is $o, 'F012L', 'FIRST and LAST';
is-deeply (do for ^4 -> int $i { $i * 2 }), (0, 2, 4, 6), '`do for` collects';
my atomicint $t = 0;
hyper for ^1000 -> int $i { $t ⚛+= $i }
is $t, 499500, '`hyper for`';

$o = '';
for ^0 -> int $i { $o ~= 'x' }
for 5..3 -> int $i { $o ~= 'x' }
for -3..-1 -> int $i { $o ~= $i }
is $o, '-3-2-1', 'empty, reversed and negative ranges';
my $i = 'outer';
for ^5 -> int $i { }
is $i, 'outer', 'the loop variable does not outlive the loop';

# not a Range of machine integers: general binding, with its type check —
# and a BigInt end is not in the integer fields the counted path walks
$o = '';
for 2**64 .. 2**64 + 2 -> Int $k { $o ~= "$k " }
is $o, '18446744073709551616 18446744073709551617 18446744073709551618 ', 'a BigInt range binds `Int`';
$o = '';
for 9223372036854775806 .. 9223372036854775808 -> Int $k { $o ~= "$k " }
is $o, '9223372036854775806 9223372036854775807 9223372036854775808 ', '…and one that ends past int64';
dies-ok { for 2**64 .. 2**64 + 2 -> int $k { } }, 'a BigInt does not bind an `int`';
dies-ok { for 'a', 'b' -> int $i { } }, 'a Str does not bind an `int`';
dies-ok { for ('x',) -> Int $n { } }, '…nor an `Int`';
$o = '';
for (1, 2) -> Int $k { $o ~= $k }
is $o, '12', 'a List of Ints binds `Int`';
$o = '';
for 250..258 -> uint8 $k { $o ~= "$k " }
is $o, '250 251 252 253 254 255 0 1 2 ', 'a sized native still wraps at its width';
$o = '';
for -1..1 -> uint $k { $o ~= "$k " }
is $o, '18446744073709551615 0 1 ', '…and an unsigned one';
$o = '';
for 1.5 .. 3 -> Num() $k { $o ~= "$k " }
is $o, '1.5 2.5 ', 'a coercion still coerces';
