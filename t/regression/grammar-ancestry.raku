# Regression: `grammar G {} ; G ~~ Grammar` answered False, and G.^mro was
# (G, Any, Mu) — a grammar declaration had NO Grammar ancestor at all, where
# Rakudo derives it from Grammar (whose own chain is Match, Capture, Cool).
#
# The fix rides the existing nativeParent seam: a parentless grammar gets
# nativeParent = "Grammar", the Grammar/Match/Capture family joins the
# built-in ancestry tables, and the type-object conformance walk — which
# ignored nativeParent entirely (so `class F is Str; F ~~ Cool` was False
# too) — now follows a built-in parent's whole ancestry. Match VALUES are
# also Capture/Cool per the oracle.
#
# Found building the G0 grammar-service shim, which had to validate compiled
# grammars via .can('parse') because ~~ Grammar could not work (the comment
# is still there — the duck check stays, it also accepts hand-rolled
# parser classes).
#
# Every check verified against Rakudo (this file runs on both engines).
#
# Contract: exit 0 + last line PASS.
my @fail;

sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

grammar G { token TOP { \w+ } }
grammar H is G { token TOP { \d+ } }
class F is Str { }

check ?(G ~~ Grammar), True,  'G ~~ Grammar';
check ?(H ~~ Grammar), True,  'a derived grammar still ~~ Grammar';
check ?(G ~~ Str),     False, 'a grammar is not Str';
check G.^mro.map(*.^name).join(','), 'G,Grammar,Match,Capture,Cool,Any,Mu', 'G.^mro';
check H.^mro.map(*.^name).join(','), 'H,G,Grammar,Match,Capture,Cool,Any,Mu', 'H.^mro';
check G.^parents.map(*.^name).join(','), 'Grammar,Match,Capture', 'G.^parents (no hidden Cool, no Any/Mu)';
check F.^parents.map(*.^name).join(','), 'Str', 'built-in parent shows in .^parents';
check ?(F ~~ Cool),     True, 'class F is Str ~~ Cool (native-parent ancestry)';
check ?G.isa(Match),    True, 'G.isa(Match)';
check ?G.isa(Cool),     True, 'G.isa(Cool)';
check ?G.does(Grammar), True, 'G.does(Grammar)';
check ?(G.new ~~ Grammar), True, 'a grammar INSTANCE ~~ Grammar';
check Match.^mro.map(*.^name).join(','),   'Match,Capture,Cool,Any,Mu',         'Match.^mro';
check Grammar.^mro.map(*.^name).join(','), 'Grammar,Match,Capture,Cool,Any,Mu', 'Grammar.^mro';
check Capture.^mro.map(*.^name).join(','), 'Capture,Any,Mu',                    'Capture.^mro';

my $m = "ab" ~~ /\w+/;
check ?($m ~~ Match),   True, 'a match ~~ Match';
check ?($m ~~ Capture), True, 'a match ~~ Capture';
check ?($m ~~ Cool),    True, 'a match ~~ Cool';
check ?(G.parse('hi') ~~ Match), True, 'parse result ~~ Match';

# the default constructor still refuses positionals — the implicit Grammar
# ancestor must not look like a built-in with a positional .new
try { G.new(42) }
check ?$!, True, 'G.new(42) still refused';

check (do given G { when Grammar { 'g' }; default { 'other' } }), 'g', 'when Grammar dispatches';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
