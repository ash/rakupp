# Regression: `.first`, `.head` and `.tail` of an Array handed back a COPY
# wherever the call was not itself the assignment target.
#
# `@a.first = 9` wrote @a already; `my \x = @a.first; x = 9` died ("Cannot
# modify an immutable Int"), `my $x := @a.tail` bound a copy, and
# `for @a.head(2) { $_ = 7 }` and `@a.grep(* > 1).first` wrote nothing. Rakudo
# hands back the elements' own containers. The positions they name
# (endsArrayView) are walked by `for` and bound by a sigilless or `:=`
# declaration as `@a[$i]` is; the parser keeps such a name assignable
# (endsElementCall). `.first(:k)` and a List's elements stay values.

my $fails = 0;
sub ok($cond, $what) { $fails++ unless $cond; say "not ok - $what" unless $cond }

{ my @a = 1, 2, 3; my \x = @a.first; x = 9; ok @a eqv [9, 2, 3], 'my \x = @a.first' }
{ my @a = 1, 2, 3; my \x = @a.head;  x = 9; ok @a eqv [9, 2, 3], 'my \x = @a.head' }
{ my @a = 1, 2, 3; my \x = @a.tail;  x = 9; ok @a eqv [1, 2, 9], 'my \x = @a.tail' }
{ my @a = 1, 2, 3; my $x := @a.first(* > 1); $x = 9; ok @a eqv [1, 9, 3], '$x := @a.first(matcher)' }
{ my @a = 1, 2, 3; my \x = @a.first(:end); x = 7; ok @a eqv [1, 2, 7], '.first(:end)' }
{ my @a = 1, 2, 3; my \x = @a.grep(* > 1).tail; x = 5; ok @a eqv [1, 2, 5], 'over a grep' }
{ my @a = 1, 2, 3; for @a.head(2) { $_ = 7 }; ok @a eqv [7, 7, 3], 'for @a.head(2)' }
{ my @a = 1, 2, 3; for @a.tail(2) { $_ = 7 }; ok @a eqv [1, 7, 7], 'for @a.tail(2)' }
{ my @a = 1, 2, 3; for @a.first(* > 1) { $_ = 0 }; ok @a eqv [1, 0, 3], 'for @a.first(…)' }
{ my @a = 1, 2, 3; for @a.head { $_ = 0 }; ok @a eqv [0, 2, 3], 'for @a.head' }
{ my @a = 1, 2, 3; for @a.grep(* > 1).head { $_ = 5 }; ok @a eqv [1, 5, 3], 'for over grep, then head' }
{ my @a = 1, 2, 3; @a.head(2)»++; ok @a eqv [2, 3, 3], '»++ over head(2)' }
# …and what is still a value
{ my @a = 1, 2, 3; my \k = @a.first(:k); ok k == 0, '.first(:k) answers the index' }
{ my \x = (1, 2, 3).first; ok (try { x = 9; 1 }) === Nil, "a List's element is a value" }
{ my @a = 1, 2, 3; my $n = 0; for @a.first(* > 9) { $n++ }; ok $n == 1, 'no match iterates Nil once' }

say $fails == 0 ?? 'PASS' !! "FAIL ($fails)";
exit $fails == 0 ?? 0 !! 1;
