# Regression: a compiled call MOVES a variable into an `is rw` parameter when
# nothing else can reach it during the call, and back out after (issue #130).
# Copied in and out, the callee found the string shared and copied all of it
# on every `$x ~= …`: 40,000 calls 94 ms compiled, 17 interpreted.
#
# What a move must keep: the result (the parameter IS the variable), a write
# made before the callee throws (Rakudo keeps it — the copy-back used to be
# skipped), the value of the call when it returns the parameter, and a variable
# passed twice. What it must never do is leave the variable empty while code
# that can see it runs: a closure that reads it, a routine that names it, a
# WhateverCode over it. Those calls keep the copy, so such code sees the value
# from before the call (Rakudo shows the live one — a separate, older
# divergence of the compiled backend); this test only holds them to that or to
# the live value, never to an empty one.
#
# Expectations checked against Rakudo 2026.09 via /opt/homebrew/bin/rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

sub app($x is rw, $y) { $x ~= $y }
sub boom($x is rw) { $x ~= '!'; die 'no' }
sub inc($x is rw) { $x++ }
sub ret($x is rw) { $x ~= 'k'; $x }
sub two($a is rw, $b is rw) { $a ~= 1; $b ~= 2 }

{ my $s = ''; app($s, 'ab') for ^3; ck($s, 'ababab', 'the caller gets every append') }
{ my $e = 'e'; try boom($e); ck($e, 'e!', 'a write before a throw stays') }
{ my $i = 1; inc($i) for ^3; ck($i, 4, 'an Int') }
{ my $m = 'm'; my $r = ret($m); $m ~= '!'; ck(($r, $m), ('mk', 'mk!'), 'the returned parameter is a value') }
{ my $d = 'd'; two($d, $d); ck($d.substr(0, 1), 'd', 'one variable passed twice keeps its text') }
sub local-loop() { my $l = ''; app($l, 'q') for ^4; $l }
ck(local-loop(), 'qqqq', "a routine's own local");
my $top = 't'; app($top, 'u'); app($top, 'v');
ck($top, 'tuv', 'a top-level variable');
{ my @a = 'p'; app(@a[0], 'q'); ck(@a[0], 'pq', 'an array element (copied)') }

# reachable during the call: never empty there
sub peek($x is rw, &p) { $x ~= 'b'; p() }
{ my $s = 'a'; my $seen = peek($s, { $s.chars }); ck(($seen == 1|2, $s), (True, 'ab'), 'a closure reads it mid-call') }
my $g = 'x'; sub names-it($y is rw) { $y ~= 'y'; $g.chars }
{ my $seen = names-it($g); ck(($seen == 1|2, $g), (True, 'xy'), 'a routine names it') }
sub curry($x is rw, &f) { $x ~= 'z'; f(1) }
{ my $w = 'w'; my $seen = curry($w, * ~ $w); ck(($seen eq '1w' | '1wz', $w), (True, 'wz'), 'a WhateverCode over it') }

# linear: 160,000 appends through the parameter in well under a second
{ my $t = now; my $s = ''; app($s, 'abcde') for ^160_000; my $ms = (now - $t) * 1000;
  ck(($s.chars, $ms < 1000), (800_000, True), '160,000 appends through `is rw`') }

say $fails ?? "FAILED $fails" !! "PASS";
