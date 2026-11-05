# Issue #47's Math::NumberTheory driver: eleven of the distribution's
# functions over 1 .. 3000, each folded into a checksum so that both engines
# can be compared line by line.
#
# V6-PLAN P0 and its second number ("issue #47 at parity"). The 2026-09-26
# survey read this workload at 2.35× Rakudo's time, most of it in multi
# dispatch: euler-phi alone makes ~1.1M calls to the module's own
# `multi infix:<gcd>`, whose candidates all decline an Int pair before the
# builtin answers.
#
# It lives below tools/bench/ because it needs Math::NumberTheory installed
# (`rakupp install Math::NumberTheory`), which t/run.raku's coverage check and
# the CI legs do not have. run-bench runs it when the module loads and says
# so when it does not. carmichael-lambda is left out, so the kernel stays the
# one first measured: when it was written, rakupp's `[lcm]` skipped the
# module's Rat candidate, which Rakudo uses (fixed the same day,
# t/regression/metaop-core-operands-user-infix.raku).
#
#     rakupp tools/bench/issue47/numbertheory.raku [N] [function ...]
use Math::NumberTheory;

my $N = +(@*ARGS[0] // 3000);
my @only = @*ARGS[1..*];
my %fold =
    euler-phi        => -> $n { euler-phi($n) },
    divisors         => -> $n { divisors($n).sum },
    divisor-sigma    => -> $n { divisor-sigma($n) },
    factor-integer   => -> $n { factor-integer($n).map({ .head * .tail }).sum },
    is-prime         => -> $n { is-prime($n) ?? 1 !! 0 },
    moebius-mu       => -> $n { moebius-mu($n) },
    liouville-lambda => -> $n { liouville-lambda($n) },
    prime-omega      => -> $n { prime-omega($n) },
    prime-nu         => -> $n { prime-nu($n) },
    is-square-free   => -> $n { is-square-free($n) ?? 1 !! 0 },
    integer-digits   => -> $n { integer-digits($n).sum };
for %fold.keys.sort -> $name {
    next if @only && $name !(elem) @only;
    my &f = %fold{$name};
    my $t = 0;
    $t += f($_) for 1 .. $N;
    say "$name $t";
}
