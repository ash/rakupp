# Regression: inside a routine, `my @x = (…).map({ return 5 if …; … })` ran
# none of the map — a `.map` whose block can `return` is lazy, and list
# assignment kept it lazy, so the block ran only when (if ever) the Array was
# read, after the routine had moved on. List assignment is eager in Rakudo.
# The native backend compiled the same `return` as a C++ return from the
# block's lambda, so it left the block and the routine went on; a `return`
# inside `try { }` or `do { }` did the same. Such a routine is now an
# interpreter frame for its call, and the return is thrown at it.
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

sub grepret(@a) { @a.grep({ return "found $_" if $_ > 2; False }); "none" }
ck(grepret([1, 2, 3, 4]), 'found 3', 'return from a grep block');
ck(grepret([1]), 'none', 'a grep block that does not return');
sub nested { for 1..3 -> $i { (1..3).map({ return "$i/$_" if $i * $_ == 4 }).eager }; "no" }
ck(nested(), '2/2', 'return from a map inside a for loop');
class C { method m { (1..5).first({ return "m$_" if $_ == 3; False }); "x" } }
ck(C.new.m, 'm3', 'return from a block in a method');
my &anon = sub ($n) { (1..$n).map({ return $_ * 10 if $_ == 2; $_ }).eager; -1 };
ck((anon(3), anon(1)), (20, -1), 'return from a block in an anonymous sub');
sub outer { sub inner { (1, 2).map({ return "inner" if $_ == 2; $_ }).eager; "i" }; my $r = inner(); "outer got $r" }
ck(outer(), 'outer got inner', 'a nested sub returns from itself');
sub deep($n) { $n == 0 ?? (1, 2).map({ return "bottom" }).eager !! deep($n - 1) ~ "+" }
ck(deep(3), 'bottom+++', 'each recursive call has its own frame');
sub escape { my &b = { return 1 }; &b }
{ my &e = escape(); ck((try { e() }) // $!.^name, 'X::ControlFlow::Return', 'a block that outlived its routine') }
sub t1 { try { return 5 }; 0 }
ck(t1(), 5, 'return inside try');
sub t2 { my $x = do { return 7 if True; 1 }; 0 }
ck(t2(), 7, 'return inside do');
sub t4 { my $v = try { return "early" if True; 2 }; "late $v" }
ck(t4(), 'early', 'return inside a try that has a value');

say $fails ?? "FAILED $fails" !! "PASS";
