# Regression: `fails-like` printed one flat `ok 1 - ` (empty description)
# where Rakudo's Test prints a subtest: "code returned a Failure", then a
# nested "Failure threw when sunk" ("code dies", "right exception type (T)",
# one ".KEY matches VALUE" per matcher). The verdict was already right; the
# per-test Roast count missed about four leaf tests per call (S32-num/base.t
# printed 76 leaf `ok` lines against Rakudo's 108). A code that throws is
# reported as "expected code to fail but it threw T instead". A Bool matcher
# is X::Match::Bool, in throws-like too.
#
# Contract: exit 0 + last line PASS. Runs under Rakudo unchanged; the
# expected TAP is Rakudo 2026.09's, checked on 2026-10-19.
my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}
sub tap($code) {
    state $n = 0;
    my $f = $*TMPDIR.add("rakupp-fails-like-{$*PID}-{$n++}.raku");
    $f.spurt("use Test;\n$code\n");
    my $p = run $*EXECUTABLE, $f, :out, :err;
    my $out = $p.out.slurp(:close);
    $p.err.slurp(:close);
    $f.unlink;
    $out.lines.List
}

ck(tap('sub f { fail "x" }; fails-like { f() }, X::AdHoc, message => "x";'),
   ('# Subtest: did we fails-like X::AdHoc?',
    '    1..2',
    '    ok 1 - code returned a Failure',
    '    # Subtest: Failure threw when sunk',
    '        1..3',
    '        ok 1 - code dies',
    '        ok 2 - right exception type (X::AdHoc)',
    '        ok 3 - .message matches x',
    '    ok 2 - Failure threw when sunk',
    'ok 1 - did we fails-like X::AdHoc?'),
   'a Failure that matches: the two-level subtest');
ck(tap('sub f { fail "x" }; fails-like { f() }, X::AdHoc, "my reason";').tail,
   'ok 1 - my reason', 'a reason names the subtest');
ck(tap('fails-like { die "x" }, X::AdHoc;'),
   ('# Subtest: did we fails-like X::AdHoc?',
    '    1..2',
    '    not ok 1 - expected code to fail but it threw X::AdHoc instead',
    '    ok 2 - # SKIP expected code to fail but it threw X::AdHoc instead',
    'not ok 1 - did we fails-like X::AdHoc?'),
   'a code that throws');
ck(tap('fails-like { 42 }, X::AdHoc;'),
   ('# Subtest: did we fails-like X::AdHoc?',
    '    1..2',
    '    not ok 1 - code returned a Failure',
    '    # Subtest: Failure threw when sunk',
    '        1..2',
    '        not ok 1 - code dies',
    '        ok 2 - # SKIP Code did not die, can not check exception',
    '    not ok 2 - Failure threw when sunk',
    'not ok 1 - did we fails-like X::AdHoc?'),
   'a code that answers no Failure');
ck(tap('sub f { fail "x" }; fails-like { f() }, X::Str::Numeric;').grep(*.contains('exception type')).List,
   ('        not ok 2 - right exception type (X::Str::Numeric)',), 'the wrong type');
ck(tap('fails-like { "msg".Failure }, X::AdHoc, message => "msg";').tail,
   'ok 1 - did we fails-like X::AdHoc?', 'a Failure made from a Str');
ck(tap('sub f { fail "x" }; fails-like { my $r = f(); $r.so; $r }, X::AdHoc;').tail,
   'not ok 1 - did we fails-like X::AdHoc?', 'a handled Failure does not throw when sunk');
ck(tap('sub f { fail "x" }; { fails-like { f() }, X::AdHoc, message => True; CATCH { default { say .^name } } }'),
   ('X::Match::Bool',), 'a Bool matcher is refused before the subtest');
ck(tap('{ throws-like { die "x" }, X::AdHoc, message => True; CATCH { default { say .^name } } }'),
   ('X::Match::Bool',), 'by throws-like as well');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
