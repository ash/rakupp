#!/usr/bin/env rakupp
# Two ways to find out WHERE a parse failed, before reaching for panics.
#
#   .subparse   — how much of the input a prefix parse could account for
#   high-water  — the furthest position any rule reached, recorded in <ws>
#
# Neither is the error. Both are hints, and they are hints of different
# quality, which is the point of printing them side by side.

my $*FURTHEST = 0;

grammar Config {
    rule TOP { <.ws> <statement>* }

    # <ws> runs between every pair of atoms, so it is the cheapest place to
    # record how far the parser ever got.
    token ws {
        <!ww> \h* [ [ '#' \N* ]? \n \s* ]*
        { $*FURTHEST = $/.to if $/.to > $*FURTHEST }
    }

    proto rule statement {*}
    rule  statement:sym<block>   { <name> <arg>? '{' <statement>* '}' }
    token statement:sym<setting> { <name> \h+ [ <value>+ % \h+ ] \h* ';' <.ws> }

    token name { <[\w\-]>+ }
    token arg  { <[\w/\.\-]>+ }

    proto token value {*}
    token value:sym<string> { '"' ~ '"' $<text>=<-["]>* }
    token value:sym<bare>   { <[\w/:\.\-]>+ }
}

sub point-at(Str $text, Int $pos, Str $label) {
    my $before = $text.substr(0, $pos);
    my $line   = $before.comb("\n").elems + 1;
    my $col    = $pos - ($before.rindex("\n") // -1);
    say "$label: line $line, column $col";
    say '    ', $text.lines[$line - 1];
    say '    ', ' ' x ($col - 1), '^';
}

sub MAIN(Str $file = 'broken.conf') {
    my $text = slurp $file;
    $*FURTHEST = 0;

    if Config.parse($text) {
        say "$file parses";
        exit 0;
    }

    my $partial = Config.subparse($text);
    point-at($text, $partial ?? $partial.to !! 0, 'subparse stopped at');
    point-at($text, $*FURTHEST, 'furthest rule reached');
}
