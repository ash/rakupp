# Interpreter performance baseline — the release gate reads this.
#
#   baseline : the LAST RELEASE's measured time. `perf-guard --check` fails if the
#              current build is slower than this by more than `tolerance-pct`.
#              Re-record it (`--record`) as part of cutting a release, and only
#              when the numbers are ones we are happy to ship.
#   best     : the fastest we have ever measured for this kernel, and when. This
#              is NOT the gate — it is the standing debt, so that a regression
#              which once got waved through does not quietly become the new normal.
#
# Absolute times are machine-specific: the gate compares a build against another
# build measured on the SAME machine, which is why both numbers carry the machine
# they were taken on. Re-record on a new machine before trusting a failure.
#
# Regenerate with:  rakupp tools/perf-guard.raku --record
{
    'machine'       => 'macOS Darwin 24.6, Apple Silicon, idle desktop',
    'tolerance-pct' => 5,     # a build may be this much slower before the gate fails
    'kernels' => {
        # kernel  => { baseline-ms, best-ms, best-version, best-date }
        'fib'     => { 'baseline' => 827.9, 'best' => 816.3, 'best-version' => '1.0.0', 'best-date' => '2026-07-22' },
        'asg'     => { 'baseline' => 501.5, 'best' => 501.5, 'best-version' => 'unreleased', 'best-date' => '2026-07-29' },
        'loopsum' => { 'baseline' => 200.9, 'best' => 194.4, 'best-version' => '1.0.0', 'best-date' => '2026-07-22' },
        'hash'    => { 'baseline' => 38.7, 'best' => 38.7, 'best-version' => 'unreleased', 'best-date' => '2026-07-29' },
    },
    # Recorded 2026-07-29 at 625/1462 Roast files. The baseline is deliberately
    # the CURRENT (slower) state rather than `best`: a permanently-red gate gets
    # ignored, so the gate guards against NEW regressions while the `best` column
    # keeps the ~11.6% fib debt since 1.0.0 visible until it is bisected.
    'recorded' => '2026-07-29',
}
