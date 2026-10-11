# Regression: grammar and regex issues #147, #150, #152, #153 (2026-10-11).
#
#   #150 a rule's arguments arrived as strings: `<.on(False)>` bound "False",
#        which is true, so the rule matched with its flag off.
#   #152 `G.Grammar::parse(…)` ran G's own `parse` override, not Grammar's.
#   #153 `G.parse` answered a plain Match: `.^name` was Match, `~~ G` False,
#        and G's methods were not there.
#   #147 a regex's code blocks ran in the scope that MATCHED it, not the one
#        it was written in: a lexical sub (or variable) there was undeclared.
#
# Expected answers are Rakudo 2026.09's (2026-10-11).

my $fails = 0;
sub check($got, $want, $what) {
    return if $got eqv $want;
    $fails++;
    say "not ok - $what\n  got:  {$got.raku}\n  want: {$want.raku}";
}

# --- #150
grammar G150 {
    token show($v = False) { <?{ @*SEEN.push: $v.^name ~ ':' ~ $v; True }> }
    token on($flag = False) { <?{ $flag }> }
    token typed(Bool $flag) { <?{ $flag }> }
    token args      { <.show(True)> <.show(42)> <.show('x')> <.show> <.show(1 + 1)> <.show(-3)> }
    token off       { <.on(False)> \w+ }
    token default   { <.on> \w+ }
    token yes       { <.on(True)> \w+ }
    token typed-off { <.typed(False)> \w+ }
    token rep($c)   { $c }
    token reps      { <rep('ab')> <rep(3)> }
    multi token pred(0) { 'z' }
    multi token pred($n) { 'x' <pred($n - 1)> }
    token preds     { <pred(2)> }
}
my @*SEEN;
G150.parse('', :rule<args>);
check @*SEEN.join(' '), 'Bool:True Int:42 Str:x Bool:False Int:2 Int:-3', 'rule arguments keep their types';
check ?G150.parse('abc', :rule<off>), False, '<.on(False)> does not match';
check ?G150.parse('abc', :rule<default>), False, '<.on>, default False, does not match';
check ?G150.parse('abc', :rule<yes>), True, '<.on(True)> matches';
check ?G150.parse('abc', :rule<typed-off>), False, '<.typed(False)> does not match';
check ?G150.parse('ab3', :rule<reps>), True, 'an argument interpolated into the pattern is its Str';
check ?G150.parse('xxz', :rule<preds>), True, 'literal multi candidates still dispatch on the value';

# --- #152
my @ran;
grammar G152 {
    token TOP { \w+ }
    method parse($t, |c) { @ran.push: 'own'; callsame }
}
check ~G152.Grammar::parse('abc'), 'abc', 'G.Grammar::parse parses';
check ~G152.new.Grammar::parse('def'), 'def', 'G.new.Grammar::parse parses';
check @ran.elems, 0, 'a qualified Grammar::parse skips the override';
check ~G152.parse('ghi'), 'ghi', 'the plain call still runs the override…';
check @ran.elems, 1, '…exactly once';

# --- #153
grammar G153 {
    token TOP { <x> }
    token x { a }
    method greet { 'hello from ' ~ self.Str }
}
my $m = G153.parse('a');
check $m.^name, 'G153', 'the parse result is a G';
check ($m ~~ G153), True, '~~ G';
check ($m ~~ Match), True, '~~ Match';
check $m.greet, 'hello from a', "G's own method answers on it";
check $m<x>.^name, 'G153', 'and so is every match inside it';
check G153.subparse('a').^name, 'G153', 'subparse too';
check ('a' ~~ /a/).^name, 'Match', 'an ordinary match stays a Match';
class A153 { method TOP($/) { make $/.^name } }
check G153.parse('a', :actions(A153)).made, 'G153', "an action's \$/ is a G";

# --- #147
my $rx = do {
    my sub two-letters($s) { $s.chars == 2 }
    rx/ \w+ <?{ two-letters(~$/) }> /
};
check ?('ab' ~~ $rx), True, 'a lexical sub in a code assertion, matched outside its scope';
check ~('abc' ~~ $rx), 'ab', '…backtracking into it as before';
my $rx2 = do { my $n = 2; rx/ \w+ <?{ $/.chars == $n }> / };
check ?('ab' ~~ $rx2), True, 'a lexical variable in a code assertion, likewise';
sub mk { my $hits = 0; my sub bump { ++$hits }; (rx/ a { bump() } /, { $hits }) }
my ($r3, $count) = mk();
'a' ~~ $r3 for ^3;
check $count(), 3, 'a code block writes the scope it closed over';
my $outer = 5;
'a5' ~~ / a { $outer++ } \d /;
check $outer, 6, 'a literal regex still sees the scope it is matched in';

if $fails { say "$fails failure(s)"; exit 1 }
say 'PASS';
