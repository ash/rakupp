#!/usr/bin/env raku
# A command-line client for JSON APIs — the curl+jq of this repository, built
# almost entirely out of our own modules: HTTP::Simple carries the request
# (plain sockets, TLS, redirects, retries) and JSON::Native does every piece of
# JSON work — validating the request body, parsing the response, and printing
# the result. Where the json/ showcase parses JSON with a grammar it owns, this
# one is the ecosystem answer: the program is just the argument parser and the
# glue.
#
#   jsonreq.raku URL                              # GET, pretty-print the JSON
#   jsonreq.raku URL --query=.users[0].name       # pull one value out
#   jsonreq.raku POST URL --json='{"a": 1}'       # send JSON (implies POST)
#   jsonreq.raku URL --header='X-Token: t' -v     # headers; -v shows the exchange
#   jsonreq.raku data.json --query=.users[-1]     # a local file — no socket at all
#   cat data.json | jsonreq.raku -                # or stdin
#
# The named options go anywhere on the command line — URL first reads naturally
# — which %*SUB-MAIN-OPTS<named-anywhere> asks of the MAIN dispatcher.
my %*SUB-MAIN-OPTS = :named-anywhere;

use JSON::Native;
use HTTP::Simple;

my @METHODS = <GET POST PUT PATCH DELETE HEAD OPTIONS>;

#| request a URL and print the JSON that comes back
sub MAIN(
    $method-or-url,           #= GET/POST/… (optional), then the URL
    $url?,
    Str  :q(:$query),         #= extract one value: .users[0].name, [-1].id
    Bool :c(:$compact) = False,   #= minified output instead of pretty-printed
    Bool :r(:$string)  = False,   #= print a Str result bare, like jq -r
    Bool :$sorted      = False,   #= sort object keys (stable output for diffs)
    Bool :$raw         = False,   #= print the body untouched, whatever it is
    :@header,                 #= repeatable: --header='Name: value'
    Str  :$json,              #= request body: JSON text, @file, or - for stdin
    Str  :$auth,              #= user:pass — HTTP Basic
    Str  :$bearer,            #= token — Authorization: Bearer
    Real :$timeout  = 30,     #= seconds for the whole exchange
    Bool :$insecure = False,  #= skip TLS certificate verification
    Bool :v(:$verbose) = False,   #= the exchange, on stderr
) {
    CATCH {
        default {
            note "jsonreq: " ~ .message;
            exit 2;
        }
    }

    # jsonreq URL / jsonreq METHOD URL; a body with no method means POST
    my $method;
    my $target;
    if $url.defined {
        $method = $method-or-url.uc;
        $target = $url;
        die "unknown method '$method-or-url' (expected one of @METHODS[])"
            unless $method eq any(@METHODS);
    }
    else {
        $method = $json.defined ?? 'POST' !! 'GET';
        $target = $method-or-url;
    }

    # A target with no http(s):// scheme is a LOCAL document — a path, a
    # file:// URL, or `-` for stdin. Same query/pretty-print pipeline, no
    # socket involved; it is presumed JSON (that is why you pointed jsonreq
    # at it), so a parse error surfaces instead of the file echoing through.
    # --raw still prints it untouched.
    my $text;
    my $ctype  = '';
    my $failed = False;   # HTTP >= 400: exit 1 after the body has printed
    if !($target.starts-with('http://') || $target.starts-with('https://')) {
        my $path = $target.starts-with('file://') ?? $target.substr(7) !! $target;
        die "a local file takes no method ('$method-or-url')" if $url.defined;
        die "--json, --header, --auth, --bearer and --insecure need a URL, not a local file"
            if $json.defined || @header || $auth.defined || $bearer.defined || $insecure;
        if $path eq '-' {
            $text = $*IN.slurp;
        }
        else {
            die "no such file '$path'" unless $path.IO.e;
            $text = $path.IO.slurp;
        }
        $ctype = 'application/json';
        note "* local $path  (JSON via JSON::Native, {json-backend()} backend)"
            if $verbose;
    }
    else {
        my %opt = timeout => $timeout;
        %opt<insecure> = True if $insecure;
        %opt<auth>     = $auth   if $auth.defined;
        %opt<bearer>   = $bearer if $bearer.defined;

        my %headers;
        for @header -> $h {
            my ($name, $value) = $h.split(':', 2);
            die "bad --header '$h' (expected 'Name: value')" unless $value.defined;
            %headers{$name.trim.lc} = $value.trim;
        }
        %opt<headers> = %headers if %headers;

        # The body is parsed BEFORE it is sent: a request body this program
        # puts on the wire is valid JSON, canonicalized by the same to-json
        # that prints responses. A typo dies here, not as a server-side 400.
        if $json.defined {
            my $body = $json eq '-'            ?? $*IN.slurp
                    !! $json.starts-with('@')  ?? $json.substr(1).IO.slurp
                    !!                            $json;
            my $data = try from-json($body);
            die "request body is not valid JSON: " ~ ($! // 'parse failed') without $data;
            %opt<body>         = to-json($data, :!pretty);
            %opt<content-type> = 'application/json';
        }

        note "* $method $target  (JSON via JSON::Native, {json-backend()} backend)"
            if $verbose;

        my $resp = http-request($method, $target, |%opt);

        if $verbose {
            for $resp.history -> $hop {
                note "< HTTP {$hop.status} {$hop.reason} -> {$hop.header('location')}";
            }
            note "< HTTP {$resp.status} {$resp.reason}";
            for $resp.headers.keys.sort -> $k {
                note "< $k: " ~ $resp.header($k);
            }
        }

        $text   = $resp.text;
        $ctype  = $resp.content-type;
        $failed = $resp.status >= 400;
    }

    if $raw {
        print $text;
        print "\n" if $text.chars && !$text.ends-with("\n");
    }
    elsif !$text.chars {
        # HEAD, 204, an empty 200 — nothing to print (use -v for the headers)
    }
    else {
        my $is-json = $ctype.contains('json')
                   || $text.trim.starts-with('{')
                   || $text.trim.starts-with('[');
        if !$is-json {
            die "response is {$ctype || 'untyped'}, not JSON (--raw prints it anyway)"
                if $query.defined;
            print $text;
            print "\n" unless $text.ends-with("\n");
        }
        else {
            my $data = from-json($text);
            $data = extract($data, $query) if $query.defined;
            if $string && $data ~~ Str {
                say $data;
            }
            else {
                my %ser = pretty => !$compact;
                %ser<sorted-keys> = True if $sorted;
                say to-json($data, |%ser);
            }
        }
    }

    # like curl --fail, but the (JSON) error body still prints above
    exit 1 if $failed;
}

# ---------- jq-lite ------------------------------------------------------
# A path is `.key` and `[index]` steps: .users[0].name, [-1].id — the same
# little language as the json/ showcase, plus negative indexes from the end.
sub extract($data is copy, Str $path) {
    my @steps = $path.comb(/ '.' <-[.\[]>+ || '[' '-'? \d+ ']' /);
    die "cannot parse query '$path'" unless @steps.join eq $path;
    for @steps -> $step {
        if $step.starts-with('[') {
            my $i = $step.substr(1, $step.chars - 2).Int;
            die "not an array at '$step'" unless $data ~~ Positional;
            my $n = $data.elems;
            my $j = $i < 0 ?? $n + $i !! $i;
            die "index $i out of range (0..{$n - 1})" unless 0 <= $j < $n;
            $data = $data[$j];
        }
        else {
            my $key = $step.substr(1);
            die "no key '$key'" unless $data ~~ Associative && ($data{$key}:exists);
            $data = $data{$key};
        }
    }
    $data
}
