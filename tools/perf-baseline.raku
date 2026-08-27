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
        'fib'     => { 'baseline' => 350.6, 'best' => 350.6, 'best-version' => 'unreleased', 'best-date' => '2026-08-27' },
        'asg'     => { 'baseline' => 162.1, 'best' => 162.1, 'best-version' => 'unreleased', 'best-date' => '2026-08-27' },
        'loopsum' => { 'baseline' => 95.6, 'best' => 95.6, 'best-version' => 'unreleased', 'best-date' => '2026-08-27' },
        'hash'    => { 'baseline' => 19.4, 'best' => 17.7, 'best-version' => 'v3.7.0', 'best-date' => '2026-08-24' },
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
        'strscan' => { 'baseline' => 108.2, 'best' => 108.2, 'best-version' => 'unreleased', 'best-date' => '2026-08-27' },
        'strpass' => { 'baseline' => 74.8, 'best' => 74.8, 'best-version' => 'unreleased', 'best-date' => '2026-08-27' },
        'subcall' => { 'baseline' => 150.3, 'best' => 150.3, 'best-version' => 'unreleased', 'best-date' => '2026-08-27' },
        'rats'    => { 'baseline' => 176.4, 'best' => 176.4, 'best-version' => 'unreleased', 'best-date' => '2026-08-27' },
        'regexloop'=> { 'baseline' => 99.7, 'best' => 99.7, 'best-version' => 'unreleased', 'best-date' => '2026-08-27' },
        # `rats` was added to the guard on 2026-08-22, after the cold block
        # moved the Rat numerator/denominator pair out of the inline Value, and
        # it went in here WITHOUT a number: it was written on the M1/Darwin 25.5
        # box, not the Darwin 24.6 machine this file is stamped with, and that
        # box measures the Raku++ binary 1.3-1.5x slower. Its 256.9 ms would
        # therefore have sat ~30% loose beside its neighbours and let a real
        # regression through the very gate it was added to build -- the same
        # mistake the strscan note above describes avoiding, in a new form.
        # Two changes made the absence safe rather than broken: `--check`
        # reports an unrecorded kernel as "not gated" instead of dividing by a
        # missing baseline, and `--record` ADDS a missing kernel's line instead
        # of failing verification, so the first re-record on the benchmarks
        # machine picks it up with no hand-editing. If a 'rats' line is present
        # above, that re-record has happened and 256.9 is only the M1 reference
        # the number should have landed below.
    },
    # Re-recorded 2026-08-11 for v3.14.0, at 594/1462 Roast files. The check
    # against the v3.1.0 baseline passed first (every kernel 1.9-3.0% FASTER
    # -- the SLIM campaign's interpreter-side changes cost nothing), so this
    # re-record tightens the reference rather than absorbing a debt: on
    # 2026-08-22 every kernel came down 32-62% (asg 447.1 -> 169.3, hash
    # 36.0 -> 17.8) and not one moved up, so nothing was absorbed. `best`
    # moves with all of them this time — loopsum's long-standing debt against
    # its 1.0.0 best of 194.4 is CLOSED rather than reset, since 98.0 beats it
    # outright. `rats` joins with its first recorded number.
    #
    # NOTE: `--record` does not stamp the 'recorded' field below, and `--check`
    # prints it — so it must be edited by hand after every record, or the gate
    # reports the wrong provenance for its own numbers.
    'recorded' => '2026-08-24 (v3.7.0)',
}
