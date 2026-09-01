#!/usr/bin/env raku
# Benchmark harness — times each program in tools/bench/ on every engine it can
# find and prints a comparison table:
#   * interp — Raku++ interpreting the source
#   * native — Raku++ `--exe` (transpiled to C++, compiled to a native binary)
#   * mutsu  — mutsu, a Raku implementation in Rust (bytecode VM + Cranelift JIT)
#   * rakudo — Rakudo interpreting the source
#
# The mutsu lane is OPTIONAL and skipped when no mutsu is installed; see
# resolve-mutsu below. Rakudo is the correctness oracle for every lane.
#
# Run it with Raku++ (dogfooding, matches run-roast.raku):
#
#     ./build/rakupp tools/run-bench.raku
#
# It also runs under Rakudo (`raku tools/run-bench.raku`). Either way the runner
# only spawns each engine as a fresh subprocess and times it, so the language it
# is written in does not favour any contestant.
#
# The engines are interleaved: each measured round times every engine once,
# back to back, so a load spike hits all lanes rather than one column. The
# table prints the best (minimum) run, as it always has; the machine-readable
# output carries the median as well, for consumers on noisy shared hardware.
#
# Options (also fine under Rakudo):
#     --only=name,name     run just these kernels
#     --tsv=PATH           write a machine-readable TSV: metadata header lines
#                          (`# key=value`), then one row per kernel with min
#                          and median per lane
#     --rusage             also measure CPU time (user+sys) and peak RSS per
#                          lane, printed as a second table. Measured in its own
#                          pass AFTER the timing rounds, so the wall-clock
#                          numbers stay exactly what they were without it.
#
# Override the binaries via environment:
#     RAKUPP=/path/to/rakupp RAKUDO=raku ./build/rakupp tools/run-bench.raku

my $tools = $*PROGRAM.absolute.IO.parent;   # tools/
my $repo  = $tools.parent;                  # repo root
my $bench = $tools.add('bench');            # benchmark programs live here
use lib $?FILE.IO.parent.add('lib').Str;
use Gate;
my $RAKUDO = %*ENV<RAKUDO> // 'raku';
my $PERL   = %*ENV<PERL>   // 'perl';   # only used by benches that ship a .pl twin

my $tsv-path = '';
my @only;
my $rusage = False;
for @*ARGS -> $a {
    if $a.starts-with('--tsv=') { $tsv-path = $a.substr(6) }
    elsif $a.starts-with('--only=') { @only = $a.substr(7).split(',')>>.trim }
    elsif $a eq '--rusage' { $rusage = True }
    else { note "run-bench: unknown argument $a"; exit 2 }
}

# A binary built for ANOTHER ARCHITECTURE runs under translation, which costs a
# uniform 1.7-2x. This harness once silently benchmarked one: a stale x86_64
# build/ beside a build-arm64/ inflated every interp row ~1.9x and startup 8x,
# and the numbers nearly shipped in BENCHMARKS.md at v3.14.0. tools/lib/Gate.rakumod
# owns the check now — and the DEFAULT is searched rather than hardcoded, because
# `build/rakupp` was both the documented default and, for two releases, the wrong
# binary: this tool's own documented command exited 2 rather than measuring.
my %PICK = pick-rakupp($repo);
require-native(%PICK, :tool<run-bench>,
    :consequence('It would be measured under translation, which costs 1.7-2x uniformly.'));
my $RAKUPP = %PICK<path>;
note provenance-line('run-bench', %PICK);

my $RUNS   = 7;   # 1 warm-up round (discarded) + 6 measured

my @benches =
    %( :name<startup>,  :file("startup.raku"),  :note('hello world (startup-dominated)') ),
    %( :name<loopsum>,  :file("loopsum.raku"),  :note('sum 1 .. 1_000_000 in a for loop') ),
    %( :name<fib>,      :file("fib.raku"),      :note('naïve recursive fib(29)') ),
    %( :name<strcat>,   :file("strcat.raku"),   :note('50_000 string concatenations') ),
    %( :name<arrayops>, :file("arrayops.raku"), :note('grep+map+sum over 200_000') ),
    %( :name<arraypush>,:file("arraypush.raku"),:note('200k push, then indexed read and write') ),
    %( :name<sortnums>, :file("sortnums.raku"), :note('sort 50_000 integers') ),
    %( :name<sortby>,   :file("sortby.raku"),   :note('sort 30_000 strings by a 1-ary key extractor') ),
    %( :name<regex>,    :file("regex.raku"),    :note('50_000 regex matches') ),
    %( :name<hash>,     :file("hash.raku"),     :note('100_000 hash increments') ),
    %( :name<hashfill>, :file("hashfill.raku"), :perl("hashfill.pl"),
       :note('200k-key hash fill + values sweep + 50k-append string build') ),
    %( :name<bigint>,   :file("bigint.raku"),   :note('factorial(5000) via BigInt multiply') ),
    %( :name<streq>,    :file("streq.raku"),    :note('1M string eq/lt comparisons') ),
    %( :name<textsplit>,:file("textsplit.raku"),:perl("textsplit.pl"),
       :note('20k lines split into fields, reordered, rejoined') ),
    %( :name<rats>,     :file("rats.raku"),     :note('200k short-lived Rats summed and read') ),
    %( :name<objects>,  :file("objects.raku"),  :note('200k .new + 300k method calls') );
@benches = @benches.grep({ .<name> (elem) @only }) if @only;
unless @benches {
    note "run-bench: --only matched no kernels";
    exit 2;
}

# Compile a program to a native binary via `--exe`; True on success.
sub compile-native(Str $path, Str $out --> Bool) {
    my $p = run($RAKUPP, '--exe', $path, '-o', $out, :out, :err);
    $p.out.slurp(:close); $p.err.slurp(:close);
    $p.exitcode == 0;
}

# Capture a command's stdout (stderr is drained but ignored — Raku++ prints
# compile-time warnings there). Returns Nil if the program exits non-zero, so a
# crash reads as "no output" and is flagged rather than silently compared.
sub capture(@cmd --> Str) {
    my $p = run(|@cmd, :out, :err);
    my $out = $p.out.slurp(:close);
    $p.err.slurp(:close);
    $p.exitcode == 0 ?? $out !! Str;
}

# First stdout line of a command, or '' — for the metadata header.
sub first-line(@cmd --> Str) {
    my $p = run(|@cmd, :out, :err);
    my $out = $p.out.slurp(:close);
    $p.err.slurp(:close);
    $p.exitcode == 0 ?? ($out.lines[0] // '') !! ''
}

# The mutsu lane is OPTIONAL. mutsu (github.com/tokuhirom/mutsu) is a separate
# Raku implementation — a Rust bytecode VM with a Cranelift JIT — and most
# machines running this harness will not have one. Resolution order is $MUTSU,
# then `mutsu` on PATH, then a checkout under $HOME; when nothing answers
# `--version` with a mutsu banner the column reads `—` and no other lane is
# affected. A mutsu disagreement is REPORTED but never fails the run: this
# harness gates OUR lanes, and another implementation's result is a reference
# point, not a defect in our suite.
sub resolve-mutsu(--> Str) {
    my @cand;
    @cand.push(%*ENV<MUTSU>) if %*ENV<MUTSU>;
    @cand.push('mutsu');
    @cand.push(%*ENV<HOME> ~ '/mutsu/target/release/mutsu') if %*ENV<HOME>;
    for @cand -> $c {
        my $v = (try first-line([$c, '--version'])) // '';
        return $c if $v.lc.starts-with('mutsu');
    }
    return Str;
}
my $MUTSU = resolve-mutsu();

# CPU time (ms, user+sys) and peak RSS (KiB) for ONE run of @cmd, read out of
# `/usr/bin/time`. Two incompatible formats are in play and both are parsed:
#
#   BSD/macOS  `-l`:  "0.05 real  0.02 user  0.01 sys" then "1359872  maximum
#                     resident set size" — RSS in BYTES.
#   GNU/Linux  `-v`:  "User time (seconds): 0.02" / "Maximum resident set size
#                     (kbytes): 1327" — RSS in KILOBYTES.
#
# Getting the unit wrong is a 1024x error that still looks like a plausible
# memory figure, so the two are parsed by their own distinct wording rather than
# by grabbing the first number on a "maximum resident set size" line.
#
# Why not getrusage(RUSAGE_CHILDREN) from inside this harness: its ru_maxrss is
# the maximum over ALL children reaped so far and never falls, so after one
# heavy lane every later lane would report that same peak. Spawning under
# /usr/bin/time gives each run its own isolated accounting.
#
# Returns a Hash with <cpu rss>, or an empty Hash when neither format parses —
# every caller renders that as n/a rather than as a zero.
#
# $reps runs are wrapped in ONE `sh -c` and the CPU total divided by it, because
# both formats report CPU to 10ms and the fast kernels finish inside a single
# quantum: measured one at a time, `native`/`hash` reads a flat 0ms however many
# times it is sampled. Peak RSS needs no such help — it is the max over the
# wrapped children, which is the same figure a single run would give.
sub rusage-run(@cmd, Int $reps = 1 --> Hash) {
    my $gnu = !$*DISTRO.name.lc.contains('macos' | 'darwin');
    my $flag = $gnu ?? '-v' !! '-l';
    my $p = do if $reps > 1 {
        my $one = @cmd.map({ q{'} ~ .subst(q{'}, q{'\''}, :g) ~ q{'} }).join(' ');
        run('/usr/bin/time', $flag, '/bin/sh', '-c', "$one; " x $reps, :out, :err);
    }
    else {
        run('/usr/bin/time', $flag, |@cmd, :out, :err);
    }
    $p.out.slurp(:close);
    my $err = $p.err.slurp(:close);
    return {} unless $p.exitcode == 0;

    my ($cpu, $rss);
    # BSD: bare numbers labelled by trailing words, RSS in bytes.
    if $err ~~ / $<u>=[\d+ ['.' \d+]?] \s+ 'user' \s+ $<s>=[\d+ ['.' \d+]?] \s+ 'sys' / {
        $cpu = (+$<u> + +$<s>) * 1000;
    }
    if $err ~~ / $<b>=[\d+] \s+ 'maximum resident set size' / {
        $rss = (+$<b>) / 1024;
    }
    # GNU: "Key: value", RSS already in kilobytes.
    if $err ~~ / 'User time (seconds):' \s* $<u>=[\d+ ['.' \d+]?] / {
        my $u = +$<u>;
        my $s = $err ~~ / 'System time (seconds):' \s* $<s>=[\d+ ['.' \d+]?] / ?? +$<s> !! 0;
        $cpu = ($u + $s) * 1000;
    }
    if $err ~~ / 'Maximum resident set size (kbytes):' \s* $<k>=[\d+] / {
        $rss = +$<k>;
    }
    ($cpu.defined && $rss.defined) ?? { cpu => $cpu / $reps, rss => $rss } !! {}
}

sub median(@ms --> Numeric) {
    my @s = @ms.sort;
    my $n = +@s;
    $n %% 2 ?? (@s[$n div 2 - 1] + @s[$n div 2]) / 2 !! @s[$n div 2]
}

my $tfh = $tsv-path ?? $tsv-path.IO.open(:w) !! Nil;
if $tfh {
    # The metadata every ledger row needs: what ran, on what, compiled by what.
    my $commit = do {
        my $p = run('git', '-C', ~$repo, 'rev-parse', 'HEAD', :out, :err);
        my $c = $p.out.slurp(:close).trim; $p.err.slurp(:close);
        $p.exitcode == 0 ?? $c !! ''
    };
    my $rakudo-v = first-line([$RAKUDO, '--version']);
    my $mutsu-v  = $MUTSU.defined ?? first-line([$MUTSU, '--version']) !! '';
    my $rakupp-v = first-line([$RAKUPP, '--version']);
    my $cpu = do {
        my $p = run('sysctl', '-n', 'machdep.cpu.brand_string', :out, :err);
        my $c = $p.out.slurp(:close).trim; $p.err.slurp(:close);
        if $p.exitcode != 0 || !$c {
            # Split across statements on purpose: as one chained ternary, with
            # `.split(':')` continued onto its own line, Rakudo ends the
            # expression before the `[1]` and dies with "Missing infix inside
            # []" — so `raku -c tools/run-bench.raku` failed even though rakupp
            # parsed it. This file documents that it runs under BOTH engines.
            my $model = '/proc/cpuinfo'.IO.e
                        ?? ('/proc/cpuinfo'.IO.lines.first(*.starts-with('model name')) // '')
                        !! '';
            $c = ($model ?? ($model.split(':')[1] // '') !! '').trim;
        }
        $c
    };
    # Mirror main.cpp's --exe compiler choice: $CXX, else clang++ on PATH, else c++.
    my $cxx = '';
    for (%*ENV<CXX> // Empty), 'clang++', 'c++' -> $cand {
        my $line = first-line([$cand, '--version']);
        if $line { $cxx = "$cand: $line"; last }
    }
    $tfh.say: "# date={Date.today}";
    $tfh.say: "# rakupp_commit=$commit";
    $tfh.say: "# rakupp_version=$rakupp-v";
    $tfh.say: "# rakudo_version=$rakudo-v";
    $tfh.say: "# mutsu_version=$mutsu-v";
    $tfh.say: "# cpu=$cpu";
    $tfh.say: "# cxx=$cxx";
    $tfh.say: ('kernel',
               'interp_min_ms', 'interp_med_ms', 'native_min_ms', 'native_med_ms',
               'mutsu_min_ms', 'mutsu_med_ms',
               'rakudo_min_ms', 'rakudo_med_ms', 'perl_min_ms', 'perl_med_ms',
               'flags').join("\t");
}

my $mismatch = False;
my @rusage-rows;   # one per kernel, filled only under --rusage
printf "%-12s %10s %10s %10s %10s %10s   %s\n", 'benchmark', 'interp', 'native', 'mutsu', 'rakudo', 'perl', 'note';
for @benches -> %b {
    my $path = $bench.add(%b<file>).Str;
    my $nbin = "/tmp/rakupp-bench-$*PID-{%b<name>}"; # unique per run: macOS wedges re-execs of an overwritten exe path
    my $ppath = %b<perl> ?? $bench.add(%b<perl>).Str !! Str;

    # Correctness gate: every engine must emit byte-identical stdout, else the
    # timings aren't a like-for-like comparison. Rakudo is the oracle; if it
    # isn't available, Raku++'s interpreter is the reference instead.
    my $built = compile-native($path, $nbin);
    my $oi = capture([$RAKUPP, $path]);
    my $on = $built ?? capture([$nbin]) !! Str;
    my $or = capture([$RAKUDO, $path]);
    my $om = $MUTSU.defined ?? capture([$MUTSU, $path]) !! Str;
    my $op = $ppath.defined ?? capture([$PERL, $ppath]) !! Str;
    my $oracle = $or.defined ?? 'rakudo' !! 'interp';
    my $ref    = $or // $oi;
    my @bad;
    @bad.push('interp did not run')        unless $oi.defined;
    @bad.push('native did not run')        if $built && !$on.defined;
    @bad.push('perl did not run')          if $ppath.defined && !$op.defined;
    @bad.push("interp ≠ $oracle")          if $oi.defined && $or.defined && $oi ne $or;
    @bad.push("native ≠ $oracle")          if $on.defined && $ref.defined && $on ne $ref;
    @bad.push("perl ≠ $oracle")            if $op.defined && $ref.defined && $op ne $ref;
    # A third-party engine's disagreement is recorded next to the row and stays
    # out of @bad: it must not turn our own correctness gate red.
    my @othereng;
    @othereng.push('mutsu did not run')     if $MUTSU.defined && !$om.defined;
    @othereng.push("mutsu ≠ $oracle")       if $om.defined && $ref.defined && $om ne $ref;
    my $flag = '';
    if @bad { $mismatch = True; $flag = "   ⚠ {@bad.join('; ')}"; }
    $flag ~= "   (mutsu: {@othereng.join('; ')})" if @othereng;

    # One lane per engine that produced output; each measured round times every
    # lane once, back to back, warm-up round discarded.
    my @lanes;
    @lanes.push: 'interp' => [$RAKUPP, $path]              if $oi.defined;
    @lanes.push: 'native' => [$nbin]                       if $built && $on.defined;
    @lanes.push: 'mutsu'  => [$MUTSU, $path]               if $om.defined;
    @lanes.push: 'rakudo' => [$RAKUDO, $path]              if $or.defined;
    @lanes.push: 'perl'   => [$PERL, $ppath]               if $ppath.defined && $op.defined;
    my %times;
    for ^$RUNS -> $i {
        for @lanes -> $lane {
            my @cmd = |$lane.value;
            my $t0 = now;
            run(|@cmd, :out).out.slurp(:close);   # drain stdout => waits for exit
            %times{$lane.key}.push((now - $t0) * 1000) if $i > 0;
        }
    }
    # A SEPARATE pass, after the timing rounds: every run here is wrapped in
    # /usr/bin/time, and that wrapper's own fork/exec would otherwise land
    # inside the wall-clock figures the rest of this file publishes.
    if $rusage {
        my %ru;
        for @lanes -> $lane {
            # Enough repetitions to clear ~80ms of CPU, from the wall time this
            # lane just recorded: a 300ms kernel needs one, a 7ms kernel twelve.
            # Capped so a pathologically fast lane cannot spawn hundreds.
            my $wall = %times{$lane.key} ?? %times{$lane.key}.min !! 100;
            my $reps = $wall > 0 ?? min(20, max(1, ceiling(80 / $wall))) !! 1;
            my (@cpu, @rss);
            for ^3 {
                my %r = rusage-run($lane.value, $reps);
                if %r { @cpu.push(%r<cpu>); @rss.push(%r<rss>) }
            }
            %ru{$lane.key} = { cpu => @cpu.min, rss => @rss.min } if @cpu;
        }
        @rusage-rows.push: { name => %b<name>, ru => %ru };
    }

    my sub cell(Str $k) { %times{$k} ?? sprintf('%.1fms', %times{$k}.min) !! 'n/a' }
    my $interp = cell('interp');
    my $native = cell('native');
    my $mutsu  = $MUTSU.defined ?? cell('mutsu') !! '—';
    my $rakudo = cell('rakudo');
    my $perl   = $ppath.defined ?? cell('perl') !! '—';
    printf "%-12s %10s %10s %10s %10s %10s   %s%s\n", %b<name>, $interp, $native, $mutsu, $rakudo, $perl, %b<note>, $flag;

    if $tfh {
        my sub pair(Str $k) {
            %times{$k} ?? (sprintf('%.1f', %times{$k}.min), sprintf('%.1f', median(%times{$k})))
                       !! ('', '')
        }
        my @all = |@bad, |@othereng;
        $tfh.say: (%b<name>,
                   |pair('interp'), |pair('native'), |pair('mutsu'),
                   |pair('rakudo'), |pair('perl'),
                   (@all ?? @all.join('; ') !! 'ok')).join("\t");
    }
}
$tfh andthen .close;

if @rusage-rows {
    # Two figures per lane: CPU is user+sys, so a lane that uses more than one
    # core reads ABOVE its own wall time — that is the point of showing it next
    # to the first table rather than instead of it. Peak RSS is the high-water
    # mark of the process, which for the native lane includes no interpreter.
    my @lanes = <interp native mutsu rakudo perl>;
    say '';
    say '# CPU time (user+sys) and peak RSS — min of 3 runs, measured separately';
    printf "%-12s %s\n", 'benchmark',
           @lanes.map({ sprintf '%18s', $_ }).join;
    for @rusage-rows -> %row {
        printf "%-12s %s\n", %row<name>,
               @lanes.map(-> $l {
                   my %r = %row<ru>{$l} // {};
                   sprintf '%18s', %r ?? sprintf('%.0fms/%.1fMB', %r<cpu>, %r<rss> / 1024)
                                      !! '—'
               }).join;
    }
}

if $mismatch {
    note '';
    note '⚠ OUTPUT MISMATCH — a flagged engine disagreed with the reference; those rows are not a like-for-like comparison.';
    exit 1;
}
say '';
say '# all engines produced identical output';
