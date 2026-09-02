# Regression: a Whatever that arrives as a VALUE does not Whatever-curry.
# Rakudo decides currying SYNTACTICALLY — only a literal `*` written in the
# expression composes into a WhateverCode — so the `*` a variable holds, the
# topic of a `when`, the argument a `where` clause checks or an element a
# matcher tests is an ordinary object: `Pair.ACCEPTS(*)` is False. rakupp
# curried on the VALUE inside applyArith, so `given * { when Pair {…} }` took
# the Pair arm (a curried WhateverCode is truthy), `sub f($x where Int)`
# accepted `*`, and `(*, 1).grep(Str)` kept the star. Mathematica::Serializer's
# encoder dispatches a bare `*` through exactly such a `when` chain.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

my $w = *;
check(($w ~~ Pair).gist,     'False', 'a stored Whatever is not a Pair');
check(($w ~~ Whatever).gist, 'True',  'but it is a Whatever');
check((do given $w { when Str { 'STR' }; when Whatever { 'WHAT' }; default { 'DEF' } }),
      'WHAT', 'given a stored Whatever: the Str arm is passed over');
class E { method m($x) { given $x { when Pair { 'PAIR' }; when Whatever { 'WHAT' }; default { 'DEF' } } } }
check(E.new.m(*),        'WHAT', 'a bare * passed as an argument, given inside a method');
check(E.new.m(Whatever), 'WHAT', 'the Whatever type object too');
check((do given $w { when any(Pair, Int) { 'J' }; default { 'DEF' } }),
      'DEF', 'a junction of types threads over the value instead of currying');
check((*, 1, "x").grep(Str).elems, '1', 'a matcher tests a * element as a value');
multi f($x where Int) { 'INT' }; multi f($x) { 'ANY' }
check(f(*), 'ANY', 'a where-constrained candidate declines *');
sub g($x where Pair) { 'P' }
check((try g(*)) // 'rejected', 'rejected', 'a sole where-constrained sub rejects *');
sub h($x where Whatever) { 'W' }
check(h(*), 'W', 'and one constrained to Whatever takes it');

# the things that DO curry, or already matched, are unchanged
# (held in variables: a written `*` composes through a trailing `.^name` too)
my $lc = * ~~ Pair;  check($lc.^name, 'WhateverCode', 'a written * on the left of ~~ still curries');
my $rc = 5 ~~ *;     check($rc.^name, 'WhateverCode', 'and on the right');
check((1..5).grep(* ~~ Int).elems,        '5', '.grep(* ~~ Int) still composes');
check((1, "a", 2).grep(* !~~ Str).elems,  '2', 'so does * !~~ Str');
my $c = * > 3;
check((5 ~~ $c).gist, 'True',  'a stored WhateverCode matcher is called');
check((2 ~~ $c).gist, 'False', 'and answers False when it fails');
check(($c ~~ Code).gist, 'True', 'a stored WhateverCode is a Code');
check((do given * { when * { 'STAR' }; default { 'DEF' } }), 'STAR', 'when * matches a * topic');
check((do given * { when Any { 'ANY' }; default { 'DEF' } }), 'ANY', 'when Any matches a * topic');
check((do given (* + 1) { when Code { 'CODE' }; default { 'DEF' } }), 'CODE', 'a WhateverCode topic is a Code');
check(("a", *, "b").first(Whatever).raku, '*', '.first(Whatever) finds the star');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
