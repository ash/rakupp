# Regression: two threads wrote the same `$/` at once and freed its old value
# twice. A subset's `where /…/` sets the `$/` of the scope the subset was
# declared in, and that scope is every thread's; a grammar's inline `{ make …
# }` saved and restored `$/` through a lookup that walked on past the routine
# into such a scope. Cro::HTTP checks `subset Method of Str where /…/` for
# each request it builds and parses each header with such a grammar, and its
# t/http-session-inmemory.rakutest, whose two clients run at once, crashed in
# about one run in five. A check off the main thread now keeps its match, and
# the save and restore stop at the routine's own `$/`.
#
# Expectations checked against Rakudo 2026.09
# (~/.rakubrew/versions/moar-2026.09/install/bin/rakudo). Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

grammar Header {
    token TOP { <name> ':' \s* <value> }
    token name { <[\w-]>+ { make ~$/ } }
    token value { \S+ { make ~$/ } }
}
class Req {
    subset Method of Str where /^<[A..Z]>+$/;
    has Method $.method;
    method header(Str $line) { given Header.parse($line) { .<name>.made ~ '=' ~ .<value>.made } }
}

"x" ~~ /x/;    # the mainline has a `$/` of its own
my @p = (^8).map: -> $t { start {
    my $n = 0;
    for ^3000 {
        my $r = Req.new(method => 'GET');
        $n++ if $r.method eq 'GET' && Req.header("X-$t: v$_") eq "X-$t=v$_";
    }
    $n
} };
ck (await @p).List, (3000 xx 8).List,
    'subset checks and grammar parses on eight threads at once';

# …and on the main thread a `where /…/` still leaves its match in the scope
# the subset was declared in, as Rakudo's does
subset S of Str where /a(.)/;
my S $x = "abc";
ck ~$/[0], 'b', "a main-thread check's match is the declaring scope's `\$/`";

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
