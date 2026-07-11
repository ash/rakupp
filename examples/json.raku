#!/usr/bin/env raku
# A JSON parser written as a Raku grammar plus an actions class.
#
# The grammar describes JSON's shape; the actions class turns each match into
# a native Raku value as the parse reduces, so a JSON object becomes a Hash, an
# array becomes an Array, and the scalars become Str / Int / Rat / Bool / Any.
# `proto rule value {*}` with `:sym<...>` alternatives gives one method per JSON
# value kind. Integers stay Int and decimals become an exact Rat, not a float.

grammar JSON {
    rule  TOP              { <value> }
    proto rule value       {*}
    rule  value:sym<object> { '{' <pairs> '}' }
    rule  value:sym<array>  { '[' <items> ']' }
    token value:sym<str>    { <str> }
    token value:sym<num>    { '-'? \d+ ['.' \d+]? }
    token value:sym<true>   { 'true' }
    token value:sym<false>  { 'false' }
    token value:sym<null>   { 'null' }
    rule  pairs             { <pair>* % ',' }
    rule  pair              { <str> ':' <value> }
    rule  items             { <value>* % ',' }
    token str               { '"' ( <-["]>* ) '"' }
}

class Actions {
    method TOP($/)               { make $<value>.made }
    method value:sym<object>($/) { make $<pairs>.made }
    method value:sym<array>($/)  { make $<items>.made }
    method value:sym<str>($/)    { make $<str>.made }
    method value:sym<num>($/)    { make( (~$/).contains('.') ?? (~$/).Rat !! (~$/).Int ) }
    method value:sym<true>($/)   { make True }
    method value:sym<false>($/)  { make False }
    method value:sym<null>($/)   { make Any }
    method pairs($/)             { make $<pair>.map({ .made }).hash }
    method pair($/)              { make $<str>.made => $<value>.made }
    method items($/)             { make $<value>.map({ .made }).Array }
    method str($/)               { make ~$0 }
}

sub type-of($v) {
    return 'Null'  unless $v.defined;
    return 'Bool'  if $v ~~ Bool;
    return 'Int'   if $v ~~ Int;
    return 'Rat'   if $v ~~ Rat;
    return 'Str'   if $v ~~ Str;
    return 'Array' if $v ~~ Positional;
    return 'Hash'  if $v ~~ Associative;
    $v.^name;
}

# Walk the parsed structure and print it indented, annotating each scalar with
# the Raku type it became — proof that this is a live data structure, not text.
sub dump($v, $indent = 0) {
    my $pad = '  ' x $indent;
    if $v ~~ Associative {
        for $v.keys.sort -> $k {
            my $child = $v{$k};
            if $child ~~ Associative | Positional {
                say "$pad$k:";
                dump($child, $indent + 1);
            }
            else {
                say "$pad$k: $child.gist() ({type-of($child)})";
            }
        }
    }
    elsif $v ~~ Positional {
        for $v.kv -> $i, $child {
            if $child ~~ Associative | Positional {
                say "$pad\[$i]:";
                dump($child, $indent + 1);
            }
            else {
                say "$pad\[$i] $child.gist() ({type-of($child)})";
            }
        }
    }
}

sub MAIN() {
    my $json = q:to/END/;
    {
        "mission": "Voyager 1",
        "launched": 1977,
        "active": true,
        "distance_au": 163.9,
        "instruments": ["ISS", "PLS", "MAG"],
        "target": null,
        "trajectory": { "escaped": true, "speed_km_s": 17.0 }
    }
    END

    my %data = JSON.parse($json.trim, actions => Actions).made;

    say 'Parsed JSON into a live Raku structure:';
    dump(%data);
    say '';

    # Query the structure the way you would any Raku Hash/Array.
    say "mission is a {type-of(%data<mission>)}: %data<mission>";
    say "first instrument: %data<instruments>[0]";
    say "instrument count: {%data<instruments>.elems}";
    say "distance stayed exact: {%data<distance_au>.nude.join('/')}";
    say "nested speed: {%data<trajectory><speed_km_s>} ({type-of(%data<trajectory><speed_km_s>)})";
}
