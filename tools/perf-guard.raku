#!/usr/bin/env raku
# Lightweight interpreter performance guard — run before/after a change to catch
# per-iteration regressions in the eval/exec hot path. Unlike tools/run-bench.raku
# (which also times --exe and Rakudo across the full kernel set), this times only
# the interpreter on a few tight-loop kernels, best-of-3, in a couple of seconds.
#
# The kernels deliberately stress the paths a batch is most likely to slow down:
#   fib     — recursive sub calls + int arithmetic (dispatch + applyArith)
#   asg     — a plain scalar `=` assignment loop
#   loopsum — a `+=` compound assignment over a Range with the `$_` topic
#   hash    — hash-index post-increment in a loop
#   strscan — per-character .substr over a long string
#   strpass — a long string passed to a sub, 200k times
#   subcall — a typed `is rw` signature called 200k times
#   rats    — 200k short-lived Rats created, summed and read once each
# (loopsum/hash were added after a ~8-22% interp regression on exactly these
# shapes slipped past the old fib+asg-only guard over the 0.7.1->0.9.0 cycle.
# strscan/strpass/subcall were added after v3.0.1 replaced the string
# representation and the guard, being all-Int, could not see it either way.
# rats was added after the cold block moved the Rat pair out of line: every
# kernel above lives entirely in the inline part of a Value, so the one type
# the change could plausibly have taxed was again invisible to the gate.)
#
# Usage — A/B two binaries:
#     RAKUPP=/path/to/old rakupp tools/perf-guard.raku
#     RAKUPP=/path/to/new rakupp tools/perf-guard.raku
# Default RAKUPP is ./build/rakupp. It also runs under Rakudo (`raku`), since it
# only spawns the target binary as a subprocess and times it.

my $repo   = $*PROGRAM.absolute.IO.parent.parent;
my $RAKUPP = %*ENV<RAKUPP> // $repo.add('build/rakupp').Str;
# A MISSING binary must stop the run, not pass it. Every kernel then "runs" in
# under a millisecond and the guard reports a huge speed-up and exits OK — which
# is exactly what happened when a stale build/ directory was removed and the
# default path stopped existing.
unless $RAKUPP.IO.x {
    note "perf-guard: no runnable binary at $RAKUPP";
    note "Set RAKUPP=/path/to/rakupp (the default is <repo>/build/rakupp).";
    exit 2;
}

# …and a binary built for ANOTHER ARCHITECTURE runs under translation, which
# costs a uniform 1.7-2x: every kernel goes over tolerance at once and none of it
# says anything about the code. It is easy to hit — the default is
# <repo>/build/rakupp, and a machine that also keeps a build-arm64/ can leave a
# stale x86_64 binary at the default path. The gate reported a 70-100% regression
# across the board that way while the arm64 build of the same commit measured 7%
# FASTER than the baseline.
sub host-arch(--> Str) {
    return '' if $*DISTRO.is-win;
    # hw.optional.arm64 is 1 on Apple Silicon even when the ASKING process is
    # translated — `uname -m` is not, it reports the caller's own architecture.
    my $s = run('sysctl', '-n', 'hw.optional.arm64', :out, :err);
    my $v = $s.out.slurp(:close).trim; $s.err.slurp(:close);
    return 'arm64' if $v eq '1';
    my $u = run('uname', '-m', :out, :err);
    my $m = $u.out.slurp(:close).trim; $u.err.slurp(:close);
    $m
}
{
    my $f = run('file', '-b', $RAKUPP, :out, :err);
    my $desc = $f.out.slurp(:close); $f.err.slurp(:close);
    my $bin = $desc.contains('arm64') ?? 'arm64'
           !! $desc.contains('x86_64') ?? 'x86_64' !! '';
    my $host = host-arch();
    if $bin && $host && $bin ne $host {
        note "perf-guard INCONCLUSIVE — $RAKUPP is $bin on a $host host.";
        note "It would be measured under translation, which costs 1.7-2x uniformly";
        note "and makes every kernel look like a regression.";
        note "Build for this machine, or point RAKUPP at the $host binary.";
        exit 2;
    }
}
my $RUNS   = 4;   # 1 warm-up (discarded) + 3 measured

my %kernels =
    fib     => 'sub fib($n) { $n < 2 ?? $n !! fib($n-1) + fib($n-2) }; say fib(29);',
    asg     => 'my $x = 0; for ^2_000_000 { $x = $x + 1 }; say $x;',
    loopsum => 'my $t = 0; for 1 .. 1_000_000 { $t += $_ }; say $t;',
    hash    => 'my %c; for 1 .. 100_000 { %c{$_ % 1_000}++ }; say %c.elems;',
    # The three string/call kernels below were added 2026-08-09, after v3.0.1
    # changed the string representation (CowStr) and the guard could not see it:
    # every kernel above is Int-and-Array work, so a release that made string
    # operations 6x slower would have passed. Each guards a distinct thing that
    # the JSON::Fast work depended on, and each FAILS LOUDLY if it comes back:
    #   strscan — per-character .substr over a long string. Catches any op that
    #             re-derives a string property by scanning, or copies the whole
    #             invocant, per call (STRING-SCAN-QUADRATICS.md). Nearly flat in
    #             the string's length when it is right, O(n) per call when not.
    #   strpass — a long string passed to a sub 200k times. Catches a regression
    #             in CowStr promotion/sharing: without it this is a 200 KB memcpy
    #             per call.
    #   subcall — a typed `is rw` signature called 200k times. Catches per-call
    #             work that belongs on the AST: signature rescans, type-name
    #             string lookups, the binder's slow path.
    strscan => 'my $s = "abcdefghij" x 20000; my int $n = 0; my $a = 0;
                while $n < 200000 { $a = $a + $s.substr($n % 199000, 1).ord; $n = $n + 1 }; say $a;',
    strpass => 'use nqp; sub f(str $t) { nqp::chars($t) }; my str $s = "abcdefghij" x 20000;
                my int $n = 0; my $a = 0; while $n < 200000 { $a = $a + f($s); $n = $n + 1 }; say $a;',
    subcall => 'use nqp; my str $s = "   x" x 200000;
                sub nomws(str $t, int $p is rw --> Nil) { nqp::while(nqp::iseq_i(nqp::ordat($t,$p),32), ++$p) }
                my int $n = 0; my int $p = 0;
                while $n < 200000 { nomws($s, $p); $p = $p + 4; $n = $n + 1 }; say $p;',
    # rats was added 2026-08-22, after the batch-two representation work moved
    # the Rat numerator/denominator pair out of the inline Value and behind the
    # lazily-allocated cold block (REPRESENTATION-PLAN.md). That trade pays for
    # itself on values that get COPIED — 128 bytes instead of 344 — and charges
    # one small allocation to values that are merely CREATED and read. A program
    # that mass-creates short-lived Rats and reads each once is therefore the one
    # named shape that could pay without collecting, and no kernel isolated it:
    # fib/asg/loopsum/hash/streq are Int, strscan/strpass/subcall are string.
    # This is not a synthetic shape either — every decimal literal in Raku is a
    # Rat, so any money or unit-conversion loop is exactly this program.
    # 0.01 * n keeps the denominator bounded (no bignum promotion, so the kernel
    # measures Rat handling and not the BigInt path), while the gcd reduction
    # still varies per iteration. The same loop with `1 *` instead of `0.01 *`
    # runs in 60 ms against this kernel's ~440, so ~85% of what it times is
    # Rat-specific: a regression in that path cannot hide in loop overhead.
    rats    => 'my $t = 0; my $d = 0;
                for 1 .. 200_000 { my $r = 0.01 * ($_ % 97); $t += $r; $d += $r.denominator }
                say $t, " ", $d;',
    # regexloop was added 2026-08-27 (REVIEW-3.7 batch 3), after the review
    # found per-evaluation costs on the regex-literal path — a static-mutex
    # lock plus a map probe keyed by the whole pattern (rejectObsoleteRegex),
    # and boolify(Regex) walking the env chain for $/ — that NO kernel could
    # see: every kernel above is regex-free, so the guard was blind to the
    # whole class. `if $s ~~ /\d/` in a loop is the shape that pays it, and
    # it is not synthetic: any grep-like scan over lines is this program.
    regexloop => 'my $s = "a1b2c3"; my int $n = 0; my $k = 0;
                  while $n < 200_000 { $k++ if $s ~~ /\d/; $n = $n + 1 }; say $k;';

# The kernel list, in one place: the run loop and the gate loop must agree, and
# they used to carry two hardcoded copies of it.
my @KERNELS = <fib asg loopsum hash strscan strpass subcall rats regexloop>;


# The 1-minute load average, and how many cores there are to carry it. A busy
# machine cannot be judged: absolute times here move by tens of percent when
# something else is running, so a loaded run is INCONCLUSIVE, not a regression.
sub machine-load(--> Numeric) {
    if $*DISTRO.is-win { return 0 }
    my $p = run('uptime', :out, :err); my $t = $p.out.slurp(:close); $p.err.slurp(:close);
    return 0 unless $t ~~ / 'average' 's'? ':' \s* $<l>=[<[\d.]>+] /;
    +$<l>
}
sub core-count(--> Int) {
    my $p = run('sysctl', '-n', 'hw.ncpu', :out, :err);
    my $t = $p.out.slurp(:close).trim; $p.err.slurp(:close);
    return +$t if $t ~~ /^\d+$/;
    my $n = '/proc/cpuinfo'.IO.e ?? '/proc/cpuinfo'.IO.lines.grep(*.starts-with('processor')).elems !! 0;
    $n || 1
}

sub measure(Str $code --> Numeric) {
    my $tmp = $*TMPDIR.add("perf-guard-{$*PID}-{1e6.rand.Int}.raku");
    $tmp.spurt($code);
    my @ms;
    for ^$RUNS {
        my $t0 = now;
        my $p  = run($RAKUPP, $tmp.Str, :out, :err);
        $p.out.slurp(:close); $p.err.slurp(:close);
        @ms.push: ((now - $t0) * 1000).round(0.1);
    }
    $tmp.unlink;
    @ms.skip(1).min;   # best of the measured runs
}

# --check compares against tools/perf-baseline.raku and EXITS NON-ZERO on a
# regression, so a release can be gated on it. --record rewrites the baseline.
# Without either it just prints the numbers, as it always did.
my $BASEFILE = $repo.add('tools/perf-baseline.raku');
my $check  = so @*ARGS.grep('--check');
my $record = so @*ARGS.grep('--record');

say "perf-guard: $RAKUPP";
my %now;
say "kernel      best (ms)";
say "-" x 24;
for @KERNELS -> $k {
    %now{$k} = measure(%kernels{$k});
    printf "%-10s %8.1f\n", $k, %now{$k};
}

if $record {
    my %b = EVAL slurp $BASEFILE;
    # Rewrite ONLY the kernel lines, rebuilt from the parsed data — every comment
    # in the file survives, because the file's explanation of why the baseline is
    # what it is outlives the numbers. (An earlier version tried to patch the
    # numbers with a regex and silently matched nothing, which is worse than not
    # having a record path at all; hence the verification at the end.)
    my @out;
    my %seen;          # kernels that already had a line in the file
    my $last-kernel = -1;   # where to splice a NEW kernel's line in
    for $BASEFILE.slurp.lines -> $line {
        my $matched = '';
        # plain string test, not a regex: interpolating $k into a pattern does not
        # match under rakupp itself (which is what runs this), and this tool has to
        # work on the interpreter it measures
        for %now.keys -> $k { $matched = $k if $line.trim.starts-with("'$k'") }
        if $matched {
            %seen{$matched} = True;
            my $e    = %b<kernels>{$matched};
            my $base = %now{$matched};
            # a kernel that got FASTER than anything seen before moves `best` too
            my ($best, $ver, $date) = $e<best>, $e<best-version>, $e<best-date>;
            if $base < $best {
                $best = $base; $ver = 'unreleased'; $date = Date.today.Str;
            }
            # format to one decimal: `.round(0.1)` on a Num still carries its
            # representation error into the file (841.4000000000001)
            $base = $base.fmt('%.1f'); $best = $best.fmt('%.1f');
            my $pad = ' ' x (8 - $matched.chars);
            my $open = '{';
            my $close = '}';
            @out.push: "        '$matched'$pad=> $open 'baseline' => $base, 'best' => $best, "
                     ~ "'best-version' => '$ver', 'best-date' => '$date' $close,";
            $last-kernel = @out.end;
        }
        else { @out.push: $line }
    }
    # A kernel ADDED to @KERNELS since the file was last written has no line to
    # rewrite. Before this, the loop above matched nothing for it, the
    # verification at the end reported "record FAILED", and the only way to add
    # a kernel was to hand-edit this file — which is how strscan/strpass/subcall
    # got in. Write the missing lines instead, right after the last kernel line.
    my @new = %now.keys.grep({ !%seen{$_} }).sort;
    if @new && $last-kernel >= 0 {
        my @lines;
        for @new -> $k {
            my $v    = %now{$k}.fmt('%.1f');
            my $pad  = ' ' x (8 - $k.chars);
            my $open = '{'; my $close = '}';
            @lines.push: "        '$k'$pad=> $open 'baseline' => $v, 'best' => $v, "
                       ~ "'best-version' => 'unreleased', 'best-date' => '{Date.today}' $close,";
        }
        @out.splice($last-kernel + 1, 0, |@lines);
    }
    $BASEFILE.spurt(@out.join("\n") ~ "\n");
    # verify the write actually took, rather than trusting the substitution
    my %after = EVAL slurp $BASEFILE;
    # compare at the precision actually written, not bit-for-bit
    my @bad = %now.keys.grep({ abs(%after<kernels>{$_}<baseline> - %now{$_}) > 0.05 });
    if @bad { note "record FAILED to update: @bad.join(', ')"; exit 1 }
    say "";
    say "recorded {%now.elems} kernels into {$BASEFILE.basename}";
    say "added: @new.join(', ') (no previous baseline)" if @new;
    exit 0;
}

if $check {
    my %b   = EVAL slurp $BASEFILE;
    my $tol = %b<tolerance-pct>;
    my @bad;
    say "";
    say "gate: baseline {$BASEFILE.basename} (recorded %b<recorded>), tolerance {$tol}%";
    say "kernel        now   baseline    delta   vs best";
    say "-" x 52;
    # A kernel with NO line in the baseline file is one that was added to
    # @KERNELS and not yet recorded — most often because it was added on a
    # machine other than the one the file is stamped with, so its number here
    # would not be comparable with its neighbours. It is reported and skipped.
    # (Before this it divided by a missing baseline, printed `+Inf%`, and took
    # the whole gate down with it — a new kernel could not be added without
    # hand-editing the baseline file first.)
    my @ungated;
    for @KERNELS -> $k {
        my $e = %b<kernels>{$k};
        unless $e.defined && $e<baseline>.defined {
            printf "%-10s %7.1f  %9s  %7s  %7s\n", $k, %now{$k}, '—', 'n/a', 'n/a';
            @ungated.push($k);
            next;
        }
        my $base = $e<baseline>;
        my $best = $e<best>;
        my $d    = 100 * (%now{$k} - $base) / $base;
        my $vb   = 100 * (%now{$k} - $best) / $best;
        printf "%-10s %7.1f  %9.1f  %+6.1f%%  %+6.1f%%\n", $k, %now{$k}, $base, $d, $vb;
        @bad.push("$k {$d.round(0.1)}% slower than baseline") if $d > $tol;
    }
    say "";
    if @ungated {
        note "note: not gated (no recorded baseline): @ungated.join(', ') — "
           ~ "run `perf-guard --record` on the machine {$BASEFILE.basename} names.";
    }
    # A failing kernel is RE-MEASURED before the gate believes it. Absolute times
    # on a desktop move by tens of percent when a background daemon wakes up —
    # during one afternoon this gate reported four regressions of up to +50%,
    # every one of them Spotlight/analytics/WindowServer rather than the build.
    # A real regression reproduces; a busy machine usually does not.
    if @bad {
        note "perf-guard: {@bad.elems} kernel(s) over tolerance — re-measuring before believing it";
        my @still;
        for @KERNELS -> $k {
            my $base = %b<kernels>{$k}<baseline> // next;
            next unless 100 * (%now{$k} - $base) / $base > $tol;
            my $again = measure(%kernels{$k});
            my $d = 100 * ($again - $base) / $base;
            printf "  %-10s re-run %7.1f  (was %.1f)  %+.1f%%\n", $k, $again, %now{$k}, $d;
            @still.push("$k {$d.round(0.1)}% slower than baseline") if $d > $tol;
        }
        @bad = @still;
        unless @bad {
            say "";
            say "perf-guard OK — the first reading did not reproduce (machine noise).";
            exit 0;
        }
    }
    if @bad {
        # Confirmed over tolerance twice — but if the machine is loaded, that says
        # nothing about the build. Report inconclusive (exit 2) rather than
        # accusing the code. (Every false alarm this gate has raised so far was a
        # background process, not a regression.)
        my $load = machine-load(); my $cores = core-count();
        if $load > $cores * 0.6 {
            note "";
            note "perf-guard INCONCLUSIVE — load average {$load.round(0.1)} on {$cores} cores.";
            note "Something else is using this machine; these timings mean nothing.";
            note "Kernels over tolerance: @bad.join('; ')";
            note "Re-run when the machine is idle.";
            exit 2;
        }
        note "perf-guard FAILED (confirmed on re-measure): @bad.join('; ')";
        note "A release must not ship a performance regression. Either fix it, or —";
        note "if the cost is understood and accepted — re-record the baseline with";
        note "`rakupp tools/perf-guard.raku --record` and say why in the CHANGELOG.";
        exit 1;
    }
    say "perf-guard OK — no kernel is more than {$tol}% slower than the last release.";
    my @debt = <fib asg loopsum hash>.grep({
        %b<kernels>{$_}<best>.defined
        && 100 * (%now{$_} - %b<kernels>{$_}<best>) / %b<kernels>{$_}<best> > $tol });
    note "note: still behind the best ever measured on: @debt.join(', ') "
       ~ "(see the `best` column — standing debt, not a new regression)" if @debt;
    exit 0;
}
