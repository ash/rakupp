# `for … -> int $i`, the counted loop with a native counter
# (countedForParam, FORTRAN-LESSONS-2026-10-15 item 1a): the interpreter binds
# the counter as a native, so the kernel's register is one, and a native
# operation on it wraps at 64 bits as the interpreter's does. Nested in a
# kernel body it lowers as `my int $j` (jit::nestedForLoop). `-> Int $i` is
# t/jit/cases/typed-counted-for.raku.
my num @a = (^1000) »*» 0.5e0;
my num $s = 0e0;
for ^1000 -> int $i { $s = $s + @a[$i] * @a[$i] }
say "dot $s";
my $w = 0;
for ^200 -> int $i { $w = $i + 9223372036854775800 }
say "wrap $w";
my $c = 0;
for 1 .. 300 -> int $i { next if $i %% 3; last if $i > 250; $c = $c + $i * 2 }
say "control $c";
my $t = 0; my $k = 0;
while $k < 40 { for ^25 -> int $j { $t = $t + $k * $j }; $k++ }
say "nested $t";
my $v = 0; my $m = 0;
while $m < 40 { for ^25 -> int64 $j { $v = $j + 9223372036854775790 }; $m++ }
say "nested wrap $v";
