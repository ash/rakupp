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
        'fib'     => { 'baseline' => 377.7, 'best' => 350.6, 'best-version' => 'unreleased', 'best-date' => '2026-08-27' },
        'asg'     => { 'baseline' => 160.4, 'best' => 160.4, 'best-version' => 'unreleased', 'best-date' => '2026-08-29' },
        'loopsum' => { 'baseline' => 96.6, 'best' => 95.6, 'best-version' => 'unreleased', 'best-date' => '2026-08-27' },
        'hash'    => { 'baseline' => 18.3, 'best' => 17.7, 'best-version' => 'v3.7.0', 'best-date' => '2026-08-24' },
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
        'strscan' => { 'baseline' => 121.4, 'best' => 108.2, 'best-version' => 'unreleased', 'best-date' => '2026-08-27' },
        'strpass' => { 'baseline' => 73.5, 'best' => 73.5, 'best-version' => 'unreleased', 'best-date' => '2026-08-29' },
        'subcall' => { 'baseline' => 171.5, 'best' => 150.3, 'best-version' => 'unreleased', 'best-date' => '2026-08-27' },
        'rats'    => { 'baseline' => 237.0, 'best' => 176.4, 'best-version' => 'unreleased', 'best-date' => '2026-08-27' },
        'regexloop'=> { 'baseline' => 123.8, 'best' => 99.7, 'best-version' => 'unreleased', 'best-date' => '2026-08-27' },
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
    # Re-recorded 2026-08-29 for v3.21.0, and this one DOES absorb a debt —
    # five kernels moved up: fib 350.8 -> 377.7 (+7.7%), strscan 109.7 ->
    # 121.4, subcall 152.2 -> 171.5, rats 185.9 -> 237.0 (+27%), regexloop
    # 100.1 -> 123.8. Four did not: asg, loopsum, hash and strpass all held or
    # improved, and asg/strpass set new bests.
    #
    # What is known about the cause, so the next person does not re-derive it:
    # it is NOT the release's code. Every commit between the previous record
    # (d19263b, 2026-08-27) and this one was built and measured — eight points
    # across the window, arm64, one at a time on a settled machine — and no
    # kernel moves outside noise at any of them. The CONTROL is the finding:
    # d19263b itself, rebuilt, measures fib 377.7 / rats 237.0 / regexloop
    # 134.8, i.e. today's numbers and not the ones it recorded. The single real
    # movement in the window is ea81a5f making regexloop ~10% FASTER (134.8 ->
    # 123.2), which held to the tag.
    #
    # Ruled out by measurement: machine load (identical results at load 2.5 and
    # 4.0, before and after a reboot, with and without mediaanalysisd/mds
    # running), OS and toolchain (Darwin 24.6.0 and clang 17.0.0 unchanged
    # since February), power and thermal state (AC, no low-power mode, no
    # thermal warnings), and the kernels themselves (perf-guard.raku is
    # byte-identical since the previous record). A different machine does not
    # explain it either: four of nine kernels still match the old numbers.
    #
    # So the previous baseline is not reproducible on this box and the reason
    # is not yet known. `best` keeps every earlier figure — fib 350.6, rats
    # 176.4, regexloop 99.7 — so the `vs best` column carries the debt in the
    # open rather than resetting it, and v3.22.0's review owns finding the
    # cause. Do not read these five numbers as a target that was met.
    #
    # NOTE: `--record` does not stamp the 'recorded' field below, and `--check`
    # prints it — so it must be edited by hand after every record, or the gate
    # reports the wrong provenance for its own numbers.
    'recorded' => '2026-08-29 (v3.21.0)',
}
