#!/usr/bin/env rakupp
# Four requests that each take two seconds: one after another, and then all at
# once. Needs the API server from the HTTP recipe (docs/cookbook/http/):
#
#   rakupp api-server.raku

use HTTP::Tiny;

my $URL = 'http://127.0.0.1:8099/slow';

sub took($t0) { ((now - $t0) * 1000).Int ~ ' ms' }

sub MAIN(Int $n = 4) {
    my @urls = $URL xx $n;

    my $t0 = now;
    my @one-by-one = @urls.map({ HTTP::Tiny.new.get($_)<status> });
    say "one after another : @one-by-one[] in { took $t0 }";

    # start returns immediately with a Promise; the requests overlap, and the
    # await collects them in the order they were started.
    $t0 = now;
    my @promises = @urls.map(-> $u { start HTTP::Tiny.new.get($u)<status> });
    my @together = await @promises;
    say "all at once       : @together[] in { took $t0 }";

    # The mistake this replaces: awaiting inside the loop is the sequential
    # version again, with threads.
    $t0 = now;
    my @wrong = @urls.map(-> $u { await start HTTP::Tiny.new.get($u)<status> });
    say "await in the loop : @wrong[] in { took $t0 }";
}
