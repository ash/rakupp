# Regression: `sprintf('%s', $e)` of an exception that writes only `message`
# prints the message, as `~$e`, `"$e"` and `$e.Str` always did. The `%s`
# directive asks an object for its Str only when its class writes `Str`
# itself, and Exception's built-in Str (which is `.message`) was not one of
# those, so it printed `X::FieldNotFound<4365607088>`. Template::Mustache logs
# every warning that way: t/06-logging.
# Contract: exit 0 + last line PASS.
my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want }

class E is Exception { method message { 'E-msg' } }
role R[Str:D $err] is Exception { has $.str; method message { "$err ❮$!str❯" } }
class F does R['Field not found'] { }
class P { method Str { 'P-str' } }

ck sprintf('%s', E.new),                 'E-msg',                 'a message method';
ck sprintf('%s: %s', 'WARN', F.new(:str<x>)), 'WARN: Field not found ❮x❯', 'a message from a parameterised role';
ck sprintf('%5s|', E.new),               'E-msg|',                'width still applies';
ck sprintf('%.1s', E.new),               'E',                     'precision too';
ck sprintf('%s', P.new),                 'P-str',                 'a Str of its own still answers';

.say for @fail;
say @fail ?? 'FAIL' !! 'PASS';
