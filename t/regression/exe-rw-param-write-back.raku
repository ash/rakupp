# Regression: an `is rw` parameter reached through anything but a direct call
# to a top-level sub, in a program compiled with --exe.
#
# The binary copied a callee's write back into the caller's variable only at a
# direct call of a top-level sub (the call site passes a ValueList and copies
# the rw slots out after). A lexical sub, a pointy block, a method, or a
# top-level sub called through `&name` bound the parameter to a slot of a
# temporary list, so every write was lost: `{ sub w($p is rw) { $p = 5 }; w($x) }`
# left $x as it was. Those routines now stay interpreted. And a direct call
# whose argument is an assignment — `w($x = $x + 1)` — passed the assignment's
# value, not its target, so the callee's writes never reached $x.
#
# Contract: exit 0 + last line PASS. Runs unchanged under Rakudo.
my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku}") unless $got eqv $want }

# a lexical sub
{ sub w($p is rw) { $p = 5 }; my $x = 0; w($x); ck $x, 5, 'a lexical sub' }
# a pointy block held in a variable
{ my &pb = -> $p is rw { $p = 7 }; my $y = 0; pb($y); ck $y, 7, 'a pointy block' }
# a method
{ class C { method set($x is rw) { $x = 21 } }; my $v = 1; C.set($v); ck $v, 21, 'a method' }
# a top-level sub called directly, through &name and through a copy of it
sub tw($p is rw) { $p = 9 }
my $z = 0; tw($z); ck $z, 9, 'a direct call';
$z = 0; &tw($z); ck $z, 9, 'a call through &name';
my &f = &tw; $z = 0; f($z); ck $z, 9, 'a call through a copy of &name';
# an assignment as the argument: the callee writes its target
sub add($p is rw) { $p = $p + 10 }
my $a = 0; add($a = $a + 1); ck $a, 11, 'an assignment argument';
my $b = 5; add($b += 2); ck $b, 17, 'an op= argument';

# `**@` keeps a lone Array whole; `+@` and `*@` spread it
sub nonf(**@v) { @v.elems }
sub plus(+@v)  { @v.elems }
my @arr = 1, 2, 3;
ck nonf(@arr), 1, '**@ keeps a lone Array whole';
ck nonf([1, 2, 3]), 1, '…and a literal one';
ck plus(@arr), 3, '+@ spreads it';

if @fail { .say for @fail; say 'FAIL'; exit 1 }
say 'PASS';
