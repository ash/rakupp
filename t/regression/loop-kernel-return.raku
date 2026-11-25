# Regression: a loop with a `return` in it runs as a kernel.
#
# `return` declined a loop kernel outright ("a `return`"), so a search loop
# that leaves its routine from the middle ran interpreted — a Kaprekar-number
# search spent three quarters of its time there. The kernel now ends at the
# `return` as at a `last` of every loop it is in, writes its variables back,
# and the statement itself then runs the ordinary way: its value may be
# anything (a list, a call, a closure's answer), but a `return` that names a
# loop variable, a `my` of the body or the loop's topic still declines.
#
# Expectations checked against Rakudo 2026.09. Green on both. The second half
# is Raku++'s own: the same answers with RAKUPP_NO_KERNELS=1, and the trace
# says the loops ran as kernels.
#
# Contract: exit 0 + last line PASS.
my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}
my $prog = q:to/END/;
    sub a($n) { my $i = 0; my $s = 0; while $i < 100 { $s += $i; return $s, $i if $s > $n; $i++ }; 'none' }
    say a(10); say a(100000); say a(-1);
    sub b($n) { my $s = 0; loop (my $i = 0; $i < 50; $i++) { $s += $i * $i; return $s if $s > $n }; -1 }
    say b(100); say b(10**9);
    sub c($n) { my $s = 0; for ^100 { $s += $_; return "$s/$_" if $s > $n }; -1 }
    say c(50); say c(10**6);
    sub d($n) { my $s = 0; for ^100 -> $i { $s += $i; return "i$i" if $s > $n }; -1 }
    say d(50);
    sub e($n) { my $s = 0; my $u = 7; for ^100 -> $i { $s += $i; $u += 1; return "$s $u" if $s > $n }; -1 }
    say e(50); say e(10**6);
    sub f($n) { my $t = 0; my $i = 0; while $i < 10 { my $j = 0; while $j < 10 { $t += 1; return "$i:$t" if $t == $n; $j++ }; $i++ }; "end $t" }
    say f(37); say f(1000);
    sub g($n) { my $x = 1; repeat { $x *= 3; return $x.Str ~ '!' if $x > $n } while $x < 1000; $x }
    say g(50); say g(10**9);
    sub h($n) { my $x = 1; my $y = 0; while True { $x *= 2; $y++; if $x > $n { return ($y, $x) } elsif $y > 70 { return 'far' } } }
    say h(1000); say h(10**30);
    sub i($n) { my @a = 1, 2, 3; my $k = 0; while $k < 5 { @a[$k % 3] += $k; return @a.join('-') if @a[0] > $n; $k++ }; @a.sum }
    say i(3); say i(100);
    sub j($n) { my $s = 0; my $i = 0; while $i < 10 { $i++; return if $s > $n; $s += $i }; $s }
    say j(5).raku; say j(100).raku;
    sub k($n) { my $s = 0; my $i = 0; until $i == 20 { $i++; $s += $i; return { $s + $i }() if $s > $n }; 0 }
    say k(30);
    my $z = 0; my $w = 0; sub l() { while $w < 10 { $w++; $z += $w; return $z if $z > 20 }; 0 }
    say l(); say "$z $w"; say l(); say "$z $w";
    sub m($n) { my $q = 9223372036854775800; my $i = 0; while $i < 20 { $q += 1; $i++; return $q if $i == $n }; $q }
    say m(3); say m(15);
    sub kaprekar(Int $n, Int :$base = 10) {
        my Int $hi = $n ** 2;
        my Int $lo = 0;
        loop (my Int $s = 1; $hi; $s *= $base) {
            $lo += ($hi % $base) * $s;
            $hi div= $base;
            return $hi, $lo if $lo + $hi == $n and $lo;
        }
        ();
    }
    say (^3000).grep({ kaprekar($_) });
    say kaprekar(297);
    END
my @want = '(15 5)', 'none', '(0 0)', '140', '-1', '55/10', '-1', 'i10', '55 18', '-1', '3:37', 'end 100', '81!', '2187',
           '(10 1024)', 'far', '4-3-5', '16', 'Nil', '55', '44', '21', '21 6', '28', '28 7',
           '9223372036854775803', '9223372036854775815', '(1 9 45 55 99 297 703 999 2223 2728)', '(88 209)';
if $*VM.name eq 'moar' {
    ck EVAL("my \@o; my sub say(\$x) \{ \@o.push: \$x.gist }; $prog; \@o"), @want.Array, 'every answer';
}
else {
    my %env = %*ENV;
    %env<RAKUPP_KERNEL_TRACE> = 1;
    %env<RAKUPP_NO_KERNELS>:delete;
    my $with = run($*EXECUTABLE, '-e', $prog, :out, :err, :env(%env));
    my $out-with = $with.out.slurp(:close);
    my $trace = $with.err.slurp(:close);
    %env<RAKUPP_NO_KERNELS> = 1;
    my $without = run($*EXECUTABLE, '-e', $prog, :out, :err, :env(%env));
    my $out-without = $without.out.slurp(:close);
    $without.err.slurp(:close);
    ck $out-with.lines.Array, @want.Array, 'every answer, with kernels';
    ck $out-without.lines.Array, @want.Array, 'every answer, with RAKUPP_NO_KERNELS=1';
    ck so($trace ~~ /'declined: a `return`' \n/), False, 'no loop declines a `return` as such';
    for 1, 3, 11, 13, 15, 17, 19, 21, 23, 25, 30 -> $line {
        ck so($trace ~~ /'kernel: loop at line ' $line ' compiled'/), True, "the loop at line $line is a kernel";
    }
    ck so($trace ~~ /'loop at line 5 declined: a `return` under a topic of the loop\'s own'/), True, 'a `return` under the loop\'s topic declines';
    ck so($trace ~~ /'loop at line 7 declined: a `return` naming a variable of the loop\'s own'/), True, 'a `return` of the loop variable declines';
}

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
