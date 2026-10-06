# Regression: `$s ~= X` appends in place, compiled as interpreted (issue #130).
# The native backend's rtCatAssign grew the string in place only when both
# sides were Strs, so `$s ~= $_ for ^$n` over Ints rebuilt the whole string on
# every append: 40,000 appends took 0.7 s under --exe and 1 ms interpreted.
# Its Str + Str path also skipped renormalization, so `"e" ~= "\x[301]"` was
# two codepoints compiled and one (é) interpreted and in Rakudo.
#
# Expectations checked against Rakudo 2026.09 via /opt/homebrew/bin/rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

{ my $s = "e"; $s ~= "\x[301]"; ck(($s.chars, $s.codes), (1, 1), 'a combining mark joins its base') }
{ my $s = "a"; $s ~= "\c[COMBINING ACUTE ACCENT]b"; ck($s.chars, 2, 'a mark and a letter') }
{ my $s = "e\x[301]"; $s ~= "x"; ck($s.codes, 2, 'ASCII after a composed character') }
{ my $t = "a"; $t ~= 5; $t ~= 2.5; $t ~= -3; $t ~= 2**70; ck($t, 'a52.5-31180591620717411303424', 'Ints, a Rat, a BigInt') }
{ my $u = ""; $u ~= $_ for ^5; ck($u, '01234', 'the topic of a modifier for') }
{ my Str $v; $v ~= "x"; ck($v, 'x', 'an undefined Str') }
{ my $w = "ab"; my $w2 = $w; $w ~= "c"; ck(($w, $w2), ('abc', 'ab'), 'a copy keeps its own text') }
{ my $x = "x" x 30; my $y = $x; $x ~= "!"; ck(($y.chars, $x.chars), (30, 31), 'a long copy keeps its own text') }
# linear, not quadratic: 100,000 Int appends take milliseconds (about 4.5 s
# when every append rebuilt the string), so the bound is far from both
{ my $t = now; my $s = ''; $s ~= $_ for ^100_000; my $ms = (now - $t) * 1000;
  ck(($s.chars, $ms < 2000), (488890, True), '100,000 appends in well under 2 s') }

say $fails ?? "FAILED $fails" !! "PASS";
