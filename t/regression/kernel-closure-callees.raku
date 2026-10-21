# Regression: closures of one body share an integer kernel (it lives in the
# CodeInfo since 6d67f0cd), and each must still call the routines ITS scope
# finds.
#
# The kernel checked its callees against the scope of the closure that
# compiled it. A second closure whose `my sub g` was a different routine, or
# wrapped, ran the first closure's `g`:
#
#   sub make { my sub g(Int $n --> Int) { $n + 1 }; (sub (Int $x --> Int) { g($x) + g($x) }, &g) }
#   my ($a, $ag) = make; my ($b, $bg) = make;
#   $a(5); $bg.wrap(-> $n { 1000 }); say $b(5);   # 12, Rakudo 2000
#
# Each entry now resolves the kernel's own calls from the entering closure's
# scope, as a loop kernel resolves its calls from the frame it runs in. (Found
# by the 2026-10-21 review.)
#
# Also: `1 +< $n` with $n = -2**63 recursed until the stack was gone, since
# -$n is $n again; Rakudo answers 1.
#
# Runs clean under Rakudo too.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

sub make() {
    my sub g(Int $n --> Int) { $n + 1 }
    my &r = sub (Int $x --> Int) { g($x) + g($x) };
    return (&r, &g);
}
my ($a, $ag) = make();
my ($b, $bg) = make();
ck $a(5), 12, 'the first closure runs';
for ^3 { $a(5); $b(5) }
ck ($a(5), $b(5)), (12, 12), 'both closures run, once compiled';
$bg.wrap(-> $n { 1000 });
ck $b(5), 2000, 'a wrapper on the second closure\'s g is honoured';
ck $a(5), 12, '…and does not reach the first closure';
$ag.wrap(-> $n { 7 });
ck $a(5), 14, 'a wrapper on the first closure\'s g is honoured too';
ck $b(5), 2000, '…and the second keeps its own';

# a sub at the top level is one routine for every closure that calls it
sub h(Int $n --> Int) { $n * 2 }
sub mk() { sub (Int $x --> Int) { h($x) + 1 } }
my @c = mk() xx 3;
ck @c.map({ $_(4) }).List, (9, 9, 9), 'closures calling one shared sub';

my $min = -9223372036854775808;
ck (1 +< $min, 1 +> $min, -5 +< $min), (1, 1, -5), 'a shift by -2**63 answers, as Rakudo does';

say $fails ?? "FAIL ($fails)" !! 'PASS';
