# Regression: compiled, a LEAVE runs however its block is left. `--exe` emitted
# a block's LEAVE phasers after its last statement, so a `next`, `last`,
# `return` or exception that left the block early skipped them:
# `for ^3 -> $i { LEAVE { … }; next if $i == 1; … }` ran two of the three. A
# LEAVE is a C++ scope guard now, armed at the block's start unless it reads
# a variable the block declares (then where it is written), and a death in
# it propagates on an ordinary exit. Interpreted it was right all along; this
# file compiles natively (t/run.raku checks that it does), so t/exe/run.raku
# compares the binary with the interpreter, with `-O` and without. Found
# while fixing #195. Every answer below is Rakudo 2026.09's.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($what, $got, $want) { @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want }

my @o;
for ^3 -> $i { LEAVE { @o.push: "L$i" }; next if $i == 1; @o.push: "B$i" }
check 'next', @o, [<B0 L0 L1 B2 L2>];
@o = (); for ^3 -> $i { LEAVE { @o.push: "L$i" }; $i == 1 and next; @o.push: "B$i" }
check 'next as an operand', @o, [<B0 L0 L1 B2 L2>];
@o = (); for ^3 -> $i { LEAVE { @o.push: "L$i" }; last if $i == 1; @o.push: "B$i" }
check 'last', @o, [<B0 L0 L1>];
@o = (); try { for ^3 -> $i { LEAVE { @o.push: "L$i" }; die "x" if $i == 1; @o.push: "B$i" } }
check 'die', @o, [<B0 L0 L1>];
@o = (); for ^3 -> $i { { LEAVE { @o.push: "i$i" }; last if $i == 1 }; @o.push: "o$i" }
check 'a bare block left by last', @o, [<i0 o0 i1>];
@o = (); for ^3 -> $i { next if $i == 1; LEAVE { @o.push: "L$i" }; @o.push: "B$i" }
check 'a LEAVE written after the next', @o, [<B0 L0 L1 B2 L2>];
@o = (); for ^3 -> $i { my $v = $i * 10; LEAVE { @o.push: "L$v" }; next if $i == 1; @o.push: "B$v" }
check 'a LEAVE that reads a local', @o, [<B0 L0 L10 B20 L20>];
@o = ();
for ^3 -> $i { my $a = "a$i"; LEAVE { @o.push: "1$a" }; my $b = "b$i"; LEAVE { @o.push: "2$b" }; next if $i == 1; @o.push: "body$i" }
check 'two that read locals', @o, [<body0 2b0 1a0 2b1 1a1 body2 2b2 1a2>];
@o = (); for ^2 -> $i { LEAVE { @o.push: "first$i" }; LEAVE { @o.push: "second$i" }; next }
check 'reverse order', @o, [<second0 first0 second1 first1>];
@o = (); my $j = 0; while $j < 3 { $j++; LEAVE { @o.push: "W$j" }; next if $j == 2; @o.push: "b$j" }
check 'while', @o, [<b1 W1 W2 b3 W3>];
@o = (); loop (my $k = 0; $k < 3; $k++) { LEAVE { @o.push: "C$k" }; next if $k == 1 }
check 'loop', @o, [<C0 C1 C2>];
@o = (); my $n = 0; repeat { $n++; LEAVE { @o.push: "R$n" }; next if $n == 1 } while $n < 2;
check 'repeat', @o, [<R1 R2>];
# (a `redo` STATEMENT has no native form, so these are the operand one)
@o = (); my $tries = 0;
for ^2 -> $i { LEAVE { @o.push: "L$i" }; $tries++; $tries == 1 and redo; @o.push: "B$i" }
check 'redo', @o, [<L0 B0 L0 B1 L1>];
@o = (); $tries = 0;
for ^2 -> $i { my $v = "v$i"; LEAVE { @o.push: "L$v" }; $tries++; $tries == 1 and redo; @o.push: "B$i" }
check 'redo, a LEAVE that reads a local', @o, [<Lv0 B0 Lv0 B1 Lv1>];
@o = ();
sub g2 { for ^3 -> $i { LEAVE { @o.push: "L$i" }; return "r$i" if $i == 1 } }
check 'return from a loop', (g2(), |@o), ('r1', 'L0', 'L1');
@o = ();
sub ret($n) { for ^$n -> $i { LEAVE { @o.push: "r$i" }; for ^2 -> $j { LEAVE { @o.push: "in$i$j" }; return "out$i$j" if $i == 1 && $j == 1 } }; 'none' }
check 'return through two loops', (ret(3), |@o), ('out11', <in00 in01 r0 in10 in11 r1>.Slip);
@o = (); for ^2 -> $i { LEAVE { @o.push: "leave$i" }; CATCH { default { @o.push: "catch$i" } }; next if $i == 0; die "x" }
check 'CATCH runs first', @o, [<leave0 catch1 leave1>];
@o = (); for ^2 -> $i { if $i == 1 { LEAVE { @o.push: "if$i" }; @o.push: "in$i" }; @o.push: "after$i" }
check 'an if block', @o, [<after0 in1 if1 after1>];
@o = (); for ^2 -> $i { given $i { when 1 { LEAVE { @o.push: "w$i" }; @o.push: "when$i" } }; @o.push: "g$i" }
check 'a when block', @o, [<g0 when1 w1 g1>];
@o = (); my @r = (^4).map(-> $i { LEAVE { @o.push: "m$i" }; next if $i == 1; $i * 10 });
check 'a map block', (|@r, |@o), (0, 20, 30, <m0 m1 m2 m3>.Slip);
@o = (); my @g = gather for ^3 -> $i { LEAVE { @o.push: "L$i" }; next if $i == 1; take $i };
check 'gather', (|@g, |@o), (0, 2, <L0 L1 L2>.Slip);
@o = (); for ^2 -> $i { LEAVE { @o.push: "x$i"; next if False }; @o.push: "y$i" }
check 'a loop control inside the LEAVE', @o, [<y0 x0 y1 x1>];
@o = (); my $dv = do { LEAVE { @o.push: 'do-leave' }; 42 };
check 'a do block', ($dv, |@o), (42, 'do-leave');
sub dl { LEAVE { die "from leave" }; 1 }
check "a death in a routine's LEAVE", (try dl()) // $!.message, 'from leave';
try { for ^1 { LEAVE { die "in leave" } } }
check "a death in a loop body's LEAVE", $!.message, 'in leave';

if @fail { .say for @fail; say 'FAIL' } else { say 'PASS' }
