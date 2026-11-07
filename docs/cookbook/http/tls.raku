#!/usr/bin/env rakupp
# HTTPS. Cro::HTTP::Client speaks TLS through IO::Socket::SSL.

use Cro::HTTP::Client;

sub MAIN(Str $url = 'https://raku.online/') {
    my $resp = await Cro::HTTP::Client.get($url);
    say 'status : ', $resp.status;
    say 'type   : ', $resp.header('Content-Type');
    say 'bytes  : ', (await $resp.body-text).chars;
}
