# BROKE: a test file that died or ran short exited with a different status
# from Rakudo's. rakupp checked the plan first (`plan 3; ok 0; ok 0` exited
# 255, Rakudo 2), and a `die` skipped Test's END altogether: the status stayed
# the die's 1, where Rakudo's Test overrides it (255 for `plan 2; ok 1; die`,
# 3 for three failures and a die), and the "# You planned …" summary was not
# printed. An explicit `exit 0` after a failure exited 255, and `ok` after a
# no-plan `done-testing` counted as a plan mismatch. FastRoast found about
# 40 files per sample whose only difference was this status.
# FIXED: the run ends the way Rakudo's Test END does. An `exit` from anywhere
# (mainline, a sub, MAIN, LEAVE, END, CATCH) keeps its status. Otherwise, any
# failed test makes the status the number of failures, capped at 254; else
# a declared plan that the run count misses makes it 255; else a `die` keeps
# its 1. TODO failures do not count; a failed subtest counts once. The
# summary is printed after a `die` as well. Every expectation below was read
# off Rakudo 2026.09.
#
# Contract: exit 0 + last line PASS.

my @fail;
sub status(Str $code, Int $want) {
    my $p = run $*EXECUTABLE, '-e', $code, :out, :err;
    $p.out.slurp(:close);
    $p.err.slurp(:close);
    @fail.push("$code.raku(): exit {$p.exitcode}, want $want")
        unless $p.exitcode == $want;
}

# ---- failures come before the plan ------------------------------------------
status 'use Test; plan 3; ok 0; ok 0;',                 2;
status 'use Test; plan 3; ok 0; ok 0; ok 0;',           3;
status 'use Test; plan 300; ok 0 for ^260;',            254;
status 'use Test; plan 255; ok 0 for ^255;',            254;
status 'use Test; plan 1; ok 1; END { ok 0 }',          1;
status 'use Test; plan 3; ok 0; ok 1; done-testing;',   1;

# ---- a short or long plan with nothing failed --------------------------------
status 'use Test; plan 2; ok 1;',                       255;
status 'use Test; plan 2; ok 1; ok 1; ok 1;',           255;
status 'use Test; plan 3; ok 1; ok 1; done-testing;',   255;
status 'use Test; ok 0; ok 0; done-testing;',           2;
status 'use Test; ok 1; ok 1;',                         0;

# ---- a no-plan done-testing is not a plan ------------------------------------
status 'use Test; ok 1; done-testing; ok 1;',           0;
status 'use Test; ok 0; done-testing; ok 1;',           1;

# ---- Test's status overrides a die's -----------------------------------------
status 'use Test; plan 2; ok 1; die "x"',               255;
status 'use Test; plan 3; die "x"',                     255;
status 'use Test; plan 5; ok 0; ok 0; ok 0; die "x"',   3;
status 'use Test; plan 2; ok 0; ok 0; die "x"',         2;
status 'use Test; ok 0; ok 0; die "x"',                 2;
status 'use Test; ok 0; ok 0; done-testing; die "x"',   2;
status 'use Test; plan 3; ok 1; ok 1; fail "x"',        255;
status 'use Test; plan 2; ok 1; sub MAIN() { die "m" }', 255;
status 'use Test; plan 2; ok 1; die X::AdHoc.new(payload => "y")', 255;
# …and when Test has nothing to say, the die's own 1 stands
status 'use Test; plan 2; ok 1; ok 1; die "x"',         1;
status 'use Test; ok 1; die "x"',                       1;
status 'die "x"',                                       1;

# ---- an explicit exit keeps its status ---------------------------------------
status 'use Test; plan 2; ok 0; exit 0',                0;
status 'use Test; plan 2; ok 0; ok 0; ok 0; exit 0',    0;
status 'use Test; plan 2; ok 0; ok 0; exit 3',          3;
status 'use Test; plan 2; ok 1; exit 3',                3;
status 'use Test; plan 2; ok 1; sub f { exit 0 }; f;',  0;
status 'use Test; plan 2; ok 0; END { exit 0 }',        0;
status 'use Test; plan 2; ok 0; ok 0; LEAVE { exit 0 }', 0;
status 'use Test; plan 2; ok 0; ok 0; sub MAIN() { exit 0 }', 0;
status 'use Test; plan 2; subtest "s" => { ok 0; exit 6 }; ok 1;', 6;
status 'use Test; plan 2; ok 1; bail-out "b"',          255;
# …while a die in an END is not one
status 'use Test; plan 2; ok 0; ok 0; END { die "e" }', 2;

# ---- TODO failures do not count -----------------------------------------------
status 'use Test; plan 2; todo "t"; ok 0; ok 1;',       0;
status 'use Test; plan 3; todo "t"; ok 0; ok 0;',       1;
status 'use Test; plan 4; todo "t", 2; ok 0; ok 0; die "x"', 255;
status 'use Test; plan 2; todo "t"; ok 0; die "x"',     255;
status 'use Test; plan 1; todo "t"; ok 0; die "x"',     1;

# ---- a failed subtest counts once ---------------------------------------------
status 'use Test; plan 3; subtest "s" => { ok 0; ok 0 };', 1;
status 'use Test; plan 2; subtest "s" => { ok 0 }; ok 1;', 1;
status 'use Test; plan 1; todo "t"; subtest "s" => { ok 0 };', 0;
status 'use Test; plan 2; subtest "s" => { plan 2; ok 1; }; ok 1;', 1;

# ---- the summary still prints after a die -------------------------------------
{
    my $p = run $*EXECUTABLE, '-e', 'use Test; plan 3; die "x"', :out, :err;
    my $out = $p.out.slurp(:close);
    $p.err.slurp(:close);
    @fail.push("no plan summary on stdout after a die: {$out.raku}")
        unless $out.contains('# You planned 3 tests, but ran 0');
    $p = run $*EXECUTABLE, '-e', 'use Test; plan 5; ok 0; die "x"', :out, :err;
    $p.out.slurp(:close);
    my $err = $p.err.slurp(:close);
    @fail.push("no plan summary on stderr after a die: {$err.raku}")
        unless $err.contains('# You planned 5 tests, but ran 1');
}

if @fail {
    note "FAILED: @fail.join('; ')";
    say 'FAIL';
}
else {
    say 'PASS';
}
