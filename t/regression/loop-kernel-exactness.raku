# Regression: a `for` over an integer Range whose body is plain Int / Str
# arithmetic runs as a loop kernel (src/IntKernel.cpp), and nothing about the
# answer may show it. Most cases here are ones where the kernel has to step
# aside, or bail half-way and let the ordinary loop run again from the start:
# a sum that outgrows int64, a variable that holds something other than a
# plain Int or Str, a typed, constrained, readonly or bound container, two
# names for one container, a join that has to be renormalized, `last` and
# `next`, nesting, threads.
#
# Expectations checked against Rakudo 2026.09 via /opt/homebrew/bin/rakudo. Green on both,
# and identical with RAKUPP_NO_KERNELS=1.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# the shapes
{ my $t = 0; for 1 .. 1000 { $t += $_ }; ck($t, 500500, 'block form, topic') }
{ my $t = 0; for 1 .. 1000 -> $i { $t += $i }; ck($t, 500500, 'pointy loop variable') }
{ my $t = 0; $t += $_ for 1 .. 1000; ck($t, 500500, 'statement modifier') }
{ my $t = 0; $t += $_ for 1 ^.. 10; ck($t, 54, '^..') }
{ my $t = 0; $t += $_ for 1 ..^ 10; ck($t, 45, '..^') }
{ my $t = 0; $t += $_ for ^10; ck($t, 45, '^N') }
{ my $t = 7; for 5 .. 1 { $t++ }; ck($t, 7, 'an empty range') }
{ my $t = 0; my $n = -3; for $n .. 3 { $t += $_ * $_ }; ck($t, 28, 'negative bounds') }
{ my $big = 2 ** 130; my $c = 0; ++$c for $big .. $big + 2; ck($c, 3, 'a range with BigInt ends') }
{ my $s = 0; for 1 .. * { $s += $_; last if $_ >= 10 }; ck($s, 55, 'an endless range ended by last') }
{ my $s = 0; for 1 .. Inf { $s += $_; last if $_ >= 10 }; ck($s, 55, 'a range to Inf') }
{ my $c = 0; for 9223372036854775805 .. 9223372036854775807 { $c++ }; ck($c, 3, 'a range up to the largest int64') }

# arithmetic past int64: the BigInt the ordinary loop computes
{ my $t = 0; $t += 2**62 for 1 .. 4; ck($t, 18446744073709551616, 'a sum that outgrows int64') }
{ my $x = 9223372036854775800; $x++ for 1 .. 20; ck($x, 9223372036854775820, '++ past int64') }
{ my $x = -9223372036854775800; $x-- for 1 .. 20; ck($x, -9223372036854775820, '-- past int64') }
{ my $p = 1; $p *= 3 for 1 .. 50; ck($p, 717897987691852588770249, '*= past int64') }
{ my $s = 0; my $e = (try { for 1 .. 5 { $s += 10 div ($_ - 3) } }) // $!.^name;
  ck(($s, $e.Str), (-15, 'X::Numeric::DivideByZero'), 'a zero divisor mid-loop: the ordinary loop throws where it would') }

# variables that are not a plain Int / Str keep their own semantics
{ my $t = 0.5; $t += $_ for 1 .. 10; ck($t, 55.5, 'a Rat accumulator') }
{ my $t = 0e0; $t += $_ for 1 .. 10; ck($t, 55e0, 'a Num accumulator') }
{ my $t; $t += $_ for 1 .. 10; ck($t, 55, 'an undefined accumulator') }
{ my $t = <5>; $t += $_ for 1 .. 3; ck($t, 11, 'an IntStr accumulator') }
{ my $t = 2**70; $t += $_ for 1 .. 3; ck($t, 1180591620717411303430, 'a BigInt accumulator') }
{ my $t = True; $t += $_ for 1 .. 3; ck($t, 7, 'a Bool accumulator') }
{ my Str $s; $s ~= "x" for 1 .. 3; ck($s, 'xxx', 'a Str type object') }
{ my $s = ""; $s ~= $_ for 1 .. 5; ck($s, '12345', 'an Int appended to a Str') }
{ my $x = 1; for 1 .. 3 { $x = "s$_" }; ck($x, 's3', 'a variable that changes type') }

# containers a plain store would not honour
{ my Int $t = 0; $t += $_ for 1 .. 10; ck($t, 55, 'a typed container') }
{ subset Small of Int where * < 10;
  my Small $s = 0;
  my $e = (try { for 1 .. 20 { $s++ } }) // $!.^name;
  ck(($s, $e.Str), (9, 'X::TypeCheck::Assignment'), 'a subset-typed container refuses mid-loop') }
{ my $w where * < 5 = 0;
  my $e = (try { for 1 .. 20 { $w++ } }) // $!.^name;
  ck(($w, $e.Str), (4, 'X::TypeCheck::Assignment'), 'a `where` container refuses mid-loop') }
{ sub ro($x) { for 1 .. 3 { $x++ }; $x }
  ck((try { ro(1); True }) // False, False, 'a readonly parameter refuses') }
{ ck((try { for 1 .. 3 { $_ = 5 }; True }) // False, False, 'the loop variable refuses') }
{ ck((try { for 1 .. 3 -> $o { for 1 .. 2 { $o++ } }; True }) // False, False, 'an enclosing loop variable refuses') }
{ my $d is default(5) = 0; $d++ for 1 .. 3; ck($d, 3, '`is default`') }
{ my int $n = 0; $n += $_ for 1 .. 10; ck($n, 55, 'a native int') }
{ my int $w = 9223372036854775807; $w++ for 1 .. 1; ck($w, -9223372036854775808, 'a native int wraps') }
{ my str $ns = "a"; $ns ~= "b" for 1 .. 3; ck($ns, 'abbb', 'a native str') }

# one container, two names
{ my $a = 0; my $b := $a; for 1 .. 5 { $a++; $b++ }; ck(($a, $b), (10, 10), 'a bound alias') }
{ my $c = 0; my &get = { $c }; $c++ for 1 .. 5; ck(get(), 5, 'a closure sees the write-back') }

# where the variable lives
{ our $pv = 0; $pv += $_ for 1 .. 10; ck($pv, 55, 'an `our` variable'); ck($OUR::pv, 55, '…seen through OUR') }
{ sub stf() { state $n = 0; $n += $_ for 1 .. 3; $n }
  ck((stf(), stf()), (6, 12), 'a `state` variable') }
{ our $g = 0; sub tmp() { temp $g = 1; $g++ for 1 .. 3; $g }
  ck((tmp(), $g), (4, 0), 'a `temp` variable is restored') }
{ my $i = 100; my $s = 0; for 1 .. 3 -> $i { $s += $i }; ck(($i, $s), (100, 6), 'a loop variable shadows an outer one') }
{ $_ = 'keep'; my $s = 0; $s += $_ for 1 .. 3; ck(($_, $s), ('keep', 6), 'the topic is restored after a modifier loop') }

# strings
{ my $a = "delta"; my $b = "delt" ~ "a"; my $c = 0;
  for 1 .. 1000 { $c++ if $a eq $b; $c-- if $a lt $b; }
  ck($c, 1000, 'eq / lt') }
{ my $x = "abc"; my $n = 0; for 1 .. 1 { $n++ if $x lt "abd"; $n++ if $x le "abc"; $n++ if $x gt "abb"; $n++ unless $x ge "abd"; $n++ if $x ne "abd" }
  ck($n, 5, 'the six comparisons') }
{ my $u = "é"; my $z = "z"; my $n = 0; for 1 .. 3 { $n++ if $u gt $z }; ck($n, 3, 'code-point order past ASCII') }
{ my $s = ""; $s ~= "x" for 1 .. 5000; ck($s.chars, 5000, '~= builds a string') }
{ my $s = "e"; for 1 .. 1 { $s ~= "\x[301]" }; ck(($s, $s.chars, $s.codes), ("\x[E9]", 1, 1), '~= renormalizes a combining mark') }
{ my $s = "a"; for 1 .. 2 { $s = $s ~ "\x[308]" }; ck(($s.chars, $s.codes), (1, 2), '~ renormalizes a combining mark') }
{ my $s = "a"; for 1 .. 3 { $s ~= $s }; ck($s, 'a' x 8, 'a string appended to itself') }
{ my $a = "x"; my $b = ""; for 1 .. 3 { $b = $a; $a ~= "y" }; ck(($a, $b), ('xyyy', 'xyy'), 'a copy is a copy') }
{ my $a = "a"; my $b = "b"; my $s = ""; for 1 .. 2 { $s = $s ~ $a ~ $b ~ "-" }; ck($s, 'ab-ab-', 'a chain of ~') }
{ my $s = ""; for 1 .. 3 { my $t = "<"; $t ~= "#"; $s ~= $t }; ck($s, '<#<#<#', 'a `my` Str local') }
{ my $s = "ab"; my $n = 0; for 1 .. 4 { if $s eq "ab" { $s = "cd"; $n++ } elsif $s eq "cd" { $s = "ab"; $n += 10 } }
  ck(($s, $n), ('ab', 22), 'if / elsif over strings') }

# control flow
{ my $s = 0; for 1 .. 100 { next if $_ %% 2; last if $_ > 10; $s += $_ }; ck($s, 25, 'next and last') }
{ my $s = 0; for 1 .. 10 -> $i { for 1 .. $i -> $j { $s += $i * $j } }; ck($s, 1705, 'nested for') }
{ my $s = 0; for 1 .. 5 -> $i { for 1 .. 5 -> $j { last if $j > $i; $s++ } }; ck($s, 15, 'last ends the inner loop only') }
{ my $c = 0; for 1 .. 5 { my $k = $_; while $k > 0 { $k--; $c++ } }; ck($c, 15, 'while inside for') }
{ my $c = 0; for 1 .. 5 { my $k = 0; until $k >= $_ { $k++; next if $k == 2; $c++ } }; ck($c, 11, 'until with next') }
{ my $s = 0; for 1 .. 10 { my $sq = $_ * $_; $s += $sq unless $sq > 50 }; ck($s, 140, '`my` local and unless') }
{ my $s = 0; for 1 .. 10 { $s += $_ > 5 ?? 2 !! 1 }; ck($s, 15, '?? !!') }
{ my $s = 0; for 1 .. 20 { if $_ %% 3 && !($_ %% 2) { $s += $_ } elsif $_ == 4 || $_ == 8 { $s -= 1 } else { } }; ck($s, 25, '&& || !') }
{ my $s = 0; L1: for 1 .. 3 { for 1 .. 3 { next L1 if $_ == 2; $s++ } }; ck($s, 3, 'a labelled next') }
{ my $s = 0; for 1 .. 10 { $s = $s - $_ }; ck($s, -55, 'a plain assignment') }
{ my $s = 0; my $m = 0; for 1 .. 10 { $m = $_ div 3; $s += $_ % 3 + $m }; ck($s, 25, 'div and %') }

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
{ my $n = 0; for 1 .. 4 { given $_ { when 2 { given $_ + 1 { when 3 { $n += 100 } }; $n += 7 }; default { $n++ } } }
  ck($n, 110, 'nested `given`') }

# while / until / loop at the top of a kernel
{ my $t = 0; my $i = 0; while $i < 1000 { $i++; $t += $i }; ck(($t, $i), (500500, 1000), 'a top-level while') }
{ my $i = 10; until $i <= 0 { $i -= 3 }; ck($i, -2, 'a top-level until') }
{ my $t = 0; loop (my $i = 1; $i <= 100; $i++) { next if $i %% 2; $t += $i }; ck(($t, $i), (2500, 101), 'a C-style loop: `next` still steps, and its `my` outlives it') }
{ my $t = 0; my $j; loop ($j = 0; $j < 10; $j += 3) { $t += $j }; ck(($t, $j), (18, 12), 'a C-style loop over an existing variable') }
{ my $n = 0; loop { $n++; last if $n >= 7 }; ck($n, 7, 'a bare `loop` ended by `last`') }
{ my $x = 9223372036854775800; my $i = 0; while $i < 20 { $i++; $x++ }; ck($x, 9223372036854775820, 'a while that outgrows int64') }
{ my $c = 0; while (my $w = $c) < 5 { $c++ }; ck($c, 5, 'a `my` in a while condition') }

# Ints as text, strings in ?? !!, .chars
{ my $s = ""; for 1 .. 12 { $s ~= $_ % 10 }; ck($s, '123456789012', 'an Int appended to a Str') }
{ my $s = ""; for 1 .. 3 { $s = $s ~ $_ ~ "," }; ck($s, '1,2,3,', 'an Int in a ~ chain') }
{ my $s = ""; for 1 .. 3 -> $i { $s = "n$i:$s" }; ck($s, 'n3:n2:n1:', 'interpolation of an Int and a Str') }
{ my $s = "e"; my $t = ""; for 1 .. 1 { $t = "$s\x[301]" }; ck(($t.chars, $t.codes), (1, 1), 'interpolation renormalizes') }
{ my $k = "ab"; my $n = 0; for 1 .. 5 { $n++ if $k lt "b"; $k = $k eq "ab" ?? "ba" !! "ab" }; ck(($k, $n), ('ba', 3), 'Strs in ?? !!') }
{ my $t = 0; my $s = "héllo"; for 1 .. 4 { $t += $s.chars; $t += $_.chars }; ck($t, 24, '.chars of a Str and an Int') }
{ my $t = 0; my $s = "e\x[301]a"; for 1 .. 2 { $t += $s.chars }; ck($t, 4, '.chars counts graphemes') }
{ my $n = 0; my $s = ""; for 1 .. 10 { $s ~= "ab"; $n = $s.chars if $_ == 7 }; ck(($n, $s.chars), (14, 20), '.chars of a growing Str') }
{ my $t = 0; for 1 .. 3 { $t += 5 eq "5" ?? 1 !! 0 }; ck($t, 3, 'an Int compared as a Str') }

# a loop whose kernel was built for other types, entered again
{ sub acc($init) { my $a = $init; for 1 .. 3 { $a ~= "x" }; $a }
  ck((acc("s"), acc(5), acc("t")), ('sxxx', '5xxx', 'txxx'), 'one loop, two types') }
{ sub sum($init) { my $a = $init; for 1 .. 3 { $a += $_ }; $a }
  ck((sum(1), sum(1.5), sum(2**64), sum(2)), (7, 7.5, 18446744073709551622, 8), 'one loop, four types') }
{ my $tot = 0; for 1 .. 300 -> $o { my $s = 0; my $n = $o %% 7 ?? 0 !! 2; $s += $_ for 1 .. $n; $tot += $s }
  ck($tot, 774, 'a kernel entered many times') }

# a loop that calls, or reaches the world, is not a kernel's
{ sub f($x) { $x * 2 }; my $s = 0; $s += f($_) for 1 .. 10; ck($s, 110, 'a call in the body') }
{ my @a; for 1 .. 3 { @a.push: $_ }; ck(@a, [1, 2, 3], 'a method call in the body') }
{ my @a = 0 xx 3; for ^3 { @a[$_]++ }; ck(@a, [1, 1, 1], 'an element store') }
{ my $s = 0; my $p = start { my $q = 0; $q += $_ for 1 .. 1000; $q };
  $s += $_ for 1 .. 1000; ck(($s, await $p), (500500, 500500), 'with another thread live') }
{ my &infix:<+> = -> $a, $b { $a * $b };
  my $s = 1; $s = $s + $_ for 1 .. 5; ck($s, 120, 'a lexically shadowed infix:<+>') }

say $fails ?? "FAILED $fails" !! "all ok";
