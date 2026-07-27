# Regression: two P0s from the semantic-duplication audit, both cases of ONE RULE
# written in more than one place and the copies drifting.
#
#   * `-` and `'` continue an identifier ONLY when a LETTER or `_` follows, never
#     a digit — so `$x-1` is `$x - 1`. Six sites implemented that rule three ways:
#     the lexer's canonical form (letter/underscore), two rewrites that dropped
#     `_`, and three that used isalnum and so ACCEPTED A DIGIT. Two of those were
#     the string-interpolation scanners, which glued `-1` into the name and handed
#     `"$x-1"` to the expression parser — which re-split it correctly and
#     interpolated the ARITHMETIC RESULT. `my $x = 5; say "$x-1"` printed 4.
#     One inline predicate in Lexer.h now answers it for all six.
#
#   * a routine body is a ReturnEx boundary. The interpreter states that at
#     callCallableRaw; the --exe backend compiled `return` to a C++ return and had
#     no catch at all, so a ReturnEx thrown from anywhere ELSE — a builtin like
#     `fail`, or an interpreter-evaluated callback — escaped main() and killed the
#     binary with "terminating due to uncaught exception of type rakupp::ReturnEx".
#     Fixing that exposed a second copy of the same kind: codegen inlined
#     "undefined" as `t==Nil||t==Any||t==Type` under a comment claiming Failure was
#     covered. A Failure is a Hash tagged "Failure", so it was not, and `//` did
#     not see through it. Both now call the one exported rtIsDefined.
#
# The native half of this is checked by t/run.raku's --exe parity fixture; what is
# pinned here is the interpreted behaviour the native build must match.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# a digit never continues a name
my $x = 5;
check("$x-1",   '5-1',   'a digit after the hyphen is not part of the name');
check("$x-1-2", '5-1-2', 'twice over');
check("$x+1",   '5+1',   'and a plain operator never was');

# a letter or underscore still does
my $foo-bar = 9;
check("$foo-bar", '9', 'a letter continues it');
my $a_b = 7;
check("$a_b", '7', 'an underscore is an ordinary identifier character');
sub kebab-case() { 'kc' }
check("&kebab-case()", 'kc', 'and a hyphenated sub name still interpolates');
my @a = 1, 2, 3;
check("@a[0]-1", '1-1', 'a subscript ends the name before the hyphen is examined');

# the same rule outside strings
my $elems = 4;
check($elems-1, '3', 'in expression position the hyphen is subtraction');
my $n-x = 'declared';   # a LETTER after the hyphen really is one name
check($n-x, 'declared', 'and a name that really does contain a hyphen still binds');

# `fail` yields a Failure, and `//` sees through it
sub rgfail() { fail 'boom' }
my $r = rgfail();
check($r // 'dflt', 'dflt', 'defined-or sees an undefined Failure');
check($r.defined.gist, 'False', 'which is what .defined says too');
my $caught = try { rgfail() // 'caught' };
check($caught, 'caught', 'inside a try as well');

# `return` from inside a loop, and a conditional return, still work
sub rgret($n) { return $n * 2 if $n > 3; $n }
check(rgret(5), '10', 'a conditional return');
check(rgret(1), '1',  'and falling off the end');
sub rgloop() { for 1..10 { return $_ if $_ > 3 }; -1 }
check(rgloop(), '4', 'a return out of a loop');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
