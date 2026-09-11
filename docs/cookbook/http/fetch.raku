#!/usr/bin/env rakupp
# GET a JSON list and print it, with HTTP::Tiny — one small dependency, and
# the response is yours to decode.
#
# Needs api-server.raku running in another terminal.

use HTTP::Tiny;
use JSON::Fast;

sub MAIN(Str $url = 'http://127.0.0.1:8099/books') {
    my %resp = HTTP::Tiny.new.get($url);

    # A transport failure is a status too: HTTP::Tiny answers 599 and puts the
    # reason in the content, so there is nothing to catch.
    unless %resp<success> {
        note "GET $url: %resp<status> %resp<reason>";
        note '  ', %resp<content>.decode.trim if %resp<content>;
        exit 1;
    }

    my @books = from-json %resp<content>.decode;

    say sprintf('%-3s %-22s %-18s %s', 'id', 'title', 'author', 'year');
    for @books -> %b {
        say sprintf('%-3d %-22s %-18s %d', %b<id>, %b<title>, %b<author>, %b<year>);
    }
    say @books.elems, ' books';
}
