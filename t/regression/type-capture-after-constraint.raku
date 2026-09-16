# Regression: `Int ::T $x` — a type capture that also carries a constraint.
#
# A bare `::T $x` captures whatever type arrives. Written with a constraint in
# front — `Response ::RESPONSE = Net::HTTP::Response` — the parameter is BOTH
# constrained and captured, and this engine could not parse it at all: the
# capture branch only fired when `::` opened the parameter, so Net::HTTP's GET
# and POST died at "expected ) (got '::')" and took every distribution that
# loads them with them.
#
# typeCapture stays FALSE for this spelling on purpose. That flag means "this
# parameter has no real type", and this one does — Rakudo refuses `f("s")` for
# `f(Int ::T $x)`. The capture is recorded separately and bound on all three
# paths: an argument, a default, and a named parameter.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

# the constrained capture names what actually arrived
sub pos-arg(Int ::T $x) { T.^name }
check pos-arg(5), 'Int', 'a constrained capture binds the argument\'s type';

# …and the constraint is still enforced. Rakudo refuses the call at COMPILE
# time, so the check has to go through EVAL to be catchable on both engines.
check (try EVAL 'pos-arg("s")').defined, False, 'and the constraint still refuses a Str';

# a DEFAULT names the default's type
class Fallback {}
sub with-default(Any ::R = Fallback) { R.^name }
check with-default(),    'Fallback', 'no argument: the capture names the default';
check with-default(Int), 'Int',      'an argument: the capture names the argument';

# a NAMED parameter captures too
sub named(Str ::S :$n!) { S.^name }
check named(n => 'x'), 'Str', 'a named parameter captures its type';

# the bare form it sits beside must keep working
sub bare(::T $x) { T.^name }
check bare(5),     'Int', 'a bare capture still binds';
check bare('x'),   'Str', 'whatever it is given';

# …and the captured name is usable as a type for the rest of the signature
sub pair-up(Int ::T $a, T $b) { "{T.^name}:{$a + $b}" }
check pair-up(2, 3), 'Int:5', 'the captured name types a later parameter';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
