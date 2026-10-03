# Regression: whether an argument binds a destructuring sub-signature is
# decided by binding it — its `.Capture` against the inner parameters, with
# their types, required nameds, slurpies and nested sub-signatures. Signature
# .ACCEPTS and multi dispatch only checked the arity (counting named pairs as
# positionals) or, for named-only sub-signatures, nothing at all. Cro asks
# `$handler.signature.ACCEPTS(\(body))` before calling a `request-body` block:
# a form without a required field went on to the call and died (500) instead
# of being refused (400). And a candidate with a sub-signature is narrower than
# the same type without one, as in Rakudo.
# Contract: exit 0 + last line PASS.
my @fail;
sub ck($got, $want, $desc) { @fail.push("$desc: {$got.raku}") unless $got eqv $want }

class F { has %.h; method Capture() { Capture.new(hash => %!h) } }
my &need = -> (:$name!, :$colour = 'none', *%rest) { $name };
ck &need.signature.ACCEPTS(\(F.new(h => { colour => 'blue' }))), False, 'required named missing';
ck &need.signature.ACCEPTS(\(F.new(h => { name => 'x' }))),      True,  'required named present';

my &typed = -> (Int $a) { $a };
ck &typed.signature.ACCEPTS(\(['s'])), False, 'inner type refused';
ck &typed.signature.ACCEPTS(\([5])),   True,  'inner type accepted';

my &mixed = -> ($a, :$k!) { $a };
ck &mixed.signature.ACCEPTS(\(\(1, :k(2)))), True,  'capture with its named';
ck &mixed.signature.ACCEPTS(\(\(1))),        False, 'capture without its named';

sub outer(Int $n, (Str $s, *@)) { }
ck &outer.signature.ACCEPTS(\(1, ['a', 2])), True,  'nested: first is a Str';
ck &outer.signature.ACCEPTS(\(1, [3, 2])),   False, 'nested: first is not';

# dispatch
multi g((Int $a, Int $b)) { 'ints' }
multi g(($a, $b))         { 'any' }
ck (g((1, 2)), g(('a', 'b')), g((1, 'b'))), ('ints', 'any', 'any'), 'typed destructure dispatch';

class P { has $.x; has $.y; method Capture() { \(:$!x, :$!y) } }
multi m(P $ (:$x!, :$y!)) { "both $x $y" }
multi m(P $)              { 'plain' }
ck m(P.new(x => 1, y => 2)), 'both 1 2', 'sub-signature outranks the bare type';

class Q { has $.x }
multi q(Q $ (:$x!)) { "sub $x" }
multi q(Q $)        { 'plain' }
ck q(Q.new(x => 5)), 'sub 5', 'attribute destructure outranks the bare type';

multi w(Q $ (:$x!)) { 'Q' }
multi w($ (:$x!))   { 'any' }
class R { has $.x }
ck (w(Q.new(x => 1)), w(R.new(x => 1))), ('Q', 'any'), 'the declared type is checked';

multi f([$a, $b]) { 'two' }
multi f([$a])     { 'one' }
multi f(@a)       { 'any' }
ck (f([1, 2]), f([1]), f([1, 2, 3])), ('two', 'one', 'any'), 'arity still decides';

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
