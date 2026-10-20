#!/usr/bin/env rakupp
# The `--exe` leg of Test's end of run: a compiled `use Test` program must exit
# with the status the interpreter gives it and print the same summary.
#
# Each program below is compiled twice (`--exe` and `--exe -O`), and each binary
# is held to the SAME rakupp interpreting the same file: the exit status, the
# whole of stdout, and the `#` lines of stderr — Test's diagnostics ("# Failed
# test … line N", "# You planned …", "# Looks like you failed …"). The rest of
# stderr is left out: a native die prints its message without the backtrace.
# The interpreter is the oracle, as it is for t/exe/run.raku, and
# t/regression/test-exit-status.raku holds the interpreter to Rakudo on the
# same programs.
#
# Unlike t/exe/run.raku this is strict on compiled-vs-interpreted, which is
# the point: before 2026-10-19 a natively compiled Test program never ran
# Test's end of run at all (`plan 3; ok 0; ok 0` exited 0, with no summary),
# and every failure it reported was "line 0". A program the backend refuses is
# bundled and runs the interpreter's own end of run; the gate still compares
# it, and says how many of each it saw, so a backend that starts refusing
# everything cannot pass by bundling.
#
#   rakupp t/exe/test-status.raku          # ~2 min: two C++ compiles per program
#
# Exit 1 on any disagreement, or when fewer than half the programs compile
# natively.

my @programs =
    # failures come before the plan
    'use Test; plan 3; ok 0; ok 0;',
    'use Test; plan 3; ok 0; ok 0; ok 0;',
    'use Test; plan 300; ok 0 for ^260;',
    'use Test; plan 1; ok 1; END { ok 0 }',
    'use Test; plan 3; ok 0; ok 1; done-testing;',
    # a short or long plan with nothing failed
    'use Test; plan 2; ok 1;',
    'use Test; plan 2; ok 1; ok 1; ok 1;',
    'use Test; plan 3; ok 1; ok 1; done-testing;',
    'use Test; ok 0; ok 0; done-testing;',
    'use Test; ok 1; ok 1;',
    'use Test; ok 1; done-testing; ok 1;',
    'use Test; ok 0; done-testing; ok 1;',
    # Test's status overrides a die's
    'use Test; plan 2; ok 1; die "x"',
    'use Test; plan 3; die "x"',
    'use Test; plan 5; ok 0; ok 0; ok 0; die "x"',
    'use Test; ok 0; ok 0; die "x"',
    'use Test; plan 3; ok 1; ok 1; fail "x"',
    'use Test; plan 2; ok 1; sub MAIN() { die "m" }',
    'use Test; plan 2; ok 1; ok 1; die "x"',
    'die "x"',
    # an explicit exit keeps its status
    'use Test; plan 2; ok 0; exit 0',
    'use Test; plan 2; ok 0; ok 0; exit 3',
    'use Test; plan 2; ok 1; sub f { exit 0 }; f;',
    'use Test; plan 2; ok 0; END { exit 0 }',
    'use Test; plan 2; ok 0; ok 0; sub MAIN() { exit 0 }',
    'use Test; plan 2; ok 1; bail-out "b"',
    'use Test; plan 2; ok 0; ok 0; END { die "e" }',
    # TODO failures do not count; a failed subtest counts once
    'use Test; plan 2; todo "t"; ok 0; ok 1;',
    'use Test; plan 3; todo "t"; ok 0; ok 0;',
    'use Test; plan 4; todo "t", 2; ok 0; ok 0; die "x"',
    'use Test; plan 3; subtest "s" => { ok 0; ok 0 };',
    'use Test; plan 1; todo "t"; subtest "s" => { ok 0 };',
    ;
# each failure names its own line — in the mainline, in a sub, and as a
# routine's last statement
@programs.push: q:to/END/;
    use Test;
    plan 4;
    ok 1;
    ok 0, 'third';
    sub f($x) {
        is $x, 2, 'in f';
    }
    f(1);
    sub g { nok 1 }
    g();
    is-deeply [1], [2];
    END

my $rakupp = $*EXECUTABLE;
# Resolved: a compiled binary names its source by its real path, so on macOS
# (where $TMPDIR is under the /var -> /private/var link) the interpreter must
# be handed that same path, or every diagnostic differs in its file name.
my $tmp = $*TMPDIR.resolve.add("rakupp-exe-test-status-$*PID");
$tmp.mkdir;
END { if $tmp.e { for $tmp.dir { .unlink }; $tmp.rmdir } }

# Bounded, with a closed stdin, as t/exe/run.raku runs everything.
my $LIMIT = 60;
sub bounded(*@cmd) {
    my $p = Proc::Async.new(|@cmd);
    my ($o, $e) = '', '';
    $p.stdout.tap({ $o ~= $_ });
    $p.stderr.tap({ $e ~= $_ });
    my $done = $p.start;
    await Promise.anyof($done, Promise.in($LIMIT));
    unless $done { $p.kill: SIGKILL; try await $done; return Nil }
    my $rc = (try await $done).exitcode;
    ($rc, $o, $e)
}
sub diag-lines($err) { $err.lines.grep(*.trim-leading.starts-with('#')).join("\n") }

my ($native, $bundled, $n) = 0, 0, 0;
my @bad;
for @programs.kv -> $i, $code {
    my $src = $tmp.add("case-$i.raku");
    $src.spurt: $code.ends-with("\n") ?? $code !! "$code\n";
    my $what = $code.lines.join(' ⏎ ');
    my ($irc, $iout, $ierr) = bounded($rakupp.Str, $src.Str) // (Nil, '', '');
    for [], ['-O'] -> @flags {
        $n++;
        my $bin = $tmp.add("case-$i.bin");
        $bin.unlink if $bin.e;
        my ($crc, $cout, $cerr) = bounded($rakupp.Str, '--exe', |@flags, $src.Str, '-o', $bin.Str) // (Nil, '', '');
        unless ($crc // -1) == 0 && $bin.e {
            @bad.push: "$what (--exe @flags[]): did not compile: {($cout ~ $cerr).lines.tail(2).join(' | ')}";
            next;
        }
        ($cout ~ $cerr).contains('(native)') ?? $native++ !! $bundled++;
        my ($rc, $out, $err) = bounded($bin.Str) // (Nil, '', '');
        my @why;
        @why.push: "exit {$rc // 'timeout'}, interpreted {$irc // 'timeout'}" unless $rc.defined && $irc.defined && $rc == $irc;
        @why.push: "stdout {$out.raku}, interpreted {$iout.raku}" unless $out eq $iout;
        @why.push: "stderr diagnostics {diag-lines($err).raku}, interpreted {diag-lines($ierr).raku}"
            unless diag-lines($err) eq diag-lines($ierr);
        @bad.push: "$what (--exe @flags[]): @why.join('; ')" if @why;
        $bin.unlink if $bin.e;
    }
}

say "1..1";
my $enough = $native * 2 >= $n;
if @bad || !$enough {
    say "not ok 1 - compiled Test programs end as interpreted ones do";
    note "  $_" for @bad;
    note "  only $native of $n binaries were native: the gate is not testing the backend" unless $enough;
}
else {
    say "ok 1 - compiled Test programs end as interpreted ones do";
}
say "# $n binaries: $native native, $bundled bundled";
exit @bad || !$enough ?? 1 !! 0;
