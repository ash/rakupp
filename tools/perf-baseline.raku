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
        'fib'     => { 'baseline' => 759.6, 'best' => 759.6, 'best-version' => '1.5.1', 'best-date' => '2026-07-29' },
        'asg'     => { 'baseline' => 512.2, 'best' => 501.5, 'best-version' => '1.5.1', 'best-date' => '2026-07-29' },
        'loopsum' => { 'baseline' => 201.6, 'best' => 194.4, 'best-version' => '1.0.0', 'best-date' => '2026-07-22' },
        'hash'    => { 'baseline' => 39.1, 'best' => 38.7, 'best-version' => '1.5.1', 'best-date' => '2026-07-29' },
        # The three string/call kernels were added 2026-08-09 and have no release
        # history, so their FIRST baseline is the number measured the day they
        # landed rather than the last release's. That is deliberate: v3.0.1
        # measured strscan at 2883.0, strpass 184.3 and subcall 375.3 on this
        # machine, and recording those would have let a 13x regression back in
        # through the very gate added to stop it. The v3.0.1 figures are kept
        # here as the record of what the kernels were introduced to catch.
        #   strscan  2883.0 -> 221.6   (.substr stopped copying and rescanning)
        #   strpass   184.3 -> 153.8
        #   subcall   375.3 -> 281.1   (binder fast path, cached signature facts)
        'strscan' => { 'baseline' => 221.6, 'best' => 221.6, 'best-version' => 'unreleased', 'best-date' => '2026-08-09' },
        'strpass' => { 'baseline' => 153.8, 'best' => 153.8, 'best-version' => 'unreleased', 'best-date' => '2026-08-09' },
        'subcall' => { 'baseline' => 281.1, 'best' => 281.1, 'best-version' => 'unreleased', 'best-date' => '2026-08-09' },
    },
    # Recorded 2026-07-29 at 625/1462 Roast files. The baseline is deliberately
    # the CURRENT (slower) state rather than `best`: a permanently-red gate gets
    # ignored, so the gate guards against NEW regressions while the `best` column
    # keeps the ~11.6% fib debt since 1.0.0 visible until it is bisected.
    'recorded' => '2026-07-29 (v1.5.1)',
}
