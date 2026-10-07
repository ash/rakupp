# Ordinary, sigiled Raku must come out of the translator byte for byte: the
# translator only touches bare names that a declaration in scope explains.
#   find … -name '*.raku' | xargs raku -Ilib t/passthrough.raku
need Acme::Sigilless;
my ($same, $changed, $died) = 0, 0, 0;
for @*ARGS -> $f {
    my $src = $f.IO.slurp;
    my $out = try Acme::Sigilless::translate($src, :!warn);
    without $out {
        $died++;
        say "DIED    $f: {$!.message.lines[0].substr(0, 140)}";
        next;
    }
    if $out eq $src {
        $same++;
        next;
    }
    $changed++;
    my @a = $src.lines;
    my @b = $out.lines;
    my @d = (^@a).grep({ @a[$_] ne @b[$_] });
    say "CHANGED $f ({+@d} lines)";
    for @d.head(3) {
        say "   {$_ + 1}- {@a[$_].trim.substr(0, 150)}";
        say "   {$_ + 1}+ {@b[$_].trim.substr(0, 150)}";
    }
}
say "same $same, changed $changed, died $died";
exit $changed || $died ?? 1 !! 0;
