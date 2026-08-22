# Array mutation — push, indexed read, indexed write. arrayops.raku times the
# lazy grep/map/sum pipeline; this times the eager side, which is a different
# path: the element vector grows, then is read and written in place by index.
# The array pointer was the second most common live field in the value census
# (4.3M of 30M destructions), and nothing measured it being mutated.
my @a;
for 1 .. 200_000 -> $i {
    @a.push($i * 3 % 1000);
}
my $s = 0;
for 0 ..^ @a.elems -> $i {
    $s += @a[$i];
}
for 0 ..^ @a.elems -> $i {
    @a[$i] = @a[$i] + 1;
}
say @a.elems, " ", $s, " ", @a[999];
