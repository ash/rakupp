# BROKE (during the arity-enforcement leg, caught by gates ar1/ar2):
# enforcing "Calling foo(...) will never work with declared signature (...)"
# naively rejected four roast-blessed lax patterns:
#   1. internal C++ callers (HOF callbacks, subst replacements) call user
#      subs with adjusted arg lists — enforcement is DIRECT syntactic calls
#      only (subst.t 191 -> 140, categorize.t 18 -> 2 on gate ar1);
#   2. `sub f { @_ }` and sigless `sub s { ... }` slurp implicitly
#      (slurpy-params.t 75 -> 11);
#   3. quoted-key pairs ("a" => 1) bind positionally and capture-flattened
#      named args LOSE the namedArg bit — pairs never count strictly;
#   4. foo(|$capture) with sub foo(@arr) binds a whole flattened capture
#      (capture.t), and TIGHT foo[10] indexes the call RESULT, it is not a
#      listop argument (advent2009-day23.t died mid-file on gate ar2).
# FIXED: same leg (ar3 gate clean).
sub strict($a, $b) { $a + $b }
die 'too-few must throw'  unless (try strict(1)) === Nil;
die 'too-many must throw' unless (try strict(1, 2, 3)) === Nil;
die 'exact still works'   unless strict(1, 2) == 3;
sub implicit { @_.elems }
die 'implicit @_ slurpy broken' unless implicit(1, 2, 3) == 3;
sub sigless { 'ok' }
die 'sigless lax call broken' unless sigless() eq 'ok';
sub onepos($a) { $a.raku }
die 'quoted-key pair is positional' unless onepos("k" => 1).contains('k');
sub arrparam(@arr) { ~@arr }
my $c = \("a", "b", "c");
die 'capture into @-param must not throw arity' unless arrparam(|$c).defined;
sub lister() { 10, 20, 30 }
die 'tight [i] indexes the call result' unless lister[1] == 20;
say 'PASS';
