# A full-width `int`/`num` array in a kernel (PACKED-ARRAY-PLAN batch 3): the
# array is packed, so the kernel reads, sizes and writes its words (idxget,
# alen, idxset); a read with a Rat key unpacks it, and the loop goes on with
# plain element stores. `for @a` over the packed words tiers up as over any
# array. A Bool stores as 1, and a Nil or a Num is refused (X::TypeCheck), as
# the interpreter and Rakudo refuse them.
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
my $u = 0;
while $j < 150 { $u = @n[1.5] if $j == 7; @n[$j] = 9223372036854775807 if $j == 9; @n[$j] = True if $j == 11; $j = $j + 1 }
say @n[7], ' ', @n[9], ' ', @n[11], ' ', @n[10], ' ', $u;
my int @g = ^120;
my $m = 0;
my $err = '';
while $m < 120 { if $m == 5 { try { @g[$m] = @g[$m] + 1e0 }; $err = $!.^name; try { @g[$m] = Nil }; $err ~= ' ' ~ $!.^name }; $m = $m + 1 }
say @g[5], ' ', $err, ' ', @g[6];
