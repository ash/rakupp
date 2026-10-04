# Regression: a call's value discarded at statement level is SUNK under --exe,
# as the interpreter sinks it. The generated code dropped it: an unhandled
# Failure never detonated (`try { "abc".Int; … }` went on as if it had
# worked), and a lazy `.map` or a `gather` written as a statement never ran
# its block. t/exe/run.raku compiles this file and compares it with the
# interpreter.
#
# Every expectation below was checked against Rakudo 2026.09.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my $r = try { "abc".Int; "answered" };
ck($r // 'died', 'died', 'a discarded Failure from a method detonates');
sub g { "abc".Int }
$r = try { g(); "answered" };
ck($r // 'died', 'died', 'and one from a sub');
my @lazy = lazy 1..3;
$r = try { @lazy.sum; "answered" };
ck($r // 'died', 'died', 'and a lazy list refusing .sum');

my $n = 0;
(^Inf).map({ last if $_ > 4; $n++ });
ck($n, 5, 'a sunk lazy .map runs until last');
my $m = 0;
(gather { $m++; take 1 });
ck($m, 1, 'a sunk gather runs its block');

my $ok = "abc".Int.defined;
ck($ok, False, 'a Failure that is used is not sunk');
ck((try { "12".Int; "answered" }), 'answered', 'and a good value sinks quietly');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
