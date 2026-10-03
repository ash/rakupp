# Regression: the loops of t/regression/loop-kernel-exactness.raku that the
# native backend compiles, so t/exe/run.raku runs them as --exe LANES (the Str
# slots and `given` / `when` of Codegen's unboxed loops, which plain --exe now
# takes as well as -O) against the interpreter's loop kernels. Nothing here may
# keep the program from compiling natively: no allomorph, no `start`, no
# `subset`, which would bundle the whole file and test neither.
#
# Expectations checked against Rakudo 2026.09 via /opt/homebrew/bin/rakudo. Green on both,
# and identical compiled, interpreted, and with RAKUPP_NO_KERNELS=1.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# strings
{ my $a = "delta"; my $b = "delt" ~ "a"; my $c = 0;
  for 1 .. 1000 { $c++ if $a eq $b; $c-- if $a lt $b; }
  ck($c, 1000, 'eq / lt') }
{ my $x = "abc"; my $n = 0;
  for 1 .. 2 { $n++ if $x lt "abd"; $n++ if $x le "abc"; $n++ if $x gt "abb"; $n++ unless $x ge "abd"; $n++ if $x ne "abd" }
  ck($n, 10, 'the six comparisons') }
{ my $u = "é"; my $z = "z"; my $n = 0; for 1 .. 3 { $n++ if $u gt $z }; ck($n, 3, 'code-point order past ASCII') }
{ my $s = ""; for 1 .. 5000 { $s ~= "x" }; ck($s.chars, 5000, '~= builds a string') }
{ my $s = "e"; for 1 .. 1 { $s ~= "\x[301]" }; ck(($s.chars, $s.codes), (1, 1), '~= renormalizes a combining mark') }
{ my $s = "a"; for 1 .. 2 { $s = $s ~ "\x[308]" }; ck(($s.chars, $s.codes), (1, 2), '~ renormalizes a combining mark') }
{ my $s = "a"; for 1 .. 3 { $s ~= $s }; ck($s, 'a' x 8, 'a string appended to itself') }
{ my $a = "x"; my $b = ""; for 1 .. 3 { $b = $a; $a ~= "y" }; ck(($a, $b), ('xyyy', 'xyy'), 'a copy is a copy') }
{ my $a = "a"; my $b = "b"; my $s = ""; for 1 .. 2 { $s = $s ~ $a ~ $b ~ "-" }; ck($s, 'ab-ab-', 'a chain of ~') }
{ my $s = ""; for 1 .. 3 { my $t = "<"; $t ~= "#"; $s ~= $t }; ck($s, '<#<#<#', 'a `my` Str local') }

# what a Str slot must refuse
{ my $s = 5; for 1 .. 3 { $s ~= "x" }; ck($s, '5xxx', 'an Int in a variable used as a Str') }
{ my $x = 5; my $n = 0; for 1 .. 3 { $n++ if $x eq "5"; $x = $x + 1 }; ck(($x, $n), (8, 1), 'one variable as Str and as number') }
{ my $s = "a"; for 1 .. 3 { $s = 7 if $_ == 2; $s ~= "b" }; ck($s, '7bb', 'a Str variable assigned an Int') }
{ my $s = ""; for 1 .. 5 { $s ~= $_ }; ck($s, '12345', 'an Int appended to a Str') }

# a bail rewinds the Str slots with the others
{ my $x = 9223372036854775800; my $s = "";
  for 1 .. 20 { $x++; $s ~= "a" }
  ck(($x, $s.chars), (9223372036854775820, 20), 'an integer overflow beside a Str') }

# ranges
{ my $t = 0; for ^10 { $t += $_ }; ck($t, 45, '^N') }
{ my $t = 7; my $n = 0; for ^$n { $t++ }; ck($t, 7, '^0') }
{ my $t = 7; my $n = -3; for ^$n { $t++ }; ck($t, 7, '^ of a negative') }
{ my $s = 0; for 1 .. 10 -> $i { for ^$i -> $j { $s += $j } }; ck($s, 165, 'nested ^N') }
{ my $c = 0; for 9223372036854775805 .. 9223372036854775807 { $c++ }; ck($c, 3, 'a range up to the largest int64') }

# given / when
{ my $n = 0; for ^30 -> $i { given $i % 3 { when 0 { $n = $n + 1 }; when 1 { $n = $n + 2 }; default { $n = $n + 3 } } }
  ck($n, 60, 'given / when / default over an Int') }
{ my $n = 0; my $w = "b"; for 1 .. 4 { given $w { when "a" { $n += 1 }; when "b" { $n += 10; $w = "a" } } }
  ck(($n, $w), (13, 'a'), 'given / when over a Str') }
{ my $n = 0; for 1 .. 10 { given $_ { when $_ > 7 { $n += 100 }; when 3 { $n += 3 }; $n += 1 } }
  ck($n, 309, 'a Bool `when`, and statements after the whens') }
{ my $n = 0; for 1 .. 10 { given $_ * 2 { when 4 { next }; when 8 { last } }; $n++ }; ck($n, 2, '`next` and `last` from a `when`') }
{ my $n = 0; for 1 .. 3 { given $_ { when 2 { $n += 10 } }; $n++ }; ck($n, 13, 'a `when` leaves the `given`, not the loop') }
{ my $n = 0; for 1 .. 3 { given $_ { when "2" { $n += 10 }; default { $n++ } } }; ck($n, 12, 'an Int topic against a Str `when`') }
{ my $n = 0; my $x = 0e0; for 1 .. 4 { $x = $x + 0.5e0; given $x { when 1 { $n += 10 }; when 2 { $n += 100 }; default { $n++ } } }
  ck($n, 112, 'a Num topic against Int `when`s') }

# Nums: each operation rounds on its own, as the interpreter's do — a compiler
# left to fuse `a * b + c` into one FMA answered 5.55e-17 here
{ my $a = 0.1e0; my $r = 0e0; for 1 .. 3 { $a = $a + 0e0; $r = $a * 10e0 + -1e0 }; ck($r, 0e0, 'no fused multiply-add') }

# ** on Ints and Nums, min and max: an exact Int power, and the ordinary loop
# where the answer is a Rat, a BigInt or a Num that underflowed
{ my $t = 0; for 1 .. 1000 { $t += $_ ** 2 % 7 }; ck($t, 2002, 'an Int power') }
{ my $p = 0; for 1 .. 70 { $p = 2 ** $_ }; ck($p, 1180591620717411303424, 'a power past int64') }
{ my $q = 0; for 1 .. 1 { $q = (-2) ** 63 }; ck($q, -9223372036854775808, 'the smallest int64 as a power') }
{ my $r = 0; for 1 .. 1 { $r = 2 ** -1 }; ck($r, 0.5, 'a negative exponent is a Rat') }
{ my $u = 0; for 0 .. 5 { $u += 0 ** $_ + 1 ** $_ + (-1) ** $_ }; ck($u, 7, 'bases 0, 1 and -1') }
{ my $x = 0e0; for 1 .. 10 { $x += 2e0 ** $_ }; ck($x, 2046e0, 'a Num power') }
{ my $z = 0e0; for 1 .. 4 { $z = $z + $_ ** 0.5e0 }; ck($z, 6.146264369941973e0, 'an Int to a Num power') }
{ my $y = 2e0; my $e = (try { for 1 .. 2 { $y = $y ** -2000 } }) // $!.^name; ck($e.Str, 'X::Numeric::Underflow', 'a Num power that underflows') }
{ my $lo = 1_000_000; my $hi = 0; for 1 .. 10_000 { my $v = ($_ * 7919) % 1_000_003; $lo = $lo min $v; $hi = $hi max $v }
  ck(($lo, $hi), (41, 999877), 'min and max') }

# what a lane may store: an Int into a Num variable is an Int, a comparison is
# a Bool, and `&&` / `||` give an operand
{ my $x = 0e0; for 1 .. 3 { $x = $x + 0.5e0; $x = 1 if $_ == 3 }; ck(($x, $x.^name), (1, 'Int'), 'an Int stored over a Num') }
{ my $b = 0; for 1 .. 3 { $b = $_ > 1 }; ck(($b, $b.^name), (True, 'Bool'), 'a comparison stored') }
{ my $c = 0; for 1 .. 3 { $c = $c + ($_ > 1) }; ck($c, 2, 'a comparison as a number') }
{ my $d = 0; for 1 .. 3 { $d = $_ && 7 }; ck($d, 7, '&& gives an operand') }
{ my $e = 0; for 1 .. 4 { $e++ unless $_ == 2 || $_ == 3 }; ck($e, 2, '|| as a condition') }
{ my $f = 0.5e0; my $g = 0e0; for 1 .. 1000 { $g = $g + $f }; ck($g, 500e0, 'a Num from outside the loop') }

# while / until, last / next
{ my $steps = 0;
  for 1 .. 300 -> $i { my $n = $i; while $n != 1 { if $n %% 2 { $n = $n div 2 } else { $n = 3 * $n + 1 }; $steps++ } }
  ck($steps, 14167, 'Collatz') }
{ my $s = 0; for 1 .. 100 { next if $_ %% 2; last if $_ > 10; $s += $_ }; ck($s, 25, 'next and last') }

say $fails ?? "FAILED $fails" !! "PASS";
