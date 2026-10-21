# Regression: a string search's out-of-range start position fails with the
# exception Rakudo builds for it.
#
# `.index`, `.rindex`, `.indices`, `.contains` and `.substr-eq` with a
# negative start (or one past what an Int holds) return a Failure. Ours said
# "start argument to index out of range. Is: -1; should be in 0..3", with
# `what` "start argument to index" and no `.comment`, and `.substr-eq`'s held
# a bare X::OutOfRange type with no message at all. Rakudo's Str!fail-oor
# makes `what` "Position in calling 'index'", the range a Str, and the
# message "… out of range. Is: -1, should be in 0..3". Found while fixing the
# 2026-10-21 review's O23 (ledger O35).
#
# Runs clean under Rakudo too.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}
sub shape($r) {
    my $e = $r.exception;
    ($r.^name, $e.^name, $e.message, $e.what, $e.got, $e.range, $e.?comment)
}
sub want($m, $got = -1, $chars = 3) {
    ('Failure', 'X::OutOfRange', "Position in calling '$m' out of range. Is: $got, should be in 0..$chars",
     "Position in calling '$m'", $got, "0..$chars", Any)
}

ck shape("abc".index("b", -1)),        want('index'),     '.index';
ck shape("abc".rindex("b", -1)),       want('rindex'),    '.rindex';
ck shape("abc".indices("b", -1)),      want('indices'),   '.indices';
ck shape("abc".contains("b", -1)),     want('contains'),  '.contains';
ck shape("abc".contains(/b/, -1)),     want('contains'),  '.contains with a regex';
ck shape("abc".substr-eq("b", -1)),    want('substr-eq'), '.substr-eq';
ck shape("abc".index("b", -1, :i)),    want('index'),     '.index(:i)';
ck shape("abc".indices("b", -1, :i)),  want('indices'),   '.indices(:i)';
ck shape("abc".substr-eq("b", -1, :i)), want('substr-eq'), '.substr-eq(:i)';
ck shape("abc".substr-eq("b", *-5)),   want('substr-eq', -2), 'a WhateverCode position is the one it computes';
ck shape("q\x[301]bc".index("b", -2)), want('index', -2), 'the range counts characters';
ck shape("abc".index("b", 2**70)),     want('index', 2**70), 'past what an Int holds';

say $fails ?? "FAIL ($fails)" !! 'PASS';
