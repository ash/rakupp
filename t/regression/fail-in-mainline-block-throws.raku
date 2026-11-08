# A `fail` in a block written at the mainline throws, as Rakudo's does with no
# routine around it to return a Failure from: `sub ap(&b) { b() }; my $r =
# ap({ fail "x" })` returned a Failure from ap, the routine that ran the
# block, and `ap({ fail "x" }) // "def"` was "def". A `fail` written in a
# routine still returns its Failure from that routine, a block inside it
# included; and the top level of a string Test's `eval-lives-ok` runs is a
# routine of its own, from which a `fail` returns. (Each case runs in a
# program of its own: inside a `try` a `fail` throws anyway.)
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
sub ran($code) {
    my $p = run $*EXECUTABLE, '-e', $code, :out, :err;
    ($p.out.slurp(:close).trim, $p.err.slurp(:close).lines.head // '', $p.exitcode)
}

my $ap = 'sub ap(&b) { b() }; ';
ck ran($ap ~ 'my $r = ap({ fail "x" }); say $r.^name'), ('', 'x', 1), 'a mainline block dies';
ck ran($ap ~ 'my $r = ap({ fail "x" }) // "def"; say $r'), ('', 'x', 1), 'before a //';
ck ran('my &b = { fail "v" }; my $r = b(); say $r.^name'), ('', 'v', 1), 'a block held in a variable';
ck ran($ap ~ 'sub q { my $r = ap({ fail "z" }); "after" }; say q().^name'), ('Failure', '', 0),
   'a block in a routine returns from the routine';
ck ran('sub q2 { fail "w" }; say q2().^name'), ('Failure', '', 0), 'a routine returns its Failure';
ck ran('use Test; eval-lives-ok q[fail "e"], "lives"'), ('ok 1 - lives', '', 0), "eval-lives-ok's string";

say 'PASS';
