# Regression: language behaviour from the fourth batch of audited mutsu gap
# files (2026-10-05).
#
# - A user trait on an anonymous sub (`sub (…) is memoized(%c, {…}) {…}`)
#   runs, with every argument; a block among the arguments is no body.
# - `.BIND-KEY($k, EXPR)` evaluates EXPR once (callsame in a wrapper).
# - `last`/`next` inside lives-ok/dies-ok escape to an enclosing loop and
#   record no test; an unmet plan says "# You planned N tests, but ran M".
# - `do 1 if $x if $x` is a syntax error, as the statement form is.
#
# Every expectation below was checked against Rakudo.

use Test;

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "# ok - $desc" }
    else { $fails++; say "# FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my @trait-args;
multi sub trait_mod:<is>(Sub:D \routine, List:D :$memoized!) {
    @trait-args.push($memoized.elems);
    my \cache = $memoized[0]<>;
    my &keyer = $memoized[1];
    routine.wrap(-> |capture {
        my $key = keyer(capture);
        cache.EXISTS-KEY($key) ?? cache.AT-KEY($key) !! cache.BIND-KEY($key, callsame)
    });
}
my %cache;
my $body-calls = 0;
my &double = sub ($value) is memoized(%cache, { .[0] }) { ++$body-calls; $value * 2 };
ck(@trait-args.List, (2,), 'an anonymous sub trait runs once, with both arguments');
ck((double(3), double(3), $body-calls), (6, 6, 1), '... and its wrapper caches');

my $evals = 0;
my %h;
%h.BIND-KEY('k', do { ++$evals; 1 });
ck($evals, 1, 'BIND-KEY evaluates its value once');

my @seen;
for 1..3 {
    @seen.push: "start $_";
    lives-ok { last }, 'never recorded';
    @seen.push: 'after';
}
ck(@seen.List, ('start 1',), 'last escapes lives-ok to the enclosing loop');

ck((try { EVAL 'sub f($x) { do return 1 unless $x if $x if $x }'; 'parsed' } // 'refused'), 'refused',
   'two conditional modifiers after do');

my $out = run($*EXECUTABLE, '-e', 'use Test; plan 1;', :out, :err);
ck($out.out.slurp(:close).lines.List, ('1..1', '# You planned 1 test, but ran 0'), 'an unmet plan says so');
$out.err.slurp(:close);

done-testing;
say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
