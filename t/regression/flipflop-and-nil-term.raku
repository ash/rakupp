# Conformance (behaviour matrix): the flip-flop family, and `Nil` as an operand.
#
# `Nil` is a TERM, never a routine. The parser fell through to its general
# identifier path, so anything that could start a listop argument turned into a
# call: `Nil ~ 1` parsed as `Nil(~1)` and died "No such method 'Nil'", `Nil ff 1`
# as `Nil(ff 1)`. That one parse fix covers ~9 operator rows at once.
#
# The flip-flop itself was implemented but unreachable in three of its eight
# spellings: `^ff`, `ff^` and `^ff^` lexed as three tokens, so the carets read as
# prefix/postfix on the operands. It also answered Any while OFF where Rakudo
# answers Nil.
#
# And a bare `Nil` operand was excluded from set coercion, so `Nil (|) 1` lost it —
# while a Nil INSIDE a list had always been kept, which was the tell.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }
# NOTE: each flip-flop must sit at a FIXED source position with the loop around it.
# Wrapping one in a `.map` closure is not equivalent — Rakudo gives that a fresh
# latch per invocation while rakupp keys state on the source occurrence, which is a
# real divergence but a different one from what this file pins.
my (@ff, @xff, @ffx, @xffx, @fff);
for 1..6 -> $n { @ff.push:   (($n == 2) ff   ($n == 4)).gist }
for 1..6 -> $n { @xff.push:  (($n == 2) ^ff  ($n == 4)).gist }
for 1..6 -> $n { @ffx.push:  (($n == 2) ff^  ($n == 4)).gist }
for 1..6 -> $n { @xffx.push: (($n == 2) ^ff^ ($n == 4)).gist }
for 1..6 -> $n { @fff.push:  (($n == 2) fff  ($n == 2)).gist }
my @ffsame;
for 1..6 -> $n { @ffsame.push: (($n == 2) ff ($n == 2)).gist }

check(@ff.join(' '),     'Nil 1 2 3 Nil Nil',    'ff runs from the on-element through the off-element');
check(@xff.join(' '),    'Nil Nil 2 3 Nil Nil',  '^ff excludes the on-element but still counts it');
check(@ffx.join(' '),    'Nil 1 2 Nil Nil Nil',  'ff^ excludes the off-element');
check(@xffx.join(' '),   'Nil Nil 2 Nil Nil Nil','^ff^ excludes both');
check(@ffsame.join(' '), 'Nil 1 Nil Nil Nil Nil','ff tests the right side on the SAME element');
check(@fff.join(' '),    'Nil 1 2 3 4 5',        'fff waits for the next element, so this never closes');

# Nil as an operand — each of these used to be a parse-level listop call
check((Nil max 1).gist,        '1',         'Nil max');
check((Nil minmax 1).gist,     '1..1',      'Nil minmax');
check((Nil ^ 1).gist,          'one(Nil, 1)', 'Nil ^ (a one-junction)');
check((Nil X 1).gist,          '((Nil 1))', 'Nil X');
check((Nil Z 1).gist,          '((Nil 1))', 'Nil Z');
check((Nil notandthen 1).gist, '1',         'Nil notandthen');
check((Nil ?^ 1).gist,         'True',      'Nil ?^');
check((Nil ff 1).gist,         'Nil',       'Nil ff');
check(Nil.gist,                'Nil',       'and a bare Nil is still Nil');
check(Nil.defined.gist,        'False',     'still undefined');

# Nil is a set ELEMENT
check((Nil (-) 1).gist, 'Set(Nil)', 'Nil survives set difference');
check((Nil (<) 1).gist, 'False',    'and is not a subset of Set(1)');
check((Nil (<=) 1).gist, 'False',   'nor an improper one');
check(set(1, Nil).elems.gist, '2',  'a set holding Nil has two elements');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
