#!/usr/bin/env rakupp
# What is taking up the space in a directory? Totals it by file extension.
#
#   rakupp dirstat.raku ~/src/project
#   rakupp dirstat.raku --top=5 --json .

use lib 'lib';
use DirStat::Scan;
use DirStat::Tally;

use JSON::Fast;
use Number::Bytes::Human :functions;
use Terminal::ANSIColor;
use Terminal::Boxer;

sub MAIN(
    Str  $dir = '.',    #= the directory to scan
    Int  :$top = 10,    #= how many extensions to show
    Bool :$json,        #= print JSON instead of a table
) {
    my @kinds = tally scan $dir.IO;
    my $total = @kinds.map(*<bytes>).sum;

    if $json {
        say to-json { :$dir, :$total, kinds => @kinds.head($top) };
        return;
    }

    my @rows = <extension files size share>,
               |@kinds.head($top).map: {
                   .<kind>, .<files>, format-bytes(.<bytes>),
                   sprintf('%.1f%%', 100 * .<bytes> / ($total || 1))
               };

    # Terminal::Boxer gives every column the same width: pad the cells
    # ourselves, the name to the left and the numbers to the right.
    my $width = 2 + @rows.map(|*)».chars.max;
    my @cells = flat @rows.map: -> ($name, *@numbers) {
        $name.fmt(" %-{$width - 1}s"), |@numbers».fmt("%{$width - 1}s ")
    };

    say colored("$dir — {format-bytes $total} in {+@kinds} kinds of file", 'bold');
    print ss-box :4col, :cw($width), :f(*.Str), @cells;

    if @kinds > $top {
        say colored("…and {@kinds - $top} more; --top={+@kinds} shows them all", 'italic');
    }
}
