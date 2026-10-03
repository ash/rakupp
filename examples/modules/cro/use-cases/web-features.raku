# The rest of what a Cro web service tends to need, end to end.
#
# Middleware (`before` and `after`), a route set `include`d under a prefix,
# static files, a Cro::WebApp template, a response streamed from a Supply,
# server-sent events, slow handlers served in parallel, and a megabyte each
# way. A Cro::HTTP::Client in the same process makes every request.
#
#     rakupp web-features.raku
#
# The port is 20303, or CRO_EXAMPLE_PORT when that is set. The static file and
# the template sit beside this program, in static/ and templates/.

use Cro::HTTP::Router;
use Cro::HTTP::Server;
use Cro::HTTP::Client;
use Cro::WebApp::Template;

my $here = $*PROGRAM.parent;
my @seen;    # what the `before` middleware saw

my $api = route {
    get -> 'version' { content 'application/json', { api => 2 } }
    get -> 'echo', *@rest { content 'text/plain', @rest.join('/') }
}

my $application = route {
    template-location $here.add('templates');
    before { @seen.push(request.target) }
    after { response.append-header('X-Served-By', 'web-features') }
    include api => $api;
    get -> 'static', *@path { static $here.add('static'), @path }
    get -> 'page' {
        template 'page.crotmp', {
            title => 'Shop',
            items => [{ name => 'tea', price => 3 }, { name => 'cake', price => 5 }],
            note  => 'fresh today',
        }
    }
    get -> 'count' {
        content 'text/plain', supply { for 1..5 -> $i { emit "line $i\n".encode } }
    }
    get -> 'events' {
        content 'text/event-stream', supply {
            for <alpha beta gamma> -> $e { emit "event: tick\ndata: $e\n\n".encode }
        }
    }
    get -> 'slow', Int $ms { await Promise.in($ms / 1000); content 'text/plain', "slept $ms" }
    post -> 'upload' {
        request-body-blob -> $blob { content 'text/plain', "got {$blob.bytes} bytes, sum {[+] $blob.list}" }
    }
    get -> 'download' { content 'application/octet-stream', Blob.new(|(^256) xx 4096) }
}

my $port = %*ENV<CRO_EXAMPLE_PORT> // 20303;
my $server = Cro::HTTP::Server.new(:host<127.0.0.1>, :$port, :$application);
$server.start;
LEAVE $server.stop;
my $client = Cro::HTTP::Client.new(base-uri => "http://127.0.0.1:$port");

sub show($label, &request) {
    my $out = try {
        my $resp = await request();
        my $body = await $resp.body;
        "{$resp.status} " ~ ($body ~~ Str ?? $body.subst("\n", '⏎', :g) !! $body.raku);
    }
    $out //= $! ~~ X::Cro::HTTP::Error ?? "{$!.response.status} (error)" !! "died: {$!.message}";
    say "$label: $out";
}

show 'included',     { $client.get('/api/version') };
show 'slurpy',       { $client.get('/api/echo/a/b/c') };
say  'after header: ', (await $client.get('/api/version')).header('X-Served-By');
show 'static',       { $client.get('/static/site.css') };
say  'static type: ', (await $client.get('/static/site.css')).content-type.type-and-subtype;
show 'static 404',   { $client.get('/static/nope.css') };
show 'template',     { $client.get('/page') };
show 'streamed',     { $client.get('/count') };
show 'events',       { $client.get('/events') };

# a streamed body can be read chunk by chunk as it arrives
my $bytes = 0;
react whenever (await $client.get('/count')).body-byte-stream -> $chunk { $bytes += $chunk.bytes }
say "streamed bytes: $bytes";

# three slow requests at once take about as long as the slowest one
my $started = now;
my @replies = (600, 400, 200).map: -> $ms { $client.get("/slow/$ms").then({ await .result.body }) };
say 'parallel: ', (await @replies).join(', ');
say 'overlapped: ', now - $started < 1.0 ?? 'yes' !! 'no';

# a megabyte up, and a megabyte down
my $blob = Blob.new(|(^256) xx 4096);
show 'upload',       { $client.post('/upload', content-type => 'application/octet-stream', body => $blob) };
my $down = await (await $client.get('/download')).body-blob;
say "download: {$down.bytes} bytes, same as sent: {$down eq $blob}";

say "middleware saw {@seen.elems} requests, the first {@seen[0]}";
