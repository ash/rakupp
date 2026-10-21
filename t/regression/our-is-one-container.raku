# Regression: `our $x` names the package's ONE container, however often the
# declaration runs (review ledger O26).
#
# Each run of an `our` declaration made the variable a fresh Proxy view onto
# the scope that ran it and published that view again, flushing the dispatch
# cache: `for ^400_000 { our $n += 1 }` cost ~8k instructions a turn, every
# read of `$n` after it was a FETCH call, and the views lost track of the
# variable. Re-running a bare `our $n` in a loop or a sub counted to 1, a
# value the variable had before the loop was dropped, `$P::q` in a package
# missed the loop's writes, a `.map` block saw a fresh variable each time,
# and `$x := 5` rebound the package's variable where Rakudo rebinds only the
# name. Now the package keeps a cell under the published name and each run
# binds the name to it, as Rakudo binds an `our` lexical to its stash entry.
# Answers are Rakudo 2026.09's.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

for ^3 { our $a1 += 1 }
check (our $a1), 3, 'our $n += 1 in a loop body';
for ^3 { our $a2; $a2++ }
check (our $a2), 3, 'a bare our, then ++, in a loop body';
check $GLOBAL::a2, 3, '…and $GLOBAL:: is the same variable';
for 1..3 { our $a3; $a3 += $_ }
check (our $a3), 6, 'a running total';
for ^3 -> $v { our $a4 ~= $v }
check (our $a4), '012', 'with a loop parameter';
my $i = 0;
while $i++ < 3 { our $a5 += $i }
check (our $a5), 6, 'in a while body';
my @m = (^3).map: { our $a6 += 1 };
check @m, [1, 2, 3], 'in a map block';
our $a7 = 100;
for ^3 { our $a7; $a7++ }
check $a7, 103, 'a value the variable had before the loop';
our $a8;
for ^2 { our $a8 += 1 }
check $a8, 2, 'declared bare before the loop';
for ^2 { our $a9; $a9++; { our $a9; $a9++ } }
check (our $a9), 4, 'a nested block declares the same variable';

sub s1 { our $b1 += 1 }
s1() for ^3;
check (our $b1), 3, 'our += in a sub';
sub s2 { our $b2; $b2++ }
s2() for ^3;
check (our $b2), 3, 'a bare our in a sub';
sub s3 { our $b3 //= 0; $b3 += 5 }
s3(); s3();
check (our $b3), 10, '//= then += in a sub';
our $b4 = 1;
my $c4 = sub { our $b4; $b4 * 10 };
$b4 = 3;
check $c4(), 30, 'a closure that declares it reads the current value';
my $c5;
for ^1 { our $b5 = 1; $c5 = { $b5 } }
our $b5 = 42;
check $c5(), 42, 'a closure over the loop body sees a later assignment';
my @subs = do for ^3 { our $b6 += 1; sub { our $b6 } };
check @subs».(), [3, 3, 3], 'routines made in a loop share it';

package P1 { for ^3 { our $q += 1 } }
check $P1::q, 3, 'a package loop body, read by its qualified name';
package P2 { our @l = 1, 2; for ^2 { our @l; @l.push: 9 } }
check @P2::l, [1, 2, 9, 9], 'an array, in a package';
for ^3 { our %d1; %d1{$_} = 1 }
check (our %d1).keys.sort.join(','), '0,1,2', 'a hash';
for ^3 { our $d2 //= []; $d2.push($_) }
check (our $d2), [0, 1, 2], 'an array held in a scalar';

{ our $e1 = 1; $e1 := 5 }
check (our $e1), 1, 'binding the name leaves the package variable alone';
check $GLOBAL::e1, 1, '…as $GLOBAL:: sees it';
for ^2 { our $e2; $e2 := 9 }
check (our $e2), Any, '…in a loop body too';
$GLOBAL::e3 = 3;
{ our $e3; check $e3, 3, 'a value set through GLOBAL before the declaration' }
for ^3 { our $e4; my $r := $e4; $r++ }
check (our $e4), 3, 'a my bound to it';

our $t1 = 1;
sub t1g { $t1 }
sub t1f { temp $t1 = 2; t1g() }
check t1f(), 2, 'temp is seen by a callee';
check $t1, 1, '…and restored';
sub t2f { our $t2; temp $t2 = 5; $t2 }
our $t2 = 1;
check t2f(), 5, 'temp on a sub-declared our';
check $t2, 1, '…restored';

for ^200 { our $f1 += 1 }
check (our $f1), 200, 'many turns';
await (^4).map: { start { for ^50 { our $f2; $f2++ } } };
check (our $f2) > 0, True, 'from several threads';

if @fail { note "FAILED:\n  " ~ @fail.join("\n  "); say 'FAIL' } else { say 'PASS' }
