# HTML forms with Cro: the two encodings a browser uses, and a file upload.
#
# `request-body -> (:$name, :$colour) { … }` takes the form's fields apart in
# the handler's signature; the same route answers a urlencoded form and a
# multipart one. A multipart part can carry a file, whose name and bytes the
# handler reads from the part object. (Raku++ issue #119 was the first of
# these: the fields arrived empty.)
#
#     rakupp forms.raku
#
# The port is 20302, or CRO_EXAMPLE_PORT when that is set.

use Cro::HTTP::Router;
use Cro::HTTP::Server;
use Cro::HTTP::Client;
use Cro::HTTP::Body;

my $application = route {
    post -> 'order' {
        request-body -> (:$name!, :$colour = 'none', *%rest) {
            content 'text/plain', "order from {~$name} in {~$colour}" ~
                (%rest ?? ", also {%rest.keys.sort.join(',')}" !! '');
        }
    }
    post -> 'upload' {
        request-body -> (:$title!, :$file!) {
            content 'text/plain',
                "{~$title}: {$file.filename}, {$file.body-blob.bytes} bytes, " ~
                "starts {$file.body-text.lines.head.raku}";
        }
    }
    post -> 'raw' {
        request-body -> $form {
            content 'text/plain', $form.^name.split('::').tail ~ ' ' ~
                $form.hash.sort(*.key).map({ "{.key}={~.value}" }).join('&');
        }
    }
}

my $port = %*ENV<CRO_EXAMPLE_PORT> // 20302;
my $server = Cro::HTTP::Server.new(:host<127.0.0.1>, :$port, :$application);
$server.start;
LEAVE $server.stop;

my $client = Cro::HTTP::Client.new(base-uri => "http://127.0.0.1:$port");
sub show($label, &request) {
    my $out = try { my $resp = await request(); "{$resp.status} {await $resp.body-text}" }
    $out //= $! ~~ X::Cro::HTTP::Error ?? "{$!.response.status} (error)" !! "died: {$!.message}";
    say "$label: $out";
}

show 'urlencoded', { $client.post('/order', content-type => 'application/x-www-form-urlencoded',
                                  body => [name => 'Ann', colour => 'teal']) };
show 'multipart',  { $client.post('/order', content-type => 'multipart/form-data',
                                  body => [name => 'Bob', colour => 'red']) };
show 'default',    { $client.post('/order', content-type => 'application/x-www-form-urlencoded',
                                  body => [name => 'Cat']) };
show 'extra',      { $client.post('/order', content-type => 'application/x-www-form-urlencoded',
                                  body => [name => 'Dan', gift => 'yes', note => 'hi']) };
show 'missing',    { $client.post('/order', content-type => 'application/x-www-form-urlencoded',
                                  body => [colour => 'blue']) };
show 'escaped',    { $client.post('/raw', content-type => 'application/x-www-form-urlencoded',
                                  body => ['a b' => 'x&y', 'ключ' => 'знач']) };
show 'raw multi',  { $client.post('/raw', content-type => 'multipart/form-data',
                                  body => [one => '1', two => '2']) };

my $file = Cro::HTTP::Body::MultiPartFormData::Part.new(
    headers => [Cro::HTTP::Header.new(name => 'Content-Type', value => 'text/plain')],
    name => 'file', filename => 'notes.txt',
    body-blob => "first line\nsecond line\n".encode);
show 'upload',     { $client.post('/upload', content-type => 'multipart/form-data',
                                  body => [title => 'Notes', $file]) };
