# Regression: a `return` written as an operand keeps its argument when the
# argument's TEXT is a keyword. `f($i) or return "no"` returned Nil and made
# "no" a statement of its own: the test that ends a bare `return` before a
# keyword (`… or return if $c`) read a string literal's contents as a word,
# so "no", "or", "and", "xor", "if", "my" and `q/or/` all went, and so did
# `not 1`, a prefix like any other. The statement form was right.
# Found while fixing #195. Every answer below is Rakudo 2026.09's.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($what, $got, $want) { @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want }

sub t1 { 0 or return "no"; 'fell' }
check 'or return "no"', t1(), 'no';
sub t2 { 0 or return "or"; 'fell' }
check 'or return "or"', t2(), 'or';
sub t3 { 1 and return "and"; 'fell' }
check 'and return "and"', t3(), 'and';
sub t4 { 0 xor return "xor"; 'fell' }
check 'xor return "xor"', t4(), 'xor';
sub t5 { Nil // return "my"; 'fell' }
check '// return "my"', t5(), 'my';
sub t6 { 0 or return "if"; 'fell' }
check 'or return "if"', t6(), 'if';
sub t7 { 0 or return <no>; 'fell' }
check 'or return <no>', t7(), 'no';
sub t8 { 0 or return q/or/; 'fell' }
check 'or return q/or/', t8(), 'or';
sub t9 { 0 or return "no", 1; 'fell' }
check 'or return "no", 1', t9(), ('no', 1);
sub t10 { 0 or return not 1; 'fell' }
check 'or return not 1', t10(), False;
sub t11($i) { $i or return 'no'; 'yes' }
check 'a parameter or return', (^2).map(&t11).list, ('no', 'yes');

# a bare `return` before a statement modifier stays bare
sub b1 { 0 or return if True; 'fell' }
check 'or return if', b1(), Nil;
sub b2 { 0 or return unless False; 'fell' }
check 'or return unless', b2(), Nil;
sub b3 { 0 or return; 'fell' }
check 'or return;', b3(), Nil;

if @fail { .say for @fail; say 'FAIL' } else { say 'PASS' }
