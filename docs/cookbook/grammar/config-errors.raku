#!/usr/bin/env rakupp
# The same grammar as config.raku, taught to say where a file goes wrong.
#
# A grammar that only answers Nil is no use to whoever wrote the file. Every
# place where the format leaves no choice gets `|| <.panic(...)>`: if the
# expected text is not there, the parse stops with a position and a reason
# instead of unwinding all the way back to TOP.

grammar Config {
    rule TOP { <.ws> <statement>* [ $ || <.panic('expected a setting or a block')> ] }

    token ws { <!ww> \h* [ [ '#' \N* ]? \n \s* ]* }

    proto rule statement {*}

    rule statement:sym<block> {
        <name> <arg>? '{' <statement>*
        [ '}' || <.panic("expected '}' to close this block")> ]
    }

    # <!before '{'> is what makes the panic safe: without it, this candidate
    # panics on the first line of every block, because it is tried there too.
    token statement:sym<setting> {
        <name> \h+ [ <value>+ % \h+ ] \h* <!before '{'>
        [ ';' || <.panic("expected ';' at the end of the setting")> ]
        <.ws>
    }

    token name { <[\w\-]>+ }
    token arg  { <[\w/\.\-]>+ }

    proto token value {*}
    token value:sym<string> { '"' ~ '"' $<text>=<-["]>* }
    token value:sym<bare>   { <[\w/:\.\-]>+ }

    # self.pos is where the cursor stands, self.orig the whole input, so the
    # line and column are a substring away.
    method panic($reason) {
        my $before = self.orig.substr(0, self.pos);
        my $line   = $before.comb("\n").elems + 1;
        my $col    = self.pos - ($before.rindex("\n") // -1);
        die "line $line, column $col: $reason";
    }
}

sub MAIN(*@files) {
    for (@files || <sample.conf broken.conf>) -> $file {
        my $m = try Config.parse(slurp $file);
        if $! {
            say "$file: $!.message()";
        }
        else {
            say "$file: parsed, {$m<statement>.elems} top-level statements";
        }
    }
}
