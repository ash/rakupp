# Regression (issue #106): `next` inside a loop's FIRST phaser ends the first
# iteration — NEXT and LAST still run — instead of dying with "next without
# loop construct". Contract: exit 0 + last line PASS.
my @fail;
my @out;
for 1..3 -> $i { FIRST { @out.push('f'); next }; NEXT @out.push("n$i"); LAST @out.push('L'); @out.push($i) }
@fail.push("for: {@out}") unless @out eqv ['f', 'n1', 2, 'n2', 3, 'n3', 'L'];
@out = ();
my $j = 0;
while $j++ < 3 { FIRST { next }; @out.push($j) }
@fail.push("while: {@out}") unless @out eqv [2, 3];
my @m = (1..3).map({ FIRST next; $_ * 10 });
@fail.push("map: {@m}") unless @m eqv [20, 30];
.say for @fail;
say @fail ?? 'FAIL' !! 'PASS';
