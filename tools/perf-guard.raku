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
# With RAKUPP unset the default searches ./build/rakupp, ./build-arm64/rakupp and
# ./rakupp, and picks the first one built for THIS architecture — a box that keeps
# both an x86_64 build/ and a native build-arm64/ otherwise measured the wrong one.
# An explicitly set RAKUPP is always honoured as given. It also runs under Rakudo
# (`raku`), since it only spawns the target binary as a subprocess and times it.

my $repo   = $*PROGRAM.absolute.IO.parent.parent;
use lib $?FILE.IO.parent.add('lib').Str;
use Gate;

# Which binary to measure, and whether it is the right one for this machine —
# all of it in tools/lib/Gate.rakumod now. A binary built for ANOTHER
# ARCHITECTURE runs under translation at a uniform 1.7-2x: every kernel goes
# over tolerance at once and none of it says anything about the code. This gate
# once reported a 70-100% regression that way while the arm64 build of the same
# commit measured 7% FASTER than baseline. It says INCONCLUSIVE rather than
# REFUSED because exit 2 is this gate's documented "re-run", never a pass.
my %PICK = pick-rakupp($repo);
require-native(%PICK, :tool<perf-guard>, :verdict<INCONCLUSIVE>,
    :consequence('It would be measured under translation, which costs a uniform 1.7-2x '
               ~ 'and makes every kernel look like a regression.'));
my $HOST   = %PICK<host>;
my $RAKUPP = %PICK<path>;

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
                  while $n < 200_000 { $k++ if $s ~~ /\d/; $n = $n + 1 }; say $k;',
    # The five OO kernels were added 2026-08-30 (DISPATCH-PERF-PLAN.md), after
    # Anton's issue #47 measured Graph at 4x Rakudo and the guard could not see
    # why. Every kernel above is a sub call, a loop, a string or a regex — SUB
    # calls are well covered (fib is recursive calls, subcall is a typed `is rw`
    # signature 200k times) and they run at ~2x Rakudo. Nothing called a METHOD,
    # read an attribute, or dispatched a multi, and those run at 5.8x, 4x and 10x.
    # The whole class was invisible, which is how BinaryHeap's sift-down — thirty
    # such operations, 38k times for one `Graph.diameter` — became the reason a
    # correct Graph is slower than a wrong one.
    #   method    — the plain case: a user method on an instance. The baseline the
    #               other four are read against.
    #   attrread  — the same call plus ONE attribute read, so the difference from
    #               `method` is the attribute; both must be quoted to read it.
    #   privmeth  — `self!p(…)`. Measured at ~2x `method`: it builds the "!name"
    #               string per call and re-checks the calling scope.
    #   multimeth — proto + two candidates. Measured at ~2x `method` as well, and
    #               scoreCandidate is its own line in every profile taken.
    #   objnew    — construction, which is the frame cost plus BUILDALL.
    method    => 'class K { method m($a) { $a } }
                  my $k = K.new; my $t = 0; my int $n = 0;
                  while $n < 400_000 { $t = $k.m(1); $n = $n + 1 }; say $t;',
    attrread  => 'class K { has @!a = 1, 2, 3; method m($a) { @!a[0] } }
                  my $k = K.new; my $t = 0; my int $n = 0;
                  while $n < 400_000 { $t = $k.m(1); $n = $n + 1 }; say $t;',
    privmeth  => 'class K { method !p($a) { $a }; method m($a) { self!p($a) } }
                  my $k = K.new; my $t = 0; my int $n = 0;
                  while $n < 400_000 { $t = $k.m(1); $n = $n + 1 }; say $t;',
    multimeth => 'class K { proto method m(|) {*}
                            multi method m(Int $x) { $x }
                            multi method m(Str $x) { $x } }
                  my $k = K.new; my $t = 0; my int $n = 0;
                  while $n < 400_000 { $t = $k.m(1); $n = $n + 1 }; say $t;',
    objnew    => 'class K { has $.a; has $.b }
                  my $t; my int $n = 0;
                  while $n < 200_000 { $t = K.new(a => 1, b => 2); $n = $n + 1 }; say $t.a;';

# The kernel list, in one place: the run loop and the gate loop must agree, and
# they used to carry two hardcoded copies of it.
my @KERNELS = <fib asg loopsum hash strscan strpass subcall rats regexloop
                method attrread privmeth multimeth objnew>;

# …and it must stay in step with %kernels. A kernel added to the hash but not to
# this list is never measured and never gated, silently — the same shape as
# run-optbench's hand-written @benches, which produced a false MISSED against a
# working gate (findings/GATES-3.22.md, Part C).
{
    my @missing = %kernels.keys.grep({ $_ !(elem) @KERNELS }).sort;
    my @unknown = @KERNELS.grep({ !%kernels{$_}:exists }).sort;
    if @missing || @unknown {
        note "perf-guard: \@KERNELS and %kernels disagree — the gate would measure the wrong set.";
        note "  in %kernels but not gated: @missing.join(', ')" if @missing;
        note "  gated but not defined:     @unknown.join(', ')" if @unknown;
        exit 2;
    }
}

# The standing debt against `best`, worst first. RELEASING.md: "The `vs best`
# column is the honest headline, not `delta` … Quote `vs best` when reporting."
# Every success path calls this, so the note cannot be skipped by the exit taken.
sub report-debt(%now, %b, $tol) {
    my @debt = @KERNELS.map({
            my $best = %b<kernels>{$_}<best>;
            next unless $best.defined && %now{$_}.defined;
            my $pct = 100 * (%now{$_} - $best) / $best;
            $pct > $tol ?? ($_ => $pct) !! Empty
        }).sort({ -.value });
    return unless @debt;
    note "note: still behind the best ever measured on: "
       ~ @debt.map({ "{.key} +{.value.round(0.1)}%" }).join(', ')
       ~ " (the `best` column — standing debt, not a new regression)";
}


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

# Returns (best, spread%) — the minimum of the measured runs, and how far the
# SLOWEST measured run sat above it. That spread is the gate's own noise reading,
# taken from data it already collects, and it is a better instrument than the
# load average: GATES-3.22 Part B measured 1.7% run-to-run on a quiet machine, so
# a kernel whose runs span far more than that was not measured on a quiet one —
# whatever `uptime` says. Load average is a 1-minute decayed figure over runnable
# threads; it can read 4.0 on a machine that is quiet right now and 4.0 on one
# that is fighting this process for a core, and the gate cannot tell those apart.
# The spread can.
sub measure(Str $code) {
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
    my @m = @ms.skip(1);             # drop the warm-up
    my $min = @m.min;
    ($min, $min > 0 ?? 100 * (@m.max - $min) / $min !! 0)
}

# --check compares against tools/perf-baseline.raku and EXITS NON-ZERO on a
# regression, so a release can be gated on it. --record rewrites the baseline.
# Without either it just prints the numbers, as it always did.
my $BASEFILE = $repo.add('tools/perf-baseline.raku');
# The tolerance, for the record guard below — the gate block re-reads the file
# for itself, so this is deliberately a small separate read rather than shared
# state between two code paths that run at different times.
sub b-tol() { (EVAL slurp $BASEFILE)<tolerance-pct> // 5 }

# `git describe` ends in -modified when the binary was built from a tree with
# uncommitted changes. RELEASING.md forbids shipping such a binary; a baseline
# RECORDED from one is the same defect one level down — every later release
# would be gated against a number produced by a build nobody can reconstruct.
sub build-is-modified(Str $bin --> Bool) {
    my $p = run($bin, '--version', :out, :err);
    my $t = $p.out.slurp(:close); $p.err.slurp(:close);
    so $t.contains('-modified')
}
my $check  = so @*ARGS.grep('--check');
my $record = so @*ARGS.grep('--record');
# --for=vX.Y.Z names the release the baseline is being recorded FOR. Without it
# the stamp names the version of the binary that was measured, which during a
# release sitting is still the PREVIOUS one — gates run before the version bump.
# (`.?substr` is NOT the way to write this: the safe call finds a `substr` on
# Nil, which coerces to the EMPTY STRING rather than staying undefined — so `//`
# keeps it and the stamp reads `2026-08-29 ()`. Rakudo warns about the coercion;
# rakupp does it silently. Both engines agree on the behaviour.)
my $RECORD-FOR = do {
    my $a = @*ARGS.first(*.starts-with('--for='));
    $a.defined && $a.chars > 6 ?? $a.substr(6) !! Str;
};


say provenance-line('perf-guard', %PICK);
note "note: this build is -modified (built from a tree with uncommitted changes), "
   ~ "so its numbers name no commit that exists." if build-is-modified($RAKUPP);
my %now;
my %spread;
say "kernel      best (ms)   spread";
say "-" x 33;
for @KERNELS -> $k {
    (%now{$k}, %spread{$k}) = measure(%kernels{$k});
    printf "%-10s %8.1f    %5.1f%%\n", $k, %now{$k}, %spread{$k};
}
# One line saying how quiet the machine was, in the gate's own units.
my $worst-spread = %spread.values.max;
say sprintf('%-10s %8s    %5.1f%%   (worst kernel; the quiet-machine floor is ~1.7%%)',
            'spread', '', $worst-spread);

if $record {
    # --record REWRITES the baseline every future release is gated against, and
    # until v3.23.0 it was the only path here with no guard at all: `--check`,
    # which merely reports, refuses to judge a noisy machine three ways, while
    # the IRREVERSIBLE operation measured whatever it got and wrote it down.
    # That is backwards. A bad `--check` is re-run; a bad `--record` becomes the
    # number every later release is compared against, and RELEASING.md documents
    # four releases where a stale baseline passed silently the whole time.
    #
    # Same signals as the gate, plus the spread, and `--force` for the case where
    # the noise is understood and the record is deliberate anyway.
    if build-is-modified($RAKUPP) {
        note "";
        note "perf-guard REFUSED to record — $RAKUPP was built from a modified tree.";
        note "Its `--version` Build line ends in -modified, so it names no commit that";
        note "exists. A baseline recorded from it could never be re-measured, and every";
        note "later release would be gated against it. Commit or stash, rebuild, re-run.";
        note "(--force does not override this: the problem is the input, not the noise.)";
        exit 2;
    }

    my $force = so @*ARGS.grep('--force');
    unless $force {
        my @noisy = @KERNELS.grep({ (%spread{$_} // 0) > b-tol() });
        my $load  = machine-load();
        my $cores = core-count();
        my @why;
        @why.push("load {$load.round(0.1)} on {$cores} cores ({(100 * $load / $cores).round}%)")
            if $load > $cores * 0.6;
        @why.push("{@noisy.elems} kernel(s) whose runs span more than the tolerance: "
                ~ @noisy.map({ "$_ {%spread{$_}.round(0.1)}%" }).join(', '))
            if @noisy;
        if @why {
            note "";
            note "perf-guard REFUSED to record — this machine cannot be measured right now.";
            note "  $_" for @why;
            note "";
            note "A recorded baseline is what every later release is gated against, so a";
            note "number taken here would not be re-measurable — and the gate would then";
            note "certify it silently. Re-run on an idle machine, or pass --force if the";
            note "noise is understood and the record is deliberate (and say so in the";
            note "CHANGELOG, as RELEASING.md asks).";
            exit 2;
        }
    }

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
    # Stamp the provenance line. --record used to leave `recorded` alone while
    # --check printed it, so the field had to be hand-edited after every record
    # or the gate reported the wrong provenance for its own numbers. That is the
    # mechanism behind RELEASING.md's documented blind spot: the baseline went
    # four releases without a re-record (`2026-07-29 (v1.5.1)` while v3.0.0
    # shipped) and passed the whole time, because nothing in the output moved.
    my $stamp = "{Date.today} ({$RECORD-FOR // 'v' ~ binary-version($RAKUPP)})";
    my $stamped = False;
    for @out.kv -> $i, $l {
        next unless $l.trim.starts-with("'recorded'");
        @out[$i] = "    'recorded' => '$stamp',";
        $stamped = True;
    }
    unless $stamped {
        note "record: no 'recorded' line in {$BASEFILE.basename} to stamp — the file's";
        note "shape changed and this tool can no longer date its own output. Refusing";
        note "to write a baseline nobody can date.";
        exit 1;
    }
    $BASEFILE.spurt(@out.join("\n") ~ "\n");
    # verify the write actually took, rather than trusting the substitution
    my %after = EVAL slurp $BASEFILE;
    # compare at the precision actually written, not bit-for-bit
    my @bad = %now.keys.grep({ abs(%after<kernels>{$_}<baseline> - %now{$_}) > 0.05 });
    if @bad { note "record FAILED to update: @bad.join(', ')"; exit 1 }
    if (%after<recorded> // '') ne $stamp {
        note "record FAILED to stamp 'recorded': wrote '$stamp', reads back '{%after<recorded> // ''}'";
        exit 1;
    }
    say "";
    say "recorded {%now.elems} kernels into {$BASEFILE.basename}";
    say "stamped: recorded => '$stamp'   (measured with $RAKUPP)";
    say "added: @new.join(', ') (no previous baseline)" if @new;
    exit 0;
}

if $check {
    my %b   = EVAL slurp $BASEFILE;
    my $tol = %b<tolerance-pct>;
    my @bad;
    my @bad-kernels;
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
        if $d > $tol { @bad.push("$k {$d.round(0.1)}% slower than baseline"); @bad-kernels.push($k) }
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
        my @still-kernels;
        for @KERNELS -> $k {
            my $base = %b<kernels>{$k}<baseline> // next;
            next unless 100 * (%now{$k} - $base) / $base > $tol;
            my ($again, $again-spread) = measure(%kernels{$k});
            %spread{$k} = %spread{$k} max $again-spread;
            my $d = 100 * ($again - $base) / $base;
            printf "  %-10s re-run %7.1f  (was %.1f)  %+.1f%%\n", $k, $again, %now{$k}, $d;
            if $d > $tol { @still.push("$k {$d.round(0.1)}% slower than baseline"); @still-kernels.push($k) }
        }
        @bad = @still;
        @bad-kernels = @still-kernels;
        unless @bad {
            say "";
            say "perf-guard OK — the first reading did not reproduce (machine noise).";
            # …and the standing debt still applies. This path used to exit here,
            # so a run where any kernel flapped above tolerance and re-measured
            # clean printed no `vs best` note at all — and with a 5% tolerance
            # sitting barely above the 1.7% run-to-run and 3.5% layout floors
            # measured in findings/GATES-3.22.md, that path is common, not rare.
            report-debt(%now, %b, $tol);
            exit 0;
        }
    }
    if @bad {
        # Confirmed over tolerance twice — but if the machine is loaded, that says
        # nothing about the build. Report inconclusive (exit 2) rather than
        # accusing the code. (Every false alarm this gate has raised so far was a
        # background process, not a regression.)
        my $load = machine-load(); my $cores = core-count();

        # The gate's OWN noise reading comes first, because it is measured rather
        # than inferred. If a kernel's three runs span more than the tolerance
        # it is being asked to enforce, the reading cannot support a verdict
        # either way — the difference it is accusing the build of is smaller than
        # the difference between two runs of the SAME build.
        #
        # This exists because the load check alone was not enough. Measured
        # 2026-08-29: at load 4.33 on 8 cores (54%, under the 60% cut below) an
        # UNMODIFIED HEAD binary failed this gate with five kernels 7.6-10.3%
        # slower than baseline, and the re-measure CONFIRMED it rather than
        # clearing it. Both of those defences were built against a transient — a
        # daemon waking up — and a sustained load is not transient, so it
        # reproduces and passes straight through them.
        my @noisy = @bad-kernels.grep({ (%spread{$_} // 0) > $tol });
        if @noisy {
            note "";
            note "perf-guard INCONCLUSIVE — the measurement is noisier than the thing it measures.";
            for @noisy -> $k {
                note sprintf('  %-10s runs span %.1f%%, but the gate fires at %s%% — '
                           ~ 'this reading cannot tell a regression from the machine.',
                             $k, %spread{$k}, $tol);
            }
            note "Load average {$load.round(0.1)} on {$cores} cores. Re-run on an idle machine.";
            note "Kernels over tolerance: @bad.join('; ')";
            exit 2;
        }

        # Second measured signal: is the slowdown LOCALIZED?
        #
        # A code regression is localized — it slows the kernels that touch the
        # code it changed. Machine contention is not: it slows everything at
        # once, and roughly equally, which keeps each kernel's own spread SMALL
        # while inflating every absolute number. That is the case the spread
        # check above cannot see, and it is the one actually observed: at load
        # 4.33 on 8 cores an unmodified binary came back with fib +9.1%,
        # asg +10.3%, strpass +8.4%, subcall +7.6% and rats +8.7% — five of nine,
        # spanning arithmetic, calls, strings and Rats, which share no code path.
        # A change that slowed all four of those families at once would be a
        # rewrite, not a regression, and would not arrive as a surprise.
        #
        # Reporting INCONCLUSIVE here is safe in the direction that matters:
        # RELEASING.md defines exit 2 as "run it again on an idle machine, never
        # a pass", so a genuine broad regression is still blocked — it is simply
        # not yet ACCUSED, and an idle re-run will confirm it.
        my $gated = @KERNELS.grep({ %b<kernels>{$_}<baseline>.defined }).elems;
        if $gated >= 4 && @bad-kernels >= $gated / 2 {
            note "";
            note "perf-guard INCONCLUSIVE — {@bad-kernels.elems} of $gated kernels are over tolerance at once.";
            note "That is not the shape of a code regression: these kernels span arithmetic,";
            note "calls, strings, hashing and regex, and share no code path. A machine slows";
            note "them all together; a change slows the ones it touched.";
            note "Load average {$load.round(0.1)} on {$cores} cores.";
            note "Kernels over tolerance: @bad.join('; ')";
            note "Re-run on an idle machine — if it really is this broad, it will say so again.";
            exit 2;
        }

        if $load > $cores * 0.6 {
            note "";
            note "perf-guard INCONCLUSIVE — load average {$load.round(0.1)} on {$cores} cores.";
            note "Something else is using this machine; these timings mean nothing.";
            note "Kernels over tolerance: @bad.join('; ')";
            note "Re-run when the machine is idle.";
            exit 2;
        }
        # A FAILED verdict must show the machine state that produced it. The
        # INCONCLUSIVE branch above prints the load; this one did not, so the
        # gate's most consequential output was the one that said least about
        # its own conditions. Measured 2026-08-29: at load 4.33 on 8 cores —
        # 54%, just under the 60% cut above — an UNMODIFIED HEAD binary failed
        # this gate with five kernels 7.6-10.3% slower than baseline, and the
        # re-measure confirmed rather than cleared it, because the load was
        # sustained rather than a spike. Both defences assume a transient.
        note "perf-guard FAILED (confirmed on re-measure): @bad.join('; ')";
        note "Machine at the time: load {$load.round(0.1)} on {$cores} cores "
           ~ "({(100 * $load / $cores).round}% — the inconclusive cut is 60%).";
        note "A sustained load defeats BOTH the re-measure and the cut above: it"
           ~ " is not a spike, so it reproduces. Confirm on an idle machine, and"
           ~ " A/B against a binary built WITHOUT the change before believing it.";
        note "A release must not ship a performance regression. Either fix it, or —";
        note "if the cost is understood and accepted — re-record the baseline with";
        note "`rakupp tools/perf-guard.raku --record` and say why in the CHANGELOG.";
        exit 1;
    }
    say "perf-guard OK — no kernel is more than {$tol}% slower than the last release.";
    report-debt(%now, %b, $tol);
    exit 0;
}
