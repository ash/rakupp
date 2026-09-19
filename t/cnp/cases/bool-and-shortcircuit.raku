# A comparison's VALUE is a Bool, not a number, and `&&`/`||`/`//` yield an
# OPERAND rather than a Bool. A register file that got any of that wrong would
# still print something plausible, so each one is printed. Everything the loop
# touches is a plain scalar, because a method call would refuse the loop and
# then this case would prove nothing.
my $i = 0; my $out = ""; my $nil = Nil;
while $i < 4 {
    my $b = $i < 2;
    my $a = $i && "yes";
    my $o = $i || "zero";
    my $d = $nil // "undef";
    $out = $out ~ $b ~ " " ~ $a ~ " " ~ $o ~ " " ~ $d ~ "\n";
    $i = $i + 1;
}
print $out;
say (0 // 9), " ", (0 || 9), " ", (0 && 9);
