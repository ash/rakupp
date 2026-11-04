# A method that changes its invocant's container — a Buf's push, pop, splice
# and the rest write through the method's lvalue — changes the loop's
# variable, not a copy of it (#219). Each call ran through a scratch scope the
# invocant was COPIED into, so every push the kernel made after the first
# interpreted pass was lost. A loop variable is now named as itself in that
# call, and a `my` of the loop's own is written back from its scratch copy.
# Every loop here enters a kernel; checked against Rakudo 2026.09.
{ my $b = Buf.new; my $k = 9; while $k >= 0 { $b.push($k); $k = $k - 1 }; say "B1 ", $b.list }
{ my $b = Buf.new; my $k = 9; while $k >= 0 { $b.append($k); $k = $k - 1 }; say "B2 ", $b.list }
{ my $b = Buf.new; my $k = 9; while $k >= 0 { $b.prepend($k); $k = $k - 1 }; say "B3 ", $b.list }
{ my $b = Buf.new; my $k = 9; while $k >= 0 { $b.unshift($k); $k = $k - 1 }; say "B4 ", $b.list }
{ my $b = Buf.new(1..10); my $s = 0; my $k = 9; while $k >= 0 { $s = $s + $b.pop; $k = $k - 1 }; say "B5 $s {$b.elems}" }
{ my $b = Buf.new(1..10); my $s = 0; my $k = 9; while $k >= 0 { $s = $s + $b.shift; $k = $k - 1 }; say "B6 $s {$b.elems}" }
{ my $b = Buf.new(1..10); my $k = 1; while $k < 6 { $b.splice($k, 1); $k = $k + 1 }; say "B7 ", $b.list }
{ my $b = Buf.new(1, 2, 3); my $k = 4; while $k <= 8 { $b.reallocate($k); $k = $k + 1 }; say "B8 ", $b.list }
# a pure method after the mutator reads what it left
{ my $b = Buf.new; my $t = 0; my $k = 4; while $k >= 0 { $b.push($k); $t = $t + $b.elems; $k = $k - 1 }; say "B9 $t" }
# a slot the loop also assigns
{ my $b = Buf.new; my $n = Buf.new(42); my $k = 4; while $k >= 0 { if $k == 2 { $b = $n }; $b.push($k); $k = $k - 1 }; say "B10 ", $b.list }
# a `my` of the loop's own
{ my $e = Buf.new; my $s = 0; my $k = 9; while $k >= 0 { my $c = $e.clone; $c.push($k); $c.push(1); $s = $s + $c.elems; $k = $k - 1 }; say "B11 $s" }
# a loop in a routine, whose variables live in its pad
sub fill($n) { my $b = Buf.new; my $k = $n; while $k >= 0 { $b.push($k); $k = $k - 1 }; $b.list }
say "B12 ", fill(4);
