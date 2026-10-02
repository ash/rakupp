# Regression (issue #114): `[**]` flattens every leaf, however deep, on any
# base — an EVAL result, a call, a parenthesised list, a Range, a Seq — not only
# on an @-variable. `EVAL($s)[**]` answered the top level unflattened.
# Contract: exit 0 + last line PASS.
use MONKEY-SEE-NO-EVAL;
my @fail;
sub want($label, $got, $want) { @fail.push("$label: got {$got.raku}") unless $got eqv $want }
want 'EVAL',   EVAL('[[[1,2,3,5],4,5],7,8,[6,1,2]]')[**].sort.unique.List, (1, 2, 3, 4, 5, 6, 7, 8);
sub f { [[1, 2], 3] }
want 'call',   f()[**].List,                  (1, 2, 3);
want 'parens', ([[1, 2], 3])[**].List,        (1, 2, 3);
want 'nested lists', (1, (2, (3, 4)))[**].List, (1, 2, 3, 4);
want 'range inside', (1, (2..3))[**].List,    (1, 2, 3);
want 'gather', (gather { take [1, [2]]; take 3 })[**].List, (1, 2, 3);
want 'scalar', 5[**].List,                    (5,);
my $calls = 0;
sub g { $calls++; [1, [2]] }
g()[**];
@fail.push("base evaluated $calls times") unless $calls == 1;
.say for @fail;
say @fail ?? 'FAIL' !! 'PASS';
