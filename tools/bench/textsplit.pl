# Perl 5 twin of textsplit.raku — the same work, byte-identical output.
my $text = join("\n", map { "field$_:value$_:tag$_" } (1 .. 20_000));
my $n     = 0;
my $chars = 0;
my @out;
for my $line (split /\n/, $text) {
    my @f = split /:/, $line;
    $n     += scalar @f;
    $chars += length $f[1];
    push @out, $f[2] . ':' . $f[0];
}
my $joined = join("\n", @out);
print "$n $chars ", length($joined), "\n";
