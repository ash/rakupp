#!/usr/bin/env raku
# Optimizer showcase — times each program in tools/optbench/ compiled two ways:
#
#   * --exe      Raku++ transpiled to C++ and compiled (the optimizer OFF)
#   * --exe -O   the same, with the `-O` codegen passes ON
#
# and prints the speed-up `-O` buys, plus Rakudo for reference. Every program is
# first checked to produce identical output all three ways, so the timings
# compare like for like. See OPTIMIZATION.md for what each pass does.
#
# Dogfooded — run it with Raku++ (it also runs under Rakudo):
#
#     ./build/rakupp tools/run-optbench.raku
#
# Override binaries via env: RAKUPP=/path/to/rakupp RAKUDO=raku

my $tools  = $*PROGRAM.absolute.IO.parent;
my $repo   = $tools.parent;
my $bench  = $tools.add('optbench');
my $RAKUPP = %*ENV<RAKUPP> // $repo.add('build/rakupp').Str;
my $RAKUDO = %*ENV<RAKUDO> // 'raku';
my $RUNS   = 6;   # 1 warm-up (discarded) + 5 measured

# name => one-line note on which pass it showcases
my @benches =
    %( :name<stringbuild>, :note('400k `~=` appends — in-place O(n) string build') ),
    %( :name<intsum>,      :note('5M int accumulation — inline + - *') ),
    %( :name<fibcalls>,    :note('fib(32) — direct-arity calls + inline < + -') ),
    %( :name<powmod>,      :note('1M `** 3` then `% 1000` — inline pow + mod') ),
    %( :name<sieve>,       :note('primes < 200k by trial division — inline * <= %%') );

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

# Capture a command's stdout (trimmed), for the correctness check.
sub output-of(@cmd --> Str) {
    my $p = run(|@cmd, :out, :err);
    my $o = $p.out.slurp(:close); $p.err.slurp(:close);
    $o.trim;
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
for @benches -> %b {
    my $path = $bench.add("%b<name>.raku").Str;
    my $base = "/tmp/rakupp-opt-%b<name>-base";
    my $opt  = "/tmp/rakupp-opt-%b<name>-O";

    unless compile($path, $base) && compile($path, $opt, '-O') {
        printf "%-12s %9s   (compile failed)\n", %b<name>, 'n/a';
        next;
    }

    # Correctness gate: interp, --exe, --exe -O must all agree.
    my $want = output-of([$RAKUPP, $path]);
    my $gotB = output-of([$base]);
    my $gotO = output-of([$opt]);
    unless $gotB eq $want && $gotO eq $want {
        printf "%-12s   MISMATCH (interp=%s exe=%s exe-O=%s)\n", %b<name>, $want, $gotB, $gotO;
        next;
    }

    my $tB = measure([$base]);
    my $tO = measure([$opt]);
    my $tR = measure([$RAKUDO, $path]);
    my $sp = $tO > 0 ?? $tB / $tO !! 0;

    printf "%-12s %8.1fms %8.1fms %7.1f× %8.1fms   %s\n",
           %b<name>, $tB, $tO, $sp, $tR, %b<note>;
    @rows.push: %( :name(%b<name>), :$sp );
}
