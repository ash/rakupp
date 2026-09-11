#!/usr/bin/env rakupp
# Read a nginx-shaped configuration file into a nested data structure.

grammar Config {
    # TOP opens with <.ws> because a rule inserts whitespace AFTER each atom,
    # never before the first one — without it a leading comment is unparsable.
    rule TOP { <.ws> <statement>* }

    # The whitespace of this format includes comments, so every rule skips
    # them for free.
    token ws { <!ww> \h* [ [ '#' \N* ]? \n \s* ]* }

    proto rule statement {*}
    rule  statement:sym<block>   { <name> <arg>? '{' <statement>* '}' }
    token statement:sym<setting> { <name> \h+ [ <value>+ % \h+ ] \h* ';' <.ws> }

    token name { <[\w\-]>+ }
    token arg  { <[\w/\.\-]>+ }

    proto token value {*}
    token value:sym<string> { '"' ~ '"' $<text>=<-["]>* }
    token value:sym<bare>   { <[\w/:\.\-]>+ }
}

# One method per rule, each one making the value its rule stands for.
class Conf {
    method TOP($/)                    { make $<statement>».made }
    method statement:sym<setting>($/) { make %( name => ~$<name>, values => $<value>».made ) }
    method statement:sym<block>($/)   { make %( name => ~$<name>,
                                                arg  => ($<arg> ?? ~$<arg> !! ''),
                                                body => $<statement>».made ) }
    method value:sym<string>($/)      { make ~$<text> }
    method value:sym<bare>($/)        { make ~$/ }
}

sub outline(@nodes, Int $depth = 0) {
    my $pad = '  ' x $depth;
    for @nodes -> %n {
        if %n<body>:exists {
            say $pad, %n<name>, (%n<arg> ?? " %n<arg>" !! '');
            outline(%n<body>, $depth + 1);
        }
        else {
            say $pad, %n<name>, ' = ', %n<values>.join(' ');
        }
    }
}

# Settings are a list, not a hash: a key may legitimately appear twice.
sub setting(@nodes, Str $name) {
    my $hit = @nodes.first({ !(.<body>:exists) && .<name> eq $name });
    $hit ?? $hit<values>.join(' ') !! Nil
}

sub blocks(@nodes, Str $name) {
    @nodes.grep({ .<body>:exists && .<name> eq $name })
}

sub MAIN(Str $file = 'sample.conf') {
    my @tree = Config.parse(slurp($file), :actions(Conf)).made;

    say "--- outline ---";
    outline(@tree);

    say "\n--- servers ---";
    for blocks(@tree, 'server') -> %server {
        my @paths = blocks(%server<body>, 'location').map(*.<arg>);
        say setting(%server<body>, 'name'),
            ' root=', setting(%server<body>, 'root'),
            ' locations=', (@paths ?? @paths.join(',') !! '(none)');
    }
    say "\nlisten port: ", setting(@tree, 'listen');
}
