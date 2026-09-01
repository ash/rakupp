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
use lib $?FILE.IO.parent.add('lib').Str;
use Gate;
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
# All of it lives in tools/lib/Gate.rakumod now. For THIS gate a wrong-arch
# compiler is worse than a missing one: it compiles successfully and emits
# binaries that crash, so the failure arrives dressed as a correctness bug.
my %PICK = pick-rakupp($repo);
require-native(%PICK, :tool<run-optbench>, :verdict<INCONCLUSIVE>,
    :consequence('It would COMPILE successfully and emit binaries that crash here, so '
               ~ 'every kernel would report `--exe did not run` — which reads as a '
               ~ 'code-generator bug and is not one.'));
my $HOST   = %PICK<host>;
my $RAKUPP = %PICK<path>;
note provenance-line('run-optbench', %PICK);
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
    %( :name<bigmul>,      :note('10000! by `*=` — the bignum compound-assign lane, no -O route') ),
    %( :name<methodcalls>, :note('1M monomorphic method calls — not devirtualized yet') );

# @benches is hand-written (each kernel carries a note saying which pass it
# showcases), so it can drift from what is actually on disk — a file dropped
# into tools/optbench/ is never run and nothing says so. That is not
# hypothetical: findings/GATES-3.22.md Part C records a prove-gates plant that
# added a new kernel file, watched gate 4 stay green, and first reported MISSED
# against a gate that was working correctly. Cross-check the two.
{
    my @on-disk = $bench.dir.grep(*.extension eq 'raku').map(*.basename.subst(/'.raku'$/, '')).sort;
    my @listed  = @benches.map({ ~.<name> }).sort;
    my @unrun   = @on-disk.grep({ $_ !(elem) @listed });
    my @missing = @listed.grep({ $_ !(elem) @on-disk });
    if @unrun || @missing {
        note "run-optbench: tools/optbench/ and \@benches disagree.";
        note "  on disk but never run: @unrun.join(', ')"      if @unrun;
        note "  listed but no file:    @missing.join(', ')"    if @missing;
        note "A kernel this gate does not run is a kernel it cannot gate.";
        exit 2;
    }
}

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
    # PID-scoped: these were fixed paths, so two runs of this gate at once
    # compiled over each other's binaries and each measured the other's build.
    my $base = $*TMPDIR.add("rakupp-opt-{$*PID}-%b<name>-base").Str;
    my $opt  = $*TMPDIR.add("rakupp-opt-{$*PID}-%b<name>-O").Str;
    LEAVE { .IO.unlink for $base, $opt }

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
