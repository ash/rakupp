# Regression (RAKUDO-ISSUES-PLAN R4; rakudo#4553): the residual signature of
# `.assuming` keeps each remaining parameter's default as a callable
# `.default`.
#
# Before the fix the residual parameter said `$b = 2` but its `.default` was
# the Code type object, undefined and not callable, where Rakudo hands back a
# block that answers 2. The call itself already used the default.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check(Str $what, $got, $want) {
    @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

sub f($a, $b = 2) { $a + $b }
my &g = &f.assuming(1);
check 'a positional default is defined (rakudo#4553)', &g.signature.params[0].default.defined, True;
check '…and answers its value', &g.signature.params[0].default.(), 2;
check 'the call uses it', g(), 3;
check 'an argument still wins', g(5), 6;

sub h($a, :$k = 7) { $a + $k }
my &h2 = &h.assuming(1);
check 'a named default', &h2.signature.params[0].default.(), 7;
check '…in the call', h2(), 8;

sub z($a, $b) { }
check 'no default: the Code type object', &z.assuming(1).signature.params[0].default.raku, 'Code';

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
