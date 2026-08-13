# Splits a grammar parse's cost into MATCHING vs RESULT-BUILDING.
#
# Two JSON grammars with byte-identical rule bodies; JNC dots every subrule
# call (<.value> …), so it does the same matching work but never builds
# captures, children maps, or the user-visible Match tree. The timing gap
# between them is the result-building share of a capturing parse.
#
#     rakupp grammar-split.raku both 400 5
#     raku   grammar-split.raku both 400 5
#     mode = cap | nocap | both | loop      (loop = parse forever, for `sample`)
#
# Runs unchanged under both engines — the comparison IS the measurement.
#
# (2026-08-13: the default true-LTM ranker pruned this grammar's `value`
# branch outright — `X* % sep` with a recursive element hid its prefix
# accept behind the separator; fixed in LtmNfa.cpp the same day, see
# t/regression/ltm-declarative-prefix.raku t16-t18. Numbers below predate
# the fix and were taken under RAKUPP_LTM=0; the probe ranker double-
# executes branches, so its matching share is inflated — the
# result-building share is a floor.)
#
# Measured 2026-08-13 (build-arm64 @ 931777c, 51 KB input, RAKUPP_LTM=0):
#   rakupp  cap 57.6 ms/parse   nocap 21.8 ms   → 62% result-building
#   rakudo  cap 151.3 ms/parse  nocap 100.9 ms  → 33% result-building
# sample(1) decomposition of the rakupp capturing parse:
#   47% GrammarMatcher::parse (matching + memo/children construction inline)
#   28% ~GrammarMatcher — destroying the packrat memo's ParseNode trees
#   17% ParseNode→Match conversion ($_7 in grammarParse)
#    8% setMatchVar destroying the previous parse's $/ tree
# Same day, after the FlatMap children + memo-reaper batch
# (REPRESENTATION-PLAN.md phase-1 batch 3): teardown off the main thread
# (main-thread shares now 76/~0/15.5/8), quiet-machine wall clock
# cap 50-53 ms (−6% interleaved vs pre-batch), default-LTM cap 57-59 ms.

grammar J {
    token TOP      { \s* <value> \s* }
    token value    { <object> | <array> | <string> | <number> | <jtrue> | <jfalse> | <jnull> }
    token object   { '{' \s* [ <pair>* % [ \s* ',' \s* ] ] \s* '}' }
    token pair     { <string> \s* ':' \s* <value> }
    token array    { '[' \s* [ <value>* % [ \s* ',' \s* ] ] \s* ']' }
    token string   { \x22 <strchar>* \x22 }
    token strchar  { <-[ \x22 \\ ]> | '\\' . }
    token number   { '-'? \d+ [ '.' \d+ ]? [ <[eE]> <[+-]>? \d+ ]? }
    token jtrue    { 'true' }
    token jfalse   { 'false' }
    token jnull    { 'null' }
}

grammar JNC {
    token TOP      { \s* <.value> \s* }
    token value    { <.object> | <.array> | <.string> | <.number> | <.jtrue> | <.jfalse> | <.jnull> }
    token object   { '{' \s* [ <.pair>* % [ \s* ',' \s* ] ] \s* '}' }
    token pair     { <.string> \s* ':' \s* <.value> }
    token array    { '[' \s* [ <.value>* % [ \s* ',' \s* ] ] \s* ']' }
    token string   { \x22 <.strchar>* \x22 }
    token strchar  { <-[ \x22 \\ ]> | '\\' . }
    token number   { '-'? \d+ [ '.' \d+ ]? [ <[eE]> <[+-]>? \d+ ]? }
    token jtrue    { 'true' }
    token jfalse   { 'false' }
    token jnull    { 'null' }
}

sub gen(Int $records) {
    my @items;
    for ^$records -> $i {
        my $score = ($i * 7) % 1000;
        my $frac  = $i % 100;
        @items.push: '{"id":' ~ $i
            ~ ',"name":"user' ~ $i ~ '"'
            ~ ',"active":' ~ ($i % 2 ?? 'true' !! 'false')
            ~ ',"score":' ~ $score ~ '.' ~ $frac
            ~ ',"tags":["alpha","beta","gamma"]'
            ~ ',"nested":{"x":' ~ $i ~ ',"y":null,"z":[1,2,3]}}';
    }
    '[' ~ @items.join(',') ~ ']'
}

sub bench($g, $name, $json, $reps) {
    my $ok = 0;
    my $t0 = now;
    for ^$reps {
        my $m = $g.parse($json);
        $ok++ if $m;
    }
    my $dt = now - $t0;
    printf "%-6s %d/%d parses ok, %.1f ms/parse\n", $name, $ok, $reps, 1000 * $dt / $reps;
    die "bad parse — never time a no-op" unless $ok == $reps;
    $dt / $reps
}

my $mode    = @*ARGS[0] // 'both';
my $records = (@*ARGS[1] // 400).Int;
my $reps    = (@*ARGS[2] // 5).Int;

my $json = gen($records);
say "input: {$json.chars} chars, $records records";

if $mode eq 'loop' {
    loop {
        J.parse($json);
    }
}

my $tcap = bench(J,   'cap',   $json, $reps) if $mode eq 'cap'   || $mode eq 'both';
my $tnc  = bench(JNC, 'nocap', $json, $reps) if $mode eq 'nocap' || $mode eq 'both';

if $mode eq 'both' {
    printf "result-building share: %.0f%% of a capturing parse\n", 100 * ($tcap - $tnc) / $tcap;
    printf "speedup if capture cost went to zero: %.2fx\n", $tcap / $tnc;
}
