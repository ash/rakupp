#!/usr/bin/env rakupp
# A JSON API to talk to, so the rest of this recipe needs no third party.
#
#   rakupp api-server.raku            # serves http://127.0.0.1:8099
#
# GET  /books        -> the list
# GET  /books/<id>   -> one book, or 404
# POST /books        -> add one, 201 with the new book
# GET  /slow         -> answers after 2 seconds, for the timeout example
# GET  /flaky        -> 503 twice, then 200, for the retry example
# GET  /notjson      -> 200 with text/plain, for the malformed-JSON example

use Cro::HTTP::Router;
use Cro::HTTP::Server;

my @books =
    { :id(1), :title("Perl 6 Deep Dive"),  :author("Andrew Shitov"),     :year(2017) },
    { :id(2), :title("Think Perl 6"),      :author("Laurent Rosenfeld"), :year(2017) },
    { :id(3), :title("Parsing with Raku"), :author("Moritz Lenz"),       :year(2020) };

my $flaky-hits = 0;

my $app = route {
    get -> 'books' {
        content 'application/json', @books;
    }

    # An Int in the signature is the route's type check: /books/x never
    # reaches this handler, it is simply not a match.
    get -> 'books', Int $id {
        with @books.first(*.<id> == $id) -> %book {
            content 'application/json', %book;
        }
        else {
            not-found 'application/json', { error => "no book $id" };
        }
    }

    post -> 'books' {
        my %new = await request.body;
        if %new<title>:exists && %new<author>:exists {
            my %book = id => @books.map(*.<id>).max + 1, |%new;
            @books.push(%book);
            created "books/%book<id>", 'application/json', %book;
        }
        else {
            bad-request 'application/json', { error => 'title and author are required' };
        }
    }

    get -> 'slow' {
        sleep 2;
        content 'application/json', { slept => 2 };
    }

    # Fails twice, then works: something to point a retry loop at.
    get -> 'flaky' {
        $flaky-hits++;
        if $flaky-hits %% 3 {
            content 'application/json', { ok => True, attempt => $flaky-hits };
        }
        else {
            response.status = 503;
            content 'application/json', { error => 'try again', attempt => $flaky-hits };
        }
    }

    # 200, and not JSON at all.
    get -> 'notjson' {
        content 'text/plain', 'not json at all';
    }
};

my Cro::Service $service = Cro::HTTP::Server.new(:host<127.0.0.1>, :port(8099), :application($app));
$service.start;
say "listening on http://127.0.0.1:8099";
react whenever signal(SIGINT) { $service.stop; exit }
