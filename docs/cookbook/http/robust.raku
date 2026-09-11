#!/usr/bin/env rakupp
# The same GET, written for a network that misbehaves: a deadline per attempt,
# retries for the failures worth retrying, and one place that decides which is
# which. Needs api-server.raku running in another terminal.

use HTTP::Tiny;
use JSON::Fast;

#| Fetch and decode JSON, or return Nil having said why.
sub get-json(Str $url, Real :$deadline = 2.0, Int :$tries = 3) {
    my $wait = 0.2;

    for 1 .. $tries -> $attempt {
        # HTTP::Tiny has no timeout of its own, so the deadline is imposed from
        # outside: whichever promise finishes first wins.
        my $work = start HTTP::Tiny.new.get($url);
        await Promise.anyof($work, Promise.in($deadline));

        unless $work.status == Kept {
            say "  $attempt: no answer within $deadline s";
            sleep $wait;
            $wait *= 2;
            next;
        }

        my %resp = $work.result;

        # 599 is HTTP::Tiny's own code for "never got an answer" — DNS,
        # connection refused, a socket that died mid-response.
        if %resp<status> == 599 {
            say "  $attempt: { %resp<content>.decode.trim }";
        }
        elsif 400 <= %resp<status> < 500 {
            say "  $attempt: { %resp<status> } { %resp<reason> } — the request is wrong; retrying cannot help";
            return Nil;
        }
        elsif %resp<status> >= 500 {
            say "  $attempt: { %resp<status> } { %resp<reason> }";
        }
        else {
            my $data = try from-json %resp<content>.decode;
            unless $data.defined {
                say "  $attempt: { %resp<status> }, but the body is not JSON";
                return Nil;
            }
            say "  $attempt: { %resp<status> }";
            return $data;
        }

        sleep $wait;
        $wait *= 2;
    }

    say "  gave up after $tries attempts";
    Nil
}

sub MAIN(Str $base = 'http://127.0.0.1:8099') {
    say 'flaky   (503, 503, then 200)';
    my $flaky = get-json("$base/flaky");
    say '  -> ', $flaky.defined ?? $flaky.raku !! 'Nil';

    say 'slow    (answers in 2s, deadline 1s)';
    my $slow = get-json("$base/slow", :deadline(1.0), :tries(2));
    say '  -> ', $slow.defined ?? $slow.raku !! 'Nil';

    say 'missing (404)';
    my $missing = get-json("$base/books/99");
    say '  -> ', $missing.defined ?? $missing.raku !! 'Nil';

    say 'notjson (200, text/plain)';
    my $notjson = get-json("$base/notjson");
    say '  -> ', $notjson.defined ?? $notjson.raku !! 'Nil';

    say 'nothing listening';
    my $down = get-json('http://127.0.0.1:8098/books', :tries(2));
    say '  -> ', $down.defined ?? $down.raku !! 'Nil';
}
