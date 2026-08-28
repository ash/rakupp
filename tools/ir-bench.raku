#!/usr/bin/env rakupp
# ir-bench.raku — compare TWO builds of rakupp on the same kernels, the way
# docs/dev/experiments/PERF-CAMPAIGN.md says a change must be measured:
#
#   * alternating round by round, not all-of-A then all-of-B (absolute times
#     drift several percent over minutes; measuring one build then the other
#     is equally consistent with the machine having been quieter the second
#     time),
#   * with a CONTROL kernel the change provably cannot touch, in the same run
#     (`bigint` — its time is inside BigInt multiply, not the interpreter loop),
#   * and with the two extra kernels perf-guard is blind to: `method`
#     (dispatch-dominated) and `call` (non-recursive sub calls). The
#     invocant-by-const-reference change measured -3.4% and was invisible to
#     all four perf-guard kernels; that is a coverage gap, not an absence of
#     effect.
#
# This does NOT replace tools/perf-guard.raku. perf-guard is the RELEASE GATE
# and compares one build against a recorded baseline; this compares two builds
# that exist right now, which is what every phase of docs/dev/plans/IR-PLAN.md
# needs.
#
# Usage:
#   rakupp tools/ir-bench.raku BIN_A BIN_B [ROUNDS]
#
#   rakupp tools/ir-bench.raku build-arm64/rakupp build-ir/rakupp 5

my $A     = @*ARGS[0] // '';
my $B     = @*ARGS[1] // '';
my $ROUNDS = (@*ARGS[2] // 3).Int;

unless $A && $B {
    note "usage: rakupp tools/ir-bench.raku BIN_A BIN_B [ROUNDS]";
    exit 2;
}
for $A, $B -> $bin {
    unless $bin.IO.e {
        note "missing binary: $bin";
        exit 2;
    }
}

# The trap this machine sets: `cmake -S . -B build` here is driven by an
# x86_64 cmake under Rosetta, so it produces an x86_64 binary that runs ~2x
# slower than the arm64 one. Comparing across architectures measures Rosetta,
# not the change. perf-guard refuses the same way.
sub arch-of(Str $bin --> Str) {
    my $p = run('file', '-b', $bin, :out, :err);
    my $o = $p.out.slurp(:close);
    $p.err.slurp(:close);
    return 'arm64'  if $o.contains('arm64');
    return 'x86_64' if $o.contains('x86_64');
    return 'unknown';
}
my ($archA, $archB) = arch-of($A), arch-of($B);
if $archA ne $archB {
    note "REFUSING: $A is $archA but $B is $archB.";
    note "Comparing across architectures measures Rosetta, not the change.";
    exit 2;
}

# name => [ source, is-control ]
my %kernels =
    fib     => 'sub fib($n) { $n < 2 ?? $n !! fib($n-1) + fib($n-2) }; say fib(29);',
    asg     => 'my $x = 0; for ^2_000_000 { $x = $x + 1 }; say $x;',
    loopsum => 'my $t = 0; for 1 .. 1_000_000 { $t += $_ }; say $t;',
    hash    => 'my %c; for 1 .. 100_000 { %c{$_ % 1_000}++ }; say %c.elems;',
    method  => 'my $s = "abcdef"; my $n = 0; for ^3_000_000 { my $x = $s.item; $n = $n + 1 }; say $n;',
    call    => 'sub id($x) { $x }; my $n = 0; for ^2_000_000 { $n = id($n) + 1 }; say $n;',
    bigint  => 'my $f = 1; for 1 .. 3_000 { $f = $f * $_ }; say $f.chars;';

my @order   = <fib asg loopsum hash method call bigint>;
my $control = 'bigint';

# Kernels live in files: -e quoting differences between shells are one more
# thing that can differ between two runs, and a file cannot.
my %path;
for @order -> $k {
    my $p = "/tmp/ir-bench-{$*PID}-$k.raku";
    $p.IO.spurt(%kernels{$k} ~ "\n");
    %path{$k} = $p;
}

sub run-once(Str $bin, Str $path --> Str) {
    my $p = run($bin, $path, :out, :err);
    my $out = $p.out.slurp(:close);
    $p.err.slurp(:close);
    return $p.exitcode == 0 ?? $out !! Str;
}

sub time-once(Str $bin, Str $path --> Numeric) {
    my $t0 = now;
    run-once($bin, $path);
    return (now - $t0) * 1000;
}

# Correctness gate first: two builds that disagree are not comparable, and a
# timing table for a build that prints the wrong answer is worse than no table.
say "checking both builds agree on every kernel...";
my $bad = False;
for @order -> $k {
    my $oa = run-once($A, %path{$k});
    my $ob = run-once($B, %path{$k});
    unless $oa.defined && $ob.defined && $oa eq $ob {
        note "MISMATCH on $k: A={$oa // '<crash>'} B={$ob // '<crash>'}";
        $bad = True;
    }
}
if $bad {
    note "refusing to time builds that disagree.";
    exit 1;
}

my %best;
for @order -> $k {
    %best{$k} = { 'a' => Inf, 'b' => Inf };
}

# One warm-up round (discarded), then $ROUNDS measured rounds, A and B
# alternating WITHIN each round so drift hits both equally.
say "warm-up...";
for @order -> $k {
    time-once($A, %path{$k});
    time-once($B, %path{$k});
}

for 1 .. $ROUNDS -> $r {
    say "round $r of $ROUNDS...";
    for @order -> $k {
        my $ta = time-once($A, %path{$k});
        my $tb = time-once($B, %path{$k});
        %best{$k}<a> = $ta if $ta < %best{$k}<a>;
        %best{$k}<b> = $tb if $tb < %best{$k}<b>;
    }
}

say "";
say "A = $A ($archA)";
say "B = $B ($archB)";
say "best of $ROUNDS alternating rounds, ms";
say "";
printf "%-10s %10s %10s %9s   %s\n", 'kernel', 'A', 'B', 'B vs A', '';
for @order -> $k {
    my $a = %best{$k}<a>;
    my $b = %best{$k}<b>;
    my $d = $a > 0 ?? ($b - $a) / $a * 100 !! 0;
    my $tag = $k eq $control ?? 'CONTROL — must not move' !! '';
    printf "%-10s %10.1f %10.1f %+8.1f%%   %s\n", $k, $a, $b, $d, $tag;
}
say "";

my $cd = (%best{$control}<b> - %best{$control}<a>) / %best{$control}<a> * 100;
if $cd.abs > 3 {
    say "WARNING: the control kernel moved {$cd.round(1)}%. That is the machine,";
    say "not the change — re-run when it is quiet before believing any row above.";
}
else {
    say "control moved {$cd.round(1)}% — the machine held still; the rows above are the change.";
}

for @order -> $k { %path{$k}.IO.unlink }
