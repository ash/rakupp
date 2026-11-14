# An actions class's FALLBACK is called for every rule it has no method for,
# as `FALLBACK($name, $/)`, as in Rakudo (rakudo#2059): it was never called.
# And the actions of `ws` (an implicit `<.ws>` in a rule, or a written one) and
# of a one-literal token called as `<.q>` fire, as Rakudo's do; neither did.

my @log;
sub check($got, $want, $what) {
    state $fails = 0;
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
    $fails
}

grammar G { rule TOP { <a>+ }; token a { x } }
class F { method FALLBACK($name, $/) { @log.push($name) } }
G.parse('xx', actions => F.new);
check @log.join(','), 'a,a,ws,TOP', 'FALLBACK for every rule (rakudo#2059)';

@log = ();
grammar H { token TOP { <p> }; proto token p {*}; token p:sym<y> { y } }
H.parse('y', actions => F.new);
check @log.join(','), 'p:sym<y>,TOP', 'a proto candidate by its own name';

class M { method TOP($/) { make 42 }; method FALLBACK($n, $/) { @log.push($n) } }
@log = ();
check G.parse('x', actions => M.new).made, '42', 'a method still wins';
check @log.join(','), 'a,ws', '…and FALLBACK takes the rest';

class Q { method q($/) { @log.push('q') }; method ws($/) { @log.push('ws') } }
@log = ();
grammar K { token TOP { <.q> 'z' }; token q { 'q' } }
K.parse('qz', actions => Q.new);
check @log.join(','), 'q', 'a one-literal token called as <.q>';
@log = ();
grammar W { rule TOP { a b } }
W.parse('a b', actions => Q.new);
check @log.join(','), 'ws,ws', 'the implicit <.ws> of a rule';
@log = ();
grammar W2 { token TOP { a <.ws> b } }
my $fails = check (W2.parse('a b', actions => Q.new) ?? @log.join(',') !! 'no parse'), 'ws', 'a written <.ws>';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
