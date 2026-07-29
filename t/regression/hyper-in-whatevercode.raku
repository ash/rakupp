# From the Cognates port (docs/rakupp-findings finding 6): `»` inside a
# WhateverCode dispatched to the CONTAINER instead of hyper-ing over its elements —
# `@groups.map(*.forms».lang)` died with "No such method 'lang' for invocant of
# type 'Array'" while the block form `.map({ .forms».lang })` worked. The curried
# closure was built with a plain method call and simply dropped the hyper flag.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

class W { has $.lang }
class F { has @.forms }
my @groups = F.new(forms => [W.new(lang=>'a'), W.new(lang=>'b')]),
             F.new(forms => [W.new(lang=>'c')]);

check(@groups.map(*.forms>>.lang).map(*.List).List, (('a','b'), ('c',)),
      'a hyper inside a WhateverCode reaches the elements');
check(@groups.map({ .forms>>.lang }).map(*.List).List, (('a','b'), ('c',)),
      'and agrees with the block form');

# The plain paths are unchanged.
my @n = [1, 2], [3, 4, 5];
check(@n>>.elems.List, (2, 3), 'a direct hyper over an array');
check(@n.map(*>>.Str).map(*.List).List, (('1','2'), ('3','4','5')),
      'a hyper on the Whatever itself');
my %h = a => 1, b => 2;
check(%h>>.succ.sort.List, ('a' => 2, 'b' => 3), 'a hyper over a hash keeps its keys');

# `.map(*.attr)` with NO hyper still returns the attribute itself.
check(@groups.map(*.forms).map(*.elems).List, (2, 1), 'a non-hyper curry is untouched');

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
