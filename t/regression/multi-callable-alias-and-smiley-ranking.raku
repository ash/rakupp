# Regression: three ways multi dispatch ranked candidates unlike Rakudo, all
# met in Cro::RPC::JSON:
#
#   * `:ws(:web-socket($websocket))` takes `:ws` and `:web-socket`, not
#     `:websocket` — the variable is a key only when written `:$websocket`.
#     Counted as one, its `json-rpc(Any:D $obj, …)` candidate won a call with
#     `:$websocket` and then failed to bind it ("Unexpected named argument");
#   * an `&block` parameter scored nothing for the Callable its sigil implies,
#     so `(Any:D $obj)` — two points for the smiley — took every block;
#   * a smiley counted against a NARROWER TYPE at another position: Rakudo
#     weighs definedness only once every type has tied, so `($j, @x)` is
#     narrower than `(Any:D $j, Any $x)` for a Positional type object, and an
#     untyped `$j` ties `Any $j`. Cro::RPC::JSON's unmarshaller went to the
#     Any candidate and called `.new` on `Positional[IntraFoo]`.
#
# Expectations checked against Rakudo 2026.09
# (~/.rakubrew/versions/moar-2026.09/install/bin/rakudo). Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

proto g(|) {*}
multi g(&b, Bool :ws(:web-socket(:$websocket)) where not *) { "plain" }
multi g(&b, Bool :ws(:web-socket(:$websocket)) where so *, :&async) { "ws" }
multi g(Any:D $obj, Bool :ws(:web-socket($websocket))) { "obj" }
my sub h($a, $b?) { }
ck g(&h, :websocket), 'ws', 'a key only the &block candidates declare reaches them';
ck g(&h, :!websocket), 'plain', '…either way';
ck g(-> $r { }, :ws), 'ws', 'a block goes to &block, not Any:D';
ck g(-> $r { }), 'plain', '…with no named as well';
ck g(42, :web-socket), 'obj', 'a value goes to Any:D by either of its keys';
ck (try { g(42, :websocket); 'bound' }) // $!.^name, 'X::Multi::NoMatch', '…and not by its variable';
sub k(Bool :ws(:web-socket($websocket))) { $websocket }
ck (try { k(:websocket); 'bound' }) // 'refused', 'refused', 'a lone sub refuses it too';
sub kk(Bool :ws(:web-socket(:$websocket))) { $websocket }
ck (kk(:ws), kk(:web-socket), kk(:websocket)), (True, True, True), '`:$var` innermost answers all three';

multi i(&b, :$x) { "code" }
multi i(Any:D $obj, :$x) { "obj" }
ck i(-> $r { }, :x), 'code', 'an implied Callable outranks Any:D';
multi c(Code $c) { "Code" }
multi c(&b) { "&" }
ck c(-> { }), 'Code', 'a written Callable type still comes first';

multi v(Any:D $j, Any $x) { "any" }
multi v($j, @x) { "array" }
ck v([1], Positional[Int]), 'array', 'a Positional type object goes to @x past an Any:D';
ck v([1], Int), 'any', '…and a plain type object to Any';
multi u(Any $x) { "Any" }
multi u(Mu $x) { "Mu" }
multi u2(Mu $x) { "Mu" }
multi u2($x) { "untyped" }
ck (u(1), u2(1)), ('Any', 'untyped'), 'an untyped $ is an Any, narrower than Mu';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
