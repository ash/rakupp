# Regression: two things Cro::RPC::JSON does with methods:
#
#   * `meth does Cro::RPC::JSON::Method; meth.set-json-rpc-name($name)` — a
#     role mixed into a ROUTINE whose own method writes its `$!attr`. The
#     write went nowhere (and a `$!attr` read saw nothing), so the method's
#     JSON-RPC name read back undefined;
#   * `$obj.&m(…)` with `&m = Kid.^find_method('foo')`, where Kid declares a
#     multi of its own over a parent's (and a role's): a dispatcher called
#     directly had only Kid's candidates, and the parent's were never tried —
#     a method call reaches them by deferring up the tree, a direct call had
#     nothing to defer to.
#
# Expectations checked against Rakudo 2026.09
# (~/.rakubrew/versions/moar-2026.09/install/bin/rakudo). Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

role Named { has $.n; method set-n($x) { $!n = $x }; method twice { $!n x 2 } }
multi trait_mod:<is>(Method:D \m, :$named!) { m does Named; m.set-n($named) }
class K { method foo is named<kfoo> { } }
ck K.^find_method('foo').n, 'kfoo', "a mixin's method writes its attribute on the routine";
ck K.^find_method('foo').twice, 'kfookfoo', '…and reads it back as $!n';
role R { method bar is named<rbar> { } }
class KR does R { }
ck KR.^find_method('bar').n, 'rbar', '…through a role composed into a class';
role P[::T] { method baz is named<pbaz> { } }
class KP does P[Int] { }
ck KP.^find_method('baz').n, 'pbaz', '…and a parameterized one';
my &f = method () { };
&f does Named;
&f.set-n('cl');
ck &f.clone.n, 'cl', '…and a clone keeps it';

role Bar { proto method foo (|) { * }; multi method foo () { "role ()" } }
class Baz does Bar { multi method foo(Str $s) { "Baz Str" }; multi method foo(Num $n) { "Baz Num" } }
class Mid is Baz { }
class Kid is Mid { multi method foo(Int $i, Str $s) { "Kid Int Str" } }
my &m = Kid.^find_method('foo');
my $k = Kid.new;
ck ($k.&m(), $k.&m(π), $k.&m("x"), $k.&m(1, "x")), ("role ()", "Baz Num", "Baz Str", "Kid Int Str"),
   "a dispatcher called directly reaches the parents' candidates";
ck ($k.foo(), $k.foo(1, "x")), ("role ()", "Kid Int Str"), '…as a method call does';
ck (try { $k.&m(1); 'bound' }) // $!.^name, 'X::Multi::NoMatch', '…and still refuses what none takes';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
