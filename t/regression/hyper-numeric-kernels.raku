# Regression: a hyper operator over plain machine numbers — `@a »*« @b`,
# `4 «*» @a`, `@a».sqrt` — runs as a native kernel, split over threads when the
# list is long (src/InterpreterRegex.cpp, 2026-10-09), and nothing about the
# answer may show it. Every case below is one the kernel computes, or one where
# it has to decline and let hyperCore answer: an Int that overflows, an Int/Int
# quotient (a Rat), a zero divisor (a Failure), a mixed Int/Num comparison, a
# nested list, a typed or native array, a shadowed operator.
#
# Expectations checked against Rakudo 2026.09 (/opt/homebrew/bin/rakudo). Green
# on both. The second half is Raku++'s own: it runs a program with the kernels
# and again with RAKUPP_NO_KERNELS=1, and asks RAKUPP_KERNEL_TRACE=1 whether
# they ran — so this file cannot pass because the kernels quietly stopped.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

ck ((1, 2.5e0, -3) »+» 1), (2, 3.5e0, -2), 'Int and Num elements, a List in and out';
{
    my @a = 1, 2, 3;
    ck (@a »*» 2), [2, 4, 6], 'an Array in gives an Array out';
    ck (2 «*« @a), [2, 4, 6], 'the number on the left';
    ck (2 «*» @a), [2, 4, 6], '…dwimmy both ways';
    ck (@a »-« (3, 2, 1)), [-2, 0, 2], 'two lists of one length';
}
ck ((9223372036854775807, 1) »+» 1), (9223372036854775808, 2), 'an Int that overflows is a BigInt';
ck ((-9223372036854775808, 3) »*» -1), (9223372036854775808, -3), '…multiplying too';
ck ((1, 2) »/» 2), (0.5, 1.0), 'Int / Int is a Rat';
ck ((3, 6e0) »/» 2), (1.5, 3e0), 'a Num on either side is a Num';
ck ((1e0, 2e0) »/» 0).map({ .so; .^name }).List, <Failure Failure>, 'a zero divisor is a Failure';
ck ((1, 2, 3) »==« (1, 5, 3)), (True, False, True), 'Int comparisons';
ck ((1e0, 2e0) »<« (3e0, 1e0)), (True, False), 'Num comparisons';
ck ((1e0, 2) »<« (3, 1e0)), (True, False), 'mixed Int / Num comparisons';
ck ((9007199254740993, 1) »==« (9007199254740992e0, 1e0)), (True, True), '…as Nums, past 2**53 too';
ck ((-0e0, 0e0) »*» -1).raku, '(0e0, -0e0)', 'the sign of zero';
ck ((NaN, Inf, -Inf) »+» 1).raku, '(NaN, Inf, -Inf)', 'NaN and the infinities';
ck ((NaN, 1e0) »==« (NaN, 1e0)), (False, True), 'NaN is not equal to itself';
ck (((1, 2), 3e0) »+» 1), ((2, 3), 4e0), 'a nested list keeps its shape';
ck ((1..5) »*» 2), (2, 4, 6, 8, 10), 'a Range';
ck ((1, 2, 3) »+» (1..3)), (2, 4, 6), '…on the right';
{
    my Int @t = 1, 2, 3;
    ck (@t »*» 2).^name, 'Array[Int]', 'a typed Array keeps its type';
    my num @n = 1e0, 2e0;
    ck (@n »*» 2e0).List, (2e0, 4e0), 'a native num array';
    my int @i = 5, 6;
    ck (@i »-» 1).List, (4, 5), 'a native int array';
}
ck (<1> »+» 1), 2, 'an allomorph';
ck ((True, 2) »+» 1), (2, 3), 'a Bool';
{
    sub infix:<+>($a, $b) { "plus" }
    ck ((1, 2) »+» 1), ('plus', 'plus'), 'a lexical infix:<+> answers instead';
}
ck (4, 9e0)».sqrt, (2e0, 3e0), '».sqrt';
ck (-4, 4e0)».abs, (4, 4e0), '».abs keeps an Int an Int';
ck (-9223372036854775808,)».abs, (9223372036854775808,), '…and |-2**63| is a BigInt';
ck (0, 0e0)».exp, (1e0, 1e0), '».exp';
ck (0,)».sin, (0e0,), '».sin';
ck (-4,)».sqrt».isNaN, (True,), 'the square root of a negative number is NaN';
{
    my @big = (^100000).map(*.Num);
    my @sq  = @big».sqrt;
    ck @sq.elems, 100000, 'a long list: every element';
    ck @sq[99999], 99999e0.sqrt, '…the last one right';
    ck (@big »*« @big)[12345], 12345e0 * 12345e0, 'two long lists';
    ck ((^100000) »+» 1).sum, 5000050000, 'a long Range';
}

# Raku++ only: the same answers with and without the kernels, and the kernels ran
if $*VM.name ne 'moar' {
    my $prog = q:to/END/;
        srand(7);
        my @edge = 0, 1, -1, 3, -0e0, 1e0, 0.5e0, 1e300, 5e-324, NaN, Inf, -Inf,
                   9223372036854775807, -9223372036854775808, 9007199254740993, 3037000500;
        my @l = |@edge, |(^24).map({ rand < .5 ?? (rand * 2e6 - 1e6).Int !! rand * 200e0 - 100e0 });
        my @r = @l.reverse;
        my @long = (^40000).map({ $_ %% 3 ?? $_ !! $_ * 0.37e0 });
        my $x = 3e0;
        for <+ - * / < <= > >= == !=> -> $op {
            say $op, ' ', EVAL("\@l »{$op}« \@r").raku;
            say $op, ' ', EVAL("\@l »{$op}» \$x").raku;
            say $op, ' ', EVAL("\$x «{$op}« \@l").raku;
        }
        for <sqrt abs exp sin cos tan asin acos atan sinh cosh tanh> -> $m {
            say $m, ' ', EVAL("\@l».$m").raku;
        }
        say 'long * ', (@long »*« @long).raku;
        say 'long sqrt ', @long».sqrt.raku;
        END
    my %env = %*ENV;
    %env<RAKUPP_KERNEL_TRACE> = 1;
    %env<RAKUPP_NO_KERNELS RAKUPP_GIL RAKUPP_PARALLEL RAKUPP_HYPER_THREADS>:delete;
    my $with = run($*EXECUTABLE, '-e', $prog, :out, :err, :env(%env));
    my $out-with = $with.out.slurp(:close);
    my $trace = $with.err.slurp(:close);
    %env<RAKUPP_NO_KERNELS> = 1;
    my $without = run($*EXECUTABLE, '-e', $prog, :out, :err, :env(%env));
    my $out-without = $without.out.slurp(:close);
    $without.err.slurp(:close);
    ck $out-with.lines.elems, 44, 'the program ran';
    ck $out-with eq $out-without, True, 'the same answers with RAKUPP_NO_KERNELS=1';
    ck so($trace ~~ /'kernel: hyper infix:<*> over 40 element(s) on 1 thread'/), True, 'the infix kernel ran';
    ck so($trace ~~ /'kernel: hyper .sin over 40 element(s) on 1 thread'/), True, 'the method kernel ran';
    ck so($trace ~~ /'kernel: hyper infix:<+> over 40 element(s) declined'/), True, '…and declined an overflow';
    my $ways = $trace ~~ /'kernel: hyper infix:<*> over 40000 element(s) on ' (\d+) ' thread'/;
    ck so($ways), True, 'a long list ran as a kernel';
    ck +($ways[0] // 0) > 1, $*KERNEL.cpu-cores > 1, '…on more than one thread when there are cores for it';
}

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
