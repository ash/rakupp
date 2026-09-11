# Cookbook — talking to a JSON API

Fetching JSON, posting JSON, and surviving a network that does not cooperate.
Nothing here needs a third-party service: the recipe ships the API it talks to,
so every program on the page can be run as it stands.

Everything was run under Raku++ 3.27 on macOS 15 (arm64), against the server
below, and the output shown is what it printed.

```sh
rakupp install HTTP::Tiny JSON::Fast Cro::HTTP
```

## The API to talk to

[http/api-server.raku](http/api-server.raku) is a Cro service with five
routes — a list, one book, a create, and two that misbehave on purpose:

```raku
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
        ...
    }
};
```

Start it in one terminal and leave it running:

```sh
rakupp api-server.raku
```

```output
listening on http://127.0.0.1:8099
```

`content 'application/json', @books` is the whole of the serialisation: Cro
takes the data structure and the media type and does the rest. `not-found`,
`bad-request` and `created` are the same idea for the statuses that carry a
body.

## GET and decode

[http/fetch.raku](http/fetch.raku), with `HTTP::Tiny` — one small dependency,
a blocking call, and the response is yours to decode:

```raku
use HTTP::Tiny;
use JSON::Fast;

my %resp = HTTP::Tiny.new.get($url);

unless %resp<success> {
    note "GET $url: %resp<status> %resp<reason>";
    note '  ', %resp<content>.decode.trim if %resp<content>;
    exit 1;
}

my @books = from-json %resp<content>.decode;
```

```sh
rakupp fetch.raku
```

```output
id  title                  author             year
1   Perl 6 Deep Dive       Andrew Shitov      2017
2   Think Perl 6           Laurent Rosenfeld  2017
3   Parsing with Raku      Moritz Lenz        2020
3 books
```

Three details worth keeping. `%resp<content>` is a `Blob`, not a `Str` — a
response is bytes until you say what they mean, hence the `.decode`.
`%resp<success>` is `True` for a 2xx, which is the only status test most
programs need. And header names are lowercased, so it is `%resp<headers><location>`
whatever the server sent.

## POST, and read it back

[http/post.raku](http/post.raku) sends JSON, reads the created resource, and
then makes a request the server refuses:

```raku
my %resp = $http.post("$BASE/books",
    content => to-json({ :$title, :$author, :$year }),
    headers => { 'Content-Type' => 'application/json' });
```

```sh
rakupp post.raku
```

```output
status   : 201 Created
location : books/4
created  : 4 Raku Fundamentals
read back: 4 Raku Fundamentals by Moritz Lenz
refused  : 400 title and author are required
```

Nothing throws on that last line. With `HTTP::Tiny` a 4xx is a status like any
other, which means the decision about what counts as a failure stays in your
program rather than in the client.

The `Content-Type` header is not decoration: drop it and the same body comes
back as `400 title and author are required`, because the server decodes a
request body according to the type it was given, and an unlabelled body never
becomes a hash.

## Four ways a call fails

A request to another machine has more outcomes than "it worked". These are the
four that matter, and they want different answers:

| what happened | how it looks | retry? |
|---|---|---|
| no answer at all | status **599**, reason `Internal Exception` | yes |
| the server is unwell | 5xx | yes, with a pause |
| the request is wrong | 4xx | no — it will fail identically |
| an answer that is not JSON | 200, and `from-json` throws | no |

[http/robust.raku](http/robust.raku) puts all four in one function — a
deadline per attempt, a doubling pause, and one `if` chain that decides which
outcome is which:

```raku
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
            ...
        }

        sleep $wait;
        $wait *= 2;
    }
}
```

Against the four bad endpoints and one dead port:

```sh
rakupp robust.raku
```

```output
flaky   (503, 503, then 200)
  1: 503 Service Unavailable
  2: 503 Service Unavailable
  3: 200
  -> ${:attempt(3), :ok(Bool::True)}
slow    (answers in 2s, deadline 1s)
  1: no answer within 1 s
  2: no answer within 1 s
  gave up after 2 attempts
  -> Nil
missing (404)
  1: 404 Not Found — the request is wrong; retrying cannot help
  -> Nil
notjson (200, text/plain)
  1: 200, but the body is not JSON
  -> Nil
nothing listening
  1: Cannot connect to 127.0.0.1:8098: Connection refused
  2: Cannot connect to 127.0.0.1:8098: Connection refused
  gave up after 2 attempts
  -> Nil
```

The 404 and the unparsable body take one attempt each and stop. That is the
part worth copying: a retry loop that retries everything turns a typo in a URL
into three times the load on someone else's server.

## HTTPS

`Cro::HTTP::Client` speaks TLS, through `IO::Socket::SSL`:

```raku
use Cro::HTTP::Client;

my $resp = await Cro::HTTP::Client.get($url, http => '1.1');
say 'status : ', $resp.status;
say 'type   : ', $resp.header('Content-Type');
say 'bytes  : ', (await $resp.body-text).chars;
```

```sh
rakupp tls.raku
```

```output
status : 200
type   : text/html; charset=utf-8
bytes  : 67813
```

The program is [http/tls.raku](http/tls.raku). Two notes for Raku++ 3.27, both
of which a first attempt runs straight into:

- **`http => '1.1'` is not optional here.** Left to itself the client offers
  HTTP/2 in the TLS handshake, and against a server that accepts it the request
  then hangs rather than answering.
- **`HTTP::Tiny` will not do HTTPS under Raku++**, whatever is installed. It
  decides by asking the module repository for candidates of
  `IO::Socket::SSL:ver<0.0.2+>`, and Raku++'s installation repository does not
  answer that question yet, so the check concludes the module is missing:

  ```output
  599 Internal Exception
  HTTPS requests not supported: IO::Socket::SSL:ver<0.0.2+> must be installed
  ```

  The module is installed and TLS itself works — `IO::Socket::SSL` used
  directly connects and returns `HTTP/1.1 200 OK`. Use `Cro::HTTP::Client` for
  TLS until the repository answers.

## Three things that bite

**599 is not a status the server sent.** `HTTP::Tiny` reports transport
failures — DNS, connection refused, a socket that dies mid-response — as a
response with status 599 and the reason in the body. It is a useful convention,
and it means `%resp<status> >= 500` quietly includes "there was no server at
all" unless you separate them.

**`HTTP::Tiny.new(:timeout(1))` does nothing.** There is no such attribute, and
Raku does not complain about a named argument a class has no use for. The call
runs to completion at whatever pace the far end chooses:

```output
HTTP::Tiny :timeout(1) -> 200 after 2009 ms
```

Bound it from outside, as `get-json` does.

**A deadline does not cancel the request.** `Promise.anyof` returns as soon as
the timer wins, but the `start` block is still sitting in a blocking read, and
it will finish in its own time. That is fine for a program that is about to
retry or exit, and it is not fine in a loop that opens a new request every
second: those threads accumulate. If the far end is reliably slow, the fix is
a smaller request, not a shorter deadline.

## What to reach for next

- `Cro::HTTP::Client.new(base-uri => …)` keeps the host in one place and takes
  paths from then on, which is what you want once there is more than one call.
- Concurrent requests are `start` plus `await`: see
  [doing several things at once](parallel.md).
- For an API that needs a header on every call — a token, an accept type —
  `HTTP::Tiny.new(default-headers => { … })` sets them once.
