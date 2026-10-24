# A `for` over a Range with a BigInt end. Such a range keeps its real ends
# in RangeEnds and only saturated (or width-only) numbers in rFrom/rTo, and
# the counted loop walked those numbers, interpreted and compiled, with -O
# and without: `for 2**64 .. 2**64 + 2 -> $i` ran ONCE, with $i =
# 9223372036854775807, where Rakudo 2026.09 runs it three times from 2**64.
# And `2**64 .. *` did not carry its start at all, so even its .raku said
# 9223372036854775807..Inf.
#
# Both walks now step through the carried ends (rtBigRangeStep), lazily, so
# the endless form runs until its `last`; and `BIG .. *` carries its start.
# This file holds each engine to Rakudo's lines: interpreted, `--exe` and
# `--exe -O`, all compiled natively (a bundle would test the interpreter).
#
# (The endless and exclusive loops also stop below 2**64: a build that walks
# the saturated fields starts at int64's max and wraps, never reaching a `last`.)
#
# Contract: exit 0 + last line PASS.
my @fail;
my $bin = $*EXECUTABLE.absolute;
my $dir = $*TMPDIR.add("rakupp-for-bigint-range-{$*PID}");
$dir.mkdir;
LEAVE { try { .unlink for $dir.dir; $dir.rmdir } }
my $src = q:to/END/;
    for 2**64 .. 2**64 + 2 -> $i { say "pointy $i" }
    for 2**64 .. 2**64 + 1 { say "topic $_" }
    for 9223372036854775806 .. 9223372036854775808 -> $i { say "across $i" }
    for 2**64 ^..^ 2**64 + 3 -> $i { last if $i < 2**64; say "excl $i" }
    for 2**64 .. * -> $i { last if $i > 2**64 + 1 || $i < 2**64; say "endless $i" }
    for 2**64 ..^ * -> $i { last if $i > 2**64 || $i < 2**64; say "endless-ex $i" }
    for 2**64 .. 2**64 + 4 -> $i { next if $i %% 2; say "next $i" }
    for -2**64 - 1 .. -2**64 -> $i { say "neg $i" }
    for 2**64 + 2 .. 2**64 -> $i { say "never $i" }
    for 2**64 .. 2**64 + 1 -> Int $i { say "typed $i" }
    sub span($n) { my $s = 0; for $n .. $n + 999 -> $i { $s = $s + ($i - $n) }; $s }
    say "sum ", span(2**64);
    say "small ", span(5);
    say (2**64 .. *).raku;
    END

# What Rakudo 2026.09 prints for $src, line for line.
my @want =
    'pointy 18446744073709551616', 'pointy 18446744073709551617', 'pointy 18446744073709551618',
    'topic 18446744073709551616', 'topic 18446744073709551617',
    'across 9223372036854775806', 'across 9223372036854775807', 'across 9223372036854775808',
    'excl 18446744073709551617', 'excl 18446744073709551618',
    'endless 18446744073709551616', 'endless 18446744073709551617',
    'endless-ex 18446744073709551616',
    'next 18446744073709551617', 'next 18446744073709551619',
    'neg -18446744073709551617', 'neg -18446744073709551616',
    'typed 18446744073709551616', 'typed 18446744073709551617',
    'sum 499500', 'small 499500',
    '18446744073709551616..Inf';

sub held($what, $out) {
    my @got = $out.lines;
    for ^max(+@want, +@got) -> $k {
        @fail.push("$what, line {$k + 1}: got {(@got[$k] // '(nothing)').raku}, want {(@want[$k] // '(nothing)').raku}")
            if (@got[$k] // '') ne (@want[$k] // '');
    }
}

my $f = $dir.add("bigrange.raku");
$f.spurt($src);
my $interp = run($bin, $f.absolute, :out, :err);
held('interpreted', $interp.out.slurp(:close)); $interp.err.slurp(:close);

for (('--exe', '-O'), ('--exe',)) -> @mode {
    my $exe = $dir.add("bigrange-{+@mode}.bin");
    my $c = run($bin, |@mode, $f.absolute, '-o', $exe.absolute, :out, :err);
    $c.out.slurp(:close); my $cerr = $c.err.slurp(:close);
    if !$exe.e { @fail.push("{@mode} did not compile: $cerr"); next }
    @fail.push("{@mode} bundled the program, so it tests the interpreter: $cerr") if $cerr.contains('bundling');
    my $r = run($exe.absolute, :out, :err);
    held(@mode.join(' '), $r.out.slurp(:close)); $r.err.slurp(:close);
}

if @fail { .say for @fail; say "FAIL" } else { say "PASS" }
