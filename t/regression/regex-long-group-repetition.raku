# Regression: a quantified group the engine has no loop for — here
# `[.*? 'Hello' \s* '=' \s* $<result>=(\d+)] *` — spent C++ stack on every
# repetition, so 100,000 records overflowed even the main thread's 1 GiB
# and the process died (SIGSEGV on a Raspberry Pi, SIGBUS on macOS). Deep
# in a run the engine now follows the first match of each repetition in a
# loop — the recursion's own first attempt — and falls back to the
# recursion only if what follows fails. Past that, the fixed 8M-step
# budget against catastrophic backtracking made 400,000 such records come
# back as no match; it now grows with the subject (issue #250).
# Every line answers the same under Rakudo 2026.09.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

my $n = 500_000;
my $str = 'a b c Hello=10 a b c' x $n;
my $m = $str ~~ m/[.*? 'Hello' \s* '=' \s* $<result>=(\d+)] */;
check so($m), True, 'the report\'s match, at 500,000 records';
check $m<result>.elems, $n, 'every record captured';
check $m<result>[*-1].from, 20 * ($n - 1) + 12, 'the last one where it is';
check ~$m<result>[$n div 2], '10', 'one in the middle';

# the loop gives the recursion's answer, captures and all, where what
# follows the run needs some of it back
my $m2 = ('a' x 400) ~~ /^ ( a )* aa $/;
check ($m2[0].elems, $m2.chars), (398, 400), 'giving back two of a long run';
my $m3 = ('x,' x 500 ~ 'x') ~~ /^ (x)+ % ',' $/;
check $m3[0].elems, 501, 'with a separator';
my $m4 = ('kv;' x 500) ~~ /^ [ $<k>=(k) $<v>=(v) ';' ]* $/;
check ($m4<k>.elems, $m4<v>.elems), (500, 500), 'named captures';
my $m5 = ('ab' x 400 ~ 'c') ~~ /^ [ a | ab | b ]* c $/;
check $m5.chars, 801, 'alternation inside';
my $m6 = ('ab' x 400) ~~ /^ [ (a) b ]* ab $/;
check ($m6[0].elems, $m6.chars), (399, 800), 'the last repetition given back';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
