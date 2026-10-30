# Regression: Cro::RPC::JSON tells its `is json-rpc(…)` forms apart with
#
#   multi trait_mod:<is>(Method:D \m, :$json-rpc! (Str:D $name, *%params))
#   multi trait_mod:<is>(Method:D \m, Hash:D(List:D(Pair:D)) :$json-rpc!)
#
# and `is json-rpc(:async)` died "expected Str but got Pair (:async)":
#
#   * a named parameter's sub-signature was checked against a list or a hash
#     but not a Pair, so the first candidate won and then failed its bind. A
#     Pair binds as its Capture, one named and no positional;
#   * `Hash:D(List:D(Pair:D))` read as `Hash:D` with a sub-signature: a
#     nested coercion was only seen when `(` followed the inner type's name,
#     and here a smiley sits between them.
#
# Expectations checked against Rakudo 2026.09
# (~/.rakubrew/versions/moar-2026.09/install/bin/rakudo). Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

multi f(:$x! (Str:D $name, *%params)) { "sub-sig $name " ~ %params.keys.sort.join(",") }
multi f(Hash:D(List:D(Pair:D)) :$x!) { "hash " ~ $x.keys.sort.join(",") }
ck f(x => ("bar", :a)), 'sub-sig bar a', 'a list binds the named sub-signature';
ck f(x => (:async)), 'hash async', "a Pair does not: it is a named, and there is no \$name";
ck f(x => (:async, :auth<x>)), 'hash async,auth', '…and a list of Pairs coerces to the Hash';

multi g(Method:D \m, :$x! (Str:D $name, *%params)) { "sub-sig" }
multi g(Method:D \m, Hash:D(List:D(Pair:D)) :$x!) { "hash" }
ck g(method { }, :x(:async)), 'hash', "…with a positional before it, as a trait's";

sub b(Hash:D(List:D(Pair:D)) $x) { $x }
ck b((:async)), ${:async}, 'a nested coercion with smileys converts a positional';
ck b((:a, :b(2))), ${:a, :b(2)}, '…from a List as well';
sub d(List:D(Pair:D) $x) { $x }
ck d((:a)), $(:a,), 'the inner coercion alone';
sub s(Str(Rat(Int)) $x) { $x }
ck s(3), '3', 'a nested coercion with no smiley, as before';

# …and dispatch asks a nested coercion's whole source chain: a bare
# `is json-rpc-actor` (True) is the Bool:D candidate's, not the Hash one's
multi t(Mu:U \t, Hash:D(List:D(Pair:D)) :$a!) { "hash" }
multi t(Mu:U \t, Bool:D :$a!) { "bool" }
ck (t(Int, :a), t(Int, :a(:x<1>)), t(Int, :a((:x<1>, :y<2>)))), ('bool', 'hash', 'hash'),
   "a named nested coercion takes only what its chain can convert";
multi p(Str(Rat(Int)) $x) { "coerce" }
multi p(Bool $x) { "bool" }
ck (p(3), p(1.5), p(True)), ('coerce', 'coerce', 'bool'), '…and a positional one';

my $err = try { b(42); Nil } // $!;
ck $err.^name, 'X::TypeCheck::Binding::Parameter', 'a value no inner type takes is still refused';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
