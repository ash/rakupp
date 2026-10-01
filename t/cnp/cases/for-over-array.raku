# `for @a` tiers up as `loop (; $i < @a.elems; $i++) { $x = @a[$i]; … }`
# (jit::synthArrayLoop): the element is read at the top of each iteration and
# the size is asked every time, as the interpreter's array path does. Named
# and topic forms, mixed element types, holes, `next`/`last`, a call in the
# body, and a body that grows the array through a routine — the one way the
# walk can change that the eligibility walk cannot see.
my @n = 1 .. 300;
my $s = 0;
for @n -> $x { $s = $s + $x * 2 }
say $s;
my $t = 0;
for @n { next if $_ %% 3; last if $_ > 280; $t = $t + $_ }
say $t;
my @mixed = 1, 2.5, "3", 4e0, Nil, 6;
my $acc = 0;
for @mixed -> $v { $acc = $acc + ($v // 0) }
say $acc;
my @holes; @holes[120] = 7;
my $defined = 0;
for @holes { $defined = $defined + 1 if $_.defined }
say $defined;
my @g = 1 .. 150;
sub grow($v) { @g.push(0) if $v == 140; $v }
my $gs = 0;
for @g -> $x { $gs = $gs + grow($x) }
say "$gs {@g.elems}";
