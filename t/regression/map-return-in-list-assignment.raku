# Regression: inside a routine, `my @x = (…).map({ return 5 if …; … })` ran
# none of the map — a `.map` whose block can `return` is lazy, and list
# assignment kept it lazy, so the block ran only when (if ever) the Array was
# read, after the routine had moved on. List assignment is eager in Rakudo.
# The native backend compiled the same `return` as a C++ return from the
# block's lambda, so it now refuses it and the program runs bundled.
#
# Expectations checked against Rakudo 2026.09 via /opt/homebrew/bin/rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

sub mret { my @x = (1, 2).map({ return 5 if $_ == 2; $_ }); 0 }
ck(mret(), 5, 'return from a map assigned to an array');
sub mret2 { my @x; @x = (1, 2).map({ return 5 if $_ == 2; $_ }); 0 }
ck(mret2(), 5, 'return from a map assigned to a declared array');
sub mlog { my @log; my @x = (1, 2, 3).map({ @log.push($_); return @log.elems if $_ == 2; $_ }); 0 }
ck(mlog(), 2, 'the map stops at the return');
sub mkeep { my @x = (1, 2, 3).map({ return 5 if $_ == 9; $_ * 2 }); @x }
ck(mkeep(), [2, 4, 6], 'a map that does not return');
{ my @a = (1..Inf).map({ last if $_ > 3; $_ }); ck(@a[^3], (1, 2, 3), 'an endless source stays lazy') }

say $fails ?? "FAILED $fails" !! "PASS";
