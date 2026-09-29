# Array and hash elements inside a kernel (copy-and-patch). Reads go through
# rtIndexGet and stores through rtIndexRef, the runtime indexing the compiled
# programs use, into the variable's own storage; a store is normalised as the
# interpreter's is (Nil resets, a container goes in as an item). Only plain
# Arrays and Hashes are taken: a typed, native, shaped, lazy array, a List, a
# Set/Bag/Map keep the loop interpreted. The interpreter is the oracle.
{ my @a = 1..100; my $s = 0; my $i = 0; while $i < 100 { $s = $s + @a[$i]; $i = $i + 1 }; say "A1 $s" }
{ my @a; my $i = 0; while $i < 10 { @a[$i] = $i * 2; $i = $i + 1 }; say "A2 {@a.raku}" }
{ my @a = 0 xx 5; my $i = 0; while $i < 20 { @a[$i % 5] += $i; @a[$i % 5]++; ++@a[0]; $i = $i + 1 }; say "A3 {@a.raku}" }
{ my @a = 0 xx 5; my $i = 0; my $o = 0; while $i < 5 { $o = $o + @a[$i]++; $i = $i + 1 }; say "A3b $o {@a.raku}" }
{ my @g = [0 xx 4] xx 3; my $y = 0; while $y < 3 { my $x = 0; while $x < 4 { @g[$y][$x] = $y * 10 + $x; $x = $x + 1 }; $y = $y + 1 }; say "A4 {@g.raku}" }
{ my @g = [1,2],[3,4]; my $s = 0; my $y = 0; while $y < 2 { my $x = 0; while $x < 2 { $s = $s + @g[$y][$x]; $x = $x + 1 }; $y = $y + 1 }; say "A5 $s" }
{ my @a = 1,2; my $i = 0; my $u = 0; while $i < 5 { $u = $u + 1 unless @a[$i].defined; $i = $i + 1 }; say "A6 $u" }
{ my @a; my $i = 0; while $i < 3 { @a[$i + 2] = $i; $i = $i + 1 }; say "A7 {@a.raku}" }
{ my @a = 1,2,3; my $i = 0; while $i < 3 { @a[$i] = Nil; $i = $i + 1 }; say "A8 {@a.raku}" }
{ my @a; my @b = 1,2; my $i = 0; while $i < 2 { @a[$i] = @b; $i = $i + 1 }; say "A9 {@a.raku} {@a.elems}" }
my @shared = 0 xx 3;
sub bump($k) { @shared[$k] = @shared[$k] + 100 }
{ my $i = 0; while $i < 3 { @shared[$i] = $i; bump($i); @shared[$i] = @shared[$i] + 1; $i = $i + 1 }; say "A10 {@shared.raku}" }
{ my @a = 1,2,3; my $i = 0; try { while $i < 5 { @a[$i - 2] = 7; $i = $i + 1 }; CATCH { default { say "A11 dies ", .^name, " i=$i" } } } }
{ my Int @t = 1,2,3; my $i = 0; while $i < 3 { @t[$i] = @t[$i] * 2; $i = $i + 1 }; say "A12 {@t.raku}" }
{ my int @n = 1,2,3; my $i = 0; while $i < 3 { @n[$i] = @n[$i] + 1; $i = $i + 1 }; say "A13 {@n.raku}" }
{ my @s = True xx 50; @s[0] = @s[1] = False; my $p = 2; while $p < 50 { if @s[$p] { my $m = $p * $p; while $m < 50 { @s[$m] = False; $m = $m + $p } }; $p = $p + 1 }; my $c = 0; my $i = 0; while $i < 50 { $c = $c + 1 if @s[$i]; $i = $i + 1 }; say "A14 $c" }
{ my %h; my $i = 0; while $i < 20 { %h{$i % 3}++; $i = $i + 1 }; say "H1 ", %h.sort.map({ .key ~ "=" ~ .value }).join(",") }
{ my %h = a => 1, b => 2; my $s = 0; my $i = 0; while $i < 10 { $s = $s + %h<a> + %h{"b"}; $i = $i + 1 }; say "H2 $s" }
{ my %h; my $i = 0; while $i < 4 { %h{"k$i"} = $i * 10; $i = $i + 1 }; say "H3 ", %h.sort.map({ .key ~ "=" ~ .value }).join(",") }
{ my %h; my $i = 0; while $i < 5 { %h{"x"}[$i] = $i; $i = $i + 1 }; say "H4 ", %h<x>.raku }
{ my %s is SetHash; my $i = 0; while $i < 3 { %s{$i} = True; $i = $i + 1 }; say "H5 ", %s.keys.sort.join(",") }
{ my $n = 360; my %exp; my $d = 2; while $d * $d <= $n { while $n %% $d { %exp{$d}++; $n = $n div $d }; $d = $d + 1 }; %exp{$n}++ if $n > 1; say "H6 ", %exp.sort(*.key.Int).map({ .key ~ "^" ~ .value }).join(" ") }
