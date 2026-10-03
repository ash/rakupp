# A JSON REST API with Cro, and a client that exercises it.
#
# One process: a Cro::HTTP::Server with a small in-memory book store, and a
# Cro::HTTP::Client that walks through it — typed and slurpy path segments,
# query parameters, JSON in and out, 201/404/418/500, a redirect, request
# headers and a cookie jar. Each line printed is one request and what came back.
#
#     rakupp rest-api.raku
#
# The port is 20301, or CRO_EXAMPLE_PORT when that is set.

use Cro::HTTP::Router;
use Cro::HTTP::Server;
use Cro::HTTP::Client;

my %books = 1 => { title => 'Think Raku', year => 2020 };
my $next-id = 2;

my $application = route {
    get -> { content 'text/plain', 'home' }
    get -> 'hello', $name { content 'text/plain', "Hello, $name!" }
    get -> 'add', Int $a, Int $b { content 'text/plain', ~($a + $b) }
    get -> 'search', :$q!, :$limit = '10' { content 'text/plain', "q=$q limit=$limit" }
    get -> 'books' {
        content 'application/json', [%books.sort(*.key.Int).map({ %(id => .key.Int, |.value) })];
    }
    get -> 'books', Int $id {
        with %books{$id} { content 'application/json', %(id => $id, |$_) }
        else { not-found }
    }
    post -> 'books' {
        request-body -> (:$title!, :$year!) {
            my $id = $next-id++;
            %books{$id} = { :$title, :$year };
            created "/books/$id", 'application/json', %(:$id, :$title, :$year);
        }
    }
    delete -> 'books', Int $id {
        %books{$id}:delete ?? content('text/plain', 'deleted') !! not-found;
    }
    get -> 'headers' { content 'text/plain', 'agent=' ~ (request.header('X-Agent') // 'none') }
    get -> 'old' { redirect :permanent, '/hello/redirected' }
    get -> 'visits' {
        my $n = (request.cookie-value('visits') // 0) + 1;
        set-cookie 'visits', ~$n;
        content 'text/plain', "visit $n";
    }
    get -> 'teapot' { response.status = 418; content 'text/plain', 'short and stout' }
    get -> 'boom' { die 'kaboom' }
}

my $port = %*ENV<CRO_EXAMPLE_PORT> // 20301;
my $server = Cro::HTTP::Server.new(:host<127.0.0.1>, :$port, :$application);
$server.start;
LEAVE $server.stop;

my $client = Cro::HTTP::Client.new(base-uri => "http://127.0.0.1:$port");

# one request: its status and body, or the status of the error it raised
sub show($label, &request) {
    my $out = try {
        my $resp = await request();
        my $body = await $resp.body;
        "{$resp.status} {$body ~~ Str ?? $body !! $body.raku}";
    }
    $out //= $! ~~ X::Cro::HTTP::Error ?? "{$!.response.status} (error)" !! "died: {$!.message}";
    say "$label: $out";
}

show 'root',          { $client.get('/') };
show 'path segment',  { $client.get('/hello/Raku') };
show 'typed segment', { $client.get('/add/2/40') };
show 'wrong type',    { $client.get('/add/x/1') };
show 'query',         { $client.get('/search?q=cro&limit=3') };
show 'query default', { $client.get('/search?q=raku') };
show 'list',          { $client.get('/books') };
show 'one',           { $client.get('/books/1') };
show 'missing',       { $client.get('/books/99') };
show 'create',        { $client.post('/books', content-type => 'application/json',
                                     body => { title => 'Raku Fundamentals', year => 2023 }) };
show 'list again',    { $client.get('/books') };
show 'delete',        { $client.delete('/books/1') };
show 'delete again',  { $client.delete('/books/1') };
show 'header',        { $client.get('/headers', headers => [X-Agent => 'rest-api/1.0']) };
show 'redirect',      { $client.get('/old') };
show 'teapot',        { $client.get('/teapot') };
show 'server error',  { $client.get('/boom') };

# a client with a cookie jar sends back what the server set
my $jar = Cro::HTTP::Client.new(base-uri => "http://127.0.0.1:$port", :cookie-jar);
show 'cookie 1',      { $jar.get('/visits') };
show 'cookie 2',      { $jar.get('/visits') };
show 'no jar',        { $client.get('/visits') };
