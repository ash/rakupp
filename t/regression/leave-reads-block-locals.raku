# Regression: a LEAVE that reads a variable its block declares runs however
# the block is left, with what the variable holds then. `--exe` armed such a
# LEAVE where it was written — after the declaration — so an exit before it
# skipped it; such a block now keeps its routine interpreted (the cases moved
# here from leave-on-every-exit-native.raku, which compiles natively).
# t/exe/parity.raku compares the binary with the interpreter.
# Contract: exit 0 + last line PASS. Every answer below is Rakudo 2026.09's.
my @fail;
sub check($what, $got, $want) { @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want }
my @o;
my $tries = 0;
@o = (); for ^3 -> $i { my $v = $i * 10; LEAVE { @o.push: "L$v" }; next if $i == 1; @o.push: "B$v" }
check 'a LEAVE that reads a local', @o, [<B0 L0 L10 B20 L20>];
@o = ();
for ^3 -> $i { my $a = "a$i"; LEAVE { @o.push: "1$a" }; my $b = "b$i"; LEAVE { @o.push: "2$b" }; next if $i == 1; @o.push: "body$i" }
check 'two that read locals', @o, [<body0 2b0 1a0 2b1 1a1 body2 2b2 1a2>];
@o = (); $tries = 0;
for ^2 -> $i { my $v = "v$i"; LEAVE { @o.push: "L$v" }; $tries++; $tries == 1 and redo; @o.push: "B$i" }
check 'redo, a LEAVE that reads a local', @o, [<Lv0 B0 Lv0 B1 Lv1>];
say @fail ?? "FAIL\n" ~ @fail.join("\n") !! 'PASS';
