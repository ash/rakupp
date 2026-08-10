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
        'fib'     => { 'baseline' => 666.0, 'best' => 666.0, 'best-version' => '3.1.0', 'best-date' => '2026-08-11' },
        'asg'     => { 'baseline' => 455.3, 'best' => 455.3, 'best-version' => '3.1.0', 'best-date' => '2026-08-11' },
        'loopsum' => { 'baseline' => 202.0, 'best' => 194.4, 'best-version' => '1.0.0', 'best-date' => '2026-07-22' },
        'hash'    => { 'baseline' => 36.6, 'best' => 36.6, 'best-version' => '3.1.0', 'best-date' => '2026-08-11' },
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
        'strscan' => { 'baseline' => 205.6, 'best' => 205.6, 'best-version' => '3.1.0', 'best-date' => '2026-08-11' },
        'strpass' => { 'baseline' => 150.5, 'best' => 150.5, 'best-version' => '3.1.0', 'best-date' => '2026-08-11' },
        'subcall' => { 'baseline' => 274.8, 'best' => 274.8, 'best-version' => '3.1.0', 'best-date' => '2026-08-11' },
    },
    # Re-recorded 2026-08-11 for v3.1.0, at 595/1462 Roast files, and it was
    # overdue: the previous entry read 2026-07-29 (v1.5.1), so the gate had been
    # comparing against a reference three releases old. It kept passing, which is
    # exactly why nobody noticed -- RELEASING.md lists this as a known blind
    # spot. Every kernel had drifted FASTER than that stale reference (fib
    # -14.6%, hash -10.2%, strscan -9.6%), so the old baseline could no longer
    # have caught a regression of less than about a tenth.
    #
    # `best` moves with it for six of the seven kernels, because this build IS
    # the fastest measured. loopsum keeps its 1.0.0 best (194.4): that debt is
    # still open and stays visible rather than being quietly reset.
    'recorded' => '2026-08-11 (v3.1.0)',
}
