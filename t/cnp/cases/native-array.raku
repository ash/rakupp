# A full-width `int`/`num` array in a kernel (PACKED-ARRAY-PLAN batch 3): the
# array is packed, so the kernel reads, sizes and writes its words (idxget,
# alen, idxset), and a store that is not a word unpacks it and goes on as a
# plain element store. `for @a` over the packed words tiers up as over any
# array. Nil resets an element to the array's zero, as the interpreter does.
my int @a = ^300;
my $s = 0;
for @a -> $x { $s = $s + $x }
say $s;
my $t = 0;
for @a { next if $_ %% 3; $t = $t + $_ }
say $t;
my $i = 0;
while $i < @a.elems { @a[$i] = $i * 3; $i = $i + 1 }
say @a[299], ' ', @a.sum;
my num @f = (^200).map(* + 0.5e0);
my $fs = 0e0;
my $k = 0;
while $k < @f.elems { @f[$k] = @f[$k] * 2e0; $fs = $fs + @f[$k]; $k = $k + 1 }
say $fs, ' ', @f[199];
my int @n = ^150;
my $j = 0;
while $j < 150 { @n[$j] = Nil if $j == 7; @n[$j] = 9223372036854775807 if $j == 9; $j = $j + 1 }
say @n[7], ' ', @n[9], ' ', @n[10];
my int @g = ^120;
my $m = 0;
while $m < 120 { @g[$m] = @g[$m] + 1e0 if $m == 5; $m = $m + 1 }
say @g[5], ' ', @g[5].^name, ' ', @g[6];
