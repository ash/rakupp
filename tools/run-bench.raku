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
printf "%-12s %10s %10s %10s   %s\n", 'benchmark', 'interp', 'native', 'rakudo', 'note';
for @benches -> %b {
    my $path = $bench.add(%b<file>).Str;
    my $nbin = "/tmp/rakupp-bench-$*PID-{%b<name>}"; # unique per run: macOS wedges re-execs of an overwritten exe path

    # Correctness gate: every engine must emit byte-identical stdout, else the
    # timings aren't a like-for-like comparison. Rakudo is the oracle; if it
    # isn't available, Raku++'s interpreter is the reference instead.
    my $built = compile-native($path, $nbin);
    my $oi = capture([$RAKUPP, $path]);
    my $on = $built ?? capture([$nbin]) !! Str;
    my $or = capture([$RAKUDO, $path]);
    my $oracle = $or.defined ?? 'rakudo' !! 'interp';
    my $ref    = $or // $oi;
    my @bad;
    @bad.push('interp did not run')        unless $oi.defined;
    @bad.push('native did not run')        if $built && !$on.defined;
    @bad.push("interp ≠ $oracle")          if $oi.defined && $or.defined && $oi ne $or;
    @bad.push("native ≠ $oracle")          if $on.defined && $ref.defined && $on ne $ref;
    my $flag = '';
    if @bad { $mismatch = True; $flag = "   ⚠ {@bad.join('; ')}"; }

    my $interp = sprintf '%.1fms', measure([$RAKUPP, $path]);
    my $native = $built     ?? sprintf('%.1fms', measure([$nbin]))    !! 'n/a';
    my $rakudo = $or.defined ?? sprintf('%.1fms', measure([$RAKUDO, $path])) !! 'n/a';

    printf "%-12s %10s %10s %10s   %s%s\n", %b<name>, $interp, $native, $rakudo, %b<note>, $flag;
}

if $mismatch {
    note '';
    note '⚠ OUTPUT MISMATCH — a flagged engine disagreed with the reference; those rows are not a like-for-like comparison.';
    exit 1;
}
say '';
say '# all engines produced identical output';
