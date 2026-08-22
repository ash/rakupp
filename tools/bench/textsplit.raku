# Text munging — build a 20k-line record file in memory, then split it into
# lines, split each line into fields, and rebuild it with the fields reordered.
# This is the shape of most real scripts and of nothing else in this directory:
# regex.raku matches, strcat.raku appends, hashfill.raku interpolates, but
# nothing splits and rejoins. It is the second kernel with a Perl 5 twin
# (textsplit.pl) — line-for-line the same program — because text munging is the
# comparison Perl is usually invoked to win.
my $text = (1 .. 20_000).map({ "field{$_}:value{$_}:tag{$_}" }).join("\n");
my $n     = 0;
my $chars = 0;
my @out;
for $text.split("\n") -> $line {
    my @f = $line.split(':');
    $n     += @f.elems;
    $chars += @f[1].chars;
    @out.push(@f[2] ~ ':' ~ @f[0]);
}
my $joined = @out.join("\n");
say $n, " ", $chars, " ", $joined.chars;
