# --exe: a `for` with two or more parameters, one of them named `$_`, makes
# that parameter the topic. Compiled, `for … -> $_, $name { .IO }` bound
# `$_` but read the outer topic, so `.IO` was Any's and roast
# S32-io/io-special.t died on its first test once its EVAL no longer kept it
# interpreted; `-> $_, $n { say $_ }` printed (Any). Kept native by
# t/run.raku.
# Contract: exit 0 + last line PASS.

my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want }

my @got;
for 1, 2, 3, 4 -> $_, $n { @got.push: ($_, $n) }
check @got, [(1, 2), (3, 4)], '-> $_, $n binds the topic';
@got = ();
for 1, 2, 3, 4 -> $_, $n { @got.push: .succ }
check @got, [2, 4], 'a method on the topic';
@got = ();
for 1, 2, 3, 4 -> $a, $_ { @got.push: .succ }
check @got, [3, 5], 'the second parameter as the topic';
@got = ();
for 1, 2, 3, 4 -> $_, Int $n { @got.push: .succ + $n }
check @got, [4, 8], 'beside a typed parameter';
@got = ();
for 1, 2, 3, 4 -> $_, $n { sub d { $^v ~ $n }; @got.push: d(.succ) }
check @got, ['22', '44'], 'read from inside a sub';
@got = ();
for 1, 2, 3, 4 -> $_, $n { for 7, 8 -> $_, $m { @got.push: $_ }; @got.push: $_ }
check @got, [7, 1, 7, 3], 'a nested loop has its own';
$_ = 9;
@got = ();
for 1, 2, 3, 4 -> $a, $b { @got.push: $_ }
check @got, [9, 9], 'a loop with no `$_` parameter leaves the outer topic';

if @fail { .say for @fail; say 'FAIL'; exit 1 }
say 'PASS';
