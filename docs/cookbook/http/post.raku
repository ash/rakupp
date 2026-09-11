#!/usr/bin/env rakupp
# POST JSON, read the new resource back, and see what a rejected request looks
# like. Needs api-server.raku running in another terminal.

use HTTP::Tiny;
use JSON::Fast;

my $BASE = 'http://127.0.0.1:8099';

sub MAIN(Str :$title = 'Raku Fundamentals', Str :$author = 'Moritz Lenz', Int :$year = 2020) {
    my $http = HTTP::Tiny.new;

    my %resp = $http.post("$BASE/books",
        content => to-json({ :$title, :$author, :$year }),
        headers => { 'Content-Type' => 'application/json' });

    say 'status   : ', %resp<status>, ' ', %resp<reason>;
    say 'location : ', %resp<headers><location>;

    my %created = from-json %resp<content>.decode;
    say 'created  : ', %created<id>, ' ', %created<title>;

    my %back = from-json $http.get("$BASE/books/%created<id>")<content>.decode;
    say 'read back: ', %back<id>, ' ', %back<title>, ' by ', %back<author>;

    # A rejected request is a status, not an exception: nothing throws here.
    my %bad = $http.post("$BASE/books",
        content => to-json({ title => 'a book with no author' }),
        headers => { 'Content-Type' => 'application/json' });

    say 'refused  : ', %bad<status>, ' ', from-json(%bad<content>.decode)<error>;
}
