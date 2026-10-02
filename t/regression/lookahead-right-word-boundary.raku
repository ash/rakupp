# Regression (issue #105): `>>` (the right word boundary) inside a lookahead is
# the boundary, not the end of the assertion. Contract: exit 0 + last line PASS.
my @fail;
@fail.push('A') unless 'X'  ~~ / <!before 'ab' >> > X /;
@fail.push('D') unless 'X'  ~~ / <!before [ 'ab' >> ] > X /;
@fail.push('E') unless 'X'  ~~ / <!before 'ab' >> \s > X /;
@fail.push('F') unless 'ab' ~~ / <?before 'ab' >> > 'ab' /;
@fail.push('H') unless 'X'  ~~ / <!before 'ab' >>> X /;
@fail.push('neg') if 'ab c' ~~ / ^ <!before 'ab' >> > /;
grammar G { token TOP { <?before <b>> . }; token b { a } }
@fail.push('nested') unless G.parse('a');
.say for @fail;
say @fail ?? 'FAIL' !! 'PASS';
