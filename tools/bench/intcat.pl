# Perl 5 twin of intcat.raku — the same work, byte-identical output.
my $s = '';
$s .= $_ for 0 .. 999_999;
my $t = '';
for my $i (0 .. 999_999) { $t .= $i; $t .= ',' }
print length($s), ' ', length($t), "\n";
