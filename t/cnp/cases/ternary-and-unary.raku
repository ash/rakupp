# Prefix and postfix `++`/`--` differ in what they YIELD, and the lowering has
# to copy the old value before it increments for the postfix form. The unary
# operators go through the same cold path as everything else the fast lanes
# refuse.
my $i = 0; my $acc = 0; my $s = "";
while $i < 50 {
    $acc = $acc + ($i %% 3 ?? -$i !! +$i);
    $s = ~($i < 10) ~ "/" ~ $s;
    $i++;
}
say $acc, " ", $s.chars;
my $j = 5; my $k = 0;
while $j > 0 { $k = $k + $j--; }        # postfix: the OLD value
say "$j $k";
my $p = 5; my $q = 0;
while $p > 0 { $q = $q + --$p; }        # prefix: the NEW value
say "$p $q";
my $z = 0; my $w = 0;
while $z < 10 { $w = $w + (+^$z) + (?$z ?? 1 !! 0); $z++ }
say "$z $w";
