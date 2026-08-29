#!/usr/bin/env raku
# Optimizer showcase — times each program in tools/optbench/ compiled two ways:
#
#   * --exe      Raku++ transpiled to C++ and compiled (the optimizer OFF)
#   * --exe -O   the same, with the `-O` codegen passes ON
#
# and prints the speed-up `-O` buys, plus Rakudo for reference. Before timing,
# every program is checked to produce identical output four ways — interp,
# `--exe`, `--exe -O`, and Rakudo (the oracle) — so the timings compare like for
# like; a divergent row is flagged and the run exits non-zero. See
# OPTIMIZATION.md for what each pass does.
#
# Dogfooded — run it with Raku++ (it also runs under Rakudo):
#
#     ./build/rakupp tools/run-optbench.raku
#
# Override binaries via env: RAKUPP=/path/to/rakupp RAKUDO=raku

my $tools  = $*PROGRAM.absolute.IO.parent;
my $repo   = $tools.parent;
my $bench  = $tools.add('optbench');
# Which rakupp compiles the kernels. An EXPLICIT RAKUPP is honoured as given;
# with none set the default SEARCHES and filters by ARCHITECTURE, because taking
# the first path that exists is a trap this repo has now hit three times. On the
# machine of record `build/` holds an x86_64 build beside a native
# `build-arm64/`, so the gate command exactly as RELEASING.md writes it compiled
# x86_64 binaries — which SIGSEGV under translation here. Every kernel then
# reports `--exe did not run`, which reads as a code-generator bug and is not
# one. (perf-guard had the same defect; so did rakujs/build.sh before it.)
sub host-arch(--> Str) {
    return '' if $*DISTRO.is-win;
    # hw.optional.arm64 is 1 on Apple Silicon even when the ASKING process is
    # translated — `uname -m` reports the caller's own architecture instead.
    my $s = run('sysctl', '-n', 'hw.optional.arm64', :out, :err);
    my $v = $s.out.slurp(:close).trim; $s.err.slurp(:close);
    return 'arm64' if $v eq '1';
    my $u = run('uname', '-m', :out, :err);
    my $m = $u.out.slurp(:close).trim; $u.err.slurp(:close);
    $m
}
sub binary-arch(Str $path --> Str) {
    my $f = run('file', '-b', $path, :out, :err);
    my $d = $f.out.slurp(:close); $f.err.slurp(:close);
    $d.contains('arm64') ?? 'arm64' !! $d.contains('x86_64') ?? 'x86_64' !! ''
}
my $HOST = host-arch();
my @candidates = <build/rakupp build-arm64/rakupp rakupp>.map({ $repo.add($_).Str });
my $RAKUPP = %*ENV<RAKUPP>;
my $picked-by-arch = False;
without $RAKUPP {
    my @runnable = @candidates.grep(*.IO.x);
    my $native = $HOST ?? @runnable.first({ binary-arch($_) eq $HOST }) !! Nil;
    $picked-by-arch = ?$native && @runnable && $native ne @runnable[0];
    $RAKUPP = $native // @runnable[0] // @candidates[0];
}
# A missing binary must stop the run: without this, every program "fails to
# compile" identically and the comparison looks like a real, reproducible bug
# (a stale build/ directory once sent a release bisect down exactly that path).
unless $RAKUPP.IO.x {
    note "run-optbench: no runnable binary at $RAKUPP";
    note "Searched: {@candidates.join(', ')}";
    note "Set RAKUPP=/path/to/rakupp, or build one of those.";
    exit 2;
}
# …and one built for another architecture is worse than missing: it compiles,
# and the binaries it emits crash, which looks like a correctness failure.
{
    my $bin = binary-arch($RAKUPP);
    if $bin && $HOST && $bin ne $HOST {
        note "run-optbench INCONCLUSIVE — $RAKUPP is $bin on a $HOST host.";
        note "The binaries it compiles would not run here. Point RAKUPP at the $HOST build.";
        exit 2;
    }
}
note "run-optbench: $RAKUPP"
   ~ ($picked-by-arch ?? "  (chosen over an earlier candidate: it is the $HOST build)" !! "");
my $RAKUDO = %*ENV<RAKUDO> // 'raku';
my $RUNS   = 6;   # 1 warm-up (discarded) + 5 measured

# name => one-line note on which pass it showcases
my @benches =
    %( :name<stringbuild>, :note('400k `~=` appends — in-place O(n) string build') ),
    %( :name<intsum>,      :note('5M int accumulation — inline + - *') ),
    %( :name<fibcalls>,    :note('fib(32) — direct-arity calls + inline < + -') ),
    %( :name<powmod>,      :note('1M `** 3` then `% 1000` — inline pow + mod') ),
    %( :name<sieve>,       :note('primes < 200k by trial division — inline * <= %%') ),
    # The three below are here to show where `-O` does NOT reach yet: each is the
    # measuring stick for one of the levers named in OPTIMIZATION.md's "Limits
    # and what's next", so the table reports the gap rather than only the wins.
    %( :name<nummath>,     :note('Mandelbrot escape count — Num math, no lane yet') ),
    %( :name<arrayidx>,    :note('2M @a[$i] read-modify-write — no element lane yet') ),
    %( :name<methodcalls>, :note('1M monomorphic method calls — not devirtualized yet') );

# Best (minimum) wall-clock over the measured runs, in milliseconds.
sub measure(@cmd --> Numeric) {
    my @t;
    for ^$RUNS -> $i {
        my $t0 = now;
        run(|@cmd, :out).out.slurp(:close);
        @t.push((now - $t0) * 1000) if $i > 0;
    }
    @t.min;
}

# Capture a command's stdout (trimmed) for the correctness check. Returns Str
# (undefined) if the program exits non-zero, so a crash is flagged rather than
# silently compared as empty output.
sub output-of(@cmd --> Str) {
    my $p = run(|@cmd, :out, :err);
    my $o = $p.out.slurp(:close); $p.err.slurp(:close);
    $p.exitcode == 0 ?? $o.trim !! Str;
}

# Compile a program with the given extra flags; True on success.
sub compile(Str $path, Str $out, *@flags --> Bool) {
    my $p = run($RAKUPP, '--exe', |@flags, $path, '-o', $out, :out, :err);
    $p.out.slurp(:close); $p.err.slurp(:close);
    $p.exitcode == 0;
}

printf "%-12s %9s %9s %8s %9s   %s\n",
       'benchmark', '--exe', '--exe -O', 'speedup', 'rakudo', 'showcases';
printf "%s\n", '-' x 92;

my @rows;
my $mismatch = False;
for @benches -> %b {
    my $path = $bench.add("%b<name>.raku").Str;
    my $base = "/tmp/rakupp-opt-%b<name>-base";
    my $opt  = "/tmp/rakupp-opt-%b<name>-O";

    unless compile($path, $base) && compile($path, $opt, '-O') {
        printf "%-12s %9s   (compile failed)\n", %b<name>, 'n/a';
        next;
    }

    # Correctness gate: interp, --exe, --exe -O, and Rakudo must all emit the
    # same output before the timings mean anything. Rakudo is the oracle when
    # available; otherwise the interpreter is.
    my $oi = output-of([$RAKUPP, $path]);    # interp
    my $gB = output-of([$base]);             # --exe
    my $gO = output-of([$opt]);              # --exe -O
    my $oR = output-of([$RAKUDO, $path]);    # rakudo
    my $oracle = $oR.defined ?? 'rakudo' !! 'interp';
    my $ref    = $oR // $oi;
    my @bad;
    @bad.push('interp did not run')   unless $oi.defined;
    @bad.push('--exe did not run')    unless $gB.defined;
    @bad.push('--exe -O did not run') unless $gO.defined;
    @bad.push("interp ≠ $oracle")     if $oi.defined && $oR.defined && $oi ne $oR;
    @bad.push("--exe ≠ $oracle")      if $gB.defined && $ref.defined && $gB ne $ref;
    @bad.push("--exe -O ≠ $oracle")   if $gO.defined && $ref.defined && $gO ne $ref;
    if @bad {
        $mismatch = True;
        printf "%-12s   ⚠ MISMATCH: %s\n", %b<name>, @bad.join('; ');
        next;
    }

    my $tB = measure([$base]);
    my $tO = measure([$opt]);
    my $tR = $oR.defined ?? measure([$RAKUDO, $path]) !! Numeric;
    my $sp = $tO > 0 ?? $tB / $tO !! 0;

    my $rak = $tR.defined ?? sprintf('%8.1fms', $tR) !! sprintf('%10s', 'n/a');
    printf "%-12s %8.1fms %8.1fms %7.1f× %s   %s\n",
           %b<name>, $tB, $tO, $sp, $rak, %b<note>;
    @rows.push: %( :name(%b<name>), :$sp );
}

if $mismatch {
    note '';
    note '⚠ OUTPUT MISMATCH — a flagged engine disagreed with the reference; those rows were skipped.';
    exit 1;
}
