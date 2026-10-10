# Regression: FIRST in a block that is CALLED rather than looped. A loop told
# execBlock to leave FIRST to it with a thread-wide flag, so a block called
# from inside any loop never ran its FIRST at all (`my &b = { FIRST … }; b()
# for 1..3`), and outside a loop FIRST ran on every call. rak calls a code
# pattern once per item and counts on FIRST running once per closure clone.
# The loop now names the one body whose FIRST it drives, and a called closure
# runs its FIRST on its first call only; a block run in place is a fresh clone
# each time it runs, so its FIRST still runs every time.
#
# Expectations checked against Rakudo 2026.09 via /opt/homebrew/bin/rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my @o;
sub got { my $s = @o.join(' '); @o = (); $s }

{ my &b = { FIRST @o.push('F'); @o.push('b') }; b() for 1..3;
  ck(got, 'F b b b', 'a block called per item of a loop: FIRST once') }
{ my &c = { FIRST @o.push('F'); NEXT @o.push('N'); @o.push('c') }; c(); c();
  ck(got, 'F c c', 'with a NEXT, called outside a loop: FIRST once') }
{ sub f { FIRST @o.push('F'); @o.push('f') }; f(); f(); f() for 1..2;
  ck(got, 'F f f f f', 'a sub: FIRST once') }
{ sub h { FIRST @o.push('F'); @o.push('h') }; for 1..3 { h() }
  ck(got, 'F h h h', 'a sub called from a loop body') }
{ class K { method m { FIRST @o.push('F'); @o.push('m') } }; K.m; K.m; K.new.m;
  ck(got, 'F m m m', 'a method: FIRST once') }
{ my &e = -> $x { FIRST @o.push("F$x"); @o.push("e$x") }; e($_) for 1..3;
  ck(got, 'F1 e1 e2 e3', 'a pointy block with a parameter') }
{ sub mk { return { FIRST @o.push('F'); @o.push('k') } }; my &k1 = mk(); my &k2 = mk();
  k1(); k1(); k2();
  ck(got, 'F k k F k', 'each closure clone runs its own FIRST') }
{ my &r = { FIRST @o.push('F'); @o.push('r') }; my &r2 = &r.clone; r(); r2(); r(); r2();
  ck(got, 'F r F r r r', '…and so does a .clone') }
{ for 1..2 { my &d = { FIRST @o.push('F'); @o.push('d') }; d(); d() }
  ck(got, 'F d d F d d', 'a closure made afresh each iteration') }
{ for 1..3 { { FIRST @o.push('F'); @o.push('i') } }
  ck(got, 'F i F i F i', 'a bare block in a loop body runs FIRST every time') }
{ for 1..3 { if True { FIRST @o.push('F'); @o.push('i') } }
  ck(got, 'F i F i F i', '…and so does an if block') }
{ for 1..2 { for 1..2 { FIRST @o.push('F'); @o.push('x') } }
  ck(got, 'F x x F x x', 'a nested loop drives its own FIRST') }
{ my $i = 0; while $i++ < 3 { FIRST @o.push('F'); @o.push('w') }
  ck(got, 'F w w w', 'a while loop') }
{ my @r = (1..3).map({ FIRST @o.push('F'); $_ * 2 }).eager;
  ck((got, @r.List), ('F', (2, 4, 6)), '.map over a block with FIRST') }

say $fails ?? "FAILED $fails" !! "PASS";
