# Regression: an untyped routine parameter accepted `Mu`.
#
# In Raku an untyped parameter is implicitly Any-constrained, and `Any` is every
# type BELOW Mu — so `sub f($x) { }; f(Mu)` is a type-check failure. Raku++ bound
# it happily, because typeCheckBind returned early for EVERY type object and was
# never called at all for an untyped parameter.
#
# The distinction that makes this subtle, and which this file exists to pin: a
# BLOCK's untyped parameter is Mu-constrained, not Any-constrained. So the pointy
# and bare-block cases below must keep working — and `for (Mu) -> $x` and
# `.map(-> $x {…})` depend on it, which is how a too-eager fix announces itself.
#
# Found porting JSON::Native to an explicit signature: its tests passed on Raku++
# and failed on Rakudo, because Raku++ never applied the constraint.
#
# Contract: exit 0 + last line PASS.
my @fail;

sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}
sub dies(&c, $desc)  { check ((try { c(); 'lived' }) // 'died'), 'died',  $desc }
sub lives(&c, $desc) { check ((try { c(); 'lived' }) // 'died'), 'lived', $desc }

# --- routines: an untyped parameter is Any-constrained, so Mu is refused ------
dies { sub f($x) { }; f(Mu) },                    'a named sub refuses Mu';
dies { (sub ($x) { })(Mu) },                      'an anonymous sub refuses Mu';
dies { class C { method mth($x) { } }; C.mth(Mu) }, 'a method refuses Mu';
dies { sub f(:$y) { }; f(:y(Mu)) },               'a named parameter refuses Mu';
dies { sub f($x = 5) { }; f(Mu) },                'an optional parameter refuses Mu';

# An explicitly written Any refuses Mu wherever it appears — blocks included.
dies { sub f(Any $x) { }; f(Mu) },                'an explicit Any parameter refuses Mu';
dies { (-> Any $x { })(Mu) },                     'an explicit Any refuses Mu in a block too';

# --- blocks: an untyped parameter is Mu-constrained, so Mu binds --------------
lives { (-> $x { })(Mu) },                        'a pointy block accepts Mu';
lives { ({ $^a })(Mu) },                          'a bare block accepts Mu';
lives { my $b = -> $x { }; $b(Mu) },              'a stored pointy block accepts Mu';
lives { for (Mu) -> $x { } },                     'for -> $x accepts Mu';
lives { (Mu,).map(-> $x { }).eager },             'map -> $x accepts Mu';

# --- and everything that was already legal stays legal ------------------------
lives { sub f(Mu $x) { }; f(Mu) },                'an explicit Mu parameter accepts Mu';
lives { sub f($x) { }; f(Any) },                  'an untyped parameter still accepts Any';
lives { sub f($x) { }; f(42) },                   'an untyped parameter still accepts a value';
lives { sub f($x) { }; my $u; f($u) },            'an untyped parameter accepts an empty slot';
lives { sub f(*@a) { }; f(Mu) },                  'a slurpy accepts Mu';
lives { sub f(|c) { }; f(Mu) },                   'a capture accepts Mu';
lives { sub f(Int $x) { }; f(Int) },              'a type object still binds its own type';

# Values still reach the body intact — the check must not have eaten anything.
check (sub ($x) { $x + 1 })(41), 42, 'an ordinary argument still binds and is usable';
check (-> $x { $x.^name })(Mu), 'Mu', 'a block receives Mu itself, not a substitute';

# Multi-dispatch already told Mu and Any apart, and must continue to.
multi pick-t(Mu  $x) { 'Mu'  }
multi pick-t(Any $x) { 'Any' }
check pick-t(Mu),  'Mu',  'a Mu candidate wins for Mu';
check pick-t(42),  'Any', 'the Any candidate wins for a value';
check pick-t(Any), 'Any', 'the Any candidate wins for Any';

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
