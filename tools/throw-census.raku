#!/usr/bin/env rakupp
# The share of run time spent throwing C++ exceptions — interpreted and under
# `--exe` (V6-PLAN P0).
#
# tools/throw-census.c adds up, per process, the time from each __cxa_throw to
# the __cxa_begin_catch that ends it. This builds it, runs every program in
# tools/bench/ and a few control-flow shapes once under it per lane, times the
# same program without it (minimum of --runs), and prints the exception time
# as a share of that wall time.
#
#     ./build/rakupp tools/throw-census.raku
#     ./build/rakupp tools/throw-census.raku --only=fib,die-catch --no-interp
#
# Options:
#     --only=name,name   just these programs
#     --runs=N           timed runs per lane, minimum taken (default 3)
#     --no-interp        the `--exe` lane only
#
# Override the binary via RAKUPP=/path/to/rakupp. macOS only: the counter is a
# DYLD interposer.

my $repo = $*PROGRAM.absolute.IO.parent.parent;
use lib $?FILE.IO.parent.add('lib').Str;
use Gate;

my @only;
my $runs = 3;
my $want-interp = True;
for @*ARGS -> $a {
    if    $a.starts-with('--only=') { @only = $a.substr(7).split(',')>>.trim }
    elsif $a.starts-with('--runs=') { $runs = +$a.substr(7) }
    elsif $a eq '--no-interp'       { $want-interp = False }
    else { note "throw-census: unknown argument $a"; exit 2 }
}
unless $*DISTRO.name.lc.contains('macos' | 'darwin') {
    note "throw-census: macOS only (tools/throw-census.c is a DYLD interposer)";
    exit 2;
}

my %PICK = pick-rakupp($repo);
require-native(%PICK, :tool<throw-census>,
    :consequence('It would be timed under translation, which costs 1.7-2x uniformly.'));
my $RAKUPP = %PICK<path>;
note provenance-line('throw-census', %PICK);

# The bench kernels, and the control-flow shapes the kernels do not have. The
# guard-* shapes are perf-guard's mainnext, mainwhen and exprnext: the loop
# words at the mainline and as operands, at perf-guard's sizes.
my @progs = $repo.add('tools/bench').dir(test => *.ends-with('.raku')).sort.map({
    %( :name(.basename.subst(/'.raku' $/, '')), :file($_) )
});
my %shapes =
    die-catch   => 'my $n = 0; for ^200_000 { try { die "x" }; $n++ }; say $n;',
    catch-block => 'sub f($i) { die "odd" if $i % 2; $i; CATCH { default { 0 } } }; my $t = 0; $t += f($_) for ^200_000; say $t;',
    return-loop => 'sub f($n) { for ^$n -> $i { return $i if $i == 3 } }; my $t = 0; $t += f(10) for ^500_000; say $t;',
    last-nested => 'my $t = 0; for ^200_000 -> $i { for ^10 -> $j { last if $j == 4; $t++ } }; say $t;',
    when-sub    => 'sub g($i) { given $i % 3 { when 0 { 1 }; when 1 { 2 }; default { 3 } } }; my $t = 0; $t += g($_) for ^1_000_000; say $t;',
    guard-mainnext => 'my $n = 0; for ^10_000_000 { next if $_ % 2; $n = $n + 1 }; say $n;',
    guard-mainwhen => 'my $n = 0; for ^6_000_000 -> $i { given $i % 3 { when 0 { $n = $n + 1 }; when 1 { $n = $n + 2 }; default { $n = $n + 3 } } }; say $n;',
    guard-exprnext => 'sub f($i) { $i %% 2 ?? $i !! Nil }; sub h($i) { my $x = f($i) // return 0; $x }; my $a = 0; my $c = 0; for ^80_000 -> $i { my $x = f($i) // next; $a += $x }; for ^80_000 -> $i { f($i) or next; $c++ }; for ^80_000 -> $i { $a += h($i) }; say $a, " ", $c;';

my $tmp = $*TMPDIR.add("throw-census-{$*PID}");
mkdir $tmp;
END { try { .unlink for $tmp.dir; $tmp.rmdir } }

for %shapes.sort(*.key) -> (:$key, :$value) {
    my $f = $tmp.add("$key.raku");
    $f.spurt("$value\n");
    @progs.push: %( :name($key), :file($f) );
}
@progs = @progs.grep({ .<name> (elem) @only }) if @only;
unless @progs {
    note "throw-census: --only matched no programs";
    exit 2;
}

my $lib = $tmp.add('throw-census.dylib');
{
    my $p = run('cc', '-dynamiclib', '-O2', ~$repo.add('tools/throw-census.c'), '-lc++', '-o', ~$lib, :out, :err);
    my $err = $p.err.slurp(:close); $p.out.slurp(:close);
    unless $p.exitcode == 0 && $lib.e {
        note "throw-census: could not build tools/throw-census.c:\n$err";
        exit 2;
    }
}

# (throws, ns) for one run under the interposer; Nil when the run failed.
sub census(@cmd --> List) {
    my $p = run(|@cmd, :out, :err, :env(%(|%*ENV, DYLD_INSERT_LIBRARIES => ~$lib)));
    $p.out.slurp(:close);
    my $err = $p.err.slurp(:close);
    return (Nil, Nil) unless $p.exitcode == 0;
    $err ~~ / '--- throw census ---' \s* 'throws' \s+ $<t>=[\d+] .*? 'ns' \s+ $<ns>=[\d+] /
        ?? (+$<t>, +$<ns>) !! (Nil, Nil)
}
# Minimum wall time in ns over $runs runs, without the interposer.
sub wall(@cmd --> Numeric) {
    my @ns;
    for ^$runs {
        my $t0 = now;
        my $p = run(|@cmd, :out, :err);
        $p.out.slurp(:close); $p.err.slurp(:close);
        return Nil unless $p.exitcode == 0;
        @ns.push: (now - $t0) * 1e9;
    }
    @ns.min
}

sub cell($throws, $ns, $wall) {
    return sprintf('%28s', 'n/a') unless $throws.defined && $wall;
    sprintf('%9d %9.1fms %6.1f%%', $throws, $ns / 1e6, 100 * $ns / $wall)
}
say "# throws, time from throw to catch, and that time as a share of the wall time (min of $runs)";
say sprintf('%-17s', 'program')
  ~ ($want-interp ?? sprintf('%28s', 'interp: throws  time  share') !! '')
  ~ sprintf('%28s', '--exe: throws  time  share') ~ '  wall interp / exe';
for @progs -> %p {
    my $line = sprintf('%-17s', %p<name>);
    my ($wi, $we);
    if $want-interp {
        my @cmd = $RAKUPP, ~%p<file>;
        my ($t, $ns) = census(@cmd);
        $wi = wall(@cmd);
        $line ~= cell($t, $ns, $wi);
    }
    my $bin = $tmp.add("%p<name>-exe");
    my $c = run($RAKUPP, '--exe', ~%p<file>, '-o', ~$bin, :out, :err);
    $c.out.slurp(:close); $c.err.slurp(:close);
    if $c.exitcode == 0 && $bin.e {
        my ($t, $ns) = census([~$bin]);
        $we = wall([~$bin]);
        $line ~= cell($t, $ns, $we);
        $bin.unlink;
    }
    else { $line ~= sprintf('%28s', 'did not compile') }
    $line ~= sprintf('  %7s / %7s', ($wi ?? sprintf('%.0fms', $wi / 1e6) !! '-'),
                                  ($we ?? sprintf('%.0fms', $we / 1e6) !! '-'));
    say $line;
}
