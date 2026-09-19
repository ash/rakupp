# Values that cannot live unboxed — strings, Rats, bignums — ride in the box
# array beside the register file, and a register moves between the two states
# as the loop runs. Every transition here happens thousands of times.
my $s = ""; my $i = 0; my $r = 0; my $big = 1;
while $i < 60 {
    $s = $s ~ "x";
    $r = $r + 1/3;           # a Rat, and it stays one
    $big = $big * 3;         # overflows int64 on the way past iteration 40
    $i = $i + 1;
}
say $s.chars, " ", $r, " ", $r.WHAT.^name, " ", $big;
# a register that goes int -> box -> int again
my $v = 0; my $j = 0;
while $j < 20 {
    $v = $j %% 2 ?? "even" !! $j;
    $j = $j + 1;
}
say $v;
