# `return has("a")` returned Nil when the routine declares `sub has`.
#
# Once a unit declares a routine named after a keyword (`sub has`), a call
# written tight against its paren is that call. A statement starting with it
# already read that way, but after `return` the keyword check came first:
# `return` took no operand, and `has("a")` became a separate statement whose
# value nobody saw. nige123/cli.321.do noted it as "an inner sub named `has`
# clashes". Each probe returns a value that a bare `return` (Nil) cannot fake.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

sub g1(%h) { sub has($k) { %h{$k}:exists }; return has("a") }
sub g2() { my sub has($k) { "h$k" }; return has("z") }
sub g3() { sub has($k) { "h$k" }; my $r = has("y"); $r }
sub g4() { sub has($k) { "h$k" }; (return has("x")) if True; 'not reached' }

check(g1({a => 1}), True, 'return has(…) with a sub has');
check(g2(),         'hz', '…with a my sub has');
check(g3(),         'hy', 'a plain call is still a call');
check(g4(),         'hx', 'return has(…) as an expression');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
