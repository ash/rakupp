# Regression: a compound assignment to a constrained `$` variable is checked as
# `=` is. `my Int $e = 5; $e /= 1` holds a Rat, so it dies with
# X::TypeCheck::Assignment and the variable keeps 5 (Rakudo, oracle-checked).
# Every compound form went unchecked before: `/=` `*=` `+=` `-=` `**=` `~=`
# `//=` `||=` `max=` all quietly retyped the variable. A coercion type converts
# instead (`my Int() $c = 1; $c /= 2` holds 0), and a `where` is asked too.
# Contract: exit 0 + last line PASS.
my @fail;

sub attempt(&op) { try { op(); CATCH { default { return $_ } } }; Nil }

{ my Int $e = 5; my $x = attempt { $e /= 1 };   @fail.push('div') unless $x ~~ X::TypeCheck::Assignment && $e === 5 }
{ my Int $e = 5; my $x = attempt { $e *= 0.5 }; @fail.push('mul') unless $x ~~ X::TypeCheck::Assignment && $e === 5 }
{ my Int $e = 5; my $x = attempt { $e += 0.5 }; @fail.push('add') unless $x ~~ X::TypeCheck::Assignment && $e === 5 }
{ my Int $e = 5; my $x = attempt { $e -= 1e0 }; @fail.push('sub') unless $x ~~ X::TypeCheck::Assignment && $e === 5 }
{ my Int $e = 5; my $x = attempt { $e **= -1 }; @fail.push('pow') unless $x ~~ X::TypeCheck::Assignment && $e === 5 }
{ my Int $e = 5; my $x = attempt { $e ~= 1 };   @fail.push('cat') unless $x ~~ X::TypeCheck::Assignment && $e === 5 }
{ my Int $e;     my $x = attempt { $e //= 'x' }; @fail.push('dor') unless $x ~~ X::TypeCheck::Assignment && $e === Int }
{ my Int $e = 0; my $x = attempt { $e ||= 'x' }; @fail.push('or')  unless $x ~~ X::TypeCheck::Assignment && $e === 0 }
{ my Int $e = 5; my $x = attempt { $e max= 7.5 }; @fail.push('max') unless $x ~~ X::TypeCheck::Assignment && $e === 5 }
{ my Rat $r = 0.5; my $x = attempt { $r *= 1e0 }; @fail.push('rat') unless $x ~~ X::TypeCheck::Assignment && $r == 0.5 }

# what conforms still works, and keeps its value
{ my Int $h = 6; $h div= 4; $h += 1; $h *= 3; @fail.push("int-ok $h") unless $h === 6 }
{ my Int $u; $u += 3;           @fail.push("undef-start $u") unless $u === 3 }
{ my Real $r = 5; $r /= 2;      @fail.push("real $r") unless $r == 2.5 }
{ my Num $n = 1e0; $n += 1;     @fail.push("num $n") unless $n === 2e0 }
{ my Str $s; $s ~= 'a'; $s ~= 'b'; @fail.push("str $s") unless $s eq 'ab' }
{ my $plain = 5; $plain /= 2;   @fail.push("untyped $plain") unless $plain == 2.5 }

# a coercion type converts; a `where` is asked
{ my Int() $c = 1; $c /= 2;     @fail.push("coerce {$c.raku}") unless $c === 0 }
{ my $w where * < 10 = 1; my $x = attempt { $w += 20 }; @fail.push("where $w") unless $x ~~ X::TypeCheck::Assignment && $w == 1 }

say @fail ?? "FAIL: @fail[]" !! 'PASS';
exit @fail ?? 1 !! 0;
