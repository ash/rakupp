#!/usr/bin/env rakupp

# Without this, a named option is only recognised BEFORE the first positional
# argument: `wordfreq words.txt --top=3` would not match MAIN at all.
my %*SUB-MAIN-OPTS = :named-anywhere;

#| Count the commonest words in a text.
sub MAIN(
    Str  $file = '-',        #= file to read, or "-" for standard input
    Int  :$top = 10,         #= how many words to show
    Int  :$min-length = 1,   #= ignore words shorter than this
    Bool :$json = False,     #= print JSON instead of a table
) {
    my $text;
    if $file eq '-' {
        $text = $*IN.slurp;
    }
    else {
        unless $file.IO.f {
            note "wordfreq: $file: no such file";
            exit 2;
        }
        $text = $file.IO.slurp;
    }

    my $words = bag $text.lc.comb(/ <[\w']>+ /).grep(*.chars >= $min-length);
    my @top   = $words.sort({ -.value, .key }).head($top);

    if $json {
        require JSON::Fast <&to-json>;
        say to-json @top.map({ %( word => .key, count => .value ) });
    }
    else {
        say sprintf('%-14s %4d', .key, .value) for @top;
    }
}
