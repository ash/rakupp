#!/usr/bin/env raku
# Benchmark harness — times each program in tools/bench/ three ways and prints a
# comparison table:
#   * interp — Raku++ interpreting the source
#   * native — Raku++ `--exe` (transpiled to C++, compiled to a native binary)
#   * rakudo — Rakudo interpreting the source
#
# Run it with Raku++ (dogfooding, matches run-roast.raku):
#
#     ./build/rakupp tools/run-bench.raku
#
# It also runs under Rakudo (`raku tools/run-bench.raku`). Either way the runner
# only spawns each engine as a fresh subprocess and times it, so the language it
# is written in does not favour any contestant.
#
# Override the binaries via environment:
#     RAKUPP=/path/to/rakupp RAKUDO=raku ./build/rakupp tools/run-bench.raku

my $tools = $*PROGRAM.absolute.IO.parent;   # tools/
my $repo  = $tools.parent;                  # repo root
my $bench = $tools.add('bench');            # benchmark programs live here
my $RAKUPP = %*ENV<RAKUPP> // $repo.add('build/rakupp').Str;
my $RAKUDO = %*ENV<RAKUDO> // 'raku';
my $PERL   = %*ENV<PERL>   // 'perl';   # only used by benches that ship a .pl twin

# A binary built for ANOTHER ARCHITECTURE runs under translation, which costs
# a uniform 1.7-2x — perf-guard refuses that binary; this harness silently
# benchmarked it (a stale x86_64 build/ beside a build-arm64/ inflated every
# interp row ~1.9x and startup 8x, and the numbers nearly shipped in
# BENCHMARKS.md at v3.14.0). Same guard, same wording, same remedy.
unless $*DISTRO.is-win {
    my $s = run('sysctl', '-n', 'hw.optional.arm64', :out, :err);
    my $host = $s.out.slurp(:close).trim eq '1' ?? 'arm64' !! do {
        $s.err.slurp(:close);
        my $u = run('uname', '-m', :out, :err);
        my $m = $u.out.slurp(:close).trim; $u.err.slurp(:close);
        $m
    };
    my $f = run('file', '-b', $RAKUPP, :out, :err);
    my $desc = $f.out.slurp(:close); $f.err.slurp(:close);
    my $bin = $desc.contains('arm64') ?? 'arm64'
           !! $desc.contains('x86_64') ?? 'x86_64' !! '';
    if $bin && $host && $bin ne $host {
        note "run-bench REFUSED — $RAKUPP is $bin on a $host host.";
        note "It would be measured under translation, which costs 1.7-2x uniformly.";
        note "Build for this machine, or point RAKUPP at the $host binary.";
        exit 2;
    }
}
my $RUNS   = 7;   # 1 warm-up run (discarded) + 6 measured

my @benches =
    %( :name<startup>,  :file("startup.raku"),  :note('hello world (startup-dominated)') ),
    %( :name<loopsum>,  :file("loopsum.raku"),  :note('sum 1 .. 1_000_000 in a for loop') ),
    %( :name<fib>,      :file("fib.raku"),      :note('naïve recursive fib(29)') ),
    %( :name<strcat>,   :file("strcat.raku"),   :note('50_000 string concatenations') ),
    %( :name<arrayops>, :file("arrayops.raku"), :note('grep+map+sum over 200_000') ),
    %( :name<sortnums>, :file("sortnums.raku"), :note('sort 50_000 integers') ),
    %( :name<regex>,    :file("regex.raku"),    :note('50_000 regex matches') ),
    %( :name<hash>,     :file("hash.raku"),     :note('100_000 hash increments') ),
    %( :name<hashfill>, :file("hashfill.raku"), :perl("hashfill.pl"),
       :note('200k-key hash fill + values sweep + 50k-append string build') ),
    %( :name<bigint>,   :file("bigint.raku"),   :note('factorial(5000) via BigInt multiply') ),
    %( :name<streq>,    :file("streq.raku"),    :note('1M string eq/lt comparisons') );

# Best (minimum) wall-clock over the measured runs, in milliseconds.
sub measure(@cmd --> Numeric) {
    my @times;
    for ^$RUNS -> $i {
        my $t0 = now;
        run(|@cmd, :out).out.slurp(:close);   # drain stdout => waits for exit
        my $ms = (now - $t0) * 1000;
        @times.push($ms) if $i > 0;           # drop the warm-up run
    }
    @times.min;
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

my $mismatch = False;
printf "%-12s %10s %10s %10s %10s   %s\n", 'benchmark', 'interp', 'native', 'rakudo', 'perl', 'note';
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
    my $flag = '';
    if @bad { $mismatch = True; $flag = "   ⚠ {@bad.join('; ')}"; }

    my $interp = sprintf '%.1fms', measure([$RAKUPP, $path]);
    my $native = $built     ?? sprintf('%.1fms', measure([$nbin]))    !! 'n/a';
    my $rakudo = $or.defined ?? sprintf('%.1fms', measure([$RAKUDO, $path])) !! 'n/a';
    my $perl   = !$ppath.defined ?? '—'
              !! $op.defined     ?? sprintf('%.1fms', measure([$PERL, $ppath]))
              !! 'n/a';

    printf "%-12s %10s %10s %10s %10s   %s%s\n", %b<name>, $interp, $native, $rakudo, $perl, %b<note>, $flag;
}

if $mismatch {
    note '';
    note '⚠ OUTPUT MISMATCH — a flagged engine disagreed with the reference; those rows are not a like-for-like comparison.';
    exit 1;
}
say '';
say '# all engines produced identical output';
