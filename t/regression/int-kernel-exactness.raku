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

# results are values, not containers
sub id($n) { $n }
ck((try { id(1) = 5; 'assigned' }) // 'refused', 'refused', 'a result is not assignable');

# typed signatures: an Int parameter and an Int return keep their checks for
# every argument the kernel does not take
sub tfib(Int $n --> Int) { $n < 2 ?? $n !! tfib($n - 1) + tfib($n - 2) }
ck(tfib(20), 6765, 'Int $n --> Int');
my ($rat, $str) = 2.5, "7";
ck((try { tfib($rat); True }) // False, False, 'a Rat argument to an Int parameter refuses');
ck((try { tfib($str); True }) // False, False, 'a Str argument to an Int parameter refuses');
sub tdd(Int:D $n --> Int:D) { $n * 2 }
ck(tdd(21), 42, 'Int:D');
my $tobj = Int;
ck((try { tdd($tobj); True }) // False, False, 'a type object to an Int:D parameter refuses');
sub tu(Int:U $n) { 5 }
ck(tu(Int), 5, 'Int:U is no kernel\'s');
sub big(Int $n --> Int) { $n * $n }
ck(big(2**40), 1208925819614629174706176, 'Int return past int64');

# a body of statements: `my` locals, loops, assignments, `return` anywhere
sub collatz(Int $n is copy --> Int) {
    my $steps = 0;
    while $n != 1 {
        if $n %% 2 { $n = $n div 2 } else { $n = 3 * $n + 1 }
        $steps++;
    }
    $steps
}
ck(collatz(27), 111, 'while, is copy, a local');
sub firstdiv($n) {
    for 2 .. $n -> $d {
        return $d if $n %% $d;
    }
    0
}
ck((firstdiv(91), firstdiv(97), firstdiv(1)), (7, 97, 0), 'return from inside a for');
sub tri($n) { my $s = 0; for 1 .. $n { $s += $_ }; $s }
ck(tri(100), 5050, 'a for loop in a sub');
sub sgn($n) { if $n > 0 { 1 } elsif $n < 0 { -1 } else { 0 } }
ck((sgn(5), sgn(-5), sgn(0)), (1, -1, 0), 'an if/elsif/else as the value');
sub nested($n) { my $c = 0; for 1 .. $n -> $i { for 1 .. $i -> $j { next if $j %% 2; $c += $j; last if $c > 1000 } }; $c }
ck(nested(30), 1020, 'nested loops with next / last');
sub ovf($n) { my $x = 9223372036854775800; for 1 .. $n { $x++ }; $x }
ck(ovf(20), 9223372036854775820, 'a local that outgrows int64');
sub ro($n) { $n = 5; $n }
ck((try ro(1)) // 'refused', 'refused', 'a read-only parameter refuses');
sub lastif($n) { if $n > 0 { 1 } }
ck((lastif(1), lastif(0)), (1, Empty), 'an if with no else as the value');

# calls with more arguments than the fast forms take
sub s4($a, $b, $c, $d) { $a + 2 * $b + 3 * $c + 4 * $d }
sub c4($n) { s4($n, $n + 1, $n + 2, $n + 3) }
ck(c4(1), 30, 'four arguments');
sub s6($a, $b, $c, $d, $e, $f) { $a - $b + $c - $d + $e - $f }
sub c6($n) { s6($n, 1, 2, 3, 4, 5) + s6(1, 2, 3, 4, 5, $n) }
ck(c6(10), 0, 'six arguments');

# a loop kernel that calls sub kernels
sub sq($x) { $x * $x }
{ my $s = 0; for 1 .. 100 { $s += sq($_) }; ck($s, 338350, 'a loop calling a sub') }
{ my $s = 0; for 1 .. 10 { $s += tfib($_) }; ck($s, 143, 'a loop calling a recursive sub') }

# a routine bound to a variable can be re-bound; a kernel looks again
sub ra($x) { $x + 1 }
sub rb($x) { $x + 100 }
my &rg = &ra;
sub rf($n) { rg($n) * 2 }
ck(rf(1), 4, 'through a variable');
&rg = &rb;
ck(rf(1), 202, 'the variable re-assigned');
{ my $s = 0; for 1 .. 3 { $s += rg($_) }; ck($s, 306, 'a loop through the variable') }
&rg = &ra;
{ my $s = 0; for 1 .. 3 { $s += rg($_) }; ck($s, 9, 'and back') }

# a wrapper on a sub a loop calls
sub w1($x) { $x + 1 }
{ my $s = 0; for 1 .. 3 { $s += w1($_) }; ck($s, 9, 'before the wrap') }
my $wh = &w1.wrap(-> $x { callsame() * 10 });
{ my $s = 0; for 1 .. 3 { $s += w1($_) }; ck($s, 90, 'a loop calling a wrapped sub') }
&w1.unwrap($wh);

# (last: a shadowed `+` arms a program-wide filter, and every kernel after it
# that uses `+` steps aside)
# a lexically shadowed operator
sub g($n) { $n + 1 }
ck(g(1), 2, 'before the shadow');
{
    my &infix:<+> = -> $a, $b { "plus($a,$b)" };
    ck(3 + 4, 'plus(3,4)', 'the shadow answers in its own scope');
    ck(g(3), 4, '…and not inside a sub declared outside it');
}

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
