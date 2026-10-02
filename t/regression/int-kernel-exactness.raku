# Regression: a plain sub of closed integer arithmetic runs as an integer
# kernel (src/IntKernel.cpp), and nothing about the answer may show it. Every
# case here is one where the kernel has to step aside, or bail half-way and
# let the ordinary call path redo the call: a result that outgrows int64, an
# argument that is not a plain Int, a zero divisor, recursion past the
# generic limit, a wrapped routine, a shadowed operator.
#
# Expectations checked against Rakudo 2026.09 via /opt/homebrew/bin/rakudo. Green on both,
# and identical with RAKUPP_NO_KERNELS=1.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

sub fib($n) { $n < 2 ?? $n !! fib($n - 1) + fib($n - 2) }
sub ack($m, $n) { $m == 0 ?? $n + 1 !! $n == 0 ?? ack($m - 1, 1) !! ack($m - 1, ack($m, $n - 1)) }
sub gcd2($a, $b) { $b == 0 ?? $a !! gcd2($b, $a % $b) }
ck(fib(20), 6765, 'fib');
ck(ack(2, 3), 9, 'ackermann');
ck(gcd2(1071, 462), 21, 'gcd through %');

sub md($a, $b) { $a div $b }
sub mm($a, $b) { $a % $b }
sub dd($a, $b) { $a %% $b }
ck((md(7, 3), md(-7, 3), md(7, -3), md(-7, -3)), (2, -3, -3, 2), 'div floors');
ck((mm(7, 3), mm(-7, 3), mm(7, -3), mm(-7, -3)), (1, 2, -2, -1), '% takes the divisor\'s sign');
sub ddi($a, $b) { $a %% $b ?? 1 !! 0 }
ck((ddi(6, 3), ddi(7, 3), ddi(5, -1)), (1, 0, 1), '%% in a condition');

sub st($n) {
    return 0 if $n <= 1;
    if $n %% 2 { return 1 + st($n div 2) }
    elsif $n == 3 { 7 }
    else { 1 + st(3 * $n + 1) }
}
ck(st(27), 111, 'if / elsif / else / return modifier');
sub un($n) { unless $n > 0 { return -1 }; $n }
ck((un(5), un(0)), (5, -1), 'unless');

# int64 overflow: the answer is the BigInt the ordinary path computes
sub p2($n) { $n == 0 ?? 1 !! 2 * p2($n - 1) }
ck(p2(62), 4611686018427387904, '2**62 fits');
ck(p2(64), 18446744073709551616, '2**64 does not: BigInt');
sub fact($n) { $n <= 1 ?? 1 !! $n * fact($n - 1) }
ck(fact(25), 15511210043330985984000000, 'factorial past int64');
sub ng($a) { -$a }
ck(ng(-9223372036854775808), 9223372036854775808, 'negating the smallest int64');

# arguments that are not plain Ints keep their own semantics
ck(fib(10.0), 55.0, 'a Rat argument stays a Rat');
ck(fib(<10>), 55, 'an IntStr argument');
ck(fib(3.5e0), 3.5e0, 'a Num argument');
sub inc($n) { $n + 1 }
ck(inc(2**65), 36893488147419103233, 'a BigInt argument');
ck(inc(True), 2, 'a Bool argument');

# divisors of zero: the ordinary path's error or Failure
ck((try md(1, 0)) // $!.^name, 'X::Numeric::DivideByZero', 'div by zero throws');
ck(mm(1, 0) ~~ Failure, True, '% by zero is a Failure');

# recursion: as deep as the ordinary path goes (a kernel bails at its limit)
sub d($n) { $n == 0 ?? 0 !! 1 + d($n - 1) }
ck(d(50000), 50000, '50,000 deep');

# a wrapper runs: the kernel steps aside for a wrapped routine
my $h = &inc.wrap(-> $n { callsame() * 10 });
ck(inc(2), 30, 'wrapped');
&inc.unwrap($h);
ck(inc(2), 3, 'unwrapped');

# closures rebuilt from one body share its kernel; a wrapper on one copy
# stays on that copy
my @cl = (^3).map: { sub ($x) { $x * 2 + 1 } };
ck(@cl[0](5), 11, 'a closure copy');
@cl[1].wrap(-> $x { callsame() * 100 });
ck(@cl.map({ .(5) }).List, (11, 1100, 11), 'a wrapper on one closure copy only');

# a lexically shadowed operator
sub g($n) { $n + 1 }
ck(g(1), 2, 'before the shadow');
{
    my &infix:<+> = -> $a, $b { "plus($a,$b)" };
    ck(3 + 4, 'plus(3,4)', 'the shadow answers in its own scope');
    ck(g(3), 4, '…and not inside a sub declared outside it');
}

# results are values, not containers
sub id($n) { $n }
ck((try { id(1) = 5; 'assigned' }) // 'refused', 'refused', 'a result is not assignable');

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
